#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QtTest>

#include <windows.h>
#include <crtdbg.h>
#include <shlobj.h>
#include <stdlib.h>
#include <tlhelp32.h>

#include <functional>
#include <memory>
#include <optional>

#include "helpers/own_processes.h"
#include "lyrics/google/browser_agent.h"

// What the three webview test executables share: the browser arguments of the checks, waiting for a page call, the watch on
// visible windows, finding the browser processes of a test's own user-data folder, and the child process that plays a PC
// without the WebView2 runtime.
namespace xgs::webview::testing {

using lyrics::google::PageOutcome;

/// Q3 decision 14: the checks (not the product) ask WebView2 to keep its own background traffic down. What WebView2 still
/// does by itself is outside our control; our code sends requests to 127.0.0.1 only.
inline const QString CheckBrowserArguments = QStringLiteral("--disable-background-networking --disable-component-update");

/// Runs call(done) and waits for done, processing events, at most limitMs. Nothing when done did not come in time; a done
/// that comes later goes nowhere (the state is shared, not on this stack). `calls` counts the dones (a page must answer once).
struct Waited {
    std::optional<PageOutcome> outcome;
    int calls = 0;
};

inline Waited waitForPage(const std::function<void(std::function<void(PageOutcome)>)>& call, int limitMs) {
    auto state = std::make_shared<Waited>();
    call([state](PageOutcome outcome) {
        ++state->calls;
        if (!state->outcome) state->outcome = std::move(outcome);
    });
    (void)QTest::qWaitFor([&] { return state->outcome.has_value(); }, limitMs);
    return *state;
}

/// The processes that belong to this test: this process, and its descendants that are WebView2 processes or copies of this
/// executable (the child of the no-runtime checks). conhost and other helpers that Windows starts are left out.
inline QSet<DWORD> ownProcessTree() {
    QSet<DWORD> tree{GetCurrentProcessId()};
    const QString ownImage = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return tree;
    struct Entry {
        DWORD pid, parent;
        QString image;
    };
    QList<Entry> entries;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
        entries.append({entry.th32ProcessID, entry.th32ParentProcessID, QString::fromWCharArray(entry.szExeFile)});
    CloseHandle(snapshot);
    for (bool grew = true; grew;) {
        grew = false;
        for (const Entry& e : entries) {
            if (tree.contains(e.pid) || !tree.contains(e.parent)) continue;
            if (e.image.compare(u"msedgewebview2.exe", Qt::CaseInsensitive) != 0 && e.image.compare(ownImage, Qt::CaseInsensitive) != 0)
                continue;
            tree.insert(e.pid);
            grew = true;
        }
    }
    return tree;
}

/// The visible top-level windows of ownProcessTree(), described ("pid class 'title'").
inline QStringList visibleOwnWindows() {
    struct Scan {
        QSet<DWORD> tree;
        QStringList found;
    } scan{ownProcessTree(), {}};
    EnumWindows(
        [](HWND window, LPARAM data) -> BOOL {
            auto* s = reinterpret_cast<Scan*>(data);
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (!s->tree.contains(pid) || !IsWindowVisible(window)) return TRUE;
            wchar_t name[256] = {};
            wchar_t title[256] = {};
            GetClassNameW(window, name, 255);
            GetWindowTextW(window, title, 255);
            s->found.append(QStringLiteral("%1 %2 '%3'").arg(pid).arg(QString::fromWCharArray(name), QString::fromWCharArray(title)));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&scan));
    return scan.found;
}

/// Looks for visible top-level windows of ownProcessTree() every 100 ms while it lives (and once when made). A window that
/// shows and goes again between two looks is not seen.
class ScreenWatch final : public QObject {
public:
    ScreenWatch() {
        look();
        connect(&m_timer, &QTimer::timeout, this, &ScreenWatch::look);
        m_timer.start(100);
    }
    QStringList seen() const { return m_seen; }
    int looks() const { return m_looks; }

private:
    void look() {
        ++m_looks;
        for (const QString& window : visibleOwnWindows())
            if (!m_seen.contains(window)) m_seen.append(window);
    }
    QTimer m_timer;
    QStringList m_seen;
    int m_looks = 0;
};

/// The running msedgewebview2 processes whose command line holds folder (a test's own user-data folder), waited for at most
/// limitMs. Only reads; nothing here ends a process.
inline QList<testsupport::SeenProcess> browserProcessesLeft(const QString& folder, const FILETIME& since, DWORD limitMs) {
    return testsupport::waitForNoneMentioning(QDir::toNativeSeparators(folder), since, limitMs);
}

/// How long each window of a view took to be made ("1203 ms, never"), for failure messages.
inline QString creationTimes(const QList<qint64>& times) {
    QStringList out;
    for (const qint64 ms : times) out.append(ms < 0 ? QStringLiteral("never") : QString::number(ms) + QStringLiteral(" ms"));
    return out.isEmpty() ? QStringLiteral("no window made") : out.join(QStringLiteral(", "));
}

inline QString describe(const QList<testsupport::SeenProcess>& processes) {
    QStringList out;
    for (const testsupport::SeenProcess& p : processes) out.append(QString::number(p.pid) + u' ' + p.image);
    return out.join(QStringLiteral(", "));
}

/// The user's Downloads folder (only looked at).
inline QString downloadsFolder() {
    PWSTR path = nullptr;
    QString folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path)) && path) folder = QString::fromWCharArray(path);
    CoTaskMemFree(path);
    return folder;
}

/// The environment variable that turns a copy of the test executable into the no-runtime child, and its value.
inline constexpr char ChildVariable[] = "XGS_WEBVIEW_TEST_CHILD";

/// Runs this executable again as the child: WEBVIEW2_BROWSER_EXECUTABLE_FOLDER points at emptyFolder (the loader then finds no
/// runtime, Q0's method), ChildVariable is set, no console window is made. Returns its exit code and output; -1 when it did
/// not finish within limitMs (it is then killed: it is our own child).
struct ChildRun {
    int exitCode = -1;
    QString output;
};

inline ChildRun runNoRuntimeChild(const QString& emptyFolder, const QString& userDataFolder, int limitMs) {
    QProcess child;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("WEBVIEW2_BROWSER_EXECUTABLE_FOLDER"), QDir::toNativeSeparators(emptyFolder));
    environment.insert(QString::fromLatin1(ChildVariable), QDir::toNativeSeparators(userDataFolder));
    child.setProcessEnvironment(environment);
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
    child.start(QCoreApplication::applicationFilePath(), {});
    ChildRun run;
    if (!child.waitForStarted(10000)) return run;
    if (!child.waitForFinished(limitMs)) {
        child.kill();
        child.waitForFinished(5000);
        run.output = QString::fromUtf8(child.readAll());
        return run;
    }
    run.exitCode = child.exitCode();
    run.output = QString::fromUtf8(child.readAll());
    return run;
}

/// The user-data folder the child was given; nothing when this process is not the child.
inline std::optional<QString> childUserDataFolder() {
    const QString value = qEnvironmentVariable(ChildVariable);
    if (value.isEmpty()) return std::nullopt;
    return value;
}

/// The child runs before QTest::qExec, without Qt Test's fault handler: should it crash, abort or fail an assertion, it must
/// end without a box on screen (no Windows error dialog, no CRT "Debug Error" or assertion box; reports go to the debugger
/// output instead). Called first thing in the child.
inline void quietChildFailures() {
    SetErrorMode(SetErrorMode(0) | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
#endif
}

}  // namespace xgs::webview::testing

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <latch>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include <windows.h>
#include <tlhelp32.h>

#include "helpers/argument_lists.h"
#include "process/system_process_runner.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::process::LineHandler;
using xgs::process::RunResult;
using xgs::process::RunStatus;
using xgs::process::SystemProcessRunner;

namespace {

// The helper executable (qt/tests/helpers/test_child.cpp), with `\` as the engine's paths have them.
QString child() {
    QString path = QString::fromUtf8(XGS_TEST_CHILD);
    return path.replace(u'/', u'\\');
}

QString windowsPath(const QString& path) {
    QString out = path;
    return out.replace(u'/', u'\\');
}

const wchar_t* wide(const QString& text) {
    return reinterpret_cast<const wchar_t*>(text.utf16());
}

// The UTF-16 units as 4 hex digits each, "." for the empty string (what `xgs-test-child argv` writes).
QString hexUnits(const QString& text) {
    if (text.isEmpty()) return u"."_s;
    QString out;
    for (QChar c : text) out += u"%1"_s.arg(int(c.unicode()), 4, 16, QChar(u'0'));
    return out;
}

QString hexBytes(const QByteArray& bytes) {
    return bytes.isEmpty() ? u"."_s : QString::fromLatin1(bytes.toHex());
}

// Lines collected from the handlers (which may run on different threads).
struct Lines {
    std::mutex mutex;
    QStringList out;
    QStringList err;
    LineHandler onOut() {
        return [this](const QString& line) { std::lock_guard lock(mutex); out.append(line); };
    }
    LineHandler onErr() {
        return [this](const QString& line) { std::lock_guard lock(mutex); err.append(line); };
    }
};

// A run on its own thread, with its own stop source. The destructor stops the run (if it has not returned) and waits for it,
// so no process outlives a test, on failure paths too.
class BackgroundRun {
public:
    BackgroundRun(SystemProcessRunner& runner, QStringList command, LineHandler onOut, LineHandler onErr) {
        m_thread = std::thread([this, &runner, command = std::move(command), onOut = std::move(onOut), onErr = std::move(onErr)] {
            const RunResult result = runner.run(command, onOut, onErr, m_stop.get_token());
            std::lock_guard lock(m_mutex);
            m_result = result;
            m_done.notify_all();
        });
    }
    ~BackgroundRun() {
        m_stop.request_stop();
        if (m_thread.joinable()) m_thread.join();
    }
    void stop() { m_stop.request_stop(); }
    // The result once the run has returned, or nothing when it has not within the time.
    std::optional<RunResult> waitFor(std::chrono::milliseconds time) {
        std::unique_lock lock(m_mutex);
        m_done.wait_for(lock, time, [this] { return m_result.has_value(); });
        return m_result;
    }

private:
    std::stop_source m_stop;
    std::mutex m_mutex;
    std::condition_variable m_done;
    std::optional<RunResult> m_result;
    std::thread m_thread;
};

// A run that is stopped when it takes longer than the time (so that a broken runner fails the test instead of hanging it).
RunResult runWithin(SystemProcessRunner& runner, const QStringList& command, Lines& lines, std::chrono::milliseconds time = 20s) {
    BackgroundRun run(runner, command, lines.onOut(), lines.onErr());
    if (const std::optional<RunResult> result = run.waitFor(time)) return *result;
    run.stop();
    const std::optional<RunResult> result = run.waitFor(20s);
    return result ? *result : RunResult{RunStatus::StartFailed, -1, u"the run did not return"_s};
}

// A one-shot signal set from a handler.
class Signal {
public:
    void set() {
        std::lock_guard lock(m_mutex);
        m_set = true;
        m_cv.notify_all();
    }
    bool waitFor(std::chrono::milliseconds time) {
        std::unique_lock lock(m_mutex);
        return m_cv.wait_for(lock, time, [this] { return m_set; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_set = false;
};

// A process handle that terminates the process (if it still runs) and closes the handle at the end of the scope.
class KilledAtExit {
public:
    explicit KilledAtExit(HANDLE process = nullptr) : m_process(process) {}
    ~KilledAtExit() { release(); }
    KilledAtExit(const KilledAtExit&) = delete;
    KilledAtExit& operator=(const KilledAtExit&) = delete;
    HANDLE get() const { return m_process; }
    void reset(HANDLE process) {
        release();
        m_process = process;
    }

private:
    void release() {
        if (!m_process) return;
        if (WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT) {
            TerminateProcess(m_process, 99);
            WaitForSingleObject(m_process, 5000);
        }
        CloseHandle(m_process);
        m_process = nullptr;
    }

    HANDLE m_process;
};

// The process id written by `xgs-test-child spawn`, 0 when the file is not there (yet).
DWORD readPid(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return 0;
    bool ok = false;
    const DWORD pid = DWORD(QString::fromLatin1(file.readAll()).toULong(&ok));
    return ok ? pid : 0;
}

// The full path of a process's image ("" when it cannot be read).
QString imageName(HANDLE process) {
    wchar_t buffer[32768];
    DWORD size = DWORD(std::size(buffer));
    return QueryFullProcessImageNameW(process, 0, buffer, &size) ? QString::fromWCharArray(buffer, qsizetype(size)) : QString();
}

HANDLE openForWaiting(DWORD pid) {
    return OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
}

// This process and all of its descendants (by the parent process ids of a snapshot).
std::set<DWORD> processTree() {
    std::set<DWORD> tree{GetCurrentProcessId()};
    std::vector<std::pair<DWORD, DWORD>> links;   // (pid, parent)
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return tree;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
        links.emplace_back(entry.th32ProcessID, entry.th32ParentProcessID);
    CloseHandle(snapshot);
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& [pid, parent] : links)
            if (tree.contains(parent) && !tree.contains(pid)) grew = tree.insert(pid).second || grew;
    }
    return tree;
}

// The visible top-level windows that belong to this process tree.
int visibleWindowsOfThisTree() {
    struct Search {
        std::set<DWORD> tree;
        int count = 0;
    } search{processTree()};
    EnumWindows(
        [](HWND window, LPARAM data) -> BOOL {
            auto* s = reinterpret_cast<Search*>(data);
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if (IsWindowVisible(window) && s->tree.contains(pid)) ++s->count;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.count;
}

// Sets an environment variable of this process for the scope, then puts the old value back.
class ScopedVariable {
public:
    ScopedVariable(const wchar_t* name, const wchar_t* value) : m_name(name) {
        const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
        if (size > 0) {
            std::wstring old(size, L'\0');
            old.resize(GetEnvironmentVariableW(name, old.data(), size));
            m_old = old;
        }
        SetEnvironmentVariableW(name, value);
    }
    ~ScopedVariable() { SetEnvironmentVariableW(m_name, m_old ? m_old->c_str() : nullptr); }

private:
    const wchar_t* m_name;
    std::optional<std::wstring> m_old;
};

}  // namespace

class SystemProcessRunnerTest : public QObject {
    Q_OBJECT

    SystemProcessRunner runner;

private slots:
    // ---- the Kotlin tests (the child is xgs-test-child instead of java) ----

    void capturesOutputLinesAndExitCode() {
        Lines lines;
        const RunResult result = runWithin(runner, {child(), u"emit"_s, hexBytes("out-line\r\n"), hexBytes("err-line\r\n")}, lines);
        QCOMPARE(result.status, RunStatus::Exited);
        QCOMPARE(result.exitCode, 3);
        QCOMPARE(lines.out, QStringList({u"out-line"_s}));
        QCOMPARE(lines.err, QStringList({u"err-line"_s}));
    }

    void decodesOutputAsUtf8() {
        Lines lines;
        const RunResult result = runWithin(runner, {child(), u"emit"_s, hexBytes(u"한글 제목\r\n"_s.toUtf8()), u"."_s}, lines);
        QCOMPARE(result.status, RunStatus::Exited);
        QCOMPARE(lines.out, QStringList({u"한글 제목"_s}));
    }

    void cancellingTheCallerKillsTheProcess() {
        Signal started;
        BackgroundRun run(runner, {child(), u"sleep"_s, u"60000"_s}, [&](const QString& line) { if (line == u"started") started.set(); }, {});
        QVERIFY(started.waitFor(30s));

        QElapsedTimer timer;
        timer.start();
        run.stop();
        const std::optional<RunResult> result = run.waitFor(15s);
        const double elapsedSeconds = double(timer.elapsed()) / 1000.0;

        QVERIFY2(result.has_value(), "the run did not return within 15 s");
        QVERIFY2(elapsedSeconds < 10, qPrintable(u"cancel took %1 s; the process was probably not killed"_s.arg(elapsedSeconds)));
        QCOMPARE(result->status, RunStatus::Cancelled);
    }

    // ---- added ----

    void argumentsReachTheChildVerbatim() {
        const QList<QStringList> lists = xgs::testing::argumentLists(2000, 20261118);
        std::atomic<int> next{0};
        std::mutex mutex;
        QStringList failures;
        int checked = 0;
        std::vector<std::thread> workers;
        for (int w = 0; w < 8; ++w) {
            workers.emplace_back([&] {
                for (int i = next++; i < lists.size(); i = next++) {
                    Lines lines;
                    const RunResult result = runWithin(runner, QStringList{child(), u"argv"_s} + lists.at(i), lines);
                    QStringList expected;
                    for (const QString& argument : lists.at(i)) expected.append(hexUnits(argument));
                    std::lock_guard lock(mutex);
                    ++checked;
                    if (result.status != RunStatus::Exited || result.exitCode != 0 || lines.out != expected)
                        failures.append(u"list %1 %2: sent %3, received %4"_s.arg(i).arg(int(result.status))
                                            .arg(expected.join(u' '), lines.out.join(u' ')));
                }
            });
        }
        for (std::thread& worker : workers) worker.join();
        QCOMPARE(checked, 2000);
        QVERIFY2(failures.isEmpty(), qPrintable(u"%1 lists differ, the first: %2"_s.arg(failures.size()).arg(failures.value(0))));
    }

    void closesStandardInput() {
        Lines lines;
        const RunResult result = runWithin(runner, {child(), u"stdin"_s}, lines, 10s);
        QCOMPARE(result.status, RunStatus::Exited);
        QCOMPARE(result.exitCode, 0);
        QCOMPARE(lines.out, QStringList({u"0"_s}));
    }

    void setsThePythonEncodingVariables() {
        const ScopedVariable inherited(L"XGS_TEST_INHERITED", L"물려받은 값");
        const ScopedVariable encoding(L"PYTHONIOENCODING", L"cp949");
        const ScopedVariable utf8(L"PYTHONUTF8", L"0");
        QStringList seen;
        for (const QString name : {u"PYTHONIOENCODING"_s, u"PYTHONUTF8"_s, u"XGS_TEST_INHERITED"_s, u"SystemRoot"_s}) {
            Lines lines;
            const RunResult result = runWithin(runner, {child(), u"env"_s, name}, lines);
            QCOMPARE(result.status, RunStatus::Exited);
            QCOMPARE(lines.out.size(), 1);
            seen.append(lines.out.at(0));
        }
        QCOMPARE(seen.at(0), u"utf-8"_s);
        QCOMPARE(seen.at(1), u"1"_s);
        QCOMPARE(seen.at(2), u"물려받은 값"_s);
        QCOMPARE(seen.at(3), qEnvironmentVariable("SystemRoot"));
    }

    void aMissingExecutableIsAStartFailure() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        std::atomic<int> calls{0};
        const RunResult result = runner.run({windowsPath(dir.filePath(u"missing.exe"_s)), u"x"_s}, [&](const QString&) { ++calls; },
                                            [&](const QString&) { ++calls; }, std::stop_token());
        QCOMPARE(result.status, RunStatus::StartFailed);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(calls.load(), 0);
        // and no command at all
        QCOMPARE(runner.run({}, {}, {}, std::stop_token()).status, RunStatus::StartFailed);
    }

    void aBatchFileIsNotExecuted() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString marker = windowsPath(dir.filePath(u"ran.txt"_s));
        const QStringList names = {u"x.cmd"_s, u"y.BAT"_s, u"z.Cmd"_s, u"w.bat"_s};
        for (const QString& name : names) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write((u"@echo ran> \""_s + marker + u"\"\r\n"_s).toLocal8Bit());
        }
        // as named, and with the trailing dot or space that Windows drops from a path
        const QStringList commands = {u"x.cmd"_s, u"y.BAT"_s, u"z.Cmd"_s, u"w.bat."_s, u"w.bat "_s, u"w.bat. ."_s};
        for (const QString& name : commands) {
            std::atomic<int> calls{0};
            const RunResult result = runner.run({windowsPath(dir.filePath(name)), u"%PATH%&echo"_s},
                                                [&](const QString&) { ++calls; }, [&](const QString&) { ++calls; }, std::stop_token());
            QVERIFY2(result.status == RunStatus::StartFailed, qPrintable(name));
            QVERIFY(!result.error.isEmpty());
            QCOMPARE(calls.load(), 0);
        }
        QVERIFY(!QFile::exists(marker));
    }

    void cancellingKillsGrandchildrenToo() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pidFile = windowsPath(dir.filePath(u"pid.txt"_s));
        Signal started;
        KilledAtExit grandchild;
        {
            BackgroundRun run(runner, {child(), u"spawn"_s, pidFile, u"60000"_s},
                              [&](const QString& line) { if (line == u"started") started.set(); }, {});
            QVERIFY(started.waitFor(30s));
            const DWORD pid = readPid(pidFile);
            QVERIFY(pid != 0);
            grandchild.reset(openForWaiting(pid));
            QVERIFY(grandchild.get() != nullptr);
            QCOMPARE(WaitForSingleObject(grandchild.get(), 0), DWORD(WAIT_TIMEOUT));   // alive before the stop

            run.stop();
            const std::optional<RunResult> result = run.waitFor(15s);
            QVERIFY(result.has_value());
            QCOMPARE(result->status, RunStatus::Cancelled);
        }
        QCOMPARE(WaitForSingleObject(grandchild.get(), 5000), DWORD(WAIT_OBJECT_0));
    }

    void withoutAJobTheGrandchildSurvives() {
        // The control run: the same child started without a job, and only the child killed.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pidFile = windowsPath(dir.filePath(u"pid.txt"_s));
        QString commandLine = u"\"%1\" spawn \"%2\" 60000"_s.arg(child(), pidFile);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        QVERIFY(CreateProcessW(wide(child()), reinterpret_cast<wchar_t*>(commandLine.data()), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &startup, &info));
        CloseHandle(info.hThread);
        KilledAtExit spawned(info.hProcess);
        KilledAtExit grandchild;
        DWORD pid = 0;
        for (int i = 0; i < 300 && pid == 0; ++i) {
            pid = readPid(pidFile);
            if (pid == 0) Sleep(100);
        }
        QVERIFY(pid != 0);
        grandchild.reset(openForWaiting(pid));
        QVERIFY(grandchild.get() != nullptr);

        QVERIFY(TerminateProcess(spawned.get(), 1));
        QCOMPARE(WaitForSingleObject(spawned.get(), 5000), DWORD(WAIT_OBJECT_0));
        QCOMPARE(WaitForSingleObject(grandchild.get(), 1000), DWORD(WAIT_TIMEOUT));   // the grandchild lives on

        QVERIFY(TerminateProcess(grandchild.get(), 1));
        QCOMPARE(WaitForSingleObject(grandchild.get(), 5000), DWORD(WAIT_OBJECT_0));
    }

    void parallelRunsDoNotHoldEachOthersPipes() {
        std::latch go(9);
        std::stop_source stopLong;
        std::atomic<qint64> slowest{0};
        std::atomic<int> exited{0};
        QElapsedTimer timer;
        std::thread longRun([&] {
            go.arrive_and_wait();
            runner.run({child(), u"sleep"_s, u"60000"_s}, {}, {}, stopLong.get_token());
        });
        std::vector<std::thread> shortRuns;
        for (int i = 0; i < 8; ++i) {
            shortRuns.emplace_back([&, i] {
                go.arrive_and_wait();
                QElapsedTimer own;
                own.start();
                std::stop_source stop;
                std::thread watchdog([&] { for (int t = 0; t < 200 && !stop.stop_requested(); ++t) Sleep(100); stop.request_stop(); });
                const RunResult result = runner.run({child(), u"emit"_s, hexBytes("short " + QByteArray::number(i) + "\n"), u"."_s}, {}, {},
                                                    stop.get_token());
                const qint64 took = own.elapsed();
                stop.request_stop();
                watchdog.join();
                if (result.status == RunStatus::Exited) ++exited;
                for (qint64 seen = slowest.load(); took > seen && !slowest.compare_exchange_weak(seen, took);) {}
            });
        }
        for (std::thread& t : shortRuns) t.join();
        stopLong.request_stop();
        longRun.join();
        QCOMPARE(exited.load(), 8);
        QVERIFY2(slowest.load() < 2000, qPrintable(u"the slowest short run took %1 ms"_s.arg(slowest.load())));
    }

    void aStopRequestedBeforeTheStartRunsNothing() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pidFile = windowsPath(dir.filePath(u"pid.txt"_s));
        std::stop_source stop;
        stop.request_stop();
        std::atomic<int> calls{0};
        const RunResult result = runner.run({child(), u"spawn"_s, pidFile, u"60000"_s}, [&](const QString&) { ++calls; },
                                            [&](const QString&) { ++calls; }, stop.get_token());
        QCOMPARE(result.status, RunStatus::Cancelled);
        Sleep(500);
        QCOMPARE(calls.load(), 0);
        QVERIFY(!QFile::exists(pidFile));
    }

    void opensNoConsoleWindow() {
        const int before = visibleWindowsOfThisTree();
        // The child has no console window at all (CREATE_NO_WINDOW).
        Lines lines;
        const RunResult result = runWithin(runner, {child(), u"console"_s}, lines);
        QCOMPARE(result.status, RunStatus::Exited);
        QCOMPARE(lines.out, QStringList({u"none"_s}));
        // While a child runs, this process tree shows no more windows than before.
        std::atomic<int> during{-1};
        Signal counted;
        {
            BackgroundRun run(runner, {child(), u"sleep"_s, u"60000"_s},
                              [&](const QString& line) {
                                  if (line != u"started") return;
                                  Sleep(300);   // time for a window to appear, were there one
                                  during = visibleWindowsOfThisTree();
                                  counted.set();
                              },
                              {});
            QVERIFY(counted.waitFor(30s));
        }
        QCOMPARE(during.load(), before);
        QCOMPARE(visibleWindowsOfThisTree(), before);
    }

    // Author decision 5: what the child leaves running when it exits normally is killed too (Kotlin kills descendants only on
    // cancellation). The grandchild holds none of the pipes, so run returns as soon as the child has exited.
    void aGrandchildLeftRunningAfterANormalExitIsKilled() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pidFile = windowsPath(dir.filePath(u"pid.txt"_s));
        KilledAtExit grandchild;   // (kills it at the end of the test should the runner have left it alive)
        std::atomic<bool> aliveWhileRunning{false};
        Lines lines;
        const LineHandler onOut = [&](const QString& line) {
            lines.onOut()(line);
            if (line != u"started") return;
            // the child wrote the pid file before "started", and the job is still open while a handler runs
            if (const DWORD pid = readPid(pidFile)) {
                grandchild.reset(openForWaiting(pid));
                aliveWhileRunning = grandchild.get() && WaitForSingleObject(grandchild.get(), 0) == WAIT_TIMEOUT;
            }
        };
        std::optional<RunResult> result;
        {
            BackgroundRun run(runner, {child(), u"spawnexit"_s, pidFile, u"60000"_s}, onOut, lines.onErr());
            result = run.waitFor(20s);
        }
        QVERIFY2(result.has_value(), "the run did not return within 20 s");
        QCOMPARE(result->status, RunStatus::Exited);
        QCOMPARE(result->exitCode, 0);
        QCOMPARE(lines.out, QStringList({u"started"_s}));
        QVERIFY(grandchild.get() != nullptr);
        QVERIFY2(aliveWhileRunning.load(), "the grandchild was not running while the child's output was read");
        QVERIFY2(WaitForSingleObject(grandchild.get(), 5000) == WAIT_OBJECT_0, "the grandchild still runs 5 s after run returned");
    }

    // A command element holding U+0000 would cut the command line short (the arguments after it would silently be lost), so
    // nothing is started.
    void aNulInTheCommandIsAStartFailure() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pidFile = windowsPath(dir.filePath(u"pid.txt"_s));
        const QString nul(QChar(u'\0'));
        const QList<QStringList> commands = {
            {child(), u"spawn"_s, pidFile, u"100"_s, u"a"_s + nul + u"b"_s},
            {child(), u"spawn"_s, pidFile, u"100"_s + nul},
            {child() + nul + u"x"_s, u"spawn"_s, pidFile, u"100"_s},
        };
        for (const QStringList& command : commands) {
            std::atomic<int> calls{0};
            const RunResult result = runner.run(command, [&](const QString&) { ++calls; }, [&](const QString&) { ++calls; },
                                                std::stop_token());
            QCOMPARE(result.status, RunStatus::StartFailed);
            QCOMPARE(result.error, u"NUL 문자(U+0000)가 든 명령은 실행하지 않습니다"_s);
            QCOMPARE(calls.load(), 0);
        }
        Sleep(300);
        QVERIFY(!QFile::exists(pidFile));
    }

    // The 2 s device of a stop: a process outside the job (here this test process) holds a duplicate of the child's stdout
    // writing end, so the pipe does not break when the job is killed; the blocked read is cancelled (CancelIoEx on the pipe)
    // and run still returns Cancelled.
    void aStopReturnsWhenAProcessOutsideTheJobHoldsThePipe() {
        struct Duplicate {
            HANDLE handle = nullptr;
            void close() {
                if (handle) CloseHandle(handle);
                handle = nullptr;
            }
            ~Duplicate() { close(); }
        };
        Signal held;
        std::optional<RunResult> result;
        qint64 elapsed = -1;
        bool duplicated = false;
        {
            Duplicate duplicate;   // closed below before the run is destroyed, so that a broken runner cannot hang the test
            BackgroundRun run(runner, {child(), u"sleepshow"_s, u"60000"_s},
                              [&](const QString& line) {
                                  const QStringList parts = line.split(u' ');
                                  if (parts.size() != 3 || parts.at(0) != u"started") return;
                                  const HANDLE process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, DWORD(parts.at(1).toULong()));
                                  if (process) {
                                      const auto value = reinterpret_cast<HANDLE>(quintptr(parts.at(2).toULongLong()));
                                      DuplicateHandle(process, value, GetCurrentProcess(), &duplicate.handle, 0, FALSE,
                                                      DUPLICATE_SAME_ACCESS);
                                      CloseHandle(process);
                                  }
                                  held.set();
                              },
                              {});
            if (held.waitFor(30s)) {
                duplicated = duplicate.handle != nullptr;
                QElapsedTimer timer;
                timer.start();
                run.stop();
                result = run.waitFor(10s);
                elapsed = timer.elapsed();
            }
            duplicate.close();
        }
        QVERIFY2(duplicated, "the test could not take the child's stdout handle");
        QVERIFY2(result.has_value(), "the run did not return within 10 s of the stop");
        QCOMPARE(result->status, RunStatus::Cancelled);
        // it waited for the 2 s device (the pipe did not break on its own), and not much longer
        QVERIFY2(elapsed >= 1500 && elapsed < 8000, qPrintable(u"the stop took %1 ms"_s.arg(elapsed)));
    }

    void aThrowingHandlerKillsTheProcessAndRethrows() {
        // As Kotlin: an exception from a handler kills the process tree and comes out of run on the caller's thread. Once for
        // the stdout handler (sleep writes "started" on stdout), once for the stderr handler (sleeperr: on stderr).
        struct HandlerError : std::runtime_error {
            using std::runtime_error::runtime_error;
        };
        for (const bool onStderr : {false, true}) {
            const QString mode = onStderr ? u"sleeperr"_s : u"sleep"_s;
            std::vector<HANDLE> children;   // the xgs-test-child processes of this tree when the handler threw
            std::atomic<int> callsAfterReturn{0};
            std::atomic<bool> returned{false};
            const LineHandler thrower = [&](const QString& line) {
                if (returned) ++callsAfterReturn;
                if (line != u"started") return;
                // (only the helper's images: a snapshot's parent ids may also lead to unrelated processes, by id reuse)
                for (DWORD pid : processTree())
                    if (pid != GetCurrentProcessId())
                        if (HANDLE h = openForWaiting(pid)) {
                            if (imageName(h).endsWith(u"\\xgs-test-child.exe"_s, Qt::CaseSensitive)) children.push_back(h);
                            else CloseHandle(h);
                        }
                throw HandlerError("handler failed");
            };
            const LineHandler counter = [&](const QString&) { if (returned) ++callsAfterReturn; };
            std::stop_source stop;
            std::mutex mutex;
            std::condition_variable done;
            bool finished = false;
            QString outcome;
            std::thread runThread([&] {
                QString what;
                try {
                    const RunResult result = runner.run({child(), mode, u"60000"_s}, onStderr ? counter : thrower,
                                                        onStderr ? thrower : counter, stop.get_token());
                    what = u"returned status %1"_s.arg(int(result.status));
                } catch (const HandlerError& e) {
                    what = u"threw: "_s + QString::fromLatin1(e.what());
                } catch (...) {
                    what = u"threw something else"_s;
                }
                returned = true;
                std::lock_guard lock(mutex);
                outcome = what;
                finished = true;
                done.notify_all();
            });
            bool inTime = false;
            {
                std::unique_lock lock(mutex);
                inTime = done.wait_for(lock, 10s, [&] { return finished; });
            }
            if (!inTime) stop.request_stop();   // a broken runner: stop it so that no child outlives the test
            runThread.join();
            std::deque<KilledAtExit> guards;   // (kills whatever is still alive at the end)
            for (HANDLE h : children) guards.emplace_back(h);
            QVERIFY2(inTime, qPrintable(mode + u": run did not return within 10 s"_s));
            QCOMPARE(outcome, u"threw: handler failed"_s);
            QVERIFY(!guards.empty());
            for (const KilledAtExit& g : guards)
                QVERIFY2(WaitForSingleObject(g.get(), 5000) == WAIT_OBJECT_0, qPrintable(u"still alive: "_s + imageName(g.get())));
            QCOMPARE(callsAfterReturn.load(), 0);
        }
    }
};

QTEST_GUILESS_MAIN(SystemProcessRunnerTest)
#include "tst_system_process_runner.moc"

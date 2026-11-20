#include <QtTest>

#include <QDir>
#include <QFile>
#include <QMap>
#include <QTemporaryDir>

#include <functional>
#include <memory>
#include <optional>
#include <stop_token>

#include "paths/paths.h"
#include "support/fake_process_runner.h"
#include "tools/tool_manager.h"

using namespace Qt::StringLiterals;
using xgs::Outcome;
using xgs::OutcomeStatus;
using xgs::ToolPaths;
using xgs::ToolsStatus;
using xgs::process::LineHandler;
using xgs::testsupport::FakeProcessRunner;
using xgs::testsupport::ProcessHandler;
using xgs::testsupport::StartFailure;
using xgs::tools::Fetch;
using xgs::tools::ToolManager;
using xgs::tools::ToolPathProvider;

namespace {

QString windowsPath(const QString& path) {
    QString out = path;
    return out.replace(u'/', u'\\');
}

QString at(const QString& dir, const QString& name) {
    return *xgs::paths::child(dir, name);
}

// Kotlin's String.lines(): split at \r\n, \n and \r.
QStringList ktLines(const QString& text) {
    QStringList lines;
    QString line;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == u'\r' || c == u'\n') {
            lines.append(line);
            line.clear();
            if (c == u'\r' && i + 1 < text.size() && text.at(i + 1) == u'\n') ++i;
        } else {
            line += c;
        }
    }
    lines.append(line);
    return lines;
}

// runnerFor of the Kotlin test: the tool is the file name of command[0] up to its first '.'; a tool in outputs writes its
// lines to stdout and exits with exitCode, any other exits with 1.
ProcessHandler outputsHandler(QMap<QString, QString> outputs, int exitCode = 0) {
    return [outputs = std::move(outputs), exitCode](const QStringList& command, const LineHandler& out, const LineHandler&) {
        QString tool = xgs::paths::fileName(command.first()).value_or(QString());
        const qsizetype dot = tool.indexOf(u'.');
        if (dot >= 0) tool.truncate(dot);
        if (!outputs.contains(tool)) return 1;
        for (const QString& line : ktLines(outputs.value(tool))) out(line);
        return exitCode;
    };
}

ToolPaths pathsOf(bool ytDlp = true, const QString& js = u"node"_s) {
    return ToolPaths{ytDlp ? xgs::paths::fromText(u"C:/t/yt-dlp.exe"_s) : std::nullopt, xgs::paths::fromText(u"C:/t/ffmpeg.exe"_s),
                     xgs::paths::fromText(u"C:/t/"_s + js + u".exe"_s)};
}

// ToolPathProvider { ... } of the Kotlin test: asked again on every call.
class FunctionTools final : public ToolPathProvider {
public:
    explicit FunctionTools(std::function<ToolPaths()> current) : m_current(std::move(current)) {}
    ToolPaths current() const override { return m_current(); }

private:
    std::function<ToolPaths()> m_current;
};

FunctionTools fixed(ToolPaths paths) {
    return FunctionTools([paths] { return paths; });
}

// The manager's default fetch downloads from the network; the tests that do not install never call it.
Outcome<QByteArray> noFetch(const QString&, std::stop_token) {
    return Outcome<QByteArray>::failed(u"not used"_s);
}

Fetch bytesOf(qsizetype size) {
    return [size](const QString&, std::stop_token) { return Outcome<QByteArray>::ok(QByteArray(size, '\0')); };
}

bool writeText(const QString& path, const QByteArray& text) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}

QByteArray readText(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

}  // namespace

// Port of DefaultToolManagerTest.kt. Kotlin's fetch lambdas that throw IOException return Outcome::failed here, and a thrown
// ToolException is a Failed outcome.
class DefaultToolManagerTest : public QObject {
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_temp;
    QString m_root, m_binDir;

private slots:
    void init() {
        // a fresh directory per test, as Kotlin's per-instance Files.createTempDirectory
        m_temp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_temp->isValid());
        m_root = *xgs::paths::fromText(windowsPath(m_temp->path()));
        m_binDir = at(m_root, u"bin"_s);
    }
    void cleanup() { m_temp.reset(); }

    void reportsVersionsOfAllTools() {
        FakeProcessRunner runner(outputsHandler({
            {u"yt-dlp"_s, u"2026.10.01"_s},
            {u"ffmpeg"_s, u"ffmpeg version 8.1-full_build-www.gyan.dev Copyright (c) 2000-2026 the FFmpeg developers"_s},
            {u"node"_s, u"v24.11.1"_s},
        }));
        const FunctionTools tools = fixed(pathsOf());

        const ToolsStatus status = ToolManager(tools, runner, m_binDir, noFetch).status({});

        QVERIFY(status.ytDlp.found);
        QCOMPARE(status.ytDlp.version, std::optional<QString>(u"2026.10.01"_s));
        QCOMPARE(status.ffmpeg.version, std::optional<QString>(u"8.1-full_build-www.gyan.dev"_s));
        QVERIFY(status.jsRuntime.found);
        QCOMPARE(status.jsRuntime.version, std::optional<QString>(u"24.11.1"_s));
    }

    void denoVersionIsParsedFromItsBanner() {
        FakeProcessRunner runner(outputsHandler({{u"yt-dlp"_s, u"1"_s},
                                                 {u"ffmpeg"_s, u"ffmpeg version 7"_s},
                                                 {u"deno"_s, u"deno 2.5.0 (stable, release, x86_64-pc-windows-msvc)"_s}}));
        const FunctionTools tools = fixed(pathsOf(true, u"deno"_s));

        const ToolsStatus status = ToolManager(tools, runner, m_binDir, noFetch).status({});

        QVERIFY(status.jsRuntime.found);
        QCOMPARE(status.jsRuntime.version, std::optional<QString>(u"2.5.0"_s));
    }

    void oldJsRuntimesAreReportedAsNotUsable() {
        FakeProcessRunner oldRunner(
            outputsHandler({{u"yt-dlp"_s, u"1"_s}, {u"ffmpeg"_s, u"ffmpeg version 7"_s}, {u"node"_s, u"v20.11.0"_s}}));
        const FunctionTools nodeTools = fixed(pathsOf());
        const ToolsStatus old = ToolManager(nodeTools, oldRunner, m_binDir, noFetch).status({});
        QVERIFY(!old.jsRuntime.found);
        QCOMPARE(old.jsRuntime.version, std::optional<QString>(u"20.11.0"_s));

        FakeProcessRunner denoRunner(
            outputsHandler({{u"yt-dlp"_s, u"1"_s}, {u"ffmpeg"_s, u"ffmpeg version 7"_s}, {u"deno"_s, u"deno 2.2.9 (stable)"_s}}));
        const FunctionTools denoTools = fixed(pathsOf(true, u"deno"_s));
        const ToolsStatus oldDeno = ToolManager(denoTools, denoRunner, m_binDir, noFetch).status({});
        QVERIFY(!oldDeno.jsRuntime.found);
    }

    void missingToolsAreReportedNotFound() {
        FakeProcessRunner runner(outputsHandler({}));
        const FunctionTools tools = fixed(ToolPaths{std::nullopt, std::nullopt, std::nullopt});
        ToolManager manager(tools, runner, m_binDir, noFetch);

        const ToolsStatus status = manager.status({});

        QVERIFY(!status.ytDlp.found);
        QVERIFY(!status.ffmpeg.found);
        QVERIFY(!status.jsRuntime.found);
    }

    void installDownloadsYtDlpIntoTheAppBinDirectory() {
        const QString binDir = m_binDir;
        const FunctionTools provider([binDir] {
            const QString ytDlp = at(binDir, u"yt-dlp.exe"_s);
            return ToolPaths{QFileInfo::exists(ytDlp) ? std::optional<QString>(ytDlp) : std::nullopt, std::nullopt, std::nullopt};
        });
        QStringList fetched;
        FakeProcessRunner runner(outputsHandler({{u"yt-dlp"_s, u"2026.10.01"_s}}));
        ToolManager manager(provider, runner, m_binDir, [&fetched](const QString& url, std::stop_token) {
            fetched.append(url);
            return Outcome<QByteArray>::ok(QByteArray(2'000'000, '\0'));
        });

        const Outcome<QString> result = manager.installYtDlp({});

        QCOMPARE(fetched, QStringList{xgs::tools::YtDlpUrl.toString()});
        QVERIFY(QFileInfo::exists(at(m_binDir, u"yt-dlp.exe"_s)));
        QVERIFY(result.isOk());
        QVERIFY(result.value->contains(u"2026.10.01"_s));
    }

    void installRejectsATooSmallDownload() {
        FakeProcessRunner runner(outputsHandler({}));
        const FunctionTools tools = fixed(pathsOf());
        ToolManager manager(tools, runner, m_binDir, bytesOf(10));

        QCOMPARE(manager.installYtDlp({}).status, OutcomeStatus::Failed);
        QVERIFY(!QFileInfo::exists(at(m_binDir, u"yt-dlp.exe"_s)));
    }

    void installReportsNetworkFailures() {
        FakeProcessRunner runner(outputsHandler({}));
        const FunctionTools tools = fixed(pathsOf());
        ToolManager manager(tools, runner, m_binDir,
                            [](const QString&, std::stop_token) { return Outcome<QByteArray>::failed(u"offline"_s); });

        const Outcome<QString> error = manager.installYtDlp({});

        QCOMPARE(error.status, OutcomeStatus::Failed);
        QVERIFY(error.error.contains(u"offline"_s));
    }

    void installRejectsADownloadThatDoesNotRunAndKeepsTheOldBinary() {
        QVERIFY(QDir().mkpath(m_binDir));
        QVERIFY(writeText(at(m_binDir, u"yt-dlp.exe"_s), "old"));
        FakeProcessRunner runner(outputsHandler({}));
        const FunctionTools tools = fixed(pathsOf());
        ToolManager manager(tools, runner, m_binDir, bytesOf(2'000'000));

        QCOMPARE(manager.installYtDlp({}).status, OutcomeStatus::Failed);

        QCOMPARE(readText(at(m_binDir, u"yt-dlp.exe"_s)), QByteArray("old"));
        QVERIFY(!QFileInfo::exists(at(m_binDir, u"yt-dlp.new.exe"_s)));
    }

    void installReplacesAnExistingBinaryOnlyAfterTheNewOneRuns() {
        QVERIFY(QDir().mkpath(m_binDir));
        QVERIFY(writeText(at(m_binDir, u"yt-dlp.exe"_s), "old"));
        std::optional<QByteArray> textWhenVerified;
        const QString binDir = m_binDir;
        FakeProcessRunner runner([&textWhenVerified, binDir](const QStringList& command, const LineHandler& out, const LineHandler&) {
            if (command.first().endsWith(u"yt-dlp.new.exe"_s)) {
                textWhenVerified = readText(at(binDir, u"yt-dlp.exe"_s));
                out(u"2026.10.01"_s);
                return 0;
            }
            return 1;
        });
        const FunctionTools tools = fixed(pathsOf());
        ToolManager manager(tools, runner, m_binDir, bytesOf(2'000'000));

        QVERIFY(manager.installYtDlp({}).isOk());   // Kotlin's installYtDlp() does not throw

        QCOMPARE(textWhenVerified, std::optional<QByteArray>("old"));
        QCOMPARE(QFileInfo(at(m_binDir, u"yt-dlp.exe"_s)).size(), qint64(2'000'000));
        QVERIFY(!QFileInfo::exists(at(m_binDir, u"yt-dlp.new.exe"_s)));
        const QStringList names = QDir(m_binDir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        QCOMPARE(names, QStringList{u"yt-dlp.exe"_s});
    }

    void installReportsFileSystemFailuresAsToolExceptions() {
        const QString blocker = at(m_root, u"blocker"_s);
        QVERIFY(writeText(blocker, "not a directory"));
        FakeProcessRunner runner(outputsHandler({{u"yt-dlp"_s, u"2026.10.01"_s}}));
        const FunctionTools tools = fixed(pathsOf());
        ToolManager manager(tools, runner, at(blocker, u"bin"_s), bytesOf(2'000'000));

        const Outcome<QString> error = manager.installYtDlp({});

        QCOMPARE(error.status, OutcomeStatus::Failed);
        QVERIFY(error.error.contains(u"설치하지 못했습니다"_s));
    }

    void installReportsAMoveFailureAndKeepsTheOldBinary() {
        const QString target = at(m_binDir, u"yt-dlp.exe"_s);
        QVERIFY(QDir().mkpath(target));
        QVERIFY(writeText(at(target, u"inner.txt"_s), "keep"));
        FakeProcessRunner runner(outputsHandler({{u"yt-dlp"_s, u"2026.10.01"_s}}));
        const FunctionTools tools = fixed(pathsOf());
        ToolManager manager(tools, runner, m_binDir, bytesOf(2'000'000));

        const Outcome<QString> error = manager.installYtDlp({});

        QCOMPARE(error.status, OutcomeStatus::Failed);
        QVERIFY(error.error.contains(u"설치하지 못했습니다"_s));
        QVERIFY(QFileInfo(target).isDir());
        QCOMPARE(readText(at(target, u"inner.txt"_s)), QByteArray("keep"));
        QVERIFY(!QFileInfo::exists(at(m_binDir, u"yt-dlp.new.exe"_s)));
    }

    void updateRunsYtDlpSelfUpdate() {
        FakeProcessRunner runner(outputsHandler({{u"yt-dlp"_s, u"Current version: 2026.10.01\nUpdated yt-dlp to 2026.11.02"_s}}));
        const FunctionTools tools = fixed(pathsOf());
        ToolManager manager(tools, runner, m_binDir, noFetch);

        const Outcome<QString> result = manager.updateYtDlp({});

        QCOMPARE(runner.commands().size(), 1);
        QVERIFY(runner.commands().first().contains(u"-U"_s));
        QVERIFY(result.isOk());
        QCOMPARE(*result.value, u"Updated yt-dlp to 2026.11.02"_s);
    }

    void updateFailureIsReported() {
        FakeProcessRunner runner(outputsHandler({{u"yt-dlp"_s, u"ERROR: Unable to update"_s}}, 1));
        const FunctionTools tools = fixed(pathsOf());
        ToolManager manager(tools, runner, m_binDir, noFetch);

        const Outcome<QString> error = manager.updateYtDlp({});

        QCOMPARE(error.status, OutcomeStatus::Failed);
        QCOMPARE(error.error, u"ERROR: Unable to update"_s);
    }

    void updateNeedsAnInstalledYtDlp() {
        FakeProcessRunner runner(outputsHandler({}));
        const FunctionTools tools = fixed(pathsOf(false));
        ToolManager manager(tools, runner, m_binDir, noFetch);

        QCOMPARE(manager.updateYtDlp({}).status, OutcomeStatus::Failed);
    }

    void updateReportsAnExecutableThatCannotBeStarted() {
        // Kotlin's runner throws IOException("Cannot run program"); here the fake's start failure carries that text.
        FakeProcessRunner runner([](const QStringList&, const LineHandler&, const LineHandler&) -> int {
            throw StartFailure{u"Cannot run program"_s};
        });
        const FunctionTools tools = fixed(pathsOf());
        ToolManager manager(tools, runner, m_binDir, noFetch);

        const Outcome<QString> error = manager.updateYtDlp({});

        QCOMPARE(error.status, OutcomeStatus::Failed);
        QVERIFY(error.error.contains(u"실행할 수 없습니다"_s));
        QVERIFY(error.error.contains(u"Cannot run program"_s));
    }

    // Added (no Kotlin counterpart): where Kotlin's coroutine would be cancelled, the outcome is Cancelled.
    void installIsCancelledWhenTheFetchIsCancelled() {
        FakeProcessRunner runner(outputsHandler({{u"yt-dlp"_s, u"2026.10.01"_s}}));
        const FunctionTools tools = fixed(pathsOf());
        ToolManager manager(tools, runner, m_binDir,
                            [](const QString&, std::stop_token) { return Outcome<QByteArray>::cancelled(); });

        QCOMPARE(manager.installYtDlp({}).status, OutcomeStatus::Cancelled);

        QVERIFY(runner.commands().isEmpty());
        QVERIFY(!QFileInfo::exists(at(m_binDir, u"yt-dlp.exe"_s)));
        QVERIFY(!QFileInfo::exists(at(m_binDir, u"yt-dlp.new.exe"_s)));
    }

    void cancelledInstallLeavesNoTemporaryFile() {
        std::stop_source source;
        bool stagedWhileRunning = false;
        const QString binDir = m_binDir;
        FakeProcessRunner runner([&](const QStringList& command, const LineHandler& out, const LineHandler&) {
            stagedWhileRunning = command.first() == at(binDir, u"yt-dlp.new.exe"_s) && QFileInfo::exists(command.first());
            out(u"2026.10.01"_s);
            source.request_stop();   // the user cancels while the downloaded yt-dlp runs
            return 0;
        });
        const FunctionTools tools = fixed(pathsOf());
        ToolManager manager(tools, runner, m_binDir, bytesOf(2'000'000));

        QCOMPARE(manager.installYtDlp(source.get_token()).status, OutcomeStatus::Cancelled);

        QVERIFY(stagedWhileRunning);
        QVERIFY(!QFileInfo::exists(at(m_binDir, u"yt-dlp.new.exe"_s)));
        QVERIFY(!QFileInfo::exists(at(m_binDir, u"yt-dlp.exe"_s)));
    }

    void updateIsCancelledWhenTheRunIsCancelled() {
        std::stop_source source;
        FakeProcessRunner runner([&source](const QStringList&, const LineHandler& out, const LineHandler&) {
            out(u"Updated yt-dlp to 2026.11.02"_s);
            source.request_stop();
            return 0;
        });
        const FunctionTools tools = fixed(pathsOf());
        ToolManager manager(tools, runner, m_binDir, noFetch);

        QCOMPARE(manager.updateYtDlp(source.get_token()).status, OutcomeStatus::Cancelled);
    }
};

QTEST_APPLESS_MAIN(DefaultToolManagerTest)
#include "tst_tool_manager.moc"

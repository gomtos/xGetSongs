#include <QtTest>

#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <windows.h>

#include <chrono>
#include <memory>
#include <thread>

#include "app_services.h"
#include "app_test_support.h"
#include "crash_dump.h"
#include "diag/file_logger.h"
#include "diagnostics.h"
#include "support/diag_support.h"

// The diagnostics wired into the process (Q5 Task 2; no Kotlin pair: the Kotlin tests of the parts are ported in Task 1): the log
// file and Qt's messages, the start and exit records with the marker, the end of the Windows session, the hang watch of the GUI
// thread, the crash dump (through crash_probe, a console exe that crashes on purpose: no window, no error box), the dump files
// next to the rolled logs, and the clearing of the work folder. Everything is written below a QTemporaryDir that stands for
// APPDATA and the temp folder; no window reaches the screen (offscreen platform); nothing touches the network.

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::app::AppPaths;
using xgs::app::Diagnostics;
using xgs::app::DiagnosticsOptions;

Q_LOGGING_CATEGORY(lcTest, "xgs.test")
Q_LOGGING_CATEGORY(lcOther, "other.lib")

namespace {

QString readText(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(file.readAll());
}

QStringList lines(const QString& path) {
    QStringList out = readText(path).split(u"\r\n"_s);
    if (!out.isEmpty() && out.last().isEmpty()) out.removeLast();
    return out;
}

QStringList containing(const QStringList& all, const QString& part) {
    QStringList out;
    for (const QString& line : all) {
        if (line.contains(part)) out << line;
    }
    return out;
}

bool isMiniDump(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    return file.size() > 32 && file.read(4) == QByteArray("MDMP");
}

/// Blocks the GUI thread (the test's) without letting it run any event.
void stall(std::chrono::milliseconds time) {
    std::this_thread::sleep_for(time);
}

bool threadHasEnded(unsigned long id) {
    const HANDLE thread = OpenThread(SYNCHRONIZE, FALSE, id);
    if (thread == nullptr) return true;   // gone altogether
    const bool ended = WaitForSingleObject(thread, 0) == WAIT_OBJECT_0;
    CloseHandle(thread);
    return ended;
}

}  // namespace

class DiagnosticsTest : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_temp;
    AppPaths m_paths;

    QString logDir() const { return m_paths.appData + u"\\logs"_s; }
    QString logFile() const { return logDir() + u"\\xgetsongs.log"_s; }
    QString tempDir() const { return m_temp->filePath(u"temp"_s); }

    DiagnosticsOptions quiet() const {
        DiagnosticsOptions options;
        options.tempDir = tempDir();
        options.crashHandler = false;
        return options;
    }

    // A watch that finds a hang after 400 ms (instead of 5 s): a beat every 50 ms; a gap between ticks is only taken for sleep
    // after 3 s, so a busy machine (20 runs of the test, a debug build) does not drop a hang as a gap.
    DiagnosticsOptions quickWatch() const {
        DiagnosticsOptions options = quiet();
        options.hangIntervalMs = 50;
        options.hang.thresholdMs = 400;
        options.hang.repeatMs = 60'000;
        options.hang.maxRepeatMs = 60'000;
        options.hang.maxTickGapMs = 3'000;
        return options;
    }

private slots:
    void init() {
        m_temp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_temp->isValid());
        m_paths = AppPaths::fromValues(m_temp->filePath(u"roaming"_s), m_temp->filePath(u"local"_s), std::nullopt);
    }

    void cleanup() { m_temp.reset(); }

    // ---- the log file, the records -----------------------------------------------------------------------------------------

    void theStartWritesTheStartupRecordAndThisRunsMarker() {
        {
            auto diagnostics = Diagnostics::start(m_paths, quiet());
            QVERIFY(diagnostics);
            QCOMPARE(diagnostics->logDir(), logDir());
            const QString log = readText(logFile());
            QVERIFY2(log.contains(u" INFO  [main] xgs.diagnostics - 시작 정보\n"_s), qPrintable(log));
            QVERIFY2(log.contains(u"  로그 폴더: "_s + logDir()), qPrintable(log));
            QVERIFY2(log.contains(u"  Qt: "_s), qPrintable(log));
            const QString marker = readText(logDir() + u"\\last-run.txt"_s);
            QVERIFY2(marker.startsWith(u"state=running\npid="_s + QString::number(GetCurrentProcessId()) + u'\n'), qPrintable(marker));
        }
        const QString marker = readText(logDir() + u"\\last-run.txt"_s);
        QVERIFY2(marker.startsWith(u"state=running\n"_s), "dropping the diagnostics is no exit record: only logExit writes one");
    }

    void qtMessagesGoToTheLogWithTheirCategoryAndWithoutPaths() {
        auto diagnostics = Diagnostics::start(m_paths, quiet());
        qCWarning(lcTest) << "failed to open" << u"C:\\Users\\Some User\\Music\\더미 하나.m4a"_s;
        qCInfo(lcTest).noquote() << u"plain info"_s;
        qInfo("no category");
        std::jthread worker([] {
            xgs::diag::setCurrentThreadName(u"engine-worker"_s);
            qCWarning(lcTest) << "from a worker";
        });
        worker.join();

        const QStringList all = lines(logFile());
        const QStringList warned = containing(all, u"failed to open"_s);
        QCOMPARE(warned.size(), 1);
        QVERIFY2(warned.first().contains(u" WARN  [main] xgs.test - failed to open \"<경로>\""_s), qPrintable(warned.first()));
        QVERIFY2(!readText(logFile()).contains(u"Some User"_s) && !readText(logFile()).contains(u"더미"_s), "no path, no name");
        QCOMPARE(containing(all, u" INFO  [main] xgs.test - plain info"_s).size(), 1);
        QCOMPARE(containing(all, u" INFO  [main] default - no category"_s).size(), 1);
        QCOMPARE(containing(all, u" WARN  [engine-worker] xgs.test - from a worker"_s).size(), 1);
    }

    void debugLinesAreKeptOnlyForTheAppsOwnCategories() {
        // logback-sidecar.xml: com.xgetsongs at DEBUG, everything else at INFO.
        auto diagnostics = Diagnostics::start(m_paths, quiet());
        qCDebug(lcTest) << "own debug";
        qCDebug(lcOther) << "other debug";
        qCInfo(lcOther) << "other info";

        const QStringList all = lines(logFile());
        QCOMPARE(containing(all, u"DEBUG [main] xgs.test - own debug"_s).size(), 1);
        QCOMPARE(containing(all, u"other debug"_s).size(), 0);
        QCOMPARE(containing(all, u"INFO  [main] other.lib - other info"_s).size(), 1);
    }

    void theMessageHandlerIsPutBackWhenTheDiagnosticsGo() {
        {
            auto diagnostics = Diagnostics::start(m_paths, quiet());
            qCWarning(lcTest) << "while running";
        }
        qCWarning(lcTest) << "after the end";
        QCOMPARE(containing(lines(logFile()), u"while running"_s).size(), 1);
        QCOMPARE(containing(lines(logFile()), u"after the end"_s).size(), 0);
    }

    void aUserExitIsRecordedOnceAndMarksTheRun() {
        auto diagnostics = Diagnostics::start(m_paths, quiet());
        diagnostics->setRunningJobs([] { return 2; });
        diagnostics->markUserExit();
        diagnostics->logExit();
        diagnostics->logExit();

        const QStringList all = lines(logFile());
        QCOMPARE(containing(all, u"INFO  [main] xgs.diagnostics - 사용자가 창 닫기를 요청함"_s).size(), 1);
        const QStringList exits = containing(all, u" xgs.exit - "_s);
        QCOMPARE(exits.size(), 1);
        QVERIFY2(exits.first().contains(u"INFO  [main] xgs.exit - JVM 종료 시작: 사용자가 창을 닫음 (가동 "_s) &&
                     exits.first().endsWith(u"초, 등록된 작업 2개)"_s),
                 qPrintable(exits.first()));
        const QString marker = readText(logDir() + u"\\last-run.txt"_s);
        QVERIFY2(marker.startsWith(u"state=exited\n"_s) && marker.contains(u"\nreason=user\n"_s), qPrintable(marker));
    }

    void theEndOfTheWindowsSessionWritesTheExitRecordAtOnce() {
        auto diagnostics = Diagnostics::start(m_paths, quiet());
        diagnostics->handleWindowsMessage(WM_QUERYENDSESSION, 0);
        QCOMPARE(containing(lines(logFile()), u" xgs.exit - "_s).size(), 0);
        diagnostics->handleWindowsMessage(WM_ENDSESSION, FALSE);   // the end was called off
        QCOMPARE(containing(lines(logFile()), u" xgs.exit - "_s).size(), 0);

        diagnostics->handleWindowsMessage(WM_ENDSESSION, TRUE);
        diagnostics->handleWindowsMessage(WM_ENDSESSION, TRUE);   // every top-level window gets it
        diagnostics->logExit();                                    // main, if it gets that far

        const QStringList all = lines(logFile());
        QCOMPARE(containing(all, u"INFO  [main] xgs.diagnostics - Windows가 로그오프·종료를 묻음"_s).size(), 1);
        const QStringList exits = containing(all, u" xgs.exit - "_s);
        QCOMPARE(exits.size(), 1);
        QVERIFY2(exits.first().contains(u"WARN  [main] xgs.exit - JVM 종료 시작: 창을 닫지 않은 종료"_s), qPrintable(exits.first()));
        const QString marker = readText(logDir() + u"\\last-run.txt"_s);
        QVERIFY2(marker.startsWith(u"state=exited\n"_s) && marker.contains(u"\nreason=other\n"_s), qPrintable(marker));
    }

    void aLogFolderThatCannotBeMadeFallsBackToTheTempFolder() {
        // APPDATA\xGetSongs is a file: no logs folder can be made below it.
        QVERIFY(QDir().mkpath(m_temp->filePath(u"roaming"_s)));
        QFile blocker(m_paths.appData);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();

        auto diagnostics = Diagnostics::start(m_paths, quiet());
        QVERIFY(diagnostics);
        const QString fallback = QDir::toNativeSeparators(tempDir()) + u"\\xgetsongs-logs"_s;
        QCOMPARE(diagnostics->logDir(), fallback);
        QVERIFY(readText(fallback + u"\\xgetsongs.log"_s).contains(u"시작 정보"_s));
    }

    void aLogFileAnotherProgramHoldsDoesNotKeepTheAppFromStarting() {
        // Review Focus 1: another program holds xgetsongs.log without sharing it, and the marker is read-only.
        QVERIFY(QDir().mkpath(logDir()));
        const HANDLE held = CreateFileW(reinterpret_cast<const wchar_t*>(logFile().utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(held != INVALID_HANDLE_VALUE);
        QFile marker(logDir() + u"\\last-run.txt"_s);
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.close();
        SetFileAttributesW(reinterpret_cast<const wchar_t*>((logDir() + u"\\last-run.txt"_s).utf16()), FILE_ATTRIBUTE_READONLY);

        {
            DiagnosticsOptions options = quiet();
            options.crashHandler = true;
            auto diagnostics = Diagnostics::start(m_paths, options);
            QVERIFY(diagnostics);
            qCWarning(lcTest) << "lost";
            diagnostics->markUserExit();
            diagnostics->logExit();
        }
        CloseHandle(held);
        SetFileAttributesW(reinterpret_cast<const wchar_t*>((logDir() + u"\\last-run.txt"_s).utf16()), FILE_ATTRIBUTE_NORMAL);
        QCOMPARE(readText(logFile()), QString());
    }

    // ---- the hang watch -------------------------------------------------------------------------------------------------------

    void aStalledGuiThreadIsRecordedOnceWithOneDump() {
        auto diagnostics = Diagnostics::start(m_paths, quickWatch());
        QVERIFY(diagnostics->watching());

        stall(1'500ms);
        // The beat runs again; the watch says so within a few ticks.
        QTRY_VERIFY_WITH_TIMEOUT(!containing(lines(logFile()), u"화면 스레드 응답 회복"_s).isEmpty(), 10'000);
        diagnostics->stopWatching();

        const QStringList all = lines(logFile());
        const QStringList hung = containing(all, u" xgs.hang - 화면 스레드가 "_s);
        QCOMPARE(hung.size(), 1);
        QVERIFY2(hung.first().contains(u" ERROR [xgs-hang-watchdog] xgs.hang - 화면 스레드가 "_s) &&
                     hung.first().endsWith(u"초 동안 응답하지 않음 (덤프: xgetsongs-hang.dmp)"_s),
                 qPrintable(hung.first()));
        const QStringList recovered = containing(all, u"화면 스레드 응답 회복"_s);
        QCOMPARE(recovered.size(), 1);
        QVERIFY2(recovered.first().contains(u" WARN  [xgs-hang-watchdog] xgs.hang - 화면 스레드 응답 회복 (총 "_s), qPrintable(recovered.first()));
        QVERIFY(isMiniDump(logDir() + u"\\xgetsongs-hang.dmp"_s));
    }

    void aModalDialogIsNotAHang() {
        // A dialog's exec runs an event loop of its own: the beats are delivered there. (A native file dialog runs the Windows
        // modal loop, which dispatches the message that makes Qt deliver posted calls; that one is not shown in a test.)
        auto diagnostics = Diagnostics::start(m_paths, quickWatch());
        QDialog dialog;
        QTimer::singleShot(1'500, &dialog, &QDialog::accept);
        dialog.exec();
        QCOMPARE(containing(lines(logFile()), u"응답하지 않음"_s).size(), 0);

        // The watch was alive all along: a real stall right after is found.
        stall(1'000ms);
        QTRY_VERIFY_WITH_TIMEOUT(!containing(lines(logFile()), u"화면 스레드 응답 회복"_s).isEmpty(), 10'000);
        QCOMPARE(containing(lines(logFile()), u"응답하지 않음"_s).size(), 1);
    }

    void afterStopWatchingABlockedGuiThreadIsNoHang() {
        // main stops the watch when the event loop ends: the teardown (up to about 1.5 s for a job) is not a hang.
        auto diagnostics = Diagnostics::start(m_paths, quickWatch());
        diagnostics->stopWatching();
        QVERIFY(!diagnostics->watching());
        stall(1'200ms);
        QTest::qWait(300);
        diagnostics.reset();
        QCOMPARE(containing(lines(logFile()), u"xgs.hang"_s).size(), 0);
        QVERIFY(!QFile::exists(logDir() + u"\\xgetsongs-hang.dmp"_s));
    }

    void stoppingTheWatchEndsItsThread() {
        auto diagnostics = Diagnostics::start(m_paths, quickWatch());
        const unsigned long id = diagnostics->watchThreadId();
        QVERIFY(id != 0);
        QVERIFY(!threadHasEnded(id));
        diagnostics->stopWatching();
        QVERIFY2(threadHasEnded(id), "stopWatching joins the thread");
        QCOMPARE(diagnostics->watchThreadId(), 0UL);
        diagnostics->stopWatching();   // twice is fine

        auto again = Diagnostics::start(m_paths, quickWatch());
        const unsigned long second = again->watchThreadId();
        again.reset();
        QVERIFY2(threadHasEnded(second), "the destructor stops the watch too");
    }

    // ---- the crash dump ----------------------------------------------------------------------------------------------------

    void aCrashLeavesAMinidumpAndALogLine_data() {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("line");
        QTest::newRow("access violation") << u"access-violation"_s << u"처리되지 않은 예외 0xC0000005로 끝남 (덤프: xgetsongs-crash.dmp)"_s;
        QTest::newRow("std::terminate") << u"terminate"_s << u"std::terminate로 끝남 (덤프: xgetsongs-crash.dmp)"_s;
    }

    void aCrashLeavesAMinidumpAndALogLine() {
        QFETCH(QString, mode);
        QFETCH(QString, line);
        const QString dir = QDir::toNativeSeparators(m_temp->filePath(u"crash"_s));
        QProcess probe;
        probe.setProcessChannelMode(QProcess::MergedChannels);
        probe.setCreateProcessArgumentsModifier(
            [](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
        probe.start(QStringLiteral(XGS_CRASH_PROBE), {dir, mode});
        QVERIFY(probe.waitForStarted(10'000));
        // A minidump of a small process takes well under a second; the limit is generous for a loaded machine.
        QVERIFY(probe.waitForFinished(60'000));
        QVERIFY2(probe.exitCode() != 0, probe.readAll().constData());

        QVERIFY2(isMiniDump(dir + u"\\xgetsongs-crash.dmp"_s), probe.readAll().constData());
        const QStringList crash = containing(lines(dir + u"\\xgetsongs.log"_s), u" xgs.crash - "_s);
        QCOMPARE(crash.size(), 1);
        QVERIFY2(crash.first().contains(u" ERROR [main] xgs.crash - "_s + line), qPrintable(crash.first()));
        QVERIFY2(crash.first().size() > 24 && crash.first().at(4) == u'-' && crash.first().at(19) == u'.', qPrintable(crash.first()));
        QCOMPARE(containing(lines(dir + u"\\xgetsongs.log"_s), u"probe ready"_s).size(), 1);
    }

    void anUnusedCrashDumpFileIsNotLeftBehind() {
        const QString dir = QDir::toNativeSeparators(m_temp->filePath(u"crash"_s));
        QVERIFY(QDir().mkpath(dir));
        {
            xgs::app::CrashHandler handler(dir, nullptr);
            QVERIFY(handler.installed());
            xgs::app::CrashHandler second(dir, nullptr);
            QVERIFY2(!second.installed(), "one at a time");
        }
        QVERIFY(!QFile::exists(dir + u"\\xgetsongs-crash.dmp"_s));
    }

    void theDumpOfAnEarlierCrashIsKeptUntilTheNextCrash() {
        const QString dir = QDir::toNativeSeparators(m_temp->filePath(u"crash"_s));
        QVERIFY(QDir().mkpath(dir));
        QFile earlier(dir + u"\\xgetsongs-crash.dmp"_s);
        QVERIFY(earlier.open(QIODevice::WriteOnly));
        earlier.write("MDMP earlier dump");
        earlier.close();
        {
            xgs::app::CrashHandler handler(dir, nullptr);
            QVERIFY(handler.installed());
        }
        QCOMPARE(readText(dir + u"\\xgetsongs-crash.dmp"_s), u"MDMP earlier dump"_s);
    }

    void theDumpFilesAreNeverTakenForRolledLogs() {
        QVERIFY(QDir().mkpath(logDir()));
        for (const QString& name : {xgs::app::CrashDumpFileName, xgs::app::HangDumpFileName}) {
            QFile dump(logDir() + u'\\' + name);
            QVERIFY(dump.open(QIODevice::WriteOnly));
            dump.write(QByteArray(1024 * 1024, 'x'));
        }
        xgs::diag::FileLoggerOptions tiny;
        tiny.maxFileBytes = 100;
        tiny.totalSizeCapBytes = 10;   // any rolled file would be over it
        xgs::diag::FileLogger logger({}, tiny);
        QVERIFY(logger.open(logDir()));
        for (int i = 0; i < 20; ++i) logger.write(xgs::diag::Level::Info, u"main"_s, u"xgs.test"_s, QString(80, u'.'));
        logger.close();
        QVERIFY(QFile::exists(logDir() + u'\\' + xgs::app::CrashDumpFileName));
        QVERIFY(QFile::exists(logDir() + u'\\' + xgs::app::HangDumpFileName));
    }

    // ---- the work folder ---------------------------------------------------------------------------------------------------

    void staleWorkIsRemovedAtStart() {
        xgs::diag::test::LogCapture capture;
        const QString work = m_paths.workDir();
        QVERIFY(QDir().mkpath(work + u"\\job-1\\deep"_s));
        QFile file(work + u"\\job-1\\deep\\part.m4a"_s);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QFile top(work + u"\\left.tmp"_s);
        QVERIFY(top.open(QIODevice::WriteOnly));
        top.close();

        xgs::app::clearStaleWork(work, capture.sink());

        QVERIFY(QDir(work).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System));
        QCOMPARE(capture.events().size(), size_t(0));
        xgs::app::clearStaleWork(m_temp->filePath(u"missing"_s), capture.sink());
        QCOMPARE(capture.events().size(), size_t(0));
    }

    void aJunctionInTheWorkFolderIsRemovedButNotFollowed() {
        xgs::diag::test::LogCapture capture;
        const QString work = m_paths.workDir();
        const QString outside = QDir::toNativeSeparators(m_temp->filePath(u"outside"_s));
        QVERIFY(QDir().mkpath(work));
        QVERIFY(QDir().mkpath(outside));
        QFile kept(outside + u"\\keep.txt"_s);
        QVERIFY(kept.open(QIODevice::WriteOnly));
        kept.close();
        QProcess mklink;
        mklink.start(u"cmd.exe"_s, {u"/c"_s, u"mklink"_s, u"/J"_s, work + u"\\link"_s, outside});
        QVERIFY(mklink.waitForFinished(10'000));
        QCOMPARE(mklink.exitCode(), 0);
        QVERIFY(QFileInfo(work + u"\\link\\keep.txt"_s).exists());

        xgs::app::clearStaleWork(work, capture.sink());

        QVERIFY(QFile::exists(outside + u"\\keep.txt"_s));
        QVERIFY(!QFileInfo::exists(work + u"\\link"_s));
        QCOMPARE(capture.events().size(), size_t(0));
    }

    void workThatCannotBeRemovedIsOneLineAndNoFailure() {
        xgs::diag::test::LogCapture capture;
        const QString work = m_paths.workDir();
        QVERIFY(QDir().mkpath(work + u"\\job-1"_s));
        const QString busy = work + u"\\job-1\\busy.m4a"_s;
        const HANDLE held = CreateFileW(reinterpret_cast<const wchar_t*>(busy.utf16()), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(held != INVALID_HANDLE_VALUE);
        QFile other(work + u"\\other.tmp"_s);
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.close();

        xgs::app::clearStaleWork(work, capture.sink());
        CloseHandle(held);

        QVERIFY(!QFile::exists(work + u"\\other.tmp"_s));
        const auto events = capture.events();
        QCOMPARE(events.size(), size_t(1));
        QCOMPARE(events.front().level, xgs::diag::Level::Warn);
        QCOMPARE(events.front().logger, u"xgs.app"_s);
        QVERIFY2(events.front().message.startsWith(u"지난 실행의 작업 파일을 다 지우지 못함 ("_s), qPrintable(events.front().message));
        QVERIFY2(!events.front().message.contains(u"busy"_s), "no file name");
    }
};

XGS_OFFSCREEN_TEST_MAIN(DiagnosticsTest)
#include "tst_diagnostics.moc"

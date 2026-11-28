#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <limits>
#include <map>
#include <memory>
#include <stdexcept>

#include "diag/exit_record.h"
#include "diag/run_marker.h"
#include "diag/run_record.h"
#include "support/diag_support.h"

using namespace Qt::StringLiterals;
using xgs::diag::DIAGNOSTICS_LOGGER_NAME;
using xgs::diag::ExitRecord;
using xgs::diag::Instant;
using xgs::diag::Level;
using xgs::diag::MARKER_FILE_NAME;
using xgs::diag::parseRunMarker;
using xgs::diag::Processes;
using xgs::diag::renderExited;
using xgs::diag::renderRunning;
using xgs::diag::RunMarker;
using xgs::diag::RunRecord;
using xgs::diag::RunState;
using xgs::diag::test::LogCapture;

namespace {

Instant instant(const QString& text) {
    const auto parsed = Instant::parse(text);
    return parsed ? *parsed : Instant{};
}

class FakeProcesses final : public Processes {
public:
    explicit FakeProcesses(std::map<qint64, Instant> alive = {}) : m_alive(std::move(alive)) {}
    qint64 ownPid() const override { return 4242; }
    Instant ownStart() const override { return instant(u"2026-10-07T13:00:00Z"_s); }
    std::optional<Instant> startOf(qint64 pid) const override {
        const auto at = m_alive.find(pid);
        return at == m_alive.end() ? std::nullopt : std::optional<Instant>(at->second);
    }

private:
    std::map<qint64, Instant> m_alive;
};

class ThrowingProcesses final : public Processes {
public:
    ThrowingProcesses(bool startOfFails, bool ownStartFails) : m_startOfFails(startOfFails), m_ownStartFails(ownStartFails) {}
    qint64 ownPid() const override { return 4242; }
    Instant ownStart() const override {
        if (m_ownStartFails) throw std::runtime_error("no start time");
        return instant(u"2026-10-07T13:00:00Z"_s);
    }
    std::optional<Instant> startOf(qint64) const override {
        if (m_startOfFails) throw std::runtime_error("no process table");
        return std::nullopt;
    }

private:
    bool m_startOfFails;
    bool m_ownStartFails;
};

QString readText(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return u"<unreadable>"_s;
    return QString::fromUtf8(file.readAll());
}

}  // namespace

// The tests of Kotlin's DiagnosticsTest about Diagnostics.start that do not need a JVM: the marker of the last run and the exit
// record, through RunRecord. Not ported: startPreparesTheFolderLogsTheStartupRecordAndHooksTheExceptionHandler (the part about
// Thread.setDefaultUncaughtExceptionHandler; the rest is the added test below), stopEndsTheWatchdogThread (the thread is the
// app's).
class DiagnosticsTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    LogCapture m_capture;
    const Instant m_earlier = instant(u"2026-10-07T12:15:40.123Z"_s);

    QString logs() const { return m_dir.filePath(u"logs"_s); }
    QString markerPath() const { return logs() + u"/last-run.txt"_s; }

    void writeMarker(const QString& text) {
        QVERIFY(QDir().mkpath(logs()));
        QFile file(markerPath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text.toUtf8());
    }

    std::unique_ptr<RunRecord> start(const Processes& processes = FakeProcesses()) {
        return std::make_unique<RunRecord>(logs(), m_capture.sink(), processes, u"시작 정보\n  앱: xGetSongs"_s);
    }

    QString diagnosticsLogger() const { return DIAGNOSTICS_LOGGER_NAME; }
    QString exitLogger() const { return QString::fromUtf16(ExitRecord::LoggerName); }

    QString show(const std::optional<RunMarker>& marker) const {
        if (!marker) return u"null"_s;
        return u"%1 pid=%2 started=%3"_s.arg(marker->state == RunState::Running ? u"RUNNING"_s : u"EXITED"_s)
            .arg(marker->pid ? QString::number(*marker->pid) : u"null"_s)
            .arg(marker->started ? marker->started->toString() : u"null"_s);
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void init() {
        m_capture.clear();
        QDir(logs()).removeRecursively();
    }

    // ---- added: the part of the start that does not need the JVM -------------------------------------------------

    void startPreparesTheFolderAndLogsTheStartupRecord() {
        auto run = start();

        QVERIFY(QFileInfo(logs()).isDir());
        const auto infos = m_capture.at(Level::Info, diagnosticsLogger());
        QCOMPARE(infos.size(), size_t(1));
        QVERIFY2(infos.front().message.contains(u"시작 정보"_s), qPrintable(infos.front().message));
    }

    void theStartDoesNotThrowWhenTheLogFolderCannotBeMade() {
        QVERIFY(QDir().mkpath(m_dir.path()));
        const QString blocker = m_dir.filePath(u"a-file"_s);
        QFile file(blocker);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        RunRecord run(blocker + u"/logs"_s, m_capture.sink(), FakeProcesses(), u"시작 정보"_s);  // must not throw
        run.logExit();                                                                            // nor this

        QVERIFY(!m_capture.events(exitLogger()).empty());
    }

    // ---- start -----------------------------------------------------------------------------

    void theExitHookKnowsWhetherTheUserClosedTheWindowAndHowManyJobsAreRegistered() {
        auto run = start();

        QCOMPARE(run->runningJobs(), -1);  // unknown until the server says
        run->logExit();
        run->setRunningJobs([] { return 5; });  // set once the server is up
        run->markUserExit();
        run->logExit();

        const auto events = m_capture.events(exitLogger());
        QCOMPARE(events.size(), size_t(2));
        const auto& external = events[0];
        const auto& user = events[1];
        QCOMPARE(external.level, Level::Warn);
        QVERIFY2(external.message.contains(u"창을 닫지 않은 종료"_s) && external.message.contains(u"등록된 작업 수를 알 수 없음"_s),
                 qPrintable(external.message));
        QCOMPARE(user.level, Level::Info);
        QVERIFY2(user.message.contains(u"사용자가 창을 닫음"_s) && user.message.contains(u"등록된 작업 5개"_s), qPrintable(user.message));
    }

    void theExitRecordSaysWhenAnUncaughtExceptionCameBeforeAndStartForgetsEarlierOnes() {
        {
            auto first = start();
            first->noteUncaught();
            first->logExit();
            const auto events = m_capture.events(exitLogger());
            QVERIFY2(events.back().message.contains(u"직전에 처리되지 않은 예외가 있었음"_s), qPrintable(events.back().message));
        }

        auto second = start();
        second->logExit();
        const auto events = m_capture.events(exitLogger());
        QVERIFY2(!events.back().message.contains(u"직전에"_s), qPrintable(events.back().message));
    }

    // ---- the marker of the last run ---------------------------------------------------------

    void startWritesTheRunningMarkerAndTheExitHookRewritesIt() {
        auto run = start();

        QCOMPARE(show(parseRunMarker(readText(markerPath()))), show(RunMarker{RunState::Running, 4242, instant(u"2026-10-07T13:00:00Z"_s)}));

        run->logExit();  // an exit nobody in the window asked for
        const QString other = readText(markerPath());
        QVERIFY2(other.contains(u"state=exited"_s) && other.contains(u"reason=other"_s) && other.contains(u"pid=4242"_s) &&
                     other.contains(u"time="_s),
                 qPrintable(other));

        run->markUserExit();
        run->logExit();
        const QString user = readText(markerPath());
        QVERIFY2(user.contains(u"state=exited"_s) && user.contains(u"reason=user"_s), qPrintable(user));
        const auto parsed = parseRunMarker(user);
        QVERIFY(parsed && parsed->state == RunState::Exited);
    }

    void aPreviousRunThatEndedCleanlyIsNotMentioned() {
        writeMarker(renderExited(77, m_earlier, true, m_earlier.plusSeconds(60)));

        start();

        const auto events = m_capture.events(diagnosticsLogger());
        QVERIFY2(events.size() == 1, qPrintable(u"only the startup record: "_s + m_capture.allMessages(diagnosticsLogger()).join(u'|')));
    }

    void aPreviousRunWithoutAnExitRecordIsAWarning() {
        writeMarker(renderRunning(77, m_earlier));

        start();

        const auto warnings = m_capture.at(Level::Warn, diagnosticsLogger());
        QCOMPARE(warnings.size(), size_t(1));
        const QString& warning = warnings.front().message;
        QVERIFY2(warning.contains(u"이전 실행(PID 77, 시작 "_s) && warning.contains(u"정상 종료 기록 없이 끝났음"_s), qPrintable(warning));
        QCOMPARE(m_capture.at(Level::Info, diagnosticsLogger()).size(), size_t(1));  // the startup record
    }

    void anotherRunThatIsStillAliveIsAnInfoLine() {
        writeMarker(renderRunning(77, m_earlier));

        start(FakeProcesses({{77, m_earlier}}));

        QVERIFY(m_capture.at(Level::Warn, diagnosticsLogger()).empty());
        const QStringList infos = m_capture.messages(Level::Info, diagnosticsLogger());
        QVERIFY2(infos.contains(u"다른 실행이 아직 실행 중임 (PID 77)"_s), qPrintable(infos.join(u'|')));
    }

    void aMissingOrCorruptMarkerSaysNothing() {
        start();  // no file yet
        writeMarker(u"this is not a marker\n\x01"_s);
        start();

        QVERIFY2(m_capture.events(diagnosticsLogger()).size() == 2, "two startup records and nothing else");
        QVERIFY(m_capture.at(Level::Warn, diagnosticsLogger()).empty());
    }

    void aHandEditedMarkerWithAnAbsurdTimeDoesNotStopTheApp() {
        writeMarker(u"state=running\npid=1234\nstarted=+1000000000-12-31T23:59:59Z\n"_s);

        start();  // must not throw

        QVERIFY2(m_capture.events(diagnosticsLogger()).size() == 1, qPrintable(m_capture.allMessages(diagnosticsLogger()).join(u'|')));
        const auto parsed = parseRunMarker(readText(markerPath()));
        QVERIFY2(parsed && parsed->state == RunState::Running, "and the marker is this run's now");
    }

    void otherAbsurdMarkerValuesDoNotStopTheAppEither() {
        const QStringList texts = {
            u"state=running\npid=99999999999999999999\nstarted=2026-10-07T12:15:40.123Z"_s,
            u"state=running\npid=-5\nstarted=2026-10-07T12:15:40.123Z"_s,
            u"state=running\npid=%1\nstarted=2026-10-07T12:15:40.123Z"_s.arg(std::numeric_limits<qint64>::max()),
            u"state=running\npid=1234\nstarted=-1000000000-01-01T00:00:00Z"_s,
            u"state=exited\npid=1234\nstarted=+1000000000-12-31T23:59:59Z"_s,
        };
        for (const QString& text : texts) {
            writeMarker(text);

            start();  // must not throw
        }
    }

    void aProcessTableThatFailsDoesNotStopTheAppAndTheMarkerIsStillWritten() {
        writeMarker(renderRunning(77, m_earlier));

        start(ThrowingProcesses(true, false));  // must not throw

        const auto warnings = m_capture.at(Level::Warn, diagnosticsLogger());
        QCOMPARE(warnings.size(), size_t(1));
        QVERIFY2(warnings.front().message.contains(u"이전 실행 기록을 확인하지 못함 (runtime_error)"_s), qPrintable(warnings.front().message));
        const auto parsed = parseRunMarker(readText(markerPath()));
        QVERIFY(parsed && parsed->pid == std::optional<qint64>(4242));
    }

    void aProcessThatCannotSayWhenItStartedDoesNotStopTheApp() {
        start(ThrowingProcesses(false, true));  // must not throw

        QCOMPARE(m_capture.at(Level::Info, diagnosticsLogger()).size(), size_t(1));  // the startup record
        const auto warnings = m_capture.at(Level::Warn, diagnosticsLogger());
        QCOMPARE(warnings.size(), size_t(1));  // and a note that the marker is not kept
        QVERIFY2(warnings.front().message.contains(u"이번 실행 기록을 만들지 못함 (runtime_error)"_s), qPrintable(warnings.front().message));
    }
};

QTEST_GUILESS_MAIN(DiagnosticsTest)
#include "tst_run_record.moc"

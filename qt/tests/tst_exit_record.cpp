#include <QtTest>

#include <chrono>
#include <stdexcept>
#include <utility>
#include <vector>

#include "diag/exit_record.h"
#include "support/diag_support.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::diag::describeExit;
using xgs::diag::ExitRecord;
using xgs::diag::ExitSources;
using xgs::diag::formatUptime;
using xgs::diag::Level;
using xgs::diag::test::LogCapture;

namespace {

struct NotAStdException {};   // as an `Error` of the JVM is not an Exception

bool has(const QString& text, const QString& part) {
    return text.contains(part);
}

}  // namespace

// None of these tests exits anything: the record's body is called directly.
class ExitLoggerTest : public QObject {
    Q_OBJECT

private:
    LogCapture m_capture;

    ExitRecord record(std::function<int()> runningJobs = [] { return 0; },
                      std::function<std::chrono::milliseconds()> uptime = [] { return std::chrono::milliseconds(90'000); },
                      std::function<bool()> uncaughtSeen = [] { return false; }, std::function<void(bool)> onExit = {}) {
        ExitSources sources;
        sources.runningJobs = std::move(runningJobs);
        sources.uptime = std::move(uptime);
        sources.memoryMb = [] { return std::pair<qint64, qint64>(100, 2'000); };
        sources.uncaughtSeen = std::move(uncaughtSeen);
        sources.onExit = std::move(onExit);
        return ExitRecord(std::move(sources), m_capture.sink());
    }

private slots:
    void init() { m_capture.clear(); }

private slots:
    // ---- describeExit ----------------------------------------------------------------------

    void aUserExitIsInfoAndSaysTheUserClosedTheWindow() {
        const auto exit = describeExit(true, 0, 3s);

        QVERIFY(!exit.warn);
        QVERIFY2(has(exit.text, u"JVM 종료 시작"_s), qPrintable(exit.text));
        QVERIFY2(has(exit.text, u"사용자가 창을 닫음"_s), qPrintable(exit.text));
        QVERIFY2(has(exit.text, u"등록된 작업 0개"_s), qPrintable(exit.text));
        QVERIFY2(has(exit.text, u"가동 3초"_s), qPrintable(exit.text));
        QVERIFY2(!has(exit.text, u"창을 닫지 않은 종료"_s), qPrintable(exit.text));
    }

    void aUserExitWithRunningJobsSaysHowMany() {
        const auto exit = describeExit(true, 2, 61s);

        QVERIFY(!exit.warn);
        QVERIFY2(has(exit.text, u"등록된 작업 2개"_s), qPrintable(exit.text));
        QVERIFY2(has(exit.text, u"가동 1분 1초"_s), qPrintable(exit.text));
    }

    void anExternalExitIsAWarningThatNamesTheUsualCauses() {
        const auto exit = describeExit(false, 1, 5min);

        QVERIFY(exit.warn);
        QVERIFY2(has(exit.text, u"JVM 종료 시작"_s), qPrintable(exit.text));
        QVERIFY2(has(exit.text, u"창을 닫지 않은 종료 (콘솔 종료, SIGTERM, 로그오프·시스템 종료, 또는 처리되지 않은 예외 뒤의 종료)"_s),
                 qPrintable(exit.text));
        QVERIFY2(has(exit.text, u"등록된 작업 1개"_s), qPrintable(exit.text));
        QVERIFY2(!has(exit.text, u"사용자가 창을 닫음"_s), qPrintable(exit.text));
    }

    void anExitAfterAnUncaughtExceptionSaysSo() {
        const auto exit = describeExit(false, 0, 1s, true);

        QVERIFY(exit.warn);
        QVERIFY2(exit.text.endsWith(u" | 직전에 처리되지 않은 예외가 있었음"_s), qPrintable(exit.text));
        QVERIFY(!has(describeExit(false, 0, 1s).text, u"직전에"_s));
    }

    void aUserExitKeepsItsTextWhateverHappenedBefore() {
        const auto clean = describeExit(true, 1, 1s);
        const auto afterAnException = describeExit(true, 1, 1s, true);

        QCOMPARE(clean.warn, afterAnException.warn);
        QCOMPARE(clean.text, afterAnException.text);
    }

    void anUnknownNumberOfJobsIsSaidSo() {
        for (const bool userRequested : {true, false}) {
            const auto exit = describeExit(userRequested, -1, 10s);

            QVERIFY2(has(exit.text, u"등록된 작업 수를 알 수 없음"_s), qPrintable(exit.text));
            QVERIFY2(!has(exit.text, u"-1"_s), qPrintable(exit.text));
        }
    }

    void theUptimeIsWrittenInHoursMinutesAndSeconds() {
        QCOMPARE(formatUptime(0ms), u"0초"_s);
        QCOMPARE(formatUptime(999ms), u"0초"_s);
        QCOMPARE(formatUptime(59s), u"59초"_s);
        QCOMPARE(formatUptime(60s), u"1분 0초"_s);
        QCOMPARE(formatUptime(12min + 3s), u"12분 3초"_s);
        QCOMPARE(formatUptime(1h), u"1시간 0분 0초"_s);
        QCOMPARE(formatUptime(1h + 2min + 3s), u"1시간 2분 3초"_s);
        QCOMPARE(formatUptime(27h + 1s), u"27시간 0분 1초"_s);
        QCOMPARE(formatUptime(-5s), u"0초"_s);  // never a negative time
    }

    // ---- the record ------------------------------------------------------------------------

    void theHookLogsAnExitTheUserAskedForAtInfoWithJobsAndHeap() {
        ExitRecord exitRecord = record([] { return 3; }, [] { return 125s; });

        exitRecord.markUserExit();
        exitRecord.logExit();

        const auto events = m_capture.events();
        QCOMPARE(events.size(), size_t(1));
        QCOMPARE(events.front().level, Level::Info);
        QCOMPARE(events.front().logger, QString::fromUtf16(ExitRecord::LoggerName));
        const QString& message = events.front().message;
        QVERIFY2(has(message, u"JVM 종료 시작"_s), qPrintable(message));
        QVERIFY2(has(message, u"사용자가 창을 닫음"_s), qPrintable(message));
        QVERIFY2(has(message, u"등록된 작업 3개"_s), qPrintable(message));
        QVERIFY2(has(message, u"가동 2분 5초"_s), qPrintable(message));
        QVERIFY2(has(message, u"힙 사용 100MB (최대 2000MB)"_s), qPrintable(message));
    }

    void theHookLogsAnExitNobodyAskedForAtWarn() {
        ExitRecord exitRecord = record([] { return 1; });

        exitRecord.logExit();

        const auto events = m_capture.events();
        QCOMPARE(events.size(), size_t(1));
        QCOMPARE(events.front().level, Level::Warn);
        QVERIFY2(has(events.front().message, u"창을 닫지 않은 종료"_s), qPrintable(events.front().message));
        QVERIFY2(has(events.front().message, u"등록된 작업 1개"_s), qPrintable(events.front().message));
    }

    void theHookMentionsAnUncaughtExceptionBeforeTheHeapFigure() {
        ExitRecord exitRecord = record([] { return 0; }, [] { return 90s; }, [] { return true; });

        exitRecord.logExit();

        const auto events = m_capture.events();
        QCOMPARE(events.size(), size_t(1));
        const QString& message = events.front().message;
        QVERIFY2(has(message, u"창을 닫지 않은 종료"_s) && has(message, u" | 직전에 처리되지 않은 예외가 있었음 | 힙 사용"_s),
                 qPrintable(message));
    }

    void theExitCallbackGetsTheUserFlagAfterTheRecordIsWritten() {
        std::vector<std::pair<bool, size_t>> seen;
        ExitRecord exitRecord = record([] { return 0; }, [] { return 90s; }, [] { return false; },
                                       [&](bool user) { seen.emplace_back(user, m_capture.events().size()); });

        exitRecord.logExit();
        exitRecord.markUserExit();
        exitRecord.logExit();

        // false, then true, each after its own log record
        const std::vector<std::pair<bool, size_t>> expected = {{false, 1}, {true, 2}};
        QVERIFY(seen == expected);
    }

    void aFailingExitCallbackAndAFailingFlagNeverBreakTheHook() {
        ExitRecord exitRecord = record([] { return 0; }, [] { return 90s; }, []() -> bool { throw std::runtime_error("flag"); },
                                       [](bool) { throw std::runtime_error("callback"); });

        exitRecord.logExit();  // must not throw

        QCOMPARE(m_capture.events().size(), size_t(1));
    }

    void theHookStillLogsWhenTheJobCountCannotBeAsked() {
        ExitRecord exitRecord = record([]() -> int { throw std::runtime_error("server is gone"); });

        exitRecord.logExit();  // must not throw

        const auto events = m_capture.events();
        QCOMPARE(events.size(), size_t(1));
        QCOMPARE(events.front().level, Level::Warn);
        QVERIFY2(has(events.front().message, u"등록된 작업 수를 알 수 없음"_s), qPrintable(events.front().message));
    }

    void theHookNeverThrowsWhateverGoesWrong() {
        ExitSources sources;
        sources.runningJobs = []() -> int { throw std::runtime_error("jobs"); };
        sources.uptime = []() -> std::chrono::milliseconds { throw std::runtime_error("clock"); };
        sources.memoryMb = []() -> std::pair<qint64, qint64> { throw std::runtime_error("heap"); };
        ExitRecord exitRecord(std::move(sources), m_capture.sink());

        exitRecord.markUserExit();
        exitRecord.logExit();  // must not throw

        const auto events = m_capture.events();
        QCOMPARE(events.size(), size_t(1));  // it still says that the process is going down
        QVERIFY(has(events.front().message, u"JVM 종료 시작"_s));
    }

    void anErrorFromAnyOfTheFiguresStillLeavesTheRecord() {
        ExitSources sources;
        sources.runningJobs = []() -> int { throw NotAStdException{}; };
        sources.uptime = []() -> std::chrono::milliseconds { throw NotAStdException{}; };
        sources.memoryMb = []() -> std::pair<qint64, qint64> { throw NotAStdException{}; };
        sources.uncaughtSeen = []() -> bool { throw NotAStdException{}; };
        sources.onExit = [](bool) { throw NotAStdException{}; };
        ExitRecord exitRecord(std::move(sources), m_capture.sink());

        exitRecord.logExit();  // must not throw

        const auto events = m_capture.events();
        QCOMPARE(events.size(), size_t(1));
        const QString& message = events.front().message;
        QVERIFY2(has(message, u"JVM 종료 시작"_s) && has(message, u"등록된 작업 수를 알 수 없음"_s), qPrintable(message));
    }

    void theUserFlagIsNotUndoneByLoggingTwice() {
        ExitRecord exitRecord = record();
        exitRecord.markUserExit();

        exitRecord.logExit();
        exitRecord.logExit();

        const auto events = m_capture.events();
        QCOMPARE(events.size(), size_t(2));
        QCOMPARE(events[0].level, Level::Info);
        QCOMPARE(events[1].level, Level::Info);
    }

    // ---- added ------------------------------------------------------------------------------

    void theJobCountSourceCanBeReplacedLaterAndIsMinusOneUntilThen() {
        ExitSources sources;
        ExitRecord exitRecord(std::move(sources), m_capture.sink());
        QCOMPARE(exitRecord.runningJobs(), -1);

        exitRecord.setRunningJobs([] { return 5; });

        QCOMPARE(exitRecord.runningJobs(), 5);
        exitRecord.markUserExit();
        exitRecord.logExit();
        QVERIFY2(has(m_capture.events().front().message, u"등록된 작업 5개"_s), qPrintable(m_capture.events().front().message));
    }

    void theTextIsTheDescriptionOfTheRecordsOwnUserFlag() {
        ExitRecord exitRecord = record();

        QCOMPARE(exitRecord.text(2, 61s, false), describeExit(false, 2, 61s, false).text);
        QCOMPARE(exitRecord.text(2, 61s, true), describeExit(false, 2, 61s, true).text);
        exitRecord.markUserExit();
        QCOMPARE(exitRecord.text(2, 61s, true), describeExit(true, 2, 61s, true).text);
        QVERIFY(!exitRecord.describe(2, 61s, true).warn);
    }

    void withoutAMemorySourceTheRecordHasNoHeapFigure() {
        ExitSources sources;
        ExitRecord exitRecord(std::move(sources), m_capture.sink());

        exitRecord.logExit();

        QVERIFY(!has(m_capture.events().front().message, u"힙"_s));
    }
};

QTEST_GUILESS_MAIN(ExitLoggerTest)
#include "tst_exit_record.moc"

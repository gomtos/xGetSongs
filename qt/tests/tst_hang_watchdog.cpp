#include <QtTest>

#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

#include "diag/hang_detector.h"
#include "support/diag_support.h"

using namespace Qt::StringLiterals;
using xgs::diag::HangWatchdog;
using xgs::diag::Hung;
using xgs::diag::Level;
using xgs::diag::Recovered;
using xgs::diag::test::LogCapture;

namespace {

// Not a std::exception, as an `Error` of the JVM is not an Exception.
struct NotAStdException {};

}  // namespace

// Time is a variable here and the heartbeat is a queue the test empties when it wants the watched thread to answer, so the loop
// body (tick) is called directly and nothing sleeps. What the Kotlin watchdog does about a hang (the thread dump text, the
// hang-*.txt files, the one ERROR line) and its thread are the app's now; the watchdog here hands the findings to callbacks.
class HangWatchdogTest : public QObject {
    Q_OBJECT

private:
    LogCapture m_log;
    qint64 m_now = 0;
    bool m_uiAnswers = false;
    std::vector<std::function<void()>> m_queuedHeartbeats;
    int m_posts = 0;
    int m_dumps = 0;    // the number of times the watchdog said "hung" (Kotlin: the number of dumps made)
    std::vector<Hung> m_hungs;
    std::vector<Recovered> m_recoveries;
    std::function<void()> m_onHungHook;
    std::function<void(std::function<void()>)> m_postOverride;

    std::unique_ptr<HangWatchdog> makeWatchdog() {
        auto watchdog = std::make_unique<HangWatchdog>(
            [this](std::function<void()> heartbeat) {
                if (m_postOverride) {
                    m_postOverride(std::move(heartbeat));
                    return;
                }
                ++m_posts;
                if (m_uiAnswers) heartbeat();
                else m_queuedHeartbeats.push_back(std::move(heartbeat));
            },
            [this] { return m_now; }, m_log.sink());
        watchdog->setOnHung([this](const Hung& hung) {
            ++m_dumps;
            m_hungs.push_back(hung);
            if (m_onHungHook) m_onHungHook();
        });
        watchdog->setOnRecovered([this](const Recovered& recovered) { m_recoveries.push_back(recovered); });
        return watchdog;
    }

    // One second passes, then the watchdog thread's loop body runs; count times.
    void ticks(HangWatchdog& watchdog, int count) {
        for (int i = 0; i < count; ++i) {
            m_now += 1'000;
            watchdog.tick();
        }
    }

    // The watched thread gets to its queue: every heartbeat posted so far runs, at the current time.
    void answerQueuedHeartbeats() {
        const auto queued = m_queuedHeartbeats;
        for (const auto& heartbeat : queued) heartbeat();
        m_queuedHeartbeats.clear();
    }

private slots:
    void init() {
        m_log.clear();
        m_now = 0;
        m_uiAnswers = false;
        m_queuedHeartbeats.clear();
        m_posts = 0;
        m_dumps = 0;
        m_hungs.clear();
        m_recoveries.clear();
        m_onHungHook = nullptr;
        m_postOverride = nullptr;
    }

private slots:
    void aNewHeartbeatIsPostedOnlyWhenThePreviousOneHasRun() {
        auto watchdog = makeWatchdog();

        ticks(*watchdog, 10);
        QCOMPARE(m_posts, 1);  // ten ticks, one heartbeat

        answerQueuedHeartbeats();
        QCOMPARE(m_queuedHeartbeats.size(), size_t(0));
        ticks(*watchdog, 1);
        QCOMPARE(m_posts, 2);  // the next tick posts again
        ticks(*watchdog, 5);
        QCOMPARE(m_posts, 2);
    }

    void theBeatCountsFromTheTimeTheHeartbeatRunsNotFromTheTimeItWasPosted() {
        auto watchdog = makeWatchdog();
        ticks(*watchdog, 3);  // the heartbeat is posted at 1 s and waits in the queue

        m_now = 4'000;
        answerQueuedHeartbeats();  // it runs at 4 s: that is the beat
        ticks(*watchdog, 5);       // 5 s to 9 s; the next heartbeat (posted at 5 s) is never run

        QCOMPARE(m_dumps, 0);  // 9 s after the post but only 5 s after the beat: not more than the threshold
        ticks(*watchdog, 1);
        QCOMPARE(m_dumps, 1);  // 10 s: 6 s since the beat
    }

    void aHeartbeatThatCouldNotBePostedIsNotLeftPendingForever() {
        bool failNext = true;
        std::vector<std::function<void()>> delivered;
        m_postOverride = [&](std::function<void()> heartbeat) {
            if (failNext) throw std::runtime_error("no queue");
            delivered.push_back(std::move(heartbeat));
        };
        auto watchdog = makeWatchdog();
        ticks(*watchdog, 2);  // both fail

        failNext = false;
        ticks(*watchdog, 2);

        QCOMPARE(delivered.size(), size_t(1));  // the post that works goes out, and the next waits for it to run
    }

    void aThreadThatAnswersImmediatelyNeverGetsADump() {
        m_uiAnswers = true;
        auto watchdog = makeWatchdog();

        ticks(*watchdog, 300);

        QCOMPARE(m_dumps, 0);
        QVERIFY(m_log.at(Level::Error).empty());
        QVERIFY(m_log.at(Level::Warn).empty());
        QCOMPARE(m_queuedHeartbeats.size(), size_t(0));
    }

    void aHeartbeatThatCannotBePostedDoesNotKillTheTick() {
        m_postOverride = [](std::function<void()>) { throw std::runtime_error("no queue"); };
        auto watchdog = makeWatchdog();

        ticks(*watchdog, 3);  // must not throw

        const QStringList warnings = m_log.messages(Level::Warn);
        QVERIFY2(!warnings.isEmpty() && warnings.first().contains(u"runtime_error"_s), qPrintable(warnings.join(u'|')));
    }

    void aFailureThatKeepsComingIsLoggedOnceInAWhileNotEverySecond() {
        m_postOverride = [](std::function<void()>) { throw std::runtime_error("no queue"); };
        auto watchdog = makeWatchdog();

        ticks(*watchdog, 59);
        QCOMPARE(m_log.messages(Level::Warn).size(), 1);  // the first one is logged, the 58 after it are not

        ticks(*watchdog, 1);
        QCOMPARE(m_log.messages(Level::Warn).size(), 2);  // and a reminder at the sixtieth
    }

    void aGapBetweenTicksIsLoggedButNotReportedAsAHang() {
        auto watchdog = makeWatchdog();
        ticks(*watchdog, 3);

        m_now += 3'600'000;  // the machine slept for an hour
        watchdog->tick();

        QVERIFY(m_log.at(Level::Error).empty());
        QVERIFY(m_log.at(Level::Warn).empty());
        QCOMPARE(m_dumps, 0);
        const QStringList info = m_log.messages(Level::Info);
        QCOMPARE(info.size(), 1);
        QVERIFY2(info.first().contains(u"확인 간격"_s) && info.first().contains(u"1회"_s), qPrintable(info.first()));
    }

    void anErrorFromThePostIsSurvivedToo() {
        m_postOverride = [](std::function<void()>) { throw NotAStdException{}; };
        auto watchdog = makeWatchdog();

        ticks(*watchdog, 3);  // must not throw

        QVERIFY(!m_log.messages(Level::Warn).isEmpty());
    }

    void aSlowDumpDoesNotMakeTheNextTickAGapThatDropsTheHang() {
        int calls = 0;
        m_onHungHook = [&] {
            ++calls;
            m_now += 30'000;  // building and writing this dump takes half a minute
        };
        auto watchdog = makeWatchdog();

        ticks(*watchdog, 6);  // the hang is reported at 6 s; that tick ends at 36 s
        QCOMPARE(calls, 1);

        ticks(*watchdog, 1);  // 37 s: one second after the end of that tick, and the repeat is due

        QCOMPARE(calls, 2);  // the hang is still known: no gap came between the ticks
        QVERIFY2(m_log.at(Level::Info).empty(), "no gap line");
    }

    void theRepeatsOfALongHangBackOff() {
        auto watchdog = makeWatchdog();

        ticks(*watchdog, 300);
        QCOMPARE(m_dumps, 4);  // at 6, 36, 96 and 216 s

        ticks(*watchdog, 160);  // 460 s: the next one was due at 456 s
        QCOMPARE(m_dumps, 5);
    }

    // ---- added: what the Kotlin tests check through the dump and the log, checked on the callbacks ----------------------

    void aFrozenThreadIsReportedOnceAndOnlyOneHeartbeatWaits() {
        auto watchdog = makeWatchdog();

        ticks(*watchdog, 5);
        QCOMPARE(m_dumps, 0);  // 5 s without an answer is not a hang yet

        ticks(*watchdog, 5);

        QCOMPARE(m_dumps, 1);  // one report, not one per tick
        QCOMPARE(m_hungs.front().silentMs, qint64(6'000));
        QVERIFY(!m_hungs.front().repeat);
        QCOMPARE(m_queuedHeartbeats.size(), size_t(1));  // only one heartbeat waits in the queue of the blocked thread
    }

    void theRecoveryIsReportedOnceWithTheWholeSilentTime() {
        auto watchdog = makeWatchdog();
        ticks(*watchdog, 6);
        QCOMPARE(m_dumps, 1);
        QVERIFY(m_recoveries.empty());

        m_now += 2'000;
        answerQueuedHeartbeats();  // the thread was blocked until now, 8 s after its last answer
        m_uiAnswers = true;
        ticks(*watchdog, 1);

        QCOMPARE(m_recoveries.size(), size_t(1));
        QCOMPARE(m_recoveries.front().totalMs, qint64(8'000));
        ticks(*watchdog, 60);
        QCOMPARE(m_dumps, 1);  // no new report once it answers
        QCOMPARE(m_recoveries.size(), size_t(1));  // the recovery is reported once
    }

    void aHungCallbackThatThrowsDoesNotKillTheTickOrTheLaterReports() {
        m_onHungHook = [] { throw std::runtime_error("while writing the dump"); };
        auto watchdog = makeWatchdog();

        ticks(*watchdog, 8);  // must not throw

        QCOMPARE(m_dumps, 1);
        const QStringList warnings = m_log.messages(Level::Warn);
        QVERIFY2(!warnings.isEmpty() && warnings.first().contains(u"runtime_error"_s), qPrintable(warnings.join(u'|')));
        ticks(*watchdog, 30);  // 38 s: the repeat is due at 36 s
        QCOMPARE(m_dumps, 2);  // it went on ticking and reported again
        QVERIFY(m_hungs.back().repeat);
    }

    void aHeartbeatThatRunsAfterTheWatchdogIsGoneDoesNothing() {
        auto watchdog = makeWatchdog();
        ticks(*watchdog, 1);
        QCOMPARE(m_queuedHeartbeats.size(), size_t(1));

        watchdog.reset();
        m_queuedHeartbeats.front()();  // must not touch freed memory
    }
};

QTEST_GUILESS_MAIN(HangWatchdogTest)
#include "tst_hang_watchdog.moc"

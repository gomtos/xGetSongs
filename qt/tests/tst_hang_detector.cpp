#include <QtTest>

#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "diag/hang_detector.h"

using namespace Qt::StringLiterals;
using xgs::diag::HangDetector;
using xgs::diag::HangEvent;
using xgs::diag::Hung;
using xgs::diag::Recovered;

namespace {

using Timed = std::vector<std::pair<qint64, HangEvent>>;

// Events as text, so that a failed QCOMPARE shows what they were.
QString show(const HangEvent& event) {
    if (const auto* hung = std::get_if<Hung>(&event)) {
        return u"Hung(%1, repeat=%2)"_s.arg(hung->silentMs).arg(hung->repeat ? u"true"_s : u"false"_s);
    }
    return u"Recovered(%1)"_s.arg(std::get<Recovered>(event).totalMs);
}

QString show(const std::optional<HangEvent>& event) {
    return event ? show(*event) : u"null"_s;
}

QString show(const Timed& events) {
    QStringList parts;
    for (const auto& [at, event] : events) parts << u"%1: %2"_s.arg(at).arg(show(event));
    return u"["_s + parts.join(u", "_s) + u"]"_s;
}

QString show(const std::vector<qint64>& times) {
    QStringList parts;
    for (const qint64 time : times) parts << QString::number(time);
    return u"["_s + parts.join(u", "_s) + u"]"_s;
}

// Defaults of the production setup: 5 s threshold, 30 s repeat, ticks every second.
HangDetector detector(qint64 startMs = 0, qint64 thresholdMs = 5'000, qint64 repeatMs = 30'000, qint64 maxTickGapMs = 4'000,
                      qint64 maxRepeatMs = 30 * 60'000) {
    return HangDetector(startMs, thresholdMs, repeatMs, maxTickGapMs, maxRepeatMs);
}

// Ticks once a second from fromMs to toMs (both included) and returns the events with the time they came at.
Timed tickEverySecond(HangDetector& detector, qint64 fromMs, qint64 toMs) {
    Timed events;
    for (qint64 now = fromMs; now <= toMs; now += 1'000) {
        if (const auto event = detector.onTick(now)) events.emplace_back(now, *event);
    }
    return events;
}

std::vector<qint64> reportTimesUntil(HangDetector& detector, qint64 toMs, qint64 from = 1'000) {
    std::vector<qint64> times;
    for (const auto& [at, event] : tickEverySecond(detector, from, toMs)) times.push_back(at);
    return times;
}

Hung hung(qint64 silentMs, bool repeat) {
    return Hung{silentMs, repeat};
}

}  // namespace

// The detector has no clock: every call says what time it is, so none of these tests waits for anything.
class HangDetectorTest : public QObject {
    Q_OBJECT

private slots:
    void thereIsNoHangWhileBeatsArrive() {
        HangDetector d = detector();
        for (qint64 now = 1'000; now <= 120'000; now += 1'000) {
            d.onBeat(now);
            QVERIFY2(!d.onTick(now), qPrintable(u"at %1"_s.arg(now)));
        }
        QCOMPARE(d.gapsIgnored(), 0);
    }

    void aBeatEveryFewSecondsIsStillAnswering() {
        HangDetector d = detector();
        for (qint64 now = 1'000; now <= 60'000; now += 1'000) {
            if (now % 4'000 == 0) d.onBeat(now);  // silent for 3 s in between
            QVERIFY2(!d.onTick(now), qPrintable(u"at %1"_s.arg(now)));
        }
    }

    void exactlyTheThresholdIsNotYetAHangButOneMillisecondMoreIs() {
        HangDetector d = detector();
        QCOMPARE(show(tickEverySecond(d, 1'000, 5'000)), show(Timed{}));  // 5000 ms without a beat is still fine
        QCOMPARE(show(d.onTick(5'001)), show(HangEvent{hung(5'001, false)}));
    }

    void theFirstReportIsNotARepeat() {
        HangDetector d = detector();
        const Timed events = tickEverySecond(d, 1'000, 6'000);
        QCOMPARE(show(events), show(Timed{{6'000, hung(6'000, false)}}));
    }

    void aLastingHangIsReportedAgainOnlyAfterTheRepeatInterval() {
        HangDetector d = detector();
        const Timed events = tickEverySecond(d, 1'000, 100'000);
        QCOMPARE(show(events), show(Timed{
                                   {6'000, hung(6'000, false)},
                                   {36'000, hung(36'000, true)},  // 30 s after the first report
                                   {96'000, hung(96'000, true)},  // then 60 s
                               }));
    }

    void theRepeatIntervalDoublesFromThirtySecondsUpToThirtyMinutes() {
        HangDetector d = detector();
        const std::vector<qint64> times = reportTimesUntil(d, 5LL * 60 * 60 * 1'000);  // five hours
        std::vector<qint64> gapsInSeconds;
        for (size_t i = 1; i < times.size(); ++i) gapsInSeconds.push_back((times[i] - times[i - 1]) / 1'000);
        QCOMPARE(times.front(), qint64(6'000));  // the first report
        const std::vector<qint64> expected = {30, 60, 120, 240, 480, 960, 1'800, 1'800, 1'800, 1'800};
        QVERIFY2(gapsInSeconds.size() >= expected.size(), "enough reports");
        QCOMPARE(show(std::vector<qint64>(gapsInSeconds.begin(), gapsInSeconds.begin() + expected.size())), show(expected));
        for (const qint64 gap : gapsInSeconds) QVERIFY2(gap <= 1'800, "never more than 30 minutes apart");
    }

    void theLimitOfTheRepeatIntervalCanBeChosen() {
        HangDetector d = detector(0, 5'000, 10'000, 4'000, 25'000);
        const std::vector<qint64> times = reportTimesUntil(d, 200'000);
        std::vector<qint64> gaps;
        for (size_t i = 1; i < times.size() && gaps.size() < 5; ++i) gaps.push_back((times[i] - times[i - 1]) / 1'000);
        QCOMPARE(show(gaps), show(std::vector<qint64>{10, 20, 25, 25, 25}));
    }

    void aRecoveryStartsTheRepeatIntervalOverAgain() {
        HangDetector d = detector();
        const std::vector<qint64> before = reportTimesUntil(d, 1'000'000);  // well into the 480 s steps
        QVERIFY2(before.size() >= 6, qPrintable(show(before)));

        d.onBeat(1'000'500);
        QCOMPARE(show(d.onTick(1'001'000)), show(HangEvent{Recovered{1'000'500}}));

        // The new hang counts from the beat at 1000.5 s: reported at 1006 s, then 30 s later, then 60 s later.
        QCOMPARE(show(reportTimesUntil(d, 1'120'000, 1'002'000)), show(std::vector<qint64>{1'006'000, 1'036'000, 1'096'000}));
    }

    void aGapThatDropsTheHangStartsTheRepeatIntervalOverAgain() {
        HangDetector d = detector();
        reportTimesUntil(d, 100'000);  // reports at 6, 36 and 96 s: the next gap would be 120 s
        QVERIFY(!d.onTick(500'000));   // asleep: the hang is forgotten

        const std::vector<qint64> after = reportTimesUntil(d, 600'000, 501'000);

        QCOMPARE(show(after), show(std::vector<qint64>{506'000, 536'000, 596'000}));
    }

    void aTickThatTookLongIsNotAGapOnceItsEndIsReported() {
        HangDetector d = detector();
        QCOMPARE(show(tickEverySecond(d, 1'000, 6'000)), show(Timed{{6'000, hung(6'000, false)}}));

        d.onTickFinished(40'000);  // the dump of that tick took 34 s

        QCOMPARE(show(d.onTick(41'000)), show(HangEvent{hung(41'000, true)}));  // the hang is still there: the repeat is due
        QCOMPARE(d.gapsIgnored(), 0);
    }

    void withoutTheEndOfTheSlowTickTheNextOneLooksLikeAGap() {
        HangDetector d = detector();
        tickEverySecond(d, 1'000, 6'000);

        QVERIFY(!d.onTick(41'000));
        QCOMPARE(d.gapsIgnored(), 1);
    }

    void theEndOfATickMovesTheGapBaselineOnly() {
        HangDetector d = detector();
        tickEverySecond(d, 1'000, 3'000);
        d.onBeat(3'000);

        d.onTickFinished(3'500);

        // 3.5 s after the end of the last tick: not a gap, and 4 s without a beat is fine
        QVERIFY(!d.onTick(7'000));
        QCOMPARE(d.gapsIgnored(), 0);
        // the silence still counts from the last beat
        QCOMPARE(show(d.onTick(9'000)), show(HangEvent{hung(6'000, false)}));
    }

    void theRepeatComesExactlyRepeatMsAfterTheLastReport() {
        HangDetector d = detector();
        tickEverySecond(d, 1'000, 5'000);
        QCOMPARE(show(d.onTick(5'001)), show(HangEvent{hung(5'001, false)}));
        for (qint64 now = 6'001; now <= 34'001; now += 1'000) QVERIFY2(!d.onTick(now), qPrintable(u"at %1"_s.arg(now)));

        QVERIFY2(!d.onTick(35'000), "29999 ms after the report");
        QCOMPARE(show(d.onTick(35'001)), show(HangEvent{hung(35'001, true)}));
    }

    void aLateBeatIsReportedOnceWithTheWholeSilentTime() {
        HangDetector d = detector();
        d.onBeat(1'000);
        const Timed hungEvents = tickEverySecond(d, 2'000, 8'000);
        QCOMPARE(show(hungEvents), show(Timed{{7'000, hung(6'000, false)}}));

        d.onBeat(8'500);  // the thread came back after 7.5 s without a beat
        QCOMPARE(show(d.onTick(9'000)), show(HangEvent{Recovered{7'500}}));
        for (qint64 now = 10'000; now <= 60'000; now += 1'000) {
            d.onBeat(now);
            QVERIFY2(!d.onTick(now), qPrintable(u"nothing more once it is over (at %1)"_s.arg(now)));
        }
    }

    void aNewHangAfterTheRecoveryIsAFirstReportAgain() {
        HangDetector d = detector();
        tickEverySecond(d, 1'000, 6'000);
        d.onBeat(7'000);
        QCOMPARE(show(d.onTick(7'000)), show(HangEvent{Recovered{7'000}}));

        const Timed events = tickEverySecond(d, 8'000, 13'000);

        QCOMPARE(show(events), show(Timed{{13'000, hung(6'000, false)}}));
    }

    void aBeatBeforeTheReportMeansNoRecoveryEvent() {
        HangDetector d = detector();
        tickEverySecond(d, 1'000, 4'000);
        d.onBeat(4'500);  // late, but not late enough to have been reported

        QCOMPARE(show(tickEverySecond(d, 5'000, 9'000)), show(Timed{}));
    }

    void aBeatThatArrivesExactlyWhenTheThresholdPassesPreventsTheReport() {
        HangDetector d = detector();
        tickEverySecond(d, 1'000, 5'000);

        d.onBeat(5'001);

        QVERIFY(!d.onTick(5'001));
        QCOMPARE(show(tickEverySecond(d, 6'001, 10'000)), show(Timed{}));
    }

    void aBeatRightAfterTheReportMakesTheNextTickARecovery() {
        HangDetector d = detector();
        tickEverySecond(d, 1'000, 5'000);
        QCOMPARE(show(d.onTick(5'001)), show(HangEvent{hung(5'001, false)}));

        d.onBeat(5'001);

        QCOMPARE(show(d.onTick(6'001)), show(HangEvent{Recovered{5'001}}));
    }

    void aGapBetweenTicksResetsTheBaselineAndReportsNothing() {
        HangDetector d = detector();
        tickEverySecond(d, 1'000, 3'000);
        d.onBeat(3'000);

        // The machine slept: without the reset this would be 17 s without a beat.
        QVERIFY(!d.onTick(20'000));
        QCOMPARE(d.gapsIgnored(), 1);
        QVERIFY(!d.onTick(21'000));
    }

    void aRealHangAfterAResetIsDetectedFromTheNewBaseline() {
        HangDetector d = detector();
        tickEverySecond(d, 1'000, 3'000);
        QVERIFY(!d.onTick(20'000));  // reset: the baseline is 20000 now

        // 5 s after the baseline is not more than the threshold
        QCOMPARE(show(tickEverySecond(d, 21'000, 25'000)), show(Timed{}));
        QCOMPARE(show(d.onTick(26'000)), show(HangEvent{hung(6'000, false)}));
    }

    void aGapOfExactlyTheLimitIsNotIgnored() {
        HangDetector d = detector(0, 2'000, 30'000, 4'000);

        QCOMPARE(show(d.onTick(4'000)), show(HangEvent{hung(4'000, false)}));
        QCOMPARE(d.gapsIgnored(), 0);
    }

    void aGapOneMillisecondOverTheLimitIsIgnored() {
        HangDetector d = detector(0, 2'000, 30'000, 4'000);

        QVERIFY(!d.onTick(4'001));
        QCOMPARE(d.gapsIgnored(), 1);
    }

    void everyIgnoredGapIsCounted() {
        HangDetector d = detector();

        QVERIFY(!d.onTick(10'000));
        QVERIFY(!d.onTick(30'000));
        QVERIFY(!d.onTick(31'000));
        QVERIFY(!d.onTick(100'000));

        QCOMPARE(d.gapsIgnored(), 3);
    }

    void aGapDuringAReportedHangForgetsTheHangWithoutARecovery() {
        HangDetector d = detector();
        QCOMPARE(show(tickEverySecond(d, 1'000, 6'000)), show(Timed{{6'000, hung(6'000, false)}}));

        QVERIFY(!d.onTick(60'000));  // asleep while it hung
        d.onBeat(60'500);

        QVERIFY2(!d.onTick(61'000), "no Recovered: the hang was dropped by the reset, not ended");
        QCOMPARE(show(tickEverySecond(d, 62'000, 65'000)), show(Timed{}));
    }

    void theBaselineIsTheStartTimeUntilTheFirstBeat() {
        HangDetector d = detector(100'000);

        QCOMPARE(show(tickEverySecond(d, 101'000, 105'000)), show(Timed{}));
        QCOMPARE(show(d.onTick(105'001)), show(HangEvent{hung(5'001, false)}));
    }

    void theLimitsMustBePositive() {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, detector(0, 0));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, detector(0, 5'000, 0));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, detector(0, 5'000, 30'000, 0));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, detector(0, 5'000, 30'000, 4'000, 29'999));
    }
};

QTEST_GUILESS_MAIN(HangDetectorTest)
#include "tst_hang_detector.moc"

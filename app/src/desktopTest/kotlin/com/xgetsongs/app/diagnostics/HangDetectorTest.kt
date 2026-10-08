package com.xgetsongs.app.diagnostics

import com.xgetsongs.app.diagnostics.HangEvent.Hung
import com.xgetsongs.app.diagnostics.HangEvent.Recovered
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNull
import kotlin.test.assertTrue

// The detector has no clock: every call says what time it is, so none of these tests waits for anything.
class HangDetectorTest {
    /** Defaults of the production setup: 5 s threshold, 30 s repeat, ticks every second. */
    private fun detector(
        startMs: Long = 0,
        thresholdMs: Long = 5_000,
        repeatMs: Long = 30_000,
        maxTickGapMs: Long = 4_000,
        maxRepeatMs: Long = 30 * 60_000,
    ) = HangDetector(startMs, thresholdMs, repeatMs, maxTickGapMs, maxRepeatMs)

    /** Ticks once a second from [fromMs] to [toMs] (both included) and returns the events with the time they came at. */
    private fun HangDetector.tickEverySecond(fromMs: Long, toMs: Long): List<Pair<Long, HangEvent>> {
        val events = mutableListOf<Pair<Long, HangEvent>>()
        var now = fromMs
        while (now <= toMs) {
            onTick(now)?.let { events += now to it }
            now += 1_000
        }
        return events
    }

    @Test
    fun thereIsNoHangWhileBeatsArrive() {
        val detector = detector()

        for (now in 1_000L..120_000L step 1_000L) {
            detector.onUiBeat(now)
            assertNull(detector.onTick(now), "at $now")
        }
        assertEquals(0, detector.gapsIgnored)
    }

    @Test
    fun aBeatEveryFewSecondsIsStillAnswering() {
        val detector = detector()

        for (now in 1_000L..60_000L step 1_000L) {
            if (now % 4_000L == 0L) detector.onUiBeat(now) // silent for 3 s in between
            assertNull(detector.onTick(now), "at $now")
        }
    }

    @Test
    fun exactlyTheThresholdIsNotYetAHangButOneMillisecondMoreIs() {
        val detector = detector()

        assertEquals(emptyList(), detector.tickEverySecond(1_000, 5_000), "5000 ms without a beat is still fine")
        assertEquals(Hung(5_001, repeat = false), detector.onTick(5_001))
    }

    @Test
    fun theFirstReportIsNotARepeat() {
        val events = detector().tickEverySecond(1_000, 6_000)

        assertEquals(listOf<Pair<Long, HangEvent>>(6_000L to Hung(6_000, repeat = false)), events)
    }

    @Test
    fun aLastingHangIsReportedAgainOnlyAfterTheRepeatInterval() {
        val events = detector().tickEverySecond(1_000, 100_000)

        assertEquals(
            listOf<Pair<Long, HangEvent>>(
                6_000L to Hung(6_000, repeat = false),
                36_000L to Hung(36_000, repeat = true), // 30 s after the first report
                96_000L to Hung(96_000, repeat = true), // then 60 s
            ),
            events,
        )
    }

    private fun HangDetector.reportTimesUntil(toMs: Long, from: Long = 1_000): List<Long> = tickEverySecond(from, toMs).map { it.first }

    @Test
    fun theRepeatIntervalDoublesFromThirtySecondsUpToThirtyMinutes() {
        val times = detector().reportTimesUntil(5 * 60 * 60 * 1_000L) // five hours

        val gapsInSeconds = times.zipWithNext { a, b -> (b - a) / 1_000 }
        assertEquals(6_000L, times.first(), "the first report")
        assertEquals(listOf(30L, 60, 120, 240, 480, 960, 1_800, 1_800, 1_800, 1_800), gapsInSeconds.take(10))
        assertTrue(gapsInSeconds.all { it <= 1_800 }, "never more than 30 minutes apart")
    }

    @Test
    fun theLimitOfTheRepeatIntervalCanBeChosen() {
        val times = detector(repeatMs = 10_000, maxRepeatMs = 25_000).reportTimesUntil(200_000)

        assertEquals(listOf(10L, 20, 25, 25, 25), times.zipWithNext { a, b -> (b - a) / 1_000 }.take(5))
    }

    @Test
    fun aRecoveryStartsTheRepeatIntervalOverAgain() {
        val detector = detector()
        val before = detector.reportTimesUntil(1_000_000) // well into the 480 s steps
        assertTrue(before.size >= 6, before.toString())

        detector.onUiBeat(1_000_500)
        assertEquals(Recovered(1_000_500), detector.onTick(1_001_000))

        // The new hang counts from the beat at 1000.5 s: reported at 1006 s, then 30 s later, then 60 s later.
        assertEquals(listOf(1_006_000L, 1_036_000L, 1_096_000L), detector.reportTimesUntil(1_120_000, from = 1_002_000))
    }

    @Test
    fun aGapThatDropsTheHangStartsTheRepeatIntervalOverAgain() {
        val detector = detector()
        detector.reportTimesUntil(100_000) // reports at 6, 36 and 96 s: the next gap would be 120 s
        assertNull(detector.onTick(500_000)) // asleep: the hang is forgotten

        val after = detector.tickEverySecond(501_000, 600_000).map { it.first }

        assertEquals(listOf(506_000L, 536_000L, 596_000L), after)
    }

    @Test
    fun aTickThatTookLongIsNotAGapOnceItsEndIsReported() {
        val detector = detector()
        assertEquals(listOf<Pair<Long, HangEvent>>(6_000L to Hung(6_000, repeat = false)), detector.tickEverySecond(1_000, 6_000))

        detector.onTickFinished(40_000) // the dump of that tick took 34 s

        assertEquals(Hung(41_000, repeat = true), detector.onTick(41_000), "the hang is still there: the repeat is due")
        assertEquals(0, detector.gapsIgnored)
    }

    @Test
    fun withoutTheEndOfTheSlowTickTheNextOneLooksLikeAGap() {
        val detector = detector()
        detector.tickEverySecond(1_000, 6_000)

        assertNull(detector.onTick(41_000))
        assertEquals(1, detector.gapsIgnored)
    }

    @Test
    fun theEndOfATickMovesTheGapBaselineOnly() {
        val detector = detector()
        detector.tickEverySecond(1_000, 3_000)
        detector.onUiBeat(3_000)

        detector.onTickFinished(3_500)

        assertNull(detector.onTick(7_000), "3.5 s after the end of the last tick: not a gap, and 4 s without a beat is fine")
        assertEquals(0, detector.gapsIgnored)
        assertEquals(Hung(6_000, repeat = false), detector.onTick(9_000), "the silence still counts from the last beat")
    }

    @Test
    fun theRepeatComesExactlyRepeatMsAfterTheLastReport() {
        val detector = detector()
        detector.tickEverySecond(1_000, 5_000)
        assertEquals(Hung(5_001, repeat = false), detector.onTick(5_001))
        for (now in 6_001L..34_001L step 1_000L) assertNull(detector.onTick(now), "at $now")

        assertNull(detector.onTick(35_000), "29999 ms after the report")
        assertEquals(Hung(35_001, repeat = true), detector.onTick(35_001))
    }

    @Test
    fun aLateBeatIsReportedOnceWithTheWholeSilentTime() {
        val detector = detector()
        detector.onUiBeat(1_000)
        val hung = detector.tickEverySecond(2_000, 8_000)
        assertEquals(listOf<Pair<Long, HangEvent>>(7_000L to Hung(6_000, repeat = false)), hung)

        detector.onUiBeat(8_500) // the UI thread came back after 7.5 s without a beat
        assertEquals(Recovered(7_500), detector.onTick(9_000))
        for (now in 10_000L..60_000L step 1_000L) {
            detector.onUiBeat(now)
            assertNull(detector.onTick(now), "nothing more once it is over (at $now)")
        }
    }

    @Test
    fun aNewHangAfterTheRecoveryIsAFirstReportAgain() {
        val detector = detector()
        detector.tickEverySecond(1_000, 6_000)
        detector.onUiBeat(7_000)
        assertEquals(Recovered(7_000), detector.onTick(7_000))

        val events = detector.tickEverySecond(8_000, 13_000)

        assertEquals(listOf<Pair<Long, HangEvent>>(13_000L to Hung(6_000, repeat = false)), events)
    }

    @Test
    fun aBeatBeforeTheReportMeansNoRecoveryEvent() {
        val detector = detector()
        detector.tickEverySecond(1_000, 4_000)
        detector.onUiBeat(4_500) // late, but not late enough to have been reported

        assertEquals(emptyList(), detector.tickEverySecond(5_000, 9_000))
    }

    @Test
    fun aBeatThatArrivesExactlyWhenTheThresholdPassesPreventsTheReport() {
        val detector = detector()
        detector.tickEverySecond(1_000, 5_000)

        detector.onUiBeat(5_001)

        assertNull(detector.onTick(5_001))
        assertEquals(emptyList(), detector.tickEverySecond(6_001, 10_000))
    }

    @Test
    fun aBeatRightAfterTheReportMakesTheNextTickARecovery() {
        val detector = detector()
        detector.tickEverySecond(1_000, 5_000)
        assertEquals(Hung(5_001, repeat = false), detector.onTick(5_001))

        detector.onUiBeat(5_001)

        assertEquals(Recovered(5_001), detector.onTick(6_001))
    }

    @Test
    fun aGapBetweenTicksResetsTheBaselineAndReportsNothing() {
        val detector = detector()
        detector.tickEverySecond(1_000, 3_000)
        detector.onUiBeat(3_000)

        // The machine slept: without the reset this would be 17 s without a beat.
        assertNull(detector.onTick(20_000))
        assertEquals(1, detector.gapsIgnored)
        assertNull(detector.onTick(21_000))
    }

    @Test
    fun aRealHangAfterAResetIsDetectedFromTheNewBaseline() {
        val detector = detector()
        detector.tickEverySecond(1_000, 3_000)
        assertNull(detector.onTick(20_000)) // reset: the baseline is 20000 now

        assertEquals(emptyList(), detector.tickEverySecond(21_000, 25_000), "5 s after the baseline is not more than the threshold")
        assertEquals(Hung(6_000, repeat = false), detector.onTick(26_000))
    }

    @Test
    fun aGapOfExactlyTheLimitIsNotIgnored() {
        val detector = detector(thresholdMs = 2_000, maxTickGapMs = 4_000)

        assertEquals(Hung(4_000, repeat = false), detector.onTick(4_000))
        assertEquals(0, detector.gapsIgnored)
    }

    @Test
    fun aGapOneMillisecondOverTheLimitIsIgnored() {
        val detector = detector(thresholdMs = 2_000, maxTickGapMs = 4_000)

        assertNull(detector.onTick(4_001))
        assertEquals(1, detector.gapsIgnored)
    }

    @Test
    fun everyIgnoredGapIsCounted() {
        val detector = detector()

        assertNull(detector.onTick(10_000))
        assertNull(detector.onTick(30_000))
        assertNull(detector.onTick(31_000))
        assertNull(detector.onTick(100_000))

        assertEquals(3, detector.gapsIgnored)
    }

    @Test
    fun aGapDuringAReportedHangForgetsTheHangWithoutARecovery() {
        val detector = detector()
        assertEquals(listOf<Pair<Long, HangEvent>>(6_000L to Hung(6_000, repeat = false)), detector.tickEverySecond(1_000, 6_000))

        assertNull(detector.onTick(60_000)) // asleep while it hung
        detector.onUiBeat(60_500)

        assertNull(detector.onTick(61_000), "no Recovered: the hang was dropped by the reset, not ended")
        assertEquals(emptyList(), detector.tickEverySecond(62_000, 65_000))
    }

    @Test
    fun theBaselineIsTheStartTimeUntilTheFirstBeat() {
        val detector = detector(startMs = 100_000)

        assertEquals(emptyList(), detector.tickEverySecond(101_000, 105_000))
        assertEquals(Hung(5_001, repeat = false), detector.onTick(105_001))
    }

    @Test
    fun theLimitsMustBePositive() {
        assertFailsWith<IllegalArgumentException> { detector(thresholdMs = 0) }
        assertFailsWith<IllegalArgumentException> { detector(repeatMs = 0) }
        assertFailsWith<IllegalArgumentException> { detector(maxTickGapMs = 0) }
        assertFailsWith<IllegalArgumentException> { detector(repeatMs = 30_000, maxRepeatMs = 29_999) }
    }
}

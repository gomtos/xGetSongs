package com.xgetsongs.app.diagnostics

/** What [HangDetector.onTick] has to tell about the UI thread. */
internal sealed interface HangEvent {
    /** No beat for [silentMs]; [repeat] is false for the first report of a hang and true for the later ones. */
    data class Hung(val silentMs: Long, val repeat: Boolean) : HangEvent

    /** The UI thread beat again after a reported hang; [totalMs] is the time from its last beat before the hang to the first one after. */
    data class Recovered(val totalMs: Long) : HangEvent
}

/**
 * Decides from two kinds of calls whether the UI thread has stopped answering. [onUiBeat] is what a task posted to the UI
 * thread calls when it runs; [onTick] is what the watchdog thread calls once per interval. The detector owns neither a
 * thread nor a clock: every call says what time it is ([startMs] says it for the creation), so it can be tested without
 * waiting. The calls may come from different threads.
 *
 * A hang is reported when the last beat is more than [thresholdMs] ago. While it lasts the report is repeated, first
 * [repeatMs] after the first report, and then at intervals that double each time up to [maxRepeatMs] (a hang that goes on
 * for hours must not fill the disk); a recovery starts the sequence over.
 *
 * If two ticks are more than [maxTickGapMs] apart the machine was probably asleep or the watchdog thread itself starved,
 * so the silence proves nothing: the baseline moves to that tick, any hang that was being reported is dropped without a
 * [HangEvent.Recovered], and the tick reports nothing ([gapsIgnored] counts them). The gap is measured from the end of the
 * previous tick when the caller says when that was ([onTickFinished]), so a tick that took long (it writes a dump) does
 * not count as a gap by itself.
 *
 * [maxTickGapMs] is 4 times the default one second between ticks.
 */
internal class HangDetector(
    startMs: Long,
    private val thresholdMs: Long = 5_000,
    private val repeatMs: Long = 30_000,
    private val maxTickGapMs: Long = 4_000,
    private val maxRepeatMs: Long = 30 * 60_000,
) {
    init {
        require(thresholdMs > 0) { "thresholdMs must be positive" }
        require(repeatMs > 0) { "repeatMs must be positive" }
        require(maxTickGapMs > 0) { "maxTickGapMs must be positive" }
        require(maxRepeatMs >= repeatMs) { "maxRepeatMs must not be less than repeatMs" }
    }

    private var lastBeatMs = startMs
    private var lastTickMs = startMs

    /** The last beat before the hang that is being reported, or null while the UI thread is not known to be hung. */
    private var hangFromMs: Long? = null
    private var lastReportMs = 0L

    /** How long after the last report the next one is due. */
    private var repeatDueInMs = repeatMs

    /** The wait that follows the next report: it doubles with every report of the hang, up to [maxRepeatMs]. */
    private var followingRepeatInMs = repeatMs

    /** The time of the first beat after the reported hang, once there was one and the tick has not said so yet. */
    private var recoveredAtMs: Long? = null

    /** How many ticks were skipped because of a gap (see the class comment). */
    @Volatile
    var gapsIgnored = 0
        private set

    @Synchronized
    fun onUiBeat(nowMs: Long) {
        if (hangFromMs != null && recoveredAtMs == null) recoveredAtMs = nowMs
        lastBeatMs = nowMs
    }

    @Synchronized
    fun onTick(nowMs: Long): HangEvent? {
        val gap = nowMs - lastTickMs
        lastTickMs = nowMs
        if (gap > maxTickGapMs) {
            gapsIgnored++
            lastBeatMs = nowMs
            forgetHang()
            return null
        }

        val hangFrom = hangFromMs
        val recoveredAt = recoveredAtMs
        if (hangFrom != null && recoveredAt != null) {
            forgetHang()
            return HangEvent.Recovered(recoveredAt - hangFrom)
        }

        val silentMs = nowMs - lastBeatMs
        if (silentMs <= thresholdMs) return null
        if (hangFrom == null) {
            hangFromMs = lastBeatMs
            report(nowMs)
            return HangEvent.Hung(silentMs, repeat = false)
        }
        if (nowMs - lastReportMs >= repeatDueInMs) {
            report(nowMs)
            return HangEvent.Hung(silentMs, repeat = true)
        }
        return null
    }

    /**
     * Says when the work of the last [onTick] ended; the next tick's gap counts from here. Without it the gap counts from
     * the start of the last tick.
     */
    @Synchronized
    fun onTickFinished(nowMs: Long) {
        lastTickMs = nowMs
    }

    private fun report(nowMs: Long) {
        lastReportMs = nowMs
        repeatDueInMs = followingRepeatInMs
        followingRepeatInMs = minOf(followingRepeatInMs * 2, maxRepeatMs)
    }

    private fun forgetHang() {
        hangFromMs = null
        recoveredAtMs = null
        repeatDueInMs = repeatMs
        followingRepeatInMs = repeatMs
    }
}

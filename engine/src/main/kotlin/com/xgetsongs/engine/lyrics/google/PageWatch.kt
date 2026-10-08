package com.xgetsongs.engine.lyrics.google

import kotlinx.serialization.Serializable
import kotlin.time.Duration
import kotlin.time.TimeMark
import kotlin.time.TimeSource

/** What one look at the page in the browser saw. [bodyText] is the first part of the visible text. */
@Serializable
internal class PageProbe(val url: String, val readyState: String, val hasCard: Boolean, val bodyText: String)

/**
 * Decides when to stop waiting for a page. Done when the page shows a lyrics card or a block page (it will not change),
 * or when it is complete and has stayed so, at the same address, for [settleTime]: Google loads its result page in two
 * steps, so a page that is complete for a moment is not yet the final one. Loading again or a new address starts that
 * quiet time over.
 */
internal class PageWatch(private val settleTime: Duration, private val timeSource: TimeSource) {
    private var watchedUrl: String? = null
    private var completeSince: TimeMark? = null

    fun isDone(probe: PageProbe): Boolean {
        if (probe.hasCard) return true
        if (PageClassifier.blockOf(probe.url, null, probe.bodyText) != null) return true
        if (probe.readyState != "complete") {
            completeSince = null
            return false
        }
        if (probe.url != watchedUrl) {
            watchedUrl = probe.url
            completeSince = null
        }
        val since = completeSince ?: timeSource.markNow().also { completeSince = it }
        return since.elapsedNow() >= settleTime
    }
}

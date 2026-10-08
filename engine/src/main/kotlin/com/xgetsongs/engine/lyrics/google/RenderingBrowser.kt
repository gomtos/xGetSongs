package com.xgetsongs.engine.lyrics.google

import kotlin.time.Duration

/** A browser that can open an address and hand back the finished page. */
internal interface RenderingBrowser {
    /**
     * Opens [url], waits until the page shows a lyrics card or a block page or has settled without one (but at most
     * [timeout]), and returns it. Never throws, except for a `CancellationException` when the calling coroutine is
     * cancelled; a browser that cannot be used answers [RenderResult.Unavailable].
     */
    suspend fun render(url: String, timeout: Duration): RenderResult
}

internal sealed interface RenderResult {
    /** The page as it stands: its final address, its HTML and the first part of its visible text. */
    class Loaded(val url: String, val html: String, val bodyText: String) : RenderResult

    /** The page did not settle within the time limit. */
    data object TimedOut : RenderResult

    /** The browser cannot be used; [reason] is the name of what went wrong (an exception class, say). */
    class Unavailable(val reason: String) : RenderResult
}

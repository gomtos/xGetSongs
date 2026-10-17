package com.xgetsongs.engine.lyrics.google

/**
 * What the Google lyrics lookup needs from a hidden browser, and nothing more: open an address, run a script in the page, go back
 * to the blank page. Everything that depends on how Google writes its pages (what to look for, when the page is done) is in
 * [DrivenBrowser], on this side. [BrowserChannel] implements it for the desktop shell's WebView2.
 */
interface PageDriver {
    /** Opens [url] (making the window when there is none) and returns once the load has started. */
    suspend fun load(url: String)

    /** The text [script] gave, or null when it gave nothing. Throws [BrowserUnavailableException] when the browser could not run it. */
    suspend fun evaluate(script: String): String?

    /** Back to `about:blank`, so that nothing of the last page stays loaded. The window stays. */
    suspend fun reset()
}

/** The browser cannot be used; [reason] is a short name of why (`no-agent`, `no-answer`, `closed`, or what the browser reported). */
class BrowserUnavailableException(val reason: String) : Exception(reason)

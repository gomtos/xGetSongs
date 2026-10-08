package com.xgetsongs.engine.lyrics.google

/** What one trip to Google's lyrics card came to. A lookup that goes wrong is a result, never an exception. */
internal sealed interface GoogleLyricsResult {
    /** The card was read. [lyrics] is tidy text: lines joined by `\n`, paragraphs by one blank line. */
    class Found(val lyrics: String, val lineCount: Int, val paragraphCount: Int) : GoogleLyricsResult {
        // Lyrics are a copyrighted work: whatever prints a result (a log line, a failed test) must not print them.
        override fun toString() = "Found(lines=$lineCount, paragraphs=$paragraphCount)"
    }

    /** The page loaded and has no lyrics card. */
    data object NoCard : GoogleLyricsResult

    /** The page has a lyrics card but the lines could not be read from it: Google probably changed its markup. */
    data object ExtractionFailed : GoogleLyricsResult

    /** Google's block page (CAPTCHA, "unusual traffic"). */
    data object Captcha : GoogleLyricsResult

    /** Google's consent page. */
    data object Consent : GoogleLyricsResult

    /** HTTP 429 or a page that says "too many requests". */
    data object RateLimited : GoogleLyricsResult

    /** The page did not settle in time. */
    data object Timeout : GoogleLyricsResult

    /** The web view cannot be used (JavaFX is missing or failed to start). [reason] is the name of what went wrong. */
    class BrowserUnavailable(val reason: String) : GoogleLyricsResult {
        override fun toString() = "BrowserUnavailable($reason)"
    }

    /** Google is being left alone after trouble: no request was made. */
    data object CoolingDown : GoogleLyricsResult
}

/** Where the lookup says what it did. The text holds counts and names of results, never a song or lyrics. */
interface GoogleLyricsLog {
    fun info(message: String)

    fun warn(message: String)

    companion object {
        val None: GoogleLyricsLog = object : GoogleLyricsLog {
            override fun info(message: String) = Unit

            override fun warn(message: String) = Unit
        }
    }
}

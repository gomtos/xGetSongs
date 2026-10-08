package com.xgetsongs.engine.integration

import com.xgetsongs.engine.lyrics.google.FxWebViewBrowser
import com.xgetsongs.engine.lyrics.google.GoogleLyricsFetcher
import com.xgetsongs.engine.lyrics.google.GoogleLyricsLog
import com.xgetsongs.engine.lyrics.google.GoogleLyricsResult
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.jupiter.api.Assumptions.assumeTrue
import org.junit.jupiter.api.Tag
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertTrue
import kotlin.test.fail

/**
 * Asks the real Google, through the real hidden web view, for the lyrics card of one song, so it is excluded from `test`
 * and only runs through `integrationTest`. One request, no retry. Lyrics are copyrighted works: this test prints and
 * asserts STRUCTURE only (result name, counts of lines and paragraphs), never a word of what Google returns.
 *
 * If Google serves a block page (CAPTCHA, consent, 429), the web view times out or JavaFX is unavailable, the test is
 * skipped, not failed, and nothing tries again. If the page loads but the card is not read (`NoCard`,
 * `ExtractionFailed`), Google has probably changed its markup: fix `GoogleLyricsSelectors`.
 */
@Tag("integration")
class RealGoogleLyricsIntegrationTest {
    private val browser = FxWebViewBrowser()

    private val log = object : GoogleLyricsLog {
        override fun info(message: String) = println("log: $message")

        override fun warn(message: String) = println("log WARN: $message")
    }

    @AfterTest
    fun shutDown() = browser.close()

    @Test
    fun readsTheLyricsCardOfTheTestQuery(): Unit = runBlocking {
        withTimeout(120_000) {
            val result = GoogleLyricsFetcher(browser, log = log).fetch("RESCENE (리센느)", "LOVE ATTACK")

            println("google lyrics card: $result")
            when (result) {
                is GoogleLyricsResult.Found -> {
                    assertTrue(result.lineCount >= 10, "at least 10 lines expected, got ${result.lineCount}")
                    assertTrue(result.paragraphCount >= 2, "at least 2 paragraphs expected, got ${result.paragraphCount}")
                }
                GoogleLyricsResult.NoCard, GoogleLyricsResult.ExtractionFailed ->
                    fail("the page loaded but the lyrics card was not read ($result): has Google changed its markup? see GoogleLyricsSelectors")
                else -> assumeTrue(false, "Google did not serve the page, or the web view is unavailable: $result")
            }
        }
    }
}

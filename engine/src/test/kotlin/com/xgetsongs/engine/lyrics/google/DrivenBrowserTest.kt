package com.xgetsongs.engine.lyrics.google

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertIs
import kotlin.test.assertTrue
import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TestTimeSource

class DrivenBrowserTest {
    private val url = "https://www.google.com/search?q=x&hl=en"
    private val time = TestTimeSource()

    /** Answers the probe from [probes] (the last one repeats) and the page's HTML with [html]; every look takes half a second. */
    private inner class FakeDriver(private vararg val probes: String?, private val html: String? = "<html>page</html>") : PageDriver {
        val calls = mutableListOf<String>()
        var loadFailure: Exception? = null

        /** Thrown by the look at the page (the probe) when set. */
        var probeFailure: Exception? = null

        /** Thrown by the read of the page's HTML when set. */
        var htmlFailure: Exception? = null
        private var looks = 0

        override suspend fun load(url: String) {
            calls += "load $url"
            loadFailure?.let { throw it }
        }

        override suspend fun evaluate(script: String): String? {
            if ("outerHTML" in script) {
                calls += "html"
                htmlFailure?.let { throw it }
                return html
            }
            calls += "probe"
            time += 500.milliseconds
            probeFailure?.let { throw it }
            return probes[minOf(looks++, probes.size - 1)]
        }

        override suspend fun reset() {
            calls += "reset"
        }
    }

    private fun probe(url: String = this.url, readyState: String = "complete", hasCard: Boolean = false, bodyText: String = "") =
        """{"url":"$url","readyState":"$readyState","hasCard":$hasCard,"bodyText":"$bodyText"}"""

    private fun browser(driver: PageDriver) = DrivenBrowser(driver, pollInterval = 500.milliseconds, settleTime = 4.seconds, timeSource = time)

    private suspend fun TestScope.render(driver: PageDriver, timeout: Duration = 20.seconds) = browser(driver).render(url, timeout)

    @Test
    fun aPageThatShowsACardIsReturnedWithItsHtml() = runTest {
        val driver = FakeDriver(probe(hasCard = true, bodyText = "text"), html = "<html>card</html>")

        val result = render(driver)

        assertIs<RenderResult.Loaded>(result)
        assertEquals(url, result.url)
        assertEquals("<html>card</html>", result.html)
        assertEquals("text", result.bodyText)
    }

    @Test
    fun aCompletePageThatStaysTheSameForTheSettleTimeIsReturned() = runTest {
        val driver = FakeDriver(probe(readyState = "loading"), probe(), probe(), probe())

        val result = render(driver)

        assertIs<RenderResult.Loaded>(result)
        // Complete from the second look on: four seconds of looks every half second must pass before it counts as settled.
        assertTrue(driver.calls.count { it == "probe" } >= 10, "${driver.calls}")
    }

    @Test
    fun aProbeOfTheBlankPageIsIgnored() = runTest {
        val driver = FakeDriver(probe(url = "about:blank", hasCard = true), probe(hasCard = true, bodyText = "real"))

        val result = render(driver)

        assertIs<RenderResult.Loaded>(result)
        assertEquals("real", result.bodyText)
    }

    @Test
    fun aProbeThatIsNullOrNotJsonKeepsWaiting() = runTest {
        val driver = FakeDriver(null, "this is not json", probe(hasCard = true))

        assertIs<RenderResult.Loaded>(render(driver))
    }

    @Test
    fun aPageThatNeverSettlesTimesOut() = runTest {
        val driver = FakeDriver(probe(readyState = "loading"))

        assertEquals(RenderResult.TimedOut, render(driver, timeout = 10.seconds))
    }

    @Test
    fun aDriverThatIsUnavailableGivesUnavailableWithItsReason() = runTest {
        val noAgent = FakeDriver(probe()).apply { loadFailure = BrowserUnavailableException("no-agent") }
        val broken = FakeDriver(probe()).apply { loadFailure = IllegalStateException("boom") }

        assertEquals("no-agent", assertIs<RenderResult.Unavailable>(render(noAgent)).reason)
        assertEquals("IllegalStateException", assertIs<RenderResult.Unavailable>(render(broken)).reason)
    }

    @Test
    fun aLookAtThePageThatFailsEndsTheRenderAsUnavailableAtOnce() = runTest {
        // The browser could not run the script (its window is gone, the page does not answer): waiting on it for the rest of the time
        // limit would end as a timeout, which is held against Google.
        val driver = FakeDriver(probe()).apply { probeFailure = BrowserUnavailableException("script timed out") }

        val result = render(driver)

        assertEquals("script timed out", assertIs<RenderResult.Unavailable>(result).reason)
        assertEquals(1, driver.calls.count { it == "probe" }, "no second look: ${driver.calls}")
    }

    @Test
    fun aReadOfThePagesHtmlThatFailsIsNotAnEmptyPage() = runTest {
        // An empty page would be read as "no lyrics card" and remembered for the song, although the page had shown its card.
        val driver = FakeDriver(probe(hasCard = true)).apply { htmlFailure = BrowserUnavailableException("script timed out") }

        val result = render(driver)

        assertEquals("script timed out", assertIs<RenderResult.Unavailable>(result).reason)
    }

    @Test
    fun aPageThatGivesNoHtmlIsUnavailableNotAnEmptyPage() = runTest {
        val driver = FakeDriver(probe(hasCard = true), html = null)

        assertEquals("no-html", assertIs<RenderResult.Unavailable>(render(driver)).reason)
    }

    @Test
    fun theBrowserIsResetAfterEveryRenderEvenWhenItWasCancelled() = runTest {
        val finished = FakeDriver(probe(hasCard = true))
        render(finished)
        assertEquals("reset", finished.calls.last())

        val endless = FakeDriver(probe(readyState = "loading"))
        val job = launch { render(endless) }
        runCurrent()
        job.cancelAndJoin()
        assertEquals("reset", endless.calls.last())
    }

    @Test
    fun aCancellationGetsThrough() = runTest {
        val driver = FakeDriver(probe()).apply { loadFailure = CancellationException("stop") }

        assertFailsWith<CancellationException> { render(driver) }
    }
}

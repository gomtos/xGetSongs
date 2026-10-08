package com.xgetsongs.engine.lyrics.google

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertIs
import kotlin.test.assertTrue
import kotlin.time.Duration
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TestTimeSource

/** [GoogleLyricsFetcher] with a scripted browser and a clock the test moves itself: no network, no real waiting. */
class GoogleLyricsFetcherTest {
    private val clock = TestTimeSource()
    private val pauses = mutableListOf<Duration>()
    private val logLines = mutableListOf<String>()
    private val log = object : GoogleLyricsLog {
        override fun info(message: String) {
            logLines += "INFO $message"
        }

        override fun warn(message: String) {
            logLines += "WARN $message"
        }
    }

    private fun fetcher(browser: RenderingBrowser) = GoogleLyricsFetcher(
        browser = browser,
        timeSource = clock,
        log = log,
        pause = { duration ->
            pauses += duration
            clock += duration
        },
    )

    @Test
    fun aFoundCardGivesItsLyricsAfterOneRequest() = runTest {
        val browser = FakeBrowser(loadedCard())

        val found = assertIs<GoogleLyricsResult.Found>(fetcher(browser).fetch("IU", "Love poem"))

        assertEquals("더미 하나\n더미 둘\n더미 셋", found.lyrics)
        assertEquals(listOf("https://www.google.com/search?q=IU%20Love%20poem%20lyrics&hl=en"), browser.urls)
        assertTrue(logLines.any { "줄 3, 문단 1" in it })
        assertTrue(logLines.none { "더미" in it }, "the log must not hold lyrics")
    }

    @Test
    fun noCardForAnArtistWithAParenthesisedNameTriesTheNameWithoutItOnce() = runTest {
        val browser = FakeBrowser(loadedWithoutCard(), loadedCard())

        val result = fetcher(browser).fetch("RESCENE (리센느)", "LOVE ATTACK")

        assertIs<GoogleLyricsResult.Found>(result)
        assertEquals(2, browser.urls.size)
        assertTrue("q=RESCENE%20LOVE%20ATTACK%20lyrics&hl=ko" in browser.urls[1])
        assertEquals(listOf(10.seconds), pauses, "the second request keeps the minimum interval too")
    }

    @Test
    fun noCardTwiceIsNoCardAndIsNotAskedAgain() = runTest {
        val browser = FakeBrowser(loadedWithoutCard(), loadedWithoutCard())
        val fetcher = fetcher(browser)

        assertEquals(GoogleLyricsResult.NoCard, fetcher.fetch("RESCENE (리센느)", "LOVE ATTACK"))
        assertEquals(2, browser.urls.size)
        assertEquals(GoogleLyricsResult.NoCard, fetcher.fetch("RESCENE (리센느)", "LOVE ATTACK"))
        assertEquals(2, browser.urls.size, "a song seen once is not asked again")
    }

    @Test
    fun aPlainArtistWithNoCardMakesOnlyOneRequest() = runTest {
        val browser = FakeBrowser(loadedWithoutCard())

        assertEquals(GoogleLyricsResult.NoCard, fetcher(browser).fetch("IU", "Love poem"))
        assertEquals(1, browser.urls.size)
    }

    @Test
    fun theSameSongWrittenDifferentlyIsAskedOnce() = runTest {
        val browser = FakeBrowser(loadedCard())
        val fetcher = fetcher(browser)

        assertIs<GoogleLyricsResult.Found>(fetcher.fetch("IU", "Love poem"))
        assertIs<GoogleLyricsResult.Found>(fetcher.fetch(" iu ", "LOVE POEM"))
        assertEquals(1, browser.urls.size)
    }

    @Test
    fun twoSongsAreKeptTheMinimumIntervalApart() = runTest {
        val browser = FakeBrowser(loadedCard(), loadedCard())
        val fetcher = fetcher(browser)

        fetcher.fetch("IU", "Love poem")
        assertEquals(emptyList(), pauses, "the first request waits for nothing")
        clock += 4.seconds
        fetcher.fetch("IU", "Palette")

        assertEquals(listOf(6.seconds), pauses)
    }

    @Test
    fun noPauseWhenTheIntervalHasPassedAlready() = runTest {
        val browser = FakeBrowser(loadedCard(), loadedCard())
        val fetcher = fetcher(browser)

        fetcher.fetch("IU", "Love poem")
        clock += 11.seconds
        fetcher.fetch("IU", "Palette")

        assertEquals(emptyList(), pauses)
    }

    @Test
    fun aBlockPageStartsACoolDownDuringWhichNothingIsRequested() = runTest {
        val browser = FakeBrowser(loadedSorry(), loadedCard())
        val fetcher = fetcher(browser)

        assertEquals(GoogleLyricsResult.Captcha, fetcher.fetch("IU", "Love poem"))
        assertEquals(GoogleLyricsResult.CoolingDown, fetcher.fetch("IU", "Palette"))
        assertEquals(1, browser.urls.size, "no request while cooling down")
        clock += 5.minutes
        assertIs<GoogleLyricsResult.Found>(fetcher.fetch("IU", "Palette"))
        assertEquals(2, browser.urls.size)
    }

    @Test
    fun theCoolDownDoublesWhileGoogleKeepsBlockingAndStartsOverWhenItAnswers() = runTest {
        val browser = FakeBrowser(loadedSorry(), loadedSorry(), loadedCard(), loadedSorry(), loadedCard())
        val fetcher = fetcher(browser)

        assertEquals(GoogleLyricsResult.Captcha, fetcher.fetch("A", "a")) // first block: 5 minutes
        clock += 5.minutes
        assertEquals(GoogleLyricsResult.Captcha, fetcher.fetch("B", "b")) // blocked again: 10 minutes
        clock += 9.minutes
        assertEquals(GoogleLyricsResult.CoolingDown, fetcher.fetch("C", "c"))
        clock += 1.minutes
        assertIs<GoogleLyricsResult.Found>(fetcher.fetch("C", "c")) // an ordinary answer ends the streak
        assertEquals(GoogleLyricsResult.Captcha, fetcher.fetch("D", "d")) // blocked again: back to 5 minutes
        clock += 5.minutes
        assertIs<GoogleLyricsResult.Found>(fetcher.fetch("E", "e"))
    }

    @Test
    fun theCoolDownIsCappedAtSixtyMinutes() = runTest {
        val browser = FakeBrowser(loadedSorry(), loadedSorry(), loadedSorry(), loadedSorry(), loadedSorry(), loadedCard())
        val fetcher = fetcher(browser)

        for ((index, minutes) in listOf(5, 10, 20, 40).withIndex()) {
            assertEquals(GoogleLyricsResult.Captcha, fetcher.fetch("S$index", "t"))
            clock += minutes.minutes
        }
        assertEquals(GoogleLyricsResult.Captcha, fetcher.fetch("S4", "t")) // 5 * 2^4 = 80, capped
        clock += 59.minutes
        assertEquals(GoogleLyricsResult.CoolingDown, fetcher.fetch("S5", "t"))
        clock += 1.minutes
        assertIs<GoogleLyricsResult.Found>(fetcher.fetch("S5", "t"))
    }

    @Test
    fun aTimeoutAlsoStartsACoolDown() = runTest {
        val browser = FakeBrowser(RenderResult.TimedOut)
        val fetcher = fetcher(browser)

        assertEquals(GoogleLyricsResult.Timeout, fetcher.fetch("IU", "Love poem"))
        assertEquals(GoogleLyricsResult.CoolingDown, fetcher.fetch("IU", "Palette"))
    }

    @Test
    fun blockPagesAreToldApart() = runTest {
        val consent = RenderResult.Loaded("https://consent.google.com/m?continue=x", "<html></html>", "")
        val tooMany = RenderResult.Loaded(TEST_SEARCH_URL, "<html></html>", "429 Too Many Requests")

        assertEquals(GoogleLyricsResult.Consent, fetcher(FakeBrowser(consent)).fetch("IU", "Love poem"))
        assertEquals(GoogleLyricsResult.RateLimited, fetcher(FakeBrowser(tooMany)).fetch("IU", "Love poem"))
        assertEquals(GoogleLyricsResult.Captcha, fetcher(FakeBrowser(loadedSorry())).fetch("IU", "Love poem"))
    }

    @Test
    fun anUnavailableBrowserNeitherCoolsDownNorIsRemembered() = runTest {
        val browser = FakeBrowser(RenderResult.Unavailable("NoClassDefFoundError"), loadedCard())
        val fetcher = fetcher(browser)

        val first = assertIs<GoogleLyricsResult.BrowserUnavailable>(fetcher.fetch("IU", "Love poem"))
        assertEquals("NoClassDefFoundError", first.reason)
        assertIs<GoogleLyricsResult.Found>(fetcher.fetch("IU", "Love poem"))
        assertEquals(2, browser.urls.size)
    }

    @Test
    fun aFailedExtractionIsRememberedAndDoesNotCoolDown() = runTest {
        val browser = FakeBrowser(loadedBrokenCard(), loadedCard())
        val fetcher = fetcher(browser)

        assertEquals(GoogleLyricsResult.ExtractionFailed, fetcher.fetch("IU", "Love poem"))
        assertEquals(GoogleLyricsResult.ExtractionFailed, fetcher.fetch("IU", "Love poem"))
        assertEquals(1, browser.urls.size)
        assertIs<GoogleLyricsResult.Found>(fetcher.fetch("IU", "Palette"))
        assertTrue(logLines.any { it.startsWith("WARN") && "형식이 바뀌었을 수" in it })
    }

    @Test
    fun anExceptionFromTheBrowserIsAnUnavailableBrowser() = runTest {
        val browser = object : RenderingBrowser {
            override suspend fun render(url: String, timeout: Duration): RenderResult = throw IllegalStateException("boom")
        }

        val result = assertIs<GoogleLyricsResult.BrowserUnavailable>(fetcher(browser).fetch("IU", "Love poem"))
        assertEquals("IllegalStateException", result.reason)
    }

    @Test
    fun aCancellationIsNotSwallowed() = runTest {
        val browser = object : RenderingBrowser {
            override suspend fun render(url: String, timeout: Duration): RenderResult = throw CancellationException("stop")
        }

        assertFailsWith<CancellationException> { fetcher(browser).fetch("IU", "Love poem") }
    }

    @Test
    fun aBlankArtistOrTitleMakesNoRequest() = runTest {
        val browser = FakeBrowser()
        val fetcher = fetcher(browser)

        assertEquals(GoogleLyricsResult.NoCard, fetcher.fetch("", "Love poem"))
        assertEquals(GoogleLyricsResult.NoCard, fetcher.fetch("IU", " "))
        assertEquals(emptyList(), browser.urls)
    }
}

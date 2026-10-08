package com.xgetsongs.engine.lyrics.google

import com.xgetsongs.engine.lyrics.LyricsQuery
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlin.time.Duration
import kotlin.time.TestTimeSource

class GoogleLyricsProviderTest {
    private val query = LyricsQuery(artist = "IU", title = "Love poem", album = null, durationSeconds = 258)

    private fun provider(browser: RenderingBrowser, onClose: () -> Unit = {}) =
        GoogleLyricsProvider(GoogleLyricsFetcher(browser, timeSource = TestTimeSource(), pause = {}), onClose)

    @Test
    fun aFoundCardGivesItsLyrics() = runTest {
        assertEquals("더미 하나\n더미 둘\n더미 셋", provider(FakeBrowser(loadedCard())).find(query))
    }

    @Test
    fun noCardGivesNull() = runTest {
        assertNull(provider(FakeBrowser(loadedWithoutCard())).find(query))
    }

    @Test
    fun aBlockPageGivesNull() = runTest {
        assertNull(provider(FakeBrowser(loadedSorry())).find(query))
    }

    @Test
    fun aBlankArtistOrTitleMakesNoRequest() = runTest {
        val browser = FakeBrowser()
        val provider = provider(browser)

        assertNull(provider.find(query.copy(artist = " ")))
        assertNull(provider.find(query.copy(title = "")))
        assertEquals(emptyList(), browser.urls)
    }

    @Test
    fun aCancellationGetsThrough() = runTest {
        val browser = object : RenderingBrowser {
            override suspend fun render(url: String, timeout: Duration): RenderResult = throw CancellationException("stop")
        }

        assertFailsWith<CancellationException> { provider(browser).find(query) }
    }

    @Test
    fun closingRunsTheCloseAction() {
        var closed = false
        provider(FakeBrowser()) { closed = true }.close()

        assertTrue(closed)
    }
}

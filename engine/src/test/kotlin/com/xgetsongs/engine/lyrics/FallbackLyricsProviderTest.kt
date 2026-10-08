package com.xgetsongs.engine.lyrics

import com.xgetsongs.engine.testutil.FakeLyricsProvider
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class FallbackLyricsProviderTest {
    private val query = LyricsQuery(artist = "IU", title = "Love poem", album = null, durationSeconds = null)

    @Test
    fun theFirstAnswerWinsAndLaterProvidersAreNotAsked() = runTest {
        val first = FakeLyricsProvider { "첫째 더미" }
        val second = FakeLyricsProvider { "둘째 더미" }

        assertEquals("첫째 더미", FallbackLyricsProvider(first, second).find(query))
        assertEquals(emptyList(), second.queries)
    }

    @Test
    fun aProviderWithNothingPassesTheQueryOn() = runTest {
        val first = FakeLyricsProvider { null }
        val second = FakeLyricsProvider { "둘째 더미" }

        assertEquals("둘째 더미", FallbackLyricsProvider(first, second).find(query))
        assertEquals(listOf(query), first.queries)
        assertEquals(listOf(query), second.queries)
    }

    @Test
    fun noAnswerAnywhereIsNull() = runTest {
        assertNull(FallbackLyricsProvider(FakeLyricsProvider(), FakeLyricsProvider()).find(query))
        assertNull(FallbackLyricsProvider().find(query))
    }
}

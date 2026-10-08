package com.xgetsongs.engine.lyrics.google

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

class GoogleSearchQueryTest {
    private fun texts(artist: String, title: String) = GoogleSearchQuery.requests(artist, title).map { it.text }

    @Test
    fun anArtistWithAParenthesisedNameGivesTheFullNameFirstAndThenTheNameWithoutIt() {
        val requests = GoogleSearchQuery.requests("RESCENE (리센느)", "LOVE ATTACK")

        assertEquals(listOf("RESCENE (리센느) LOVE ATTACK lyrics", "RESCENE LOVE ATTACK lyrics"), requests.map { it.text })
        assertEquals(listOf("ko", "ko"), requests.map { it.language })
    }

    @Test
    fun aPlainArtistGivesOneRequestInEnglish() {
        val requests = GoogleSearchQuery.requests("IU", "Love poem")

        assertEquals(listOf("IU Love poem lyrics"), requests.map { it.text })
        assertEquals("en", requests.single().language)
    }

    @Test
    fun aKoreanTitleAloneMakesItKorean() {
        assertEquals("ko", GoogleSearchQuery.requests("IU", "좋은 날").single().language)
    }

    @Test
    fun theAddressIsEncodedAsUtf8WithPercentTwentyForSpaces() {
        val url = GoogleSearchQuery.requests("RESCENE (리센느)", "LOVE ATTACK").first().url

        assertEquals("https://www.google.com/search?q=RESCENE%20%28%EB%A6%AC%EC%84%BC%EB%8A%90%29%20LOVE%20ATTACK%20lyrics&hl=ko", url)
        assertEquals("https://www.google.com/search?q=IU%20Love%20poem%20lyrics&hl=en", GoogleSearchQuery.requests("IU", "Love poem").single().url)
    }

    @Test
    fun quotesAroundWordsOfTheTitleAreDropped() {
        assertEquals(listOf("IU Love poem lyrics"), texts("IU", "‘Love’ “poem”"))
        assertEquals(listOf("IU Love poem lyrics"), texts("IU", "\"Love\" poem"))
    }

    @Test
    fun anApostropheInsideAWordIsKeptAsAStraightOne() {
        assertEquals(listOf("IU Don't Stop lyrics"), texts("IU", "Don’t Stop"))
        assertEquals(listOf("IU Don't Stop lyrics"), texts("IU", "Don't Stop"))
    }

    @Test
    fun aTrailingCreditIsDropped() {
        assertEquals(listOf("IU Song lyrics"), texts("IU", "Song feat. Someone"))
        assertEquals(listOf("IU Song lyrics"), texts("IU", "Song FT. Someone Else"))
    }

    @Test
    fun aCreditInBracketsIsDropped() {
        assertEquals(listOf("IU Song lyrics"), texts("IU", "Song (feat. Someone)"))
        assertEquals(listOf("IU Song lyrics"), texts("IU", "Song [Prod. By Someone]"))
        assertEquals(listOf("소연 퇴사할게여 lyrics"), texts("소연", "퇴사할게여 (Narr. 기안84)"))
    }

    @Test
    fun aTrailingCreditStopsAtTheNextBracket() {
        assertEquals(listOf("IU Song (Remix) lyrics"), texts("IU", "Song ft. A & B (Remix)"))
    }

    @Test
    fun aBracketThatNamesAnotherVersionStays() {
        assertEquals(listOf("IU Hello (Japanese Ver.) lyrics"), texts("IU", "Hello (Japanese Ver.)"))
    }

    @Test
    fun whitespaceIsCollapsed() {
        assertEquals(listOf("IU Love poem lyrics"), texts("  IU  ", "Love   poem "))
    }

    @Test
    fun anArtistThatIsPartlyInParenthesesLosesOnlyThatPartInTheSecondRequest() {
        assertEquals(listOf("(G)I-DLE Tomboy lyrics", "I-DLE Tomboy lyrics"), texts("(G)I-DLE", "Tomboy"))
    }

    @Test
    fun aBlankArtistOrTitleGivesNoRequest() {
        assertTrue(GoogleSearchQuery.requests("", "Love poem").isEmpty())
        assertTrue(GoogleSearchQuery.requests("IU", "  ").isEmpty())
    }

    @Test
    fun theCacheKeyIgnoresCaseAndSpacing() {
        val one = GoogleSearchQuery.cacheKey(GoogleSearchQuery.requests("IU", "Love poem").first())
        val other = GoogleSearchQuery.cacheKey(GoogleSearchQuery.requests(" iu ", "LOVE  POEM").first())
        val another = GoogleSearchQuery.cacheKey(GoogleSearchQuery.requests("IU", "Palette").first())

        assertEquals(one, other)
        assertNotEquals(one, another)
    }
}

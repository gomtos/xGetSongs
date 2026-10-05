package com.xgetsongs.engine.lyrics

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertSame
import kotlin.test.assertTrue

/** Every title, artist and lyric in here is made up (or a title that carries no lyrics at all). */
class LyricsMatcherTest {
    private fun query(
        artist: String = "IU",
        title: String = "Love poem",
        album: String? = null,
        durationSeconds: Int? = 258,
    ) = LyricsQuery(artist, title, album, durationSeconds)

    private fun candidate(
        trackName: String? = "Love poem",
        artistName: String? = "IU",
        albumName: String? = "Love poem",
        duration: Double? = 258.0,
        instrumental: Boolean? = false,
        plainLyrics: String? = "La la la\nLa la\nLa",
    ) = LyricsCandidate(trackName, artistName, albumName, duration, instrumental, plainLyrics)

    // ---- titleVariants ----

    @Test
    fun aPlainTitleHasOnlyItself() {
        assertEquals(listOf("Love poem"), LyricsMatcher.titleVariants("Love poem"))
        assertEquals(listOf("사랑의 시"), LyricsMatcher.titleVariants("사랑의 시"))
    }

    @Test
    fun aBracketedPartIsLeftOutInTheSecondVariant() {
        assertEquals(listOf("Title (feat. X)", "Title"), LyricsMatcher.titleVariants("Title (feat. X)"))
        assertEquals(listOf("Title [Live]", "Title"), LyricsMatcher.titleVariants("Title [Live]"))
        assertEquals(listOf("Title {Remix}", "Title"), LyricsMatcher.titleVariants("Title {Remix}"))
        assertEquals(listOf("Title <Inst.>", "Title"), LyricsMatcher.titleVariants("Title <Inst.>"))
        assertEquals(listOf("사랑의 시 (Love poem)", "사랑의 시"), LyricsMatcher.titleVariants("사랑의 시 (Love poem)"))
    }

    @Test
    fun spacesAreCollapsedWhereABracketedPartWasTaken() {
        assertEquals(listOf("A (B) C", "A C"), LyricsMatcher.titleVariants("A (B) C"))
        assertEquals(listOf("A (B)  C [D]", "A C"), LyricsMatcher.titleVariants("A (B)  C [D]"))
        assertEquals(listOf("Title (Live) (Remix)", "Title"), LyricsMatcher.titleVariants("Title (Live) (Remix)"))
    }

    @Test
    fun nestedBracketsAreTakenToo() {
        assertEquals(listOf("A (B (C)) D", "A D"), LyricsMatcher.titleVariants("A (B (C)) D"))
        assertEquals(listOf("A [B (C)] D", "A D"), LyricsMatcher.titleVariants("A [B (C)] D"))
    }

    @Test
    fun aTitleThatIsOnlyBracketsKeepsOnlyItself() {
        assertEquals(listOf("(Live)"), LyricsMatcher.titleVariants("(Live)"))
        assertEquals(listOf("(A) [B]"), LyricsMatcher.titleVariants("(A) [B]"))
    }

    @Test
    fun aTrailingCreditIsLeftOutInTheThirdVariant() {
        assertEquals(listOf("Title feat. X", "Title"), LyricsMatcher.titleVariants("Title feat. X"))
        assertEquals(listOf("Title ft. X Y", "Title"), LyricsMatcher.titleVariants("Title ft. X Y"))
        assertEquals(listOf("Title with X", "Title"), LyricsMatcher.titleVariants("Title with X"))
        assertEquals(listOf("Title prod. X", "Title"), LyricsMatcher.titleVariants("Title prod. X"))
        assertEquals(listOf("Title Narr. X", "Title"), LyricsMatcher.titleVariants("Title Narr. X"))
        assertEquals(listOf("Title feat X", "Title"), LyricsMatcher.titleVariants("Title feat X"))
    }

    @Test
    fun aTrailingCreditIsFoundWhateverTheCase() {
        assertEquals(listOf("Title FEAT. X", "Title"), LyricsMatcher.titleVariants("Title FEAT. X"))
        assertEquals(listOf("Title Prod. X", "Title"), LyricsMatcher.titleVariants("Title Prod. X"))
        assertEquals(listOf("Title WITH X", "Title"), LyricsMatcher.titleVariants("Title WITH X"))
    }

    @Test
    fun aCreditIsLeftOutAfterTheBracketedPartsWentAndAllThreeKindsComeInOrder() {
        assertEquals(
            listOf("Title (Live) feat. X", "Title feat. X", "Title"),
            LyricsMatcher.titleVariants("Title (Live) feat. X"),
        )
        assertEquals(listOf("Title (feat. X)", "Title"), LyricsMatcher.titleVariants("Title (feat. X)"), "equal variants are listed once")
    }

    @Test
    fun aCreditWordInsideAWordOrInFrontOfTheTitleIsNotACredit() {
        assertEquals(listOf("Without You"), LyricsMatcher.titleVariants("Without You"))
        assertEquals(listOf("Featuring Love"), LyricsMatcher.titleVariants("Featuring Love"))
        assertEquals(listOf("With You"), LyricsMatcher.titleVariants("With You"), "nothing in front of it, so nothing is a credit")
        assertEquals(listOf("Prodigal Son"), LyricsMatcher.titleVariants("Prodigal Son"))
    }

    @Test
    fun aTitleThatHasWithInTheMiddleIsAlsoOfferedWithoutTheRest() {
        // The cut is a guess that costs one more request; the match still needs the artist and the length to agree.
        assertEquals(listOf("Dance With Me", "Dance"), LyricsMatcher.titleVariants("Dance With Me"))
    }

    @Test
    fun quoteCharactersAreLeftOutInTheLastVariant() {
        assertEquals(listOf("Don't Stop", "Dont Stop"), LyricsMatcher.titleVariants("Don't Stop"))
        assertEquals(listOf("\"Golden\" 'x'", "Golden x"), LyricsMatcher.titleVariants("\"Golden\" 'x'"))
        assertEquals(listOf("Don\u2019t \u201CStop\u201D", "Dont Stop"), LyricsMatcher.titleVariants("Don\u2019t \u201CStop\u201D"))
        assertEquals(listOf("\u2018Golden\u2019 \u201Ex\u201F", "Golden x"), LyricsMatcher.titleVariants("\u2018Golden\u2019 \u201Ex\u201F"))
    }

    @Test
    fun quotesAreLeftOutOfTheReducedTextAndTheOrderStaysTitleBracketsCreditQuotes() {
        assertEquals(
            listOf("\"Golden\" (Live) feat. X", "\"Golden\" feat. X", "\"Golden\"", "Golden"),
            LyricsMatcher.titleVariants("\"Golden\" (Live) feat. X"),
        )
    }

    @Test
    fun titleVariantsAreDistinctAndNeverBlank() {
        assertEquals(emptyList(), LyricsMatcher.titleVariants(""))
        assertEquals(emptyList(), LyricsMatcher.titleVariants("   "))
        assertEquals(listOf("Title"), LyricsMatcher.titleVariants("  Title  "))
        assertEquals(listOf("''"), LyricsMatcher.titleVariants("''"), "the quote-free text would be blank")
    }

    // ---- artistVariants ----

    @Test
    fun aPlainArtistHasOnlyItself() {
        assertEquals(listOf("IU"), LyricsMatcher.artistVariants("IU"))
        assertEquals(listOf("아이유"), LyricsMatcher.artistVariants("아이유"))
    }

    @Test
    fun anArtistWithAnAliasInParenthesesHasTheWholeTheOutsideAndTheInside() {
        assertEquals(listOf("소연 (SOYEON)", "소연", "SOYEON"), LyricsMatcher.artistVariants("소연 (SOYEON)"))
        assertEquals(listOf("(G)I-DLE", "I-DLE", "G"), LyricsMatcher.artistVariants("(G)I-DLE"))
    }

    @Test
    fun equalArtistVariantsAreListedOnce() {
        assertEquals(listOf("LOVE ATTACK (LOVE ATTACK)", "LOVE ATTACK"), LyricsMatcher.artistVariants("LOVE ATTACK (LOVE ATTACK)"))
    }

    @Test
    fun atMostThreeArtistVariantsAreGiven() {
        assertEquals(listOf("A (B) (C) (D)", "A", "B"), LyricsMatcher.artistVariants("A (B) (C) (D)"))
    }

    @Test
    fun blankArtistVariantsAreNotGiven() {
        assertEquals(emptyList(), LyricsMatcher.artistVariants(""))
        assertEquals(emptyList(), LyricsMatcher.artistVariants("  "))
        assertEquals(listOf("A ()", "A"), LyricsMatcher.artistVariants("A ()"), "the empty part inside the parentheses is dropped")
    }

    // ---- normalize ----

    @Test
    fun normalizeKeepsOnlyLowercaseLettersAndDigits() {
        assertEquals("lovepoem", LyricsMatcher.normalize("Love Poem!"))
        assertEquals("abc12", LyricsMatcher.normalize("A-B_C 1.2"))
        assertEquals("dontstop", LyricsMatcher.normalize("Don't Stop"))
        assertEquals("dontstop", LyricsMatcher.normalize("Don\u2019t   Stop"))
        assertEquals("", LyricsMatcher.normalize(" -- !? "))
        assertEquals("", LyricsMatcher.normalize(""))
    }

    @Test
    fun normalizeKeepsHangulAndOtherLettersAndLowercasesThem() {
        assertEquals("소연soyeon", LyricsMatcher.normalize("소연 (SOYEON)"))
        assertEquals("사랑의시", LyricsMatcher.normalize("사랑의 시"))
        assertEquals("ünï", LyricsMatcher.normalize("ÜNÏ"))
        assertEquals("日本語123", LyricsMatcher.normalize("日本語 123"))
    }

    // ---- isMatch ----

    @Test
    fun anExactRecordIsAMatch() {
        assertTrue(LyricsMatcher.isMatch(query(), candidate()))
    }

    @Test
    fun titlesAreEqualAfterNormalisation() {
        assertTrue(LyricsMatcher.isMatch(query(title = "Love poem"), candidate(trackName = "LOVE POEM!")))
        assertTrue(LyricsMatcher.isMatch(query(title = "Don't Stop"), candidate(trackName = "Don\u2019t Stop")))
        assertTrue(LyricsMatcher.isMatch(query(title = "A-B"), candidate(trackName = "a b")))
    }

    @Test
    fun aVariantOfTheTitleOnEitherSideIsEnough() {
        assertTrue(LyricsMatcher.isMatch(query(title = "Love poem (feat. X)"), candidate(trackName = "Love poem")))
        assertTrue(LyricsMatcher.isMatch(query(title = "Love poem"), candidate(trackName = "Love poem [Live]")))
        assertTrue(LyricsMatcher.isMatch(query(title = "Love poem (Official)"), candidate(trackName = "Love poem (Live)")))
        assertTrue(LyricsMatcher.isMatch(query(title = "Love poem feat. X"), candidate(trackName = "Love poem")))
    }

    @Test
    fun aTitleThatMerelyContainsOrStartsLikeTheOtherIsNotAMatch() {
        assertFalse(LyricsMatcher.isMatch(query(title = "Love poem"), candidate(trackName = "Love poem 2")))
        assertFalse(LyricsMatcher.isMatch(query(title = "Love"), candidate(trackName = "Love poem")))
        assertFalse(LyricsMatcher.isMatch(query(title = "Love poem"), candidate(trackName = "Another song")))
    }

    @Test
    fun aMissingOrBlankCandidateTitleIsNotAMatch() {
        assertFalse(LyricsMatcher.isMatch(query(), candidate(trackName = null)))
        assertFalse(LyricsMatcher.isMatch(query(), candidate(trackName = "")))
        assertFalse(LyricsMatcher.isMatch(query(title = "!!!"), candidate(trackName = "???")), "titles with no letter or digit say nothing")
    }

    @Test
    fun theArtistMayContainTheOtherOrBeContainedInIt() {
        assertTrue(LyricsMatcher.isMatch(query(artist = "IU"), candidate(artistName = "IU (아이유)")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "IU"), candidate(artistName = "IU & Someone")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "IU & Someone"), candidate(artistName = "IU")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "iu"), candidate(artistName = "I.U.")))
    }

    @Test
    fun theArtistVariantsOfBothSidesAreUsed() {
        assertTrue(LyricsMatcher.isMatch(query(artist = "소연 (SOYEON)"), candidate(artistName = "SOYEON")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "소연 (SOYEON)"), candidate(artistName = "소연")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "SOYEON"), candidate(artistName = "소연 (SOYEON)")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "(G)I-DLE"), candidate(artistName = "I-DLE")))
    }

    @Test
    fun anotherArtistIsNotAMatch() {
        assertFalse(LyricsMatcher.isMatch(query(artist = "IU"), candidate(artistName = "Somebody Else")))
        assertFalse(LyricsMatcher.isMatch(query(artist = "아이유"), candidate(artistName = "IU")))
        assertFalse(LyricsMatcher.isMatch(query(), candidate(artistName = null)))
        assertFalse(LyricsMatcher.isMatch(query(), candidate(artistName = "")))
    }

    @Test
    fun anArtistOfOneCharacterIsNeverAMatch() {
        assertFalse(LyricsMatcher.isMatch(query(artist = "A"), candidate(artistName = "A")))
        assertFalse(LyricsMatcher.isMatch(query(artist = "A"), candidate(artistName = "ABBA")))
        assertFalse(LyricsMatcher.isMatch(query(artist = "ABBA"), candidate(artistName = "A")))
        assertFalse(LyricsMatcher.isMatch(query(artist = "!"), candidate(artistName = "?")), "nothing is left of either after normalising")
    }

    @Test
    fun aDifferenceOfEightSecondsIsAcceptedAndNineIsNot() {
        assertTrue(LyricsMatcher.isMatch(query(durationSeconds = 258), candidate(duration = 266.0)))
        assertTrue(LyricsMatcher.isMatch(query(durationSeconds = 258), candidate(duration = 250.0)))
        assertFalse(LyricsMatcher.isMatch(query(durationSeconds = 258), candidate(duration = 267.0)))
        assertFalse(LyricsMatcher.isMatch(query(durationSeconds = 258), candidate(duration = 249.0)))
    }

    @Test
    fun aFractionalCandidateDurationIsComparedAsItIs() {
        assertTrue(LyricsMatcher.isMatch(query(durationSeconds = 258), candidate(duration = 265.9)))
        assertTrue(LyricsMatcher.isMatch(query(durationSeconds = 258), candidate(duration = 250.1)))
        assertFalse(LyricsMatcher.isMatch(query(durationSeconds = 258), candidate(duration = 266.1)))
        assertFalse(LyricsMatcher.isMatch(query(durationSeconds = 258), candidate(duration = 249.9)))
    }

    @Test
    fun anUnknownDurationOnEitherSideIsAccepted() {
        assertTrue(LyricsMatcher.isMatch(query(durationSeconds = null), candidate(duration = 999.0)))
        assertTrue(LyricsMatcher.isMatch(query(durationSeconds = 258), candidate(duration = null)))
        assertTrue(LyricsMatcher.isMatch(query(durationSeconds = null), candidate(duration = null)))
    }

    @Test
    fun anInstrumentalRecordIsNotAMatch() {
        assertFalse(LyricsMatcher.isMatch(query(), candidate(instrumental = true)))
        assertFalse(LyricsMatcher.isMatch(query(), candidate(instrumental = true, plainLyrics = "La la la\nLa la\nLa")))
        assertTrue(LyricsMatcher.isMatch(query(), candidate(instrumental = null)), "an unknown flag is not true")
        assertTrue(LyricsMatcher.isMatch(query(), candidate(instrumental = false)))
    }

    @Test
    fun aRecordWithoutLyricsIsNotAMatch() {
        assertFalse(LyricsMatcher.isMatch(query(), candidate(plainLyrics = null)))
        assertFalse(LyricsMatcher.isMatch(query(), candidate(plainLyrics = "")))
        assertFalse(LyricsMatcher.isMatch(query(), candidate(plainLyrics = "  \n\t ")))
    }

    // ---- pick ----

    @Test
    fun pickTakesTheAcceptableCandidateWithTheSmallestDurationDifference() {
        val far = candidate(albumName = "far", duration = 264.0)
        val near = candidate(albumName = "near", duration = 259.0)
        val exact = candidate(albumName = "exact", duration = 258.0)
        val other = candidate(trackName = "Another song", albumName = "other", duration = 258.0)

        assertSame(exact, LyricsMatcher.pick(query(), listOf(far, near, other, exact)))
        assertSame(near, LyricsMatcher.pick(query(), listOf(far, other, near)))
        assertSame(far, LyricsMatcher.pick(query(), listOf(other, far)))
    }

    @Test
    fun pickKeepsTheFirstOfEqualDifferences() {
        val below = candidate(albumName = "below", duration = 256.0)
        val above = candidate(albumName = "above", duration = 260.0)
        val sameAsBelow = candidate(albumName = "again", duration = 256.0)

        assertSame(below, LyricsMatcher.pick(query(), listOf(below, above, sameAsBelow)))
        assertSame(above, LyricsMatcher.pick(query(), listOf(above, below)))
    }

    @Test
    fun anUnknownDurationDifferenceCountsAsTheLargest() {
        val unknown = candidate(albumName = "unknown", duration = null)
        val far = candidate(albumName = "far", duration = 265.0)

        assertSame(far, LyricsMatcher.pick(query(), listOf(unknown, far)))
        assertSame(unknown, LyricsMatcher.pick(query(), listOf(unknown)))
        assertSame(unknown, LyricsMatcher.pick(query(durationSeconds = null), listOf(unknown, far)), "everything unknown: the first")
    }

    @Test
    fun pickSkipsWhatIsNotAcceptable() {
        val instrumental = candidate(albumName = "inst", instrumental = true, duration = 258.0)
        val empty = candidate(albumName = "empty", plainLyrics = " ", duration = 258.0)
        val tooLong = candidate(albumName = "long", duration = 300.0)
        val good = candidate(albumName = "good", duration = 262.0)

        assertSame(good, LyricsMatcher.pick(query(), listOf(instrumental, empty, tooLong, good)))
    }

    @Test
    fun pickGivesNullWhenNothingIsAcceptable() {
        assertNull(LyricsMatcher.pick(query(), emptyList()))
        assertNull(LyricsMatcher.pick(query(), listOf(candidate(trackName = "Another song"), candidate(artistName = "Somebody Else"))))
        assertNull(LyricsMatcher.pick(query(), listOf(candidate(duration = 300.0), candidate(instrumental = true))))
    }
}

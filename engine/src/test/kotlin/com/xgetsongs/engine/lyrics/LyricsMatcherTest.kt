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
        assertEquals(listOf("Title prod. X", "Title"), LyricsMatcher.titleVariants("Title prod. X"))
        assertEquals(listOf("Title Narr. X", "Title"), LyricsMatcher.titleVariants("Title Narr. X"))
        assertEquals(listOf("Title feat X", "Title"), LyricsMatcher.titleVariants("Title feat X"))
    }

    @Test
    fun aTrailingCreditIsFoundWhateverTheCase() {
        assertEquals(listOf("Title FEAT. X", "Title"), LyricsMatcher.titleVariants("Title FEAT. X"))
        assertEquals(listOf("Title Prod. X", "Title"), LyricsMatcher.titleVariants("Title Prod. X"))
        assertEquals(listOf("Title NARR. X", "Title"), LyricsMatcher.titleVariants("Title NARR. X"))
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
    fun withIsNotACreditWordSoNoShorterTitleIsOfferedForTitlesThatHaveIt() {
        // "Stay With Me" is a whole title, not the song "Stay" with a credit. A credit written (with X) is a bracketed part.
        assertEquals(listOf("Dance With Me"), LyricsMatcher.titleVariants("Dance With Me"))
        assertEquals(listOf("Stay With Me"), LyricsMatcher.titleVariants("Stay With Me"))
        assertEquals(listOf("Title with X"), LyricsMatcher.titleVariants("Title with X"))
        assertEquals(listOf("Title WITH X"), LyricsMatcher.titleVariants("Title WITH X"))
        assertEquals(listOf("Title (with X)", "Title"), LyricsMatcher.titleVariants("Title (with X)"))
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

    @Test
    fun normalizeAppliesCompatibilityNormalisationFirst() {
        // Full-width letters and digits (U+FF29 U+FF35 U+FF11) are the ASCII ones.
        assertEquals("iu1", LyricsMatcher.normalize("\uFF29\uFF35\uFF11"))
        assertEquals("iu", LyricsMatcher.normalize("\uFF29\uFF35"))
        // Hangul as separate jamo (U+1112 U+1161 U+11AB) is the composed syllable.
        assertEquals("한", LyricsMatcher.normalize("\u1112\u1161\u11AB"))
        assertEquals(LyricsMatcher.normalize("한글"), LyricsMatcher.normalize("\u1112\u1161\u11AB\u1100\u1173\u11AF"))
        // A ligature (U+FB01) is two letters.
        assertEquals("fine", LyricsMatcher.normalize("\uFB01ne"))
    }

    @Test
    fun titlesThatDifferOnlyInWidthOrInComposedHangulAreEqual() {
        assertTrue(LyricsMatcher.isMatch(query(title = "\uFF2Cove poem"), candidate(trackName = "Love poem")))
        assertTrue(LyricsMatcher.isMatch(query(title = "한글"), candidate(trackName = "\u1112\u1161\u11AB\u1100\u1173\u11AF")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "한글"), candidate(artistName = "\u1112\u1161\u11AB\u1100\u1173\u11AF")))
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
    fun aVariantOfTheTitleOnEitherSideIsEnoughWhenWhatIsLeftOutIsNoVersionMarker() {
        assertTrue(LyricsMatcher.isMatch(query(title = "Love poem (feat. X)"), candidate(trackName = "Love poem")))
        assertTrue(LyricsMatcher.isMatch(query(title = "Love poem"), candidate(trackName = "Love poem (Official Audio)")))
        assertTrue(LyricsMatcher.isMatch(query(title = "Love poem (Official)"), candidate(trackName = "Love poem (MV)")))
        assertTrue(LyricsMatcher.isMatch(query(title = "Love poem feat. X"), candidate(trackName = "Love poem")))
        assertTrue(LyricsMatcher.isMatch(query(title = "Love poem"), candidate(trackName = "Love poem feat. X")))
        assertTrue(LyricsMatcher.isMatch(query(title = "LOVE ATTACK"), candidate(trackName = "LOVE ATTACK (LOVE ATTACK)")))
    }

    @Test
    fun theQuerysOwnVersionWordsMayBeLeftOutBecauseAVideoOfALiveVersionHasTheSameLyrics() {
        assertTrue(LyricsMatcher.isMatch(query(title = "Love poem (Live)"), candidate(trackName = "Love poem")))
    }

    @Test
    fun aVersionMarkerOfTheCandidateKeepsItFromMatchingAShorterTitle() {
        val markers = listOf(
            "ver", "Ver.", "version", "Japanese Ver.", "English Version", "Chinese", "Korean ver", "Inst", "Inst.", "Instrumental",
            "Remix", "Remixes", "remix by X", "Live", "Live at Somewhere", "Acoustic", "Acoustic Version", "Cover", "Edit", "Radio Edit",
            "Mix", "Club Mix", "Remaster", "Remastered 2011", "2011 Remaster", "Demo", "demo ver.", "Ver2", "Remix2", "Live2023",
            "반주", "일본어 버전", "영어", "중국어", "한국어", "라이브", "리믹스", "어쿠스틱", "한국어Ver", "AR 반주",
        )
        for (marker in markers) {
            for ((open, close) in listOf("(" to ")", "[" to "]", "{" to "}", "<" to ">")) {
                assertFalse(
                    LyricsMatcher.isMatch(query(title = "Hello"), candidate(trackName = "Hello $open$marker$close")),
                    "marker: $open$marker$close",
                )
            }
        }
        assertFalse(LyricsMatcher.isMatch(query(title = "Hello"), candidate(trackName = "Hello (Japanese Ver.)")))
    }

    @Test
    fun aVersionMarkerInACreditOfTheCandidateKeepsItFromMatchingAShorterTitleToo() {
        assertFalse(LyricsMatcher.isMatch(query(title = "Hello"), candidate(trackName = "Hello feat. X Remix")))
        assertFalse(LyricsMatcher.isMatch(query(title = "Hello"), candidate(trackName = "Hello (Live) feat. X")))
        assertTrue(LyricsMatcher.isMatch(query(title = "Hello"), candidate(trackName = "Hello feat. X")), "a plain credit is left out")
    }

    @Test
    fun theFullTitleOfTheCandidateStillMatchesWhateverItsVersionMarker() {
        assertTrue(LyricsMatcher.isMatch(query(title = "Hello (Japanese Ver.)"), candidate(trackName = "Hello (Japanese Ver.)")))
        assertTrue(LyricsMatcher.isMatch(query(title = "Hello Japanese Ver."), candidate(trackName = "Hello (Japanese Ver.)")))
        assertTrue(LyricsMatcher.isMatch(query(title = "Hello (Live)"), candidate(trackName = "Hello (Live)")))
    }

    @Test
    fun aMarkerNeedsToBeAWholeWordForEnglishButNotForKorean() {
        // "Verse", "Lively", "Editor" and "Mixture" only start like a marker; the shorter title is still offered.
        for (word in listOf("Verse 2", "Lively", "Editor's Note", "Mixture", "Democracy", "Discover", "Korea")) {
            assertTrue(LyricsMatcher.isMatch(query(title = "Hello"), candidate(trackName = "Hello ($word)")), "not a marker: $word")
        }
    }

    @Test
    fun aTitleThatMerelyStartsWithTheShorterOneDoesNotMatchBecauseOfAWordLikeWith() {
        assertFalse(LyricsMatcher.isMatch(query(title = "Stay"), candidate(trackName = "Stay With Me")))
        assertFalse(LyricsMatcher.isMatch(query(title = "Stay With Me"), candidate(trackName = "Stay")))
        assertFalse(LyricsMatcher.isMatch(query(title = "Dance With Me"), candidate(trackName = "Dance")))
        assertFalse(LyricsMatcher.isMatch(query(title = "Dance"), candidate(trackName = "Dance With Me")))
        assertTrue(LyricsMatcher.isMatch(query(title = "Dance With Me"), candidate(trackName = "Dance With Me")))
        assertTrue(LyricsMatcher.isMatch(query(title = "Dance (with X)"), candidate(trackName = "Dance")), "a credit in brackets is left out")
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
    fun artistsAreComparedByWholeWordsNotBySubstrings() {
        assertFalse(LyricsMatcher.isMatch(query(artist = "Rain"), candidate(artistName = "Rainbow")))
        assertFalse(LyricsMatcher.isMatch(query(artist = "Rainbow"), candidate(artistName = "Rain")))
        assertFalse(LyricsMatcher.isMatch(query(artist = "IU"), candidate(artistName = "Liu Yifei")))
        assertFalse(LyricsMatcher.isMatch(query(artist = "Liu Yifei"), candidate(artistName = "IU")))
        assertFalse(LyricsMatcher.isMatch(query(artist = "Jin"), candidate(artistName = "Jinx")))
        assertFalse(LyricsMatcher.isMatch(query(artist = "BTS"), candidate(artistName = "Abtsa")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "Rain"), candidate(artistName = "Rain")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "Rain"), candidate(artistName = "Rain & Friends")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "Friends & Rain"), candidate(artistName = "Rain")))
    }

    @Test
    fun aRunOfWordsOfOneSideMustSitTogetherInTheOther() {
        assertTrue(LyricsMatcher.isMatch(query(artist = "Taylor Swift"), candidate(artistName = "Taylor Swift feat. Somebody")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "Somebody & Taylor Swift"), candidate(artistName = "Taylor Swift")))
        assertFalse(LyricsMatcher.isMatch(query(artist = "Taylor Swift"), candidate(artistName = "Taylor Somebody Swift")), "the words are not together")
        assertFalse(LyricsMatcher.isMatch(query(artist = "Taylor Swift"), candidate(artistName = "Swift Taylor")), "the order differs")
    }

    @Test
    fun anAliasAndAnAbbreviationStillMatchThroughTheirVariantsOrTheirLettersAndDigits() {
        assertTrue(LyricsMatcher.isMatch(query(artist = "아이오아이 (I.O.I)"), candidate(artistName = "아이오아이")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "아이오아이 (I.O.I)"), candidate(artistName = "I.O.I")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "아이오아이 (I.O.I)"), candidate(artistName = "IOI")), "I.O.I and IOI are the same letters")
        assertTrue(LyricsMatcher.isMatch(query(artist = "IOI"), candidate(artistName = "I.O.I")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "IU & Someone"), candidate(artistName = "IU")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "AKMU"), candidate(artistName = "AKMU (악뮤)")))
    }

    @Test
    fun theWidthOfCharactersAndTheirCompositionDoNotMatterForTheArtistEither() {
        assertTrue(LyricsMatcher.isMatch(query(artist = "\uFF29\uFF35"), candidate(artistName = "IU")))
        assertTrue(LyricsMatcher.isMatch(query(artist = "IU"), candidate(artistName = "\uFF29\uFF35 & Someone")))
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
    fun pickNeverTakesAVersionOfTheSongInsteadOfTheSongItselfWhateverTheDurationDifference() {
        val version = candidate(trackName = "Hello (Japanese Ver.)", albumName = "v", duration = 258.0, plainLyrics = "Japanese one\nJapanese two\nJapanese three")
        val exact = candidate(trackName = "Hello", albumName = "e", duration = 264.0)

        assertSame(exact, LyricsMatcher.pick(query(title = "Hello"), listOf(version, exact)))
        assertSame(exact, LyricsMatcher.pick(query(title = "Hello"), listOf(exact, version)))
    }

    @Test
    fun pickRanksTheTitleMatchFirstWholeThenShortenedOnTheQuerySideThenShortenedOnTheCandidateSide() {
        val whole = candidate(trackName = "Love poem (feat. X)", albumName = "whole", duration = 264.0)
        val queryShortened = candidate(trackName = "Love poem", albumName = "q", duration = 262.0)
        val candidateShortened = candidate(trackName = "Love poem (MV)", albumName = "c", duration = 258.0)
        val asked = query(title = "Love poem (feat. X)")

        assertSame(whole, LyricsMatcher.pick(asked, listOf(candidateShortened, queryShortened, whole)))
        assertSame(queryShortened, LyricsMatcher.pick(asked, listOf(candidateShortened, queryShortened)))
        assertSame(candidateShortened, LyricsMatcher.pick(asked, listOf(candidateShortened)))
        // The whole title of a plain query beats a candidate that had to be shortened, even when its length is farther off.
        val plain = query(title = "Love poem")
        val exact = candidate(trackName = "Love poem", albumName = "exact", duration = 265.0)
        val official = candidate(trackName = "Love poem (Official Audio)", albumName = "official", duration = 258.0)
        assertSame(exact, LyricsMatcher.pick(plain, listOf(official, exact)))
    }

    @Test
    fun withEqualTitleQualityAnAlbumEqualToTheQueryAlbumWinsEvenWithALargerDurationDifference() {
        val otherAlbum = candidate(albumName = "Another album", duration = 258.0)
        val sameAlbum = candidate(albumName = "LOVE POEM!", duration = 263.0)
        val asked = query(album = "Love poem")

        assertSame(sameAlbum, LyricsMatcher.pick(asked, listOf(otherAlbum, sameAlbum)))
        assertSame(sameAlbum, LyricsMatcher.pick(asked, listOf(sameAlbum, otherAlbum)))
    }

    @Test
    fun theAlbumIsNotConsideredWhenTheQueryHasNone() {
        val near = candidate(albumName = "Another album", duration = 258.0)
        val far = candidate(albumName = "Love poem", duration = 263.0)

        assertSame(near, LyricsMatcher.pick(query(album = null), listOf(far, near)))
        assertSame(near, LyricsMatcher.pick(query(album = "  "), listOf(far, near)), "a blank album says nothing either")
    }

    @Test
    fun aMissingCandidateAlbumIsNoMatch() {
        val noAlbum = candidate(albumName = null, duration = 258.0)
        val sameAlbum = candidate(albumName = "Love poem", duration = 263.0)

        assertSame(sameAlbum, LyricsMatcher.pick(query(album = "Love poem"), listOf(noAlbum, sameAlbum)))
    }

    @Test
    fun theTitleQualityOutranksTheAlbum() {
        val exactOtherAlbum = candidate(trackName = "Love poem", albumName = "Another album", duration = 258.0)
        val shortenedSameAlbum = candidate(trackName = "Love poem (Official Audio)", albumName = "Love poem", duration = 258.0)

        assertSame(exactOtherAlbum, LyricsMatcher.pick(query(album = "Love poem"), listOf(shortenedSameAlbum, exactOtherAlbum)))
    }

    @Test
    fun withEqualTitleQualityAndAlbumTheSmallestDurationDifferenceWinsAndTiesKeepTheFirst() {
        val asked = query(album = "Love poem")
        val far = candidate(albumName = "Love poem", duration = 265.0)
        val near = candidate(albumName = "Love poem", duration = 259.0)
        val tie = candidate(albumName = "Love poem", duration = 257.0)

        assertSame(near, LyricsMatcher.pick(asked, listOf(far, near)))
        assertSame(near, LyricsMatcher.pick(asked, listOf(near, tie)), "equal differences: the first")
        assertSame(tie, LyricsMatcher.pick(asked, listOf(tie, near)))
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

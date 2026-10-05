package com.xgetsongs.shared.lyrics

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

/** Every lyric and description in here is made up. */
class LyricsExtractorTest {
    private val block = "첫 번째 줄\n두 번째 줄\nLa la la"

    /** [lines] joined with `\n`. */
    private fun text(vararg lines: String) = lines.joinToString("\n")

    // ---- the structure of a real description ----

    @Test
    fun findsTheBlockBetweenTheHeadingAndTheSeparatorThatFollowsIt() {
        val description = text(
            "Example Artist - Example Song (Official Audio)",
            "Listen on every platform.",
            "=======",
            "[Lyrics]",
            "=======",
            "첫 번째 줄",
            "두 번째 줄",
            "",
            "La la la",
            "네 번째 줄",
            "=======",
            "Instagram: https://example.invalid/artist",
            "Twitter: https://example.invalid/artist",
            "",
            "© 2024 Example Label. All rights reserved.",
            "",
            "#example #song",
        )

        assertEquals("첫 번째 줄\n두 번째 줄\n\nLa la la\n네 번째 줄", LyricsExtractor.extract(description))
    }

    @Test
    fun theSeparatorAfterTheHeadingIsOptional() {
        val description = text("Intro", "=======", "[Lyrics]", "Line one", "Line two", "Line three", "=======", "Links")

        assertEquals("Line one\nLine two\nLine three", LyricsExtractor.extract(description))
    }

    @Test
    fun aHeadingLaterInTheDescriptionIsFound() {
        val description = text("[Credits]", "Someone", "Somebody", "", "[Lyrics]", "Line one", "Line two", "Line three")

        assertEquals("Line one\nLine two\nLine three", LyricsExtractor.extract(description))
    }

    // ---- the heading line ----

    @Test
    fun everyHeadingSpellingIsAMarker() {
        val spellings = listOf(
            "[Lyrics]", "Lyrics", "Lyrics:", "LYRICS", "lyric", "Lyric:", "■ LYRICS ■", "♬ Lyrics ♬", "[ Lyrics ]",
            "가사", "[가사]", "【가사】", "가사:", "🎵 가사 🎵",
            "歌詞", "【歌詞】", "歌词", "[歌词]",
            "[Lyrics / 가사]", "[가사 / Lyrics]", "Lyrics 가사", "Lyrics (Korean)", "[Lyrics - Kor]", "Lyrics (Original)",
            "  [Lyrics]  ", "\t가사\t",
        )
        for (spelling in spellings) {
            assertEquals(block, LyricsExtractor.extract(spelling + "\n" + block), "marker: [$spelling]")
        }
    }

    @Test
    fun aLineThatIsMoreThanAHeadingIsNotAMarker() {
        val notMarkers = listOf(
            "Lyrics by 소연", "Lyrics: some words", "Lyricsmith", "My lyrics are here", "Korean lyrics", "Song lyrics",
            "Lyrics (English)", "Lyrics (Romanized)", "[Lyrics] 2024", "가사집", "Lyric video", "Lyrics & Music",
        )
        for (line in notMarkers) {
            assertNull(LyricsExtractor.extract(line + "\n" + block), "not a marker: [$line]")
        }
    }

    @Test
    fun theFirstMarkerWinsAndALaterBlockIsIgnored() {
        val description = text(
            "[Lyrics]", "Line one", "Line two", "Line three",
            "=======",
            "[Lyrics]", "Other one", "Other two", "Other three",
        )

        assertEquals("Line one\nLine two\nLine three", LyricsExtractor.extract(description))
    }

    @Test
    fun aSecondMarkerAfterARomanizedSectionIsIgnoredToo() {
        val description = text(
            "가사", "첫 줄", "둘째 줄", "셋째 줄",
            "[Romanized]", "cheot jul", "dul jjae jul", "set jjae jul",
            "[Lyrics]", "Other one", "Other two", "Other three",
        )

        assertEquals("첫 줄\n둘째 줄\n셋째 줄", LyricsExtractor.extract(description))
    }

    @Test
    fun aFirstMarkerWithTooFewLinesDoesNotFallBackToALaterMarker() {
        val description = text("[Lyrics]", "Only one", "=======", "[Lyrics]", "Line one", "Line two", "Line three")

        assertNull(LyricsExtractor.extract(description))
    }

    // ---- where the block starts ----

    @Test
    fun blankAndSeparatorLinesDirectlyAfterTheMarkerAreSkipped() {
        val description = text("Lyrics", "", "=======", "", "-----", "   ", "Line one", "Line two", "Line three")

        assertEquals("Line one\nLine two\nLine three", LyricsExtractor.extract(description))
    }

    @Test
    fun aSeparatorInTheMiddleOfTheBlockEndsIt() {
        val description = text("Lyrics", "Line one", "Line two", "Line three", "=======", "Line four", "Line five")

        assertEquals("Line one\nLine two\nLine three", LyricsExtractor.extract(description))
    }

    // ---- where the block ends ----

    private fun withTerminator(terminator: String) = text("[Lyrics]", "Line one", "Line two", "", "Line three", terminator, "Tail one", "Tail two")

    @Test
    fun everySeparatorStyleEndsTheBlock() {
        val separators = listOf(
            "=======", "===", "-----", "___", "*****", "~~~", "###", "...", "+++", "━━━━", "───", "═══",
            "   =====   ",
        )
        for (separator in separators) {
            assertEquals("Line one\nLine two\n\nLine three", LyricsExtractor.extract(withTerminator(separator)), "separator: [$separator]")
        }
    }

    @Test
    fun aLineWithALinkEndsTheBlock() {
        val links = listOf("https://example.invalid/a", "http://example.invalid", "Follow: www.example.invalid", "Visit HTTPS://EXAMPLE.INVALID")
        for (link in links) {
            assertEquals("Line one\nLine two\n\nLine three", LyricsExtractor.extract(withTerminator(link)), "link: [$link]")
        }
    }

    @Test
    fun aCopyrightLineEndsTheBlock() {
        val notices = listOf(
            "© 2024 Example Label", "  ©2024", "ⓒ 2024 Example", "(c) 2024 Example", "(C) 2024 Example",
            "Copyright 2024 Example", "COPYRIGHT: Example", "All rights reserved.", "ALL RIGHTS RESERVED",
        )
        for (notice in notices) {
            assertEquals("Line one\nLine two\n\nLine three", LyricsExtractor.extract(withTerminator(notice)), "notice: [$notice]")
        }
    }

    @Test
    fun aHashtagLineEndsTheBlock() {
        for (tag in listOf("#example", "#example #song", "  #태그", "##double")) {
            assertEquals("Line one\nLine two\n\nLine three", LyricsExtractor.extract(withTerminator(tag)), "hashtag: [$tag]")
        }
    }

    @Test
    fun aHashFollowedByASpaceIsNotAHashtag() {
        val description = text("Lyrics", "Line one", "# Line two", "Line three", "#")

        assertEquals("Line one\n# Line two\nLine three\n#", LyricsExtractor.extract(description))
    }

    @Test
    fun aBracketedHeadingOfAnotherSectionEndsTheBlock() {
        val headings = listOf(
            "[Rom]", "[Romanized]", "[Romanization]", "[English Translation]", "[Eng]", "【번역】", "(Translation)", "[Translated]",
            "[Credits]", "[Info]", "[Staff]", "[Links]", "[SNS]", "[Follow us]", "[번역]", "[해석]", "[영문]", "[로마자]",
            "[크레딧]", "[정보]", "  [ROM]  ", "[Lyrics]", "【가사】", "(Lyrics)", "[Lyrics / 가사]",
        )
        for (heading in headings) {
            assertEquals("Line one\nLine two\n\nLine three", LyricsExtractor.extract(withTerminator(heading)), "heading: [$heading]")
        }
    }

    @Test
    fun songStructureTagsAreLyrics() {
        val description = text(
            "[Lyrics]", "[Verse 1]", "Line one", "[Pre-Chorus]", "Line two", "[Chorus]", "Line three",
            "[Bridge: Name]", "[후렴]", "(Ooh)", "[Intro]", "[Interlude]", "=======", "Tail",
        )

        assertEquals(
            "[Verse 1]\nLine one\n[Pre-Chorus]\nLine two\n[Chorus]\nLine three\n[Bridge: Name]\n[후렴]\n(Ooh)\n[Intro]\n[Interlude]",
            LyricsExtractor.extract(description),
        )
    }

    @Test
    fun aHeadingWordThatIsNotInBracketsIsLyrics() {
        val description = text("Lyrics", "Romance in the air", "English words", "Info is what I need", "Link my hands", "=======")

        assertEquals("Romance in the air\nEnglish words\nInfo is what I need\nLink my hands", LyricsExtractor.extract(description))
    }

    @Test
    fun aMachineGeneratedLineEndsTheBlock() {
        val lines = listOf("Provided to YouTube by Example Label", "Released on: 2024-01-01", "Auto-generated by YouTube.", "  PROVIDED TO YOUTUBE")
        for (line in lines) {
            assertEquals("Line one\nLine two\n\nLine three", LyricsExtractor.extract(withTerminator(line)), "line: [$line]")
        }
    }

    @Test
    fun almostASeparatorIsALyricLine() {
        val description = text("Lyrics", "Line one", "==", "=-=", "Line two", "Line three")

        assertEquals("Line one\n==\n=-=\nLine two\nLine three", LyricsExtractor.extract(description))
    }

    @Test
    fun aBlockWithoutATerminatorRunsToTheEnd() {
        val description = text("Intro", "[Lyrics]", "Line one", "Line two", "", "Line three", "Line four")

        assertEquals("Line one\nLine two\n\nLine three\nLine four", LyricsExtractor.extract(description))
    }

    @Test
    fun aTerminatorRightAfterTheMarkerLeavesNothing() {
        assertNull(LyricsExtractor.extract(text("[Lyrics]", "https://example.invalid", "Line one", "Line two", "Line three")))
    }

    // ---- validity ----

    @Test
    fun fewerThanThreeNonBlankLinesGivesNull() {
        assertNull(LyricsExtractor.extract(text("[Lyrics]", "Line one", "Line two", "=======", "Tail")))
        assertNull(LyricsExtractor.extract(text("[Lyrics]", "Line one", "", "", "Line two", "")))
        assertNull(LyricsExtractor.extract(text("[Lyrics]", "Line one")))
        assertNull(LyricsExtractor.extract("[Lyrics]"))
    }

    @Test
    fun exactlyThreeNonBlankLinesIsEnough() {
        assertEquals("a\n\nb\nc", LyricsExtractor.extract(text("[Lyrics]", "a", "", "b", "c")))
    }

    @Test
    fun aDescriptionWithoutAMarkerGivesNull() {
        assertNull(LyricsExtractor.extract(text("Song description", "Line one", "Line two", "Line three", "https://example.invalid")))
    }

    @Test
    fun nullAndBlankInputGiveNull() {
        assertNull(LyricsExtractor.extract(null))
        assertNull(LyricsExtractor.extract(""))
        assertNull(LyricsExtractor.extract("   \n\t\n  "))
    }

    // ---- normalising and cleaning up ----

    @Test
    fun lineBreaksAreNormalisedToLineFeeds() {
        val crlf = "[Lyrics]\r\nLine one\r\nLine two\r\n\r\nLine three\r\n=======\r\nTail"
        val cr = "[Lyrics]\rLine one\rLine two\r\rLine three\r=======\rTail"

        assertEquals("Line one\nLine two\n\nLine three", LyricsExtractor.extract(crlf))
        assertEquals("Line one\nLine two\n\nLine three", LyricsExtractor.extract(cr))
    }

    @Test
    fun aByteOrderMarkInFrontIsStripped() {
        assertEquals(block, LyricsExtractor.extract("﻿[Lyrics]\n$block"))
        assertEquals(block, LyricsExtractor.extract("﻿가사\r\n$block"))
    }

    @Test
    fun runsOfThreeOrMoreBlankLinesBecomeTwo() {
        val description = text("Lyrics", "a", "", "", "", "", "b", "", "", "c", "", "d", "   ", "\t", " ", "e")

        assertEquals("a\n\n\nb\n\n\nc\n\nd\n\n\ne", LyricsExtractor.extract(description))
    }

    @Test
    fun leadingAndTrailingBlankLinesOfTheBlockAreDropped() {
        val description = text("Lyrics", "", "", "Line one", "Line two", "Line three", "", "   ", "", "=======")

        assertEquals("Line one\nLine two\nLine three", LyricsExtractor.extract(description))
    }

    @Test
    fun trailingWhitespaceIsTrimmedButIndentationStays() {
        val description = text("Lyrics", "  Line one   ", "Line two\t", "Line three 　", "   ", "Line four")

        assertEquals("  Line one\nLine two\nLine three\n\nLine four", LyricsExtractor.extract(description))
    }

    @Test
    fun controlCharactersAreRemovedButTabsAndLineBreaksStay() {
        val description = "Lyrics\nLine\u0000 one\u0007\nLine\u007F two\u009F\n\u0001Line three\u000B\nA\tB\n\u0000\n\u0000"

        assertEquals("Line one\nLine two\nLine three\nA\tB", LyricsExtractor.extract(description))
    }

    @Test
    fun aControlCharacterNextToASeparatorDoesNotHideIt() {
        val description = text("Lyrics", "Line one", "Line two", "Line three", "=====\u0000", "Tail")

        assertEquals("Line one\nLine two\nLine three", LyricsExtractor.extract(description))
    }

    @Test
    fun theResultIsCutAtALineBoundaryWhenItWouldPassThirtyThousandCharacters() {
        // 40 lines of 1000 characters: 29 lines plus their line feeds are 29028 characters, the 30th line would make 30029.
        val longLines = (1..40).map { it.toString().padStart(4, '0') + "x".repeat(996) }
        val result = LyricsExtractor.extract(text("Lyrics", *longLines.toTypedArray()))!!

        assertEquals(longLines.take(29), result.split("\n"))
        assertEquals(29028, result.length)
    }

    @Test
    fun aBlockJustUnderTheLimitIsKeptWhole() {
        // 3000 lines of 9 characters: 3000 * 10 - 1 = 29999 characters; one more line would make 30009.
        val shortLines = (0 until 3001).map { "Line " + it.toString().padStart(4, '0') }
        val kept = LyricsExtractor.extract(text("Lyrics", *shortLines.take(3000).toTypedArray()))!!
        val cut = LyricsExtractor.extract(text("Lyrics", *shortLines.toTypedArray()))!!

        assertEquals(29999, kept.length)
        assertEquals(shortLines.take(3000).joinToString("\n"), kept)
        assertEquals(kept, cut)
    }

    @Test
    fun aCutDoesNotLeaveABlankLineAtTheEnd() {
        // 29 lines of 1000 characters (29028), a blank line (29029), then a line that does not fit: the cut lands right after the blank line.
        val longLines = (1..29).map { it.toString().padStart(4, '0') + "x".repeat(996) }
        val description = text("Lyrics", *longLines.toTypedArray(), "", "y".repeat(1000))

        val result = LyricsExtractor.extract(description)!!

        assertEquals(longLines, result.split("\n"))
    }

    @Test
    fun koreanEmojiAndOtherNonBmpTextSurvives() {
        val lyrics = text("한국어 가사 첫 줄 🎵", "日本語の歌詞 𠮷野家", "𝄞 music ♪ 🎶 가나다라", "Ünïcödé ñ")

        assertEquals(lyrics, LyricsExtractor.extract("[가사]\n$lyrics\n=======\nTail"))
    }

    @Test
    fun theResultNeverContainsACarriageReturn() {
        val result = LyricsExtractor.extract("[Lyrics]\r\nLine one\r\nLine two\rLine three\r\n")!!

        assertTrue('\r' !in result, result)
    }
}

package com.xgetsongs.shared.filename

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class FilenameFormatterTest {
    @Test
    fun formatsTheDocumentedExample() {
        assertEquals(
            "001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).mp3",
            FilenameFormatter.format(1, "소연 (SOYEON)", "퇴사할게여 (Narr. 기안84)"),
        )
    }

    @Test
    fun padsRankToThreeDigits() {
        assertTrue(FilenameFormatter.format(7, "A", "B").startsWith("007 "))
        assertTrue(FilenameFormatter.format(42, "A", "B").startsWith("042 "))
        assertTrue(FilenameFormatter.format(999, "A", "B").startsWith("999 "))
    }

    @Test
    fun rejectsRankOutsideOneTo999() {
        assertFailsWith<IllegalArgumentException> { FilenameFormatter.format(0, "A", "B") }
        assertFailsWith<IllegalArgumentException> { FilenameFormatter.format(1000, "A", "B") }
    }

    @Test
    fun replacesForbiddenCharactersWithFullWidthOnes() {
        assertEquals(
            "005 태연 (TAEYEON) - 만찬가 (晩餐歌 ／ BANSANKA) ： J-POP REMAKE Vol.1.mp3",
            FilenameFormatter.format(5, "태연 (TAEYEON)", "만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1"),
        )
        assertEquals("001 A - ＼／：＊？＂＜＞｜.mp3", FilenameFormatter.format(1, "A", "\\/:*?\"<>|"))
    }

    @Test
    fun dropsControlCharactersAndTrailingDotsAndSpaces() {
        assertEquals("001 A - Mr.mp3", FilenameFormatter.format(1, "A", "Mr.\t\n. "))
        assertEquals("001 A - B.C.mp3", FilenameFormatter.format(1, "A", "B.C"))
    }

    @Test
    fun emptyTitleGetsAPlaceholder() {
        assertEquals("001 A - untitled.mp3", FilenameFormatter.format(1, "A", "  "))
    }

    @Test
    fun longTitleIsTruncatedWithEllipsisKeepingRankAndArtist() {
        val name = FilenameFormatter.format(12, "ARTIST", "가".repeat(500))
        val base = name.removeSuffix(".mp3")
        assertEquals(FilenameFormatter.MAX_BASE_LENGTH, base.length)
        assertTrue(base.startsWith("012 ARTIST - 가"))
        assertTrue(base.endsWith("…"))
    }

    @Test
    fun veryLongArtistIsTruncatedToo() {
        val name = FilenameFormatter.format(1, "A".repeat(300), "Song")
        val base = name.removeSuffix(".mp3")
        assertTrue(base.length <= FilenameFormatter.MAX_BASE_LENGTH)
        assertTrue(base.startsWith("001 ${"A".repeat(10)}"))
        assertTrue(base.endsWith(" - Song"))
        assertTrue(base.contains("…"))
    }

    @Test
    fun truncationNeverSplitsASurrogatePair() {
        val name = FilenameFormatter.format(1, "A", "😀".repeat(200))
        val base = name.removeSuffix(".mp3")
        assertTrue(base.length <= FilenameFormatter.MAX_BASE_LENGTH)
        base.forEachIndexed { i, c ->
            if (c.isHighSurrogate()) assertTrue(base.getOrNull(i + 1)?.isLowSurrogate() == true, "lone high surrogate at $i")
            if (c.isLowSurrogate()) assertTrue(base.getOrNull(i - 1)?.isHighSurrogate() == true, "lone low surrogate at $i")
        }
    }
}

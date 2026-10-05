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

    @Test
    fun missingOrBlankPlaylistTitleGetsTheUntitledFolderName() {
        assertEquals("재생목록", FilenameFormatter.folderName(null))
        assertEquals("재생목록", FilenameFormatter.folderName(""))
        assertEquals("재생목록", FilenameFormatter.folderName("   "))
        assertEquals("재생목록", FilenameFormatter.folderName("\t\n"))
        assertEquals(FilenameFormatter.UNTITLED_PLAYLIST, FilenameFormatter.folderName(null))
    }

    @Test
    fun ordinaryAndKoreanPlaylistTitlesStayUnchanged() {
        assertEquals("Melon Daily Top 100", FilenameFormatter.folderName("Melon Daily Top 100"))
        assertEquals("멜론 일간 차트 TOP 100", FilenameFormatter.folderName("멜론 일간 차트 TOP 100"))
    }

    @Test
    fun folderNameReplacesForbiddenCharactersWithFullWidthOnes() {
        assertEquals("Best： Hits？ ＜2024＞", FilenameFormatter.folderName("Best: Hits? <2024>"))
        assertEquals("＼／：＊？＂＜＞｜", FilenameFormatter.folderName("\\/:*?\"<>|"))
    }

    @Test
    fun folderNameDropsTrailingDotsAndSpaces() {
        assertEquals("Mix", FilenameFormatter.folderName("Mix... "))
        assertEquals("Mix", FilenameFormatter.folderName("Mix. . "))
        assertEquals("Mix.Vol.1", FilenameFormatter.folderName("Mix.Vol.1."))
    }

    @Test
    fun folderNameThatIsOnlyDotsOrSpacesGetsTheUntitledFolderName() {
        assertEquals("재생목록", FilenameFormatter.folderName("..."))
        assertEquals("재생목록", FilenameFormatter.folderName(" . . "))
    }

    @Test
    fun folderNameDropsControlCharacters() {
        assertEquals("MyMix", FilenameFormatter.folderName("My\u0000Mix\u001F"))
        assertEquals("My Mix", FilenameFormatter.folderName("\tMy \u007FMix\n"))
        assertEquals("재생목록", FilenameFormatter.folderName("\u0001\u0002"))
    }

    @Test
    fun folderNameIsCutToEightyUnitsWithAnEllipsis() {
        val name = FilenameFormatter.folderName("a".repeat(81))
        assertEquals(FilenameFormatter.MAX_FOLDER_LENGTH, name.length)
        assertEquals("a".repeat(79) + "…", name)
        assertEquals("a".repeat(80), FilenameFormatter.folderName("a".repeat(80)))
    }

    @Test
    fun folderNameTruncationNeverEndsInALoneHighSurrogate() {
        val name = FilenameFormatter.folderName("🎵".repeat(100))
        assertTrue(name.length <= FilenameFormatter.MAX_FOLDER_LENGTH)
        assertTrue(name.endsWith("…"))
        name.forEachIndexed { i, c ->
            if (c.isHighSurrogate()) assertTrue(name.getOrNull(i + 1)?.isLowSurrogate() == true, "lone high surrogate at $i")
            if (c.isLowSurrogate()) assertTrue(name.getOrNull(i - 1)?.isHighSurrogate() == true, "lone low surrogate at $i")
        }
    }

    @Test
    fun reservedWindowsDeviceNamesGetAnUnderscoreAfterTheStem() {
        assertEquals("CON_", FilenameFormatter.folderName("CON"))
        assertEquals("nul_", FilenameFormatter.folderName("nul"))
        assertEquals("Com1_", FilenameFormatter.folderName("Com1"))
        assertEquals("LPT9_", FilenameFormatter.folderName("LPT9"))
        assertEquals("con_.txt", FilenameFormatter.folderName("con.txt"))
        assertEquals("aux_.tar.gz", FilenameFormatter.folderName("aux.tar.gz"))
        assertEquals("CON_ .txt", FilenameFormatter.folderName("CON .txt"))
        for (device in listOf("CON", "PRN", "AUX", "NUL") + (1..9).flatMap { listOf("COM$it", "LPT$it") }) {
            assertEquals("${device}_", FilenameFormatter.folderName(device), device)
        }
    }

    @Test
    fun namesThatMerelyResembleReservedOnesStayUnchanged() {
        assertEquals("Console", FilenameFormatter.folderName("Console"))
        assertEquals("CON TEST", FilenameFormatter.folderName("CON TEST"))
        assertEquals("NULL", FilenameFormatter.folderName("NULL"))
        assertEquals("COM0", FilenameFormatter.folderName("COM0"))
        assertEquals("COM10", FilenameFormatter.folderName("COM10"))
        assertEquals("my.con", FilenameFormatter.folderName("my.con"))
    }
}

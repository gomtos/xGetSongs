package com.xgetsongs.engine.lyrics.google

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs

/** [LyricsCardParser] against made-up pages (see GoogleTestSupport.kt). */
class LyricsCardParserTest {
    private fun found(html: String) = assertIs<GoogleLyricsResult.Found>(LyricsCardParser.parse(html))

    @Test
    fun linesOfAParagraphAreJoinedByNewlineAndParagraphsByOneBlankLine() {
        val result = found(testPage(testCard(testParagraph("더미 문장 1", "더미 문장 2"), testParagraph("더미 문장 3", "더미 문장 4", "더미 문장 5"))))

        assertEquals("더미 문장 1\n더미 문장 2\n\n더미 문장 3\n더미 문장 4\n더미 문장 5", result.lyrics)
        assertEquals(5, result.lineCount)
        assertEquals(2, result.paragraphCount)
    }

    @Test
    fun aParagraphOfOneLineIsAParagraph() {
        val result = found(testPage(testCard(testParagraph("앞 1", "앞 2"), testParagraph("후렴 더미"), testParagraph("뒤 1", "뒤 2"))))

        assertEquals("앞 1\n앞 2\n\n후렴 더미\n\n뒤 1\n뒤 2", result.lyrics)
        assertEquals(5, result.lineCount)
        assertEquals(3, result.paragraphCount)
    }

    @Test
    fun linesAreTrimmedAndEmptyOnesAreDropped() {
        val result = found(testPage(testCard(testParagraph("  더미   문장  ", "&nbsp;", "", "더미 둘", "더미 셋"))))

        assertEquals("더미 문장\n더미 둘\n더미 셋", result.lyrics)
        assertEquals(3, result.lineCount)
        assertEquals(1, result.paragraphCount)
    }

    @Test
    fun markupInsideALineKeepsItsText() {
        val html = testPage(testCard("""<div jsname="U8S5sf">${testLine("더미 <b>강조</b> 문장")}${testLine("둘")}${testLine("셋")}</div>"""))

        assertEquals("더미 강조 문장\n둘\n셋", found(html).lyrics)
    }

    @Test
    fun withoutALyricIdCardTheWbkContainerIsTheScope() {
        val html = testPage("""<div jsname="WbKHeb">${testParagraph("더미 1", "더미 2", "더미 3")}</div>""")

        assertEquals("더미 1\n더미 2\n더미 3", found(html).lyrics)
    }

    @Test
    fun withoutAnyContainerTheLineSpansOfThePageAreRead() {
        val html = testPage("""<div>${testLine("가")}${testLine("나")}</div><div>${testLine("다")}</div>""")

        val result = found(html)
        assertEquals("가\n나\n\n다", result.lyrics)
        assertEquals(2, result.paragraphCount)
    }

    @Test
    fun lineSpansOutsideTheCardAreIgnored() {
        val html = testPage(testParagraph("바깥 줄") + testCard(testParagraph("안 1", "안 2", "안 3")))

        assertEquals("안 1\n안 2\n안 3", found(html).lyrics)
    }

    @Test
    fun aPageWithoutAnyCardMarkerHasNoCard() {
        assertEquals(GoogleLyricsResult.NoCard, LyricsCardParser.parse(testPage("")))
    }

    @Test
    fun aCardWithoutLinesIsAFailedExtraction() {
        val html = testPage("""<div data-lyricid="id-1"><div jsname="WbKHeb"></div></div>""")

        assertEquals(GoogleLyricsResult.ExtractionFailed, LyricsCardParser.parse(html))
    }

    @Test
    fun aCardWithFewerThanThreeLinesIsAFailedExtraction() {
        assertEquals(GoogleLyricsResult.ExtractionFailed, LyricsCardParser.parse(testPage(testCard(testParagraph("더미 1", "더미 2")))))
    }

    @Test
    fun aFoundResultNeverPrintsTheLyrics() {
        val result = found(testPage(testCard(testParagraph("비밀 문장 1", "비밀 문장 2", "비밀 문장 3"))))

        assertFalse("비밀" in result.toString())
    }
}

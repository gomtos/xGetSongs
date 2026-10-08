package com.xgetsongs.shared.log

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class LogRedactionTest {
    private fun paths(text: String) = LogRedaction.redactPaths(text)

    @Test
    fun aPathWithSpacesInItsFolderNamesIsReplacedAsAWhole() {
        val text = "NoSuchFileException: C:\\Users\\Some User\\AppData\\Roaming\\xGetSongs\\work\\job-1\\001 Artist - Title.f251.webm"

        assertEquals("NoSuchFileException: <경로>", paths(text))
    }

    @Test
    fun twoPathsSeparatedByAnArrowAreReplacedOneByOne() {
        val text = "C:\\work\\job 1\\001 A - B.mp3 -> D:\\Music\\My Playlist\\001 A - B.mp3"

        assertEquals("<경로> -> <경로>", paths(text))
    }

    @Test
    fun theSystemReasonAfterTheLastPathStaysReadable() {
        val text = "FileSystemException: C:\\work\\job-1\\001 A - B.mp3 -> D:\\Music\\My Playlist\\001 A - B.mp3: " +
            "The process cannot access the file because it is being used by another process"

        assertEquals(
            "FileSystemException: <경로> -> <경로>: The process cannot access the file because it is being used by another process",
            paths(text),
        )
    }

    @Test
    fun aSingleFilePathFollowedByAReasonKeepsTheReason() {
        assertEquals("<경로>: Access is denied", paths("D:\\Music\\a.mp3: Access is denied"))
    }

    @Test
    fun aUncPathIsReplacedWithItsHostAndShare() {
        assertEquals("<경로>: Access is denied", paths("\\\\NAS\\Music Share\\Playlists\\001 A - B.mp3: Access is denied"))
        assertEquals("open <경로>:failed", paths("open \\\\server\\share\\dir\\f.mp3:failed"))
    }

    @Test
    fun aLongPathPrefixIsPartOfThePath() {
        assertEquals("error: <경로>", paths("error: \\\\?\\C:\\very\\long\\path\\file.mp3"))
    }

    @Test
    fun aPathWithDoubledBackslashesLikeAPythonErrorIsReplaced() {
        val text = "ERROR: unable to open for writing: [Errno 13] Permission denied: 'C:\\\\Users\\\\Some User\\\\Music\\\\a.mp3'"

        val redacted = paths(text)

        assertFalse("Users" in redacted, redacted)
        assertFalse("Music" in redacted, redacted)
        assertTrue(redacted.startsWith("ERROR: unable to open for writing: [Errno 13] Permission denied: '<경로>"), redacted)
    }

    @Test
    fun aPathEndsAtTheEndOfTheLine() {
        assertEquals("first <경로>\nnext line stays\n<경로>\r\ndone", paths("first C:\\a\\b\nnext line stays\nD:\\c\\d\r\ndone"))
    }

    @Test
    fun everyPathInSeveralLinesIsReplaced() {
        assertEquals("<경로>\n<경로>\n<경로>", paths("C:\\a\\1.mp3\nD:\\b\\2.mp3\n\\\\h\\s\\3.mp3"))
    }

    @Test
    fun textWithoutPathsIsUnchanged() {
        for (text in listOf(
            "",
            "연결이 끊어졌습니다 (HTTP 429)",
            "HTTP 403: Forbidden",
            "https://www.youtube.com/watch?v=abc&list=PL1",
            "ERROR: [youtube] abcdefghijk: Video unavailable",
            "at 10:30 the job (a:b) ended",
            "C:",
            "C:\\",
            "a\\b",
            "5 / 3 | 2",
            "-> arrow ->",
        )) {
            assertEquals(text, paths(text), text)
        }
    }

    @Test
    fun aKnownNameIsReplacedIgnoringCase() {
        val redacted = LogRedaction.redact("Cannot write 001 a - b.MP3 now", listOf("001 A - B.mp3"))

        assertEquals("Cannot write <파일명> now", redacted)
    }

    @Test
    fun theNameWithoutItsExtensionIsReplacedToo() {
        assertEquals("<파일명> and <파일명>", LogRedaction.redact("001 A - B.mp3 and 001 a - b", listOf("001 A - B.mp3")))
    }

    @Test
    fun aKnownNameInsideAPathGoesWithThePath() {
        val redacted = LogRedaction.redact("NoSuchFileException: C:\\Users\\me\\Music\\001 A - B.mp3", listOf("001 A - B.mp3"))

        assertEquals("NoSuchFileException: <경로>", redacted)
    }

    @Test
    fun severalKnownNamesAndRepeatedOccurrencesAreAllReplaced() {
        val redacted = LogRedaction.redact("x 001 A - B.mp3, 002 C - D.mp3, 001 A - B.mp3", listOf("001 A - B.mp3", "002 C - D.mp3"))

        assertEquals("x <파일명>, <파일명>, <파일명>", redacted)
    }

    @Test
    fun blankAndVeryShortNamesAreIgnored() {
        assertEquals("a b c", LogRedaction.redact("a b c", listOf("", "  ", "a", "ab")))
    }

    @Test
    fun noKnownNamesJustRedactsThePaths() {
        assertEquals("<경로>: reason", LogRedaction.redact("C:\\a\\b.mp3: reason", emptyList()))
    }
}

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

    // ---- two paths in a row, forward slashes, long prefixes, URIs -----------------------------

    @Test
    fun twoPathsSeparatedByACommaAreBothRedactedCompletely() {
        assertEquals("<경로>, <경로>", paths("C:\\a\\b.mp3, D:\\Music\\My Playlist\\x.mp3"))
        assertEquals("<경로>,<경로>", paths("C:\\a\\b.mp3,D:\\Music\\My Playlist\\x.mp3"))
        assertEquals("<경로>; <경로>: Access is denied", paths("C:\\a\\b.mp3; D:\\x\\y z.mp3: Access is denied"))
    }

    @Test
    fun aPathThatStartsRightAfterTheEndOfAnotherIsStillAPath() {
        assertEquals("<경로><경로>", paths("C:\\a\\fooD:\\Music\\x.mp3"))
    }

    @Test
    fun forwardSlashesRightAfterTheDriveAreAccepted() {
        assertEquals("<경로>", paths("C:/Users/me/Song Title.mp3"))
        assertEquals("failed: <경로>: Access is denied", paths("failed: c:/Users/me/Song Title.mp3: Access is denied"))
    }

    @Test
    fun mixedSeparatorsAreOnePath() {
        assertEquals("<경로>", paths("D:\\Music/My Playlist/001 A - B.mp3"))
        assertEquals("<경로>", paths("D:/Music\\My Playlist\\001 A - B.mp3"))
    }

    @Test
    fun theVerbatimPrefixesAreHandled() {
        assertEquals("error: <경로>", paths("error: \\\\?\\C:\\x\\y z\\a.mp3"))
        assertEquals("error: <경로>: Access is denied", paths("error: \\\\?\\UNC\\server\\share\\dir\\a.mp3: Access is denied"))
        assertEquals("<경로>", paths("\\\\?\\unc\\SERVER\\Music Share\\a.mp3"))
        assertEquals("<경로>", paths("\\\\.\\C:\\x\\a.mp3"))
    }

    @Test
    fun aFileUriIsRedactedAsAWhole() {
        assertEquals("cannot read <경로>", paths("cannot read file:///C:/Users/me/Music/My%20Song.mp3"))
        assertEquals("cannot read <경로>", paths("cannot read FILE:///c:/Users/me/a.mp3"))
        assertEquals("<경로>: reason", paths("file:///D:/Music/My Playlist/a.mp3: reason"))
    }

    @Test
    fun aHttpUrlIsNotAPath() {
        for (text in listOf(
            "https://www.youtube.com/watch?v=abc&list=PL1",
            "see http://example.com/a/b and https://example.com:8080/x",
            "ftp://host/dir",
        )) {
            assertEquals(text, paths(text), text)
        }
    }

    @Test
    fun relativePathsAreNotRecognisedOnPurpose() {
        // Without a drive or a UNC host there is nothing to tell a path from a sentence: these stay as they are.
        for (text in listOf("Music\\a.mp3", ".\\a\\b.mp3", "..\\x\\y.mp3", "a/b/c.mp3", "/usr/lib/x", "\\just\\absolute\\on\\this\\drive.mp3")) {
            assertEquals(text, paths(text), text)
        }
    }

    // ---- hostile input -----------------------------------------------------------------------

    @Test
    fun aPathOfFiveThousandPartsIsRedactedWithoutAnError() {
        val text = "C:" + "\\a".repeat(5_000)

        val redacted = paths(text)

        assertEquals("<경로>", redacted)
    }

    @Test
    fun aHugeHostileInputIsCutAndRedactedQuickly() {
        val shapes = listOf(
            "C:" + "\\a".repeat(100_000),
            "C:\\".repeat(66_000),
            "\\\\".repeat(100_000),
            "a:\\ ".repeat(50_000),
            "C:\\a -> ".repeat(25_000),
            "x".repeat(200_000),
            "file:///".repeat(25_000),
            "\\\\?\\UNC\\".repeat(25_000),
        )
        val mark = kotlin.time.TimeSource.Monotonic.markNow()

        for (text in shapes) {
            val redacted = paths(text)
            // The input is cut; the output can be a little longer (a one-character path becomes a placeholder and its space stays).
            assertTrue(redacted.length <= 2 * LogRedaction.MAX_TEXT_LENGTH, "cut to the limit: ${redacted.length}")
        }

        assertTrue(mark.elapsedNow().inWholeSeconds < 10, "took ${mark.elapsedNow()}")
    }

    @Test
    fun aTextOverTheLimitIsCutAndEndsWithAnEllipsis() {
        val text = "x".repeat(10_000)

        val redacted = paths(text)

        assertEquals("x".repeat(LogRedaction.MAX_TEXT_LENGTH) + "…", redacted)
        assertEquals("x".repeat(LogRedaction.MAX_TEXT_LENGTH), paths("x".repeat(LogRedaction.MAX_TEXT_LENGTH)), "exactly the limit is not cut")
    }

    @Test
    fun theCutNeverSplitsASurrogatePair() {
        val text = "x".repeat(LogRedaction.MAX_TEXT_LENGTH - 1) + "\uD83C\uDFB5" + "tail" // the pair starts at the last kept index

        val redacted = paths(text)

        assertEquals("x".repeat(LogRedaction.MAX_TEXT_LENGTH - 1) + "…", redacted)
    }

    @Test
    fun whatIsCutOffIsNotKeptEvenWhenItWasAPath() {
        val text = "x".repeat(LogRedaction.MAX_TEXT_LENGTH) + " C:\\Users\\secret\\a.mp3"

        assertFalse("secret" in paths(text))
    }

    // ---- known names with regular expression characters -------------------------------------

    @Test
    fun aKnownNameWithRegexCharactersIsReplacedLiterally() {
        val name = "A (feat. B) [x]$1.mp3"

        val redacted = LogRedaction.redact("could not write A (feat. B) [x]$1.mp3 and a (FEAT. b) [X]$1", listOf(name))

        assertEquals("could not write <파일명> and <파일명>", redacted)
    }

    @Test
    fun aNameThatLooksLikeAPatternDoesNotMatchOtherText() {
        val redacted = LogRedaction.redact("Axxx and A.*", listOf("A.*"))

        assertEquals("Axxx and <파일명>", redacted)
    }

    @Test
    fun aReplacementPlaceholderIsNotExpandedAsAReference() {
        assertEquals("<경로>", paths("C:\\a\\b\$1\\c.mp3"))
        assertEquals("<파일명>", LogRedaction.redact("\$0\$1", listOf("\$0\$1")))
    }
}

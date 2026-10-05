package com.xgetsongs.engine.tags

import com.xgetsongs.engine.testutil.Id3v2Tag
import java.io.IOException
import java.nio.file.DirectoryNotEmptyException
import java.nio.file.Files
import java.nio.file.NoSuchFileException
import java.nio.file.Path
import kotlin.random.Random
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class Id3CommentTest {
    private val dir: Path = Files.createTempDirectory("xgs-id3comment")
    private val file = dir.resolve("track.mp3")

    @AfterTest
    fun cleanUp() {
        dir.toFile().deleteRecursively()
    }

    private fun bytes(vararg values: Int) = ByteArray(values.size) { values[it].toByte() }

    private fun ascii(text: String) = text.toByteArray(Charsets.ISO_8859_1)

    private fun bigEndian(n: Int) = bytes((n shr 24) and 0xFF, (n shr 16) and 0xFF, (n shr 8) and 0xFF, n and 0xFF)

    private fun syncsafe(n: Int) = bytes((n shr 21) and 0x7F, (n shr 14) and 0x7F, (n shr 7) and 0x7F, n and 0x7F)

    /** An ID3v2.3 frame: id, big-endian size (plain, not syncsafe), no flags, body. */
    private fun frame(id: String, body: ByteArray) = ascii(id) + bigEndian(body.size) + bytes(0, 0) + body

    /** An ID3v2.3 text frame in ISO-8859-1. */
    private fun textFrame(id: String, text: String) = frame(id, bytes(0) + ascii(text))

    /** A file that starts with an ID3v2 tag: header, [frames], [padding] zero bytes, then [audio]. */
    private fun tagged(
        frames: List<ByteArray>,
        padding: Int,
        audio: ByteArray,
        major: Int = 3,
        flags: Int = 0,
        size: Int? = null,
    ): ByteArray {
        val area = frames.fold(ByteArray(0)) { all, next -> all + next } + ByteArray(padding)
        return ascii("ID3") + bytes(major, 0, flags) + syncsafe(size ?: area.size) + area + audio
    }

    // What an mp3 frame header looks like, then arbitrary bytes: the engine must never touch them.
    private val audio = bytes(0xFF, 0xFB, 0x90, 0x00) + Random(7).nextBytes(2000)

    // COMM frame for the text "hi", written out by hand: 43 4F 4D 4D = "COMM", size 7 (big-endian), no flags, then
    // encoding 0, language "eng", an empty description (one 00) and the text.
    private val commHi = bytes(0x43, 0x4F, 0x4D, 0x4D, 0, 0, 0, 7, 0, 0, 0, 0x65, 0x6E, 0x67, 0, 0x68, 0x69)

    // COMM frame for the text U+D55C: size 12, encoding 1 (UTF-16 with BOM), language "eng", an empty description
    // (BOM FF FE and terminator 00 00), then the text (BOM FF FE and the UTF-16LE bytes 5C D5).
    private val commHan = bytes(
        0x43, 0x4F, 0x4D, 0x4D, 0, 0, 0, 12, 0, 0,
        1, 0x65, 0x6E, 0x67, 0xFF, 0xFE, 0, 0, 0xFF, 0xFE, 0x5C, 0xD5,
    )

    private val title = textFrame("TIT2", "Song")
    private val artist = textFrame("TPE1", "Me")

    private fun filesInDir(): List<String> = Files.list(dir).use { stream -> stream.map { it.fileName.toString() }.sorted().toList() }

    @Test
    fun addsAnAsciiCommentRightAfterTheLastFrameAndBeforeThePadding() {
        Files.write(file, tagged(listOf(title, artist), padding = 10, audio = audio))

        Id3Comment.addComment(file, "hi")

        assertContentEquals(tagged(listOf(title, artist, commHi), padding = 10, audio = audio), Files.readAllBytes(file))
        assertEquals(
            listOf(Id3v2Tag.Comment(0, "eng", "", "hi")),
            Id3v2Tag.read(file).comments(),
        )
    }

    @Test
    fun addsANonAsciiCommentAsUtf16WithAByteOrderMarkAndAnEmptyDescription() {
        Files.write(file, tagged(listOf(title, artist), padding = 10, audio = audio))

        Id3Comment.addComment(file, "한")

        assertContentEquals(tagged(listOf(title, artist, commHan), padding = 10, audio = audio), Files.readAllBytes(file))
        assertEquals(
            listOf(Id3v2Tag.Comment(1, "eng", "", "한")),
            Id3v2Tag.read(file).comments(),
        )
    }

    @Test
    fun aTextWithASingleNonAsciiCharacterUsesUtf16ForTheWholeText() {
        Files.write(file, tagged(listOf(title), padding = 0, audio = audio))

        Id3Comment.addComment(file, "café \"x\" https://www.youtube.com/watch?v=abc\r\nline two 🎵")

        assertEquals(
            listOf(Id3v2Tag.Comment(1, "eng", "", "café \"x\" https://www.youtube.com/watch?v=abc\r\nline two 🎵")),
            Id3v2Tag.read(file).comments(),
        )
    }

    @Test
    fun worksWhenTheTagHasNoPadding() {
        Files.write(file, tagged(listOf(title), padding = 0, audio = audio))

        Id3Comment.addComment(file, "hi")

        assertContentEquals(tagged(listOf(title, commHi), padding = 0, audio = audio), Files.readAllBytes(file))
    }

    @Test
    fun theTagSizeIsSyncsafeAndCrossesA7BitBoundary() {
        // 10-byte frame header + 110-byte body = 120 bytes of tag; the 17-byte comment makes it 137 = 1 * 128 + 9.
        val big = frame("TIT2", bytes(0) + ascii("x".repeat(109)))
        Files.write(file, tagged(listOf(big), padding = 0, audio = audio))

        Id3Comment.addComment(file, "hi")

        val result = Files.readAllBytes(file)
        assertContentEquals(bytes(0x49, 0x44, 0x33, 3, 0, 0, 0, 0, 1, 9), result.copyOfRange(0, 10))
        assertContentEquals(tagged(listOf(big, commHi), padding = 0, audio = audio), result)
    }

    @Test
    fun theOtherFramesAndTheAudioStayByteIdenticalInALargeTag() {
        val picture = frame("APIC", bytes(0) + ascii("image/jpeg") + bytes(0, 3) + ascii("Album cover") + bytes(0) + Random(3).nextBytes(40_000))
        val mp3Audio = bytes(0xFF, 0xFB, 0x90, 0x00) + Random(11).nextBytes(300_000)
        val frames = listOf(title, artist, textFrame("TALB", "Album"), picture)
        Files.write(file, tagged(frames, padding = 10, audio = mp3Audio))

        Id3Comment.addComment(file, "hi")

        val result = Files.readAllBytes(file)
        assertContentEquals(tagged(frames + listOf(commHi), padding = 10, audio = mp3Audio), result)
        assertEquals(listOf("TIT2", "TPE1", "TALB", "APIC", "COMM"), Id3v2Tag.parse(result).ids)
    }

    @Test
    fun aFileWithoutAnId3HeaderGetsAFreshTagWithOnlyTheComment() {
        Files.write(file, audio)

        Id3Comment.addComment(file, "hi")

        assertContentEquals(ascii("ID3") + bytes(3, 0, 0) + syncsafe(commHi.size) + commHi + audio, Files.readAllBytes(file))
        assertEquals(listOf("COMM"), Id3v2Tag.read(file).ids)
    }

    @Test
    fun anEmptyFileGetsAFreshTagToo() {
        Files.write(file, ByteArray(0))

        Id3Comment.addComment(file, "hi")

        assertContentEquals(ascii("ID3") + bytes(3, 0, 0) + syncsafe(commHi.size) + commHi, Files.readAllBytes(file))
    }

    @Test
    fun noTemporaryFileStaysBehind() {
        Files.write(file, tagged(listOf(title), padding = 10, audio = audio))

        Id3Comment.addComment(file, "hi")

        assertEquals(listOf("track.mp3"), filesInDir())
    }

    @Test
    fun layoutsItCannotRewriteAreRejectedAndLeaveTheFileUntouched() {
        val cases = mapOf(
            "version 2.4" to tagged(listOf(title), 10, audio, major = 4),
            "version 2.2" to tagged(listOf(title), 10, audio, major = 2),
            "unsynchronisation flag" to tagged(listOf(title), 10, audio, flags = 0x80),
            "extended header flag" to tagged(listOf(title), 10, audio, flags = 0x40),
            "undefined flag 0x10" to tagged(listOf(title), 10, audio, flags = 0x10),
            "undefined flag 0x08" to tagged(listOf(title), 10, audio, flags = 0x08),
            "undefined flag 0x01" to tagged(listOf(title), 10, audio, flags = 0x01),
            "undefined flag with the experimental one" to tagged(listOf(title), 10, audio, flags = 0x24),
        )
        for ((name, content) in cases) {
            Files.write(file, content)

            val error = assertFailsWith<IOException>(name) { Id3Comment.addComment(file, "hi") }

            assertEquals("unsupported ID3 tag layout", error.message, name)
            assertContentEquals(content, Files.readAllBytes(file), name)
            assertEquals(listOf("track.mp3"), filesInDir(), name)
        }
    }

    @Test
    fun theExperimentalFlagDoesNotChangeTheLayoutAndIsKept() {
        Files.write(file, tagged(listOf(title, artist), padding = 10, audio = audio, flags = 0x20))

        Id3Comment.addComment(file, "hi")

        val result = Files.readAllBytes(file)
        assertContentEquals(tagged(listOf(title, artist, commHi), padding = 10, audio = audio, flags = 0x20), result)
        assertEquals(0x20, result[5].toInt() and 0xFF)
        assertEquals(listOf(Id3v2Tag.Comment(0, "eng", "", "hi")), Id3v2Tag.read(file).comments())
    }

    @Test
    fun aTagWithAHeaderOnlyGetsTheCommentAndACorrectSize() {
        Files.write(file, tagged(emptyList(), padding = 0, audio = audio))

        Id3Comment.addComment(file, "hi")

        val result = Files.readAllBytes(file)
        assertContentEquals(tagged(listOf(commHi), padding = 0, audio = audio), result)
        assertContentEquals(ascii("ID3") + bytes(3, 0, 0) + syncsafe(commHi.size), result.copyOfRange(0, 10))
        assertEquals(listOf("COMM"), Id3v2Tag.read(file).ids)
    }

    @Test
    fun aFailingCleanupDoesNotHideTheRealError() {
        val content = tagged(listOf(title), padding = 10, audio = audio)
        Files.write(file, content)
        // The temporary file's name is taken by a folder that is not empty: writing fails, and so does deleting it.
        val blocker = Files.createDirectory(dir.resolve("track.mp3.id3tmp"))
        Files.write(blocker.resolve("keep.txt"), bytes(1))

        val error = assertFailsWith<IOException> { Id3Comment.addComment(file, "hi") }

        assertFalse(error is DirectoryNotEmptyException, "the cleanup failure replaced the real error: $error")
        assertTrue(error.suppressed.any { it is DirectoryNotEmptyException }, "the cleanup failure is attached: ${error.suppressed.toList()}")
        assertContentEquals(content, Files.readAllBytes(file))
    }

    @Test
    fun brokenTagsAreRejectedAndLeaveTheFileUntouched() {
        val cases = mapOf(
            "header cut short" to ascii("ID3") + bytes(3, 0),
            "tag larger than the file" to tagged(listOf(title), 0, ByteArray(0), size = 500),
            "size that is not syncsafe" to ascii("ID3") + bytes(3, 0, 0, 0, 0, 0x80, 0x10) + title,
            "frame running past the tag" to ascii("ID3") + bytes(3, 0, 0) + syncsafe(14) + ascii("TIT2") + bigEndian(500) + bytes(0, 0, 0, 0, 0, 0),
        )
        for ((name, content) in cases) {
            Files.write(file, content)

            assertFailsWith<IOException>(name) { Id3Comment.addComment(file, "hi") }

            assertContentEquals(content, Files.readAllBytes(file), name)
            assertEquals(listOf("track.mp3"), filesInDir(), name)
        }
    }

    @Test
    fun aMissingFileIsAnIoException() {
        assertFailsWith<NoSuchFileException> { Id3Comment.addComment(dir.resolve("missing.mp3"), "hi") }
        assertEquals(emptyList<String>(), filesInDir())
    }
}

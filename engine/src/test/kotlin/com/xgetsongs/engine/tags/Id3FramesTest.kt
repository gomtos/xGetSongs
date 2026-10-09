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

class Id3FramesTest {
    private val dir: Path = Files.createTempDirectory("xgs-id3frames")
    private val file = dir.resolve("track.mp3")

    @AfterTest
    fun cleanUp() {
        dir.toFile().deleteRecursively()
    }

    private fun bytes(vararg values: Int) = ByteArray(values.size) { values[it].toByte() }

    private fun ascii(text: String) = text.toByteArray(Charsets.ISO_8859_1)

    private fun syncsafe(n: Int) = bytes((n shr 21) and 0x7F, (n shr 14) and 0x7F, (n shr 7) and 0x7F, n and 0x7F)

    /** An ID3v2.4 frame: id, syncsafe size, no flags, body. */
    private fun frame(id: String, body: ByteArray) = ascii(id) + syncsafe(body.size) + bytes(0, 0) + body

    /** An ID3v2.4 text frame in UTF-8 (encoding 3), like the ones ffmpeg writes. */
    private fun textFrame(id: String, text: String) = frame(id, bytes(3) + text.toByteArray(Charsets.UTF_8))

    /** A file that starts with an ID3v2 tag: header, [frames], [padding] zero bytes, then [audio]. */
    private fun tagged(
        frames: List<ByteArray>,
        padding: Int,
        audio: ByteArray,
        major: Int = 4,
        flags: Int = 0,
        size: Int? = null,
    ): ByteArray {
        val area = frames.fold(ByteArray(0)) { all, next -> all + next } + ByteArray(padding)
        return ascii("ID3") + bytes(major, 0, flags) + syncsafe(size ?: area.size) + area + audio
    }

    // What an mp3 frame header looks like, then arbitrary bytes: the engine must never touch them.
    private val audio = bytes(0xFF, 0xFB, 0x90, 0x00) + Random(7).nextBytes(2000)

    // COMM frame for the text "hi", written out by hand: 43 4F 4D 4D = "COMM", size 7 (syncsafe), no flags, then
    // encoding 3 (UTF-8), language "eng", an empty description (one 00) and the text.
    private val commHi = bytes(0x43, 0x4F, 0x4D, 0x4D, 0, 0, 0, 7, 0, 0, 3, 0x65, 0x6E, 0x67, 0, 0x68, 0x69)

    // COMM frame for the text U+D55C: size 8, encoding 3, language "eng", an empty description (one 00), then the
    // text in UTF-8 (ED 95 9C, no byte order mark).
    private val commHan = bytes(
        0x43, 0x4F, 0x4D, 0x4D, 0, 0, 0, 8, 0, 0,
        3, 0x65, 0x6E, 0x67, 0, 0xED, 0x95, 0x9C,
    )

    private val title = textFrame("TIT2", "Song")
    private val artist = textFrame("TPE1", "Me")

    private fun filesInDir(): List<String> = Files.list(dir).use { stream -> stream.map { it.fileName.toString() }.sorted().toList() }

    @Test
    fun addsAnAsciiCommentAsUtf8RightAfterTheLastFrameAndBeforeThePadding() {
        Files.write(file, tagged(listOf(title, artist), padding = 10, audio = audio))

        Id3Frames.add(file, "hi", null)

        assertContentEquals(tagged(listOf(title, artist, commHi), padding = 10, audio = audio), Files.readAllBytes(file))
        assertEquals(
            listOf(Id3v2Tag.Comment(3, "eng", "", "hi")),
            Id3v2Tag.read(file).comments(),
        )
    }

    @Test
    fun addsANonAsciiCommentAsUtf8WithoutAByteOrderMarkAndWithAnEmptyDescription() {
        Files.write(file, tagged(listOf(title, artist), padding = 10, audio = audio))

        Id3Frames.add(file, "한", null)

        assertContentEquals(tagged(listOf(title, artist, commHan), padding = 10, audio = audio), Files.readAllBytes(file))
        assertEquals(
            listOf(Id3v2Tag.Comment(3, "eng", "", "한")),
            Id3v2Tag.read(file).comments(),
        )
    }

    @Test
    fun aCommentWithAccentsQuotesAnEmojiAndALineBreakRoundTripsAsOneUtf8Text() {
        Files.write(file, tagged(listOf(title), padding = 0, audio = audio))
        val text = "café \"x\" https://www.youtube.com/watch?v=abc\r\nline two 🎵"

        Id3Frames.add(file, text, null)

        assertEquals(listOf(Id3v2Tag.Comment(3, "eng", "", text)), Id3v2Tag.read(file).comments())
    }

    @Test
    fun worksWhenTheTagHasNoPadding() {
        Files.write(file, tagged(listOf(title), padding = 0, audio = audio))

        Id3Frames.add(file, "hi", null)

        assertContentEquals(tagged(listOf(title, commHi), padding = 0, audio = audio), Files.readAllBytes(file))
    }

    @Test
    fun theTagSizeIsSyncsafeAndCrossesA7BitBoundary() {
        // 10-byte frame header + 110-byte body = 120 bytes of tag; the 17-byte comment makes it 137 = 1 * 128 + 9.
        val big = frame("TIT2", bytes(3) + ascii("x".repeat(109)))
        Files.write(file, tagged(listOf(big), padding = 0, audio = audio))

        Id3Frames.add(file, "hi", null)

        val result = Files.readAllBytes(file)
        assertContentEquals(bytes(0x49, 0x44, 0x33, 4, 0, 0, 0, 0, 1, 9), result.copyOfRange(0, 10))
        assertContentEquals(tagged(listOf(big, commHi), padding = 0, audio = audio), result)
    }

    @Test
    fun theOtherFramesAndTheAudioStayByteIdenticalInALargeTag() {
        val picture = frame("APIC", bytes(0) + ascii("image/jpeg") + bytes(0, 3) + ascii("Album cover") + bytes(0) + Random(3).nextBytes(40_000))
        val mp3Audio = bytes(0xFF, 0xFB, 0x90, 0x00) + Random(11).nextBytes(300_000)
        val koreanTitle = textFrame("TIT2", "한글 제목 🎵")
        val frames = listOf(koreanTitle, artist, textFrame("TALB", "Album"), picture)
        Files.write(file, tagged(frames, padding = 10, audio = mp3Audio))

        Id3Frames.add(file, "hi", null)

        val result = Files.readAllBytes(file)
        assertContentEquals(tagged(frames + listOf(commHi), padding = 10, audio = mp3Audio), result)
        assertEquals(listOf("TIT2", "TPE1", "TALB", "APIC", "COMM"), Id3v2Tag.parse(result).ids)
    }

    @Test
    fun aFileWithoutAnId3HeaderGetsAFreshV24TagWithOnlyTheComment() {
        Files.write(file, audio)

        Id3Frames.add(file, "hi", null)

        assertContentEquals(ascii("ID3") + bytes(4, 0, 0) + syncsafe(commHi.size) + commHi + audio, Files.readAllBytes(file))
        assertEquals(listOf("COMM"), Id3v2Tag.read(file).ids)
        assertEquals(4, Id3v2Tag.read(file).version)
    }

    @Test
    fun anEmptyFileGetsAFreshTagToo() {
        Files.write(file, ByteArray(0))

        Id3Frames.add(file, "hi", null)

        assertContentEquals(ascii("ID3") + bytes(4, 0, 0) + syncsafe(commHi.size) + commHi, Files.readAllBytes(file))
    }

    @Test
    fun noTemporaryFileStaysBehind() {
        Files.write(file, tagged(listOf(title), padding = 10, audio = audio))

        Id3Frames.add(file, "hi", null)

        assertEquals(listOf("track.mp3"), filesInDir())
    }

    @Test
    fun layoutsItCannotRewriteAreRejectedAndLeaveTheFileUntouched() {
        val cases = mapOf(
            "version 2.3" to tagged(listOf(title), 10, audio, major = 3),
            "version 2.2" to tagged(listOf(title), 10, audio, major = 2),
            "unsynchronisation flag" to tagged(listOf(title), 10, audio, flags = 0x80),
            "extended header flag" to tagged(listOf(title), 10, audio, flags = 0x40),
            "footer flag" to tagged(listOf(title), 10, audio, flags = 0x10),
            "undefined flag 0x08" to tagged(listOf(title), 10, audio, flags = 0x08),
            "undefined flag 0x01" to tagged(listOf(title), 10, audio, flags = 0x01),
            "undefined flag with the experimental one" to tagged(listOf(title), 10, audio, flags = 0x24),
        )
        for ((name, content) in cases) {
            Files.write(file, content)

            val error = assertFailsWith<IOException>(name) { Id3Frames.add(file, "hi", null) }

            assertEquals("unsupported ID3 tag layout", error.message, name)
            assertContentEquals(content, Files.readAllBytes(file), name)
            assertEquals(listOf("track.mp3"), filesInDir(), name)
        }
    }

    @Test
    fun theExperimentalFlagDoesNotChangeTheLayoutAndIsKept() {
        Files.write(file, tagged(listOf(title, artist), padding = 10, audio = audio, flags = 0x20))

        Id3Frames.add(file, "hi", null)

        val result = Files.readAllBytes(file)
        assertContentEquals(tagged(listOf(title, artist, commHi), padding = 10, audio = audio, flags = 0x20), result)
        assertEquals(0x20, result[5].toInt() and 0xFF)
        assertEquals(listOf(Id3v2Tag.Comment(3, "eng", "", "hi")), Id3v2Tag.read(file).comments())
    }

    @Test
    fun aTagWithAHeaderOnlyGetsTheCommentAndACorrectSize() {
        Files.write(file, tagged(emptyList(), padding = 0, audio = audio))

        Id3Frames.add(file, "hi", null)

        val result = Files.readAllBytes(file)
        assertContentEquals(tagged(listOf(commHi), padding = 0, audio = audio), result)
        assertContentEquals(ascii("ID3") + bytes(4, 0, 0) + syncsafe(commHi.size), result.copyOfRange(0, 10))
        assertEquals(listOf("COMM"), Id3v2Tag.read(file).ids)
    }

    @Test
    fun aFailingCleanupDoesNotHideTheRealError() {
        val content = tagged(listOf(title), padding = 10, audio = audio)
        Files.write(file, content)
        // The temporary file's name is taken by a folder that is not empty: writing fails, and so does deleting it.
        val blocker = Files.createDirectory(dir.resolve("track.mp3.id3tmp"))
        Files.write(blocker.resolve("keep.txt"), bytes(1))

        val error = assertFailsWith<IOException> { Id3Frames.add(file, "hi", null) }

        assertFalse(error is DirectoryNotEmptyException, "the cleanup failure replaced the real error: $error")
        assertTrue(error.suppressed.any { it is DirectoryNotEmptyException }, "the cleanup failure is attached: ${error.suppressed.toList()}")
        assertContentEquals(content, Files.readAllBytes(file))
    }

    @Test
    fun brokenTagsAreRejectedAndLeaveTheFileUntouched() {
        val cases = mapOf(
            "header cut short" to ascii("ID3") + bytes(4, 0),
            "tag larger than the file" to tagged(listOf(title), 0, ByteArray(0), size = 500),
            "size that is not syncsafe" to ascii("ID3") + bytes(4, 0, 0, 0, 0, 0x80, 0x10) + title,
            "frame running past the tag" to ascii("ID3") + bytes(4, 0, 0) + syncsafe(14) + ascii("TIT2") + syncsafe(500) + bytes(0, 0, 0, 0, 0, 0),
            // A v2.3 style plain integer with a byte of 0x80 or more is not a valid v2.4 frame size.
            "frame size that is not syncsafe" to ascii("ID3") + bytes(4, 0, 0) + syncsafe(14) + ascii("TIT2") + bytes(0, 0, 0, 0x80) + bytes(0, 0, 0, 0, 0, 0),
        )
        for ((name, content) in cases) {
            Files.write(file, content)

            assertFailsWith<IOException>(name) { Id3Frames.add(file, "hi", null) }

            assertContentEquals(content, Files.readAllBytes(file), name)
            assertEquals(listOf("track.mp3"), filesInDir(), name)
        }
    }

    @Test
    fun aMissingFileIsAnIoException() {
        assertFailsWith<NoSuchFileException> { Id3Frames.add(dir.resolve("missing.mp3"), "hi", null) }
        assertEquals(emptyList<String>(), filesInDir())
    }

    // ---- the USLT (lyrics) frame ----
    // Every USLT frame below is written out by hand: 55 53 4C 54 = "USLT", the syncsafe size, no flags, then encoding 3
    // (UTF-8), a 3-byte language, an empty content descriptor (one 00) and the text in UTF-8 (no byte order mark, no
    // terminator after the text).

    private val usltId = bytes(0x55, 0x53, 0x4C, 0x54)

    // The text "La": size 1 + 3 + 1 + 2 = 7.
    private val usltLa = usltId + bytes(0, 0, 0, 7, 0, 0, 3, 0x65, 0x6E, 0x67, 0, 0x4C, 0x61)

    // The text U+D55C: language "kor" (6B 6F 72), the text is ED 95 9C, size 1 + 3 + 1 + 3 = 8.
    private val usltHan = usltId + bytes(0, 0, 0, 8, 0, 0, 3, 0x6B, 0x6F, 0x72, 0, 0xED, 0x95, 0x9C)

    // The text "a", CR, LF, "b": size 1 + 3 + 1 + 4 = 9.
    private val usltTwoLines = usltId + bytes(0, 0, 0, 9, 0, 0, 3, 0x65, 0x6E, 0x67, 0, 0x61, 0x0D, 0x0A, 0x62)

    // The text U+1F3B5, which is F0 9F 8E B5 in UTF-8 (four bytes): size 1 + 3 + 1 + 4 = 9.
    private val usltNote = usltId + bytes(0, 0, 0, 9, 0, 0, 3, 0x65, 0x6E, 0x67, 0, 0xF0, 0x9F, 0x8E, 0xB5)

    @Test
    fun addsALyricsFrameRightAfterTheLastFrameAndBeforeThePadding() {
        Files.write(file, tagged(listOf(title, artist), padding = 10, audio = audio))

        Id3Frames.add(file, null, "La")

        assertContentEquals(tagged(listOf(title, artist, usltLa), padding = 10, audio = audio), Files.readAllBytes(file))
        assertEquals(listOf("TIT2", "TPE1", "USLT"), Id3v2Tag.read(file).ids)
        assertEquals(listOf(Id3v2Tag.Lyrics(3, "eng", "", "La")), Id3v2Tag.read(file).lyrics())
    }

    @Test
    fun addsTheCommentAndTheLyricsInOnePassCommentFirst() {
        Files.write(file, tagged(listOf(title, artist), padding = 10, audio = audio))

        Id3Frames.add(file, "hi", "La")

        val result = Files.readAllBytes(file)
        assertContentEquals(tagged(listOf(title, artist, commHi, usltLa), padding = 10, audio = audio), result)
        // The tag body was 15 + 13 + 10 bytes; it grew by the 17 bytes of COMM and the 17 bytes of USLT.
        assertContentEquals(ascii("ID3") + bytes(4, 0, 0) + syncsafe(15 + 13 + 10 + 17 + 17), result.copyOfRange(0, 10))
        assertEquals(listOf("TIT2", "TPE1", "COMM", "USLT"), Id3v2Tag.parse(result).ids)
        assertContentEquals(audio, result.copyOfRange(result.size - audio.size, result.size))
    }

    @Test
    fun theOtherFramesAndTheAudioStayByteIdenticalWhenBothFramesAreAdded() {
        val picture = frame("APIC", bytes(0) + ascii("image/jpeg") + bytes(0, 3) + ascii("Album cover") + bytes(0) + Random(5).nextBytes(40_000))
        val mp3Audio = bytes(0xFF, 0xFB, 0x90, 0x00) + Random(13).nextBytes(300_000)
        val frames = listOf(title, artist, textFrame("TALB", "Album"), picture)
        Files.write(file, tagged(frames, padding = 10, audio = mp3Audio))

        Id3Frames.add(file, "hi", "La")

        val result = Files.readAllBytes(file)
        assertContentEquals(tagged(frames + listOf(commHi, usltLa), padding = 10, audio = mp3Audio), result)
        assertEquals(listOf("TIT2", "TPE1", "TALB", "APIC", "COMM", "USLT"), Id3v2Tag.parse(result).ids)
    }

    @Test
    fun theTagSizeStaysSyncsafeWhenBothFramesCrossA7BitBoundary() {
        // 10-byte frame header + 110-byte body = 120 bytes of tag; the 17 + 17 bytes of the new frames make 154 = 1 * 128 + 26.
        val big = frame("TIT2", bytes(3) + ascii("x".repeat(109)))
        Files.write(file, tagged(listOf(big), padding = 0, audio = audio))

        Id3Frames.add(file, "hi", "La")

        val result = Files.readAllBytes(file)
        assertContentEquals(bytes(0x49, 0x44, 0x33, 4, 0, 0, 0, 0, 1, 26), result.copyOfRange(0, 10))
        assertContentEquals(tagged(listOf(big, commHi, usltLa), padding = 0, audio = audio), result)
    }

    @Test
    fun theFrameSizeIsSyncsafeNotAPlainBigEndianInteger() {
        Files.write(file, tagged(emptyList(), padding = 0, audio = audio))
        val text = "a".repeat(20_000)

        Id3Frames.add(file, null, text)

        // Payload: 1 + 3 + 1 + 20000 = 20005 = 0x4E25. Syncsafe stores it as 00 01 1C 25; a plain integer would be 00 00 4E 25.
        val result = Files.readAllBytes(file)
        assertContentEquals(usltId, result.copyOfRange(10, 14))
        assertContentEquals(bytes(0, 1, 0x1C, 0x25), result.copyOfRange(14, 18))
        assertContentEquals(bytes(0, 0), result.copyOfRange(18, 20))
        assertContentEquals(ascii("ID3") + bytes(4, 0, 0) + syncsafe(10 + 20_005), result.copyOfRange(0, 10))
        assertEquals(text, Id3v2Tag.read(file).lyrics().single().text)
    }

    @Test
    fun hangulTextGetsTheLanguageKorAndTheContentIsUtf8() {
        Files.write(file, tagged(listOf(title, artist), padding = 10, audio = audio))

        Id3Frames.add(file, null, "한")

        assertContentEquals(tagged(listOf(title, artist, usltHan), padding = 10, audio = audio), Files.readAllBytes(file))
        assertEquals(listOf(Id3v2Tag.Lyrics(3, "kor", "", "한")), Id3v2Tag.read(file).lyrics())
    }

    @Test
    fun theLanguageIsKorWhenAHangulSyllableIsAnywhereInTheTextAndEngOtherwise() {
        val cases = mapOf(
            "Line one\nLine two" to "eng",
            "Hello 한 world" to "kor",
            "Hello\nworld 가" to "kor",
            "가" to "kor",
            "힣" to "kor",
            "꯿" to "eng",
            "힤" to "eng",
            "ㄱㄴㄷ ᄀ" to "eng", // Hangul compatibility jamo and a conjoining jamo are not syllables
            "日本語のかな 歌詞" to "eng",
            "🎵 ♪" to "eng",
        )
        for ((text, language) in cases) {
            Files.write(file, tagged(listOf(title), padding = 4, audio = audio))

            Id3Frames.add(file, null, text)

            assertEquals(language, Id3v2Tag.read(file).lyrics().single().language, "text: [$text]")
        }
    }

    @Test
    fun lineFeedsAreWrittenAsCarriageReturnLineFeeds() {
        Files.write(file, tagged(listOf(title), padding = 0, audio = audio))

        Id3Frames.add(file, null, "a\nb")

        assertContentEquals(tagged(listOf(title, usltTwoLines), padding = 0, audio = audio), Files.readAllBytes(file))
        assertEquals("a\r\nb", Id3v2Tag.read(file).lyrics().single().text)
    }

    @Test
    fun aLineBreakThatIsAlreadyCarriageReturnLineFeedIsNotDoubled() {
        Files.write(file, tagged(listOf(title), padding = 0, audio = audio))

        Id3Frames.add(file, null, "a\r\nb")

        assertContentEquals(tagged(listOf(title, usltTwoLines), padding = 0, audio = audio), Files.readAllBytes(file))
    }

    @Test
    fun aNonBmpCharacterIsWrittenAsFourBytesOfUtf8() {
        Files.write(file, tagged(listOf(title), padding = 0, audio = audio))

        Id3Frames.add(file, null, "🎵")

        assertContentEquals(tagged(listOf(title, usltNote), padding = 0, audio = audio), Files.readAllBytes(file))
        assertEquals("🎵", Id3v2Tag.read(file).lyrics().single().text)
    }

    @Test
    fun multiLineKoreanAndEmojiLyricsRoundTripWithoutAByteOrderMark() {
        Files.write(file, tagged(listOf(title, artist), padding = 10, audio = audio))
        val lyrics = "첫 번째 줄\n\n두 번째 줄 🎵\nLa la la 𠮷\n"

        Id3Frames.add(file, "hi", lyrics)

        val expectedText = lyrics.replace("\n", "\r\n")
        val decoded = Id3v2Tag.read(file).lyrics().single()
        assertEquals(Id3v2Tag.Lyrics(3, "kor", "", expectedText), decoded)
        assertTrue('﻿' !in decoded.text, "the text must not hold a byte order mark of its own")
        // Encoding 3, the language "kor", the single 00 that ends the empty descriptor, then exactly the UTF-8 bytes of the text.
        val body = Id3v2Tag.read(file).frame("USLT").body
        assertContentEquals(bytes(3, 0x6B, 0x6F, 0x72, 0), body.copyOfRange(0, 5))
        assertContentEquals(expectedText.toByteArray(Charsets.UTF_8), body.copyOfRange(5, body.size))
    }

    @Test
    fun aFileWithoutAnId3HeaderGetsAFreshTagWithOnlyTheLyrics() {
        Files.write(file, audio)

        Id3Frames.add(file, null, "La")

        assertContentEquals(ascii("ID3") + bytes(4, 0, 0) + syncsafe(usltLa.size) + usltLa + audio, Files.readAllBytes(file))
        assertEquals(listOf("USLT"), Id3v2Tag.read(file).ids)
    }

    @Test
    fun aFileWithoutAnId3HeaderGetsBothFramesInOneFreshTag() {
        Files.write(file, audio)

        Id3Frames.add(file, "hi", "La")

        assertContentEquals(
            ascii("ID3") + bytes(4, 0, 0) + syncsafe(commHi.size + usltLa.size) + commHi + usltLa + audio,
            Files.readAllBytes(file),
        )
    }

    @Test
    fun aBlankCommentIsSkippedButTheLyricsAreWritten() {
        Files.write(file, tagged(listOf(title), padding = 4, audio = audio))

        Id3Frames.add(file, " \t", "La")

        assertContentEquals(tagged(listOf(title, usltLa), padding = 4, audio = audio), Files.readAllBytes(file))
    }

    @Test
    fun blankLyricsAreSkippedButTheCommentIsWritten() {
        Files.write(file, tagged(listOf(title), padding = 4, audio = audio))

        Id3Frames.add(file, "hi", " \r\n\t ")

        assertContentEquals(tagged(listOf(title, commHi), padding = 4, audio = audio), Files.readAllBytes(file))
    }

    @Test
    fun blankOrMissingCommentAndLyricsLeaveTheFileByteIdentical() {
        val withTag = tagged(listOf(title, artist), padding = 10, audio = audio)
        for ((name, content) in mapOf("with a tag" to withTag, "without a tag" to audio, "empty" to ByteArray(0))) {
            Files.write(file, content)
            for ((comment, lyrics) in listOf(null to null, "" to "", "  \t" to "\n \r\n", null to "   ", " " to null)) {
                Id3Frames.add(file, comment, lyrics)

                assertContentEquals(content, Files.readAllBytes(file), "$name: [$comment] [$lyrics]")
                assertEquals(listOf("track.mp3"), filesInDir(), name)
            }
        }
    }

    @Test
    fun blankOrMissingCommentAndLyricsDoNotEvenOpenTheFile() {
        // A tag layout that is rejected when a frame is added, and a file that is not there at all: nothing to do, no error.
        val unsupported = tagged(listOf(title), 10, audio, major = 3)
        Files.write(file, unsupported)

        Id3Frames.add(file, null, null)
        Id3Frames.add(file, "", "  ")
        Id3Frames.add(dir.resolve("missing.mp3"), null, null)

        assertContentEquals(unsupported, Files.readAllBytes(file))
        assertEquals(listOf("track.mp3"), filesInDir())
    }

    @Test
    fun unsupportedHeaderFlagsAreRejectedForTheLyricsToo() {
        val cases = mapOf(
            "unsynchronisation flag" to tagged(listOf(title), 10, audio, flags = 0x80),
            "extended header flag" to tagged(listOf(title), 10, audio, flags = 0x40),
            "footer flag" to tagged(listOf(title), 10, audio, flags = 0x10),
            "version 2.3" to tagged(listOf(title), 10, audio, major = 3),
        )
        for ((name, content) in cases) {
            Files.write(file, content)
            for ((comment, lyrics) in listOf(null to "La", "hi" to "La")) {
                val error = assertFailsWith<IOException>("$name [$comment]") { Id3Frames.add(file, comment, lyrics) }

                assertEquals("unsupported ID3 tag layout", error.message, name)
                assertContentEquals(content, Files.readAllBytes(file), name)
                assertEquals(listOf("track.mp3"), filesInDir(), name)
            }
        }
    }

    @Test
    fun theExperimentalFlagIsKeptWhenLyricsAreWritten() {
        Files.write(file, tagged(listOf(title), padding = 10, audio = audio, flags = 0x20))

        Id3Frames.add(file, null, "La")

        val result = Files.readAllBytes(file)
        assertContentEquals(tagged(listOf(title, usltLa), padding = 10, audio = audio, flags = 0x20), result)
        assertEquals(0x20, result[5].toInt() and 0xFF)
    }

    @Test
    fun noTemporaryFileStaysBehindAfterWritingBothFrames() {
        Files.write(file, tagged(listOf(title), padding = 10, audio = audio))

        Id3Frames.add(file, "hi", "La")

        assertEquals(listOf("track.mp3"), filesInDir())
    }

    @Test
    fun aFailingCleanupDoesNotHideTheRealErrorWhenWritingLyrics() {
        val content = tagged(listOf(title), padding = 10, audio = audio)
        Files.write(file, content)
        val blocker = Files.createDirectory(dir.resolve("track.mp3.id3tmp"))
        Files.write(blocker.resolve("keep.txt"), bytes(1))

        val error = assertFailsWith<IOException> { Id3Frames.add(file, "hi", "La") }

        assertFalse(error is DirectoryNotEmptyException, "the cleanup failure replaced the real error: $error")
        assertTrue(error.suppressed.any { it is DirectoryNotEmptyException }, "the cleanup failure is attached: ${error.suppressed.toList()}")
        assertContentEquals(content, Files.readAllBytes(file))
    }
}

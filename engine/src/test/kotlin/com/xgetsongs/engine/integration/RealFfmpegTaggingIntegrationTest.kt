package com.xgetsongs.engine.integration

import com.xgetsongs.engine.process.SystemProcessRunner
import com.xgetsongs.engine.tags.Id3Frames
import com.xgetsongs.engine.tags.Id3Tagger
import com.xgetsongs.engine.tags.TrackTags
import com.xgetsongs.engine.testutil.Id3v2Tag
import com.xgetsongs.engine.tools.ToolLocator
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.jupiter.api.Assumptions.assumeTrue
import org.junit.jupiter.api.Tag
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * Runs the real [Id3Tagger] with the real ffmpeg and reads the result back with the real ffprobe. It needs no
 * network, but it is excluded from `test` like the other real-tool tests and skipped when a tool is missing.
 */
@Tag("integration")
class RealFfmpegTaggingIntegrationTest {
    private val appData = System.getenv("APPDATA")?.let { Path.of(it, "xGetSongs") }
        ?: Path.of(System.getProperty("user.home"), ".xgetsongs")
    private val locator = ToolLocator(appBinDir = appData.resolve("bin"))
    private val runner = SystemProcessRunner()
    private val root: Path = Files.createTempDirectory("xgs-tagging")
    private lateinit var ffmpeg: Path
    private lateinit var ffprobe: Ffprobe

    @BeforeTest
    fun requireTools() {
        val found = locator.current().ffmpeg
        val probe = Ffprobe.besides(found)
        assumeTrue(found != null && probe != null, "ffmpeg and ffprobe must be installed")
        ffmpeg = found!!
        ffprobe = Ffprobe(probe!!, runner)
    }

    @AfterTest
    fun cleanUp() {
        root.toFile().deleteRecursively()
    }

    private suspend fun generate(vararg args: String) {
        val stderr = mutableListOf<String>()
        val exitCode = runner.run(
            listOf(ffmpeg.toString(), "-hide_banner", "-loglevel", "error", "-nostdin", "-y") + args,
            onStderr = { synchronized(stderr) { stderr += it } },
        )
        assertEquals(0, exitCode, stderr.toString())
    }

    /** A one-second silent mp3, like the one yt-dlp leaves behind. */
    private suspend fun silentMp3(): Path = root.resolve("track.mp3").also {
        generate("-f", "lavfi", "-i", "anullsrc=r=44100:cl=mono", "-t", "1", "-c:a", "libmp3lame", it.toString())
    }

    /** A 640x360 red picture, like a YouTube thumbnail (16:9). */
    private suspend fun redJpg(): Path = root.resolve("track.jpg").also {
        generate("-f", "lavfi", "-i", "color=c=red:s=640x360", "-frames:v", "1", it.toString())
    }

    private val tags = TrackTags(
        title = "\"Golden\" '골든' (feat. 지민)",
        artist = "방탄소년단",
        album = "My \"Best\" List",
        albumArtist = "방탄소년단",
        trackNumber = 7,
        comment = "https://www.youtube.com/watch?v=jNQXAC9IVRw",
    )

    private fun assertTags(expected: TrackTags, actual: Map<String, String>) {
        assertEquals(expected.title, actual["title"])
        assertEquals(expected.artist, actual["artist"])
        assertEquals(expected.albumArtist, actual["album_artist"])
        assertEquals(expected.album, actual["album"])
        assertEquals(expected.trackNumber.toString(), actual["track"])
        assertEquals(expected.comment, actual["comment"])
    }

    // JUnit does not discover test methods with a non-void return type, and the last expression of the runBlocking
    // block is not Unit in general, so the return type is declared explicitly.
    @Test
    fun writesEveryTagAndASquareCoverIntoTheMp3(): Unit = runBlocking {
        withTimeout(60_000) {
            val mp3 = silentMp3()

            val failure = Id3Tagger(runner, locator).tag(mp3, redJpg(), tags)

            assertNull(failure)
            val probed = ffprobe.probe(mp3)
            assertTags(tags, probed.tags)
            assertEquals(2, probed.streams.size, probed.toString())
            val audio = probed.streams.single { it.codecType == "audio" }
            assertEquals("mp3", audio.codecName)
            assertFalse(audio.attachedPic)
            val picture = probed.streams.single { it.codecType == "video" }
            assertEquals("mjpeg", picture.codecName)
            assertEquals(360, picture.width)
            assertEquals(360, picture.height)
            assertTrue(picture.attachedPic)
            val left = Files.list(root).use { files -> files.map { it.fileName.toString() }.sorted().toList() }
            assertEquals(listOf("track.jpg", "track.mp3"), left, "no temporary file may stay behind")
        }
    }

    @Test
    fun withoutACoverOnlyTheAudioStreamIsWritten(): Unit = runBlocking {
        withTimeout(60_000) {
            val mp3 = silentMp3()

            val failure = Id3Tagger(runner, locator).tag(mp3, null, tags)

            assertNull(failure)
            val probed = ffprobe.probe(mp3)
            assertTags(tags, probed.tags)
            assertEquals(listOf("audio"), probed.streams.map { it.codecType }, probed.toString())
        }
    }

    @Test
    fun aSingleVideoGetsNoAlbumTag(): Unit = runBlocking {
        withTimeout(60_000) {
            val mp3 = silentMp3()

            assertNull(Id3Tagger(runner, locator).tag(mp3, null, tags.copy(album = null)))

            val probed = ffprobe.probe(mp3)
            assertNull(probed.tags["album"])
            assertEquals(tags.title, probed.tags["title"])
        }
    }

    @Test
    fun syntaxCharactersAndLineBreaksSurviveTheRoundTrip(): Unit = runBlocking {
        withTimeout(60_000) {
            val mp3 = silentMp3()
            val nasty = tags.copy(
                title = "a=b;c#d\\e",
                album = "100% [x] {y} #1; k=v",
                comment = "line one\nline two\r\nline three",
            )

            assertNull(Id3Tagger(runner, locator).tag(mp3, null, nasty))

            assertTags(nasty, ffprobe.probe(mp3).tags)
        }
    }

    @Test
    fun theTagIsId3v2Point3WithUtf16TextAndAFrontCoverAndNoId3v1(): Unit = runBlocking {
        withTimeout(60_000) {
            val mp3 = silentMp3()

            assertNull(Id3Tagger(runner, locator).tag(mp3, redJpg(), tags))

            val tag = Id3v2Tag.read(mp3)
            assertEquals(3, tag.version)
            for (id in listOf("TIT2", "TPE1", "TPE2", "TALB", "TRCK", "APIC")) {
                assertTrue(id in tag.ids, "$id is missing from ${tag.ids}")
            }
            // ID3 text encoding 1 is UTF-16 with a byte order mark; the title has Korean text.
            assertEquals(1, tag.frame("TIT2").body[0].toInt())
            // APIC body: encoding byte, MIME type and a zero byte, then the picture type (3 = front cover).
            val apic = tag.frame("APIC").body
            val mimeEnd = 1 + apic.drop(1).indexOfFirst { it.toInt() == 0 }
            assertEquals("image/jpeg", String(apic, 1, mimeEnd - 1, Charsets.ISO_8859_1))
            assertEquals(3, apic[mimeEnd + 1].toInt())
            // ffmpeg itself can only write a comment as a user-defined TXXX frame; the engine adds a real COMM frame.
            assertEquals(listOf(Id3v2Tag.Comment(0, "eng", "", tags.comment!!)), tag.comments(), tag.ids.toString())
            assertEquals(1, tag.ids.count { it == "COMM" }, tag.ids.toString())
            assertTrue(
                tag.frames.filter { it.id == "TXXX" }.none { "comment" in String(it.body, Charsets.ISO_8859_1) },
                "no TXXX frame named comment: ${tag.ids}",
            )
            assertFalse(Id3v2Tag.hasId3v1(mp3))
        }
    }

    @Test
    fun aKoreanCommentIsStoredAsUtf16AndReadBackByFfprobe(): Unit = runBlocking {
        withTimeout(60_000) {
            val mp3 = silentMp3()
            val korean = tags.copy(comment = "메모: \"인용\" 'x' https://www.youtube.com/watch?v=jNQXAC9IVRw\n둘째 줄 \uD83C\uDFB5")

            assertNull(Id3Tagger(runner, locator).tag(mp3, redJpg(), korean))

            val comment = Id3v2Tag.read(mp3).comments().single()
            assertEquals(1, comment.encoding)
            assertEquals(korean.comment, comment.text)
            assertTags(korean, ffprobe.probe(mp3).tags)
        }
    }

    @Test
    fun lyricsAreWrittenAsOneUsltFrameAndFfprobeShowsThemWhileTheOtherTagsAndTheCoverStay(): Unit = runBlocking {
        withTimeout(60_000) {
            val mp3 = silentMp3()
            // Made-up lyrics: Korean and English lines, a blank line and a character outside the BMP.
            val lyrics = "첫 번째 줄\nLa la la 🎵\n\nSecond line\n마지막 줄"
            val withLyrics = tags.copy(lyrics = lyrics)

            assertNull(Id3Tagger(runner, locator).tag(mp3, redJpg(), withLyrics))

            val tag = Id3v2Tag.read(mp3)
            assertEquals(1, tag.ids.count { it == "USLT" }, tag.ids.toString())
            val frame = tag.lyrics().single()
            assertEquals(1, frame.encoding, "UTF-16 with a byte order mark")
            assertEquals("kor", frame.language)
            assertEquals("", frame.descriptor)
            assertEquals(lyrics.replace("\n", "\r\n"), frame.text)
            assertTrue('\uFEFF' !in frame.text, "no byte order mark inside the text")
            // The comment is still a real COMM frame, next to the lyrics.
            assertEquals(listOf(Id3v2Tag.Comment(0, "eng", "", tags.comment!!)), tag.comments(), tag.ids.toString())
            for (id in listOf("TIT2", "TPE1", "TPE2", "TALB", "TRCK", "APIC", "COMM", "USLT")) {
                assertTrue(id in tag.ids, "$id is missing from ${tag.ids}")
            }

            val probed = ffprobe.probe(mp3)
            // ffprobe names a lyrics frame "lyrics", or "lyrics-" and the language when it is not English.
            val shown = probed.tags.filterKeys { it == "lyrics" || it == "lyrics-kor" }
            assertEquals(1, shown.size, probed.tags.toString())
            println("ffprobe shows the lyrics as '${shown.keys.single()}'; all tags: ${probed.tags.keys}")
            assertEquals(lyrics, shown.values.single().replace("\r", ""))
            assertTags(withLyrics, probed.tags)
            assertEquals(2, probed.streams.size, probed.toString())
            val picture = probed.streams.single { it.codecType == "video" }
            assertTrue(picture.attachedPic)
            assertEquals(360, picture.width)
            assertEquals(360, picture.height)
            assertEquals("mp3", probed.streams.single { it.codecType == "audio" }.codecName)
            assertFalse(Id3v2Tag.hasId3v1(mp3))
            val left = Files.list(root).use { files -> files.map { it.fileName.toString() }.sorted().toList() }
            assertEquals(listOf("track.jpg", "track.mp3"), left, "no temporary file may stay behind")
        }
    }

    @Test
    fun withoutLyricsNoUsltFrameIsWritten(): Unit = runBlocking {
        withTimeout(60_000) {
            val mp3 = silentMp3()

            assertNull(Id3Tagger(runner, locator).tag(mp3, null, tags.copy(lyrics = null)))

            assertTrue("USLT" !in Id3v2Tag.read(mp3).ids)
            assertTrue(ffprobe.probe(mp3).tags.keys.none { it.startsWith("lyrics") })
        }
    }

    /** Like [silentMp3], but with an ID3v2.3 tag (or none), which is what the engine's own frame writer extends. */
    private suspend fun silentMp3WithAV23Tag(): Path = root.resolve("track.mp3").also {
        generate("-f", "lavfi", "-i", "anullsrc=r=44100:cl=mono", "-t", "1", "-c:a", "libmp3lame", "-id3v2_version", "3", it.toString())
    }

    @Test
    fun noLyricsGivenMeansNoLyricsFrameAndNoLyricsTagEvenWhenTheInputFileCarriesLyrics(): Unit = runBlocking {
        withTimeout(60_000) {
            val mp3 = silentMp3WithAV23Tag()
            // The input already has a USLT frame, written the way the engine writes one.
            Id3Frames.add(mp3, null, "Old made-up line one\nOld line two\nOld line three")
            assertTrue("USLT" in Id3v2Tag.read(mp3).ids, "precondition: the input has a USLT frame")
            val before = ffprobe.probe(mp3).tags.keys.filter { it.startsWith("lyrics") }
            println("ffprobe shows the old lyrics of the input as $before")
            assertTrue(before.isNotEmpty(), "precondition: ffprobe sees the old lyrics, so its silence afterwards means something")

            assertNull(Id3Tagger(runner, locator).tag(mp3, redJpg(), tags.copy(lyrics = null)))

            val tag = Id3v2Tag.read(mp3)
            assertTrue("USLT" !in tag.ids, "no lyrics frame may survive: ${tag.ids}")
            val probed = ffprobe.probe(mp3)
            assertTrue(probed.tags.keys.none { it.startsWith("lyrics") }, "no lyrics tag may survive: ${probed.tags.keys}")
            assertTrue(tag.frames.none { it.id == "TXXX" && "lyrics" in String(it.body, Charsets.ISO_8859_1).lowercase() }, "no TXXX lyrics frame either: ${tag.ids}")
            assertTags(tags, probed.tags)
            assertEquals(2, probed.streams.size, probed.toString())
            assertTrue(probed.streams.single { it.codecType == "video" }.attachedPic)
        }
    }

    @Test
    fun withoutACommentNoCommentFrameIsWritten(): Unit = runBlocking {
        withTimeout(60_000) {
            val mp3 = silentMp3()

            assertNull(Id3Tagger(runner, locator).tag(mp3, null, tags.copy(comment = null)))

            assertTrue("COMM" !in Id3v2Tag.read(mp3).ids)
            assertNull(ffprobe.probe(mp3).tags["comment"])
        }
    }

    // A value ending in a backslash made ffmpeg swallow the next line of the ffmetadata file (the artist went missing).
    @Test
    fun aTitleEndingInABackslashDoesNotSwallowTheNextTag(): Unit = runBlocking {
        withTimeout(60_000) {
            val mp3 = silentMp3()
            val trailing = tags.copy(title = "Path to C:\\", artist = "Artist\\\\", albumArtist = "Artist\\\\", album = "Album\\")

            assertNull(Id3Tagger(runner, locator).tag(mp3, null, trailing))

            val probed = ffprobe.probe(mp3).tags
            assertEquals("Path to C:\uFF3C", probed["title"])
            assertEquals("Artist\uFF3C\uFF3C", probed["artist"])
            assertEquals("Artist\uFF3C\uFF3C", probed["album_artist"])
            assertEquals("Album\uFF3C", probed["album"])
            assertEquals("7", probed["track"])
            assertEquals(trailing.comment, probed["comment"])
        }
    }

    @Test
    fun aNulInATitleCannotInjectAnotherTag(): Unit = runBlocking {
        withTimeout(60_000) {
            val mp3 = silentMp3()
            val injected = tags.copy(title = "abc\u0000album=Injected", album = "Real Album")

            assertNull(Id3Tagger(runner, locator).tag(mp3, null, injected))

            val probed = ffprobe.probe(mp3).tags
            assertEquals("abcalbum=Injected", probed["title"])
            assertEquals("Real Album", probed["album"])
        }
    }
}

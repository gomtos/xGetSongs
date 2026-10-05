package com.xgetsongs.engine.integration

import com.xgetsongs.engine.process.SystemProcessRunner
import com.xgetsongs.engine.tags.Id3Tagger
import com.xgetsongs.engine.tags.TrackTags
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
            // ffmpeg has no COMM writer: the comment is stored as a user-defined text frame named "comment".
            assertTrue("TXXX" in tag.ids && "COMM" !in tag.ids, tag.ids.toString())
            assertFalse(Id3v2Tag.hasId3v1(mp3))
        }
    }
}

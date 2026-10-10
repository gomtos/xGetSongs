package com.xgetsongs.engine.integration

import com.xgetsongs.engine.process.SystemProcessRunner
import com.xgetsongs.engine.tags.M4aTagger
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
 * Runs the real [M4aTagger] with the real ffmpeg and reads the result back with the real ffprobe. It needs no
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

    /** A one-second silent AAC file in a regular MP4 container. */
    private suspend fun silentM4a(): Path = root.resolve("track.m4a").also {
        generate("-f", "lavfi", "-i", "anullsrc=r=44100:cl=mono", "-t", "1", "-c:a", "aac", it.toString())
    }

    /** Like [silentM4a], but fragmented with the `dash` brand, which is how yt-dlp leaves YouTube's itag 140 audio. */
    private suspend fun fragmentedM4a(): Path = root.resolve("track.m4a").also {
        generate(
            "-f", "lavfi", "-i", "anullsrc=r=44100:cl=mono", "-t", "1", "-c:a", "aac",
            "-movflags", "frag_keyframe+empty_moov+default_base_moof", "-brand", "dash", "-f", "mp4", it.toString(),
        )
    }

    /** A 640x360 red picture, like a YouTube thumbnail (16:9). */
    private suspend fun redJpg(): Path = root.resolve("track.jpg").also {
        generate("-f", "lavfi", "-i", "color=c=red:s=640x360", "-frames:v", "1", it.toString())
    }

    /** The MD5 of the audio stream as it is stored: a stream copy must not change it. */
    private suspend fun audioMd5(file: Path): String {
        val stdout = mutableListOf<String>()
        val exitCode = runner.run(
            listOf(ffmpeg.toString(), "-hide_banner", "-loglevel", "error", "-nostdin", "-i", file.toString(), "-map", "0:a", "-c", "copy", "-f", "md5", "-"),
            onStdout = { synchronized(stdout) { stdout += it } },
        )
        assertEquals(0, exitCode, stdout.toString())
        return stdout.single { it.startsWith("MD5=") }
    }

    private val tags = TrackTags(
        title = "\"Golden\" '골든' (feat. 지민)",
        artist = "방탄소년단",
        album = "My \"Best\" List",
        albumArtist = "방탄소년단",
        trackNumber = 7,
        comment = "https://www.youtube.com/watch?v=jNQXAC9IVRw",
        lyrics = "첫 번째 줄\nLa la la 🎵\n\nSecond line\n마지막 줄",
    )

    private fun assertTags(expected: TrackTags, actual: Map<String, String>) {
        assertEquals(expected.title, actual["title"])
        assertEquals(expected.artist, actual["artist"])
        assertEquals(expected.albumArtist, actual["album_artist"])
        assertEquals(expected.album, actual["album"])
        assertEquals(expected.trackNumber.toString(), actual["track"])
        assertEquals(expected.comment, actual["comment"])
        assertEquals(expected.lyrics, actual["lyrics"])
    }

    // JUnit does not discover test methods with a non-void return type, and the last expression of the runBlocking
    // block is not Unit in general, so the return type is declared explicitly.
    @Test
    fun writesEveryTagAndASquareCoverIntoTheM4aWithoutTouchingTheAudio(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = silentM4a()
            val audioBefore = audioMd5(m4a)

            val failure = M4aTagger(runner, locator).tag(m4a, redJpg(), tags)

            assertNull(failure)
            val probed = ffprobe.probe(m4a)
            assertTags(tags, probed.tags)
            assertEquals("M4A ", probed.tags["major_brand"])
            assertEquals(2, probed.streams.size, probed.toString())
            val audio = probed.streams.single { it.codecType == "audio" }
            assertEquals("aac", audio.codecName)
            assertFalse(audio.attachedPic)
            val picture = probed.streams.single { it.codecType == "video" }
            assertEquals("mjpeg", picture.codecName)
            assertEquals(360, picture.width)
            assertEquals(360, picture.height)
            assertTrue(picture.attachedPic)
            assertEquals(audioBefore, audioMd5(m4a), "the audio must be copied, not re-encoded")
            val left = Files.list(root).use { files -> files.map { it.fileName.toString() }.sorted().toList() }
            assertEquals(listOf("track.jpg", "track.m4a"), left, "no temporary file may stay behind")
        }
    }

    @Test
    fun aFragmentedDashFileBecomesARegularM4aWithTheSameAudio(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = fragmentedM4a()
            assertEquals("dash", ffprobe.probe(m4a).tags["major_brand"], "precondition: the input is a fragmented dash file")
            val audioBefore = audioMd5(m4a)

            assertNull(M4aTagger(runner, locator).tag(m4a, redJpg(), tags))

            val probed = ffprobe.probe(m4a)
            assertEquals("M4A ", probed.tags["major_brand"])
            assertTags(tags, probed.tags)
            assertEquals(audioBefore, audioMd5(m4a), "the audio must be copied, not re-encoded")
        }
    }

    @Test
    fun withoutACoverOnlyTheAudioStreamIsWritten(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = silentM4a()

            val failure = M4aTagger(runner, locator).tag(m4a, null, tags)

            assertNull(failure)
            val probed = ffprobe.probe(m4a)
            assertTags(tags, probed.tags)
            assertEquals(listOf("audio"), probed.streams.map { it.codecType }, probed.toString())
        }
    }

    @Test
    fun aSingleVideoGetsNoAlbumTag(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = silentM4a()

            assertNull(M4aTagger(runner, locator).tag(m4a, null, tags.copy(album = null)))

            val probed = ffprobe.probe(m4a)
            assertNull(probed.tags["album"])
            assertEquals(tags.title, probed.tags["title"])
        }
    }

    @Test
    fun withoutCommentAndLyricsNeitherTagIsWritten(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = silentM4a()

            assertNull(M4aTagger(runner, locator).tag(m4a, null, tags.copy(comment = null, lyrics = null)))

            val probed = ffprobe.probe(m4a)
            assertNull(probed.tags["comment"])
            assertTrue(probed.tags.keys.none { it.startsWith("lyrics") }, probed.tags.keys.toString())
            assertEquals(tags.title, probed.tags["title"])
        }
    }

    @Test
    fun syntaxCharactersAndLineBreaksSurviveTheRoundTrip(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = silentM4a()
            val nasty = tags.copy(
                title = "a=b;c#d\\e",
                album = "100% [x] {y} #1; k=v",
                comment = "line one\nline two\r\nline three",
                lyrics = "x=y;z\r\nsecond #line\n\nlast \\ line",
            )

            assertNull(M4aTagger(runner, locator).tag(m4a, null, nasty))

            assertTags(nasty, ffprobe.probe(m4a).tags)
        }
    }

    // A value ending in a backslash made ffmpeg swallow the next line of the ffmetadata file (the artist went missing).
    @Test
    fun aTitleEndingInABackslashDoesNotSwallowTheNextTag(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = silentM4a()
            val trailing = tags.copy(title = "Path to C:\\", artist = "Artist\\\\", albumArtist = "Artist\\\\", album = "Album\\")

            assertNull(M4aTagger(runner, locator).tag(m4a, null, trailing))

            val probed = ffprobe.probe(m4a).tags
            assertEquals("Path to C:\uFF3C", probed["title"])
            assertEquals("Artist\uFF3C\uFF3C", probed["artist"])
            assertEquals("Artist\uFF3C\uFF3C", probed["album_artist"])
            assertEquals("Album\uFF3C", probed["album"])
            assertEquals("7", probed["track"])
            assertEquals(trailing.comment, probed["comment"])
            assertEquals(trailing.lyrics, probed["lyrics"])
        }
    }

    @Test
    fun aNulInATitleCannotInjectAnotherTag(): Unit = runBlocking {
        withTimeout(60_000) {
            val m4a = silentM4a()
            val injected = tags.copy(title = "abc\u0000album=Injected", album = "Real Album")

            assertNull(M4aTagger(runner, locator).tag(m4a, null, injected))

            val probed = ffprobe.probe(m4a).tags
            assertEquals("abcalbum=Injected", probed["title"])
            assertEquals("Real Album", probed["album"])
        }
    }
}

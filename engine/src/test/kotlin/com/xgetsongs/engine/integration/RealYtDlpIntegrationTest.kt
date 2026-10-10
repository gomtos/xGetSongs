package com.xgetsongs.engine.integration

import com.xgetsongs.engine.DownloadRequest
import com.xgetsongs.engine.job.DefaultDownloadService
import com.xgetsongs.engine.job.ItemDownloader
import com.xgetsongs.engine.lyrics.LrclibLyricsProvider
import com.xgetsongs.engine.output.LocalFolderSink
import com.xgetsongs.engine.process.SystemProcessRunner
import com.xgetsongs.engine.tools.ToolLocator
import com.xgetsongs.engine.ytdlp.VideoInfoFile
import com.xgetsongs.engine.ytdlp.YtDlpCommands
import com.xgetsongs.engine.ytdlp.YtDlpResolver
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.Stage
import com.xgetsongs.shared.input.ParsedInput
import com.xgetsongs.shared.lyrics.LyricsExtractor
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.junit.jupiter.api.Assumptions.assumeTrue
import org.junit.jupiter.api.Tag
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * Talks to the real YouTube through the real yt-dlp and ffmpeg, so it is excluded from `test` and
 * only runs through `integrationTest`. It is skipped (not failed) when a tool is missing.
 */
@Tag("integration")
class RealYtDlpIntegrationTest {
    private val appData = System.getenv("APPDATA")?.let { Path.of(it, "xGetSongs") }
        ?: Path.of(System.getProperty("user.home"), ".xgetsongs")
    private val locator = ToolLocator(appBinDir = appData.resolve("bin"))
    private val runner = SystemProcessRunner()

    @BeforeTest
    fun requireTools() {
        val tools = locator.current()
        assumeTrue(
            tools.ytDlp != null && tools.ffmpeg != null && tools.jsRuntime != null,
            "yt-dlp, ffmpeg and Node 22+/Deno 2.3+ must be installed",
        )
    }

    @Test
    fun resolvesTheReferencePlaylistWithoutDownloadingAnything(): Unit = runBlocking {
        withTimeout(120_000) {
            val response = YtDlpResolver(runner, locator)
                .resolve("https://www.youtube.com/playlist?list=PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV")

            println("playlist '${response.playlistTitle}' has ${response.items.size} items")
            response.items.take(25).forEach {
                println("%03d %-5s %s".format(it.rank, if (it.lowConfidence) "WARN" else "ok", it.expectedFileName ?: "(${it.unavailableReason})"))
            }

            assertTrue(response.items.size > 10, "expected a long chart playlist")
            assertEquals(response.items.indices.map { it + 1 }, response.items.map { it.rank })
            val available = response.items.filter { it.available }
            assertTrue(available.isNotEmpty())
            assertTrue(available.all { Regex("""\d{3} .+ - .+\.m4a""").matches(it.expectedFileName!!) })
        }
    }

    /**
     * Runs the real [YtDlpCommands.download] command for [videoId] into [dir], with [extraOptions] put in front of the `--`
     * separator, and prints what yt-dlp left in [dir].
     */
    private suspend fun runDownload(videoId: String, dir: Path, extraOptions: List<String> = emptyList()) {
        val base = YtDlpCommands.download(locator.current(), ParsedInput.Video(videoId).canonicalUrl, dir, videoId)
        val separator = base.lastIndexOf("--")
        val command = base.take(separator) + extraOptions + base.drop(separator)
        val output = CopyOnWriteArrayList<String>()
        val exitCode = runner.run(command, onStdout = { output += it }, onStderr = { output += it })
        assertEquals(0, exitCode, "yt-dlp failed: $output")
        println("yt-dlp left in the work folder: " + Files.list(dir).use { files -> files.map { "${it.fileName} (${Files.size(it)} bytes)" }.sorted().toList() })
    }

    // JUnit does not discover test methods with a non-void return type, and the last expression of the runBlocking
    // blocks below is not Unit, so the return type must be declared Unit explicitly.
    @Test
    fun writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum(): Unit = runBlocking {
        withTimeout(180_000) {
            // "Me at the zoo" again: 19 seconds, uploaded by a person, so YouTube knows no album for it.
            val videoId = "jNQXAC9IVRw"
            val dir = Files.createTempDirectory("xgs-info-integration")
            try {
                runDownload(videoId, dir)

                val info = dir.resolve("$videoId.info.json")
                assertTrue(Files.isRegularFile(dir.resolve("$videoId.m4a")), "the m4a must be there: ${Files.list(dir).use { it.toList() }}")
                assertTrue(Files.isRegularFile(info), "yt-dlp must write <videoId>.info.json next to the m4a: ${Files.list(dir).use { it.toList() }}")
                val root = Json.parseToJsonElement(Files.readString(info)).jsonObject
                assertEquals(videoId, root.getValue("id").jsonPrimitive.content)
                assertEquals("140", root.getValue("format_id").jsonPrimitive.content, "the AAC stream (itag 140) is what is downloaded")
                println("info file: ${Files.size(info)} bytes; album=${root["album"]}, artist=${root["artist"]}, track=${root["track"]}")
                assertNull(VideoInfoFile.readAlbum(info), "the test video has no album")
            } finally {
                dir.toFile().deleteRecursively()
            }
        }
    }

    @Test
    fun readsTheAlbumOfAMusicVideoFromItsInfoFileWithoutDownloadingAudio(): Unit = runBlocking {
        withTimeout(120_000) {
            // Metadata only: --skip-download keeps yt-dlp from fetching any audio.
            // This depends on the music metadata YouTube currently shows for this video; if it changes, pick another track
            // that YouTube lists with an album.
            val videoId = "kcx0a2OAhN0"
            val dir = Files.createTempDirectory("xgs-info-integration")
            try {
                runDownload(videoId, dir, listOf("--skip-download"))

                val info = dir.resolve("$videoId.info.json")
                assertTrue(Files.isRegularFile(info), "yt-dlp must write <videoId>.info.json: ${Files.list(dir).use { it.toList() }}")
                assertTrue(Files.notExists(dir.resolve("$videoId.m4a")), "no audio may be downloaded")
                val root = Json.parseToJsonElement(Files.readString(info)).jsonObject
                println("info file: ${Files.size(info)} bytes; album=${root["album"]}, artist=${root["artist"]}, track=${root["track"]}")
                assertEquals("Love poem", VideoInfoFile.readAlbum(info))
            } finally {
                dir.toFile().deleteRecursively()
            }
        }
    }

    /** Counts the kinds of lines in [description]: enough to describe its structure without quoting any of it. */
    private fun structureOf(description: String): String {
        val lines = description.replace("\r\n", "\n").split("\n")
        fun count(predicate: (String) -> Boolean) = lines.count(predicate)
        val separators = count { line -> line.trim().let { it.length >= 3 && it.all { c -> c == it[0] } && it[0] in "=-_*~#.+" } }
        val links = count { "http://" in it || "https://" in it || "www." in it }
        val copyright = count { it.trim().startsWith("©") || it.trim().startsWith("copyright", ignoreCase = true) }
        val hashtags = count { it.trim().startsWith("#") && it.trim().length > 1 && !it.trim()[1].isWhitespace() }
        val bracketed = count { it.trim().let { t -> t.length >= 2 && ((t.first() == '[' && t.last() == ']') || (t.first() == '【' && t.last() == '】')) } }
        return "lines=${lines.size}, blank=${count { it.isBlank() }}, separator lines=$separators, link lines=$links, " +
            "copyright lines=$copyright, hashtag lines=$hashtags, bracketed headings=$bracketed"
    }

    @Test
    fun findsTheLyricsInTheDescriptionOfAVideoThatHasThemWithoutDownloadingAudio(): Unit = runBlocking {
        withTimeout(120_000) {
            // Metadata only: --skip-download keeps yt-dlp from fetching any audio. The test asserts structure only, never lyric text.
            // This depends on the CURRENT description of this video: a "[Lyrics]" heading with 20 or more non-blank lyric
            // lines below it, "=======" separator lines around the block, and channel link lines. If the uploader edits
            // the description, pick another video whose description has a lyrics section.
            val videoId = "7mDDM0eBWR0"
            val dir = Files.createTempDirectory("xgs-info-integration")
            try {
                runDownload(videoId, dir, listOf("--skip-download"))

                val info = dir.resolve("$videoId.info.json")
                assertTrue(Files.isRegularFile(info), "yt-dlp must write <videoId>.info.json: ${Files.list(dir).use { it.toList() }}")
                assertTrue(Files.notExists(dir.resolve("$videoId.m4a")), "no audio may be downloaded")
                val description = VideoInfoFile.read(info).description
                assertNotNull(description, "the video must have a description")
                println("description structure: ${structureOf(description)}")

                val lyrics = LyricsExtractor.extract(description)
                assertNotNull(lyrics, "a lyrics block must be found in the description")
                val lines = lyrics.split("\n")
                println("lyrics block: ${lines.size} lines, ${lines.count { it.isNotBlank() }} non-blank, ${lyrics.length} characters")
                assertTrue(lines.count { it.isNotBlank() } >= 20, "expected at least 20 non-blank lines, got ${lines.count { it.isNotBlank() }}")
                assertTrue("http" !in lyrics, "no link may be part of the lyrics")
                assertTrue(lines.none { it.startsWith("i-dle Official") }, "the channel's link block must not be part of the lyrics")
                assertTrue(lines.none { it == "=======" }, "no separator line may be part of the lyrics")
                assertTrue('\r' !in lyrics)
            } finally {
                dir.toFile().deleteRecursively()
            }
        }
    }

    @Test
    fun downloadsAShortVideoAsM4aWithoutReencoding(): Unit = runBlocking {
        val ffprobePath = Ffprobe.besides(locator.current().ffmpeg)
        assumeTrue(ffprobePath != null, "ffprobe must be installed next to ffmpeg")
        val ffprobe = Ffprobe(ffprobePath!!, runner)
        withTimeout(180_000) {
            // "Me at the zoo": the first video ever uploaded to YouTube, 19 seconds long.
            val resolver = YtDlpResolver(runner, locator)
            val resolved = resolver.resolve("https://www.youtube.com/watch?v=jNQXAC9IVRw")
            val root = Files.createTempDirectory("xgs-integration")
            try {
                val outDir = root.resolve("out")

                // The lyrics lookup is on and the real LRCLIB is asked: this video has no lyrics in its description and none
                // may be found anywhere else, so the file must come out without any lyrics tag.
                val service = DefaultDownloadService(ItemDownloader(runner, locator, resolver, LrclibLyricsProvider()), root.resolve("work"), this)
                val events = service
                    .start(
                        DownloadRequest(
                            resolved.items, LocalFolderSink(outDir), overwrite = false, concurrency = 1, album = "Test Album",
                            searchLyricsOnline = true,
                        ),
                    )
                    .events.receiveAsFlow().toList()

                val done = events.last() as JobEvent.JobDone
                assertEquals(JobStatus.COMPLETED, done.status, events.toString())
                assertEquals(1, done.summary.succeeded, events.toString())
                val file = Files.list(outDir).use { it.toList().single() }
                println("downloaded: ${file.fileName} (${Files.size(file)} bytes)")
                assertTrue(Regex("""001 .+ - .+\.m4a""").matches(file.fileName.toString()), file.fileName.toString())
                assertTrue(Files.size(file) > 50_000)

                val stages = events.filterIsInstance<JobEvent.Progress>().map { it.stage }
                println("reported stages: ${stages.fold(emptyList<Stage>()) { all, next -> if (all.lastOrNull() == next) all else all + next }}")
                assertTrue(Stage.DOWNLOADING in stages, "no download progress was reported: $stages")
                assertEquals(Stage.FINISHING, stages.last(), "the finishing stage comes last: $stages")
                assertEquals(1, stages.count { it == Stage.FINISHING }, stages.toString())

                val probed = ffprobe.probe(file)
                // Never print a lyrics tag: if the lookup wrongly found lyrics, they must not end up in the log.
                println("tags: ${probed.tags.filterKeys { !it.startsWith("lyrics") }}; lyrics tags: ${probed.tags.keys.count { it.startsWith("lyrics") }}; streams: ${probed.streams}")
                assertTrue(probed.tags.keys.none { it.startsWith("lyrics") }, "the video has no lyrics anywhere: ${probed.tags.keys}")
                assertEquals("M4A ", probed.tags["major_brand"])
                assertEquals("Me at the zoo", probed.tags["title"])
                assertEquals("jawed", probed.tags["artist"])
                assertEquals("Various Artists", probed.tags["album_artist"])
                assertEquals("Test Album", probed.tags["album"])
                assertEquals("1", probed.tags["track"])
                assertTrue(probed.tags["comment"].orEmpty().startsWith("https://www.youtube.com/watch?v="), probed.tags.toString())
                assertEquals(listOf("audio", "video"), probed.streams.map { it.codecType }.sorted(), probed.streams.toString())
                assertEquals("aac", probed.streams.single { it.codecType == "audio" }.codecName, "YouTube's AAC stream is copied as it is")
                val cover = probed.streams.single { it.codecType == "video" }
                assertTrue(cover.attachedPic, "the picture must be an attached cover")
                assertNotNull(cover.width)
                assertEquals(cover.width, cover.height, "the cover is cropped to a square")
            } finally {
                root.toFile().deleteRecursively()
            }
        }
    }
}

package com.xgetsongs.engine.integration

import com.xgetsongs.engine.DownloadRequest
import com.xgetsongs.engine.job.DefaultDownloadService
import com.xgetsongs.engine.job.ItemDownloader
import com.xgetsongs.engine.output.LocalFolderSink
import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.engine.process.SystemProcessRunner
import com.xgetsongs.engine.tools.ToolLocator
import com.xgetsongs.engine.ytdlp.YtDlpResolver
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobStatus
import com.xgetsongs.shared.api.Stage
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.jupiter.api.Assumptions.assumeTrue
import org.junit.jupiter.api.Tag
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
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

    /**
     * Passes everything to the real runner and remembers the progress lines yt-dlp prints (`XGSP|...` while downloading,
     * `XGSPP|<status>|<post-processor>` around each post-processor), in order, repeats collapsed.
     */
    private class ProgressRecordingRunner(private val real: ProcessRunner) : ProcessRunner {
        val sequence = CopyOnWriteArrayList<String>()

        override suspend fun run(command: List<String>, onStdout: (String) -> Unit, onStderr: (String) -> Unit): Int =
            real.run(
                command,
                onStdout = { line ->
                    val label = when {
                        line.startsWith("XGSPP|") -> line.trim()
                        line.startsWith("XGSP|") -> "XGSP|" + line.split('|').getOrNull(1)
                        else -> null
                    }
                    if (label != null && sequence.lastOrNull() != label) sequence += label
                    onStdout(line)
                },
                onStderr = onStderr,
            )
    }

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
            assertTrue(available.all { Regex("""\d{3} .+ - .+\.mp3""").matches(it.expectedFileName!!) })
        }
    }

    // JUnit does not discover test methods with a non-void return type, and the last expression of this
    // runBlocking block is a Boolean, so the return type must be declared Unit explicitly.
    @Test
    fun downloadsAShortVideoAsMp3(): Unit = runBlocking {
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

                val recording = ProgressRecordingRunner(runner)
                val service = DefaultDownloadService(ItemDownloader(recording, locator, resolver), root.resolve("work"), this)
                val events = service
                    .start(
                        DownloadRequest(
                            resolved.items, LocalFolderSink(outDir), overwrite = false, concurrency = 1, album = "Test Album",
                        ),
                    )
                    .events.receiveAsFlow().toList()

                val done = events.last() as JobEvent.JobDone
                assertEquals(JobStatus.COMPLETED, done.status, events.toString())
                assertEquals(1, done.summary.succeeded, events.toString())
                val file = Files.list(outDir).use { it.toList().single() }
                println("downloaded: ${file.fileName} (${Files.size(file)} bytes)")
                assertTrue(Regex("""001 .+ - .+\.mp3""").matches(file.fileName.toString()), file.fileName.toString())
                assertTrue(Files.size(file) > 50_000)

                // The thumbnail converter runs before the download and every post-processor prints its progress line:
                // none of them may make the job report "converting" before the download has started.
                println("progress lines in order: ${recording.sequence}")
                println("post-processors seen: ${recording.sequence.filter { it.startsWith("XGSPP|") }.map { it.split('|').last() }.distinct()}")
                val stages = events.filterIsInstance<JobEvent.Progress>().map { it.stage }
                println("reported stages: ${stages.fold(emptyList<Stage>()) { all, next -> if (all.lastOrNull() == next) all else all + next }}")
                val firstDownloading = stages.indexOf(Stage.DOWNLOADING)
                assertTrue(firstDownloading >= 0, "no download progress was reported: $stages")
                assertTrue(Stage.CONVERTING !in stages.take(firstDownloading), "converting was reported before the download: $stages")
                assertTrue(Stage.CONVERTING in stages, "the audio conversion must still be reported: $stages")

                val probed = ffprobe.probe(file)
                println("tags: ${probed.tags}; streams: ${probed.streams}")
                assertEquals("Me at the zoo", probed.tags["title"])
                assertEquals("jawed", probed.tags["artist"])
                assertEquals("jawed", probed.tags["album_artist"])
                assertEquals("Test Album", probed.tags["album"])
                assertEquals("1", probed.tags["track"])
                assertTrue(probed.tags["comment"].orEmpty().startsWith("https://www.youtube.com/watch?v="), probed.tags.toString())
                assertEquals(listOf("audio", "video"), probed.streams.map { it.codecType }.sorted(), probed.streams.toString())
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

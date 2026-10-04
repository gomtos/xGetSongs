package com.xgetsongs.engine.integration

import com.xgetsongs.engine.DownloadRequest
import com.xgetsongs.engine.job.DefaultDownloadService
import com.xgetsongs.engine.job.ItemDownloader
import com.xgetsongs.engine.output.LocalFolderSink
import com.xgetsongs.engine.process.SystemProcessRunner
import com.xgetsongs.engine.tools.ToolLocator
import com.xgetsongs.engine.ytdlp.YtDlpResolver
import com.xgetsongs.shared.api.JobEvent
import com.xgetsongs.shared.api.JobStatus
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.jupiter.api.Assumptions.assumeTrue
import org.junit.jupiter.api.Tag
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
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
            assertTrue(available.all { Regex("""\d{3} .+ - .+\.mp3""").matches(it.expectedFileName!!) })
        }
    }

    // JUnit does not discover test methods with a non-void return type, and the last expression of this
    // runBlocking block is a Boolean, so the return type must be declared Unit explicitly.
    @Test
    fun downloadsAShortVideoAsMp3(): Unit = runBlocking {
        withTimeout(180_000) {
            // "Me at the zoo": the first video ever uploaded to YouTube, 19 seconds long.
            val resolver = YtDlpResolver(runner, locator)
            val resolved = resolver.resolve("https://www.youtube.com/watch?v=jNQXAC9IVRw")
            val root = Files.createTempDirectory("xgs-integration")
            try {
                val outDir = root.resolve("out")

                val service = DefaultDownloadService(ItemDownloader(runner, locator, resolver), root.resolve("work"), this)
                val events = service
                    .start(DownloadRequest(resolved.items, LocalFolderSink(outDir), overwrite = false, concurrency = 1))
                    .events.receiveAsFlow().toList()

                val done = events.last() as JobEvent.JobDone
                assertEquals(JobStatus.COMPLETED, done.status, events.toString())
                assertEquals(1, done.summary.succeeded, events.toString())
                val file = Files.list(outDir).use { it.toList().single() }
                println("downloaded: ${file.fileName} (${Files.size(file)} bytes)")
                assertTrue(Regex("""001 .+ - .+\.mp3""").matches(file.fileName.toString()), file.fileName.toString())
                assertTrue(Files.size(file) > 50_000)
            } finally {
                root.toFile().deleteRecursively()
            }
        }
    }
}

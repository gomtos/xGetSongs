package com.xgetsongs.server

import com.xgetsongs.engine.job.DefaultDownloadService
import com.xgetsongs.engine.job.ItemDownloader
import com.xgetsongs.engine.lyrics.LrclibLyricsProvider
import com.xgetsongs.engine.process.SystemProcessRunner
import com.xgetsongs.engine.tools.DefaultToolManager
import com.xgetsongs.engine.tools.ToolLocator
import com.xgetsongs.engine.tools.ToolPathProvider
import com.xgetsongs.engine.tools.ToolPaths
import com.xgetsongs.engine.ytdlp.YtDlpResolver
import io.ktor.server.engine.embeddedServer
import io.ktor.server.netty.Netty
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.slf4j.LoggerFactory
import java.nio.file.Path
import java.security.SecureRandom
import java.util.Base64

/** Where the app keeps its own tools (a yt-dlp it installed). */
private fun binDirOf(appDataDir: Path): Path = appDataDir.resolve("bin")

/** Wires the real engine implementations. Leftover work files from a previous run are removed. */
fun createServices(appDataDir: Path, scope: CoroutineScope): Services {
    val binDir = binDirOf(appDataDir)
    val workDir = appDataDir.resolve("work")
    workDir.toFile().deleteRecursively()

    val runner = SystemProcessRunner()
    val locator = ToolLocator(appBinDir = binDir)
    val resolver = YtDlpResolver(runner, locator)
    // Lyrics are looked up on lrclib.net, but only for the jobs whose options allow it (JobOptions.searchLyricsOnline).
    val downloader = ItemDownloader(runner, locator, resolver, LrclibLyricsProvider())
    return Services(
        resolver = resolver,
        downloads = DefaultDownloadService(downloader, workDir, scope),
        tools = DefaultToolManager(locator, runner, binDir),
    )
}

/** The desktop app's embedded server: loopback only, on a random port, guarded by a random token. */
class LocalServer private constructor(
    val port: Int,
    val token: String,
    private val server: io.ktor.server.engine.EmbeddedServer<*, *>,
    private val scope: CoroutineScope,
    private val jobs: JobRegistry,
    private val lifecycle: Lifecycle,
) {
    /** How many download jobs are registered: running ones, plus any that ended but whose last events nobody has read yet. */
    fun runningJobs(): Int = jobs.size()

    fun stop() {
        jobs.markClosing() // the event streams that end from now on were not dropped by their readers
        lifecycle.markStopped { log.info("내장 서버 정지") }
        scope.cancel()
        server.stop(gracePeriodMillis = 200, timeoutMillis = 2_000)
    }

    companion object {
        private val log = LoggerFactory.getLogger(LocalServer::class.java)

        fun start(appDataDir: Path): LocalServer {
            val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
            return start(createServices(appDataDir, scope), scope, ToolLocator(appBinDir = binDirOf(appDataDir)))
        }

        /**
         * [tools] only serves the start line: where the tools are (it is not asked again). Walking PATH can stall on a
         * share that does not answer, so it is done off the thread that starts the app, and the start line follows
         * when it is done. Without [tools] the line is written at once.
         */
        fun start(
            services: Services,
            scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default),
            tools: ToolPathProvider? = null,
        ): LocalServer {
            val token = newToken()
            val jobs = JobRegistry()
            val server = embeddedServer(Netty, port = 0, host = "127.0.0.1") {
                module(services, ServerConfig(token = token, mode = ServerMode.LOCAL), jobs)
            }
            server.start(wait = false)
            val port = runBlocking { server.engine.resolvedConnectors().first().port }
            val lifecycle = Lifecycle()
            if (tools == null) {
                lifecycle.ifRunning { log.info("내장 서버 시작: 127.0.0.1:{}", port) }
            } else {
                scope.launch(Dispatchers.IO) { logStart(port, tools, lifecycle) }
            }
            return LocalServer(port, token, server, scope, jobs, lifecycle)
        }

        private fun logStart(port: Int, tools: ToolPathProvider, lifecycle: Lifecycle) {
            val paths = try {
                describe(tools.current())
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                "도구 경로를 확인하지 못함 (${e.javaClass.simpleName})"
            }
            lifecycle.ifRunning { log.info("내장 서버 시작: 127.0.0.1:{}, {}", port, paths) }
        }

        private fun describe(paths: ToolPaths): String =
            "yt-dlp=${paths.ytDlp ?: "없음"}, ffmpeg=${paths.ffmpeg ?: "없음"}, JS 런타임=${paths.jsRuntime ?: "없음"}"

        private fun newToken(): String {
            val bytes = ByteArray(32).also { SecureRandom().nextBytes(it) }
            return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes)
        }
    }
}

/**
 * Orders the two lines of a server's life in the log. The start line comes from another thread, after the tool paths are
 * looked up; it is dropped when the stop line is out already, so the log never says the server started after it stopped.
 */
internal class Lifecycle {
    private var stopped = false

    /** Runs [action] unless the server was stopped. */
    fun ifRunning(action: () -> Unit) = synchronized(this) { if (!stopped) action() }

    /** Marks the server stopped and runs [action] (the stop line) in the same step. */
    fun markStopped(action: () -> Unit) = synchronized(this) {
        stopped = true
        action()
    }
}

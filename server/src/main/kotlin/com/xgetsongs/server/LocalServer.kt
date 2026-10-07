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
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
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
) {
    /** How many download jobs are registered: running ones, plus any that ended but whose last events nobody has read yet. */
    fun runningJobs(): Int = jobs.size()

    fun stop() {
        log.info("내장 서버 정지")
        scope.cancel()
        server.stop(gracePeriodMillis = 200, timeoutMillis = 2_000)
    }

    companion object {
        private val log = LoggerFactory.getLogger(LocalServer::class.java)

        fun start(appDataDir: Path): LocalServer {
            val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
            return start(createServices(appDataDir, scope), scope, ToolLocator(appBinDir = binDirOf(appDataDir)))
        }

        /** [tools] only serves the startup record: where the tools are (it is not asked again). */
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
            log.info("내장 서버 시작: 127.0.0.1:{}{}", port, tools?.let { ", ${describe(it.current())}" }.orEmpty())
            return LocalServer(port, token, server, scope, jobs)
        }

        private fun describe(paths: ToolPaths): String =
            "yt-dlp=${paths.ytDlp ?: "없음"}, ffmpeg=${paths.ffmpeg ?: "없음"}, JS 런타임=${paths.jsRuntime ?: "없음"}"

        private fun newToken(): String {
            val bytes = ByteArray(32).also { SecureRandom().nextBytes(it) }
            return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes)
        }
    }
}

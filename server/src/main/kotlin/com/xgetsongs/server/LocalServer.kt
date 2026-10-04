package com.xgetsongs.server

import com.xgetsongs.engine.job.DefaultDownloadService
import com.xgetsongs.engine.job.ItemDownloader
import com.xgetsongs.engine.process.SystemProcessRunner
import com.xgetsongs.engine.tools.DefaultToolManager
import com.xgetsongs.engine.tools.ToolLocator
import com.xgetsongs.engine.ytdlp.YtDlpResolver
import io.ktor.server.engine.embeddedServer
import io.ktor.server.netty.Netty
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import java.nio.file.Path
import java.security.SecureRandom
import java.util.Base64

/** Wires the real engine implementations. Leftover work files from a previous run are removed. */
fun createServices(appDataDir: Path, scope: CoroutineScope): Services {
    val binDir = appDataDir.resolve("bin")
    val workDir = appDataDir.resolve("work")
    workDir.toFile().deleteRecursively()

    val runner = SystemProcessRunner()
    val locator = ToolLocator(appBinDir = binDir)
    val resolver = YtDlpResolver(runner, locator)
    val downloader = ItemDownloader(runner, locator, resolver)
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
) {
    fun stop() {
        scope.cancel()
        server.stop(gracePeriodMillis = 200, timeoutMillis = 2_000)
    }

    companion object {
        fun start(appDataDir: Path): LocalServer {
            val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
            return start(createServices(appDataDir, scope), scope)
        }

        fun start(services: Services, scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)): LocalServer {
            val token = newToken()
            val server = embeddedServer(Netty, port = 0, host = "127.0.0.1") {
                module(services, ServerConfig(token = token, mode = ServerMode.LOCAL))
            }
            server.start(wait = false)
            val port = runBlocking { server.engine.resolvedConnectors().first().port }
            return LocalServer(port, token, server, scope)
        }

        private fun newToken(): String {
            val bytes = ByteArray(32).also { SecureRandom().nextBytes(it) }
            return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes)
        }
    }
}

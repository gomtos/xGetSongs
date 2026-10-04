package com.xgetsongs.engine.tools

import com.xgetsongs.engine.ToolException
import com.xgetsongs.engine.ToolManager
import com.xgetsongs.engine.process.ProcessRunner
import com.xgetsongs.shared.api.ActionResult
import com.xgetsongs.shared.api.ToolInfo
import com.xgetsongs.shared.api.ToolsStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.runInterruptible
import kotlinx.coroutines.withContext
import java.io.IOException
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption
import java.time.Duration

/**
 * Reports tool versions and installs/updates yt-dlp. Installing downloads a file from GitHub, so
 * callers must ask the user for consent before calling [installYtDlp].
 */
class DefaultToolManager(
    private val tools: ToolPathProvider,
    private val runner: ProcessRunner,
    private val binDir: Path,
    private val fetch: suspend (url: String) -> ByteArray = ::httpGet,
) : ToolManager {

    override suspend fun status(): ToolsStatus {
        val paths = tools.current()
        return ToolsStatus(
            ytDlp = info(paths.ytDlp, "--version") { it },
            ffmpeg = info(paths.ffmpeg, "-version") { it.removePrefix("ffmpeg version ").substringBefore(' ') },
            jsRuntime = jsRuntimeInfo(paths.jsRuntime),
        )
    }

    override suspend fun installYtDlp(): ActionResult {
        val bytes = try {
            fetch(YTDLP_URL)
        } catch (e: IOException) {
            throw ToolException("yt-dlp 다운로드에 실패했습니다: ${e.message}")
        }
        if (bytes.size < MIN_YTDLP_BYTES) throw ToolException("내려받은 파일이 너무 작습니다. 다시 시도하세요.")
        // Stage next to the target, verify that it runs, and only then replace yt-dlp.exe, so a bad
        // download never shadows (or overwrites) a working binary.
        val staged = binDir.resolve("yt-dlp.new.exe")
        try {
            withContext(Dispatchers.IO) {
                Files.createDirectories(binDir)
                Files.write(staged, bytes)
            }
            val version = firstLine(staged, "--version")
                ?: throw ToolException("내려받은 yt-dlp를 실행할 수 없습니다.")
            withContext(Dispatchers.IO) {
                Files.move(staged, binDir.resolve("yt-dlp.exe"), StandardCopyOption.REPLACE_EXISTING)
            }
            return ActionResult("yt-dlp $version 을(를) 설치했습니다.")
        } catch (e: IOException) {
            throw ToolException("yt-dlp를 설치하지 못했습니다: ${e.message}")
        } finally {
            // Best effort: a stale yt-dlp.new.exe is harmless (the next install overwrites it and ToolLocator ignores it).
            withContext(NonCancellable + Dispatchers.IO) {
                try {
                    Files.deleteIfExists(staged)
                } catch (ignored: IOException) {
                }
            }
        }
    }

    override suspend fun updateYtDlp(): ActionResult {
        val ytDlp = tools.current().ytDlp ?: throw ToolException("yt-dlp가 설치되어 있지 않습니다.")
        val lines = mutableListOf<String>()
        val exitCode = try {
            runner.run(
                listOf(ytDlp.toString(), "--ignore-config", "-U"),
                onStdout = { synchronized(lines) { lines += it } },
                onStderr = { synchronized(lines) { lines += it } },
            )
        } catch (e: IOException) {
            throw ToolException("yt-dlp를 실행할 수 없습니다: ${e.message}")
        }
        val output = synchronized(lines) { lines.filter { it.isNotBlank() } }
        if (exitCode != 0) {
            throw ToolException(output.lastOrNull() ?: "yt-dlp 업데이트에 실패했습니다.")
        }
        return ActionResult(output.lastOrNull() ?: "yt-dlp를 업데이트했습니다.")
    }

    private suspend fun info(path: Path?, versionArg: String, parse: (String) -> String): ToolInfo {
        if (path == null) return ToolInfo(found = false)
        val version = firstLine(path, versionArg)?.let(parse)
        return ToolInfo(found = version != null, version = version, path = path.toString())
    }

    private suspend fun jsRuntimeInfo(path: Path?): ToolInfo {
        if (path == null) return ToolInfo(found = false)
        val name = path.fileName.toString().lowercase().removeSuffix(".exe")
        val line = firstLine(path, "--version")
        val version = when (name) {
            "deno" -> line?.split(' ')?.getOrNull(1)
            else -> line?.removePrefix("v")
        }
        val minimum = if (name == "deno") DENO_MINIMUM else NODE_MINIMUM
        val supported = version != null && isAtLeast(version, minimum)
        return ToolInfo(found = supported, version = version, path = path.toString())
    }

    private suspend fun firstLine(path: Path, arg: String): String? {
        val lines = mutableListOf<String>()
        val exitCode = try {
            runner.run(listOf(path.toString(), arg), onStdout = { synchronized(lines) { lines += it } })
        } catch (e: IOException) {
            return null
        }
        if (exitCode != 0) return null
        return synchronized(lines) { lines.firstOrNull { it.isNotBlank() } }?.trim()
    }

    private fun isAtLeast(version: String, minimum: List<Int>): Boolean {
        val parts = version.split('.').map { it.takeWhile(Char::isDigit).toIntOrNull() ?: 0 }
        for (i in minimum.indices) {
            val actual = parts.getOrElse(i) { 0 }
            if (actual != minimum[i]) return actual > minimum[i]
        }
        return true
    }

    companion object {
        const val YTDLP_URL = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe"
        private const val MIN_YTDLP_BYTES = 1_000_000
        private val DENO_MINIMUM = listOf(2, 3, 0)
        private val NODE_MINIMUM = listOf(22, 0, 0)
    }
}

private suspend fun httpGet(url: String): ByteArray = runInterruptible(Dispatchers.IO) {
    val client = HttpClient.newBuilder()
        .followRedirects(HttpClient.Redirect.NORMAL)
        .connectTimeout(Duration.ofSeconds(15))
        .build()
    val response = client.send(HttpRequest.newBuilder(URI.create(url)).GET().build(), HttpResponse.BodyHandlers.ofByteArray())
    if (response.statusCode() != 200) throw IOException("HTTP ${response.statusCode()}")
    response.body()
}

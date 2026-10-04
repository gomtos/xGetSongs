package com.xgetsongs.engine.tools

import java.io.File
import java.nio.file.Files
import java.nio.file.Path

data class ToolPaths(
    val ytDlp: Path?,
    val ffmpeg: Path?,
    /** Deno or Node, which yt-dlp needs to solve YouTube's JavaScript challenges. */
    val jsRuntime: Path?,
)

fun interface ToolPathProvider {
    fun current(): ToolPaths
}

/**
 * Finds the external tools. The app's own [appBinDir] wins over PATH so an app-managed yt-dlp is
 * preferred. Re-evaluated on every call so a freshly installed tool is picked up immediately.
 */
class ToolLocator(
    private val appBinDir: Path,
    private val pathEnv: String = System.getenv("PATH").orEmpty(),
    private val ffmpegOverride: Path? = null,
) : ToolPathProvider {
    override fun current(): ToolPaths = ToolPaths(
        ytDlp = find("yt-dlp"),
        ffmpeg = ffmpegOverride?.takeIf { Files.isRegularFile(it) } ?: find("ffmpeg"),
        jsRuntime = find("deno") ?: find("node"),
    )

    private fun find(name: String): Path? {
        val dirs = listOf(appBinDir) + pathEnv.split(File.pathSeparator)
            .filter { it.isNotBlank() }
            .mapNotNull { runCatching { Path.of(it.trim('"')) }.getOrNull() }
        for (dir in dirs) {
            for (candidate in listOf("$name.exe", name)) {
                val path = dir.resolve(candidate)
                if (Files.isRegularFile(path)) return path
            }
        }
        return null
    }
}

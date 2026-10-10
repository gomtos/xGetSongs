package com.xgetsongs.engine.ytdlp

import com.xgetsongs.engine.tools.ToolPaths
import java.nio.file.Path

/**
 * Every yt-dlp command line lives here. Commands are argument lists (never shell strings) and the
 * URL always comes last, after `--`, so it can never be read as an option.
 */
object YtDlpCommands {
    const val PROGRESS_PREFIX = "XGSP"

    private val COMMON = listOf("--ignore-config", "--no-warnings", "--encoding", "utf-8")

    /** Lists a playlist's entries without extracting each video (fast). */
    fun resolvePlaylist(tools: ToolPaths, url: String): List<String> =
        listOf(ytDlp(tools)) + COMMON + listOf("--flat-playlist", "-J", "--", url)

    /** Full metadata for one video. */
    fun resolveVideo(tools: ToolPaths, url: String): List<String> =
        listOf(ytDlp(tools)) + COMMON + jsRuntimeArgs(tools) + listOf("--no-playlist", "-J", "--", url)

    /**
     * Downloads the AAC audio stream of [url] (YouTube's itag 140) as it is, without re-encoding, as
     * `<outputDir>/<videoId>.m4a`, and leaves the thumbnail, converted to JPEG, as `<outputDir>/<videoId>.jpg` (the
     * cover for the tags) and the video's info as `<outputDir>/<videoId>.info.json` (where [VideoInfoFile] reads the
     * album from). The m4a is a fragmented DASH file: `--fixup never` leaves it so, because the tag step rewrites the
     * container anyway.
     */
    fun download(tools: ToolPaths, url: String, outputDir: Path, videoId: String): List<String> {
        val progress = "download:$PROGRESS_PREFIX|%(progress.status)s|%(progress.downloaded_bytes)s|" +
            "%(progress.total_bytes)s|%(progress.total_bytes_estimate)s"
        val output = outputDir.resolve("$videoId.%(ext)s").toString()
        val ffmpeg = tools.ffmpeg?.let { listOf("--ffmpeg-location", it.toString()) }.orEmpty()
        return listOf(ytDlp(tools)) + COMMON + jsRuntimeArgs(tools) + ffmpeg + listOf(
            "--no-playlist", "--newline",
            "--progress-template", progress,
            "-f", "bestaudio[ext=m4a]", "--fixup", "never",
            "--write-thumbnail", "--convert-thumbnails", "jpg",
            "--write-info-json",
            "-o", output,
            "--", url,
        )
    }

    private fun ytDlp(tools: ToolPaths): String =
        requireNotNull(tools.ytDlp) { "yt-dlp path is required" }.toString()

    private fun jsRuntimeArgs(tools: ToolPaths): List<String> {
        val path = tools.jsRuntime ?: return emptyList()
        val name = path.fileName.toString().lowercase().removeSuffix(".exe")
        return if (name == "deno" || name == "node") listOf("--js-runtimes", "$name:$path") else emptyList()
    }
}

package com.xgetsongs.engine.ytdlp

import java.nio.file.Path

/** Every ffmpeg command line lives here. Commands are argument lists (never shell strings). */
object FfmpegCommands {
    private val COVER_OUTPUT = listOf(
        "-c:v", "mjpeg", "-q:v", "2",
        // `\,` keeps the comma inside the filter argument. This is a single argument: no quoting is involved.
        "-vf", "crop=min(iw\\,ih):min(iw\\,ih)",
        "-disposition:v", "attached_pic",
        "-metadata:s:v", "title=Album cover",
        "-metadata:s:v", "comment=Cover (front)",
    )

    /**
     * Copies the audio of [input] into [output] without re-encoding and writes ID3v2.4 tags (text in UTF-8) read from the
     * ffmetadata file [metadataFile]. With a [cover] the picture is cropped to a centered square, re-encoded as JPEG and
     * attached as the front cover. [output] must end in `.mp3`: ffmpeg picks the muxer from the extension.
     */
    fun tag(ffmpeg: Path, input: Path, cover: Path?, metadataFile: Path, output: Path): List<String> {
        require(output.fileName.toString().endsWith(".mp3", ignoreCase = true)) { "output must be an .mp3 file: $output" }
        val coverInput = cover?.let { listOf("-i", it.toString()) }.orEmpty()
        val coverMap = if (cover != null) listOf("-map", "1:v") else emptyList()
        val coverOutput = if (cover != null) COVER_OUTPUT else emptyList()
        // Inputs are numbered in order: the mp3 is 0, the cover (when there is one) is 1, the ffmetadata file is last.
        val metadataIndex = if (cover != null) 2 else 1
        return listOf(ffmpeg.toString(), "-hide_banner", "-loglevel", "error", "-nostdin", "-y") +
            listOf("-i", input.toString()) + coverInput + listOf("-f", "ffmetadata", "-i", metadataFile.toString()) +
            listOf("-map", "0:a") + coverMap +
            listOf("-map_chapters", "-1", "-map_metadata", metadataIndex.toString()) +
            listOf("-c:a", "copy") + coverOutput +
            listOf("-id3v2_version", "4", output.toString())
    }
}

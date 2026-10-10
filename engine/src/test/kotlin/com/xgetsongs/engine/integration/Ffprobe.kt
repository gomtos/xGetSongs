package com.xgetsongs.engine.integration

import com.xgetsongs.engine.process.ProcessRunner
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import java.nio.file.Files
import java.nio.file.Path

/** One stream of a probed file. [attachedPic] is ffprobe's `disposition.attached_pic` (an embedded cover). */
data class ProbedStream(
    val codecType: String,
    val codecName: String,
    val width: Int?,
    val height: Int?,
    val attachedPic: Boolean,
)

/** What ffprobe reports about a file: the container's tags and its streams. */
data class Probed(val tags: Map<String, String>, val streams: List<ProbedStream>)

/** Reads a file with the real ffprobe, which is installed next to the real ffmpeg. */
class Ffprobe(private val ffprobe: Path, private val runner: ProcessRunner) {
    suspend fun probe(file: Path): Probed {
        val stdout = mutableListOf<String>()
        val stderr = mutableListOf<String>()
        val exitCode = runner.run(
            listOf(
                ffprobe.toString(), "-v", "error", "-of", "json",
                "-show_entries", "format_tags:stream=codec_type,codec_name,width,height:stream_disposition=attached_pic",
                file.toString(),
            ),
            onStdout = { synchronized(stdout) { stdout += it } },
            onStderr = { synchronized(stderr) { stderr += it } },
        )
        check(exitCode == 0) { "ffprobe failed ($exitCode): $stderr" }
        return parse(Json.parseToJsonElement(stdout.joinToString("\n")).jsonObject)
    }

    private fun parse(root: JsonObject): Probed {
        val tags = root["format"]?.jsonObject?.get("tags")?.jsonObject
            ?.mapValues { it.value.jsonPrimitive.content }.orEmpty()
        val streams = root["streams"]?.jsonArray.orEmpty().map { element ->
            val stream = element.jsonObject
            ProbedStream(
                codecType = stream.getValue("codec_type").jsonPrimitive.content,
                codecName = stream.getValue("codec_name").jsonPrimitive.content,
                width = stream["width"]?.jsonPrimitive?.int,
                height = stream["height"]?.jsonPrimitive?.int,
                attachedPic = stream["disposition"]?.jsonObject?.get("attached_pic")?.jsonPrimitive?.int == 1,
            )
        }
        return Probed(tags, streams)
    }

    companion object {
        /** ffprobe sits next to ffmpeg (`ffmpeg.exe` -> `ffprobe.exe`); null when it is not there. */
        fun besides(ffmpeg: Path?): Path? =
            ffmpeg?.resolveSibling(ffmpeg.fileName.toString().replaceFirst("ffmpeg", "ffprobe"))
                ?.takeIf { Files.isRegularFile(it) }
    }
}

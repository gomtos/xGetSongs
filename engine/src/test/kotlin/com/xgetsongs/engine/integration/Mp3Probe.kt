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

/** The frames of the ID3v2 tag at the start of an mp3, read from the raw bytes (ffprobe does not show frame types). */
class Id3v2Tag private constructor(val version: Int, val frames: List<Frame>) {
    class Frame(val id: String, val body: ByteArray)

    val ids: List<String> get() = frames.map { it.id }

    fun frame(id: String): Frame = frames.first { it.id == id }

    companion object {
        fun read(file: Path): Id3v2Tag {
            val bytes = Files.readAllBytes(file)
            check(bytes.size > 10 && String(bytes, 0, 3, Charsets.ISO_8859_1) == "ID3") { "no ID3v2 tag in $file" }
            val tagSize = (6..9).fold(0) { size, i -> (size shl 7) or (bytes[i].toInt() and 0x7F) }
            val frames = mutableListOf<Frame>()
            var pos = 10
            // ID3v2.3 frame: 4-byte id, 4-byte big-endian size, 2 flag bytes, body.
            while (pos + 10 <= 10 + tagSize && bytes[pos].toInt() != 0) {
                val id = String(bytes, pos, 4, Charsets.ISO_8859_1)
                val size = (4..7).fold(0) { size, i -> (size shl 8) or (bytes[pos + i].toInt() and 0xFF) }
                frames += Frame(id, bytes.copyOfRange(pos + 10, pos + 10 + size))
                pos += 10 + size
            }
            return Id3v2Tag(bytes[3].toInt(), frames)
        }

        /** True when the last 128 bytes hold an ID3v1 block. */
        fun hasId3v1(file: Path): Boolean {
            val bytes = Files.readAllBytes(file)
            return bytes.size >= 128 && String(bytes, bytes.size - 128, 3, Charsets.ISO_8859_1) == "TAG"
        }
    }
}

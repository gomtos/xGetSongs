package com.xgetsongs.engine.testutil

import java.nio.file.Files
import java.nio.file.Path

/**
 * The frames of the ID3v2.3 tag at the start of an mp3, read from the raw bytes. It is written independently of the
 * engine's own tag code, so tests can check what really ended up in a file (ffprobe does not show frame types).
 */
class Id3v2Tag private constructor(val version: Int, val flags: Int, val size: Int, val frames: List<Frame>) {
    class Frame(val id: String, val body: ByteArray)

    /** A decoded COMM frame. */
    data class Comment(val encoding: Int, val language: String, val description: String, val text: String)

    val ids: List<String> get() = frames.map { it.id }

    fun frame(id: String): Frame = frames.first { it.id == id }

    /** Every COMM frame, decoded (text encodings 0 = ISO-8859-1 and 1 = UTF-16 with a byte order mark). */
    fun comments(): List<Comment> = frames.filter { it.id == "COMM" }.map { frame ->
        val body = frame.body
        val encoding = body[0].toInt()
        val language = String(body, 1, 3, Charsets.ISO_8859_1)
        val rest = body.copyOfRange(4, body.size)
        when (encoding) {
            0 -> {
                val end = rest.indexOf(0)
                Comment(0, language, String(rest, 0, end, Charsets.ISO_8859_1), String(rest, end + 1, rest.size - end - 1, Charsets.ISO_8859_1))
            }
            1 -> {
                // Both strings carry their own byte order mark; the description ends with an aligned 00 00.
                var end = 2
                while (!(rest[end].toInt() == 0 && rest[end + 1].toInt() == 0)) end += 2
                Comment(1, language, utf16(rest, 0, end), utf16(rest, end + 2, rest.size))
            }
            else -> error("unexpected text encoding $encoding")
        }
    }

    private fun utf16(bytes: ByteArray, from: Int, to: Int): String {
        if (to - from < 2) return ""
        val bigEndian = (bytes[from].toInt() and 0xFF) == 0xFE && (bytes[from + 1].toInt() and 0xFF) == 0xFF
        return String(bytes, from + 2, to - from - 2, if (bigEndian) Charsets.UTF_16BE else Charsets.UTF_16LE)
    }

    private fun ByteArray.indexOf(value: Int): Int = indexOfFirst { it.toInt() == value }

    companion object {
        fun read(file: Path): Id3v2Tag = parse(Files.readAllBytes(file))

        fun parse(bytes: ByteArray): Id3v2Tag {
            check(bytes.size >= 10 && String(bytes, 0, 3, Charsets.ISO_8859_1) == "ID3") { "no ID3v2 tag" }
            val tagSize = (6..9).fold(0) { size, i ->
                check((bytes[i].toInt() and 0x80) == 0) { "tag size is not syncsafe" }
                (size shl 7) or bytes[i].toInt()
            }
            val frames = mutableListOf<Frame>()
            var pos = 10
            // ID3v2.3 frame: 4-byte id, 4-byte big-endian size, 2 flag bytes, body. Zero bytes after the frames are padding.
            while (pos + 10 <= 10 + tagSize && bytes[pos].toInt() != 0) {
                val id = String(bytes, pos, 4, Charsets.ISO_8859_1)
                val size = (4..7).fold(0) { size, i -> (size shl 8) or (bytes[pos + i].toInt() and 0xFF) }
                frames += Frame(id, bytes.copyOfRange(pos + 10, pos + 10 + size))
                pos += 10 + size
            }
            return Id3v2Tag(bytes[3].toInt(), bytes[5].toInt() and 0xFF, tagSize, frames)
        }

        /** True when the last 128 bytes hold an ID3v1 block. */
        fun hasId3v1(file: Path): Boolean {
            val bytes = Files.readAllBytes(file)
            return bytes.size >= 128 && String(bytes, bytes.size - 128, 3, Charsets.ISO_8859_1) == "TAG"
        }
    }
}

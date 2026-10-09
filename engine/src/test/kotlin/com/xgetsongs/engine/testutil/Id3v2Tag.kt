package com.xgetsongs.engine.testutil

import java.nio.charset.Charset
import java.nio.file.Files
import java.nio.file.Path

/**
 * The frames of the ID3v2.3 or ID3v2.4 tag at the start of an mp3, read from the raw bytes. It is written independently
 * of the engine's own tag code, so tests can check what really ended up in a file (ffprobe does not show frame types).
 */
class Id3v2Tag private constructor(val version: Int, val flags: Int, val size: Int, val frames: List<Frame>) {
    class Frame(val id: String, val body: ByteArray)

    /** A decoded COMM frame. */
    data class Comment(val encoding: Int, val language: String, val description: String, val text: String)

    /** A decoded USLT (unsynchronised lyrics) frame; it has the same layout as COMM. */
    data class Lyrics(val encoding: Int, val language: String, val descriptor: String, val text: String)

    val ids: List<String> get() = frames.map { it.id }

    fun frame(id: String): Frame = frames.first { it.id == id }

    /** Every COMM frame, decoded (text encodings 0 = ISO-8859-1, 1 = UTF-16 with a byte order mark, 3 = UTF-8). */
    fun comments(): List<Comment> = frames.filter { it.id == "COMM" }.map { frame ->
        val (encoding, language, description, text) = decodeLanguageText(frame)
        Comment(encoding, language, description, text)
    }

    /** Every USLT frame, decoded the same way as the COMM frames. */
    fun lyrics(): List<Lyrics> = frames.filter { it.id == "USLT" }.map { frame ->
        val (encoding, language, descriptor, text) = decodeLanguageText(frame)
        Lyrics(encoding, language, descriptor, text)
    }

    /** What COMM and USLT share: encoding byte, 3-byte language, a terminated description, then the text. */
    private fun decodeLanguageText(frame: Frame): Comment {
        val body = frame.body
        val encoding = body[0].toInt()
        val language = String(body, 1, 3, Charsets.ISO_8859_1)
        val rest = body.copyOfRange(4, body.size)
        return when (encoding) {
            0, 3 -> {
                // One byte per terminator; the text has no byte order mark. Encoding 0 is ISO-8859-1, encoding 3 is UTF-8.
                val charset: Charset = if (encoding == 0) Charsets.ISO_8859_1 else Charsets.UTF_8
                val end = rest.indexOf(0)
                Comment(encoding, language, String(rest, 0, end, charset), String(rest, end + 1, rest.size - end - 1, charset))
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
            val version = bytes[3].toInt()
            val tagSize = (6..9).fold(0) { size, i ->
                check((bytes[i].toInt() and 0x80) == 0) { "tag size is not syncsafe" }
                (size shl 7) or bytes[i].toInt()
            }
            // The frame size is syncsafe (7 bits per byte) in ID3v2.4 and a plain big-endian integer in ID3v2.3.
            val sizeBits = if (version >= 4) 7 else 8
            val frames = mutableListOf<Frame>()
            var pos = 10
            // Frame: 4-byte id, 4-byte size, 2 flag bytes, body. Zero bytes after the frames are padding.
            while (pos + 10 <= 10 + tagSize && bytes[pos].toInt() != 0) {
                val id = String(bytes, pos, 4, Charsets.ISO_8859_1)
                val size = (4..7).fold(0) { size, i -> (size shl sizeBits) or (bytes[pos + i].toInt() and 0xFF) }
                frames += Frame(id, bytes.copyOfRange(pos + 10, pos + 10 + size))
                pos += 10 + size
            }
            return Id3v2Tag(version, bytes[5].toInt() and 0xFF, tagSize, frames)
        }

        /** True when the last 128 bytes hold an ID3v1 block. */
        fun hasId3v1(file: Path): Boolean {
            val bytes = Files.readAllBytes(file)
            return bytes.size >= 128 && String(bytes, bytes.size - 128, 3, Charsets.ISO_8859_1) == "TAG"
        }
    }
}

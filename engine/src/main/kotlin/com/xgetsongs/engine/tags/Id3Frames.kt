package com.xgetsongs.engine.tags

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.StandardCopyOption

/**
 * Adds a `COMM` (comment) frame and a `USLT` (unsynchronised lyrics) frame to the ID3v2.3 tag at the start of an mp3.
 * ffmpeg's ID3 muxer cannot write either: it turns a `comment` or `lyrics` entry into a user-defined `TXXX` frame,
 * which most players do not show as the comment or the lyrics. So the engine patches the tag after ffmpeg has
 * finished. Everything else in the file is copied byte for byte.
 */
internal object Id3Frames {
    private const val HEADER_SIZE = 10
    private const val FRAME_HEADER_SIZE = 10
    private const val MAX_TAG_SIZE = 0x0FFFFFFF // 4 bytes of 7 bits
    private const val UNSUPPORTED = "unsupported ID3 tag layout"

    // The language of a lyrics frame: Korean when the text has a Hangul syllable, else English (the tag's language
    // field is a hint, there is no detection beyond this).
    private const val LANGUAGE_KOREAN = "kor"
    private const val LANGUAGE_ENGLISH = "eng"
    private const val HANGUL_SYLLABLES_FIRST = '가' // U+AC00
    private const val HANGUL_SYLLABLES_LAST = '힣' // U+D7A3

    // The flags byte of an ID3v2.3 header is %abc00000: a = unsynchronisation, b = extended header, c = experimental.
    // Only the experimental bit (0x20) leaves the layout alone, so it passes through. The other two change how the tag
    // must be read, and the five low bits are undefined in v2.3: a tag that sets one is not understood, so not rewritten.
    private const val FLAGS_WE_CANNOT_REWRITE = 0x80 or 0x40 or 0x1F

    /**
     * Rewrites [file] so that its ID3v2.3 tag also holds a `COMM` frame with [comment] and a `USLT` frame with
     * [lyrics], in one pass. The frames go right after the last existing frame, in front of any padding, `COMM` first.
     * A file without an ID3v2 tag gets a new tag holding only the new frames. A null or blank value writes no frame;
     * when both are null or blank nothing is done and the file is not even opened. Blocking.
     *
     * @throws IOException when the file cannot be read or written, or its tag is one that is not safe to extend
     * (another version, unsynchronisation, an extended header, flag bits that v2.3 does not define, or broken sizes).
     * The file is left untouched.
     */
    fun add(file: Path, comment: String?, lyrics: String?) {
        val frames = listOfNotNull(
            comment?.takeIf { it.isNotBlank() }?.let(::commentFrame),
            lyrics?.takeIf { it.isNotBlank() }?.let(::lyricsFrame),
        ).fold(ByteArray(0)) { all, next -> all + next }
        if (frames.isEmpty()) return

        val temp = file.resolveSibling(file.fileName.toString() + ".id3tmp")
        var failure: Throwable? = null
        try {
            val fileSize = Files.size(file)
            Files.newInputStream(file).use { input ->
                Files.newOutputStream(temp).use { output -> rewrite(input, output, fileSize, frames) }
            }
            Files.move(temp, file, StandardCopyOption.REPLACE_EXISTING)
        } catch (e: Throwable) {
            failure = e
            throw e
        } finally {
            // Only tidying up: it must not replace the error that is already on its way out. After a successful move
            // there is nothing left to delete.
            try {
                Files.deleteIfExists(temp)
            } catch (cleanup: IOException) {
                failure?.addSuppressed(cleanup)
            }
        }
    }

    private fun rewrite(input: InputStream, output: OutputStream, fileSize: Long, frames: ByteArray) {
        val header = input.readNBytes(HEADER_SIZE)
        if (!hasId3Magic(header)) {
            // No tag at all: a new one that only holds the new frames, then the file as it was.
            output.write(newHeader(frames.size))
            output.write(frames)
            output.write(header)
            input.transferTo(output)
            return
        }
        if (header.size < HEADER_SIZE) throw IOException("invalid ID3 tag: the header is cut short")
        val major = header[3].toInt()
        val flags = header[5].toInt() and 0xFF
        if (major != 3 || (flags and FLAGS_WE_CANNOT_REWRITE) != 0) throw IOException(UNSUPPORTED)

        val tagSize = readSyncsafe(header, 6)
        // Checked before reading, so a corrupt size cannot make us allocate a huge buffer.
        if (tagSize > fileSize - HEADER_SIZE) throw IOException("invalid ID3 tag: the file ends inside the tag")
        val tag = input.readNBytes(tagSize)
        val framesEnd = endOfFrames(tag)
        val newSize = tagSize + frames.size
        if (newSize > MAX_TAG_SIZE) throw IOException("invalid ID3 tag: too large")

        // The new frames go after the last real frame; the padding that followed it keeps following.
        output.write(header.copyOfRange(0, 6))
        output.write(syncsafe(newSize))
        output.write(tag, 0, framesEnd)
        output.write(frames)
        output.write(tag, framesEnd, tagSize - framesEnd)
        input.transferTo(output)
    }

    private fun hasId3Magic(bytes: ByteArray) =
        bytes.size >= 3 && bytes[0] == 'I'.code.toByte() && bytes[1] == 'D'.code.toByte() && bytes[2] == '3'.code.toByte()

    /** Where the frames in [tag] stop: the first zero byte (padding) or the end. */
    private fun endOfFrames(tag: ByteArray): Int {
        var pos = 0
        while (pos + FRAME_HEADER_SIZE <= tag.size && tag[pos].toInt() != 0) {
            // ID3v2.3 frame: 4-byte id, 4-byte big-endian size (plain, not syncsafe), 2 flag bytes, body.
            val size = readBigEndian(tag, pos + 4)
            if (size < 0 || size > tag.size - pos - FRAME_HEADER_SIZE) throw IOException("invalid ID3 tag: a frame runs past the tag")
            pos += FRAME_HEADER_SIZE + size
        }
        return pos
    }

    private fun readSyncsafe(bytes: ByteArray, at: Int): Int {
        var value = 0
        for (i in at until at + 4) {
            if ((bytes[i].toInt() and 0x80) != 0) throw IOException("invalid ID3 tag: the size is not syncsafe")
            value = (value shl 7) or bytes[i].toInt()
        }
        return value
    }

    private fun readBigEndian(bytes: ByteArray, at: Int): Int {
        var value = 0
        for (i in at until at + 4) value = (value shl 8) or (bytes[i].toInt() and 0xFF)
        return value
    }

    private fun syncsafe(value: Int) = byteArrayOf(
        ((value shr 21) and 0x7F).toByte(),
        ((value shr 14) and 0x7F).toByte(),
        ((value shr 7) and 0x7F).toByte(),
        (value and 0x7F).toByte(),
    )

    /** `ID3`, version 2.3.0, no flags, and the syncsafe [size] of the tag body. */
    private fun newHeader(size: Int) = byteArrayOf('I'.code.toByte(), 'D'.code.toByte(), '3'.code.toByte(), 3, 0, 0) + syncsafe(size)

    /**
     * A `COMM` frame: id, big-endian size, no flags, then encoding byte, language `eng`, an empty short description
     * (with its terminator) and the text. Pure ASCII text is stored as ISO-8859-1 (encoding 0), anything else as
     * UTF-16 with a byte order mark (encoding 1), where each string carries its own mark and the description ends
     * with two zero bytes.
     */
    private fun commentFrame(text: String): ByteArray {
        val body = ByteArrayOutputStream()
        if (text.all { it.code < 0x80 }) {
            body.write(0)
            body.write("eng".toByteArray(Charsets.ISO_8859_1))
            body.write(0)
            body.write(text.toByteArray(Charsets.ISO_8859_1))
        } else {
            body.write(1)
            body.write("eng".toByteArray(Charsets.ISO_8859_1))
            body.write(byteArrayOf(0xFF.toByte(), 0xFE.toByte(), 0, 0))
            body.write(byteArrayOf(0xFF.toByte(), 0xFE.toByte()))
            body.write(text.toByteArray(Charsets.UTF_16LE))
        }
        val payload = body.toByteArray()
        return frame("COMM", payload)
    }

    /**
     * A `USLT` frame: id, big-endian size, no flags, then encoding byte 1 (UTF-16 with a byte order mark), the language
     * (`kor` when the text has a Hangul syllable, else `eng`), an empty content descriptor (its own mark, then the two
     * zero bytes that end it) and the lyrics (their own mark, UTF-16LE, no terminator). Line breaks are written as
     * CR LF; a CR LF that is already there stays one.
     */
    private fun lyricsFrame(text: String): ByteArray {
        val lyrics = text.replace("\r\n", "\n").replace("\n", "\r\n")
        val language = if (lyrics.any { it in HANGUL_SYLLABLES_FIRST..HANGUL_SYLLABLES_LAST }) LANGUAGE_KOREAN else LANGUAGE_ENGLISH
        val body = ByteArrayOutputStream()
        body.write(1)
        body.write(language.toByteArray(Charsets.ISO_8859_1))
        body.write(byteArrayOf(0xFF.toByte(), 0xFE.toByte(), 0, 0))
        body.write(byteArrayOf(0xFF.toByte(), 0xFE.toByte()))
        body.write(lyrics.toByteArray(Charsets.UTF_16LE))
        return frame("USLT", body.toByteArray())
    }

    /** An ID3v2.3 frame: the 4-character [id], the plain big-endian size of [payload] (not syncsafe), two zero flag bytes, [payload]. */
    private fun frame(id: String, payload: ByteArray) =
        id.toByteArray(Charsets.ISO_8859_1) + bigEndian(payload.size) + byteArrayOf(0, 0) + payload

    private fun bigEndian(value: Int) = byteArrayOf(
        (value ushr 24).toByte(),
        (value ushr 16).toByte(),
        (value ushr 8).toByte(),
        value.toByte(),
    )
}

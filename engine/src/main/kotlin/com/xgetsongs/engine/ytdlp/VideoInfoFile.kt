package com.xgetsongs.engine.ytdlp

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path

/**
 * What the engine takes from the info file of a video: the [album] and the [description] (where uploaders often put the
 * lyrics). A field is null when the file has no usable value for it.
 */
data class VideoInfo(val album: String?, val description: String?)

/**
 * Reads the info file `<videoId>.info.json` that yt-dlp writes next to the audio (`--write-info-json`, see
 * [YtDlpCommands.download]). It holds everything yt-dlp knows about the video; the engine only needs the album and the
 * description.
 */
object VideoInfoFile {
    /** What yt-dlp prints for a field it does not know. It is not an album, whichever way it ends up in the file. */
    private const val NOT_AVAILABLE = "NA"

    private val NOTHING = VideoInfo(album = null, description = null)

    /**
     * The [VideoInfo] of the video in [file], from the keys of the root object only (the formats list and other nested
     * objects have keys of the same name that mean something else):
     *  - `album` (which YouTube fills for many official tracks), trimmed; null when it is absent, null, not a string,
     *    blank or yt-dlp's `NA` placeholder.
     *  - `description` as written, with the blank lines in front of the first text and the whitespace after the last
     *    text dropped, and the line breaks inside kept; null when it is absent, null, not a string or blank.
     *
     * Both are null when the file is missing, unreadable, not valid UTF-8 or not valid JSON, or its root is not an
     * object. The info file only improves the tags, so this never throws. Blocking; the file is closed again before
     * it returns.
     */
    fun read(file: Path): VideoInfo {
        val root = try {
            // readString decodes strictly: bytes that are not UTF-8 are an error, not a replacement character.
            Json.parseToJsonElement(Files.readString(file))
        } catch (e: IOException) {
            return NOTHING
        } catch (e: IllegalArgumentException) {
            // A SerializationException (invalid JSON) is one of these.
            return NOTHING
        }
        val info = root as? JsonObject ?: return NOTHING
        val album = stringOf(info, "album")?.trim()?.takeIf { it.isNotEmpty() && it != NOT_AVAILABLE }
        val description = stringOf(info, "description")?.let(::dropBlankEnds)?.takeIf { it.isNotEmpty() }
        return VideoInfo(album, description)
    }

    /** The album of the video in [file]: [VideoInfo.album] of [read]. */
    fun readAlbum(file: Path): String? = read(file).album

    /** The value of [key] when it is a JSON string; null when it is absent, null or of another type. */
    private fun stringOf(info: JsonObject, key: String): String? {
        val value = info[key] as? JsonPrimitive ?: return null
        return value.content.takeIf { value.isString }
    }

    /** Drops the blank lines in front of the first text (its own indentation stays) and all whitespace after the last text. */
    private fun dropBlankEnds(text: String): String {
        val firstText = text.indexOfFirst { !it.isWhitespace() }
        if (firstText < 0) return ""
        val lineStart = maxOf(text.lastIndexOf('\n', firstText), text.lastIndexOf('\r', firstText)) + 1
        return text.substring(lineStart).trimEnd()
    }
}

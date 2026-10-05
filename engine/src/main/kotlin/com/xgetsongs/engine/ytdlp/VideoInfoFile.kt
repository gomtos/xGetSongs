package com.xgetsongs.engine.ytdlp

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import java.io.IOException
import java.nio.file.Files
import java.nio.file.Path

/**
 * Reads the info file `<videoId>.info.json` that yt-dlp writes next to the audio (`--write-info-json`, see
 * [YtDlpCommands.download]). It holds everything yt-dlp knows about the video; the engine only needs the album.
 */
object VideoInfoFile {
    /** What yt-dlp prints for a field it does not know. It is not an album, whichever way it ends up in the file. */
    private const val NOT_AVAILABLE = "NA"

    /**
     * The album of the video in [file] (yt-dlp's `album`, which YouTube fills for many official tracks), trimmed.
     * Null when there is none: the file is missing, unreadable, not valid UTF-8 or not valid JSON, its root is not an
     * object, `album` is absent, null or not a string, or the text is blank or yt-dlp's `NA` placeholder. The info
     * file only improves a tag, so this never throws. Blocking; the file is closed again before it returns.
     */
    fun readAlbum(file: Path): String? {
        val root = try {
            // readString decodes strictly: bytes that are not UTF-8 are an error, not a replacement character.
            Json.parseToJsonElement(Files.readString(file))
        } catch (e: IOException) {
            return null
        } catch (e: IllegalArgumentException) {
            // A SerializationException (invalid JSON) is one of these.
            return null
        }
        val album = (root as? JsonObject)?.get("album") as? JsonPrimitive ?: return null
        if (!album.isString) return null
        return album.content.trim().takeIf { it.isNotEmpty() && it != NOT_AVAILABLE }
    }
}

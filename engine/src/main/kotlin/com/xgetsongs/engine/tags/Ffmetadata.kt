package com.xgetsongs.engine.tags

/**
 * Renders [TrackTags] as an ffmetadata file (`-f ffmetadata`), so tag text reaches ffmpeg through a file and never
 * through a command line. Write the result as UTF-8.
 */
object Ffmetadata {
    /** Characters that need a backslash in front: the syntax characters and the line breaks (which stay in place). */
    private val ESCAPED = setOf('=', ';', '#', '\\', '\n', '\r')

    fun render(tags: TrackTags): String = buildString {
        append(";FFMETADATA1\n")
        entry("title", tags.title)
        entry("artist", tags.artist)
        entry("album_artist", tags.albumArtist)
        tags.album?.takeIf { it.isNotBlank() }?.let { entry("album", it) }
        entry("track", tags.trackNumber.toString())
        tags.comment?.takeIf { it.isNotBlank() }?.let { entry("comment", it) }
    }

    private fun StringBuilder.entry(key: String, value: String) {
        append(key).append('=')
        for (c in value) {
            if (c in ESCAPED) append('\\')
            append(c)
        }
        append('\n')
    }
}

package com.xgetsongs.engine.tags

/**
 * Renders [TrackTags] as an ffmetadata file (`-f ffmetadata`), so tag text reaches ffmpeg through a file and never
 * through a command line. Write the result as UTF-8.
 *
 * The MP4 muxer stores `comment` as `©cmt` and `lyrics` as `©lyr`, so every value is rendered. A null or blank album,
 * comment or lyrics text writes no entry.
 */
object Ffmetadata {
    /** Characters that need a backslash in front: the syntax characters and the line breaks (which stay in place). */
    private val ESCAPED = setOf('=', ';', '#', '\\', '\n', '\r')

    /** U+FF3C, the look-alike that replaces a trailing backslash, like file names do for forbidden characters. */
    private const val FULLWIDTH_BACKSLASH = '＼'

    fun render(tags: TrackTags): String = buildString {
        append(";FFMETADATA1\n")
        entry("title", tags.title)
        entry("artist", tags.artist)
        entry("album_artist", tags.albumArtist)
        tags.album?.takeIf { it.isNotBlank() }?.let { entry("album", it) }
        entry("track", tags.trackNumber.toString())
        tags.comment?.takeIf { it.isNotBlank() }?.let { entry("comment", it) }
        tags.lyrics?.takeIf { it.isNotBlank() }?.let { entry("lyrics", it) }
    }

    private fun StringBuilder.entry(key: String, value: String) {
        append(key).append('=')
        for (c in sanitize(value)) {
            if (c in ESCAPED) append('\\')
            append(c)
        }
        append('\n')
    }

    /**
     * Two things in a value would corrupt the file. A NUL (or any other control character except tab and the line
     * breaks, which are escaped) can end a line and so inject another tag: those are dropped. And ffmpeg joins a line
     * that ends in a backslash with the next one, even when that backslash is escaped, so the next tag would vanish
     * into this value: a trailing backslash becomes a full-width one.
     */
    private fun sanitize(value: String): String {
        val printable = value.filter { it >= ' ' || it == '\t' || it == '\n' || it == '\r' }
        val kept = printable.trimEnd('\\')
        return kept + FULLWIDTH_BACKSLASH.toString().repeat(printable.length - kept.length)
    }
}

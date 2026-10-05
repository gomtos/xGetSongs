package com.xgetsongs.shared.filename

/**
 * Builds `{rank 3 digits} {artist} - {title}.mp3` file names and playlist folder names
 * that are safe on Windows.
 */
object FilenameFormatter {
    const val MIN_RANK = 1
    const val MAX_RANK = 999

    /** Limit for the file name without extension. */
    const val MAX_BASE_LENGTH = 180
    const val MAX_ARTIST_LENGTH = 80

    /** Limit for a playlist folder name, before a reserved-name `_` is added. */
    const val MAX_FOLDER_LENGTH = 80

    /** Folder name used when a playlist has no usable title. */
    const val UNTITLED_PLAYLIST = "재생목록"
    private const val ELLIPSIS = "…"
    private const val EXTENSION = ".mp3"
    private const val EMPTY_TITLE_PLACEHOLDER = "untitled"

    private val FULLWIDTH = mapOf(
        '\\' to '＼', '/' to '／', ':' to '：', '*' to '＊', '?' to '？',
        '"' to '＂', '<' to '＜', '>' to '＞', '|' to '｜',
    )

    /** Windows device names that cannot be used as a file or folder name, with or without an extension. */
    private val RESERVED_DEVICE_NAMES = setOf("CON", "PRN", "AUX", "NUL") +
        (1..9).flatMap { listOf("COM$it", "LPT$it") }

    fun format(rank: Int, artist: String, title: String): String {
        require(rank in MIN_RANK..MAX_RANK) { "rank must be in $MIN_RANK..$MAX_RANK but was $rank" }
        val cleanArtist = truncate(sanitize(artist), MAX_ARTIST_LENGTH)
        val prefix = "${rank.toString().padStart(3, '0')} $cleanArtist - "
        val cleanTitle = sanitize(title).ifEmpty { EMPTY_TITLE_PLACEHOLDER }
        val titleBudget = MAX_BASE_LENGTH - prefix.length
        val base = (prefix + truncate(cleanTitle, titleBudget)).trimEnd('.', ' ')
        return base + EXTENSION
    }

    /**
     * Builds the folder name for a playlist titled [title]: [sanitize]d, cut to [MAX_FOLDER_LENGTH],
     * without trailing dots or spaces, and with a `_` after a reserved Windows device name
     * (`CON` becomes `CON_`, `con.txt` becomes `con_.txt`). Falls back to [UNTITLED_PLAYLIST].
     */
    fun folderName(title: String?): String {
        if (title.isNullOrBlank()) return UNTITLED_PLAYLIST
        val name = truncate(sanitize(title), MAX_FOLDER_LENGTH).trimEnd('.', ' ').trimStart(' ')
        if (name.isEmpty()) return UNTITLED_PLAYLIST
        val stem = name.substringBefore('.').trimEnd(' ')
        return if (stem.uppercase() in RESERVED_DEVICE_NAMES) stem + "_" + name.substring(stem.length) else name
    }

    /** Replaces forbidden characters with full-width look-alikes and drops control characters. */
    fun sanitize(text: String): String {
        val sb = StringBuilder(text.length)
        for (c in text) {
            when {
                c.code < 0x20 || c.code == 0x7F -> Unit
                else -> sb.append(FULLWIDTH[c] ?: c)
            }
        }
        return sb.toString().trim()
    }

    /** Cuts [text] to at most [max] UTF-16 units, ending with an ellipsis, never splitting a surrogate pair. */
    private fun truncate(text: String, max: Int): String {
        if (text.length <= max) return text
        var end = max - ELLIPSIS.length
        if (end > 0 && text[end - 1].isHighSurrogate()) end--
        return text.substring(0, end.coerceAtLeast(0)).trimEnd() + ELLIPSIS
    }
}

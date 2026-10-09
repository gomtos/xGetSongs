package com.xgetsongs.shared.filename

/**
 * Builds `{rank 3 digits} {artist} - {title}.mp3` file names (the rank is optional) and playlist folder names
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

    /**
     * Builds `{rank 3 digits} {artist} - {title}.mp3`, or `{artist} - {title}.mp3` when [includeRank] is false. The length
     * limits are the same either way ([MAX_BASE_LENGTH] for the whole name, so the title gets what the prefix leaves).
     * [rank] must be in [MIN_RANK]..[MAX_RANK] even when it is not printed. Without the rank the artist starts the name,
     * so an artist whose text before the first `.` is a reserved Windows device name gets a `_` after it (`NUL.x` becomes
     * `NUL_.x`; plain `Con - Song.mp3` is fine because the name's stem is `Con - Song`); the digits of the rank already
     * rule that out when it is printed.
     */
    fun format(rank: Int, artist: String, title: String, includeRank: Boolean = true): String {
        require(rank in MIN_RANK..MAX_RANK) { "rank must be in $MIN_RANK..$MAX_RANK but was $rank" }
        val cutArtist = truncate(sanitize(artist), MAX_ARTIST_LENGTH)
        val cleanArtist = if (includeRank || '.' !in cutArtist) cutArtist else escapeDeviceName(cutArtist)
        val rankPrefix = if (includeRank) "${rank.toString().padStart(3, '0')} " else ""
        val prefix = "$rankPrefix$cleanArtist - "
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
        return escapeDeviceName(name)
    }

    /**
     * The folder inside the output folder that a job saves into, or null for the output folder itself. A [typed] name
     * that is not blank wins, for a playlist and for a single video alike; otherwise a playlist gets a folder named
     * after [playlistTitle] and a single video gets none. The name is made safe by [folderName] either way.
     */
    fun destinationFolder(isPlaylist: Boolean, playlistTitle: String?, typed: String?): String? = when {
        !typed.isNullOrBlank() -> folderName(typed)
        isPlaylist -> folderName(playlistTitle)
        else -> null
    }

    /**
     * Windows treats the text before the first `.` of a name (trailing spaces ignored) as a device when it is one of
     * the [RESERVED_DEVICE_NAMES], whatever the case. Puts a `_` right after that stem then; other names are returned as is.
     */
    private fun escapeDeviceName(name: String): String {
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

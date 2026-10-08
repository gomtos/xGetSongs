package com.xgetsongs.shared.log

/**
 * Keeps file names and paths out of the log files. Messages that come out of the file system or out of yt-dlp name the
 * files they were working on (`NoSuchFileException: C:\Users\...\001 Artist - Title.mp3`), and a song's file name is its
 * title: neither belongs in a log the user may send to someone.
 */
object LogRedaction {
    const val PATH_PLACEHOLDER = "<경로>"
    const val FILE_NAME_PLACEHOLDER = "<파일명>"

    /** Names shorter than this are not replaced: they would hit unrelated text. */
    private const val MIN_NAME_LENGTH = 3

    // A part of a path: any characters Windows allows in a name (spaces included), up to the next separator or one of the
    // characters it forbids. It also stops in front of ` ->`, so that the two paths of `src -> dst` are two matches, and at
    // a colon, so that the reason after the last path (`...\a.mp3: The process cannot access the file...`) stays readable.
    private const val PART = """(?:(?! ->)[^\\/:*?"<>|\r\n])+"""

    // A drive (`C:`) or a UNC host (`\\host`), perhaps behind `\\?\`, followed by one or more `\part`. Python's repr of a
    // path, as yt-dlp prints it, doubles every backslash: one or two are a separator.
    private val windowsPath = Regex("""(?:\\\\[?.]\\)?(?:[A-Za-z]:|\\\\$PART)(?:\\{1,2}$PART)+""")

    /** Replaces every Windows path in [text] by [PATH_PLACEHOLDER]. A path ends at the end of its line. */
    fun redactPaths(text: String): String = windowsPath.replace(text, PATH_PLACEHOLDER)

    /**
     * [redactPaths], then each of [knownNames] (a file name the caller knows is in the text, such as the one an item was
     * started with) and the same name without its extension, wherever it appears and whatever the case, by
     * [FILE_NAME_PLACEHOLDER]. Paths go first so that a known name inside a path leaves no half of the path behind.
     */
    fun redact(text: String, knownNames: Collection<String>): String {
        var result = redactPaths(text)
        val names = knownNames
            .flatMap { listOf(it, it.substringBeforeLast('.')) }
            .filter { it.isNotBlank() && it.length >= MIN_NAME_LENGTH }
            .distinct()
            .sortedByDescending { it.length }
        for (name in names) result = result.replace(name, FILE_NAME_PLACEHOLDER, ignoreCase = true)
        return result
    }
}

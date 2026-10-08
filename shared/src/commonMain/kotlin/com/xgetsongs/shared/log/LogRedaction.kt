package com.xgetsongs.shared.log

/**
 * Keeps file names and paths out of the log files. Messages that come out of the file system or out of yt-dlp name the
 * files they were working on (`NoSuchFileException: C:\Users\...\001 Artist - Title.mp3`), and a song's file name is its
 * title: neither belongs in a log the user may send to someone.
 *
 * The paths are found by a small scanner, not by a regular expression: a pattern for "a path with spaces in its parts" has
 * to repeat a group that repeats a character class, and the JVM's matcher answers a path of a few thousand parts with a
 * stack overflow. The scanner looks at every character at most a few times and uses no recursion.
 */
object LogRedaction {
    const val PATH_PLACEHOLDER = "<경로>"
    const val FILE_NAME_PLACEHOLDER = "<파일명>"

    /** Longer text is cut to this many characters (and ends with `…`) before it is looked at: a log line has no use for more. */
    const val MAX_TEXT_LENGTH = 4_000

    /** Names shorter than this are not replaced: they would hit unrelated text. */
    private const val MIN_NAME_LENGTH = 3

    // The characters Windows does not allow in a file name, and the line breaks, end a part of a path.
    private const val FORBIDDEN_IN_PART = "\\/:*?\"<>|\r\n"

    // What a path leaves at its end when it is followed by a list: `C:\a\b.mp3, D:\c\d.mp3`. File names cannot end in a space.
    private const val LIST_PUNCTUATION = " ,;"

    /**
     * Replaces every Windows path in [text] by [PATH_PLACEHOLDER], and then a text over [MAX_TEXT_LENGTH] is cut. A path
     * is one of
     *  - a drive (`C:`) followed by `\` or `/` and one or more parts, parts being separated by one or two `\` or `/` (a
     *    Python error message doubles every backslash), also behind `\\?\` or `\\.\` and as a `file:///C:/...` URI;
     *  - a UNC path: `\\host` followed by the parts, also as `\\?\UNC\host`.
     *
     * A part is anything up to the next separator that is allowed in a name (spaces too, so a part runs on to the end of
     * the sentence it is in, up to a `:` or a line end: over-redaction is the safe side); it also ends in front of ` ->`
     * (`src -> dst` is two paths), in front of another drive (`C:\a, D:\b` is two paths) and at a colon, so that a reason
     * after the last path (`...\a.mp3: The process cannot access the file`) stays readable. A path ends at the end of its
     * line. Paths without a drive or a host (`Music\a.mp3`, `..\x`, `/usr/lib`) cannot be told from other text and are left.
     */
    fun redactPaths(text: String): String {
        val bounded = bound(text)
        val out = StringBuilder(bounded.length)
        var i = 0
        var lastEnd = -1
        while (i < bounded.length) {
            val end = pathEnd(bounded, i, mayFollowLetter = i == lastEnd)
            if (end < 0) {
                out.append(bounded[i])
                i++
                continue
            }
            var keep = end
            while (keep > i && bounded[keep - 1] in LIST_PUNCTUATION) keep--
            out.append(PATH_PLACEHOLDER).append(bounded.substring(keep, end))
            i = end
            lastEnd = end
        }
        return out.toString()
    }

    /**
     * [redactPaths], then each of [knownNames] (a file name the caller knows is in the text, such as the one an item was
     * started with) and the same name without its extension, wherever it appears and whatever the case, by
     * [FILE_NAME_PLACEHOLDER]. The names are plain text, not patterns. Paths go first so that a known name inside a path
     * leaves no half of the path behind.
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

    private fun bound(text: String): String {
        if (text.length <= MAX_TEXT_LENGTH) return text
        // Never end on the first half of a surrogate pair.
        val end = if (text[MAX_TEXT_LENGTH - 1].isHighSurrogate()) MAX_TEXT_LENGTH - 1 else MAX_TEXT_LENGTH
        return text.substring(0, end) + "…"
    }

    private fun isAsciiLetter(c: Char) = c in 'A'..'Z' || c in 'a'..'z'

    private fun isSeparator(c: Char) = c == '\\' || c == '/'

    /** `C:` followed by a separator, starting at [p]. */
    private fun isDriveStart(t: String, p: Int) = p + 2 < t.length && isAsciiLetter(t[p]) && t[p + 1] == ':' && isSeparator(t[p + 2])

    /** The index after the path that starts at [i], or -1 when no path starts there. */
    private fun pathEnd(t: String, i: Int, mayFollowLetter: Boolean): Int {
        val first = t[i]
        val afterPrefix: Int
        if ((first == 'f' || first == 'F') && t.regionMatches(i, "file:///", 0, 8, ignoreCase = true)) {
            if (!isDriveStart(t, i + 8)) return -1
            afterPrefix = i + 10
        } else if (isAsciiLetter(first)) {
            if (!isDriveStart(t, i)) return -1
            // `https://...` is not a drive: a letter that belongs to a word is no drive (unless a path just ended in it).
            if (i > 0 && !mayFollowLetter && (isAsciiLetter(t[i - 1]) || t[i - 1] in '0'..'9')) return -1
            afterPrefix = i + 2
        } else if (first == '\\' && t.startsWith("\\\\", i)) {
            afterPrefix = uncPrefixEnd(t, i + 2)
            if (afterPrefix < 0) return -1
        } else {
            return -1
        }
        return partsEnd(t, afterPrefix)
    }

    /** After `\\`: `?\` or `.\` and then a drive or `UNC\host`, or a host. Returns the index where the parts start, or -1. */
    private fun uncPrefixEnd(t: String, p: Int): Int {
        if (p + 1 < t.length && (t[p] == '?' || t[p] == '.') && t[p + 1] == '\\') {
            val q = p + 2
            return when {
                t.regionMatches(q, "UNC\\", 0, 4, ignoreCase = true) -> hostEnd(t, q + 4)
                isDriveStart(t, q) -> q + 2
                else -> -1
            }
        }
        return hostEnd(t, p)
    }

    private fun hostEnd(t: String, p: Int): Int {
        val end = partEnd(t, p)
        return if (end > p) end else -1
    }

    /** The end of the last of the `\part` pieces that follow [start]; -1 when there is none. */
    private fun partsEnd(t: String, start: Int): Int {
        var p = start
        var end = -1
        while (p < t.length && isSeparator(t[p])) {
            var q = p + 1
            if (q < t.length && isSeparator(t[q])) q++
            val e = partEnd(t, q)
            if (e == q) break
            end = e
            p = e
        }
        return end
    }

    /** The end of the part that starts at [p] (the index of its first character when it is empty). */
    private fun partEnd(t: String, p: Int): Int {
        var q = p
        while (q < t.length) {
            val c = t[q]
            if (FORBIDDEN_IN_PART.indexOf(c) >= 0) break
            if (c == ' ' && t.startsWith(" ->", q)) break
            if (isAsciiLetter(c) && isDriveStart(t, q)) break
            q++
        }
        return q
    }
}

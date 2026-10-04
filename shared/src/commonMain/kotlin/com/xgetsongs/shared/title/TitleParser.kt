package com.xgetsongs.shared.title

enum class Confidence { HIGH, MEDIUM, LOW }

data class ParsedTrack(val artist: String, val title: String, val confidence: Confidence)

/**
 * Splits a YouTube video title into artist and song title.
 *
 * Order: title patterns (`Artist - Title`, `Artist 'Title'`) -> yt-dlp `artist`/`track` metadata ->
 * channel name. Text is kept as written; only quotes around the title and noise such as
 * `Official MV` are removed. `Title - Artist` ordering is not supported.
 */
object TitleParser {
    const val UNKNOWN_ARTIST = "Unknown Artist"
    private const val TOPIC_SUFFIX = " - Topic"
    private const val VEVO_SUFFIX = "vevo"

    private val LEADING_TAGS = setOf(
        "mv", "m/v", "official mv", "official m/v", "official video", "official audio",
        "official music video", "music video", "audio", "lyric video", "performance video",
        "가사", "뮤직비디오",
    )

    /** Longest first so "Official Music Video" is removed as a whole, not just "Music Video". */
    private val TRAILING_NOISE = listOf(
        "Official Music Video", "Official Video", "Official Audio", "Official MV", "Official M/V",
        "Performance Video", "Special Video", "Special Clip", "Lyric Video", "Music Video",
        "Live Clip", "Visualizer", "Audio", "M/V", "MV",
    ).map { it.lowercase() }.sortedByDescending { it.length }

    private val SEPARATORS = listOf(" - ", " – ", " — ", "_ ")
    private val SINGLE_QUOTES = charArrayOf('\'', '‘', '’')
    private val DOUBLE_QUOTES = charArrayOf('"', '“', '”')
    private val ALL_QUOTES = SINGLE_QUOTES + DOUBLE_QUOTES

    fun parse(
        rawTitle: String,
        channel: String? = null,
        metaArtist: String? = null,
        metaTrack: String? = null,
    ): ParsedTrack {
        val text = clean(rawTitle)

        // Auto-generated "Artist - Topic" channels carry the bare track name, so " - " in the
        // title belongs to the track and must not be treated as an artist separator.
        val isTopicChannel = channel?.trim()?.endsWith(TOPIC_SUFFIX, ignoreCase = true) == true
        if (!isTopicChannel) {
            split(text)?.let { (artist, title) -> return ParsedTrack(artist, title, Confidence.HIGH) }
        }

        val title = unwrapQuotes(text).ifEmpty { rawTitle.trim() }
        val artist = metaArtist?.trim().orEmpty()
        if (artist.isNotEmpty()) {
            val track = metaTrack?.trim().orEmpty().ifEmpty { title }
            return ParsedTrack(artist, track, Confidence.MEDIUM)
        }
        return ParsedTrack(artistFromChannel(channel), title, Confidence.LOW)
    }

    private fun artistFromChannel(channel: String?): String {
        var name = channel?.trim().orEmpty()
        if (name.endsWith(TOPIC_SUFFIX, ignoreCase = true)) name = name.dropLast(TOPIC_SUFFIX.length)
        if (name.length > VEVO_SUFFIX.length && name.endsWith(VEVO_SUFFIX, ignoreCase = true)) {
            name = name.dropLast(VEVO_SUFFIX.length)
        }
        return name.trim().ifEmpty { UNKNOWN_ARTIST }
    }

    // ---- cleaning -------------------------------------------------------------------------

    private fun clean(raw: String): String {
        var s = collapseWhitespace(raw)
        while (true) {
            val next = collapseWhitespace(
                stripTrailingNoiseToken(
                    stripTrailingNoiseParen(
                        stripTrailingBracketBlock(cutAtPipe(stripLeadingTag(stripLeadingJunk(s)))),
                    ),
                ),
            )
            if (next == s) return s
            s = next
        }
    }

    private fun collapseWhitespace(s: String): String {
        val sb = StringBuilder(s.length)
        var previousWasSpace = false
        for (c in s) {
            if (c.isWhitespace()) {
                if (!previousWasSpace) sb.append(' ')
                previousWasSpace = true
            } else {
                sb.append(c)
                previousWasSpace = false
            }
        }
        return sb.toString().trim()
    }

    /** Drops leading emoji and symbols; stops at a letter, digit, bracket or quote. */
    private fun stripLeadingJunk(s: String): String {
        val i = s.indexOfFirst { it.isLetterOrDigit() || it == '[' || it == '(' || it in ALL_QUOTES }
        return if (i <= 0) s else s.substring(i)
    }

    private fun stripLeadingTag(s: String): String {
        if (!s.startsWith("[")) return s
        val end = s.indexOf(']')
        if (end < 0) return s
        if (s.substring(1, end).trim().lowercase() !in LEADING_TAGS) return s
        return s.substring(end + 1).trim().ifEmpty { s }
    }

    private fun cutAtPipe(s: String): String {
        val i = s.indexOf(" | ")
        return if (i <= 0) s else s.substring(0, i).trim()
    }

    private fun stripTrailingBracketBlock(s: String): String {
        if (!s.endsWith("]")) return s
        val start = s.lastIndexOf('[')
        if (start <= 0) return s
        return s.substring(0, start).trim().ifEmpty { s }
    }

    private fun stripTrailingNoiseParen(s: String): String {
        if (!s.endsWith(")")) return s
        val start = s.lastIndexOf('(')
        if (start <= 0) return s
        val inner = s.substring(start + 1, s.length - 1).trim().lowercase()
        return if (inner in TRAILING_NOISE) s.substring(0, start).trim().ifEmpty { s } else s
    }

    private fun stripTrailingNoiseToken(s: String): String {
        for (token in TRAILING_NOISE) {
            if (s.length > token.length &&
                s.endsWith(token, ignoreCase = true) &&
                s[s.length - token.length - 1].isWhitespace()
            ) {
                return s.substring(0, s.length - token.length)
                    .trimEnd { it.isWhitespace() || it == '-' || it == '–' || it == '—' }
                    .ifEmpty { s }
            }
        }
        return s
    }

    // ---- splitting ------------------------------------------------------------------------

    /** Splits at whichever comes first: a separator (`Artist - Title`) or an opening quote (`Artist 'Title'`). */
    private fun split(text: String): Pair<String, String>? {
        val separator = findSeparator(text)
        val quote = findQuotedSpan(text, 0)
        val result = when {
            separator != null && (quote == null || separator.first < quote.first) ->
                text.substring(0, separator.first).trim() to
                    unwrapQuotes(text.substring(separator.first + separator.second).trim())
            quote != null && quote.first > 0 ->
                text.substring(0, quote.first).trim() to text.substring(quote.first + 1, quote.second).trim()
            else -> null
        }
        return result?.takeIf { it.first.isNotEmpty() && it.second.isNotEmpty() }
    }

    /** Returns (index, length) of the earliest separator, ignoring position 0. */
    private fun findSeparator(s: String): Pair<Int, Int>? {
        var best: Pair<Int, Int>? = null
        for (separator in SEPARATORS) {
            val i = s.indexOf(separator, startIndex = 1)
            if (i > 0 && (best == null || i < best.first)) best = i to separator.length
        }
        return best
    }

    private fun quoteFamily(c: Char): CharArray? = when (c) {
        in SINGLE_QUOTES -> SINGLE_QUOTES
        in DOUBLE_QUOTES -> DOUBLE_QUOTES
        else -> null
    }

    /**
     * Finds the first quoted span at or after [from] as (openIndex, closeIndex). An opening quote must
     * follow whitespace or the start of the text, which skips apostrophes inside words such as
     * "Girls'". A closing quote must not be followed by a letter or digit, which skips "Don't".
     */
    private fun findQuotedSpan(s: String, from: Int): Pair<Int, Int>? {
        for (i in from until s.length) {
            val family = quoteFamily(s[i]) ?: continue
            if (i > 0 && !s[i - 1].isWhitespace()) continue
            val close = findCloser(s, i, family) ?: continue
            return i to close
        }
        return null
    }

    private fun findCloser(s: String, open: Int, family: CharArray): Int? {
        for (j in open + 2 until s.length) {
            if (s[j] in family && (j == s.length - 1 || !s[j + 1].isLetterOrDigit())) return j
        }
        return null
    }

    private fun unwrapQuotes(s: String): String {
        val span = findQuotedSpan(s, 0)
        return if (span != null && span.first == 0) s.substring(1, span.second).trim() else s
    }
}

package com.xgetsongs.engine.lyrics

import kotlin.math.abs

/**
 * A record of the lyrics service, reduced to what [LyricsMatcher] looks at. Every field can be missing in the answer of a
 * service, so every field may be null. [duration] is in seconds and may be fractional.
 */
data class LyricsCandidate(
    val trackName: String?,
    val artistName: String?,
    val albumName: String?,
    val duration: Double?,
    val instrumental: Boolean?,
    val plainLyrics: String?,
)

/**
 * Decides whether a record of the lyrics service is the song that was asked for. A service answers a search with
 * everything that looks a little like it (covers, remixes, other songs of the same name), and the lyrics of the wrong
 * song are worse than none, so a record must agree on title, artist and length. Pure text and number comparisons: no
 * network, no clock.
 */
object LyricsMatcher {
    /** Two lengths further apart than this (seconds) are two versions of a song, not one. */
    private const val MAX_DURATION_DIFFERENCE = 8.0

    /** After normalising, an artist this short or shorter says too little to match on. */
    private const val MIN_ARTIST_LENGTH = 2

    private const val MAX_ARTIST_VARIANTS = 3

    private val BRACKETED = Regex("""\([^()]*\)|\[[^\[\]]*\]|\{[^{}]*\}|<[^<>]*>""")
    private val PARENTHESISED = Regex("""\(([^()]*)\)""")
    private val CREDIT = Regex(
        """\s+(?:feat\.?|ft\.?|with|prod\.?|narr\.?)\s+.*$""",
        setOf(RegexOption.IGNORE_CASE, RegexOption.DOT_MATCHES_ALL),
    )
    private val WHITESPACE = Regex("""\s+""")

    /** The straight quotes, the typographic single and double quotes, and the low and reversed forms of them. */
    private const val QUOTES = "'\"\u2018\u2019\u201A\u201B\u201C\u201D\u201E\u201F"

    /**
     * The spellings of [title] to look for, most specific first, equal ones listed once and blank ones never:
     *  1. the title itself (trimmed);
     *  2. the title without its bracketed parts (`(...)`, `[...]`, `{...}`, `<...>`, nested ones too), spaces collapsed;
     *  3. that without a trailing credit (` feat. X`, ` ft. X`, ` with X`, ` prod. X`, ` Narr. X`, any case, up to the end);
     *  4. that without quote characters.
     */
    fun titleVariants(title: String): List<String> {
        val whole = title.trim()
        val withoutBrackets = collapse(withoutBrackets(whole))
        val withoutCredit = collapse(CREDIT.replace(withoutBrackets, ""))
        val withoutQuotes = collapse(withoutCredit.filterNot { it in QUOTES })
        return listOf(whole, withoutBrackets, withoutCredit, withoutQuotes).filter { it.isNotBlank() }.distinct()
    }

    /**
     * The spellings of [artist] to look for: the artist itself, the text outside its parentheses and each parenthesised
     * part, so `소연 (SOYEON)` gives `소연 (SOYEON)`, `소연` and `SOYEON`. Equal ones are listed once, blank ones never, and
     * at most three are given (in that order).
     */
    fun artistVariants(artist: String): List<String> {
        val whole = artist.trim()
        var outside = whole
        val parts = mutableListOf<String>()
        while (true) {
            val found = PARENTHESISED.findAll(outside).toList()
            if (found.isEmpty()) break
            found.mapTo(parts) { it.groupValues[1].trim() }
            outside = PARENTHESISED.replace(outside, " ")
        }
        return (listOf(whole, collapse(outside)) + parts).filter { it.isNotBlank() }.distinct().take(MAX_ARTIST_VARIANTS)
    }

    /** [text] in lowercase with everything but letters and digits (of any script, Hangul included) taken out. */
    fun normalize(text: String): String = buildString {
        text.lowercase().codePoints().forEach { if (Character.isLetterOrDigit(it)) appendCodePoint(it) }
    }

    /**
     * True when [candidate] can be the song of [query]: it has lyrics (not blank) and is not marked instrumental; some
     * title variant of the query equals, after [normalize], some title variant of the candidate's track name; some artist
     * variant of the query contains, or is contained in, some artist variant of the candidate's artist name (both of at
     * least two characters once normalised); and, when both lengths are known, they differ by at most eight seconds.
     */
    fun isMatch(query: LyricsQuery, candidate: LyricsCandidate): Boolean {
        if (candidate.plainLyrics.isNullOrBlank() || candidate.instrumental == true) return false
        val trackName = candidate.trackName ?: return false
        val artistName = candidate.artistName ?: return false
        if (!sameTitle(query.title, trackName) || !sameArtist(query.artist, artistName)) return false
        val difference = durationDifference(query, candidate)
        return difference == null || difference <= MAX_DURATION_DIFFERENCE
    }

    /**
     * The acceptable one ([isMatch]) of [candidates] whose length is closest to the one asked for; a candidate or a query
     * without a length counts as the farthest, and of equal ones the first wins. Null when none is acceptable.
     */
    fun pick(query: LyricsQuery, candidates: List<LyricsCandidate>): LyricsCandidate? =
        candidates.filter { isMatch(query, it) }.minByOrNull { durationDifference(query, it) ?: Double.MAX_VALUE }

    private fun sameTitle(wanted: String, found: String): Boolean {
        val foundKeys = titleVariants(found).map(::normalize).filter { it.isNotEmpty() }.toSet()
        return titleVariants(wanted).map(::normalize).any { it.isNotEmpty() && it in foundKeys }
    }

    private fun sameArtist(wanted: String, found: String): Boolean {
        val foundKeys = artistVariants(found).map(::normalize).filter { it.length >= MIN_ARTIST_LENGTH }
        return artistVariants(wanted).map(::normalize).filter { it.length >= MIN_ARTIST_LENGTH }.any { key ->
            foundKeys.any { it.contains(key) || key.contains(it) }
        }
    }

    /** The seconds between the length asked for and the candidate's; null when either is not known. */
    private fun durationDifference(query: LyricsQuery, candidate: LyricsCandidate): Double? {
        val wanted = query.durationSeconds ?: return null
        val found = candidate.duration ?: return null
        return abs(wanted - found)
    }

    /** [text] with every bracketed part replaced by a space, until no bracket pair is left (so nested pairs go too). */
    private fun withoutBrackets(text: String): String {
        var current = text
        while (true) {
            val next = BRACKETED.replace(current, " ")
            if (next == current) return next
            current = next
        }
    }

    /** Runs of whitespace become one space, and the ends are trimmed. */
    private fun collapse(text: String): String = text.replace(WHITESPACE, " ").trim()
}

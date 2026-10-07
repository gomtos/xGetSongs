package com.xgetsongs.engine.lyrics

import java.text.Normalizer
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
 * Decides whether a record of the lyrics service is the song that was asked for, and which of several is the best. A
 * service answers a search with everything that looks a little like it (covers, remixes, other songs of the same name,
 * versions in another language), and the lyrics of the wrong song are worse than none, so a record must agree on title,
 * artist and length. Pure text and number comparisons: no network, no clock.
 */
object LyricsMatcher {
    /** Two lengths further apart than this (seconds) are two versions of a song, not one. */
    private const val MAX_DURATION_DIFFERENCE = 8.0

    /** After normalising, an artist this short or shorter says too little to match on. */
    private const val MIN_ARTIST_LENGTH = 2

    private const val MAX_ARTIST_VARIANTS = 3

    private val BRACKETED = Regex("""\([^()]*\)|\[[^\[\]]*\]|\{[^{}]*\}|<[^<>]*>""")
    private val PARENTHESISED = Regex("""\(([^()]*)\)""")

    /**
     * A trailing credit: whitespace, `feat.`, `ft.`, `prod.` or `Narr.` (the dot is optional), whitespace and the rest.
     * `with` is not one: `Stay With Me` is a title, not the song `Stay` with a credit (a credit written `(with X)` is a
     * bracketed part and goes with the brackets).
     */
    private val CREDIT = Regex(
        """\s+(?:feat\.?|ft\.?|prod\.?|narr\.?)\s+.*$""",
        setOf(RegexOption.IGNORE_CASE, RegexOption.DOT_MATCHES_ALL),
    )
    private val WHITESPACE = Regex("""\s+""")

    /** The straight quotes, the typographic single and double quotes, and the low and reversed forms of them. */
    private const val QUOTES = "'\"\u2018\u2019\u201A\u201B\u201C\u201D\u201E\u201F"

    /**
     * Words that mark another version of a song (another language, an instrumental, a remix, a live or acoustic take...).
     * A word of a bracketed part or credit has to be one of these as a whole, with an optional plural or past-tense ending
     * and an optional number (`Remixes`, `Remastered`, `Ver2`); `Verse`, `Lively` or `Discover` are not.
     */
    private val VERSION_WORD = Regex(
        "(?:ver|version|japanese|english|chinese|korean|inst|instrumental|remix|live|acoustic|cover|edit|mix|remaster|demo)(?:ed|es|s)?[0-9]*",
    )

    /** Korean version words: Korean text has no spaces to cut it into words, so these count anywhere inside. */
    private val VERSION_KOREAN = listOf(
        "반주", "일본어", "영어", "중국어", "한국어", "라이브", "리믹스", "어쿠스틱", "버전", "커버", "데모", "리마스터", "믹스", "에디트", "인스트",
    )

    /**
     * The version words a bracketed part of the QUERY may hold without making its video another song: a live or acoustic
     * take has the lyrics of the song, a Japanese version, an instrumental or a remix has not.
     */
    private val LIVE_LIKE_WORD = Regex("(?:live|acoustic)(?:ed|es|s|d)?[0-9]*")
    private val LIVE_LIKE_KOREAN = listOf("라이브", "어쿠스틱")

    /** How well the titles agree; a lower number is a better match. */
    private const val TITLE_WHOLE = 0
    private const val TITLE_QUERY_SHORTENED = 1
    private const val TITLE_CANDIDATE_SHORTENED = 2

    /** A title and the steps that make it shorter, with the text each step took away. */
    private class Reduction(
        val whole: String,
        val withoutBrackets: String,
        val bracketedParts: String,
        val withoutCredit: String,
        val credit: String,
        val withoutQuotes: String,
    )

    /** The normalised title as it is, and the normalised shorter forms that are different and not blank. */
    private class TitleKeys(val whole: String, val shortened: List<String>)

    /**
     * The spellings of [title] to look for, most specific first, equal ones listed once and blank ones never:
     *  1. the title itself (trimmed);
     *  2. the title without its bracketed parts (`(...)`, `[...]`, `{...}`, `<...>`, nested ones too), spaces collapsed;
     *  3. that without a trailing credit (` feat. X`, ` ft. X`, ` prod. X`, ` Narr. X`, any case, up to the end);
     *  4. that without quote characters.
     */
    fun titleVariants(title: String): List<String> {
        val reduction = reduce(title)
        return listOf(reduction.whole, reduction.withoutBrackets, reduction.withoutCredit, reduction.withoutQuotes)
            .filter { it.isNotBlank() }
            .distinct()
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

    /**
     * [text] after compatibility normalisation (NFKC: full-width letters become ASCII, separate Hangul jamo become
     * syllables), in lowercase, with everything but letters and digits (of any script, Hangul included) taken out.
     */
    fun normalize(text: String): String = words(text).joinToString("")

    /**
     * True when [candidate] can be the song of [query]: it has lyrics (not blank) and is not marked instrumental; the
     * titles agree (see below); some artist variant of the query and some artist variant of the candidate are the same
     * letters and digits, or the words of one are a run of words of the other (`IU` is in `IU & Someone`, not in `Liu
     * Yifei`; `Rain` is not in `Rainbow`), both of at least two characters once normalised; and, when both lengths are
     * known, they differ by at most eight seconds.
     *
     * The titles agree when they are equal after [normalize], or when a shorter form of the query (without its bracketed
     * parts, its trailing credit) equals the candidate's title, or the other way round. A shorter form of a title is only
     * used when what it leaves out is no version marker, on either side: `Hello (Japanese Ver.)` is not `Hello` (a video
     * of the Japanese version must not get the lyrics of the original, nor the other way round), whereas `LOVE ATTACK
     * (LOVE ATTACK)` is `LOVE ATTACK`. The one exception: a video of a live or acoustic take has the lyrics of the song,
     * so the QUERY's own `(Live)` or `(Acoustic)` may be left out.
     */
    fun isMatch(query: LyricsQuery, candidate: LyricsCandidate): Boolean = rank(query, candidate) != null

    /**
     * The acceptable one ([isMatch]) of [candidates] that ranks best, or null when none is acceptable. They rank by, in
     * this order: how well the titles agree (equal as they are, then equal after shortening only the query's, then
     * equal after shortening the candidate's); whether the album equals the query's (when the query has one); the
     * closeness of the lengths (an unknown difference counts as the largest). Of equally good ones the first wins.
     */
    fun pick(query: LyricsQuery, candidates: List<LyricsCandidate>): LyricsCandidate? =
        candidates
            .mapNotNull { candidate -> rank(query, candidate)?.let { candidate to it } }
            .minWithOrNull(compareBy({ it.second.title }, { it.second.album }, { it.second.difference }))
            ?.first

    /** What [pick] sorts by. */
    private class Rank(val title: Int, val album: Int, val difference: Double)

    /** The [Rank] of [candidate] for [query], or null when it is not acceptable. */
    private fun rank(query: LyricsQuery, candidate: LyricsCandidate): Rank? {
        if (candidate.plainLyrics.isNullOrBlank() || candidate.instrumental == true) return null
        val trackName = candidate.trackName ?: return null
        val artistName = candidate.artistName ?: return null
        val title = titleRank(query.title, trackName) ?: return null
        if (!sameArtist(query.artist, artistName)) return null
        val difference = durationDifference(query, candidate)
        if (difference != null && difference > MAX_DURATION_DIFFERENCE) return null
        return Rank(title, if (sameAlbum(query.album, candidate.albumName)) 0 else 1, difference ?: Double.MAX_VALUE)
    }

    private fun titleRank(wanted: String, found: String): Int? {
        val query = titleKeys(wanted, forCandidate = false)
        val candidate = titleKeys(found, forCandidate = true)
        if (query.whole.isNotEmpty() && query.whole == candidate.whole) return TITLE_WHOLE
        if (candidate.whole.isNotEmpty() && candidate.whole in query.shortened) return TITLE_QUERY_SHORTENED
        val queryForms = listOf(query.whole) + query.shortened
        if (queryForms.any { it.isNotEmpty() && it in candidate.shortened }) return TITLE_CANDIDATE_SHORTENED
        return null
    }

    /**
     * The normalised forms of [title]. A shorter form is left out when a bracketed part or the credit it lacks holds a
     * version marker, so that the song and a version of it are not taken for each other, whichever side the version is
     * on. (The query's own `Live` and `Acoustic` do not count: see [hasVersionMarker].)
     */
    private fun titleKeys(title: String, forCandidate: Boolean): TitleKeys {
        val reduction = reduce(title)
        val whole = normalize(reduction.whole)
        val bracketsMayGo = !hasVersionMarker(reduction.bracketedParts, forCandidate)
        val creditMayGo = bracketsMayGo && !hasVersionMarker(reduction.credit, forCandidate)
        val shorter = buildList {
            if (bracketsMayGo) add(reduction.withoutBrackets)
            if (creditMayGo) {
                add(reduction.withoutCredit)
                add(reduction.withoutQuotes)
            }
        }
        return TitleKeys(whole, shorter.map(::normalize).filter { it.isNotEmpty() && it != whole }.distinct())
    }

    private fun reduce(title: String): Reduction {
        val whole = title.trim()
        val brackets = removeBrackets(whole)
        val withoutBrackets = collapse(brackets.first)
        val credit = CREDIT.find(withoutBrackets)?.value.orEmpty()
        val withoutCredit = collapse(CREDIT.replace(withoutBrackets, ""))
        val withoutQuotes = collapse(withoutCredit.filterNot { it in QUOTES })
        return Reduction(whole, withoutBrackets, brackets.second, withoutCredit, credit, withoutQuotes)
    }

    /**
     * True when [removed] (a bracketed part or a credit of a title) has a version marker word in it. For the QUERY
     * ([forCandidate] false) `live` and `acoustic` are no marker: a video of a live or acoustic take has the lyrics of the
     * song. For a record of the service they are, because the record of a live take is its own entry.
     */
    private fun hasVersionMarker(removed: String, forCandidate: Boolean): Boolean {
        if (removed.isBlank()) return false
        val text = Normalizer.normalize(removed, Normalizer.Form.NFKC).lowercase()
        val korean = VERSION_KOREAN.any { word -> word in text && (forCandidate || word !in LIVE_LIKE_KOREAN) }
        return korean || words(text).any { VERSION_WORD.matches(it) && (forCandidate || !LIVE_LIKE_WORD.matches(it)) }
    }

    /** The artist forms of both sides that are long enough; a match needs one of each to be the same name. */
    private fun sameArtist(wanted: String, found: String): Boolean {
        val foundForms = artistForms(found)
        return artistForms(wanted).any { query ->
            foundForms.any { candidate ->
                query.key == candidate.key || containsRun(query.words, candidate.words) || containsRun(candidate.words, query.words)
            }
        }
    }

    private class ArtistForm(val words: List<String>, val key: String)

    private fun artistForms(artist: String): List<ArtistForm> =
        artistVariants(artist).map { ArtistForm(words(it), normalize(it)) }.filter { it.key.length >= MIN_ARTIST_LENGTH }

    /** True when [needle] is not empty and its words are some consecutive words of [haystack]. */
    private fun containsRun(haystack: List<String>, needle: List<String>): Boolean =
        needle.isNotEmpty() && haystack.size >= needle.size &&
            (0..haystack.size - needle.size).any { start -> haystack.subList(start, start + needle.size) == needle }

    /** True when the query has an album (not blank) and [found] is the same album after [normalize]. */
    private fun sameAlbum(wanted: String?, found: String?): Boolean {
        val key = wanted?.let(::normalize).orEmpty()
        return key.isNotEmpty() && found != null && normalize(found) == key
    }

    /** The seconds between the length asked for and the candidate's; null when either is not known. */
    private fun durationDifference(query: LyricsQuery, candidate: LyricsCandidate): Double? {
        val wanted = query.durationSeconds ?: return null
        val found = candidate.duration ?: return null
        return abs(wanted - found)
    }

    /**
     * The words of [text] after NFKC and lowercasing: runs of letters and digits (of any script), everything else
     * separates.
     */
    private fun words(text: String): List<String> {
        val words = mutableListOf<String>()
        val current = StringBuilder()
        Normalizer.normalize(text, Normalizer.Form.NFKC).lowercase().codePoints().forEach { codePoint ->
            if (Character.isLetterOrDigit(codePoint)) {
                current.appendCodePoint(codePoint)
            } else if (current.isNotEmpty()) {
                words += current.toString()
                current.setLength(0)
            }
        }
        if (current.isNotEmpty()) words += current.toString()
        return words
    }

    /**
     * [text] with every bracketed part replaced by a space, until no bracket pair is left (so nested pairs go too), and the
     * text of the parts that were taken, separated by spaces.
     */
    private fun removeBrackets(text: String): Pair<String, String> {
        var current = text
        val taken = StringBuilder()
        while (true) {
            val parts = BRACKETED.findAll(current).toList()
            if (parts.isEmpty()) return current to taken.toString()
            parts.forEach { taken.append(it.value).append(' ') }
            current = BRACKETED.replace(current, " ")
        }
    }

    /** Runs of whitespace become one space, and the ends are trimmed. */
    private fun collapse(text: String): String = text.replace(WHITESPACE, " ").trim()
}

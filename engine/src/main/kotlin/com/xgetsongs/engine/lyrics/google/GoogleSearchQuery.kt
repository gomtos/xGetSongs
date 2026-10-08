package com.xgetsongs.engine.lyrics.google

import java.net.URLEncoder
import java.nio.charset.StandardCharsets
import java.text.Normalizer

/** One Google search: its words ([text]) and the language of the page ([language], `ko` or `en`). */
internal class GoogleSearchRequest(val text: String, val language: String) {
    /** The address of the result page. A space is `%20`, never `+`. */
    val url: String
        get() = "https://www.google.com/search?q=${URLEncoder.encode(text, StandardCharsets.UTF_8).replace("+", "%20")}&hl=$language"
}

/**
 * Turns the artist and title of a song into the searches to make. The words are `{artist} {title} lyrics`. The title
 * loses its quotes and its credits (`feat.`, `ft.`, `prod.`, `narr.`, as a tail or in brackets) but keeps a bracket that
 * names another version (`(Japanese Ver.)`): Google should look for that version, not for the song. The artist is
 * tried as written first and, only when it holds a parenthesised part, a second time without that part (`RESCENE
 * (리센느)` then `RESCENE`). The language is `ko` when the artist or the title has Hangul, else `en`.
 */
internal object GoogleSearchQuery {
    /** Double quotes and the typographic single and double quotes (the straight apostrophe is not among them). */
    private const val QUOTES = "\"‘’‚‛“”„‟"

    private val CREDIT_BRACKET = Regex("""[(\[]\s*(?:feat|ft|prod|narr)\b\.?[^()\[\]]*[)\]]""", RegexOption.IGNORE_CASE)
    private val TRAILING_CREDIT = Regex("""\s+(?:feat|ft|prod|narr)\b\.?\s+[^()\[\]]*""", RegexOption.IGNORE_CASE)
    private val PARENTHESISED = Regex("""\([^()]*\)""")
    private val APOSTROPHE = Regex("""(?<=\p{L})[‘’](?=\p{L})""")
    private val WHITESPACE = Regex("""\s+""")

    /** The searches for the song, best first; empty when the artist or the title is blank. */
    fun requests(artist: String, title: String): List<GoogleSearchRequest> {
        val fullArtist = collapse(artist)
        val cleanTitle = cleanTitle(title)
        if (fullArtist.isEmpty() || cleanTitle.isEmpty()) return emptyList()
        val language = if (hasHangul(artist) || hasHangul(title)) "ko" else "en"
        val artistWithoutParentheses = collapse(PARENTHESISED.replace(fullArtist, " "))
        return listOf(fullArtist, artistWithoutParentheses)
            .filter { it.isNotEmpty() }
            .distinct()
            .map { GoogleSearchRequest(collapse("$it $cleanTitle lyrics"), language) }
    }

    /** What two spellings of one song have in common: the words of the first search, NFKC and lowercase. */
    fun cacheKey(request: GoogleSearchRequest): String = Normalizer.normalize(request.text, Normalizer.Form.NFKC).lowercase()

    private fun cleanTitle(title: String): String {
        val cleaned = title
            .replace(APOSTROPHE, "'")
            .let { CREDIT_BRACKET.replace(it, " ") }
            .let { TRAILING_CREDIT.replace(it, " ") }
            .filterNot { it in QUOTES }
        return collapse(cleaned).ifEmpty { collapse(title) }
    }

    private fun collapse(text: String): String = text.replace(WHITESPACE, " ").trim()

    private fun hasHangul(text: String): Boolean =
        text.any { it in '가'..'힣' || it in 'ᄀ'..'ᇿ' || it in '㄰'..'㆏' }
}

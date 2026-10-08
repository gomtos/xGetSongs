package com.xgetsongs.engine.lyrics.google

import com.xgetsongs.shared.lyrics.LyricsExtractor
import org.jsoup.Jsoup
import org.jsoup.nodes.Element

/**
 * Reads the lyrics out of the HTML of a Google result page. The lines are the `span`s of [GoogleLyricsSelectors.LINE]
 * inside the card ([GoogleLyricsSelectors.CARD_SCOPES], the first one found; the whole page when there is none). Google
 * puts every paragraph in an element of its own and does not put blank lines in the text, so a paragraph is a run of
 * consecutive lines that share a parent element. Pure: no network, no browser.
 *
 * The answer is [GoogleLyricsResult.Found], [GoogleLyricsResult.NoCard] (no card and no lines at all) or
 * [GoogleLyricsResult.ExtractionFailed] (there is a card but no usable lines come out of it).
 */
internal object LyricsCardParser {
    fun parse(html: String): GoogleLyricsResult {
        val document = Jsoup.parse(html)
        val scope = GoogleLyricsSelectors.CARD_SCOPES.firstNotNullOfOrNull { document.selectFirst(it) }
        val lineElements = (scope ?: document).select(GoogleLyricsSelectors.LINE)
        if (lineElements.isEmpty()) {
            return if (scope == null) GoogleLyricsResult.NoCard else GoogleLyricsResult.ExtractionFailed
        }

        val paragraphs = mutableListOf<MutableList<String>>()
        var parent: Element? = null
        for (element in lineElements) {
            val line = element.text().trim()
            if (line.isEmpty()) continue
            if (paragraphs.isEmpty() || element.parent() !== parent) {
                paragraphs += mutableListOf<String>()
                parent = element.parent()
            }
            paragraphs.last() += line
        }

        val lyrics = LyricsExtractor.tidy(paragraphs.joinToString("\n\n") { it.joinToString("\n") })
            ?: return GoogleLyricsResult.ExtractionFailed
        return GoogleLyricsResult.Found(
            lyrics = lyrics,
            lineCount = lyrics.split('\n').count { it.isNotEmpty() },
            paragraphCount = lyrics.split("\n\n").count { it.isNotBlank() },
        )
    }
}

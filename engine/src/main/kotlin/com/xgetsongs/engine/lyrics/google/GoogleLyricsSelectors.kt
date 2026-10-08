package com.xgetsongs.engine.lyrics.google

/**
 * Everything that depends on how Google writes its result page, in one place. Google changes these obfuscated attributes
 * from time to time; when it does, this is the only file to touch (the smoke test `RealGoogleLyricsIntegrationTest`
 * is what notices).
 */
internal object GoogleLyricsSelectors {
    /** Where the lyrics card is, outermost first: the first one that matches limits the search for lines. */
    val CARD_SCOPES = listOf("div[data-lyricid]", "[jsname=WbKHeb]")

    /** One line of the lyrics. Searched inside the scope, or in the whole page when there is no scope. */
    const val LINE = "span[jsname=YS01Ge]"

    /** Matches a page that shows a lyrics card or the first sign of one; the browser stops waiting at it. */
    val CARD_PRESENCE: String = (CARD_SCOPES + LINE).joinToString(", ")

    /** The address of Google's block page ("we have detected unusual traffic"). */
    const val SORRY_PATH = "/sorry/"

    /** The host of Google's consent page. */
    const val CONSENT_HOST = "consent.google"

    /** Lowercase. Block pages say this in English or Korean. */
    val UNUSUAL_TRAFFIC_TEXTS = listOf("unusual traffic", "비정상적인 트래픽")

    /** Lowercase. */
    val TOO_MANY_REQUESTS_TEXTS = listOf("too many requests")

    /** The text of a page longer than this (characters) is not read for block signs: a block page says little, a result page a lot. */
    const val BLOCK_PAGE_MAX_TEXT = 2000
}

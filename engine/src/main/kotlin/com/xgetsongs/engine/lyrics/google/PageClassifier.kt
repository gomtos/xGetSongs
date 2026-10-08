package com.xgetsongs.engine.lyrics.google

/**
 * Tells Google's block pages from result pages. Pure. The address says most: a consent page lives on
 * [GoogleLyricsSelectors.CONSENT_HOST], the CAPTCHA page at [GoogleLyricsSelectors.SORRY_PATH]. The visible text is read
 * only when the page is short (see [GoogleLyricsSelectors.BLOCK_PAGE_MAX_TEXT]), so that a result page that merely
 * talks about "too many requests" is not taken for a block. [statusCode] is null when the browser cannot tell it.
 */
internal object PageClassifier {
    /** [GoogleLyricsResult.Consent], [GoogleLyricsResult.Captcha] or [GoogleLyricsResult.RateLimited]; null when the page is no block. */
    fun blockOf(url: String, statusCode: Int?, bodyText: String): GoogleLyricsResult? {
        val address = url.lowercase()
        if (GoogleLyricsSelectors.CONSENT_HOST in address) return GoogleLyricsResult.Consent
        if (GoogleLyricsSelectors.SORRY_PATH in address) return GoogleLyricsResult.Captcha
        val text = if (bodyText.length <= GoogleLyricsSelectors.BLOCK_PAGE_MAX_TEXT) bodyText.lowercase() else ""
        if (GoogleLyricsSelectors.UNUSUAL_TRAFFIC_TEXTS.any { it in text }) return GoogleLyricsResult.Captcha
        if (statusCode == 429 || GoogleLyricsSelectors.TOO_MANY_REQUESTS_TEXTS.any { it in text }) return GoogleLyricsResult.RateLimited
        return null
    }
}

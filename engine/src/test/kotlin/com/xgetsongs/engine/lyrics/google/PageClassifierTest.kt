package com.xgetsongs.engine.lyrics.google

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class PageClassifierTest {
    private val resultsUrl = "https://www.google.com/search?q=x&hl=ko"

    @Test
    fun theConsentHostIsTheConsentPage() {
        assertEquals(GoogleLyricsResult.Consent, PageClassifier.blockOf("https://consent.google.com/m?continue=x", null, ""))
    }

    @Test
    fun theSorryPathIsTheCaptchaPage() {
        assertEquals(GoogleLyricsResult.Captcha, PageClassifier.blockOf("https://www.google.com/sorry/index?continue=x", null, ""))
    }

    @Test
    fun unusualTrafficTextOnAShortPageIsCaptcha() {
        val english = "Our systems have detected unusual traffic from your computer network."
        val korean = "비정상적인 트래픽이 감지되었습니다."

        assertEquals(GoogleLyricsResult.Captcha, PageClassifier.blockOf(resultsUrl, null, english))
        assertEquals(GoogleLyricsResult.Captcha, PageClassifier.blockOf(resultsUrl, null, korean))
    }

    @Test
    fun tooManyRequestsTextOnAShortPageIsRateLimited() {
        assertEquals(GoogleLyricsResult.RateLimited, PageClassifier.blockOf(resultsUrl, null, "429. That's an error. Too Many Requests"))
    }

    @Test
    fun status429IsRateLimitedWhateverTheText() {
        assertEquals(GoogleLyricsResult.RateLimited, PageClassifier.blockOf(resultsUrl, 429, ""))
    }

    @Test
    fun theSorryPathBeatsStatus429() {
        assertEquals(GoogleLyricsResult.Captcha, PageClassifier.blockOf("https://www.google.com/sorry/index", 429, ""))
    }

    @Test
    fun theWordsOnALongPageAreNotReadForBlockSigns() {
        val longPage = "unusual traffic too many requests " + "가".repeat(GoogleLyricsSelectors.BLOCK_PAGE_MAX_TEXT)

        assertNull(PageClassifier.blockOf(resultsUrl, null, longPage))
    }

    @Test
    fun anOrdinaryResultPageIsNoBlock() {
        assertNull(PageClassifier.blockOf(resultsUrl, 200, "검색 결과 더미"))
        assertNull(PageClassifier.blockOf(resultsUrl, null, ""))
    }
}

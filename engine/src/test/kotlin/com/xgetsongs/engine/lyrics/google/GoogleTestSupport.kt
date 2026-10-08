package com.xgetsongs.engine.lyrics.google

import kotlin.time.Duration

/**
 * Builders of made-up Google pages for the tests. The markup follows what a real result page looks like (a card holding
 * paragraphs holding lines), but every sentence passed in is a dummy: there are no real lyrics in this repository.
 */
internal fun testLine(text: String) = """<span jsname="YS01Ge">$text</span>"""

internal fun testParagraph(vararg lines: String) =
    """<div jsname="U8S5sf">${lines.joinToString("<br>") { testLine(it) }}</div>"""

internal fun testCard(vararg paragraphs: String) =
    """<div data-lyricid="id-1"><div jsname="WbKHeb">${paragraphs.joinToString("")}</div></div>"""

internal fun testPage(body: String) = "<html><body><div>검색 결과 더미</div>$body</body></html>"

internal const val TEST_SEARCH_URL = "https://www.google.com/search?q=x&hl=en"

/** A loaded page with a card of three dummy lines. */
internal fun loadedCard() = RenderResult.Loaded(TEST_SEARCH_URL, testPage(testCard(testParagraph("더미 하나", "더미 둘", "더미 셋"))), "")

/** A loaded result page with no lyrics card. */
internal fun loadedWithoutCard() = RenderResult.Loaded(TEST_SEARCH_URL, testPage(""), "")

/** A loaded page that has a card marker but no lines in it. */
internal fun loadedBrokenCard() = RenderResult.Loaded(TEST_SEARCH_URL, testPage("""<div data-lyricid="id-1"></div>"""), "")

/** Google's block page. */
internal fun loadedSorry() = RenderResult.Loaded("https://www.google.com/sorry/index?continue=x", "<html><body></body></html>", "unusual traffic")

/** A browser that answers from a script and remembers the addresses it was asked for. */
internal class FakeBrowser(vararg answers: RenderResult) : RenderingBrowser {
    val urls = mutableListOf<String>()
    private val queue = ArrayDeque(answers.toList())

    override suspend fun render(url: String, timeout: Duration): RenderResult {
        urls += url
        return queue.removeFirstOrNull() ?: error("unexpected request to $url")
    }
}

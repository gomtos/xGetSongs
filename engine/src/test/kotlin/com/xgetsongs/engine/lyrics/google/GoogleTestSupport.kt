package com.xgetsongs.engine.lyrics.google

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

# 구글 가사 카드 가사 검색 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** LRCLIB에도 가사가 없을 때, 구글 검색 결과의 가사 카드에서 가사를 줄·문단 단위로 읽어 오는 `LyricsProvider`를 추가한다.

**Architecture:** 앱 안에 창 없이 숨겨 둔 JavaFX `WebEngine`이 `가수 제목 lyrics` 검색 페이지를 열고, 렌더된 HTML을 Kotlin(Jsoup)이 추출한다. 브라우저는 `RenderingBrowser` 인터페이스 뒤에 두어 나머지(정규화, 분류, 파서, 간격·백오프·캐시)를 가짜 브라우저와 가짜 시계로 테스트한다. `FallbackLyricsProvider(LRCLIB, 구글)`을 `createServices`에 연결하고, 기존 옵션 `searchLyricsOnline` 하나가 두 단계를 모두 켜고 끈다.

**Tech Stack:** Kotlin 2.4.20 / JDK 21 / Gradle Kotlin DSL, kotlinx.coroutines, kotlinx.serialization, **Jsoup 1.23.2**, **JavaFX web 21.0.12**, kotlin.test.

설계 문서: [2026-10-08-google-lyrics-card-design.md](../specs/2026-10-08-google-lyrics-card-design.md) (구조 실측 결과는 그 문서의 2와 9)

## Global Constraints

- 가사 원문을 소스, 테스트 픽스처, 로그, 커밋 메시지에 넣지 않는다. 테스트의 가짜 HTML에는 임의의 더미 문장만 쓰고, 실제 쿼리 결과는 줄 수·문단 수만 출력한다.
- 구글 차단을 우회하지 않는다(프록시, CAPTCHA 우회, 쿠키 재사용, UA 변경 없음). 차단되면 실패 결과로 끝낸다. 결과는 예외가 아니라 `GoogleLyricsResult`로 돌려준다.
- 요청 제한: 곡당 요청 1회(첫 시도가 `NoCard`이고 가수에 괄호 병기가 있을 때만 괄호를 뗀 이름으로 1회 더, 최대 2회), 연속 호출 사이 최소 간격 10초, 차단류(`Captcha` / `Consent` / `RateLimited` / `Timeout`)는 5분, 10분, 20분… 최대 60분 백오프, 캐시는 앱 실행 중 메모리만.
- 새 의존성은 `org.jsoup:jsoup:1.23.2`와 `org.openjfx:javafx-web:21.0.12`뿐이다.
- 새 옵션, API 모델, 설정, 화면, `LyricsOutcome` 값은 추가하지 않는다.
- 실제 구글 요청은 Task 9의 통합 테스트 1회뿐이다. 그 결과가 차단이면 멈추고 사용자에게 보고한다(재시도 금지).
- Gradle은 항상 `--no-daemon`으로 돌린다(공유 데몬이 죽으면 사용자의 앱이 같이 죽은 적이 있다).
- 코드 주석은 영어(기존 코드와 같다), 로그와 문서는 한국어. 커밋은 main에 바로 하고 메시지는 한국어, 끝에 `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` 줄을 붙인다. push는 Task 10에서 한 번에 한다.
- 패키지는 `com.xgetsongs.engine.lyrics.google`, 경로는 `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/`(테스트는 `engine/src/test/...` 같은 패키지 경로).

## 파일 구조

| 파일 | 역할 | Task |
|---|---|---|
| `gradle/libs.versions.toml` (수정) | jsoup, javafx 버전·라이브러리 | 1, 6 |
| `engine/build.gradle.kts` (수정) | 의존성 추가 | 1, 6 |
| `.../google/GoogleLyricsSelectors.kt` | 난독화 속성과 막힘 문자열 상수 | 1 |
| `.../google/GoogleLyricsResult.kt` | 결과 타입, `GoogleLyricsLog` | 1 |
| `.../google/LyricsCardParser.kt` | HTML → `Found`/`NoCard`/`ExtractionFailed` | 1 |
| `.../google/PageClassifier.kt` | 막힘 페이지 분류 | 2 |
| `.../google/GoogleSearchQuery.kt` | 검색어 정규화, 주소, 캐시 키 | 3 |
| `.../google/RenderingBrowser.kt` | 브라우저 인터페이스와 결과 | 4 |
| `.../google/GoogleLyricsFetcher.kt` | 조합, 간격, 백오프, 캐시 | 4 |
| `.../google/PageWatch.kt` | 기다림 종료 판단, `PageProbe` | 5 |
| `.../google/FxWebViewBrowser.kt` | JavaFX 구현 | 6 |
| `.../google/GoogleLyricsProvider.kt` | `LyricsProvider` 어댑터 | 7 |
| `engine/.../lyrics/FallbackLyricsProvider.kt` | 차례로 시도 | 7 |
| `server/.../LocalServer.kt` (수정) | 연결, 종료 | 8 |
| `engine/src/test/.../integration/RealGoogleLyricsIntegrationTest.kt` | 실제 스모크 | 9 |
| `README.md`, 스펙 6.4, `ApiModels.kt` 주석 (수정) | 문서 | 9 |

테스트 파일은 각 Task에 적는다. 테스트용 가짜 페이지 빌더와 `FakeBrowser`는 `.../google/GoogleTestSupport.kt` 한 파일에 모은다(Task 1에서 만들고 Task 4에서 덧붙인다).

---

### Task 1: 셀렉터 상수, 결과 타입, 가사 카드 파서

**Files:**
- Modify: `gradle/libs.versions.toml`, `engine/build.gradle.kts`
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/GoogleLyricsSelectors.kt`
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/GoogleLyricsResult.kt`
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/LyricsCardParser.kt`
- Create: `engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/GoogleTestSupport.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/LyricsCardParserTest.kt`

**Interfaces:**
- Consumes: `com.xgetsongs.shared.lyrics.LyricsExtractor.tidy(text: String?): String?`
- Produces:
  - `internal object GoogleLyricsSelectors` — `CARD_SCOPES: List<String>`, `LINE: String`, `CARD_PRESENCE: String`, `SORRY_PATH`, `CONSENT_HOST`, `UNUSUAL_TRAFFIC_TEXTS: List<String>`, `TOO_MANY_REQUESTS_TEXTS: List<String>`, `BLOCK_PAGE_MAX_TEXT: Int`
  - `internal sealed interface GoogleLyricsResult` — `Found(lyrics: String, lineCount: Int, paragraphCount: Int)`, `NoCard`, `ExtractionFailed`, `Captcha`, `Consent`, `RateLimited`, `Timeout`, `BrowserUnavailable(reason: String)`, `CoolingDown`
  - `interface GoogleLyricsLog { fun info(message: String); fun warn(message: String); companion object { val None } }`
  - `internal object LyricsCardParser { fun parse(html: String): GoogleLyricsResult }` (돌려주는 값은 `Found` / `NoCard` / `ExtractionFailed` 중 하나)
  - 테스트용 `testLine(text)`, `testParagraph(vararg lines)`, `testCard(vararg paragraphs)`, `testPage(body)`

- [ ] **Step 1: 의존성 추가**

`gradle/libs.versions.toml`의 `[versions]`에 두 줄, `[libraries]`에 한 줄을 더한다(javafx는 Task 6에서 쓰지만 버전은 여기서 같이 둔다).

```toml
jsoup = "1.23.2"
javafx = "21.0.12"
```

```toml
jsoup = { module = "org.jsoup:jsoup", version.ref = "jsoup" }
javafx-web = { module = "org.openjfx:javafx-web", version.ref = "javafx" }
```

`engine/build.gradle.kts`의 `dependencies`에서 `implementation(libs.kotlinx.serialization.json)` 다음 줄에 추가한다.

```kotlin
    implementation(libs.jsoup)
```

- [ ] **Step 2: 가짜 페이지 빌더 만들기**

`engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/GoogleTestSupport.kt`:

```kotlin
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
```

- [ ] **Step 3: 파서의 실패하는 테스트 쓰기**

`engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/LyricsCardParserTest.kt`:

```kotlin
package com.xgetsongs.engine.lyrics.google

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs

/** [LyricsCardParser] against made-up pages (see GoogleTestSupport.kt). */
class LyricsCardParserTest {
    private fun found(html: String) = assertIs<GoogleLyricsResult.Found>(LyricsCardParser.parse(html))

    @Test
    fun linesOfAParagraphAreJoinedByNewlineAndParagraphsByOneBlankLine() {
        val result = found(testPage(testCard(testParagraph("더미 문장 1", "더미 문장 2"), testParagraph("더미 문장 3", "더미 문장 4", "더미 문장 5"))))

        assertEquals("더미 문장 1\n더미 문장 2\n\n더미 문장 3\n더미 문장 4\n더미 문장 5", result.lyrics)
        assertEquals(5, result.lineCount)
        assertEquals(2, result.paragraphCount)
    }

    @Test
    fun aParagraphOfOneLineIsAParagraph() {
        val result = found(testPage(testCard(testParagraph("앞 1", "앞 2"), testParagraph("후렴 더미"), testParagraph("뒤 1", "뒤 2"))))

        assertEquals("앞 1\n앞 2\n\n후렴 더미\n\n뒤 1\n뒤 2", result.lyrics)
        assertEquals(5, result.lineCount)
        assertEquals(3, result.paragraphCount)
    }

    @Test
    fun linesAreTrimmedAndEmptyOnesAreDropped() {
        val result = found(testPage(testCard(testParagraph("  더미   문장  ", "&nbsp;", "", "더미 둘", "더미 셋"))))

        assertEquals("더미 문장\n더미 둘\n더미 셋", result.lyrics)
        assertEquals(3, result.lineCount)
        assertEquals(1, result.paragraphCount)
    }

    @Test
    fun markupInsideALineKeepsItsText() {
        val html = testPage(testCard("""<div jsname="U8S5sf">${testLine("더미 <b>강조</b> 문장")}${testLine("둘")}${testLine("셋")}</div>"""))

        assertEquals("더미 강조 문장\n둘\n셋", found(html).lyrics)
    }

    @Test
    fun withoutALyricIdCardTheWbkContainerIsTheScope() {
        val html = testPage("""<div jsname="WbKHeb">${testParagraph("더미 1", "더미 2", "더미 3")}</div>""")

        assertEquals("더미 1\n더미 2\n더미 3", found(html).lyrics)
    }

    @Test
    fun withoutAnyContainerTheLineSpansOfThePageAreRead() {
        val html = testPage("""<div>${testLine("가")}${testLine("나")}</div><div>${testLine("다")}</div>""")

        val result = found(html)
        assertEquals("가\n나\n\n다", result.lyrics)
        assertEquals(2, result.paragraphCount)
    }

    @Test
    fun lineSpansOutsideTheCardAreIgnored() {
        val html = testPage(testParagraph("바깥 줄") + testCard(testParagraph("안 1", "안 2", "안 3")))

        assertEquals("안 1\n안 2\n안 3", found(html).lyrics)
    }

    @Test
    fun aPageWithoutAnyCardMarkerHasNoCard() {
        assertEquals(GoogleLyricsResult.NoCard, LyricsCardParser.parse(testPage("")))
    }

    @Test
    fun aCardWithoutLinesIsAFailedExtraction() {
        val html = testPage("""<div data-lyricid="id-1"><div jsname="WbKHeb"></div></div>""")

        assertEquals(GoogleLyricsResult.ExtractionFailed, LyricsCardParser.parse(html))
    }

    @Test
    fun aCardWithFewerThanThreeLinesIsAFailedExtraction() {
        assertEquals(GoogleLyricsResult.ExtractionFailed, LyricsCardParser.parse(testPage(testCard(testParagraph("더미 1", "더미 2")))))
    }

    @Test
    fun aFoundResultNeverPrintsTheLyrics() {
        val result = found(testPage(testCard(testParagraph("비밀 문장 1", "비밀 문장 2", "비밀 문장 3"))))

        assertFalse("비밀" in result.toString())
    }
}
```

- [ ] **Step 4: 실패 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test --tests "com.xgetsongs.engine.lyrics.google.LyricsCardParserTest"`
Expected: FAIL, 컴파일 오류 `Unresolved reference: LyricsCardParser` 등.

- [ ] **Step 5: 상수와 결과 타입 쓰기**

`engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/GoogleLyricsSelectors.kt`:

```kotlin
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
```

`engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/GoogleLyricsResult.kt`:

```kotlin
package com.xgetsongs.engine.lyrics.google

/** What one trip to Google's lyrics card came to. A lookup that goes wrong is a result, never an exception. */
internal sealed interface GoogleLyricsResult {
    /** The card was read. [lyrics] is tidy text: lines joined by `\n`, paragraphs by one blank line. */
    class Found(val lyrics: String, val lineCount: Int, val paragraphCount: Int) : GoogleLyricsResult {
        // Lyrics are a copyrighted work: whatever prints a result (a log line, a failed test) must not print them.
        override fun toString() = "Found(lines=$lineCount, paragraphs=$paragraphCount)"
    }

    /** The page loaded and has no lyrics card. */
    data object NoCard : GoogleLyricsResult

    /** The page has a lyrics card but the lines could not be read from it: Google probably changed its markup. */
    data object ExtractionFailed : GoogleLyricsResult

    /** Google's block page (CAPTCHA, "unusual traffic"). */
    data object Captcha : GoogleLyricsResult

    /** Google's consent page. */
    data object Consent : GoogleLyricsResult

    /** HTTP 429 or a page that says "too many requests". */
    data object RateLimited : GoogleLyricsResult

    /** The page did not settle in time. */
    data object Timeout : GoogleLyricsResult

    /** The web view cannot be used (JavaFX is missing or failed to start). [reason] is the name of what went wrong. */
    class BrowserUnavailable(val reason: String) : GoogleLyricsResult {
        override fun toString() = "BrowserUnavailable($reason)"
    }

    /** Google is being left alone after trouble: no request was made. */
    data object CoolingDown : GoogleLyricsResult
}

/** Where the lookup says what it did. The text holds counts and names of results, never a song or lyrics. */
interface GoogleLyricsLog {
    fun info(message: String)

    fun warn(message: String)

    companion object {
        val None: GoogleLyricsLog = object : GoogleLyricsLog {
            override fun info(message: String) = Unit

            override fun warn(message: String) = Unit
        }
    }
}
```

- [ ] **Step 6: 파서 쓰기**

`engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/LyricsCardParser.kt`:

```kotlin
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
```

- [ ] **Step 7: 통과 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test --tests "com.xgetsongs.engine.lyrics.google.LyricsCardParserTest"`
Expected: PASS (11 tests), `BUILD SUCCESSFUL`.

- [ ] **Step 8: Commit**

```bash
git add gradle/libs.versions.toml engine/build.gradle.kts engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google
git commit -m "feat(engine): 구글 가사 카드의 줄과 문단을 읽는 파서를 추가한다" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: 막힘 페이지 분류기

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/PageClassifier.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/PageClassifierTest.kt`

**Interfaces:**
- Consumes: `GoogleLyricsSelectors`, `GoogleLyricsResult` (Task 1)
- Produces: `internal object PageClassifier { fun blockOf(url: String, statusCode: Int?, bodyText: String): GoogleLyricsResult? }` — `Consent` / `Captcha` / `RateLimited` 중 하나, 막힘이 아니면 null

- [ ] **Step 1: 실패하는 테스트 쓰기**

```kotlin
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
```

- [ ] **Step 2: 실패 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test --tests "com.xgetsongs.engine.lyrics.google.PageClassifierTest"`
Expected: FAIL, `Unresolved reference: PageClassifier`.

- [ ] **Step 3: 구현**

```kotlin
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
```

- [ ] **Step 4: 통과 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test --tests "com.xgetsongs.engine.lyrics.google.PageClassifierTest"`
Expected: PASS (8 tests).

- [ ] **Step 5: Commit**

```bash
git add engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/PageClassifier.kt engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/PageClassifierTest.kt
git commit -m "feat(engine): 구글 막힘 페이지(CAPTCHA, 동의창, 429)를 가려내는 분류기를 추가한다" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: 검색어 정규화

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/GoogleSearchQuery.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/GoogleSearchQueryTest.kt`

**Interfaces:**
- Consumes: 없음
- Produces:
  - `internal class GoogleSearchRequest(val text: String, val language: String)` — `val url: String`
  - `internal object GoogleSearchQuery { fun requests(artist: String, title: String): List<GoogleSearchRequest>; fun cacheKey(request: GoogleSearchRequest): String }` — `requests`는 가수나 제목이 비면 빈 목록, 아니면 1~2개(첫째는 원문 가수, 둘째는 괄호를 뗀 가수이고 둘이 다를 때만)

- [ ] **Step 1: 실패하는 테스트 쓰기**

```kotlin
package com.xgetsongs.engine.lyrics.google

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

class GoogleSearchQueryTest {
    private fun texts(artist: String, title: String) = GoogleSearchQuery.requests(artist, title).map { it.text }

    @Test
    fun anArtistWithAParenthesisedNameGivesTheFullNameFirstAndThenTheNameWithoutIt() {
        val requests = GoogleSearchQuery.requests("RESCENE (리센느)", "LOVE ATTACK")

        assertEquals(listOf("RESCENE (리센느) LOVE ATTACK lyrics", "RESCENE LOVE ATTACK lyrics"), requests.map { it.text })
        assertEquals(listOf("ko", "ko"), requests.map { it.language })
    }

    @Test
    fun aPlainArtistGivesOneRequestInEnglish() {
        val requests = GoogleSearchQuery.requests("IU", "Love poem")

        assertEquals(listOf("IU Love poem lyrics"), requests.map { it.text })
        assertEquals("en", requests.single().language)
    }

    @Test
    fun aKoreanTitleAloneMakesItKorean() {
        assertEquals("ko", GoogleSearchQuery.requests("IU", "좋은 날").single().language)
    }

    @Test
    fun theAddressIsEncodedAsUtf8WithPercentTwentyForSpaces() {
        val url = GoogleSearchQuery.requests("RESCENE (리센느)", "LOVE ATTACK").first().url

        assertEquals("https://www.google.com/search?q=RESCENE%20%28%EB%A6%AC%EC%84%BC%EB%8A%90%29%20LOVE%20ATTACK%20lyrics&hl=ko", url)
        assertEquals("https://www.google.com/search?q=IU%20Love%20poem%20lyrics&hl=en", GoogleSearchQuery.requests("IU", "Love poem").single().url)
    }

    @Test
    fun quotesAroundWordsOfTheTitleAreDropped() {
        assertEquals(listOf("IU Love poem lyrics"), texts("IU", "‘Love’ “poem”"))
        assertEquals(listOf("IU Love poem lyrics"), texts("IU", "\"Love\" poem"))
    }

    @Test
    fun anApostropheInsideAWordIsKeptAsAStraightOne() {
        assertEquals(listOf("IU Don't Stop lyrics"), texts("IU", "Don’t Stop"))
        assertEquals(listOf("IU Don't Stop lyrics"), texts("IU", "Don't Stop"))
    }

    @Test
    fun aTrailingCreditIsDropped() {
        assertEquals(listOf("IU Song lyrics"), texts("IU", "Song feat. Someone"))
        assertEquals(listOf("IU Song lyrics"), texts("IU", "Song FT. Someone Else"))
    }

    @Test
    fun aCreditInBracketsIsDropped() {
        assertEquals(listOf("IU Song lyrics"), texts("IU", "Song (feat. Someone)"))
        assertEquals(listOf("IU Song lyrics"), texts("IU", "Song [Prod. By Someone]"))
        assertEquals(listOf("소연 퇴사할게여 lyrics"), texts("소연", "퇴사할게여 (Narr. 기안84)"))
    }

    @Test
    fun aTrailingCreditStopsAtTheNextBracket() {
        assertEquals(listOf("IU Song (Remix) lyrics"), texts("IU", "Song ft. A & B (Remix)"))
    }

    @Test
    fun aBracketThatNamesAnotherVersionStays() {
        assertEquals(listOf("IU Hello (Japanese Ver.) lyrics"), texts("IU", "Hello (Japanese Ver.)"))
    }

    @Test
    fun whitespaceIsCollapsed() {
        assertEquals(listOf("IU Love poem lyrics"), texts("  IU  ", "Love   poem "))
    }

    @Test
    fun anArtistThatIsPartlyInParenthesesLosesOnlyThatPartInTheSecondRequest() {
        assertEquals(listOf("(G)I-DLE Tomboy lyrics", "I-DLE Tomboy lyrics"), texts("(G)I-DLE", "Tomboy"))
    }

    @Test
    fun aBlankArtistOrTitleGivesNoRequest() {
        assertTrue(GoogleSearchQuery.requests("", "Love poem").isEmpty())
        assertTrue(GoogleSearchQuery.requests("IU", "  ").isEmpty())
    }

    @Test
    fun theCacheKeyIgnoresCaseAndSpacing() {
        val one = GoogleSearchQuery.cacheKey(GoogleSearchQuery.requests("IU", "Love poem").first())
        val other = GoogleSearchQuery.cacheKey(GoogleSearchQuery.requests(" iu ", "LOVE  POEM").first())
        val another = GoogleSearchQuery.cacheKey(GoogleSearchQuery.requests("IU", "Palette").first())

        assertEquals(one, other)
        assertNotEquals(one, another)
    }
}
```

- [ ] **Step 2: 실패 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test --tests "com.xgetsongs.engine.lyrics.google.GoogleSearchQueryTest"`
Expected: FAIL, `Unresolved reference: GoogleSearchQuery`.

- [ ] **Step 3: 구현**

```kotlin
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
```

- [ ] **Step 4: 통과 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test --tests "com.xgetsongs.engine.lyrics.google.GoogleSearchQueryTest"`
Expected: PASS (14 tests). 실패하면 정규식 결과를 출력해 보고 고친다(예: `Song ft. A & B (Remix)`가 `Song (Remix)`가 되는지).

- [ ] **Step 5: Commit**

```bash
git add engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/GoogleSearchQuery.kt engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/GoogleSearchQueryTest.kt
git commit -m "feat(engine): 구글 가사 검색어 정규화와 주소, 캐시 키를 추가한다" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: 브라우저 인터페이스와 Fetcher (간격, 백오프, 캐시)

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/RenderingBrowser.kt`
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/GoogleLyricsFetcher.kt`
- Modify: `engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/GoogleTestSupport.kt` (덧붙임)
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/GoogleLyricsFetcherTest.kt`

**Interfaces:**
- Consumes: `GoogleSearchQuery.requests/cacheKey`, `GoogleSearchRequest.url` (Task 3), `PageClassifier.blockOf` (Task 2), `LyricsCardParser.parse` (Task 1), `GoogleLyricsResult`/`GoogleLyricsLog` (Task 1)
- Produces:
  - `internal interface RenderingBrowser { suspend fun render(url: String, timeout: Duration): RenderResult }`
  - `internal sealed interface RenderResult { class Loaded(val url: String, val html: String, val bodyText: String); data object TimedOut; class Unavailable(val reason: String) }`
  - `internal class GoogleLyricsFetcher(browser: RenderingBrowser, timeSource: TimeSource = TimeSource.Monotonic, minInterval: Duration = 10.seconds, firstBackoff: Duration = 5.minutes, maxBackoff: Duration = 60.minutes, pageTimeout: Duration = 20.seconds, log: GoogleLyricsLog = GoogleLyricsLog.None, pause: suspend (Duration) -> Unit = { delay(it) })` — `suspend fun fetch(artist: String, title: String): GoogleLyricsResult`
  - 테스트용 `FakeBrowser(vararg answers: RenderResult)` (`urls: MutableList<String>`), `TEST_SEARCH_URL`, `loadedCard()`, `loadedWithoutCard()`, `loadedBrokenCard()`, `loadedSorry()`

- [ ] **Step 1: 테스트 지원 코드 덧붙이기**

`GoogleTestSupport.kt` 맨 위 import 아래(파일은 `package` 줄 다음)에 import 두 줄을 넣고, 파일 끝에 아래를 덧붙인다.

```kotlin
import kotlin.time.Duration
```

```kotlin

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
```

- [ ] **Step 2: 실패하는 Fetcher 테스트 쓰기**

```kotlin
package com.xgetsongs.engine.lyrics.google

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertIs
import kotlin.test.assertTrue
import kotlin.time.Duration
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TestTimeSource

/** [GoogleLyricsFetcher] with a scripted browser and a clock the test moves itself: no network, no real waiting. */
class GoogleLyricsFetcherTest {
    private val clock = TestTimeSource()
    private val pauses = mutableListOf<Duration>()
    private val logLines = mutableListOf<String>()
    private val log = object : GoogleLyricsLog {
        override fun info(message: String) {
            logLines += "INFO $message"
        }

        override fun warn(message: String) {
            logLines += "WARN $message"
        }
    }

    private fun fetcher(browser: RenderingBrowser) = GoogleLyricsFetcher(
        browser = browser,
        timeSource = clock,
        log = log,
        pause = { duration ->
            pauses += duration
            clock += duration
        },
    )

    @Test
    fun aFoundCardGivesItsLyricsAfterOneRequest() = runTest {
        val browser = FakeBrowser(loadedCard())

        val found = assertIs<GoogleLyricsResult.Found>(fetcher(browser).fetch("IU", "Love poem"))

        assertEquals("더미 하나\n더미 둘\n더미 셋", found.lyrics)
        assertEquals(listOf("https://www.google.com/search?q=IU%20Love%20poem%20lyrics&hl=en"), browser.urls)
        assertTrue(logLines.any { "줄 3, 문단 1" in it })
        assertTrue(logLines.none { "더미" in it }, "the log must not hold lyrics")
    }

    @Test
    fun noCardForAnArtistWithAParenthesisedNameTriesTheNameWithoutItOnce() = runTest {
        val browser = FakeBrowser(loadedWithoutCard(), loadedCard())

        val result = fetcher(browser).fetch("RESCENE (리센느)", "LOVE ATTACK")

        assertIs<GoogleLyricsResult.Found>(result)
        assertEquals(2, browser.urls.size)
        assertTrue("q=RESCENE%20LOVE%20ATTACK%20lyrics&hl=ko" in browser.urls[1])
        assertEquals(listOf(10.seconds), pauses, "the second request keeps the minimum interval too")
    }

    @Test
    fun noCardTwiceIsNoCardAndIsNotAskedAgain() = runTest {
        val browser = FakeBrowser(loadedWithoutCard(), loadedWithoutCard())
        val fetcher = fetcher(browser)

        assertEquals(GoogleLyricsResult.NoCard, fetcher.fetch("RESCENE (리센느)", "LOVE ATTACK"))
        assertEquals(2, browser.urls.size)
        assertEquals(GoogleLyricsResult.NoCard, fetcher.fetch("RESCENE (리센느)", "LOVE ATTACK"))
        assertEquals(2, browser.urls.size, "a song seen once is not asked again")
    }

    @Test
    fun aPlainArtistWithNoCardMakesOnlyOneRequest() = runTest {
        val browser = FakeBrowser(loadedWithoutCard())

        assertEquals(GoogleLyricsResult.NoCard, fetcher(browser).fetch("IU", "Love poem"))
        assertEquals(1, browser.urls.size)
    }

    @Test
    fun theSameSongWrittenDifferentlyIsAskedOnce() = runTest {
        val browser = FakeBrowser(loadedCard())
        val fetcher = fetcher(browser)

        assertIs<GoogleLyricsResult.Found>(fetcher.fetch("IU", "Love poem"))
        assertIs<GoogleLyricsResult.Found>(fetcher.fetch(" iu ", "LOVE POEM"))
        assertEquals(1, browser.urls.size)
    }

    @Test
    fun twoSongsAreKeptTheMinimumIntervalApart() = runTest {
        val browser = FakeBrowser(loadedCard(), loadedCard())
        val fetcher = fetcher(browser)

        fetcher.fetch("IU", "Love poem")
        assertEquals(emptyList(), pauses, "the first request waits for nothing")
        clock += 4.seconds
        fetcher.fetch("IU", "Palette")

        assertEquals(listOf(6.seconds), pauses)
    }

    @Test
    fun noPauseWhenTheIntervalHasPassedAlready() = runTest {
        val browser = FakeBrowser(loadedCard(), loadedCard())
        val fetcher = fetcher(browser)

        fetcher.fetch("IU", "Love poem")
        clock += 11.seconds
        fetcher.fetch("IU", "Palette")

        assertEquals(emptyList(), pauses)
    }

    @Test
    fun aBlockPageStartsACoolDownDuringWhichNothingIsRequested() = runTest {
        val browser = FakeBrowser(loadedSorry(), loadedCard())
        val fetcher = fetcher(browser)

        assertEquals(GoogleLyricsResult.Captcha, fetcher.fetch("IU", "Love poem"))
        assertEquals(GoogleLyricsResult.CoolingDown, fetcher.fetch("IU", "Palette"))
        assertEquals(1, browser.urls.size, "no request while cooling down")
        clock += 5.minutes
        assertIs<GoogleLyricsResult.Found>(fetcher.fetch("IU", "Palette"))
        assertEquals(2, browser.urls.size)
    }

    @Test
    fun theCoolDownDoublesWhileGoogleKeepsBlockingAndStartsOverWhenItAnswers() = runTest {
        val browser = FakeBrowser(loadedSorry(), loadedSorry(), loadedCard(), loadedSorry(), loadedCard())
        val fetcher = fetcher(browser)

        assertEquals(GoogleLyricsResult.Captcha, fetcher.fetch("A", "a")) // first block: 5 minutes
        clock += 5.minutes
        assertEquals(GoogleLyricsResult.Captcha, fetcher.fetch("B", "b")) // blocked again: 10 minutes
        clock += 9.minutes
        assertEquals(GoogleLyricsResult.CoolingDown, fetcher.fetch("C", "c"))
        clock += 1.minutes
        assertIs<GoogleLyricsResult.Found>(fetcher.fetch("C", "c")) // an ordinary answer ends the streak
        assertEquals(GoogleLyricsResult.Captcha, fetcher.fetch("D", "d")) // blocked again: back to 5 minutes
        clock += 5.minutes
        assertIs<GoogleLyricsResult.Found>(fetcher.fetch("E", "e"))
    }

    @Test
    fun theCoolDownIsCappedAtSixtyMinutes() = runTest {
        val browser = FakeBrowser(loadedSorry(), loadedSorry(), loadedSorry(), loadedSorry(), loadedSorry(), loadedCard())
        val fetcher = fetcher(browser)

        for ((index, minutes) in listOf(5, 10, 20, 40).withIndex()) {
            assertEquals(GoogleLyricsResult.Captcha, fetcher.fetch("S$index", "t"))
            clock += minutes.minutes
        }
        assertEquals(GoogleLyricsResult.Captcha, fetcher.fetch("S4", "t")) // 5 * 2^4 = 80, capped
        clock += 59.minutes
        assertEquals(GoogleLyricsResult.CoolingDown, fetcher.fetch("S5", "t"))
        clock += 1.minutes
        assertIs<GoogleLyricsResult.Found>(fetcher.fetch("S5", "t"))
    }

    @Test
    fun aTimeoutAlsoStartsACoolDown() = runTest {
        val browser = FakeBrowser(RenderResult.TimedOut)
        val fetcher = fetcher(browser)

        assertEquals(GoogleLyricsResult.Timeout, fetcher.fetch("IU", "Love poem"))
        assertEquals(GoogleLyricsResult.CoolingDown, fetcher.fetch("IU", "Palette"))
    }

    @Test
    fun blockPagesAreToldApart() = runTest {
        val consent = RenderResult.Loaded("https://consent.google.com/m?continue=x", "<html></html>", "")
        val tooMany = RenderResult.Loaded(TEST_SEARCH_URL, "<html></html>", "429 Too Many Requests")

        assertEquals(GoogleLyricsResult.Consent, fetcher(FakeBrowser(consent)).fetch("IU", "Love poem"))
        assertEquals(GoogleLyricsResult.RateLimited, fetcher(FakeBrowser(tooMany)).fetch("IU", "Love poem"))
        assertEquals(GoogleLyricsResult.Captcha, fetcher(FakeBrowser(loadedSorry())).fetch("IU", "Love poem"))
    }

    @Test
    fun anUnavailableBrowserNeitherCoolsDownNorIsRemembered() = runTest {
        val browser = FakeBrowser(RenderResult.Unavailable("NoClassDefFoundError"), loadedCard())
        val fetcher = fetcher(browser)

        val first = assertIs<GoogleLyricsResult.BrowserUnavailable>(fetcher.fetch("IU", "Love poem"))
        assertEquals("NoClassDefFoundError", first.reason)
        assertIs<GoogleLyricsResult.Found>(fetcher.fetch("IU", "Love poem"))
        assertEquals(2, browser.urls.size)
    }

    @Test
    fun aFailedExtractionIsRememberedAndDoesNotCoolDown() = runTest {
        val browser = FakeBrowser(loadedBrokenCard(), loadedCard())
        val fetcher = fetcher(browser)

        assertEquals(GoogleLyricsResult.ExtractionFailed, fetcher.fetch("IU", "Love poem"))
        assertEquals(GoogleLyricsResult.ExtractionFailed, fetcher.fetch("IU", "Love poem"))
        assertEquals(1, browser.urls.size)
        assertIs<GoogleLyricsResult.Found>(fetcher.fetch("IU", "Palette"))
        assertTrue(logLines.any { it.startsWith("WARN") && "형식이 바뀌었을 수" in it })
    }

    @Test
    fun anExceptionFromTheBrowserIsAnUnavailableBrowser() = runTest {
        val browser = object : RenderingBrowser {
            override suspend fun render(url: String, timeout: Duration): RenderResult = throw IllegalStateException("boom")
        }

        val result = assertIs<GoogleLyricsResult.BrowserUnavailable>(fetcher(browser).fetch("IU", "Love poem"))
        assertEquals("IllegalStateException", result.reason)
    }

    @Test
    fun aCancellationIsNotSwallowed() = runTest {
        val browser = object : RenderingBrowser {
            override suspend fun render(url: String, timeout: Duration): RenderResult = throw CancellationException("stop")
        }

        assertFailsWith<CancellationException> { fetcher(browser).fetch("IU", "Love poem") }
    }

    @Test
    fun aBlankArtistOrTitleMakesNoRequest() = runTest {
        val browser = FakeBrowser()
        val fetcher = fetcher(browser)

        assertEquals(GoogleLyricsResult.NoCard, fetcher.fetch("", "Love poem"))
        assertEquals(GoogleLyricsResult.NoCard, fetcher.fetch("IU", " "))
        assertEquals(emptyList(), browser.urls)
    }
}
```

- [ ] **Step 3: 실패 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test --tests "com.xgetsongs.engine.lyrics.google.GoogleLyricsFetcherTest"`
Expected: FAIL, `Unresolved reference: RenderingBrowser` 등 컴파일 오류.

- [ ] **Step 4: 브라우저 인터페이스 쓰기**

`RenderingBrowser.kt`:

```kotlin
package com.xgetsongs.engine.lyrics.google

import kotlin.time.Duration

/** A browser that can open an address and hand back the finished page. */
internal interface RenderingBrowser {
    /**
     * Opens [url], waits until the page shows a lyrics card or a block page or has settled without one (but at most
     * [timeout]), and returns it. Never throws, except for a `CancellationException` when the calling coroutine is
     * cancelled; a browser that cannot be used answers [RenderResult.Unavailable].
     */
    suspend fun render(url: String, timeout: Duration): RenderResult
}

internal sealed interface RenderResult {
    /** The page as it stands: its final address, its HTML and the first part of its visible text. */
    class Loaded(val url: String, val html: String, val bodyText: String) : RenderResult

    /** The page did not settle within the time limit. */
    data object TimedOut : RenderResult

    /** The browser cannot be used; [reason] is the name of what went wrong (an exception class, say). */
    class Unavailable(val reason: String) : RenderResult
}
```

- [ ] **Step 5: Fetcher 쓰기**

`GoogleLyricsFetcher.kt`:

```kotlin
package com.xgetsongs.engine.lyrics.google

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.util.concurrent.ConcurrentHashMap
import kotlin.time.Duration
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TimeMark
import kotlin.time.TimeSource

/**
 * Looks a song up in Google's lyrics card through a [RenderingBrowser], politely. The rules:
 *  - one trip per song, or two when the first finds no card and the artist has a parenthesised name
 *    ([GoogleSearchQuery.requests]); trips run one at a time, at least [minInterval] after the previous one ended;
 *  - Google answering with trouble ([GoogleLyricsResult.Captcha], [GoogleLyricsResult.Consent],
 *    [GoogleLyricsResult.RateLimited], [GoogleLyricsResult.Timeout]) puts Google off limits for [firstBackoff], then twice
 *    that for the next trouble in a row, and so on up to [maxBackoff]; an ordinary answer ends the streak. While it is off
 *    limits the answer is [GoogleLyricsResult.CoolingDown] and no trip is made. Nothing tries to get around a block;
 *  - what Google said about a song ([GoogleLyricsResult.Found], [GoogleLyricsResult.NoCard],
 *    [GoogleLyricsResult.ExtractionFailed]) is remembered for as long as the fetcher lives; trouble and an unusable
 *    browser are not, they say nothing about the song.
 *
 * Whatever goes wrong is a [GoogleLyricsResult]; only a cancellation is thrown. [pause] is how the interval is waited
 * out (a test replaces it).
 */
internal class GoogleLyricsFetcher(
    private val browser: RenderingBrowser,
    private val timeSource: TimeSource = TimeSource.Monotonic,
    private val minInterval: Duration = 10.seconds,
    private val firstBackoff: Duration = 5.minutes,
    private val maxBackoff: Duration = 60.minutes,
    private val pageTimeout: Duration = 20.seconds,
    private val log: GoogleLyricsLog = GoogleLyricsLog.None,
    private val pause: suspend (Duration) -> Unit = { delay(it) },
) {
    private val lock = Mutex()
    private val remembered = ConcurrentHashMap<String, GoogleLyricsResult>()

    // The three below are only touched while holding [lock].
    private var lastTripEnd: TimeMark? = null
    private var coolDownEnd: TimeMark? = null
    private var troubleStreak = 0

    suspend fun fetch(artist: String, title: String): GoogleLyricsResult {
        val requests = GoogleSearchQuery.requests(artist, title)
        if (requests.isEmpty()) return GoogleLyricsResult.NoCard
        val key = GoogleSearchQuery.cacheKey(requests.first())
        remembered[key]?.let { return it }
        return lock.withLock {
            // Another call may have asked about this song while this one waited for the lock.
            remembered[key]?.let { return@withLock it }
            var result: GoogleLyricsResult = GoogleLyricsResult.NoCard
            for (request in requests) {
                if (isCoolingDown()) {
                    result = GoogleLyricsResult.CoolingDown
                    break
                }
                waitForInterval()
                result = tripTo(request)
                if (result != GoogleLyricsResult.NoCard) break
            }
            if (result is GoogleLyricsResult.Found || result == GoogleLyricsResult.NoCard || result == GoogleLyricsResult.ExtractionFailed) {
                remembered[key] = result
            }
            result
        }
    }

    private fun isCoolingDown(): Boolean = coolDownEnd?.let { !it.hasPassedNow() } ?: false

    private suspend fun waitForInterval() {
        val last = lastTripEnd ?: return
        val wait = minInterval - last.elapsedNow()
        if (wait.isPositive()) pause(wait)
    }

    /** One trip: the browser, then the classification of what it brought back. */
    private suspend fun tripTo(request: GoogleSearchRequest): GoogleLyricsResult {
        val result = try {
            when (val page = browser.render(request.url, pageTimeout)) {
                RenderResult.TimedOut -> GoogleLyricsResult.Timeout
                is RenderResult.Unavailable -> GoogleLyricsResult.BrowserUnavailable(page.reason)
                is RenderResult.Loaded -> PageClassifier.blockOf(page.url, null, page.bodyText) ?: LyricsCardParser.parse(page.html)
            }
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            GoogleLyricsResult.BrowserUnavailable(e.javaClass.simpleName)
        }
        lastTripEnd = timeSource.markNow()
        record(result)
        return result
    }

    /** Logs [result] (counts and names only) and keeps the cool-down books. */
    private fun record(result: GoogleLyricsResult) {
        when (result) {
            is GoogleLyricsResult.Found -> {
                troubleStreak = 0
                log.info("구글 가사 카드: 찾음 (줄 ${result.lineCount}, 문단 ${result.paragraphCount})")
            }
            GoogleLyricsResult.NoCard -> {
                troubleStreak = 0
                log.info("구글 가사 카드: 카드 없음")
            }
            GoogleLyricsResult.ExtractionFailed -> {
                troubleStreak = 0
                log.warn("구글 가사 카드: 카드는 있으나 줄을 읽지 못함 (구글 페이지 형식이 바뀌었을 수 있음)")
            }
            GoogleLyricsResult.Captcha -> startCoolDown("CAPTCHA로 막힘")
            GoogleLyricsResult.Consent -> startCoolDown("동의창이 뜸")
            GoogleLyricsResult.RateLimited -> startCoolDown("요청이 너무 많다는 응답(429)")
            GoogleLyricsResult.Timeout -> startCoolDown("시간 초과")
            is GoogleLyricsResult.BrowserUnavailable -> log.warn("구글 가사 카드: 웹뷰를 쓸 수 없음 (${result.reason})")
            GoogleLyricsResult.CoolingDown -> Unit
        }
    }

    private fun startCoolDown(what: String) {
        val length = minOf(maxBackoff, firstBackoff * (1 shl minOf(troubleStreak, MAX_DOUBLINGS)))
        troubleStreak++
        coolDownEnd = timeSource.markNow() + length
        log.warn("구글 가사 카드: $what, ${length.inWholeMinutes}분 동안 구글 검색을 쉼")
    }

    private companion object {
        /** More doublings than this are beyond any [maxBackoff]; it only keeps the shift from overflowing. */
        const val MAX_DOUBLINGS = 16
    }
}
```

- [ ] **Step 6: 통과 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test --tests "com.xgetsongs.engine.lyrics.google.GoogleLyricsFetcherTest"`
Expected: PASS (17 tests). 실패하면 `System.out` 출력 대신 어떤 단언이 깨졌는지 보고 `record`/`fetch`의 분기를 고친다.

- [ ] **Step 7: 구글 패키지 전체가 같이 도는지 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test --tests "com.xgetsongs.engine.lyrics.google.*"`
Expected: PASS (Task 1~4의 모든 테스트).

- [ ] **Step 8: Commit**

```bash
git add engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google
git commit -m "feat(engine): 구글 가사 조회에 최소 간격, 지수 백오프, 곡 단위 캐시를 둔다" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: 기다림 종료 판단 (PageWatch)

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/PageWatch.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/PageWatchTest.kt`

**Interfaces:**
- Consumes: `PageClassifier.blockOf` (Task 2)
- Produces:
  - `@Serializable internal class PageProbe(val url: String, val readyState: String, val hasCard: Boolean, val bodyText: String)`
  - `internal class PageWatch(settleTime: Duration, timeSource: TimeSource)` — `fun isDone(probe: PageProbe): Boolean`

- [ ] **Step 1: 실패하는 테스트 쓰기**

```kotlin
package com.xgetsongs.engine.lyrics.google

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TestTimeSource

class PageWatchTest {
    private val clock = TestTimeSource()
    private val watch = PageWatch(settleTime = 4.seconds, timeSource = clock)

    private fun probe(
        url: String = "https://www.google.com/search?q=x",
        state: String = "complete",
        card: Boolean = false,
        body: String = "",
    ) = PageProbe(url = url, readyState = state, hasCard = card, bodyText = body)

    @Test
    fun aVisibleCardEndsTheWaitAtOnce() {
        assertTrue(watch.isDone(probe(state = "loading", card = true)))
    }

    @Test
    fun aBlockPageEndsTheWaitAtOnce() {
        assertTrue(watch.isDone(probe(url = "https://www.google.com/sorry/index?continue=x", state = "loading")))
        assertTrue(watch.isDone(probe(state = "complete", body = "unusual traffic")))
    }

    @Test
    fun aPageThatIsStillLoadingIsNeverDone() {
        assertFalse(watch.isDone(probe(state = "loading")))
        clock += 1.minutes
        assertFalse(watch.isDone(probe(state = "loading")))
    }

    @Test
    fun aCompletePageWithoutACardIsDoneOnlyOnceItHasStayedQuiet() {
        assertFalse(watch.isDone(probe()))
        clock += 3.seconds
        assertFalse(watch.isDone(probe()))
        clock += 1.seconds
        assertTrue(watch.isDone(probe()))
    }

    @Test
    fun loadingAgainStartsTheQuietTimeOver() {
        assertFalse(watch.isDone(probe()))
        clock += 3.seconds
        assertFalse(watch.isDone(probe(state = "loading")))
        clock += 3.seconds
        assertFalse(watch.isDone(probe()))
        clock += 3.seconds
        assertFalse(watch.isDone(probe()))
        clock += 1.seconds
        assertTrue(watch.isDone(probe()))
    }

    @Test
    fun anotherAddressStartsTheQuietTimeOver() {
        assertFalse(watch.isDone(probe(url = "https://www.google.com/search?q=a")))
        clock += 3.seconds
        assertFalse(watch.isDone(probe(url = "https://www.google.com/search?q=b")))
        clock += 3.seconds
        assertFalse(watch.isDone(probe(url = "https://www.google.com/search?q=b")))
        clock += 1.seconds
        assertTrue(watch.isDone(probe(url = "https://www.google.com/search?q=b")))
    }
}
```

- [ ] **Step 2: 실패 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test --tests "com.xgetsongs.engine.lyrics.google.PageWatchTest"`
Expected: FAIL, `Unresolved reference: PageWatch`.

- [ ] **Step 3: 구현**

```kotlin
package com.xgetsongs.engine.lyrics.google

import kotlinx.serialization.Serializable
import kotlin.time.Duration
import kotlin.time.TimeMark
import kotlin.time.TimeSource

/** What one look at the page in the browser saw. [bodyText] is the first part of the visible text. */
@Serializable
internal class PageProbe(val url: String, val readyState: String, val hasCard: Boolean, val bodyText: String)

/**
 * Decides when to stop waiting for a page. Done when the page shows a lyrics card or a block page (it will not change),
 * or when it is complete and has stayed so, at the same address, for [settleTime]: Google loads its result page in two
 * steps, so a page that is complete for a moment is not yet the final one. Loading again or a new address starts that
 * quiet time over.
 */
internal class PageWatch(private val settleTime: Duration, private val timeSource: TimeSource) {
    private var watchedUrl: String? = null
    private var completeSince: TimeMark? = null

    fun isDone(probe: PageProbe): Boolean {
        if (probe.hasCard) return true
        if (PageClassifier.blockOf(probe.url, null, probe.bodyText) != null) return true
        if (probe.readyState != "complete") {
            completeSince = null
            return false
        }
        if (probe.url != watchedUrl) {
            watchedUrl = probe.url
            completeSince = null
        }
        val since = completeSince ?: timeSource.markNow().also { completeSince = it }
        return since.elapsedNow() >= settleTime
    }
}
```

- [ ] **Step 4: 통과 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test --tests "com.xgetsongs.engine.lyrics.google.PageWatchTest"`
Expected: PASS (6 tests).

- [ ] **Step 5: Commit**

```bash
git add engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/PageWatch.kt engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/PageWatchTest.kt
git commit -m "feat(engine): 페이지 대기를 끝낼 때를 가르는 PageWatch를 추가한다" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: 숨김 JavaFX 웹뷰 구현

JavaFX가 필요한 첫 Task다. 이 클래스는 FX 스레드와 네이티브 런타임이 있어야 돌므로 단위 테스트가 없고, Task 9의 통합 테스트가 검증한다. 여기서는 컴파일과 의존성 해석을 확인한다.

**Files:**
- Modify: `engine/build.gradle.kts`
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/FxWebViewBrowser.kt`

**Interfaces:**
- Consumes: `RenderingBrowser`, `RenderResult` (Task 4), `PageProbe`, `PageWatch` (Task 5), `GoogleLyricsSelectors.CARD_PRESENCE` (Task 1)
- Produces: `internal class FxWebViewBrowser(pollInterval: Duration = 500.milliseconds, settleTime: Duration = 4.seconds) : RenderingBrowser, AutoCloseable` — `close()`는 JavaFX 런타임을 내린다(한 번 내리면 같은 JVM에서 다시 못 올린다)

- [ ] **Step 1: 의존성 추가**

`engine/build.gradle.kts`의 `implementation(libs.jsoup)` 다음 줄에 추가한다.

```kotlin
    implementation(libs.javafx.web)
```

- [ ] **Step 2: Windows용 네이티브가 해석되는지 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:dependencies --configuration runtimeClasspath`
Expected: 출력에 `org.openjfx:javafx-web:21.0.12`, `javafx-controls`, `javafx-graphics`, `javafx-base`, `javafx-media`가 `21.0.12`로 보인다. 그다음 실제 jar에 `win` 네이티브가 들어오는지 확인한다.

Run: `.\gradlew.bat --no-daemon --console=plain :engine:build -x test` 후 PowerShell에서
`(Get-ChildItem -Recurse "$env:USERPROFILE\.gradle\caches\modules-2\files-2.1\org.openjfx" -Filter "javafx-web-21.0.12*.jar").Name`
Expected: `javafx-web-21.0.12-win.jar`가 보인다. **`-win` 없이 `javafx-web-21.0.12.jar`만 보이면** Gradle이 OS 변형을 고르지 못한 것이므로, `libs.versions.toml`의 `javafx-web` 줄을 `{ module = "org.openjfx:javafx-web", version.ref = "javafx" }` 대신 모듈 표기 뒤에 분류자를 붙이는 방식으로 바꾼다: `engine/build.gradle.kts`에서 `implementation("org.openjfx:javafx-web:${libs.versions.javafx.get()}:win")` 한 줄로 대체하고 다시 확인한다.

- [ ] **Step 3: 구현**

`FxWebViewBrowser.kt`:

```kotlin
package com.xgetsongs.engine.lyrics.google

import javafx.application.Platform
import javafx.scene.web.WebEngine
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.delay
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonPrimitive
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TimeSource

/**
 * A [RenderingBrowser] on a JavaFX [WebEngine] that is never shown: no window, no process of its own, the engine's own
 * User-Agent, and no cookie store (so nothing is kept between pages). The engine is made on first use and reused.
 *
 * Everything that touches the engine runs on the JavaFX thread; the waiting happens in a coroutine that looks at the
 * page every [pollInterval] with a short script (see [PROBE_SCRIPT]) and lets a [PageWatch] say when to stop. The
 * engine does not report HTTP statuses, so the page itself (its address and text) tells a block from a result.
 *
 * The JavaFX thread keeps the JVM alive until the runtime is shut down, so whoever owns this browser must [close] it
 * when the application ends. If JavaFX is missing or does not start, [render] answers [RenderResult.Unavailable].
 */
internal class FxWebViewBrowser(
    private val pollInterval: Duration = 500.milliseconds,
    private val settleTime: Duration = 4.seconds,
) : RenderingBrowser, AutoCloseable {
    private val json = Json { ignoreUnknownKeys = true }
    private val startLock = Any()

    @Volatile
    private var started = false

    /** Only touched on the JavaFX thread. */
    private var engine: WebEngine? = null

    override suspend fun render(url: String, timeout: Duration): RenderResult {
        try {
            startToolkit()
        } catch (e: Exception) {
            return RenderResult.Unavailable(e.javaClass.simpleName)
        } catch (e: LinkageError) {
            // JavaFX classes or natives missing from the class path.
            return RenderResult.Unavailable(e.javaClass.simpleName)
        }
        try {
            return withTimeoutOrNull(timeout) { load(url) } ?: RenderResult.TimedOut
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            return RenderResult.Unavailable(e.javaClass.simpleName)
        } finally {
            // Stop whatever is still loading and let go of the page, also when the caller was cancelled.
            withContext(NonCancellable) { runCatching { onFxThread { webEngine().load(BLANK) } } }
        }
    }

    private suspend fun load(url: String): RenderResult.Loaded {
        onFxThread { webEngine().load(url) }
        val watch = PageWatch(settleTime, TimeSource.Monotonic)
        while (true) {
            delay(pollInterval)
            val probe = onFxThread { probe() } ?: continue
            // Right after load() the engine may still show the page that was there before.
            if (probe.url == BLANK || !watch.isDone(probe)) continue
            return onFxThread { snapshot(probe) }
        }
    }

    /** Null when the script cannot run (no document yet) or does not answer what it should. */
    private fun probe(): PageProbe? = try {
        (webEngine().executeScript(PROBE_SCRIPT) as? String)?.let { json.decodeFromString<PageProbe>(it) }
    } catch (e: Exception) {
        null
    }

    private fun snapshot(probe: PageProbe): RenderResult.Loaded {
        val html = webEngine().executeScript("document.documentElement.outerHTML") as? String ?: ""
        return RenderResult.Loaded(url = probe.url, html = html, bodyText = probe.bodyText)
    }

    private fun webEngine(): WebEngine = engine ?: WebEngine().also { engine = it }

    /** Starts the JavaFX runtime once, without a window and without it ending on its own. */
    private fun startToolkit() {
        if (started) return
        synchronized(startLock) {
            if (started) return
            Platform.setImplicitExit(false)
            try {
                Platform.startup { }
            } catch (e: IllegalStateException) {
                // The runtime is running already (something else started it).
            }
            started = true
        }
    }

    /** Shuts the JavaFX runtime down (it cannot be started again in this JVM). Does nothing when it never started. */
    override fun close() {
        synchronized(startLock) {
            if (!started) return
            started = false
            Platform.exit()
        }
    }

    /** Runs [block] on the JavaFX thread and waits for its value. */
    private suspend fun <T> onFxThread(block: () -> T): T = suspendCancellableCoroutine { continuation ->
        Platform.runLater {
            try {
                continuation.resume(block())
            } catch (e: Throwable) {
                continuation.resumeWithException(e)
            }
        }
    }

    private companion object {
        const val BLANK = "about:blank"

        /** One look at the page: where it is, how far it is, whether a lyrics card shows, and the start of its text. */
        val PROBE_SCRIPT = """
            (function () {
              var text = document.body ? document.body.innerText : '';
              return JSON.stringify({
                url: location.href,
                readyState: document.readyState,
                hasCard: !!document.querySelector(${JsonPrimitive(GoogleLyricsSelectors.CARD_PRESENCE)}),
                bodyText: text.length > 4000 ? text.substring(0, 4000) : text
              });
            })()
        """.trimIndent()
    }
}
```

- [ ] **Step 4: 컴파일 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:compileKotlin`
Expected: `BUILD SUCCESSFUL`. 실패하면 오류의 JavaFX 심볼(`Platform`, `WebEngine`)이 컴파일 클래스패스에 없는 것이므로 Step 2의 분류자 방식으로 바꾸거나 `javafx-graphics`, `javafx-base`를 `implementation`에 같이 둔다.

- [ ] **Step 5: 기존 단위 테스트가 그대로인지 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test`
Expected: PASS (엔진의 모든 단위 테스트).

- [ ] **Step 6: Commit**

```bash
git add engine/build.gradle.kts engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/FxWebViewBrowser.kt
git commit -m "feat(engine): 창 없이 구글 페이지를 여는 JavaFX 웹뷰 브라우저를 추가한다" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: 제공자 어댑터와 폴백 제공자

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/GoogleLyricsProvider.kt`
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/FallbackLyricsProvider.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/GoogleLyricsProviderTest.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/lyrics/FallbackLyricsProviderTest.kt`

**Interfaces:**
- Consumes: `GoogleLyricsFetcher.fetch`, `GoogleLyricsResult` (Task 4), `FxWebViewBrowser` (Task 6), `LyricsProvider`, `LyricsQuery` (기존), 테스트의 `FakeBrowser`, `loadedCard()` 등 (Task 4), `FakeLyricsProvider` (기존 `testutil/Fakes.kt`)
- Produces:
  - `class GoogleLyricsProvider : LyricsProvider, AutoCloseable` — 공개 생성자 `GoogleLyricsProvider(log: GoogleLyricsLog = GoogleLyricsLog.None)`, 내부 생성자 `(fetcher: GoogleLyricsFetcher, onClose: () -> Unit = {})`
  - `class FallbackLyricsProvider(vararg providers: LyricsProvider) : LyricsProvider`

- [ ] **Step 1: 실패하는 테스트 쓰기**

`GoogleLyricsProviderTest.kt`:

```kotlin
package com.xgetsongs.engine.lyrics.google

import com.xgetsongs.engine.lyrics.LyricsQuery
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlin.time.Duration
import kotlin.time.TestTimeSource

class GoogleLyricsProviderTest {
    private val query = LyricsQuery(artist = "IU", title = "Love poem", album = null, durationSeconds = 258)

    private fun provider(browser: RenderingBrowser, onClose: () -> Unit = {}) =
        GoogleLyricsProvider(GoogleLyricsFetcher(browser, timeSource = TestTimeSource(), pause = {}), onClose)

    @Test
    fun aFoundCardGivesItsLyrics() = runTest {
        assertEquals("더미 하나\n더미 둘\n더미 셋", provider(FakeBrowser(loadedCard())).find(query))
    }

    @Test
    fun noCardGivesNull() = runTest {
        assertNull(provider(FakeBrowser(loadedWithoutCard())).find(query))
    }

    @Test
    fun aBlockPageGivesNull() = runTest {
        assertNull(provider(FakeBrowser(loadedSorry())).find(query))
    }

    @Test
    fun aBlankArtistOrTitleMakesNoRequest() = runTest {
        val browser = FakeBrowser()
        val provider = provider(browser)

        assertNull(provider.find(query.copy(artist = " ")))
        assertNull(provider.find(query.copy(title = "")))
        assertEquals(emptyList(), browser.urls)
    }

    @Test
    fun aCancellationGetsThrough() = runTest {
        val browser = object : RenderingBrowser {
            override suspend fun render(url: String, timeout: Duration): RenderResult = throw CancellationException("stop")
        }

        assertFailsWith<CancellationException> { provider(browser).find(query) }
    }

    @Test
    fun closingRunsTheCloseAction() {
        var closed = false
        provider(FakeBrowser()) { closed = true }.close()

        assertTrue(closed)
    }
}
```

`FallbackLyricsProviderTest.kt`:

```kotlin
package com.xgetsongs.engine.lyrics

import com.xgetsongs.engine.testutil.FakeLyricsProvider
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class FallbackLyricsProviderTest {
    private val query = LyricsQuery(artist = "IU", title = "Love poem", album = null, durationSeconds = null)

    @Test
    fun theFirstAnswerWinsAndLaterProvidersAreNotAsked() = runTest {
        val first = FakeLyricsProvider { "첫째 더미" }
        val second = FakeLyricsProvider { "둘째 더미" }

        assertEquals("첫째 더미", FallbackLyricsProvider(first, second).find(query))
        assertEquals(emptyList(), second.queries)
    }

    @Test
    fun aProviderWithNothingPassesTheQueryOn() = runTest {
        val first = FakeLyricsProvider { null }
        val second = FakeLyricsProvider { "둘째 더미" }

        assertEquals("둘째 더미", FallbackLyricsProvider(first, second).find(query))
        assertEquals(listOf(query), first.queries)
        assertEquals(listOf(query), second.queries)
    }

    @Test
    fun noAnswerAnywhereIsNull() = runTest {
        assertNull(FallbackLyricsProvider(FakeLyricsProvider(), FakeLyricsProvider()).find(query))
        assertNull(FallbackLyricsProvider().find(query))
    }
}
```

- [ ] **Step 2: 실패 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test --tests "com.xgetsongs.engine.lyrics.google.GoogleLyricsProviderTest" --tests "com.xgetsongs.engine.lyrics.FallbackLyricsProviderTest"`
Expected: FAIL, `Unresolved reference: GoogleLyricsProvider` / `FallbackLyricsProvider`.

- [ ] **Step 3: 구현**

`FallbackLyricsProvider.kt`:

```kotlin
package com.xgetsongs.engine.lyrics

/** Asks its [providers] one after the other and answers with the first lyrics found; null when none has any. */
class FallbackLyricsProvider(private vararg val providers: LyricsProvider) : LyricsProvider {
    override suspend fun find(query: LyricsQuery): String? {
        for (provider in providers) {
            provider.find(query)?.let { return it }
        }
        return null
    }
}
```

`GoogleLyricsProvider.kt`:

```kotlin
package com.xgetsongs.engine.lyrics.google

import com.xgetsongs.engine.lyrics.LyricsProvider
import com.xgetsongs.engine.lyrics.LyricsQuery
import kotlinx.coroutines.CancellationException

/**
 * Finds lyrics in the lyrics card of a Google search ("artist title lyrics"), read by a web view that is never shown
 * ([FxWebViewBrowser]). The last resort after the description and LRCLIB, and a fragile one: Google changes its pages,
 * and its terms of service all but forbid automated queries. This is a personal tool, so the lookup is polite (see
 * [GoogleLyricsFetcher]) and gives up for good at the first sign of a block. Lyrics are copyrighted works; a use beyond
 * personal use needs an official lyrics API (Musixmatch, say) instead of this.
 *
 * Never throws, except for a `CancellationException`; whatever goes wrong is "no lyrics". Close it when the application
 * ends: the web view's runtime would keep the JVM alive.
 */
class GoogleLyricsProvider internal constructor(
    private val fetcher: GoogleLyricsFetcher,
    private val onClose: () -> Unit = {},
) : LyricsProvider, AutoCloseable {
    /** The real thing; [log] gets one line per lookup (counts and result names, never a song or lyrics). */
    constructor(log: GoogleLyricsLog = GoogleLyricsLog.None) : this(FxWebViewBrowser(), log)

    private constructor(browser: FxWebViewBrowser, log: GoogleLyricsLog) : this(GoogleLyricsFetcher(browser, log = log), browser::close)

    override suspend fun find(query: LyricsQuery): String? {
        if (query.artist.isBlank() || query.title.isBlank()) return null
        return try {
            (fetcher.fetch(query.artist, query.title) as? GoogleLyricsResult.Found)?.lyrics
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            null
        }
    }

    override fun close() = onClose()
}
```

- [ ] **Step 4: 통과 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test --tests "com.xgetsongs.engine.lyrics.google.GoogleLyricsProviderTest" --tests "com.xgetsongs.engine.lyrics.FallbackLyricsProviderTest"`
Expected: PASS (9 tests).

- [ ] **Step 5: Commit**

```bash
git add engine/src/main/kotlin/com/xgetsongs/engine/lyrics engine/src/test/kotlin/com/xgetsongs/engine/lyrics
git commit -m "feat(engine): 구글 가사 제공자와 차례로 시도하는 폴백 제공자를 추가한다" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 8: 서버 연결과 종료

**Files:**
- Modify: `server/src/main/kotlin/com/xgetsongs/server/LocalServer.kt`

**Interfaces:**
- Consumes: `FallbackLyricsProvider(vararg LyricsProvider)`, `GoogleLyricsProvider(log: GoogleLyricsLog)` + `close()` (Task 7), `LrclibLyricsProvider()` (기존)
- Produces: `createServices`가 LRCLIB → 구글 순의 제공자를 `ItemDownloader`에 넘기고, 서버 범위(scope)가 끝날 때 `GoogleLyricsProvider.close()`를 부른다.

- [ ] **Step 1: import 추가**

`LocalServer.kt`의 import 목록에 알파벳 순으로 끼워 넣는다.

```kotlin
import com.xgetsongs.engine.lyrics.FallbackLyricsProvider
```
(`com.xgetsongs.engine.job.ItemDownloader` 다음, `LrclibLyricsProvider` 앞)

```kotlin
import com.xgetsongs.engine.lyrics.google.GoogleLyricsLog
import com.xgetsongs.engine.lyrics.google.GoogleLyricsProvider
```
(`LrclibLyricsProvider` 다음)

```kotlin
import kotlinx.coroutines.job
```
(`kotlinx.coroutines.cancel` 다음, `kotlinx.coroutines.launch` 앞)

- [ ] **Step 2: 로그 어댑터 추가**

`private fun binDirOf(appDataDir: Path): Path = ...` 바로 아래에 추가한다.

```kotlin

private val googleLyricsLogger = LoggerFactory.getLogger("com.xgetsongs.engine.lyrics.google")

/** Hands the lines of the Google lookup to the app's log (the lines hold counts and result names only). */
private val googleLyricsLog = object : GoogleLyricsLog {
    override fun info(message: String) = googleLyricsLogger.info("{}", message)

    override fun warn(message: String) = googleLyricsLogger.warn("{}", message)
}
```

- [ ] **Step 3: 제공자 연결**

`createServices`에서 아래 두 줄을

```kotlin
    // Lyrics are looked up on lrclib.net, but only for the jobs whose options allow it (JobOptions.searchLyricsOnline).
    val downloader = ItemDownloader(runner, locator, resolver, LrclibLyricsProvider())
```

다음으로 바꾼다.

```kotlin
    // Lyrics are looked up on lrclib.net and then, for what it does not have, in Google's lyrics card (a web view that is
    // never shown), but only for the jobs whose options allow it (JobOptions.searchLyricsOnline).
    val googleLyrics = GoogleLyricsProvider(googleLyricsLog)
    // The web view's runtime keeps the JVM alive until it is shut down, so it goes down with the server's scope.
    scope.coroutineContext.job.invokeOnCompletion { googleLyrics.close() }
    val downloader = ItemDownloader(runner, locator, resolver, FallbackLyricsProvider(LrclibLyricsProvider(), googleLyrics))
```

- [ ] **Step 4: 서버와 앱이 컴파일되고 기존 테스트가 통과하는지 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :server:test :app:compileKotlinDesktop`
Expected: `BUILD SUCCESSFUL`. 서버 테스트 중 `LocalServer.start(appDataDir)`를 부르는 것이 있으면 JavaFX가 시작되지 않은 채(`close()`는 아무 일도 안 한다) 통과해야 한다.

- [ ] **Step 5: Commit**

```bash
git add server/src/main/kotlin/com/xgetsongs/server/LocalServer.kt
git commit -m "feat(server): LRCLIB에 없는 가사는 구글 가사 카드에서 찾고 서버가 끝날 때 웹뷰를 내린다" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 9: 실제 스모크 테스트, 한 번의 실행, 문서

**Files:**
- Create: `engine/src/test/kotlin/com/xgetsongs/engine/integration/RealGoogleLyricsIntegrationTest.kt`
- Modify: `README.md`, `docs/superpowers/specs/2026-10-04-xgetsongs-design.md`, `shared/src/commonMain/kotlin/com/xgetsongs/shared/api/ApiModels.kt`

**Interfaces:**
- Consumes: `FxWebViewBrowser`, `GoogleLyricsFetcher`, `GoogleLyricsResult`, `GoogleLyricsLog` (internal이지만 같은 모듈의 테스트라 접근 가능)

- [ ] **Step 1: 스모크 테스트 쓰기**

```kotlin
package com.xgetsongs.engine.integration

import com.xgetsongs.engine.lyrics.google.FxWebViewBrowser
import com.xgetsongs.engine.lyrics.google.GoogleLyricsFetcher
import com.xgetsongs.engine.lyrics.google.GoogleLyricsLog
import com.xgetsongs.engine.lyrics.google.GoogleLyricsResult
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.jupiter.api.Assumptions.assumeTrue
import org.junit.jupiter.api.Tag
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertTrue
import kotlin.test.fail

/**
 * Asks the real Google, through the real hidden web view, for the lyrics card of one song, so it is excluded from `test`
 * and only runs through `integrationTest`. One request, no retry. Lyrics are copyrighted works: this test prints and
 * asserts STRUCTURE only (result name, counts of lines and paragraphs), never a word of what Google returns.
 *
 * If Google serves a block page (CAPTCHA, consent, 429), the web view times out or JavaFX is unavailable, the test is
 * skipped, not failed, and nothing tries again. If the page loads but the card is not read (`NoCard`,
 * `ExtractionFailed`), Google has probably changed its markup: fix `GoogleLyricsSelectors`.
 */
@Tag("integration")
class RealGoogleLyricsIntegrationTest {
    private val browser = FxWebViewBrowser()

    private val log = object : GoogleLyricsLog {
        override fun info(message: String) = println("log: $message")

        override fun warn(message: String) = println("log WARN: $message")
    }

    @AfterTest
    fun shutDown() = browser.close()

    @Test
    fun readsTheLyricsCardOfTheTestQuery(): Unit = runBlocking {
        withTimeout(120_000) {
            val result = GoogleLyricsFetcher(browser, log = log).fetch("RESCENE (리센느)", "LOVE ATTACK")

            println("google lyrics card: $result")
            when (result) {
                is GoogleLyricsResult.Found -> {
                    assertTrue(result.lineCount >= 10, "at least 10 lines expected, got ${result.lineCount}")
                    assertTrue(result.paragraphCount >= 2, "at least 2 paragraphs expected, got ${result.paragraphCount}")
                }
                GoogleLyricsResult.NoCard, GoogleLyricsResult.ExtractionFailed ->
                    fail("the page loaded but the lyrics card was not read ($result): has Google changed its markup? see GoogleLyricsSelectors")
                else -> assumeTrue(false, "Google did not serve the page, or the web view is unavailable: $result")
            }
        }
    }
}
```

- [ ] **Step 2: 통합 테스트 파일이 컴파일되고 일반 테스트에서는 빠지는지 확인**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:test`
Expected: PASS, 그리고 이 테스트는 실행되지 않는다(`excludeTags("integration")`). 출력에 `RealGoogleLyricsIntegrationTest`가 없어야 한다.

- [ ] **Step 3: 실제 구글에 딱 한 번 실행 (게이트)**

Run: `.\gradlew.bat --no-daemon --console=plain :engine:integrationTest --tests "*RealGoogleLyricsIntegrationTest*"`
Expected (통과): 출력에 `google lyrics card: Found(lines=80, paragraphs=13)` 같은 줄이 있고 테스트가 PASSED. 줄 수와 문단 수만 보고한다.

**결과별 행동 (재시도 금지):**
- `Found` → 계속 진행한다.
- `NoCard` / `ExtractionFailed`로 FAILED → 구글 마크업이 바뀐 것이다. 로그의 결과 이름만 보고하고 멈춘다(셀렉터 수정은 사용자와 정한다. 실제 DOM 재확인은 이 한 번의 요청을 쓰지 않고 내장 브라우저로 한다).
- SKIPPED(`Captcha` / `Consent` / `RateLimited` / `Timeout` / `BrowserUnavailable`) → 우회하지 않고 멈춘다. 어떤 결과 이름인지, 그리고 이미 오늘 구글 요청이 몇 번 나갔는지를 사용자에게 보고하고 다음 결정을 묻는다.

- [ ] **Step 4: README 갱신**

`README.md`에서 `## 필요한 것`의 위치를 확인한다(한 번만 나와야 한다).

Run: `Grep "^## 필요한 것" README.md`

`## 필요한 것` 앞에 아래 단락을 넣는다(Edit: `old_string` = `## 필요한 것`, `new_string` = 아래 + 빈 줄 + `## 필요한 것`).

```markdown
LRCLIB에도 없으면 구글 검색 결과의 가사 카드에서 가져옵니다(같은 옵션이 켜져 있을 때만). 앱 안에 창 없이 숨겨 둔 웹뷰(JavaFX WebEngine)로 `가수 제목 lyrics`를 검색하고, 가사 카드의 줄을 문단 단위로 읽습니다. 이때 아티스트와 제목이 구글로 보내집니다(웹뷰 기본 User-Agent 그대로, 쿠키는 저장하지 않음). 구글 호출은 곡당 1회(아티스트 이름에 괄호 병기가 있고 카드가 없으면 괄호를 뗀 이름으로 1회 더)이고 호출 사이에 10초 이상 간격을 두며, CAPTCHA·동의창·429·시간 초과가 나오면 우회하지 않고 5분, 10분, 20분…(최대 60분) 동안 구글 검색을 쉽니다. 같은 곡의 결과는 앱을 켜 두는 동안 기억해 다시 묻지 않습니다. 구글 카드는 곡의 버전(일본어판, 라이브 등)을 알려 주지 않아 다른 버전의 가사가 섞일 수 있습니다. 구글이 가사 카드의 마크업을 바꾸면 이 단계는 조용히 실패하고 로그에 `카드는 있으나 줄을 읽지 못함`이 남습니다. 구글 약관은 자동 질의를 사실상 금지하고 가사는 저작물이므로 개인 용도로만 쓰세요. 배포하거나 서비스로 넓힌다면 Musixmatch 같은 정식 API로 바꿔야 합니다.
```

같은 파일의 두 줄도 고친다.

- `.\gradlew.bat :engine:integrationTest  # 실제 yt-dlp, YouTube, LRCLIB를 쓰는 통합 테스트` → `실제 yt-dlp, YouTube, LRCLIB, 구글 가사 카드를 쓰는 통합 테스트`
- `engine/   ... 가사 검색(LRCLIB)` → `가사 검색(LRCLIB, 구글 가사 카드)`

- [ ] **Step 5: 스펙 6.4 갱신**

`docs/superpowers/specs/2026-10-04-xgetsongs-design.md`의 "가사 검색의 근거" 항목을 바꾼다(Edit).

old_string:
```
- **가사 검색의 근거:** 뮤직캣의 태그 편집기는 구글 검색 주소(`lyrics + 제목 + 가수`)를 브라우저로 열어 사용자가 복사하게 하는 방식이라 프로그램이 읽을 수 없다. 그래서 같은 검색어 구성(제목, 가수, 앨범)을 프로그램용 공개 API인 LRCLIB에 쓴다. 가사는 저작물이므로 개인 사용을 전제로 하며, 검색은 제목 등 네 가지 값을 외부 서비스로 보내므로 옵션으로 끌 수 있다.
```
new_string:
```
- **가사 검색의 근거:** 뮤직캣의 태그 편집기는 구글 검색 주소(`lyrics + 제목 + 가수`)를 브라우저로 열어 사용자가 복사하게 한다. 구글은 JavaScript를 실행하지 않는 클라이언트에게 결과를 주지 않으므로, 먼저 같은 검색어 구성(제목, 가수, 앨범)을 프로그램용 공개 API인 LRCLIB에 쓴다. LRCLIB에 없으면 앱 안의 숨은 웹뷰(JavaFX WebEngine)로 구글 가사 카드를 읽는다(설계: `2026-10-08-google-lyrics-card-design.md`). 가사는 저작물이므로 개인 사용을 전제로 하며, 검색은 제목 등의 값을 외부 서비스로 보내므로 옵션으로 끌 수 있다.
```

또 "값:" 항목의 끝에 한 문장을 덧붙인다. 먼저 그 항목의 끝부분을 본다.

Run: `Grep -n "^- \*\*값:\*\*" docs/superpowers/specs/2026-10-04-xgetsongs-design.md` 로 줄 번호를 확인하고, 그 줄의 마지막 100자를 읽어 그 끝 문장을 `old_string`으로 삼아, 같은 문장 뒤에 ` LRCLIB에도 없으면 구글 가사 카드에서 찾는다(곡당 요청 1~2회, 10초 간격, 차단되면 백오프; 설계 문서 참조).`를 붙여 Edit한다.

- [ ] **Step 6: `LyricsOutcome.ONLINE` 주석 갱신**

`shared/src/commonMain/kotlin/com/xgetsongs/shared/api/ApiModels.kt`에서

```
    /** Not in the description; the internet lookup (lrclib.net) found them. */
```
를
```
    /** Not in the description; the internet lookup (lrclib.net, or Google's lyrics card) found them. */
```
로 바꾼다.

- [ ] **Step 7: Commit**

```bash
git add engine/src/test/kotlin/com/xgetsongs/engine/integration/RealGoogleLyricsIntegrationTest.kt README.md docs/superpowers/specs/2026-10-04-xgetsongs-design.md shared/src/commonMain/kotlin/com/xgetsongs/shared/api/ApiModels.kt
git commit -m "문서: 구글 가사 카드 단계를 README와 스펙에 적고 실제 스모크 테스트를 추가한다" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 10: 전체 검증과 마무리

**Files:** 없음 (검증만)

- [ ] **Step 1: 모든 모듈의 단위 테스트**

Run: `.\gradlew.bat --no-daemon --console=plain test`
Expected: `BUILD SUCCESSFUL`, 실패 0. (통합 테스트는 포함되지 않는다.)

- [ ] **Step 2: 빠뜨린 것 점검**

- `git status`가 깨끗한지(작업 메모 txt는 그대로 untracked).
- `Grep`으로 소스·테스트·문서에 실제 가사가 들어가지 않았는지 확인: 테스트의 문자열은 `더미`로 시작하는 문장뿐이어야 한다.
- 스펙(`2026-10-08-google-lyrics-card-design.md`)의 각 항목에 해당하는 Task가 있는지 훑는다: 5.1(Task 3), 5.2(Task 5, 6), 5.3(Task 1), 5.4(Task 1), 5.5(Task 4), 5.6(Task 6, 8), 6(Task 1~9), 7(Task 9).

- [ ] **Step 3: 앱에서 눈으로 확인할 것을 사용자에게 알린다**

JavaFX 런타임이 Compose 창과 같은 프로세스에서 도는 구성이라, 사용자가 앱을 실행해 (a) 옵션 `가사가 없으면 인터넷에서 검색`을 켜고 설명란에 가사가 없는 곡을 받은 뒤 (b) 창의 크기·글자 배율이 달라지지 않는지, (c) 앱을 닫으면 프로세스가 남지 않고 끝나는지 확인해 달라고 요청한다. 로그(`%APPDATA%\xGetSongs\logs\xgetsongs.log`)에는 `구글 가사 카드: …` 줄이 남는다.

- [ ] **Step 4: push**

```bash
git push origin main
```

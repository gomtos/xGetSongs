# Qt 전환 Q3: 구글 가사 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Kotlin `engine/lyrics/google`(구글 검색의 가사 카드를 읽는 마지막 가사 공급자)와, 그 페이지를 띄우던 Rust 셸의 숨은 WebView2 창을 C++/Qt 한 프로세스로 옮긴다. Kotlin 시험 87개를 같은 이름으로 통과시키고, 구글 없는 숨은 창 점검(7단계의 G5)을 Qt Test로 다시 통과시키며, 사용자가 허락하면 실제 구글 확인(G7)을 한 번 한다.

**Architecture:** 가사 조회는 내려받기 작업의 일꾼 스레드에서 블로킹으로 돈다(`LyricsProvider::find`). WebView2는 GUI 스레드에서만 쓸 수 있으므로 둘 사이에 `BrowserChannel`을 그대로 둔다. 지금은 사이드카와 셸 사이의 HTTP 긴 대기였던 것이 같은 프로세스 안의 잠금 큐가 된다. 채널의 이쪽 끝(검색어, 기다림, 막힘 판별, 예의 규칙)은 `qt/core`(Qt Core만)에 두고, 저쪽 끝(숨은 WebView2 창)은 새 정적 라이브러리 `qt/webview`에 둔다. 카드에서 줄을 뽑는 일은 HTML 파서(Jsoup) 대신 **페이지 안에서 도는 스크립트 파일 하나**가 하고, 그 결과(JSON)를 C++이 정리한다.

**Tech Stack:** C++20(`std::stop_token`, `std::condition_variable_any`), Qt 6.11.3 Core·Gui·Test 정적 링크, WebView2 SDK(vcpkg `webview2`, 정적 로더), MSVC, CMake + Ninja, PowerShell 7, 대조용 JVM은 `server/build/sidecar-image`.

**Spec:** [설계 문서](../specs/2026-11-15-qt-rewrite-and-playlists-design.md) §2, §3, §4(Q3 행). 원래 설계: [구글 가사 카드](../specs/2026-10-08-google-lyrics-card-design.md), [7단계 보고서](../specs/2026-10-16-tauri-webview2-lyrics-report.md)(G5·G7, 점검이 잡은 결함, 최종 리뷰). 인계: [Q0 보고서](../specs/2026-11-15-qt-spike-report.md)의 숨긴 WebView2 절(`HiddenWebView`는 버리는 코드), [Q2a 보고서](../specs/2026-11-15-qt-engine-logic-report.md) "Q3에 넘길 것", [Q2d 보고서](../specs/2026-11-21-qt-engine-job-report.md) "Q3·Q4에 넘길 것". 원본: `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/*.kt`, 시험 `engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/*.kt`, `sidecar-host/src/browser.rs`, `shell/src-tauri/src/browser_window.rs`, `shell/src-tauri/src/browser_check.rs`, `shell/tools/browser-check/fixture.mjs`.

## 이 계획에서 작성자가 정한 것 (사용자가 바꿀 수 있다)

1. **`BrowserChannel`을 남긴다.** 두 프로세스를 잇던 장치지만, 일꾼 스레드와 GUI 스레드를 잇는 데 같은 모양이 필요하다. 달라지는 것: HTTP와 긴 대기가 없어지고, "에이전트가 40초 안에 연락했는가" 대신 **"에이전트가 붙어 있는가"**(`attach`/`detach`)를 본다. 명령 ID는 UUID 대신 차례 번호다.
2. **옮기지 않는 Kotlin 시험 1개.** `BrowserChannelTest.theAgentCountsAsPresentForTheFreshnessTimeOnly`는 신선도 시간이 없어져 뜻이 없다. 그 자리에 `theAgentCountsAsPresentOnlyWhileAttached`를 더한다. 나머지 86개는 같은 이름으로 옮긴다. (Q1에서 `ApiModels` 시험을 옮기지 않기로 한 것과 같은 종류의 결정이라 승인이 필요하다.)
3. **옮기지 않는 Rust·서버 시험.** HTTP 계약과 긴 대기에 관한 것은 없어진다: `sidecar-host/src/browser.rs`의 계약 파일 2개와 루프 2개(`the_loop_ends…`, `a_lost_connection…`), `server`의 `BrowserRoutesTest`·`BrowserProtocolTest`·`BrowserChainTest`. `browser.rs`의 나머지 16개(주소 허용 5, 스크립트 결과 풀기 4, 명령 처리 5, 쉬는 창 닫기 2)는 낙타 표기 이름으로 옮긴다. `BrowserChainTest`의 자리는 Task 6의 사슬 시험이 잇는다.
4. **WebView2 런타임 확인 상자는 옮기지 않는다**(`sidecar-host/src/webview2.rs`와 그 시험 7개). Tauri 판은 주 창이 WebView2라서 런타임이 없으면 앱이 못 떴다. Qt 판의 주 창은 Widgets라서 런타임이 없어도 뜬다. 런타임이 없으면 구글 가사만 "웹뷰를 쓸 수 없음"으로 끝난다(그 곡은 가사 없음, 쉼 없음). 화면에 알릴지는 Q4에서 정한다.
5. **카드 읽기는 스크립트 + C++ 정리**(설계 §4 Q3 행). 스크립트 파일은 `qt/core/lyrics/google/lyrics_card.js` 하나다. 스크립트는 판단하지 않고 본 것만 돌려준다(카드 범위가 있었는지, 줄마다 부모 번호와 글자). 줄 다듬기, 문단 묶기, `tidy`, 줄·문단 수 세기는 C++(`parseCard`)이 한다. 그래서 `RenderResult`의 `html` 자리에 스크립트 결과(`card`)가 온다. `DrivenBrowser` 시험 가운데 이름에 `Html`이 든 셋은 이름을 그대로 두고 "카드 읽기 결과"로 읽는다.
6. **Jsoup의 글자 처리는 따라 한다.** Kotlin은 `Element.text()`(공백 줄이기, `<br>` 처리)를 썼다. Jsoup은 MIT 라이선스라서 소스를 봐도 된다(JDK·kotlinx와 다르다). 스크립트와 `parseCard`가 같은 결과를 내는지는 고정 HTML 시험과 대조로 본다.
7. **코루틴 대신 블로킹 + 멈춤 토큰**(Q2c·Q2d와 같다). 취소는 예외가 아니라 결과다: `fetch`는 멈추면 빈 값을 돌려주고, `find`도 빈 값이다. 시간은 주입한 시계와 주입한 `pause`로 흐른다(Kotlin 시험의 가상 시간 자리). 곡 사이 10초 대기, 차례 기다리기, 채널의 답 기다리기는 모두 멈춤 요청에 곧 깨어난다.
8. **한 번의 조회 시간 한도(20초)는 걸음 사이에서만 본다.** Kotlin은 `withTimeoutOrNull`이 도는 호출을 끊는다. C++은 채널 호출마다 남은 시간을 한도로 넘긴다(채널의 기본 한도 15초보다 짧으면 그것이 쓰인다). 결과는 같다(`Timeout`).
9. **캐시 키의 시그마.** 사용자가 가사 매칭에서 σ와 ς를 같은 글자로 보기로 했으므로 캐시 키도 그렇게 한다(`lowercase` 뒤 ς를 σ로). 영향은 "같은 곡의 두 표기가 캐시를 나눠 쓰는가"뿐이다. 대조에서는 Σ·σ·ς가 든 입력의 키를 양쪽 모두 접어서 비교한다.
10. **숨은 창의 규칙은 지금 것 그대로**(`browser_window.rs`): 화면에 보이지 않음, 작업 표시줄에 없음, InPrivate(디스크에 남기지 않음), 새 창 거절, 다운로드 거절, `https://`와 자기 PC로 가는 `http://`와 `about:blank`만 이동, 스크립트 답 한도 5초, 실패한 창은 닫고 다음 `load`가 새로 만듦, 60초 동안 명령이 없으면 닫음, User-Agent는 WebView2 기본값. 더하는 것(Q0 보고서의 지적): 권한 요청 거절, 브라우저 프로세스가 죽으면(`ProcessFailed`) 창을 닫고 실패로 답함.
11. **WebView2 사용자 데이터 폴더.** 만드는 쪽이 넘긴다. 앱의 기본값은 `%LOCALAPPDATA%\xGetSongs\qt-webview2`로 제안한다(Tauri 판의 폴더와 섞지 않는다). 실제 연결은 Q4의 일이고, 이 계획의 시험은 `QTemporaryDir`만 쓴다.
12. **검색 주소의 앞부분을 주입할 수 있다**(기본 `https://www.google.com/search`). 구글 없는 사슬 시험이 자기 PC의 고정 페이지 서버를 가리키게 하려는 것이다(Kotlin·셸의 `XGS_CHECK_LYRICS_URL` 자리). 환경 변수로는 받지 않는다.
13. **고정 페이지 서버는 C++로 쓴다**(`QTcpServer`, 시험 안). `fixture.mjs`(Node)를 대신한다. 설계의 "Node 의존은 남기지 않는다".
14. **WebView2를 띄우는 시험은 따로 묶는다**(ctest 라벨 `webview`). 런타임이 없으면 건너뛴다. 이 시험들은 창을 화면에 띄우지 않지만 `msedgewebview2.exe` 프로세스를 만든다. 우리 코드가 보내는 요청은 `127.0.0.1`뿐이다. WebView2가 스스로 하는 배경 통신을 줄이려고 시험에서만 브라우저 인자 `--disable-background-networking --disable-component-update`를 준다(제품은 기본값). 사용자가 실행 중인 xGetSongs(Tauri 판)와는 사용자 데이터 폴더가 달라 브라우저 프로세스를 나눠 쓰지 않는다.
15. **실제 구글 확인(G7)은 이 계획의 실행에서 돌리지 않는다.** `tst_real_google`은 `XGS_INTEGRATION=1`과 `XGS_INTEGRATION_GOOGLE=1`이 둘 다 있을 때만 돈다. 곡 하나(가수 `RESCENE`, 제목 `LOVE ATTACK`: 괄호 없는 가수라 **요청 1회**)이고, 출력은 결과 이름과 줄·문단 수뿐이다. 사용자가 허락하면 관문에서 한 번 돌린다.
16. **이 계획에 넣지 않는 것.** 공급자를 앱에 잇기(설명란 → LRCLIB → 구글을 하나로 묶어 재생목록 전체가 나눠 쓰기), 주 창을 닫을 때 숨은 창이 앱을 붙잡지 않게 하기, 로그 파일에 쓰기(여기서는 `GoogleLyricsLog` 인터페이스까지), `qt/spike` 지우기. 앞의 둘은 Q4, 로그는 Q5다.

## Global Constraints

- 정답은 Kotlin(과 지금 셸의 Rust)이다. `shared/`, `engine/`, `server/`, `diagnostics/`, `shell/`, `sidecar-host/`는 읽기만 한다. C++ 결과가 다르면 C++을 고친다. 원본이 버그로 보여도 고치지 않고 보고서에 적는다. 예외는 위 "작성자가 정한 것"과 앞 단계 보고서의 "Kotlin과 다르게 한 것"뿐이다.
- **JDK(OpenJDK)와 `kotlinx.coroutines`·`kotlinx.serialization`의 소스를 보지 않고, 기억에서 옮겨 쓰지 않는다.** `java.text.Normalizer`, `URLEncoder`, `java.util.regex`의 동작은 공개 문서와 대조 도구가 관찰한 답으로만 정한다. Jsoup(MIT), Tauri·wry(MIT/Apache-2.0), WebView2 SDK 문서는 봐도 된다.
- 바꾸는 곳은 `qt/`(단 `qt/spike/` 제외)와 Task 7의 보고서뿐이다.
- `xgscore`는 Qt Core, Qt Network, Win32 API만 쓴다. WebView2와 Qt Gui는 `qt/webview`(`xgswebview`)에만 있다. 네임스페이스 `xgs`(여기서는 `xgs::lyrics::google`, `xgs::webview`), C++20, `/W4 /WX /permissive- /utf-8`. 금지 호출 목록은 그대로다(`qt/tools/forbidden-calls.ps1`). 대소문자 접기, 공백 판정, NFKC는 `xgs::kt`의 것을 쓴다.
- **구글에 요청을 보내는 것은 `tst_real_google`뿐이고, 그것은 사용자의 허락 없이 돌리지 않는다.** 단위 테스트와 `ctest`(라벨 `webview` 포함)는 `127.0.0.1` 밖으로 아무것도 보내지 않는다. CAPTCHA·동의창·429를 우회하는 코드를 쓰지 않는다(쉬기만 한다).
- **가사는 저작물이다.** 로그·시험 출력·보고서·커밋에 가사나 실제 서비스가 돌려준 곡 이름을 남기지 않는다. `GoogleLyricsResult`를 글로 바꾸는 함수는 가사를 넣지 않는다. 시험의 글은 Kotlin 시험처럼 지어낸 것(`더미 하나`)만 쓴다.
- WebView2 호출은 모두 그 창을 만든 스레드(GUI 스레드)에서 한다. 일꾼 스레드는 `BrowserChannel`만 만진다. 채널·에이전트·창은 어느 순서로 없어져도 기다리던 호출이 `closed`로 끝나고, 없어진 뒤에 불리는 콜백이 없다.
- 어떤 시험도 창을 화면에 보이지 않는다. `webview` 묶음은 "보이는 최상위 창 0개"를 스스로 확인한다. 이 묶음을 처음 돌리기 전에 사용자에게 알린다(숨은 브라우저 프로세스가 뜬다).
- 테스트는 Qt Test. Kotlin의 `@Test` 하나가 같은 이름의 슬롯 하나가 되고 단언을 하나도 빼지 않는다. 기대값·통계 문턱·생성기의 강도를 바꿔 통과시키지 않는다. 기다림은 사건으로 하고, 시간에 기대는 곳은 넉넉한 한도와 이유를 주석으로 적는다. `emit`은 Qt 매크로라 이름으로 쓰지 않고, moc가 읽는 파일에 raw 문자열 리터럴을 쓰지 않는다.
- 대조는 종류마다 1만 건 × seed 셋(20261115, 1, 987654321)과 10만 건 한 번(seed 424242)을 돈다(`-DebugTool` 한 번 포함). 이식 Task마다 변이가 대조나 시험에 잡히는지 본다.
- 관문은 릴리스와 디버그 두 빌드에서 `ctest`를 돈다. Gradle은 돌리지 않는다. 실행 중인 xGetSongs와 `%APPDATA%\xGetSongs`, `%LOCALAPPDATA%`의 앱 폴더를 건드리지 않는다. 임시 파일과 WebView2 사용자 데이터 폴더는 `QTemporaryDir` 아래에만 만든다. `C:\dev\vcpkg`와 `build/qt*/vcpkg_installed`를 지우지 않는다.
- 커밋은 `main`에 직접, 메시지는 한국어, 코드와 주석은 영어, 끝에 실행하는 세션의 Co-Authored-By 줄. push하지 않는다.

## Review Focus

Kotlin 시험 87개가 다루지 않지만 실제로 만날 것이다.

1. **재생목록 수백 곡이 한꺼번에 구글 차례를 기다리는 중의 취소.** 조회는 한 번에 하나, 10초 간격이라 일꾼들이 줄을 선다. 취소하면 줄 선 호출, 10초를 기다리던 호출, 채널의 답을 기다리던 호출이 모두 곧(1초 안에) 빈 값으로 돌아오고, 쉼(쿨다운)은 시작되지 않는다 → Task 4 `stoppingEndsEveryWaitingLookupPromptly`.
2. **조회 중에 앱이 닫힌다.** 일꾼이 답을 기다리는 동안 GUI 쪽이 에이전트·창·채널을 없앤다. 교착 없이 `closed`로 끝나고 뒤늦은 콜백이 없다 → Task 3 `destroyingTheAgentWhileACallWaitsFailsItWithClosed`, Task 6 `closingTheBrowserDuringALookupEndsItAtOnce`.
3. **GUI 스레드가 바쁘거나 페이지가 멈췄다.** 끝나지 않는 루프의 페이지는 5초 뒤 `script timed out`으로 창이 닫히고 다음 조회는 새 창에서 된다. 이것은 구글의 시간 초과로 세지 않는다(쉼 없음, 기억하지 않음) → Task 6 `aHungPageIsReplacedAndIsNotHeldAgainstGoogle`.
4. **런타임이 없거나 브라우저 프로세스가 죽는다.** 창을 못 만들면 `BrowserUnavailable`(쉼 없음)이고 상자가 뜨지 않는다. 도중에 죽으면 그 조회만 실패하고 다음 `load`가 새로 만든다 → Task 6 `withoutARuntimeTheLookupIsUnavailableAndShowsNothing`, `aBrowserProcessThatDiesIsReplaced`.
5. **주소에 들어가는 이상한 글자.** 가수·제목에 `&`, `#`, `%`, `+`, `?`, 이모지, 짝 없는 서로게이트, 제어 문자가 있어도 주소가 Kotlin과 같고 `allowedUrl`을 통과하며 구글 밖으로 가지 않는다 → Task 1 `oddCharactersStayInsideTheQueryOfTheAddress`와 Task 5의 대조.
6. **카드가 모양만 있고 내용이 이상하다.** 줄이 수천 개, 한 줄이 수 MB, 줄 안에 `<script>`·중첩 태그, 스크립트 결과가 JSON이 아님·형이 틀림·`null` → 죽지 않고 `ExtractionFailed`나 `BrowserUnavailable("no-html")` → Task 2 `aCardResultOfTheWrongShapeIsNotACrash`, `aHugeCardIsReadWithinALimit`.

---

## File Structure

```
qt/core/lyrics/google/
  selectors.h                      GoogleLyricsSelectors의 상수
  search_query.h/.cpp              GoogleSearchRequest, requests, cacheKey
  lyrics_result.h/.cpp             GoogleLyricsResult, GoogleLyricsLog
  page_classifier.h/.cpp           blockOf
  page_watch.h/.cpp                PageProbe, probeFromJson, PageWatch
  lyrics_card.js                   카드에서 줄을 뽑는 스크립트(파일 하나)
  page_scripts.h/.cpp              cardScript(), probeScript() (빌드 때 파일을 넣는다)
  lyrics_card.h/.cpp               parseCard
  page_driver.h                    PageDriver, DriverAnswer, Wait
  rendering_browser.h              RenderingBrowser, RenderResult
  driven_browser.h/.cpp            DrivenBrowser
  browser_channel.h/.cpp           BrowserChannel, BrowserCommand, BrowserAnswer
  browser_agent.h/.cpp             Page, BrowserAgent, allowedUrl, decodeScriptResult
  lyrics_fetcher.h/.cpp            GoogleLyricsFetcher
  google_lyrics_provider.h/.cpp    GoogleLyricsProvider
qt/webview/
  CMakeLists.txt                   xgswebview (Qt6::Gui, WebView2 정적 로더, xgscore)
  hidden_web_view.h/.cpp           HiddenWebView: Page의 WebView2 구현
  lyrics_browser.h/.cpp            LyricsBrowser: 채널에 붙는 QObject(깨우기, 쉬는 창 타이머)
  runtime.h/.cpp                   runtimeVersion(): 런타임이 있는지
qt/tests/
  tst_google_search_query.cpp, tst_page_classifier.cpp, tst_page_watch.cpp,
  tst_lyrics_card.cpp, tst_driven_browser.cpp, tst_browser_channel.cpp,
  tst_browser_agent.cpp, tst_google_lyrics_fetcher.cpp, tst_google_lyrics_provider.cpp
  support/google_fakes.h           GoogleTestSupport.kt의 자리(가짜 브라우저, 카드 결과 만들기)
  webview/CMakeLists.txt           라벨 webview
  webview/fixture_server.h/.cpp    고정 페이지 서버(QTcpServer)
  webview/tst_lyrics_card_parser.cpp, tst_hidden_web_view.cpp, tst_lyrics_browser_chain.cpp
  integration/tst_real_google.cpp
qt/tools/differential/             종류 gq, pc, pw, W gf
```

---

### Task 1: 검색어, 막힘 판별, 기다림 판단

**Files:**
- Create: `qt/core/lyrics/google/{selectors.h, search_query.h, search_query.cpp, lyrics_result.h, lyrics_result.cpp, page_classifier.h, page_classifier.cpp, page_watch.h, page_watch.cpp}`
- Test: `qt/tests/{tst_google_search_query.cpp, tst_page_classifier.cpp, tst_page_watch.cpp}`
- Modify: `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/tools/test-classes.json`, `qt/tests/tst_tool_locator.cpp`

**Interfaces:**
- Consumes: `xgs::kt`(`lowercase`, `nfkc`, `trim`, `isWhitespace`, `isLetter`), `xgs::json`.
- Produces (`namespace xgs::lyrics::google`):
  - `selectors.h`: `CardScopes`(`div[data-lyricid]`, `[jsname=WbKHeb]`), `Line`(`span[jsname=YS01Ge]`), `CardPresence`(셋을 `, `로 이음), `SorryPath`(`/sorry/`), `ConsentHost`(`consent.google`), `UnusualTrafficTexts`(`unusual traffic`, `비정상적인 트래픽`), `TooManyRequestsTexts`(`too many requests`), `BlockPageMaxText = 2000`.
  - `inline constexpr QStringView DefaultSearchBase = u"https://www.google.com/search";`
  - `struct GoogleSearchRequest { QString text; QString language; QString url(QStringView searchBase = DefaultSearchBase) const; };` 주소는 `{base}?q={UTF-8 퍼센트 인코딩, 공백은 %20}&hl={language}`.
  - `std::vector<GoogleSearchRequest> requests(const QString& artist, const QString& title);`
  - `QString cacheKey(const GoogleSearchRequest& request);` NFKC, 소문자, ς→σ(결정 9).
  - `struct GoogleLyricsResult { enum class Kind { Found, NoCard, ExtractionFailed, Captcha, Consent, RateLimited, Timeout, BrowserUnavailable, CoolingDown }; Kind kind; QString lyrics; int lineCount = 0; int paragraphCount = 0; QString reason; QString toString() const; };` `toString`은 `Found(lines=N, paragraphs=M)`, `BrowserUnavailable(reason)`, 나머지는 이름.
  - `class GoogleLyricsLog { public: virtual ~GoogleLyricsLog() = default; virtual void info(const QString&) = 0; virtual void warn(const QString&) = 0; };`
  - `std::optional<GoogleLyricsResult::Kind> blockOf(const QString& url, std::optional<int> statusCode, const QString& bodyText);`
  - `using Clock = std::function<std::chrono::nanoseconds()>;`(빈 값이면 `steady_clock`)
  - `struct PageProbe { QString url; QString readyState; bool hasCard = false; QString bodyText; };`
  - `std::optional<PageProbe> probeFromJson(const QString& text);` 네 필드가 모두 맞는 형으로 있어야 한다(모르는 키는 무시).
  - `class PageWatch { public: PageWatch(std::chrono::nanoseconds settleTime, Clock clock); bool isDone(const PageProbe& probe); };`

- [ ] **Step 1: `tst_tool_locator`의 임시 폴더 누수 고치기**(Q2d 인계). `-functions`로 불려도 `%TEMP%\qt_temp-*`가 남지 않게 `QTemporaryDir`를 `initTestCase`에서 만든다. 확인: `build/qt/tst_tool_locator.exe -functions` 앞뒤로 `%TEMP%\qt_temp-*` 수가 같다. 예전에 쌓인 폴더는 지우지 않는다(보고서에 수만 적는다).
- [ ] **Step 2: 실패하는 시험 쓰기.** `GoogleSearchQueryTest` 14개, `PageClassifierTest` 8개, `PageWatchTest` 6개를 같은 이름, 같은 입력, 같은 기대값으로 옮긴다. `PageWatchTest`의 가상 시간은 시험이 직접 움직이는 `Clock`이다. 더하는 시험:
  - `oddCharactersStayInsideTheQueryOfTheAddress`: 가수 `A&B #1`, 제목 `100% +? \u{1F600}`의 주소가 `https://www.google.com/search?q=`로 시작하고 `&hl=en`으로 끝나며 그 사이에 `&`, `#`, `+`, `?`, 공백이 없다.
  - `aSearchBaseOfTheCheckIsUsedAsGiven`: `url(u"http://127.0.0.1:8123/search")`가 그 앞부분으로 시작한다.
  - `theCacheKeyTreatsBothSigmasAsOneLetter`: `ΟΔΟΣ x`와 `οδοσ x`의 키가 같다.
  - `aProbeOfTheWrongShapeIsNoProbe`: `null`, 숫자, 배열, 필드 빠짐, `hasCard`가 문자열 → 빈 값.
- [ ] **Step 3: 실패 확인.** `cmake --build build/qt` → 컴파일 오류(헤더 없음).
- [ ] **Step 4: 구현.** 정규식 다섯은 Kotlin의 것을 그대로 옮긴다. `IGNORE_CASE`는 `feat|ft|prod|narr`에만 걸리고 그 글자들에는 유니코드 접기로 ASCII에 닿는 다른 글자가 없다(Q2a 인계의 `s`·`k`·`i`가 없다). `\s`, `\b`, `\p{L}`의 뜻은 Java의 것이어야 하고 Task 5의 대조가 확인한다. 한글 판정은 Kotlin의 세 범위 그대로다.
- [ ] **Step 5: 통과 확인.** `ctest --test-dir build/qt -R "tst_(google_search_query|page_classifier|page_watch|tool_locator)"` → 모두 통과. 디버그 빌드도 같다.
- [ ] **Step 6: 이름 대조와 금지 호출.** `test-classes.json`에 세 클래스를 더하고 `pwsh qt/tools/test-names.ps1`, `pwsh qt/tools/forbidden-calls.ps1` 통과.
- [ ] **Step 7: 커밋.** `Qt: 구글 가사의 검색어·막힘 판별·기다림 판단`

### Task 2: 카드 스크립트, 카드 정리, 구동 브라우저

**Files:**
- Create: `qt/core/lyrics/google/{lyrics_card.js, page_scripts.h, page_scripts.cpp, lyrics_card.h, lyrics_card.cpp, page_driver.h, rendering_browser.h, driven_browser.h, driven_browser.cpp}`, `qt/tests/support/google_fakes.h`
- Test: `qt/tests/{tst_lyrics_card.cpp, tst_driven_browser.cpp}`
- Modify: `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/tools/test-classes.json`

**Interfaces:**
- Consumes: Task 1의 것, `xgs::lyrics::tidy`.
- Produces:
  - `QString cardScript();` `lyrics_card.js`의 글자 그대로. `QString probeScript();` Kotlin의 `PROBE_SCRIPT`와 같은 일을 한다(`url`, `readyState`, `hasCard`, `bodyText`는 앞 4000자).
  - 카드 스크립트의 결과(JSON 글): `{"scope": true|false, "lines": [{"p": <부모 번호>, "t": "<줄의 글자>"}, …]}`. `scope`는 `CardScopes` 가운데 처음 맞은 것이 있었는지, `lines`는 그 범위(없으면 문서 전체) 안의 `Line`을 문서 순서로, `p`는 같은 부모 요소면 같은 수, `t`는 그 요소의 글자(다듬지 않음).
  - `GoogleLyricsResult parseCard(const QString& scriptResult);` 줄이 없으면 `scope`에 따라 `NoCard`/`ExtractionFailed`. 줄마다 Jsoup `text()`처럼 공백을 줄이고 `trim`, 빈 줄은 버린다. 잇닿은 같은 부모의 줄이 한 문단. `tidy`가 빈 값이면 `ExtractionFailed`. 형이 틀린 결과는 `ExtractionFailed`.
  - `inline constexpr int CardMaxLines = 5000; inline constexpr qsizetype CardMaxUnits = 1'000'000;` 넘으면 `ExtractionFailed`(스크립트도 같은 한도에서 멈춘다).
  - `struct Wait { std::stop_token stop; std::chrono::nanoseconds limit; };`
  - `struct DriverAnswer { enum class Status { Ok, Unavailable, Stopped }; Status status; std::optional<QString> value; QString reason; };`
  - `class PageDriver { public: virtual ~PageDriver() = default; virtual DriverAnswer load(const QString& url, const Wait& wait) = 0; virtual DriverAnswer evaluate(const QString& script, const Wait& wait) = 0; virtual DriverAnswer reset(const Wait& wait) = 0; };` 던지지 않는다.
  - `struct RenderResult { enum class Kind { Loaded, TimedOut, Unavailable, Stopped }; Kind kind; QString url; QString card; QString bodyText; QString reason; };`
  - `class RenderingBrowser { public: virtual ~RenderingBrowser() = default; virtual RenderResult render(const QString& url, std::chrono::nanoseconds timeout, std::stop_token stop) = 0; };`
  - `using Pause = std::function<bool(std::chrono::nanoseconds, std::stop_token)>;` 멈춰서 끝났으면 `false`.
  - `struct DrivenBrowserOptions { std::chrono::nanoseconds pollInterval{500ms}; std::chrono::nanoseconds settleTime{4s}; Clock clock; Pause pause; };`
  - `class DrivenBrowser final : public RenderingBrowser { public: explicit DrivenBrowser(PageDriver& driver, DrivenBrowserOptions options = {}); … };`
  - `google_fakes.h`: `cardResult(문단들)`(스크립트가 줄 JSON), `loadedCard()`, `loadedWithoutCard()`, `loadedBrokenCard()`, `loadedSorry()`, `FakeBrowser`, `FakeDriver`(Kotlin 시험 지원과 같은 이름·같은 내용).

- [ ] **Step 1: 실패하는 시험 쓰기.** `DrivenBrowserTest` 11개를 옮긴다(가짜 `PageDriver`, 시험이 움직이는 시계, 시계를 밀어 주는 `pause`). `aCancellationGetsThrough`와 `theBrowserIsResetAfterEveryRenderEvenWhenItWasCancelled`는 멈춤 토큰으로 옮기고 `Stopped`를 본다. `tst_lyrics_card`(더하는 시험, WebView2 없음):
  - `LyricsCardParserTest`의 고정 HTML 10개 각각에 해당하는 스크립트 결과(손으로 쓴 JSON)를 `parseCard`에 넣어 Kotlin 시험과 같은 기대값을 본다(슬롯 이름은 Kotlin 이름 뒤에 `FromAScriptResult`).
  - `aFoundResultNeverPrintsTheLyrics`(Kotlin 이름 그대로; `LyricsCardParserTest`의 11번째).
  - `aCardResultOfTheWrongShapeIsNotACrash`: 빈 글, `null`, `[]`, `{"scope":"yes"}`, `lines`가 객체, `t`가 숫자, `p`가 없음 → 모두 `ExtractionFailed`.
  - `aHugeCardIsReadWithinALimit`: 줄 5001개, 한 줄 1,000,001자 → `ExtractionFailed`, 1초 안.
  - `theEmbeddedScriptIsTheFile`: `cardScript()`가 저장소의 `lyrics_card.js`와 글자 그대로 같다(경로는 CMake가 정의로 넘긴다).
- [ ] **Step 2: 실패 확인.** 컴파일 오류.
- [ ] **Step 3: 구현.** `render`: `load` → `pollInterval`마다 `probe` → `about:blank`이거나 `PageWatch`가 아직이면 계속 → 끝나면 `cardScript()`를 돌려 `Loaded`. 걸음마다 한도와 멈춤을 본다. 드라이버가 `Unavailable`이면 그 이유로 끝난다. 카드 스크립트가 아무것도 주지 않으면 `Unavailable("no-html")`. 어떻게 끝나든 `reset`을 한 번 부른다(멈춘 뒤에도, 멈춤 토큰 없이 짧은 한도로). 스크립트 파일은 빌드 때 넣는다(qrc든 생성한 헤더든 만드는 쪽이 고른다; moc가 읽는 파일에는 넣지 않는다).
- [ ] **Step 4: 통과 확인.** 두 빌드에서 `ctest -R "tst_(lyrics_card|driven_browser)"`.
- [ ] **Step 5: 변이.** (a) `reset`을 멈춘 경우에 빼기, (b) `about:blank` 무시를 빼기, (c) `no-html`을 빈 카드로 바꾸기 → 각각 시험이 잡는지 보고 되돌린다.
- [ ] **Step 6: 이름 대조, 금지 호출, 커밋.** `Qt: 구글 가사 카드 스크립트와 구동 브라우저`

### Task 3: 채널과 에이전트

**Files:**
- Create: `qt/core/lyrics/google/{browser_channel.h, browser_channel.cpp, browser_agent.h, browser_agent.cpp}`
- Test: `qt/tests/{tst_browser_channel.cpp, tst_browser_agent.cpp}`
- Modify: `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/tools/test-classes.json`

**Interfaces:**
- Consumes: `PageDriver`, `Wait`, `DriverAnswer`, `Clock`.
- Produces:
  - `struct BrowserCommand { enum class Type { Load, Evaluate, Reset }; quint64 id; Type type; QString text; };`
  - `struct BrowserAnswer { quint64 id; bool ok; std::optional<QString> value; QString error; };`
  - `class BrowserChannel final : public PageDriver { public: explicit BrowserChannel(std::chrono::nanoseconds answerLimit = 15s); void attach(std::function<void()> wake); void detach(); bool hasAgent() const; std::optional<BrowserCommand> takeCommand(); bool answer(const BrowserAnswer& answer); void close(); … };`
    - 부르는 쪽(어느 스레드든): 에이전트가 없으면 곧 `Unavailable("no-agent")`, 닫혔으면 `closed`, `min(answerLimit, wait.limit)` 안에 답이 없으면 `no-answer`, 멈추면 `Stopped`, `ok`가 거짓인 답은 `Unavailable(error)`(빈 오류는 `failed`).
    - 에이전트 쪽: `wake`는 명령이 들어올 때마다 불린다(부르는 스레드에서, 잠금 밖에서). `takeCommand`는 아직 누가 기다리는 명령만 준다. `detach`와 `close`는 기다리던 호출을 `closed`로 끝내고, 돌아온 뒤에는 `wake`가 불리지 않는다.
  - `struct PageOutcome { bool ok; QString text; };`(`ok`면 스크립트가 준 JSON 글, 아니면 오류 이름)
  - `class Page { public: virtual ~Page() = default; virtual void load(const QString& url, std::function<void(PageOutcome)> done) = 0; virtual void evaluate(const QString& script, std::function<void(PageOutcome)> done) = 0; virtual void reset(std::function<void(PageOutcome)> done) = 0; virtual void close() = 0; };` 창이 없을 때 `reset`은 성공, `evaluate`는 `no window`.
  - `bool allowedUrl(QStringView url); bool allowedNavigation(QStringView url); std::optional<QString> decodeScriptResult(const QString& jsonText);`
  - `class BrowserAgent { public: BrowserAgent(Page& page, std::chrono::nanoseconds idleClose = 60s, Clock clock = {}); void handle(const BrowserCommand& command, std::function<void(BrowserAnswer)> done); void idleTick(); };` 한 스레드에서만 쓴다. 실패한 페이지는 닫는다. 허용되지 않은 주소의 `load`는 페이지를 건드리지 않고 `url not allowed`.

- [ ] **Step 1: 실패하는 시험 쓰기.** `BrowserChannelTest` 11개(결정 2의 하나 빼고)를 옮긴다. 폴링하는 에이전트는 시험의 둘째 스레드가 `wake`를 받아 `takeCommand`·`answer`를 부르는 것으로 옮긴다. 시간에 기대는 둘(`aCommandNobodyAnswersFailsWithNoAnswerAfterTheLimit`, `aCommandThatTimedOutIsNotHandedOutLater`)은 `answerLimit` 50ms로 한다. 더하는 시험: `theAgentCountsAsPresentOnlyWhileAttached`, `destroyingTheAgentWhileACallWaitsFailsItWithClosed`(다른 스레드가 답을 기다리는 동안 `detach` → 1초 안에 `closed`, 그 뒤 `wake` 0번), `aStoppedCallerReturnsAtOnce`, `aCallNeverWaitsLongerThanItsOwnLimit`, `manyCallersFromManyThreadsEachGetTheirOwnAnswer`(스레드 16개 × 호출 200번). `tst_browser_agent`: `browser.rs`의 16개를 낙타 표기로 옮긴다(예: `https_addresses_are_allowed` → `httpsAddressesAreAllowed`), 가짜 `Page`는 `done`을 바로 또는 나중에 부른다.
- [ ] **Step 2: 실패 확인.** 컴파일 오류.
- [ ] **Step 3: 구현.**
- [ ] **Step 4: 통과 확인.** 두 빌드에서 `ctest -R "tst_browser_(channel|agent)"`, 그리고 `tst_browser_channel`을 빌드마다 20번.
- [ ] **Step 5: 변이.** (a) 포기한 명령을 건네주기, (b) `detach`가 기다리는 호출을 깨우지 않기, (c) `allowedUrl`이 `http://` 바깥 주소를 받기, (d) 실패한 페이지를 닫지 않기 → 잡히는지 보고 되돌린다.
- [ ] **Step 6: 이름 대조, 금지 호출, 커밋.** `test-names.ps1`은 Kotlin 이름이 하나라도 빠지면 실패한다. 결정 2의 하나를 위해 `test-classes.json` 항목에 `"omitted": [{"name": …, "reason": …}]`을 받게 하고(없는 이름을 적으면 실패, 출력에 빠뜨린 수를 따로 보인다), README에 그 규칙과 Rust에서 온 이름을 어떻게 적었는지를 적는다. `Qt: 구글 가사의 브라우저 채널과 에이전트`

### Task 4: 조회 규칙과 공급자

**Files:**
- Create: `qt/core/lyrics/google/{lyrics_fetcher.h, lyrics_fetcher.cpp, google_lyrics_provider.h, google_lyrics_provider.cpp}`
- Test: `qt/tests/{tst_google_lyrics_fetcher.cpp, tst_google_lyrics_provider.cpp}`
- Modify: `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/tools/test-classes.json`

**Interfaces:**
- Consumes: Task 1~3의 것, `xgs::lyrics::LyricsProvider`, `LyricsQuery`.
- Produces:
  - `struct FetcherOptions { std::chrono::nanoseconds minInterval{10s}; std::chrono::nanoseconds firstBackoff{5min}; std::chrono::nanoseconds maxBackoff{60min}; std::chrono::nanoseconds pageTimeout{20s}; QString searchBase = DefaultSearchBase.toString(); Clock clock; Pause pause; GoogleLyricsLog* log = nullptr; };`
  - `class GoogleLyricsFetcher { public: explicit GoogleLyricsFetcher(RenderingBrowser& browser, FetcherOptions options = {}); std::optional<GoogleLyricsResult> fetch(const QString& artist, const QString& title, std::stop_token stop); };` 멈추면 빈 값. 어느 스레드에서 불러도 된다. 던지지 않는다.
  - `class GoogleLyricsProvider final : public LyricsProvider { public: GoogleLyricsProvider(BrowserChannel& channel, GoogleLyricsLog* log = nullptr, FetcherOptions options = {}); std::optional<QString> find(const LyricsQuery& query, std::stop_token stop) override; void close(); };` `close`는 채널을 닫는다. 시험용 생성자(가짜 `RenderingBrowser`와 닫을 때 부를 함수)를 하나 더 둔다.
  - 로그 문구는 Kotlin 그대로다: `구글 가사 카드: 찾음 (줄 N, 문단 M)`, `구글 가사 카드: 카드 없음`, `구글 가사 카드: 카드는 있으나 줄을 읽지 못함 (구글 페이지 형식이 바뀌었을 수 있음)`, `구글 가사 카드: {CAPTCHA로 막힘|동의창이 뜸|요청이 너무 많다는 응답(429)|시간 초과}, N분 동안 구글 검색을 쉼`, `구글 가사 카드: 웹뷰를 쓸 수 없음 (이유)`.

- [ ] **Step 1: 실패하는 시험 쓰기.** `GoogleLyricsFetcherTest` 17개, `GoogleLyricsProviderTest` 8개를 옮긴다. 가짜 브라우저는 `google_fakes.h`의 것, 시간은 시험의 시계와 기다린 길이를 적는 `pause`. `anExceptionFromTheBrowserIsAnUnavailableBrowser`는 가짜가 `std::runtime_error`를 던지는 것으로 옮기고 이유는 `exception`으로 본다(Kotlin은 예외 클래스 이름; 보고서에 차이로 적는다). 취소 시험 둘은 멈춤 토큰으로 옮긴다. 더하는 시험:
  - `stoppingEndsEveryWaitingLookupPromptly`: 스레드 64개가 서로 다른 곡을 묻고(브라우저는 멈출 때까지 막힘), 멈춤 요청 뒤 1초 안에 모두 빈 값, 그 뒤 새 조회가 쉼 없이 된다.
  - `aStopDuringTheIntervalWaitMakesNoTrip`.
  - `lookupsFromManyThreadsNeverOverlapAndKeepTheInterval`: 스레드 8개, 브라우저 호출이 겹친 적 없음, 호출 사이 간격이 시계로 10초 이상.
  - `theSecondOfTwoWaitersForOneSongUsesTheRememberedAnswer`.
- [ ] **Step 2: 실패 확인.** 컴파일 오류.
- [ ] **Step 3: 구현.** 한 번에 한 조회만 하는 차례는 멈춤 요청으로 깨어나는 기다림이어야 한다(잠금을 쥔 채 10초를 자지 않는다). 기억하는 것은 `Found`·`NoCard`·`ExtractionFailed`뿐이다. 쉼의 길이는 `min(maxBackoff, firstBackoff × 2^min(연속 수, 16))`.
- [ ] **Step 4: 통과 확인.** 두 빌드에서 `ctest -R "tst_google_lyrics_(fetcher|provider)"`, 빌드마다 20번.
- [ ] **Step 5: 변이.** (a) `BrowserUnavailable`을 기억하기, (b) 쉼의 두 배를 빼기, (c) 간격 대기를 빼기, (d) 멈춘 조회가 쉼을 시작하기 → 잡히는지 보고 되돌린다.
- [ ] **Step 6: 이름 대조, 금지 호출, 커밋.** `Qt: 구글 가사 조회 규칙과 공급자`

### Task 5: Kotlin과 대조

**Files:**
- Modify: `qt/tools/differential/{gen.h, gen.cpp, main.cpp, Oracle.java, run.ps1}`, `qt/README.md`

**Interfaces:**
- Consumes: Task 1, 2, 4의 것. Kotlin 쪽은 `server/build/sidecar-image`의 `engine` 클래스(`internal`이지만 바이트코드에서는 공개다).
- Produces: 대조 종류 넷.
  - `gq`: 가수·제목 → 요청마다 `text`, `language`, `url`, 그리고 첫 요청의 `cacheKey`. 생성기는 괄호·대괄호, `feat`/`ft`/`prod`/`narr`(대소문자 섞음, 점 있고 없음), 따옴표 아홉 가지와 곧은 아포스트로피, 한글 세 범위의 경계 글자, 공백 종류, `&#%+?`, BMP 밖 글자, 짝 없는 서로게이트, 호환 글자(전각, 합자), Σσς를 섞는다. 키 비교에서만 결정 9의 접기를 양쪽에 한다.
  - `pc`: 주소·상태 코드(없음 포함)·본문 → `blockOf`. 본문 길이는 2000 앞뒤를 고루, 표식 글은 대소문자를 섞는다.
  - `pw`: 정착 시간과 (시각 증가분, 프로브)의 줄 → 프로브마다 `isDone`. 양쪽 모두 주입한 시계다.
  - `W gf`: 조회 순서(곡들)와 브라우저가 줄 답의 줄(카드 있음·없음·깨짐·막힘 셋·시간 초과·쓸 수 없음), 조회 사이에 흐르는 시간 → 조회마다 결과(`toString`), 브라우저가 받은 주소들, `pause`가 받은 길이들, 로그 줄들. Kotlin 쪽 가짜 브라우저는 카드를 HTML로, C++ 쪽은 같은 문단·줄을 스크립트 결과로 받는다(줄의 글자는 글자·숫자·한글·공백 종류·탭·NBSP에서 뽑는다).
- 통계 문턱(종류마다 `$Statistics`에 넣는다): `gq`는 요청 0개 ≥ 3%, 2개 ≥ 15%, `ko` ≥ 20%, 제목이 바뀐 것 ≥ 30%; `pc`는 네 답(없음·Consent·Captcha·RateLimited) 각각 ≥ 8%; `pw`는 참 ≥ 15%, 정착 시간으로 참이 된 것 ≥ 3%, 다시 시작된 것 ≥ 5%; `W gf`는 아홉 결과 각각 ≥ 2%, 둘째 요청까지 간 조회 ≥ 5%, 기억된 답을 쓴 조회 ≥ 5%, 쉼이 두 번 이상 이어진 경우 ≥ 3%, 60분 상한에 닿은 경우 ≥ 0.5%.

- [ ] **Step 1: `gq`·`pc`·`pw`를 더하고 1만 건 × seed 20261115로 돌린다.** 불일치가 나오면 C++을 고친다(생성기나 비교를 약하게 하지 않는다). 고친 것마다 단위 시험을 하나 더한다.
- [ ] **Step 2: `W gf`를 더한다.** Kotlin 쪽은 `GoogleLyricsFetcher`를 가짜 `RenderingBrowser`, 주입한 `TimeSource`, 주입한 `pause`로 돌린다(Q2d의 `W jb`가 `suspend` 함수를 부른 방식). **코루틴 소스를 봐야만 Java에서 부를 수 있다면 멈추고 controller에게 알린다**(그때는 `W gf`를 빼고 보고서에 적는다).
- [ ] **Step 3: 전체 돌리기.** 1만 건 × seed 셋, `-DebugTool`, 10만 건 × 424242. 불일치 0, 문턱 모두 충족.
- [ ] **Step 4: 변이가 대조에 잡히는지.** (a) 주소의 공백을 `+`로, (b) `TRAILING_CREDIT`이 괄호를 넘어가게, (c) `BlockPageMaxText` 비교를 `<`로, (d) 주소가 바뀌어도 정착 시간을 다시 시작하지 않기, (e) 쉼 상한을 빼기, (f) `NoCard` 뒤 둘째 요청을 하지 않기 → 각각 불일치 수를 적고 되돌린다.
- [ ] **Step 5: README의 대조 문단에 네 종류를 더하고 커밋.** `Qt: 구글 가사를 Kotlin과 대조`

### Task 6: 숨은 WebView2 창과 구글 없는 점검

**Files:**
- Create: `qt/webview/{CMakeLists.txt, hidden_web_view.h, hidden_web_view.cpp, lyrics_browser.h, lyrics_browser.cpp, runtime.h, runtime.cpp}`, `qt/tests/webview/{CMakeLists.txt, fixture_server.h, fixture_server.cpp, tst_lyrics_card_parser.cpp, tst_hidden_web_view.cpp, tst_lyrics_browser_chain.cpp}`
- Modify: `qt/CMakeLists.txt`(`find_package`에 `Gui`와 WebView2), `qt/tests/CMakeLists.txt`, `qt/tools/test-classes.json`, `qt/tools/test-names.ps1`(필요하면), `qt/README.md`

**Interfaces:**
- Consumes: `Page`, `BrowserAgent`, `BrowserChannel`, `cardScript`, `probeScript`, `parseCard`, `GoogleLyricsProvider`, Q0의 `qt/spike/src/hidden_webview.cpp`(읽기만, 옮겨 쓰지 않고 다시 쓴다).
- Produces (`namespace xgs::webview`):
  - `std::optional<QString> runtimeVersion();` 런타임이 없으면 빈 값(`GetAvailableCoreWebView2BrowserVersionString`). 상자를 띄우지 않는다.
  - `struct HiddenWebViewOptions { QString userDataFolder; QString extraBrowserArguments; std::chrono::milliseconds scriptLimit{5000}; };`
  - `class HiddenWebView final : public QObject, public lyrics::google::Page { public: explicit HiddenWebView(HiddenWebViewOptions options, QObject* parent = nullptr); bool hasWindow() const; bool isShownOnScreen() const; int windowsMade() const; void setHtmlForTest(const QString& html, std::function<void(lyrics::google::PageOutcome)> done); … };` 만든 스레드에서만 쓴다. 규칙은 결정 10. `load`의 `done`은 이동이 시작되면 불린다(Kotlin `PageDriver.load`의 뜻). 없어질 때 답하지 않은 `done`을 `closed`로 한 번씩 부른다.
  - `class LyricsBrowser final : public QObject { public: LyricsBrowser(lyrics::google::BrowserChannel& channel, HiddenWebViewOptions options, QObject* parent = nullptr); ~LyricsBrowser() override; };` 생성자에서 채널에 붙고(`wake`는 큐에 넣은 호출로 GUI 스레드에서 명령을 비운다), 소멸자에서 뗀다. 타이머로 `idleTick`.

- [ ] **Step 0: 사용자에게 알린다.** 이 Task의 시험은 보이지 않는 WebView2 창을 만들고 `msedgewebview2.exe` 프로세스가 뜬다는 것, 화면에는 아무것도 뜨지 않아야 한다는 것. (controller가 실행 전에 알린다.)
- [ ] **Step 1: 실패하는 시험 쓰기**(모두 `QGuiApplication`, 런타임이 없으면 `QSKIP("WebView2 runtime must be installed")`, 사용자 데이터 폴더는 `QTemporaryDir`, 결정 14의 브라우저 인자).
  - `tst_lyrics_card_parser`: `LyricsCardParserTest`의 10개를 같은 이름·같은 고정 HTML(`testLine`, `testParagraph`, `testCard`, `testPage`를 그대로 옮긴 것)·같은 기대값으로. 고정 HTML을 `setHtmlForTest`로 띄우고 `cardScript()`를 돌려 `parseCard`에 넣는다. 더하는 시험 `theScriptGivesWhatTheFakesAssume`: `google_fakes.h`의 `loadedCard`·`loadedWithoutCard`·`loadedBrokenCard`가 쓰는 스크립트 결과가, 같은 HTML에 실제 스크립트를 돌린 결과와 같다. `aLineWithScriptAndNestedMarkupGivesOnlyItsText`.
  - `tst_hidden_web_view`(7단계 G5의 자리, 고정 페이지 서버 `/first`, `/second`, `/popup`, `/hang`, `/download.bin`):
    - `theWindowIsMadeOnTheFirstLoadAndIsNeverShown`: 만든 뒤 `isShownOnScreen()`이 거짓이고, 이 프로세스와 그 자손의 보이는 최상위 창이 0개(`EnumWindows`, 시험 내내 100ms마다).
    - `timersRunInTheHiddenWindow`: 50ms 타이머가 500ms에 8번 이상.
    - `theWindowIsReusedForTheNextPage`: `/first` 뒤 `/second`, `windowsMade() == 1`.
    - `aNewWindowHasNoCookieOfTheOldOne`: `/first`가 쿠키를 놓고, `close` 뒤 새 창의 `/second`에 쿠키가 없다. 사용자 데이터 폴더에 쿠키 파일이 남지 않는다.
    - `aDownloadIsRefused`: `/popup`이 내려받기를 눌러도 파일이 생기지 않는다(사용자의 `Downloads`와 임시 폴더를 본다). `aPopupIsNotOpened`: 서버가 `/popup-child` 요청을 받지 않는다(WebView2가 클릭 없는 `window.open`을 스스로 막으므로 거절 줄 자체는 시험되지 않는다: 주석과 보고서에 적는다).
    - `aNavigationOutsideTheAllowedAddressesIsRefused`: 페이지가 `file:///`과 `ftp://`로 가려 해도 주소가 그대로다. 허용되지 않은 `load`는 `url not allowed`.
    - `aHungPageAnswersScriptTimedOutAfterFiveSeconds`: `/hang`의 `evaluate`가 4.5~8초 사이에 `script timed out`.
    - `aBrowserProcessThatDiesIsReplaced`: 창의 브라우저 프로세스(`BrowserProcessId`)를 끝내면 다음 `evaluate`가 실패하고, `close` 뒤 `load`가 새 창에서 된다(이 사용자 데이터 폴더의 프로세스만 끝낸다).
    - `destroyingTheViewAnswersWhatIsPendingWithClosed`.
    - `withoutARuntimeLoadFailsAndShowsNothing`: `WEBVIEW2_BROWSER_EXECUTABLE_FOLDER`를 빈 폴더로 준 자식 프로세스에서(Q0의 방법) `load`가 실패하고 상자가 없다.
  - `tst_lyrics_browser_chain`(`BrowserChainTest`의 자리; 고정 페이지 서버의 `/search`가 `q`에 따라 지어낸 세 줄 카드, 카드 없는 페이지, `/sorry/`로 넘기기, 멈춘 페이지를 준다; `FetcherOptions::searchBase`가 서버를 가리키고 `minInterval`은 0, `settleTime`은 300ms):
    - `aSongWithACardIsFoundThroughTheWholeChain`: 일꾼 스레드의 `find`가 지어낸 가사를 돌려주고 로그에 `찾음 (줄 3, 문단 1)`.
    - `aSongWithoutACardIsNoLyrics`, `aBlockPageStartsTheCoolDown`(다음 조회는 서버에 요청이 없다).
    - `aHungPageIsReplacedAndIsNotHeldAgainstGoogle`: 멈춘 페이지 → 그 곡은 `웹뷰를 쓸 수 없음 (script timed out)`, 쉼 없음, 다음 곡은 새 창에서 찾는다.
    - `closingTheBrowserDuringALookupEndsItAtOnce`: 조회 중 `LyricsBrowser`를 없애면 1초 안에 빈 값, 프로세스가 끝날 때 남는 `msedgewebview2`(이 폴더의 것)가 없다.
    - `stoppingALookupLeavesTheBlankPage`, `anIdleWindowIsClosedAndTheNextLookupMakesANewOne`(쉬는 시간을 1초로).
    - `withoutARuntimeTheLookupIsUnavailableAndShowsNothing`.
- [ ] **Step 2: 실패 확인.** 컴파일 오류.
- [ ] **Step 3: 구현.** 부모는 한 번도 보이지 않는 최상위 창(Q0 보고서: `WS_POPUP`, 도구 창, 화면 밖, `ShowWindow` 없음), 컨트롤러는 보임(타이머가 돌게). 환경은 `userDataFolder`, 프로필은 InPrivate. `NewWindowRequested`·`DownloadStarting`·`PermissionRequested`는 거절, `NavigationStarting`은 `allowedNavigation`으로, `ProcessFailed`는 닫고 실패. 이벤트 토큰은 쥐고 있다가 뗀다. COM은 그 스레드가 STA여야 한다(`QGuiApplication`이 초기화한다; 아니면 실패를 답한다).
- [ ] **Step 4: 통과 확인.** 두 빌드에서 `ctest -L webview`, 빌드마다 10번. 끝난 뒤 이 시험들의 사용자 데이터 폴더를 쓰는 `msedgewebview2.exe`가 0개. `ctest`(전체)도 통과.
- [ ] **Step 5: 변이.** (a) InPrivate를 빼기 → 쿠키 시험, (b) 다운로드 거절을 빼기, (c) 스크립트 한도를 빼기, (d) `NavigationStarting` 검사를 빼기, (e) 카드 스크립트의 범위 고르기를 `Line` 전체로 → `lineSpansOutsideTheCardAreIgnored`, (f) 소멸자의 `detach`를 빼기 → 각각 잡히는지 보고 되돌린다.
- [ ] **Step 6: README**(`webview` 묶음: 무엇을 띄우는지, 건너뛰는 조건, 보내는 요청은 `127.0.0.1`뿐), 이름 대조, 금지 호출, 커밋. `Qt: 숨은 WebView2 창과 구글 없는 점검`

### Task 7: 실제 구글 시험(건너뜀), 관문, 보고서

**Files:**
- Create: `qt/tests/integration/tst_real_google.cpp`, `docs/superpowers/specs/2026-11-21-qt-google-lyrics-report.md`
- Modify: `qt/tests/integration/CMakeLists.txt`, `qt/README.md`

- [ ] **Step 1: `tst_real_google`.** 슬롯 `oneSongIsLookedUpWithOneRequest`: 실제 `LyricsBrowser`와 `GoogleLyricsProvider`(기본 옵션)로 결정 15의 곡을 한 번 묻는다. 통과 조건: 결과가 `Found`이고 줄 ≥ 3이거나, 막힘(`Captcha`·`Consent`·`RateLimited`·`Timeout`)이면 **실패가 아니라 그 이름을 알리고 건너뜀**(우회하지 않는다). 브라우저가 연 주소는 1개. 출력은 결과 이름, 줄·문단 수, 걸린 시간뿐. 두 변수가 없으면 `QSKIP`. 이 계획의 실행에서는 건너뛰는 것만 확인한다.
- [ ] **Step 2: 관문**(깨끗한 빌드 두 벌; `vcpkg_installed`는 지우지 않는다).
  - `ctest --test-dir build/qt`, `build/qt-debug`: 모두 통과.
  - 20번씩(빌드마다): `tst_browser_channel`, `tst_google_lyrics_fetcher`, `tst_google_lyrics_provider`, `tst_driven_browser`. 10번씩: `ctest -L webview`.
  - `pwsh qt/tools/test-names.ps1`(Kotlin 87개 가운데 빠진 것은 결정 2의 하나뿐), `pwsh qt/tools/forbidden-calls.ps1`.
  - 대조 전체: 1만 건 × seed 셋, `-DebugTool`, 10만 건 × 424242. 불일치 0.
  - 릴리스 빌드의 크기 기록: `xgswebview`를 쓰는 시험 실행 파일 하나의 바이트 수(설계의 "exe 크기는 기록만").
  - 끝난 뒤 남은 `msedgewebview2.exe`(시험 폴더의 것)와 `%TEMP%\qt_temp-*` 증가분이 0.
- [ ] **Step 3: 보고서**(한국어, 앞 단계 보고서의 틀): 요약, 관문 표(원본 출력 파일 이름), 대조가 찾은 C++ 버그, 변이 표, Kotlin·Rust와 다르게 한 것(결정 1~15와 실행 중에 생긴 것), 고치지 않은 원본 쪽 동작, 계획과 달라진 것, 확인하지 못한 것(실제 구글, 실제 배율·보이는 Qt 창과 함께 있을 때의 WebView2, 새 창 거절 줄), 인계 항목별 결과, Q4·Q5에 넘길 것(공급자 엮기와 하나 나눠 쓰기, `LyricsBrowser`를 GUI 스레드에서 만들고 없애는 순서: 작업 핸들 → 공급자 → `LyricsBrowser` → 채널, 주 창을 닫을 때 숨은 창, 런타임이 없을 때 알릴지, 사용자 데이터 폴더, 로그 연결), 사용자가 정할 것, 라이선스 메모, 판정.
- [ ] **Step 4: README의 관문 문단에 Q3을 더하고 커밋.** `문서: Qt 전환 Q3(구글 가사) 보고서`

## 이 단계의 관문

| | 조건 |
|---|---|
| G1 | Kotlin 구글 가사 시험 87개 가운데 86개가 같은 이름으로 통과하고, 하나는 결정 2대로 바뀌었다. Rust 에이전트 시험 16개가 옮겨져 통과한다 |
| G2 | 대조 `gq`·`pc`·`pw`·`W gf`의 불일치가 0이다(10만 건 포함) |
| G3 | 구글 없는 숨은 창 점검(7단계 G5의 항목: 창 생성과 숨김, 타이머, 재사용, 쿠키, 다운로드 거절, 멈춘 페이지, 닫힘)이 `ctest -L webview`로 통과한다 |
| G4 | 사슬 전체(일꾼 스레드의 `find` → 채널 → 에이전트 → 숨은 WebView2 → 자기 PC의 고정 페이지)가 통과한다 |
| G5 | **(사용자의 허락 뒤)** `tst_real_google` 한 번: 요청 1회로 `Found`이거나, 막혔다면 그 이름이 남는다. 허락이 없으면 "확인하지 못한 것"에 적는다 |
| G6 | 전체 브랜치 리뷰에서 Critical·Important가 없거나 고쳐진다 |

## Self-Review

- **설계 §4 Q3 행:** 요청 수 규칙(Task 4의 간격·쉼, Task 7의 요청 1회), 스크립트 파일 하나(Task 2), `LyricsCardParser` 시험을 같은 이름·같은 HTML·같은 기대값으로 숨긴 WebView2에서(Task 6), 런타임이 없으면 건너뜀·네트워크 없음(결정 14), Node 없음(결정 13), 카드 읽기는 무작위 대조에서 뺌(Task 5는 스크립트를 대조하지 않고 `parseCard`만 `W gf` 안에서 본다).
- **설계 §2 "하면 안 되는 것":** 가사·제목을 로그에 남기지 않음(Global Constraints, `toString`), 우회 금지(Task 7 Step 1), 실행 중인 앱을 건드리지 않음(결정 14, Global Constraints).
- **인계:** 시그마와 캐시 키(결정 9), `IGNORE_CASE`의 유니코드 접기(Task 1 Step 4), `HiddenWebView`는 다시 씀(Task 6), `tst_tool_locator` 누수(Task 1 Step 1). 공급자 엮기는 Q4로 넘긴다(결정 16).
- **형 이름 맞춤:** `Wait`·`DriverAnswer`(Task 2)를 Task 3의 채널이 구현하고, `PageOutcome`·`Page`(Task 3)를 Task 6의 `HiddenWebView`가 구현하며, `FetcherOptions::searchBase`(Task 4)를 Task 6의 사슬 시험이 쓴다.
- **분량:** 원본은 Kotlin 약 620줄 + Rust 약 580줄, 시험 약 1,000줄이다.

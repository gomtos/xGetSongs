# 구글 가사 카드 가사 검색 설계

작성일: 2026-10-08 (2026-10-09 실측 결과로 개정)

## 1. 목표

가사 검색(스펙 6.4)의 마지막 단계로, 구글 검색 결과의 "가사 카드"에서 가사를 가져온다. 입력은 가수와 제목, 출력은 줄 단위 가사 텍스트(문단 구분 보존)다.

스펙 6.4는 구글이 프로그램에서 읽히지 않는다는 이유로 LRCLIB를 골랐다. 이 설계는 그 전제를 JavaScript를 실행하는 숨은 웹뷰로 푼다. LRCLIB는 그대로 두고, 구글은 그 뒤의 폴백으로만 쓴다.

## 2. 확인된 사실 (2026-10-08 사용자 실측, 2026-10-09 재확인)

테스트 쿼리: `RESCENE (리센느) LOVE ATTACK lyrics` (`hl=ko`)

- HTTP 단독 요청(데스크톱·모바일 UA, 구형 UA)으로는 가사를 받지 못한다. JS 필요 안내 페이지나 "지원하지 않는 브라우저" 안내만 내려오고 `BNeawe` 기본 HTML 모드는 없어졌다. 그래서 HTTP 클라이언트나 대체 셀렉터는 쓰지 않는다.
- JS가 실행되는 브라우저에서는 가사 카드가 렌더링된다. 이 쿼리에서 `div[data-lyricid]` 1개, `[jsname="WbKHeb"]` 1개, `span[jsname="YS01Ge"]` 80개가 나온다.
- **문단 구조:** `[data-lyricid]` 안에 `[jsname=WbKHeb]`가 있고, 그 아래 문단마다 `div[jsname=U8S5sf]` 하나가 있다. 줄은 `span[jsname=YS01Ge]`이고 한 문단 안에서는 `<br>`로 이어진다. 이 쿼리는 80줄이 13문단으로 나뉜다(문단별 줄 수 1, 6, 8, 4, 13, 1, 8, 4, 13, 1, 7, 13, 1). 한 줄짜리 문단 4개는 서로 같은 후렴 줄이다. 모든 줄은 가사이고, 앞뒤 공백·빈 줄·nbsp는 없다.
- **`innerText`에는 빈 줄이 없다**(문단 사이도). 그래서 문단 구분은 텍스트가 아니라 DOM에서 읽는다: 같은 부모를 공유하는 연속된 줄이 한 문단이다. `U8S5sf` 같은 난독화 이름에 기대지 않아도 되고, 세 단계 셀렉터 모두에 같은 규칙이 통한다.

## 3. 확정된 결정

| 항목 | 결정 |
|---|---|
| 연결 | 기존 옵션 `searchLyricsOnline` 하나로 켜고 끈다. 순서는 설명란, LRCLIB, 구글. 새 옵션, API 모델, 설정, 화면 변경은 없다. |
| 렌더링 | 앱 안에 숨겨 둔 JavaFX `WebEngine`(WebKit)을 쓴다. 창을 만들지 않고, 쿠키를 저장하지 않으며, 별도 프로세스나 브라우저 설치·다운로드가 없다. 의존성은 `org.openjfx:javafx-web`(Windows 네이티브 포함 약 35MB). |
| 추출 | 렌더된 HTML(`document.documentElement.outerHTML`)을 받아 Kotlin에서 Jsoup으로 추출한다. 파서가 순수 함수라서 더미 HTML로 테스트할 수 있다. 새 의존성: Jsoup. |
| User-Agent | 엔진 기본값을 그대로 쓴다. 위조하지 않는다. (실측: `… AppleWebKit/623.1 (KHTML, like Gecko) JavaFX/21 Version/18.4 Safari/623.1`) |
| 차단 | 우회하지 않는다(프록시, CAPTCHA 우회, 쿠키 재사용, UA 변경 없음). 차단되면 실패 결과로 끝낸다. |
| 요청 | 사용자가 작업을 시작했고 옵션이 켜져 있을 때만 나간다. |

검토하고 채택하지 않은 대안:

- **외부 헤드리스 Edge/Chrome을 CDP로 구동:** 구글이 차단했다(9의 실측 1). 별도 프로세스 관리 문제(본체 분리, 포트 파일 잠김)도 있었다.
- **JCEF/KCEF:** KCEF는 2025-10에 아카이브됐고 사용을 권하지 않는다고 적혀 있으며, 크롬 런타임을 따로 내려받아야 한다.
- **webview_java(WebView2):** 포크라 유지보수가 불투명하고, 숨긴 창에서 내비게이션 이벤트가 오지 않는 사례가 있다.
- **Playwright/Selenium:** 브라우저와 드라이버를 따로 받아야 해서 앱이 무거워진다.

## 4. 구성

`engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/` 아래에 둔다.

| 파일 | 책임 |
|---|---|
| `GoogleLyricsSelectors` | 난독화된 속성(`data-lyricid`, `jsname` 값)과 "막힘" 판별 문자열을 한 곳에 상수로 모은다. 구글이 바꾸면 여기만 고친다. |
| `GoogleLyricsResult` | sealed 결과 타입(5.4)과 로그 인터페이스 `GoogleLyricsLog`. 예외를 던지지 않고 이것을 돌려준다. |
| `GoogleSearchQuery` | 가수·제목 정규화, `hl` 결정, 검색 주소, 캐시 키(5.1). |
| `LyricsCardParser` | HTML을 받아 `Found` / `NoCard` / `ExtractionFailed`를 돌려주는 순수 함수(5.3). |
| `PageClassifier` | 최종 URL, (알 수 있으면) HTTP 상태, 본문 텍스트로 `Captcha` / `Consent` / `RateLimited`를 가려낸다. |
| `RenderingBrowser` (인터페이스) | "이 URL을 열고, 카드나 막힘 신호가 나타나거나 페이지가 조용해질 때까지(시간 제한) 기다린 뒤 최종 URL·HTML·본문 텍스트를 돌려준다." 테스트에서는 가짜로 바꾼다. |
| `PageWatch` | 기다림을 끝낼 때를 가르는 순수 로직(5.2). 시계를 주입받는다. |
| `FxWebViewBrowser` | `RenderingBrowser`의 JavaFX 구현. FX 스레드와 코루틴을 잇고, 앱 종료 때 FX 런타임을 내린다. |
| `GoogleLyricsFetcher` | 위 조각을 조합하고 최소 간격, 지수 백오프, 캐시를 지킨다(5.5). 구글 호출은 한 번에 하나씩만 한다. |
| `GoogleLyricsProvider` | `LyricsProvider` 어댑터. `Found`는 가사 문자열, 나머지는 null. |
| `FallbackLyricsProvider` (`engine/lyrics/`) | 여러 `LyricsProvider`를 차례로 시도해 처음 찾은 것을 돌려준다. |

연결은 [LocalServer.kt](../../../server/src/main/kotlin/com/xgetsongs/server/LocalServer.kt)의 `createServices`에서 `FallbackLyricsProvider(LrclibLyricsProvider(), GoogleLyricsProvider(log))`로 바꾸고, 서버 범위(scope)가 끝날 때 `GoogleLyricsProvider.close()`를 부르는 것이 전부다. [ItemDownloader](../../../engine/src/main/kotlin/com/xgetsongs/engine/job/ItemDownloader.kt)와 `LyricsOutcome`은 그대로다(구글에서 찾은 가사도 `ONLINE`).

## 5. 동작

### 5.1 검색어 정규화

- 제목: 따옴표(`"` `‘ ’ “ ” „ ‟ ‚ ‛`)를 지우되 글자 사이의 `’`는 `'`로 바꾼다(`Don’t` → `Don't`). 끝의 `feat.`/`ft.`/`prod.`/`narr.` 크레딧과 크레딧을 뜻하는 괄호 `(feat. …)` `[Prod. …]`를 떼고, 연속 공백을 하나로 한다. `(Japanese Ver.)`처럼 다른 버전을 뜻하는 괄호는 **떼지 않는다**(`LyricsMatcher.titleVariants`는 이를 떼므로 그대로 쓰지 않는다).
- 가수: 먼저 원문(`RESCENE (리센느)`)으로 한 번 시도한다. 결과가 `NoCard`이고 가수에 괄호 병기가 있을 때만 괄호 밖 표기(`RESCENE`)로 한 번 더 시도한다. 즉 곡당 요청은 1회, 많아야 2회이고, 두 번째 요청도 최소 간격을 지킨다. 차단류 결과가 나오면 두 번째 시도는 하지 않는다.
- 쿼리는 `"{가수} {제목} lyrics"`다. 가수나 제목에 한글이 있으면 `hl=ko`, 아니면 `hl=en`.

### 5.2 페이지 대기와 분류

`FxWebViewBrowser`는 시간 제한(기본 20초) 안에서 0.5초마다 짧은 스크립트로 `{url, readyState, hasCard, bodyText(앞 4000자)}`를 읽고, `PageWatch`가 다음 중 하나가 되면 기다림을 끝낸다: 가사 카드 표지가 보임, 막힘 페이지, 같은 URL에서 `readyState == complete`가 4초 이어짐(카드 없는 페이지). 끝나면 `outerHTML`을 받는다. 시간 제한을 넘기면 `Timeout`, JavaFX를 쓸 수 없으면 `BrowserUnavailable`이다.

분류 순서는 `Consent` → `Captcha` → `RateLimited` → 카드 추출 → `NoCard`다.

- URL에 `consent.google`이 있으면 `Consent`. URL에 `/sorry/`가 있으면 `Captcha`.
- 본문이 짧을 때(2000자 이하, 막힘 페이지는 말이 적고 결과 페이지는 많다)만 본문을 본다: "unusual traffic" 또는 "비정상적인 트래픽"이면 `Captcha`, "too many requests"이면 `RateLimited`.
- `WebEngine`은 HTTP 상태를 알려 주지 않으므로 429는 본문으로만 가린다. 상태를 아는 구현이 있으면 429도 `RateLimited`로 본다(`/sorry/`가 함께 있으면 더 구체적인 `Captcha`가 이긴다).

### 5.3 추출 규칙

1. `div[data-lyricid]`를 찾고, 없으면 `[jsname=WbKHeb]`, 그것도 없으면 문서 전체를 범위로 삼는다. 범위 안의 `span[jsname=YS01Ge]`가 가사 줄이다.
2. 줄마다 trim하고 빈 줄은 버린다. 같은 부모를 공유하는 연속된 줄이 한 문단이다. 줄은 `\n`, 문단은 빈 줄 1개로 잇는다.
3. 결과를 기존 `LyricsExtractor.tidy`에 통과시킨다(제어 문자 제거, 앞뒤 빈 줄 제거, 3줄 미만이면 null).
4. 어떤 범위도 없고 줄도 없으면 `NoCard`. 범위(카드)는 있는데 줄이 없거나 3줄 미만이면 `ExtractionFailed`다. 둘을 구분해 로그를 남기므로, 구글이 속성을 바꿨는지(`ExtractionFailed`가 이어짐)와 단순히 가사가 없는 곡인지를 알 수 있다.

### 5.4 결과 타입

`Found(lyrics, lineCount, paragraphCount)`, `NoCard`, `ExtractionFailed`, `Captcha`, `Consent`, `RateLimited`, `Timeout`, `BrowserUnavailable(reason)`, `CoolingDown`(백오프 중이라 요청하지 않음). 로그에는 타입 이름과 줄 수·문단 수만 남기고 가사, 제목, 가수는 남기지 않는다(`Found.toString()`도 가사를 찍지 않는다).

### 5.5 빈도 제한과 캐시

- 연속 호출 사이 최소 간격 10초(끝난 시점부터 다음 시작까지, 주입 가능한 시계). 구글 호출은 직렬이다.
- `Captcha` / `Consent` / `RateLimited` / `Timeout`이면 전역 백오프에 들어간다: 5분, 10분, 20분… 최대 60분, 구글이 정상적으로 답하면(`Found` / `NoCard` / `ExtractionFailed`) 초기화. 백오프 중에는 브라우저를 쓰지 않고 `CoolingDown`을 돌려준다.
- 캐시는 앱 실행 동안 메모리에만 둔다. 키는 정규화한 첫 검색어(NFKC, 소문자)이고 `Found`와 `NoCard`, `ExtractionFailed`를 캐시한다. 차단류는 곡의 결과가 아니라 구글 쪽 상태이므로 곡 캐시에 넣지 않고 백오프로만 다룬다. `BrowserUnavailable`은 구글 요청이 아니므로 백오프도 캐시도 없다.

### 5.6 웹뷰 수명

`WebEngine`은 FX 스레드에서만 만지고, 처음 쓸 때 만들어 계속 재사용한다. JavaFX 런타임은 처음 쓸 때 시작한다(`Platform.startup`, 암묵적 종료 끔). 호출이 끝나면(취소됐을 때도) `about:blank`를 불러 페이지를 멈춘다. 쿠키 저장소를 설정하지 않으므로 쿠키는 남지 않는다. FX 스레드는 데몬이 아니라서 `Platform.exit()`을 부르지 않으면 앱을 닫아도 JVM이 끝나지 않는다. 그래서 `GoogleLyricsProvider.close()`가 이를 부르고, 서버 범위가 끝날 때 호출된다.

## 6. 테스트

- **단위(`:engine:test`, 네트워크·JavaFX 없음):** `LyricsCardParser`(더미 HTML: 세 범위 폴백 순서, 문단 구분, `NoCard`와 `ExtractionFailed` 구분), `PageClassifier`, `GoogleSearchQuery`(괄호 병기, 따옴표, `feat.`, `(Prod. …)`, 공백, `hl`, 주소 인코딩), `PageWatch`, `GoogleLyricsFetcher`(가짜 브라우저와 가짜 시계로 결과 타입별 동작, 최소 간격, 지수 백오프와 초기화, 상한, 곡당 요청 수, 캐시), `GoogleLyricsProvider`, `FallbackLyricsProvider`. 더미 HTML에는 임의의 더미 문장만 쓰고 실제 가사는 쓰지 않는다.
- **스모크(`:engine:integrationTest`, `@Tag("integration")`):** 실제 `FxWebViewBrowser`로 위 테스트 쿼리를 한 번 실행한다. 막히면(`Captcha` / `Consent` / `RateLimited` / `Timeout` / `BrowserUnavailable`) 실패가 아니라 건너뜀(skip)이고, `NoCard`나 `ExtractionFailed`면 셀렉터가 낡은 것이므로 실패다. 줄 수와 문단 수만 출력한다.

## 7. 문서 갱신

- README "가사 검색": 설명란, LRCLIB, 구글 순서. 구글 단계가 앱 안의 숨은 웹뷰로 가사 카드를 읽는다는 것, 아티스트와 제목이 구글로도 가는 것, 곡 사이 10초 간격과 차단 시 백오프. 구글 약관상 자동 질의는 금지에 가깝고 가사는 저작물이므로 개인 사용 범위를 넘으면 정식 API(Musixmatch 등)로 바꿔야 한다는 한 줄.
- 스펙 6.4: "가사 검색의 근거"에서 구글을 읽을 수 없다는 전제를 이 설계로 갱신한다.
- 코드 주석: `GoogleLyricsProvider` 첫머리에 위 약관·저작물 한 줄.

## 8. 범위 밖과 위험

- 새 옵션, 설정 항목, 화면 변경, `LyricsOutcome` 값 추가는 하지 않는다.
- 구글 카드는 곡의 버전(일본어판, 라이브 등)을 알려 주지 않아서 LRCLIB처럼 제목·길이 대조를 할 수 없다. 다른 버전의 가사가 섞일 수 있다.
- JavaFX WebKit은 구글의 지원 브라우저가 아닐 수 있고, 구글이 이를 구분해 막을 수 있다. 막히면 우회하지 않고 `Captcha` 등의 실패 결과로 끝나며, 스모크 테스트는 건너뜀으로 보고한다.
- JavaFX 런타임이 같은 프로세스에서 Compose 창과 함께 도는 구성이라, 처음 가사를 찾은 뒤 창 배율이나 표시가 달라지지 않는지 실행해서 확인한다.
- Windows 전용이다(JavaFX Windows 네이티브만 의존성에 넣는다). JavaFX를 쓸 수 없으면 `BrowserUnavailable`로 구글 단계만 건너뛴다.
- 구글이 마크업을 바꾸면 `ExtractionFailed`가 이어지고 스모크 테스트가 실패한다. 고칠 곳은 `GoogleLyricsSelectors` 한 곳이다.

## 9. 실측 기록 (2026-10-09, 같은 테스트 쿼리, 구글 요청은 방식당 1회)

1. **외부 헤드리스 Edge + CDP(빈 임시 프로필, 기본 UA `Chrome/154`):** 가사 카드 대신 차단 페이지. 문서 응답이 200 다음 HTTP 429이고 `/sorry/index`로 리다이렉트, 본문 "unusual traffic", CAPTCHA 폼이 나왔다(동의창은 아님, 1.3초). 이 시험 전에 `--dump-dom`으로 출력이 비는 시도가 한 번 있었고 이것도 요청이 나갔을 수 있다(총 1~2회). 교훈: 브라우저가 `DevToolsActivePort`를 쓰는 동안 파일이 잠기고, Edge는 시작 프로세스와 별개로 본체를 띄워서 시작 PID의 트리를 죽여도 본체가 남는다. 이 방식은 채택하지 않았으므로 설계에는 남기지 않는다.
2. **Claude 앱의 내장 브라우저:** 같은 쿼리에서 가사 카드가 떴다(`data-lyricid` 1, `WbKHeb` 1, `YS01Ge` 80). 차단·동의창 없음. 그래서 1의 차단은 IP 문제가 아니라 그 실행 방식(외부 프로세스, 빈 프로필, 자동화)에 걸린 것으로 보인다. 문단 구조(2)는 여기서 확인했다.
3. **숨김 JavaFX `WebEngine`(창 없음, 기본 UA, 쿠키 없음, JavaFX 21.0.12):** 약 4초 만에 가사 카드가 떴다(`data-lyricid` 1, `WbKHeb` 1, `YS01Ge` 80). 같은 부모로 묶으면 80줄이 13문단이고 문단별 줄 수가 위와 같다. 첫 페이지 로드가 끝난 직후 두 번째 로드가 시작되므로 `readyState`가 한 번 `complete`가 되었다가 다시 `loading`으로 돌아간다(그래서 5.2의 대기는 `complete`가 같은 URL에서 일정 시간 이어질 때만 "카드 없음"으로 본다). 이 결과로 본 설계의 렌더링 방식을 정했다.

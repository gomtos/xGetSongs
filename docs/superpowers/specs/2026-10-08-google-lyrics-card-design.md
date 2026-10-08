# 구글 가사 카드 가사 검색 설계

작성일: 2026-10-08

## 1. 목표

가사 검색(스펙 6.4)의 마지막 단계로, 구글 검색 결과의 "가사 카드"에서 가사를 가져온다. 입력은 가수와 제목, 출력은 줄 단위 가사 텍스트(문단 구분 보존)다.

스펙 6.4는 구글이 프로그램에서 읽히지 않는다는 이유로 LRCLIB를 골랐다. 이 설계는 그 전제를 JavaScript를 실행하는 브라우저로 푼다. LRCLIB는 그대로 두고, 구글은 그 뒤의 폴백으로만 쓴다.

## 2. 이미 확인된 사실 (사용자가 2026-10-08에 직접 실측, 구현 첫 단계에서 다시 확인)

테스트 쿼리: `RESCENE (리센느) LOVE ATTACK lyrics` (`hl=ko`)

- HTTP 단독 요청(데스크톱·모바일 UA, 구형 UA)으로는 가사를 받지 못한다. JS 필요 안내 페이지나 "지원하지 않는 브라우저" 안내만 내려오고 `BNeawe` 기본 HTML 모드는 없어졌다. 그래서 HTTP 클라이언트나 대체 셀렉터는 쓰지 않는다.
- JS가 실행되는 브라우저에서는 가사 카드가 렌더링된다. CAPTCHA와 동의창은 없었다. `[data-lyricid]` 1개, `[jsname="WbKHeb"]` 1개, `[jsname="YS01Ge"]` 80개가 DOM에 있었다. 가사 줄은 `span[jsname="YS01Ge"]`다.
- 아직 모르는 것: 문단 구분이 DOM에서 어떻게 표현되는지, 헤드리스 브라우저의 기본 User-Agent로도 같은 결과가 나오는지.

## 3. 확정된 결정

| 항목 | 결정 |
|---|---|
| 연결 | 기존 옵션 `searchLyricsOnline` 하나로 켜고 끈다. 순서는 설명란, LRCLIB, 구글. 새 옵션, API 모델, 설정, 화면 변경은 없다. |
| 렌더링 | 이 PC에 설치된 Edge 또는 Chrome을 `--headless=new`와 호출마다 새로 만드는 임시 프로필로 실행하고, CDP(DevTools 프로토콜)를 JDK 내장 `java.net.http.WebSocket`으로 직접 말한다. 브라우저 라이브러리는 추가하지 않는다. |
| 추출 | 렌더된 HTML을 받아 Kotlin에서 Jsoup으로 추출한다. 새 의존성은 Jsoup 하나다. 파서가 순수 함수라서 더미 HTML로 테스트할 수 있다. |
| User-Agent | 브라우저 기본값을 그대로 쓴다. 위조하지 않는다. |
| 차단 | 우회하지 않는다(프록시, CAPTCHA 우회, 쿠키 재사용 없음). 차단되면 실패 결과로 끝낸다. |
| 요청 | 사용자가 작업을 시작했고 옵션이 켜져 있을 때만 나간다. |

대안으로 Playwright·Selenium(브라우저·드라이버를 따로 받아야 해서 앱이 무거워진다)과 `--dump-dom` 한 번 호출(HTTP 상태와 리다이렉트를 구분할 수 없고 대기 시점을 정할 수 없다)을 검토했고 채택하지 않았다.

## 4. 구성

`engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/` 아래에 둔다.

| 파일 | 책임 |
|---|---|
| `GoogleLyricsSelectors` | 난독화된 속성(`data-lyricid`, `jsname` 값)과 "막힘" 판별 문자열을 한 곳에 상수로 모은다. 구글이 바꾸면 여기만 고친다. |
| `GoogleLyricsResult` | sealed 결과 타입(5.4). 예외를 던지지 않고 이것을 돌려준다. |
| `GoogleSearchQuery` | 가수·제목 정규화와 `hl` 결정(5.1). |
| `LyricsCardParser` | HTML을 받아 `Found` / `NoCard` / `ExtractionFailed`를 돌려주는 순수 함수(5.3). |
| `PageClassifier` | 최종 URL, HTTP 상태, HTML 일부로 `Captcha` / `Consent` / `RateLimited`를 가려낸다. |
| `RenderingBrowser` (인터페이스) + `CdpBrowser` | "이 URL을 열고, 가사 카드나 막힘 신호가 나타날 때까지(시간 제한) 기다린 뒤, 최종 URL·상태·HTML을 돌려준다." `CdpBrowser`는 Edge/Chrome 탐색, 기동, 종료를 맡는다. 테스트에서는 가짜로 바꾼다. |
| `GoogleLyricsFetcher` | 위 조각을 조합하고 최소 간격, 지수 백오프, 캐시를 지킨다(5.5). 구글 호출은 한 번에 하나씩만 한다. |
| `GoogleLyricsProvider` | `LyricsProvider` 어댑터. `Found`는 가사 문자열, 나머지는 null. 사유는 결과 이름만 로그에 남긴다. |
| `FallbackLyricsProvider` (`engine/lyrics/`) | 여러 `LyricsProvider`를 차례로 시도해 처음 찾은 것을 돌려준다. |

연결은 [LocalServer.kt](../../../server/src/main/kotlin/com/xgetsongs/server/LocalServer.kt)의 `createServices` 한 줄을 `FallbackLyricsProvider(LrclibLyricsProvider(), GoogleLyricsProvider(...))`로 바꾸는 것이 전부다. [ItemDownloader](../../../engine/src/main/kotlin/com/xgetsongs/engine/job/ItemDownloader.kt)와 `LyricsOutcome`은 그대로다(구글에서 찾은 가사도 `ONLINE`).

## 5. 동작

### 5.1 검색어 정규화

- 제목: 따옴표(`'` `"` `‘ ’ “ ”`), 끝의 `feat.`/`ft.`/`prod.` 크레딧, 크레딧을 뜻하는 괄호 `(feat. …)` `(Prod. …)`를 떼고 연속 공백을 하나로 한다. `(Japanese Ver.)`처럼 다른 버전을 뜻하는 괄호는 **떼지 않는다**(`LyricsMatcher.titleVariants`는 이를 떼므로 그대로 쓰지 않는다).
- 가수: 먼저 원문(`RESCENE (리센느)`)으로 한 번 시도한다. 결과가 `NoCard`이고 가수에 괄호 병기가 있을 때만 괄호 밖 표기(`RESCENE`)로 한 번 더 시도한다. 즉 곡당 요청은 1회, 많아야 2회이고, 두 번째 요청도 최소 간격을 지킨다. 차단류 결과가 나오면 두 번째 시도는 하지 않는다.
- 쿼리는 `"{가수} {제목} lyrics"`다. 가수나 제목에 한글이 있으면 `hl=ko`, 아니면 `hl=en`.

### 5.2 페이지 대기와 분류

`CdpBrowser`는 시간 제한(기본 20초) 안에서 다음 중 먼저 오는 것을 기다린다: 가사 카드 셀렉터가 나타남, 최종 URL이 `/sorry/` 또는 `consent.google`, 본문에 "unusual traffic", 문서 요청이 HTTP 429, 로드 후에도 카드가 없음(`NoCard` 후보). 분류 순서는 `RateLimited(429)` → `Captcha` → `Consent` → 카드 추출 → `NoCard`다. 시간 제한을 넘기면 `Timeout`, 브라우저를 찾지 못하거나 실행하지 못하면 `BrowserUnavailable`이다.

### 5.3 추출 규칙

1. `div[data-lyricid]`를 찾고, 없으면 `[jsname="WbKHeb"]`, 그것도 없으면 문서 전체의 `span[jsname="YS01Ge"]`를 모은다.
2. 줄마다 trim한다. 문단 구분은 빈 줄 1개다. DOM에서 문단이 어떻게 나뉘는지(빈 줄이 별도 요소인지, 컨테이너 구조인지)는 첫 단계에서 실측한 뒤 이 항의 규칙을 확정해 스펙에 적는다.
3. 결과를 기존 `LyricsExtractor.tidy`에 통과시킨다(제어 문자 제거, 앞뒤 빈 줄 제거, 3줄 미만이면 null). 셀렉터는 맞는데 줄이 3개 미만이면 `ExtractionFailed`.
4. 어떤 셀렉터도 맞지 않으면 `NoCard`, 카드 요소는 있는데 줄을 못 꺼내면 `ExtractionFailed`다. 둘을 구분해 로그를 남기므로, 구글이 속성을 바꿨는지(`ExtractionFailed`가 이어짐)와 단순히 가사가 없는 곡인지를 알 수 있다.

### 5.4 결과 타입

`Found(lyrics, lineCount, paragraphCount)`, `NoCard`, `ExtractionFailed`, `Captcha`, `Consent`, `RateLimited`, `Timeout`, `BrowserUnavailable`, `CoolingDown`(백오프 중이라 요청하지 않음). 로그에는 타입 이름과 줄 수·문단 수만 남기고 가사, 제목, 가수는 남기지 않는다.

### 5.5 빈도 제한과 캐시

- 연속 호출 사이 최소 간격 10초(끝난 시점부터 다음 시작까지, 주입 가능한 시계). 구글 호출은 직렬이다.
- `Captcha` / `Consent` / `RateLimited` / `Timeout`이면 전역 백오프에 들어간다: 5분, 10분, 20분… 최대 60분, 성공하면 초기화. 백오프 중에는 브라우저를 띄우지 않고 `CoolingDown`을 돌려준다.
- 캐시는 앱 실행 동안 메모리에만 둔다. 키는 정규화한 (가수, 제목)이고 `Found`와 `NoCard`, `ExtractionFailed`를 캐시한다. 차단류는 곡의 결과가 아니라 구글 쪽 상태이므로 곡 캐시에 넣지 않고 백오프로만 다룬다. `BrowserUnavailable`은 구글 요청이 아니므로 백오프도 캐시도 없다.

### 5.6 브라우저 수명과 정리

호출마다 브라우저를 새로 띄우고 임시 프로필을 만든다. 끝나면(호출한 코루틴이 취소됐을 때도) CDP `Browser.close`를 보내고, 임시 프로필 경로를 명령줄에 가진 프로세스를 모두 종료한 뒤 프로필을 지운다(프로세스가 사라질 때까지 삭제를 재시도). 시작한 PID의 트리에 기대지 않는 것은 9의 실측에서 Edge가 본체를 분리해 띄우기 때문이다. 호출 간격이 10초 이상이라 기동 비용은 문제가 안 되고, 쿠키나 로그인 상태가 남지 않는다.

## 6. 테스트

- **단위(`:engine:test`, 네트워크 없음):** `LyricsCardParser`(더미 HTML: 세 셀렉터 폴백 순서, 문단 구분, `NoCard`와 `ExtractionFailed` 구분), `PageClassifier`(429, `/sorry/`, `consent.google`, "unusual traffic"), `GoogleSearchQuery`(괄호 병기, 따옴표, `feat.`, `(Prod. …)`, 공백, `hl`), `GoogleLyricsFetcher`(가짜 브라우저와 가짜 시계로 결과 타입별 동작, 최소 간격, 지수 백오프와 초기화, 곡당 요청 수, 캐시), `GoogleLyricsProvider`, `FallbackLyricsProvider`. 더미 HTML에는 임의의 더미 문장만 쓰고 실제 가사는 쓰지 않는다.
- **스모크(`:engine:integrationTest`, `@Tag("integration")`):** 실제 Edge/Chrome으로 위 테스트 쿼리를 한 번 실행해 셀렉터가 아직 맞는지 본다. 브라우저가 없거나 막히면 실패가 아니라 건너뜀(skip)이다. 줄 수와 문단 수만 출력한다.

## 7. 문서 갱신

- README "가사 검색": 설명란, LRCLIB, 구글 순서. 구글 단계가 설치된 Edge/Chrome을 창 없이 실행한다는 것, 아티스트와 제목이 구글로 가는 것(옵션 설명 포함), 곡 사이 10초 간격과 차단 시 백오프. 구글 약관상 자동 질의는 금지에 가깝고 가사는 저작물이므로 개인 사용 범위를 넘으면 정식 API(Musixmatch 등)로 바꿔야 한다는 한 줄.
- 스펙 6.4: "가사 검색의 근거"에서 구글을 읽을 수 없다는 전제를 이 설계로 갱신한다.
- 코드 주석: `GoogleLyricsProvider` 첫머리에 위 약관·저작물 한 줄.

## 8. 범위 밖과 위험

- 새 옵션, 설정 항목, 화면 변경, `LyricsOutcome` 값 추가는 하지 않는다.
- 구글 카드는 곡의 버전(일본어판, 라이브 등)을 알려 주지 않아서 LRCLIB처럼 제목·길이 대조를 할 수 없다. 다른 버전의 가사가 섞일 수 있다.
- Windows 전용이다(Edge/Chrome 설치 경로 탐색). 브라우저가 없으면 `BrowserUnavailable`로 구글 단계만 건너뛴다.
- 구글이 마크업을 바꾸면 `ExtractionFailed`가 이어지고 스모크 테스트가 실패한다. 고칠 곳은 `GoogleLyricsSelectors` 한 곳이다.
- 헤드리스 브라우저의 기본 UA가 막히면(동의창, CAPTCHA) 우회하지 않고 멈춰서 보고한다. UA를 위조하지 않는 대안(창이 있는 최소화 모드 등)은 그때 다시 정한다.

## 9. 구현 첫 단계: 실측 결과 (2026-10-09)

테스트 쿼리를 헤드리스 Edge(`--headless=new`, 빈 임시 프로필, 기본 UA, CDP 연결)로 구글에 한 번 요청했다. 가사 본문은 출력하지 않고 구조만 봤다. 이 시험 전에 `--dump-dom`으로 출력이 비는 시도가 한 번 있었고, 이것도 요청이 나갔을 수 있다(총 1~2회).

**결과: 가사 카드가 아니라 차단 페이지가 왔다.** 문서 응답이 200 다음 HTTP 429이고, `https://www.google.com/sorry/index?continue=…`로 리다이렉트되며, 본문에 "unusual traffic", CAPTCHA 폼이 있었다(동의창은 아니다). 1.3초 만에 확정됐다. 그래서 문단 구분의 DOM 구조(5.3의 2)와 기본 UA로 카드가 뜨는지는 **아직 확인하지 못했다.** 우회하지 않고 여기서 멈췄다.

원인은 구분할 수 없다: 헤드리스 자동화 탐지, 이 IP의 최근 요청량(사용자의 2026-10-08 실측과 이번 시험 포함), 쿠키 없는 빈 프로필 중 무엇인지 모른다. 기본 UA는 `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/154.0.0.0 Safari/537.36`이어서 UA 문자열에 `Headless`는 없었다.

시험에서 맞다고 확인된 것과 설계에 더할 것:

- 차단 신호 네 가지(최종 URL `/sorry/`, 문서 응답 429, 본문 "unusual traffic", CAPTCHA 폼)가 모두 실제로 관측되어 5.2의 분류 기준이 맞다.
- CDP 연결은 된다(브라우저 기동 후 `DevToolsActivePort` 읽기까지 약 0.9초). 이 파일은 브라우저가 쓰는 동안 잠겨 있어 읽기에 실패할 수 있으므로, 읽기를 재시도하고 두 줄이 모두 채워졌을 때만 받아들인다.
- Edge는 시작한 프로세스와 별개로 본체 프로세스를 띄워서 시작 프로세스의 트리를 죽여도 본체 10여 개가 남았다. 5.6의 종료는 시작한 PID에 기대지 않고, CDP `Browser.close`를 먼저 보낸 뒤 임시 프로필 경로를 명령줄에 가진 프로세스를 찾아 종료하고, 프로필 삭제는 프로세스가 사라질 때까지 재시도한다.

이후 진행은 사용자의 결정을 기다린다. 창이 있는 모드나 사용자의 평소 브라우저 프로필 사용처럼 차단 탐지를 피하는 성격의 방법은 사용자의 명시적 결정 없이 시도하지 않는다.

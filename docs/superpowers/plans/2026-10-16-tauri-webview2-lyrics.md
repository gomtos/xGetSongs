# 구글 가사 검색의 웹뷰를 WebView2로 (7단계) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 구글 가사 카드를 읽는 숨은 브라우저를 JavaFX WebView에서 셸이 이미 가진 WebView2로 바꿔, 사이드카 이미지에서 JavaFX(44.2 MB)를 뺀다. `dist\xGetSongs.exe`가 129.2 MB에서 약 85 MB가 된다. 가사 검색의 동작(곡당 1회, 호출 간격 10초, 차단 시 쉼, 기억)은 한 줄도 바뀌지 않는다.

**Architecture:** 구글에 대한 지식(셀렉터, 페이지가 끝났는지 보는 논리, 차단 분류)은 모두 Kotlin에 남는다. 서버의 `RenderingBrowser` 구현이 `FxWebViewBrowser`에서 `DrivenBrowser`로 바뀌고, 이것은 `PageDriver`(열기·스크립트 실행·되돌리기, 셋뿐)만 안다. `PageDriver`를 구현한 `BrowserChannel`이 명령을 대기열에 쌓고, 셸의 Rust가 `GET /browser/next`(긴 대기)로 가져가 숨은 WebView2 창에서 실행한 뒤 `POST /browser/answer`로 답한다. Rust 쪽 브라우저는 구글을 모르는 단순한 실행기다.

**Tech Stack:** Kotlin(ktor, kotlinx.serialization), Rust(`ureq`, `serde`; 셸은 Tauri 2.12.2), WebView2(COM `ICoreWebView2::ExecuteScript`), PowerShell 점검. **새 직접 의존성 둘(셸, `cfg(windows)`): `webview2-com = "0.39.1"`, `windows-core = "0.62"`.** 둘 다 이미 Tauri가 쓰는 버전이라 새로 컴파일되는 크레이트는 없다(`Cargo.lock`에 `webview2-com 0.39.1`, `windows-core 0.62.2`). 사용자의 허락이 필요하다(계획서 검토 때 묻는다). 사라지는 것: `org.openjfx:javafx-*` 다섯.

**Spec:** [docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md](../specs/2026-10-10-tauri-kotlin-sidecar-design.md) §5의 7단계("(선택) 가사 검색의 JavaFX 웹뷰를 WebView2로 교체", 완료 증거 "구글 가사 통합 테스트 재검증")와 §3. 가사 검색의 규칙은 [구글 가사 카드 설계](../specs/2026-10-08-google-lyrics-card-design.md)가 정한 그대로다: 차단을 우회하는 규칙을 더하지 않는다.

## 실험으로 확인한 것 (2026-10-16, 저장소 밖 스크래치의 셸 사본)

| 물음 | 결과 |
|---|---|
| 숨은 창을 만들 수 있나 | `WebviewWindowBuilder::new(&app, label, WebviewUrl::External(url)).visible(false).incognito(true).build()`가 다른 스레드에서 0.8~0.9초에 끝났고 `is_visible()`은 `false` |
| 숨은 창에서 타이머가 도나 | 돈다. 50ms 간격 타이머 10번, `requestAnimationFrame` 30번이 500ms마다 정상(`visibilityState`는 `visible`로 남는다). 조절(throttle) 없음 |
| 페이지를 읽는 법 | `window.with_webview(\|platform\| ...)`(메인 스레드에서 실행됨)에서 `platform.controller().CoreWebView2()` → `ExecuteScript(&HSTRING, &ExecuteScriptCompletedHandler::create(Box::new(\|_, result\| ...)))`. 결과는 JSON으로 인코딩된 문자열(`"\"abc\""`, 없으면 `null`). 호출 스레드는 채널로 기다린다(5초 한도) |
| 창 재사용 | `window.navigate(url)`로 다른 주소로, `about:blank`로 되돌리기 모두 된다. 이동 500ms 뒤의 첫 조회가 새 주소를 보였다 |
| 쿠키 | **시크릿이 아니면** 창을 닫고 새로 만들어도 쿠키가 남았다(`cookie[seen=1]`). `incognito(true)`면 사라졌다. README의 "쿠키는 저장하지 않음"을 지키려면 시크릿이어야 한다 |
| User-Agent | `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/155.0.0.0 Safari/537.36 Edg/155.0.0.0`: 예전 JavaFX WebKit과 다른 값이다 |
| 보안 | 원격 주소의 페이지에도 `window.__TAURI__`가 주입되지만 `invoke('check_ping')`은 `Command check_ping not allowed by ACL`로 거부된다(Tauri 소스: 원격 출처는 `remote` 권한이 없으면 앱 명령에 닿지 못한다) |
| **하지 않은 것** | 실제 구글(Edge UA에 구글이 가사 카드·동의창·CAPTCHA 중 무엇을 주는가), 응답 없는 렌더러. 구글은 사용자의 허락 없이 부르지 않았다 → 관문 G7 |

## 이 계획이 내린 결정

사용자가 계획서를 검토할 때 바꿀 수 있다.

| 결정 | 내용 | 근거와 대안 |
|---|---|---|
| 하는가 | **WebView2로 옮긴다** | 대안 (b) 구글 단계를 없앤다: 가장 단순하고 같은 크기가 되지만 LRCLIB에 없는 곡의 가사가 사라진다. (c) JavaFX를 둔다: 129 MB 그대로. 7단계는 선택 단계였고 사용자가 계획을 요청했다 |
| 브라우저가 사는 곳 | 셸의 **숨은 Tauri 창 + COM `ExecuteScript`** | 대안: 사이드카(JVM)가 JNA로 WebView2를 직접(COM 바인딩을 새로 짜야 함), 별도 도우미 exe(이미지에 또 하나), 헤드리스 브라우저를 받아 오기(수십~백 MB) |
| 서버와 셸 사이 | 서버가 명령을 쌓고 셸이 **긴 대기 요청**으로 가져가 답한다: `GET /browser/next?wait=<ms>`(200 + 명령 JSON, 시간 안에 없으면 204), `POST /browser/answer`(204, 기다리는 명령이 없으면 404). 둘 다 기존 토큰 가드 안이고 셸의 `Bridge`(`ureq`)로 부른다 | 대안: SSE 하나 + POST(서버·브리지에 새 스트림 코드), 웹소켓(서버 의존성 추가). 긴 대기는 새 의존성이 없다 |
| 논리가 있는 곳 | 구글 지식은 **Kotlin에 남기고** 셸은 명령 셋(`load`, `evaluate`, `reset`)만 실행한다. 대기·안정 논리는 `JavaFX` 구현의 것을 `DrivenBrowser`로 그대로 옮긴다 | 대안: Rust가 "주소 → 끝난 페이지"를 한 번에 돌려줌(왕복은 적지만 `PageWatch`·프로브·셀렉터를 Rust에 복제하고 구글 페이지가 바뀌면 두 곳을 고쳐야 한다). 명령당 왕복은 루프백 HTTP 둘이고 500ms에 한 번이다 |
| 창의 수명 | 첫 `load`에 만들고 같은 창을 재사용한다. 렌더가 끝나면 `about:blank`로 되돌린다(지금과 같다). **60초 동안 명령이 없으면 창을 없앤다**(렌더러 프로세스 반환). **시크릿** | 구글의 동의 쿠키는 창이 있는 동안 유지돼 같은 재생목록 안에서 한 번만 묻는다(지금의 JavaFX 엔진처럼). 시크릿은 위 실험 때문 |
| 셸이 없을 때 | 서버는 40초 안에 셸의 대기 요청이 한 번도 없었으면 `Unavailable("no-agent")`로 바로 답한다. 명령에 15초 안에 답이 없으면 `Unavailable("no-answer")`. 사이드카를 혼자 띄운 시험·개발에서도 다운로드가 멈추지 않고 가사 없음으로 끝난다 | 구글 단계는 "가사가 없다"로 끝나는 것이 원래 계약이다(`GoogleLyricsProvider`는 던지지 않는다) |
| 안전 | 권한 파일은 `main` 창만, `remote` 없음을 **시험으로 고정**한다. 숨은 창은 새 창 요청과 다운로드를 거부하고, Rust는 `https://`와 루프백 `http://`만 열며(이동은 `about:blank`도), 어긋난 명령은 창을 건드리지 않고 거절한다 | 실험에서 ACL 거부를 봤지만, 나중에 누가 `remote`를 넣거나 창 이름을 권한에 넣어도 시험이 막는다 |
| 통합 시험 | `RealGoogleLyricsIntegrationTest`는 JavaFX 전용이라 **지운다**. 자리를 채우는 것: (a) 서버 안에서 가짜 에이전트가 있는 사슬 시험, (b) 셸의 `check_browser` 점검(로컬 고정 페이지, 구글 없음), (c) **구글을 실제로 한두 곡 부르는 수동 관문 G7**(사용자의 허락 뒤 포장한 exe에서 작업 하나를 돌려 로그의 `구글 가사 카드: 찾음 (줄 N, 문단 M)`을 본다) | 스펙의 "구글 가사 통합 테스트 재검증"은 (c)가 맡는다. 대안: 셸에 "에이전트 전용 모드"를 넣어 Kotlin 통합 시험이 진짜 WebView2로 구글을 부르게 함: 제품의 시작 경로를 시험을 위해 바꾸므로 채택하지 않는다 |
| 이 단계가 하지 않는 것 | 런타임 줄이기(`jlink`, 80.7 MB), 서명, 구글 말고 다른 가사 공급자, User-Agent 바꾸기, 차단·동의창·CAPTCHA를 피하는 어떤 일 | 범위 밖 |

## Global Constraints

- `RenderingBrowser`, `RenderResult`, `GoogleLyricsFetcher`, `PageWatch`, `PageClassifier`, `LyricsCardParser`, `GoogleSearchQuery`의 동작과 그 시험은 바뀌지 않는다(`engine`의 기존 시험이 그대로 통과해야 한다). 구글 호출 규칙: 곡당 1회(괄호 병기가 있고 카드가 없으면 1회 더), 호출 사이 10초 이상, 차단·동의창·429·시간 초과 뒤 5분에서 60분까지 쉼.
- 로그 줄에는 곡이나 가사가 없다(개수와 이름만). 새 로그를 더하더라도 그렇다.
- 셸의 릴리스 프로필은 `panic = "abort"`다: **에이전트 스레드의 패닉은 앱 전체를 죽인다.** 에이전트 경로(`sidecar-host/src/browser.rs`, `shell/src-tauri/src/browser_window.rs`)에는 `unwrap`·`expect`·인덱싱 패닉이 없다. 모든 실패는 `Result`다.
- 코드와 코드 주석은 영어, 사용자에게 보이는 문구와 커밋 메시지는 한국어. `main`에 직접 커밋, 메시지 끝에 `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. 푸시는 사용자에게 묻고 한다.
- Bash 도구에서는 **매 호출 첫머리에 `cd /c/Projects/xGetSongs`**(PowerShell 호출이 작업 폴더를 바꾼다). `cargo`·`npm`·`node`·Gradle은 PowerShell 도구로(`$env:Path += ";$env:USERPROFILE\.cargo\bin"`). 코드는 Write·Edit 도구로 쓴다. Gradle은 `--no-daemon --offline`만. 환경 변수는 `Remove-Item "Env:$name"`으로 지운다. 빌드 대상 폴더는 5단계의 따뜻한 `CARGO_TARGET_DIR`을 쓸 수 있다.
- **창을 띄우는 확인(Task 5, 6)을 시작하기 전에 사용자에게 알린다.** 숨은 창 점검은 화면에 아무것도 띄우지 않아야 하지만, 뜨면 이 점검 때문이다. **구글을 실제로 부르는 확인은 사용자가 허락한 뒤에만, 한 번에 한두 곡**(차단되면 쉼이 5분 이상 걸리고 사용자의 IP가 구글에서 의심받는다).
- 작업 폴더의 일부 파일은 CRLF다(셸의 `lib.rs`·`Cargo.toml` 등). Edit 도구는 줄 끝을 지킨다. 새 파일은 LF로 쓴다.
- 점검 스크립트는 `%TEMP%\xgs-bridge-check\`(저장소 밖): `helpers.ps1`, `desktop-helpers.ps1`, `clean-pc-helpers.ps1`, `clean-pc-check.ps1`, `packed-screen-check.ps1`, `measure-start.ps1`, `count-tests.mjs`. 없으면 [5단계 계획서](2026-10-15-tauri-packaging.md)의 같은 이름의 블록으로 만든다.

## Review Focus

계획의 시험이 직접 다루지 않아 사용자에게 먼저 문제가 될 것, 가능성이 높은 순서. 각 줄의 시험은 맡은 Task에 있다.

1. **구글이 응답하지 않거나 에이전트가 없을 때 다운로드가 느려지거나 멈춘다.** 사용자가 기대하는 것: 가사 없이 파일이 정상으로 끝난다. → Task 2 `aCommandNobodyAnswersFailsWithNoAnswerAfterTheLimit`, `aLookupWithNoAgentIsNullAtOnce`, Task 3 `aLookupWithoutAnAgentEndsAtOnceWithNothing`
2. **작업 중에 창을 닫으면 보이지 않는 창이 남아 앱이 계속 살아 있다**(Tauri는 창이 하나라도 있으면 끝나지 않는다. 사용자에게는 작업 관리자에만 보이는 유령 프로세스). 기대: 주 창을 닫으면 앱이 끝난다. → Task 5 `main` 창이 없어지면 앱 종료, 점검 `browserWindowDoesNotKeepTheAppAlive`(주 창을 닫고 5초 안에 프로세스가 끝남)
3. **외부 페이지가 앱의 명령(폴더 열기, 작업 시작, 다운로드)을 부른다.** 기대: 할 수 없다. → Task 5 `theHiddenBrowserWindowIsInNoCapability`, `everyCapabilityFileGrantsTheMainWindowOnly`, 점검 `popupAndDownloadAreRefused`
4. **쿠키·캐시가 디스크에 남는다**(README는 "저장하지 않음"이라고 말한다). → Task 5 점검 `cookieDoesNotSurviveTheWindow`(실험에서 시크릿이 아니면 남았다)
5. **응답 없는 페이지나 커다란 페이지.** 스크립트가 끝나지 않는 페이지는 `ExecuteScript`가 답하지 않는다: 기대는 몇 초 뒤 실패하고 다음 구글 조회가 새 창으로 된다. 구글 결과 HTML은 수백 KB~수 MB다. → Task 4 `a_command_the_page_refuses_is_answered_with_its_error_and_the_page_is_closed`, Task 3 `anAnswerOfThreeMegabytesPasses`, Task 5 점검 `hungPageIsReplaced`

## 이 단계의 관문

| 번호 | 조건 | 확인 |
|---|---|---|
| G1 | `gradlew check` 오프라인 통과: **971개**(938 + engine 21 + server 12), 실패 0, 건너뜀 0. `:server:sidecarImageTest` **5개**(4 + `theImageHasNoJavaFx`). 기존 시험은 한 줄도 바뀌지 않았다(`git diff`가 시험 추가만 보인다, 지운 `RealGoogleLyricsIntegrationTest` 제외) | Task 1~3, 6 |
| G2 | TS **241개**(239 + 권한 시험 2), `tsc` 종료 코드 0 | Task 5, 6 |
| G3 | `sidecar-host`: `lib` **186개**(161 + 브리지 5 + `browser` 20), `pack_cli` 5개, `real_sidecar`(`--ignored`) 통과(새 시험 2개 포함) | Task 4, 6 |
| G4 | 셸이 일반 빌드와 점검용 빌드로 빌드되고 `Cargo.lock`에 새로 컴파일되는 크레이트가 없다(직접 의존성만 둘 늘었다) | Task 5 |
| G5 | 점검 `check_browser`(구글 없음, 로컬 고정 페이지)가 통과한다: 창 생성, 타이머, 재사용, 쿠키, 새 창·다운로드 거부, 응답 없는 페이지, 주 창을 닫으면 종료 | Task 5, 6 |
| G6 | `package-exe.bat`의 `dist\xGetSongs.exe`가 **90 MB 이하**(예상 약 85 MB; 지금 129.2 MB), 이미지 `lib`에 `javafx` 파일이 없다. 깨끗한 PC 점검 7개 시나리오 `ALL OK`(38줄), 포장한 exe의 화면 점검 `ALL OK`, 시작 대기 두 번째 5초 이하·첫 번째 10초 이하 | Task 6 |
| G7 | **(사용자의 허락 뒤)** 포장한 exe에서 LRCLIB에 없는 곡 한두 개의 작업을 가사 옵션을 켜고 돌린다. 로그에 `구글 가사 카드: 찾음 (줄 N, 문단 M)`이 나오거나, 구글이 막았다면 막은 이유가 로그에 이름으로 나오고 다운로드는 정상으로 끝난다 | Task 6 |
| G8 | 전체 브랜치 리뷰(가장 유능한 모델)에서 Critical·Important가 없거나 고쳐진다 | 실행 마지막 |

## File Structure

| 파일 | 책임 |
|---|---|
| `contract/browser-protocol.json` (새) | 서버와 셸이 주고받는 명령·답의 예. 두 언어의 시험이 같은 파일을 읽는다 |
| `shared/.../api/ApiModels.kt` (수정) | `BrowserCommand`, `BrowserAnswer` |
| `engine/.../lyrics/google/PageDriver.kt`, `BrowserChannel.kt`, `DrivenBrowser.kt` (새) | 명령 대기열과 에이전트 입출구, 페이지 대기 논리(옛 `FxWebViewBrowser`의 것) |
| `engine/.../google/FxWebViewBrowser.kt`, `engine/src/test/.../integration/RealGoogleLyricsIntegrationTest.kt` (삭제), `engine/build.gradle.kts`, `gradle/libs.versions.toml` (수정) | JavaFX를 걷는다 |
| `engine/.../google/GoogleLyricsProvider.kt` (수정) | 생성자가 `BrowserChannel`을 받는다 |
| `server/.../Application.kt`, `ServerConfig.kt`, `LocalServer.kt` (수정) | 라우트 둘, `Services.browser`, 채널을 만들어 제공자와 서비스에 준다 |
| `sidecar-host/src/bridge.rs` (수정), `sidecar-host/src/browser.rs` (새), `lib.rs`, `tests/real_sidecar.rs` (수정) | 긴 대기 호출, `Page` 트레이트와 에이전트 루프, 주소 허용 규칙 |
| `shell/src-tauri/src/browser_window.rs` (새), `lib.rs`, `Cargo.toml` (수정) | WebView2 숨은 창이 `Page`를 구현한다. 에이전트를 시작하고 주 창이 없어지면 앱을 끝낸다. 점검 명령 |
| `shell/src/check.ts`, `shell/tests/capabilities.test.ts` (수정) | 점검 호출, 권한 고정 |
| `README.md`, `shell/README.md`, 설계 문서 둘, 보고서 (수정·새) | 문서 |
| `%TEMP%\xgs-bridge-check\browser-check.ps1`, `browser-fixture.mjs` (저장소 밖) | G5 점검과 고정 페이지 서버 |

---

### Task 1: 명령·답 모델과 계약 파일

**Files:**
- Modify: `shared/src/commonMain/kotlin/com/xgetsongs/shared/api/ApiModels.kt`
- Create: `contract/browser-protocol.json`
- Test: `server/src/test/kotlin/com/xgetsongs/server/BrowserProtocolTest.kt` (`shared`의 `commonTest`는 파일을 읽지 못한다)

**Interfaces:**
- Produces:
  ```kotlin
  @Serializable sealed interface BrowserCommand {
      val id: String
      @Serializable @SerialName("load") data class Load(override val id: String, val url: String) : BrowserCommand
      @Serializable @SerialName("evaluate") data class Evaluate(override val id: String, val script: String) : BrowserCommand
      @Serializable @SerialName("reset") data class Reset(override val id: String) : BrowserCommand
  }
  @Serializable data class BrowserAnswer(val id: String, val ok: Boolean, val value: String? = null, val error: String? = null)
  ```
  `evaluate`의 `value`는 스크립트가 낸 값의 **문자열**이고 값이 없으면 `null`이다. `ok:false`의 `error`는 사람이 읽는 짧은 영어 이름이다.

- [ ] **Step 1: 계약 파일을 쓴다** (`contract/browser-protocol.json`, `commands` 셋과 `answers` 셋)
  ```json
  {"commands":[
    {"type":"load","id":"c-1","url":"https://www.google.com/search?q=IU+Love+poem+lyrics&hl=en"},
    {"type":"evaluate","id":"c-2","script":"document.title"},
    {"type":"reset","id":"c-3"}],
   "answers":[
    {"id":"c-1","ok":true,"value":null,"error":null},
    {"id":"c-2","ok":true,"value":"한글 \"title\"\n","error":null},
    {"id":"c-3","ok":false,"value":null,"error":"window gone"}]}
  ```
- [ ] **Step 2: 시험을 쓴다** `BrowserProtocolTest`: `everyCommandOfTheContractFileDecodesAndEncodesBackToItself`(파일의 각 `commands` 항목을 `ApiJson.instance.decodeFromString(BrowserCommand.serializer(), …)`로 읽어 기대한 클래스(`Load("c-1", "https://www.google.com/…")` 등)와 같고, `encodeToJsonElement`한 것이 파일의 원소와 같다), `everyAnswerOfTheContractFileDecodesAndEncodesBackToItself`(같은 방식, `BrowserAnswer`). 파일은 `Path.of("../contract/browser-protocol.json")`로 읽는다(Gradle의 시험 작업 폴더는 모듈 폴더).
- [ ] **Step 3: 실패를 본다.** `.\gradlew.bat --no-daemon --offline :server:test --tests "com.xgetsongs.server.BrowserProtocolTest"` → 컴파일 오류(`BrowserCommand` 없음)로 FAIL
- [ ] **Step 4: 위 Interfaces의 모델을 `ApiModels.kt`에 더한다.** `JobEvent`와 같은 스타일(`@SerialName`이 `type`의 값)
- [ ] **Step 5: 통과를 본다.** 같은 명령 → 2 PASS. `:shared:test`도 통과
- [ ] **Step 6: 커밋** `git add shared contract server && git commit` — "기능: 서버와 셸의 브라우저 명령·답 모델과 계약 파일"

### Task 2: 채널, 구동 브라우저, JavaFX 걷어내기

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/google/{PageDriver,BrowserChannel,DrivenBrowser}.kt`
- Modify: `GoogleLyricsProvider.kt`, `server/.../ServerConfig.kt`, `server/.../LocalServer.kt`, `engine/build.gradle.kts`, `gradle/libs.versions.toml`
- Delete: `engine/.../google/FxWebViewBrowser.kt`, `engine/src/test/kotlin/com/xgetsongs/engine/integration/RealGoogleLyricsIntegrationTest.kt`
- Test: `engine/src/test/kotlin/com/xgetsongs/engine/lyrics/google/{BrowserChannelTest,DrivenBrowserTest}.kt` (새), `GoogleLyricsProviderTest.kt` (둘 더함), `server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarImageTest.kt` (하나 더함)

**Interfaces:**
- Consumes: Task 1의 `BrowserCommand`, `BrowserAnswer`.
- Produces:
  ```kotlin
  interface PageDriver {                       // public: BrowserChannel is public and implements it
      suspend fun load(url: String)             // throws BrowserUnavailableException
      suspend fun evaluate(script: String): String?   // null: no value, or the script could not run
      suspend fun reset()                       // back to about:blank; throws BrowserUnavailableException
  }
  class BrowserUnavailableException(val reason: String) : Exception(reason)
  class BrowserChannel(timeSource: TimeSource = TimeSource.Monotonic, agentFreshness: Duration = 40.seconds,
                       answerLimit: Duration = 15.seconds) : PageDriver, AutoCloseable {
      suspend fun nextCommand(wait: Duration): BrowserCommand?  // the agent's poll; null: nothing within wait, or closed
      fun answer(answer: BrowserAnswer): Boolean                // false: nobody waits for that id (given up, answered, unknown)
      override fun close()                                      // pending calls fail with reason "closed"; polls end at once
  }
  internal class DrivenBrowser(driver: PageDriver, pollInterval: Duration = 500.milliseconds,
                               settleTime: Duration = 4.seconds) : RenderingBrowser
  // GoogleLyricsProvider: constructor(channel: BrowserChannel, log: GoogleLyricsLog = GoogleLyricsLog.None); the old constructor(log) is gone
  // Services(…, val browser: BrowserChannel = BrowserChannel())
  ```
  Reasons: `"no-agent"` (no poll within `agentFreshness`), `"no-answer"` (no answer within `answerLimit`), `"closed"`, or the agent's `error` text for `load`/`reset`; an `evaluate` answered `ok:false` is `null`. A command whose caller gave up (timeout, cancellation) is skipped by `nextCommand`. Each poll and each answer counts as contact with the agent.

- [ ] **Step 1: `BrowserChannelTest`을 쓴다** (`runTest`, `TestTimeSource`; 에이전트는 `launch { channel.nextCommand(25.seconds) }`로 먼저 한 번 두드려 "있음"을 만든다):
  `aCallWithNoAgentFailsAtOnceWithNoAgent`, `aCommandReachesThePollingAgentAndItsAnswerReturnsToTheCaller`(evaluate "1+1" → 에이전트가 `Evaluate`를 받고 `answer(BrowserAnswer(id, true, "2"))`가 `true`, 호출자는 `"2"`), `anEvaluateAnsweredWithAnErrorGivesNull`, `aLoadAnsweredWithAnErrorFailsWithTheErrorText`(`reason == "window gone"`), `aCommandNobodyAnswersFailsWithNoAnswerAfterTheLimit`(가상 시간 15초), `aCommandThatTimedOutIsNotHandedOutLater`(그 뒤 `nextCommand(1.seconds)`는 `null`), `aCommandOfACancelledCallerIsNotHandedOutLater`, `anAnswerNobodyWaitsForIsRefused`(모르는 id와 이미 답한 id 둘 다 `false`), `theAgentCountsAsPresentForTheFreshnessTimeOnly`(39초 뒤엔 명령이 대기열에 들어가고 41초 뒤엔 `no-agent`), `closingFailsPendingCallsAndEndsPolls`, `everyCommandGetsItsOwnId`.
- [ ] **Step 2: `DrivenBrowserTest`를 쓴다** (가짜 `PageDriver`가 `evaluate`마다 정해 둔 답을 준다; 프로브 JSON은 `PageProbe`의 모양, 가짜 `TimeSource`):
  `aPageThatShowsACardIsReturnedWithItsHtml`(`RenderResult.Loaded`, `html`은 두 번째 `evaluate("document.documentElement.outerHTML")`의 값), `aCompletePageThatStaysTheSameForTheSettleTimeIsReturned`, `aProbeOfTheBlankPageIsIgnored`, `aProbeThatIsNullOrNotJsonKeepsWaiting`, `aPageThatNeverSettlesTimesOut`(`RenderResult.TimedOut`), `aDriverThatIsUnavailableGivesUnavailableWithItsReason`(`load`이 `BrowserUnavailableException("no-agent")` → `Unavailable("no-agent")`), `theBrowserIsResetAfterEveryRenderEvenWhenItWasCancelled`(`reset()`이 호출됨), `aCancellationGetsThrough`. 같은 논리를 JavaFX 구현이 지켰던 것이다(그 구현은 JavaFX 없이는 시험할 수 없었다).
- [ ] **Step 3: 공급자 시험 둘** (`GoogleLyricsProviderTest`에): `closingTheProviderClosesTheChannel`(`close()` 뒤 `channel.load("https://x")`가 `reason == "closed"`), `aLookupWithNoAgentIsNullAtOnce`(`provider.find(query)`가 `null`이고 가상 시간이 거의 흐르지 않았다).
- [ ] **Step 4: 이미지 시험 하나** (`SidecarImageTest`, 태그 `image`): `theImageHasNoJavaFx`(`lib`의 파일 이름 중 `javafx`가 든 것이 없다). 지금 이미지로 실패하는 것을 본다.
- [ ] **Step 5: 실패를 본다.** `:engine:test --tests "…BrowserChannelTest"`, `…DrivenBrowserTest`, `…GoogleLyricsProviderTest` → 컴파일 오류로 FAIL. `:server:sidecarImageTest --tests "*theImageHasNoJavaFx"` → FAIL(`javafx-*.jar`가 있다)
- [ ] **Step 6: 구현한다.** `BrowserChannel`: 명령 대기열은 `kotlinx.coroutines.channels.Channel`, 호출마다 `CompletableDeferred<BrowserAnswer>`를 id로 `ConcurrentHashMap`에 둔다. `nextCommand`는 대기열에서 꺼낸 명령의 id가 맵에 없으면(포기한 호출) 건너뛴다. `DrivenBrowser`는 `FxWebViewBrowser`의 `render`/`load`/`PROBE_SCRIPT`를 옮기되 `onFxThread`를 `driver` 호출로 바꾸고, 끝에 `driver.reset()`을 `withContext(NonCancellable) { runCatching { … } }`로 부른다. `BrowserUnavailableException`은 `Unavailable(e.reason)`, 다른 예외는 `Unavailable(e.javaClass.simpleName)`. `GoogleLyricsProvider(channel, log)`는 `GoogleLyricsFetcher(DrivenBrowser(channel), log = log)`와 `channel::close`를 쓴다. `LocalServer.createServices`는 `BrowserChannel()` 하나를 만들어 제공자와 `Services(browser = …)`에 준다. 주석("a web view that is never shown (FxWebViewBrowser)", "The web view's runtime keeps the JVM alive")을 바뀐 사실에 맞춘다. `engine/build.gradle.kts`의 JavaFX 블록과 카탈로그의 `javafx` 버전을 지운다.
- [ ] **Step 7: 통과를 본다.** 위 명령들 → 새 시험 전부 PASS, 그리고 `.\gradlew.bat --no-daemon --offline check`가 **엔진 465개**(444 + 21)와 나머지 기존 숫자로 통과, `:server:sidecarImageTest` 5개 통과
- [ ] **Step 8: 커밋** — "기능: 가사 검색의 브라우저를 채널 너머의 구동 브라우저로 바꾸고 JavaFX를 걷는다"

### Task 3: 서버 라우트와 사슬 시험

**Files:**
- Modify: `server/src/main/kotlin/com/xgetsongs/server/Application.kt`
- Test: `server/src/test/kotlin/com/xgetsongs/server/BrowserRoutesTest.kt`, `BrowserChainTest.kt` (새; 기존 `TestSupport.kt`의 `testApplication` 도우미와 `TEST_TOKEN`을 쓴다)

**Interfaces:**
- Consumes: Task 2의 `BrowserChannel.nextCommand/answer`, `Services.browser`.
- Produces: `GET /browser/next?wait=<millis>` → 200 + `BrowserCommand` JSON(`type` 판별자) 또는 204. `POST /browser/answer` (본문 `BrowserAnswer`) → 204, 기다리는 명령이 없으면 404 + `ErrorResponse("기다리는 명령이 없습니다.")`. `internal fun browserWait(raw: String?): Duration` — 숫자가 아니거나 음수면 0, 위로는 30초에서 자른다.

각 시험은 에이전트의 첫 요청이 서버에 닿은 뒤에 호출자를 시작한다(그 전의 호출은 `no-agent`로 끝난다): 요청을 보낸 뒤 `delay(100)`이면 루프백에서 충분하고, 시험이 스스로 다시 시도하지는 않는다.

- [ ] **Step 1: `BrowserRoutesTest`를 쓴다:**
  `aPollWithNothingToDoAnswersNoContentAfterTheWait`(`wait=100` → 204), `aPollGetsTheQueuedCommandAsJsonWithItsType`(대기 요청을 먼저 시작해 에이전트가 "있음"이 되게 한 뒤 `channel.evaluate("1")`을 부르면 응답 JSON이 `"type":"evaluate"`, `"script":"1"`), `anAnswerPostedToTheRouteReachesTheCaller`(답을 `POST`하면 204이고 호출자가 `value`를 받는다), `anAnswerNobodyWaitsForIsNotFound`(404와 위 메시지), `aGarbageOrNegativeWaitDoesNotWait`(`wait=abc`, `wait=-5`가 1초 안에 204), `theBrowserRoutesNeedTheToken`(토큰 없이 둘 다 401), `anAnswerOfThreeMegabytesPasses`(`value`가 3 MB 문자열), `theWaitIsParsedAndCutAtThirtySeconds`(`browserWait(null)`, `"abc"`, `"-5"` → 0, `"100"` → 100ms, `"999999"` → 30초).
- [ ] **Step 2: `BrowserChainTest`를 쓴다:** `aLyricsLookupGoesThroughTheRoutesAndComesBackFound` — `GoogleLyricsProvider(services.browser)`와 시험용 클라이언트로 도는 가짜 에이전트(코루틴: `/browser/next`를 두드려 `load`에는 ok, 프로브 스크립트의 `evaluate`에는 `{"url":"https://www.google.com/search?q=x","readyState":"complete","hasCard":true,"bodyText":""}`, `outerHTML`의 `evaluate`에는 `GoogleTestSupport.testCard`와 같은 모양의 더미 카드 HTML(`data-lyricid`, `jsname="WbKHeb"`/`"U8S5sf"`/`"YS01Ge"`, 더미 문장 "더미 하나", "더미 둘"), `reset`에는 ok)에 `find(LyricsQuery("IU", "Love poem", null, 258))`가 `"더미 하나\n더미 둘"`을 돌려주고, 에이전트가 본 명령의 종류 순서가 `load, evaluate, evaluate, reset`이다. `aLookupWithoutAnAgentEndsAtOnceWithNothing` — 에이전트가 없는 서버에서 `find`가 `null`이고 1초 안에 끝난다.
- [ ] **Step 3: 실패를 본다.** `:server:test --tests "com.xgetsongs.server.Browser*"` → 라우트가 없어 404/컴파일 오류로 FAIL
- [ ] **Step 4: 라우트 둘과 `browserWait`를 `Application.kt`에 더한다.** `call.respond<BrowserCommand>(command)`로 판별자가 나가게 한다(정적 타입이 인터페이스여야 `type`이 붙는다). 계약 파일 시험(Task 1)이 이 직렬화를 함께 지킨다
- [ ] **Step 5: 통과를 본다.** 같은 명령 → 10 PASS, `check` 전체 **971개**
- [ ] **Step 6: 커밋** — "기능: 서버에 숨은 브라우저의 명령·답 라우트를 더한다"

### Task 4: Rust 에이전트 루프

**Files:**
- Modify: `sidecar-host/src/bridge.rs` (`Limit::Within(Duration)` 추가), `sidecar-host/src/lib.rs` (`pub mod browser;`), `sidecar-host/tests/real_sidecar.rs`
- Create: `sidecar-host/src/browser.rs`
- Test: 같은 파일들의 `#[cfg(test)]`, `real_sidecar.rs`

**Interfaces:**
- Consumes: Task 1의 계약 파일(`include_str!("../../contract/browser-protocol.json")`), `Bridge`, `FakeServer`.
- Produces:
  ```rust
  // bridge.rs
  pub fn next_browser_command(&self, wait: Duration) -> Result<Option<Value>, BridgeError>  // GET /browser/next?wait=<ms>; 204 -> None; limit = wait + 10 s
  pub fn answer_browser_command(&self, answer: &Value) -> Result<(), BridgeError>           // POST /browser/answer
  // browser.rs
  pub trait Page {
      fn load(&mut self, url: &str) -> Result<(), String>;        // opens the window if there is none; returns when the load has started
      fn evaluate(&mut self, script: &str) -> Result<String, String>;  // WebView2's JSON text of the result
      fn reset(&mut self) -> Result<(), String>;                  // about:blank, window kept
      fn close(&mut self);                                        // destroys the window; also a no-op when there is none
  }
  pub struct AgentSettings { pub poll_wait: Duration, pub idle_close: Duration, pub retry_pause: Duration }  // Default: 25 s, 60 s, 1 s
  pub fn serve(get_bridge: impl Fn() -> Result<Bridge, BridgeError>, page: &mut impl Page, settings: &AgentSettings)
  pub fn allowed_url(url: &str) -> bool          // https://<host>... , or http://127.0.0.1 / http://localhost (port and path allowed)
  pub fn allowed_navigation(url: &str) -> bool   // allowed_url, or exactly about:blank
  pub fn decode_script_result(json_text: &str) -> Option<String>  // "\"a\"" -> Some("a"), null -> None, other JSON -> its text, not JSON -> None
  ```
  `serve` returns when `get_bridge` fails (the app ends or the sidecar never came). Per iteration: poll; a command is run by `handle` and answered (`{"id","ok","value","error"}`; an answer the server refuses with 404 is ignored); `204` counts the idle time and closes an opened page after `idle_close`; a poll that fails (connection lost) waits `retry_pause` and asks `get_bridge` again. `load` of a not allowed address is answered `ok:false, error:"url not allowed"` without touching the page. A command of an unknown `type` with an `id` is answered `ok:false, error:"unknown command"`. **Any `Err` from the page's `load`, `evaluate` or `reset` is answered `ok:false` with the error text and then `page.close()` is called** (a stuck window is replaced by the next `load`).

- [ ] **Step 1: 브리지 시험을 쓴다** (`bridge.rs`의 시험 모듈, `FakeServer`): `a_browser_poll_is_a_get_with_the_wait_in_milliseconds_and_the_token`(`get /browser/next?wait=25000 http/1.1`, 토큰 머리), `an_empty_browser_poll_answers_none`(204), `a_browser_poll_returns_the_command_as_it_is`, `a_browser_poll_may_last_longer_than_the_short_limit`(`with_short_timeout(200ms)`인 브리지에 서버가 500ms 뒤 답해도 `Ok`), `an_answer_is_posted_as_json_and_a_refusal_carries_the_servers_message`(본문이 보낸 JSON과 같고, 404 + `{"message":"기다리는 명령이 없습니다."}`이 `Err`의 메시지).
- [ ] **Step 2: `browser.rs` 시험을 쓴다** (함수 시험 + 루프 시험. 루프 시험은 `FakeServer` 스크립트: 연결마다 하나의 응답이다. `get_bridge`는 정해진 횟수만 `Ok`를 주고 그 뒤 `Err`를 줘서 루프를 끝낸다. 가짜 `Page`는 부른 것을 기록하고 정해 둔 답을 준다):
  - 함수: `https_addresses_are_allowed`, `plain_http_is_allowed_for_the_loopback_only`(`http://127.0.0.1:8080/x`, `http://localhost/` 허용, `http://www.google.com/`·`http://127.0.0.1.evil.com/` 거부), `other_schemes_are_refused`(`file:///c:/x`, `javascript:alert(1)`, `data:text/html,x`, `ftp://x`, `about:blank`), `a_control_character_refuses_the_address`(`https://a.com/\n`), `navigation_may_also_go_to_the_blank_page`, `a_string_result_loses_its_json_quotes_and_escapes`(`"\"a\\nb\""` → `a\nb`), `a_null_result_is_none`, `another_json_value_is_kept_as_text`(`42`, `{"a":1}`), `text_that_is_not_json_is_none`.
  - 계약: `the_commands_of_the_contract_file_parse`(파일의 세 명령이 `load("c-1", …)`, `evaluate("c-2", "document.title")`, `reset("c-3")`로 읽힌다), `the_answers_of_the_contract_file_are_what_the_agent_writes`(에이전트가 만드는 답 JSON 셋이 파일의 `answers`와 `serde_json::Value`로 같다).
  - 루프: `a_load_command_opens_the_page_and_is_answered_ok`, `an_evaluate_command_answers_the_decoded_value`, `a_command_the_page_refuses_is_answered_with_its_error_and_the_page_is_closed`, `a_load_of_a_forbidden_address_is_refused_without_touching_the_page`, `a_command_of_an_unknown_type_is_answered_with_an_error`, `an_idle_page_is_closed_after_the_idle_time`(`idle_close` 1ms, 204가 몇 번), `a_page_that_was_never_opened_is_not_closed_when_idle`, `the_loop_ends_when_the_server_is_not_coming_back`(`get_bridge`가 처음부터 `Err`), `a_lost_connection_is_retried_after_the_pause`.
- [ ] **Step 3: `real_sidecar` 시험 둘** (`#[ignore]`, 실제 이미지): `the_real_server_answers_an_empty_browser_poll_with_no_content`(`wait=100` → `None`), `an_answer_nobody_waits_for_is_refused_with_the_servers_message`(`Err`의 메시지가 `기다리는 명령이 없습니다.`).
- [ ] **Step 4: 실패를 본다.** `cargo test --manifest-path sidecar-host\Cargo.toml browser` → 컴파일 오류로 FAIL
- [ ] **Step 5: 구현한다.** `Limit::Within(d)`는 `agent.timeout(d)`. `serde` 열거형(`#[serde(tag = "type", rename_all = "lowercase")]`)으로 명령을 읽는다. 에이전트 경로에 패닉이 없다(Global Constraints).
- [ ] **Step 6: 통과를 본다.** `cargo test --manifest-path sidecar-host\Cargo.toml` → `lib` **186개**, `pack_cli` 5개. 이미지로: `.\gradlew.bat --no-daemon --offline :server:sidecarImage`, `$env:XGS_SIDECAR_DIR = "$PWD\server\build\sidecar-image"`, `cargo test --manifest-path sidecar-host\Cargo.toml --test real_sidecar -- --ignored` → 새 둘 포함 통과
- [ ] **Step 7: 커밋** — "기능: 숨은 브라우저의 명령을 가져와 실행하는 에이전트 루프(Rust)"

### Task 5: 셸의 WebView2 숨은 창

**Files:**
- Create: `shell/src-tauri/src/browser_window.rs`
- Modify: `shell/src-tauri/Cargo.toml` (**사용자의 허락을 받은 뒤**: `[target.'cfg(windows)'.dependencies]`에 `webview2-com = "0.39.1"`, `windows-core = "0.62"`), `shell/src-tauri/src/lib.rs`, `shell/src/check.ts`, `shell/tests/capabilities.test.ts`, `shell/README.md`
- Test: `shell/tests/capabilities.test.ts`, 점검 `%TEMP%\xgs-bridge-check\browser-check.ps1`

**Interfaces:**
- Consumes: Task 4의 `xgs_sidecar::browser::{Page, AgentSettings, serve, allowed_navigation}`, `Lifecycle::wait_ready`.
- Produces: `pub struct BrowserWindow` (`BrowserWindow::new(handle: tauri::AppHandle) -> BrowserWindow`) implementing `Page`; `pub const BROWSER_LABEL: &str = "xgs-browser"`; 점검 명령 `check_browser(url: String, keep_open: bool) -> Result<Vec<String>, String>`(점검 모드에서만, `check_file()`로 거부)가 `BrowserWindow`로 `load(url)` → 500ms 간격 `evaluate`(프로브) → 첫 카드까지의 줄들과 `visible=`, `windows=`(`app.webview_windows().len()`)를 돌려주고, `keep_open`이 거짓이면 창을 닫는다. `check_env`의 허용 이름에 `XGS_CHECK_BROWSER_URL`을 더한다.

`BrowserWindow`의 약속: `load`는 창이 없으면 `WebviewWindowBuilder::new(&handle, BROWSER_LABEL, WebviewUrl::External(url)).visible(false).incognito(true).focused(false).skip_taskbar(true)`에 `.on_navigation(|url| allowed_navigation(url.as_str()))`, `.on_new_window(|_, _| NewWindowResponse::Deny)`, `.on_download(|_, _| false)`를 달아 만들고, 있으면 `navigate`한다(둘 다 `allowed_url`을 먼저 본다). `evaluate`는 실험에서 확인한 패턴:
```rust
// run on the main thread by with_webview; the answer comes back over a channel (Sender<Result<String, String>>), 5 s at most
let tx_failed = tx.clone();
window.with_webview(move |platform| unsafe {
    let started = platform.controller().CoreWebView2().and_then(|core| {
        core.ExecuteScript(&script, &ExecuteScriptCompletedHandler::create(Box::new(move |code, result| {
            let _ = tx.send(code.map(|_| result).map_err(|error| error.to_string()));
            Ok(())
        })))
    });
    if let Err(error) = started { let _ = tx_failed.send(Err(error.to_string())); }
})
```
`reset`은 `navigate(about:blank)`, `close`는 `destroy()`(창이 없으면 아무 일도 안 한다). 모든 실패는 `Err(String)`.

- [ ] **Step 1: TS 시험을 쓴다** (`capabilities.test.ts`): `theHiddenBrowserWindowIsInNoCapability`(`browser_window.rs`에서 `BROWSER_LABEL`의 값을 정규식으로 읽어, 그 값이 `capabilities/*.json` 어디의 `windows`에도 없고, 어느 파일에도 `remote` 키가 없다), `everyCapabilityFileGrantsTheMainWindowOnly`(`capabilities/` 안의 모든 `.json`의 `windows`가 `["main"]`). 첫째는 파일이 없어 지금 FAIL, 둘째는 이미 통과하는 지킴이다(시험이 먼저 통과하는 것을 보는 것도 기록한다).
- [ ] **Step 2: 실패를 본다.** `Set-Location shell; node --test "tests/capabilities.test.ts"` → 첫째 FAIL
- [ ] **Step 3: `Cargo.toml`에 의존성 둘을 더하고(허락 뒤), `browser_window.rs`를 쓴다.** `lib.rs`: (a) `app.manage(Lifecycle…)` 직후 스레드를 하나 띄워 `xgs_sidecar::browser::serve(|| lifecycle.wait_ready(READY_WAIT), &mut BrowserWindow::new(handle), &AgentSettings::default())`를 돈다. (b) `.on_window_event`에 `WindowEvent::Destroyed`이고 `window.label() == "main"`이면 `window.app_handle().exit(0)`(보이지 않는 창이 앱을 살려 두지 않도록; 끝낼 때 `RunEvent::Exit`의 `Lifecycle::shutdown`은 그대로 돈다). (c) `check_browser`와 `check_env` 허용 이름. `check.ts`: `XGS_CHECK_BROWSER_URL`이 있으면 `invoke("check_browser", …)`의 줄들을 `record("browser", …)`.
- [ ] **Step 4: 통과를 본다.** `node --test "tests/*.test.ts"` → **241개**, `npx tsc --noEmit` 종료 코드 0. `cargo build --manifest-path shell\src-tauri\Cargo.toml`(일반)과 점검용 빌드(`--config check.conf.json`, 5단계와 같은 방법) 둘 다 통과
- [ ] **Step 5: 점검을 쓰고 돌린다** (`browser-fixture.mjs`: 고정 페이지 서버. `/first`는 `Set-Cookie: seen=1; Max-Age=3600`을 주고 700ms 뒤 `div[data-lyricid]`를 DOM에 넣는다. `/second`는 1500ms 뒤에 넣고 `document.cookie`를 본문에 쓴다. `/popup`은 `window.open('/first')`와 `a[download]`의 클릭을 한다. `/hang`은 `while(true){}`. **먼저 사용자에게 알린다**, 창은 뜨지 않아야 한다). `browser-check.ps1`은 점검용 셸을 `XGS_CHECK_BROWSER_URL`로 돌려 다음 이름의 줄이 모두 `ok`인지 본다:
  `browserBuiltFast`(`visible=false`, 창 생성 3초 이하), `timersRunHidden`(500ms당 틱 8 이상), `windowIsReused`(`/first`의 카드 뒤 같은 창이 `/second`의 카드도 봄), `cookieDoesNotSurviveTheWindow`(`close` 뒤 새 창의 `/second` 본문이 `cookie[]`), `popupAndDownloadAreRefused`(`/popup` 뒤 `windows=1`, 다운로드된 파일 없음), `hungPageIsReplaced`(`/hang`의 `evaluate`가 8초 안에 `Err`, 루프가 창을 닫고 다음 `load /first`가 성공), `browserWindowDoesNotKeepTheAppAlive`(`keep_open`으로 창을 둔 채 주 창을 닫으면 프로세스가 5초 안에 끝남).
- [ ] **Step 6: 커밋** — "기능: 구글 가사 검색의 숨은 브라우저를 셸의 WebView2 창으로"

### Task 6: 문서, 크기, 종단 확인

**Files:**
- Modify: `README.md`(구글 가사 단락), `shell/README.md`(숨은 브라우저 창과 `check_browser`), `docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md`(§5 7단계 행에 계획서·보고서 링크), `docs/superpowers/specs/2026-10-08-google-lyrics-card-design.md`(맨 위에 한 단락)
- Create: `docs/superpowers/specs/2026-10-16-tauri-webview2-lyrics-report.md`

- [ ] **Step 1: README의 구글 가사 단락을 고친다:** "앱 안에 창 없이 숨겨 둔 웹뷰(JavaFX WebEngine)로" → "앱 안에 보이지 않게 숨겨 둔 WebView2 창(시크릿 모드)으로", "(웹뷰 기본 User-Agent 그대로, 쿠키는 저장하지 않음)" → "(WebView2 기본 User-Agent(Edge) 그대로, 쿠키와 캐시는 창을 닫으면 사라짐)". 그 밖의 문장(곡당 1회, 10초 간격, 차단 시 쉼, 우회 안 함)은 그대로다. `git grep -n -i "javafx\|FxWebView" -- . ':!docs'`가 아무것도 보이지 않는다(코드 주석 포함). 문서의 용량 숫자가 있으면(`git grep -n "129"`) 새 숫자로
- [ ] **Step 2: 포장한다.** `.\package-exe.bat` → `dist\xGetSongs.exe`의 크기가 **90 MB 이하**(G6), 이미지 `lib`에 `javafx`가 없다
- [ ] **Step 3: 5단계의 종단 점검을 그대로 돌린다**(창이 뜰 수 있다고 먼저 알린다): `clean-pc-check.ps1`(7개 시나리오 `ALL OK`, 38줄), `packed-screen-check.ps1`(`ALL OK`), `measure-start.ps1`(두 번째 5초 이하, 첫 번째 10초 이하; 이미지가 작아져 풀기도 빨라져야 한다). 숫자를 보고서에 적는다
- [ ] **Step 4: 전체 시험을 세어 기준선과 맞춘다:** `gradlew check` 971 + `sidecarImageTest` 5, TS 241, Rust `lib` 186 + `pack_cli` 5 + `real_sidecar`. 맞지 않으면 이유를 먼저 찾는다
- [ ] **Step 5: G7 — 구글을 실제로 부르는 확인.** 사용자에게 묻는다: "LRCLIB에 없는 곡의 영상 주소 한두 개로, 가사 옵션을 켠 작업을 포장한 exe에서 한 번 돌려도 될까요? 구글에 한두 번 요청이 나갑니다." **허락이 있을 때만** 돌려 `%APPDATA%\xGetSongs\logs\xgetsongs.log`의 가사 줄(줄·문단의 개수만)을 보고서에 적는다. 허락이 없으면 보고서에 "G7 미실행"이라고 쓴다
- [ ] **Step 6: 보고서를 쓴다**(실험 결과, 숫자, 한계: 구글 반응 미확인 시 그대로, 통합 시험 교체 결정, `real_download` 미실행 등)
- [ ] **Step 7: 커밋** — "문서: 7단계(구글 가사 검색을 WebView2로) 문서와 보고서"

---

## Self-Review

- **스펙 범위:** §5의 7단계(교체 + 구글 가사 통합 재검증) → Task 2~5가 교체, 통합 재검증은 G5(구글 없는 점검)와 G7(실제 구글, 허락 뒤)로 나눴고 결정 표에 이유를 적었다. §3(사이드카 구조)는 그대로: 새 통로는 같은 토큰·같은 `Bridge`.
- **Step scan:** 각 단계에 시험 이름·단언 또는 서명이 있다. 본문 전체를 쓴 곳은 `ExecuteScript` 호출 하나(실험에서 확인한 API 모양이라 서명과 시험이 정하지 못한다)뿐이다.
- **타입 일관성:** `BrowserCommand.Load/Evaluate/Reset`, `BrowserAnswer(id, ok, value, error)`는 Task 1의 정의가 Task 2·3·4(`type` 소문자 `load|evaluate|reset`, 계약 파일)에서 같다. `PageDriver`는 public(public 클래스 `BrowserChannel`이 구현). `Page`의 네 메서드는 Task 4·5가 같다. 사유 문자열(`no-agent`, `no-answer`, `closed`)은 Task 2의 정의와 Task 2·3의 시험이 같다. `BROWSER_LABEL`은 Task 5에서만.
- **Review Focus:** 다섯 줄 모두 시험이 있다(줄마다 Task와 시험 이름을 적었다).
- **크기:** 스펙의 한 행에 비해 길지만 결정 표와 시험 목록이 대부분이고 구현 본문은 없다.
- **알려진 빈틈:** 실제 구글의 반응(G7)과 응답 없는 렌더러 외의 WebView2 장애(런타임 갱신 중 등)는 계획의 시험이 다루지 못한다. 창을 못 만들면 `load`가 `Err`이고 에이전트가 `Unavailable(error)`로 답하므로 가사 없음으로 끝난다.

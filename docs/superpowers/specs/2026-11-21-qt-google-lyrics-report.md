# Qt Q3(구글 가사 조회) 보고서

작성일: 2026-11-25
설계: [2026-11-15-qt-rewrite-and-playlists-design.md](2026-11-15-qt-rewrite-and-playlists-design.md) §4(Q3 행)
계획: [2026-11-21-qt-google-lyrics.md](../plans/2026-11-21-qt-google-lyrics.md)
이전 단계: [2026-11-21-qt-engine-job-report.md](2026-11-21-qt-engine-job-report.md)(Q2d)

환경: Windows 11 Home, Visual Studio 2022(vcvars64), Qt 6.11.3(정적), 정답 쪽 JVM은 `server/build/sidecar-image`의 OpenJDK 21과 그 안의 jar. Gradle은 돌리지 않았다. 실행 중인 xGetSongs 앱과 `%APPDATA%\xGetSongs`, `%LOCALAPPDATA%`의 앱 폴더는 건드리지 않았다.

**네트워크.** 이 단계의 어떤 실행도 구글에 요청을 보내지 않았다. `tst_real_google`은 변수 없이 한 번(두 빌드) 건너뜀만 확인했고, `XGS_INTEGRATION_GOOGLE`·`XGS_INTEGRATION_AUDIO`·`XGS_INTEGRATION_DOWNLOAD`는 한 번도 켜지 않았으며 `tst_real_lrclib`와 `yt-dlp -U`도 돌리지 않았다. 숨은 WebView2가 하는 시험은 우리 쪽 요청이 `127.0.0.1`뿐이다(WebView2 자신의 배경 통신은 시험에서만 줄이는 인자를 줬고 0임을 보장하지는 않는다).

**사용자의 지시로 가볍게 마쳤다.** 2026-11-25에 사용자가 "되도록 빨리, 토큰을 아껴서" 끝내라고 해서, 관문은 아래 "관문 표"의 가벼운 판으로 돌렸고 **전체 브랜치 리뷰(G6)는 하지 않았다.** 관문 표의 각 행에 "이 작업에서 다시 돌림"인지 "앞 작업의 실행을 인용"인지 적었다. 인용한 숫자는 그 Task의 보고서에서 옮긴 것이고 이번에 다시 잰 것이 아니다.

## 읽는 법

| Task | 내용 | 커밋 |
|---|---|---|
| 1 | 검색어·막힘 판별·기다림 판단(`GoogleSearchRequest`, `PageClassifier`, `PageWatch`)과 `GoogleLyricsResult` | `f0b9923`, 리뷰 반영 `d17677d` |
| 2 | 카드 읽기 스크립트(`lyrics_card.js`)와 `parseCard`, `DrivenBrowser` | `394dff0`, 리뷰 반영 `789ef6b` |
| 3 | `BrowserChannel`(일꾼 스레드와 GUI 스레드를 잇는 채널)과 `BrowserAgent` | `cc96bc9`, 리뷰 반영 `d5cbffb` |
| 4 | 조회 규칙 `GoogleLyricsFetcher`(간격·쉼·기억)와 `GoogleLyricsProvider` | `477ad34`, 리뷰 반영 `6227173` |
| 5 | Kotlin과의 무작위 대조(`P gq/pc/pw/pj`, `W gf`)와 그것이 찾은 C++ 차이 고침 | `698882b`, `cc877a5`, 리뷰 반영 `2fd2d89` |
| 6 | 숨은 WebView2 창(`xgswebview`)과 구글 없는 점검 | `278ae3b`, 리뷰 반영 `4180565` |
| 7 | 건너뛰는 실제 구글 시험, 관문, README, 이 보고서 | `88b6f30`, 이 보고서 |

"일꾼 스레드"는 `find`를 부르는 스레드, "GUI 스레드"는 숨은 창과 `LyricsBrowser`가 사는 스레드다. "고정 페이지"는 시험 안의 `QTcpServer`(`127.0.0.1`)가 구글 자리에 내주는 만든 페이지다.

## 요약

- **판정: 조건부 통과 — G1–G4 통과, G5(실제 구글)·G6(전체 브랜치 리뷰) 미확인.** 두 빌드의 `ctest`(`tst_real_lrclib` 제외) 51/51, 동시성 시험 네 개를 빌드마다 20번씩 모두 통과(실패 0), 이름 대조·금지 호출 통과.
- 옮긴 것: 구글 가사 조회 전체. 검색어 만들기, 막힘(CAPTCHA·동의창·429) 판별, 페이지 안정 판단, 카드 읽기(스크립트 하나 + C++ 정리), 일꾼 스레드 → `BrowserChannel` → `BrowserAgent` → 숨은 WebView2 → 고정 페이지로 가는 사슬, 조회 간격·쉼·기억, `GoogleLyricsProvider`. 공급자를 앱에 잇는 일은 Q4다(계획 결정 16).
- Kotlin 구글 가사 시험 87개(`test-names.ps1`이 세는 값, 아래 "계획과 달라진 것")에서 86개가 같은 이름으로 통과하고 하나(`theAgentCountsAsPresentForTheFreshnessTimeOnly`)는 결정 2대로 `theAgentCountsAsPresentOnlyWhileAttached`로 바뀌었다. Rust 에이전트 시험 16개는 `tst_browser_agent`로 옮겨 통과한다.
- 대조가 C++ 버그 셋을 찾았고 모두 고쳤다(아래). 그 뒤 대조는 불일치 0이다.
- **실제 구글은 한 번도 묻지 않았다.** 그래서 구글이 지금 돌려주는 페이지로 카드를 읽는지, 실제 배율·보이는 Qt 창과 함께일 때의 WebView2는 모른다.
- 원인을 끝내 모르는 시험 흔들림이 둘 있다(아래 "고치지 않고 남긴 것", Task 6).

## 관문 표

| | 조건 | 결과 | 출처 |
|---|---|---|---|
| G1 | Kotlin 87개 중 86개 같은 이름 통과 + 하나는 결정 2대로 교체, Rust 에이전트 16개 이식 | 통과. `test-names.ps1`: `PASSED ... 34 classes; 1 names were omitted on purpose; only the 98 intended names were added`. 구글 가사 클래스별 Kotlin/Qt 수: GoogleSearchQuery 14/20, PageClassifier 8/9, PageWatch 6/8, DrivenBrowser 11/14, LyricsCardParser 11/13, BrowserChannel 12/19(옮기지 않은 1 `omitted`), GoogleLyricsFetcher 17/22, GoogleLyricsProvider 8/8. 모두 `missing 0`. `tst_browser_agent`는 두 빌드의 `ctest`에서 통과 | **이 작업에서 다시 돌림** (`test-names.ps1` 1회, 두 빌드 `ctest` 각 1회) |
| G2 | 대조 `gq`·`pc`·`pw`·`W gf`(와 `pj`)의 불일치 0, 10만 건 포함 | 통과: 대조 `-Kinds PW -Ops gq,pc,pw,pj,gf`를 op마다 1만 건 × seed 셋(20261115, 1, 987654321) 모두 불일치 0, `-DebugTool`(1만 건) 0, 10만 건 × seed 424242 0. 모든 종류(26개) 1만 건 seed 20261115도 0 | **앞 작업의 실행을 인용** (Task 5 리뷰 반영 커밋 `2fd2d89`). 그 뒤 `qt/core`·`qt/tools/differential`은 바뀌지 않았다: `git diff --stat 2fd2d89..HEAD -- qt/core qt/tools/differential`의 출력이 비어 있다(Task 6의 `4180565`와 이번 작업의 `88b6f30`은 이 경로를 건드리지 않았다). 이번에 다시 돌리지 않았다 |
| G3 | 구글 없는 숨은 창 점검(창 생성·숨김, 타이머, 재사용, 쿠키, 다운로드 거절, 멈춘 페이지, 닫힘)이 `ctest -L webview`로 통과 | 통과. `tst_hidden_web_view`·`tst_lyrics_card_parser`·`tst_lyrics_browser_chain` 세 실행 파일이 두 빌드의 `ctest`에서 통과(릴리스 webview 라벨 합 73.09 s, 디버그 75.16 s). 10번 되풀이: 릴리스·디버그 각 10/10(Task 6, 흔들림 수정 뒤 코드 `278ae3b`), 창 생성 한도를 더한 뒤 각 3/3(`4180565`) | 전체 `ctest`의 1회는 **이 작업에서 다시 돌림**. 10번 되풀이는 **앞 작업의 실행을 인용** (Task 6, `278ae3b`·`4180565`). 이번에는 10번을 다시 하지 않았다 |
| G4 | 사슬 전체(일꾼 스레드의 `find` → 채널 → 에이전트 → 숨은 WebView2 → 고정 페이지)가 통과 | 통과: `tst_lyrics_browser_chain`이 두 빌드의 `ctest`에서 통과(릴리스 23.82 s) | **이 작업에서 다시 돌림** (1회씩). 되풀이는 G3와 같음 |
| G5 | (사용자의 허락 뒤) `tst_real_google` 한 번 | **돌리지 않음.** 허락이 없었다. 변수 없이 두 빌드에서 `Totals: 2 passed, 0 failed, 1 skipped`, 종료 코드 0 | 이 작업에서 건너뜀만 확인 |
| G6 | 전체 브랜치 리뷰에서 Critical·Important가 없거나 고쳐진다 | **하지 않음.** 사용자가 가볍게 끝내라고 해서 최종 리뷰와 그 수정 단계를 건너뛰었다 | - |

### 이번에 다시 돌린 것(가벼운 관문)

- 빌드: 깨끗한 빌드는 하지 않았다(증분). 새 파일 `tst_real_google.cpp`만 더해졌고 헤더는 바뀌지 않아 ninja가 놓칠 일이 없었다. 두 빌드 모두 오류 없이 링크됐고(`/W4 /WX`), `build/qt/tst_real_google.exe`가 15,782,912바이트다.
- `ctest --test-dir build/qt -E tst_real_lrclib`: `100% tests passed, 0 tests failed out of 51`, 157.85 s. `build/qt-debug`: 51/51, 190.80 s. `XGS_*` 변수는 없었다(`integration` 라벨 4개는 모두 건너뜀).
- 되풀이 20번(`ctest --repeat until-fail:20 -R "^(tst_browser_channel|tst_google_lyrics_fetcher|tst_google_lyrics_provider|tst_driven_browser)$"`): 릴리스 `100% tests passed, 0 tests failed out of 4`(33.82 s), 디버그 같은 결과(43.01 s). 흔들림 0.
- `pwsh qt/tools/test-names.ps1`: PASSED(위). `pwsh qt/tools/forbidden-calls.ps1`: `PASSED: 0 hits`.
- 크기 기록(릴리스, 설계의 "exe 크기는 기록만"): `xgswebview`를 쓰는 시험 `tst_lyrics_browser_chain.exe` 15,942,144바이트.
- 남은 것: 시험 사용자 데이터 폴더의 `msedgewebview2.exe` 0개(명령줄로 골랐고 이름으로 프로세스를 끝내지 않았다; 사용자의 다른 WebView2 42개는 그대로), `%TEMP%\qt_temp-*` 32개 → 32개(처음부터 있던 빈 폴더, 지우지 않았다).

## 대조가 찾은 C++ 버그 (Task 5)

각각 단위 시험이 붙었다(RED 확인 뒤 고침, 커밋 `698882b`).

1. 아포스트로피 앞 글자 검사(`search_query.cpp`): C++은 `’` 앞의 코드 포인트(서로게이트 쌍이면 한 글자)를 봤고, Kotlin의 `(?<=\p{L})`는 바로 앞 UTF-16 단위 하나만 본다. `unitBefore`로 고쳤다.
2. 단어 경계 `\b`가 결합 부호(Mn) 앞에서 다르다(`search_query.cpp`, 새 `kt::isNonSpacingMark`): Kotlin은 크레딧 단어 뒤에 Mn이 오면 경계가 없다고 본다. 전수 조사 중 U+1171E(JVM 21의 유니코드에서는 Mn, Qt의 유니코드에서는 Mc)를 따로 처리했다(손으로 넣은 예외다).
3. `probeFromJson`의 문법(`page_watch.cpp`): Kotlin `decodeFromString<PageProbe>`는 쉼표 없이 다음 키가 와도 받고, `hasCard`를 따옴표 있든 없든 대소문자 무시로 읽고, `null` 문자열은 거절하고, 되풀이된 키는 마지막 것을 쓴다. `qt/core/json`은 건드리지 않고 빈 `ClassShape`의 `json::parse`와 작은 `ProbeReader`로 맞췄다. 앞서 더한 시험 `aProbeOfTheWrongShapeIsNoProbe`의 한 단언(`"hasCard":"true"`는 거절)은 Kotlin이 받는 것이라 `"yes"`로 바꿨다.

## 변이 표

각 변이는 적용 → 빌드 → 시험 → 되돌림(해시로 확인)이다. 원본은 Task 보고서에 있다.

| Task | 변이 | 잡은 것 |
|---|---|---|
| 1 | 캐시 키에서 ς→σ 제거, 아포스트로피 앞 글자 검사 제거, `PageWatch`가 새 주소에서 다시 안 시작, 크레딧 끝의 점 깨기, 한글 음절 범위 빼기, 따옴표 안 지우기 | 모두 잡힘 |
| 1 | `\b`를 검사하지 않음, 막힘 판별 한도를 `<=` 대신 `<` | **살아남음**(Task 1 시점). 뒤에 Task 5의 대조가 잡았다(변이 b, c) |
| 2 | 멈췄을 때 `reset` 안 함, `about:blank` 검사 제거, 빈 값을 빈 카드로 읽음 | 모두 잡힘 |
| 3 | 포기한 명령을 나중에 내줌, `detach`가 기다리는 호출을 안 깨움, `allowedUrl`이 모든 `http://`를 받음, 실패한 페이지를 안 닫음 | 모두 잡힘. 리뷰 반영 뒤 소멸자의 기다림을 빼면 SegFault로 잡힘 |
| 4 | 안 되는 브라우저를 기억함, 쉼이 두 배로 안 늚, 간격 대기 없음, 멈춘 조회가 쉼을 시작함, 차례를 얻은 뒤 기억 확인 빼기, 차례가 서로를 막지 않음 | 모두 잡힘 |
| 5 | 주소의 공백을 `+`로, 뒤 크레딧이 괄호를 넘음, 막힘 한도 `<`, 새 주소가 안정 시간을 안 되돌림, 쉼 상한 없음, NoCard 뒤 두 번째 요청 없음, 필드 빠진 probe 허용 | 대조가 잡음: P 1462/W 882, 19, 29, 239, 76, 273, 115건 불일치 |
| 6 | InPrivate 끔, 다운로드 `Cancel` 뺌, 스크립트 한도 타이머 뺌, `NavigationStarting` 취소 뺌, `lyrics_card.js`의 `(scope \|\| document)`를 `document`로, `~LyricsBrowser`의 `detach` 뺌, 창 생성 마감의 `failWindow` 뺌 | 모두 잡힘. 단 `NavigationStarting` 변이는 `blob:` 시도만이 잡는다(`file:`·`ftp:`는 브라우저가 스스로 막음) |

Task 6에서 찾은 것: WebView2 런타임(158.0.4370.33)은 프로필의 다운로드 폴더를 슬래시로 주면 `DownloadStarting` 전에 모든 다운로드를 버려서, 첫 `aDownloadIsRefused`가 거절 코드를 빼도 통과했다(빈 시험). 폴더를 네이티브 구분자로 하고 `HiddenWebView::downloadsRefused()` 카운터를 단언해 고쳤다.

## Kotlin·Rust와 다르게 한 것

계획의 결정 1~16은 `global-constraints.md`와 계획서에 있다(작성자가 정했고 사용자가 바꿀 수 있다). 그 밖에 실행 중에 생긴 차이:

- (Task 1) `GoogleLyricsLog.None`은 옮기지 않았다(계획의 인터페이스에 없음). URL 인코더를 LRCLIB 쪽에서 `qt/core/net/url_encoding.{h,cpp}`로 옮겼다(내용 그대로). `kt::isLetter(char32_t)`를 더했다. `probeFromJson`은 처음에 `parseToJsonElement`의 문법으로 읽었다가 Task 5가 Kotlin 쪽 문법으로 고쳤다.
- (Task 2) 예외의 이유 문자열이 `exception`이다(Kotlin은 예외 클래스 이름). `FakeBrowser`는 예상 밖 요청에 `Unavailable`을 답하고 `unexpected`에 적는다(이후 시험은 그 목록이 비었는지 본다). Jsoup의 블록 요소 안 줄 구분자와 `<pre>`의 공백은 따라 하지 않았다. Jsoup의 규칙은 jsoup 1.23.2 jar의 바이트코드를 `javap`로 읽어 알았다(MIT). 카드 스크립트는 `*.js eol=lf`로 고정(`qt/.gitattributes`).
- (Task 3) 한도가 0 이하면 즉시 답 없음. 이미 붙은 에이전트 위에 `attach`하면 앞 에이전트는 끝나고 동시 `attach`는 안전하다. 던지는 `wake`는 삼킨다. `allowedUrl`은 짝 없는 서로게이트가 든 주소를 거절한다. 명령이 진행 중이면 쉬는 창 닫기가 일어나지 않는다. `done`을 두 번 불러도 답은 한 번이다. 던지는 페이지 호출은 `exception`으로 답한다. `decodeScriptResult`가 serde_json과 다른 곳: `1e400`, 다시 직렬화, 깊이 128 초과, 안쪽 공백. 위의 `tst_browser_agent` 셋 더한 시험은 아래 판단에 있다.
- (Task 4) 멈춤은 예외가 아니라 빈 값이다. 멈춤이 이미 요청된 호출은 기억된 답·`CoolingDown`·빈 곡도 빈 값으로 끝난다(Kotlin은 멈추지 않고 돌려줄 수 있다). 문제 횟수는 16에서 멈춘다(`min(streak, 16)`만 쓰므로 같은 동작). 공급자의 `log`가 `options.log`를 대신한다. 공급자 생성자에 네 번째 인자 `DrivenBrowserOptions`가 있다.
- (Task 5) 대조에서 σ/ς는 결정 9대로 접어서 비교한다. `W gf`는 던지는 브라우저를 만들지 않는다(이유 문자열이 다르게 한 것이므로). 카드 읽기는 스크립트를 대조하지 않고 `W gf` 안의 `scriptAnswer` 모델로만 본다.
- (Task 6) `browser_window.rs`에 더한 것: 권한 요청 거절, `ProcessFailed` 때 창을 닫고 `browser process failed`로 답함, 기본 스크립트 대화상자 끔, 숨은 폴더 `<userData>\refused-downloads`로 새어 나온 다운로드 가두기, 외부 URI·기본 인증 거절, 컨텍스트 메뉴·확대·상태줄·개발자 도구 끔, 호스트 창이 `WM_CLOSE`를 삼킴, 창 생성 마감(`createLimit` 30초; Rust에는 없다). 런타임 확인 상자는 옮기지 않았다(결정 4).
- 에이전트의 쉬는 시간은 명령이 **도착한** 때부터 센다(Rust와 같음). 그런데 C++의 타이머는 명령과 무관하게 돌아서, Rust(빈 25초 폴링 뒤에만 확인)와 달리 첫 `load`가 창을 만드는 1초 안에 닫힐 수 있다. 그래서 쉬는 창 시험은 3초를 쓴다(계획은 1초). 제품의 60초에서는 문제가 아니다.

## 고치지 않은 원본 쪽 동작

원본(Kotlin·Rust)이 버그로 보여 고치지 않고 둔 것은 없다. 대조가 보인 차이는 모두 C++을 Kotlin에 맞췄다. 결정으로 다르게 한 것은 위와 "사용자가 정할 것"에 있다.

## 계획과 달라진 것

- 대조의 새 비교는 한 글자 종류가 아니라 `P`와 `W`의 op다(`P gq`·`pc`·`pw`·`pj`, `W gf`). 쓸 수 있는 글자가 없었다(26자 모두 쓰이고, `$Statistics`의 키가 대소문자를 구분하지 않는다). 이 op들은 `-Ops`로 따로 돌린다(op마다 1만 건 × seed 셋; 기본 `P`·`W`의 사례 안에 섞으면 op마다 600~1600건으로 줄어든다는 리뷰 지적 때문). `pc`는 `P pc`다(`W pc`가 이미 있다).
- 다섯째 비교 `pj`(`probeFromJson`과 Kotlin의 `decodeFromString<PageProbe>`)를 더했다(Task 1이 문법을 확인하지 못했다고 표시했기 때문).
- Kotlin `LyricsCardParserTest`는 10개가 아니라 11개다. `test-names.ps1`의 합계로는 구글 가사 클래스의 Kotlin 시험이 14+8+6+11+11+12+17+8 = 87개이고(`ToolLocatorTest` 6은 별개), 그 가운데 `BrowserChannelTest`의 1개를 옮기지 않으므로 86개가 이식됐다. 계획이 적은 "87"과 합계는 같지만, 계획의 클래스별 수(카드 10개)와는 맞지 않는다. 실제 값은 위 표대로다.
- 쉬는 창 시험은 3초를 쓴다(계획 1초; 위).
- 숨은 창에 만드는 데 걸리는 마감(`createLimit` 30초)을 더했다(Task 6 리뷰의 Important). 런타임이 없는 자식 프로세스가 오류 상자를 띄우지 않게 하는 `quietChildFailures()`도 더했다.
- 가볍게 마친 관문: 깨끗한 빌드 없음, 대조와 `-L webview` 10번 되풀이는 다시 돌리지 않고 인용, 전체 브랜치 리뷰와 그 수정 단계를 건너뜀.
- `tst_browser_agent`의 시험 셋(`theControlAndWhitespaceSetIsRusts`, `aScriptResultThatSerdeWouldRefuseIsNone`, `noIdlePageIsClosedWhileACommandIsUnderway`)은 계획에 없던 것이다(아래 판단).
- `test-names.ps1`에 `omitted`(일부러 옮기지 않은 Kotlin 이름) 지원을 더했다(Task 3, Step 6).

## 실행 중 내린 판단

각 항목은 진행 기록의 `Ruling:` 줄이다. 틀렸을 때의 값도 적었다.

1. (Task 2 사전 확인) `LyricsCardParserTest`는 Task 2에서는 클래스 항목을 등록하지 않고 `aFoundResultNeverPrintsTheLyrics` 하나만 `tst_lyrics_card`에 두었다가, 나머지가 옮겨지는 Task 6에서 실행 파일 배열로 등록하기로 했다. 틀렸다면: 이름 대조용 JSON 한 줄을 고치면 된다. (실제로는 Task 6에서 `tst_lyrics_card_parser`가 11개 전부를 받았다.)
2. (Task 3 사전 확인) 계획의 파일 목록에 없는 `qt/tools/test-names.ps1`을 `omitted` 때문에 고친다(Step 문구를 따름). 틀렸다면: 비용 없음.
3. (Task 3) `tst_browser_agent`에 계획에 없던 시험 셋을 받아들였다. 더한 동작을 고정하고 비용이 없다. 틀렸다면: 시험 셋을 지우면 된다.
4. (Task 4 사전 확인) `GoogleLyricsProvider`가 `DrivenBrowserOptions`를 넘길 방법(네 번째 생성자 인자, 기본값 있음)을 갖게 해서 Task 6이 안정 시간 300 ms를 줄 수 있게 한다. 틀렸다면: 인자 하나.
5. (Task 5) 다섯째 대조 `pj`를 더했다. 틀렸다면: 작은 종류 하나이고, Kotlin 직렬화기를 `kotlinx` 소스 없이 부를 수 없었다면 버리고 문서화한다(실제로는 공개 API로 가능했다).
6. (사용자의 가볍게 끝내라는 지시, 2026-11-25) Task 6은 Critical/Important만 한 번에 고치고 싼 재리뷰, Task 7은 구현자 하나와 컨트롤러의 diff 확인(작업 리뷰 없음), 전체 브랜치 리뷰와 그 수정 단계는 건너뜀. 틀렸다면: Task 간 결함과 아래 "고치지 않고 남긴 것"이 사용자가 리뷰를 시키기 전까지 리뷰 없이 남는다.
7. (Task 7) 가벼운 관문: 깨끗한 빌드 없이 증분, 전체 `ctest`는 빌드마다 한 번, 동시성 시험 네 개 20번, 대조와 `-L webview` 10번 되풀이는 인용. 틀렸다면: 대조나 10번 되풀이만 드러낼 회귀가 다음 관문까지 안 보인다(이번 작업은 `qt/core`를 바꾸지 않았다는 diff로 대조 쪽은 줄였다).

## 고치지 않고 남긴 것

진행 기록의 "Minor (deferred)" 항목을 Task별로 묶었다. 고치지 않았고 전체 브랜치 리뷰도 하지 않았다.

**Task 1**
- 대조에서 덮을 것으로 넘겼고(Task 5), 대부분 덮였다: 크레딧 단어 뒤 `\b`, `kt::isLetter(char32_t)`의 U+FFFF 위, `probeFromJson`의 문법. `GoogleLyricsLog.None`을 옮기지 않았고 `toString`을 시험하지 않았다.

**Task 2**
- `p`를 숫자 문자열로 비교한다(앞에 0이 있을 때; 스크립트는 그렇게 쓰지 않는다).
- `catch(...)`가 원래 오류 글을 잃는다.

**Task 3**
- `destroyingTheAgentWhileACallWaitsFailsItWithClosed`에 1초 한도가 있다(계획이 정한 것).
- `browser_channel.cpp:173`의 `lock.lock()`이 던지는 것은 이론상의 일이다.

**Task 4**
- 새 대기자 시험에서 대기자가 200 ms 먼저 시작을 놓치면 시험이 잘못 통과할 수 있다(잘못 실패하지는 않는다).
- `stoppingEnds...`에서 멈춤을 무시하는 스레드는 `ctest`의 120초 `TIMEOUT`까지 join을 붙잡는다.

**Task 5**
- `W gf`는 던지지 않고 그 `pause`는 멈추지 않는다(단위 시험으로만 본다). `W gf`의 C++ 쪽은 스크립트의 모델인 `scriptAnswer`를 먹인다(진짜 스크립트는 Task 6에서만 확인).
- `-Ops pc`는 늘 `P pc`다(`W pc`만 따로 돌릴 수 없다). `run.ps1`의 주석 하나가 0번 열에 있다. `rangesAreWellFormed`가 `kNonSpacingMarkRanges`에는 적용되지 않는다.
- MSVC가 `/showIncludes` 안내를 한국어로 찍어 ninja의 `msvc_deps_prefix`가 깨진다. 그래서 헤더만 고치면 다시 빌드되지 않을 수 있다(Task 5·6 구현자가 이 때문에 파일을 건드려 다시 빌드시켰다).

**Task 6**
- 흔들림 둘의 원인을 끝내 모른다(원장 기준; 둘 다 수정 전 라운드에서 한 번씩 났고 이후 수십 번 다시 나지 않았다): 디버그 `tst_hidden_web_view`의 `aNavigationOutsideTheAllowedAddressesIsRefused`에서 성공한 로드 직후 `waitForCard`가 10초 동안 카드를 못 봄, 릴리스 `tst_lyrics_browser_chain`의 `closingTheBrowserDuringALookupEndsItAtOnce`에서 10초 동안 `/search` 요청이 안 보임. 대기를 늘리고 `waitForCard`가 포기할 때 상태를 남기게 했지만 원인은 찾지 못했다. 같은 라운드의 다른 흔들림(멈춘 페이지 고정 페이지 경쟁, 쉬는 창)은 원인을 찾아 고쳤다.
- 쉬는 창 시험이 1초 대신 3초이고 여전히 시간에 기댄다(리뷰: 시험의 일이지 60초의 제품 결함은 아니라고 봄).
- `aDownloadIsRefused`가 `%TEMP%` 전체를 걷는다. `found()`가 `QCOMPARE`가 실패한 뒤에도 계속된다. 자식 프로세스의 한국어 출력이 깨져 보인다(단언은 맞다).
- 시험하지 않은 처리기: `NewWindowRequested`(WebView2가 클릭 없는 `window.open`을 앞서 막아 "창이 안 뜬다"만 본다), 외부 URI, 기본 인증, 권한 요청, 스크립트 대화상자, `WM_CLOSE`.
- `RENDER_PROCESS_UNRESPONSIVE`는 창을 닫지 않는다(스크립트 한도에 맡김). SDK 헤더 둘레의 `#define __attribute__(x)`(MSVC C++20이 `__has_attribute`를 정의해서). `EnumWindows`를 100 ms마다 보므로 그 사이에 떴다 숨는 창은 못 본다.

## 확인하지 못한 것

- **실제 구글(G5).** 사용자의 허락이 없어 한 번도 묻지 않았다. 구글이 지금 내주는 카드 마크업을 `lyrics_card.js`가 읽는지, 실제 요청 1회로 `Found`가 되는지, 막힘 페이지가 어떻게 오는지 모른다. 카드 스크립트는 고정 페이지(구글 마크업을 흉내 낸 만든 HTML)로만 확인했고 진짜 WebView2의 답은 가짜의 답과 바이트까지 같았다.
- **전체 브랜치 리뷰(G6).** 하지 않았다. Task별 리뷰는 했지만 Task 간 결함과 위 "고치지 않고 남긴 것"은 리뷰되지 않았다.
- **실제 화면 배율과 보이는 Qt 창과 함께일 때의 WebView2.** 시험은 창을 보이지 않으며 숨은 창만 만든다. `WS_POPUP` 도구 창이 -32000에 있다.
- **새 창 거절 줄**: WebView2가 클릭 없는 `window.open`을 이벤트 전에 막아서 우리 거절 코드가 실행되는지는 안 본다.
- 런타임이 없을 때(가짜 자식 프로세스로 확인했다)의 실제 PC, 구글 쪽 재현이 어려운 막힘 3종의 실제 페이지.
- 디버그 빌드에서 `tst_real_google`이 실제로 돌 때의 동작(둘 다 건너뜀만 확인).
- 대조와 `-L webview` 10번은 이번에 다시 돌리지 않았다(인용).

## 인계 항목별 결과

| 인계 | 결과 |
|---|---|
| 시그마와 캐시 키(결정 9) | 캐시 키가 σ/ς를 접는다. 대조는 Σσς가 든 입력의 키를 양쪽 모두 접어서 비교하고, 접힌 사례는 1만 건에 약 100건(그중 절반이 Kotlin 키에 ς)이다. 사용자 결정 대기 |
| `IGNORE_CASE`의 유니코드 접기(Task 1 Step 4) | `kt` 쪽 대소문자 접기를 쓴다. 대조가 아포스트로피·`\b` 두 곳의 차이를 찾아 고쳤다 |
| `HiddenWebView`를 다시 씀 | 새로 만들었다(Rust `browser_window.rs`의 규칙을 따름) |
| `tst_tool_locator` 누수 | 고쳤다. 원인: `QTemporaryDir` 멤버가 Qt Test의 `-functions` 읽기보다 먼저 만들어졌다. 두 빌드의 `ctest`와 `test-names.ps1` 뒤에도 `%TEMP%\qt_temp-*`가 32개로 늘지 않는다(이번 작업에서 다시 확인) |
| 공급자 엮기 | Q4로 넘김(결정 16) |

## Q4·Q5에 넘길 것

- **공급자 엮기와 하나 나눠 쓰기.** 설명란 → LRCLIB → 구글을 `FallbackLyricsProvider`로 묶고, 재생목록 전체가 `GoogleLyricsProvider`(와 채널·`LyricsBrowser`) 하나를 나눠 쓰게 한다. 공급자의 `find`는 어느 스레드에서든 부를 수 있고 차례는 안에서 지켜진다.
- **만들고 없애는 순서.** `LyricsBrowser`는 GUI 스레드에서 만들고 없앤다. 없앨 때의 순서: 작업 핸들 → 공급자 → `LyricsBrowser` → 채널(채널을 먼저 없애려면 `LyricsBrowser`가 사는 스레드에서). `done`이나 WebView2 처리기 안에서 `LyricsBrowser`·`HiddenWebView`를 없애면 안 된다.
- 주 창을 닫을 때 숨은 창이 앱을 붙잡지 않게 하기(Q4).
- 런타임이 없을 때 화면에 알릴지 정하기. 지금은 구글 가사만 "웹뷰를 쓸 수 없음"으로 끝나고 그 곡은 가사 없음, 쉼 없음이다.
- WebView2 사용자 데이터 폴더(제안: `%LOCALAPPDATA%\xGetSongs\qt-webview2`, Tauri 판과 섞지 않음)와 `GoogleLyricsLog`를 로그 파일에 잇기(Q5).
- 사용자의 실행 중인 xGetSongs(Tauri 판)와 사용자 데이터 폴더가 달라 브라우저 프로세스를 나눠 쓰지 않는다.

## 사용자가 정할 것

1. **결정 2**: Kotlin 시험 하나(`theAgentCountsAsPresentForTheFreshnessTimeOnly`)를 옮기지 않고 `theAgentCountsAsPresentOnlyWhileAttached`로 바꾼 것을 승인할지(신선도 시간이 없어져 뜻이 없어진 시험).
2. **결정 9**: 캐시 키가 σ와 ς를 같은 글자로 접는 것을 승인할지(영향: 같은 곡의 두 표기가 캐시를 나눠 쓰는가).
3. **실제 구글 확인**: `tst_real_google`을 한 번 돌려도 되는지(결정 15의 곡, 요청 1회, 출력은 결과 이름과 줄·문단 수뿐). `XGS_INTEGRATION=1`과 `XGS_INTEGRATION_GOOGLE=1`이 필요하다.
4. **건너뛴 전체 브랜치 리뷰**를 지금 돌릴지.
5. **빌드 설정 고치기**: MSVC가 `/showIncludes` 안내를 한국어로 찍어 ninja가 헤더만 바뀐 것을 놓칠 수 있다. 프리셋에 `VSLANG=1033`을 두거나 `CMAKE_CL_SHOWINCLUDES_PREFIX`를 주면 고쳐진다.
6. **`BrowserAgent`가 답을 낼 때 쉬는 시간을 다시 셀지**: 지금은 Rust처럼 명령이 도착한 때부터 세서, 쉬는 창 시험이 계획의 1초 대신 3초를 쓴다. 답이 나갈 때 시간을 새로 세면 Rust와 달라지지만 시험이 단순해진다.

## 라이선스 메모

- OpenJDK(JDK), `kotlinx.coroutines`, `kotlinx.serialization`의 소스는 보지 않았고 기억에서 옮겨 쓰지 않았다. `java.text.Normalizer`·`URLEncoder`·`java.util.regex`의 동작은 공개 문서와 대조 도구가 관찰한 답으로만 정했다.
- Jsoup(MIT)은 1.23.2 jar의 바이트코드를 `javap`로 읽어 `Element.text()`의 공백 규칙을 알았다. Tauri·wry(MIT/Apache-2.0)와 WebView2 SDK 문서는 참고해도 되는 것으로 보았다.
- 이 보고서와 시험 출력에는 가사와 실제 서비스가 돌려준 곡 이름이 없다. 시험의 글은 지어낸 것(`더미 하나` 등)이고, 실제 구글 시험은 결정 15의 곡을 쓰며 출력은 결과 이름과 개수뿐이다.

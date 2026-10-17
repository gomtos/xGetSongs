# 7단계(구글 가사 검색을 WebView2로) 보고서

작성: 2026-10-17. 계획서: [2026-10-16-tauri-webview2-lyrics.md](../plans/2026-10-16-tauri-webview2-lyrics.md). 실행: Native(커밋 `c4c5509..03bb9a4`, 이 보고서를 더하는 커밋 포함 전). 구글 가사 카드를 읽는 숨은 브라우저를 JavaFX WebView에서 셸의 WebView2 창으로 옮기고 사이드카 이미지에서 JavaFX를 뺐다.

## 결과

| 관문 | 결과 |
|---|---|
| G1 `gradlew check` | **975개** 통과(938 + engine 25 + server 12; 리뷰 반영 전 971개), 실패 0, 건너뜀 0. `:server:sidecarImageTest` **5개**(4 + `theImageHasNoJavaFx`) |
| G2 TS | **241개**(239 + 권한 시험 2), `tsc` 종료 코드 0 |
| G3 `sidecar-host` | `lib` **187개**(161 + 브리지 5 + `browser` 20 + `close_gate` 1), `pack_cli` 5개, `real_sidecar`(`--ignored`, 새 이미지) 3개 통과(새 시험 2개 포함) |
| G4 셸 빌드 | 일반 빌드와 점검용 빌드 모두 통과. `Cargo.lock`은 두 줄만 늘었다(셸의 의존성 목록: `webview2-com`, `windows-core 0.62.2`), 새로 컴파일되는 크레이트 없음 |
| G5 숨은 창 점검(`shell/tools/browser-check/check.ps1`, 구글 없음) | `ALL OK`: 창 생성 137~168ms·숨겨짐, 숨은 창에서 타이머 500ms에 10번, 창 재사용, 새 창은 쿠키가 없음, 다운로드 거부(파일이 남지 않음), 응답 없는 페이지는 5.0초에 포기하고 다음 창이 정상, 숨은 창이 열려 있어도 주 창을 닫으면 0.7초 안에 앱과 사이드카가 끝남 |
| G6 크기와 종단 | `dist\xGetSongs.exe` **90,381,205바이트 = 86.2MB**(129.2MB에서 43.0MB 줄었다; 사용자가 `package-exe.bat`으로 만들어 90MB 이하임을 확인했다). 묶인 이미지 103.7MB(485파일, 147.9MB에서 줄었다). 깨끗한 PC 점검 7개 시나리오 `ALL OK`(`ok` 38줄, `FAIL` 0). 시간(5회 중앙값): 푸는 데 2.0초, 첫 실행 사이드카 대기 3.7초(6단계 4.9초), 두 번째 1.2초(1.4초). **포장한 exe의 화면 점검은 돌리지 못했다**: 사용자의 `xGetSongs.exe`가 떠 있어 단일 인스턴스가 두 번째 실행을 막았다(사용자의 앱을 끌 수는 없다). 깨끗한 PC 점검이 같은 코드 경로를 점검용 페이지로 이미 확인한다 |
| G7 실제 구글 | **통과**(2026-10-17, 사용자의 허락). 점검용 셸(`XGS_CHECK_LYRICS_URL`, `check.ts`의 시나리오)이 실제 사이드카 이미지로 영상 하나("RESCENE (리센느) 'LOVE ATTACK' Official MV")의 작업을 가사 검색을 켠 채 돌렸다: 사슬 전체(사이드카 → `BrowserChannel` → 셸의 에이전트 → 숨은 WebView2 → 구글)를 지나 로그에 `구글 가사 카드: 찾음 (줄 80, 문단 13)`, 작업은 `COMPLETED`(성공 1, 실패 0), 파일은 `가사 ONLINE`. **구글 요청은 한 번**(첫 조회에서 찾아 두 번째 조회는 없었다). Edge User-Agent에도 동의창·CAPTCHA 없이 가사 카드가 나왔다. 처음 돌린 한 번은 LRCLIB가 이 곡을 갖고 있어 구글 단계가 쓰이지 않았다(로그에 구글 줄이 없었다): 그래서 두 번째에는 사이드카의 JVM에만 `JAVA_TOOL_OPTIONS=-Djdk.net.hosts.file=…`로 `lrclib.net`을 이 PC로 돌려 LRCLIB가 내려간 상황을 만들었다(제품 코드는 그대로). 구글에는 합쳐 한 번만 물었다 |
| G8 전체 리뷰 | 아래 "최종 리뷰": Critical 0, Important 2는 고쳤고 Minor는 미뤘다 |

## 계획과 다르게 한 것 (원장의 Ruling)

- `DrivenBrowser`에 기본값이 있는 `timeSource` 매개변수를 더했다. `PageWatch`가 시간원을 필요로 하는데 계획의 서명에 없어서 "설정 시간 동안 같은 주소로 머문다"를 가상 시간으로 시험할 수 없었다.
- `BrowserChannel`의 대기열은 `onUndeliveredElement`로 되돌려 넣고, `ConcurrentHashMap`은 `containsKey`로 본다(컴파일러가 `in`이 `containsValue`임을 알려 줬다).
- `BrowserChannel.hasAgent`(공개, 읽기 전용)를 더했다. 이미 호출이 하던 신선도 판단이고, 라우트·사슬 시험이 잠으로 맞추지 않고 이것을 기다린다.
- 라우트·사슬 시험은 `testApplication`이 아니라 실제 Netty 서버(`LocalServer.start` + CIO 클라이언트)로 한다: 긴 대기가 실제 시간으로 흘러야 하고 15초 무응답 한도가 가상 시계에서 먼저 터지면 안 된다.
- 가짜 카드는 세 줄이다(가사 정리 `LyricsExtractor.tidy`가 세 줄 미만이면 null).
- `real_sidecar.rs`의 `work_folder()`가 프로세스 번호만 써서 병렬로 도는 시험끼리 서로의 폴더를 지웠다(기존 시험이 실패하는 것을 봤다): 시험마다 이름을 붙인다.
- `check_browser(base_url, keep_open)`은 한 주소를 여는 것이 아니라 고정 페이지 전체를 도는 점검이다. 고정 페이지 서버와 스크립트는 `%TEMP%`가 아니라 저장소(`shell/tools/browser-check/`)에 둔다.

## 점검이 잡은 결함

- **같은 라벨의 창을 바로 다시 만들 수 없다.** 첫 점검에서 `window.close()` 직후의 `load`가 "a webview with label `xgs-browser` already exists"로 거부됐다. `destroy()`된 창이 한동안 라벨을 쥐고 있기 때문이다. 에이전트는 실패한 페이지를 닫고 다음 `load`가 새 창을 만드는 것이 규칙이라서 실제로 응답 없는 페이지 뒤에 가사 검색이 계속 실패했을 것이다. 창마다 `xgs-browser-N`이라는 고유 라벨을 쓴다(권한 시험은 접두사를 지킨다).
- **숨은 창이 앱을 살려 둔다.** 종료 처리를 끈 빌드에서 점검이 실패했다(주 창을 닫고 5초 뒤에도 앱과 사이드카가 남음). 주 창이 없어지면 `exit(0)`를 부른다.

## 한계

- 점검 도구(`%TEMP%\xgs-bridge-check\helpers.ps1`의 `Close-CheckShell`)는 프로세스의 첫 `Tauri Window` 클래스 창을 닫는다. 숨은 브라우저 창이 있으면 그 창이 먼저 잡혀 주 창이 닫히지 않고 점검용 셸이 남는다(G7 두 번째 실행에서 그랬고 손으로 끝냈다; 리뷰 지적 1의 도구 쪽 증상이고 제품은 영향이 없다). 제목이나 라벨로 주 창을 고르도록 도구를 고쳐야 한다(저장소 밖).
- 실제 구글 확인(G7)은 곡 하나, 요청 한 번뿐이다: 이 PC·이 시간대의 한 번이고 동의창·CAPTCHA·차단이 다른 곳에서 어떻게 뜰지는 모른다(그때의 동작은 6단계까지와 같다: 이름을 로그에 남기고 쉰다). 예전 `RealGoogleLyricsIntegrationTest`(구글에 한 번 묻고 구조만 단언)의 자리를 이어받는 자동 시험은 없다: 대신 서버 안 사슬 시험, 구글 없는 숨은 창 점검, 수동 G7(`XGS_CHECK_LYRICS_URL`)이다.
- **새 창 거부(`on_new_window`)는 시험하지 못한다.** WebView2는 클릭 없는 `window.open`을 막아서 고정 페이지로는 팝업이 열리지 않는다(그 줄을 지워도 점검이 잡지 못했다). 시크릿(`incognito`)과 다운로드 거부(`on_download`)는 줄을 지우면 각각 쿠키·파일 저장으로 점검이 실패한다.
- 실제 yt-dlp·네트워크가 필요한 `real_download`는 이 단계에서도 돌리지 않았다.
- 숨은 창을 만드는 데 0.14초면 되지만(점검), 첫 구글 조회에서 WebView2 렌더러가 새로 뜨는 시간은 한 번의 조회 시간 한도(20초) 안에 있다.

## 최종 리뷰

가장 유능한 모델(opus)이 `a524de5..faa69ae` 전체를 새 맥락에서 읽었다. Critical 0, Important 2, Minor 6.

**고친 것** (둘 다 시험이 먼저 실패하는 것을 봤다):

- **Important 1: 숨은 창에 닿은 닫기 요청이 주 창의 닫기 문턱을 써 버린다.** `on_window_event`의 `CloseRequested` 가지가 창 라벨을 보지 않았다. 창을 닫는 도구(점검 도우미의 `Get-AppWindow`는 클래스 `Tauri Window`의 첫 창을 고르고 숨은 창도 같은 클래스다)나 떠도는 WM_CLOSE가 숨은 창에 닿으면 문턱(`CloseGate`)이 "물어봄" 상태가 되어 앱이 닫히거나(페이지가 `close_window`로 답함) 주 창이 닫히지 않게 된다(다음 닫기마다 "기다림"). 점검 단계 `closeRequestOnTheHiddenWindowIsHarmless`가 고치기 전 코드에서 그대로 재현했고(숨은 창이 살아남고 앱과 사이드카가 남음), `window.label() == "main"` 조건으로 통과했다. 점검이 문턱을 쓰지 않고 보도록 `CloseGate::was_asked`를 더했다(단위 시험 포함).
- **Important 2: 스크립트를 실행하지 못했을 때 구글 시간 초과로 센다.** 에이전트는 `evaluate`가 실패하면 창을 닫는데, `BrowserChannel.evaluate`가 `ok:false`를 `null`("아직 준비 안 됨")로 바꿔서 그 뒤 모든 조회가 20초 한도까지 닫힌 창을 들여다보다가 `Timeout`으로 끝나고 5~60분 쉼이 시작되며 로그에는 구글이 응답하지 않은 것처럼 남는다. 이제 실행하지 못한 스크립트는 `BrowserUnavailableException`이고 그 곡의 구글 조회는 바로 "브라우저를 쓸 수 없음"으로 끝난다(쉼 없음). `outerHTML` 읽기 실패와 값 없음도 같은 길이다(빈 페이지로 읽혀 "카드 없음"으로 세션 내내 기억되던 문제, Minor 3).

**미룬 Minor:**
- 에이전트 스레드는 `wait_ready` 실패가 한 번이면(준비 대기 90초 초과 포함) 세션 내내 끝난다. 그러면 가사 조회가 모두 `no-agent`다. 풀기와 JVM 시작은 합쳐 약 6초라서 지금은 닿지 않는다.
- 제품 빌드에서도 `http://` 루프백 이동이 허용된다(점검에만 필요). 개발 빌드의 `localhost:1420`은 Tauri의 로컬 출처다. 문서의 "https만 연다"는 문장은 고쳤다.
- 권한 시험은 `webviews` 키와 `tauri.conf.json`의 인라인 권한을 보지 않고, 숨은 창 시험은 실제 라벨(`xgs-browser-1`) 대신 접두사를 본다.

**알려 둘 것:** 사용자가 직접 돌리는 `packed-screen-check.ps1`은 실제 영상을 기본 옵션으로 받는다. "가사가 없으면 인터넷에서 검색"이 기본으로 켜져 있어서, 영상 설명과 LRCLIB에 가사가 없으면 **실제 구글 요청이 나간다**(G7의 허락 없이도).

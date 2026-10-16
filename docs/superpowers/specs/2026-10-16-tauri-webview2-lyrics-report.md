# 7단계(구글 가사 검색을 WebView2로) 보고서

작성: 2026-10-17. 계획서: [2026-10-16-tauri-webview2-lyrics.md](../plans/2026-10-16-tauri-webview2-lyrics.md). 실행: Native(커밋 `c4c5509..03bb9a4`, 이 보고서를 더하는 커밋 포함 전). 구글 가사 카드를 읽는 숨은 브라우저를 JavaFX WebView에서 셸의 WebView2 창으로 옮기고 사이드카 이미지에서 JavaFX를 뺐다.

## 결과

| 관문 | 결과 |
|---|---|
| G1 `gradlew check` | **971개** 통과(938 + engine 21 + server 12), 실패 0, 건너뜀 0. `:server:sidecarImageTest` **5개**(4 + `theImageHasNoJavaFx`) |
| G2 TS | **241개**(239 + 권한 시험 2), `tsc` 종료 코드 0 |
| G3 `sidecar-host` | `lib` **186개**(161 + 브리지 5 + `browser` 20), `pack_cli` 5개, `real_sidecar`(`--ignored`, 새 이미지) 3개 통과(새 시험 2개 포함) |
| G4 셸 빌드 | 일반 빌드와 점검용 빌드 모두 통과. `Cargo.lock`은 두 줄만 늘었다(셸의 의존성 목록: `webview2-com`, `windows-core 0.62.2`), 새로 컴파일되는 크레이트 없음 |
| G5 숨은 창 점검(`shell/tools/browser-check/check.ps1`, 구글 없음) | `ALL OK`: 창 생성 137~168ms·숨겨짐, 숨은 창에서 타이머 500ms에 10번, 창 재사용, 새 창은 쿠키가 없음, 다운로드 거부(파일이 남지 않음), 응답 없는 페이지는 5.0초에 포기하고 다음 창이 정상, 숨은 창이 열려 있어도 주 창을 닫으면 0.7초 안에 앱과 사이드카가 끝남 |
| G6 크기와 종단 | `dist\xGetSongs.exe` **90,381,205바이트 = 86.2MB**(129.2MB에서 43.0MB 줄었다; 사용자가 `package-exe.bat`으로 만들어 90MB 이하임을 확인했다). 묶인 이미지 103.7MB(485파일, 147.9MB에서 줄었다). 깨끗한 PC 점검 7개 시나리오 `ALL OK`(`ok` 38줄, `FAIL` 0). 시간(5회 중앙값): 푸는 데 2.0초, 첫 실행 사이드카 대기 3.7초(6단계 4.9초), 두 번째 1.2초(1.4초). **포장한 exe의 화면 점검은 돌리지 못했다**: 사용자의 `xGetSongs.exe`가 떠 있어 단일 인스턴스가 두 번째 실행을 막았다(사용자의 앱을 끌 수는 없다). 깨끗한 PC 점검이 같은 코드 경로를 점검용 페이지로 이미 확인한다 |
| G7 실제 구글 | **미실행.** 사용자의 허락이 있어야 한다 |
| G8 전체 리뷰 | 아래 "최종 리뷰" |

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

- **실제 구글의 반응은 확인하지 못했다**(Edge User-Agent로 가사 카드가 나오는지, 동의창·CAPTCHA가 뜨는지). 허락을 받으면 G7로 로그의 `구글 가사 카드: …` 줄을 본다. 예전 `RealGoogleLyricsIntegrationTest`(구글에 한 번 묻고 구조만 단언)의 자리를 이어받는 자동 시험은 없다: 대신 서버 안 사슬 시험, 구글 없는 숨은 창 점검, 수동 G7이다.
- **새 창 거부(`on_new_window`)는 시험하지 못한다.** WebView2는 클릭 없는 `window.open`을 막아서 고정 페이지로는 팝업이 열리지 않는다(그 줄을 지워도 점검이 잡지 못했다). 시크릿(`incognito`)과 다운로드 거부(`on_download`)는 줄을 지우면 각각 쿠키·파일 저장으로 점검이 실패한다.
- 실제 yt-dlp·네트워크가 필요한 `real_download`는 이 단계에서도 돌리지 않았다.
- 숨은 창을 만드는 데 0.14초면 되지만(점검), 첫 구글 조회에서 WebView2 렌더러가 새로 뜨는 시간은 한 번의 조회 시간 한도(20초) 안에 있다.

## 최종 리뷰

(아직 하지 않았다.)

# C++/Qt 전면 재작성(B안) 분석

작성일: 2026-11-06
상태: **분석만 했고 결정은 보류.** 크기 수치는 현행만 측정값이고 Qt 쪽은 모두 추정이다. 권고는 §8에 있다.
관련: [Tauri 사이드카 설계](2026-10-10-tauri-kotlin-sidecar-design.md), [7단계 보고서(WebView2 가사)](2026-10-16-tauri-webview2-lyrics-report.md), [5단계 포장 보고서](2026-10-15-tauri-packaging-report.md), [Python 이행 분석](2026-10-16-python-single-exe-migration-analysis.md)

## 1. 질문과 전제

"Qt로 바꾸기"에는 두 가지 뜻이 있다.

- **A안: 화면만 Qt로 바꾼다.** Tauri와 TS 화면을 Qt Widgets로 바꾸고 Kotlin 사이드카는 그대로 둔다. exe 크기의 95%가 JVM과 jar라서 크기는 줄지 않고 오히려 5~10MB 커진다(Tauri 셸 4.8MB → Qt 약 10~15MB). 사이드카 구조도 그대로 남는다. 얻는 것이 거의 없어서 이 문서에서는 다루지 않는다.
- **B안(이 문서): 전부 C++/Qt로 다시 쓴다.** Kotlin 백엔드, Rust `sidecar-host`, Tauri 셸, TS 화면을 모두 C++ 한 프로세스(Qt 6 Widgets)로 바꾼다. JVM, 사이드카, HTTP 서버, Rust 브리지가 모두 사라진다.

기준선(2026-11-06, `main` 기준):

| 항목 | 값 | 근거 |
|---|---|---|
| `dist\xGetSongs.exe` | **86.2MB** (90,381,205바이트) | 7단계 보고서 G6 |
| ㄴ Tauri 셸 | 약 4.8MB | 5단계 보고서 |
| ㄴ 붙인 이미지 | 풀면 103.7MB(485파일): Java 런타임(JBR 21) 약 80MB + 서버 jar 약 23MB. 압축해서 약 81MB | 7단계 보고서, `server/build.gradle.kts` |
| 시작 시간 | 창 약 50ms. 사이드카 준비는 첫 실행 3.7초(푸는 데 2.0초), 두 번째부터 1.2초 | 7단계 보고서 |
| Kotlin 본 코드 / 테스트 | `shared` 965 / 1,787, `engine` 2,677 / 7,015, `server` 952 / 2,762, `diagnostics` 988 / 2,237. 합계 **5,582 / 13,801줄**, `@Test` 997개 | 이 문서를 쓰며 센 값 |
| Rust | `sidecar-host/src` 4,530줄, `shell/src-tauri/src` 643줄 | 같음 |
| TS | 화면 2,396줄, 화면 시험 3,092줄 | 같음 |
| 외부 도구 | yt-dlp, ffmpeg, Node.js/Deno는 어느 안이든 exe 밖에 남는다 | README "필요한 것" |

## 2. 무엇이 무엇으로 바뀌나

| 지금 | 하는 일 | C++/Qt에서 | 비고 |
|---|---|---|---|
| `shared` (Kotlin) | 입력 판별, 제목 파싱, 파일명, 가사 추출, 로그 가리기 | 순수 C++ + `QString` | **`QString`은 UTF-16이라** 파일명 길이 제한(`FilenameFormatter.kt`의 `text.length`, 서로게이트 보호)을 Kotlin·TS와 같은 단위로 셀 수 있다. Python안(§3의 2번)의 함정이 없다 |
| `engine/process` | 외부 프로세스 실행, 취소 시 자손까지 종료 | `QProcess` + Win32 Job Object | `QProcess::kill()`은 자식만 죽인다. 자손(yt-dlp가 띄운 ffmpeg 등)은 Job Object에 넣어야 한다. 지금 `sidecar-host`가 `win32job`으로 하는 일과 같다 |
| `engine/ytdlp` | 명령 조립, 진행률·오류·info JSON 파싱 | `QJsonDocument`, `QRegularExpression` | 정규식의 의미 차이(유니코드 클래스 등)는 확인하지 않았다 |
| `engine/tags` | ffmpeg로 태그·커버 쓰기 | 같은 ffmpeg 명령을 `QProcess`로 | 로직은 그대로 옮긴다 |
| `engine/lyrics` (LRCLIB) | HTTPS 조회, 매칭 | `QNetworkAccessManager` | Qt 6의 Windows TLS 백엔드는 Schannel이라 OpenSSL을 넣지 않아도 된다(플러그인 `qschannelbackend`) |
| `engine/lyrics/google` | 숨은 브라우저 조종(`BrowserChannel` → HTTP 폴링 → 셸의 WebView2) | **같은 프로세스 안에서 WebView2 COM을 직접 사용** | 폴링 채널(`BrowserChannel`, `browser.rs`, 라우트)이 사라진다. `LyricsCardParser`가 쓰는 jsoup의 대안이 필요하다: 페이지 안에서 JS로 줄을 뽑아 오거나(이미 `evaluate`로 하는 일과 같은 방식), lexbor·gumbo 같은 C 파서를 넣는다 |
| `engine/job` | 동시 다운로드(코어 수의 70%), 취소, 진행 이벤트 | `QThreadPool`/`QtConcurrent` + 시그널 | 코루틴 구조화 동시성에 대응하는 것이 없어 취소 전파를 직접 짜야 한다 |
| `server` (Ktor) | 127.0.0.1 HTTP·SSE, 토큰, Guard | **없어진다** | 화면이 같은 프로세스에서 시그널로 받는다. 대신 "웹으로 옮길 수 있게"(설계 §1)를 잃는다 |
| `diagnostics` | logback 회전 로그, 종료 기록, `last-run.txt`, 멈춤 감시(JVM 스레드 덤프) | 자체 회전 로거(또는 spdlog), `qInstallMessageHandler`, `MiniDumpWriteDump` | 스레드 덤프는 JVM `ManagementFactory`라 그대로 옮길 수 없다. 대신 미니덤프는 크래시까지 남길 수 있다(지금은 셸이 갑자기 끝나면 기록이 없다) |
| `sidecar-host` (Rust) | 사이드카 시작·종료, 브리지, settings.json, 폴더 열기, 런타임 풀기, `xgs-pack` | 대부분 **없어진다** | settings.json은 `QJsonDocument` + `QStandardPaths`, 폴더 열기는 `explorer /select,`. 런타임 풀기(`payload.rs` 802줄)는 정적 링크면 필요 없다 |
| `shell` (Tauri + TS) | 화면, 단일 인스턴스, 닫기 문턱 | Qt Widgets(`QTableView` + 모델), `QLockFile` + `QLocalServer` | 화면 로직은 위젯과 분리해 두어야 Qt Test로 시험할 수 있다 |

## 3. 크기 추정

| 구성 | 정적 링크 | 동적 링크(DLL 동봉) |
|---|---|---|
| 앱 코드 | exe에 포함 | exe 약 1~3MB |
| Qt Core·Gui·Widgets·Network + `qwindows` + 스타일 | 링크된 만큼만 들어가서 exe 약 8~15MB(LTO) | DLL 약 20~25MB. `opengl32sw.dll`(약 20MB)과 `D3Dcompiler`는 Widgets 앱이라 뺀다 |
| WebView2 로더 | `WebView2LoaderStatic.lib` 수백 KB | `WebView2Loader.dll` 약 0.2MB |
| VC++ 런타임 | `/MT`로 포함 | 약 1MB 동봉 |
| **exe 하나로 낼 때** | **약 10~18MB** | 지금처럼 exe 뒤에 붙여 압축하면 **약 12~15MB** |

비교:

| 경로 | exe 크기 | 첫 실행 준비 | 두 번째 실행 |
|---|---|---|---|
| 현행 (측정) | 86.2MB | 3.7초 | 1.2초 |
| `jlink`로 런타임만 줄인 현행 (추정) | 약 50~60MB | 3초 안팎 | 1.2초 |
| **B안, 가사는 WebView2** (추정) | **약 10~18MB** | 정적이면 풀 것이 없어 1초 미만 | 1초 미만 |
| B안 + QtWebEngine (추정) | 약 80~100MB | Chromium 풀기 필요 | — |
| B안, 구글 가사 삭제 (추정) | B안과 거의 같음 | — | — |

- QtWebEngine은 Chromium을 통째로 넣어서(`Qt6WebEngineCore.dll`, `.pak`, ICU 데이터, `QtWebEngineProcess.exe`, 풀면 150MB 이상) B안의 크기 이득을 거의 다 없앤다. **B안은 구글 가사를 WebView2로 유지하거나 삭제한다는 전제에서만 의미가 있다.**
- UPX 같은 실행 압축기는 백신 오탐이 잦아서 쓰지 않는다고 본다.

## 4. 장점

1. **크기와 시작 시간(가장 확실한 이득).** 86.2MB → 약 10~18MB. JVM 기동과 런타임 풀기가 없으니 첫 실행과 두 번째 실행이 같고 둘 다 1초 미만으로 본다. 첫 실행 때 `%LOCALAPPDATA%`에 100MB를 푸는 일도 없어진다.
2. **구조가 단순해진다.** 사라지는 것: 사이드카 프로세스, `XGS-READY` 핸드셰이크, stdin 종료 신호, `ParentWatch`, 토큰과 `Guard`, HTTP·SSE, Rust 브리지, 숨은 브라우저용 폴링 채널, 런타임 풀기와 `xgs-pack`. 이 부분들 때문에 생긴 문제(이벤트를 한 번만 내주는 SSE 때문에 새로고침을 막은 일, 연결이 끊기면 같은 작업에 다시 붙지 못하는 제한 등, README "알려진 제한")가 함께 없어진다.
3. **언어가 하나.** 지금은 Kotlin, Rust, TS 세 가지이고 빌드 도구도 Gradle, Cargo, npm 세 가지다. B안은 C++와 CMake뿐이다.
4. **WebView2가 없어도 창이 뜬다.** WebView2가 필요한 것은 구글 가사 단계뿐이고, 없으면 그 단계만 꺼진다.
5. **메모리.** JVM 힙과 웹뷰 렌더러 프로세스가 없어진다(측정하지 않음).
6. **크래시 기록.** 미니덤프로 지금은 남지 않는 "창이 갑자기 사라짐"의 원인을 남길 수 있다.

## 5. 단점과 위험

1. **재작성 비용과 회귀 위험(가장 큰 비용).** 본 코드는 Kotlin 5,582줄 + TS 2,396줄 + 필요한 Rust 일부이고, 테스트는 Kotlin 13,801줄(997개) + TS 3,092줄이다. 이 프로젝트의 가치는 테스트(제목 파싱 픽스처, LRCLIB 매칭, 가사 구역 추출, 오류 분류)에 있으니 테스트를 먼저 같은 이름으로 옮기고, 3단계에서 Kotlin과 TS를 대조할 때 쓴 무작위 입력 대조 도구(`shell/tools/differential`)를 C++용으로 다시 만들어야 한다.
2. **동시성과 취소.** 코루틴의 구조화 동시성(취소 전파, 타임아웃, `withContext`)에 1:1로 대응하는 것이 Qt에 없다. 동시 다운로드, 곡별 취소, 가사 검색의 쉬는 시간(5·10·20…60분), 10초 간격 같은 규칙을 스레드 풀과 시그널로 다시 짜야 하고, 이 부분이 회귀가 가장 나기 쉽다.
3. **메모리 안전성.** Kotlin과 Rust가 막아 주던 수명·스레드 문제를 C++에서는 직접 관리해야 한다. 특히 `QObject`는 자신이 속한 스레드에서만 다뤄야 하고, WebView2 COM은 STA인 GUI 스레드에서만 불러야 한다.
4. **라이선스.** §6.
5. **웹 이식 목표를 잃는다.** 설계 §1의 "나중에 웹으로"를 위해 둔 HTTP API와 TS 화면이 없어진다. 나중에 웹이 필요하면 서버를 따로 만들어야 한다.
6. **UI 품질을 다시 확인해야 한다.** 이 프로젝트는 UI 품질(한글 입력, 글꼴, 네이티브 느낌) 때문에 Compose에서 Tauri로 옮겼다. Qt 6 Widgets의 한글 IME와 HiDPI는 대체로 괜찮지만 직접 확인해야 한다(0a 관문 U2~U4와 같은 조건). 스타일은 CSS보다 QSS가 제약이 많다.
7. **빌드 환경.** MSVC + CMake + Qt SDK(수 GB). 정적 Qt는 공식 바이너리가 없어서 Qt를 직접 빌드하거나(수십 분~수 시간) vcpkg의 `qtbase:x64-windows-static`을 쓴다.
8. **구글 가사의 HTML 파싱.** jsoup의 대안이 필요하다(§2 표). 페이지 안에서 JS로 뽑는 방식이 의존성은 없지만, 지금 Kotlin에서 단위 시험하는 `LyricsCardParser`의 시험이 JS 쪽으로 옮겨 가 시험하기가 더 어려워진다.
9. **멈춤 감시의 성격이 바뀐다.** 지금은 서버 스레드가 5초 넘게 응답하지 않으면 JVM의 모든 스레드 스택을 텍스트로 남긴다. C++에서는 미니덤프(분석에 WinDbg와 PDB 필요)나 자체 감시 스레드로 바뀌어서 로그만 보고 원인을 알기는 지금보다 어렵다.

## 6. 라이선스 (단일 exe일 때 중요)

Qt 6의 qtbase(Core, Gui, Widgets, Network)는 LGPLv3 또는 상용 라이선스다.

| 방식 | LGPLv3에서의 의무 | 단일 exe |
|---|---|---|
| 동적 링크 | Qt DLL을 사용자가 바꿀 수 있으면 됨. 라이선스 고지와 Qt 소스 제공 방법을 안내 | exe 뒤에 DLL을 붙이고 첫 실행에 푸는 방식(지금의 `payload.rs`와 같은 방식)이 필요하다. 풀 크기가 25MB 정도라 지금(100MB)보다는 가볍다 |
| 정적 링크 | 사용자가 다른 Qt로 **다시 링크할 수 있게** 앱의 오브젝트 파일(또는 소스)을 제공해야 함 | 바로 exe 하나 |
| 상용 | 의무 없음 | 바로 exe 하나. 비용이 든다 |

- Qt Charts처럼 GPL로만 제공되는 모듈은 쓰지 않는다(앱이 GPLv3라면 써도 된다).
- 이 문서는 법률 자문이 아니다. 배포하기 전에 Qt의 라이선스 문서로 확인한다.

### 6.1 무료 오픈 소스로 공개할 때

**이 앱의 소스를 공개하면 정적 링크로 exe 하나를 내도 큰 문제가 없다.** LGPLv3가 정적 링크에 요구하는 핵심은 "사용자가 Qt를 다른 버전으로 바꿔 앱을 다시 만들 수 있어야 한다"는 것이다. 비공개 앱은 이를 위해 오브젝트 파일을 따로 제공해야 하지만, 소스와 빌드 방법이 공개돼 있으면 사용자가 직접 다시 빌드할 수 있으니 이 요건이 채워진다. 상용 라이선스도 필요 없다.

그래도 지켜야 할 것:

1. **앱의 라이선스를 정한다.** 지금 저장소에는 `LICENSE` 파일이 없다. 라이선스가 없으면 법적으로는 "모든 권리 보유"라서 소스를 공개해도 오픈 소스가 아니다. MIT, Apache-2.0, GPLv3, LGPLv3는 모두 LGPLv3와 함께 쓸 수 있다. 앱 라이선스가 수정이나 디버깅용 역분석을 금지하면 안 되는데, 이 넷은 금지하지 않는다.
2. **배포한 exe와 소스 버전을 맞춘다.** 릴리스마다 태그를 붙여 "이 exe는 이 커밋으로 만들었다"를 알 수 있게 하고, 빌드 방법(CMake 설정, 쓴 Qt 버전, 정적 빌드 옵션)을 문서로 남긴다.
3. **고지한다.** 앱의 정보 창이나 동봉 문서에 Qt를 LGPLv3로 쓴다는 사실과 Qt의 저작권, LGPLv3 전문, 사용한 Qt 버전의 소스를 받을 수 있는 곳을 적는다. Qt를 고치지 않았다면 Qt 공식 소스 위치를 안내하는 정도면 되고, 고쳤다면 고친 부분도 공개한다.
4. **다른 구성 요소.** WebView2 SDK는 로더를 함께 배포해도 되는 BSD 계열 라이선스다. yt-dlp와 ffmpeg는 exe에 넣지 않고 사용자가 따로 설치하므로 이 앱의 배포 의무와 상관이 없다.

오픈 소스여도 남는 문제(Qt와 별개):

- 구글 검색 결과를 자동으로 조회하는 것은 구글 약관상 문제가 되고, YouTube에서 받는 것과 가사에는 저작권 문제가 있다(README "가사 검색", "주의"). 공개 배포하면 "개인 용도"라는 전제가 약해지므로, 공개하기 전에 구글 가사 단계를 빼거나 정식 API(Musixmatch 등)로 바꾸는 것을 함께 검토한다.

## 7. 한다면: 단계와 관문

현행 이행처럼 단계마다 관문을 두고, 앞 단계가 통과해야 다음으로 간다.

| 단계 | 내용 | 관문 |
|---|---|---|
| Q0 | 스파이크(§9) | Q1~Q7. Q4, Q5, Q6 중 하나라도 실패하면 접는다 |
| Q1 | `shared` 이식 + 테스트 이식 + Kotlin과 무작위 입력 대조 | `shared` 테스트(1,787줄)를 같은 이름으로 모두 통과. 대조에서 불일치 0 |
| Q2 | `engine`의 프로세스, yt-dlp, 태그, LRCLIB, 작업과 동시성, 취소 | `engine` 단위 테스트 이식분 통과. 통합 테스트(실제 yt-dlp, LRCLIB) 통과. 취소 시 자손까지 5초 안에 정리(M3과 같은 조건) |
| Q3 | 구글 가사를 같은 프로세스의 WebView2로 | 7단계의 G 시나리오(구글 없는 점검)와 실제 구글 확인(G7) 재통과 |
| Q4 | Qt Widgets 화면, 설정, 단일 인스턴스, 닫기 문턱 | 지금 화면 시험(`shell/tests`)의 로직 부분 이식분 통과. 한글 IME, 붙여넣기, 우클릭 메뉴 직접 확인 |
| Q5 | 로그, 종료 기록, `last-run.txt`, 멈춤 감시, 미니덤프 | `diagnostics` 테스트 이식분 통과. 로그에 토큰·가사·제목·파일 이름이 남지 않음(지금의 가리기 규칙 유지) |
| Q6 | 포장, 깨끗한 PC 점검 | 5단계의 깨끗한 PC 시나리오 7개 재통과. 크기·시작 시간을 기준선과 같은 방법(5회 중앙값)으로 측정 |
| Q7 | 옛 코드 삭제, 문서 갱신 | Kotlin·Rust·TS 코드와 Gradle·Cargo·npm 설정 제거. README 갱신 |

## 8. 권고

**지금은 하지 않는다.** 이유:

1. B안의 확실한 이득은 크기(86 → 약 15MB)와 시작 시간인데, 이 둘은 Tauri 이행의 비목표였다(설계 §1). 지금 앱이 크기나 시작 시간 때문에 요건을 못 맞추고 있다는 근거는 없다.
2. 크기만 문제라면 **`jlink`로 런타임을 줄이는 쪽이 훨씬 싸다**(5단계 계획에서 "남은 일"로 미룬 것). Temurin 21 JDK의 jmods로 필요한 모듈만 남기면 런타임 약 80MB가 절반 정도로 줄어 exe가 약 50~60MB가 될 것으로 본다(측정하지 않음). 코드는 `server/build.gradle.kts`의 `sidecarImage` 작업만 바뀐다.
3. B안의 비용(테스트 1만 7천 줄 규모의 이식, 동시성 재설계, 라이선스 정리, 웹 이식 포기)은 크기 이득에 비해 크다.

## 8.1 B안을 검토할 조건

아래가 모두 맞을 때 검토한다.

1. `jlink`로 줄인 뒤에도 크기나 첫 실행 시간이 요건을 못 맞춘다(예: 배포 채널의 크기 제한, 저사양 PC에서 첫 실행이 너무 느림).
2. 웹으로 옮기는 계획을 접어도 된다.
3. 구글 가사를 WebView2로 유지하거나 삭제한다(QtWebEngine은 쓰지 않는다).
4. Qt 라이선스 방식(동적, 정적 + 소스 공개 또는 오브젝트 제공, 상용) 중 하나를 정했다(§6.1).

## 9. 결정 전에 싸게 확인할 것

**스파이크(하루~이틀):** 정적 링크한 Qt 6 Widgets exe 하나에 아래를 넣고 잰다. 값은 제안값이고 바꿀 수 있다.

| 번호 | 조건 | 제안 한도 |
|---|---|---|
| Q1 | 깨끗한 Windows(가짜 `APPDATA`·`LOCALAPPDATA`·`TEMP`, 5단계와 같은 방법)에서 exe가 뜨고 창이 보인다 | 통과 여부 |
| Q2 | 더블클릭 → 창이 뜨고 조작할 수 있을 때까지, 첫 실행과 두 번째 실행 각각 5회 중앙값 | 현행(첫 실행 3.7초, 두 번째 1.2초)보다 나쁘지 않음 |
| Q3 | exe 크기 (Qt Core·Gui·Widgets·Network + WebView2 로더 포함) | 기록만. 추정은 10~18MB |
| Q4 | Windows Defender·SmartScreen이 막거나 경고하는가 | 막으면 중단 |
| Q5 | `QProcess` + Job Object로 yt-dlp를 띄우고 취소하면 yt-dlp와 ffmpeg 자손까지 5초 안에 정리되는가 | M3(자손까지 5초 안에 정리)과 같은 조건 |
| Q6 | `QLineEdit`에서 한글 조합 입력, 붙여넣기, 선택, 우클릭 메뉴가 되고, 150%·200% 배율에서 글꼴이 흐리지 않다 | 0a의 U2~U4와 같은 조건. 사용자가 직접 확인 |
| Q7 | 숨긴 WebView2(같은 프로세스, GUI 스레드)로 페이지를 열고 스크립트 결과를 받는다 | 7단계의 점검 페이지(`shell/tools/browser-check/fixture.mjs`가 띄우는 고정 페이지, `browser_check.rs`가 쓰는 것)로 확인. 구글은 부르지 않는다 |
| Q8 | `QNetworkAccessManager`로 `https://lrclib.net`을 Schannel 백엔드로 부를 수 있다(OpenSSL 없이) | 통과 여부 |

`jlink` 스파이크도 같이 하면 비교가 된다: Temurin 21 JDK로 `sidecarImage`의 런타임을 `jlink` 결과로 바꾸고 `package-exe.bat`의 크기와 5단계 시간 측정을 다시 돌린다.

## 10. 확인하지 않은 것

- Qt 쪽 크기, 시작 시간, 메모리, 백신 반응은 모두 Qt 6 배포물의 일반적인 크기에 근거한 추정이며 이 저장소에서 측정하지 않았다.
- `jlink` 축소 효과(런타임이 절반 정도)도 추정이다.
- `engine`의 정규식을 `QRegularExpression`(PCRE2)으로 옮길 때의 의미 차이는 확인하지 않았다.
- Qt Widgets의 한글 IME 품질은 일반적인 경험에 근거한 가정이다.
- 라이선스 내용(§6, §6.1)은 요약이며 법률 검토를 거치지 않았다.
- 이 문서는 Windows 전용(README의 범위)을 전제로 한다.

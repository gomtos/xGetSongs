# 포장과 단일 exe (5단계) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 앱을 **exe 한 파일**(`dist\xGetSongs.exe`)로 건넬 수 있게 한다. 그 파일을 복사해 간 PC에는 Java도 Node도 Rust도 필요 없고(yt-dlp, ffmpeg, Node.js/Deno는 앱이 원래 요구하는 대로), Microsoft Edge WebView2가 없으면 앱이 말없이 사라지는 대신 안내한다.

**Architecture:** Tauri 셸의 exe 뒤에 사이드카 이미지(Kotlin 서버의 jar들과 Java 런타임, 493개 파일)를 **우리 형식으로 압축해 덧붙이고** 맨 끝에 어디서부터가 그것인지 적은 꼬리표(footer)를 둔다. Windows는 구역 뒤에 덧붙은 데이터를 무시하므로 그 파일은 그대로 셸로 실행된다. 셸은 시작하자마자 자기 exe의 꼬리표를 읽고, 있으면 `%LOCALAPPDATA%\xGetSongs\runtime\<해시>`에 한 번 풀어 그 사본에서 사이드카를 시작한다(다음 실행부터는 풀어 둔 폴더를 쓴다). 이 로직은 Tauri 없이 시험할 수 있게 `sidecar-host`에 둔다(`payload`, `SidecarConfig::resolve`, `webview2`, 묶는 도구 `xgs-pack`). 깨끗한 PC에서 `java.exe`가 도는 데 필요한 Visual C++ 런타임 DLL은 이미지에 되살려 넣는다(런타임이 `*.dll.ortbak`으로 이름을 바꿔 놓아 아무도 읽지 않던 것).

**Tech Stack:** Rust 2021(`sidecar-host`)/2024(셸), Tauri 2.12, `flate2` 1.1.10(deflate), `sha2` 0.10.9(키), `winreg` 0.56.0(WebView2 감지), Gradle 9.5.1(이미지), PowerShell 7 + UI Automation(점검). **새 의존성은 `sidecar-host`의 Rust 크레이트 셋**이다(npm, Kotlin 의존성은 없다): 셋 다 셸의 의존성 그래프에 이미 있는 것이라 `cargo --offline`으로 풀리고, 셸의 `Cargo.lock`에는 `xgs-sidecar`의 의존성 줄만 더해진다(새 패키지는 없다). 그래도 새 직접 의존성이므로 Task 1 전에 사용자의 허락을 받는다.

**Spec:** [docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md](../specs/2026-10-10-tauri-kotlin-sidecar-design.md) §5의 5단계("포장과 단일 exe: 번들 JRE + 셸. 이전 설계(§6.4)의 SFX 판정 방식 재사용", 완료 증거: 깨끗한 PC 흉내 검증), §6의 위험 둘("단일 exe: Tauri의 `resources`는 exe에 내장되지 않는다…", "WebView2 런타임: Windows 11에는 있지만 Windows 10은 따로 설치해야 한다"). 이전 설계(보류된 문서, git `e504f99`)의 §6.3(깨끗한 PC 흉내: 앱 데이터 폴더를 빈 임시 폴더로, PATH에서 Node·Deno·yt-dlp 제거)과 §6.4(두 번째 실행의 창이 5초 이하이면 SFX, 아니면 다른 후보)를 이어받는다.

## 이 계획이 내린 결정

설계 문서가 5단계에 맡긴 것과 범위의 경계다. 사용자가 계획서를 검토할 때 바꿀 수 있다.

| 결정 | 내용 | 근거 |
|---|---|---|
| 단일 exe의 방식 | **셸 exe 뒤에 이미지를 덧붙이는 자체 오버레이**(7-Zip SFX도 warp-packer도 아니다). 첫 실행에만 풀고 다음부터는 풀어 둔 폴더를 쓴다 | 설계 §6.4의 판정(두 번째 실행의 창이 5초 이하)을 같은 방식으로 쟀다. 오버레이: 창은 시작 후 약 50ms에 뜨고(풀기 전에), 사이드카 준비까지 **첫 실행 2.9초(3회 중앙값, 푸는 데 1.5초), 두 번째부터 1.1초**. 7-Zip으로 같은 이미지(그때의 159MB)를 푸는 데만 3.0~3.1초(zip 131MB, 7z LZMA2 122MB)라서 SFX는 **매 실행** 그만큼을 더 쓰고 148MB를 풀었다 지우기를 되풀이한다(SFX는 만들어 재 보지 않았고 이 풀기 시간으로 추정했다). warp-packer는 외부 도구를 받아야 해서 시험하지 않았다. 외부 도구와 설정 스크립트가 없고 코드가 `sidecar-host` 한 모듈이라 시험이 된다 |
| 푸는 곳과 이름 | `%LOCALAPPDATA%\xGetSongs\runtime\<해시 16자리>`(`XGS_SIDECAR_APPDATA`가 있으면 그 아래 `runtime`). 해시는 덧붙인 데이터 전체의 SHA-256 앞 8바이트. 완료 표식 `.xgs-complete`가 있는 폴더만 쓴다. **`<해시>.partial-<pid>`에 풀고 끝나면 이름을 바꾼다**. 다른 빌드의 폴더와 풀다 끊긴 `.partial-*`는 시작할 때 지운다 | 로밍 프로필(`%APPDATA%`)에 148MB를 두지 않는다. 풀다가 앱이 꺼져도(점검 시나리오 3) 반쯤 푼 폴더를 쓰는 일이 없다. exe를 새 빌드로 바꾸면 옛 148MB가 쌓이지 않는다(시나리오 4) |
| 풀 수 없을 때 | 오류다. 다른 곳을 뒤지지 않는다. 화면은 `서버를 시작하지 못했습니다: xGetSongs의 실행 파일에 든 런타임을 풀지 못했습니다: …`를 받고 창은 열려 있다(시나리오 7) | 런타임이 든 exe가 그것을 내놓지 못하는데 exe 옆의 `sidecar` 폴더로 조용히 넘어가면 원인이 숨는다 |
| 사이드카를 찾는 순서 | ① `XGS_SIDECAR_LIB`(개발) ② `XGS_SIDECAR_DIR` ③ **exe가 지닌 런타임** ④ exe 옆의 `sidecar` 폴더. 빈 환경 변수는 없는 것으로 친다 | 앞의 둘은 지금처럼 이긴다(점검 스크립트와 개발이 그대로 돈다). 빈 변수는 PowerShell의 `SetEnvironmentVariable($null)`이 만들어 낸 실제 사고의 교훈이다(점검이 "점검 모드"로 오인돼 안내 상자가 떴다) |
| 로그 위치 | 묶은 exe는 로그를 앱 데이터의 `logs`에 쓴다(`--log-dir`을 주지 않으면 사이드카가 정하는 곳). 개발·`sidecar` 폴더 방식은 지금처럼 exe 옆 `log` | 어디에 둘지 모르는 exe 옆(다운로드 폴더, 바탕화면)에 `log` 폴더를 만들지 않는다(시나리오 1이 확인). 설계 §6의 "로그 위치" 위험 |
| 사이드카 위치 해석을 사이드카 스레드로 | 지금은 `build()` 뒤에서 하던 `SidecarConfig::from_env()`를 `Lifecycle`의 스레드(클로저 안)로 옮긴다 | 풀기가 창 스레드를 막지 않는다. 창은 뜨고 화면은 "시작하는 중"을 보인다 |
| 단일 인스턴스와 풀기 | 바꾸지 않는다. 두 번째 실행은 `build()` 안에서 끝나므로(4단계) 풀기와 겹치지 않는다 | 푸는 중에 한 번 더 눌러도 두 번째가 31ms에 끝나고 폴더 하나·사이드카 하나다(시나리오 5) |
| WebView2 | 시작하자마자 레지스트리(`EdgeUpdate\Clients\{F3017226-…}`의 `pv`, 세 곳, `0.0.0.0`은 없는 것)로 확인하고, 없으면 안내 상자(`확인` = 설치 프로그램을 받는 페이지를 연다, `취소` = 닫기)를 띄우고 끝낸다. 설치 파일을 exe에 넣지 않는다 | 없으면 창이 안 열리고 프로세스가 말없이 끝난다. Evergreen 설치 파일을 넣으면 크기가 늘고 관리자 권한이 필요하다. 점검에서는 `XGS_BRIDGE_CHECK_FILE`이 있을 때만 `XGS_FAKE_NO_WEBVIEW2`로 없는 척한다(이 PC에서 런타임을 지울 수 없다). **`확인`을 눌러 브라우저가 열리는 경로는 실행하지 않는다**(`explorer.exe <주소>` 한 줄이고 4단계의 폴더 열기와 같은 방식) |
| 이미지 다듬기 | 이미지에서 `lib/ct.sym`(10.7MB), `lib/jvm.lib`(1MB)를 뺀다(11.7MB 감소). `*.dll.ortbak` 셋(VC++ 런타임)은 **`.dll`로 이름을 되돌려** 이미지에 넣는다 | 둘 다 실행에 쓰이지 않는다. 시나리오 1이 사이드카의 `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`이 **풀어 둔 사본의 `bin`에서** 읽힌 것을 확인한다(이 PC의 System32에 있는 것을 쓰지 않았다는 증거). UCRT는 Windows에 들어 있다 |
| 크기와 JavaFX | exe는 약 129MB(이미지 148MB를 124MB로 압축). **`lib/javafx-*.jar` 45MB를 그대로 둔다** | 그것은 구글 가사 검색(`FxWebViewBrowser`)이 쓴다. 빼면 그 기능이 조용히 꺼진다(약 85MB가 되지만 동작이 바뀐다). 설계 §5의 7단계(WebView2로 교체)에서 다룬다 |
| `jlink`는 이 계획에 없다 | 설계 §5 1단계 행이 5단계에 맡긴 것이지만 하지 않는다 | `jlink`는 `jmods`가 있는 JDK가 필요한데 지금 런타임(JetBrains Runtime 21)에는 없다. Temurin 21 JDK(약 200MB)를 받아야 하고, 줄어드는 것은 `lib/modules`(51MB) 중 일부다. **원하면 따로 한다**(보고서의 "남은 일") |
| 서명, 아이콘, SmartScreen | 하지 않는다. README와 보고서에 한계로 적는다 | 이전 설계 §6.6과 같다. 아이콘은 Tauri 기본 아이콘이고 사용자가 PNG를 주면 `tauri icon`으로 바꿀 수 있다 |
| exe의 이름 | 파일 이름은 `xGetSongs.exe`(묶는 도구가 정한다). exe의 메타데이터(작업 관리자에 보이는 이름)는 `tauri.conf.json`의 `productName`을 `xGetSongs`로 | 지금은 `shell`이다. `Cargo.toml`의 패키지 이름은 그대로라 `shell.exe`가 나오고 `identifier`(단일 인스턴스·WebView2 프로필)도 그대로다 |
| 이 단계가 미루는 것 | `app/` 삭제와 README 전체 갱신(6단계), 구글 가사의 WebView2 교체(7단계), 서명·아이콘, `jlink`, 자동 갱신 | 범위 밖 |

## 실행 전에 사용자에게 받을 것

1. **Rust 크레이트 셋(`flate2` 1.1.10, `sha2` 0.10.9, `winreg` 0.56.0)을 `sidecar-host`의 직접 의존성으로 더해도 되는지.** 셋 다 셸이 이미 쓰는 그래프 안에 있고 오프라인 캐시에서 풀린다(새로 내려받지 않는다).
2. 위 결정 표에서 바꾸고 싶은 것. 특히 `jlink`(JDK를 받아야 한다)와 JavaFX(45MB, 구글 가사)를 이 단계에서 어떻게 할지.

## Global Constraints

- 서버(`server/`)의 Kotlin 코드, `engine/`, `shared/`, `app/`, 셸의 TypeScript(`shell/src`)는 바꾸지 않는다. 이 단계의 변경은 `sidecar-host/`, 셸의 Rust·설정 두 곳, `server/build.gradle.kts`와 그 이미지 시험, 루트의 `package-exe.*`, 문서뿐이다.
- 이 마이그레이션은 지금 동작을 그대로 옮긴다(설계 §3). 사용자에게 보이는 새 문구는 WebView2 안내 상자와 풀기 실패 메시지 둘뿐이다.
- 코드와 코드 주석은 영어, 사용자에게 보이는 문구와 커밋 메시지는 한국어. `main`에 직접 커밋한다. 커밋 메시지 끝에는 이 세션이 정한 `Co-Authored-By` 줄을 붙인다. 푸시는 사용자에게 묻고 한다.
- Bash 도구는 명령 텍스트의 백슬래시를 한 번 더 해석하고 이 PC의 도구 셸에는 `cargo`가 PATH에 없다. 코드는 Write·Edit 도구로 쓰고 `cargo`, `npm`, `node`, Gradle은 PowerShell 도구로 부른다(PowerShell 호출마다 `$env:Path += ";$env:USERPROFILE\.cargo\bin"`). PowerShell 도구는 `Remove-Item`을 막는 일이 있으므로 도구 명령에서는 `[IO.File]::Delete`, `[IO.Directory]::Delete($path, $true)`를 쓴다(점검 **스크립트 파일** 안에서는 `Remove-Item`이 된다).
- Gradle은 `--no-daemon`으로만 부른다(공유 데몬을 끝냈다가 사용자의 앱이 죽은 적이 있다).
- **크레이트는 오프라인 캐시에서만 받는다**(`cargo ... --offline`). 캐시에 없어 네트워크가 필요하다는 오류가 나면 멈추고 사용자에게 묻는다.
- **환경 변수는 `Remove-Item "Env:$name"`으로 지운다.** PowerShell 7의 `[Environment]::SetEnvironmentVariable($name, $null)`은 지우지 않고 빈 문자열로 만든다.
- **창을 띄우는 점검(Task 6의 회귀, Task 8)을 시작하기 전에 사용자에게 알린다**("점검으로 창과 안내 상자가 잠깐 뜰 수 있습니다"). 설명 없이 뜬 상자 때문에 사용자가 앱의 문제로 오해한 적이 있다.
- 확인은 사용자의 `%APPDATA%\xGetSongs`, `%LOCALAPPDATA%\xGetSongs`, `~\Music\xGetSongs`를 건드리지 않는다. 깨끗한 PC 점검은 `%TEMP%\xgs-bridge-check-<id>` 아래에 가짜 `APPDATA`·`LOCALAPPDATA`·`TEMP`를 만들고 **끝나면 지운다**(풀어 둔 런타임이 148MB씩이다). 점검 스크립트는 사용자의 폴더를 읽지도 않는다(이 단계는 실제 다운로드를 하지 않는다).
- 점검은 창을 `Process.MainWindowHandle`이 아니라 클래스 `Tauri Window`로 찾고(4단계), 프로세스를 이름이 아니라 **경로**(`%TEMP%\xgs-bridge-check-*` 아래)로 끝낸다(사용자의 앱이 같은 이름일 수 있다).
- 이 계획의 diff는 LF 줄 끝 기준이다. 작업 폴더의 파일이 CRLF이면(`config.rs`, 셸의 `lib.rs`와 `Cargo.toml` 등) `git apply --ignore-whitespace`를 쓰거나 Edit 도구로 같은 내용을 넣는다. 새 파일은 LF로 쓴다.
- 점검 스크립트는 `%TEMP%\xgs-bridge-check\`에 둔다(저장소 밖, 4단계와 같다). 4단계의 `helpers.ps1`, `desktop-helpers.ps1`, `single-instance.ps1`이 거기 있어야 한다. 없으면 [4단계 계획서](2026-10-11-tauri-desktop-features.md)의 같은 이름의 블록으로 만든다.

## Review Focus

계획의 시험이 직접 다루지 않아 사용자에게 먼저 문제가 될 것, 가능성이 높은 순서:

1. **첫 실행이 풀다가 끊기면 다음 실행이 영영 안 된다.** 작업 관리자로 끝내거나 전원이 꺼지거나 백신이 파일을 잡고 있을 때, 반쯤 푼 폴더를 완성본으로 착각하면 안 되고(표식과 `.partial-<pid>`), 다음 실행이 스스로 복구해야 한다. → Task 1의 시험(끊긴 아카이브, 표식 없는 폴더), Task 8의 시나리오 3(풀다 죽임)과 7(풀 수 없음)
2. **exe를 새 빌드로 바꿨는데 옛 런타임이 쌓이거나 옛것을 쓴다.** 해시가 다르면 다른 폴더여야 하고 옛 폴더는 지워져야 한다. 단, 캐시 폴더 안의 **우리가 만든 이름이 아닌** 것은 지우지 않는다. → Task 1(`the_runtimes_of_other_builds…`), 시나리오 4
3. **깨끗한 PC에서 `java.exe`가 안 뜬다.** Visual C++ 런타임이 없는 PC. 이 PC에는 System32에 있어서 흉내만 낼 수 있으므로 사이드카가 **어디서 DLL을 읽었는지**를 본다. → Task 5의 이미지 시험, 시나리오 1의 모듈 경로
4. **exe의 위치와 환경.** 이름에 한글과 공백이 든 폴더, exe 옆에 무엇이든 만드는 것(다운로드 폴더에 `log`가 생기면 안 된다), 읽기 전용 위치, 이미 떠 있는 두 번째 실행. → 시나리오 1(한글·공백 폴더, 옆에 아무것도 안 생김), 5(두 번째 실행). 풀기는 exe 옆이 아니라 `LOCALAPPDATA`에 쓰므로 읽기 전용 위치의 exe는 읽기만 하는데, 그것은 시험하지 못했다
5. **WebView2가 없거나 환경 변수가 이상하다.** 없으면 안내하고 사이드카를 풀지도 시작하지도 않는다. 빈 환경 변수가 점검 모드나 "없는 척"을 켜면 안 된다. → Task 2의 빈 변수 시험, 시나리오 6

## 이 단계의 관문

| 번호 | 조건 | 확인 |
|---|---|---|
| P1 | `sidecar-host` 시험이 모두 통과한다: 지금의 119개 + `payload` 17개 + `config` 9개 + `webview2` 7개 = **152개**, 통합 시험 `pack_cli` 5개, 기존 `#[ignore]` 2개는 그대로. `cargo clippy --all-targets`는 새 경고가 없다(원래 있던 `job.rs:11` 하나만) | Task 1~4 |
| P2 | `:server:sidecarImageTest`가 4개(새 2개 포함) 통과한다. 이미지에 `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`이 있고 `.ortbak`이 없으며 `ct.sym`, `jvm.lib`가 없다. 이미지 크기 약 148MB(전에는 159MB) | Task 5 |
| P3 | 셸이 일반 빌드와 점검용 빌드로 빌드된다. 일반 빌드의 exe 메타데이터가 `ProductName=xGetSongs`. 4단계의 `single-instance.ps1`이 점검용 빌드로 그대로 통과한다(두 번째 실행이 끝나고 표식과 사이드카 하나가 남고 첫 창이 앞에 온다) | Task 6 |
| P4 | `package-exe.bat`가 `dist\xGetSongs.exe`(약 129MB)를 만든다. `dist\`는 git이 무시한다 | Task 7 |
| P5 | 깨끗한 PC 점검(점검용 빌드를 묶은 exe)이 시나리오 7개 모두 `ALL OK`다: 첫 실행에 풀고 사본의 java로 사이드카가 돌고 VC++ DLL이 사본에서 읽히고 exe 옆에 아무것도 안 생김 / 두 번째 실행은 다시 풀지 않음 / 풀다 죽여도 복구 / 다른 빌드의 폴더를 지움 / 풀 때 한 번 더 실행해도 하나뿐 / WebView2 없음 안내 / 풀 수 없을 때 메시지 | Task 8 |
| P6 | 실제 exe(`dist\xGetSongs.exe`, 점검 페이지 없음)가 깨끗한 PC에서 창을 3초 안에 열고 화면에 도구 카드(`yt-dlp ✗`, `ffmpeg ✗`, `JS 런타임 ✗`)와 입력란이 UI Automation으로 읽힌다 | Task 8 |
| P7 | 시간(설계 §5.1과 이전 설계 §6.4의 한도): 두 번째 실행에서 사이드카를 기다린 시간(`waitedMs`) 5초 이하, 첫 실행 10초 이하. 계획을 쓸 때 측정: 첫 실행 2.9초(푸는 데 1.5초), 두 번째 1.1초 | Task 8 |
| P8 | 시험이 결함을 잡는다: 변이 12개가 모두 잡힌다. 앞 단계의 것이 유지된다: TS 242개와 `tsc`, `sidecar-host`의 `real_sidecar`(`#[ignore]`) 제외 전체 | Task 8 |
| P9 | 전체 브랜치 리뷰(가장 유능한 모델)에서 Critical·Important가 없거나 고쳐진다 | 실행 마지막 |

## File Structure

| 파일 | 책임 |
|---|---|
| `sidecar-host/src/payload.rs` (새) | 덧붙인 런타임의 형식과 읽기·쓰기: `Footer`, `read_footer`, `pack`, `extract`, `ensure_extracted` |
| `sidecar-host/src/bin/xgs-pack.rs` (새) | 셸 exe 뒤에 이미지를 붙이는 명령줄 도구 |
| `sidecar-host/src/config.rs` (수정) | `runtime_cache_dir`, `SidecarConfig::resolve`(exe가 지닌 런타임을 풀어 쓴다), `from_env`가 그것을 쓴다 |
| `sidecar-host/src/webview2.rs` (새) | WebView2 런타임 감지, 없을 때의 안내 상자 |
| `sidecar-host/src/lib.rs`, `sidecar-host/Cargo.toml`, `sidecar-host/Cargo.lock` (수정) | 모듈 둘, 의존성 셋 |
| `sidecar-host/tests/pack_cli.rs` (새) | `xgs-pack`을 사람이 쓰듯 돌리는 통합 시험 |
| `server/build.gradle.kts`, `server/src/test/.../SidecarImageTest.kt` (수정) | 이미지에서 불필요한 둘을 빼고 VC++ 런타임 DLL을 되살린다, 그 시험 |
| `shell/src-tauri/src/lib.rs` (수정) | WebView2 사전 점검, 사이드카 위치 해석을 사이드카 스레드로, 빈 점검 변수 무시 |
| `shell/src-tauri/Cargo.toml`, `shell/src-tauri/tauri.conf.json`, `shell/src-tauri/Cargo.lock` (수정) | 설명, `productName`, 잠금 |
| `package-exe.ps1`, `package-exe.bat`, `.gitignore` (새·수정) | `dist\xGetSongs.exe`를 만드는 한 줄짜리 진입점 |
| `README.md` (수정) | "하나의 exe로 묶기" 절 |
| `%TEMP%\xgs-bridge-check\*.ps1`, `mutate.mjs` (저장소 밖) | `clean-pc-helpers.ps1`, `clean-pc-check.ps1`, `packed-screen-check.ps1`, `measure-start.ps1`, 변이 시험 도구 |
| `docs/superpowers/specs/2026-10-15-tauri-packaging-report.md` (새), 설계 문서 (수정) | 종단 확인 결과, 설계의 5단계 행과 위험 둘 |

---

### Task 1: exe에 붙인 런타임을 읽고 푸는 모듈 (`payload`)

**Files:**
- Modify: `sidecar-host/Cargo.toml`, `sidecar-host/Cargo.lock`, `sidecar-host/src/lib.rs`
- Create: `sidecar-host/src/payload.rs`

**Interfaces:**
- Consumes: 없다.
- Produces (Task 2, 3이 쓴다): `payload::Footer { offset: u64, length: u64, key: [u8; 32] }`와 `Footer::key_name() -> String`(해시 앞 8바이트의 소문자 16진수 16자리), `read_footer<R: Read + Seek>(&mut R) -> io::Result<Option<Footer>>`(꼬리표가 없으면 `None`, 있는데 파일과 안 맞으면 오류), `pack(exe: &Path, image: &Path, output: &Path) -> io::Result<Packed>`와 `Packed { footer, files: usize, original_bytes: u64, stored_bytes: u64 }`, `extract<R: Read + Seek>(&mut R, &Footer, target: &Path) -> io::Result<usize>`, `ensure_extracted(exe: &Path, cache: &Path) -> Result<Option<PathBuf>, String>`(런타임이 없는 exe는 `Ok(None)`이고 아무것도 만들지 않는다; 오류 문구는 한국어).
- 형식: 항목 `[방법 u8][경로 길이 u32][경로][원래 크기 u64][저장 크기 u64][데이터]`(리틀 엔디언, 방법 0 = 그대로, 1 = deflate, 경로는 `/`)가 이어지고 꼬리표 `XGSPAYLD` + 버전 u32 + 시작 u64 + 길이 u64 + SHA-256(32바이트).

- [ ] **Step 1: 의존성을 더한다**

사용자의 허락(위 "Tech Stack")을 받은 뒤 적용한다.

```diff
diff --git a/sidecar-host/Cargo.toml b/sidecar-host/Cargo.toml
index 182db62..54f649d 100644
--- a/sidecar-host/Cargo.toml
+++ b/sidecar-host/Cargo.toml
@@ -9,6 +9,9 @@ description = "Starts, talks to and stops the Kotlin sidecar of xGetSongs, and t
 ureq = { version = "2", default-features = false }
 serde = { version = "1", features = ["derive"] }
 serde_json = "1"
+flate2 = "1.1.10"
+sha2 = "0.10.9"
 
 [target.'cfg(windows)'.dependencies]
 win32job = "2"
+winreg = "0.56.0"
```

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo build --offline 2>&1 | Select-Object -Last 3
```

Expected: `Finished`. `sidecar-host/Cargo.lock`에 패키지 19개가 더해진다(`adler2`, `block-buffer`, `cfg-if`, `cpufeatures`, `crc32fast`, `crypto-common`, `digest`, `flate2`, `generic-array`, `libc`, `miniz_oxide`, `sha2`, `simd-adler32`, `typenum`, `version_check`, `windows-link`, `windows-sys`, `winreg`, `zlib-rs`). 네트워크가 필요하다는 오류가 나면 멈추고 사용자에게 묻는다.

- [ ] **Step 2: 실패하는 시험을 쓴다**

`sidecar-host/src/payload.rs`를 **시험 모듈만으로** 만든다(구현은 다음 단계):

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    /// A fresh folder in the temp folder that is deleted again when the test is over.
    struct Scratch(PathBuf);

    impl Scratch {
        fn new(name: &str) -> Scratch {
            let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
            let dir = std::env::temp_dir().join(format!("xgs-payload-test-{}-{name}-{nanos}", std::process::id()));
            fs::create_dir_all(&dir).unwrap();
            Scratch(dir)
        }

        fn path(&self, relative: &str) -> PathBuf {
            self.0.join(relative)
        }
    }

    impl Drop for Scratch {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    /// A sidecar image in small: nested folders, an empty file, text that compresses, bytes that do not, and a Korean name.
    fn image(scratch: &Scratch) -> PathBuf {
        let image = scratch.path("image");
        fs::create_dir_all(image.join("runtime").join("bin")).unwrap();
        fs::create_dir_all(image.join("lib")).unwrap();
        fs::write(image.join("runtime").join("bin").join("java.exe"), b"MZ fake java").unwrap();
        fs::write(image.join("lib").join("empty.txt"), b"").unwrap();
        fs::write(image.join("lib").join("text.txt"), "abc ".repeat(10_000)).unwrap();
        let noise: Vec<u8> = (0..20_000u32).map(|n| (n.wrapping_mul(2_654_435_761) >> 13) as u8).collect();
        fs::write(image.join("lib").join("noise.bin"), noise).unwrap();
        fs::write(image.join("lib").join("가사.txt"), "한글 이름의 파일").unwrap();
        image
    }

    fn fake_exe(scratch: &Scratch) -> PathBuf {
        let exe = scratch.path("shell.exe");
        fs::write(&exe, b"MZ the shell, as far as a test cares".repeat(100)).unwrap();
        exe
    }

    fn same_files(left: &Path, right: &Path) {
        let (a, b) = (list_files(left).unwrap(), list_files(right).unwrap());
        let b: Vec<String> = b.into_iter().filter(|name| name != COMPLETE_MARKER).collect(); // the marker is the cache's, not the image's
        assert_eq!(a, b, "the same files");
        for name in a {
            assert_eq!(fs::read(left.join(&name)).unwrap(), fs::read(right.join(&name)).unwrap(), "{name}");
        }
    }

    /// An archive of the given entries, written by hand, for the cases `pack` never makes.
    fn archive_of(entries: &[(&str, &[u8])]) -> (Vec<u8>, Footer) {
        let mut bytes = Vec::new();
        for (name, data) in entries {
            bytes.push(STORED);
            bytes.extend_from_slice(&(name.len() as u32).to_le_bytes());
            bytes.extend_from_slice(name.as_bytes());
            bytes.extend_from_slice(&(data.len() as u64).to_le_bytes());
            bytes.extend_from_slice(&(data.len() as u64).to_le_bytes());
            bytes.extend_from_slice(data);
        }
        let footer = Footer { offset: 0, length: bytes.len() as u64, key: Sha256::digest(&bytes).into() };
        bytes.extend_from_slice(&footer.to_bytes());
        (bytes, footer)
    }

    #[test]
    fn a_file_that_does_not_end_with_a_footer_has_none() {
        assert_eq!(read_footer(&mut Cursor::new(Vec::new())).unwrap(), None);
        assert_eq!(read_footer(&mut Cursor::new(b"short".to_vec())).unwrap(), None);
        assert_eq!(read_footer(&mut Cursor::new(vec![7u8; 500])).unwrap(), None);
    }

    #[test]
    fn a_footer_that_does_not_fit_the_file_is_an_error() {
        let (mut bytes, _) = archive_of(&[("a.txt", b"data")]);
        bytes.insert(0, 0); // everything moves by one: the footer no longer points at the archive's end

        let error = read_footer(&mut Cursor::new(bytes)).unwrap_err();

        assert!(error.to_string().contains("does not fit"), "{error}");
    }

    #[test]
    fn a_footer_of_another_version_is_an_error() {
        let (mut bytes, _) = archive_of(&[("a.txt", b"data")]);
        let at = bytes.len() - FOOTER_LEN as usize + 8;
        bytes[at] = 9;

        let error = read_footer(&mut Cursor::new(bytes)).unwrap_err();

        assert!(error.to_string().contains("version 9"), "{error}");
    }

    #[test]
    fn what_is_packed_comes_out_the_same_and_the_exe_is_still_in_front() {
        let scratch = Scratch::new("roundtrip");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));
        let output = scratch.path("packed.exe");

        let packed = pack(&exe, &image, &output).unwrap();

        assert_eq!(packed.files, 5);
        let bytes = fs::read(&output).unwrap();
        let original = fs::read(&exe).unwrap();
        assert_eq!(&bytes[..original.len()], &original[..], "the exe is unchanged in front");
        let mut file = File::open(&output).unwrap();
        let footer = read_footer(&mut file).unwrap().expect("a footer");
        assert_eq!(footer, packed.footer);
        assert_eq!(footer.offset, original.len() as u64);
        let target = scratch.path("out");
        fs::create_dir_all(&target).unwrap();
        assert_eq!(extract(&mut file, &footer, &target).unwrap(), 5);
        same_files(&image, &target);
    }

    #[test]
    fn text_is_stored_smaller_and_noise_is_not_made_bigger() {
        let scratch = Scratch::new("sizes");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));

        let packed = pack(&exe, &image, &scratch.path("packed.exe")).unwrap();

        assert!(packed.stored_bytes < packed.original_bytes, "{packed:?}");
        let noise = 20_000u64;
        assert!(packed.stored_bytes < packed.original_bytes - 30_000 + noise + 5 * 64, "the 40,000 bytes of text shrank: {packed:?}");
    }

    #[test]
    fn the_same_folder_packs_to_the_same_key_and_another_content_to_another() {
        let scratch = Scratch::new("key");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));

        let first = pack(&exe, &image, &scratch.path("a.exe")).unwrap();
        let second = pack(&exe, &image, &scratch.path("b.exe")).unwrap();
        fs::write(image.join("lib").join("text.txt"), "changed").unwrap();
        let third = pack(&exe, &image, &scratch.path("c.exe")).unwrap();

        assert_eq!(first.footer.key, second.footer.key);
        assert_ne!(first.footer.key, third.footer.key);
        assert_eq!(first.footer.key_name().len(), 16);
    }

    #[test]
    fn an_exe_that_carries_a_runtime_is_not_packed_again() {
        let scratch = Scratch::new("twice");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));
        pack(&exe, &image, &scratch.path("once.exe")).unwrap();

        let error = pack(&scratch.path("once.exe"), &image, &scratch.path("twice.exe")).unwrap_err();

        assert!(error.to_string().contains("already carries"), "{error}");
    }

    #[test]
    fn paths_that_could_leave_the_folder_are_refused() {
        for name in ["../escape.txt", "a/../../escape.txt", "/absolute.txt", "C:/drive.txt", "a\\b.txt", "a//b.txt", "./a.txt", "a/./b.txt"] {
            let scratch = Scratch::new("slip");
            let target = scratch.path("out");
            fs::create_dir_all(&target).unwrap();
            let (bytes, footer) = archive_of(&[(name, b"data")]);

            let error = extract(&mut Cursor::new(bytes), &footer, &target).unwrap_err();

            assert!(error.to_string().contains("not allowed"), "{name:?}: {error}");
            assert!(!scratch.path("escape.txt").exists(), "{name:?} wrote outside");
            assert_eq!(fs::read_dir(&target).unwrap().count(), 0, "{name:?} left something behind");
        }
    }

    #[test]
    fn an_entry_without_a_path_is_an_error() {
        let scratch = Scratch::new("nameless");
        let mut bytes = vec![STORED];
        bytes.extend_from_slice(&0u32.to_le_bytes());
        let footer = Footer { offset: 0, length: bytes.len() as u64, key: [0; 32] };

        let error = extract(&mut Cursor::new(bytes), &footer, &scratch.0).unwrap_err();

        assert!(error.to_string().contains("impossible length"), "{error}");
    }

    /// What is in [cache], by name (nothing when it does not exist).
    fn names_in(cache: &Path) -> Vec<String> {
        fs::read_dir(cache).map(|entries| entries.flatten().map(|entry| entry.file_name().to_string_lossy().into_owned()).collect()).unwrap_or_default()
    }

    #[test]
    fn an_exe_that_lost_bytes_in_the_middle_is_an_error_and_leaves_nothing_in_the_cache() {
        let scratch = Scratch::new("lost");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));
        let output = scratch.path("packed.exe");
        pack(&exe, &image, &output).unwrap();
        // the footer still sits at the end, but its offsets no longer fit the file
        let mut bytes = fs::read(&output).unwrap();
        let at = fs::metadata(&exe).unwrap().len() as usize + 100;
        bytes.drain(at..at + 200);
        fs::write(&output, bytes).unwrap();
        let cache = scratch.path("cache");

        let error = ensure_extracted(&output, &cache).unwrap_err();

        assert!(error.contains("읽지 못했습니다") && error.contains("does not fit"), "{error}");
        assert_eq!(names_in(&cache), Vec::<String>::new());
    }

    #[test]
    fn an_archive_that_ends_inside_a_file_is_an_error_and_leaves_nothing_in_the_cache() {
        let scratch = Scratch::new("cut");
        let mut bytes = vec![STORED];
        bytes.extend_from_slice(&5u32.to_le_bytes());
        bytes.extend_from_slice(b"a.txt");
        bytes.extend_from_slice(&100u64.to_le_bytes());
        bytes.extend_from_slice(&100u64.to_le_bytes()); // says 100 bytes
        bytes.extend_from_slice(b"only ten b"); // holds 10
        let footer = Footer { offset: 0, length: bytes.len() as u64, key: [1; 32] };
        bytes.extend_from_slice(&footer.to_bytes());
        let exe = scratch.path("cut.exe");
        fs::write(&exe, bytes).unwrap();
        let cache = scratch.path("cache");

        let error = ensure_extracted(&exe, &cache).unwrap_err();

        assert!(error.contains("풀지 못했습니다") && error.contains("cut short at a.txt"), "{error}");
        assert_eq!(names_in(&cache), Vec::<String>::new());
    }

    #[test]
    fn a_file_whose_size_is_not_what_the_entry_says_is_an_error() {
        let scratch = Scratch::new("size");
        let target = scratch.path("out");
        fs::create_dir_all(&target).unwrap();
        let mut bytes = Vec::new();
        bytes.push(STORED);
        bytes.extend_from_slice(&5u32.to_le_bytes());
        bytes.extend_from_slice(b"a.txt");
        bytes.extend_from_slice(&9u64.to_le_bytes()); // says 9 bytes
        bytes.extend_from_slice(&4u64.to_le_bytes()); // holds 4
        bytes.extend_from_slice(b"data");
        let footer = Footer { offset: 0, length: bytes.len() as u64, key: [0; 32] };
        bytes.extend_from_slice(&footer.to_bytes());

        let error = extract(&mut Cursor::new(bytes), &footer, &target).unwrap_err();

        assert!(error.to_string().contains("damaged at a.txt"), "{error}");
    }

    #[test]
    fn ensure_extracted_unpacks_once_and_uses_the_folder_the_next_time() {
        let scratch = Scratch::new("once");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));
        let output = scratch.path("packed.exe");
        pack(&exe, &image, &output).unwrap();
        let cache = scratch.path("cache");

        let first = ensure_extracted(&output, &cache).unwrap().expect("a runtime");

        same_files(&image, &first.join("."));
        assert!(first.join(COMPLETE_MARKER).is_file());
        // a mark of ours in the folder survives the next start: it is used, not unpacked again
        fs::write(first.join("lib").join("text.txt"), "marked").unwrap();
        let second = ensure_extracted(&output, &cache).unwrap().expect("a runtime");
        assert_eq!(second, first);
        assert_eq!(fs::read_to_string(second.join("lib").join("text.txt")).unwrap(), "marked");
    }

    #[test]
    fn a_folder_without_the_complete_marker_is_unpacked_again() {
        let scratch = Scratch::new("incomplete");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));
        let output = scratch.path("packed.exe");
        let packed = pack(&exe, &image, &output).unwrap();
        let cache = scratch.path("cache");
        let half = cache.join(packed.footer.key_name());
        fs::create_dir_all(half.join("lib")).unwrap();
        fs::write(half.join("lib").join("text.txt"), "half").unwrap();

        let ready = ensure_extracted(&output, &cache).unwrap().expect("a runtime");

        assert_eq!(ready, half);
        assert_eq!(fs::read_to_string(ready.join("lib").join("text.txt")).unwrap(), "abc ".repeat(10_000));
        assert!(ready.join(COMPLETE_MARKER).is_file());
    }

    #[test]
    fn the_runtimes_of_other_builds_are_removed_and_other_folders_are_left_alone() {
        let scratch = Scratch::new("old");
        let (exe, image) = (fake_exe(&scratch), image(&scratch));
        let output = scratch.path("packed.exe");
        pack(&exe, &image, &output).unwrap();
        let cache = scratch.path("cache");
        for name in ["0123456789abcdef", "fedcba9876543210.partial-4242", "notes", "FEDCBA0987654321", "short"] {
            fs::create_dir_all(cache.join(name)).unwrap();
        }

        ensure_extracted(&output, &cache).unwrap();

        let mut left: Vec<String> = fs::read_dir(&cache).unwrap().flatten().map(|e| e.file_name().to_string_lossy().into_owned()).collect();
        left.sort();
        assert_eq!(left.len(), 4, "{left:?}");
        assert!(left.contains(&"notes".to_string()) && left.contains(&"short".to_string()) && left.contains(&"FEDCBA0987654321".to_string()));
        assert!(!left.contains(&"0123456789abcdef".to_string()) && !left.contains(&"fedcba9876543210.partial-4242".to_string()));
    }

    #[test]
    fn an_exe_without_a_runtime_gives_none_and_touches_nothing() {
        let scratch = Scratch::new("plain");
        let exe = fake_exe(&scratch);
        let cache = scratch.path("cache");

        assert_eq!(ensure_extracted(&exe, &cache).unwrap(), None);
        assert!(!cache.exists());
    }

    #[test]
    fn an_exe_that_cannot_be_read_is_an_error_that_says_so() {
        let error = ensure_extracted(Path::new("C:\\no\\such\\shell.exe"), Path::new("C:\\no\\cache")).unwrap_err();

        assert!(error.contains("읽지 못했습니다"), "{error}");
    }
}
```

모듈을 등록한다:

```diff
diff --git a/sidecar-host/src/lib.rs b/sidecar-host/src/lib.rs
index 534dbce..a754fb5 100644
--- a/sidecar-host/src/lib.rs
+++ b/sidecar-host/src/lib.rs
@@ -6,6 +6,7 @@ pub mod folder;
 pub mod handshake;
 pub mod job;
 pub mod lifecycle;
+pub mod payload;
 pub mod settings;
 pub mod sidecar;
 pub mod sse;
```

- [ ] **Step 3: 실패를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --offline --lib payload:: 2>&1 | Select-String "^error|could not compile" | Select-Object -First 4
```

Expected: 컴파일 오류 `cannot find function `pack` in module `crate::payload``, `cannot find type `PathBuf` in this scope` 같은 것들(구현이 아직 없다), `could not compile`.

- [ ] **Step 4: 구현한다**

`sidecar-host/src/payload.rs`의 **맨 위**(시험 모듈 앞)에 아래를 넣어 파일 전체가 "구현 + 위의 시험 모듈"이 되게 한다. 시험 모듈은 그대로 둔다.

```rust
//! The sidecar's runtime inside the one exe the app is delivered as.
//!
//! `xgs-pack` appends two things to a copy of the shell's exe: the files of the sidecar image (an archive of our own, read by
//! nothing but this module) and a footer at the very end that says where the archive is. Windows ignores what follows the
//! sections of an exe, so the file still runs as the shell. When it runs, [ensure_extracted] finds the footer, unpacks the archive
//! into a folder named after the archive's hash (once: the next start finds the folder complete and uses it), and the sidecar is
//! started from there. A different build of the app has another hash and gets a folder of its own; the old ones are removed.
//!
//! The archive is a sequence of entries, each `[method u8][path length u32][path][size u64][stored length u64][stored bytes]`
//! (little endian; method 0 is stored as it is, 1 is raw deflate; paths use `/`). The footer is
//! `[XGSPAYLD][version u32][archive offset u64][archive length u64][sha-256 of the archive]`.

use flate2::read::DeflateDecoder;
use flate2::write::DeflateEncoder;
use flate2::Compression;
use sha2::{Digest, Sha256};
use std::fs::{self, File};
use std::io::{self, BufReader, BufWriter, Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

const MAGIC: &[u8; 8] = b"XGSPAYLD";
const VERSION: u32 = 1;
const FOOTER_LEN: u64 = 8 + 4 + 8 + 8 + 32;
/// Written into a runtime folder when everything is in it. A folder without it was cut short and is made again.
const COMPLETE_MARKER: &str = ".xgs-complete";
const STORED: u8 = 0;
const DEFLATED: u8 = 1;
/// No path of the sidecar image comes near this; a longer one is a damaged archive, not a reason to allocate.
const MAX_PATH_LEN: u32 = 4096;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Footer {
    pub offset: u64,
    pub length: u64,
    pub key: [u8; 32],
}

impl Footer {
    /// The name of the folder the archive is unpacked into: the first 16 hex digits of its hash.
    pub fn key_name(&self) -> String {
        self.key[..8].iter().map(|byte| format!("{byte:02x}")).collect()
    }

    fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::with_capacity(FOOTER_LEN as usize);
        bytes.extend_from_slice(MAGIC);
        bytes.extend_from_slice(&VERSION.to_le_bytes());
        bytes.extend_from_slice(&self.offset.to_le_bytes());
        bytes.extend_from_slice(&self.length.to_le_bytes());
        bytes.extend_from_slice(&self.key);
        bytes
    }
}

fn invalid(message: impl Into<String>) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message.into())
}

/// The footer at the end of [source], or None when it does not end with one (a plain exe, a short file). A footer that is there but
/// does not fit the file is an error: the file is damaged.
pub fn read_footer<R: Read + Seek>(source: &mut R) -> io::Result<Option<Footer>> {
    let size = source.seek(SeekFrom::End(0))?;
    if size < FOOTER_LEN {
        return Ok(None);
    }
    source.seek(SeekFrom::Start(size - FOOTER_LEN))?;
    let mut raw = vec![0u8; FOOTER_LEN as usize];
    source.read_exact(&mut raw)?;
    if &raw[..8] != MAGIC {
        return Ok(None);
    }
    let number = |from: usize| u64::from_le_bytes(raw[from..from + 8].try_into().expect("8 bytes"));
    let version = u32::from_le_bytes(raw[8..12].try_into().expect("4 bytes"));
    if version != VERSION {
        return Err(invalid(format!("the runtime in this exe has version {version}; this build reads version {VERSION}")));
    }
    let footer = Footer { offset: number(12), length: number(20), key: raw[28..60].try_into().expect("32 bytes") };
    if footer.offset.checked_add(footer.length) != Some(size - FOOTER_LEN) {
        return Err(invalid("the footer of the runtime does not fit the file"));
    }
    Ok(Some(footer))
}

/// What [pack] made.
#[derive(Debug)]
pub struct Packed {
    pub footer: Footer,
    pub files: usize,
    /// The size of the files of the image, and what the archive holds of them.
    pub original_bytes: u64,
    pub stored_bytes: u64,
}

/// A writer that hashes and counts what goes through it.
struct Counting<W: Write> {
    inner: W,
    hasher: Sha256,
    written: u64,
}

impl<W: Write> Write for Counting<W> {
    fn write(&mut self, buffer: &[u8]) -> io::Result<usize> {
        let count = self.inner.write(buffer)?;
        self.hasher.update(&buffer[..count]);
        self.written += count as u64;
        Ok(count)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.inner.flush()
    }
}

/// Every file under [dir] as a path relative to it with `/`, in order (the same folder always gives the same archive).
fn list_files(dir: &Path) -> io::Result<Vec<String>> {
    fn walk(root: &Path, current: &Path, found: &mut Vec<String>) -> io::Result<()> {
        for entry in fs::read_dir(current)? {
            let entry = entry?;
            let path = entry.path();
            if entry.file_type()?.is_dir() {
                walk(root, &path, found)?;
            } else {
                let relative = path.strip_prefix(root).expect("below the root");
                let parts: Vec<String> = relative
                    .components()
                    .map(|part| part.as_os_str().to_str().map(str::to_string).ok_or_else(|| invalid(format!("not a UTF-8 name: {}", path.display()))))
                    .collect::<io::Result<_>>()?;
                found.push(parts.join("/"));
            }
        }
        Ok(())
    }
    let mut found = Vec::new();
    walk(dir, dir, &mut found)?;
    found.sort();
    Ok(found)
}

/// Writes a copy of [exe] to [output] with the files of [image] and the footer appended. An exe that carries a runtime already is
/// refused: packing twice would hide the first one behind the second.
pub fn pack(exe: &Path, image: &Path, output: &Path) -> io::Result<Packed> {
    if read_footer(&mut File::open(exe)?)?.is_some() {
        return Err(invalid(format!("{} already carries a runtime", exe.display())));
    }
    let mut out = BufWriter::with_capacity(1 << 20, File::create(output)?);
    let offset = io::copy(&mut BufReader::new(File::open(exe)?), &mut out)?;
    let mut archive = Counting { inner: out, hasher: Sha256::new(), written: 0 };
    let (mut files, mut original_bytes) = (0usize, 0u64);
    for relative in list_files(image)? {
        let path = image.join(&relative);
        let mut compressed = Vec::new();
        let mut original = 0u64;
        {
            let mut encoder = DeflateEncoder::new(&mut compressed, Compression::new(6));
            let mut input = BufReader::new(File::open(&path)?);
            let mut buffer = vec![0u8; 1 << 16];
            loop {
                let count = input.read(&mut buffer)?;
                if count == 0 {
                    break;
                }
                encoder.write_all(&buffer[..count])?;
                original += count as u64;
            }
            encoder.finish()?;
        }
        // Data that does not shrink (an already compressed jar, say) is kept as it is: that costs nothing to unpack.
        let deflate = (compressed.len() as u64) < original;
        let name = relative.as_bytes();
        archive.write_all(&[if deflate { DEFLATED } else { STORED }])?;
        archive.write_all(&(name.len() as u32).to_le_bytes())?;
        archive.write_all(name)?;
        archive.write_all(&original.to_le_bytes())?;
        if deflate {
            archive.write_all(&(compressed.len() as u64).to_le_bytes())?;
            archive.write_all(&compressed)?;
        } else {
            archive.write_all(&original.to_le_bytes())?;
            io::copy(&mut BufReader::new(File::open(&path)?), &mut archive)?;
        }
        files += 1;
        original_bytes += original;
    }
    let length = archive.written;
    let key: [u8; 32] = archive.hasher.clone().finalize().into();
    let footer = Footer { offset, length, key };
    let mut out = archive.inner;
    out.write_all(&footer.to_bytes())?;
    out.flush()?;
    Ok(Packed { footer, files, original_bytes, stored_bytes: length })
}

/// A path of the archive as a path below the folder it is unpacked into. Refused: anything that could leave that folder or name a
/// drive (`..`, a leading `/`, `\`, `:`) and an empty part.
fn safe_relative(name: &str) -> io::Result<PathBuf> {
    if name.is_empty() || name.contains('\\') || name.contains(':') || name.contains('\0') {
        return Err(invalid(format!("a path in the runtime is not allowed: {name:?}")));
    }
    let mut path = PathBuf::new();
    for part in name.split('/') {
        if part.is_empty() || part == "." || part == ".." {
            return Err(invalid(format!("a path in the runtime is not allowed: {name:?}")));
        }
        path.push(part);
    }
    Ok(path)
}

/// Unpacks the archive at [footer] of [source] into the existing folder [target]. Returns how many files it made.
pub fn extract<R: Read + Seek>(source: &mut R, footer: &Footer, target: &Path) -> io::Result<usize> {
    source.seek(SeekFrom::Start(footer.offset))?;
    let mut archive = BufReader::with_capacity(1 << 20, source.by_ref().take(footer.length));
    let mut files = 0usize;
    loop {
        let mut method = [0u8; 1];
        if archive.read(&mut method)? == 0 {
            return Ok(files);
        }
        let mut word = [0u8; 4];
        archive.read_exact(&mut word)?;
        let name_length = u32::from_le_bytes(word);
        if name_length == 0 || name_length > MAX_PATH_LEN {
            return Err(invalid("the runtime is damaged (a path of an impossible length)"));
        }
        let mut name = vec![0u8; name_length as usize];
        archive.read_exact(&mut name)?;
        let name = String::from_utf8(name).map_err(|_| invalid("the runtime is damaged (a path that is not UTF-8)"))?;
        let mut number = [0u8; 8];
        archive.read_exact(&mut number)?;
        let original = u64::from_le_bytes(number);
        archive.read_exact(&mut number)?;
        let stored = u64::from_le_bytes(number);
        let destination = target.join(safe_relative(&name)?);
        if let Some(parent) = destination.parent() {
            fs::create_dir_all(parent)?;
        }
        let mut data = archive.by_ref().take(stored);
        let mut file = BufWriter::with_capacity(1 << 20, File::create(&destination)?);
        let written = match method[0] {
            STORED => io::copy(&mut data, &mut file)?,
            DEFLATED => io::copy(&mut DeflateDecoder::new(&mut data), &mut file)?,
            other => return Err(invalid(format!("the runtime is damaged (method {other})"))),
        };
        io::copy(&mut data, &mut io::sink())?;
        file.flush()?;
        if data.limit() != 0 {
            return Err(invalid(format!("the runtime is cut short at {name}")));
        }
        if written != original {
            return Err(invalid(format!("the runtime is damaged at {name}: {written} bytes instead of {original}")));
        }
        files += 1;
    }
}

fn is_complete(folder: &Path) -> bool {
    folder.join(COMPLETE_MARKER).is_file()
}

fn remove_folder(folder: &Path) {
    let _ = fs::remove_dir_all(folder);
}

/// A name the cache makes for its own folders: a key (16 hex digits) or a half-made one (`<key>.partial-<pid>`).
fn is_cache_name(name: &str) -> bool {
    let key = name.split(".partial-").next().unwrap_or(name);
    key.len() == 16 && key.chars().all(|c| c.is_ascii_hexdigit() && !c.is_ascii_uppercase())
}

/// The folder with the runtime of [exe], unpacked into [cache] when it is not there yet. None when [exe] carries no runtime. The
/// folders of other builds in [cache] are removed afterwards (best effort: a failure to do so is not a failure to start).
pub fn ensure_extracted(exe: &Path, cache: &Path) -> Result<Option<PathBuf>, String> {
    let describe = |what: &str, error: io::Error| format!("xGetSongs의 실행 파일에 든 런타임을 {what}: {error}");
    let mut file = File::open(exe).map_err(|error| describe("읽지 못했습니다", error))?;
    let Some(footer) = read_footer(&mut file).map_err(|error| describe("읽지 못했습니다", error))? else {
        return Ok(None);
    };
    let name = footer.key_name();
    let ready = cache.join(&name);
    if !is_complete(&ready) {
        remove_folder(&ready); // what an earlier start left half-way
        let partial = cache.join(format!("{name}.partial-{}", std::process::id()));
        remove_folder(&partial);
        let made = fs::create_dir_all(&partial)
            .and_then(|()| extract(&mut file, &footer, &partial))
            .and_then(|_| fs::write(partial.join(COMPLETE_MARKER), name.as_bytes()));
        if let Err(error) = made {
            remove_folder(&partial);
            return Err(describe("풀지 못했습니다", error));
        }
        if let Err(error) = fs::rename(&partial, &ready) {
            remove_folder(&partial);
            // Another copy of the app may have finished the same folder in the meantime: that one is as good.
            if !is_complete(&ready) {
                return Err(describe("자리에 놓지 못했습니다", error));
            }
        }
    }
    if let Ok(entries) = fs::read_dir(cache) {
        for entry in entries.flatten() {
            let other = entry.file_name().to_string_lossy().into_owned();
            if other != name && is_cache_name(&other) {
                remove_folder(&entry.path());
            }
        }
    }
    Ok(Some(ready))
}
```

- [ ] **Step 5: 통과를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --offline --lib payload:: 2>&1 | Select-String "^test result|FAILED"
```

Expected: `test result: ok. 17 passed`.

- [ ] **Step 6: 전체와 clippy**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --offline 2>&1 | Select-String "^test result|FAILED"; cargo clippy --offline --all-targets 2>&1 | Select-String "^(warning|error)" | ForEach-Object { $_.Line }
```

Expected: `lib`이 `136 passed`(119 + 17), 통합 시험 둘은 `ignored`. clippy는 `the method `set_extended_limit_info` doesn't need a mutable reference`(원래 있던 `job.rs:11`)와 그것을 센 줄 둘뿐이다.

- [ ] **Step 7: 커밋한다**

```powershell
Set-Location C:\Projects\xGetSongs; git add sidecar-host/Cargo.toml sidecar-host/Cargo.lock sidecar-host/src/lib.rs sidecar-host/src/payload.rs; git commit -m "feat(sidecar-host): exe 뒤에 붙인 런타임을 읽고 풀어 쓰는 payload 모듈

자체 형식(항목별 deflate, 끝에 꼬리표와 SHA-256), 해시 이름의 폴더에 한 번만 풀고 완료 표식이 있는 폴더만 쓴다. <해시>.partial-<pid>에 풀고 이름을 바꿔 풀다 끊겨도 반쪽을 쓰지 않으며 다른 빌드의 폴더는 지운다. .. 경로와 크기 불일치는 오류.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: exe가 지닌 런타임으로 사이드카를 시작한다 (`SidecarConfig::resolve`)

**Files:**
- Modify: `sidecar-host/src/config.rs`

**Interfaces:**
- Consumes (Task 1): `payload::pack`(시험에서), `payload::ensure_extracted`.
- Produces (Task 6이 쓴다): `runtime_cache_dir(get: impl Fn(&str) -> Option<OsString>) -> PathBuf`(`XGS_SIDECAR_APPDATA`가 있으면 `<그것>\runtime`, 아니면 `%LOCALAPPDATA%\xGetSongs\runtime`, 아니면 `<앱 데이터>\runtime`), `SidecarConfig::resolve(get, exe: Option<&Path>) -> Result<SidecarConfig, String>`. `from_env`가 `resolve`를 부른다. 순서: `XGS_SIDECAR_LIB`/`XGS_SIDECAR_DIR`가 있으면 지금 그대로, 없고 exe가 런타임을 지니면 그 풀어 쓴 사본(로그 폴더는 `XGS_SIDECAR_LOGDIR`뿐), 아니면 지금 그대로(`<exe>\sidecar`). 빈 값은 없는 것.

- [ ] **Step 1: 실패하는 시험을 쓴다**

`sidecar-host/src/config.rs`의 시험 모듈에 적용한다(새 시험 9개와 그 도우미):

```diff
diff --git a/sidecar-host/src/config.rs b/sidecar-host/src/config.rs
index 08b6343..a61a698 100644
--- a/sidecar-host/src/config.rs
+++ b/sidecar-host/src/config.rs
@@ -249,6 +249,186 @@ mod tests {
         assert!(!args.iter().any(|a| a == "--log-dir"), "{args:?}");
     }
 
+    /// A fresh folder in the temp folder, deleted when the test ends.
+    struct Scratch(PathBuf);
+
+    impl Scratch {
+        fn new(name: &str) -> Scratch {
+            let dir = std::env::temp_dir().join(format!("xgs-resolve-test-{}-{name}", std::process::id()));
+            let _ = std::fs::remove_dir_all(&dir);
+            std::fs::create_dir_all(&dir).unwrap();
+            Scratch(dir)
+        }
+
+        fn text(&self) -> &str {
+            self.0.to_str().unwrap()
+        }
+    }
+
+    impl Drop for Scratch {
+        fn drop(&mut self) {
+            let _ = std::fs::remove_dir_all(&self.0);
+        }
+    }
+
+    /// An exe that carries a sidecar image (a fake `java.exe` and a `lib` with one file) as `xgs-pack` makes it.
+    fn packed_exe(scratch: &Scratch) -> PathBuf {
+        let image = scratch.0.join("image");
+        std::fs::create_dir_all(image.join("runtime").join("bin")).unwrap();
+        std::fs::create_dir_all(image.join("lib")).unwrap();
+        std::fs::write(image.join("runtime").join("bin").join("java.exe"), b"fake java").unwrap();
+        std::fs::write(image.join("lib").join("server.jar"), b"fake jar").unwrap();
+        let exe = scratch.0.join("shell.exe");
+        std::fs::write(&exe, b"MZ fake shell").unwrap();
+        let packed = scratch.0.join("xGetSongs.exe");
+        crate::payload::pack(&exe, &image, &packed).unwrap();
+        packed
+    }
+
+    #[test]
+    fn an_exe_that_carries_a_sidecar_starts_it_from_a_copy_in_the_app_data_folder() {
+        let scratch = Scratch::new("carries");
+        let exe = packed_exe(&scratch);
+        let data = scratch.0.join("data");
+
+        let config = SidecarConfig::resolve(lookup(&[("XGS_SIDECAR_APPDATA", data.to_str().unwrap())]), Some(&exe)).unwrap();
+
+        let image = config.lib_dir.parent().unwrap().to_path_buf();
+        assert_eq!(image.parent().unwrap(), data.join("runtime"), "unpacked below the runtime folder of the app data folder");
+        assert_eq!(config.java, image.join("runtime").join("bin").join("java.exe"));
+        assert_eq!(std::fs::read(config.java.as_path()).unwrap(), b"fake java");
+        assert_eq!(std::fs::read(config.lib_dir.join("server.jar")).unwrap(), b"fake jar");
+        assert_eq!(config.app_data, data);
+        assert_eq!(config.log_dir, None, "no log folder next to an exe that may lie anywhere: the sidecar writes to the app data folder");
+    }
+
+    #[test]
+    fn the_unpacked_copy_goes_to_the_local_app_data_folder_when_the_app_data_folder_is_not_named() {
+        let scratch = Scratch::new("local");
+        let exe = packed_exe(&scratch);
+        let local = scratch.0.join("local");
+
+        let config = SidecarConfig::resolve(
+            lookup(&[("APPDATA", scratch.0.join("roaming").to_str().unwrap()), ("LOCALAPPDATA", local.to_str().unwrap())]),
+            Some(&exe),
+        )
+        .unwrap();
+
+        assert!(config.java.starts_with(local.join("xGetSongs").join("runtime")), "{:?}", config.java);
+        assert_eq!(config.app_data, scratch.0.join("roaming").join("xGetSongs"), "the app's own files stay where the Compose app kept them");
+    }
+
+    #[test]
+    fn the_log_folder_variable_is_still_honoured_for_an_exe_that_carries_a_sidecar() {
+        let scratch = Scratch::new("logdir");
+        let exe = packed_exe(&scratch);
+        let data = scratch.0.join("data");
+
+        let config = SidecarConfig::resolve(
+            lookup(&[("XGS_SIDECAR_APPDATA", data.to_str().unwrap()), ("XGS_SIDECAR_LOGDIR", r"C:\logs")]),
+            Some(&exe),
+        )
+        .unwrap();
+
+        assert_eq!(config.log_dir, Some(PathBuf::from(r"C:\logs")));
+    }
+
+    #[test]
+    fn the_variables_for_development_win_over_what_the_exe_carries_and_unpack_nothing() {
+        let scratch = Scratch::new("variables");
+        let exe = packed_exe(&scratch);
+        let data = scratch.0.join("data");
+        let image = fake_image("variables-image");
+
+        let by_lib = SidecarConfig::resolve(
+            lookup(&[("XGS_SIDECAR_APPDATA", data.to_str().unwrap()), ("XGS_SIDECAR_LIB", r"C:\x\lib")]),
+            Some(&exe),
+        )
+        .unwrap();
+        let by_dir = SidecarConfig::resolve(
+            lookup(&[("XGS_SIDECAR_APPDATA", data.to_str().unwrap()), ("XGS_SIDECAR_DIR", image.to_str().unwrap())]),
+            Some(&exe),
+        )
+        .unwrap();
+
+        assert_eq!(by_lib.lib_dir, PathBuf::from(r"C:\x\lib"));
+        assert_eq!(by_dir.lib_dir, image.join("lib"));
+        assert!(!data.join("runtime").exists(), "nothing was unpacked");
+        let _ = std::fs::remove_dir_all(&image);
+    }
+
+    #[test]
+    fn an_exe_that_carries_nothing_falls_back_to_the_sidecar_folder_next_to_it() {
+        let scratch = Scratch::new("plain");
+        let image = scratch.0.join("sidecar");
+        std::fs::create_dir_all(image.join("runtime").join("bin")).unwrap();
+        std::fs::write(image.join("runtime").join("bin").join("java.exe"), b"").unwrap();
+        let exe = scratch.0.join("shell.exe");
+        std::fs::write(&exe, b"MZ plain shell").unwrap();
+
+        let config = SidecarConfig::resolve(lookup(&[("XGS_SIDECAR_APPDATA", scratch.text())]), Some(&exe)).unwrap();
+
+        assert_eq!(config.lib_dir, image.join("lib"));
+        assert_eq!(config.log_dir, Some(scratch.0.join("log")), "as before: the logs go next to the exe");
+    }
+
+    #[test]
+    fn an_exe_that_carries_nothing_and_has_no_sidecar_next_to_it_is_an_error_that_names_the_ways_to_say_where_it_is() {
+        let scratch = Scratch::new("nothing");
+        let exe = scratch.0.join("shell.exe");
+        std::fs::write(&exe, b"MZ plain shell").unwrap();
+
+        let error = SidecarConfig::resolve(lookup(&[("XGS_SIDECAR_APPDATA", scratch.text())]), Some(&exe)).unwrap_err();
+
+        assert!(error.contains("java.exe"), "{error}");
+    }
+
+    #[test]
+    fn a_damaged_runtime_in_the_exe_is_an_error_that_says_so_and_not_a_silent_fallback() {
+        let scratch = Scratch::new("damaged");
+        let exe = packed_exe(&scratch);
+        let mut bytes = std::fs::read(&exe).unwrap();
+        bytes.drain(20..40); // the footer no longer fits the file
+        std::fs::write(&exe, bytes).unwrap();
+        // a sidecar folder next to the exe must not hide that
+        std::fs::create_dir_all(scratch.0.join("sidecar").join("runtime").join("bin")).unwrap();
+        std::fs::write(scratch.0.join("sidecar").join("runtime").join("bin").join("java.exe"), b"").unwrap();
+
+        let error = SidecarConfig::resolve(lookup(&[("XGS_SIDECAR_APPDATA", scratch.text())]), Some(&exe)).unwrap_err();
+
+        assert!(error.contains("런타임"), "{error}");
+    }
+
+    #[test]
+    fn variables_that_are_empty_count_as_not_set() {
+        // A variable that was "cleared" by setting it to nothing (PowerShell does that) must not switch off the exe's own runtime.
+        let scratch = Scratch::new("empty");
+        let exe = packed_exe(&scratch);
+        let data = scratch.0.join("data");
+
+        let config = SidecarConfig::resolve(
+            lookup(&[
+                ("XGS_SIDECAR_APPDATA", data.to_str().unwrap()),
+                ("XGS_SIDECAR_LIB", ""),
+                ("XGS_SIDECAR_DIR", ""),
+                ("XGS_SIDECAR_JAVA", ""),
+                ("XGS_SIDECAR_LOGDIR", ""),
+            ]),
+            Some(&exe),
+        )
+        .unwrap();
+
+        assert!(config.java.starts_with(data.join("runtime")), "{:?}", config.java);
+        assert_eq!(config.log_dir, None);
+    }
+
+    #[test]
+    fn without_an_exe_the_old_rules_apply() {
+        let error = SidecarConfig::resolve(lookup(&[]), None).unwrap_err();
+
+        assert!(error.contains("XGS_SIDECAR_LIB"), "{error}");
+    }
+
     #[test]
     fn the_stderr_log_lives_in_the_app_data_folder() {
         let config = SidecarConfig {
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --offline --lib config:: 2>&1 | Select-String "^error" | Select-Object -First 2
```

Expected: 컴파일 오류 `no associated function or constant named `resolve` found for struct `SidecarConfig``.

- [ ] **Step 3: 구현한다**

```diff
diff --git a/sidecar-host/src/config.rs b/sidecar-host/src/config.rs
index a61a698..be44ac0 100644
--- a/sidecar-host/src/config.rs
+++ b/sidecar-host/src/config.rs
@@ -1,3 +1,4 @@
+use crate::payload;
 use std::ffi::OsString;
 use std::path::{Path, PathBuf};
 use std::process::Command;
@@ -29,10 +30,48 @@ pub fn app_data_dir(get: impl Fn(&str) -> Option<OsString>) -> PathBuf {
     given("USERPROFILE").map(PathBuf::from).unwrap_or_else(std::env::temp_dir).join(".xgetsongs")
 }
 
+/// Where the runtime that an exe carries is unpacked to (a cache: it can be deleted and is made again). Below the app data folder
+/// when `XGS_SIDECAR_APPDATA` names one (so that a check that moves the app data folder moves this too), else in
+/// `%LOCALAPPDATA%\xGetSongs` (a few hundred MB do not belong in the roaming profile), else below the app data folder.
+pub fn runtime_cache_dir(get: impl Fn(&str) -> Option<OsString>) -> PathBuf {
+    let given = |name: &str| get(name).filter(|value| !value.is_empty());
+    if let Some(dir) = given("XGS_SIDECAR_APPDATA") {
+        return PathBuf::from(dir).join("runtime");
+    }
+    if let Some(local) = given("LOCALAPPDATA") {
+        return PathBuf::from(local).join("xGetSongs").join("runtime");
+    }
+    app_data_dir(&get).join("runtime")
+}
+
 impl SidecarConfig {
     pub fn from_env() -> Result<SidecarConfig, String> {
-        let exe_dir = std::env::current_exe().ok().and_then(|exe| exe.parent().map(Path::to_path_buf));
-        SidecarConfig::from_lookup(|key| std::env::var_os(key), exe_dir.as_deref())
+        let exe = std::env::current_exe().ok();
+        SidecarConfig::resolve(|key| std::env::var_os(key), exe.as_deref())
+    }
+
+    /// [from_lookup] for the running program [exe]: when neither `XGS_SIDECAR_LIB` nor `XGS_SIDECAR_DIR` says where the sidecar is
+    /// and [exe] carries one (see [crate::payload]), the sidecar is started from a copy of it unpacked to [runtime_cache_dir] (the
+    /// first start takes a while: that is where the time goes). An exe that carries a runtime but cannot give it up is an error, not
+    /// a reason to look elsewhere. The log folder is then the sidecar's own choice (the app data folder), not `log` next to an exe
+    /// that may lie anywhere; `XGS_SIDECAR_LOGDIR` still names one.
+    pub fn resolve(get: impl Fn(&str) -> Option<OsString>, exe: Option<&Path>) -> Result<SidecarConfig, String> {
+        // A variable that is set to nothing is not set (a shell that "clears" one by setting it to an empty string leaves exactly that).
+        let get = |key: &str| get(key).filter(|value| !value.is_empty());
+        let carried = match exe {
+            Some(exe) if get("XGS_SIDECAR_LIB").is_none() && get("XGS_SIDECAR_DIR").is_none() => {
+                payload::ensure_extracted(exe, &runtime_cache_dir(get))?
+            }
+            _ => None,
+        };
+        if let Some(image) = carried {
+            let image = image.into_os_string();
+            return SidecarConfig::from_lookup(
+                |key| if key == "XGS_SIDECAR_DIR" { Some(image.clone()) } else { get(key) },
+                None,
+            );
+        }
+        SidecarConfig::from_lookup(get, exe.and_then(Path::parent))
     }
 
     /// Where java and the jars come from, in this order:
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --offline 2>&1 | Select-String "^test result|FAILED"; cargo clippy --offline --all-targets 2>&1 | Select-String "^(warning|error)" | ForEach-Object { $_.Line }
```

Expected: `lib`이 `145 passed`(136 + 9), clippy는 Task 1과 같다.

- [ ] **Step 5: 커밋한다**

```powershell
Set-Location C:\Projects\xGetSongs; git add sidecar-host/src/config.rs; git commit -m "feat(sidecar-host): exe가 런타임을 지니면 풀어 쓴 사본에서 사이드카를 시작한다

SidecarConfig::resolve: 개발용 변수가 이기고, 없으면 exe의 런타임을 LOCALAPPDATA 아래에 풀어 쓰고(풀지 못하면 오류), 없으면 exe 옆 sidecar 폴더. 묶은 exe의 로그는 앱 데이터 logs에. 빈 환경 변수는 없는 것으로 본다.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: 셸 exe 뒤에 이미지를 붙이는 도구 (`xgs-pack`)

**Files:**
- Create: `sidecar-host/src/bin/xgs-pack.rs`, `sidecar-host/tests/pack_cli.rs`

**Interfaces:**
- Consumes (Task 1): `payload::pack`, `payload::read_footer`.
- Produces (Task 7이 쓴다): `xgs-pack <셸.exe> <이미지 폴더> <출력.exe>`. 이미지 폴더에 `runtime\bin\java.exe`나 `lib`가 없으면 거부(종료 코드 1), 인자가 틀리면 사용법(2), 이미 런타임을 지닌 exe나 출력이 입력과 같은 파일이면 거부, 성공하면 한 줄로 요약(`<출력>: 493 files, 147.9 MB of image stored as 124.4 MB behind the exe, key <16자리>, 129.3 MB in all`).

- [ ] **Step 1: 실패하는 시험을 쓴다**

```rust
//! `xgs-pack`, the tool that appends a sidecar image to the shell's exe, run as a person runs it.

use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Output};
use xgs_sidecar::payload;

struct Scratch(PathBuf);

impl Scratch {
    fn new(name: &str) -> Scratch {
        let dir = std::env::temp_dir().join(format!("xgs-pack-cli-test-{}-{name}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        Scratch(dir)
    }
}

impl Drop for Scratch {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

fn pack(args: &[&Path]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_xgs-pack")).args(args).output().expect("xgs-pack starts")
}

fn text(bytes: &[u8]) -> String {
    String::from_utf8_lossy(bytes).into_owned()
}

fn image_in(dir: &Path) -> PathBuf {
    let image = dir.join("image");
    fs::create_dir_all(image.join("runtime").join("bin")).unwrap();
    fs::create_dir_all(image.join("lib")).unwrap();
    fs::write(image.join("runtime").join("bin").join("java.exe"), b"fake java").unwrap();
    fs::write(image.join("lib").join("server.jar"), b"fake jar").unwrap();
    image
}

#[test]
fn an_image_is_appended_to_a_copy_of_the_exe_and_the_summary_names_what_went_in() {
    let scratch = Scratch::new("ok");
    let image = image_in(&scratch.0);
    let exe = scratch.0.join("shell.exe");
    fs::write(&exe, b"MZ the shell").unwrap();
    let output = scratch.0.join("out").join("xGetSongs.exe");

    let result = pack(&[&exe, &image, &output]);

    assert!(result.status.success(), "{}", text(&result.stderr));
    let footer = payload::read_footer(&mut fs::File::open(&output).unwrap()).unwrap().expect("the output carries a runtime");
    assert_eq!(footer.offset, b"MZ the shell".len() as u64);
    let summary = text(&result.stdout);
    assert!(summary.contains("2 files"), "{summary}");
    assert!(summary.contains(&footer.key_name()), "{summary}");
    assert_eq!(fs::read(&exe).unwrap(), b"MZ the shell", "the exe it was given is left alone");
}

#[test]
fn the_wrong_number_of_arguments_is_a_usage_message_and_exit_code_2() {
    let result = pack(&[Path::new("only-one")]);

    assert_eq!(result.status.code(), Some(2));
    assert!(text(&result.stderr).contains("usage"), "{}", text(&result.stderr));
}

#[test]
fn a_folder_that_is_not_a_sidecar_image_is_refused_and_nothing_is_written() {
    let scratch = Scratch::new("not-an-image");
    let exe = scratch.0.join("shell.exe");
    fs::write(&exe, b"MZ the shell").unwrap();
    let wrong = scratch.0.join("wrong");
    fs::create_dir_all(wrong.join("lib")).unwrap();
    let output = scratch.0.join("xGetSongs.exe");

    let result = pack(&[&exe, &wrong, &output]);

    assert_eq!(result.status.code(), Some(1));
    assert!(text(&result.stderr).contains("java.exe"), "{}", text(&result.stderr));
    assert!(!output.exists());
}

#[test]
fn an_exe_that_already_carries_a_runtime_is_refused() {
    let scratch = Scratch::new("twice");
    let image = image_in(&scratch.0);
    let exe = scratch.0.join("shell.exe");
    fs::write(&exe, b"MZ the shell").unwrap();
    let once = scratch.0.join("once.exe");
    assert!(pack(&[&exe, &image, &once]).status.success());

    let result = pack(&[&once, &image, &scratch.0.join("twice.exe")]);

    assert_eq!(result.status.code(), Some(1));
    assert!(text(&result.stderr).contains("already carries"), "{}", text(&result.stderr));
}

#[test]
fn packing_an_exe_over_itself_is_refused_and_leaves_it_alone() {
    let scratch = Scratch::new("itself");
    let image = image_in(&scratch.0);
    let exe = scratch.0.join("shell.exe");
    fs::write(&exe, b"MZ the shell").unwrap();

    let result = pack(&[&exe, &image, &exe]);

    assert_eq!(result.status.code(), Some(1));
    assert!(text(&result.stderr).contains("same file"), "{}", text(&result.stderr));
    assert_eq!(fs::read(&exe).unwrap(), b"MZ the shell");
}
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --offline --test pack_cli 2>&1 | Select-String "^error" | Select-Object -First 2
```

Expected: 컴파일 오류 `environment variable `CARGO_BIN_EXE_xgs-pack` not defined at compile time`.

- [ ] **Step 3: 구현한다**

```rust
//! Appends a sidecar image to a copy of the shell's exe, so that one file is all there is to hand out:
//!
//!     xgs-pack <shell.exe> <sidecar-image-folder> <output.exe>
//!
//! The image is what `gradlew :server:sidecarImage` makes (`runtime\bin\java.exe` and `lib`); see `xgs_sidecar::payload` for what the
//! output is and how the app unpacks it.

use std::fs;
use std::path::Path;
use std::process::ExitCode;
use xgs_sidecar::payload;

fn megabytes(bytes: u64) -> f64 {
    bytes as f64 / (1024.0 * 1024.0)
}

fn run(exe: &Path, image: &Path, output: &Path) -> Result<String, String> {
    let java = image.join("runtime").join("bin").join("java.exe");
    if !java.is_file() || !image.join("lib").is_dir() {
        return Err(format!("{} is not a sidecar image: {} or its lib folder is missing", image.display(), java.display()));
    }
    // Writing the output empties it before the exe is read: the same file would be lost.
    if fs::canonicalize(exe).ok().is_some_and(|exe| Some(exe) == fs::canonicalize(output).ok()) {
        return Err(format!("{} is the same file as the output", exe.display()));
    }
    if let Some(parent) = output.parent().filter(|parent| !parent.as_os_str().is_empty()) {
        fs::create_dir_all(parent).map_err(|error| format!("cannot create {}: {error}", parent.display()))?;
    }
    let packed = payload::pack(exe, image, output).map_err(|error| {
        let _ = fs::remove_file(output); // no half-written exe to be mistaken for a real one
        format!("cannot pack {}: {error}", image.display())
    })?;
    Ok(format!(
        "{}: {} files, {:.1} MB of image stored as {:.1} MB behind the exe, key {}, {:.1} MB in all",
        output.display(),
        packed.files,
        megabytes(packed.original_bytes),
        megabytes(packed.stored_bytes),
        packed.footer.key_name(),
        megabytes(fs::metadata(output).map(|meta| meta.len()).unwrap_or(0)),
    ))
}

fn main() -> ExitCode {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    let [exe, image, output] = arguments.as_slice() else {
        eprintln!("usage: xgs-pack <shell.exe> <sidecar-image-folder> <output.exe>");
        return ExitCode::from(2);
    };
    match run(Path::new(exe), Path::new(image), Path::new(output)) {
        Ok(summary) => {
            println!("{summary}");
            ExitCode::SUCCESS
        }
        Err(message) => {
            eprintln!("xgs-pack: {message}");
            ExitCode::FAILURE
        }
    }
}
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --offline 2>&1 | Select-String "^test result|FAILED"; cargo clippy --offline --all-targets 2>&1 | Select-String "^(warning|error)" | ForEach-Object { $_.Line }
```

Expected: `lib` 145개, `pack_cli`가 `5 passed`, clippy는 같다.

- [ ] **Step 5: 커밋한다**

```powershell
Set-Location C:\Projects\xGetSongs; git add sidecar-host/src/bin/xgs-pack.rs sidecar-host/tests/pack_cli.rs; git commit -m "feat(sidecar-host): xgs-pack — 셸 exe 뒤에 사이드카 이미지를 붙인다

이미지가 아닌 폴더, 이미 런타임을 지닌 exe, 출력이 입력과 같은 파일은 거부하고 아무것도 쓰지 않는다.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: WebView2 감지와 안내 상자 (`webview2`)

**Files:**
- Create: `sidecar-host/src/webview2.rs`
- Modify: `sidecar-host/src/lib.rs`

**Interfaces:**
- Consumes: 없다.
- Produces (Task 6이 쓴다): `webview2::installed_version() -> Option<String>`(레지스트리 세 곳의 `pv`, 비었거나 `0.0.0.0`이면 없음), `webview2::ask_to_install() -> bool`(`확인`/`취소` 상자, `확인`이면 `true`), `webview2::open_download_page()`(`explorer.exe <주소>`), 상수 `DOWNLOAD_URL`, `MISSING_TITLE`, `MISSING_TEXT`.

- [ ] **Step 1: 실패하는 시험을 쓴다**

`sidecar-host/src/webview2.rs`를 **시험 모듈만으로** 만든다:

```rust
#[cfg(test)]
mod tests {
    use super::*;

    /// A registry in small: the value `pv` at some of the places (by their index in [LOCATIONS]).
    fn registry(entries: &[(Hive, usize, &str)]) -> impl Fn(Hive, &str) -> Option<String> {
        let entries: Vec<(Hive, &'static str, String)> =
            entries.iter().map(|(hive, index, pv)| (*hive, LOCATIONS[*index].1, pv.to_string())).collect();
        move |hive, key| entries.iter().find(|(h, k, _)| *h == hive && *k == key).map(|(_, _, value)| value.clone())
    }

    #[test]
    fn nothing_in_the_registry_is_no_runtime() {
        assert_eq!(find_version(registry(&[])), None);
    }

    #[test]
    fn a_version_at_any_of_the_three_places_is_found() {
        assert_eq!(find_version(registry(&[(Hive::LocalMachine, 0, "120.0.2210.91")])).as_deref(), Some("120.0.2210.91"));
        assert_eq!(find_version(registry(&[(Hive::LocalMachine, 1, "119.0.1")])).as_deref(), Some("119.0.1"));
        assert_eq!(find_version(registry(&[(Hive::CurrentUser, 2, "118.0.5")])).as_deref(), Some("118.0.5"));
    }

    #[test]
    fn the_leftover_of_an_uninstalled_runtime_is_no_runtime() {
        assert_eq!(find_version(registry(&[(Hive::LocalMachine, 0, "0.0.0.0")])), None);
        assert_eq!(find_version(registry(&[(Hive::LocalMachine, 0, ""), (Hive::CurrentUser, 2, "  ")])), None);
    }

    #[test]
    fn a_real_version_is_found_behind_a_leftover() {
        assert_eq!(
            find_version(registry(&[(Hive::LocalMachine, 0, "0.0.0.0"), (Hive::CurrentUser, 2, "118.0.5")])).as_deref(),
            Some("118.0.5")
        );
    }

    #[test]
    fn the_same_key_in_another_hive_is_not_the_same_place() {
        assert_eq!(find_version(registry(&[(Hive::CurrentUser, 0, "120.0.1")])), None);
    }

    #[cfg(windows)]
    #[test]
    fn the_registry_of_this_pc_gives_a_version_or_none_and_never_a_leftover() {
        match installed_version() {
            None => {}
            Some(version) => {
                assert!(is_real_version(&version), "{version}");
                assert!(version.chars().all(|c| c.is_ascii_digit() || c == '.'), "{version}");
            }
        }
    }

    #[test]
    fn the_message_names_the_runtime_and_what_to_do() {
        assert!(MISSING_TEXT.contains("WebView2"));
        assert!(MISSING_TEXT.contains("[확인]"));
        assert!(DOWNLOAD_URL.starts_with("https://go.microsoft.com/"));
    }
}
```

모듈을 등록한다:

```diff
diff --git a/sidecar-host/src/lib.rs b/sidecar-host/src/lib.rs
index a754fb5..41e5d33 100644
--- a/sidecar-host/src/lib.rs
+++ b/sidecar-host/src/lib.rs
@@ -10,6 +10,7 @@ pub mod payload;
 pub mod settings;
 pub mod sidecar;
 pub mod sse;
+pub mod webview2;
 
 #[cfg(test)]
 mod fake_server;
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --offline --lib webview2:: 2>&1 | Select-String "^error" | Select-Object -First 3
```

Expected: 컴파일 오류 `cannot find ... `Hive`/`find_version`/`LOCATIONS` in this scope`.

- [ ] **Step 3: 구현한다**

`sidecar-host/src/webview2.rs`의 **맨 위**에 아래를 넣는다. 시험 모듈은 그대로 둔다.

```rust
//! Whether the window of the app can open at all: it needs Microsoft's WebView2 runtime. Windows 11 and a Windows 10 that has been
//! updated since 2022 come with it; an older one may not. Without it the window fails to open and the app ends without a word, so the
//! app looks for the runtime first and says what is missing.

/// Where Microsoft's updater writes the version (`pv`) of the Evergreen runtime, in the order Microsoft documents: for all users on
/// a 64-bit Windows, for all users on a 32-bit one, for the current user only.
const LOCATIONS: [(Hive, &str); 3] = [
    (Hive::LocalMachine, r"SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"),
    (Hive::LocalMachine, r"SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"),
    (Hive::CurrentUser, r"Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"),
];

/// The page that hands out Microsoft's installer for the runtime.
pub const DOWNLOAD_URL: &str = "https://go.microsoft.com/fwlink/p/?LinkId=2124703";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Hive {
    LocalMachine,
    CurrentUser,
}

/// A runtime that was uninstalled leaves `0.0.0.0` behind; an empty value is no version either.
fn is_real_version(pv: &str) -> bool {
    let pv = pv.trim();
    !pv.is_empty() && pv != "0.0.0.0"
}

/// The first version [read] finds in the places the runtime writes it to. [read] gets the hive and the key and returns the value
/// `pv` of that key, if there is one.
fn find_version(read: impl Fn(Hive, &str) -> Option<String>) -> Option<String> {
    LOCATIONS.iter().filter_map(|(hive, key)| read(*hive, key)).find(|pv| is_real_version(pv)).map(|pv| pv.trim().to_string())
}

/// The version of the WebView2 runtime that is installed, or None when there is none.
#[cfg(windows)]
pub fn installed_version() -> Option<String> {
    use winreg::enums::{HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    use winreg::RegKey;
    find_version(|hive, key| {
        let root = RegKey::predef(match hive {
            Hive::LocalMachine => HKEY_LOCAL_MACHINE,
            Hive::CurrentUser => HKEY_CURRENT_USER,
        });
        root.open_subkey(key).ok()?.get_value::<String, _>("pv").ok()
    })
}

#[cfg(not(windows))]
pub fn installed_version() -> Option<String> {
    None
}

/// What the message says when the runtime is missing, and what the two buttons do.
pub const MISSING_TITLE: &str = "xGetSongs";
pub const MISSING_TEXT: &str = "xGetSongs 창을 여는 데 필요한 Microsoft Edge WebView2 런타임을 이 PC에서 찾지 못했습니다.\n\n\
    [확인]을 누르면 설치 프로그램을 받는 페이지를 엽니다. 설치한 뒤 xGetSongs를 다시 실행해 주세요.";

/// Shows [MISSING_TEXT] with an OK and a Cancel button; true when the person chose OK (to go and get the runtime).
#[cfg(windows)]
pub fn ask_to_install() -> bool {
    use std::ffi::c_void;
    use std::os::windows::ffi::OsStrExt;
    const MB_OKCANCEL: u32 = 0x1;
    const MB_ICONERROR: u32 = 0x10;
    const IDOK: i32 = 1;
    #[link(name = "user32")]
    unsafe extern "system" {
        fn MessageBoxW(window: *mut c_void, text: *const u16, caption: *const u16, kind: u32) -> i32;
    }
    let wide = |text: &str| std::ffi::OsStr::new(text).encode_wide().chain(std::iter::once(0)).collect::<Vec<u16>>();
    let (text, title) = (wide(MISSING_TEXT), wide(MISSING_TITLE));
    // SAFETY: both strings are NUL-terminated and outlive the call; a null owner window is allowed.
    unsafe { MessageBoxW(std::ptr::null_mut(), text.as_ptr(), title.as_ptr(), MB_OKCANCEL | MB_ICONERROR) == IDOK }
}

#[cfg(not(windows))]
pub fn ask_to_install() -> bool {
    false
}

/// Opens [DOWNLOAD_URL] in the browser the person uses (handed to Explorer, as the folders are).
pub fn open_download_page() {
    let _ = std::process::Command::new("explorer.exe").arg(DOWNLOAD_URL).spawn();
}
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --offline 2>&1 | Select-String "^test result|FAILED"; cargo clippy --offline --all-targets 2>&1 | Select-String "^(warning|error)" | ForEach-Object { $_.Line }
```

Expected: `lib`이 `152 passed`, `pack_cli` 5개, clippy는 같다. (`the_registry_of_this_pc_…` 시험은 이 PC의 실제 레지스트리를 읽고 있으면 점 찍힌 숫자 버전인지만 본다.)

- [ ] **Step 5: 커밋한다**

```powershell
Set-Location C:\Projects\xGetSongs; git add sidecar-host/src/webview2.rs sidecar-host/src/lib.rs; git commit -m "feat(sidecar-host): WebView2 런타임 감지와 없을 때의 안내 상자

레지스트리 세 곳의 pv를 보고 0.0.0.0이나 빈 값은 없는 것으로 본다. 안내 상자는 확인이면 설치 프로그램을 받는 페이지를 연다.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: 이미지에서 불필요한 것을 빼고 VC++ 런타임을 되살린다

**Files:**
- Modify: `server/build.gradle.kts`, `server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarImageTest.kt`

**Interfaces:**
- Consumes: 없다.
- Produces (Task 7, 8이 쓴다): `:server:sidecarImage`가 `server/build/sidecar-image`(`lib\`, `runtime\`)를 만들 때 `runtime\lib\ct.sym`, `runtime\lib\jvm.lib`가 없고 `runtime\bin\vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`이 있으며 `*.ortbak`이 없다. 크기 약 147.9MB(493개 파일).

- [ ] **Step 1: 실패하는 시험을 쓴다**

```diff
diff --git a/server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarImageTest.kt b/server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarImageTest.kt
index b87dcfb..2f1e172 100644
--- a/server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarImageTest.kt
+++ b/server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarImageTest.kt
@@ -31,6 +31,27 @@ class SidecarImageTest {
         assertTrue(Files.isRegularFile(image.resolve("runtime").resolve("bin").resolve("java.exe")), "the bundled runtime")
     }
 
+    @Test
+    fun theImageCarriesWhatJavaNeedsOnAPcWithoutTheVisualCRuntime() {
+        val bin = image.resolve("runtime").resolve("bin")
+
+        // java.exe and jvm.dll import these three. The runtime ships them as `*.dll.ortbak`, which no one loads.
+        for (name in listOf("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll")) {
+            assertTrue(Files.isRegularFile(bin.resolve(name)), "$name next to java.exe")
+        }
+        val backups = Files.list(bin).use { files -> files.map { it.fileName.toString() }.filter { it.endsWith(".ortbak") }.toList() }
+        assertEquals(emptyList(), backups, "files that are renamed back and no longer there under the old name")
+    }
+
+    @Test
+    fun theImageLeavesOutWhatRunningDoesNotNeed() {
+        val runtime = image.resolve("runtime")
+
+        for (name in listOf("lib/src.zip", "lib/ct.sym", "lib/jvm.lib", "jmods", "include")) {
+            assertTrue(Files.notExists(runtime.resolve(name)), "$name is not needed to run")
+        }
+    }
+
     @Test
     fun theBundledRuntimeRunsTheSidecarAndStopsOnTheExitLine() {
         val base = Files.createTempDirectory("xgs-image-test").toAbsolutePath()
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs; & .\gradlew.bat --no-daemon :server:sidecarImageTest --tests '*SidecarImageTest.theImage*' *> $env:TEMP\xgs-gradle-red.log; "exit=$LASTEXITCODE"; Select-String -Path $env:TEMP\xgs-gradle-red.log -Pattern "FAILED|PASSED|tests completed"
```

Expected: `exit=1`. `theImageCarriesWhatJavaNeedsOnAPcWithoutTheVisualCRuntime() FAILED`, `theImageLeavesOutWhatRunningDoesNotNeed() FAILED`, `theImageHoldsNoNativesOfOtherPlatformsAndTheServerItself() PASSED`, `3 tests completed, 2 failed`.

- [ ] **Step 3: 구현한다**

```diff
diff --git a/server/build.gradle.kts b/server/build.gradle.kts
index 5d717a4..a2d3eee 100644
--- a/server/build.gradle.kts
+++ b/server/build.gradle.kts
@@ -61,7 +61,11 @@ val sidecarImage by tasks.registering(Sync::class) {
     }
     into("runtime") {
         from(runtimeHome)
-        exclude("lib/src.zip", "jmods/**", "include/**")
+        // Nothing here is needed to run: the sources, the headers, the import library of jvm.dll, the symbols of javac.
+        exclude("lib/src.zip", "jmods/**", "include/**", "lib/ct.sym", "lib/jvm.lib")
+        // The runtime ships its copies of the Visual C++ runtime as `*.dll.ortbak`, which no one loads; java.exe needs them next to
+        // it on a PC that has no Visual C++ Redistributable.
+        rename("(.*\\.dll)\\.ortbak", "$1")
     }
 }
 
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs; & .\gradlew.bat --no-daemon :server:sidecarImageTest *> $env:TEMP\xgs-gradle-green.log; "exit=$LASTEXITCODE"; Select-String -Path $env:TEMP\xgs-gradle-green.log -Pattern "FAILED|PASSED|BUILD"
$image = "server\build\sidecar-image"; "image MB: " + [math]::Round((Get-ChildItem $image -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 1) + ", files: " + @(Get-ChildItem $image -Recurse -File).Count
Get-ChildItem "$image\runtime\bin" | Where-Object { $_.Name -match 'vcruntime|msvcp|ortbak' } | Select-Object Name, Length
```

Expected: `exit=0`, 시험 4개 `PASSED`(`theBundledRuntimeRunsTheSidecarAndStopsOnTheExitLine`도, 이것이 줄어든 이미지로 사이드카가 실제로 도는 것을 확인한다), `BUILD SUCCESSFUL`. 이미지 `147.9`MB, 파일 `493`개. `msvcp140.dll`(567,448바이트), `vcruntime140.dll`(98,456), `vcruntime140_1.dll`(38,552)가 보이고 `.ortbak`은 없다.

- [ ] **Step 5: 커밋한다**

```powershell
Set-Location C:\Projects\xGetSongs; git add server/build.gradle.kts server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarImageTest.kt; git commit -m "build(server): 사이드카 이미지에서 ct.sym·jvm.lib를 빼고 VC++ 런타임 DLL을 되살린다

런타임이 *.dll.ortbak으로 이름을 바꿔 둔 vcruntime140·vcruntime140_1·msvcp140을 .dll로 되돌려 Visual C++ 재배포 패키지가 없는 PC에서도 java.exe가 뜬다. 159MB에서 148MB로.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: 셸에 연결한다 (사전 점검, 사이드카 해석 이동, exe 이름)

**Files:**
- Modify: `shell/src-tauri/src/lib.rs`, `shell/src-tauri/Cargo.toml`, `shell/src-tauri/Cargo.lock`, `shell/src-tauri/tauri.conf.json`

**Interfaces:**
- Consumes (Task 2, 4): `SidecarConfig::from_env()`(이제 `resolve`), `webview2::{installed_version, ask_to_install, open_download_page}`.
- Produces (Task 7, 8이 쓴다): 일반 빌드 `shell\src-tauri\target\release\shell.exe`와 점검용 빌드(같은 경로를 덮어쓴다). WebView2가 없으면(점검에서는 `XGS_BRIDGE_CHECK_FILE`과 `XGS_FAKE_NO_WEBVIEW2`가 둘 다 비어 있지 않을 때) 안내 상자를 띄우고 끝낸다.

이 단계에는 단위 시험이 없다(Tauri 없이 시험할 수 있는 것은 Task 1~4에 있다). 연결은 두 빌드와 Task 8의 점검으로 확인한다.

- [ ] **Step 1: 적용한다**

```diff
diff --git a/shell/src-tauri/src/lib.rs b/shell/src-tauri/src/lib.rs
index 98ad123..e0023f3 100644
--- a/shell/src-tauri/src/lib.rs
+++ b/shell/src-tauri/src/lib.rs
@@ -5,7 +5,7 @@ use tauri::ipc::Channel;
 use tauri::{Emitter, Manager, RunEvent, State, WindowEvent};
 use tauri_plugin_dialog::DialogExt;
 use xgs_sidecar::{
-    app_data_dir, folder, job, Bridge, BridgeError, CloseDecision, CloseGate, Lifecycle, SettingsFile, Sidecar, SidecarConfig,
+    app_data_dir, folder, job, webview2, Bridge, BridgeError, CloseDecision, CloseGate, Lifecycle, SettingsFile, Sidecar, SidecarConfig,
     UserSettings,
 };
 
@@ -132,7 +132,7 @@ fn close_window(window: tauri::WebviewWindow) {
 /// Check mode is on when the check script names a file in `XGS_BRIDGE_CHECK_FILE`; a normal run has no such variable and
 /// the two commands below refuse.
 fn check_file() -> Result<std::path::PathBuf, String> {
-    std::env::var_os("XGS_BRIDGE_CHECK_FILE").map(Into::into).ok_or_else(|| "not in check mode".to_string())
+    std::env::var_os("XGS_BRIDGE_CHECK_FILE").filter(|name| !name.is_empty()).map(Into::into).ok_or_else(|| "not in check mode".to_string())
 }
 
 /// The check page (`check.html`) reports what it saw as one `key=value` line of the check file.
@@ -209,12 +209,27 @@ async fn check_burst(count: u32, on_event: Channel<Value>) -> Result<(), String>
     .map_err(|error| error.to_string())?
 }
 
+/// Whether the window cannot open for want of the WebView2 runtime. The check script cannot take the runtime off the PC, so in check
+/// mode `XGS_FAKE_NO_WEBVIEW2` pretends it is not there.
+fn webview2_is_missing() -> bool {
+    let pretend = check_file().is_ok() && std::env::var_os("XGS_FAKE_NO_WEBVIEW2").is_some_and(|value| !value.is_empty());
+    pretend || webview2::installed_version().is_none()
+}
+
 #[cfg_attr(mobile, tauri::mobile_entry_point)]
 pub fn run() {
     // First of all: whatever happens to this process, the sidecar and its children go with it. (Whether this worked is not
     // recorded anywhere yet; the sidecar also ends by itself when its stdin closes.)
     let _ = job::kill_children_when_this_process_ends();
 
+    // Without WebView2 the window would fail to open and the app would end without a word: say what is missing instead.
+    if webview2_is_missing() {
+        if webview2::ask_to_install() {
+            webview2::open_download_page();
+        }
+        return;
+    }
+
     let app = tauri::Builder::default()
         // First of all: a second copy of the app ends here, while the plugins are set up, and brings the window of the first one to the
         // front. Nothing that touches the app data folder may have started by then: the sidecar clears its `work` folder when it starts.
@@ -278,9 +293,10 @@ pub fn run() {
     // Only now: a second copy of the app has ended inside `build` (the single-instance plugin), so what starts here is the one sidecar
     // of the one app. (Started before the `Builder`, as it was, a second copy would start a sidecar too, and the sidecar clears the
     // `work` folder of the app data folder when it starts: the downloads of the first copy would fail.)
-    let config = SidecarConfig::from_env();
     app.manage(Lifecycle::start(move || {
-        let config = config?;
+        // Where the sidecar is, and for an exe that carries its runtime the unpacking of it (the first start only, a few seconds),
+        // are settled here: on the thread of the sidecar, with the window already open and showing that the app is starting.
+        let config = SidecarConfig::from_env()?;
         Sidecar::spawn(config.command(), &config.stderr_log(), SIDECAR_START_LIMIT)
     }));
     app.manage(SettingsFile::new(app_data_dir(|key| std::env::var_os(key)).join("settings.json")));
```

```diff
diff --git a/shell/src-tauri/Cargo.toml b/shell/src-tauri/Cargo.toml
index 7d85f48..b2604b0 100644
--- a/shell/src-tauri/Cargo.toml
+++ b/shell/src-tauri/Cargo.toml
@@ -1,7 +1,7 @@
 [package]
 name = "shell"
 version = "0.1.0"
-description = "A Tauri App"
+description = "xGetSongs: downloads the audio of YouTube videos as m4a files"
 authors = ["you"]
 edition = "2024"
 
```

```diff
diff --git a/shell/src-tauri/tauri.conf.json b/shell/src-tauri/tauri.conf.json
index 0cbe844..eefc7a4 100644
--- a/shell/src-tauri/tauri.conf.json
+++ b/shell/src-tauri/tauri.conf.json
@@ -1,6 +1,6 @@
 {
   "$schema": "https://schema.tauri.app/config/2",
-  "productName": "shell",
+  "productName": "xGetSongs",
   "version": "0.1.0",
   "identifier": "com.xgetsongs.shell",
   "build": {
```

- [ ] **Step 2: 일반 빌드를 만들고 한 벌 따로 둔다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; $env:CARGO_NET_OFFLINE = 'true'; Set-Location C:\Projects\xGetSongs\shell; npm run tauri build -- --no-bundle *> $env:TEMP\xgs-s5-normal-build.log; "exit=$LASTEXITCODE"; Select-String -Path $env:TEMP\xgs-s5-normal-build.log -Pattern "Finished|Built application|error"
New-Item -ItemType Directory $env:TEMP\xgs-s5 -Force | Out-Null; Copy-Item src-tauri\target\release\shell.exe $env:TEMP\xgs-s5\shell-normal.exe -Force
$info = (Get-Item $env:TEMP\xgs-s5\shell-normal.exe).VersionInfo; "ProductName=$($info.ProductName) FileDescription=$($info.FileDescription)"
```

Expected: `exit=0`, `Finished `release` profile`, `Built application at: …\shell.exe`(3~5분, 점검용 빌드 뒤에는 처음부터 다시 컴파일한다), `ProductName=xGetSongs FileDescription=xGetSongs`. `git status`에 셸의 `Cargo.lock`이 바뀌어 보이면 `xgs-sidecar`의 의존성 줄(`flate2`, `sha2`, `winreg`)만 더해진 것이어야 한다(패키지가 새로 늘지 않는다).

- [ ] **Step 3: 점검용 빌드를 만들고 한 벌 따로 둔다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; $env:CARGO_NET_OFFLINE = 'true'; Set-Location C:\Projects\xGetSongs\shell; npm run tauri build -- --no-bundle --config check.conf.json *> $env:TEMP\xgs-s5-check-build.log; "exit=$LASTEXITCODE"; Select-String -Path $env:TEMP\xgs-s5-check-build.log -Pattern "Finished|error"
Copy-Item src-tauri\target\release\shell.exe $env:TEMP\xgs-s5\shell-check.exe -Force
```

Expected: `exit=0`, `Finished`.

- [ ] **Step 4: 4단계의 단일 인스턴스 점검이 그대로 통과한다 (회귀)**

사용자에게 창이 잠깐 뜬다고 알린 뒤:

```powershell
& $env:TEMP\xgs-bridge-check\single-instance.ps1 -ShellExe $env:TEMP\xgs-s5\shell-check.exe 2>&1 | Select-Object -Last 8
```

Expected(4단계 보고서와 같다):

```
first: window True, sidecar up True
first minimized: True
second: exited True after <수백 ms 이하> ms, exit code 0
marker of the first copy still there: True
sidecars now: 1 (one expected)
first still running: True; window restored: True; visible: True; in front: True
first closed: True; sidecars left: 0
```

- [ ] **Step 5: 커밋한다**

(점검용 빌드가 `target\release\shell.exe`를 덮어쓴 상태다. Task 7이 일반 빌드를 다시 만든다.)

```powershell
Set-Location C:\Projects\xGetSongs; git add shell/src-tauri/src/lib.rs shell/src-tauri/Cargo.toml shell/src-tauri/Cargo.lock shell/src-tauri/tauri.conf.json; git commit -m "feat(shell): WebView2 사전 점검, 사이드카 위치 해석을 사이드카 스레드로, exe 이름

WebView2가 없으면 창이 말없이 안 열리는 대신 안내 상자를 띄운다. SidecarConfig::from_env()(exe의 런타임을 푸는 일 포함)를 Lifecycle 스레드에서 해서 창 스레드를 막지 않는다. 점검 변수는 비어 있으면 없는 것으로 본다. productName을 xGetSongs로.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: 한 줄로 묶는 진입점 (`package-exe.bat`)

**Files:**
- Create: `package-exe.ps1`, `package-exe.bat`
- Modify: `.gitignore`, `README.md`

**Interfaces:**
- Consumes (Task 3, 5, 6): `:server:sidecarImage`, `npm run tauri build -- --no-bundle`, `cargo build --release --bin xgs-pack`, `xgs-pack`.
- Produces (Task 8이 쓴다): `dist\xGetSongs.exe`(일반 빌드의 셸 + 이미지).

- [ ] **Step 1: 스크립트와 무시 규칙과 README를 넣는다**

`package-exe.ps1`(ASCII만 쓴다: `.bat`가 Windows PowerShell 5.1로 부른다):

```powershell
# Builds dist\xGetSongs.exe: the shell (Tauri) with the sidecar image (the Kotlin server and the Java runtime it runs on) appended, so that
# one file is all there is to hand out. Needs what building the parts needs: JDK 21 for Gradle, Node.js, Rust. Run it with package-exe.bat.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$shellExe = Join-Path $root 'shell\src-tauri\target\release\shell.exe'
$pack = Join-Path $root 'sidecar-host\target\release\xgs-pack.exe'
$image = Join-Path $root 'server\build\sidecar-image'
$output = Join-Path $root 'dist\xGetSongs.exe'

# Runs a native command and stops at the first one that fails: PowerShell does not do that by itself.
function Step([string]$Title, [scriptblock]$Run) {
    Write-Host "== $Title" -ForegroundColor Cyan
    & $Run
    if ($LASTEXITCODE -ne 0) { throw "$Title failed (exit code $LASTEXITCODE)" }
}

Step 'sidecar image (Gradle)' { & (Join-Path $root 'gradlew.bat') --project-dir $root :server:sidecarImage --no-daemon }
if (-not (Test-Path (Join-Path $root 'shell\node_modules'))) {
    Step 'shell: npm install' { npm --prefix (Join-Path $root 'shell') ci }
}
# The shell is built as the product (not the check build), so a check build left in the same place is replaced.
Step 'shell (Tauri)' { npm --prefix (Join-Path $root 'shell') run tauri -- build --no-bundle }
Step 'xgs-pack (Cargo)' { cargo build --release --bin xgs-pack --manifest-path (Join-Path $root 'sidecar-host\Cargo.toml') }
Step 'pack' { & $pack $shellExe $image $output }

"{0}: {1:N1} MB" -f $output, ((Get-Item $output).Length / 1MB)
```

`package-exe.bat`(CRLF):

```bat
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0package-exe.ps1"
```

```diff
diff --git a/.gitignore b/.gitignore
index ee06d6e..4cbbd40 100644
--- a/.gitignore
+++ b/.gitignore
@@ -7,3 +7,4 @@ local.properties
 out/
 .vscode/
 /log/
+/dist/
```

````diff
diff --git a/README.md b/README.md
index 10c24f4..fefe5c3 100644
--- a/README.md
+++ b/README.md
@@ -71,6 +71,20 @@ LRCLIB에도 없으면 구글 검색 결과의 가사 카드에서 가져옵니
 .\gradlew.bat :engine:integrationTest  # 실제 yt-dlp, YouTube, LRCLIB, 구글 가사 카드를 쓰는 통합 테스트 (도구가 없거나 서비스가 응답하지 않으면 건너뜀)
 ```
 
+## 하나의 exe로 묶기 (Tauri 셸)
+
+```powershell
+.\package-exe.bat
+```
+
+`dist\xGetSongs.exe` 한 파일(약 130MB)이 만들어집니다. Tauri 셸의 exe 뒤에 사이드카 이미지(Kotlin 서버와 그것을 돌리는 Java 런타임)를 붙인 것이라 이 파일만 복사해 가면 됩니다. 만드는 PC에는 JDK 21, Node.js, Rust가 필요하고 받는 PC에는 필요 없습니다.
+
+- 처음 실행하면 런타임을 `%LOCALAPPDATA%\xGetSongs\runtime\<해시>`에 풀기 때문에 사이드카가 준비되기까지 3~4초 걸립니다(창은 바로 열립니다). 다음 실행부터는 그 폴더를 그대로 써서 1초쯤 걸립니다. 이 폴더는 지워도 다시 풀리는 캐시이고, exe를 새 빌드로 바꾸면 옛 폴더는 다음 실행에서 지웁니다.
+- 로그는 `%APPDATA%\xGetSongs\logs`에 남습니다(exe 옆에 폴더를 만들지 않습니다).
+- 창을 여는 데 Microsoft Edge WebView2 런타임이 필요합니다. Windows 11과 업데이트된 Windows 10에는 들어 있고, 없으면 안내 상자가 뜨며 [확인]을 누르면 설치 프로그램을 받는 페이지를 엽니다.
+- 서명하지 않은 파일이라 내려받은 뒤 처음 실행할 때 Windows SmartScreen이 경고할 수 있습니다. 아이콘은 Tauri의 기본 아이콘입니다.
+- yt-dlp, ffmpeg, Node.js(또는 Deno)가 필요한 점은 위 "필요한 것"과 같습니다.
+
 ## 문제가 생겼을 때
 
 앱이 갑자기 사라지거나 멈췄다면 원인이 아래 파일에 남아 있을 수 있습니다.
````

- [ ] **Step 2: 돌려서 exe를 만든다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; $env:CARGO_NET_OFFLINE = 'true'; Set-Location C:\Projects\xGetSongs; & .\package-exe.bat *> $env:TEMP\xgs-s5-package.log; "exit=$LASTEXITCODE"; Get-Content $env:TEMP\xgs-s5-package.log -Tail 5
```

Expected: `exit=0`, 단계 제목 네 개(`== sidecar image (Gradle)`, `== shell (Tauri)`, `== xgs-pack (Cargo)`, `== pack`)가 보이고 끝에 `…\dist\xGetSongs.exe: 493 files, 147.9 MB of image stored as 124.4 MB behind the exe, key <16자리>, 129.3 MB in all`과 `…\dist\xGetSongs.exe: 129.3 MB`. 점검용 빌드 뒤라 셸을 처음부터 다시 컴파일해 3~5분 걸린다.

- [ ] **Step 3: 무시되는지 본다**

```powershell
Set-Location C:\Projects\xGetSongs; git status --short; git check-ignore -v dist/xGetSongs.exe
```

Expected: `git status`에는 이 Task가 바꾼 네 파일(`package-exe.ps1`, `package-exe.bat`, `.gitignore`, `README.md`)만 보이고, `git check-ignore`가 `.gitignore:…:/dist/` 규칙을 알려 준다.

- [ ] **Step 4: 커밋한다**

```powershell
Set-Location C:\Projects\xGetSongs; git add package-exe.ps1 package-exe.bat .gitignore README.md; git commit -m "build: package-exe.bat — dist\xGetSongs.exe 한 파일로 묶는다

사이드카 이미지, 일반 빌드의 셸, xgs-pack을 차례로 만들어 붙인다. README에 한계(첫 실행에 푸는 시간, WebView2, SmartScreen, 아이콘)를 적는다.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 8: 깨끗한 PC 점검, 측정, 보고서

**Files:**
- Create: `%TEMP%\xgs-bridge-check\clean-pc-helpers.ps1`, `clean-pc-check.ps1`, `packed-screen-check.ps1`, `measure-start.ps1`, `mutate.mjs`(저장소 밖), `docs/superpowers/specs/2026-10-15-tauri-packaging-report.md`
- Modify: `docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md`

**Interfaces:**
- Consumes: 모든 앞 Task.
- Produces: 관문 P5~P8의 증거와 보고서.

- [ ] **Step 1: 점검 스크립트를 둔다**

`%TEMP%\xgs-bridge-check\clean-pc-helpers.ps1`(깨끗한 PC 흉내: 가짜 `APPDATA`·`LOCALAPPDATA`·`TEMP`, PATH에는 Windows만, `XGS_SIDECAR_*` 없음):

```powershell
# Dot-sourced after helpers.ps1 and desktop-helpers.ps1 by the checks of the packed exe (stage 5).
#
# A PC that has never seen the app, as far as one PC can pretend: the folders the app writes to (APPDATA, LOCALAPPDATA, TEMP) are empty
# folders of the check, PATH holds only Windows (no Node, Deno, yt-dlp, ffmpeg), and none of the variables that tell the shell where a
# sidecar is (XGS_SIDECAR_*) is set, so the exe has to find everything by itself. What it cannot pretend: the Visual C++ runtime that
# is installed in System32 of this PC (that is why the checks look at the paths the sidecar's DLLs were loaded from).

# A fresh pretend PC. Its folder is named like the folders of the other checks, so that Get-Sidecar (helpers.ps1) finds the sidecar of
# this run and no other: the app data folder below it matches `*--app-data*xgs-bridge-check-*`.
$script:CleanPcs = @()
function New-CleanPc {
    $base = Join-Path $env:TEMP ("xgs-bridge-check-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
    foreach ($name in 'roaming', 'local', 'temp') { New-Item -ItemType Directory "$base\$name" -Force | Out-Null }
    $script:CleanPcs += $base
    [pscustomobject]@{
        Base    = $base
        Roaming = "$base\roaming"
        Local   = "$base\local"
        Temp    = "$base\temp"
        AppData = "$base\roaming\xGetSongs"
        Cache   = "$base\local\xGetSongs\runtime"
        Record  = "$base\check-record.txt"
    }
}

# Stops whatever still runs from a folder of a check (the shell, its sidecar) and deletes the folders of the pretend PCs of this run: each holds
# the runtime the exe unpacked, 148 MB. A check that was stopped half-way leaves them behind; run this once to clear them.
function Remove-CleanPcs {
    foreach ($leftover in @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -like "$env:TEMP\xgs-bridge-check-*" })) {
        Stop-Process -Id $leftover.Id -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 500
    foreach ($base in $script:CleanPcs) {
        try { [IO.Directory]::Delete($base, $true) } catch { "could not delete ${base}: $_" }
    }
    $script:CleanPcs = @()
}

# Starts $Exe on the pretend PC. Start-Process hands over the environment of this process, so the variables are changed for the call and
# put back after it. $Check = $true names the record file (the check page of a check build writes to it; a message box of the shell
# that is about to appear needs it too). $Extra are more variables for this one start.
function Start-CleanApp($pc, [string]$Exe, [bool]$Check = $true, [hashtable]$Extra = @{}) {
    $names = @('APPDATA', 'LOCALAPPDATA', 'TEMP', 'TMP', 'PATH', 'XGS_SIDECAR_LIB', 'XGS_SIDECAR_JAVA', 'XGS_SIDECAR_DIR', 'XGS_SIDECAR_APPDATA',
        'XGS_SIDECAR_LOGDIR', 'XGS_BRIDGE_CHECK_FILE', 'XGS_FAKE_NO_WEBVIEW2') + @($Extra.Keys)
    $saved = @{}
    foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
    try {
        # Remove-Item deletes a variable. [Environment]::SetEnvironmentVariable($name, $null) does not: PowerShell hands the .NET method an
        # empty string, and a variable that is set to nothing is still "set" to the shell (an empty XGS_BRIDGE_CHECK_FILE turned check mode
        # on, and an empty XGS_FAKE_NO_WEBVIEW2 made it show the message box on a PC that has WebView2).
        foreach ($name in $names) { Remove-Item "Env:$name" -ErrorAction SilentlyContinue }
        $env:APPDATA = $pc.Roaming
        $env:LOCALAPPDATA = $pc.Local
        $env:TEMP = $pc.Temp
        $env:TMP = $pc.Temp
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
        if ($Check) { $env:XGS_BRIDGE_CHECK_FILE = $pc.Record }
        foreach ($key in $Extra.Keys) { [Environment]::SetEnvironmentVariable($key, $Extra[$key]) }
        Start-Process -FilePath $Exe -PassThru
    } finally {
        foreach ($name in $names) {
            if ($null -eq $saved[$name]) { Remove-Item "Env:$name" -ErrorAction SilentlyContinue } else { Set-Item "Env:$name" $saved[$name] }
        }
    }
}

# The folder the exe unpacked its runtime to (the one that is complete), or $null.
function Get-RuntimeFolder($pc) {
    if (-not (Test-Path $pc.Cache)) { return $null }
    Get-ChildItem $pc.Cache -Directory | Where-Object { Test-Path "$($_.FullName)\.xgs-complete" } | Select-Object -First 1 -ExpandProperty FullName
}

# Half-made folders (`<key>.partial-<pid>`) in the cache.
function Get-PartialFolders($pc) {
    if (-not (Test-Path $pc.Cache)) { return @() }
    @(Get-ChildItem $pc.Cache -Directory | Where-Object { $_.Name -like '*.partial-*' })
}

# Waits until $Test (a script block) returns something true, at most $Seconds; returns how many ms that took, or $null.
function Wait-ForCondition([scriptblock]$Test, [double]$Seconds = 60) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $Seconds) {
        if (& $Test) { return [int]$watch.ElapsedMilliseconds }
        Start-Sleep -Milliseconds 20
    }
    $null
}

# Where the DLLs of the sidecar's java.exe that matter here were loaded from.
function Get-SidecarModules {
    $sidecar = Get-Sidecar | Select-Object -First 1
    if (-not $sidecar) { return @() }
    (Get-Process -Id $sidecar.ProcessId).Modules | Where-Object { $_.ModuleName -match '^(vcruntime140|vcruntime140_1|msvcp140|ucrtbase|jvm)\.dll$' } |
        ForEach-Object { [pscustomobject]@{ Name = $_.ModuleName; Path = $_.FileName } }
}

# Prints one line "ok"/"FAIL" and remembers a failure in $script:ok (declare `$ok = $true` before the checks).
function Check([string]$What, $Actual, $Wanted) {
    $good = "$Actual" -eq "$Wanted"
    if (-not $good) { $script:ok = $false }
    "{0,-4} {1}: {2}{3}" -f $(if ($good) { "ok" } else { "FAIL" }), $What, $Actual, $(if ($good) { "" } else { "   (wanted $Wanted)" })
}
```

`clean-pc-check.ps1`(시나리오 7개):

```powershell
# The packed exe on a pretend clean PC (see clean-pc-helpers.ps1): the check build of the shell with the sidecar image appended by xgs-pack.
#   1. first start, from a folder with spaces and Korean: unpacks its runtime, starts the sidecar from it, the DLLs of the Visual C++ runtime
#      come from the unpacked copy, the logs go to the app data folder and not next to the exe
#   2. second start: uses the unpacked copy (nothing is unpacked again)
#   3. a start that is killed in the middle of unpacking leaves only a half-made folder, and the next start makes a complete one
#   4. an exe of another build removes the runtime of the first one
#   5. started a second time while the first copy is still unpacking: the second ends at once, one folder, one sidecar
#   6. WebView2 "missing": a message box says so, and nothing is unpacked or started
#   7. a runtime that cannot be unpacked (the folder is not writable): the page is told in words, the window stays open
#
# -ShellExe is the check build of the shell as cargo made it, -ImageDir the sidecar image, -Pack xgs-pack.exe, -PackedCheckExe what
# `xgs-pack ShellExe ImageDir PackedCheckExe` made of them.
param(
    [Parameter(Mandatory)][string]$ShellExe,
    [Parameter(Mandatory)][string]$ImageDir,
    [Parameter(Mandatory)][string]$Pack,
    [Parameter(Mandatory)][string]$PackedCheckExe,
    [string]$Helpers = "$env:TEMP\xgs-bridge-check\helpers.ps1",
    [string]$DesktopHelpers = "$env:TEMP\xgs-bridge-check\desktop-helpers.ps1",
    [string]$CleanHelpers = "$env:TEMP\xgs-bridge-check\clean-pc-helpers.ps1",
    [switch]$Keep
)
. $Helpers
. $DesktopHelpers
. $CleanHelpers
$Image = $ImageDir
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32Controls {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
}
"@
$uia = [System.Windows.Automation.AutomationElement]
$scope = [System.Windows.Automation.TreeScope]
$ok = $true

# The message box of the process (class #32770), or $null after $Seconds.
function Find-MessageBox($process, [double]$Seconds = 15) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    do {
        $windows = $uia::RootElement.FindAll($scope::Children, (New-Object System.Windows.Automation.PropertyCondition($uia::ProcessIdProperty, $process.Id)))
        $box = $windows | Where-Object { $_.Current.ClassName -eq '#32770' } | Select-Object -First 1
        if ($box) { return $box }
        Start-Sleep -Milliseconds 50
    } while ($watch.Elapsed.TotalSeconds -lt $Seconds)
    $null
}
function Get-ByAutomationId($parent, [string]$Id) {
    $parent.FindFirst($scope::Descendants, (New-Object System.Windows.Automation.PropertyCondition($uia::AutomationIdProperty, $Id)))
}
# The buttons of a message box are plain Win32 buttons: click them by their window handle (BM_CLICK). 1 = OK, 2 = Cancel.
function Press($button) { [void][Win32Controls]::PostMessage([IntPtr]$button.Current.NativeWindowHandle, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) }
function Count-Files([string]$Folder) { @(Get-ChildItem $Folder -Recurse -File -Force).Count }

try {
    "=== 1. first start on a pretend clean PC"
    $pc = New-CleanPc
    $home1 = "$($pc.Base)\내 앱 폴더"
    New-Item -ItemType Directory $home1 | Out-Null
    $exe1 = "$home1\xGetSongs.exe"
    Copy-Item $PackedCheckExe $exe1
    "exe: {0:N1} MB" -f ((Get-Item $exe1).Length / 1MB)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-CleanApp $pc $exe1
    $windowMs = Wait-CheckWindow $process 30
    $unpackedMs = Wait-ForCondition { Get-RuntimeFolder $pc } 120
    $record = Wait-CheckRecord $pc 120
    $readyMs = [int]$watch.ElapsedMilliseconds
    "window after $windowMs ms, runtime unpacked after $unpackedMs ms, page done after $readyMs ms (page waited $($record['waitedMs']) ms for the sidecar)"
    Check "the page finished without an error" ($record.Contains('done') -and -not $record.Contains('error')) $true
    $runtime = Get-RuntimeFolder $pc
    Check "the runtime was unpacked below LOCALAPPDATA" ([bool]$runtime -and $runtime.StartsWith($pc.Cache)) $true
    Check "every file of the image is there (and the marker)" (Count-Files $runtime) ((Count-Files $Image) + 1)
    $sidecar = Get-Sidecar | Select-Object -First 1
    Check "the sidecar runs the java.exe of the unpacked copy" $sidecar.ExecutablePath "$runtime\runtime\bin\java.exe"
    $modules = @(Get-SidecarModules)
    $modules | ForEach-Object { "     module {0,-18} {1}" -f $_.Name, $_.Path }
    $visualC = @($modules | Where-Object { $_.Name -match '^(vcruntime140|vcruntime140_1|msvcp140)\.dll$' })
    Check "the Visual C++ runtime was loaded" ($visualC.Count -ge 1) $true
    Check "all of it comes from the folder of the unpacked java.exe" (@($visualC | Where-Object { $_.Path -ne "$runtime\runtime\bin\$($_.Name)" }).Count) 0
    Check "the app data folder was made below APPDATA" (Test-Path "$($pc.AppData)\sidecar-stderr.log") $true
    Check "the sidecar wrote its log to the app data folder" (Test-Path "$($pc.AppData)\logs\xgetsongs.log") $true
    Check "nothing was made next to the exe" (@(Get-ChildItem $home1 -Force | Where-Object { $_.Name -ne 'xGetSongs.exe' }).Count) 0
    $closedMs = Close-CheckShell $process
    Check "closing took (ms, null = it hung)" ($null -ne $closedMs) $true
    "closed after $closedMs ms"
    Check "no half-made folder is left" (Get-PartialFolders $pc).Count 0
    $key1 = (Get-Item $runtime).Name

    "=== 2. second start: the unpacked copy is used"
    Remove-Item $pc.Record
    $stamp = (Get-Item "$runtime\.xgs-complete").LastWriteTimeUtc
    $watch.Restart()
    $process = Start-CleanApp $pc $exe1
    $windowMs = Wait-CheckWindow $process 30
    $record = Wait-CheckRecord $pc 120
    "window after $windowMs ms, page done after $([int]$watch.ElapsedMilliseconds) ms (page waited $($record['waitedMs']) ms for the sidecar)"
    Check "the page finished without an error" ($record.Contains('done') -and -not $record.Contains('error')) $true
    Check "the same folder, not unpacked again" ((Get-Item "$runtime\.xgs-complete").LastWriteTimeUtc -eq $stamp) $true
    Check "still one folder in the cache" @(Get-ChildItem $pc.Cache -Directory).Count 1
    [void](Close-CheckShell $process)

    "=== 3. killed in the middle of unpacking"
    $pc3 = New-CleanPc
    $process = Start-CleanApp $pc3 $exe1
    $sawPartial = Wait-ForCondition { (Get-PartialFolders $pc3).Count -gt 0 } 30
    Check "a half-made folder appeared" ($null -ne $sawPartial) $true
    Stop-Process -Id $process.Id -Force
    [void]$process.WaitForExit(10000)
    Start-Sleep -Milliseconds 300
    Check "only the half-made folder is left" "$(@(Get-PartialFolders $pc3).Count) partial, complete: $([bool](Get-RuntimeFolder $pc3))" "1 partial, complete: False"
    Remove-Item $pc3.Record -ErrorAction SilentlyContinue
    $process = Start-CleanApp $pc3 $exe1
    $record = Wait-CheckRecord $pc3 120
    Check "the next start went through" ($record.Contains('done') -and -not $record.Contains('error')) $true
    Check "it made a complete folder" ([bool](Get-RuntimeFolder $pc3)) $true
    Check "and removed the half-made one" (Get-PartialFolders $pc3).Count 0
    [void](Close-CheckShell $process)

    "=== 4. an exe of another build removes the runtime of the first"
    $image2 = "$($pc.Base)\image2"
    robocopy $Image $image2 /E /NFL /NDL /NJH /NJS /NP | Out-Null
    Set-Content "$image2\lib\build-2.txt" "another build"
    $exe2 = "$home1\xGetSongs-2.exe"
    & $Pack $ShellExe $image2 $exe2 | Out-Host
    Remove-Item $pc.Record
    $process = Start-CleanApp $pc $exe2
    $record = Wait-CheckRecord $pc 120
    Check "the other build started" ($record.Contains('done') -and -not $record.Contains('error')) $true
    [void](Close-CheckShell $process)
    $folders = @(Get-ChildItem $pc.Cache -Directory)
    Check "one folder is left" $folders.Count 1
    Check "and it is not the first one" ($folders[0].Name -ne $key1) $true

    "=== 5. started a second time while the first one is still unpacking"
    $pc6 = New-CleanPc
    $first = Start-CleanApp $pc6 $exe1
    Check "the first copy began to unpack" ($null -ne (Wait-ForCondition { (Get-PartialFolders $pc6).Count -gt 0 } 30)) $true
    $second = Start-CleanApp $pc6 $exe1
    $secondMs = Wait-Exit $second 20
    Check "the second copy ended by itself (ms, null = it did not)" ($null -ne $secondMs) $true
    "second copy ended after $secondMs ms"
    $record = Wait-CheckRecord $pc6 120
    Check "the first copy went through" ($record.Contains('done') -and -not $record.Contains('error')) $true
    Check "one complete runtime folder and no half-made one" "$(@(Get-ChildItem $pc6.Cache -Directory).Count) folder, complete: $([bool](Get-RuntimeFolder $pc6)), $((Get-PartialFolders $pc6).Count) partial" "1 folder, complete: True, 0 partial"
    Check "one sidecar" @(Get-Sidecar).Count 1
    [void](Close-CheckShell $first)

    "=== 6. WebView2 missing"
    $pc5 = New-CleanPc
    $watch.Restart()
    $process = Start-CleanApp $pc5 $exe1 $true @{ XGS_FAKE_NO_WEBVIEW2 = '1' }
    $box = Find-MessageBox $process
    Check "a message box opened" ([bool]$box) $true
    "box after $([int]$watch.ElapsedMilliseconds) ms: title '$($box.Current.Name)'"
    $texts = @($box.FindAll($scope::Descendants, [System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object { $_.Current.Name }) -join ' | '
    Check "it names WebView2" ($texts -like '*WebView2*') $true
    Check "it says what to do" ($texts -like '*[[]확인[]]*') $true
    Check "the window of the app was not opened" ((Get-AppWindow $process) -eq [IntPtr]::Zero) $true
    Check "nothing was unpacked yet" (Test-Path $pc5.Cache) $false
    Press (Get-ByAutomationId $box "2")
    Check "the shell ended after Cancel (ms, null = it hung)" ($null -ne (Wait-Exit $process 10)) $true
    Check "no sidecar was started" @(Get-Sidecar).Count 0

    "=== 7. the runtime cannot be unpacked"
    $pc7 = New-CleanPc
    [IO.Directory]::Delete($pc7.Local, $true)
    Set-Content $pc7.Local "not a folder"   # LOCALAPPDATA is a file: nothing can be made below it
    $process = Start-CleanApp $pc7 $exe1
    $record = Wait-CheckRecord $pc7 90
    "the page was told: $($record['error'])"
    Check "the page was told in words what failed" ($record['error'] -like '*런타임을 풀지 못했습니다*') $true
    Check "no sidecar was started" @(Get-Sidecar).Count 0
    Check "the window is still open" (-not $process.HasExited) $true
    [void](Close-CheckShell $process)
} finally {
    Check "no sidecar of this check is left running" @(Get-Sidecar).Count 0
    # Stops what is left (by path: the user's own apps are none of this check's business) and deletes the folders of the pretend PCs.
    if (-not $Keep) { Remove-CleanPcs }
}
"=== " + $(if ($ok) { "ALL OK" } else { "FAILED" })
```

`packed-screen-check.ps1`(점검 페이지가 없는 실제 exe의 화면):

```powershell
# The real exe (the product build with the sidecar image appended: dist\xGetSongs.exe) on a pretend clean PC (see clean-pc-helpers.ps1), with
# no check page: the window opens at once, the runtime is unpacked, the sidecar starts from the unpacked copy, and the screen shows the tools
# card the sidecar answered (on a PC with no yt-dlp, ffmpeg or Node.js all three are missing, which is what the card must say).
# The screen is read through UI Automation (the accessibility tree of WebView2 wakes up when a client asks for it).
param(
    [Parameter(Mandatory)][string]$Exe,
    [string]$Helpers = "$env:TEMP\xgs-bridge-check\helpers.ps1",
    [string]$DesktopHelpers = "$env:TEMP\xgs-bridge-check\desktop-helpers.ps1",
    [string]$CleanHelpers = "$env:TEMP\xgs-bridge-check\clean-pc-helpers.ps1",
    [switch]$Keep
)
. $Helpers
. $DesktopHelpers
. $CleanHelpers
Add-Type -AssemblyName UIAutomationClient
$uia = [System.Windows.Automation.AutomationElement]
$scope = [System.Windows.Automation.TreeScope]
$ok = $true

$pc = New-CleanPc
$process = Start-CleanApp $pc $Exe $false
try {
    $windowMs = Wait-CheckWindow $process 30
    "window after $windowMs ms"
    Check "the window opened at once (under 3 s)" ($windowMs -lt 3000) $true
    Check "the runtime was unpacked" ($null -ne (Wait-ForCondition { Get-RuntimeFolder $pc } 120)) $true
    Check "the sidecar was started" ($null -ne (Wait-ForCondition { Get-Sidecar } 60)) $true
    $runtime = Get-RuntimeFolder $pc
    Check "from the unpacked java.exe" (Get-Sidecar | Select-Object -First 1).ExecutablePath "$runtime\runtime\bin\java.exe"

    $main = $null
    for ($i = 0; $i -lt 40 -and -not $main; $i++) {
        $main = $uia::RootElement.FindAll($scope::Children, (New-Object System.Windows.Automation.PropertyCondition($uia::ProcessIdProperty, $process.Id))) |
            Where-Object { $_.Current.ClassName -eq 'Tauri Window' } | Select-Object -First 1
        Start-Sleep -Milliseconds 250
    }
    Check "the title of the window" $main.Current.Name "xGetSongs"
    # The tools card appears when the sidecar answered: wait for it.
    $names = @()
    $deadline = (Get-Date).AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 500
        $names = @($main.FindAll($scope::Descendants, [System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object { $_.Current.Name } | Where-Object { $_ })
    } while ((Get-Date) -lt $deadline -and -not ($names -contains 'yt-dlp ✗'))
    $names | Select-Object -Unique | ForEach-Object { "     screen: $_" }
    Check "the tools card says yt-dlp is missing" ($names -contains 'yt-dlp ✗') $true
    Check "the tools card says ffmpeg is missing" ($names -contains 'ffmpeg ✗') $true
    Check "the tools card says the JS runtime is missing" ($names -contains 'JS 런타임 ✗') $true
    Check "the input box is there" ($names -contains '재생목록 ID 또는 영상 주소') $true
    Check "no message box of the shell" (@($uia::RootElement.FindAll($scope::Children, (New-Object System.Windows.Automation.PropertyCondition($uia::ProcessIdProperty, $process.Id))) | Where-Object { $_.Current.ClassName -eq '#32770' }).Count) 0
    $closedMs = Close-CheckShell $process
    Check "closing took (ms, null = it hung)" ($null -ne $closedMs) $true
    "closed after $closedMs ms"
    Check "no sidecar left" @(Get-Sidecar).Count 0
} finally {
    if (-not $Keep) { Remove-CleanPcs }
}
"=== " + $(if ($ok) { "ALL OK" } else { "FAILED" })
```

`measure-start.ps1`(첫 실행과 두 번째 실행의 시간):

```powershell
# How long the packed check exe takes to be ready on a pretend clean PC: the first start (it unpacks its runtime) and the second one (it uses
# the unpacked copy), $Runs times each. `waitedMs` is what the check page measured: how long it waited for the sidecar after it loaded.
# The rule of the earlier design (§6.4 of the single-exe design) for a second start is that the window is up in 5 s or less; the window of
# this exe opens before anything is unpacked, so the number that matters is how long the page waits for the sidecar.
param(
    [Parameter(Mandatory)][string]$PackedCheckExe,
    [int]$Runs = 4,
    [string]$Helpers = "$env:TEMP\xgs-bridge-check\helpers.ps1",
    [string]$DesktopHelpers = "$env:TEMP\xgs-bridge-check\desktop-helpers.ps1",
    [string]$CleanHelpers = "$env:TEMP\xgs-bridge-check\clean-pc-helpers.ps1",
    [switch]$Keep
)
. $Helpers
. $DesktopHelpers
. $CleanHelpers

$rows = foreach ($i in 1..$Runs) {
    $pc = New-CleanPc
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-CleanApp $pc $PackedCheckExe
    $unpacked = Wait-ForCondition { Get-RuntimeFolder $pc } 120
    $first = Wait-CheckRecord $pc 120
    $firstDone = [int]$watch.ElapsedMilliseconds
    [void](Close-CheckShell $process)
    [IO.File]::Delete($pc.Record)
    $watch.Restart()
    $process = Start-CleanApp $pc $PackedCheckExe
    $second = Wait-CheckRecord $pc 120
    $secondDone = [int]$watch.ElapsedMilliseconds
    [void](Close-CheckShell $process)
    [pscustomobject]@{ Run = $i; UnpackedMs = $unpacked; FirstWaitedMs = $first['waitedMs']; FirstDoneMs = $firstDone; SecondWaitedMs = $second['waitedMs']; SecondDoneMs = $secondDone }
}
$rows | Format-Table -AutoSize | Out-String
$folder = Get-RuntimeFolder $pc
"the unpacked runtime: {0:N1} MB in {1} files" -f ((Get-ChildItem $folder -Recurse -File | Measure-Object Length -Sum).Sum / 1MB), @(Get-ChildItem $folder -Recurse -File).Count
if (-not $Keep) { Remove-CleanPcs }
$median = { param($values) $sorted = @($values | Sort-Object); $sorted[[int][math]::Floor(($sorted.Count - 1) / 2)] }
"median: unpacking {0} ms, first start waited {1} ms, second start waited {2} ms" -f (& $median $rows.UnpackedMs), (& $median $rows.FirstWaitedMs), (& $median $rows.SecondWaitedMs)
```

`mutate.mjs`(Rust 시험이 일부러 넣은 결함을 잡는지):

```javascript
// Mutation check of the stage-5 Rust tests: each mutation breaks one rule of the code, and the named test has to fail.
//   node mutate.mjs <sidecar-host folder>
import { execFileSync } from "node:child_process";
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const root = process.argv[2];
const only = process.argv[3];
const mutations = [
  {
    file: "src/payload.rs",
    name: "paths are not checked for leaving the folder",
    from: "    if name.is_empty() || name.contains('\\\\') || name.contains(':') || name.contains('\\0') {",
    to: "    if false {",
    expect: "paths_that_could_leave_the_folder_are_refused",
  },
  {
    file: "src/payload.rs",
    name: "a folder without the marker counts as complete",
    from: "    folder.join(COMPLETE_MARKER).is_file()",
    to: "    folder.is_dir()",
    expect: "a_folder_without_the_complete_marker_is_unpacked_again",
  },
  {
    file: "src/payload.rs",
    name: "the runtimes of other builds are kept",
    from: "            if other != name && is_cache_name(&other) {",
    to: "            if false && other != name && is_cache_name(&other) {",
    expect: "the_runtimes_of_other_builds_are_removed_and_other_folders_are_left_alone",
  },
  {
    file: "src/payload.rs",
    name: "any folder of the cache is removed, not only the ones the cache made",
    from: "            if other != name && is_cache_name(&other) {",
    to: "            if other != name {",
    expect: "the_runtimes_of_other_builds_are_removed_and_other_folders_are_left_alone",
  },
  {
    file: "src/payload.rs",
    name: "an exe that carries a runtime can be packed again",
    from: "    if read_footer(&mut File::open(exe)?)?.is_some() {",
    to: "    if false {",
    expect: "an_exe_that_carries_a_runtime_is_not_packed_again",
  },
  {
    file: "src/payload.rs",
    name: "the size of an unpacked file is not checked",
    from: "        if written != original {",
    to: "        if false {",
    expect: "a_file_whose_size_is_not_what_the_entry_says_is_an_error",
  },
  {
    file: "src/payload.rs",
    name: "a half-made folder is left behind when unpacking fails",
    from: "            remove_folder(&partial);\n            return Err(describe(\"풀지 못했습니다\", error));",
    to: "            return Err(describe(\"풀지 못했습니다\", error));",
    expect: "an_archive_that_ends_inside_a_file_is_an_error_and_leaves_nothing_in_the_cache",
  },
  {
    file: "src/webview2.rs",
    name: "the leftover of an uninstalled runtime counts as a runtime",
    from: "    !pv.is_empty() && pv != \"0.0.0.0\"",
    to: "    !pv.is_empty()",
    expect: "the_leftover_of_an_uninstalled_runtime_is_no_runtime",
  },
  {
    file: "src/config.rs",
    name: "the exe's own runtime wins over the development variables",
    from: "            Some(exe) if get(\"XGS_SIDECAR_LIB\").is_none() && get(\"XGS_SIDECAR_DIR\").is_none() => {",
    to: "            Some(exe) => {",
    expect: "the_variables_for_development_win_over_what_the_exe_carries_and_unpack_nothing",
  },
  {
    file: "src/config.rs",
    name: "empty variables count as set",
    from: "        let get = |key: &str| get(key).filter(|value| !value.is_empty());",
    to: "        let get = |key: &str| get(key);",
    expect: "variables_that_are_empty_count_as_not_set",
  },
  {
    file: "src/config.rs",
    name: "the log folder next to the exe is used for an exe that carries a runtime",
    from: "|key| if key == \"XGS_SIDECAR_DIR\" { Some(image.clone()) } else { get(key) },",
    to: "|key| if key == \"XGS_SIDECAR_DIR\" { Some(image.clone()) } else if key == \"XGS_SIDECAR_LOGDIR\" { Some(OsString::from(\"C:\\\\wrong\")) } else { get(key) },",
    expect: "an_exe_that_carries_a_sidecar_starts_it_from_a_copy_in_the_app_data_folder",
  },
  {
    file: "src/bin/xgs-pack.rs",
    name: "the output may be the exe itself",
    from: "    if fs::canonicalize(exe).ok().is_some_and(|exe| Some(exe) == fs::canonicalize(output).ok()) {",
    to: "    if false {",
    expect: "packing_an_exe_over_itself_is_refused_and_leaves_it_alone",
    test: ["test", "--offline", "--test", "pack_cli"],
  },
];

let survived = 0;
for (const mutation of mutations) {
  if (only && !mutation.name.includes(only)) continue;
  const path = join(root, mutation.file);
  const original = readFileSync(path, "utf8");
  if (!original.includes(mutation.from)) {
    console.log(`SKIP   ${mutation.name}: the text to mutate is not in ${mutation.file}`);
    survived += 1;
    continue;
  }
  writeFileSync(path, original.replace(mutation.from, mutation.to), "utf8");
  let output = "";
  try {
    output = execFileSync("cargo", mutation.test ?? ["test", "--offline"], { cwd: root, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], maxBuffer: 1 << 26 });
  } catch (error) {
    output = `${error.stdout ?? ""}${error.stderr ?? ""}`;
  } finally {
    writeFileSync(path, original, "utf8");
  }
  const failed = new RegExp(`${mutation.expect} \\.\\.\\. FAILED`).test(output);
  if (!failed) survived += 1;
  console.log(`${failed ? "caught " : "SURVIVED"} ${mutation.name} (${mutation.expect})`);
}
console.log(survived === 0 ? "all mutations were caught" : `${survived} mutation(s) not caught`);
process.exit(survived === 0 ? 0 : 1);
```

- [ ] **Step 2: 점검용 빌드를 묶는다**

```powershell
$pack = "C:\Projects\xGetSongs\sidecar-host\target\release\xgs-pack.exe"; $image = "C:\Projects\xGetSongs\server\build\sidecar-image"
& $pack $env:TEMP\xgs-s5\shell-check.exe $image $env:TEMP\xgs-s5\packed-check.exe
```

Expected: `…\packed-check.exe: 493 files, 147.9 MB of image stored as 124.4 MB behind the exe, key <16자리>, 129.3 MB in all`.

- [ ] **Step 3: 깨끗한 PC 점검 (P5)**

사용자에게 창과 안내 상자가 잠깐 뜬다고 알린 뒤:

```powershell
$pack = "C:\Projects\xGetSongs\sidecar-host\target\release\xgs-pack.exe"; $image = "C:\Projects\xGetSongs\server\build\sidecar-image"
& $env:TEMP\xgs-bridge-check\clean-pc-check.ps1 -ShellExe $env:TEMP\xgs-s5\shell-check.exe -ImageDir $image -Pack $pack -PackedCheckExe $env:TEMP\xgs-s5\packed-check.exe 2>&1 | Tee-Object $env:TEMP\xgs-s5\clean-pc-run.txt
```

Expected: 모든 줄이 `ok`이고 끝이 `=== ALL OK`이다. 계획을 쓸 때의 값(자리: 이 PC): 첫 실행에서 창 약 50ms, 풀기 1.5~2.3초(`unpacked after`), 페이지가 사이드카를 기다린 시간 3~5초, 사이드카 `java.exe`가 풀어 둔 폴더의 것, `vcruntime140.dll`·`vcruntime140_1.dll`·`msvcp140.dll`이 그 폴더의 `runtime\bin\`에서 읽힘(`ucrtbase.dll`은 `C:\WINDOWS\System32`의 Windows 것이다), 파일 수 `494`(이미지 493 + 표식), 두 번째 실행은 표식의 시각이 그대로, 풀다 죽이면 `1 partial, complete: False`, 다른 빌드는 폴더 하나만 남김, 풀 때 한 번 더 실행하면 두 번째가 약 30ms에 끝남, WebView2 상자는 약 200ms에 뜸, 풀 수 없을 때 `서버를 시작하지 못했습니다: xGetSongs의 실행 파일에 든 런타임을 풀지 못했습니다: …`. 하나라도 `FAIL`이면 원인을 찾는다(`systematic-debugging`): 제품을 고치기 전에 점검 스크립트의 오류부터 의심한다(4단계의 교훈).

- [ ] **Step 4: 실제 exe의 화면 (P6)**

```powershell
& $env:TEMP\xgs-bridge-check\packed-screen-check.ps1 -Exe C:\Projects\xGetSongs\dist\xGetSongs.exe 2>&1 | Tee-Object $env:TEMP\xgs-s5\screen-run.txt
```

Expected: 모든 줄이 `ok`, 화면 목록에 `외부 도구`, `yt-dlp ✗`, `ffmpeg ✗`, `JS 런타임 ✗`, `yt-dlp 설치`, `재생목록 ID 또는 영상 주소`, `조회`가 보이고 `=== ALL OK`.

- [ ] **Step 5: 시간 (P7)**

```powershell
& $env:TEMP\xgs-bridge-check\measure-start.ps1 -PackedCheckExe $env:TEMP\xgs-s5\packed-check.exe -Runs 5 2>&1 | Tee-Object $env:TEMP\xgs-s5\measure-run.txt
```

Expected(계획을 쓸 때, 조용한 PC): 푸는 데 중앙값 약 1.5~1.7초, 첫 실행에서 페이지가 사이드카를 기다린 시간 중앙값 약 3초, 두 번째 약 1.1초. **관문은 두 번째 5초 이하, 첫 실행 10초 이하**다. 다른 일이 CPU를 쓰는 중이면(빌드 등) 첫 실행이 7초까지 나온 적이 있으므로 조용할 때 다시 잰다.

- [ ] **Step 6: 변이 시험 (P8)**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; node $env:TEMP\xgs-bridge-check\mutate.mjs C:\Projects\xGetSongs\sidecar-host
```

Expected: 열두 줄이 모두 `caught`이고 끝이 `all mutations were caught`(몇 분 걸린다: 변이마다 시험을 돌린다. 끝나면 파일이 원래대로 돌아와 있다: `git status`가 깨끗해야 한다. **첫 변이(경로 검사 제거)는 시험이 실패하는 동안 `C:/drive.txt`를 실제로 풀어 `sidecar-host\drive.txt`를 남긴다**(드라이브 상대 경로). 보이면 `[IO.File]::Delete`로 지운다. 실행했을 때 실제로 생겼다).

- [ ] **Step 7: 전체 회귀 (P8)**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --offline 2>&1 | Select-String "^test result|FAILED"; cargo clippy --offline --all-targets 2>&1 | Select-String "^(warning|error)" | ForEach-Object { $_.Line }
Set-Location C:\Projects\xGetSongs\shell\src-tauri; cargo clippy --offline 2>&1 | Select-String "^(warning|error)" | ForEach-Object { $_.Line }
Set-Location C:\Projects\xGetSongs\shell; node --test "tests/*.test.ts" 2>&1 | Select-Object -Last 8; npx tsc --noEmit; "tsc exit=$LASTEXITCODE"
```

Expected: `lib` 152개와 `pack_cli` 5개 통과(`#[ignore]` 둘 제외), clippy는 `job.rs:11` 하나(셸의 clippy는 경고 없음), TS `tests 242`/`pass 242`, `tsc exit=0`.

- [ ] **Step 8: 점검이 남긴 것을 정리한다**

```powershell
"left: " + @(Get-ChildItem $env:TEMP -Directory | Where-Object { $_.Name -match '^xgs-bridge-check-' }).Count + " check folders, " + @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -like "$env:TEMP\xgs-bridge-check-*" }).Count + " processes"
```

Expected: `left: 0 check folders, 0 processes`(스크립트가 스스로 지운다. 남았으면 `. $env:TEMP\xgs-bridge-check\clean-pc-helpers.ps1; Remove-CleanPcs`는 이번 실행의 것만 지우므로 남은 `xgs-bridge-check-*` 폴더는 `[IO.Directory]::Delete`로 직접 지운다).

- [ ] **Step 9: 보고서와 설계 문서를 쓴다**

`docs/superpowers/specs/2026-10-15-tauri-packaging-report.md`를 아래 틀로 쓴다. **자리에는 이 실행의 실제 값을 넣는다**(위 Expected의 숫자가 아니라 출력된 것).

```markdown
# 5단계 "포장과 단일 exe" 종단 확인 결과

계획: [2026-10-15-tauri-packaging.md](../plans/2026-10-15-tauri-packaging.md). 설계: [§5 5단계](2026-10-10-tauri-kotlin-sidecar-design.md).

## 결과

| 관문 | 결과 | 증거 |
|---|---|---|
| P1 … P9 | 통과/실패 | 각 줄에 이 실행의 숫자 |

## 방식과 이유
(SFX·warp-packer 대신 오버레이: 측정한 숫자, 이 PC에서의 7-Zip 풀기 시간)

## 크기
(exe, 이미지, 압축, 풀어 둔 폴더, 줄인 것: ct.sym·jvm.lib, JavaFX가 차지하는 몫)

## 시간
(푸는 시간, 첫 실행과 두 번째 실행의 waitedMs 표)

## 한계와 남은 일
- 서명하지 않음(SmartScreen), 아이콘은 Tauri 기본, 첫 실행에 풀어서 3초 안팎 더 걸림
- WebView2 안내 상자의 `확인` 경로(브라우저 열기)는 실행하지 않았다
- 이 PC에는 VC++ 재배포 패키지가 System32에 있어 "없는" PC를 흉내만 냈다: 사이드카가 DLL을 읽은 경로로 대신 확인했다
- Windows 10 실제 PC, 다른 백신, 읽기 전용 위치의 exe는 시험하지 못했다
- `jlink`(Temurin 21 JDK를 받아야 함), JavaFX 제거(구글 가사, 7단계), 서명·아이콘
- 사용자의 실제 PC에서 `dist\xGetSongs.exe`를 한 번 실행해 보는 것(Compose 앱을 끄고)

## 최종 리뷰
(Critical·Important와 처리)
```

설계 문서에는 5단계 행, 1단계 행의 `jlink` 문구, 위험 둘을 갱신한다:

```diff
diff --git a/docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md b/docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md
index a0ea6a8..30cc484 100644
--- a/docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md
+++ b/docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md
@@ -72,11 +72,11 @@
 |---|---|---|
 | 0a | **UI 프로토타입 (먼저):** 사이드카 없이 Tauri 웹뷰에 실제 화면을 가짜 데이터로 그려서 Compose와 나란히 비교한다. Rust 설치와 `shell/` 스캐폴드만 있으면 된다 | UI 관문 U1~U4 판정(§5.2) ([계획서](../plans/2026-10-10-tauri-ui-prototype.md)) |
 | 0b | **스파이크:** 사이드카 진입점 + 최소 Tauri 셸. 시작·종료·정리·크기를 잰다 | 보고서 + **기술 관문 판정** ([계획서](../plans/2026-10-10-tauri-sidecar-spike.md)) |
-| 1 | 사이드카 완성: `Diagnostics`(로그, 종료 기록, `last-run.txt`)를 `app`에서 옮기고 파일 로그를 쓴다. 번들한 JRE로 실행한다 | 사이드카만으로 로그·종료 기록이 지금과 같다 ([계획서](../plans/2026-10-10-tauri-sidecar-complete.md), [보고서](2026-10-10-tauri-sidecar-complete-report.md): 통과). 런타임은 JDK 복사본이고 `jlink`로 줄이는 것은 5단계(`jmods`가 있는 JDK가 필요) |
+| 1 | 사이드카 완성: `Diagnostics`(로그, 종료 기록, `last-run.txt`)를 `app`에서 옮기고 파일 로그를 쓴다. 번들한 JRE로 실행한다 | 사이드카만으로 로그·종료 기록이 지금과 같다 ([계획서](../plans/2026-10-10-tauri-sidecar-complete.md), [보고서](2026-10-10-tauri-sidecar-complete-report.md): 통과). 런타임은 JDK 복사본이다. `jlink`로 줄이는 것은 5단계에서도 하지 않았다(`jmods`가 있는 JDK를 받아야 한다. [5단계 보고서](2026-10-15-tauri-packaging-report.md)의 "남은 일") |
 | 2 | Rust 브리지: 명령 8개와 SSE → 이벤트. 토큰을 Rust 안에 숨긴다 | 브리지 계약 테스트 ([계획서](../plans/2026-10-11-tauri-rust-bridge.md), [보고서](2026-10-11-tauri-rust-bridge-report.md): 통과). 서버 변경 없음 |
 | 3 | 프런트엔드 이식: `AppStateHolder` 논리와 `Labels`, 화면. `shared` 로직 재사용 방식 확정 | 지금의 `AppStateHolderTest` 시나리오가 새 테스트에서 통과 ([계획서](../plans/2026-10-11-tauri-frontend-port.md), [보고서](2026-10-11-tauri-frontend-port-report.md): 통과). 재사용 방식은 TS 포팅과 같은 케이스 표 테스트이고 Kotlin 구현과의 대조 도구가 있다 |
 | 4 | 데스크톱 기능: 폴더 선택, 탐색기 열기, `settings.json`, 단일 인스턴스, 종료 연동 | README의 "알려진 제한" 항목과 동작이 같다 ([계획서](../plans/2026-10-11-tauri-desktop-features.md), [보고서](2026-10-11-tauri-desktop-features-report.md): 통과). 앱 데이터 폴더의 기본값이 `%APPDATA%\xGetSongs`(Compose와 같음)가 됐다 |
-| 5 | 포장과 단일 exe: 번들 JRE + 셸. 이전 설계(§6.4)의 SFX 판정 방식 재사용 | 깨끗한 PC 흉내 검증 ([계획서](../plans/2026-10-15-tauri-packaging.md)) |
+| 5 | 포장과 단일 exe: 번들 JRE + 셸. 이전 설계(§6.4)의 SFX 판정 방식 재사용 | 깨끗한 PC 흉내 검증 ([계획서](../plans/2026-10-15-tauri-packaging.md), [보고서](2026-10-15-tauri-packaging-report.md): 통과). 방식은 SFX가 아니라 셸 exe 뒤에 이미지를 붙이고 첫 실행에 `%LOCALAPPDATA%\xGetSongs\runtime\<해시>`로 풀어 쓰는 것이다(약 130MB 한 파일, `package-exe.bat`) |
 | 6 | `app/` 삭제, README·설계 문서 갱신 | `gradlew check` 통과 |
 | 7 | (선택) 가사 검색의 JavaFX 웹뷰를 WebView2로 교체 | 구글 가사 통합 테스트 재검증 |
 
@@ -122,8 +122,8 @@ UI 품질이 동기이므로 사이드카보다 먼저 확인한다. 가짜 데
 | 위험 | 대응 |
 |---|---|
 | 화면이 기대만큼 나아지지 않는다. 이 마이그레이션의 유일한 이득이 UI 품질이므로 이것이 가장 큰 위험이다 | 0a에서 사이드카 없이 먼저 확인한다. 통과하지 못하면 0b 이후를 하지 않는다 |
-| 단일 exe: Tauri의 `resources`는 exe에 내장되지 않는다. Enigma Virtual Box는 자식 프로세스(`java.exe`)가 가상 파일 시스템을 못 볼 수 있다 | 5단계에서 풀어서 실행하는 SFX를 기본 후보로 두고 측정으로 정한다 |
-| WebView2 런타임: Windows 11에는 있지만 Windows 10은 따로 설치해야 한다 | 5단계에서 "설치 없는 exe" 목표와 조율한다 |
+| 단일 exe: Tauri의 `resources`는 exe에 내장되지 않는다. Enigma Virtual Box는 자식 프로세스(`java.exe`)가 가상 파일 시스템을 못 볼 수 있다 | 정함(5단계): SFX 대신 셸 exe 뒤에 이미지를 붙이고 첫 실행에만 풀어 쓴다. 측정과 이유는 5단계 보고서 |
+| WebView2 런타임: Windows 11에는 있지만 Windows 10은 따로 설치해야 한다 | 정함(5단계): 시작할 때 레지스트리로 확인하고, 없으면 안내 상자를 띄워 내려받기 페이지를 연다. 설치 파일을 exe에 넣지 않는다 |
 | 로그 위치: 지금은 `jpackage.app-path`를 기준으로 `log\`를 정한다 | 1단계에서 사이드카용 규칙을 정하고 README를 고친다 |
 | `settings.json`을 쓰는 곳이 둘이 되면 충돌한다 | 4단계에서 쓰는 주체를 한 곳으로 정한다 (정함: Rust 셸 한 곳, 4단계) |
 | JavaFX가 JVM을 붙잡아 종료가 안 된다 | 사이드카는 `exitProcess`로 끝내고 `FxWebViewBrowser.close()`는 서버 scope가 끝날 때 불린다 |
```

- [ ] **Step 10: 메모리를 갱신한다**

`C:\Users\gomto\.claude\projects\C--Projects-xGetSongs\memory\project-tauri-migration.md`의 맨 위 "최신" 단락을 5단계 구현 완료(커밋 범위, 관문 결과, 남은 일: 사용자의 실제 PC 확인, push 여부, 6단계)로 고치고 `MEMORY.md`의 한 줄 소개를 맞춘다.

- [ ] **Step 11: 커밋한다**

```powershell
Set-Location C:\Projects\xGetSongs; git add docs/superpowers/specs/2026-10-15-tauri-packaging-report.md docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md; git commit -m "문서: 포장과 단일 exe(5단계)의 종단 확인 결과와 설계의 5단계 행

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## 실행 뒤

전체 브랜치 리뷰는 executing-plans의 "Final Review" 절차를 따른다(가장 유능한 모델, 이 계획의 Review Focus를 그대로 전한다). Critical·Important는 한 번에 고치고(각 고침은 실패하는 시험이 먼저), Minor는 보고서에 적는다. 끝나면 **푸시 여부를 사용자에게 묻는다**.

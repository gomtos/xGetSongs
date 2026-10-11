# Tauri 데스크톱 기능 (4단계) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **실행 뒤 메모:** 이 계획서의 코드 블록은 실행 직전의 검증본이다. 실행한 뒤 전체 브랜치 리뷰가 지적한 것은 저장소의 코드와 이 계획서의 스크립트 블록에서 고쳤고(내용은 [보고서](../specs/2026-10-11-tauri-desktop-features-report.md)의 "최종 리뷰 뒤 고친 것"), 그 밖의 코드 블록은 저장소의 것이 기준이다: `folder.rs`(쉼표가 든 경로를 `explorer.exe`에 항상 따옴표로 넘긴다, 대화상자의 시작 폴더를 절대 경로로), `settings.rs`(따옴표 친 불리언), `main.ts`(폴더 선택 창을 한 번에 하나만), `kotlin-coverage.test.ts`(`#[test]`가 있고 `#[ignore]`가 아닌 함수만 센다), `rust-names.test.ts`(새), `check.conf.json`(점검용 빌드의 `identifier`를 `com.xgetsongs.shell.check`로: 사용자가 쓰는 앱과 단일 인스턴스 뮤텍스·WebView2 프로필을 나눈다), 확인 스크립트(사이드카 범위를 점검 실행의 것으로 좁히고, 이름으로 `shell`이나 `vite`를 모두 끝내는 명령을 없앴다). 점검용 빌드는 일반 빌드와 같은 `target\release\shell.exe`를 덮어쓰므로 점검 뒤에 일반 빌드를 다시 만든다.

**Goal:** 지금 Compose 앱이 데스크톱 쪽에서 해 주는 일을 Tauri 셸이 한다: 출력 폴더 선택 창, "탐색기에서 보기", 옵션 저장(`settings.json`), 단일 인스턴스, 창을 닫을 때의 정리. 셸이 쓰는 앱 데이터 폴더는 Compose 앱과 같은 `%APPDATA%\xGetSongs`다(지금까지는 임시 폴더였다). 이 단계가 끝나면 3단계의 화면이 Compose 앱의 일을 모두 하고, 일반 빌드를 쓸 수 있다.

**Architecture:** 데스크톱이 하는 일은 Rust에 둔다. 웹뷰에는 우리가 만든 명령 몇 개만 열고 플러그인의 권한은 주지 않는다(2단계의 "웹뷰는 `invoke`와 이벤트만"을 지킨다). Tauri 없이 시험할 수 있는 부분은 `sidecar-host`의 모듈로 둔다: `app_data_dir`, `settings`(원본 `JsonSettingsStoreTest`의 22개를 같은 이름으로 이식), `folder`(`ExplorerOpenerTest`의 10개를 이식), `close_gate`(창 닫기 협의). 셸(`lib.rs`)은 그것을 명령 6개(`load_settings`, `save_settings`, `pick_folder`, `open_folder`, `page_listens_for_close`, `close_window`)와 `on_window_event`로 이어 붙이고, 공식 플러그인 둘을 쓴다(`tauri-plugin-dialog`는 Rust에서만, `tauri-plugin-single-instance`). 화면은 `Desktop`이라는 이음매(`src/desktop.ts`)만 알고, 창에서는 Rust 명령을, `?demo`에서는 가짜 데스크톱을 쓴다. 확인은 단위 시험, 데모 위 브라우저 시나리오, 그리고 실제 셸에서 시스템 대화상자와 파일 관리자까지 조작하는 PowerShell 점검이다.

**Tech Stack:** Rust 2021/2024(`sidecar-host`는 2021, 셸은 2024), Tauri 2.12, `tauri-plugin-dialog` 2.8.2, `tauri-plugin-single-instance` 2.5.3, TypeScript 6, Node 24 `node --test`, PowerShell + UI Automation. 서버와 Kotlin 변경은 없다. **새 의존성은 Rust 크레이트 둘이다**(npm 패키지는 없다): 내려받으려면 crates.io에 접속하므로 Task 5를 시작하기 전에 사용자의 허락을 받는다.

**Spec:** [docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md](../specs/2026-10-10-tauri-kotlin-sidecar-design.md) §5의 4단계("폴더 선택, 탐색기 열기, `settings.json`, 단일 인스턴스, 종료 연동", 완료 증거: README의 "알려진 제한" 항목과 동작이 같다), §6의 위험 "`settings.json`을 쓰는 곳이 둘이 되면 충돌한다". 옮기는 원본은 `app/src/desktopMain/kotlin/com/xgetsongs/app/{ExplorerOpener,FolderPicker,JsonSettingsStore,Main}.kt`와 `app/src/desktopTest/.../{ExplorerOpenerTest,JsonSettingsStoreTest}.kt`, 화면의 쪽은 `app/.../ui/OptionsPanel.kt`다.

## 이 계획이 내린 결정

설계 문서가 4단계에 맡긴 것과 범위의 경계다. 사용자가 계획서를 검토할 때 바꿀 수 있다.

| 결정 | 내용 | 근거 |
|---|---|---|
| `settings.json`을 쓰는 곳 | **Rust 셸 한 곳**(`sidecar-host`의 `settings` 모듈). 서버와 사이드카는 이 파일을 읽지도 쓰지도 않는다 | 서버·엔진·shared의 코드에는 `settings.json`이 나오지 않는다(`grep`으로 확인). 파일 형식은 Compose 앱이 쓰는 그대로(`outputDir`, `overwrite`, `includeRank`, `searchLyricsOnline`)이고 옛 `concurrency` 키는 읽을 때 무시되고 다음 저장에 빠진다 |
| 설정 읽기·쓰기의 규칙 | 원본 `JsonSettingsStore`와 같다: 없거나 읽을 수 없거나 UTF-8이 아니거나 모양이 틀리면 기본값, 쓸 수 없으면 아무것도 하지 않는다(실패하지 않는다), 임시 파일에 쓰고 옮긴다 | 원본 22개 시험을 같은 이름으로 이식하고, 이름이 빠지면 실패하는 점검 테스트를 더한다(3단계와 같은 방식) |
| 앱 데이터 폴더 | 기본값을 임시 폴더에서 **`%APPDATA%\xGetSongs`**(없으면 사용자 폴더의 `.xgetsongs`)로 바꾼다. `XGS_SIDECAR_APPDATA`가 있으면 그것을 쓴다(확인 스크립트는 항상 임시 폴더를 준다) | 이 앱이 Compose 앱을 대신하려면 설치한 yt-dlp(`bin\`)와 `settings.json`이 같은 자리에 있어야 한다. 사이드카는 시작할 때 그 안의 `work\`를 지우므로 **Compose 앱과 셸을 동시에 실행하면 안 된다**(README의 제한 그대로, 이 단계는 셸끼리만 막는다) |
| 단일 인스턴스 | `tauri-plugin-single-instance`. 두 번째 실행은 첫 창을 앞으로 가져오고 끝난다. **사이드카 시작을 `Builder` 앞에서 `build()` 뒤로 옮긴다** | 플러그인은 `build()` 안(플러그인 `setup`)에서 `process::exit(0)`로 끝난다. 사이드카를 그 앞에 시작하면 두 번째 실행도 사이드카를 띄워 첫 실행의 `work\`를 지운다. 계획을 쓰며 대조해 봤다: 단일 인스턴스가 없는 3단계 빌드는 두 번째 실행이 15초 넘게 살아 있고 사이드카가 둘이 되며 첫 실행의 `work\` 표식이 사라지고 첫 창은 최소화된 채였다. 새 빌드는 두 번째 실행이 약 100ms에 끝나고 표식은 남고 사이드카는 하나며 첫 창이 복원돼 앞에 온다 |
| 폴더 선택 | `tauri-plugin-dialog`를 **Rust 명령 `pick_folder`에서만** 쓴다(웹뷰에 `dialog:*` 권한을 주지 않는다). 창을 부모로 해 모달이고, 제목은 `출력 폴더 선택`(원본과 같다), 입력란의 폴더가 실제 폴더면 거기서 시작한다 | 원본은 Swing의 `JFileChooser`다. 시스템 대화상자(`IFileOpenDialog`)가 UI 품질이 이 마이그레이션의 동기인 점에 맞다 |
| 탐색기 | `explorer.exe`를 직접 실행하는 Rust 명령 `open_folder`. 가장 가까운 존재하는 폴더를 열고, 별도 스레드에서 하며 실패는 삼킨다 | 원본 `ExplorerOpener`와 같은 규칙이다. 원본의 `Desktop.open` 먼저·`explorer.exe` 다음 순서는 JVM의 사정이라 `explorer.exe`만 쓴다 |
| 창 닫기 | Rust가 닫기 요청을 가로채 페이지에 `xgs-close-requested`를 보내고, 페이지가 옵션을 쓴 뒤 `close_window`로 답한다. 답이 **1.5초** 안에 없으면 Rust가 창을 닫는다. 페이지가 듣고 있다고 알리기 전(점검 페이지, 부팅 중)에는 바로 닫는다 | 원본 `onCloseRequest`의 `flushSettings()`에 해당한다. 마지막 0.4초(저장 대기) 안의 변경이 사라지지 않게 하되, 멈춘 페이지가 창을 붙잡지 못하게 한다. 자바스크립트 쪽 `onCloseRequested`는 쓰지 않는다: 웹뷰에 `core:window:allow-destroy` 권한이 필요하고 페이지가 멈추면 창을 닫을 수 없다 |
| 화면의 옵션 | `#options`를 두 `fieldset`으로 나누고 저장 위치 줄을 그 사이에 둔다 | "탐색기에서 보기"는 작업 중에도 눌려야 한다(원본: 항상 활성). 비활성 `fieldset` 안의 단추는 비활성이라 한 덩어리로는 안 된다 |
| 이 단계가 미루는 것 | README 갱신(6단계), 로그 폴더 정책과 포장(5단계), Compose 앱과 셸의 동시 실행 방지, ffmpeg 경로 설정(원래 없다), 사용하지 않는 `tauri-plugin-opener`와 그 권한의 정리 | 범위 밖. 마지막 둘은 보고서에 적는다 |

## Global Constraints

- 서버(`server/`, `engine/`, `shared/`)와 `app/`은 바꾸지 않는다. 이 단계의 변경은 `sidecar-host/`, `shell/`(TypeScript와 Rust 한 곳)과 문서뿐이다.
- 이 마이그레이션은 지금 동작을 그대로 옮긴다(설계 §3). 기능을 더하지 않는다. 사용자에게 보이는 문구는 Compose 앱과 같다(`폴더 선택`, `탐색기에서 보기`, 대화상자 제목 `출력 폴더 선택`).
- 코드와 코드 주석은 영어, 사용자에게 보이는 문구와 커밋 메시지는 한국어. `main`에 직접 커밋한다. 커밋 메시지 끝에는 이 세션이 정한 `Co-Authored-By` 줄을 붙인다.
- Bash 도구는 명령 텍스트의 백슬래시를 한 번 더 해석하고 이 PC의 도구 셸에는 `cargo`가 PATH에 없다. 코드는 Write·Edit 도구로 쓰고 `cargo`, `npm`, `node`는 PowerShell 도구로 부른다(PowerShell 호출마다 `$env:Path += ";$env:USERPROFILE\.cargo\bin"`). PowerShell 도구는 `Remove-Item`과 `robocopy`를 한 명령에 섞으면 막으므로 따로 부른다.
- 확인은 사용자의 `%APPDATA%\xGetSongs`와 `~\Music\xGetSongs`를 건드리지 않는다. 확인 스크립트는 앱 데이터로 임시 폴더를 쓰고, 사용자의 폴더에서는 **읽기만** 한다(`bin\yt-dlp.exe`와 `settings.json`을 복사). 실제 다운로드는 사용자가 이 확인에 쓰라고 준 영상 한 곡과 재생목록(취소만 한다)뿐이다.
- **확인 스크립트는 창을 `Process.MainWindowHandle`로 찾지 않는다.** 단일 인스턴스 플러그인이 시작하자마자 보이는 숨은 창(클래스 `com.xgetsongs.shell-sic`)을 만들어서, 처음 몇십 ms 동안 .NET은 그것을 주 창으로 본다(계획을 쓰며 시작 직후 닫기가 10번 중 9번 "실패"한 원인이었고 제품이 아니라 스크립트의 문제였다). 새 `helpers.ps1`은 클래스 `Tauri Window`인 창을 찾는다.
- **`SendKeys`로 대화상자에 글자를 넣지 않는다.** 포커스가 다른 창(사용자의 터미널 등)에 있으면 거기에 입력된다. 폴더는 대화상자의 목록에서 UI Automation으로 고른다.
- 이 계획의 diff는 LF 줄 끝 기준이다. 작업 폴더의 파일이 CRLF이면(`shell/src-tauri/Cargo.toml`이 그렇다) `git apply --ignore-whitespace`를 쓰거나 Edit 도구로 같은 내용을 넣는다. 새 파일은 LF로 쓴다.
- 테스트는 `node --test "tests/*.test.ts"`로 돈다. `tests/`는 `tsc`의 범위 밖이다. Rust 시험은 `sidecar-host`에서 `cargo test`다(Tauri를 빌드하지 않아 빠르다).
- Gradle은 부르지 않는다. 이미 만들어진 사이드카 이미지 `server\build\sidecar-image`를 쓴다.

## Review Focus

계획의 시험이 직접 다루지 않아 사용자에게 먼저 문제가 될 것, 가능성이 높은 순서:

1. **두 번째 실행이 첫 실행의 작업을 망가뜨린다.** 앱을 두 번 눌렀을 때(바로가기 더블클릭, 작업 표시줄) 사이드카가 하나 더 떠서 실행 중인 다운로드의 `work\` 폴더를 지우면 안 된다. 첫 창이 최소화돼 있었다면 앞으로 와야 한다. → Task 5(사이드카 시작 위치), Task 8의 `single-instance.ps1`(`work\` 표식으로 확인)
2. **방금 바꾼 옵션이 닫을 때 사라진다.** 옵션은 0.4초 조용했을 때 저장되므로, 바꾸자마자 창을 닫으면 마지막 변경이 안 써진다. → Task 6의 닫기 시험, Task 8의 `settings-check.ps1`(변경 직후 닫아 파일을 읽는다)
3. **Compose 앱이 쓴 `settings.json`을 못 읽거나 지운다.** 줄 끝·BOM·UTF-8이 아닌 파일, 모르는 키, 옛 `concurrency`, 깨진 파일, 쓸 수 없는 폴더. 읽지 못했을 때 기본값으로 덮어써서 사용자의 설정을 잃으면 안 된다(화면이 바꾼 것이 없으면 쓰지 않는다: 3단계의 `lastKnown`). → Task 2의 이식 시험 22개, Task 8의 `settings-check.ps1 -UseRealSettings`(사용자의 실제 파일 복사본)
4. **폴더 선택 창이 창을 막지 않거나 취소했는데 값이 바뀐다.** 모달이어야 하고, 입력한 폴더에서 시작하고, 취소하면 입력란이 그대로이고, 고르면 입력란과 저장 위치가 바뀐다. → Task 8의 `picker-check.ps1`
5. **창이 닫히지 않는다.** 페이지가 멈췄거나 응답이 없을 때 닫기 단추가 영영 안 먹으면 안 된다. 또 "탐색기에서 보기"로 연 파일 관리자 창이 앱을 닫을 때 같이 사라지면 안 된다(셸은 프로세스를 함께 끝내는 Job Object 안에 있다). → Task 4와 Task 8의 `hang-check.ps1`, `explorer-check.ps1`

## 이 단계의 관문

| 번호 | 조건 | 확인 |
|---|---|---|
| D1 | `sidecar-host` 시험이 모두 통과한다: 기존 75개 + `config` 1개 + `settings` 24개(원본 22개와 포트가 더한 2개) + `folder` 12개(원본 10개와 2개) + `close_gate` 4개 = 116개. 원본 Kotlin 시험의 이름이 Rust에서 빠지면 점검 테스트가 실패한다 | Task 1~4, 6 |
| D2 | TS 시험이 모두 통과한다: `desktop.test.ts` 20개와 확장한 점검 테스트(3개 더)를 포함해 239개 | Task 6 |
| D3 | 데모 위 브라우저 시나리오 F(폴더 선택, 탐색기 열기, 작업 중 잠금, 저장 호출)와 회귀 A~E가 모두 `pass: true`다 | Task 7 |
| D4 | 실제 셸: Compose 형식의 `settings.json`과 사용자의 실제 파일의 복사본을 읽고, 옵션을 바꾸자마자 닫아도 파일에 써지고(`concurrency` 키는 빠지고 임시 파일은 남지 않는다), 아무것도 안 바꾼 쪽은 건드리지 않는다 | Task 8 |
| D5 | 실제 셸: 두 번째 실행이 3초 안에 끝나고 첫 실행의 `work\` 표식과 사이드카 하나가 그대로며 첫 창이 복원돼 앞에 온다 | Task 8 |
| D6 | 실제 셸: 폴더 선택 창이 모달로 열려 입력한 폴더에서 시작하고, 취소하면 입력란이 그대로며, 고르면 입력란과 저장 위치가 바뀐다 | Task 8 |
| D7 | 실제 셸: "탐색기에서 보기"가 없는 하위 경로의 가장 가까운 존재하는 폴더를 열고 앱이 끝나도 그 창이 남는다. 멈춘 페이지는 창 닫기를 5초 이상 붙잡지 못한다 | Task 8 |
| D8 | 앞 단계의 관문이 유지된다: 3단계 화면 점검(영상 한 곡, 재생목록 취소), 시작 직후 닫기 10번 모두 15초 이하·마커 `reason=user`·남은 것 0, 강제 종료 5초 이하, `waitedMs` 10초 이하, 마지막에 전체 시험·`tsc`·빌드·clippy | Task 8, 9 |

## File Structure

| 파일 | 책임 |
|---|---|
| `sidecar-host/src/config.rs` (수정) | `app_data_dir`: 앱 데이터 폴더의 규칙 |
| `sidecar-host/src/settings.rs` (새) | `UserSettings`, `SettingsFile`(`load`, `save`): `settings.json`의 읽기·쓰기 |
| `sidecar-host/src/folder.rs` (새) | `existing_folder_for`, `existing_directory`, `open_in_explorer` |
| `sidecar-host/src/close_gate.rs` (새) | `CloseGate`, `CloseDecision`: 닫기 요청을 어떻게 다룰지 |
| `sidecar-host/src/lib.rs`, `sidecar-host/Cargo.toml` (수정) | 모듈 등록, 크레이트 설명 |
| `shell/src-tauri/Cargo.toml`, `shell/src-tauri/src/lib.rs` (수정) | 플러그인 둘, 명령 6개, 단일 인스턴스, 닫기 협의, 사이드카 시작 위치, 점검용 명령(`check_close`, `check_wait`) |
| `shell/src/desktop.ts`, `shell/src/desktop-tauri.ts` (새) | 화면이 아는 데스크톱(`Desktop`)과 창의 Rust 명령으로 만든 구현, 가짜 데스크톱 |
| `shell/index.html`, `shell/src/app.css`, `shell/src/main.ts` (수정) | 옵션 분리, 두 단추의 연결, 닫기 협의의 응답, 부팅 때 설정 읽기 |
| `shell/tests/desktop.test.ts` (새), `shell/tests/kotlin-coverage.test.ts` (수정) | 데스크톱 시험, Rust 포트의 원본 이름 점검 |
| `shell/src/check.ts`, `shell/src/check-screen.ts` (수정) | 점검 모드 `settings`, `picker`, `explorer`, `hang` |
| `%TEMP%\xgs-bridge-check\*.ps1` (저장소 밖) | `helpers.ps1`(창 찾기 수정), `desktop-helpers.ps1`, `single-instance.ps1`, `settings-check.ps1`, `hang-check.ps1`, `explorer-check.ps1`, `picker-check.ps1` |
| `docs/superpowers/specs/2026-10-11-tauri-desktop-features-report.md` (새) | 종단 확인 결과 |

---

### Task 1: 앱 데이터 폴더의 기본값

**Files:**
- Modify: `sidecar-host/src/config.rs`

**Interfaces:**
- Consumes: 없다.
- Produces (Task 5가 쓴다): `app_data_dir(get: impl Fn(&str) -> Option<OsString>) -> PathBuf`: `XGS_SIDECAR_APPDATA`, 없으면 `%APPDATA%\xGetSongs`, 없으면 `<USERPROFILE>\.xgetsongs`(그것도 없으면 임시 폴더). 빈 값은 없는 것으로 친다. `SidecarConfig::from_lookup`이 이것을 쓴다.

- [ ] **Step 1: 실패하는 시험을 쓴다**

`sidecar-host/src/config.rs`의 시험 모듈에 아래를 적용한다(기존 시험 하나의 기대값이 바뀌고 시험이 하나 는다):

```diff
diff --git a/sidecar-host/src/config.rs b/sidecar-host/src/config.rs
index 8dfee2a..be4cd7d 100644
--- a/sidecar-host/src/config.rs
+++ b/sidecar-host/src/config.rs
@@ -116,10 +116,31 @@ mod tests {
         let config = SidecarConfig::from_lookup(lookup(&[("XGS_SIDECAR_LIB", r"C:\x\lib")]), None).unwrap();
         assert_eq!(config.java, PathBuf::from("java"));
         assert_eq!(config.lib_dir, PathBuf::from(r"C:\x\lib"));
-        assert_eq!(config.app_data, std::env::temp_dir().join("xgs-spike-appdata"));
+        assert_eq!(config.app_data, std::env::temp_dir().join(".xgetsongs"), "no APPDATA and no user folder: the temp folder");
         assert_eq!(config.log_dir, None);
     }
 
+    #[test]
+    fn the_app_data_folder_is_the_one_of_the_compose_app_unless_another_is_named() {
+        let appdata = r"C:\Users\me\AppData\Roaming";
+        assert_eq!(app_data_dir(lookup(&[("APPDATA", appdata)])), PathBuf::from(r"C:\Users\me\AppData\Roaming\xGetSongs"));
+        assert_eq!(
+            app_data_dir(lookup(&[("APPDATA", appdata), ("XGS_SIDECAR_APPDATA", r"D:\other")])),
+            PathBuf::from(r"D:\other"),
+            "the variable of the checks wins"
+        );
+        assert_eq!(
+            app_data_dir(lookup(&[("USERPROFILE", r"C:\Users\me")])),
+            PathBuf::from(r"C:\Users\me\.xgetsongs"),
+            "without APPDATA: the folder in the user folder, like the Compose app"
+        );
+        assert_eq!(
+            app_data_dir(lookup(&[("APPDATA", ""), ("XGS_SIDECAR_APPDATA", ""), ("USERPROFILE", r"C:\Users\me")])),
+            PathBuf::from(r"C:\Users\me\.xgetsongs"),
+            "empty values count as not set"
+        );
+    }
+
     #[test]
     fn the_given_values_are_used() {
         let config = SidecarConfig::from_lookup(
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --lib config:: 2>&1 | Select-String "^error|cannot find|test result"
```

Expected: 컴파일 오류 `cannot find function `app_data_dir` in this scope`.

- [ ] **Step 3: 구현한다**

```diff
diff --git a/sidecar-host/src/config.rs b/sidecar-host/src/config.rs
index be4cd7d..08b6343 100644
--- a/sidecar-host/src/config.rs
+++ b/sidecar-host/src/config.rs
@@ -14,6 +14,21 @@ pub struct SidecarConfig {
     pub log_dir: Option<PathBuf>,
 }
 
+/// Where the app keeps its own files: the yt-dlp it installed, `settings.json`, the temporary download folders. The same folder
+/// the Compose app uses: `%APPDATA%\xGetSongs`, else `.xgetsongs` in the user's folder. `XGS_SIDECAR_APPDATA` names another one
+/// (the checks use a temp folder: the sidecar clears `work` in it when it starts, which must never happen to the real one of a
+/// running app).
+pub fn app_data_dir(get: impl Fn(&str) -> Option<OsString>) -> PathBuf {
+    let given = |name: &str| get(name).filter(|value| !value.is_empty());
+    if let Some(dir) = given("XGS_SIDECAR_APPDATA") {
+        return PathBuf::from(dir);
+    }
+    if let Some(appdata) = given("APPDATA") {
+        return PathBuf::from(appdata).join("xGetSongs");
+    }
+    given("USERPROFILE").map(PathBuf::from).unwrap_or_else(std::env::temp_dir).join(".xgetsongs")
+}
+
 impl SidecarConfig {
     pub fn from_env() -> Result<SidecarConfig, String> {
         let exe_dir = std::env::current_exe().ok().and_then(|exe| exe.parent().map(Path::to_path_buf));
@@ -26,9 +41,8 @@ impl SidecarConfig {
     /// 2. a sidecar image, a folder with `runtime\bin\java.exe` and `lib`: `XGS_SIDECAR_DIR`, else the `sidecar` folder next
     ///    to the executable ([exe_dir]). A folder that is named but holds no runtime is an error, not a reason to look on.
     ///
-    /// The app data folder is `XGS_SIDECAR_APPDATA`, by default a temp folder, never the user's real app data (the spike
-    /// must not clear the `work` folder of a running copy of the app). The log folder is `XGS_SIDECAR_LOGDIR`, else `log`
-    /// next to the executable, else the sidecar chooses.
+    /// The app data folder is [app_data_dir]. The log folder is `XGS_SIDECAR_LOGDIR`, else `log` next to the executable, else
+    /// the sidecar chooses.
     pub fn from_lookup(
         get: impl Fn(&str) -> Option<OsString>,
         exe_dir: Option<&Path>,
@@ -54,9 +68,7 @@ impl SidecarConfig {
             }
             (java, image.join("lib"))
         };
-        let app_data = get("XGS_SIDECAR_APPDATA")
-            .map(PathBuf::from)
-            .unwrap_or_else(|| std::env::temp_dir().join("xgs-spike-appdata"));
+        let app_data = app_data_dir(&get);
         let log_dir = get("XGS_SIDECAR_LOGDIR").map(PathBuf::from).or_else(|| exe_dir.map(|dir| dir.join("log")));
         Ok(SidecarConfig { java, lib_dir, app_data, log_dir })
     }
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --lib config:: 2>&1 | Select-String "test result"; cargo test --lib 2>&1 | Select-String "test result"
```

Expected: `config::`는 `10 passed`(9에서 1개 늘었다), 크레이트 전체는 `76 passed`.

- [ ] **Step 5: Commit**

```bash
git add sidecar-host/src/config.rs
git commit -m "feat(sidecar-host): 앱 데이터 폴더의 기본값을 Compose 앱과 같은 %APPDATA%\\xGetSongs로 한다"
```

---

### Task 2: 설정 파일 `settings.json`

**Files:**
- Create: `sidecar-host/src/settings.rs`
- Modify: `sidecar-host/src/lib.rs`

**Interfaces:**
- Consumes: 없다(`serde`, `serde_json`은 이미 의존성이다).
- Produces (Task 5가 쓴다): `UserSettings { output_dir: Option<String>, overwrite: bool, include_rank: bool, search_lyrics_online: bool }`(JSON은 camelCase, `Default`는 원본과 같다: 폴더 없음, 덮어쓰기 끔, 순번 넣기 켬, 가사 검색 켬), `SettingsFile::new(path)`, `.load() -> UserSettings`(실패하지 않는다), `.save(&UserSettings)`(실패하지 않고, 두 저장이 겹쳐도 한 번에 하나씩), `Clone`.

원본의 22개 시험은 같은 이름의 snake case 함수로 있다(`aMissingFileGivesTheDefaults`는 `a_missing_file_gives_the_defaults`. Kotlin의 `null`은 Rust의 `None`, `Throw`는 `panic`). JVM의 `Proxy` 경로를 쓰는 `anUnexpectedRuntimeFailureNeverEscapesFromLoadOrSave`는 "플랫폼이 거절하는 경로(NUL)"로 옮겼다. 포트가 더한 시험은 둘이다: 여러 스레드에서 동시에 저장해도 파일이 하나로 유효하다, 페이지가 보내는 JSON이 파일과 같은 모양이다.

- [ ] **Step 1: 실패하는 시험을 쓴다**

`sidecar-host/src/lib.rs`에 `pub mod settings;`를 더하고(`pub mod lifecycle;` 다음), 새 파일 `sidecar-host/src/settings.rs`를 아래로 만든다(시험만 있다):

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    /// A fresh folder in the temp folder that is deleted again when the test is over, with the settings file of the test in it.
    struct Rig {
        dir: PathBuf,
        file: PathBuf,
        store: SettingsFile,
    }

    impl Rig {
        fn new(name: &str) -> Rig {
            let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
            let dir = std::env::temp_dir().join(format!("xgs-settings-test-{}-{name}-{nanos}", std::process::id()));
            fs::create_dir_all(&dir).unwrap();
            let file = dir.join("settings.json");
            Rig { store: SettingsFile::new(&file), dir, file }
        }

        fn write(&self, text: &str) {
            fs::write(&self.file, text.as_bytes()).unwrap();
        }

        fn files_in(&self, directory: &Path) -> Vec<String> {
            let mut names: Vec<String> =
                fs::read_dir(directory).unwrap().map(|entry| entry.unwrap().file_name().to_string_lossy().into_owned()).collect();
            names.sort();
            names
        }

        fn text(&self) -> String {
            fs::read_to_string(&self.file).unwrap()
        }
    }

    impl Drop for Rig {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.dir);
        }
    }

    fn settings(output_dir: Option<&str>, overwrite: bool, include_rank: bool, search_lyrics_online: bool) -> UserSettings {
        UserSettings { output_dir: output_dir.map(str::to_string), overwrite, include_rank, search_lyrics_online }
    }

    // ---- load ----

    #[test]
    fn a_missing_file_gives_the_defaults() {
        let rig = Rig::new("missing");

        assert_eq!(rig.store.load(), UserSettings::default());
    }

    #[test]
    fn the_defaults_are_what_the_spec_says() {
        assert_eq!(UserSettings::default(), settings(None, false, true, true));
        assert!(UserSettings::default().search_lyrics_online, "the lyrics search is on until the user turns it off");
    }

    #[test]
    fn saved_settings_come_back_unchanged_including_a_korean_path() {
        let rig = Rig::new("roundtrip");
        let saved = settings(Some("D:\\음악\\내 재생목록"), true, false, false);

        rig.store.save(&saved);

        assert_eq!(rig.store.load(), saved);
        assert_eq!(SettingsFile::new(&rig.file).load(), saved, "a second store reads the same file");
        assert!(rig.text().contains("내 재생목록"), "the file is UTF-8 text");
    }

    #[test]
    fn default_values_are_written_out_too() {
        let rig = Rig::new("defaults-written");

        rig.store.save(&UserSettings::default());

        let text = rig.text();
        for key in ["outputDir", "overwrite", "includeRank", "searchLyricsOnline"] {
            assert!(text.contains(&format!("\"{key}\"")), "{key} in {text}");
        }
        assert!(!text.contains("concurrency"), "the server decides the concurrency, so it is not stored: {text}");
        assert!(text.contains("\"searchLyricsOnline\": true"), "{text}");
    }

    #[test]
    fn an_old_file_without_the_lyrics_key_loads_with_the_search_on() {
        let rig = Rig::new("old");
        rig.write(r#"{"outputDir":"D:/x","overwrite":true,"includeRank":false,"concurrency":3}"#);

        assert_eq!(rig.store.load(), settings(Some("D:/x"), true, false, true));
        assert!(rig.store.load().search_lyrics_online);
    }

    #[test]
    fn a_stored_search_lyrics_online_value_is_read() {
        let rig = Rig::new("lyrics-read");
        rig.write(r#"{"searchLyricsOnline":false}"#);
        assert!(!rig.store.load().search_lyrics_online);

        rig.write(r#"{"overwrite":true,"searchLyricsOnline":true,"concurrency":4}"#);
        assert_eq!(rig.store.load(), UserSettings { overwrite: true, ..UserSettings::default() });
    }

    #[test]
    fn a_turned_off_lyrics_search_is_written_and_comes_back() {
        let rig = Rig::new("lyrics-off");

        rig.store.save(&UserSettings { search_lyrics_online: false, ..UserSettings::default() });

        assert!(rig.text().contains("\"searchLyricsOnline\": false"));
        assert!(!SettingsFile::new(&rig.file).load().search_lyrics_online);
    }

    #[test]
    fn files_that_cannot_be_used_give_the_defaults_and_never_panic() {
        let rig = Rig::new("unusable");
        let cases = [
            ("corrupt JSON", "{"),
            ("empty file", ""),
            ("blank file", "  \n "),
            ("wrong shape", "[]"),
            ("a bare value", "42"),
            ("the null literal", "null"),
            ("text that is not JSON", "hello"),
            ("a field of the wrong type", r#"{"overwrite":"yes"}"#),
            ("a lyrics option of the wrong type", r#"{"overwrite":true,"searchLyricsOnline":"no"}"#),
            ("a number where the folder should be", r#"{"outputDir":5}"#),
        ];
        for (name, content) in cases {
            rig.write(content);

            assert_eq!(rig.store.load(), UserSettings::default(), "{name}");
        }
    }

    #[test]
    fn valid_json_with_bytes_that_are_not_utf8_gives_the_defaults_instead_of_a_garbled_folder() {
        // The syntax is fine and only the encoding is wrong: what an ANSI/CP949 editor writes for a Korean folder name (0xC0 0xBD
        // 0xBE 0xC7) is not UTF-8, and must not turn into U+FFFD characters in the output folder.
        let rig = Rig::new("not-utf8");
        let mut body = br#"{"outputDir":"D:\\"#.to_vec();
        body.extend_from_slice(&[0xC0, 0xBD, 0xBE, 0xC7]);
        body.extend_from_slice(br#"","overwrite":true,"concurrency":4}"#);
        fs::write(&rig.file, body).unwrap();

        assert_eq!(rig.store.load(), UserSettings::default());
    }

    #[test]
    fn a_utf16_file_gives_the_defaults() {
        // What Windows PowerShell's Out-File writes by default: a byte order mark, then two bytes per character.
        let rig = Rig::new("utf16");
        let mut body = vec![0xFF, 0xFE];
        for unit in r#"{"overwrite":true}"#.encode_utf16() {
            body.extend_from_slice(&unit.to_le_bytes());
        }
        fs::write(&rig.file, body).unwrap();

        assert_eq!(rig.store.load(), UserSettings::default());
    }

    #[test]
    fn a_utf8_byte_order_mark_in_front_of_valid_json_is_ignored() {
        let rig = Rig::new("bom");
        let json = r#"{"outputDir":"D:\\음악","overwrite":true,"includeRank":false,"concurrency":3}"#;
        let mut body = vec![0xEF, 0xBB, 0xBF];
        body.extend_from_slice(json.as_bytes());
        fs::write(&rig.file, body).unwrap();

        assert_eq!(rig.store.load(), settings(Some("D:\\음악"), true, false, true));
    }

    #[test]
    fn a_folder_in_place_of_the_file_gives_the_defaults() {
        let rig = Rig::new("folder");
        fs::create_dir(&rig.file).unwrap();

        assert_eq!(rig.store.load(), UserSettings::default());
    }

    #[test]
    fn unknown_keys_are_ignored_and_the_known_ones_still_read() {
        let rig = Rig::new("unknown");
        rig.write(
            r#"{"outputDir":"D:/x","future":{"a":[1,2]},"overwrite":true,"theme":"dark","includeRank":false,"concurrency":3,"searchLyricsOnline":false}"#,
        );

        assert_eq!(rig.store.load(), settings(Some("D:/x"), true, false, false));
    }

    #[test]
    fn missing_keys_take_their_defaults() {
        let rig = Rig::new("missing-keys");
        rig.write(r#"{"overwrite":true}"#);

        assert_eq!(rig.store.load(), UserSettings { overwrite: true, ..UserSettings::default() });
    }

    #[test]
    fn a_concurrency_from_an_older_file_is_ignored_and_left_out_of_the_next_save() {
        let rig = Rig::new("concurrency");
        for stored in ["3", "99999999999", "\"many\"", "null"] {
            rig.write(&format!(r#"{{"overwrite":true,"concurrency":{stored}}}"#));

            assert_eq!(rig.store.load(), UserSettings { overwrite: true, ..UserSettings::default() }, "stored {stored}");
        }

        rig.store.save(&rig.store.load());

        assert!(!rig.text().contains("concurrency"));
    }

    #[test]
    fn a_blank_stored_folder_is_kept_as_it_is_for_the_caller_to_replace() {
        let rig = Rig::new("blank-folder");
        rig.write(r#"{"outputDir":"   "}"#);
        assert_eq!(rig.store.load().output_dir.as_deref(), Some("   "));

        rig.write(r#"{"outputDir":""}"#);
        assert_eq!(rig.store.load().output_dir.as_deref(), Some(""));

        rig.write(r#"{"outputDir":null}"#);
        assert_eq!(rig.store.load().output_dir, None);
    }

    // ---- save ----

    #[test]
    fn save_creates_the_parent_directories() {
        let rig = Rig::new("parents");
        let nested = SettingsFile::new(rig.dir.join("a").join("b").join("settings.json"));
        let saved = settings(Some("D:/Songs"), true, true, true);

        nested.save(&saved);

        assert_eq!(nested.load(), saved);
        assert_eq!(rig.files_in(&rig.dir.join("a").join("b")), ["settings.json"]);
    }

    #[test]
    fn save_leaves_no_temporary_file_behind() {
        let rig = Rig::new("no-temp");

        rig.store.save(&settings(Some("D:/Songs"), false, true, true));
        rig.store.save(&settings(Some("D:/Other"), false, true, true));

        assert_eq!(rig.files_in(&rig.dir), ["settings.json"]);
    }

    #[test]
    fn save_replaces_an_existing_file() {
        let rig = Rig::new("replace");
        rig.write(r#"{"outputDir":"D:/old","overwrite":true,"includeRank":false,"concurrency":4,"searchLyricsOnline":false,"future":1}"#);
        let saved = settings(Some("D:/new"), false, true, true);

        rig.store.save(&saved);

        assert_eq!(rig.store.load(), saved);
        assert_eq!(rig.files_in(&rig.dir), ["settings.json"]);
        assert!(!rig.text().contains("future"), "the old content is gone");
    }

    #[test]
    fn save_into_a_location_that_cannot_be_written_does_not_panic() {
        let rig = Rig::new("blocked");
        let blocker = rig.dir.join("not-a-folder");
        fs::write(&blocker, [1u8]).unwrap();
        let broken = SettingsFile::new(blocker.join("settings.json"));

        broken.save(&settings(Some("D:/Songs"), false, true, true));

        assert_eq!(broken.load(), UserSettings::default());
        assert_eq!(rig.files_in(&rig.dir), ["not-a-folder"], "nothing else was created");
    }

    #[test]
    fn an_unexpected_runtime_failure_never_escapes_from_load_or_save() {
        // The JVM test used a path whose every operation threw; here the path is one the platform refuses at every step.
        let broken = SettingsFile::new("bad\u{0}name\\settings.json");

        broken.save(&settings(Some("D:/Songs"), true, true, true));

        assert_eq!(broken.load(), UserSettings::default());
        let root = SettingsFile::new(if cfg!(windows) { "C:\\" } else { "/" });
        root.save(&UserSettings::default()); // a file system root has no name to build a temp file's from
        assert_eq!(root.load(), UserSettings::default());
    }

    #[test]
    fn save_over_a_directory_that_is_not_empty_does_not_panic_and_leaves_no_temporary_file() {
        let rig = Rig::new("over-dir");
        fs::create_dir(&rig.file).unwrap();
        fs::write(rig.file.join("keep.txt"), [1u8]).unwrap();

        rig.store.save(&settings(Some("D:/Songs"), false, true, true));

        assert_eq!(rig.files_in(&rig.dir), ["settings.json"]);
        assert_eq!(rig.files_in(&rig.file), ["keep.txt"]);
    }

    // ---- not in the Kotlin file ----

    #[test]
    fn saves_from_several_threads_leave_one_valid_file_and_no_temporary_file() {
        let rig = Rig::new("threads");
        std::thread::scope(|scope| {
            for index in 0..8 {
                let store = &rig.store;
                scope.spawn(move || store.save(&settings(Some(&format!("D:/Songs{index}")), index % 2 == 0, true, true)));
            }
        });

        let loaded = rig.store.load();
        assert!(loaded.output_dir.as_deref().is_some_and(|dir| dir.starts_with("D:/Songs")), "{loaded:?}");
        assert_eq!(rig.files_in(&rig.dir), ["settings.json"]);
    }

    #[test]
    fn the_settings_the_page_sends_are_read_in_the_shape_of_the_file() {
        let sent: UserSettings =
            serde_json::from_str(r#"{"outputDir":"D:\\음악","overwrite":true,"includeRank":false,"searchLyricsOnline":true}"#).unwrap();

        assert_eq!(sent, settings(Some("D:\\음악"), true, false, true));
        assert_eq!(serde_json::to_value(&sent).unwrap()["outputDir"], "D:\\음악", "and sent back in the same shape");
        assert!(serde_json::from_str::<UserSettings>(r#"{"outputDir":null,"overwrite":1,"includeRank":true,"searchLyricsOnline":true}"#).is_err());
    }
}
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --lib settings:: 2>&1 | Select-String "^error" | Select-Object -First 4
```

Expected: 컴파일 오류(`cannot find type `UserSettings` in this scope`, `cannot find type `SettingsFile``).

- [ ] **Step 3: 구현한다**

`sidecar-host/src/settings.rs`의 **맨 위**, 시험 모듈(`#[cfg(test)]`) 앞에 아래를 넣는다:

```rust
//! The options the app restores on the next start, kept in `settings.json` in the app data folder. Ported from
//! `app/src/commonMain/kotlin/com/xgetsongs/app/settings/UserSettings.kt` and `app/src/desktopMain/kotlin/com/xgetsongs/app/JsonSettingsStore.kt`:
//! the file is the one the Compose app wrote and reads, and the tests of this file are the cases of `JsonSettingsStoreTest.kt`, one for
//! one and under the same name in snake case.
//!
//! This is the only writer of the file: the sidecar never touches it.

use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

/// The options the app restores on the next start. The typed playlist or video address and the rank of a single video are not part
/// of it. `output_dir` is None (or blank) when the user never chose a folder: the app then uses its default. The lyrics search is on
/// for a settings file written before the option existed. A `concurrency` in an older settings file is ignored: the server decides how
/// many items are downloaded at once.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct UserSettings {
    pub output_dir: Option<String>,
    pub overwrite: bool,
    pub include_rank: bool,
    pub search_lyrics_online: bool,
}

impl Default for UserSettings {
    fn default() -> UserSettings {
        UserSettings { output_dir: None, overwrite: false, include_rank: true, search_lyrics_online: true }
    }
}

/// Two saves at once (the options change while the window closes) must not write the same temporary file together.
static WRITE_LOCK: Mutex<()> = Mutex::new(());

/// Keeps the options in a small UTF-8 JSON file. Neither function panics or fails, whatever goes wrong: a file that is missing,
/// unreadable, not valid UTF-8 or not what was expected means the defaults, and a profile that cannot be written to means the options
/// are not kept (the app works the same without remembering them).
#[derive(Clone)]
pub struct SettingsFile {
    path: PathBuf,
}

impl SettingsFile {
    pub fn new(path: impl Into<PathBuf>) -> SettingsFile {
        SettingsFile { path: path.into() }
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    pub fn load(&self) -> UserSettings {
        read(&self.path).unwrap_or_default()
    }

    /// Writes next to the target and moves it over, so a crash never leaves half a file behind.
    pub fn save(&self, settings: &UserSettings) {
        let _guard = WRITE_LOCK.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
        let mut temp: Option<PathBuf> = None;
        let _ = write(&self.path, settings, &mut temp);
        if let Some(temp) = temp {
            let _ = std::fs::remove_file(temp); // a leftover temp file is overwritten by the next save
        }
    }
}

fn read(path: &Path) -> Option<UserSettings> {
    let bytes = std::fs::read(path).ok()?;
    // Strict: bytes that are not UTF-8 (a file saved in the ANSI code page) must give the defaults, not a folder name full of U+FFFD.
    // A byte order mark, which Notepad and PowerShell like to add, is not part of the JSON.
    let text = String::from_utf8(bytes).ok()?;
    let value: Value = serde_json::from_str(text.strip_prefix('\u{feff}').unwrap_or(&text)).ok()?;
    let object = value.as_object()?;
    let defaults = UserSettings::default();
    // A key that is missing takes its default; a key of the wrong type makes the whole file unusable. Unknown keys are ignored.
    let flag = |key: &str, default: bool| match object.get(key) {
        None => Some(default),
        Some(Value::Bool(value)) => Some(*value),
        Some(_) => None,
    };
    let output_dir = match object.get("outputDir") {
        None | Some(Value::Null) => None,
        Some(Value::String(text)) => Some(text.clone()),
        Some(_) => return None,
    };
    Some(UserSettings {
        output_dir,
        overwrite: flag("overwrite", defaults.overwrite)?,
        include_rank: flag("includeRank", defaults.include_rank)?,
        search_lyrics_online: flag("searchLyricsOnline", defaults.search_lyrics_online)?,
    })
}

fn write(path: &Path, settings: &UserSettings, temp: &mut Option<PathBuf>) -> std::io::Result<()> {
    let name = path
        .file_name()
        .ok_or_else(|| std::io::Error::new(std::io::ErrorKind::InvalidInput, "a file system root has no name"))?;
    let mut temp_name = name.to_os_string();
    temp_name.push(".tmp");
    let temp_path = path.with_file_name(temp_name);
    if let Some(parent) = std::path::absolute(path)?.parent() {
        std::fs::create_dir_all(parent)?;
    }
    *temp = Some(temp_path.clone());
    std::fs::write(&temp_path, serde_json::to_vec_pretty(settings)?)?;
    std::fs::rename(&temp_path, path)
}
```

`sidecar-host/src/lib.rs`의 `pub use sidecar::Sidecar;` 앞에 `pub use settings::{SettingsFile, UserSettings};`를 더한다.

- [ ] **Step 4: 통과를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --lib settings:: 2>&1 | Select-String "FAILED|test result"
```

Expected: `24 passed; 0 failed`.

- [ ] **Step 5: 시험이 정말 무는지 확인한다**

`sidecar-host/src/settings.rs`의 `read`에서 `text.strip_prefix('\u{feff}').unwrap_or(&text)`를 `&text`로 잠시 바꾸고 돌린다:

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --lib settings:: 2>&1 | Select-String "FAILED|test result"
```

Expected: `a_utf8_byte_order_mark_in_front_of_valid_json_is_ignored`가 FAILED(`23 passed; 1 failed`). 확인한 뒤 **되돌린다**.

- [ ] **Step 6: Commit**

```bash
git add sidecar-host/src/settings.rs sidecar-host/src/lib.rs
git commit -m "feat(sidecar-host): settings.json을 읽고 쓰는 모듈을 더하고 원본 시험 22개를 같은 이름으로 옮긴다"
```

---

### Task 3: 폴더 열기

**Files:**
- Create: `sidecar-host/src/folder.rs`
- Modify: `sidecar-host/src/lib.rs`

**Interfaces:**
- Consumes: 없다.
- Produces (Task 5가 쓴다): `existing_folder_for(path: &str) -> Option<PathBuf>`(존재하는 가장 가까운 폴더, 빈 경로·플랫폼이 거절하는 경로·루트까지 없는 경로는 `None`), `existing_directory(path: &str) -> Option<PathBuf>`(그 자체가 폴더일 때만), `open_in_explorer(path: &str)`(별도 스레드, 실패는 삼킨다).

원본 `ExplorerOpenerTest`의 10개를 같은 이름으로 옮기고 둘을 더한다(폴더 선택 창이 시작할 폴더, 드라이브 문자의 `:`). 원본은 Kotlin의 `InvalidPathException`을 "열 폴더 없음"으로 다루므로, Rust는 같은 뜻으로 제어 문자와 `<>"|?*`, 드라이브 문자가 아닌 자리의 `:`를 거절한다.

- [ ] **Step 1: 실패하는 시험을 쓴다**

`sidecar-host/src/lib.rs`에 `pub mod folder;`를 더하고(`pub mod error;` 다음), 새 파일 `sidecar-host/src/folder.rs`를 아래로 만든다(시험만 있다):

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    /// A fresh folder in the temp folder that is deleted again when the test is over.
    struct Scratch(PathBuf);

    impl Scratch {
        fn new(name: &str) -> Scratch {
            let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
            let dir = std::env::temp_dir().join(format!("xgs-folder-test-{}-{name}-{nanos}", std::process::id()));
            fs::create_dir_all(&dir).unwrap();
            Scratch(dir)
        }
    }

    impl Drop for Scratch {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    fn text(path: &Path) -> &str {
        path.to_str().unwrap()
    }

    #[test]
    fn an_existing_directory_returns_itself() {
        let scratch = Scratch::new("existing");
        let folder = scratch.0.join("Music");
        fs::create_dir_all(&folder).unwrap();

        assert_eq!(existing_folder_for(text(&folder)), Some(folder.clone()));
        assert_eq!(existing_folder_for(text(&scratch.0)), Some(scratch.0.clone()));
    }

    #[test]
    fn a_missing_child_of_an_existing_directory_returns_the_parent() {
        let scratch = Scratch::new("child");
        let folder = scratch.0.join("Music");
        fs::create_dir_all(&folder).unwrap();

        assert_eq!(existing_folder_for(text(&folder.join("My Playlist"))), Some(folder));
    }

    #[test]
    fn several_missing_levels_return_the_nearest_existing_ancestor() {
        let scratch = Scratch::new("levels");
        let folder = scratch.0.join("Music").join("Rock");
        fs::create_dir_all(&folder).unwrap();

        assert_eq!(existing_folder_for(text(&folder.join("a").join("b").join("c"))), Some(folder));
        assert_eq!(existing_folder_for(text(&scratch.0.join("x").join("y"))), Some(scratch.0.clone()));
    }

    #[test]
    fn a_regular_file_returns_its_parent_directory() {
        let scratch = Scratch::new("file");
        let folder = scratch.0.join("Music");
        fs::create_dir_all(&folder).unwrap();
        let file = folder.join("001 A - T.mp3");
        fs::write(&file, "data").unwrap();

        assert_eq!(existing_folder_for(text(&file)), Some(folder));
    }

    #[test]
    fn a_path_below_a_regular_file_returns_the_directory_around_the_file() {
        let scratch = Scratch::new("below");
        let file = scratch.0.join("a-file");
        fs::write(&file, "data").unwrap();

        assert_eq!(existing_folder_for(text(&file.join("child"))), Some(scratch.0.clone()));
    }

    #[test]
    fn a_blank_path_returns_none() {
        assert_eq!(existing_folder_for(""), None);
        assert_eq!(existing_folder_for("   "), None);
        assert_eq!(existing_folder_for("\t"), None);
    }

    #[test]
    fn a_path_the_platform_rejects_returns_none_and_does_not_panic() {
        let scratch = Scratch::new("rejected");

        assert_eq!(existing_folder_for(&format!("{}\\bad\u{0}name", text(&scratch.0))), None);
        assert_eq!(existing_folder_for("\u{0}"), None);
        assert_eq!(existing_folder_for(&format!("{}\\what?", text(&scratch.0))), None, "a question mark cannot be in a name");
        assert_eq!(existing_folder_for(&format!("{}\\a<b", text(&scratch.0))), None);
    }

    #[test]
    fn a_path_with_no_existing_ancestor_returns_none() {
        // Only Windows has roots that can be missing (a drive letter nobody uses).
        if !cfg!(windows) {
            return;
        }
        let missing = ('A'..='Z').rev().find(|letter| !Path::new(&format!("{letter}:\\")).exists()).expect("a free drive letter");

        assert_eq!(existing_folder_for(&format!("{missing}:\\Music\\My Playlist")), None);
        assert_eq!(existing_folder_for(&format!("{missing}:\\")), None);
    }

    #[test]
    fn a_relative_path_is_looked_up_from_the_working_folder() {
        let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
        let missing = format!("xgs-missing-folder-{nanos}");

        let found = existing_folder_for(&missing);

        assert_eq!(found, Some(std::env::current_dir().unwrap()), "the nearest existing ancestor of a relative path is the working folder");
    }

    #[test]
    fn opening_without_a_folder_to_open_does_nothing_and_does_not_panic() {
        open_in_explorer("");
        open_in_explorer("   ");
        open_in_explorer("\u{0}");
    }

    // ---- not in the Kotlin file: where the folder chooser starts ----

    #[test]
    fn the_chooser_starts_in_the_typed_folder_only_when_it_is_one() {
        let scratch = Scratch::new("chooser");
        let file = scratch.0.join("a-file");
        fs::write(&file, "data").unwrap();

        assert_eq!(existing_directory(text(&scratch.0)), Some(scratch.0.clone()));
        assert_eq!(existing_directory(text(&scratch.0.join("missing"))), None);
        assert_eq!(existing_directory(text(&file)), None, "a file is not a folder to start in");
        assert_eq!(existing_directory(""), None);
        assert_eq!(existing_directory("   "), None);
    }

    #[test]
    fn a_drive_letter_path_with_its_colon_is_a_path() {
        assert!(is_path_the_platform_takes("C:\\Music"));
        assert!(is_path_the_platform_takes("D:/음악/내 재생목록"));
        assert!(is_path_the_platform_takes("relative\\folder"));
        assert!(!is_path_the_platform_takes("C:\\a:b"), "a colon anywhere else is not");
    }
}
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --lib folder:: 2>&1 | Select-String "^error" | Select-Object -First 4
```

Expected: 컴파일 오류(`cannot find function `existing_folder_for` in this scope` 등).

- [ ] **Step 3: 구현한다**

`sidecar-host/src/folder.rs`의 **맨 위**, 시험 모듈 앞에 아래를 넣는다:

```rust
//! Folders for the screen. Ported from `app/src/desktopMain/kotlin/com/xgetsongs/app/ExplorerOpener.kt` and `FolderPicker.kt`: the
//! tests of this file are the cases of `ExplorerOpenerTest.kt`, one for one and under the same name in snake case.

use std::path::{Path, PathBuf};
use std::process::Command;

/// Whether the platform would take [path] as a path at all: no control characters, none of `<>"|?*`, and a `:` only as the
/// one of a drive (`C:`). Kotlin's `Path.of` threw an `InvalidPathException` for the others, which meant "no folder to show".
fn is_path_the_platform_takes(path: &str) -> bool {
    path.char_indices().all(|(index, character)| match character {
        '<' | '>' | '"' | '|' | '?' | '*' => false,
        ':' => index == 1,
        other => !other.is_control(),
    })
}

/// The folder to show for [path]: the first existing directory on the way up from it (the path itself when it is a directory, the
/// parent of a regular file, then the parents, up to the root), or None for a blank path, one the platform rejects, or one whose
/// whole chain is missing (a drive that does not exist). A relative path is read from the working folder, which is where the
/// server would put the files too. Never panics.
pub fn existing_folder_for(path: &str) -> Option<PathBuf> {
    if path.trim().is_empty() || !is_path_the_platform_takes(path) {
        return None;
    }
    let mut candidate: Option<&Path> = None;
    let absolute = std::path::absolute(path).ok()?;
    let mut current = absolute.as_path();
    loop {
        if current.is_dir() {
            candidate = Some(current);
            break;
        }
        match current.parent() {
            Some(parent) => current = parent,
            None => break,
        }
    }
    candidate.map(Path::to_path_buf)
}

/// [path] itself when it is an existing directory, else None: where the folder chooser starts (Kotlin: `File(initial).takeIf
/// { it.isDirectory }`).
pub fn existing_directory(path: &str) -> Option<PathBuf> {
    if path.trim().is_empty() {
        return None;
    }
    let directory = PathBuf::from(path);
    directory.is_dir().then_some(directory)
}

/// Shows [existing_folder_for] of [path] in the system file manager; does nothing when there is no such folder. All of it runs on
/// a thread of its own, so the caller never waits for the file manager (or for a network share that does not answer). Opening the
/// folder is only a convenience, so every failure is swallowed.
pub fn open_in_explorer(path: &str) {
    let path = path.to_string();
    let _ = std::thread::Builder::new().name("open-in-explorer".into()).spawn(move || {
        if let Some(folder) = existing_folder_for(&path) {
            let _ = Command::new("explorer.exe").arg(folder).spawn();
        }
    });
}
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --lib folder:: 2>&1 | Select-String "FAILED|test result"
```

Expected: `12 passed; 0 failed`.

- [ ] **Step 5: 시험이 정말 무는지 확인한다**

`folder.rs`의 `is_path_the_platform_takes`에서 `'?'`를 지운 줄(`'<' | '>' | '"' | '|' | '*' => false,`)로 잠시 바꾸고 돌린다. Expected: `a_path_the_platform_rejects_returns_none_and_does_not_panic`가 FAILED. 확인한 뒤 **되돌린다**.

- [ ] **Step 6: Commit**

```bash
git add sidecar-host/src/folder.rs sidecar-host/src/lib.rs
git commit -m "feat(sidecar-host): 존재하는 가장 가까운 폴더를 찾아 탐색기로 여는 모듈을 더하고 원본 시험 10개를 옮긴다"
```

---

### Task 4: 창 닫기 협의와 크레이트 정리

**Files:**
- Create: `sidecar-host/src/close_gate.rs`
- Modify: `sidecar-host/src/lib.rs`, `sidecar-host/Cargo.toml`

**Interfaces:**
- Consumes: 없다.
- Produces (Task 5가 쓴다): `CloseGate`(`Default`, `page_listens()`, `on_close_requested() -> CloseDecision`), `CloseDecision::{Close, Ask, Wait}`: 페이지가 듣고 있지 않으면 `Close`, 듣고 있으면 첫 요청은 `Ask`(페이지에 묻고 시간 제한을 건다), 그 뒤 요청은 `Wait`.

- [ ] **Step 1: 실패하는 시험을 쓴다**

`sidecar-host/src/lib.rs`에 `pub mod close_gate;`를 더하고(`pub mod bridge;` 다음), 새 파일 `sidecar-host/src/close_gate.rs`를 아래로 만든다(시험만 있다):

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_window_whose_page_does_not_listen_closes_at_once() {
        let gate = CloseGate::default();

        assert_eq!(gate.on_close_requested(), CloseDecision::Close);
        assert_eq!(gate.on_close_requested(), CloseDecision::Close, "every time, there is nothing to wait for");
    }

    #[test]
    fn a_page_that_listens_is_asked_the_first_time() {
        let gate = CloseGate::default();
        gate.page_listens();

        assert_eq!(gate.on_close_requested(), CloseDecision::Ask);
    }

    #[test]
    fn a_second_request_while_the_page_is_still_answering_only_waits() {
        let gate = CloseGate::default();
        gate.page_listens();
        gate.on_close_requested();

        assert_eq!(gate.on_close_requested(), CloseDecision::Wait);
        assert_eq!(gate.on_close_requested(), CloseDecision::Wait, "however often the user presses close");
    }

    #[test]
    fn listening_after_a_close_request_changes_the_next_answer_only() {
        let gate = CloseGate::default();
        assert_eq!(gate.on_close_requested(), CloseDecision::Close);

        gate.page_listens();

        assert_eq!(gate.on_close_requested(), CloseDecision::Ask);
    }
}
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --lib close_gate:: 2>&1 | Select-String "^error" | Select-Object -First 3
```

Expected: 컴파일 오류(`cannot find type `CloseGate` in this scope` 등).

- [ ] **Step 3: 구현한다**

`sidecar-host/src/close_gate.rs`의 **맨 위**, 시험 모듈 앞에 아래를 넣는다:

```rust
//! What to do when the user closes the window. The page may have things to finish first (the options that wait for their quiet
//! period before they are written): once it says it is listening, the first request to close is answered by asking the page
//! and by arming a time limit, so a page that never answers cannot keep the window open.

use std::sync::atomic::{AtomicBool, Ordering};

/// What the window should do with a request to close it.
#[derive(Debug, PartialEq, Eq)]
pub enum CloseDecision {
    /// Let the window close now.
    Close,
    /// Keep the window, tell the page to finish up and close the window when it answers, and close it anyway after a time limit.
    Ask,
    /// The page was asked already and has not answered yet: keep the window and do nothing else.
    Wait,
}

#[derive(Default)]
pub struct CloseGate {
    listening: AtomicBool,
    asked: AtomicBool,
}

impl CloseGate {
    /// The page has a listener for the question and will answer it.
    pub fn page_listens(&self) {
        self.listening.store(true, Ordering::SeqCst);
    }

    pub fn on_close_requested(&self) -> CloseDecision {
        if !self.listening.load(Ordering::SeqCst) {
            return CloseDecision::Close;
        }
        if self.asked.swap(true, Ordering::SeqCst) {
            CloseDecision::Wait
        } else {
            CloseDecision::Ask
        }
    }
}
```

- [ ] **Step 4: 크레이트를 정리한다**

`sidecar-host/src/lib.rs`는 이제 이렇게 된다(`pub use`가 새 모듈들을 내보낸다):

```rust
pub mod bridge;
pub mod close_gate;
pub mod config;
pub mod error;
pub mod folder;
pub mod handshake;
pub mod job;
pub mod lifecycle;
pub mod settings;
pub mod sidecar;
pub mod sse;

#[cfg(test)]
mod fake_server;

pub use bridge::Bridge;
pub use close_gate::{CloseDecision, CloseGate};
pub use config::{app_data_dir, SidecarConfig};
pub use error::BridgeError;
pub use handshake::{parse_handshake, Handshake};
pub use lifecycle::Lifecycle;
pub use settings::{SettingsFile, UserSettings};
pub use sidecar::Sidecar;
```

`sidecar-host/Cargo.toml`의 설명을 바꾼다:

```diff
diff --git a/sidecar-host/Cargo.toml b/sidecar-host/Cargo.toml
index c2d5c37..182db62 100644
--- a/sidecar-host/Cargo.toml
+++ b/sidecar-host/Cargo.toml
@@ -2,7 +2,7 @@
 name = "xgs-sidecar"
 version = "0.1.0"
 edition = "2021"
-description = "Starts, talks to and stops the Kotlin sidecar of xGetSongs. No Tauri here, so its tests build fast."
+description = "Starts, talks to and stops the Kotlin sidecar of xGetSongs, and the small desktop helpers around it (settings file, folders, closing). No Tauri here, so its tests build fast."
 
 [dependencies]
 # Plain HTTP to 127.0.0.1 only: no TLS needed.
```

- [ ] **Step 5: 통과를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test --lib close_gate:: 2>&1 | Select-String "FAILED|test result"; cargo test 2>&1 | Select-String "test result"; cargo clippy --all-targets 2>&1 | Select-String "^(warning|error)|-->|Finished"
```

Expected: `close_gate::`는 `4 passed`, 전체는 단위 `116 passed`와 통합 `1 ignored` 둘(`real_download`, `real_sidecar`), clippy는 `src\job.rs:11`의 경고 하나(`set_extended_limit_info`가 `&mut`을 필요로 하지 않는다. 2단계부터 있던 것이고 이 단계가 만든 것이 아니다)와 `Finished`.

- [ ] **Step 6: 시험이 정말 무는지 확인한다**

`close_gate.rs`에서 `self.asked.swap(true, Ordering::SeqCst)`를 `self.asked.swap(false, Ordering::SeqCst)`로 잠시 바꾸고 돌린다. Expected: `a_second_request_while_the_page_is_still_answering_only_waits`가 FAILED. 확인한 뒤 **되돌린다**.

- [ ] **Step 7: Commit**

```bash
git add sidecar-host
git commit -m "feat(sidecar-host): 창 닫기 협의(CloseGate)를 더하고 크레이트가 새 모듈들을 내보내게 한다"
```

---

### Task 5: 셸의 Rust: 플러그인, 명령, 단일 인스턴스, 닫기

**Files:**
- Modify: `shell/src-tauri/Cargo.toml`, `shell/src-tauri/src/lib.rs`

**Interfaces:**
- Consumes: Task 1~4의 `app_data_dir`, `SettingsFile`, `UserSettings`, `folder`, `CloseGate`, `CloseDecision`.
- Produces (Task 6~8이 쓴다): Tauri 명령 `load_settings() -> UserSettings`, `save_settings(settings)`, `pick_folder(initial: String) -> Option<String>`, `open_folder(path)`, `page_listens_for_close()`, `close_window()`; 이벤트 `xgs-close-requested`(Rust가 페이지에); 점검 모드에서만 `check_close()`, `check_wait(name)`와 `check_env`의 허용 이름 `XGS_CHECK_DESKTOP`.

이 작업에는 단위 시험이 없다(Tauri의 연결 코드다). 시험은 Task 8의 종단 확인이다. 이 작업은 컴파일과 clippy가 깨끗함을 확인한다.

- [ ] **Step 1: 의존성을 더한다 (사용자의 허락이 필요하다)**

두 크레이트는 crates.io에서 내려받는다. 시작하기 전에 사용자에게 "공식 `tauri-plugin-dialog` 2.8.2와 `tauri-plugin-single-instance` 2.5.3(그것들의 의존성 포함)을 내려받는다"고 알리고 허락을 받는다.

```diff
diff --git a/shell/src-tauri/Cargo.toml b/shell/src-tauri/Cargo.toml
index 41ac8af..7d85f48 100644
--- a/shell/src-tauri/Cargo.toml
+++ b/shell/src-tauri/Cargo.toml
@@ -20,6 +20,8 @@ tauri-build = { version = "2", features = [] }
 [dependencies]
 tauri = { version = "2", features = [] }
 tauri-plugin-opener = "2"
+tauri-plugin-dialog = "2"
+tauri-plugin-single-instance = "2"
 serde = { version = "1", features = ["derive"] }
 serde_json = "1"
 xgs-sidecar = { path = "../../sidecar-host" }
```

- [ ] **Step 2: `lib.rs`를 바꾼다**

```diff
diff --git a/shell/src-tauri/src/lib.rs b/shell/src-tauri/src/lib.rs
index ddbc2b0..a24f05d 100644
--- a/shell/src-tauri/src/lib.rs
+++ b/shell/src-tauri/src/lib.rs
@@ -2,8 +2,12 @@ use serde_json::{json, Value};
 use std::io::Write;
 use std::time::Duration;
 use tauri::ipc::Channel;
-use tauri::{Manager, RunEvent, State};
-use xgs_sidecar::{job, Bridge, BridgeError, Lifecycle, Sidecar, SidecarConfig};
+use tauri::{Emitter, Manager, RunEvent, State, WindowEvent};
+use tauri_plugin_dialog::DialogExt;
+use xgs_sidecar::{
+    app_data_dir, folder, job, Bridge, BridgeError, CloseDecision, CloseGate, Lifecycle, SettingsFile, Sidecar, SidecarConfig,
+    UserSettings,
+};
 
 /// How long a call waits for a sidecar that is still starting. Starting itself gives up after [SIDECAR_START_LIMIT].
 const READY_WAIT: Duration = Duration::from_secs(90);
@@ -11,6 +15,10 @@ const SIDECAR_START_LIMIT: Duration = Duration::from_secs(60);
 /// When the app closes: how long the sidecar gets to end by itself, and how long one that is still starting is waited for.
 const STOP_GRACE: Duration = Duration::from_secs(3);
 const STOP_START_WAIT: Duration = Duration::from_secs(10);
+/// How long the page gets to finish up (write the options) when the window is closed, before the window closes anyway.
+const CLOSE_WAIT: Duration = Duration::from_millis(1500);
+/// What the shell tells the page when the user closes the window; the page answers with `close_window`.
+const CLOSE_REQUESTED_EVENT: &str = "xgs-close-requested";
 
 /// Runs [call] once the sidecar is ready, on a thread meant for waiting: a call lasts as long as the sidecar works (a
 /// resolve takes as long as yt-dlp does), and the thread of the window must stay free.
@@ -72,6 +80,55 @@ async fn watch_job(
     with_bridge(&lifecycle, move |bridge| bridge.watch_job(&job_id, |event| on_event.send(event).is_ok())).await
 }
 
+/// The options as the file in the app data folder holds them (the defaults when it holds none that can be used). The page asks once,
+/// before it builds its screen.
+#[tauri::command]
+async fn load_settings(file: State<'_, SettingsFile>) -> Result<UserSettings, String> {
+    let file = file.inner().clone();
+    tauri::async_runtime::spawn_blocking(move || file.load()).await.map_err(|error| error.to_string())
+}
+
+/// Writes the options. Nothing fails the page: a profile that cannot be written to means the options are not kept.
+#[tauri::command]
+async fn save_settings(file: State<'_, SettingsFile>, settings: UserSettings) -> Result<(), String> {
+    let file = file.inner().clone();
+    tauri::async_runtime::spawn_blocking(move || file.save(&settings)).await.map_err(|error| error.to_string())
+}
+
+/// Opens the system's folder chooser over the window, starting in [initial] when that is a folder. Resolves with the chosen path, or
+/// with nothing when the user cancels.
+#[tauri::command]
+async fn pick_folder(window: tauri::WebviewWindow, initial: String) -> Option<String> {
+    tauri::async_runtime::spawn_blocking(move || {
+        let mut dialog = window.dialog().file().set_title("출력 폴더 선택").set_parent(&window);
+        if let Some(directory) = folder::existing_directory(&initial) {
+            dialog = dialog.set_directory(directory);
+        }
+        dialog.blocking_pick_folder().and_then(|picked| picked.into_path().ok()).map(|path| path.to_string_lossy().into_owned())
+    })
+    .await
+    .ok()
+    .flatten()
+}
+
+/// Shows the folder in the file manager (the nearest one that exists). Returns at once; whatever goes wrong is swallowed.
+#[tauri::command]
+fn open_folder(path: String) {
+    folder::open_in_explorer(&path);
+}
+
+/// The page has a listener for [CLOSE_REQUESTED_EVENT]: from now on closing the window asks the page first.
+#[tauri::command]
+fn page_listens_for_close(gate: State<'_, CloseGate>) {
+    gate.page_listens();
+}
+
+/// The page has finished up: close the window now (the app ends with its last window).
+#[tauri::command]
+fn close_window(window: tauri::WebviewWindow) {
+    let _ = window.destroy();
+}
+
 /// Check mode is on when the check script names a file in `XGS_BRIDGE_CHECK_FILE`; a normal run has no such variable and
 /// the two commands below refuse.
 fn check_file() -> Result<std::path::PathBuf, String> {
@@ -94,13 +151,43 @@ fn check_record(key: String, value: String) -> Result<(), String> {
 #[tauri::command]
 fn check_env(name: String) -> Result<Option<String>, String> {
     check_file()?;
-    const ALLOWED: [&str; 3] = ["XGS_CHECK_VIDEO_URL", "XGS_CHECK_PLAYLIST_URL", "XGS_CHECK_OUTPUT_DIR"];
+    const ALLOWED: [&str; 4] = ["XGS_CHECK_VIDEO_URL", "XGS_CHECK_PLAYLIST_URL", "XGS_CHECK_OUTPUT_DIR", "XGS_CHECK_DESKTOP"];
     if !ALLOWED.contains(&name.as_str()) {
         return Err(format!("{name} is not one of the values of the check"));
     }
     Ok(std::env::var(&name).ok())
 }
 
+/// Closes the window as the user would (a request to close, which the page may answer first). Check mode only.
+#[tauri::command]
+fn check_close(window: tauri::WebviewWindow) -> Result<(), String> {
+    check_file()?;
+    window.close().map_err(|error| error.to_string())
+}
+
+/// Waits until the check script creates the file `<check file>.signal-<name>`: the script does something outside the window (it works
+/// a system dialog, looks at the file manager) and tells the page when it is done. Check mode only; gives up after a minute.
+#[tauri::command]
+async fn check_wait(name: String) -> Result<(), String> {
+    let mut signal = check_file()?.into_os_string();
+    if name.is_empty() || !name.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-') {
+        return Err(format!("{name} is not a name of a signal"));
+    }
+    signal.push(format!(".signal-{name}"));
+    let signal = std::path::PathBuf::from(signal);
+    tauri::async_runtime::spawn_blocking(move || {
+        for _ in 0..1200 {
+            if signal.exists() {
+                return Ok(());
+            }
+            std::thread::sleep(Duration::from_millis(50));
+        }
+        Err(format!("no signal {} after a minute", signal.display()))
+    })
+    .await
+    .map_err(|error| error.to_string())?
+}
+
 /// Does nothing. The check page times how long a round trip to Rust takes while the sidecar starts: a window whose thread is
 /// busy answers late.
 #[tauri::command]
@@ -128,15 +215,38 @@ pub fn run() {
     // recorded anywhere yet; the sidecar also ends by itself when its stdin closes.)
     let _ = job::kill_children_when_this_process_ends();
 
-    let config = SidecarConfig::from_env();
-    let lifecycle = Lifecycle::start(move || {
-        let config = config?;
-        Sidecar::spawn(config.command(), &config.stderr_log(), SIDECAR_START_LIMIT)
-    });
-
     let app = tauri::Builder::default()
+        // First of all: a second copy of the app ends here, while the plugins are set up, and brings the window of the first one to the
+        // front. Nothing that touches the app data folder may have started by then: the sidecar clears its `work` folder when it starts.
+        .plugin(tauri_plugin_single_instance::init(|app, _args, _cwd| {
+            if let Some(window) = app.get_webview_window("main") {
+                let _ = window.unminimize();
+                let _ = window.show();
+                let _ = window.set_focus();
+            }
+        }))
         .plugin(tauri_plugin_opener::init())
-        .manage(lifecycle)
+        .plugin(tauri_plugin_dialog::init())
+        .manage(CloseGate::default())
+        .on_window_event(|window, event| {
+            if let WindowEvent::CloseRequested { api, .. } = event {
+                match window.state::<CloseGate>().on_close_requested() {
+                    CloseDecision::Close => {}
+                    CloseDecision::Wait => api.prevent_close(),
+                    CloseDecision::Ask => {
+                        // The page writes the options that are still waiting for their quiet period and then asks for the window to be
+                        // closed. A page that does not answer must not keep the window open.
+                        api.prevent_close();
+                        let _ = window.emit(CLOSE_REQUESTED_EVENT, ());
+                        let window = window.clone();
+                        std::thread::spawn(move || {
+                            std::thread::sleep(CLOSE_WAIT);
+                            let _ = window.destroy();
+                        });
+                    }
+                }
+            }
+        })
         .invoke_handler(tauri::generate_handler![
             sidecar_ready,
             tools,
@@ -146,17 +256,37 @@ pub fn run() {
             start_job,
             cancel_job,
             watch_job,
+            load_settings,
+            save_settings,
+            pick_folder,
+            open_folder,
+            page_listens_for_close,
+            close_window,
             check_record,
             check_ping,
             check_env,
+            check_close,
+            check_wait,
             check_burst
         ])
         .build(tauri::generate_context!())
         .expect("error while building tauri application");
 
+    // Only now: a second copy of the app has ended inside `build` (the single-instance plugin), so what starts here is the one sidecar
+    // of the one app. (Started before the `Builder`, as it was, a second copy would start a sidecar too, and the sidecar clears the
+    // `work` folder of the app data folder when it starts: the downloads of the first copy would fail.)
+    let config = SidecarConfig::from_env();
+    app.manage(Lifecycle::start(move || {
+        let config = config?;
+        Sidecar::spawn(config.command(), &config.stderr_log(), SIDECAR_START_LIMIT)
+    }));
+    app.manage(SettingsFile::new(app_data_dir(|key| std::env::var_os(key)).join("settings.json")));
+
     app.run(|handle, event| {
-        if let RunEvent::Exit = event {
-            handle.state::<Lifecycle>().shutdown(STOP_GRACE, STOP_START_WAIT);
+        if let RunEvent::Exit = event
+            && let Some(lifecycle) = handle.try_state::<Lifecycle>()
+        {
+            lifecycle.shutdown(STOP_GRACE, STOP_START_WAIT);
         }
     });
 }
```

바뀐 것은 이렇다. 단일 인스턴스 플러그인이 `Builder`의 **첫** 플러그인이다(두 번째 실행은 `build()` 안에서 끝난다). `Lifecycle`(사이드카)은 `build()`가 돌아온 **뒤**, `app.run` **앞**에서 시작한다(두 번째 실행이 사이드카를 띄우지 못하게). `on_window_event`가 닫기 요청을 `CloseGate`로 다룬다. `app_data_dir`이 `settings.json`의 자리를 정한다.

- [ ] **Step 3: 컴파일하고 clippy를 돌린다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\shell\src-tauri; cargo clippy 2>&1 | Select-String "^(warning|error)|-->|Finished"
```

Expected: `Finished`, 경고 없음(새 크레이트를 처음 컴파일하므로 1~5분).

- [ ] **Step 4: Commit**

```bash
git add shell/src-tauri/Cargo.toml shell/src-tauri/Cargo.lock shell/src-tauri/src/lib.rs
git commit -m "feat(shell): 폴더 선택·탐색기·설정·닫기 명령과 단일 인스턴스를 더하고 사이드카를 build 뒤에 시작한다"
```

---

### Task 6: 화면이 아는 데스크톱 (`desktop.ts`)

**Files:**
- Create: `shell/src/desktop.ts`, `shell/src/desktop-tauri.ts`, `shell/tests/desktop.test.ts`
- Modify: `shell/tests/kotlin-coverage.test.ts`

**Interfaces:**
- Consumes: Task 5의 명령과 이벤트 이름, 3단계의 `SettingsStore`, `UserSettings`, `userSettings`(`state/types.ts`), `Deferred`, `flush`(`tests/support/fakes.ts`).
- Produces (Task 7이 쓴다): `Desktop { settings: SettingsStore; pickFolder(initial): Promise<string | null>; openFolder(path): void; onClose(beforeClose): Promise<void> }`, `createTauriDesktop(tauri, {closeLimitMs?})`(설정을 먼저 읽고, 실패해도 던지지 않는다), `createRealDesktop()`(`desktop-tauri.ts`), `createDemoDesktop()`(`opened`, `saved`를 기록), `createSettingsStore(initial, write)`(쓰기는 한 번에 하나씩 저장 순서대로, 실패는 삼킨다, `whenSaved()`), `normalizeSettings(raw)`, `CLOSE_REQUESTED_EVENT`.

- [ ] **Step 1: 실패하는 시험을 쓴다**

`shell/tests/desktop.test.ts`:

```typescript
// The desktop around the screen: the folder chooser, the file manager, the settings file and the closing of the window. The Rust side
// is reached through `invoke` and `listen`, which the tests replace with a recorder.

import assert from "node:assert/strict";
import test from "node:test";
import {
  CLOSE_REQUESTED_EVENT,
  createDemoDesktop,
  createSettingsStore,
  createTauriDesktop,
  normalizeSettings,
  type TauriLike,
} from "../src/desktop.ts";
import { userSettings } from "../src/state/types.ts";
import { Deferred, flush } from "./support/fakes.ts";

interface Call {
  command: string;
  args: Record<string, unknown> | undefined;
}

/** `invoke` and `listen` of a window whose Rust side answers with whatever the test put in `answers`. */
function recorder(answers: Record<string, (args: Record<string, unknown> | undefined) => unknown> = {}) {
  const calls: Call[] = [];
  const listeners = new Map<string, () => Promise<void> | void>();
  const tauri: TauriLike = {
    async invoke(command, args) {
      calls.push({ command, args });
      const answer = answers[command];
      return (answer ? await answer(args) : undefined) as never;
    },
    async listen(event, handler) {
      calls.push({ command: `listen:${event}`, args: undefined });
      listeners.set(event, handler);
    },
  };
  return { tauri, calls, listeners, names: () => calls.map((call) => call.command) };
}

const korean = userSettings({ outputDir: "D:\\음악\\내 재생목록", overwrite: true, includeRank: false, searchLyricsOnline: false });

// ---- the settings the Rust side hands over ----

test("settingsInTheShapeOfTheFileAreTakenAsTheyAre", () => {
  assert.deepEqual(normalizeSettings(korean), korean);
  assert.deepEqual(normalizeSettings({ outputDir: null, overwrite: false, includeRank: true, searchLyricsOnline: true }), userSettings());
});

test("settingsThatAreNotWhatWasExpectedAreTheDefaults", () => {
  for (const raw of [undefined, null, "x", 5, [], {}, { outputDir: 5 }, { outputDir: null, overwrite: "yes", includeRank: true, searchLyricsOnline: true }]) {
    assert.deepEqual(normalizeSettings(raw), userSettings(), JSON.stringify(raw));
  }
});

// ---- the store the screen is given ----

test("theStoreHandsOutWhatItWasStartedWith", () => {
  const store = createSettingsStore(korean, async () => {});

  assert.deepEqual(store.load(), korean);
  assert.deepEqual(store.load(), korean);
});

test("everySaveIsWrittenInTheOrderItWasMadeEvenWhenTheFirstWriteIsSlow", async () => {
  const written: string[] = [];
  const gate = new Deferred();
  const store = createSettingsStore(userSettings(), async (settings) => {
    if (settings.outputDir === "first") await gate.promise;
    written.push(settings.outputDir ?? "");
  });

  store.save(userSettings({ outputDir: "first" }));
  store.save(userSettings({ outputDir: "second" }));
  store.save(userSettings({ outputDir: "third" }));
  await flush();
  assert.deepEqual(written, [], "the first write has not finished, so nothing may overtake it");

  gate.resolve();
  await store.whenSaved();

  assert.deepEqual(written, ["first", "second", "third"]);
});

test("aWriteThatFailsDoesNotStopTheNextOnesAndNeverThrows", async () => {
  const written: string[] = [];
  const store = createSettingsStore(userSettings(), async (settings) => {
    if (settings.outputDir === "bad") throw new Error("disk full");
    written.push(settings.outputDir ?? "");
  });

  store.save(userSettings({ outputDir: "bad" }));
  store.save(userSettings({ outputDir: "good" }));
  await store.whenSaved();

  assert.deepEqual(written, ["good"]);
});

test("aWriteThatThrowsAtOnceIsTreatedLikeOneThatFails", async () => {
  const written: string[] = [];
  const store = createSettingsStore(userSettings(), (settings) => {
    if (settings.outputDir === "bad") throw new Error("not even a promise");
    written.push(settings.outputDir ?? "");
    return Promise.resolve();
  });

  store.save(userSettings({ outputDir: "bad" }));
  store.save(userSettings({ outputDir: "good" }));
  await store.whenSaved();

  assert.deepEqual(written, ["good"]);
});

test("whenSavedHasNothingToWaitForWithoutASave", async () => {
  const store = createSettingsStore(userSettings(), async () => {});

  await store.whenSaved();
});

// ---- the desktop of the real window ----

test("theDesktopLoadsTheSettingsFileBeforeItIsHandedOver", async () => {
  const { tauri, names } = recorder({ load_settings: () => korean });

  const desktop = await createTauriDesktop(tauri);

  assert.deepEqual(desktop.settings.load(), korean);
  assert.deepEqual(names(), ["load_settings"]);
});

test("aSettingsFileThatCannotBeLoadedMeansTheDefaultsAndNoFailure", async () => {
  const { tauri } = recorder({
    load_settings: () => {
      throw new Error("no such command");
    },
  });

  const desktop = await createTauriDesktop(tauri);

  assert.deepEqual(desktop.settings.load(), userSettings());
});

test("savedSettingsGoToTheRustSideUnderTheNameOfTheCommand", async () => {
  const { tauri, calls } = recorder({ load_settings: () => userSettings() });
  const desktop = await createTauriDesktop(tauri);

  desktop.settings.save(korean);
  await flush();

  assert.deepEqual(calls.at(-1), { command: "save_settings", args: { settings: korean } });
});

test("theFolderChooserStartsWhereTheScreenSaysAndHandsBackWhatWasChosen", async () => {
  const { tauri, calls } = recorder({ load_settings: () => userSettings(), pick_folder: () => "D:\\음악\\새 폴더" });
  const desktop = await createTauriDesktop(tauri);

  const chosen = await desktop.pickFolder("D:\\음악");

  assert.equal(chosen, "D:\\음악\\새 폴더");
  assert.deepEqual(calls.at(-1), { command: "pick_folder", args: { initial: "D:\\음악" } });
});

test("aCancelledFolderChooserHandsBackNothing", async () => {
  const { tauri } = recorder({ load_settings: () => userSettings(), pick_folder: () => null });
  const desktop = await createTauriDesktop(tauri);

  assert.equal(await desktop.pickFolder("D:\\음악"), null);
});

test("aFolderChooserThatFailsIsACancelledOne", async () => {
  const { tauri } = recorder({
    load_settings: () => userSettings(),
    pick_folder: () => {
      throw new Error("the dialog could not open");
    },
  });
  const desktop = await createTauriDesktop(tauri);

  assert.equal(await desktop.pickFolder("D:\\음악"), null);
});

test("showingAFolderIsAskedOfTheRustSideAndNeverFails", async () => {
  const { tauri, calls } = recorder({
    load_settings: () => userSettings(),
    open_folder: () => {
      throw new Error("explorer is gone");
    },
  });
  const desktop = await createTauriDesktop(tauri);

  desktop.openFolder("D:\\음악\\내 재생목록");
  await flush();

  assert.deepEqual(calls.at(-1), { command: "open_folder", args: { path: "D:\\음악\\내 재생목록" } });
});

// ---- closing the window ----

test("theRustSideIsToldThePageListensOnlyOnceTheListenerIsInPlace", async () => {
  const { tauri, names } = recorder({ load_settings: () => userSettings() });
  const desktop = await createTauriDesktop(tauri);

  await desktop.onClose(() => {});

  assert.deepEqual(names(), ["load_settings", `listen:${CLOSE_REQUESTED_EVENT}`, "page_listens_for_close"]);
});

test("whenTheWindowIsClosedTheOptionsAreWrittenFirstAndThenTheWindowIsClosed", async () => {
  const gate = new Deferred();
  const { tauri, calls, listeners } = recorder({
    load_settings: () => userSettings(),
    save_settings: () => gate.promise,
  });
  const desktop = await createTauriDesktop(tauri);
  await desktop.onClose(() => desktop.settings.save(korean));

  const closing = listeners.get(CLOSE_REQUESTED_EVENT)?.();
  await flush();
  assert.equal(calls.some((call) => call.command === "close_window"), false, "the options are still being written");

  gate.resolve();
  await closing;

  assert.deepEqual(calls.map((call) => call.command).slice(-2), ["save_settings", "close_window"]);
});

test("aWriteThatNeverEndsDoesNotKeepTheWindowOpenLongerThanTheLimit", async () => {
  const { tauri, calls, listeners } = recorder({
    load_settings: () => userSettings(),
    save_settings: () => new Promise(() => {}),
  });
  const desktop = await createTauriDesktop(tauri, { closeLimitMs: 30 });
  await desktop.onClose(() => desktop.settings.save(korean));

  await listeners.get(CLOSE_REQUESTED_EVENT)?.();

  assert.equal(calls.at(-1)?.command, "close_window");
});

test("theWindowIsClosedEvenWhenFinishingUpFails", async () => {
  const { tauri, calls, listeners } = recorder({ load_settings: () => userSettings() });
  const desktop = await createTauriDesktop(tauri);
  await desktop.onClose(() => {
    throw new Error("the screen is broken");
  });

  await listeners.get(CLOSE_REQUESTED_EVENT)?.();

  assert.equal(calls.at(-1)?.command, "close_window");
});

// ---- the desktop of the demo page ----

test("theDemoChooserHandsOutAnotherFolderEachTime", async () => {
  const demo = createDemoDesktop();

  const first = await demo.pickFolder("C:\\x");
  const second = await demo.pickFolder(first ?? "");

  assert.ok(first && second && first !== second);
  assert.match(first, /^C:\\Users\\사용자\\Music\\/);
});

test("theDemoRemembersWhatWasShownAndWhatWasSaved", () => {
  const demo = createDemoDesktop();

  demo.openFolder("C:\\a");
  demo.openFolder("C:\\b");
  demo.settings.save(korean);

  assert.deepEqual(demo.opened, ["C:\\a", "C:\\b"]);
  assert.deepEqual(demo.settings.load(), userSettings(), "what it hands out is what it started with: a page that is loaded again starts again");
  assert.deepEqual(demo.saved, [korean]);
});
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test tests/desktop.test.ts 2>&1 | Select-String "ERR_MODULE_NOT_FOUND|^ℹ (tests|pass|fail)"
```

Expected: `ERR_MODULE_NOT_FOUND`(`../src/desktop.ts`).

- [ ] **Step 3: 구현한다**

`shell/src/desktop.ts`:

```typescript
// The desktop around the screen: the folder chooser, the file manager, the settings file and the closing of the window. The screen
// knows only `Desktop`; the window of the app gets the one of `createTauriDesktop` (the commands of the Rust side), the demo page
// (`index.html?demo`) the one of `createDemoDesktop`.

import { userSettings, type SettingsStore, type UserSettings } from "./state/types.ts";

/** What the Rust side sends when the user closes the window. The page finishes up and answers with `close_window`. */
export const CLOSE_REQUESTED_EVENT = "xgs-close-requested";

/** How long the page waits for the options to be written when the window closes. The Rust side closes the window itself after 1.5 s. */
const CLOSE_LIMIT_MS = 1000;

export interface Desktop {
  /** The store the screen logic is given: what the settings file held when the page started, and writes of what changes. */
  readonly settings: SettingsStore;
  /** Opens the system's folder chooser starting in `initial` (when that is a folder). The chosen path, or null when cancelled. */
  pickFolder(initial: string): Promise<string | null>;
  /** Shows the folder (the nearest one that exists) in the file manager. Returns at once; never fails. */
  openFolder(path: string): void;
  /** Runs `beforeClose` when the user closes the window, waits for the options to be written, and lets the window close. */
  onClose(beforeClose: () => void): Promise<void>;
}

/** The two things of the Tauri API the desktop uses, so that the tests can stand in for them. */
export interface TauriLike {
  invoke<T>(command: string, args?: Record<string, unknown>): Promise<T>;
  listen(event: string, handler: () => Promise<void> | void): Promise<unknown>;
}

export interface PendingSettingsStore extends SettingsStore {
  /** Resolves when every save made so far has been written (or has failed). */
  whenSaved(): Promise<void>;
}

/** The settings as the Rust side hands them over: all four keys with the right types, else the defaults. */
export function normalizeSettings(raw: unknown): UserSettings {
  if (typeof raw !== "object" || raw === null || Array.isArray(raw)) return userSettings();
  const value = raw as Record<string, unknown>;
  const { outputDir, overwrite, includeRank, searchLyricsOnline } = value;
  if (
    !(outputDir === null || typeof outputDir === "string") ||
    typeof overwrite !== "boolean" ||
    typeof includeRank !== "boolean" ||
    typeof searchLyricsOnline !== "boolean"
  ) {
    return userSettings();
  }
  return { outputDir, overwrite, includeRank, searchLyricsOnline };
}

/**
 * `initial` is what `load` hands out. Every `save` is written by `write`, one at a time and in the order of the saves (two writes
 * at once could end with the older one). A write that fails is nobody's business: the options are then not kept.
 */
export function createSettingsStore(initial: UserSettings, write: (settings: UserSettings) => Promise<unknown>): PendingSettingsStore {
  let pending: Promise<void> = Promise.resolve();
  return {
    load: () => initial,
    save(settings) {
      pending = pending.then(() => write(settings)).then(
        () => {},
        () => {},
      );
    },
    whenSaved: () => pending,
  };
}

/** Resolves when `promise` settles, or after `ms` at the latest. */
function atMost(promise: Promise<unknown>, ms: number): Promise<void> {
  return new Promise((resolve) => {
    const timer = setTimeout(resolve, ms);
    const done = () => {
      clearTimeout(timer);
      resolve();
    };
    promise.then(done, done);
  });
}

/** The desktop of the window of the app. Loads the settings file first, so that the screen can be built with them. Never throws. */
export async function createTauriDesktop(tauri: TauriLike, { closeLimitMs = CLOSE_LIMIT_MS }: { closeLimitMs?: number } = {}): Promise<Desktop> {
  let stored = userSettings();
  try {
    stored = normalizeSettings(await tauri.invoke("load_settings"));
  } catch {
    // The file cannot be read: the defaults, as when there is none.
  }
  const settings = createSettingsStore(stored, (changed) => tauri.invoke("save_settings", { settings: changed }));
  return {
    settings,
    async pickFolder(initial) {
      try {
        return (await tauri.invoke<string | null>("pick_folder", { initial })) ?? null;
      } catch {
        return null;
      }
    },
    openFolder(path) {
      void Promise.resolve()
        .then(() => tauri.invoke("open_folder", { path }))
        .catch(() => {});
    },
    async onClose(beforeClose) {
      await tauri.listen(CLOSE_REQUESTED_EVENT, async () => {
        try {
          beforeClose();
          await atMost(settings.whenSaved(), closeLimitMs);
        } catch {
          // Finishing up failed: the window closes all the same.
        }
        await tauri.invoke("close_window").catch(() => {});
      });
      await tauri.invoke("page_listens_for_close");
    },
  };
}

export interface DemoDesktop extends Desktop {
  /** The folders the screen asked to be shown, in order. */
  readonly opened: string[];
  /** The settings the screen asked to be saved, in order. */
  readonly saved: UserSettings[];
}

/** A desktop for the page without a window of the app: the chooser hands out made-up folders and nothing leaves the page. */
export function createDemoDesktop(): DemoDesktop {
  const opened: string[] = [];
  const saved: UserSettings[] = [];
  let chosen = 0;
  return {
    opened,
    saved,
    settings: { load: () => userSettings(), save: (settings) => void saved.push(settings) },
    async pickFolder() {
      chosen += 1;
      return `C:\\Users\\사용자\\Music\\선택한 폴더 ${chosen}`;
    },
    openFolder: (path) => void opened.push(path),
    async onClose() {},
  };
}
```

`shell/src/desktop-tauri.ts`:

```typescript
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { createTauriDesktop } from "./desktop.ts";

/** The desktop of this window: the Tauri commands of the Rust side and the events it sends. */
export const createRealDesktop = () => createTauriDesktop({ invoke, listen: (event, handler) => listen(event, () => void handler()) });
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test tests/desktop.test.ts 2>&1 | Select-String "✖|^ℹ (tests|pass|fail)"; npx tsc --noEmit; "tsc exit $LASTEXITCODE"
```

Expected: `ℹ tests 20`, `ℹ pass 20`, `ℹ fail 0`, `tsc exit 0`.

- [ ] **Step 5: 원본 이름 점검을 Rust 포트까지 넓힌다**

`shell/tests/kotlin-coverage.test.ts`를 바꾼다(Kotlin의 `null`·`Throw`는 Rust의 `None`·`panic`이고 한 글자 단어 `A`도 단어로 센다):

```diff
diff --git a/shell/tests/kotlin-coverage.test.ts b/shell/tests/kotlin-coverage.test.ts
index 4a11f4c..3b94b93 100644
--- a/shell/tests/kotlin-coverage.test.ts
+++ b/shell/tests/kotlin-coverage.test.ts
@@ -1,5 +1,6 @@
-// Guards the port: every test of the four Kotlin suites that these files were ported from has a test of the same name here.
-// The Kotlin files are the specification; when one of them gains a test, this fails until the port gains it too.
+// Guards the port: every test of the Kotlin suites that these files were ported from has a test of the same name here (the Rust
+// ports under the same name in snake case). The Kotlin files are the specification; when one of them gains a test, this fails until
+// the port gains it too.
 //
 // It looks the Kotlin files up in the repository. When `app/` is gone (stage 6 of the migration deletes it) the app's suites are
 // skipped; `shared/` stays, so the two suites of `shared` keep being compared.
@@ -10,6 +11,11 @@ import test from "node:test";
 
 const repository = new URL("../../", import.meta.url);
 
+/** The names of the tests of a Kotlin file: `@Test fun name(`. */
+function kotlinTests(path: URL): string[] {
+  return [...readFileSync(path, "utf8").matchAll(/@Test\s+fun\s+(\w+)\(/g)].map((match) => match[1]);
+}
+
 const PORTS: ReadonlyArray<readonly [kotlin: string, port: string]> = [
   ["shared/src/commonTest/kotlin/com/xgetsongs/shared/filename/FilenameFormatterTest.kt", "filename.test.ts"],
   ["shared/src/commonTest/kotlin/com/xgetsongs/shared/input/InputClassifierTest.kt", "input.test.ts"],
@@ -22,7 +28,7 @@ for (const [kotlin, port] of PORTS) {
   // Only the app's suites may be absent (stage 6 deletes `app/`). A missing `shared` file means it was moved or renamed: fail.
   const skip = kotlin.startsWith("app/") && !existsSync(kotlinPath) && "the Kotlin file is not in this checkout";
   test(`every test of ${kotlin.split("/").pop()} is ported to ${port}`, { skip }, () => {
-    const names = [...readFileSync(kotlinPath, "utf8").matchAll(/@Test\s+fun\s+(\w+)\(/g)].map((match) => match[1]);
+    const names = kotlinTests(kotlinPath);
     const ported = new Set([...readFileSync(new URL(port, import.meta.url), "utf8").matchAll(/^test\("(\w+)"/gm)].map((match) => match[1]));
 
     assert.ok(names.length > 0, "the Kotlin file has tests");
@@ -33,3 +39,47 @@ for (const [kotlin, port] of PORTS) {
     );
   });
 }
+
+// The desktop code that moved to Rust (stage 4): the tests of the Kotlin files are ported to `#[test] fn` of the same name in snake
+// case (`aMissingFileGivesTheDefaults` is `a_missing_file_gives_the_defaults`), where Kotlin's `null` is Rust's `None` and what
+// Kotlin does not `throw` Rust does not `panic` at.
+const RUST_PORTS: ReadonlyArray<readonly [kotlin: string, rust: string]> = [
+  ["app/src/desktopTest/kotlin/com/xgetsongs/app/ExplorerOpenerTest.kt", "sidecar-host/src/folder.rs"],
+  ["app/src/desktopTest/kotlin/com/xgetsongs/app/JsonSettingsStoreTest.kt", "sidecar-host/src/settings.rs"],
+];
+
+/** `aPathBelowARegularFile` is `a_path_below_a_regular_file`; a word of one letter (`A`) is a word. */
+const snakeCase = (name: string) =>
+  name
+    .replace(/([a-z0-9])([A-Z])/g, "$1_$2")
+    .replace(/([A-Z])([A-Z][a-z])/g, "$1_$2")
+    .toLowerCase();
+const rustName = (name: string) =>
+  snakeCase(name)
+    .split("_")
+    .map((word) => (word === "null" ? "none" : word === "throw" ? "panic" : word))
+    .join("_");
+
+for (const [kotlin, rust] of RUST_PORTS) {
+  const kotlinPath = new URL(kotlin, repository);
+  const skip = !existsSync(kotlinPath) && "the Kotlin file is not in this checkout";
+  test(`every test of ${kotlin.split("/").pop()} is ported to ${rust}`, { skip }, () => {
+    const names = kotlinTests(kotlinPath);
+    const ported = new Set([...readFileSync(new URL(rust, repository), "utf8").matchAll(/^\s*fn\s+(\w+)\s*\(/gm)].map((match) => match[1]));
+
+    assert.ok(names.length > 0, "the Kotlin file has tests");
+    assert.deepEqual(
+      names.filter((name) => !ported.has(rustName(name))),
+      [],
+      `ported ${names.filter((name) => ported.has(rustName(name))).length} of ${names.length}`,
+    );
+  });
+}
+
+test("theNamesOfTheKotlinTestsBecomeTheNamesOfTheRustOnes", () => {
+  assert.equal(rustName("aMissingFileGivesTheDefaults"), "a_missing_file_gives_the_defaults");
+  assert.equal(rustName("aUtf16FileGivesTheDefaults"), "a_utf16_file_gives_the_defaults");
+  assert.equal(rustName("aPathBelowARegularFileReturnsTheDirectoryAroundTheFile"), "a_path_below_a_regular_file_returns_the_directory_around_the_file");
+  assert.equal(rustName("aBlankPathReturnsNull"), "a_blank_path_returns_none");
+  assert.equal(rustName("saveOverADirectoryThatIsNotEmptyDoesNotThrow"), "save_over_a_directory_that_is_not_empty_does_not_panic");
+});
```

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test tests/kotlin-coverage.test.ts 2>&1 | Select-String "✔|✖|^ℹ (tests|pass|fail|skipped)"
```

Expected: 일곱 개가 모두 ✔(`FilenameFormatterTest`, `InputClassifierTest`, `LabelsTest`, `AppStateHolderTest`, `ExplorerOpenerTest`→`folder.rs`, `JsonSettingsStoreTest`→`settings.rs`, 이름 규칙), `ℹ tests 7`, `ℹ pass 7`.

점검이 무는지 본다: `sidecar-host/src/folder.rs`의 `fn a_blank_path_returns_none`을 `fn a_blank_path_returns_nothing`으로 잠시 바꾸고 위 명령을 다시 돌린다. Expected: `ExplorerOpenerTest.kt` 점검이 FAIL하고 `aBlankPathReturnsNull`이 빠졌다고 말한다. 확인한 뒤 **이름을 되돌린다**.

- [ ] **Step 6: 전체를 돌린다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test "tests/*.test.ts" 2>&1 | Select-String "✖|^ℹ (tests|pass|fail|skipped)"
```

Expected: `ℹ tests 239`, `ℹ pass 239`, `ℹ fail 0`, `ℹ skipped 0`(3단계의 216개 + `desktop` 20 + 점검 3).

- [ ] **Step 7: Commit**

```bash
git add shell/src/desktop.ts shell/src/desktop-tauri.ts shell/tests/desktop.test.ts shell/tests/kotlin-coverage.test.ts
git commit -m "feat(shell): 화면이 아는 데스크톱(폴더 선택, 탐색기, 설정, 닫기)을 더하고 Rust 포트의 원본 이름 점검을 더한다"
```

---

### Task 7: 화면에 연결한다

**Files:**
- Modify: `shell/index.html`, `shell/src/app.css`, `shell/src/main.ts`

**Interfaces:**
- Consumes: Task 6의 `Desktop`, `createDemoDesktop`, `createRealDesktop`; 3단계의 `AppStateHolder`(`onOutputDir`, `flushSettings`, 생성자의 `settings`), `destinationPath`.
- Produces: 화면. `#pick-folder`는 항상 보이고(작업 중에는 비활성), `#open-folder`는 저장 위치가 있을 때 보이며 작업 중에도 활성이다. 옵션은 `#options`(출력 폴더 줄)와 `#options-more`(앨범명, 체크 상자, 순위 번호) 두 `fieldset`이고 저장 위치 줄이 그 사이에 있다. 부팅할 때 설정을 읽어 holder에 주고(`?demo`는 `window.xgsDemo`에 가짜 데스크톱을 둔다), 창이 닫히면 `holder.flushSettings()`한다.

- [ ] **Step 1: 개발 서버를 띄우고 시나리오 F가 지금 코드에서 실패하는지 본다**

개발 서버는 오래 도는 프로세스이므로 PowerShell 도구의 백그라운드로 띄우고 출력은 파일로 보낸다(출력을 `Select-Object -First`로 자르면 서버가 같이 끝난다):

```powershell
New-Item -ItemType Directory -Force "$env:TEMP\xgs-bridge-check" | Out-Null; Set-Location C:\Projects\xGetSongs\shell; npx vite *> "$env:TEMP\xgs-bridge-check\vite.log"
```

내장 브라우저 도구(`mcp__Claude_Browser__*`)로 **새 탭을 만들어**(편집한 파일이 있던 탭은 파일 보기로 바뀔 수 있다) `http://localhost:1420/?demo&speed=1`을 열고 `javascript_tool`로 시나리오 F를 실행한다(`window.xgsDemo`의 `opened`, `saved`를 본다. 잠금은 `:disabled`로 본다: `fieldset`에서 상속된 비활성은 `.disabled` 속성에 나타나지 않는다):

```javascript
(async () => {
  // Run on http://localhost:1420/?demo&speed=1 right after it loaded. The folder chooser and "show in Explorer" of the screen, on the demo
  // desktop (window.xgsDemo): the chooser hands out made-up folders, what is shown and what is saved is recorded.
  const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const $ = (selector) => document.querySelector(selector);
  const type = (element, value) => { element.value = value; element.dispatchEvent(new Event("input", { bubbles: true })); };
  const buttons = () => [...document.querySelectorAll("#actions button")].map((element) => element.textContent);
  const locked = (selector) => $(selector).matches(":disabled"); // a field inside a disabled fieldset is :disabled although its own property says false
  const failures = [];
  const expect = (name, actual, expected) => { if (JSON.stringify(actual) !== JSON.stringify(expected)) failures.push(`${name}: got ${JSON.stringify(actual)}, wanted ${JSON.stringify(expected)}`); };

  await wait(500);
  type($("#input"), "https://www.youtube.com/playlist?list=PLabcdefghijklmnop");
  $("#resolve").click();
  await wait(1200);
  expect("the chooser button is there", [$("#pick-folder").hidden, locked("#pick-folder")], [false, false]);
  expect("show-in-Explorer is there once there is a destination", [$("#destination-row").hidden, $("#open-folder").hidden], [false, false]);

  $("#pick-folder").click();
  await wait(100);
  expect("the chosen folder is in the box", $("#output-dir").value, "C:\\Users\\사용자\\Music\\선택한 폴더 1");
  expect("and the destination follows", $("#destination").textContent, "저장 위치: C:\\Users\\사용자\\Music\\선택한 폴더 1\\Melon Daily Top 100");
  await wait(600);
  expect("the options are saved once, after the quiet period, with the chosen folder", (window.xgsDemo?.saved ?? []).map((saved) => saved.outputDir), ["C:\\Users\\사용자\\Music\\선택한 폴더 1"]);

  $("#open-folder").click();
  expect("show-in-Explorer is asked for the destination", window.xgsDemo?.opened, ["C:\\Users\\사용자\\Music\\선택한 폴더 1\\Melon Daily Top 100"]);

  // while a job runs the chooser and the boxes are locked, but looking at the folder is still possible
  [...document.querySelectorAll("#actions button")].find((candidate) => candidate.textContent === "다운로드 시작").click();
  await wait(300);
  expect("running: the chooser and the boxes are locked", ["#pick-folder", "#output-dir", "#album", "#opt-overwrite", "#options", "#options-more"].map(locked), [true, true, true, true, true, true]);
  expect("running: show-in-Explorer is not", locked("#open-folder"), false);
  $("#open-folder").click();
  expect("running: and it works", window.xgsDemo?.opened.length, 2);

  [...document.querySelectorAll("#actions button")].find((candidate) => candidate.textContent === "취소").click();
  const started = Date.now();
  while (!buttons().includes("다시 다운로드") && Date.now() - started < 30000) await wait(100);
  expect("cancelled: the chooser works again", ["#pick-folder", "#options", "#options-more"].map(locked), [false, false, false]);

  // a second pick is another folder; the typed text can be replaced by it
  type($("#output-dir"), "D:\\typed");
  $("#pick-folder").click();
  await wait(100);
  expect("a second choice replaces the typed folder", $("#output-dir").value, "C:\\Users\\사용자\\Music\\선택한 폴더 2");

  // a single video saves into the output folder itself, and that is what is shown
  [...document.querySelectorAll("#actions button")].find((candidate) => candidate.textContent === "새로 시작").click();
  expect("without a preview there is nothing to show", $("#destination-row").hidden, true);

  return JSON.stringify({ pass: failures.length === 0, failures });
})()
```

Expected(지금 코드): `{"pass":false,...}`. 단추가 숨겨져 있고 눌러도 아무 일이 없으므로 "the chooser button is there", "the chosen folder is in the box" 같은 항목이 실패한다.

- [ ] **Step 2: 화면을 바꾼다**

`shell/index.html`(옵션을 두 `fieldset`으로 나누고 저장 위치 줄을 사이에 둔다):

```diff
diff --git a/shell/index.html b/shell/index.html
index 8c25fb4..a844b48 100644
--- a/shell/index.html
+++ b/shell/index.html
@@ -31,38 +31,43 @@
           <button id="video-only" type="button" hidden>이 영상만 받기</button>
         </div>
 
-        <fieldset id="options" class="options">
-          <div class="row-flex align-end">
-            <label class="field grow">
-              <span class="field-label">출력 폴더</span>
-              <input id="output-dir" type="text" spellcheck="false" />
-            </label>
-            <!-- The folder chooser and "show in Explorer" come with stage 4 of the migration. -->
-            <button id="pick-folder" type="button" hidden>폴더 선택</button>
-          </div>
+        <!-- The options: two fieldsets, which a job disables, with the line that shows the destination between them (its button
+             stays usable while a job runs, which a disabled fieldset would not allow). -->
+        <div class="options-group">
+          <fieldset id="options" class="options">
+            <div class="row-flex align-end">
+              <label class="field grow">
+                <span class="field-label">출력 폴더</span>
+                <input id="output-dir" type="text" spellcheck="false" />
+              </label>
+              <button id="pick-folder" type="button">폴더 선택</button>
+            </div>
+          </fieldset>
           <div id="destination-row" class="row-flex" hidden>
             <span id="destination" class="small grow ellipsis"></span>
             <button id="open-folder" type="button" class="small-button" hidden>탐색기에서 보기</button>
           </div>
-          <label class="field">
-            <span class="field-label">앨범명 (폴더명)</span>
-            <input id="album" type="text" spellcheck="false" />
-            <span class="hint">저장 폴더 이름과 앨범 태그가 모두 이 이름입니다. 비우면 재생목록 제목(영상 1개는 폴더 없이 영상 자체의 앨범)</span>
-          </label>
-          <div class="checks">
-            <label class="check"><input id="opt-overwrite" type="checkbox" /> 기존 파일 덮어쓰기</label>
-            <label class="check"><input id="opt-rank" type="checkbox" /> 파일명에 순번 포함</label>
-            <label class="check"><input id="opt-lyrics" type="checkbox" /> 가사가 없으면 인터넷에서 검색</label>
-          </div>
-          <div class="row-flex align-end">
-            <span class="concurrency-label">동시 다운로드</span>
-            <span>코어 수의 70% (자동)</span>
-            <label id="rank-field" class="field rank-field" hidden>
-              <span class="field-label">순위 번호</span>
-              <input id="single-rank" type="text" inputmode="numeric" autocomplete="off" />
+          <fieldset id="options-more" class="options">
+            <label class="field">
+              <span class="field-label">앨범명 (폴더명)</span>
+              <input id="album" type="text" spellcheck="false" />
+              <span class="hint">저장 폴더 이름과 앨범 태그가 모두 이 이름입니다. 비우면 재생목록 제목(영상 1개는 폴더 없이 영상 자체의 앨범)</span>
             </label>
-          </div>
-        </fieldset>
+            <div class="checks">
+              <label class="check"><input id="opt-overwrite" type="checkbox" /> 기존 파일 덮어쓰기</label>
+              <label class="check"><input id="opt-rank" type="checkbox" /> 파일명에 순번 포함</label>
+              <label class="check"><input id="opt-lyrics" type="checkbox" /> 가사가 없으면 인터넷에서 검색</label>
+            </div>
+            <div class="row-flex align-end">
+              <span class="concurrency-label">동시 다운로드</span>
+              <span>코어 수의 70% (자동)</span>
+              <label id="rank-field" class="field rank-field" hidden>
+                <span class="field-label">순위 번호</span>
+                <input id="single-rank" type="text" inputmode="numeric" autocomplete="off" />
+              </label>
+            </div>
+          </fieldset>
+        </div>
 
         <ul id="list" class="list" aria-label="곡 목록"></ul>
         <div id="actions" class="row-flex actions"></div>
```

`shell/src/app.css`:

```diff
diff --git a/shell/src/app.css b/shell/src/app.css
index 4013f13..b5f0c49 100644
--- a/shell/src/app.css
+++ b/shell/src/app.css
@@ -217,7 +217,8 @@ fieldset {
   min-height: 0;
 }
 
-.options {
+.options,
+.options-group {
   display: flex;
   flex-direction: column;
   gap: 8px;
```

`shell/src/main.ts`(`mount`가 `Desktop`을 받고, 두 단추를 연결하고, 부팅할 때 설정을 읽고, 닫기에 응답한다):

```diff
diff --git a/shell/src/main.ts b/shell/src/main.ts
index 8f7289a..960be94 100644
--- a/shell/src/main.ts
+++ b/shell/src/main.ts
@@ -4,6 +4,7 @@
 
 import "./app.css";
 import { createDemoApi } from "./demo.ts";
+import { createDemoDesktop, type Desktop } from "./desktop.ts";
 import { trim } from "./logic/text.ts";
 import { AppStateHolder } from "./state/holder.ts";
 import {
@@ -11,6 +12,7 @@ import {
   UPDATE_YT_DLP_LABEL,
   albumNameText,
   destinationLabel,
+  destinationPath,
   rankFromInput,
   rankInputText,
   rankLabel,
@@ -49,7 +51,7 @@ interface RowView {
   bar: HTMLProgressElement;
 }
 
-function mount(holder: AppStateHolder): void {
+function mount(holder: AppStateHolder, desktop: Desktop): void {
   const toolsCard = $<HTMLElement>("#tools");
   const form = $<HTMLFormElement>("#input-form");
   const input = $<HTMLInputElement>("#input");
@@ -61,10 +63,15 @@ function mount(holder: AppStateHolder): void {
   const infoTitle = $<HTMLElement>("#info-title");
   const truncated = $<HTMLElement>("#truncated");
   const videoOnly = $<HTMLButtonElement>("#video-only");
+  // The options are two fieldsets with the line that shows the destination between them: that line's button stays usable while a
+  // job runs (looking at the folder is harmless), which a disabled fieldset would not allow.
   const options = $<HTMLFieldSetElement>("#options");
+  const optionsMore = $<HTMLFieldSetElement>("#options-more");
   const outputDir = $<HTMLInputElement>("#output-dir");
+  const pickFolder = $<HTMLButtonElement>("#pick-folder");
   const destinationRow = $<HTMLElement>("#destination-row");
   const destination = $<HTMLElement>("#destination");
+  const openFolder = $<HTMLButtonElement>("#open-folder");
   const album = $<HTMLInputElement>("#album");
   const optOverwrite = $<HTMLInputElement>("#opt-overwrite");
   const optRank = $<HTMLInputElement>("#opt-rank");
@@ -264,6 +271,7 @@ function mount(holder: AppStateHolder): void {
     }
 
     options.disabled = state.phase === "RUNNING";
+    optionsMore.disabled = state.phase === "RUNNING";
     showText(outputDir, state.outputDir);
     showText(album, albumNameText(state));
     optOverwrite.checked = state.overwrite;
@@ -272,6 +280,7 @@ function mount(holder: AppStateHolder): void {
     const label = destinationLabel(state);
     destinationRow.hidden = label === null;
     setText(destination, label ?? "");
+    openFolder.hidden = destinationPath(state) === null;
     rankField.hidden = resolved?.kind !== "VIDEO";
     // The raw text stays while the user retypes the number (the box may even be empty). Like Compose's `LaunchedEffect(singleRank)`
     // the box is looked at only when the rank itself changed (or the box was just shown), and rewritten only if it says another rank.
@@ -310,6 +319,15 @@ function mount(holder: AppStateHolder): void {
     if (!composing.has(singleRank)) filterRank();
   });
   singleRank.addEventListener("compositionend", filterRank);
+  pickFolder.addEventListener("click", () => {
+    void desktop.pickFolder(holder.getState().outputDir).then((chosen) => {
+      if (chosen !== null) holder.onOutputDir(chosen);
+    });
+  });
+  openFolder.addEventListener("click", () => {
+    const path = destinationPath(holder.getState());
+    if (path !== null) desktop.openFolder(path);
+  });
   videoOnly.addEventListener("click", () => holder.switchToVideoOnly());
   errorClose.addEventListener("click", () => holder.dismissError());
 
@@ -322,6 +340,9 @@ function mount(holder: AppStateHolder): void {
   holder.subscribe(render);
   render(holder.getState());
   holder.refreshTools();
+  // The options that still wait for their quiet period are written before the window closes (Compose: `holder.flushSettings()` in
+  // `onCloseRequest`).
+  void desktop.onClose(() => holder.flushSettings());
 }
 
 // ---- start ----------------------------------------------------------------------------------
@@ -340,16 +361,22 @@ async function boot(): Promise<void> {
   const params = new URLSearchParams(location.search);
   const demo = params.get("demo");
   let api: XgsApi;
+  let desktop: Desktop;
   let defaultOutputDir: string;
   if (demo !== null) {
     // `?demo=broken` has no tools; `&speed=30` makes the made-up downloads 30 times faster.
     api = createDemoApi({ tools: demo === "broken" ? "broken" : "ok", speed: Number(params.get("speed")) || 1 });
+    const demoDesktop = createDemoDesktop();
+    // What the demo desktop was asked to do can be looked at (the browser scenarios of the developer tooling do).
+    (window as unknown as { xgsDemo: unknown }).xgsDemo = demoDesktop;
+    desktop = demoDesktop;
     defaultOutputDir = "C:\\Users\\사용자\\Music\\xGetSongs";
   } else {
     api = (await import("./bridge/tauri.ts")).bridge;
+    desktop = await (await import("./desktop-tauri.ts")).createRealDesktop();
     defaultOutputDir = await defaultOutputDirectory();
   }
-  mount(new AppStateHolder({ api, defaultOutputDir }));
+  mount(new AppStateHolder({ api, defaultOutputDir, settings: desktop.settings }), desktop);
   // The developer tooling uses the screen like a person does (see check-screen.ts); a normal start has no `?check`.
   if (params.has("check")) void import("./check-screen.ts").then((module) => module.runScreenChecks());
 }
```

- [ ] **Step 3: 컴파일하고 시험하고 빌드한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; npx tsc --noEmit; "tsc exit $LASTEXITCODE"; node --test "tests/*.test.ts" 2>&1 | Select-String "✖|^ℹ (tests|pass|fail)"; npm run build 2>&1 | Select-Object -Last 5
```

Expected: `tsc exit 0`, `ℹ tests 239`, `ℹ pass 239`, `ℹ fail 0`, vite 빌드 성공.

- [ ] **Step 4: 시나리오 F가 통과하는지 본다**

같은 탭을 다시 불러(`navigate`) 시나리오 F를 실행한다. Expected: `{"pass":true,"failures":[]}`.

- [ ] **Step 5: 회귀: 시나리오 A~E**

3단계의 시나리오에 리뷰가 지적한 것의 시나리오(E)를 더한 다섯 개가 새 HTML 구조에서도 통과해야 한다. A, B, C는 `?demo&speed=40`을 열고 **이 순서로** 같은 페이지에서 잇달아 실행한다(A가 재생목록을 조회하고 B가 그것을 받고 C가 영상 한 곡을 본다). D는 `?demo=broken`, E는 `?demo&speed=40`을 새로 연다. 각각 `{"pass":true,"failures":[]}`가 Expected다.

A(조회와 옵션):

```javascript
(async () => {
  // Run on http://localhost:1420/?demo&speed=40 right after it loaded. Looks a playlist up and changes the options.
  const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const $ = (selector) => document.querySelector(selector);
  const type = (element, value) => { element.value = value; element.dispatchEvent(new Event("input", { bubbles: true })); };
  const labels = () => [...document.querySelectorAll("#list .label")].map((element) => element.textContent);
  const buttons = () => [...document.querySelectorAll("#actions button")].map((element) => element.textContent);
  const failures = [];
  const expect = (name, actual, expected) => { if (JSON.stringify(actual) !== JSON.stringify(expected)) failures.push(`${name}: got ${JSON.stringify(actual)}, wanted ${JSON.stringify(expected)}`); };

  await wait(500);
  expect("tools", $("#tools").innerText.split("\n").slice(0, 3).join(" | "), "yt-dlp 2026.10.01 ✓ | ffmpeg 8.1 ✓ | JS 런타임 24.11.1 ✓");
  expect("resolve disabled while the box is empty", $("#resolve").disabled, true);
  type($("#input"), "https://www.youtube.com/playlist?list=PLabcdefghijklmnop");
  expect("resolve enabled once there is text", $("#resolve").disabled, false);
  $("#resolve").click();
  expect("resolve disabled while looking up", $("#resolve").disabled, true);
  await wait(1200);

  expect("rows", labels().length, 100);
  expect("ready", labels().filter((label) => label === "준비됨").length, 98);
  expect("skipped", labels().filter((label) => label.startsWith("건너뜀")).length, 2);
  expect("buttons", buttons(), ["다운로드 시작", "새로 시작"]);
  expect("title", $("#info-title").textContent, "Melon Daily Top 100 · 100개");
  expect("video-only hidden", $("#video-only").hidden, true);
  expect("rank field hidden", $("#rank-field").hidden, true);
  expect("options", [$("#opt-overwrite").checked, $("#opt-rank").checked, $("#opt-lyrics").checked], [false, true, true]);
  expect("album shows the title", $("#album").value, "Melon Daily Top 100");
  expect("output folder", $("#output-dir").value, "C:\\Users\\사용자\\Music\\xGetSongs");
  expect("destination", $("#destination").textContent, "저장 위치: C:\\Users\\사용자\\Music\\xGetSongs\\Melon Daily Top 100");
  expect("first file", $("#list .file").textContent, "001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
  expect("a guessed artist is warned about", document.querySelectorAll("#list .warn").length > 0, true);
  expect("options enabled", $("#options").disabled, false);

  $("#opt-rank").click();
  expect("rank off rewrites the names", $("#list .file").textContent, "소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");
  $("#opt-rank").click();
  expect("rank on again", $("#list .file").textContent, "001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a");

  type($("#album"), "내 앨범");
  expect("a typed album names the folder", $("#destination").textContent, "저장 위치: C:\\Users\\사용자\\Music\\xGetSongs\\내 앨범");
  type($("#album"), "");
  expect("a cleared album is the default folder", $("#destination").textContent, "저장 위치: C:\\Users\\사용자\\Music\\xGetSongs\\Melon Daily Top 100");
  expect("a cleared album field stays empty", $("#album").value, "");

  return JSON.stringify({ pass: failures.length === 0, failures });
})()
```

B(시작, 취소, 다시, 실패 재시도, 새로 시작):

```javascript
(async () => {
  // Run right after a-resolve.js (the playlist is resolved on the page). Starts, cancels, runs to the end, retries, starts over.
  const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const $ = (selector) => document.querySelector(selector);
  const labels = () => [...document.querySelectorAll("#list .label")].map((element) => element.textContent);
  const count = (test) => labels().filter(test).length;
  const buttons = () => [...document.querySelectorAll("#actions button")].map((element) => element.textContent);
  const click = (text) => { const found = [...document.querySelectorAll("#actions button")].find((candidate) => candidate.textContent === text); if (!found) throw new Error(`no button ${text} among ${buttons()}`); found.click(); };
  const until = async (test, ms = 90000) => { const started = Date.now(); while (!test()) { if (Date.now() - started > ms) throw new Error("timeout; buttons=" + buttons()); await wait(100); } };
  const failures = [];
  const expect = (name, actual, expected) => { if (JSON.stringify(actual) !== JSON.stringify(expected)) failures.push(`${name}: got ${JSON.stringify(actual)}, wanted ${JSON.stringify(expected)}`); };
  const summary = () => ($("#actions .summary") || {}).textContent ?? null;

  click("다운로드 시작");
  expect("just started: buttons", buttons(), ["취소"]);
  expect("just started: everything waits", count((label) => label === "대기 중"), 98);
  expect("the options are locked while it runs", $("#options").disabled, true);
  await until(() => count((label) => label.startsWith("다운로드")) > 0);
  expect("eight run at once", count((label) => label.startsWith("다운로드")), 8);
  expect("each running song has a progress bar", document.querySelectorAll("#list progress:not([hidden])").length, 8);

  click("취소");
  await until(() => buttons().includes("다시 다운로드"));
  expect("cancelled: buttons", buttons(), ["다시 다운로드", "새로 시작"]);
  expect("cancelled: the summary says so", summary()?.startsWith("취소됨 — 성공 0 · 건너뜀 0 · 실패 0"), true);
  expect("cancelled: unfinished rows are ready again", count((label) => label === "준비됨"), 98);
  expect("cancelled: the options are free again", $("#options").disabled, false);

  click("다시 다운로드");
  await until(() => buttons().includes("실패 항목 재시도"));
  expect("finished: 96 done", count((label) => label.startsWith("완료")), 96);
  expect("finished: the two failures say why", labels().filter((label) => label.startsWith("실패")), ["실패: m4a 오디오 형식이 없습니다.", "실패: 태그를 쓰지 못했습니다. (지원하지 않는 m4a 구조)"]);
  expect("finished: the summary", summary()?.startsWith("완료 — 성공 96 · 건너뜀 0 · 실패 2 · 소요 "), true);
  expect("finished: buttons", buttons(), ["실패 항목 재시도", "다시 다운로드", "새로 시작"]);

  click("실패 항목 재시도");
  await until(() => !buttons().includes("실패 항목 재시도") && buttons().includes("새로 시작"));
  expect("retried: all 98 done", count((label) => label.startsWith("완료")), 98);
  expect("retried: only the retry is counted", summary()?.startsWith("완료 — 성공 2 · 건너뜀 0 · 실패 0"), true);

  click("새로 시작");
  expect("started over: nothing resolved", $("#resolved").hidden, true);
  expect("started over: the address box is empty", $("#input").value, "");
  expect("started over: no buttons", buttons(), []);

  return JSON.stringify({ pass: failures.length === 0, failures });
})()
```

C(영상 한 곡, 순위, 잘못된 주소):

```javascript
(async () => {
  // Run right after b-job.js (the screen is empty). A watch address with a list, "this video only", the rank of one video.
  const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const $ = (selector) => document.querySelector(selector);
  const type = (element, value) => { element.value = value; element.dispatchEvent(new Event("input", { bubbles: true })); };
  const labels = () => [...document.querySelectorAll("#list .label")].map((element) => element.textContent);
  const buttons = () => [...document.querySelectorAll("#actions button")].map((element) => element.textContent);
  const click = (text) => { const found = [...document.querySelectorAll("#actions button")].find((candidate) => candidate.textContent === text); if (!found) throw new Error(`no button ${text} among ${buttons()}`); found.click(); };
  const failures = [];
  const expect = (name, actual, expected) => { if (JSON.stringify(actual) !== JSON.stringify(expected)) failures.push(`${name}: got ${JSON.stringify(actual)}, wanted ${JSON.stringify(expected)}`); };

  type($("#input"), "https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=PLabcdefghijklmnop");
  $("#resolve").click();
  await wait(1200);
  expect("a watch address with a list is the playlist", $("#info-title").textContent, "Melon Daily Top 100 · 100개");
  expect("and offers the one video", $("#video-only").hidden, false);
  expect("no rank box for a playlist", $("#rank-field").hidden, true);

  $("#video-only").click();
  await wait(1200);
  expect("this video only: the address becomes the video's", $("#input").value, "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
  expect("this video only: one row", labels().length, 1);
  expect("this video only: the title", $("#info-title").textContent, "단일 영상");
  expect("this video only: the rank box shows 1", [$("#rank-field").hidden, $("#single-rank").value], [false, "1"]);
  expect("this video only: saved in the output folder itself", $("#destination").textContent, "저장 위치: C:\\Users\\사용자\\Music\\xGetSongs");
  expect("this video only: the shortcut is gone", $("#video-only").hidden, true);
  expect("this video only: the album field is empty", $("#album").value, "");

  type($("#single-rank"), "42x7");
  expect("the rank box keeps digits, three at most", $("#single-rank").value, "427");
  expect("and the name follows", $("#list .file").textContent, "427 아이오아이 (I.O.I) - 갑자기 (Suddenly).m4a");
  type($("#single-rank"), "");
  expect("an emptied rank box stays empty", $("#single-rank").value, "");
  expect("and the name stays", $("#list .file").textContent, "427 아이오아이 (I.O.I) - 갑자기 (Suddenly).m4a");
  type($("#single-rank"), "0");
  expect("rank 0 is read as 1, the box keeps the 0", [$("#single-rank").value, $("#list .file").textContent.slice(0, 3)], ["0", "001"]);
  type($("#single-rank"), "7");
  $("#opt-rank").click();
  expect("without the rank in the name", $("#list .file").textContent, "아이오아이 (I.O.I) - 갑자기 (Suddenly).m4a");
  $("#opt-rank").click();

  click("다운로드 시작");
  const started = Date.now();
  while (!buttons().includes("다시 다운로드") && Date.now() - started < 60000) await wait(100);
  expect("the video is done", labels(), ["완료 · 가사 ✓ 인터넷"]);
  expect("and says so", ($("#actions .summary") || {}).textContent?.startsWith("완료 — 성공 1 · 건너뜀 0 · 실패 0"), true);

  click("새로 시작");
  type($("#input"), "https://evil.com/x");
  $("#resolve").click();
  await wait(100);
  expect("an address the server would refuse is refused at once", [$("#error").hidden, $("#error-text").textContent, $("#resolved").hidden], [false, "YouTube 주소만 사용할 수 있습니다.", true]);
  $("#error-close").click();
  expect("the banner closes", $("#error").hidden, true);

  return JSON.stringify({ pass: failures.length === 0, failures });
})()
```

D(`?demo=broken`: 설치 확인창):

```javascript
(async () => {
  // Run on http://localhost:1420/?demo=broken right after it loaded: no tools at all, and the way to install yt-dlp.
  const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const $ = (selector) => document.querySelector(selector);
  const panel = () => $("#tools").innerText.replace(/\n+/g, " | ");
  const toolButtons = () => [...$("#tools").querySelectorAll("button")];
  const failures = [];
  const expect = (name, actual, expected) => { if (JSON.stringify(actual) !== JSON.stringify(expected)) failures.push(`${name}: got ${JSON.stringify(actual)}, wanted ${JSON.stringify(expected)}`); };

  await wait(500);
  expect("nothing is found", panel().startsWith("yt-dlp ✗ | ffmpeg ✗ | JS 런타임 ✗ | yt-dlp 설치"), true);
  expect("ffmpeg and the JavaScript runtime are asked for", [panel().includes("ffmpeg가 필요합니다. 예: winget install Gyan.FFmpeg"), panel().includes("Node.js 22 이상 또는 Deno 2.3 이상")], [true, true]);
  expect("the install button is there and the update button is not", toolButtons().map((button) => button.textContent), ["yt-dlp 설치"]);

  toolButtons()[0].click();
  expect("it asks before it downloads", $("#install-dialog").open, true);
  $("#install-dialog button[value='cancel']").click();
  await wait(100);
  expect("cancelling closes it and downloads nothing", [$("#install-dialog").open, panel().includes("작업 중")], [false, false]);

  toolButtons()[0].click();
  $("#install-dialog button[value='install']").click();
  await wait(300);
  expect("installing: busy, the button is locked", [panel().includes("작업 중…"), toolButtons()[0].disabled], [true, true]);
  await wait(2200);
  expect("installed: yt-dlp is found, the update button replaces the install button", [panel().startsWith("yt-dlp 2026.10.01 ✓ | ffmpeg ✗"), toolButtons().map((button) => button.textContent)], [true, ["yt-dlp 업데이트"]]);
  expect("installed: the result of the install is said", panel().includes("yt-dlp 2026.10.01 을(를) 설치했습니다."), true);

  return JSON.stringify({ pass: failures.length === 0, failures });
})()
```

E(`?demo&speed=40`: 3단계 리뷰가 지적한 화면 문제):

```javascript
(async () => {
  // Run on http://localhost:1420/?demo&speed=40 right after it loaded. What the review of stage 3 found in the screen: the rank box and an
  // IME, the rank box refilled by other renders, text written again though unchanged, focus after a job.
  const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const $ = (selector) => document.querySelector(selector);
  const type = (element, value) => { element.value = value; element.dispatchEvent(new Event("input", { bubbles: true })); };
  const buttons = () => [...document.querySelectorAll("#actions button")].map((element) => element.textContent);
  const failures = [];
  const expect = (name, actual, expected) => { if (JSON.stringify(actual) !== JSON.stringify(expected)) failures.push(`${name}: got ${JSON.stringify(actual)}, wanted ${JSON.stringify(expected)}`); };

  await wait(500);
  type($("#input"), "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
  $("#resolve").click();
  await wait(1200);
  expect("a video preview with its rank box", [$("#rank-field").hidden, $("#single-rank").value], [false, "1"]);

  // The rank box the user cleared stays empty while another box changes (Compose refills it only when the rank itself changes).
  type($("#single-rank"), "");
  type($("#album"), "x");
  expect("a cleared rank box stays empty when another box changes", $("#single-rank").value, "");
  type($("#album"), "");

  // An IME composing in the rank box: nothing is written to the box until the composition ends.
  const rank = $("#single-rank");
  rank.focus();
  rank.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
  rank.value = "ㅎ";
  rank.dispatchEvent(new InputEvent("input", { bubbles: true, isComposing: true }));
  expect("the box is not rewritten while composing", rank.value, "ㅎ");
  rank.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true }));
  expect("only digits are left when the composition ends", rank.value, "");
  rank.value = "7";
  rank.dispatchEvent(new Event("input", { bubbles: true }));
  expect("digits typed afterwards still count", [rank.value, $("#list .file").textContent.slice(0, 3)], ["7", "007"]);

  rank.dispatchEvent(new CompositionEvent("compositionstart", { bubbles: true }));
  rank.value = "12ㅎ";
  rank.dispatchEvent(new InputEvent("input", { bubbles: true, isComposing: true }));
  expect("nothing counts while composing", $("#list .file").textContent.slice(0, 3), "007");
  rank.dispatchEvent(new CompositionEvent("compositionend", { bubbles: true }));
  expect("digits that survived the composition count", [rank.value, $("#list .file").textContent.slice(0, 3)], ["12", "012"]);

  // A text that has not changed is not written again: a screen reader would announce the banner again on every progress event.
  type($("#input"), "https://evil.com/x");
  $("#resolve").click();
  await wait(100);
  expect("the error is shown", $("#error").hidden, false);
  const seen = { "#error-text": 0, "#info-title": 0, "#destination": 0 };
  const observers = Object.keys(seen).map((selector) => {
    const observer = new MutationObserver((records) => { seen[selector] += records.length; });
    observer.observe($(selector), { childList: true, characterData: true, subtree: true });
    return observer;
  });
  type($("#output-dir"), "C:\\Music\\a");
  type($("#output-dir"), "C:\\Music\\a");
  type($("#album"), "");
  await wait(100);
  observers.forEach((observer) => observer.disconnect());
  expect("the banner and the title are not written when unchanged; the destination once", [seen["#error-text"], seen["#info-title"], seen["#destination"] <= 1], [0, 0, true]);
  $("#error-close").click();

  // After a job without failures the keyboard is still among the buttons.
  const start = [...document.querySelectorAll("#actions button")].find((candidate) => candidate.textContent === "다운로드 시작");
  start.focus();
  start.click();
  const started = Date.now();
  while (!buttons().includes("다시 다운로드") && Date.now() - started < 60000) await wait(100);
  expect("the job ended without failures", buttons(), ["다시 다운로드", "새로 시작"]);
  expect("the keyboard stays in the buttons", document.querySelector("#actions").contains(document.activeElement), true);

  // Starting over resets the rank to 1, and the box (hidden meanwhile) shows it again for the next video.
  [...document.querySelectorAll("#actions button")].find((candidate) => candidate.textContent === "새로 시작").click();
  expect("the rank box is hidden after starting over", $("#rank-field").hidden, true);
  type($("#input"), "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
  $("#resolve").click();
  await wait(1200);
  expect("a fresh video preview shows rank 1 in the box", [$("#rank-field").hidden, $("#single-rank").value], [false, "1"]);

  return JSON.stringify({ pass: failures.length === 0, failures, seen });
})()
```

- [ ] **Step 6: 눈으로 본다**

`resize_window`로 1000×760을 주고 `?demo&speed=1`에서 조회를 눌러 스크린샷을 찍는다. Expected: 3단계와 같은 모양에 더해 출력 폴더 입력란 오른쪽에 `폴더 선택`, 그 아래 저장 위치 줄의 맨 오른쪽에 작은 `탐색기에서 보기`가 있다. 끝나면 `resize_window`의 `preset: "desktop"`으로 되돌린다.

- [ ] **Step 7: 개발 서버를 끈다**

```powershell
Get-CimInstance Win32_Process -Filter "Name='node.exe'" | Where-Object { $_.CommandLine -like '*xGetSongs\shell\node_modules*vite*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }; "stopped"
```

- [ ] **Step 8: Commit**

```bash
git add shell/index.html shell/src/app.css shell/src/main.ts
git commit -m "feat(shell): 폴더 선택과 탐색기에서 보기를 화면에 연결하고 부팅 때 설정을 읽어 닫을 때 쓴다"
```

---

### Task 8: 실제 셸에서 확인한다

**Files:**
- Modify: `shell/src/check.ts`, `shell/src/check-screen.ts`
- Create(저장소 밖): `%TEMP%\xgs-bridge-check\{helpers,desktop-helpers,single-instance,settings-check,hang-check,explorer-check,picker-check}.ps1`

**Interfaces:**
- Consumes: Task 5의 점검용 명령(`check_close`, `check_wait`, `check_env`의 `XGS_CHECK_DESKTOP`), 2~3단계의 점검 페이지와 `screen-check.ps1`.
- Produces: 환경 변수 `XGS_CHECK_DESKTOP`(`settings`, `picker`, `explorer`, `hang`)이 있으면 점검 페이지가 같은 창에서 화면(`index.html?check`)으로 넘어가 그 모드를 실행한다. 모드가 바깥(스크립트)과 주고받는 것은 점검 파일의 `key=value` 줄(`screenSettingsChanged` …)과 `check_wait`가 기다리는 신호 파일이다.

- [ ] **Step 1: 점검 코드를 더한다**

`shell/src/check.ts`(점검 모드면 두 주소 없이도 화면으로 넘어간다):

```diff
diff --git a/shell/src/check.ts b/shell/src/check.ts
index a774880..d0dd4a2 100644
--- a/shell/src/check.ts
+++ b/shell/src/check.ts
@@ -108,7 +108,9 @@ async function main() {
   // it records `screenDone` and `done` itself.
   const wanted = ["XGS_CHECK_VIDEO_URL", "XGS_CHECK_PLAYLIST_URL", "XGS_CHECK_OUTPUT_DIR"];
   const given = await Promise.all(wanted.map((name) => invoke<string | null>("check_env", { name })));
-  if (given.every(Boolean)) {
+  // One of the checks of the desktop features (settings, picker, explorer, hang) needs the screen too, without both addresses.
+  const desktopCheck = await invoke<string | null>("check_env", { name: "XGS_CHECK_DESKTOP" });
+  if (given.every(Boolean) || desktopCheck) {
     await record("bridgeChecksDone", 1);
     location.href = "/index.html?check";
     return;
```

`shell/src/check-screen.ts`(화면 조작 도우미를 `screen()`으로 빼고 네 모드를 더한다):

```diff
diff --git a/shell/src/check-screen.ts b/shell/src/check-screen.ts
index eac84b3..de67d15 100644
--- a/shell/src/check-screen.ts
+++ b/shell/src/check-screen.ts
@@ -42,7 +42,10 @@ function watchFrames(): () => number {
 
 export async function runScreenChecks(): Promise<void> {
   try {
-    await check();
+    // One of the checks of the desktop features, or the whole way through a video and a playlist.
+    const desktopCheck = await invoke<string | null>("check_env", { name: "XGS_CHECK_DESKTOP" });
+    if (desktopCheck) await checkDesktop(desktopCheck);
+    else await check();
     await record("screenDone", 1);
   } catch (error) {
     await record("error", errorMessage(error));
@@ -50,11 +53,8 @@ export async function runScreenChecks(): Promise<void> {
   await record("done", 1);
 }
 
-async function check(): Promise<void> {
-  const videoUrl = await given("XGS_CHECK_VIDEO_URL");
-  const playlistUrl = await given("XGS_CHECK_PLAYLIST_URL");
-  const outputDir = await given("XGS_CHECK_OUTPUT_DIR");
-
+/** What a person does on the screen: look at it, type into it, press its buttons. */
+function screen() {
   const $ = <T extends HTMLElement>(selector: string): T => {
     const found = document.querySelector<T>(selector);
     if (!found) throw new Error(`the screen has no ${selector}`);
@@ -84,6 +84,93 @@ async function check(): Promise<void> {
     return Math.round(performance.now() - started);
   };
   const count = (test: (label: string) => boolean) => labels().filter(test).length;
+  return { $, type, buttons, press, labels, errorText, summary, until, count };
+}
+
+/** The checks of what the desktop does for the screen. The script outside the window works the system dialog and the file manager. */
+async function checkDesktop(mode: string): Promise<void> {
+  const { $, type, until, labels } = screen();
+  const outputDir = await given("XGS_CHECK_OUTPUT_DIR");
+  const box = () => $<HTMLInputElement>("#output-dir").value;
+  const lookUpTheVideo = async () => {
+    type("#input", await given("XGS_CHECK_VIDEO_URL"));
+    $<HTMLButtonElement>("#resolve").click();
+    await until("the video to be looked up", () => !$("#resolved").hidden, 90_000);
+    await record("screenVideoPreview", `${$("#info-title").textContent} | ${labels().length} row`);
+  };
+
+  if (mode === "settings") {
+    // The settings file the script prepared was read when the page started; what the screen shows is what it held.
+    await until("the screen", () => $("#tools").innerText.length > 0, 30_000);
+    const options = () => ({
+      outputDir: box(),
+      overwrite: $<HTMLInputElement>("#opt-overwrite").checked,
+      includeRank: $<HTMLInputElement>("#opt-rank").checked,
+      searchLyricsOnline: $<HTMLInputElement>("#opt-lyrics").checked,
+    });
+    const before = options();
+    await record("screenSettingsLoaded", before);
+    // Change two of them and close the window at once, within the quiet period of the save: only the write of the closing can keep them.
+    $<HTMLInputElement>("#opt-overwrite").click();
+    type("#output-dir", outputDir);
+    await record("screenSettingsChanged", options());
+    await invoke("check_close");
+    await sleep(10_000); // the window closes; the script reads the file
+    return;
+  }
+
+  if (mode === "hang") {
+    await until("the screen", () => $("#tools").innerText.length > 0, 30_000);
+    await record("screenHangStarts", 1);
+    // A page that is stuck: it does not answer the request to close. (The Rust side closes the window after 1.5 s all the same.)
+    setTimeout(() => {
+      const end = performance.now() + 8_000;
+      while (performance.now() < end) {
+        // busy
+      }
+    }, 50);
+    await sleep(60_000);
+    return;
+  }
+
+  await lookUpTheVideo();
+  type("#output-dir", outputDir);
+
+  if (mode === "picker") {
+    await record("screenPickerStart", box());
+    // First the chooser is cancelled by the script, then it is used to choose a folder.
+    $<HTMLButtonElement>("#pick-folder").click();
+    await record("screenPickerClicked", 1);
+    await invoke("check_wait", { name: "picker-cancelled" });
+    await sleep(300);
+    await record("screenPickerAfterCancel", box());
+    $<HTMLButtonElement>("#pick-folder").click();
+    await record("screenPickerClickedAgain", 1);
+    await invoke("check_wait", { name: "picker-chosen" });
+    await sleep(300);
+    await record("screenPickerAfterChoice", box());
+    await record("screenPickerDestination", $("#destination").textContent ?? "");
+    return;
+  }
+
+  if (mode === "explorer") {
+    // A folder that does not exist yet: the nearest one that does is shown.
+    type("#output-dir", `${outputDir}\\not\\there\\yet`);
+    await record("screenExplorerDestination", $("#destination").textContent ?? "");
+    $<HTMLButtonElement>("#open-folder").click();
+    await record("screenExplorerClicked", 1);
+    await invoke("check_wait", { name: "explorer-checked" });
+    return;
+  }
+
+  throw new Error(`the check of the desktop has no mode ${mode}`);
+}
+
+async function check(): Promise<void> {
+  const videoUrl = await given("XGS_CHECK_VIDEO_URL");
+  const playlistUrl = await given("XGS_CHECK_PLAYLIST_URL");
+  const outputDir = await given("XGS_CHECK_OUTPUT_DIR");
+  const { $, type, buttons, press, labels, errorText, summary, until, count } = screen();
 
   // The screen starts: the tools are asked for (the sidecar may still be starting) and the default folder is chosen.
   const toolsAfter = await until("the tools", () => $("#tools").innerText.includes("✓"), 90_000);
```

- [ ] **Step 2: 컴파일하고 시험한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; npx tsc --noEmit; "tsc exit $LASTEXITCODE"; node --test "tests/*.test.ts" 2>&1 | Select-String "^ℹ (tests|pass|fail)"
```

Expected: `tsc exit 0`, `ℹ tests 239`, `ℹ pass 239`.

- [ ] **Step 3: 확인 스크립트를 둔다**

저장소 밖의 `%TEMP%\xgs-bridge-check\`(없으면 `New-Item -ItemType Directory -Force "$env:TEMP\xgs-bridge-check"`)에 아래를 만든다. `helpers.ps1`은 3단계의 것을 **교체**한다(창을 `Process.MainWindowHandle`이 아니라 클래스 `Tauri Window`로 찾는다. 위 Global Constraints 참고). `screen-check.ps1`은 3단계의 것이 그대로 필요하다(아래에 다시 적는다).

`helpers.ps1`:

```powershell
# Dot-sourced by the steps that run the check build of the shell. $ShellExe can be set before the dot-source.
Remove-Item Env:XGS_SIDECAR_LIB, Env:XGS_SIDECAR_JAVA -ErrorAction SilentlyContinue
if (-not $ShellExe) { $ShellExe = "C:\Projects\xGetSongs\shell\src-tauri\target\release\shell.exe" }
$Image = "C:\Projects\xGetSongs\server\build\sidecar-image"

# The windows of a process. `Process.MainWindowHandle` cannot be used to find the window of the app: the single-instance plugin
# makes a hidden window of its own (class `com.xgetsongs.shell-sic`) right at the start, and for the first moments that is the one
# .NET calls the main window. The window of the app is the one of the class `Tauri Window`.
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class AppWindows {
    private delegate bool EnumProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassNameW(IntPtr window, StringBuilder name, int max);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();

    /// The first window of the process with this class, or zero.
    public static IntPtr Find(uint processId, string className) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, parameter) => {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner != processId) return true;
            StringBuilder name = new StringBuilder(256);
            GetClassNameW(window, name, name.Capacity);
            if (name.ToString() != className) return true;
            found = window;
            return false;
        }, IntPtr.Zero);
        return found;
    }

    /// What the close button of the title bar does: WM_SYSCOMMAND with SC_CLOSE. (A disabled window ignores it, as it ignores the click.)
    public static void Close(IntPtr window) { PostMessage(window, 0x0112, (IntPtr)0xF060, IntPtr.Zero); }
}
"@

# The sidecars of the check runs only: their app data folder is a temp folder named xgs-bridge-check-* (see New-CheckRun). Every java.exe that
# runs the sidecar would also match the user's own app, which a check must neither wait for nor stop.
function Get-Sidecar {
    Get-CimInstance Win32_Process -Filter "Name='java.exe'" | Where-Object { $_.CommandLine -like '*SidecarMainKt*' -and $_.CommandLine -like '*--app-data*xgs-bridge-check-*' }
}

# The window of the app of this process (zero while it does not exist yet).
function Get-AppWindow($process) { [AppWindows]::Find([uint32]$process.Id, "Tauri Window") }

# A fresh app data folder, log folder and record file: never the user's real ones.
function New-CheckRun {
    $base = Join-Path $env:TEMP ("xgs-bridge-check-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
    New-Item -ItemType Directory $base | Out-Null
    [pscustomobject]@{ Base = $base; AppData = "$base\data"; LogDir = "$base\log"; Record = "$base\check-record.txt" }
}

function Start-CheckShell($run) {
    $env:XGS_SIDECAR_DIR = $Image
    $env:XGS_SIDECAR_APPDATA = $run.AppData
    $env:XGS_SIDECAR_LOGDIR = $run.LogDir
    $env:XGS_BRIDGE_CHECK_FILE = $run.Record
    Start-Process -FilePath $ShellExe -PassThru
}

# The lines the check page wrote, as a hashtable (a key that was written twice keeps its last value).
function Read-CheckRecord($run) {
    $values = [ordered]@{}
    if (Test-Path $run.Record) {
        foreach ($line in Get-Content $run.Record -Encoding UTF8) {
            $key, $value = $line -split '=', 2
            $values[$key] = $value
        }
    }
    $values
}

# Waits until the page wrote done= or error=, at most $Seconds.
function Wait-CheckRecord($run, [int]$Seconds = 90) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        Start-Sleep -Milliseconds 200
        $record = Read-CheckRecord $run
    } while ((Get-Date) -lt $deadline -and -not ($record.Contains("done") -or $record.Contains("error")))
    $record
}

# Waits until the shell has the window of the app, and returns how many ms that took.
function Wait-CheckWindow($process, [int]$Seconds = 10) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ((Get-AppWindow $process) -eq [IntPtr]::Zero -and $watch.Elapsed.TotalSeconds -lt $Seconds) { Start-Sleep -Milliseconds 5 }
    [int]$watch.ElapsedMilliseconds
}

# Presses the close button of the window of the app.
function Close-AppWindow($process) {
    [void](Wait-CheckWindow $process)
    [AppWindows]::Close((Get-AppWindow $process))
}

# Closes the window and returns how many ms it took until the shell and the sidecar were both gone (null: after 20 s).
function Close-CheckShell($process) {
    [void](Wait-CheckWindow $process)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Close-AppWindow $process
    while ($watch.Elapsed.TotalSeconds -lt 20 -and ((Get-Sidecar) -or -not $process.HasExited)) { Start-Sleep -Milliseconds 50 }
    if ((Get-Sidecar) -or -not $process.HasExited) { return $null }
    [int]$watch.Elapsed.TotalMilliseconds
}
```

`desktop-helpers.ps1`:

```powershell
# Dot-sourced after helpers.ps1 (the helpers of the bridge check) by the checks of the desktop features (stage 4).

# Waits until the page wrote the line `key=...` to the check file (or wrote `error=...`), at most $Seconds. Returns the record, or throws.
function Wait-RecordKey($run, [string]$Key, [int]$Seconds = 60) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        Start-Sleep -Milliseconds 50
        $record = Read-CheckRecord $run
        if ($record.Contains("error")) { throw "the page reported an error: $($record['error'])" }
    } while ((Get-Date) -lt $deadline -and -not $record.Contains($Key))
    if (-not $record.Contains($Key)) { throw "the page did not write $Key within $Seconds s" }
    $record
}

# Tells the page that waits in `check_wait` for the signal that the script did what it was waiting for.
function Send-Signal($run, [string]$Name) {
    Set-Content -Path "$($run.Record).signal-$Name" -Value "go"
}

# Waits until the process has ended, and returns how many ms that took (null: it did not end within $Seconds).
function Wait-Exit($process, [int]$Seconds = 20) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $Seconds -and -not $process.HasExited) { Start-Sleep -Milliseconds 20 }
    if (-not $process.HasExited) { return $null }
    [int]$watch.Elapsed.TotalMilliseconds
}

# What a check run leaves behind: the shell and the sidecar must both be gone.
function Get-LeftOver($process) {
    "shell running: $(-not $process.HasExited), sidecars: $(@(Get-Sidecar).Count)"
}

# The user's app folder holds the yt-dlp the app installed; the checks that need to look a video up use a COPY of it in their temp
# app data folder (never the real folder: the sidecar clears its `work` folder when it starts).
function Copy-YtDlp($run) {
    New-Item -ItemType Directory "$($run.AppData)\bin" -Force | Out-Null
    Copy-Item "$env:APPDATA\xGetSongs\bin\yt-dlp.exe" "$($run.AppData)\bin\yt-dlp.exe"
}
```

`screen-check.ps1`(3단계와 같다):

```powershell
# Runs the check build of the shell with the real screen on the real sidecar and the real yt-dlp (a COPY of the app's own), and prints
# what the screen did. Needs the helpers of the bridge check (New-CheckRun, Start-CheckShell, Wait-CheckRecord, Close-CheckShell).
param(
    [string]$Helpers = "$env:TEMP\xgs-bridge-check\helpers.ps1",
    [string]$VideoUrl = 'https://www.youtube.com/watch?v=9qkpcLK422o',
    [string]$PlaylistUrl = 'https://www.youtube.com/playlist?list=PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV',
    [int]$Seconds = 420
)
. $Helpers
$run = New-CheckRun
New-Item -ItemType Directory "$($run.AppData)\bin" -Force | Out-Null
Copy-Item "$env:APPDATA\xGetSongs\bin\yt-dlp.exe" "$($run.AppData)\bin\yt-dlp.exe"
$files = Join-Path $run.Base "files"
$env:XGS_CHECK_VIDEO_URL = $VideoUrl
$env:XGS_CHECK_PLAYLIST_URL = $PlaylistUrl
$env:XGS_CHECK_OUTPUT_DIR = $files

# The screen's own default folder is in the user's Music folder: the check must never write there.
$music = Join-Path $env:USERPROFILE "Music\xGetSongs"
$before = if (Test-Path $music) { (Get-ChildItem $music -Recurse -File | Measure-Object).Count } else { -1 }

$watch = [Diagnostics.Stopwatch]::StartNew()
$process = Start-CheckShell $run
$record = Wait-CheckRecord $run $Seconds
$elapsed = [int]$watch.Elapsed.TotalSeconds
$closedMs = Close-CheckShell $process

"=== the screen checks ($elapsed s, closed after $closedMs ms)"
foreach ($key in $record.Keys) { "{0,-24} {1}" -f $key, $record[$key] }
"=== files the check wrote"
if (Test-Path $files) { Get-ChildItem $files -Recurse -File | ForEach-Object { "{0,10:N0}  {1}" -f $_.Length, $_.FullName.Substring($files.Length) } } else { "(no folder)" }
$after = if (Test-Path $music) { (Get-ChildItem $music -Recurse -File | Measure-Object).Count } else { -1 }
"=== the user's own default folder: $before files before, $after after"
"=== marker: " + ((Get-Content "$($run.LogDir)\last-run.txt" -ErrorAction SilentlyContinue) -join ' | ')
"=== left over: sidecars $(@(Get-Sidecar).Count), yt-dlp/ffmpeg $(@(Get-Process yt-dlp, ffmpeg -ErrorAction SilentlyContinue).Count)"
$run.Base | Set-Content "$env:TEMP\xgs-bridge-check\last-screen-run.txt"
Remove-Item Env:XGS_CHECK_VIDEO_URL, Env:XGS_CHECK_PLAYLIST_URL, Env:XGS_CHECK_OUTPUT_DIR
```

`single-instance.ps1`:

```powershell
# Starts the shell twice with the same app data folder and tells what the second start did to the first: the second copy ends at once,
# the work folder of the first is not touched (the sidecar clears it when it starts, so a second sidecar would delete what is in it),
# there is one sidecar, and the window of the first copy comes back to the front.
param(
    [string]$ShellExe = "C:\Projects\xGetSongs\shell\src-tauri\target\release\shell.exe",
    [string]$Helpers = "$env:TEMP\xgs-bridge-check\helpers.ps1"
)
. $Helpers

function Wait-Until($seconds, [scriptblock]$test) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while (-not (& $test)) { if ($watch.Elapsed.TotalSeconds -gt $seconds) { return $false }; Start-Sleep -Milliseconds 50 }
    return $true
}

$run = New-CheckRun
$env:XGS_BRIDGE_CHECK_FILE = "$($run.Record)"   # the shell of this run is the plain one: the record file is not used
$first = Start-CheckShell $run
$titled = Wait-Until 15 { (Get-AppWindow $first) -ne [IntPtr]::Zero }
# The sidecar deletes `work` when it starts and creates it only when a job needs it: a file we put there is the canary of a second sidecar.
$ready = Wait-Until 30 { (@(Get-Sidecar).Count -eq 1) -and (Test-Path "$($run.AppData)\sidecar-stderr.log") }
Start-Sleep -Seconds 4
"first: window $titled, sidecar up $ready"
New-Item -ItemType Directory -Force "$($run.AppData)\work" | Out-Null
Set-Content "$($run.AppData)\work\marker.txt" "the first copy's work"
$handle = Get-AppWindow $first
[void][AppWindows]::ShowWindow($handle, 6)   # SW_MINIMIZE
Start-Sleep -Milliseconds 500
"first minimized: $([AppWindows]::IsIconic($handle))"

$watch = [Diagnostics.Stopwatch]::StartNew()
$second = Start-CheckShell $run
$gone = Wait-Until 15 { $second.Refresh(); $second.HasExited }
"second: exited $gone after $([int]$watch.Elapsed.TotalMilliseconds) ms, exit code $(if ($gone) { $second.ExitCode } else { '-' })"
Start-Sleep -Seconds 3   # long enough for a sidecar of the second copy to have started and cleared `work`
"marker of the first copy still there: $(Test-Path "$($run.AppData)\work\marker.txt")"
"sidecars now: $(@(Get-Sidecar).Count) (one expected)"
"first still running: $(-not $first.HasExited); window restored: $(-not [AppWindows]::IsIconic($handle)); visible: $([AppWindows]::IsWindowVisible($handle)); in front: $([AppWindows]::GetForegroundWindow() -eq $handle)"

Close-AppWindow $first
$gone = Wait-Until 15 { $first.Refresh(); $first.HasExited }
Start-Sleep -Seconds 1
"first closed: $gone; sidecars left: $(@(Get-Sidecar).Count)"
if (-not $second.HasExited) { Stop-Process -Id $second.Id -Force }
```

`settings-check.ps1`:

```powershell
# The settings file of the app and the closing of the window, on the real shell: the options the file holds are what the screen starts
# with, and options changed right before the window is closed are written by the closing (not lost to the quiet period of the save).
# `-UseRealSettings` starts from a COPY of the settings file of the user (read only), to see that what the Compose app wrote is read.
param(
    [string]$ShellExe = "C:\Projects\xGetSongs\shell\src-tauri\target\release\shell.exe",
    [string]$Helpers = "$env:TEMP\xgs-bridge-check\helpers.ps1",
    [string]$DesktopHelpers = "$env:TEMP\xgs-bridge-check\desktop-helpers.ps1",
    [switch]$UseRealSettings
)
. $Helpers
. $DesktopHelpers

$run = New-CheckRun
New-Item -ItemType Directory $run.AppData -Force | Out-Null
$file = "$($run.AppData)\settings.json"
$utf8 = New-Object System.Text.UTF8Encoding($false)
if ($UseRealSettings) {
    Copy-Item "$env:APPDATA\xGetSongs\settings.json" $file
} else {
    # Written the way the Compose app writes it (with the `concurrency` of an older version).
    [IO.File]::WriteAllText($file, "{`n    `"outputDir`": `"D:\\음악\\내 재생목록`",`n    `"overwrite`": false,`n    `"includeRank`": false,`n    `"searchLyricsOnline`": false,`n    `"concurrency`": 3`n}`n", $utf8)
}
$seeded = Get-Content $file -Raw -Encoding UTF8 | ConvertFrom-Json
"seeded file: outputDir=$($seeded.outputDir) overwrite=$($seeded.overwrite) includeRank=$($seeded.includeRank) searchLyricsOnline=$($seeded.searchLyricsOnline)"

$newFolder = "$($run.Base)\저장 폴더"
$env:XGS_CHECK_DESKTOP = "settings"
$env:XGS_CHECK_OUTPUT_DIR = $newFolder
$process = Start-CheckShell $run
try {
    $record = Wait-RecordKey $run "screenSettingsChanged" 60
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $exitMs = Wait-Exit $process 20
    $record = Read-CheckRecord $run
    "screen loaded:  $($record['screenSettingsLoaded'])"
    "screen changed: $($record['screenSettingsChanged'])"
    "the window closed by itself (the page asked for it right after the change): ended after $exitMs ms, $(Get-LeftOver $process)"
    "marker: " + ((Get-Content "$($run.LogDir)\last-run.txt" -ErrorAction SilentlyContinue) -join ' | ')
    $after = Get-Content $file -Raw -Encoding UTF8 | ConvertFrom-Json
    "file after:  outputDir=$($after.outputDir) overwrite=$($after.overwrite) includeRank=$($after.includeRank) searchLyricsOnline=$($after.searchLyricsOnline)"
    "expected:    outputDir=$newFolder overwrite=$(-not $seeded.overwrite) includeRank=$($seeded.includeRank) searchLyricsOnline=$($seeded.searchLyricsOnline)"
    # The close request is answered by the page (it writes what is still waiting and asks for the window to be closed): the shell ends in well
    # under the 1.5 s after which it would have closed the window by itself (the file would hold the values then too: the save timer of 0.4 s runs
    # in a page that is alive).
    $answered = ($exitMs -ne $null) -and ($exitMs -lt 1500)
    "the page answered the request to close (ended in under 1500 ms): $answered"
    $ok = $answered -and ($after.outputDir -eq $newFolder) -and ($after.overwrite -eq (-not $seeded.overwrite)) -and ($after.includeRank -eq $seeded.includeRank) -and ($after.searchLyricsOnline -eq $seeded.searchLyricsOnline)
    "keys in the file: $((Get-Content $file -Raw -Encoding UTF8 | ConvertFrom-Json).PSObject.Properties.Name -join ',')"
    "leftover files in the app data folder: " + ((Get-ChildItem $run.AppData -Name | Where-Object { $_ -like 'settings*' }) -join ',')
    "RESULT: " + $(if ($ok) { "PASS" } else { "FAIL" })
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    Remove-Item Env:XGS_CHECK_DESKTOP, Env:XGS_CHECK_OUTPUT_DIR
}
```

`hang-check.ps1`:

```powershell
# A page that is stuck must not keep the window open: the shell closes the window itself 1.5 s after the request.
param(
    [string]$ShellExe = "C:\Projects\xGetSongs\shell\src-tauri\target\release\shell.exe",
    [string]$Helpers = "$env:TEMP\xgs-bridge-check\helpers.ps1",
    [string]$DesktopHelpers = "$env:TEMP\xgs-bridge-check\desktop-helpers.ps1"
)
. $Helpers
. $DesktopHelpers

$run = New-CheckRun
$env:XGS_CHECK_DESKTOP = "hang"
$env:XGS_CHECK_OUTPUT_DIR = "$($run.Base)\files"
$process = Start-CheckShell $run
try {
    [void](Wait-RecordKey $run "screenHangStarts" 60)
    Start-Sleep -Milliseconds 600   # the page is in its busy loop (it stays in it for 8 s)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Close-AppWindow $process
    $exitMs = Wait-Exit $process 20
    Start-Sleep -Seconds 1
    "the stuck page: window closed and the shell ended after $exitMs ms (the page would have been stuck for about 7.5 s more), $(Get-LeftOver $process)"
    "marker: " + ((Get-Content "$($run.LogDir)\last-run.txt" -ErrorAction SilentlyContinue) -join ' | ')
    "RESULT: " + $(if ($exitMs -ne $null -and $exitMs -lt 5000 -and -not (Get-Sidecar)) { "PASS" } else { "FAIL" })
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    Remove-Item Env:XGS_CHECK_DESKTOP, Env:XGS_CHECK_OUTPUT_DIR
}
```

`explorer-check.ps1`:

```powershell
# "탐색기에서 보기" on the real shell: the file manager opens the nearest folder that exists, and the window of the file manager is not
# taken down with the shell when the shell ends. Needs a video address (the screen shows the button once something is looked up).
param(
    [string]$ShellExe = "C:\Projects\xGetSongs\shell\src-tauri\target\release\shell.exe",
    [string]$Helpers = "$env:TEMP\xgs-bridge-check\helpers.ps1",
    [string]$DesktopHelpers = "$env:TEMP\xgs-bridge-check\desktop-helpers.ps1",
    [string]$VideoUrl = 'https://www.youtube.com/watch?v=9qkpcLK422o'
)
. $Helpers
. $DesktopHelpers

# The windows of the file manager, as the folders they show.
function Get-ExplorerFolders {
    $shell = New-Object -ComObject Shell.Application
    @($shell.Windows() | Where-Object { $_.LocationURL -like 'file:*' } | ForEach-Object { [Uri]::UnescapeDataString($_.LocationURL) -replace '^file:///', '' -replace '/', '\' })
}
function Close-ExplorerFolder([string]$Folder) {
    $shell = New-Object -ComObject Shell.Application
    $shell.Windows() | Where-Object { $_.LocationURL -like 'file:*' -and ([Uri]::UnescapeDataString($_.LocationURL) -replace '^file:///', '' -replace '/', '\') -eq $Folder } | ForEach-Object { $_.Quit() }
}

$run = New-CheckRun
Copy-YtDlp $run
# A comma in the name: Explorer splits its command line at commas, so the path has to reach it in quotes.
$files = "$($run.Base)\files,2020"
New-Item -ItemType Directory $files | Out-Null
$env:XGS_CHECK_DESKTOP = "explorer"
$env:XGS_CHECK_VIDEO_URL = $VideoUrl
$env:XGS_CHECK_OUTPUT_DIR = $files
$before = Get-ExplorerFolders
$process = Start-CheckShell $run
$opened = $null
try {
    $record = Wait-RecordKey $run "screenExplorerClicked" 120
    "destination shown: $($record['screenExplorerDestination'])"
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt 15 -and -not $opened) {
        $opened = Get-ExplorerFolders | Where-Object { $_ -eq $files -and $_ -notin $before } | Select-Object -First 1
        Start-Sleep -Milliseconds 100
    }
    "the file manager shows: $opened (after $([int]$watch.Elapsed.TotalMilliseconds) ms; wanted $files, the nearest folder that exists)"
    Send-Signal $run "explorer-checked"
    [void](Wait-RecordKey $run "done" 30)
    $exitMs = $null
    Close-AppWindow $process
    $exitMs = Wait-Exit $process 20
    Start-Sleep -Milliseconds 700
    "the shell ended after $exitMs ms, $(Get-LeftOver $process)"
    $still = (Get-ExplorerFolders) -contains $files
    "the window of the file manager is still there after the shell ended: $still"
    "RESULT: " + $(if ($opened -and $still) { "PASS" } else { "FAIL" })
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    Close-ExplorerFolder $files
    Remove-Item Env:XGS_CHECK_DESKTOP, Env:XGS_CHECK_VIDEO_URL, Env:XGS_CHECK_OUTPUT_DIR
}
```

`picker-check.ps1`:

```powershell
# "폴더 선택" on the real shell: the system's folder dialog opens over the window (the window is locked while it is open), starts in the
# folder that is typed, leaves the box alone when it is cancelled, and puts the chosen folder in the box. UI Automation works the dialog
# by its automation ids (1 = choose, 2 = cancel), which do not depend on the language of Windows; the folder to choose is a sub-folder of
# the one it starts in, picked in the list of the dialog (nothing is typed: keystrokes would go to whichever window is in front).
# Needs a video address (the screen shows the button once something is looked up).
param(
    [string]$ShellExe = "C:\Projects\xGetSongs\shell\src-tauri\target\release\shell.exe",
    [string]$Helpers = "$env:TEMP\xgs-bridge-check\helpers.ps1",
    [string]$DesktopHelpers = "$env:TEMP\xgs-bridge-check\desktop-helpers.ps1",
    [string]$VideoUrl = 'https://www.youtube.com/watch?v=9qkpcLK422o'
)
. $Helpers
. $DesktopHelpers
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

# The dialog, found as a child window of the main window (UI Automation lists an owned dialog under its owner), or $null.
function Find-Dialog($process, [double]$Seconds = 15) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    do {
        $windows = $uia::RootElement.FindAll($scope::Children, (New-Object System.Windows.Automation.PropertyCondition($uia::ProcessIdProperty, $process.Id)))
        $main = $windows | Where-Object { $_.Current.ClassName -eq "Tauri Window" } | Select-Object -First 1
        if ($main) {
            $dialog = $main.FindAll($scope::Children, [System.Windows.Automation.Condition]::TrueCondition) | Where-Object { $_.Current.ClassName -eq '#32770' } | Select-Object -First 1
            if ($dialog) { return [pscustomobject]@{ Main = $main; Dialog = $dialog } }
        }
        Start-Sleep -Milliseconds 100
    } while ($watch.Elapsed.TotalSeconds -lt $Seconds)
    return $null
}
function Get-ByAutomationId($parent, [string]$Id) {
    $parent.FindFirst($scope::Descendants, (New-Object System.Windows.Automation.PropertyCondition($uia::AutomationIdProperty, $Id)))
}
# The buttons of the system's dialog are plain Win32 buttons without an invoke pattern: click them by their window handle (BM_CLICK).
function Press($button) { [void][Win32Controls]::PostMessage([IntPtr]$button.Current.NativeWindowHandle, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) }
function Wait-DialogGone($process, [int]$Seconds = 15) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $Seconds) { if (-not (Find-Dialog $process 0)) { return $true }; Start-Sleep -Milliseconds 100 }
    $false
}

$run = New-CheckRun
Copy-YtDlp $run
$files = "$($run.Base)\files"
$chosen = "$files\picked folder"
New-Item -ItemType Directory $chosen | Out-Null
$env:XGS_CHECK_DESKTOP = "picker"
$env:XGS_CHECK_VIDEO_URL = $VideoUrl
$env:XGS_CHECK_OUTPUT_DIR = $files
$process = Start-CheckShell $run
$ok = $true
function Check([string]$What, $Actual, $Wanted) {
    $good = "$Actual" -eq "$Wanted"
    if (-not $good) { $script:ok = $false }
    "{0,-4} {1}: {2}{3}" -f $(if ($good) { "ok" } else { "FAIL" }), $What, $Actual, $(if ($good) { "" } else { "   (wanted $Wanted)" })
}
try {
    [void](Wait-RecordKey $run "screenPickerClicked" 120)
    $found = Find-Dialog $process
    Check "the dialog opened" ([bool]$found) $true
    Check "its title" $found.Dialog.Current.Name "출력 폴더 선택"
    Check "it starts in the typed folder (address bar)" ((Get-ByAutomationId $found.Dialog "1001").Current.Name -like "*$files") $true
    Check "the window is locked while the dialog is open (enabled)" $found.Main.Current.IsEnabled $false

    # 1. cancelled: the box keeps what was typed
    Press (Get-ByAutomationId $found.Dialog "2")
    Check "the dialog closed after cancel" (Wait-DialogGone $process) $true
    Send-Signal $run "picker-cancelled"
    $record = Wait-RecordKey $run "screenPickerAfterCancel" 30
    Check "after cancel the box still says" $record["screenPickerAfterCancel"] $files

    # 2. a folder is chosen: the sub-folder is selected in the list of the dialog, then "choose"
    [void](Wait-RecordKey $run "screenPickerClickedAgain" 30)
    $found = Find-Dialog $process
    Check "the dialog opened again" ([bool]$found) $true
    $item = $null
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while (-not $item -and $watch.Elapsed.TotalSeconds -lt 10) {
        $item = $found.Dialog.FindAll($scope::Descendants, (New-Object System.Windows.Automation.PropertyCondition($uia::NameProperty, "picked folder"))) | Where-Object { $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::ListItem } | Select-Object -First 1
        if (-not $item) { Start-Sleep -Milliseconds 100 }
    }
    Check "the sub-folder is listed in the dialog" ([bool]$item) $true
    $item.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Press (Get-ByAutomationId $found.Dialog "1")
    Check "the dialog closed after choosing" (Wait-DialogGone $process) $true
    Send-Signal $run "picker-chosen"
    $record = Wait-RecordKey $run "screenPickerAfterChoice" 30
    Check "after choosing the box says" $record["screenPickerAfterChoice"] $chosen
    Check "and the destination follows" ($record["screenPickerDestination"] -replace '^저장 위치: ', '') $chosen

    [void](Wait-RecordKey $run "done" 30)
    Close-AppWindow $process
    "the shell ended after $(Wait-Exit $process 20) ms, $(Get-LeftOver $process)"
    "RESULT: " + $(if ($ok) { "PASS" } else { "FAIL" })
} finally {
    Send-Signal $run "picker-cancelled"; Send-Signal $run "picker-chosen"
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    Remove-Item Env:XGS_CHECK_DESKTOP, Env:XGS_CHECK_VIDEO_URL, Env:XGS_CHECK_OUTPUT_DIR
}
```

- [ ] **Step 4: 점검용 셸을 빌드한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\shell; npm run tauri build -- --no-bundle --config check.conf.json 2>&1 | Select-Object -Last 4; Get-Item src-tauri\target\release\shell.exe | Select-Object Length, LastWriteTime
```

Expected: 빌드 성공, `shell.exe`가 방금 시각(2~5분).

- [ ] **Step 5: 단일 인스턴스 (D5)**

```powershell
& "$env:TEMP\xgs-bridge-check\single-instance.ps1"
```

Expected(계획을 쓰며 같은 코드로 돌린 값은 괄호 안): `first: window True, sidecar up True`, `first minimized: True`, `second: exited True after … ms, exit code 0`(106ms. 3초 이하), `marker of the first copy still there: True`, `sidecars now: 1 (one expected)`, `first still running: True; window restored: True; visible: True; in front: True`, `first closed: True; sidecars left: 0`.

- [ ] **Step 6: 설정 파일과 닫을 때의 쓰기 (D4)**

두 번 돈다. 첫 번째는 Compose 앱이 쓰는 모양(들여쓰기, 한글 경로, 옛 `concurrency`)으로 스크립트가 만든 파일이고, 두 번째는 **사용자의 실제 `%APPDATA%\xGetSongs\settings.json`의 복사본**이다.

```powershell
& "$env:TEMP\xgs-bridge-check\settings-check.ps1"; & "$env:TEMP\xgs-bridge-check\settings-check.ps1" -UseRealSettings
```

Expected: 두 번 모두 `screen loaded:`가 파일의 값과 같고, `screen changed:`는 `overwrite`가 뒤집히고 `outputDir`이 스크립트의 임시 폴더(`…\저장 폴더`)이며, `ended after … ms`(650~810ms)로 창이 스스로 닫히고 남는 것이 없고, 마커가 `reason=user`, `file after`가 `expected`와 같으며(바꾸자마자 닫았으므로 저장 대기 시간 0.4초 안이다: 닫을 때의 쓰기만이 이 값을 파일에 넣을 수 있다), `keys in the file: outputDir,overwrite,includeRank,searchLyricsOnline`(`concurrency`가 없다), `leftover files …: settings.json`(임시 파일이 없다), `RESULT: PASS`.

- [ ] **Step 7: 멈춘 페이지 (D7의 뒤 절반)**

```powershell
& "$env:TEMP\xgs-bridge-check\hang-check.ps1"
```

Expected: `window closed and the shell ended after … ms`(2,126ms: 응답 없는 페이지를 기다리는 1.5초 + 사이드카 정리), `shell running: False, sidecars: 0`, 마커 `reason=user`, `RESULT: PASS`(5초 이하).

- [ ] **Step 8: 탐색기 (D7의 앞 절반)**

```powershell
& "$env:TEMP\xgs-bridge-check\explorer-check.ps1"
```

Expected: `destination shown: …\files\not\there\yet`, `the file manager shows: …\files (after … ms; …)`(없는 하위 경로 대신 가장 가까운 존재하는 폴더. 1.3~1.5초), `the window of the file manager is still there after the shell ended: True`, `RESULT: PASS`. 스크립트가 연 탐색기 창은 스크립트가 닫는다.

- [ ] **Step 9: 폴더 선택 창 (D6)**

이 스크립트는 시스템 대화상자를 UI Automation으로 조작한다. 실행 중에는 키보드와 마우스를 쓰지 않는다(`SendKeys`는 쓰지 않는다. 대화상자의 목록에서 하위 폴더를 고르고 `폴더 선택` 단추를 누른다).

```powershell
& "$env:TEMP\xgs-bridge-check\picker-check.ps1"
```

Expected: 모두 `ok`: `the dialog opened`, `its title: 출력 폴더 선택`, `it starts in the typed folder (address bar)`, `the window is locked while the dialog is open (enabled): False`, `the dialog closed after cancel`, `after cancel the box still says: …\files`, `the dialog opened again`, `the sub-folder is listed in the dialog`, `the dialog closed after choosing`, `after choosing the box says: …\files\picked folder`, `and the destination follows: …`, `the shell ended after … ms`(600~650ms), `RESULT: PASS`.

- [ ] **Step 10: 앞 단계의 관문 (D8)**

3단계의 화면 점검(실제 다운로드와 취소), 시작 직후 닫기, 강제 종료를 같은 빌드로 다시 돈다.

```powershell
& "$env:TEMP\xgs-bridge-check\screen-check.ps1" *> "$env:TEMP\xgs-bridge-check\screen-run.txt"; Get-Content "$env:TEMP\xgs-bridge-check\screen-run.txt" | Where-Object { $_ -match "^(===|waitedMs|worst|burstOutOf|screen|done|error|\s+[\d,]+ )" }
```

Expected(계획을 쓰며 같은 코드로 돌린 값은 괄호 안): 3단계의 표와 같다. `waitedMs` 10,000 이하(1,124), `screenTools`가 세 도구 ✓, `screenDefaultFolder`가 `C:\Users\<이름>\Music\xGetSongs`, 영상 한 곡이 `042 ….m4a`로 `완료`(6초, 3,007,075바이트), 재생목록 101곡을 취소하면 `ready 101 done 0 still moving 0`(638ms), `screenVideoFrameGapMs`와 `screenPlaylistFrameGapMs`가 한 프레임 근처, `screenError none`, 사용자의 `~\Music\xGetSongs`는 전후가 같고, 마커 `reason=user`, `left over: sidecars 0, yt-dlp/ffmpeg 0`.

시작 직후 닫기를 **10번**(3단계는 3번이었다. 창이 막 생겼을 때 닫기가 막히는지는 한두 번으로는 보이지 않는다)과 강제 종료 3번:

```powershell
. "$env:TEMP\xgs-bridge-check\helpers.ps1"
$bad = 0
foreach ($n in 1..10) {
    $run = New-CheckRun
    $process = Start-CheckShell $run
    $windowMs = Wait-CheckWindow $process
    $closedMs = Close-CheckShell $process
    $marker = (Get-Content "$($run.LogDir)\last-run.txt" -ErrorAction SilentlyContinue) -join ' | '
    "early close $n : window after $windowMs ms, gone after $closedMs ms, shell running: $(-not $process.HasExited), sidecars left: $(@(Get-Sidecar).Count) :: " + ($marker -replace '\| pid=\d+ \| started=\S+ ', '')
    if ($closedMs -eq $null) { $bad++; if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }; Start-Sleep -Seconds 1 }
}
"failures: $bad of 10"
foreach ($n in 1..3) {
    $run = New-CheckRun
    $process = Start-CheckShell $run
    Wait-CheckRecord $run 90 | Out-Null
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Stop-Process -Id $process.Id -Force
    while ((Get-Sidecar) -and $watch.Elapsed.TotalSeconds -lt 10) { Start-Sleep -Milliseconds 25 }
    "force kill $n : sidecar gone after $([int]$watch.Elapsed.TotalMilliseconds) ms, left over: $(@(Get-Sidecar).Count)"
}
```

Expected: 시작 직후 닫기 10번 모두 15,000ms 이하(3.0~3.4초)로 남은 사이드카 0, 마커 `state=exited`와 `reason=user`, `failures: 0 of 10`. 강제 종료 3번 모두 5,000ms 이하(181~197ms), 남은 사이드카 0.

하나라도 어긋나면 **확인 스크립트의 창 찾기를 먼저 의심한다**(창 핸들, 비활성 창은 닫기를 무시한다), 그다음에 제품을 본다. 시작 직후 닫기가 실패하면 **그 실행이 띄운 셸만** 끝낸 뒤(`Stop-Process -Id`. 이름으로 `shell`을 모두 끝내지 않는다: 사용자의 앱이 같은 이름이다) 다시 돈다.

- [ ] **Step 11: Commit**

```bash
git add shell/src/check.ts shell/src/check-screen.ts
git commit -m "test(shell): 점검 페이지가 설정·폴더 선택·탐색기·멈춘 페이지를 실제 셸에서 확인하게 한다"
```

---

### Task 9: 전체 시험, 보고서, 문서

**Files:**
- Create: `docs/superpowers/specs/2026-10-11-tauri-desktop-features-report.md`
- Modify: `docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md`

- [ ] **Step 1: 전체를 한 번 더 돌린다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\shell
node --test "tests/*.test.ts" 2>&1 | Select-String "^ℹ (tests|pass|fail|skipped)"; npx tsc --noEmit; "tsc exit $LASTEXITCODE"; npm run build 2>&1 | Select-String "built in|error"
node tools/differential/fuzz.mts 20000 2>&1 | Select-Object -Last 1
Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test 2>&1 | Select-String "test result"
Set-Location C:\Projects\xGetSongs\shell\src-tauri; cargo clippy 2>&1 | Select-String "^warning|^error|Finished"
```

Expected: `ℹ tests 239`·`ℹ pass 239`·`ℹ fail 0`·`ℹ skipped 0`, `tsc exit 0`, `built in`, `80000 cases (20000 per kind), 0 mismatches`, 단위 `116 passed`와 통합 `1 ignored` 둘, clippy `Finished`.

- [ ] **Step 2: 보고서를 쓴다**

`docs/superpowers/specs/2026-10-11-tauri-desktop-features-report.md`를 아래 틀로 만들고 Task 5·7·8의 실제 값으로 채운다:

```markdown
# Tauri 데스크톱 기능(4단계) 보고서

작성일: 2026-10-11
설계: [2026-10-10-tauri-kotlin-sidecar-design.md](2026-10-10-tauri-kotlin-sidecar-design.md) §5의 4단계
계획: [2026-10-11-tauri-desktop-features.md](../plans/2026-10-11-tauri-desktop-features.md)

환경: (Windows, Node, Vite, Rust/Tauri 버전, `tauri-plugin-dialog`·`tauri-plugin-single-instance` 버전, 사이드카 이미지, 실제 yt-dlp 버전, 셸은 `--config check.conf.json`으로 만든 릴리스 빌드)

## 관문

| 번호 | 조건 | 결과 | 근거 |
|---|---|---|---|
| D1 | `sidecar-host` 시험 | | 116개와 원본 이름 점검 |
| D2 | TS 시험 | | 239개 |
| D3 | 브라우저 시나리오 F와 회귀 A~E | | |
| D4 | 설정 파일 | | 합성 파일과 사용자 파일의 복사본, 닫을 때의 쓰기 |
| D5 | 단일 인스턴스 | | 두 번째 실행의 종료 시간, `work\` 표식, 사이드카 수, 첫 창 |
| D6 | 폴더 선택 창 | | |
| D7 | 탐색기, 멈춘 페이지 | | |
| D8 | 앞 단계의 관문 | | 화면 점검, 시작 직후 닫기 10번, 강제 종료, `waitedMs` |

## 측정

(`second` 실행의 종료 시간, 닫기 시간(설정 확인·멈춘 페이지·시작 직후), 폴더 대화상자 확인의 값, `waitedMs`.)

## 막힌 점과 고친 것

(계획과 달랐던 점. 계획을 쓰며 겪은 것: 시작 직후 닫기가 10번 중 9번 실패한 것은 확인 스크립트의 창 핸들 문제였다. `SendKeys`와 `WM_SETTEXT`로 대화상자에 경로를 넣는 방식은 쓸 수 없어서 목록에서 고르는 방식으로 바꿨다. 없으면 "없음".)

## 판정

(통과 / 재시도 후 통과 / 중단. 통과이면 다음은 설계 §5의 5단계 "포장과 단일 exe"다.)

## 사용자가 직접 확인할 것

- **먼저 Compose 앱을 끈다.** 셸은 이제 같은 앱 데이터 폴더(`%APPDATA%\xGetSongs`)를 쓰고 사이드카는 시작할 때 그 안의 `work\`를 지운다.
- 일반 빌드(`npm run tauri build -- --no-bundle`)를 만들어 `XGS_SIDECAR_DIR=<저장소>\server\build\sidecar-image`를 주고 `shell\src-tauri\target\release\shell.exe`를 실행한다. 사용자의 yt-dlp와 `settings.json`(출력 폴더, 옵션)이 그대로 쓰인다.
- 한글 입력: 주소, 출력 폴더, 앨범명, **순위 번호** 칸(영상 한 곡을 조회하면 나온다).
- `폴더 선택`: 창이 앱 위에 모달로 뜨고 현재 폴더에서 시작하는지, 취소하면 그대로인지.
- `탐색기에서 보기`: 다운로드 중에도 눌리는지, 아직 없는 하위 폴더면 가장 가까운 폴더를 여는지.
- 옵션을 바꾼 직후 창을 닫고 다시 열어 기억하는지(Compose 앱과 같은 파일).
- 앱을 두 번 실행하면 두 번째는 사라지고 첫 창이 앞으로 오는지(최소화해 둔 채로도).

## 이 단계가 확인하지 않은 것

- 첫 창의 숨은 창이 만들어지기 전(시작 직후 수십 ms)의 거의 동시 실행: 플러그인은 그때 두 번째 실행을 끝내지 않고 둘 다 뜬다(플러그인 소스에서 확인했고 시험하지 않았다).
- Compose 앱과 셸을 동시에 실행하는 경우(Compose 앱에는 단일 인스턴스가 없어 막을 수 없다).
- 사용자의 실제 `%APPDATA%\xGetSongs`에서의 실행(확인은 임시 폴더로 했다. `settings.json`은 실제 파일의 복사본으로 읽기만 확인했다).
- 폴더 선택 창에서 경로를 직접 입력하는 경우, 네트워크 드라이브·존재하지 않는 드라이브.
- 사용하지 않는 `tauri-plugin-opener`와 `opener:default` 권한이 남아 있다(웹뷰가 시스템의 기본 앱으로 URL을 열 수 있다. 5단계 전에 정리한다).
- 2~3단계에서 넘어온 항목: 사이드카가 실행 중에 죽는 경우, 999곡 목록의 성능, 우클릭 새로고침, `install_yt_dlp`·`update_yt_dlp`의 실제 호출.
```

- [ ] **Step 3: 설계 문서를 갱신한다**

`docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md`에서:

§5 표의 4단계 행을 아래로 바꾼다:

```markdown
| 4 | 데스크톱 기능: 폴더 선택, 탐색기 열기, `settings.json`, 단일 인스턴스, 종료 연동 | README의 "알려진 제한" 항목과 동작이 같다 ([계획서](../plans/2026-10-11-tauri-desktop-features.md), [보고서](2026-10-11-tauri-desktop-features-report.md): 통과). 앱 데이터 폴더의 기본값이 `%APPDATA%\xGetSongs`(Compose와 같음)가 됐다 |
```

§2 표에 두 행을 더한다(`가사용 JavaFX 웹뷰` 행 앞):

```markdown
| `settings.json`을 쓰는 곳 | **Rust 셸 한 곳**(`sidecar-host`의 `settings` 모듈, 4단계에서 정함). 서버는 이 파일을 만지지 않는다 | 쓰는 곳이 둘이면 충돌한다(§6). 형식은 Compose 앱의 것 그대로다 |
| 앱 데이터 폴더 | `%APPDATA%\xGetSongs`(없으면 `~\.xgetsongs`), `XGS_SIDECAR_APPDATA`로 바꿀 수 있다(4단계에서 정함) | Compose 앱과 같은 yt-dlp·설정을 쓴다. 사이드카가 시작할 때 `work\`를 지우므로 Compose 앱과 셸을 동시에 실행하면 안 된다 |
```

§6 위험 표의 "`settings.json`을 쓰는 곳이 둘이 되면 충돌한다" 행의 대응 칸 끝에 ` (정함: Rust 셸 한 곳, 4단계)`를 붙인다.

- [ ] **Step 4: 메모리를 갱신한다**

`C:\Users\gomto\.claude\projects\C--Projects-xGetSongs\memory\project-tauri-migration.md`의 상태에 4단계 완료(관문 D1~D8, 커밋 범위, 보고서 경로, 일반 빌드를 쓸 때 Compose 앱을 끄라는 안내, 다음은 5단계 계획서)를 적고 `MEMORY.md`의 색인 줄을 맞춘다.

- [ ] **Step 5: Commit**

```bash
git add docs
git commit -m "문서: 데스크톱 기능(4단계)의 종단 확인 결과와 설계의 4단계 행"
```

---

## Self-Review

**1. Spec coverage** (설계 §5의 4단계, §6의 위험)

- "폴더 선택": Task 5(`pick_folder`)·6(`pickFolder`)·7(단추)·8(`picker-check.ps1`). "탐색기 열기": Task 3(`folder`)·5(`open_folder`)·7·8(`explorer-check.ps1`). "`settings.json`": Task 2·5·6·7·8(`settings-check.ps1`). "단일 인스턴스": Task 5·8(`single-instance.ps1`). "종료 연동": Task 4(`close_gate`)·5(`on_window_event`)·6(`onClose`)·7(`flushSettings`)·8(`settings-check.ps1`, `hang-check.ps1`).
- 완료 증거 "README의 '알려진 제한' 항목과 동작이 같다": 그 목록에서 4단계가 닿는 것은 (a) "ffmpeg는 PATH 또는 `%APPDATA%\xGetSongs\bin`에서 찾는다" → 앱 데이터 기본값을 Compose와 같게 한 것(Task 1·5), (b) "앱을 두 번 실행하지 마세요" → 단일 인스턴스(Task 5·8)로 셸끼리는 막았고 Compose 앱과의 동시 실행은 같은 제한으로 남는다, (c) "이벤트 연결이 끊기면 다시 붙을 수 없고 취소만 된다" → 3단계에서 옮긴 동작이 그대로다. README 자체의 수정은 6단계다.
- §6의 "`settings.json`을 쓰는 곳이 둘이 되면 충돌한다": 쓰는 곳을 Rust 셸 한 곳으로 정했고(위 결정 표, Task 9 Step 3에서 설계 문서에 적는다) 서버가 파일을 만지지 않음을 확인했다.
- 이 계획 밖(의도적): 5단계의 포장·단일 exe·로그 폴더 정책, 6단계의 `app/` 삭제와 README, `tauri-plugin-opener`의 정리, Compose 앱과의 동시 실행 방지.

**2. Placeholder scan:** 코드 단계는 이 계획을 쓰며 실제로 쓰고 돌려 본 코드를 담았다(`sidecar-host` 116개 시험과 변형 3종 확인, TS 239개, 브라우저 시나리오 A~F `pass: true`, 실제 셸에서 단일 인스턴스·설정 2종·멈춘 페이지·탐색기·폴더 선택·3단계 화면 점검·시작 직후 닫기 10번·강제 종료). 수정되는 파일은 저장소의 파일에 `git apply`되는 diff이고, 계획을 만들 때 모든 diff를 저장소 파일에 적용해 검증된 파일과 같음을 확인했다. 보고서의 빈 칸은 측정값을 채우는 기록 칸이다.

**3. Type consistency:** Rust: `app_data_dir`(Task 1)는 `SidecarConfig::from_lookup`과 셸의 `SettingsFile::new(app_data_dir(..).join("settings.json"))`이 쓴다. `SettingsFile`은 `Clone`이라 명령이 `State`에서 복제해 `spawn_blocking`으로 옮긴다. `CloseDecision::{Close, Ask, Wait}`는 `on_window_event`의 `match`와 같다. 명령 이름(`load_settings`, `save_settings`, `pick_folder`, `open_folder`, `page_listens_for_close`, `close_window`)과 이벤트 이름(`xgs-close-requested`)은 Rust(`lib.rs`), `desktop.ts`(`CLOSE_REQUESTED_EVENT`), `desktop.test.ts`가 같다. `UserSettings`의 JSON(camelCase)은 Rust의 `serde(rename_all)`, `types.ts`의 `UserSettings`, `normalizeSettings`가 같다. 화면의 요소 이름(`#pick-folder`, `#open-folder`, `#options`, `#options-more`, `#destination-row`)은 `index.html`, `main.ts`, 시나리오 F, `check-screen.ts`가 같다. 점검 신호 이름(`picker-cancelled`, `picker-chosen`, `explorer-checked`)과 환경 변수 `XGS_CHECK_DESKTOP`의 값은 Rust(`check_wait`, `check_env`), `check.ts`, `check-screen.ts`, 스크립트가 같다.

**4. Review Focus:** 다섯 항목은 위 Review Focus 절에 적은 Task의 시험과 확인에 대응한다. 1번(두 번째 실행)은 계획을 쓰며 3단계 빌드와 대조해 위험이 실제임을 확인했다. 2번은 변경 직후 닫아 파일을 읽는 방식이라 닫을 때의 쓰기만이 값을 넣을 수 있다. 3번은 사용자의 실제 파일의 복사본으로 확인한다. 4번과 5번은 시스템 대화상자와 파일 관리자까지 실제로 조작해 확인한다.

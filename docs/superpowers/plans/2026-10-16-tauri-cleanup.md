# 정리와 마무리 (6단계) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 마이그레이션을 끝낸다. Compose 앱(`app/`)을 지우고, 그것을 위해 있던 Gradle 설정과 의존성을 걷어 내고, 사이드카의 진단 로그에 남은 "UI"라는 말을 바로잡고, 쓰지 않는 Tauri 플러그인 권한을 없애고, README와 설계 문서를 지금의 앱(Tauri 셸 + Kotlin 사이드카)에 맞춘다. 끝나면 `gradlew check`가 통과하고 `package-exe.bat`으로 만든 exe가 5단계와 똑같이 깨끗한 PC 점검을 통과한다.

**Architecture:** 구조는 바꾸지 않는다. 지우는 것(`app/`)은 어떤 모듈도 의존하지 않는 잎이고(`settings.gradle.kts`의 `include(":app")` 말고는 참조가 없다. 계획을 쓰며 대조했다), 그 시험 187개가 맡던 일은 이미 TypeScript(`holder.test.ts`, `labels.test.ts`)와 Rust(`settings.rs`, `folder.rs`, `bridge.rs`)의 시험으로 옮겨져 있다. 시험이 먼저 실패하는 것을 보는 작업(이름 정리, 권한)은 그렇게 하고, 지우기와 문서는 기준선과 결과의 숫자를 증거로 쓴다.

**Tech Stack:** Gradle 9.5.1(Kotlin), Node 24 `node --test`, Rust(셸), PowerShell 7 + UI Automation(점검). **새 의존성은 없다**(오히려 Compose, `google()` 저장소, `tauri-plugin-opener`가 빠진다).

**Spec:** [docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md](../specs/2026-10-10-tauri-kotlin-sidecar-design.md) §5의 6단계("`app/` 삭제, README·설계 문서 갱신", 완료 증거: `gradlew check` 통과)와 §7("5~6단계에서 README의 '구조', '실행', '문제가 생겼을 때'(로그 위치)와 이 문서의 §4 기술 스택·모듈 구조를 고친다"). 앞 단계의 보고서가 6단계에 넘긴 것: 로그 문구의 "UI" 정리([1단계 보고서](../specs/2026-10-10-tauri-sidecar-complete-report.md)), 점검 페이지를 지울지의 결정([2단계 보고서](../specs/2026-10-11-tauri-rust-bridge-report.md)), 쓰지 않는 `tauri-plugin-opener`와 그 권한의 정리([4단계 계획서](2026-10-11-tauri-desktop-features.md)).

## 이 계획이 내린 결정

설계 문서가 6단계에 맡긴 것과 앞 단계가 넘긴 것의 경계다. 사용자가 계획서를 검토할 때 바꿀 수 있다.

| 결정 | 내용 | 근거 |
|---|---|---|
| 지우는 범위 | `app/`(추적되는 파일 29개, 4,329줄, 시험 187개)와 추적되지 않는 `app/bin`(VS Code의 Java 확장이 만든 것, 아래)을 폴더째 지운다. `settings.gradle.kts`의 `include(":app")`, 루트 `build.gradle.kts`의 Compose 플러그인 둘, 버전 카탈로그의 Compose 항목 전부(버전 둘, 라이브러리 `compose-*` 넷과 `kotlinx-coroutines-swing`·`ktor-client-core`·`ktor-client-mock`, 플러그인 둘), 두 저장소 선언의 `google()`도 걷는다 | `app`만 쓰던 것들이다(카탈로그 항목을 모듈별로 대조했다). `google()`은 Compose의 androidx 의존성을 위한 것이었다. **마지막으로 `app/`이 있던 커밋 `f9dbcba`에서 원본을 읽을 수 있다**(`git show f9dbcba:app/...`) |
| 추적되지 않는 `bin/` 파일 109개 | `.gitignore`에 `/*/bin/`을 더한다(최상위 모듈 폴더의 `bin/`만: `sidecar-host/src/bin/xgs-pack.rs`는 추적되는 소스라서 그냥 `bin/`으로 하면 안 된다). `engine/bin`, `server/bin`, `diagnostics/bin`의 파일은 지우지 않는다 | 사용자의 VS Code(Java 확장 = Eclipse JDT)가 Gradle 프로젝트를 읽으며 만든 것이다: 모듈 폴더 아래 `bin/`에 Kotlin 소스(`.kt` 105개)와 리소스(`.xml` 4개)의 **복사본**이 있다(`engine` 64, `server` 29, `diagnostics` 14, `app` 2 = 109). 빌드 결과도 제품도 아니고 IDE가 다시 만들며, 무시 규칙이 없어서 소스 제어 목록에 "스테이지에 올라가지 않은 파일"로 보인다. 이름을 바꾸는 작업(Task 3)에서는 옛 이름의 복사본이 거기 남지만 아무도 읽지 않는다 |
| 지운 뒤의 시험 수 | `gradlew check`: 1,125개 → **938개**(187개가 `app`의 것: `AppStateHolderTest` 95, `LabelsTest` 35, `JsonSettingsStoreTest` 22, `LogbackConfigTest` 12, `HttpXgsApiTest` 12, `ExplorerOpenerTest` 10, `EndToEndTest` 1). 계획을 쓰며 깨끗한 사본에서 둘 다 오프라인으로 돌렸다 | 앞의 넷 중 셋은 같은 이름으로 TypeScript·Rust에 옮겨 졌고(95, 35, 22+2, 10+2) `LogbackConfigTest`는 `app`의 `logback.xml` 시험이라 사이드카의 `SidecarLogbackConfigTest`가 맡는다. **Kotlin 클라이언트와 서버의 계약 시험**(`HttpXgsApiTest`, `EndToEndTest`)은 같은 자리에 대신 가는 것이 없다: 지금 서버의 클라이언트는 Rust 브리지이고 그 시험은 `bridge.rs`의 가짜 서버(28개)와 `#[ignore]`된 `real_sidecar`(실제 이미지)다. 이 계획은 `real_sidecar`를 관문에서 돌려 그 자리를 채운다(Review Focus 1) |
| 원본 시험을 지키는 시험 | `shell/tests/kotlin-coverage.test.ts`에서 `app/`의 Kotlin 파일을 읽던 항목(Kotlin 둘, Rust 포트 둘)과 건너뛰기 논리, 거기에만 쓰이던 도우미와 그 시험 둘을 지운다. **`shared`의 둘은 그대로 남는다.** 포팅한 파일의 머리 주석(7곳)은 지운 파일을 `git show f9dbcba:<경로>`로 읽을 수 있다고 적는다 | 원본이 없으면 "원본에 시험이 늘면 포트도 늘려라"는 점검이 할 일이 없다. 옮겨 간 시험들은 그냥 시험으로 남는다. 건너뛰기 논리는 이 단계를 위해 4단계에 넣어 둔 것이다 |
| "UI" 이름 정리 | 진단의 멈춤 감시(`UiWatchdog`, `postToUi`, `onUiBeat`, 로그 문구 "UI 스레드가…", 덤프 파일 `ui-hang-*.txt`, 스레드 이름 `xgs-ui-watchdog`)에서 "UI"를 뺀다: `HangWatchdog`, `postBeat`, `onBeat`, "서버 스레드가 N초 동안 응답하지 않음", `hang-*.txt`, `xgs-hang-watchdog`. 영어 주석도 맞춘다(10개 파일, 시험 포함, 스크립트로) | 1단계 보고서가 "사이드카에서는 'UI'가 서버 응답을 뜻하므로 6단계에서 정리한다"고 넘겼다. 사이드카에는 UI 스레드가 없고 지금 이 문구는 사용자가 보내 주는 로그에서 화면이 멈춘 것으로 읽힌다. **바뀌는 동작은 덤프 파일 이름 하나다**: 예전 `ui-hang-*.txt`는 지워지지 않고 남는다(20개 보관 규칙이 새 이름만 센다) |
| 건드리지 않는 진단 동작 | 덤프에서 `AWT-EventQueue` 스레드를 먼저 보이는 정렬, 시작 기록의 `jpackage.app-version`·`logDirectoryCandidates`(앱 옆 `log`를 먼저 보는 후보) | 동작이다(이름이 아니다). 사이드카는 자기 후보(`sidecarLogCandidates`)를 쓰고 AWT 스레드는 가사 검색의 JavaFX 말고는 없다. 바꾸려면 시험을 바꾸는 동작 변경이므로 이 단계 밖 |
| 점검 페이지(`check.html`)와 점검용 명령 | **남긴다.** 지우지 않는다 | 2~5단계의 모든 종단 점검(단일 인스턴스, 닫기, 설정, 깨끗한 PC)이 이것으로 돈다. 지우면 제품을 고칠 때 확인할 길이 없어진다. 일반 빌드에서는 `XGS_BRIDGE_CHECK_FILE`이 없으면 모두 거부한다(4단계). 대신 `shell/README.md`에 무엇이고 어떻게 쓰는지 적는다 |
| `tauri-plugin-opener` | 걷어 낸다: `Cargo.toml`, `lib.rs`의 `.plugin(...)`, 권한 `opener:default`, npm 패키지 `@tauri-apps/plugin-opener`. 대신 **페이지의 권한이 `core:default`뿐이고 페이지가 플러그인을 쓰지 않는다는 시험**을 둔다(`capabilities.test.ts`) | 페이지는 이 플러그인을 한 번도 부르지 않는다(`grep`). 권한을 가진 채 두면 웹뷰의 스크립트가 URL·경로 열기 API를 부를 수 있다. 4단계가 "정리할 것"으로 넘겼다 |
| `run.bat` | 옛 `gradlew :app:run` 대신 **`dist\xGetSongs.exe`를 실행한다(없으면 `package-exe.bat`으로 먼저 만든다)**. 소스를 고친 뒤에는 `package-exe.bat`을 직접 돌린다 | 옛 `run.bat`은 "소스에서 실행"이었다. 지금 앱을 실행하는 가장 단순한 길이고 5단계에서 점검된 것이다. `tauri dev`(핫 리로드)는 이 저장소에서 써 본 적이 없어 문서에 적지 않는다 |
| README | "필요한 것"(쓰는 PC와 만드는 PC를 나눈다), "실행", "테스트", "문제가 생겼을 때"(로그 위치와 실제 줄, `sidecar-stderr.log`, 풀기 실패), "구조", "알려진 제한"을 새로 쓴다. 저장 위치·태그·가사 검색은 그대로다. **쓴 문구는 실제 로그와 대조했다**(묶은 exe를 한 번 돌려 `xgetsongs.log`의 줄을 읽었다) | 설계 §7이 정한 것이다. 로그 줄(`사용자가 창 닫기를 요청함`, `내장 서버 정지`, `JVM 종료 시작: 사용자가 창을 닫음`)은 지금도 그대로 나온다 |
| `shell/README.md` | Tauri 템플릿 문구를 지우고 개발자용 설명(만들기, 시험, 환경 변수, 점검 페이지)을 쓴다 | 지금은 "This template should help get you started…"다 |
| 설계 문서 | 마이그레이션 설계: §4 저장소 배치(`diagnostics/` 추가, `app/`는 "지웠다"), §5 6단계 행에 계획서·보고서 링크. 처음 설계(`2026-10-04-xgetsongs-design.md`)는 **맨 위에 한 단락**만 더한다("화면이 Compose였을 때의 설계, 지금은 Tauri") | 처음 설계는 그때의 기록이다. 본문을 고치면 역사가 지워진다 |
| 이 단계가 하지 않는 것 | 사용자의 `docs/xGetSongs 작업 메모.txt`(개인 메모다. "단일 실행파일 구성: … 7-Zip SFX / Enigma Virtual Box"처럼 옛 내용이 있다), 옛 계획서·보고서(역사다), 구글 가사의 WebView2 교체(7단계, 선택), `jlink`, 서명·아이콘 | 범위 밖 |

## Global Constraints

- 서버·엔진·`shared`의 동작, 셸의 화면 TypeScript(`shell/src`)와 Rust 동작은 바꾸지 않는다. 이 단계의 변경은 위 결정 표의 것뿐이다(바뀌는 동작은 덤프 파일 이름과 페이지의 권한 하나씩).
- 코드와 코드 주석은 영어, 사용자에게 보이는 문구와 커밋 메시지는 한국어. `main`에 직접 커밋한다. 커밋 메시지 끝에는 이 세션이 정한 `Co-Authored-By` 줄을 붙인다. 푸시는 사용자에게 묻고 한다.
- Bash 도구는 명령 텍스트의 백슬래시를 한 번 더 해석하고 이 PC의 도구 셸에는 `cargo`가 PATH에 없다. 코드는 Write·Edit 도구로 쓰고 `cargo`, `npm`, `node`, Gradle은 PowerShell 도구로 부른다(PowerShell 호출마다 `$env:Path += ";$env:USERPROFILE\.cargo\bin"`). PowerShell 도구는 `Remove-Item`을 막는 일이 있으므로 도구 명령에서는 `[IO.File]::Delete`, `[IO.Directory]::Delete($path, $true)`를 쓴다.
- Gradle은 `--no-daemon --offline`으로만 부른다(공유 데몬을 끝냈다가 사용자의 앱이 죽은 적이 있고, 의존성은 이미 캐시에 있다). 네트워크가 필요하다는 오류가 나면 멈추고 사용자에게 묻는다.
- **환경 변수는 `Remove-Item "Env:$name"`으로 지운다.** PowerShell 7의 `[Environment]::SetEnvironmentVariable($name, $null)`은 지우지 않고 빈 문자열로 만든다.
- **창을 띄우는 확인(Task 6)을 시작하기 전에 사용자에게 알린다**("점검으로 창과 안내 상자가 잠깐 뜰 수 있습니다").
- 확인은 사용자의 `%APPDATA%\xGetSongs`, `%LOCALAPPDATA%\xGetSongs`, `~\Music\xGetSongs`를 건드리지 않는다. `run.bat`을 돌려 보는 확인은 `XGS_SIDECAR_APPDATA`에 임시 폴더를 주고, **사용자의 `xGetSongs.exe`가 이미 떠 있으면 건너뛴다**(단일 인스턴스라서 두 번째 실행이 그 창을 앞으로 가져온다).
- 이 계획의 diff는 LF 줄 끝 기준이다. 작업 폴더의 파일이 CRLF이면(셸의 `lib.rs`·`Cargo.toml`·`package.json`, `config.rs`, `README.md` 등) `git apply --ignore-whitespace --whitespace=nowarn`을 쓰거나 Edit 도구로 같은 내용을 넣는다. 새 파일은 LF로 쓴다(`run.bat`만 CRLF).
- 점검 스크립트는 `%TEMP%\xgs-bridge-check\`에 있다(저장소 밖). 5단계의 `clean-pc-helpers.ps1`, `clean-pc-check.ps1`, `packed-screen-check.ps1`, `measure-start.ps1`과 4단계의 `helpers.ps1`, `desktop-helpers.ps1`이 거기 있어야 한다. 없으면 [5단계 계획서](2026-10-15-tauri-packaging.md)와 [4단계 계획서](2026-10-11-tauri-desktop-features.md)의 같은 이름의 블록으로 만든다.

## Review Focus

계획의 시험이 직접 다루지 않아 사용자에게 먼저 문제가 될 것, 가능성이 높은 순서:

1. **`app`의 시험이 맡던 일이 어디서도 시험되지 않게 된다.** 특히 서버와 그 클라이언트 사이의 계약: Kotlin 쪽 `HttpXgsApiTest`·`EndToEndTest`가 사라지고 `gradlew check`에는 클라이언트가 없다. Rust 브리지의 가짜 서버가 진짜 서버와 어긋나도 `cargo test`는 통과한다. → 관문 C4에서 **`real_sidecar`(`#[ignore]`)를 실제 이미지로 돌린다**, `RoutesTest` 등 서버 쪽 시험은 그대로
2. **로그 문구를 바꿨는데 그것을 읽는 사람·도구가 틀어진다.** 사용자는 로그와 `ui-hang-*.txt`를 보내 준다. 새 이름은 `hang-*.txt`이고 예전 파일은 남는다. README와 코드의 옛 이름이 모두 바뀌었는지 → 관문 C6(`git grep`), 모든 시험(문구를 확인하는 시험이 같이 바뀐다)
3. **문서가 사실과 다르다.** README의 명령, 경로, 로그 줄, 오류 문구가 실제와 맞는지, 링크가 모두 있는지 → Task 5의 링크 점검과 문구 대조, Task 6의 `run.bat` 실행
4. **Gradle 정리가 다른 PC나 깨끗한 캐시에서 깨진다.** `google()`과 카탈로그 항목을 걷었다. 이 PC의 캐시로는 오프라인 빌드가 통과하지만 **빈 Gradle 캐시에서 처음 내려받는 것은 확인하지 않는다**(네트워크가 필요하고 이 단계는 내려받지 않는다). 걷은 것은 `app`만 쓰던 것이다(모듈별 대조) → 보고서의 한계에 적는다
5. **지운 폴더나 무시한 폴더에 지키고 싶던 것이 있었다.** 추적되지 않는 `app/bin`은 IDE가 만든 소스 복사본이고(내용은 `app/src`와 같다), 사용자의 설정은 앱 데이터 폴더에 있어서 `app/`에는 없다. `.gitignore`에 올리는 `/*/bin/`이 추적되는 소스를 가리지 않는지(`sidecar-host/src/bin/xgs-pack.rs`) → Task 1의 `git status`와 `git check-ignore`

## 이 단계의 관문

| 번호 | 조건 | 확인 |
|---|---|---|
| C1 | `app/`이 없다(추적되는 파일 0개, 폴더 없음), `settings.gradle.kts`에 `:app`이 없고 Gradle·카탈로그 파일 어디에도 `compose`, `google()`이 없다. `git status`에 `bin/` 파일이 나오지 않고 `sidecar-host/src/bin/xgs-pack.rs`는 무시되지 않는다 | Task 1 |
| C2 | `gradlew check`가 오프라인으로 통과한다: **938개**(`shared` 211, `engine` 444, `server` 125, `diagnostics` 158), 실패 0, 건너뜀 0. `:server:sidecarImageTest` 4개 통과 | Task 1, 3, 6 |
| C3 | TS: **239개**(242 − 지운 6(옛 포팅 점검 4와 그 도우미의 시험 2) + 새 3), 건너뜀 0, `tsc` 종료 코드 0 | Task 2, 4, 6 |
| C4 | `sidecar-host`: `lib` 161개, `pack_cli` 5개, **`real_sidecar`(`--ignored`)가 실제 이미지로 통과한다** | Task 6 |
| C5 | 셸이 `tauri-plugin-opener` 없이 일반 빌드와 점검용 빌드로 빌드되고 `shell/src-tauri/Cargo.lock`에서 그 크레이트와 그것만 쓰던 것이 빠진다(53줄). 페이지의 권한은 `core:default`뿐이다 | Task 4 |
| C6 | `git grep`(`docs/` 밖)에 `UiWatchdog`, `postToUi`, `onUiBeat`, `ui-hang`, `UI 스레드`, `UI 응답`, `UI 감시`, `xgs-ui-watchdog`, `:app:`이 없다. README·`shell/README.md`·설계 문서의 상대 링크가 모두 있다 | Task 3, 5, 6 |
| C7 | 5단계의 종단 점검이 그대로 통과한다: `package-exe.bat`이 `dist\xGetSongs.exe`를 만들고, 깨끗한 PC 점검 7개 시나리오 `ALL OK`, 실제 exe의 화면 점검 `ALL OK`, 두 번째 실행 사이드카 대기 5초 이하·첫 실행 10초 이하. `run.bat`이 임시 앱 데이터로 창을 연다 | Task 6 |
| C8 | 전체 브랜치 리뷰(가장 유능한 모델)에서 Critical·Important가 없거나 고쳐진다 | 실행 마지막 |

## File Structure

| 파일 | 책임 |
|---|---|
| `app/` (삭제) | Compose 앱. 마지막으로 있던 커밋은 `f9dbcba` |
| `settings.gradle.kts`, `build.gradle.kts`, `gradle/libs.versions.toml`, `.gitignore` (수정) | `:app`, Compose 플러그인·라이브러리, `google()`을 걷는다 |
| `run.bat` (수정) | `dist\xGetSongs.exe`를 실행한다 |
| `shell/tests/kotlin-coverage.test.ts` (수정), 포팅한 파일 7개의 머리 주석 (수정) | `app/` 의존을 걷고 원본을 읽는 길을 적는다 |
| `diagnostics/src/**`, `server/src/**/sidecar/SidecarDiagnostics*.kt` (수정, 파일 둘 이름 바꿈) | `UiWatchdog` → `HangWatchdog`와 문구 |
| `shell/tests/capabilities.test.ts` (새) | 페이지의 권한과 플러그인 사용을 고정한다 |
| `shell/src-tauri/{Cargo.toml,Cargo.lock,src/lib.rs,capabilities/default.json}`, `shell/package.json`, `shell/package-lock.json` (수정) | `tauri-plugin-opener`를 걷는다 |
| `README.md`, `shell/README.md`, 설계 문서 둘 (수정) | 지금의 앱에 맞춘 문서 |
| `%TEMP%\xgs-bridge-check\count-tests.mjs`, `ui-rename.mjs`, `links.mjs` (저장소 밖) | 시험 개수 세기, 이름 정리, 링크 점검 |
| `docs/superpowers/specs/2026-10-16-tauri-cleanup-report.md` (새) | 종단 확인 결과 |

---

### Task 1: Compose 앱을 지운다

**Files:**
- Delete: `app/`
- Modify: `settings.gradle.kts`, `build.gradle.kts`, `gradle/libs.versions.toml`, `run.bat`, `.gitignore`
- Create (저장소 밖): `%TEMP%\xgs-bridge-check\count-tests.mjs`

**Interfaces:**
- Consumes: 없다(`app`을 쓰는 모듈은 없다).
- Produces (뒤의 Task가 쓴다): `:app` 프로젝트가 없는 Gradle 빌드, 새 `run.bat`, 시험 개수 세는 도구 `node count-tests.mjs <저장소>`(Gradle 시험 결과 XML을 모듈과 시험 과제별로 센다).

삭제는 시험을 먼저 쓸 대상이 아니다. 기준선과 삭제 뒤의 개수가 증거다.

- [ ] **Step 1: 개수 세는 도구를 두고 기준선을 잰다**

`%TEMP%\xgs-bridge-check\count-tests.mjs`:

```javascript
// Sums the JUnit XML results that `gradlew check` left in a checkout, per module and per test task. TOTAL counts only the tasks that
// `check` runs (test, desktopTest, jvmTest); the results of other test tasks (integrationTest, sidecarImageTest) that someone ran in the
// same checkout earlier are listed but not added.
//   node count-tests.mjs <repository folder>
import { readdirSync, readFileSync, statSync } from "node:fs";
import { join } from "node:path";

const root = process.argv[2];
const totals = new Map();

function walk(dir) {
  for (const name of readdirSync(dir)) {
    const path = join(dir, name);
    if (["node_modules", ".git", ".gradle", "target", "dist"].includes(name)) continue;
    const info = statSync(path);
    if (info.isDirectory()) walk(path);
    else if (name.startsWith("TEST-") && name.endsWith(".xml") && path.includes("test-results")) {
      const head = readFileSync(path, "utf8").slice(0, 600);
      const read = (attribute) => Number(new RegExp(`${attribute}="(\\d+)"`).exec(head)?.[1] ?? 0);
      const relative = path.slice(root.length + 1).replaceAll("\\", "/");
      const [module] = relative.split("/");
      const task = relative.split("test-results/")[1].split("/")[0];
      const key = `${module}:${task}`;
      const total = totals.get(key) ?? { tests: 0, failures: 0, errors: 0, skipped: 0 };
      total.tests += read("tests");
      total.failures += read("failures");
      total.errors += read("errors");
      total.skipped += read("skipped");
      totals.set(key, total);
    }
  }
}

walk(root);
const CHECK_TASKS = ["test", "desktopTest", "jvmTest"];
let all = 0;
let skipped = 0;
for (const [key, total] of [...totals].sort()) {
  const counted = CHECK_TASKS.includes(key.split(":")[1]);
  console.log(`${key.padEnd(28)} tests ${String(total.tests).padStart(4)}  failures ${total.failures}  errors ${total.errors}  skipped ${total.skipped}${counted ? "" : "  (not part of check)"}`);
  if (counted) {
    all += total.tests;
    skipped += total.skipped;
  }
}
console.log(`${"TOTAL".padEnd(28)} tests ${String(all).padStart(4)}  skipped ${skipped}`);
```

```powershell
Set-Location C:\Projects\xGetSongs
git status --short
& .\gradlew.bat --no-daemon --offline check *> $env:TEMP\xgs-s6-check-before.log; "exit=$LASTEXITCODE"
Select-String -Path $env:TEMP\xgs-s6-check-before.log -Pattern "BUILD"
node $env:TEMP\xgs-bridge-check\count-tests.mjs C:\Projects\xGetSongs
```

Expected: `git status --short`에 추적되지 않는 것은 `app/bin/`, `diagnostics/bin/`, `engine/bin/`, `server/bin/`뿐이다(109개 파일: VS Code의 Java 확장이 만든 소스 복사본. 이 단계가 지우는 것은 `app/bin`뿐이고 나머지는 `.gitignore`에 올려 보이지 않게 한다). `exit=0`, `BUILD SUCCESSFUL`, 개수: `app:desktopTest 187`, `diagnostics:test 158`, `engine:test 444`, `server:test 125`, `shared:jvmTest 211`, `TOTAL 1125`, `skipped 0`.

- [ ] **Step 2: `app/`을 지운다**

```powershell
Set-Location C:\Projects\xGetSongs
git rm -r -q app
if (Test-Path app) { [IO.Directory]::Delete("C:\Projects\xGetSongs\app", $true) }   # what git leaves: app\bin (not tracked) and app\build (ignored)
"app exists: $(Test-Path app); tracked files of app: $(@(git ls-files app).Count)"
```

Expected: `app exists: False; tracked files of app: 0`.

- [ ] **Step 3: Gradle 설정과 `run.bat`을 고친다**

```diff
diff --git a/settings.gradle.kts b/settings.gradle.kts
index b263c9a..2c71e6c 100644
--- a/settings.gradle.kts
+++ b/settings.gradle.kts
@@ -4,14 +4,12 @@ pluginManagement {
     repositories {
         gradlePluginPortal()
         mavenCentral()
-        google()
     }
 }
 
 dependencyResolutionManagement {
     repositories {
         mavenCentral()
-        google()
     }
 }
 
@@ -19,4 +17,3 @@ include(":shared")
 include(":diagnostics")
 include(":engine")
 include(":server")
-include(":app")
```

```diff
diff --git a/build.gradle.kts b/build.gradle.kts
index 68870a3..77a2590 100644
--- a/build.gradle.kts
+++ b/build.gradle.kts
@@ -2,6 +2,4 @@ plugins {
     alias(libs.plugins.kotlin.multiplatform) apply false
     alias(libs.plugins.kotlin.jvm) apply false
     alias(libs.plugins.kotlin.serialization) apply false
-    alias(libs.plugins.kotlin.compose) apply false
-    alias(libs.plugins.compose.multiplatform) apply false
 }
```

```diff
diff --git a/gradle/libs.versions.toml b/gradle/libs.versions.toml
index 5bd6b81..c7abcfb 100644
--- a/gradle/libs.versions.toml
+++ b/gradle/libs.versions.toml
@@ -1,7 +1,5 @@
 [versions]
 kotlin = "2.4.20"
-compose = "1.12.1"
-composeMaterial3 = "1.9.0"
 ktor = "3.6.0"
 coroutines = "1.11.0"
 serialization = "1.11.0"
@@ -13,7 +11,6 @@ javafx = "21.0.12"
 [libraries]
 kotlinx-coroutines-core = { module = "org.jetbrains.kotlinx:kotlinx-coroutines-core", version.ref = "coroutines" }
 kotlinx-coroutines-test = { module = "org.jetbrains.kotlinx:kotlinx-coroutines-test", version.ref = "coroutines" }
-kotlinx-coroutines-swing = { module = "org.jetbrains.kotlinx:kotlinx-coroutines-swing", version.ref = "coroutines" }
 kotlinx-serialization-json = { module = "org.jetbrains.kotlinx:kotlinx-serialization-json", version.ref = "serialization" }
 
 ktor-server-core = { module = "io.ktor:ktor-server-core", version.ref = "ktor" }
@@ -24,15 +21,8 @@ ktor-server-status-pages = { module = "io.ktor:ktor-server-status-pages", versio
 ktor-server-test-host = { module = "io.ktor:ktor-server-test-host", version.ref = "ktor" }
 ktor-serialization-kotlinx-json = { module = "io.ktor:ktor-serialization-kotlinx-json", version.ref = "ktor" }
 
-ktor-client-core = { module = "io.ktor:ktor-client-core", version.ref = "ktor" }
 ktor-client-cio = { module = "io.ktor:ktor-client-cio", version.ref = "ktor" }
 ktor-client-content-negotiation = { module = "io.ktor:ktor-client-content-negotiation", version.ref = "ktor" }
-ktor-client-mock = { module = "io.ktor:ktor-client-mock", version.ref = "ktor" }
-
-compose-runtime = { module = "org.jetbrains.compose.runtime:runtime", version.ref = "compose" }
-compose-foundation = { module = "org.jetbrains.compose.foundation:foundation", version.ref = "compose" }
-compose-ui = { module = "org.jetbrains.compose.ui:ui", version.ref = "compose" }
-compose-material3 = { module = "org.jetbrains.compose.material3:material3", version.ref = "composeMaterial3" }
 
 logback-classic = { module = "ch.qos.logback:logback-classic", version.ref = "logback" }
 slf4j-api = { module = "org.slf4j:slf4j-api", version.ref = "slf4j" }
@@ -43,5 +33,3 @@ jsoup = { module = "org.jsoup:jsoup", version.ref = "jsoup" }
 kotlin-multiplatform = { id = "org.jetbrains.kotlin.multiplatform", version.ref = "kotlin" }
 kotlin-jvm = { id = "org.jetbrains.kotlin.jvm", version.ref = "kotlin" }
 kotlin-serialization = { id = "org.jetbrains.kotlin.plugin.serialization", version.ref = "kotlin" }
-kotlin-compose = { id = "org.jetbrains.kotlin.plugin.compose", version.ref = "kotlin" }
-compose-multiplatform = { id = "org.jetbrains.compose", version.ref = "compose" }
```

```diff
diff --git a/run.bat b/run.bat
index ff0e7bb..a0bc65c 100644
--- a/run.bat
+++ b/run.bat
@@ -1 +1,3 @@
-.\gradlew.bat :app:run --no-daemon
+@echo off
+if not exist "%~dp0dist\xGetSongs.exe" call "%~dp0package-exe.bat" || exit /b 1
+start "" "%~dp0dist\xGetSongs.exe"
```

```diff
diff --git a/.gitignore b/.gitignore
index 4cbbd40..ded25fb 100644
--- a/.gitignore
+++ b/.gitignore
@@ -8,3 +8,5 @@ out/
 .vscode/
 /log/
 /dist/
+# The output of the Java extension of VS Code: copies of the sources of the Kotlin modules (not the build of the project)
+/*/bin/
```

- [ ] **Step 4: 통과와 개수를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs
& .\gradlew.bat --no-daemon --offline check *> $env:TEMP\xgs-s6-check-after.log; "exit=$LASTEXITCODE"
Select-String -Path $env:TEMP\xgs-s6-check-after.log -Pattern "BUILD"
node $env:TEMP\xgs-bridge-check\count-tests.mjs C:\Projects\xGetSongs
git grep -n -i -E "compose|google\(\)|:app" -- "*.kts" "*.toml" ; "grep exit=$LASTEXITCODE (1 = nothing found)"
git status --short
git check-ignore -v engine/bin/main/x.kt; git check-ignore -v sidecar-host/src/bin/xgs-pack.rs; "ignored: $LASTEXITCODE (1 = the source is not ignored)"
```

Expected: `exit=0`, `BUILD SUCCESSFUL`, 개수: `diagnostics:test 158`, `engine:test 444`, `server:test 125`, `shared:jvmTest 211`, `TOTAL 938`, `skipped 0`(`app:desktopTest` 줄은 없다). `git grep`은 아무것도 찾지 못하고 `grep exit=1`이다. `git status --short`는 앞 Step의 변경(삭제, 수정 다섯 파일)만 보이고 **`bin/` 파일은 하나도 나오지 않는다**(109개가 사라진다). `git check-ignore`는 `engine/bin/main/x.kt`를 `.gitignore`의 `/*/bin/` 규칙으로 보고하고 `sidecar-host/src/bin/xgs-pack.rs`는 아무것도 보고하지 않는다(`ignored: 1`).

- [ ] **Step 5: 커밋한다**

```powershell
Set-Location C:\Projects\xGetSongs; git add settings.gradle.kts build.gradle.kts gradle/libs.versions.toml run.bat .gitignore; git status --short; git commit -m "chore: Compose 앱(app/)을 지운다

마이그레이션의 마지막 단계다. 화면은 Tauri 셸로 옮겨졌고 app/의 시험이 맡던 일은 TypeScript와 Rust의 시험으로 옮겨져 있다(마지막으로 app/이 있던 커밋은 f9dbcba). settings.gradle.kts의 :app, 루트의 Compose 플러그인, 버전 카탈로그의 Compose·swing·ktor-client-core·ktor-client-mock, google() 저장소를 걷는다. run.bat은 dist\xGetSongs.exe를 실행한다. gradlew check: 1,125개에서 938개.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: 원본이 사라진 포팅 점검과 머리 주석

**Files:**
- Modify: `shell/tests/kotlin-coverage.test.ts`, `shell/tests/holder.test.ts`, `shell/tests/labels.test.ts`, `shell/src/state/holder.ts`, `shell/src/state/labels.ts`, `shell/src/state/types.ts`, `sidecar-host/src/folder.rs`, `sidecar-host/src/settings.rs`

**Interfaces:**
- Consumes (Task 1): `app/`이 없는 저장소.
- Produces: `kotlin-coverage.test.ts`는 `shared`의 두 Kotlin 시험 파일만 대조한다. 포팅한 파일의 머리 주석이 `git show f9dbcba:<경로>`를 가리킨다.

- [ ] **Step 1: 지금 상태를 본다 (건너뛴 시험)**

```powershell
Set-Location C:\Projects\xGetSongs\shell
node --test "tests/*.test.ts" 2>&1 | Select-String '^ℹ (tests|pass|fail|skipped)'
```

Expected: `tests 242`, `pass 238`, `fail 0`, **`skipped 4`**. `app/`의 Kotlin 파일이 없어 `LabelsTest`, `AppStateHolderTest`와 Rust 포트 둘의 점검이 건너뛰어진다: 건너뛰는 시험은 아무것도 지키지 않으면서 통과처럼 보인다.

- [ ] **Step 2: 점검을 `shared`로 줄이고 머리 주석을 고친다**

```diff
diff --git a/shell/tests/kotlin-coverage.test.ts b/shell/tests/kotlin-coverage.test.ts
index ead5066..78850c7 100644
--- a/shell/tests/kotlin-coverage.test.ts
+++ b/shell/tests/kotlin-coverage.test.ts
@@ -1,12 +1,12 @@
-// Guards the port: every test of the Kotlin suites that these files were ported from has a test of the same name here (the Rust
-// ports under the same name in snake case). The Kotlin files are the specification; when one of them gains a test, this fails until
-// the port gains it too.
+// Guards the port: every test of the Kotlin suites of `shared/` that `src/logic` was ported from has a test of the same name here. The
+// Kotlin files are the specification; when one of them gains a test, this fails until the port gains it too.
 //
-// It looks the Kotlin files up in the repository. When `app/` is gone (stage 6 of the migration deletes it) the app's suites are
-// skipped; `shared/` stays, so the two suites of `shared` keep being compared.
+// The suites of the Compose app (`app/`), which `holder.test.ts` and `labels.test.ts` and the Rust ports in `sidecar-host` were ported
+// from, are gone with the app (stage 6 of the migration deleted it): they can be read in the git history (`git show f9dbcba:app/src/...`),
+// and what was ported from them is now tested only by the ports themselves.
 
 import assert from "node:assert/strict";
-import { existsSync, readFileSync } from "node:fs";
+import { readFileSync } from "node:fs";
 import test from "node:test";
 
 const repository = new URL("../../", import.meta.url);
@@ -19,16 +19,12 @@ function kotlinTests(path: URL): string[] {
 const PORTS: ReadonlyArray<readonly [kotlin: string, port: string]> = [
   ["shared/src/commonTest/kotlin/com/xgetsongs/shared/filename/FilenameFormatterTest.kt", "filename.test.ts"],
   ["shared/src/commonTest/kotlin/com/xgetsongs/shared/input/InputClassifierTest.kt", "input.test.ts"],
-  ["app/src/commonTest/kotlin/com/xgetsongs/app/state/LabelsTest.kt", "labels.test.ts"],
-  ["app/src/commonTest/kotlin/com/xgetsongs/app/state/AppStateHolderTest.kt", "holder.test.ts"],
 ];
 
 for (const [kotlin, port] of PORTS) {
-  const kotlinPath = new URL(kotlin, repository);
-  // Only the app's suites may be absent (stage 6 deletes `app/`). A missing `shared` file means it was moved or renamed: fail.
-  const skip = kotlin.startsWith("app/") && !existsSync(kotlinPath) && "the Kotlin file is not in this checkout";
-  test(`every test of ${kotlin.split("/").pop()} is ported to ${port}`, { skip }, () => {
-    const names = kotlinTests(kotlinPath);
+  test(`every test of ${kotlin.split("/").pop()} is ported to ${port}`, () => {
+    // A missing file means it was moved or renamed (readFileSync throws): fail.
+    const names = kotlinTests(new URL(kotlin, repository));
     const ported = new Set([...readFileSync(new URL(port, import.meta.url), "utf8").matchAll(/^test\("(\w+)"/gm)].map((match) => match[1]));
 
     assert.ok(names.length > 0, "the Kotlin file has tests");
@@ -39,70 +35,3 @@ for (const [kotlin, port] of PORTS) {
     );
   });
 }
-
-// The desktop code that moved to Rust (stage 4): the tests of the Kotlin files are ported to `#[test] fn` of the same name in snake
-// case (`aMissingFileGivesTheDefaults` is `a_missing_file_gives_the_defaults`), where Kotlin's `null` is Rust's `None` and what
-// Kotlin does not `throw` Rust does not `panic` at.
-const RUST_PORTS: ReadonlyArray<readonly [kotlin: string, rust: string]> = [
-  ["app/src/desktopTest/kotlin/com/xgetsongs/app/ExplorerOpenerTest.kt", "sidecar-host/src/folder.rs"],
-  ["app/src/desktopTest/kotlin/com/xgetsongs/app/JsonSettingsStoreTest.kt", "sidecar-host/src/settings.rs"],
-];
-
-/** `aPathBelowARegularFile` is `a_path_below_a_regular_file`; a word of one letter (`A`) is a word. */
-/** The names of the `#[test]` functions of a Rust file that run: not commented out, not `#[ignore]`d, and a `fn` that is a test at all. */
-export function rustTests(source: string): Set<string> {
-  const code = source.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/[^\n]*/g, "");
-  const names = new Set<string>();
-  for (const match of code.matchAll(/((?:#\[[^\]]*\]\s*)+)fn\s+(\w+)\s*\(/g)) {
-    if (/#\[test\]/.test(match[1]) && !/#\[ignore/.test(match[1])) names.add(match[2]);
-  }
-  return names;
-}
-
-const snakeCase = (name: string) =>
-  name
-    .replace(/([a-z0-9])([A-Z])/g, "$1_$2")
-    .replace(/([A-Z])([A-Z][a-z])/g, "$1_$2")
-    .toLowerCase();
-const rustName = (name: string) =>
-  snakeCase(name)
-    .split("_")
-    .map((word) => (word === "null" ? "none" : word === "throw" ? "panic" : word))
-    .join("_");
-
-for (const [kotlin, rust] of RUST_PORTS) {
-  const kotlinPath = new URL(kotlin, repository);
-  const skip = !existsSync(kotlinPath) && "the Kotlin file is not in this checkout";
-  test(`every test of ${kotlin.split("/").pop()} is ported to ${rust}`, { skip }, () => {
-    const names = kotlinTests(kotlinPath);
-    const ported = rustTests(readFileSync(new URL(rust, repository), "utf8"));
-
-    assert.ok(names.length > 0, "the Kotlin file has tests");
-    assert.deepEqual(
-      names.filter((name) => !ported.has(rustName(name))),
-      [],
-      `ported ${names.filter((name) => ported.has(rustName(name))).length} of ${names.length}`,
-    );
-  });
-}
-
-test("theNamesOfTheKotlinTestsBecomeTheNamesOfTheRustOnes", () => {
-  assert.equal(rustName("aMissingFileGivesTheDefaults"), "a_missing_file_gives_the_defaults");
-  assert.equal(rustName("aUtf16FileGivesTheDefaults"), "a_utf16_file_gives_the_defaults");
-  assert.equal(rustName("aPathBelowARegularFileReturnsTheDirectoryAroundTheFile"), "a_path_below_a_regular_file_returns_the_directory_around_the_file");
-  assert.equal(rustName("aBlankPathReturnsNull"), "a_blank_path_returns_none");
-  assert.equal(rustName("saveOverADirectoryThatIsNotEmptyDoesNotThrow"), "save_over_a_directory_that_is_not_empty_does_not_panic");
-});
-
-test("onlyTestsThatRunCountAsPortedOnes", () => {
-  const source = [
-    "#[test]\nfn runs() {}",
-    "#[test]\n#[ignore]\nfn ignored() {}",
-    "// #[test]\n// fn commented() {}",
-    "/* #[test]\nfn blocked() {} */",
-    "fn helper() {}",
-    "#[test]\n#[should_panic]\nfn also_runs() {}",
-  ].join("\n");
-
-  assert.deepEqual([...rustTests(source)].sort(), ["also_runs", "runs"]);
-});
```

```diff
diff --git a/shell/tests/holder.test.ts b/shell/tests/holder.test.ts
index 423c848..58d55c4 100644
--- a/shell/tests/holder.test.ts
+++ b/shell/tests/holder.test.ts
@@ -1,5 +1,6 @@
-// The scenarios of app/src/commonTest/kotlin/com/xgetsongs/app/state/AppStateHolderTest.kt, one test per Kotlin test and under
-// the same name (tests/kotlin-coverage.test.ts checks that none is missing). Kotlin's `runCurrent()` is `await flush()`,
+// The scenarios of app/src/commonTest/kotlin/com/xgetsongs/app/state/AppStateHolderTest.kt (the Compose app, deleted in stage 6 of the
+// migration: `git show f9dbcba:<that path>`), one test per Kotlin test and under the same name (tests/kotlin-coverage.test.ts checked
+// that none was missing while the file existed). Kotlin's `runCurrent()` is `await flush()`,
 // `advanceTimeBy` is `timers.advance`, and the test time source is `clock.ms`.
 
 import assert from "node:assert/strict";
```

```diff
diff --git a/shell/tests/labels.test.ts b/shell/tests/labels.test.ts
index f832539..928cb70 100644
--- a/shell/tests/labels.test.ts
+++ b/shell/tests/labels.test.ts
@@ -1,5 +1,5 @@
-// The cases of app/src/commonTest/kotlin/com/xgetsongs/app/state/LabelsTest.kt, one test per Kotlin test and under the same
-// name. (Kotlin's `Duration` is milliseconds here.)
+// The cases of app/src/commonTest/kotlin/com/xgetsongs/app/state/LabelsTest.kt (the Compose app, deleted in stage 6 of the migration:
+// `git show f9dbcba:<that path>`), one test per Kotlin test and under the same name. (Kotlin's `Duration` is milliseconds here.)
 
 import assert from "node:assert/strict";
 import test from "node:test";
```

```diff
diff --git a/shell/src/state/holder.ts b/shell/src/state/holder.ts
index ee4a79f..dc9017e 100644
--- a/shell/src/state/holder.ts
+++ b/shell/src/state/holder.ts
@@ -1,4 +1,5 @@
-// Ported from app/src/commonMain/kotlin/com/xgetsongs/app/state/AppStateHolder.kt: all the screen logic. The DOM code only
+// Ported from app/src/commonMain/kotlin/com/xgetsongs/app/state/AppStateHolder.kt (the Compose app, deleted in stage 6 of the
+// migration: `git show f9dbcba:<that path>`): all the screen logic. The DOM code only
 // renders `getState()` and calls these functions. Coroutines became promises; the 400 ms wait before the options are saved and
 // the clock behind the elapsed time are given in, so that the tests run in no time at all.
 
```

```diff
diff --git a/shell/src/state/labels.ts b/shell/src/state/labels.ts
index 43a1ab6..d841282 100644
--- a/shell/src/state/labels.ts
+++ b/shell/src/state/labels.ts
@@ -1,4 +1,5 @@
-// Ported from app/src/commonMain/kotlin/com/xgetsongs/app/state/Labels.kt: the texts of the screen.
+// Ported from app/src/commonMain/kotlin/com/xgetsongs/app/state/Labels.kt (the Compose app, deleted in stage 6 of the migration:
+// `git show f9dbcba:<that path>`): the texts of the screen.
 
 import type { LyricsOutcome } from "../bridge/types.ts";
 import { MAX_RANK, MIN_RANK, destinationFolder } from "../logic/filename.ts";
```

```diff
diff --git a/shell/src/state/types.ts b/shell/src/state/types.ts
index dd33718..d2dd082 100644
--- a/shell/src/state/types.ts
+++ b/shell/src/state/types.ts
@@ -1,4 +1,5 @@
-// Ported from app/src/commonMain/kotlin/com/xgetsongs/app/state/UiState.kt and app/.../settings/*.kt.
+// Ported from app/src/commonMain/kotlin/com/xgetsongs/app/state/UiState.kt and app/.../settings/*.kt (the Compose app, deleted in
+// stage 6 of the migration: `git show f9dbcba:<that path>`).
 
 import type {
   ActionResult,
```

```diff
diff --git a/sidecar-host/src/folder.rs b/sidecar-host/src/folder.rs
index b986072..0c15a73 100644
--- a/sidecar-host/src/folder.rs
+++ b/sidecar-host/src/folder.rs
@@ -1,5 +1,6 @@
-//! Folders for the screen. Ported from `app/src/desktopMain/kotlin/com/xgetsongs/app/ExplorerOpener.kt` and `FolderPicker.kt`: the
-//! tests of this file are the cases of `ExplorerOpenerTest.kt`, one for one and under the same name in snake case.
+//! Folders for the screen. Ported from `app/src/desktopMain/kotlin/com/xgetsongs/app/ExplorerOpener.kt` and `FolderPicker.kt` (the Compose
+//! app, deleted in stage 6 of the migration: `git show f9dbcba:<that path>`): the tests of this file are the cases of
+//! `ExplorerOpenerTest.kt`, one for one and under the same name in snake case.
 
 use std::path::{Path, PathBuf};
 use std::process::Command;
```

```diff
diff --git a/sidecar-host/src/settings.rs b/sidecar-host/src/settings.rs
index 8035666..0eb2e82 100644
--- a/sidecar-host/src/settings.rs
+++ b/sidecar-host/src/settings.rs
@@ -1,7 +1,7 @@
 //! The options the app restores on the next start, kept in `settings.json` in the app data folder. Ported from
 //! `app/src/commonMain/kotlin/com/xgetsongs/app/settings/UserSettings.kt` and `app/src/desktopMain/kotlin/com/xgetsongs/app/JsonSettingsStore.kt`:
 //! the file is the one the Compose app wrote and reads, and the tests of this file are the cases of `JsonSettingsStoreTest.kt`, one for
-//! one and under the same name in snake case.
+//! one and under the same name in snake case. (The Compose app is deleted since stage 6 of the migration: `git show f9dbcba:<that path>`.)
 //!
 //! This is the only writer of the file: the sidecar never touches it.
 
```

- [ ] **Step 3: 통과를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"
Set-Location C:\Projects\xGetSongs\shell
node --test "tests/*.test.ts" 2>&1 | Select-String '^ℹ (tests|pass|fail|skipped)'
npx tsc --noEmit; "tsc exit=$LASTEXITCODE"
Set-Location C:\Projects\xGetSongs\sidecar-host
cargo test --offline 2>&1 | Select-String '^test result|FAILED'
```

Expected: `tests 236`, `pass 236`, `fail 0`, **`skipped 0`**. `tsc exit=0`. `sidecar-host`: `lib`이 `161 passed`(머리 주석만 바뀌었다), `pack_cli` `5 passed`, `#[ignore]` 둘.

- [ ] **Step 4: 커밋한다**

```powershell
Set-Location C:\Projects\xGetSongs; git add shell/tests shell/src/state sidecar-host/src/folder.rs sidecar-host/src/settings.rs; git commit -m "test: app/이 없어진 뒤의 포팅 점검과 포팅한 파일의 머리 주석

kotlin-coverage는 shared의 Kotlin 시험 둘만 대조한다(원본이 없으면 건너뛰는 시험은 아무것도 지키지 않는다). 포팅한 파일 7곳의 머리 주석은 지운 원본을 git show f9dbcba:<경로>로 읽을 수 있다고 적는다.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: 진단의 "UI" 이름과 문구를 바로잡는다

**Files:**
- Modify: `diagnostics/src/main/kotlin/com/xgetsongs/diagnostics/{Diagnostics,HangDetector,ThreadDump,UiWatchdog}.kt`, `diagnostics/src/test/kotlin/com/xgetsongs/diagnostics/{DiagnosticsTest,HangDetectorTest,ThreadDumpTest,UiWatchdogTest}.kt`, `server/src/main/kotlin/com/xgetsongs/server/sidecar/SidecarDiagnostics.kt`, `server/src/test/kotlin/com/xgetsongs/server/sidecar/SidecarDiagnosticsTest.kt`
- Rename: `UiWatchdog.kt` → `HangWatchdog.kt`, `UiWatchdogTest.kt` → `HangWatchdogTest.kt`
- Create (저장소 밖): `%TEMP%\xgs-bridge-check\ui-rename.mjs`

**Interfaces:**
- Consumes: 없다.
- Produces: `Diagnostics.start(logDir, postBeat = …, …)`(전: `postToUi`), `HangWatchdog`(전: `UiWatchdog`), `HangDetector.onBeat`(전: `onUiBeat`), 덤프 파일 `hang-yyyyMMdd-HHmmss.txt`, 스레드 `xgs-hang-watchdog`, 로그 문구 `서버 스레드가 N초 동안 응답하지 않음` / `서버 스레드 무응답` / `서버 스레드 응답 회복 (총 N초)` / `멈춤 감시: …`.

바뀌는 동작은 덤프 파일 이름 하나다. 시험이 문구와 이름을 확인하므로 **시험부터 새 이름으로 바꿔 컴파일이 실패하는 것을 보고**, 그다음 본 코드를 바꾼다.

- [ ] **Step 1: 이름 정리 스크립트를 두고 점검 실행한다**

`%TEMP%\xgs-bridge-check\ui-rename.mjs`:

```javascript
// The hang watchdog of the diagnostics was written for the Compose app, whose UI thread it watched. The sidecar has no UI thread: what it
// watches is the server's own threads (`Dispatchers.Default`). This renames what says "UI" there: the identifiers, the texts of the log
// lines, the name of the thread and the dump files (`ui-hang-*.txt` -> `hang-*.txt`), and the English comments.
//   node s6-ui-rename.mjs <checkout> [--part tests|main|all] [--dry]
// `--part tests` renames the tests only (they then fail to compile: the red step), `--part main` the rest (the green step).
import { existsSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const root = process.argv[2];
const dry = process.argv.includes("--dry");
const part = process.argv.includes("--part") ? process.argv[process.argv.indexOf("--part") + 1] : "all";
if (!["tests", "main", "all"].includes(part)) throw new Error(`--part is tests, main or all, not ${part}`);
const isTest = (file) => file.includes("/src/test/");
const wanted = (file) => part === "all" || (part === "tests") === isTest(file);

const DIAG = "diagnostics/src/main/kotlin/com/xgetsongs/diagnostics";
const DIAG_TEST = "diagnostics/src/test/kotlin/com/xgetsongs/diagnostics";
const SERVER = "server/src/main/kotlin/com/xgetsongs/server/sidecar";
const SERVER_TEST = "server/src/test/kotlin/com/xgetsongs/server/sidecar";

// Applied to every file, in this order.
const GENERIC = [
  ["UiWatchdogTest", "HangWatchdogTest"],
  ["UiWatchdog", "HangWatchdog"],
  ["postToUi", "postBeat"],
  ["onUiBeat", "onBeat"],
  ["xgs-ui-watchdog", "xgs-hang-watchdog"],
  ["ui-hang-", "hang-"],
  ["UI 스레드가", "서버 스레드가"],
  ["UI 스레드 무응답", "서버 스레드 무응답"],
  ["UI 응답 회복", "서버 스레드 응답 회복"],
  ["UI 감시", "멈춤 감시"],
];

// The English comments that talk about a UI thread, one by one (the sentence is the old text, so a change of the file in between shows).
const COMMENTS = {
  [`${DIAG}/Diagnostics.kt`]: [
    [" * when the UI thread stops answering. [start]", " * when the server's threads stop answering. [start]"],
    [
      "     * [postBeat] runs a Runnable on the thread whose answers the watchdog waits for (the app: the AWT event queue; the\n     * sidecar: a coroutine dispatcher).",
      "     * [postBeat] runs a Runnable on the thread whose answers the watchdog waits for (the sidecar: a coroutine dispatcher).",
    ],
  ],
  [`${DIAG}/HangDetector.kt`]: [
    ["/** What [HangDetector.onTick] has to tell about the UI thread. */", "/** What [HangDetector.onTick] has to tell about the watched thread. */"],
    ["/** The UI thread beat again after a reported hang;", "/** The watched thread beat again after a reported hang;"],
    [
      " * Decides from two kinds of calls whether the UI thread has stopped answering. [onBeat] is what a task posted to the UI\n * thread calls when it runs;",
      " * Decides from two kinds of calls whether the watched thread has stopped answering. [onBeat] is what a task posted to that\n * thread calls when it runs;",
    ],
    ["or null while the UI thread is not known to be hung. */", "or null while the watched thread is not known to be hung. */"],
  ],
  [`${DIAG}/ThreadDump.kt`]: [
    [" * The text that goes into the log and into `hang-*.txt` when the UI thread stops answering:", " * The text that goes into the log and into `hang-*.txt` when the watched thread stops answering:"],
  ],
  [`${DIAG}/UiWatchdog.kt`]: [
    [" * Notices when the UI thread stops answering and leaves", " * Notices when the watched thread stops answering and leaves"],
    [
      " * daemon thread named `xgs-hang-watchdog` posts a heartbeat to the UI thread through [postBeat] (in the app that is\n * `EventQueue::invokeLater`: the Compose Desktop UI runs on the AWT event queue thread) and asks a [HangDetector] what it\n * makes of the beats so far. A new heartbeat is only posted when the previous one has run, so the queue of a blocked UI\n * thread does not grow.",
      " * daemon thread named `xgs-hang-watchdog` posts a heartbeat to the watched thread through [postBeat] (in the sidecar that is\n * a coroutine on `Dispatchers.Default`) and asks a [HangDetector] what it makes of the beats so far. A new heartbeat is only\n * posted when the previous one has run, so the queue of a blocked thread does not grow.",
    ],
    [" * When the UI thread has not answered for [thresholdMs]", " * When the watched thread has not answered for [thresholdMs]"],
    [" * When the UI thread answers again the log says how long it took.", " * When the watched thread answers again the log says how long it took."],
    ["    /** True from the posting of a heartbeat until it has run on the UI thread. */", "    /** True from the posting of a heartbeat until it has run on the watched thread. */"],
  ],
  [`${DIAG_TEST}/UiWatchdogTest.kt`]: [
    [
      "when it wants the \"UI thread\" to answer, so the",
      "when it wants the watched thread to answer, so the",
    ],
    ["fun aFrozenUiGetsOneDumpFileAndOneErrorRecord()", "fun aFrozenThreadGetsOneDumpFileAndOneErrorRecord()"],
    ["    /** The UI thread gets to its queue:", "    /** The watched thread gets to its queue:"],
    ["in the queue of the blocked UI thread\"", "in the queue of the blocked thread\""],
    ["fun theLogSaysWhenTheUiAnswersAgain()", "fun theLogSaysWhenTheThreadAnswersAgain()"],
    ["// the UI thread was blocked until now", "// the thread was blocked until now"],
    ["fun aUiThatAnswersImmediatelyNeverGetsADump()", "fun aThreadThatAnswersImmediatelyNeverGetsADump()"],
    ["// the \"UI\" answers at once", "// the thread answers at once"],
    ["\"ui-hang\" !in errors[i]", "\"hang-\" !in errors[i]"],
  ],
  [`${DIAG_TEST}/HangDetectorTest.kt`]: [["// the UI thread came back after 7.5 s", "// the thread came back after 7.5 s"]],
  [`${SERVER}/SidecarDiagnostics.kt`]: [
    [
      " * What the Compose app's `Main` does around its window, for the sidecar: the log folder is chosen and handed to logback,\n * then the diagnostics (startup record, uncaught exceptions, exit record, `last-run.txt`, hang dumps) are started.\n *\n * The sidecar has no UI thread. What can hang here is the work the server does on `Dispatchers.Default` (downloads, the\n * event streams), so the watchdog's heartbeats go there: when every worker of that dispatcher is stuck the heartbeat does\n * not run, and the thread dump of the stuck workers is written the way the app's UI hang dump is.",
      " * What the sidecar does before anything else: the log folder is chosen and handed to logback, then the diagnostics (startup\n * record, uncaught exceptions, exit record, `last-run.txt`, hang dumps) are started.\n *\n * What can hang here is the work the server does on `Dispatchers.Default` (downloads, the event streams), so the watchdog's\n * heartbeats go there: when every worker of that dispatcher is stuck the heartbeat does not run, and the thread dump of the\n * stuck workers is written to `hang-*.txt`.",
    ],
  ],
};

const FILES = [
  `${DIAG}/Diagnostics.kt`,
  `${DIAG}/HangDetector.kt`,
  `${DIAG}/ThreadDump.kt`,
  `${DIAG}/UiWatchdog.kt`,
  `${DIAG_TEST}/UiWatchdogTest.kt`,
  `${DIAG_TEST}/HangDetectorTest.kt`,
  `${DIAG_TEST}/ThreadDumpTest.kt`,
  `${DIAG_TEST}/DiagnosticsTest.kt`,
  `${SERVER}/SidecarDiagnostics.kt`,
  `${SERVER_TEST}/SidecarDiagnosticsTest.kt`,
];
const RENAMES = [
  [`${DIAG}/UiWatchdog.kt`, `${DIAG}/HangWatchdog.kt`],
  [`${DIAG_TEST}/UiWatchdogTest.kt`, `${DIAG_TEST}/HangWatchdogTest.kt`],
];

for (const file of FILES.filter(wanted)) {
  const path = join(root, file);
  let text = readFileSync(path, "utf8");
  const eol = text.includes("\r\n") ? "\r\n" : "\n";
  text = text.replace(/\r\n/g, "\n");
  for (const [from, to] of GENERIC) text = text.replaceAll(from, to);
  for (const [from, to] of COMMENTS[file] ?? []) {
    // the comment texts were written against the text after the generic renames (`[postBeat]`, `hang-*.txt`)
    if (!text.includes(from)) throw new Error(`${file}: comment not found: ${from.slice(0, 70)}`);
    text = text.replace(from, () => to);
  }
  if (!dry) writeFileSync(path, text.replace(/\n/g, eol), "utf8");
}
if (!dry) {
  for (const [from, to] of RENAMES.filter(([old]) => wanted(old))) {
    if (!existsSync(join(root, from))) throw new Error(`no ${from}`);
    renameSync(join(root, from), join(root, to));
  }
}
console.log(dry ? "dry run: every text was found" : "renamed");
```

```powershell
Set-Location C:\Projects\xGetSongs
node $env:TEMP\xgs-bridge-check\ui-rename.mjs C:\Projects\xGetSongs --dry
```

Expected: `dry run: every text was found`(바꿀 문장이 모두 지금 코드에 있다는 뜻. 하나라도 없으면 오류로 멈춘다).

- [ ] **Step 2: 시험만 새 이름으로 바꾸고 실패를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs
node $env:TEMP\xgs-bridge-check\ui-rename.mjs C:\Projects\xGetSongs --part tests
& .\gradlew.bat --no-daemon --offline :diagnostics:test :server:test *> $env:TEMP\xgs-s6-ui-red.log; "exit=$LASTEXITCODE"
Select-String -Path $env:TEMP\xgs-s6-ui-red.log -Pattern "FAILED"
(Select-String -Path $env:TEMP\xgs-s6-ui-red.log -Pattern "^e: ").Count
(Select-String -Path $env:TEMP\xgs-s6-ui-red.log -Pattern "^e: ")[1].Line -replace 'file:///.*?/diagnostics/', 'diagnostics/'
```

Expected: `exit=1`, `> Task :diagnostics:compileTestKotlin FAILED`, `e:` 줄 **53개**, 둘째 줄이 `diagnostics/src/test/kotlin/com/xgetsongs/diagnostics/DiagnosticsTest.kt:407:67 No parameter with name 'postBeat' found.`(시험이 새 이름을 쓰는데 본 코드는 아직 옛 이름이다).

- [ ] **Step 3: 본 코드를 바꾸고 통과를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs
node $env:TEMP\xgs-bridge-check\ui-rename.mjs C:\Projects\xGetSongs --part main
& .\gradlew.bat --no-daemon --offline check *> $env:TEMP\xgs-s6-ui-green.log; "exit=$LASTEXITCODE"
Select-String -Path $env:TEMP\xgs-s6-ui-green.log -Pattern "BUILD|FAILED"
node $env:TEMP\xgs-bridge-check\count-tests.mjs C:\Projects\xGetSongs
```

Expected: `exit=0`, `BUILD SUCCESSFUL`, `diagnostics:test 158`, `server:test 125`, `TOTAL 938`, `skipped 0`(개수는 그대로: 이름만 바뀌었다).

- [ ] **Step 4: 남은 옛 이름을 찾는다**

```powershell
Set-Location C:\Projects\xGetSongs
git grep -n -E "UiWatchdog|postToUi|onUiBeat|ui-hang|UI 스레드|UI 응답|UI 감시|xgs-ui-watchdog" -- . ':!docs' ; "grep exit=$LASTEXITCODE"
git add -A diagnostics/src server/src; git status --short
```

Expected: `git grep`은 **`README.md`의 두 줄**(`ui-hang-날짜-시각.txt`, `ui-hang-*.txt`)만 찾는다(README는 Task 5에서 고친다). `diagnostics/`와 `server/`에는 없다. `git status --short`에 이름 바꿈 둘(`R  diagnostics/src/main/…/UiWatchdog.kt -> …/HangWatchdog.kt`, `R  diagnostics/src/test/…/UiWatchdogTest.kt -> …/HangWatchdogTest.kt`)과 수정 8개가 보인다. 변경 줄 수(참고): `HangWatchdog.kt` +26 −27, `HangWatchdogTest.kt` +46 −46, `HangDetectorTest.kt` +13 −13, `Diagnostics.kt` +6 −7, `SidecarDiagnostics.kt` +6 −6, `HangDetector.kt` +5 −5, `ThreadDumpTest.kt`·`DiagnosticsTest.kt` +3 −3, `ThreadDump.kt`·`SidecarDiagnosticsTest.kt` +1 −1.

- [ ] **Step 5: 사이드카 이미지로 실제로 도는지 본다**

```powershell
Set-Location C:\Projects\xGetSongs
& .\gradlew.bat --no-daemon --offline :server:sidecarImageTest *> $env:TEMP\xgs-s6-image.log; "exit=$LASTEXITCODE"
Select-String -Path $env:TEMP\xgs-s6-image.log -Pattern "PASSED|FAILED|BUILD"
```

Expected: `exit=0`, 시험 4개 `PASSED`, `BUILD SUCCESSFUL`.

- [ ] **Step 6: 커밋한다**

```powershell
Set-Location C:\Projects\xGetSongs; git commit -m "refactor(diagnostics): 멈춤 감시에서 UI라는 말을 뺀다

사이드카에는 UI 스레드가 없고 감시하는 것은 서버의 스레드다. UiWatchdog를 HangWatchdog로, postToUi를 postBeat로, onUiBeat를 onBeat로, 로그 문구를 '서버 스레드가 N초 동안 응답하지 않음'으로, 덤프 파일을 hang-*.txt로, 스레드 이름을 xgs-hang-watchdog로 바꾼다. 동작은 같고 덤프 파일 이름만 다르다(예전 ui-hang-*.txt는 지워지지 않고 남는다).

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: 쓰지 않는 Tauri 플러그인(`opener`)과 그 권한을 걷는다

**Files:**
- Create: `shell/tests/capabilities.test.ts`
- Modify: `shell/src-tauri/src/lib.rs`, `shell/src-tauri/Cargo.toml`, `shell/src-tauri/Cargo.lock`, `shell/src-tauri/capabilities/default.json`, `shell/package.json`, `shell/package-lock.json`

**Interfaces:**
- Consumes: 없다.
- Produces: 페이지가 가진 권한은 `core:default`뿐이다. 셸이 쓰는 플러그인은 `tauri-plugin-dialog`(Rust에서만)와 `tauri-plugin-single-instance`뿐이다.

- [ ] **Step 1: 실패하는 시험을 쓴다**

```typescript
// What the page may do. The webview gets the permissions of the capability file and nothing else; every plugin permission in it is a
// door for the page into that plugin. The shell uses the dialog plugin from Rust only (`pick_folder`) and no plugin from the page, so the
// page needs `core:default` (invoke and events) and no more. Adding a plugin to the page is a decision: this test says so.

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const read = (path: string) => readFileSync(new URL(path, import.meta.url), "utf8");

test("thePageIsGivenNoPluginPermissions", () => {
  const capability = JSON.parse(read("../src-tauri/capabilities/default.json")) as { permissions: string[] };

  assert.deepEqual(capability.permissions, ["core:default"]);
});

test("thePageUsesNoTauriPlugin", () => {
  const manifest = JSON.parse(read("../package.json")) as { dependencies: Record<string, string> };

  assert.deepEqual(Object.keys(manifest.dependencies), ["@tauri-apps/api"]);
});

test("theShellUsesNoPluginThatNothingCalls", () => {
  const cargo = read("../src-tauri/Cargo.toml");
  const shell = read("../src-tauri/src/lib.rs");

  // The plugins the shell registers are the plugins it depends on, and the other way round.
  const registered = [...shell.matchAll(/\.plugin\(\s*(tauri_plugin_[a-z_]+)::/g)].map((match) => match[1].replaceAll("_", "-")).sort();
  const depended = [...cargo.matchAll(/^(tauri-plugin-[a-z-]+)\s*=/gm)].map((match) => match[1]).sort();

  assert.deepEqual(registered, depended);
  assert.deepEqual(depended, ["tauri-plugin-dialog", "tauri-plugin-single-instance"]);
});
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell
node --test tests/capabilities.test.ts 2>&1 | Select-String '^ℹ (tests|pass|fail)|^✖ |^\s+\+ '
```

Expected: `tests 3`, `pass 0`, `fail 3`; 실패의 `+ actual` 줄이 각각 `'opener:default'`, `'@tauri-apps/plugin-opener'`, `'tauri-plugin-opener',`.

- [ ] **Step 3: 걷어 낸다**

```diff
diff --git a/shell/src-tauri/src/lib.rs b/shell/src-tauri/src/lib.rs
index e0023f3..f3255d1 100644
--- a/shell/src-tauri/src/lib.rs
+++ b/shell/src-tauri/src/lib.rs
@@ -240,7 +240,6 @@ pub fn run() {
                 let _ = window.set_focus();
             }
         }))
-        .plugin(tauri_plugin_opener::init())
         // Used from Rust only (`pick_folder`); the page is given no `dialog:*` permission. Note that the plugin's init script replaces
         // `window.alert` and `window.confirm` of every page with calls to a command that this app does not allow: `alert()` does nothing
         // and `confirm()` returns a promise. The page uses neither; do not start to.
```

```diff
diff --git a/shell/src-tauri/Cargo.toml b/shell/src-tauri/Cargo.toml
index b2604b0..0209594 100644
--- a/shell/src-tauri/Cargo.toml
+++ b/shell/src-tauri/Cargo.toml
@@ -19,7 +19,6 @@ tauri-build = { version = "2", features = [] }
 
 [dependencies]
 tauri = { version = "2", features = [] }
-tauri-plugin-opener = "2"
 tauri-plugin-dialog = "2"
 tauri-plugin-single-instance = "2"
 serde = { version = "1", features = ["derive"] }
```

```diff
diff --git a/shell/src-tauri/capabilities/default.json b/shell/src-tauri/capabilities/default.json
index 4cdbf49..b9ec4c1 100644
--- a/shell/src-tauri/capabilities/default.json
+++ b/shell/src-tauri/capabilities/default.json
@@ -4,7 +4,6 @@
   "description": "Capability for the main window",
   "windows": ["main"],
   "permissions": [
-    "core:default",
-    "opener:default"
+    "core:default"
   ]
 }
```

npm 패키지는 명령으로 걷는다(실제 `node_modules`에서도 빠진다):

```powershell
Set-Location C:\Projects\xGetSongs\shell
npm uninstall @tauri-apps/plugin-opener --offline 2>&1 | Select-Object -Last 3
git diff --stat -- package.json package-lock.json
```

Expected: `package.json` 3줄 추가·4줄 삭제(의존성 줄이 빠지고 npm이 `vite` 줄의 탭 들여쓰기를 공백으로 바로잡는다), `package-lock.json` 1줄 추가·11줄 삭제. 결과(참고):

```diff
diff --git a/shell/package.json b/shell/package.json
index 0720d0c..4c148db 100644
--- a/shell/package.json
+++ b/shell/package.json
@@ -10,12 +10,11 @@
     "tauri": "tauri"
   },
   "dependencies": {
-    "@tauri-apps/api": "^2",
-    "@tauri-apps/plugin-opener": "^2"
+    "@tauri-apps/api": "^2"
   },
   "devDependencies": {
     "@tauri-apps/cli": "^2",
-		"vite": "^8.0.16",
-    "typescript": "~6.0.3"
+    "typescript": "~6.0.3",
+    "vite": "^8.0.16"
   }
 }
```

```diff
diff --git a/shell/package-lock.json b/shell/package-lock.json
index 33f59e6..4483d77 100644
--- a/shell/package-lock.json
+++ b/shell/package-lock.json
@@ -8,8 +8,7 @@
       "name": "shell",
       "version": "0.1.0",
       "dependencies": {
-        "@tauri-apps/api": "^2",
-        "@tauri-apps/plugin-opener": "^2"
+        "@tauri-apps/api": "^2"
       },
       "devDependencies": {
         "@tauri-apps/cli": "^2",
@@ -516,15 +515,6 @@
         "node": ">= 10"
       }
     },
-    "node_modules/@tauri-apps/plugin-opener": {
-      "version": "2.7.0",
-      "resolved": "https://registry.npmjs.org/@tauri-apps/plugin-opener/-/plugin-opener-2.7.0.tgz",
-      "integrity": "sha512-mBorYfVKh9Lt7s4ZFtKbMUTxBw+Y/sVmSlKCtkkoBgMTC/DIbt/kaPm67L/2djAUqDI1wbixy5OHHVmipjuGMw==",
-      "license": "MIT OR Apache-2.0",
-      "dependencies": {
-        "@tauri-apps/api": "^2.12.0"
-      }
-    },
     "node_modules/detect-libc": {
       "version": "2.1.2",
       "resolved": "https://registry.npmjs.org/detect-libc/-/detect-libc-2.1.2.tgz",
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; $env:CARGO_NET_OFFLINE = 'true'
Set-Location C:\Projects\xGetSongs\shell
node --test "tests/*.test.ts" 2>&1 | Select-String '^ℹ (tests|pass|fail|skipped)'
npx tsc --noEmit; "tsc exit=$LASTEXITCODE"
npm run tauri build -- --no-bundle *> $env:TEMP\xgs-s6-shell-build.log; "build exit=$LASTEXITCODE"
Select-String -Path $env:TEMP\xgs-s6-shell-build.log -Pattern "Finished|Built application|error"
git diff --stat -- src-tauri/Cargo.lock
```

Expected: `tests 239`, `pass 239`, `fail 0`, `skipped 0`; `tsc exit=0`; `build exit=0`, `Finished `release` profile`, `Built application at: …\shell.exe`(3~5분); `src-tauri/Cargo.lock | 53 ---…`, `1 file changed, 53 deletions(-)`(`tauri-plugin-opener`과 그것만 쓰던 크레이트). `Cargo.lock`에 더해지는 줄은 없다.

- [ ] **Step 5: 커밋한다**

```powershell
Set-Location C:\Projects\xGetSongs; git add shell/tests/capabilities.test.ts shell/src-tauri/src/lib.rs shell/src-tauri/Cargo.toml shell/src-tauri/Cargo.lock shell/src-tauri/capabilities/default.json shell/package.json shell/package-lock.json; git commit -m "chore(shell): 쓰지 않는 tauri-plugin-opener와 그 권한을 걷는다

페이지는 이 플러그인을 부르지 않는데 opener:default 권한이 있어 웹뷰의 스크립트가 URL·경로 열기를 부를 수 있었다. 페이지의 권한은 core:default뿐이고 페이지가 플러그인을 쓰지 않는다는 시험을 둔다.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: README와 설계 문서를 지금의 앱에 맞춘다

**Files:**
- Modify: `README.md`, `shell/README.md`, `docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md`, `docs/superpowers/specs/2026-10-04-xgetsongs-design.md`
- Create (저장소 밖): `%TEMP%\xgs-bridge-check\links.mjs`

**Interfaces:**
- Consumes (Task 3, 4): 새 덤프 파일 이름 `hang-*.txt`, 페이지 권한, 새 `run.bat`.
- Produces: 지금의 앱을 설명하는 문서. 쓴 문구는 실제와 대조됐다: 로그 줄(`사용자가 창 닫기를 요청함`, `내장 서버 정지`, `JVM 종료 시작: 사용자가 창을 닫음`)은 묶은 exe를 돌려 읽은 `xgetsongs.log`의 것이고, 오류 문구(`런타임을 풀지 못했습니다`, `자리에 놓지 못했습니다`, `checksum`)와 `sidecar-stderr.log`는 5단계의 코드와 점검의 것이다.

문서는 시험을 먼저 쓸 대상이 아니다. 링크 점검과 옛 이름 `git grep`이 증거다.

- [ ] **Step 1: 링크 점검 도구를 두고 지금 상태를 본다**

`%TEMP%\xgs-bridge-check\links.mjs`:

```javascript
// Every relative link of the given markdown files has to point at a file or folder that exists.
//   node links.mjs README.md shell/README.md ...      (paths as they are from the current folder)
import { existsSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";

let broken = 0;
let checked = 0;
for (const file of process.argv.slice(2)) {
  const text = readFileSync(file, "utf8");
  for (const match of text.matchAll(/\]\(([^)\s]+)\)/g)) {
    const target = match[1].split("#")[0];
    if (target === "" || /^[a-z][a-z0-9+.-]*:/i.test(target)) continue; // an anchor in the same file, or a URL
    checked += 1;
    if (!existsSync(join(dirname(file), decodeURIComponent(target)))) {
      console.log(`${file}: the link ${match[1]} points at nothing`);
      broken += 1;
    }
  }
}
console.log(broken === 0 ? `all ${checked} links exist` : `${broken} of ${checked} links are broken`);
process.exit(broken === 0 ? 0 : 1);
```

```powershell
Set-Location C:\Projects\xGetSongs
node $env:TEMP\xgs-bridge-check\links.mjs README.md shell/README.md docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md docs/superpowers/specs/2026-10-04-xgetsongs-design.md
git grep -n -E "ui-hang|:app" -- README.md shell/README.md
```

Expected: 링크 점검은 `all 16 links exist`(옛 문서의 링크는 모두 있다), `git grep`은 `README.md`의 `:app`(세 줄: 실행, 로그, 알려진 제한)과 `ui-hang`(두 줄)을 찾는다: 이것이 고칠 것이다.

- [ ] **Step 2: README를 고친다**

````diff
diff --git a/README.md b/README.md
index a8b217b..3f2d295 100644
--- a/README.md
+++ b/README.md
@@ -1,6 +1,6 @@
 # xGetSongs
 
-YouTube 재생목록 ID를 입력하면 목록의 모든 영상에서 오디오를 추출해 m4a로 저장하고, 영상 주소 1개를 입력하면 그 영상의 오디오만 m4a로 저장하는 Windows 데스크톱 앱입니다. 나중에 웹으로 옮길 수 있게 설계했습니다.
+YouTube 재생목록 ID를 입력하면 목록의 모든 영상에서 오디오를 추출해 m4a로 저장하고, 영상 주소 1개를 입력하면 그 영상의 오디오만 m4a로 저장하는 Windows 데스크톱 앱입니다. 화면은 Tauri(웹뷰)가 그리고, 다운로드와 태그 같은 일은 Kotlin으로 만든 서버(사이드카)가 합니다. 나중에 웹으로 옮길 수 있게 설계했습니다.
 
 오디오는 YouTube의 AAC 원본(약 130kbps, itag 140)을 재인코딩 없이 그대로 저장합니다. 음질이 좋아지는 방식은 아니고 원본 그대로라는 뜻입니다.
 
@@ -10,7 +10,7 @@ YouTube 재생목록 ID를 입력하면 목록의 모든 영상에서 오디오
 
 앱의 "파일명에 순번 포함" 옵션을 끄면 `{가수명} - {제목}.m4a`로 저장합니다(기본은 켜짐이고, 트랙 번호는 옵션과 상관없이 순위입니다). 순번이 없으면 한 재생목록에서 같은 가수와 제목의 영상이 같은 파일명이 될 수 있어서, "기존 파일 덮어쓰기"가 꺼져 있으면 하나만 저장하고 나머지는 건너뛰며 켜져 있으면 마지막에 끝난 것으로 덮어씁니다.
 
-설계 문서: [docs/superpowers/specs/2026-10-04-xgetsongs-design.md](docs/superpowers/specs/2026-10-04-xgetsongs-design.md)
+설계 문서: [docs/superpowers/specs/2026-10-04-xgetsongs-design.md](docs/superpowers/specs/2026-10-04-xgetsongs-design.md) (처음 설계, 화면이 Compose였을 때), 화면을 Tauri로 옮긴 설계: [docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md](docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md)
 
 ## 저장 위치
 
@@ -43,22 +43,26 @@ LRCLIB에도 없으면 구글 검색 결과의 가사 카드에서 가져옵니
 
 ## 필요한 것
 
+받은 `xGetSongs.exe`를 쓰는 PC에 필요한 것:
+
 | 도구 | 용도 | 설치 |
 |---|---|---|
-| JDK 21 | 빌드와 실행 | |
 | yt-dlp | 영상에서 오디오 추출 | 앱의 "yt-dlp 설치" 버튼 또는 `winget install yt-dlp.yt-dlp` |
 | ffmpeg | 태그와 커버 쓰기, 썸네일 변환 | `winget install Gyan.FFmpeg` |
 | Node.js 22+ 또는 Deno 2.3+ | yt-dlp가 YouTube를 읽는 데 필요 | `winget install OpenJS.NodeJS` |
+| Microsoft Edge WebView2 런타임 | 창을 그린다 | Windows 11과 업데이트된 Windows 10에는 들어 있다. 없으면 앱이 안내한다 |
+
+Java는 필요 없습니다(exe 안에 들어 있습니다). 앱 첫 화면 위쪽에서 yt-dlp, ffmpeg, Node.js(또는 Deno)의 설치 여부와 버전을 확인할 수 있습니다.
 
-앱 첫 화면 위쪽에서 세 도구의 설치 여부와 버전을 확인할 수 있습니다.
+소스에서 exe를 만드는 PC에는 JDK 21(Gradle과 서버), Node.js(화면), Rust(셸)가 필요합니다.
 
 ## 실행
 
 ```powershell
-.\gradlew.bat :app:run
+.\run.bat    # dist\xGetSongs.exe를 실행합니다(없으면 package-exe.bat으로 먼저 만듭니다)
 ```
 
-앱은 마지막에 쓴 옵션(출력 폴더, 덮어쓰기, 파일명에 순번 포함, 가사 인터넷 검색)을 `%APPDATA%\xGetSongs\settings.json`에 저장해 두었다가 다음 실행에 되살리며, 입력한 주소, 영상 1개의 순위 번호, 앨범명(폴더명)은 기억하지 않습니다.
+소스를 고친 뒤에는 `.\package-exe.bat`으로 exe를 다시 만드세요(아래 "하나의 exe로 묶기"). 앱은 마지막에 쓴 옵션(출력 폴더, 덮어쓰기, 파일명에 순번 포함, 가사 인터넷 검색)을 `%APPDATA%\xGetSongs\settings.json`에 저장해 두었다가 다음 실행에 되살리며, 입력한 주소, 영상 1개의 순위 번호, 앨범명(폴더명)은 기억하지 않습니다.
 
 동시에 받는 곡 수는 고르지 않고 서버가 정합니다. 이 컴퓨터의 프로세서(논리 코어) 수의 70%이고, 소수점은 버리며 최소 1개입니다(프로세서 12개이면 8개, 4개이면 2개). 작업 로그의 `concurrency=`에 실제로 쓴 수가 남습니다. 예전 `settings.json`에 있던 `concurrency` 값은 무시합니다.
 
@@ -67,10 +71,18 @@ LRCLIB에도 없으면 구글 검색 결과의 가사 카드에서 가져옵니
 ## 테스트
 
 ```powershell
-.\gradlew.bat check                    # 전체 단위 테스트
-.\gradlew.bat :engine:integrationTest  # 실제 yt-dlp, YouTube, LRCLIB, 구글 가사 카드를 쓰는 통합 테스트 (도구가 없거나 서비스가 응답하지 않으면 건너뜀)
+.\gradlew.bat check                     # Kotlin 단위 테스트 (shared, diagnostics, engine, server)
+.\gradlew.bat :engine:integrationTest   # 실제 yt-dlp, YouTube, LRCLIB, 구글 가사 카드를 쓰는 통합 테스트 (도구가 없거나 서비스가 응답하지 않으면 건너뜀)
+.\gradlew.bat :server:sidecarImageTest  # 만든 사이드카 이미지(번들 런타임)로 서버를 실제로 띄워 보는 시험
+
+cargo test --manifest-path sidecar-host\Cargo.toml   # Rust: 사이드카 제어, 브리지, 설정 파일, 묶은 exe 풀기 (Tauri 없이 빠르게 돈다)
+cd shell
+node --test "tests/*.test.ts"            # 화면 로직
+npx tsc --noEmit                         # 화면의 타입 검사
 ```
 
+화면과 셸을 개발하는 방법(환경 변수, 점검 페이지, 가짜 서버로 화면만 보기)은 [shell/README.md](shell/README.md)에 있습니다.
+
 ## 하나의 exe로 묶기 (Tauri 셸)
 
 ```powershell
@@ -87,34 +99,40 @@ LRCLIB에도 없으면 구글 검색 결과의 가사 카드에서 가져옵니
 
 ## 문제가 생겼을 때
 
-앱이 갑자기 사라지거나 멈췄다면 원인이 아래 파일에 남아 있을 수 있습니다.
+앱이 갑자기 사라지거나 멈췄다면 원인이 아래 파일에 남아 있을 수 있습니다. 로그는 서버(사이드카)가 씁니다.
 
-- **로그:** 앱 실행 위치의 `log\xgetsongs.log`. 소스 폴더에서 `gradlew :app:run`으로 실행하면 프로젝트 루트(`settings.gradle.kts`가 있는 폴더)의 `log`이고(git은 이 폴더를 무시합니다), 네이티브 실행 파일이면 그 파일이 있는 폴더, jar로 실행하면 jar가 있는 폴더의 `log`입니다. 실제로 쓰는 폴더는 로그의 시작 정보 `로그 폴더:` 줄에 있습니다. 날짜와 5MB 단위로 나뉘고(`xgetsongs.2026-10-07.0.log`처럼) 7일, 합계 50MB까지 보관합니다. 한 줄마다 바로 기록하므로 강제 종료돼도 마지막 줄이 남습니다. 시작 정보(Java, OS, 메모리, 도구 경로), 작업 시작과 옵션, 항목별 완료·건너뜀·실패와 사유, 작업 요약, 취소, 이벤트 연결이 끊긴 일(앱이 서버를 멈추는 중의 끊김은 제외), 처리되지 않은 예외를 적고, 토큰, 요청 헤더, 가사, 영상 제목과 파일 이름은 적지 않으며 오류 문구 속의 파일 경로와 파일 이름도 `<경로>`, `<파일명>`으로 가립니다. 콘솔에는 INFO 이상만 나오고, 파일에는 앱 자신의 DEBUG 줄(항목 시작 등)도 들어 있습니다. 그 `log` 폴더를 만들 수 없으면(설치 폴더가 읽기 전용인 경우 등) `%APPDATA%\xGetSongs\logs`에, 거기도 안 되면 `%TEMP%\xgetsongs-logs`에 씁니다. 예전 버전이 `%APPDATA%\xGetSongs\logs`에 남긴 파일은 옮기지 않습니다.
-- **`ui-hang-날짜-시각.txt`:** 같은 폴더에 생기는 파일입니다. 앱 창이 5초 넘게 응답하지 않으면 모든 스레드가 그때 무엇을 하고 있었는지(창 스레드가 맨 위)와 메모리·GC 정보를 여기에 저장하고 로그에도 ERROR로 남깁니다. 멈춤이 이어지면 같은 파일에 덧붙이되 간격을 30초, 60초, 120초…로 늘려 최대 30분으로 하고(로그에는 그때마다 한 줄만), 파일이 5MB에 이르면 `이후 덤프는 생략함(파일 크기 제한)` 한 줄을 쓰고 더 덧붙이지 않습니다. 다시 응답하면 로그에 `UI 응답 회복 (총 N초)`를 적습니다. 파일은 20개까지 보관합니다. 절전에서 깨어난 경우처럼 확인 주기가 크게 벌어진 때는 멈춤으로 보지 않습니다.
-- **`JVM 종료 시작` 줄:** 앱이 종료되기 시작할 때마다 로그에 남습니다. 창의 닫기 버튼을 눌렀다면 그 앞쪽에 `사용자가 창 닫기를 요청함`과 `내장 서버 정지`가 있고 이 줄은 `사용자가 창을 닫음`이라고 적힙니다. `창을 닫지 않은 종료`(WARN)라고 적혀 있으면 사용자가 창을 닫지 않았는데 끝나기 시작한 것입니다(콘솔 닫기, SIGTERM, 로그오프·시스템 종료, 또는 처리되지 않은 예외 뒤의 종료). 직전에 처리되지 않은 예외가 있었으면 `직전에 처리되지 않은 예외가 있었음`이 붙습니다. 그 줄에 종료 시점의 가동 시간, 서버에 등록된 작업 수(끝났어도 이벤트를 아직 받아 가지 않은 작업을 포함), 힙 사용량이 같이 적힙니다. 이 줄조차 없이 로그가 끊겼다면 작업 관리자의 강제 종료나 크래시처럼 종료 처리를 거치지 않은 경우입니다.
-- **`last-run.txt`:** 같은 폴더에 앱이 시작할 때 `state=running`으로 적고 종료 처리를 거치면 `state=exited`로 고치는 작은 파일이며, 다음 실행이 시작할 때 `state=running`인 채 남아 있으면 로그에 `이전 실행(PID …, 시작 …)이 정상 종료 기록 없이 끝났음: 강제 종료, 크래시, 전원 차단 등의 가능성` 경고를 남깁니다(그 프로세스가 아직 살아 있으면 대신 `다른 실행이 아직 실행 중임 (PID …)`).
+- **로그:** `%APPDATA%\xGetSongs\logs\xgetsongs.log`. 소스 폴더에서 `XGS_SIDECAR_DIR`로 사이드카를 가리켜 띄웠다면 `XGS_SIDECAR_LOGDIR`이 가리키는 폴더, 없으면 셸 exe 옆의 `log`입니다. 그 폴더를 만들 수 없으면 `%APPDATA%\xGetSongs\logs`에, 거기도 안 되면 `%TEMP%\xgetsongs-logs`에 씁니다. 실제로 쓰는 폴더는 로그의 시작 정보 `로그 폴더:` 줄에 있습니다. 날짜와 5MB 단위로 나뉘고(`xgetsongs.2026-10-07.0.log`처럼) 7일, 합계 50MB까지 보관합니다. 한 줄마다 바로 기록하므로 강제 종료돼도 마지막 줄이 남습니다. 시작 정보(Java, OS, 메모리, 도구 경로), 작업 시작과 옵션, 항목별 완료·건너뜀·실패와 사유, 작업 요약, 취소, 이벤트 연결이 끊긴 일(앱이 서버를 멈추는 중의 끊김은 제외), 처리되지 않은 예외를 적고, 토큰, 요청 헤더, 가사, 영상 제목과 파일 이름은 적지 않으며 오류 문구 속의 파일 경로와 파일 이름도 `<경로>`, `<파일명>`으로 가립니다. 파일에는 앱 자신의 DEBUG 줄(항목 시작 등)도 들어 있습니다.
+- **`sidecar-stderr.log`:** `%APPDATA%\xGetSongs`에 있습니다. 서버가 표준 오류로 쓴 것을 셸이 받아 적은 파일입니다. 로그 설정이 읽히기 전의 오류나 서버가 뜨지 못한 이유가 여기에만 남을 수 있습니다.
+- **`hang-날짜-시각.txt`:** 로그와 같은 폴더에 생기는 파일입니다. 서버의 작업 스레드가 5초 넘게 응답하지 않으면 모든 스레드가 그때 무엇을 하고 있었는지와 메모리·GC 정보를 여기에 저장하고 로그에도 ERROR로 남깁니다(창 자체가 멈춘 것은 기록하지 않습니다). 멈춤이 이어지면 같은 파일에 덧붙이되 간격을 30초, 60초, 120초…로 늘려 최대 30분으로 하고(로그에는 그때마다 한 줄만), 파일이 5MB에 이르면 `이후 덤프는 생략함(파일 크기 제한)` 한 줄을 쓰고 더 덧붙이지 않습니다. 다시 응답하면 로그에 `서버 스레드 응답 회복 (총 N초)`를 적습니다. 파일은 20개까지 보관합니다. 절전에서 깨어난 경우처럼 확인 주기가 크게 벌어진 때는 멈춤으로 보지 않습니다.
+- **`JVM 종료 시작` 줄:** 서버가 종료되기 시작할 때마다 로그에 남습니다. 창의 닫기 버튼을 눌렀다면 그 앞쪽에 `사용자가 창 닫기를 요청함`과 `내장 서버 정지`가 있고 이 줄은 `사용자가 창을 닫음`이라고 적힙니다. `창을 닫지 않은 종료`(WARN)라고 적혀 있으면 사용자가 창을 닫지 않았는데 끝나기 시작한 것입니다(셸이 갑자기 끝남, SIGTERM, 로그오프·시스템 종료, 또는 처리되지 않은 예외 뒤의 종료). 직전에 처리되지 않은 예외가 있었으면 `직전에 처리되지 않은 예외가 있었음`이 붙습니다. 그 줄에 종료 시점의 가동 시간, 서버에 등록된 작업 수(끝났어도 이벤트를 아직 받아 가지 않은 작업을 포함), 힙 사용량이 같이 적힙니다. 이 줄조차 없이 로그가 끊겼다면 작업 관리자의 강제 종료나 크래시처럼 종료 처리를 거치지 않은 경우입니다.
+- **`last-run.txt`:** 로그와 같은 폴더에 서버가 시작할 때 `state=running`으로 적고 종료 처리를 거치면 `state=exited`로 고치는 작은 파일이며, 다음 실행이 시작할 때 `state=running`인 채 남아 있으면 로그에 `이전 실행(PID …, 시작 …)이 정상 종료 기록 없이 끝났음: 강제 종료, 크래시, 전원 차단 등의 가능성` 경고를 남깁니다(그 프로세스가 아직 살아 있으면 대신 `다른 실행이 아직 실행 중임 (PID …)`).
+- **화면에 `서버를 시작하지 못했습니다: …`가 보일 때:** 뒤의 문구가 이유입니다. `런타임을 풀지 못했습니다`나 `자리에 놓지 못했습니다`이면 exe가 자기 안의 Java 런타임을 `%LOCALAPPDATA%\xGetSongs\runtime`에 풀지 못한 것입니다(디스크가 가득 찼거나 폴더를 쓸 수 없음, 백신이 막음). 그 폴더를 지우고 다시 실행해 보세요(지워도 다시 풀립니다). `checksum`이 보이면 exe 파일이 복사 중에 깨진 것이니 다시 복사하세요.
 
-문제를 알릴 때는 가장 최근의 `xgetsongs.log`(그리고 `ui-hang-*.txt`가 있으면 그것도)를 함께 보내 주세요. 로그에는 출력 폴더와 앱 폴더의 경로가 들어 있지만 곡 제목과 파일 이름은 없습니다.
+문제를 알릴 때는 가장 최근의 `xgetsongs.log`(그리고 `hang-*.txt`, `sidecar-stderr.log`가 있으면 그것도)를 함께 보내 주세요. 로그에는 출력 폴더와 앱 폴더의 경로가 들어 있지만 곡 제목과 파일 이름은 없습니다.
 
 ## 구조
 
 ```
-shared/   순수 Kotlin: 입력 판별, 제목 파싱, 파일명 생성, API 모델 (웹/Wasm에서도 재사용)
-engine/   yt-dlp·ffmpeg 호출, 재생목록 조회, 다운로드 작업 관리, 출력 저장소, 가사 검색(LRCLIB, 구글 가사 카드)
-server/   Ktor API 서버 (데스크톱 앱이 127.0.0.1에 내장 실행)
-app/      Compose Multiplatform UI (지금은 desktop 타깃만)
+shared/        순수 Kotlin: 입력 판별, 제목 파싱, 파일명 생성, API 모델
+engine/        yt-dlp·ffmpeg 호출, 재생목록 조회, 다운로드 작업 관리, 출력 저장소, 가사 검색(LRCLIB, 구글 가사 카드)
+server/        Ktor API 서버와 사이드카 진입점 (127.0.0.1에만 열리고 실행할 때마다 새 토큰이 있어야 접근)
+diagnostics/   로그 설정, 종료 기록, last-run.txt, 멈춤 감시
+sidecar-host/  Rust 라이브러리: 사이드카 시작·종료, 서버 호출과 이벤트(브리지), settings.json, 폴더 열기, 묶은 exe 안의 런타임 풀기, xgs-pack
+shell/         Tauri 앱: 화면(TypeScript)과 창을 여는 Rust 셸
 ```
 
-## 알려진 제한 (1단계)
+화면(웹뷰)은 서버에 직접 붙지 않고 Rust 셸의 명령(`invoke`)만 부릅니다. 셸이 서버의 HTTP·SSE를 대신 부르고, 서버의 토큰은 셸 안에만 있습니다. 설계: [docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md](docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md).
+
+## 알려진 제한
 
 - 음질은 YouTube의 AAC 원본(약 130kbps)을 그대로 쓰며 고를 수 없습니다. m4a를 읽지 못하는 오래된 기기에서는 재생되지 않고, 예전 버전처럼 mp3로 받는 방법은 없습니다. 이전 버전이 받은 mp3 파일은 건드리지 않으며, 같은 곡을 다시 받으면 덮어쓰지 않고 `.m4a`가 옆에 따로 생깁니다. YouTube가 m4a 오디오를 주지 않는 영상은 "m4a 오디오 형식이 없습니다."로 실패하고 "실패 항목 재시도"로 다시 받을 수 있습니다.
 - 재생목록의 모든 곡이 "m4a 오디오 형식이 없습니다."로 실패하면 영상 문제가 아니라 yt-dlp가 낡았거나 JavaScript 런타임(Node.js, Deno)에 문제가 있는 것일 수 있습니다. 앱 위쪽의 `yt-dlp 업데이트` 버튼으로 최신 버전으로 바꾸고 Node.js 22+ 또는 Deno 2.3+가 설치돼 있는지 확인한 뒤 다시 받으세요. 같은 안내가 앱 위쪽 도구 패널의 `yt-dlp 업데이트` 버튼 옆에도 표시됩니다.
 - ffmpeg는 PATH 또는 `%APPDATA%\xGetSongs\bin`에서 찾으며, 따로 경로를 지정하는 설정은 아직 없습니다.
-- 앱을 두 번 실행하지 마세요. 시작할 때 임시 폴더(`work/`)를 비우므로 먼저 실행한 앱의 진행 중 항목이 실패할 수 있습니다.
+- 앱을 두 번 실행해도 두 번째는 첫 창을 앞으로 가져오고 바로 끝납니다. 같은 앱 데이터 폴더(`%APPDATA%\xGetSongs`)를 쓰는 다른 실행(개발용 점검 등)과는 동시에 실행하지 마세요: 서버가 시작할 때 임시 폴더(`work/`)를 비우므로 먼저 실행한 앱의 진행 중 항목이 실패할 수 있습니다.
+- 화면 새로고침(F5, Ctrl+R)은 막혀 있습니다. 서버는 작업의 이벤트를 한 번만 내주므로 새로고침하면 돌고 있는 작업을 다시 따라갈 수 없기 때문입니다.
 - 이벤트 연결이 끊기면 같은 작업에 다시 붙을 수 없고 취소만 됩니다.
 - 요약의 "건너뜀" 개수에는 조회 단계에서 이미 사용할 수 없던 영상이 빠집니다(목록의 각 행에는 표시됩니다).
 - ffmpeg가 없으면 다운로드를 시작하기 전에 첫 항목이 실패하며 작업이 중단됩니다. 도구 패널의 안내를 먼저 확인하세요.
-- `packageMsi`로 만든 설치 파일은 아직 검증하지 않았습니다. 실행은 `:app:run`을 쓰세요.
 
 ## 주의
 
````

- [ ] **Step 3: `shell/README.md`를 고친다**

````diff
diff --git a/shell/README.md b/shell/README.md
index b381dcf..7187873 100644
--- a/shell/README.md
+++ b/shell/README.md
@@ -1,7 +1,41 @@
-# Tauri + Vanilla TS
+# shell — xGetSongs의 Tauri 앱
 
-This template should help get you started developing with Tauri in vanilla HTML, CSS and Typescript.
+화면(TypeScript, 프레임워크 없음)과 그것을 여는 Rust 셸이다. 다운로드와 태그 같은 일은 하지 않는다: `../server`의 Kotlin 사이드카가 한다. 셸은 사이드카를 띄우고(`../sidecar-host`) 화면이 `invoke`로 부르는 명령을 사이드카의 HTTP·SSE 호출로 바꿔 준다. 앱 전체의 설명은 [../README.md](../README.md), 설계는 [../docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md](../docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md)에 있다.
 
-## Recommended IDE Setup
+## 만들기와 실행
 
-- [VS Code](https://code.visualstudio.com/) + [Tauri](https://marketplace.visualstudio.com/items?itemName=tauri-apps.tauri-vscode) + [rust-analyzer](https://marketplace.visualstudio.com/items?itemName=rust-lang.rust-analyzer)
+- exe 한 파일로 묶기: 저장소 루트의 `package-exe.bat`.
+- 셸만 빌드: `npm run tauri build -- --no-bundle` → `src-tauri\target\release\shell.exe`. 사이드카를 찾을 수 있어야 실행된다(아래 "환경 변수").
+- 사이드카 없이 화면만 보기: `npm run dev`로 Vite를 띄우고 브라우저에서 `http://localhost:1420/?demo`를 연다. 가짜 서버(`src/demo.ts`)가 조회·다운로드·진행을 흉내 낸다.
+
+## 시험
+
+```powershell
+node --test "tests/*.test.ts"   # 화면 로직, 브리지 계약, 이름 점검
+npx tsc --noEmit                # 타입 검사
+```
+
+`tests/kotlin-coverage.test.ts`는 `shared`의 Kotlin 시험이 가진 이름이 TypeScript 포팅(`filename.test.ts`, `input.test.ts`)에도 있는지 본다. `tools/differential`은 같은 무작위 입력을 Kotlin 원본(사이드카 이미지의 jar)과 TypeScript 포팅에 넣어 답을 비교한다(`node tools/differential/fuzz.mts`, 먼저 `gradlew :server:sidecarImage`).
+
+## 환경 변수
+
+사이드카를 어디서 찾을지와 어디에 쓸지는 `../sidecar-host/src/config.rs`가 정한다. 개발에 쓰는 것:
+
+| 변수 | 뜻 |
+|---|---|
+| `XGS_SIDECAR_DIR` | 사이드카 이미지 폴더(`runtime\bin\java.exe`와 `lib`가 있는 곳). 보통 `..\server\build\sidecar-image` |
+| `XGS_SIDECAR_LIB` | 이미지 대신 `installDist`의 `lib` 폴더(개발용). `XGS_SIDECAR_JAVA`로 java를 고른다 |
+| `XGS_SIDECAR_APPDATA` | 앱 데이터 폴더. 기본은 `%APPDATA%\xGetSongs`. **개발과 점검에는 임시 폴더를 준다**: 사이드카가 시작할 때 그 안의 `work\`를 비운다 |
+| `XGS_SIDECAR_LOGDIR` | 로그 폴더 |
+
+없으면 셸은 자기 exe가 지닌 런타임(`package-exe.bat`으로 묶은 exe)을, 그것도 없으면 exe 옆의 `sidecar` 폴더를 쓴다. 빈 값은 없는 것으로 친다.
+
+## 점검 페이지 (개발자용)
+
+`check.html`은 제품이 아니다. 브리지의 명령을 실제 사이드카에 부르고 화면이 본 것을 `XGS_BRIDGE_CHECK_FILE`이 가리키는 파일에 `key=value` 줄로 적는다. `npm run tauri build -- --no-bundle --config check.conf.json`으로 만든 점검용 빌드(식별자가 `com.xgetsongs.shell.check`라 사용자가 쓰는 앱과 섞이지 않는다)만 연다. 점검 스크립트(창을 닫고, 두 번째 실행을 하고, 풀다 죽이는 것 등)는 저장소에 없고 각 단계의 계획서(`docs/superpowers/plans`)에 들어 있다. 점검용 빌드는 일반 빌드와 같은 `target\release\shell.exe`를 덮어쓰므로 점검 뒤에는 일반 빌드를 다시 만든다.
+
+점검용 명령(`check_record`, `check_env`, `check_ping`, `check_close`, `check_wait`, `check_burst`)은 `XGS_BRIDGE_CHECK_FILE`이 없으면 거부한다. 같은 변수가 있을 때만 `XGS_FAKE_NO_WEBVIEW2`가 WebView2가 없는 척한다.
+
+## 권장 개발 환경
+
+[VS Code](https://code.visualstudio.com/) + [Tauri](https://marketplace.visualstudio.com/items?itemName=tauri-apps.tauri-vscode) + [rust-analyzer](https://marketplace.visualstudio.com/items?itemName=rust-lang.rust-analyzer)
````

- [ ] **Step 4: 설계 문서를 고친다**

마이그레이션 설계(§4의 저장소 배치, §5의 6단계 행)와 처음 설계(맨 위의 한 단락):

```diff
diff --git a/docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md b/docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md
index 30cc484..5cc5e74 100644
--- a/docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md
+++ b/docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md
@@ -64,7 +64,8 @@
 | `shared/`, `engine/`, `server/` | 그대로. 사이드카 진입점(`main`, 시작 신호, 부모 감시)은 `server/`의 `sidecar` 패키지에 둔다(설계 문서 §10.1 2단계 "main()만 분리") |
 | `sidecar-host/` (새) | Rust 라이브러리 크레이트. 시작 신호 해석, 사이드카 시작·종료, Job Object, 서버 호출 6개와 이벤트 스트림(`Bridge`), 시작 대기·종료(`Lifecycle`). **Tauri에 의존하지 않는다.** Tauri 크레이트는 첫 컴파일이 수 분 걸려서 이 로직의 테스트 루프를 느리게 하기 때문이다 |
 | `shell/` (새) | Tauri 앱(프런트엔드 + `src-tauri`). `sidecar-host`를 경로 의존성으로 쓴다 |
-| `app/` | Compose 앱. 6단계까지 남겨 두고 `run.bat`으로 계속 쓸 수 있다 |
+| `diagnostics/` | 로그 설정, 종료 기록, 멈춤 감시(앱과 사이드카가 같이 쓰던 것을 1단계에서 옮겼다) |
+| `app/` | (6단계에서 지웠다) 마지막으로 있던 커밋은 `f9dbcba`: `git show f9dbcba:app/...`로 읽을 수 있다 |
 
 ## 5. 단계와 관문
 
@@ -77,7 +78,7 @@
 | 3 | 프런트엔드 이식: `AppStateHolder` 논리와 `Labels`, 화면. `shared` 로직 재사용 방식 확정 | 지금의 `AppStateHolderTest` 시나리오가 새 테스트에서 통과 ([계획서](../plans/2026-10-11-tauri-frontend-port.md), [보고서](2026-10-11-tauri-frontend-port-report.md): 통과). 재사용 방식은 TS 포팅과 같은 케이스 표 테스트이고 Kotlin 구현과의 대조 도구가 있다 |
 | 4 | 데스크톱 기능: 폴더 선택, 탐색기 열기, `settings.json`, 단일 인스턴스, 종료 연동 | README의 "알려진 제한" 항목과 동작이 같다 ([계획서](../plans/2026-10-11-tauri-desktop-features.md), [보고서](2026-10-11-tauri-desktop-features-report.md): 통과). 앱 데이터 폴더의 기본값이 `%APPDATA%\xGetSongs`(Compose와 같음)가 됐다 |
 | 5 | 포장과 단일 exe: 번들 JRE + 셸. 이전 설계(§6.4)의 SFX 판정 방식 재사용 | 깨끗한 PC 흉내 검증 ([계획서](../plans/2026-10-15-tauri-packaging.md), [보고서](2026-10-15-tauri-packaging-report.md): 통과). 방식은 SFX가 아니라 셸 exe 뒤에 이미지를 붙이고 첫 실행에 `%LOCALAPPDATA%\xGetSongs\runtime\<해시>`로 풀어 쓰는 것이다(약 130MB 한 파일, `package-exe.bat`) |
-| 6 | `app/` 삭제, README·설계 문서 갱신 | `gradlew check` 통과 |
+| 6 | `app/` 삭제, README·설계 문서 갱신 | `gradlew check` 통과 ([계획서](../plans/2026-10-16-tauri-cleanup.md)) |
 | 7 | (선택) 가사 검색의 JavaFX 웹뷰를 WebView2로 교체 | 구글 가사 통합 테스트 재검증 |
 
 각 단계의 계획서는 앞 단계의 관문을 통과한 뒤에 쓴다. 0단계 결과가 1~5단계의 방식을 바꿀 수 있기 때문이다. 0a의 UI 관문이 통과하지 못하면 0b 이후는 하지 않는다(UI가 나아지지 않으면 마이그레이션이 줄 이득이 없다).
```

```diff
diff --git a/docs/superpowers/specs/2026-10-04-xgetsongs-design.md b/docs/superpowers/specs/2026-10-04-xgetsongs-design.md
index 4df6517..0b19fd2 100644
--- a/docs/superpowers/specs/2026-10-04-xgetsongs-design.md
+++ b/docs/superpowers/specs/2026-10-04-xgetsongs-design.md
@@ -1,5 +1,7 @@
 # xGetSongs 설계
 
+> **2026-10-16:** 이 문서는 화면이 Compose Desktop이던 때의 설계다. 화면은 그 뒤 Tauri(웹뷰)로 옮겼고 `app/` 모듈은 지웠다. 로직(`shared`, `engine`, `server`)의 설계는 그대로이고, 화면·실행·포장에 관한 부분은 [Tauri 마이그레이션 설계](2026-10-10-tauri-kotlin-sidecar-design.md)가 이어받는다.
+
 작성일: 2026-10-04
 
 ## 1. 목표
```

- [ ] **Step 5: 쓴 문구를 코드와 대조한다**

```powershell
Set-Location C:\Projects\xGetSongs
git grep -n -F -e "서버를 시작하지 못했습니다" -e "런타임을 풀지 못했습니다" -e "자리에 놓지 못했습니다" -e "사용자가 창 닫기를 요청함" -e "내장 서버 정지" -e "JVM 종료 시작: 사용자가 창을 닫음" -e "서버 스레드 응답 회복" -e "sidecar-stderr.log" -- "*.rs" "*.kt" "*.ts"
```

Expected: 일곱 문구 모두 코드에서 찾아진다(`서버를 시작하지 못했습니다`는 `shell/src-tauri/src/…`가 아니라 `sidecar-host/src/lifecycle.rs`, `런타임을 풀지 못했습니다`·`자리에 놓지 못했습니다`는 `payload.rs`, 로그 줄들은 `diagnostics/`와 `server/`, `서버 스레드 응답 회복`은 `HangWatchdog.kt`, `sidecar-stderr.log`는 `config.rs`). 하나라도 안 찾아지면 README의 그 문구를 코드의 것으로 고친다.

- [ ] **Step 6: 링크와 옛 이름을 다시 점검한다**

```powershell
Set-Location C:\Projects\xGetSongs
node $env:TEMP\xgs-bridge-check\links.mjs README.md shell/README.md docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md docs/superpowers/specs/2026-10-04-xgetsongs-design.md
git grep -n -E "UiWatchdog|postToUi|onUiBeat|ui-hang|UI 스레드|UI 응답|UI 감시|xgs-ui-watchdog|:app" -- . ':!docs' ; "grep exit=$LASTEXITCODE (1 = nothing found)"
```

Expected: `all … links exist`(설계 문서의 새 링크 둘 중 보고서는 Task 6에서 만든다: 지금은 `2026-10-16-tauri-cleanup-report.md`가 **없다고 한 줄 나올 수 있다**. 그 한 줄만 있고 나머지가 모두 있으면 정상이다), `git grep`은 아무것도 찾지 못한다(`grep exit=1`).

- [ ] **Step 7: 커밋한다**

```powershell
Set-Location C:\Projects\xGetSongs; git add README.md shell/README.md docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md docs/superpowers/specs/2026-10-04-xgetsongs-design.md; git commit -m "문서: README와 설계 문서를 Tauri 셸 + 사이드카에 맞춘다

README의 필요한 것·실행·테스트·문제가 생겼을 때(로그 위치와 줄, sidecar-stderr.log, 풀기 실패)·구조·알려진 제한을 새로 쓰고 shell/README.md의 템플릿 문구를 개발자용 설명으로 바꾼다. 마이그레이션 설계의 저장소 배치와 6단계 행, 처음 설계의 맨 위 안내를 고친다.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: 종단 확인, 보고서

**Files:**
- Create: `docs/superpowers/specs/2026-10-16-tauri-cleanup-report.md`
- Modify: `docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md`(6단계 행에 보고서 링크)

**Interfaces:**
- Consumes: 모든 앞 Task.
- Produces: 관문 C1~C7의 증거와 보고서.

- [ ] **Step 1: 전체 시험 (C2, C3, C4)**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"
Set-Location C:\Projects\xGetSongs
& .\gradlew.bat --no-daemon --offline check :server:sidecarImageTest *> $env:TEMP\xgs-s6-all.log; "gradle exit=$LASTEXITCODE"
Select-String -Path $env:TEMP\xgs-s6-all.log -Pattern "BUILD|FAILED|sidecarImageTest.*PASSED|SidecarImageTest.*PASSED"
node $env:TEMP\xgs-bridge-check\count-tests.mjs C:\Projects\xGetSongs
Set-Location sidecar-host
cargo test --offline 2>&1 | Select-String '^test result|FAILED'
cargo clippy --offline --all-targets 2>&1 | Select-String '^(warning|error)' | ForEach-Object { $_.Line }
$env:XGS_SIDECAR_DIR = "C:\Projects\xGetSongs\server\build\sidecar-image"
cargo test --offline --test real_sidecar -- --ignored 2>&1 | Select-String '^test |test result'
Remove-Item Env:XGS_SIDECAR_DIR
Set-Location ..\shell
node --test "tests/*.test.ts" 2>&1 | Select-String '^ℹ (tests|pass|fail|skipped)'
npx tsc --noEmit; "tsc exit=$LASTEXITCODE"
```

Expected: `gradle exit=0`, `BUILD SUCCESSFUL`, 이미지 시험 4개 `PASSED`, 개수 `TOTAL 938`(`skipped 0`); `sidecar-host`: `lib` 161, `pack_cli` 5, `#[ignore]` 둘, clippy는 원래 있던 `job.rs:11` 하나(`the method `set_extended_limit_info` doesn't need a mutable reference`와 그것을 센 줄 둘); **`the_bridge_speaks_the_contract_of_the_real_server ... ok`**(Review Focus 1: Kotlin 클라이언트가 없어진 자리를 채우는 계약 확인); TS `tests 239`, `pass 239`, `skipped 0`, `tsc exit=0`.

- [ ] **Step 2: 묶고, 깨끗한 PC 점검과 화면 점검 (C7)**

사용자에게 창과 안내 상자가 잠깐 뜬다고 알린 뒤:

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; $env:CARGO_NET_OFFLINE = 'true'
Set-Location C:\Projects\xGetSongs
& .\package-exe.bat *> $env:TEMP\xgs-s6-package.log; "package exit=$LASTEXITCODE"
Get-Content $env:TEMP\xgs-s6-package.log -Tail 2
# the check build of the shell (the same place as the normal build: rebuild the normal one afterwards)
Set-Location shell
npm run tauri build -- --no-bundle --config check.conf.json *> $env:TEMP\xgs-s6-check-build.log; "check build exit=$LASTEXITCODE"
New-Item -ItemType Directory $env:TEMP\xgs-s6 -Force | Out-Null
Copy-Item src-tauri\target\release\shell.exe $env:TEMP\xgs-s6\shell-check.exe -Force
Set-Location ..
$pack = "C:\Projects\xGetSongs\sidecar-host\target\release\xgs-pack.exe"; $image = "C:\Projects\xGetSongs\server\build\sidecar-image"
& $pack $env:TEMP\xgs-s6\shell-check.exe $image $env:TEMP\xgs-s6\packed-check.exe | Out-Null
& $env:TEMP\xgs-bridge-check\clean-pc-check.ps1 -ShellExe $env:TEMP\xgs-s6\shell-check.exe -ImageDir $image -Pack $pack -PackedCheckExe $env:TEMP\xgs-s6\packed-check.exe 2>&1 | Tee-Object $env:TEMP\xgs-s6\clean-pc-run.txt | Select-String '^=== |FAIL|ALL OK'
$run = Get-Content $env:TEMP\xgs-s6\clean-pc-run.txt
"ok lines: $(@($run | Where-Object { $_ -match '^ok ' }).Count), FAIL lines: $(@($run | Where-Object { $_ -match '^FAIL' }).Count)"
& $env:TEMP\xgs-bridge-check\packed-screen-check.ps1 -Exe C:\Projects\xGetSongs\dist\xGetSongs.exe 2>&1 | Select-String 'window after|FAIL|ALL OK'
& $env:TEMP\xgs-bridge-check\measure-start.ps1 -PackedCheckExe $env:TEMP\xgs-s6\packed-check.exe -Runs 5 2>&1 | Select-Object -Last 4
```

Expected: `package exit=0`(`dist\xGetSongs.exe` 129MB 안팎), `check build exit=0`, 깨끗한 PC 점검의 일곱 시나리오(`=== 1.` … `=== 7.`)와 `=== ALL OK`, `ok` 38줄·`FAIL` 0줄, 화면 점검 `window after … ms`(3초 이내)와 `=== ALL OK`, 시간의 중앙값: 첫 실행 사이드카 대기 10초 이하, 두 번째 5초 이하(5단계: 4.6초와 1.2초). 하나라도 `FAIL`이면 원인을 찾는다(`systematic-debugging`): 제품을 고치기 전에 점검 스크립트의 오류부터 의심한다.

- [ ] **Step 3: 일반 빌드를 되돌리고 `run.bat`을 돌려 본다 (C7)**

점검용 빌드가 `target\release\shell.exe`를 덮어썼으므로 일반 빌드를 다시 만들고(`package-exe.bat`) `run.bat`이 창을 여는지 임시 앱 데이터로 확인한다. **사용자의 `xGetSongs.exe`가 떠 있으면 이 단계는 건너뛴다**(보고서에 적는다).

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; $env:CARGO_NET_OFFLINE = 'true'
Set-Location C:\Projects\xGetSongs
& .\package-exe.bat *> $env:TEMP\xgs-s6-package2.log; "package exit=$LASTEXITCODE"
if (@(Get-Process xGetSongs -ErrorAction SilentlyContinue).Count -gt 0) { "SKIPPED: an xGetSongs is running (not ours)"; return }
. $env:TEMP\xgs-bridge-check\helpers.ps1
$base = Join-Path $env:TEMP ("xgs-bridge-check-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory $base | Out-Null
$env:XGS_SIDECAR_APPDATA = "$base\data"
& .\run.bat
$process = $null
for ($i = 0; $i -lt 100 -and -not $process; $i++) { Start-Sleep -Milliseconds 100; $process = Get-Process xGetSongs -ErrorAction SilentlyContinue | Select-Object -First 1 }
"started: $([bool]$process)"
$ms = Wait-CheckWindow $process 20
"window after $ms ms"
$up = $false; for ($i = 0; $i -lt 300 -and -not $up; $i++) { Start-Sleep -Milliseconds 100; $up = [bool](Get-Sidecar) }
"sidecar up: $up, from: $((Get-Sidecar | Select-Object -First 1).ExecutablePath)"
"closed after $(Close-CheckShell $process) ms"
Remove-Item Env:XGS_SIDECAR_APPDATA
[IO.Directory]::Delete($base, $true)
```

Expected: `package exit=0`, `started: True`, `window after … ms`(1초 이내), `sidecar up: True`, `from: …\xGetSongs\runtime\<해시>\runtime\bin\java.exe`(앱 데이터가 임시이므로 `%TEMP%\xgs-bridge-check-…\data\runtime\…`), `closed after … ms`(몇 초 이내). `Get-Process xGetSongs`는 마지막에 0개.

- [ ] **Step 4: 정리 상태 확인 (C1, C6)**

```powershell
Set-Location C:\Projects\xGetSongs
"app exists: $(Test-Path app); tracked: $(@(git ls-files app).Count)"
git status --short
git grep -n -E "UiWatchdog|postToUi|onUiBeat|ui-hang|UI 스레드|UI 응답|UI 감시|xgs-ui-watchdog|:app" -- . ':!docs' ; "grep exit=$LASTEXITCODE"
"left: " + @(Get-ChildItem $env:TEMP -Directory | Where-Object { $_.Name -match '^xgs-bridge-check-' }).Count + " check folders, " + @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -like "$env:TEMP\xgs-bridge-check-*" }).Count + " processes"
```

Expected: `app exists: False; tracked: 0`, `git status --short`는 비어 있다(`bin/`은 `.gitignore`가 가린다), `git grep`은 아무것도 찾지 못하고(`grep exit=1`), `left: 0 check folders, 0 processes`.

- [ ] **Step 5: 보고서를 쓰고 설계 문서에 링크한다**

`docs/superpowers/specs/2026-10-16-tauri-cleanup-report.md`를 아래 틀로 쓴다. **자리에는 이 실행의 실제 값을 넣는다**(위 Expected의 숫자가 아니라 출력된 것).

```markdown
# 6단계 "정리와 마무리" 종단 확인 결과

계획: [2026-10-16-tauri-cleanup.md](../plans/2026-10-16-tauri-cleanup.md). 설계: [§5 6단계](2026-10-10-tauri-kotlin-sidecar-design.md). 실행: 날짜, Native(인라인), 커밋 범위.

## 결과

| 관문 | 결과 | 증거 |
|---|---|---|
| C1 … C8 | 통과/실패 | 각 줄에 이 실행의 숫자 |

## 지운 것과 시험 수

(`gradlew check` 1,125 → 938, `app`의 시험 187개가 맡던 일이 어디로 갔는지 표, `real_sidecar`가 계약을 확인한 출력)

## 바뀐 동작

(덤프 파일 이름 `hang-*.txt`, 페이지의 권한 `core:default`뿐, `run.bat`)

## 한계와 남은 일

- 빈 Gradle 캐시에서 처음 내려받는 것은 확인하지 않았다(`google()` 등을 걷은 것이 그때 문제가 될 수 있다)
- 예전 `ui-hang-*.txt`는 남는다
- 사용자의 `docs/xGetSongs 작업 메모.txt`는 건드리지 않았다(옛 내용이 있다)
- 사용자의 PC에서의 확인: `dist\xGetSongs.exe`로 실제 재생목록 다운로드, yt-dlp 설치 버튼 (한 번도 실제로 부르지 않았다)
- 7단계(구글 가사의 WebView2 교체, 선택)와 `jlink`, 서명·아이콘

## 최종 리뷰

(Critical·Important와 처리)
```

설계 문서의 6단계 행에 보고서 링크를 더한다(Edit 도구):

`([계획서](../plans/2026-10-16-tauri-cleanup.md))` → `([계획서](../plans/2026-10-16-tauri-cleanup.md), [보고서](2026-10-16-tauri-cleanup-report.md): 통과)`

```powershell
Set-Location C:\Projects\xGetSongs
node $env:TEMP\xgs-bridge-check\links.mjs README.md shell/README.md docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md docs/superpowers/specs/2026-10-04-xgetsongs-design.md docs/superpowers/specs/2026-10-16-tauri-cleanup-report.md
```

Expected: `all … links exist`.

- [ ] **Step 6: 메모리를 갱신한다**

`C:\Users\gomto\.claude\projects\C--Projects-xGetSongs\memory\project-tauri-migration.md`의 맨 위 "최신" 단락을 6단계 구현 완료(커밋 범위, 관문 결과, 남은 일: 사용자의 실제 PC 확인, push 여부, 7단계는 선택)로 고치고 `MEMORY.md`의 한 줄 소개를 맞춘다.

- [ ] **Step 7: 커밋한다**

```powershell
Set-Location C:\Projects\xGetSongs; git add docs/superpowers/specs/2026-10-16-tauri-cleanup-report.md docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md; git commit -m "문서: 정리와 마무리(6단계)의 종단 확인 결과

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## 실행 뒤

전체 브랜치 리뷰는 executing-plans의 "Final Review" 절차를 따른다(가장 유능한 모델, 이 계획의 Review Focus를 그대로 전한다). Critical·Important는 한 번에 고치고(각 고침은 실패하는 시험이 먼저), Minor는 보고서에 적는다. 끝나면 **푸시 여부를 사용자에게 묻는다**.

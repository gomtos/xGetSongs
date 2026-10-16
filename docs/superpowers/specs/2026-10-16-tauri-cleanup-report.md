# 6단계 "정리와 마무리" 종단 확인 결과

계획: [2026-10-16-tauri-cleanup.md](../plans/2026-10-16-tauri-cleanup.md). 설계: [§5 6단계](2026-10-10-tauri-kotlin-sidecar-design.md). 실행: 2026-10-16, Native(인라인), 커밋 `f5b64f8..`(계획서 1 + Task 1~5와 이 보고서). 모든 숫자는 이 PC(Windows 11)에서 이 실행에서 잰 것이다.

## 결과

| 관문 | 결과 | 증거 |
|---|---|---|
| C1 `app/`이 없다 | 통과 | `app` 폴더 없음, 추적되는 파일 0개(커밋 하나에 35개 파일, 4,352줄 삭제: `app` 29개 파일과 Gradle 설정 여섯). `settings.gradle.kts`에 `:app` 없음, `*.kts`·`*.toml`에 `compose`, `google()`, `:app` 없음. `git status`에 `bin/` 파일이 나오지 않는다(109개가 사라졌다). `.gitignore`의 `/*/bin/`은 `engine/bin/main/x.kt`를 가리고 `sidecar-host/src/bin/xgs-pack.rs`는 가리지 않는다 |
| C2 `gradlew check` | 통과 | 오프라인, 실패 0·건너뜀 0. **938개**: `diagnostics` 158, `engine` 444, `server` 125, `shared` 211(삭제 전 1,125개 = 여기에 `app:desktopTest` 187). `:server:sidecarImageTest` 4개 통과 |
| C3 TypeScript | 통과 | **239개**(242 − 4(옛 포팅 점검) − 2(그 도우미 시험) + 3(권한 시험)), 건너뜀 0, `tsc` 종료 코드 0. Task 2 전에는 242개 중 4개가 건너뛰어져 있었다 |
| C4 `sidecar-host` | 통과 | `lib` 161개, `pack_cli` 5개, `#[ignore]` 둘 그대로. clippy는 원래 있던 `job.rs:11` 하나. **`real_sidecar`(`--ignored`, 실제 이미지)의 `the_bridge_speaks_the_contract_of_the_real_server ... ok`**(2.9초) |
| C5 `opener` 제거 | 통과 | 일반 빌드 4분 2초, 점검용 빌드도 빌드됨. `shell/src-tauri/Cargo.lock` 53줄 삭제(더해진 줄 없음), `package.json` +3 −4, `package-lock.json` +1 −11. 권한 시험 3개는 걷기 전에 모두 실패했고(`'opener:default'`, `'@tauri-apps/plugin-opener'`, `'tauri-plugin-opener'`) 걷은 뒤 통과 |
| C6 옛 이름과 링크 | 통과 | `git grep`(`docs/` 밖)에 `UiWatchdog`, `postToUi`, `onUiBeat`, `ui-hang`, `UI 스레드`, `UI 응답`, `UI 감시`, `xgs-ui-watchdog`, `:app:` 없음. README·`shell/README.md`·설계 문서 둘의 상대 링크 모두 있음(보고서를 더한 뒤 27개). README에 쓴 일곱 문구는 코드에서 찾아졌다 |
| C7 종단 점검 | 통과 | 아래 |
| C8 최종 리뷰 | 통과 | Critical 없음, Important 1(보고서의 계약 설명)은 고침, Minor 11 중 문서의 틀린 문장은 같이 고치고 나머지는 미룸. 아래 "최종 리뷰" |

### C7: 5단계의 점검이 그대로 통과한다

- `package-exe.bat`: `dist\xGetSongs.exe` 129.2MB(이미지 147.9MB, 493개 파일).
- 깨끗한 PC 점검(점검용 빌드를 묶은 exe): 일곱 시나리오 모두 `ALL OK`, `ok` 38줄·`FAIL` 0줄.
- 점검 페이지가 없는 실제 exe의 화면: 창 64ms, UI Automation으로 `yt-dlp ✗`, `ffmpeg ✗`, `JS 런타임 ✗`, 입력란이 읽힘, `ALL OK`. 페이지의 권한이 `core:default`뿐인데도 화면이 그대로 뜨고 사이드카에서 도구 상태를 받았다.
- 시간(5회 중앙값): 푸는 데 3.1초, 첫 실행에서 사이드카를 기다린 시간 4.9초(한도 10초), 두 번째 1.4초(한도 5초). 5단계 보고서의 값(2.9초, 4.6초, 1.2초)보다 조금 느린데 같은 PC에서 그때와 다른 일이 CPU를 쓰고 있었는지는 따로 재지 않았다. 관문은 넉넉히 넘는다.
- `run.bat`(임시 앱 데이터, 사용자의 `xGetSongs.exe`는 떠 있지 않았다): 창 15ms, 사이드카가 임시 폴더에 푼 `runtime\bin\java.exe`로 올라옴, 닫기 3.9초, 남은 프로세스 0개.

## 지운 것과 시험 수

`app`의 시험 187개가 맡던 일의 지금 자리:

| `app`의 시험 | 개수 | 지금 |
|---|---|---|
| `AppStateHolderTest` | 95 | `shell/tests/holder.test.ts` 같은 이름 95 |
| `LabelsTest` | 35 | `shell/tests/labels.test.ts` 같은 이름 35 |
| `JsonSettingsStoreTest` | 22 | `sidecar-host/src/settings.rs` 24(원본 22 + 포트가 더한 2) |
| `ExplorerOpenerTest` | 10 | `sidecar-host/src/folder.rs` 12(원본 10 + 2) |
| `LogbackConfigTest` | 12 | `app`의 `logback.xml`을 시험한 것: 사이드카의 `SidecarLogbackConfigTest`가 맡는다 |
| `HttpXgsApiTest` + `EndToEndTest` | 13 | 서버와 그 클라이언트 사이의 계약. 클라이언트는 이제 Rust 브리지다. **부분적으로만 이어진다**(아래 "한계"): 도구 상태, 서버의 오류 문구, 인증 거부, 토큰 비노출, 종료 기록은 실제 이미지를 상대로 하는 `real_sidecar`(`#[ignore]`)가 확인하고(이 실행에서 통과), 조회 결과와 작업 이벤트(`item-started`, `progress`, `item-done`, `job-done` …)의 필드 모양은 실제 yt-dlp·네트워크가 필요한 `real_download`(`#[ignore]`, 수동)만 실제 서버와 대조한다. 이 단계는 `real_download`를 돌리지 않았다. `bridge.rs`의 가짜 서버(28개)와 TypeScript 시험의 JSON은 손으로 쓴 것이라 서버의 직렬화와 묶여 있지 않다 |

이 중 앞의 넷은 "원본의 이름이 포트에 모두 있다"를 지키는 점검(`kotlin-coverage.test.ts`)이 있었는데 원본이 없어져 `shared`의 둘만 남겼다. 지운 원본은 `git show f9dbcba:app/...`로 읽을 수 있고 포팅한 파일 일곱 곳의 머리 주석이 그렇게 적는다.

## 바뀐 동작

- 사이드카의 멈춤 덤프 파일: `ui-hang-날짜-시각.txt` → `hang-날짜-시각.txt`. 로그 문구도 "UI 스레드가 N초 동안 응답하지 않음" → "서버 스레드가 N초 동안 응답하지 않음", "UI 응답 회복" → "서버 스레드 응답 회복". 예전 `ui-hang-*.txt`는 지워지지 않고 남는다.
- 웹뷰(페이지)의 권한이 `core:default`뿐이다(`opener:default`가 빠졌다). 페이지는 그 플러그인을 쓴 적이 없다.
- `run.bat`: `dist\xGetSongs.exe`를 실행한다(없으면 `package-exe.bat`으로 먼저 만든다).

## 이 실행에서 계획과 달라진 것

원장(`.superpowers/sdd/2026-10-16-tauri-cleanup/progress.md`)의 `Ruling:`이 기준이다. 모두 계획서의 도구·명령의 결함을 고친 것이고 결과의 숫자는 계획과 같다: 개수 도구가 `check`에 속하지 않는 시험 과제(이전 실행이 남긴 `integrationTest`, `sidecarImageTest`)의 결과까지 더하던 것, 폴더를 지운 뒤의 `git add -A app`가 실패하던 것, `:app` 검색이 `git show f9dbcba:app/...` 주석에 걸리던 것(`:app:`으로), 코드가 이어 붙이는 문구 하나를 통째로 검색하던 것.

## 한계와 남은 일

- **서버와 클라이언트 사이의 성공 경로 계약은 자동으로 지켜지지 않는다.** `shared/api`의 필드 이름이 바뀌어도 `gradlew check`, `cargo test`, `real_sidecar`, `node --test`가 모두 통과하고 화면만 깨질 수 있다. 지운 Kotlin 클라이언트의 시험은 서버와 같은 직렬화기를 써서 이것을 잡지 못했으니 이 단계가 새로 만든 틈은 아니지만, 이 단계가 메우지도 않았다. 싸게 막는 길(보고서를 쓴 뒤 검토에서 나온 제안): 이벤트 종류마다 JSON 예시 파일을 저장소에 두고, Kotlin 시험이 서버의 직렬화 결과가 그 파일과 같은지, TypeScript 시험이 같은 파일의 필드 집합을 `bridge/types.ts`와 대조하게 한다.

- **빈 Gradle 캐시에서 처음 내려받는 것은 확인하지 않았다**(네트워크가 필요하고 이 단계는 내려받지 않는다). `google()`과 카탈로그 항목을 걷은 것이 그때 문제가 될 수 있다. 걷은 것은 `app`만 쓰던 것임을 모듈별로 대조했다.
- 예전 `ui-hang-*.txt`는 남는다(20개 보관 규칙이 새 이름만 센다).
- 사용자의 `docs/xGetSongs 작업 메모.txt`는 건드리지 않았다(옛 내용이 있다).
- 사용자의 PC에서의 확인: `dist\xGetSongs.exe`로 실제 재생목록 다운로드와 yt-dlp 설치 버튼(한 번도 실제로 부르지 않았다). Windows 10 실제 PC와 다른 백신은 시험하지 못했다.
- 7단계(구글 가사의 JavaFX 웹뷰를 WebView2로, 선택), `jlink`, 서명·아이콘은 하지 않았다. 같은 날 다른 세션이 쓴 [Python 이행 분석](2026-10-16-python-single-exe-migration-analysis.md)도 "현행 경로 완주"를 권고한다(결정은 보류).

## 최종 리뷰

가장 유능한 모델이 전체 브랜치(`38265a7..ac49485`, 커밋 7개)를 네 번에 나눠 읽었다(Gradle과 `.gitignore`, 진단의 이름 정리, 셸과 권한 시험, 문서를 코드에 대조). 사본에서 `node --test`도 돌렸다(239개 통과). **Critical 없음, Important 1, Minor 11, 판정 "고치면 병합 가능"**. 좋다고 한 것: 삭제가 깨끗하다(어느 빌드 파일에도 `:app`이 없고 쓰이는 `libs.*` 별칭이 모두 카탈로그에 있다), `google()`을 걷은 것이 낮은 위험이다(Kotlin Gradle 플러그인의 의존성은 모두 Plugin Portal·Maven Central의 `org.jetbrains.kotlin`이고 JavaFX도 Maven Central이다), `/*/bin/`이 정확하다(`git ls-files -ci --exclude-standard`가 비었고 `xgs-pack.rs`는 추적된다), 이름 정리가 이름과 문구만 바꾼다(시험의 단언은 그대로 강하고 시험 결과 XML의 시각이 커밋보다 앞서 RED→GREEN이 진짜다), 포팅에서 시험 이름이 하나도 빠지지 않았다(`f9dbcba`와 대조: 95/95, 35/35, 10/10, 22/22), 문서의 로그 줄과 오류 문구가 모두 코드에 있다.

### 고친 것

| 지적 | 영향 | 고침 |
|---|---|---|
| **Important 1.** 보고서와 계획서가 서버·클라이언트 계약이 `real_sidecar`로 이어진다고 썼다. 그 시험은 도구 상태, 오류 문구, 인증 거부, 토큰 비노출, 종료 기록만 본다. 조회 결과와 작업 이벤트의 필드 모양은 실제 네트워크가 필요한 수동 `real_download`만 실제 서버와 대조하고 이 단계는 그것을 돌리지 않았다. 가짜 서버와 TypeScript 시험의 JSON은 손으로 쓴 것이다 | 읽는 사람이 성공 경로까지 지켜진다고 믿는다 | 보고서의 표와 한계, 계획서의 해당 문장을 사실대로 고쳤고 README의 시험 절에 두 `#[ignore]` 시험의 실행법을 더했다 |
| README의 문장이 코드와 달랐다(리뷰는 Minor로 매겼으나 이 단계의 산출물이 README이므로 같은 고침에 넣었다) | 문제를 진단하는 사용자가 틀린 설명을 읽는다 | 셸이 갑자기 끝나면 Job Object가 서버를 먼저 끝내므로 종료 줄이 대개 없다(1단계 보고서의 관찰)고 고침, 로그 폴더 규칙을 묶은 exe·개발·`XGS_SIDECAR_LOGDIR`·`XGS_SIDECAR_APPDATA`로 나눠 정확히, `sidecar-stderr.log`는 시작마다 덮어쓰니 복사하라고, 만드는 PC에 Node 22.18 이상과 C++ 빌드 도구·Windows SDK가 필요하다고, `shell/README.md`의 "점검용 명령 모두 거부"에서 `check_ping`을 뺌 |

문서만의 고침이라 먼저 실패할 시험이 없다. 증거는 쓴 문장을 코드(`real_sidecar.rs`, `real_download.rs`의 `assert_wire_shape`, `config.rs`)와 1단계 보고서에 다시 대조한 것과 링크 점검(27개)이고 시험은 그대로다(Kotlin 938, TypeScript 239, `sidecar-host` 161 + 5).

### 내가 고치지 않기로 한 것 (판단, 원장의 `Ruling:`)

- **서버·클라이언트 계약을 자동으로 지키는 시험**(리뷰의 권고: 이벤트 종류마다 JSON 예시 파일을 두고 Kotlin과 TypeScript 시험이 같은 파일을 대조): 두 언어에 걸친 새 시험 장치이고 틈은 6단계 이전부터 있었다(지운 Kotlin 클라이언트도 서버와 같은 직렬화기를 써서 잡지 못했다). **잘못 판단했다면:** `shared/api`의 필드 이름이 바뀌어도 오프라인 시험이 모두 통과하고 `real_download`를 손으로 돌릴 때까지 화면만 깨진다. 위 한계에 적었다.

### 미룬 Minor

- `capabilities.test.ts`는 `capabilities/default.json`만 읽는다. Tauri는 그 폴더의 모든 파일과 `tauri.conf.json`의 인라인 `capabilities`를 켠다(리뷰가 둘째 파일로 시험을 통과시켜 보였다): 폴더에 `default.json`뿐이고 두 설정에 인라인 선언이 없다는 단언을 더한다.
- `logDirectoryCandidates`와 `applicationDirectory`(`Diagnostics.kt`)는 지운 `app`의 `Main`만 쓰던 것이라 그 시험이 닿을 수 없는 코드를 시험한다.
- C6 검색이 못 잡는 옛 표현: `diagnostics/build.gradle.kts:22`, `logback-sidecar.xml:5`, `HangWatchdogTest.kt:26`(`uiAnswers`), `config.rs:19`, `real_sidecar.rs:99`, `ExitLogger.kt:26`의 로그 문구 "(콘솔 종료, …)"(사이드카에는 콘솔이 없다), `Diagnostics.kt:54`의 201자 주석 한 줄.
- `SidecarLogbackConfigTest`(4개)는 옛 `LogbackConfigTest`(12개)보다 얇다: `maxFileSize` 5MB, `totalSizeCap` 50MB, 설정 읽기에 경고 없음, 파일 다음 콘솔의 순서, 콘솔에 DEBUG 없음이 고정되지 않았다.
- 예전 `ui-hang-*.txt`는 지워지지 않고(옛 상한이면 최대 100MB) "예전 파일은 건드리지 않는다"를 고정한 시험도 없다. 로그 줄의 로거 이름이 `UiWatchdog`에서 `HangWatchdog`로 바뀐 것도 "바뀐 동작"에 없다.
- `run.bat`: 만들고 나서 실행하는 갈래는 실행해 보지 않았다(점검은 이미 만든 exe로 했다). 더블클릭하면 빌드 실패 때 오류를 읽기 전에 창이 닫힌다(`pause`). 소스를 고친 뒤에는 옛 exe를 실행한다(README에 적었다).

### 권고

- `real_download`를 새 이미지로 한 번 돌려 본다(곡 하나를 받고 작업 하나를 취소한다: 네트워크와 yt-dlp 복사본이 필요하므로 사용자가 허락할 때).

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
| C8 최종 리뷰 | (아래 "최종 리뷰") | |

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
| `HttpXgsApiTest` + `EndToEndTest` | 13 | 서버와 그 클라이언트 사이의 계약. 클라이언트는 이제 Rust 브리지라서 `bridge.rs`의 가짜 서버 시험(28개)과 **`real_sidecar`**(실제 이미지, `#[ignore]`)가 맡는다. `gradlew check`에는 클라이언트 쪽 시험이 없다 |

이 중 앞의 넷은 "원본의 이름이 포트에 모두 있다"를 지키는 점검(`kotlin-coverage.test.ts`)이 있었는데 원본이 없어져 `shared`의 둘만 남겼다. 지운 원본은 `git show f9dbcba:app/...`로 읽을 수 있고 포팅한 파일 일곱 곳의 머리 주석이 그렇게 적는다.

## 바뀐 동작

- 사이드카의 멈춤 덤프 파일: `ui-hang-날짜-시각.txt` → `hang-날짜-시각.txt`. 로그 문구도 "UI 스레드가 N초 동안 응답하지 않음" → "서버 스레드가 N초 동안 응답하지 않음", "UI 응답 회복" → "서버 스레드 응답 회복". 예전 `ui-hang-*.txt`는 지워지지 않고 남는다.
- 웹뷰(페이지)의 권한이 `core:default`뿐이다(`opener:default`가 빠졌다). 페이지는 그 플러그인을 쓴 적이 없다.
- `run.bat`: `dist\xGetSongs.exe`를 실행한다(없으면 `package-exe.bat`으로 먼저 만든다).

## 이 실행에서 계획과 달라진 것

원장(`.superpowers/sdd/2026-10-16-tauri-cleanup/progress.md`)의 `Ruling:`이 기준이다. 모두 계획서의 도구·명령의 결함을 고친 것이고 결과의 숫자는 계획과 같다: 개수 도구가 `check`에 속하지 않는 시험 과제(이전 실행이 남긴 `integrationTest`, `sidecarImageTest`)의 결과까지 더하던 것, 폴더를 지운 뒤의 `git add -A app`가 실패하던 것, `:app` 검색이 `git show f9dbcba:app/...` 주석에 걸리던 것(`:app:`으로), 코드가 이어 붙이는 문구 하나를 통째로 검색하던 것.

## 한계와 남은 일

- **빈 Gradle 캐시에서 처음 내려받는 것은 확인하지 않았다**(네트워크가 필요하고 이 단계는 내려받지 않는다). `google()`과 카탈로그 항목을 걷은 것이 그때 문제가 될 수 있다. 걷은 것은 `app`만 쓰던 것임을 모듈별로 대조했다.
- 예전 `ui-hang-*.txt`는 남는다(20개 보관 규칙이 새 이름만 센다).
- 사용자의 `docs/xGetSongs 작업 메모.txt`는 건드리지 않았다(옛 내용이 있다).
- 사용자의 PC에서의 확인: `dist\xGetSongs.exe`로 실제 재생목록 다운로드와 yt-dlp 설치 버튼(한 번도 실제로 부르지 않았다). Windows 10 실제 PC와 다른 백신은 시험하지 못했다.
- 7단계(구글 가사의 JavaFX 웹뷰를 WebView2로, 선택), `jlink`, 서명·아이콘은 하지 않았다. 같은 날 다른 세션이 쓴 [Python 이행 분석](2026-10-16-python-single-exe-migration-analysis.md)도 "현행 경로 완주"를 권고한다(결정은 보류).

## 최종 리뷰

(전체 브랜치 리뷰의 결과를 여기에 적는다.)

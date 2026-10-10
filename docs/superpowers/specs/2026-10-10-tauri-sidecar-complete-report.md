# Tauri 사이드카 완성(1단계) 보고서

작성일: 2026-10-11 (실행과 측정은 2026-10-11 새벽, 계획은 2026-10-10)
설계: [2026-10-10-tauri-kotlin-sidecar-design.md](2026-10-10-tauri-kotlin-sidecar-design.md) §5의 1단계
계획: [2026-10-10-tauri-sidecar-complete.md](../plans/2026-10-10-tauri-sidecar-complete.md)

환경: Windows 11 Home 10.0.26200, Rust 1.99.0, Tauri 2, 번들한 런타임은 이 PC의 JetBrains Runtime 21.0.9. 측정은 `--config spike.conf.json`으로 다시 만든 릴리스 `shell.exe`를 `XGS_SIDECAR_DIR=server\build\sidecar-image`로 직접 실행해서 했다. `XGS_SIDECAR_LIB`와 `XGS_SIDECAR_JAVA`는 지웠고, 사이드카가 실제로 `sidecar-image\runtime\bin\java.exe`로 돌았음을 프로세스 목록에서 확인했다. 앱 데이터 폴더는 `%TEMP%\xgs-spike-appdata`이고 사용자의 `%APPDATA%\xGetSongs`는 건드리지 않았다.

## 증거: 사이드카만으로 로그·종료 기록이 지금과 같다

| README의 문제 확인 항목 | Compose 앱 | 사이드카 | 근거 |
|---|---|---|---|
| 로그 파일 위치 | 앱 실행 파일 옆 `log\xgetsongs.log` | 셸 실행 파일 옆 `log\`(안 되면 앱 데이터의 `logs`, 임시 폴더) | 종단 확인: `shell\src-tauri\target\release\log\`. 폴백은 `aLogFolderThatCannotBeMadeFallsBackToTheAppDataFolder` |
| 회전(5MB·7일·50MB), UTF-8, 즉시 flush | 앱의 `logback.xml` | `logback-sidecar.xml`이 같은 설정 | `SidecarLogbackConfigTest` 4개(UTF-8 한글, 즉시 flush, 7일 보관, 콘솔은 stderr, DEBUG는 파일에만) |
| 시작 정보(Java, OS, 메모리, 도구 경로) | 있음 | 있음 (`Java: 21.0.9 (JetBrains s.r.o.)`, `프로세서 12개`, `로그 폴더: …`, 도구 경로는 `내장 서버 시작` 줄) | 종단 확인의 로그. `headless` 줄만 빠진다(AWT가 없으니 `null`) |
| 작업 시작·옵션·항목 결과·취소·연결 끊김 | 서버의 `JobLog`가 파일로 | 같은 클래스가 같은 `com.xgetsongs` DEBUG 설정으로 파일로 간다 | `JobLog` 테스트는 서버 모듈에 그대로 있다. 이 단계에서 실제 작업을 돌려 보지는 않았다 |
| 처리되지 않은 예외 | 있음 | `Diagnostics.start`가 같은 핸들러를 건다 | 같은 코드(옮긴 테스트 157개) |
| 멈춤 덤프 `ui-hang-*.txt` | UI 스레드(AWT)가 5초 넘게 응답 없음 | `Dispatchers.Default`의 모든 워커가 5초 넘게 응답 없음 | `aDispatcherThatStopsAnsweringLeavesAThreadDumpInTheLogFolder`(약 9초): 덤프에 `DefaultDispatcher-worker`가 보인다. 셸에서 실제로 멈춤을 만들어 보지는 않았다 |
| 종료 기록: 사용자가 닫음 | 창 닫기 버튼 → `사용자가 창 닫기를 요청함`, `내장 서버 정지`, `JVM 종료 시작: 사용자가 창을 닫음` | 셸이 창을 닫을 때 쓰는 `exit` 줄이 같은 세 줄을 낸다 | 종단 확인 3회 모두 각 기록이 한 번씩, 마커 `state=exited` + `reason=user`. 프로세스 테스트 |
| 종료 기록: 닫지 않은 종료 | 콘솔 종료, SIGTERM 등 → `창을 닫지 않은 종료` (WARN) | `exit` 줄 없이 파이프만 닫히면 같은 WARN, 마커 `reason=other` | `aClosedPipeWithoutAnExitLineIsLoggedAsAnEndNobodyAskedFor`(프로세스 테스트) |
| `last-run.txt`와 다음 실행의 경고 | 있음 | 같은 형식과 같은 경고 | 아래 "셸이 갑자기 죽은 경우" |
| 토큰·헤더·가사·제목·파일명은 로그에 없음 | | 토큰이 로그에 없음, 시작 신호 줄도 없음 | 세 실행의 토큰이 로그에 없다(`False`). `theLogHoldsTheStartRecordsButNeverTheToken` |

Compose 앱도 같은 모듈로 옮긴 뒤 실제로 띄워 봤다: 창이 뜨고, `headless=false`와 `AWT-EventQueue-0`에서의 `사용자가 창 닫기를 요청함`, 종료 기록, 마커 `state=exited` + `reason=user`가 이전과 같다.

## 측정

| 항목 | 값 | 스파이크(JBR을 PATH에서 부름) |
|---|---|---|
| 사이드카 이미지 전체 | 159.1MB | (lib 76.8MB + 번들 없음) |
| lib | 67.2MB, 78개 | 76.8MB, 81개 |
| runtime (JBR 복사본) | 91.9MB | — |
| waitedMs 3회 중앙값 | 1,952ms (2,222 / 1,952 / 1,926) | 1,945ms |
| readyMs 3회 중앙값 | 1,503ms (1,753 / 1,503 / 1,476) | 1,405ms |
| toolsMs 3회 중앙값 | 395ms | 393ms |
| 프로세스 시작 → 창 보임 | 145ms (첫 실행 708ms) | 132ms |
| 창 닫기 → 정리 | 861 / 845 / 844ms, 남은 사이드카 0 | 984 / 884 / 928ms |
| 셸 `shell.exe` | 4.4MB | 4.4MB |

해석:

- 첫 실행(2,222ms, 창 708ms)은 파일 캐시가 식어 있던 값이고 이후 두 번은 같다. 한도(10초)에 한참 못 미친다.
- readyMs가 스파이크보다 약 100ms 길다. 같은 JBR인데 이제 시작할 때 `Diagnostics`(로그 폴더 준비, 마커, 시작 기록)가 도는 비용이 더해진 것으로 보이며, 번들 런타임 때문은 아니다(스파이크도 같은 JBR이다).
- 이미지는 lib(67.2MB)와 런타임(91.9MB)을 합쳐 159.1MB다. 런타임은 `jlink`로 줄이면 크게 작아질 것이다(5단계, `jmods`가 있는 JDK 필요).

## 셸이 갑자기 죽은 경우

셸을 `Stop-Process -Force`로 끝내자 사이드카가 324ms 만에 사라졌다. Job Object가 JVM을 먼저 죽여서 종료 기록이 남지 않았고, 마커는 `state=running`으로 남았다. 같은 로그 폴더로 다시 시작하자 첫 줄에 Compose 앱과 같은 경고가 나왔다:

`WARN … 이전 실행(PID 15440, 시작 2026-10-11 02:57:26)이 정상 종료 기록 없이 끝났음: 강제 종료, 크래시, 전원 차단 등의 가능성`

그 실행을 사용자가 정상으로 닫자 마커는 `state=exited`, `reason=user`가 됐다. 즉 갑작스러운 소멸은 "정상 종료 기록 없음"으로, 사용자 종료는 `reason=user`로 구분된다. 셸이 죽었는데 Job Object보다 stdin 닫힘이 먼저 사이드카를 끝내는 경우(`reason=other`와 `창을 닫지 않은 종료` WARN)는 이 실행에서는 나타나지 않았고, 그 경로는 프로세스 테스트로만 확인했다.

## 막힌 점과 고친 것

- **`main`이 컴파일되지 않았다.** 계획은 Task 3에서 `main`을 그대로 둔다고 했지만, `main`이 `startServer`와 `exit`를 위치 인자 람다 둘로 넘기는 터라 새 `onParentGone` 매개변수가 그 자리를 차지해 `exit`가 비었다. `exit`를 이름 인자로 넘기게 고쳤고(Task 4가 어차피 이 호출을 다시 썼다) 계획서를 맞췄다.
- **lib 개수:** 계획의 77개는 `diagnostics.jar` 하나를 빠뜨린 계산이었다. 실제는 81 − 4 + 1 = 78개다. 계획서의 숫자를 고쳤다.
- 그 밖의 새 코드(Task 2, 4, 5, 6)는 테스트를 먼저 실패시킨 뒤 구현한 첫 시도에서 컴파일과 테스트를 통과했다. Task 1(모듈 이동)과 Task 3은 위의 `main` 한 줄을 빼면 계획대로였다. 컴파일해 보지 못한 채 계획에 쓴 Rust 코드도 오류 없이 통과했다.

## 판정

**통과.** 사이드카가 앱과 같은 진단(파일 로그, 시작 정보, 사용자 종료와 그렇지 않은 종료의 구분, 마커, 다음 실행의 경고, 멈춤 덤프)을 남기고, 번들한 런타임과 줄인 lib로 시작해 `waitedMs` 약 1.95초(한도 10초)다. 전체 테스트는 1,132개 통과, 실패 0, Rust 25개 통과다. 다음은 설계 §5의 2단계 "Rust 브리지"다.

## 이 단계가 확인하지 않은 것

- `jlink`로 줄인 런타임(5단계), 콜드 스타트, Defender·SmartScreen, 셸을 띄운 채 실제 다운로드를 돌리는 경우의 로그와 취소·정리, 셸에서 실제로 서버 멈춤을 만들었을 때의 덤프.
- 사이드카의 stderr를 셸이 `sidecar-stderr.log`로 받아 쓰는 파일은 여전히 회전이나 크기 제한이 없다. 이제 stderr에는 INFO 이상 몇 줄만 나가므로 양은 작다.
- 로그 문구와 `ui-hang-*.txt` 파일 이름은 Compose 앱과 같게 두었다. 사이드카에서는 "UI"가 서버 응답을 뜻하므로 6단계에서 정리한다.
- `README.md`의 로그 설명(위치 규칙 등)은 5~6단계에서 고친다.

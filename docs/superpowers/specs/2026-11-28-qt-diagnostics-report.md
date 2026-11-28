# Qt 전환 Q5(진단) 보고서

- 계획서: `docs/superpowers/plans/2026-11-28-qt-diagnostics.md`
- 기간: 2026-11-28
- 커밋: ec8ceea(계획서), 6eafdc3(작업 1), 34c7f2d·459da1f(작업 2), 이 보고서의 커밋(작업 3)
- 실행 방식: "가볍게"(2026-11-25). 작업 3개, 리뷰는 작업 2에만 1회, 수정분 재리뷰와 전체 최종 리뷰는 하지 않았다.

## 판정: 통과 (실제 죽음·멈춤·로그오프는 실제 앱에서 본 적 없음)

D1~D5를 통과했다. 다만 진짜 `xGetSongs.exe`가 죽거나 멈추는 경우, Windows 로그오프, 작업 중의 로그는 시험용 exe와 부품 시험으로만 확인했다.

## 관문

| | 기준 | 결과 |
|---|---|---|
| D1 | 옮길 수 있는 Kotlin 시험이 같은 이름으로 통과 | 통과. `test-names.ps1` PASSED(44개 클래스, 일부러 뺀 이름 49개, 더한 이름 149개). `ThreadDumpTest` 15개는 옮길 것이 없어 등록하지 않음 |
| D2 | 로거 시험(줄 모양, 넘기기, 정리, 여러 스레드) | 통과(`tst_file_logger`) |
| D3 | 두 빌드 전체 `ctest`, 경고 0 | 통과. 2026-11-28 controller 실행: 릴리스·디버그 모두 `ctest -E tst_real_lrclib` 73/73, 새 시험 5개 20회 반복 통과, `forbidden-calls.ps1` 0건. 경고 0은 구현자 보고(다시 빌드할 것이 없어 직접 보지는 못함) |
| D4 | 릴리스 exe 1회: 시작·종료 기록과 `last-run.txt` | 통과. 아래 |
| D5 | 일부러 죽는 exe가 미니덤프와 로그 한 줄을 남김 | 통과(`tst_diagnostics`의 `crash_probe`: 접근 위반, `std::terminate`) |

## 만든 것

- **`qt/core/diag`:** `FileLogger`(`xgetsongs.log`, 5MB마다 `xgetsongs.yyyy-MM-dd.N.log`로 넘김, 7일·합계 50MB), 로그 폴더 고르기(`%APPDATA%\xGetSongs\logs` → 임시 폴더의 `xgetsongs-logs`), `last-run.txt`(`RunMarkerFile`, 이전 실행 판정), 종료 기록, 멈춤 판정(`HangDetector`, 스레드 없는 `HangWatchdog`), 시작 정보, `JobLog`.
- **`qt/app`:** `Diagnostics`(Qt 메시지를 경로를 가린 뒤 파일로, 시작 정보, 실행 기록, 멈춤 감시 스레드, 종료·로그오프 기록), `crash_dump`(처리되지 않은 예외·`std::terminate` 등에서 `xgetsongs-crash.dmp`), 멈춤 때 `xgetsongs-hang.dmp`, 시작할 때 `work` 폴더 비우기.
- **`qt/ui`:** `EngineApi`가 작업의 시작·항목·취소·끝을 `JobLog` 문구로 남김. 쓰지 않던 `ProcessRunner&`·`tempRoot` 제거.

## exe를 띄워 본 것 (2026-11-28, 사용자에게 알린 뒤 1회)

가짜 `APPDATA`·`LOCALAPPDATA`와 `XGS_CHECK_INSTANCE_ID`. 사용자의 xGetSongs는 떠 있지 않았다. 버튼은 누르지 않았다.

- 약 2.3초에 창이 생김. 8초 뒤 자식 프로세스 0개, 작업 집합 약 119MB. 닫기 요청 뒤 0.13초에 종료 코드 0, 남은 프로세스 없음.
- 생긴 파일은 `logs\xgetsongs.log`(595바이트)와 `logs\last-run.txt`뿐이다. 덤프 파일은 없다.
- 로그: `시작 정보`(앱 0.1.0, Qt 6.11.3, OS, 프로세서 12개, 작업 폴더, 로그 폴더) → `사용자가 창 닫기를 요청함` → `JVM 종료 시작: 사용자가 창을 닫음 (가동 9초, 등록된 작업 0개)`. 멈춤 줄은 없다(10초 동안).
- `last-run.txt`: `state=exited`, `reason=user`.

## 확인하지 못한 것

- 진짜 앱이 죽을 때와 멈출 때(WebView2가 붙은 상태의 덤프 시간 포함). 시험용 exe와 offscreen 시험만 있다.
- 폴더 고르기 같은 Windows 기본 창이 5초 넘게 떠 있을 때 멈춤으로 기록되지 않는지(논리로만).
- 실제 로그오프·종료 메시지(`WM_ENDSESSION`)가 필터에 오는지. 시험은 처리 함수를 직접 부른다.
- 스택 넘침, 순수 가상 호출, 잘못된 인자, `abort` 경로(걸어 두었지만 일으켜 보지 않음). GUI가 아닌 스레드의 `std::terminate`는 `abort`로 기록될 수 있다.
- 같은 식별자의 두 번째 실행이 첫 실행의 로그·기록을 건드리지 않는지(코드 순서로만).
- 실제 조회·다운로드 중의 로그 내용. 가리기는 시험으로만 확인했다.
- 수정분(459da1f) 재리뷰, 작업 1 리뷰, 전체 최종 리뷰.

## 리뷰가 찾은 것

| 지적 | 처리 |
|---|---|
| 중요: 실패 사유에서 파일 이름을 가리는 연결 부분에 시험이 없음 | 시험 추가(459da1f). 그 줄을 빼면 실패함을 확인 |
| 느린 시작이 멈춤으로 기록될 수 있음 | 고침. 감시는 이벤트 루프가 돌기 시작한 뒤에 시작 |
| 틀린 주석, 시험 중 콘솔 창, 전역 변수 경합 | 고침 |
| 죽음 덤프가 "실행마다 덮어씀"이 아니라 다음 죽음까지 남음 | 그대로 둠(판단) |
| `writeRaw`와 로그 넘기기의 경합, 떼어 낸 감시 스레드와 종료 때 정적 소멸자, 죽음 기록 스레드가 끝나는지의 시험 없음, 긴 경로(`\\?\`)의 `work` 비우기, `test-classes.json`의 분류, 다른 스레드의 `terminate` | 고치지 않음 |
| `dbghelp.dll`을 시작할 때 이름으로 불러옴 | Q6으로 넘김(`/DEPENDENTLOADFLAG:0x800`) |

## 옮기지 않은 Kotlin 시험 (JVM에만 있는 것)

- `ThreadDumpTest` 15개 전부(스레드 덤프 글).
- `DiagnosticsTest` 42개 중 20개(Java 속성, jpackage의 앱 폴더, 예외 스택 글, `UncaughtLogger`).
- `HangWatchdogTest` 26개 중 16개(덤프 글 파일 `hang-*.txt`와 그 정리, 스레드 자체). 판정 10개는 옮김.
- `ExitLoggerTest` 1개(종료 훅 등록), `SidecarLoggingTest` 1개(`--log-dir`).
- `JobLogTest`의 HTTP 경로 시험 4개는 `tst_engine_api`로 옮겼다. 토큰 단언은 뺐다(Qt 판에는 HTTP도 토큰도 없다).
- 이름별 목록과 이유는 `qt/tools/test-classes.json`의 `omitted`에 있다.

## Kotlin 판과 다른 것

- 스레드 덤프 글 대신 미니덤프 파일. 죽을 때의 덤프는 새로 생긴 기능이다.
- 죽으면 Windows 오류 상자 없이 창이 사라진다(덤프를 쓴 뒤 스스로 끝냄. Windows 오류 보고로 덤프가 나가지 않게 하려는 것).
- 시작 정보의 Java·힙 줄 대신 Qt 줄. 로거 이름을 줄이지 않는다(`%logger{20}` 없음).
- 멈춤 줄은 `화면 스레드가 N초 동안 응답하지 않음 (덤프: xgetsongs-hang.dmp)`(Kotlin은 `서버 스레드`). 죽음·로그오프 줄은 새로 지었다.
- 취소 줄은 취소 직전에 쓴다. 종료 기록의 작업 수는 이벤트 루프 직후의 값이다. `work`는 내용만 지운다.
- "7일"은 오늘에서 7일 전보다 오래된 넘긴 파일을 지우는 것으로, 50MB는 넘긴 파일만 세는 것으로 구현했다(logback과 가장자리에서 다를 수 있다).
- 원본의 버그로 보이는 것(고치지 않음): `chooseLogDirectory`가 빈 목록에서 예외, `HangWatchdog.tick`이 실패 횟수를 일찍 지워 60번마다의 알림이 한 경우에 적용되지 않음.

## 사용자가 정할 것

1. 종료 기록의 `JVM 종료 시작`을 바꿀지(Qt 판에는 JVM이 없다. 문자열 둘과 시험).
2. 미니덤프를 남기는 것 자체(스택 메모리에 곡 제목이 들어갈 수 있다. PC 밖으로 나가지 않는다).
3. 죽을 때 Windows 오류 상자를 띄우지 않는 것.
4. 죽음 덤프를 다음 죽음까지 남기는 것.
5. 새로 지은 로그 문구.
6. 작업 시작 줄에 `outputDir`(저장 폴더 경로)가 그대로 남는 것. Kotlin 판과 같지만 "경로가 남지 않는다"와 어긋난다.

## Q6으로 넘길 것

- `dbghelp.dll` 로딩 방식, exe의 아이콘·버전 리소스, Qt에 든 라이브러리의 고지 전문.
- README에 로그·덤프 위치와 손으로 지우는 법.

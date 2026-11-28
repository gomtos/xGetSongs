# Qt 전환 Q5(진단) 계획서

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. 사용자의 "가볍게" 지시(2026-11-25)에 따라 작업 3개, 리뷰는 작업 2에만.

**Goal:** Qt 판이 지금 판과 같은 자리에 같은 이름으로 로그를 쓰고, 시작·종료 기록과 `last-run.txt`, 멈춤 감시, 죽을 때의 미니덤프를 갖는다.

**Architecture:** 화면 없는 `qt/core/diag`(Qt Core만)에 로거·기록·판정 로직을 두고 Kotlin `diagnostics` 시험을 같은 이름으로 옮긴다. 프로세스 전체에 거는 것(Qt 메시지 처리기, 처리되지 않은 예외 필터, 감시 스레드)은 `qt/app`에서 조립한다.

**Spec:** `docs/superpowers/specs/2026-11-15-qt-rewrite-and-playlists-design.md` §3(호환), §4, §8; 분석 문서 `2026-11-06-qt-cpp-rewrite-analysis.md`의 Q5 관문. 정답은 Kotlin `diagnostics/`, `server/.../JobLog.kt`, `server/.../sidecar/SidecarDiagnostics.kt`·`SidecarLogging.kt`, `server/src/main/resources/logback-sidecar.xml`과 그 시험.

## Global Constraints

- 로그 폴더: `%APPDATA%\xGetSongs\logs`, 못 만들면 `<임시 폴더>\xgetsongs-logs`. 파일 `xgetsongs.log`, 넘긴 파일 `xgetsongs.yyyy-MM-dd.N.log`, 5MB마다 넘기고 7일·합계 50MB까지 둔다. 줄 모양 `yyyy-MM-dd HH:mm:ss.SSS LEVEL [스레드] 이름 - 내용`, 줄마다 바로 내보낸다. 기록 파일 `last-run.txt`도 같은 폴더.
- 로그에 토큰·가사·제목·파일 이름·경로가 남지 않는다. 지금의 `log_redaction`을 그대로 쓴다. 사용자 이름이 든 경로(로그 폴더, 작업 폴더)를 시작 기록에 쓰는 것은 Kotlin 판이 하던 대로 둔다.
- 진단이 실패해도 앱은 뜬다. 진단 코드는 예외를 밖으로 내보내지 않는다.
- 시험은 창을 띄우지 않고, 네트워크를 쓰지 않고, 실제 `%APPDATA%`·`%LOCALAPPDATA%`에 쓰지 않는다(임시 폴더만).
- JDK·kotlinx 소스를 보지 않는다. Gradle을 돌리지 않는다. 가사·실제 곡 이름을 시험·로그·커밋에 넣지 않는다.
- 두 빌드(`qt-release`, `qt-debug`) 경고 0. `qt/tools/test-names.ps1`, `qt/tools/forbidden-calls.ps1` 통과.

## 관문

| | 기준 |
|---|---|
| D1 | 옮길 수 있는 Kotlin 시험(`DiagnosticsTest`, `ExitLoggerTest`, `HangDetectorTest`, `HangWatchdogTest`, `RunMarkerTest`, `JobLogTest`, `SidecarLoggingTest`)이 같은 이름으로 통과. 옮기지 않는 것은 이유와 함께 목록으로 |
| D2 | 로거 시험: 줄 모양, 넘기기, 7일·50MB 정리, 여러 스레드에서 쓴 줄이 섞이지 않음 |
| D3 | 두 빌드 전체 `ctest`(`-E tst_real_lrclib`) 통과, 경고 0 |
| D4 | 릴리스 exe를 가짜 `APPDATA`로 1회 띄웠다 닫으면 로그에 시작 정보와 종료 기록이 있고 `last-run.txt`가 정상 종료로 남음. 사용자에게 먼저 알린다 |
| D5 | 일부러 죽이는 시험용 exe(창 없음)가 미니덤프와 로그 한 줄을 남김 |

## Review Focus

1. 로그 파일을 다른 프로그램이 잡고 있거나 폴더가 읽기 전용일 때 앱이 그대로 뜨는가.
2. 죽는 순간의 처리기가 힙·잠금을 쓰다가 스스로 멈추지 않는가(미니덤프는 미리 열어 둔 것만 쓴다).
3. 감시 스레드가 종료 때 `join`되고, 닫는 중의 긴 대기(작업 정리 최대 1.5초 등)를 멈춤으로 잘못 기록하지 않는가.
4. 시작한 파일 이름·경로가 든 실패 사유가 `JobLog`를 거쳐 가려지는가.
5. 자정과 5MB 경계에서 넘긴 파일 번호가 겹치지 않는가.

---

### Task 1: `qt/core/diag` 로직과 로거

**Files:** Create `qt/core/diag/{file_logger,log_directory,run_marker,exit_record,hang_detector,startup_record,job_log}.{h,cpp}`; Test `qt/tests/tst_{file_logger,log_directory,run_marker,exit_record,hang_detector,startup_record,job_log}.cpp`; Modify `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/README.md`.

**Produces (작업 2가 쓴다):**
- `diag::chooseLogDirectory(const QStringList& candidates, report) -> QString`, `diag::logDirectoryCandidates(appData, tempDir)`.
- `diag::FileLogger`: `open(dir)`, `write(Level, threadName, loggerName, message)`(어느 스레드에서나, 안에서 잠금), `close()`. 시계와 크기 한계는 생성자로 받는다(시험용).
- `diag::RunMarkerFile`, `judgePreviousRun`, `describePreviousRun`(Kotlin과 같은 이름·같은 문구). 프로세스 표는 `diag::Processes` 인터페이스.
- `diag::ExitRecord`: `markUserExit()`, `text(runningJobs, uptime, crashed) -> QString`(Kotlin `ExitLogger`의 문구).
- `diag::HangDetector`(Kotlin과 같은 판정, 시계 주입).
- `diag::startupRecord(...)`: Java·힙 줄 대신 `Qt: <버전>`, `OS: <이름 버전 아키텍처>`, `프로세서 N개` 줄(작성자 판단).
- `diag::JobLog`: `started`, `describe(JobEvent, fileNames) -> optional<JobLogLine>`, `shortId`, `eventsDisconnected`, `CANCEL_REQUESTED`.

- [ ] Kotlin 시험을 읽고 같은 이름으로 옮긴다(먼저 실패하는 것을 본다). JVM에만 있는 것(스레드 덤프 글, 예외의 스택 글, 종료 훅 등록)은 옮기지 않고 보고서에 목록으로 적는다.
- [ ] 로거 시험(D2)과 Review Focus 1·4·5의 시험을 더한다.
- [ ] 구현, 두 빌드 통과, 이름·금지 호출 점검, 커밋 `Qt: 진단 로직과 로그 파일`.

### Task 2: 프로세스에 걸기 (`qt/app`)

**Files:** Create `qt/app/diagnostics.{h,cpp}`, `qt/app/crash_dump.{h,cpp}`, `qt/tests/app/tst_diagnostics.cpp`, `qt/tests/support/crash_probe.cpp`(일부러 죽는 exe); Modify `qt/app/main.cpp`, `qt/app/app_services.{h,cpp}`, `qt/ui/engine_api.{h,cpp}`, `qt/app/CMakeLists.txt`, `qt/tests/CMakeLists.txt`.

- [ ] `app::Diagnostics::start(paths)`: 로그 폴더 고르기 → `FileLogger` 열기 → `qInstallMessageHandler`로 Qt 메시지(범주 이름이 로거 이름)를 파일에 → 시작 정보 → 이전 실행 판정과 이번 실행 기록 → 감시 시작. `main`에서 단일 인스턴스 확인 **뒤**, 다른 것 **앞**에 부른다(두 번째 실행이 첫 실행의 파일을 건드리지 않게).
- [ ] 죽을 때: `SetUnhandledExceptionFilter`와 `std::set_terminate`가 미리 열어 둔 `xgetsongs-crash.dmp`(로그 폴더, 실행마다 덮어씀)에 `MiniDumpWriteDump(MiniDumpNormal)`를 쓰고 로그에 한 줄을 남긴다. 덤프는 PC 밖으로 나가지 않는다.
- [ ] 멈춤 감시: 감시 스레드가 GUI 스레드에 박동을 `QMetaObject::invokeMethod(Qt::QueuedConnection)`로 보내고 `HangDetector`가 멈춤이라 하면 WARN 한 줄과 `xgetsongs-hang.dmp`(한 번의 멈춤에 한 번)를 남긴다. 문턱과 간격은 Kotlin `HangWatchdog` 값. `main`이 이벤트 루프를 나온 뒤에는 감시를 멈춘다.
- [ ] 종료 기록: 창 닫기에서 `markUserExit`, `main` 끝에서 종료 기록을 쓰고 `last-run.txt`를 고친다. `WM_QUERYENDSESSION`/`WM_ENDSESSION`(로그오프·종료)에서도 기록을 쓴다.
- [ ] `JobLog`를 `EngineApi`의 작업 흐름에 잇는다(시작, 사건, 취소 요청, 끝). `GoogleLog`는 그대로 범주 로깅(이제 파일로 간다).
- [ ] 시작할 때 `%APPDATA%\xGetSongs\work`의 남은 것을 지운다(Kotlin 판이 하던 대로, 단일 인스턴스 확인 뒤). `EngineApi`가 쓰지 않는 `ProcessRunner&`·`tempRoot`를 뺀다.
- [ ] 시험: `crash_probe`가 덤프와 로그 줄을 남김(D5, 창 없음, Windows 오류 상자가 뜨지 않게 `SetErrorMode`), 감시가 멈춘 GUI 스레드를 한 번만 기록, 종료 때 스레드가 남지 않음. 커밋 `Qt: 진단을 앱에 연결`.

### Task 3: 관문과 보고서 (controller)

- [ ] 두 빌드 전체 `ctest`, 새 시험 20번 반복, 점검 도구.
- [ ] 사용자에게 알린 뒤 릴리스 exe 1회(가짜 `APPDATA`·`LOCALAPPDATA`, `XGS_CHECK_INSTANCE_ID`): D4.
- [ ] 보고서 `docs/superpowers/specs/2026-11-28-qt-diagnostics-report.md`, 커밋.

## 사용자가 정할 것 (실행은 아래 값으로 진행)

1. 미니덤프를 남기는 것 자체(스택 메모리에 곡 제목이 들어갈 수 있다. PC 밖으로 나가지 않고 실행마다 덮어쓴다).
2. 스레드 덤프 글 대신 미니덤프 파일로 바꾸는 것(C++에는 스레드 덤프 글이 없다).

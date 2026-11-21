# Qt 전환 Q2d: 내려받기 작업·동시성·취소 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Kotlin `engine`의 작업 부분(`ItemDownloader`, `DefaultDownloadService`, `Services.kt`의 `DownloadRequest`·`JobHandle`·`DownloadService`)을 C++/Qt로 옮겨, Q2a~Q2c에서 만든 부품(실행기, 명령 조립, 진행률, 오류 분류, 정보 파일, 태그 쓰기, 출력 폴더, 가사 공급자)을 하나의 내려받기 작업으로 잇고, `DefaultDownloadServiceTest`의 테스트 103개를 같은 이름으로 통과시킨다. 이것으로 Q2(`engine`)가 끝난다.

**Architecture:** 코루틴 자리에 스레드를 쓴다. 작업 하나는 조정 스레드 하나와 일꾼 스레드 `min(동시 수, 곡 수)`개로 돌고, 일꾼은 곡을 목록 순서대로 하나씩 가져간다. 취소는 `std::stop_source` 하나이고 모든 블로킹 호출(실행기, 메타데이터 조회, 가사 조회, 태그 쓰기, 재시도 대기)에 그 토큰이 간다. 이벤트는 잠금으로 지킨 큐에 쌓이고 읽는 쪽 하나가 꺼낸다(Kotlin의 `Channel.UNLIMITED`). 순서가 정해지는 부분(동시 수 1)은 **가짜 실행기·가짜 출력·가짜 공급자를 양쪽에 끼워 Kotlin과 무작위 대조**하고, 스레드·취소·동시 수는 C++ 시험으로 본다.

**Tech Stack:** C++20(`std::jthread`, `std::stop_token`, `std::condition_variable_any`), Qt 6.11.3 Core·Network·Test 정적 링크, MSVC, CMake + Ninja, PowerShell 7, 대조용 JVM은 `server/build/sidecar-image`.

**Spec:** [설계 문서](../specs/2026-11-15-qt-rewrite-and-playlists-design.md) §2, §3, §4(Q2 행). 앞 단계의 인계 목록: [Q2a 보고서](../specs/2026-11-15-qt-engine-logic-report.md) "Q2d에 넘길 것", [Q2b 보고서](../specs/2026-11-18-qt-engine-process-report.md) "Q2d에 넘길 것", [Q2c 보고서](../specs/2026-11-20-qt-engine-lrclib-report.md) "Q2d에 넘길 것". Kotlin 원본: `engine/src/main/kotlin/com/xgetsongs/engine/Services.kt`, `job/ItemDownloader.kt`, `job/DefaultDownloadService.kt`, 시험 `engine/src/test/kotlin/com/xgetsongs/engine/job/DefaultDownloadServiceTest.kt`, `testutil/Fakes.kt`.

## 이 계획에서 작성자가 정한 것 (사용자가 바꿀 수 있다)

1. **코루틴 대신 스레드.** 곡마다 스레드를 만들지 않는다(5,000곡 재생목록). 일꾼은 `min(max(동시 수, 1), 곡 수)`개이고 곡을 목록 순서대로 가져간다(Kotlin의 세마포어도 도착 순서로 내준다).
2. **취소된 곡은 이벤트를 내지 않는다**(Kotlin과 같다). C++에서는 예외 대신 결과로 알린다: `download`의 결과에 `DownloadCancelled`가 있고, `prepare` 뒤에는 부른 쪽이 멈춤 토큰을 본다.
3. **작업의 끝 상태.** 치명적 실패가 있었으면 `Failed`, 아니고 멈춤이 요청됐으면 `Cancelled`, 아니면 `Completed`. 마지막 곡이 끝난 직후에 온 취소는 Kotlin처럼 어느 쪽으로도 보일 수 있다. 치명적 실패 뒤의 사용자 취소는 `Failed`다.
4. **`start`는 멈춤 소스를 받을 수 있다**(`std::stop_source`, 기본은 새 것). Kotlin의 "시작 직후 취소" 시험은 코루틴 디스패처 덕에 순서가 정해져 있지만 스레드에서는 경합이므로, 그 시험은 **이미 멈춤이 요청된 소스**로 시작해 "폴더를 만들지 않고 `JobDone(Cancelled)` 하나"를 본다. 가사 공급자가 취소를 던지는 Kotlin 시험도 같은 소스로 옮긴다(공급자 안에서 멈춤을 요청하고 빈 값을 돌려준다).
5. **출력의 예외를 결과로 읽는 규칙.** Kotlin은 예외의 형으로 가르고 C++의 `PutResult`는 상태·문구·Win32 코드를 준다.

   | Kotlin | C++ `PutResult` | 작업이 하는 일 |
   |---|---|---|
   | 정상 | `Stored` | `ItemDone` |
   | `FileAlreadyExistsException`, 덮어쓰기 꺼짐, 지금 `exists`가 참 | `AlreadyExists` | `ItemSkipped("이미 존재")` |
   | `FileAlreadyExistsException`, 그 밖(덮어쓰기 켜짐이거나 `exists`가 거짓) | `AlreadyExists` | `ItemFailed("파일 처리 중 오류: " + 문구)`, 문구는 `error`가 비면 파일 이름 |
   | `AccessDeniedException` | `Failed`, `code == ERROR_ACCESS_DENIED(5)` | `ItemFailed("출력 폴더에 쓸 권한이 없습니다.")`, 작업 중단 |
   | 디스크 가득(예외의 reason/message에 표식) | `Failed`, `code`가 `ERROR_DISK_FULL(112)`·`ERROR_HANDLE_DISK_FULL(39)`이거나 `error`를 소문자로 바꾼 글에 `no space left`·`not enough space`·`disk full`·`공간이 부족` | `ItemFailed("디스크 공간이 부족합니다.")`, 작업 중단 |
   | 그 밖의 `IOException` | `Failed` | `ItemFailed("파일 처리 중 오류: " + error)` |

   `AlreadyExists`에는 디스크 가득 검사를 하지 않는다(Kotlin 시험 `aFileNamedLikeADiskFullErrorDoesNotAbortTheJob`의 뜻). 곡의 작업 폴더를 만들지 못한 것도 같은 표의 `Failed` 줄로 읽는다(코드는 `GetLastError`).
6. **실행기가 프로그램을 띄우지 못했거나 놓쳤을 때**(`RunStatus::StartFailed`·`Failed`; Kotlin에서는 `IOException`). 그 곡은 `ItemFailed("파일 처리 중 오류: " + 실행기의 문구)`이고, 문구에 디스크 가득 표식이 있으면 작업 중단이다(Kotlin의 `isDiskFull`과 같은 길).
7. **재시도 대기는 끊을 수 있고 주입한다.** 기본은 Kotlin의 2초·4초. 대기 중 취소되면 곧 돌아온다. 시험은 0~수 ms를 넣는다(Kotlin 시험은 가상 시간이다).
8. **NaN·무한대 백분율은 "모름"으로 보낸다**(Q2a 인계). 진행률 솎아내기(정수 백분율이 바뀔 때만)는 Kotlin의 수(`toInt()`: NaN은 0, 무한대는 끝값)로 그대로 하고, 이벤트의 `percent`만 비운다.
9. **출력 틀의 `%`.** 작업 폴더 경로는 `escapeOutputTemplateDirectory`를 거쳐 넘긴다(Q2b 인계). **영상 ID에 `%`가 있으면 그 곡은 실패다**(`downloadCommand`가 다른 금지 글자에 쓰는 것과 같은 문구, `FailureKind::Other`). 실제 유튜브 ID에는 `%`가 없다. `downloadCommand`가 던지는 `std::invalid_argument`도 같은 식으로 그 곡 하나의 실패가 된다(Q2a 인계).
10. **`put` 직전의 취소.** 태그까지 쓴 곡이라도 `put`을 시작하기 전에 멈춤이 요청됐으면 옮기지 않는다(취소된 곡). `put`이 시작됐으면 끝까지 가고 성공이면 `ItemDone`이다.
11. **`put`이 실패한 곡의 파일은 Kotlin처럼 작업 폴더와 함께 지운다.** Q2b 보고서는 "남겨 두고 다시 시도"를 제안했지만, 남기면 임시 폴더가 쌓이고 다시 옮기는 화면이 없다. 사용자는 그 곡을 다시 받으면 된다.
12. **작업 폴더 지우기는 다시 해 본다.** 한 번에 안 지워지면 약 1.5초 동안 다시 해 본다(막 죽인 프로세스나 백신이 파일을 잠깐 쥘 수 있다; Q2b의 옮기기 재시도와 같은 이유, Kotlin은 한 번). 그래도 남으면 조용히 둔다(Kotlin과 같다).
13. **일꾼 스레드에서 새어 나온 예외는 그 곡의 실패다.** `std::exception`은 `what()`(UTF-8로 읽음), 그 밖은 `알 수 없는 오류`. Kotlin의 `catch (e: Exception)` 자리다. 가사 공급자가 던진 것은 Kotlin처럼 "가사 없음"이다.
14. **이 계획에 넣지 않는 것.**
    - yt-dlp 설치·업데이트를 내려받기와 겹치지 않게 하는 것(Q2b 인계)은 화면이 단추를 막는 것으로 Q4에서 한다. Kotlin 엔진에도 그 장치는 없다.
    - `server`의 `JobRegistry`·`ResolveCache`·`JobLog`는 Q4·Q5의 일이다.
    - 구글 가사(Q3). 작업은 `LyricsProvider`면 무엇이든 받는다.
    - 이벤트를 화면 스레드로 넘기는 다리(Q4). 여기서는 블로킹 `nextEvent`와 안 막는 `tryNextEvent`만 둔다.
    - 도구 실행의 시간 한도(Kotlin에도 없다, Q2b "사용자가 정할 것" 13).
15. **실제로 내려받는 시험은 이 계획에서 돌리지 않는다.** 미뤄 둔 `downloadsAShortVideoAsM4aWithoutReencoding`(오디오 약 300KB + LRCLIB 요청)과 "내려받는 중 취소하면 실제 ffmpeg 자손까지 죽는가"는 `XGS_INTEGRATION=1`과 `XGS_INTEGRATION_AUDIO=1`이 둘 다 있을 때만 돈다. 코드는 쓰고 건너뛰는 것만 확인한다. 사용자가 허락하면 관문에서 한 번 돌린다.

## Global Constraints

- 정답은 Kotlin이다. `shared/`, `engine/`, `server/`, `diagnostics/`, `shell/`, `sidecar-host/`는 읽기만 한다. C++ 결과가 Kotlin과 다르면 C++을 고친다. Kotlin 쪽이 버그로 보여도 고치지 않고 보고서에 적는다. 예외는 위 "작성자가 정한 것"과 Q2a~Q2c 보고서의 "Kotlin과 다르게 한 것"뿐이다.
- **JDK(OpenJDK)와 `kotlinx.coroutines`·`kotlinx.serialization`의 소스를 보지 않고, 기억에서 옮겨 쓰지 않는다.** 코루틴의 동작(세마포어 순서, 취소, `Channel`)은 공개 문서와 대조 도구가 관찰한 답으로만 정한다.
- 바꾸는 곳은 `qt/`(단 `qt/spike/` 제외)와 Task 6의 보고서뿐이다.
- `xgscore`는 Qt Core, Qt Network, Win32 API만 쓴다. 네임스페이스 `xgs`, C++20, `/W4 /WX /permissive- /utf-8`. 금지 호출 목록은 그대로다(`qt/tools/forbidden-calls.ps1`). 대소문자 접기와 공백 판정은 `xgs::kt`의 것을 쓴다.
- **단위 테스트와 `ctest`는 네트워크를 쓰지 않고 실제 yt-dlp·ffmpeg를 띄우지 않는다.** 실제 도구는 `XGS_INTEGRATION=1`인 통합 테스트만 쓰고, 내려받기는 `XGS_INTEGRATION_AUDIO=1`까지 있어야 한다. 이 계획의 실행은 그 둘째 변수를 켜지 않는다(사용자가 허락한 경우의 관문만 예외).
- 스레드는 `std::jthread`만 쓴다(`QThread`·`QtConcurrent`·전역 스레드 풀을 쓰지 않는다). 작업 핸들이 없어지면 그 작업의 스레드가 하나도 남지 않는다. 일꾼 스레드 밖으로 예외가 나가지 않는다(`std::terminate` 금지).
- 줄 처리기(`onStdout`·`onStderr`)는 막히지 않는다: 큐에 넣는 것 말고는 기다리지 않는다(Q2b 인계).
- 테스트는 Qt Test. Kotlin의 `@Test` 하나가 같은 이름의 슬롯 하나가 되고 단언을 하나도 빼지 않는다. 기대값·통계 문턱·생성기의 강도를 바꿔 통과시키지 않는다. 테스트는 창을 띄우지 않는다. 기다림은 사건(래치·조건 변수)으로 하고, 시간에 기대는 곳은 넉넉한 한도와 이유를 주석으로 적는다.
- 실제 가사나 실제 서비스가 돌려준 곡 이름을 출력·로그·보고서·커밋에 남기지 않는다. 시험의 글은 Kotlin 시험처럼 지어낸 것만 쓴다.
- 대조는 종류마다 1만 건 × seed 셋(20261115, 1, 987654321)과 10만 건 한 번(seed 424242)을 돈다(`-DebugTool` 한 번 포함). 이식 Task마다 변이가 대조나 시험에 잡히는지 본다.
- 관문은 릴리스와 디버그 두 빌드에서 `ctest`를 돈다. Gradle은 돌리지 않는다. 실행 중인 xGetSongs와 `%APPDATA%\xGetSongs`를 건드리지 않는다(통합 테스트가 그 안의 도구를 실행하는 것만 허용, 쓰기·지우기·`yt-dlp -U` 금지). 임시 파일은 `QTemporaryDir` 아래에만 만든다.
- 커밋은 `main`에 직접, 메시지는 한국어, 코드와 주석은 영어, 끝에 실행하는 세션의 Co-Authored-By 줄. push하지 않는다.

## Review Focus

Kotlin 테스트 103개가 다루지 않지만 실제로 만날 것이다.

1. **일꾼들이 서로 다른 단계에 있을 때의 취소.** 메타데이터 조회 중, yt-dlp 실행 중, 가사 조회 중, 태그 쓰는 중, 재시도 대기 중, 차례를 기다리는 중인 곡이 섞여 있어도 작업은 곧 끝나고 `JobDone(Cancelled)`가 한 번 오며 작업 폴더가 없어진다 → Task 2 `cancellingWithItemsInEveryStageEndsPromptly`.
2. **아무도 이벤트를 읽지 않거나, 도는 중에 핸들이 없어진다**(창을 닫음) → Task 2 `destroyingTheHandleOfARunningJobCancelsAndJoins`, `aJobNobodyReadsStillFinishes`.
3. **큰 재생목록.** 5,000곡에 동시 수 64, 전부 "이미 존재" → 스레드가 64개를 넘지 않고, 곡마다 `ItemStarted` 다음 `ItemSkipped`가 오며, 몇 초 안에 끝난다 → Task 2 `fiveThousandItemsUseNoMoreThreadsThanTheConcurrency`.
4. **한 곡의 치명적 실패가 다른 곡을 끊는다.** 내려받는 중이던 다른 곡의 프로세스가 죽고, 그 곡들은 실패 이벤트를 내지 않으며, 요약은 실제로 끝난 곡만 세고, `JobDone(Failed)`가 한 번 온다 → Task 2 `aFatalFailureStopsTheOtherRunningItemsWithoutFailingThem`.
5. **경로의 `%`·공백·한글.** 임시 폴더나 출력 폴더 경로에 `%(id)s`, `100%`, 공백, 한글이 있다 → Task 1 `aPercentSignInTheWorkFolderIsEscapedInTheCommand`, Task 5의 실제 실행기 시험.
6. **부품이 던진다.** 메타데이터 조회, 출력, 실행기(줄 처리기에서 난 예외를 다시 던진다)가 예외를 던져도 프로세스가 죽지 않고 그 곡만 실패한다 → Task 2 `anExceptionOnAWorkerThreadFailsOnlyThatItem`.

---

## File Structure

| 파일 | 하는 일 |
|---|---|
| `qt/core/job/item_downloader.h/.cpp` | `ItemDownloader.kt`: 곡 하나의 이름 정하기, 내려받기, 태그 |
| `qt/core/job/download_service.h` | `Services.kt`의 `DownloadRequest`·`JobHandle`·`DownloadService` |
| `qt/core/job/job_events.h/.cpp` | 이벤트 큐(쓰는 쪽 여럿, 읽는 쪽 하나, 닫기) |
| `qt/core/job/default_download_service.h/.cpp` | `DefaultDownloadService.kt` |
| `qt/tests/support/fake_output_sink.h` | 대본대로 답하는 `OutputSink`(Kotlin 시험의 익명 객체들) |
| `qt/tests/support/job_fakes.h/.cpp` | `Fakes.kt`의 나머지: `outputDirOf`, `videoIdOf`, `writeFakeM4a`·`Cover`·`Info`, `writeFakeTagged`, `ffmetadataTextOf`, `downloadRunner`, `ytDlpCommands`·`ffmpegCommands`, `FAKE_AUDIO` |
| `qt/tests/tst_item_downloader.cpp` | 새 시험(Kotlin에 짝 없음) |
| `qt/tests/tst_default_download_service.cpp` | `DefaultDownloadServiceTest.kt`의 103개와 더한 시험 |
| `qt/tests/helpers/test_child.cpp` | 가짜 yt-dlp·ffmpeg 노릇을 하는 모드 추가(Task 5) |
| `qt/tests/tst_job_with_real_processes.cpp` | 실제 실행기·실제 출력 폴더 + `xgs-test-child`(네트워크 없음) |
| `qt/tests/integration/tst_real_download.cpp` | 미룬 통합 시험 둘(`XGS_INTEGRATION_AUDIO=1` 뒤) |
| `qt/tools/differential/*` | 종류 `W`에 op `jb`(작업) |
| `docs/superpowers/specs/2026-11-21-qt-engine-job-report.md` | 결과 보고서 |

**대조 op `W jb`** (동시 수는 늘 1, 취소 없음, 재시도 대기 0)

| 사례 | 답 | 통계 문턱 (1만 건 기준) |
|---|---|---|
| 곡 1~6개(순위, 영상 ID, 제목, 채널, 가수, 곡명, `lowConfidence`), 요청(덮어쓰기, 순위 넣기, 앨범, 앨범 덮어쓰기, 가사 찾기), 메타데이터 조회의 답, yt-dlp 호출마다의 대본(종료 코드, stderr 줄, stdout 줄, m4a·표지·정보 파일을 쓰는지와 정보 파일의 바이트), ffmpeg의 종료 코드, 가사 공급자의 답, 출력의 대본(이미 있는 이름, `put`마다의 결과) | 이벤트 전체(차례대로), 태그 실행마다의 ffmetadata 글(hex), 가사 질의 목록, yt-dlp·ffmpeg 호출 수, 끝난 뒤 작업 폴더에 남은 항목 수 | `Completed` 40% 이상, `Failed` 10% 이상, `ItemSkipped`가 있는 사례 15% 이상, `ItemFailed`가 있는 사례 20% 이상, 재시도가 일어난 사례 10% 이상, 두 번 재시도 뒤 포기 3% 이상, `ItemDone`의 가사 결과 네 가지가 각각 5% 이상, `lowConfidence` 다시 파싱 10% 이상, 한 작업에 같은 파일 이름이 둘 이상 5% 이상, `put`의 `AlreadyExists` 3% 이상, 접근 거부 3% 이상, 디스크 가득 3% 이상, 진행률 이벤트가 셋 이상인 사례 20% 이상, 유한하지 않은 백분율 2% 이상, 앨범 덮어쓰기 10% 이상, 정보 파일이 깨졌거나 없음 10% 이상 |

Kotlin 쪽은 진짜 `DefaultDownloadService(ItemDownloader(가짜 실행기, 가짜 도구, 가짜 메타데이터, 가짜 공급자), 임시 폴더, scope, retryDelays = 0초 둘)`을 `runBlocking`으로 돌린다. 출력은 양쪽 다 대본 출력(진짜 폴더에 옮기지 않는다)이고, 대본의 결과는 "작성자가 정한 것 5"의 표로 양쪽에 대응시킨다. **알려진 차이는 비교 전에 맞춘다:** 유한하지 않은 백분율은 Kotlin 쪽 답을 "모름"으로 적고(8번), `AlreadyExists`가 실패가 되는 줄의 문구 꼬리는 빼고 비교한다(5번). 영상 ID는 `[A-Za-z0-9_-]`로만 만든다(9번은 따로 시험). 그 밖에 답을 손보지 않는다.

---

### Task 1: 곡 하나 내려받기 (`ItemDownloader`)

**Files:**
- Create: `qt/core/job/item_downloader.h`, `qt/core/job/item_downloader.cpp`
- Create: `qt/tests/support/job_fakes.h`, `qt/tests/support/job_fakes.cpp`, `qt/tests/tst_item_downloader.cpp`
- Modify: `qt/tests/support/fake_process_runner.h/.cpp`(멈춤 토큰을 받는 처리기), `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/tests/support/CMakeLists.txt`, `qt/README.md`(Kotlin에 짝 없는 시험 목록)

**Interfaces:**
- Consumes: `process::ProcessRunner`, `tools::ToolPathProvider`, `ytdlp::VideoMetadataSource`, `lyrics::LyricsProvider`·`NoLyricsProvider`, `tags::M4aTagger`, `ytdlp::downloadCommand`·`escapeOutputTemplateDirectory`·`parseProgress`·`classifyError`·`readVideoInfo`, `lyrics::extract`, 제목 파서, `filename::format`, `ParsedInput::canonicalUrl`, `model/models.h`
- Produces:

```cpp
namespace xgs::job {
struct PreparedItem {
    ResolvedItem item;
    QString fileName;
    QString artist;
    QString track;
    std::optional<QString> album;
    bool searchLyricsOnline = false;
    std::optional<QString> albumOverride;          // never blank: a blank one is absent
    bool operator==(const PreparedItem&) const = default;
};
struct Downloaded { QString file; LyricsOutcome lyrics; };     // the finished m4a, still in the work folder
struct DownloadFailed { ytdlp::Failure failure; };
struct DownloadIoError { QString message; };                   // the runner could not start or lost the program (Kotlin: IOException)
struct DownloadCancelled {};
using DownloadResult = std::variant<Downloaded, DownloadFailed, DownloadIoError, DownloadCancelled>;
using Emit = std::function<void(const JobEvent&)>;             // may be called from the runner's reader threads; must not block

inline constexpr QStringView AlbumArtist = u"Various Artists";

class ItemDownloader final {
public:
    // Everything is referenced, not owned, and must outlive the downloader. Safe to call from several threads at once.
    ItemDownloader(process::ProcessRunner& runner, const tools::ToolPathProvider& tools, ytdlp::VideoMetadataSource& metadata);
    ItemDownloader(process::ProcessRunner& runner, const tools::ToolPathProvider& tools, ytdlp::VideoMetadataSource& metadata,
                   lyrics::LyricsProvider& lyrics);
    PreparedItem prepare(const ResolvedItem& item, const std::optional<QString>& album, bool includeRank,
                         bool searchLyricsOnline, const std::optional<QString>& albumOverride, std::stop_token stop);
    DownloadResult download(const PreparedItem& prepared, const QString& workDir, const Emit& emit, std::stop_token stop);
};
}
```

`download`의 순서는 Kotlin 그대로다: yt-dlp 없음 → `Fatal` `yt-dlp를 찾을 수 없습니다.`, ffmpeg 없음 → `Fatal` `ffmpeg를 찾을 수 없습니다.`, 명령 실행(진행률 솎아내기), 종료 코드 0이 아님 → `classifyError(stderr)`, `Progress(Finishing)` 한 번, `<id>.m4a` 없음 → `Other` `받은 m4a 파일을 찾을 수 없습니다.`, 표지와 정보 파일 읽기, 가사(설명란 → 허용되면 공급자), 태그, `Downloaded`. 멈춤은 실행기가 `Cancelled`를 주었을 때, 가사 조회 뒤 토큰이 서 있을 때, 태그 쓰기가 `Cancelled`일 때 `DownloadCancelled`가 된다. `prepare`는 조회가 취소돼 빈 값을 받으면 처음 파싱한 이름으로 돌아오고 부른 쪽이 토큰을 본다.

- [ ] **Step 1: `job_fakes`를 쓴다.** `Fakes.kt`의 함수를 같은 뜻으로 옮긴다(이름은 `outputDirOf`, `videoIdOf`, `writeFakeM4a`, `writeFakeCover`, `writeFakeInfo(command, QByteArray json)`, `fakeAudio()`, `endsWithFakeAudio`, `isFfmpegCommand`, `writeFakeTagged`, `ffmetadataTextOf`, `downloadRunner(ProcessHandler ytDlp)`, `ytDlpCommands(runner)`, `ffmpegCommands(runner)`). 가짜 메타데이터 `FakeMetadata`(답을 주는 함수, 받은 ID 기록)도 여기 둔다. **`FakeProcessRunner`에 멈춤 토큰을 받는 처리기 꼴을 더한다**(`StoppableProcessHandler = std::function<int(const QStringList&, const LineHandler&, const LineHandler&, std::stop_token)>`와 그것을 받는 생성자; 지금 꼴과 그 동작은 그대로 둔다). Kotlin의 `awaitCancellation()`을 쓰는 가짜가 이 꼴로 "멈춤이 요청될 때까지 기다린다"가 된다. `tst_system_process_runner` 등 지금 시험이 그대로 통과하는지 본다.

- [ ] **Step 2: 실패하는 시험을 쓴다** (`tst_item_downloader.cpp`, 모두 Kotlin에 짝이 없다)

| 슬롯 | 보는 것 |
|---|---|
| `aStopDuringYtDlpIsCancelledAndClassifiesNothing` | 실행기가 stderr에 `ERROR: …`를 쓰고 `Cancelled`를 주면 결과는 `DownloadCancelled`, ffmpeg 호출 0 |
| `aStopBeforeTheRunRunsNothing` | 이미 선 토큰: 명령 기록 0, `DownloadCancelled` |
| `aStopDuringTheLookupIsCancelledAndWritesNoTags` | 공급자 안에서 멈춤 요청 → `DownloadCancelled`, ffmpeg 호출 0 |
| `aStopDuringTaggingIsCancelled` | ffmpeg 가짜가 멈춤을 요청 → `DownloadCancelled` |
| `aProgramThatCannotBeStartedIsAnIoError` | 가짜가 `StartFailure{"boom"}` → `DownloadIoError{"boom"}`; `RunFailure`도 같다 |
| `aPercentSignInTheWorkFolderIsEscapedInTheCommand` | 작업 폴더 `…\100% %(id)s 한글`: `-o` 값의 폴더 부분이 `escapeOutputTemplateDirectory`의 답이고, 파일은 원래 폴더에서 찾는다 |
| `aVideoIdWithAPercentSignFailsTheItemWithoutRunningAnything` | ID `ab%cd`: `DownloadFailed`(`Other`), 명령 기록 0, 문구는 `downloadCommand`가 `a*b`에 내는 `what()`과 같다 |
| `aVideoIdTheCommandRejectsFailsTheItem` | ID `a*b`, 빈 ID: `DownloadFailed`(`Other`), 명령 기록 0 |
| `aPercentThatIsNotFiniteIsReportedAsUnknown` | stdout에 NaN·무한대가 되는 진행률 줄(Q2a 보고서의 예): `Progress.percent`가 비고, 이벤트 수는 Kotlin의 솎아내기 규칙대로 |
| `aLineCutShortByTheKillIsIgnored` | `XGSP|downloading|5`처럼 잘린 줄 뒤 `Cancelled`: 던지지 않고 `DownloadCancelled` |
| `theInfoFileIsReadOnlyAfterYtDlpHasExited` | 가짜가 돌아오기 직전에 정보 파일을 쓴다: 그 내용의 앨범이 태그에 있다(실행 중에 읽지 않는다) |
| `aProviderThatThrowsIsNoLyrics` | 공급자가 `std::runtime_error`를 던져도 `Downloaded`, 결과 `NotFound` |
| `aMetadataSourceThatAnswersNothingAfterAStopKeepsTheFirstParse` | `prepare`: 조회가 멈춤 뒤 빈 값 → 처음 이름 그대로, 던지지 않음 |
| `twoDownloadsOfTheSameDownloaderCanRunAtOnce` | 스레드 둘이 서로 다른 작업 폴더로 동시에: 둘 다 `Downloaded`, 서로의 stderr·진행률이 섞이지 않는다 |
| `theDownloaderReturnsTheFileInsideTheWorkFolder` | `Downloaded.file`이 `<작업 폴더>\<id>.m4a`이고 태그된 내용이다 |

- [ ] **Step 3: 돌려서 실패를 본다.** `cmake --build build/qt --target tst_item_downloader` → 컴파일 실패(`item_downloader.h` 없음).

- [ ] **Step 4: `ItemDownloader`를 구현한다.** stderr 줄은 잠금 아래 모은다(두 처리기가 다른 스레드에서 불릴 수 있다). `emit`은 처리기 안에서 바로 부른다(막히지 않는다는 약속은 `Emit`의 것이다). 3인자 생성자는 안에 `NoLyricsProvider` 하나를 가진다.

- [ ] **Step 5: 돌려서 통과를 본다.** `ctest --test-dir build/qt -R "tst_item_downloader" --output-on-failure` → 15개 통과. 디버그 빌드도 같다.

- [ ] **Step 6: 변이.** (a) `Finishing` 이벤트를 종료 코드 검사 앞으로 옮긴다, (b) `escapeOutputTemplateDirectory`를 뺀다, (c) 가사 조회 뒤의 멈춤 검사를 뺀다. 각각 어느 슬롯이 잡는지 적고 되돌린다((a)는 Task 3의 Kotlin 슬롯이 잡으므로 여기서는 "아직 잡는 시험 없음"으로 적어도 된다).

- [ ] **Step 7: 커밋.** `git add qt && git commit -m "Qt: 곡 하나 내려받기(ItemDownloader) 이식"`

### Task 2: 작업 서비스와 작업 수준의 시험

**Files:**
- Create: `qt/core/job/download_service.h`, `qt/core/job/job_events.h`, `qt/core/job/job_events.cpp`, `qt/core/job/default_download_service.h`, `qt/core/job/default_download_service.cpp`
- Create: `qt/tests/support/fake_output_sink.h`, `qt/tests/tst_default_download_service.cpp`
- Modify: `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/README.md`

**Interfaces:**
- Consumes: Task 1의 `ItemDownloader`, `output::OutputSink`, `fs::makeDirectories`
- Produces:

```cpp
namespace xgs::job {
struct DownloadRequest {
    QList<ResolvedItem> items;                 // already carry their final ranks
    output::OutputSink* sink = nullptr;        // referenced, not owned: must outlive the job
    bool overwrite = false;
    int concurrency = 1;                       // below 1 is 1; no upper limit of the service's own
    std::optional<QString> album;
    bool includeRank = true;
    bool searchLyricsOnline = false;
    std::optional<QString> albumOverride;
};
// A running download. One reader. Destroying it cancels the job and waits for its threads.
class JobHandle {
public:
    ~JobHandle();
    std::optional<JobEvent> nextEvent();       // blocks; nothing once JobDone has been taken
    std::optional<JobEvent> tryNextEvent();    // never blocks; nothing when no event is waiting
    bool finished() const;                     // JobDone has been queued (it may not have been taken yet)
    void cancel();                             // returns at once
    void join();                               // until JobDone has been queued and every thread of the job has ended
};
class DownloadService {
public:
    virtual ~DownloadService() = default;
    virtual std::unique_ptr<JobHandle> start(DownloadRequest request, std::stop_source stop = std::stop_source()) = 0;
};
class DefaultDownloadService final : public DownloadService {
public:
    // downloader is referenced and must outlive every job. tempRoot is a result of paths::fromText.
    DefaultDownloadService(ItemDownloader& downloader, QString tempRoot,
                           std::vector<std::chrono::milliseconds> retryDelays = {std::chrono::seconds(2), std::chrono::seconds(4)});
    std::unique_ptr<JobHandle> start(DownloadRequest request, std::stop_source stop = std::stop_source()) override;
    const std::vector<std::chrono::milliseconds>& retryDelays() const;
};
}
namespace xgs::testing {
// An OutputSink whose answers are functions; records every call. Safe from several threads.
class FakeOutputSink final : public output::OutputSink { /* exists(name) -> bool, put(name, source, overwrite) -> PutResult */ };
}
```

작업의 흐름은 Kotlin 그대로다: 멈춤이 이미 섰으면 폴더를 만들지 않는다 → `<tempRoot>` 만들기와 그 아래 `job-…` 폴더(이름은 겹치지 않게; `QTemporaryDir`) → 곡마다 `prepare`, `ItemStarted`, 덮어쓰기가 꺼져 있고 이름이 있으면 `ItemSkipped("이미 존재")`, `<job>\<순위>` 폴더, `download`를 재시도 규칙대로, `put`, `ItemDone` → 끝에 작업 폴더 지우기, `JobDone` 한 번, 큐 닫기. 작업 폴더를 못 만들면 곡 이벤트 없이 `JobDone(Failed, 0·0·0)`. 실패 종류별 처리(`Unavailable` 건너뜀, `Transient` 재시도 뒤 실패, `Fatal` 실패하고 작업 중단, `Other` 실패)와 문구는 Kotlin의 것이다. 작업 중단은 안쪽 멈춤 소스로 다른 일꾼을 끊는다(사용자의 취소와 구별해 상태를 정한다, "작성자가 정한 것 3").

- [ ] **Step 1: 시험의 뼈대를 쓴다.** Kotlin 시험의 도우미를 옮긴다: `item(rank, artist, track, lowConfidence)`(영상 ID는 `vid%08d`), `service(runner, tools, metadata, workDir, lyrics)`(재시도 대기는 1ms 둘), `request(...)`, `collect(handle)`(`nextEvent`를 끝까지), `succeeding`, `failingWith(lines...)`, `done(events)`, `awaitStarted(runner, count)`(10초 한도로 명령 수를 기다린 뒤 200ms 더 본다, Kotlin과 같다).

- [ ] **Step 2: Kotlin 시험 26개(원본 121~470행, `// ---- tagging ----` 앞까지)를 같은 이름으로 옮긴다.** 단언을 빼지 않는다. 옮기는 방식이 정해진 것:
  - 출력이 예외를 던지는 시험은 "작성자가 정한 것 5"의 표대로 `FakeOutputSink`의 결과로 바꾼다. `IOException("disk on fire")` → `{Failed, "disk on fire", 0}`, `AccessDeniedException("x")` → `{Failed, <아무 문구>, 5}`, `IOException("No space left on device")` → `{Failed, "No space left on device", 0}`, `FileAlreadyExistsException(경로)` → `{AlreadyExists, 경로, 0}`, `FileSystemException(파일, null, "No space left on device")` → `{Failed, "No space left on device", 0}`.
  - `awaitCancellation()`을 쓰는 가짜는 "멈춤이 요청될 때까지 기다린다"로 옮긴다(Task 1에서 더한, 멈춤 토큰을 받는 처리기 꼴을 쓴다; 실행기에 온 토큰이어야 치명적 실패가 끊는 것도 보인다).
  - `cancellingRightAfterStartStillEndsWithJobDoneCancelled`는 이미 멈춤이 요청된 소스로 시작한다("작성자가 정한 것 4").
  - `transientFailuresAreRetriedWithBackoff`·`givesUpAfterTwoRetries`는 1ms 대기로 돈다.

- [ ] **Step 3: 더한 시험을 쓴다** (Kotlin에 짝 없음; `test-classes.json`의 `added`에 적는다)

| 슬롯 | 보는 것 |
|---|---|
| `theDefaultRetryDelaysAreTwoAndFourSeconds` | `retryDelays()`가 2초·4초 |
| `cancellingDuringTheRetryDelayReturnsPromptly` | 대기 60초 둘, 첫 실패 뒤 취소: 2초 안에 `JobDone(Cancelled)`, yt-dlp 호출 1 |
| `cancellingWithItemsInEveryStageEndsPromptly` | Review Focus 1: 곡 6개·동시 수 5, 각각 조회·yt-dlp·가사·태그·대기에서 멈춰 있게 하고 하나는 차례를 기다린다 → 취소 뒤 5초 안에 끝나고, `JobDone(Cancelled)` 하나, `ItemDone`·`ItemFailed` 없음, `<tempRoot>` 아래 비어 있음 |
| `destroyingTheHandleOfARunningJobCancelsAndJoins` | 도는 중에 핸들을 없앤다: 소멸자가 5초 안에 돌아오고 가짜의 "멈춤을 봤다"가 참 |
| `aJobNobodyReadsStillFinishes` | 이벤트를 하나도 안 읽고 `join()`: 돌아오고, 그 뒤 `nextEvent`로 전부 읽힌다 |
| `nextEventGivesNothingAfterJobDone` | `JobDone` 뒤 `nextEvent`·`tryNextEvent`가 빈 값이고 막히지 않는다 |
| `fiveThousandItemsUseNoMoreThreadsThanTheConcurrency` | Review Focus 3: `exists`가 늘 참인 출력, 곡 5,000개·동시 수 64 → `exists`를 부른 서로 다른 스레드 ID가 64개 이하, 곡마다 `ItemStarted` 다음 `ItemSkipped`, 요약 0·5000·0, 30초 안 |
| `aConcurrencyLargerThanTheItemCountIsFine` | 곡 3개·동시 수 100000: `Completed` 3·0·0 |
| `itemsStartInListOrder` | 동시 수 1, 곡 20개: `ItemStarted`의 순위가 목록 순서 |
| `aFatalFailureStopsTheOtherRunningItemsWithoutFailingThem` | Review Focus 4: 동시 수 3, 1번 곡은 디스크 가득 stderr로 끝나고 2·3번은 멈춤을 기다린다 → `ItemFailed`는 1번 하나, 요약 0·0·1, `JobDone(Failed)` 하나, 2·3번의 가짜가 멈춤을 봤다 |
| `aUserCancelAfterAFatalFailureIsStillFailed` | 상태 규칙("작성자가 정한 것 3") |
| `anExceptionOnAWorkerThreadFailsOnlyThatItem` | Review Focus 6: 1번 곡에서 메타데이터 조회가 `std::runtime_error("boom")`, 2번에서 출력의 `exists`가 `std::invalid_argument`, 3번에서 실행기가 `int`를 던진다, 4번은 정상 → `ItemFailed` 셋(문구 `boom`, 그 `what()`, `알 수 없는 오류`), 요약 1·0·3, `Completed` |
| `aProgramThatCannotBeStartedFailsTheItem` | `StartFailure{"boom"}` → `ItemFailed("파일 처리 중 오류: boom")`, 다음 곡은 계속 |
| `aStartFailureThatSaysTheDiskIsFullAbortsTheJob` | `StartFailure{"There is not enough space on the disk."}` → `디스크 공간이 부족합니다.`, `Failed` |
| `aDiskFullCodeFromTheSinkAbortsTheJob` | 표 5의 코드 줄: `{Failed, "x", 112}`와 `{Failed, "x", 39}`가 각각 `디스크 공간이 부족합니다.`와 중단 |
| `aSharingViolationFromTheSinkFailsOnlyThatItem` | `{Failed, "x", 32}`는 `파일 처리 중 오류: x`이고 다음 곡은 계속 |
| `anItemFolderThatCannotBeMadeIsReadLikeASinkFailure` | `<job>\<순위>` 자리에 파일을 미리 둔다(일꾼이 폴더를 만들기 직전에 `exists` 가짜가 만든다) → 그 곡은 실패, 문구는 `파일 처리 중 오류: `로 시작 |
| `anAlreadyExistsThatCannotBeASkipFailsWithTheFileName` | 덮어쓰기 켬 + `{AlreadyExists, "", 0}` → `파일 처리 중 오류: <파일 이름>` |
| `aStopBeforeThePutDeliversNothing` | 태그 가짜가 멈춤을 요청하고 성공으로 끝난다 → `put` 호출 0, `ItemDone` 없음, `Cancelled`("작성자가 정한 것 10") |
| `theWorkFolderIsRemovedEvenWhenAFileInItIsHeldForAMoment` | 작업 폴더 안 파일을 다른 스레드가 300ms 쥐고 있다가 놓는다 → 작업이 끝난 뒤 `<tempRoot>` 아래 비어 있음("작성자가 정한 것 12") |
| `aPercentThatIsNotFiniteReachesTheReaderAsUnknown` | 이벤트 큐까지 "모름"으로 온다 |

- [ ] **Step 4: 돌려서 실패를 본다.** 컴파일 실패(`default_download_service.h` 없음).

- [ ] **Step 5: `JobEvents`, `JobHandle`, `DefaultDownloadService`를 구현한다.** 큐는 `std::mutex` + `std::condition_variable`이고 `push`는 닫힌 뒤에는 버린다. 재시도 대기는 `std::condition_variable_any::wait_for`에 멈춤 토큰을 준다. 일꾼의 몸통 전체를 `try`로 싼다. 치명적 실패는 처음 것 하나만 기록한다.

- [ ] **Step 6: 돌려서 통과를 본다.** `ctest --test-dir build/qt -R "tst_default_download_service" --output-on-failure`, 디버그도. 이어서 그 실행 파일을 두 빌드에서 20번씩 돌려 모두 통과하는지 본다(스레드 시험이다; 출력은 `build/qt*/qttest/`에 남는다).

- [ ] **Step 7: 변이.** (a) 일꾼 수를 곡 수로 둔다, (b) 치명적 실패가 다른 일꾼을 끊지 않게 한다, (c) `JobDone`을 닫기 뒤에 넣는다, (d) 재시도 대기를 `std::this_thread::sleep_for`로 바꾼다, (e) `AlreadyExists`에도 디스크 가득 검사를 한다. 각각 잡는 슬롯을 적고 되돌린다.

- [ ] **Step 8: 커밋.** `git commit -m "Qt: 내려받기 작업 서비스 이식"` (이름 대조는 Task 3에서 통과한다: 이 Task 뒤에는 103개 중 26개만 있다.)

### Task 3: 나머지 Kotlin 시험 77개

**Files:**
- Modify: `qt/tests/tst_default_download_service.cpp`, `qt/tests/support/job_fakes.h/.cpp`(필요한 도우미), `qt/tools/test-classes.json`
- Modify(시험이 C++ 버그를 찾았을 때만): `qt/core/job/*`

**Interfaces:**
- Consumes: Task 1·2의 것 전부, `testing::FakeLyricsProvider`, `testsupport::kotlinLines`, ffmetadata 읽개(`qt/tests/support/ffmetadata_reader.h`)
- Produces: 없음

- [ ] **Step 1: 원본 475행부터 끝까지의 `@Test` 77개를 같은 이름으로 옮긴다.** 구역은 태그 쓰기, 앨범(재생목록 제목·영상의 앨범·사용자가 적은 이름), 설명란 가사, 온라인 가사 조회와 질의 내용, 가사 결과(`LyricsOutcome`) 보고, 순위 없는 파일 이름, 같은 이름의 두 곡이다. 도우미(`taggingRunner`, `infoRunner`, `albumLines`, `infoWithDescription`, `describedLyrics`, `deliveredTag`, `onlineLyrics`, `descriptionWithoutLyrics`, `deliveredWith`, `eventsWith`, `itemDone`, `infoRunnerWith`, `DownloaderCase`, `filesIn`, `firstTwin`·`secondTwin`, `heldRunner`)도 같은 이름으로 옮긴다. 가사 글은 Kotlin 시험의 지어낸 글 그대로다. 옮기는 방식이 정해진 것:
  - `aCancellationExceptionFromTheProviderIsNotSwallowed`: 공급자가 시험의 멈춤 소스에 멈춤을 요청하고 빈 값을 돌려준다. 단언(질의 1번, `ItemDone`·`ItemFailed` 없음, 출력에 파일 없음)은 그대로다.
  - `cancellingTheJobWhileTheLookupRunsCancelsTheLookupAndReportsCancelled`: 공급자는 래치를 세우고 토큰이 설 때까지 기다린 뒤 `cancelledInside`를 세운다.
  - `aProviderThatThrowsGivesNoLyricsAndASuccessfulItem` 등 던지는 공급자는 `std::runtime_error`를 던진다.
  - 동시에 받는 두 곡 시험은 `awaitStarted(held, 2)` 뒤에 문을 연다(Kotlin과 같다).

- [ ] **Step 2: 돌려서 통과를 본다.** 실패하는 슬롯이 있으면 C++을 고친다(시험의 기대값은 Kotlin의 것이다). 고친 것은 보고에 적는다.

- [ ] **Step 3: 이름 대조.** `qt/tools/test-classes.json`에 `{kotlin: "engine/src/test/kotlin/com/xgetsongs/engine/job/DefaultDownloadServiceTest.kt", exe: "tst_default_download_service", added: [Task 2의 더한 슬롯들]}`를 넣고 `pwsh qt/tools/test-names.ps1` → 빠진 이름 0.

- [ ] **Step 4: 두 빌드에서 그 실행 파일을 20번씩 돈다.** 모두 통과.

- [ ] **Step 5: 변이.** (a) 앨범 우선순위에서 사용자가 적은 이름과 재생목록 제목을 바꾼다, (b) 가사 질의의 앨범에 사용자가 적은 이름을 쓴다, (c) `SearchOff`와 `NotFound`를 바꾼다, (d) `put`의 `AlreadyExists`를 늘 실패로 한다. 각각 잡는 슬롯을 적고 되돌린다.

- [ ] **Step 6: 커밋.** `git commit -m "Qt: 내려받기 작업의 Kotlin 시험 103개 이식"`

### Task 4: 작업을 Kotlin과 대조 (`W jb`)

**Files:**
- Modify: `qt/tools/differential/gen.h`, `gen.cpp`, `main.cpp`, `Oracle.java`, `run.ps1`, `qt/README.md`
- Modify(대조가 C++ 버그를 찾았을 때만): `qt/core/job/*`와 그 시험

**Interfaces:**
- Consumes: Task 1~3, 기존 생성기(제목·가수·정보 파일 JSON·설명란 가사·yt-dlp stderr·진행률 줄)
- Produces: `run.ps1 -Kinds W`가 `jb` 사례를 만들고 비교한다(위 "대조 op `W jb`" 표의 사례·답·문턱)

- [ ] **Step 1: 사례와 답의 줄 형식을 정해 `gen.h`와 `qt/README.md`에 적는다.** 글은 전부 hex. 대본은 호출 차례대로 소비하고, 대본이 모자라면 마지막 항목을 되풀이한다(양쪽 같은 규칙).

- [ ] **Step 2: 생성기(`gen.cpp`).** 문턱을 채우도록 섞는다: 같은 가수·곡명의 두 곡(순위 넣기 끔), `lowConfidence`와 메타데이터 답(없음·제목만·가수와 곡명), 일시적 실패 한두 번 뒤 성공 또는 세 번 실패, 받을 수 없는 영상, 치명적 stderr, m4a를 안 쓰는 성공, 깨진·없는 정보 파일, 설명란 가사 있음·없음·세 줄 미만, 공급자의 답(가사·빈 글·없음), 출력의 대본(이미 있음, `put` 결과 여섯 가지), 진행률 줄(같은 정수 백분율 되풀이, 모름, NaN, 100 넘음, 진행률이 아닌 줄).

- [ ] **Step 3: Kotlin 쪽(`Oracle.java`).** 진짜 `DefaultDownloadService`와 `ItemDownloader`를 가짜들과 함께 만들고(`ProcessRunner`·`ToolPathProvider`·`VideoMetadataSource`·`LyricsProvider`·`OutputSink`는 인터페이스다; `suspend` 함수와 `Duration` 인자는 Q2c의 `lf`처럼 반사로 다룬다), 이벤트를 끝까지 받아 답 줄을 쓴다. 가짜 실행기는 C++ 가짜와 같은 일을 한다(작업 폴더에 파일 쓰기, 줄 내보내기, 종료 코드). **JDK·코루틴 소스를 보지 않는다.**

- [ ] **Step 4: C++ 쪽(`main.cpp`)과 `run.ps1`의 통계.** 문턱은 표의 값 그대로다. 알려진 차이 둘(유한하지 않은 백분율, `AlreadyExists` 실패 문구의 꼬리)만 표의 설명대로 맞춘다.

- [ ] **Step 5: 돈다.** `pwsh qt/tools/differential/run.ps1 -Kinds W -PerKind 10000 -Seed 20261115`, seed 1, 987654321, `-DebugTool` 한 번, `-PerKind 100000 -Seed 424242` → 불일치 0, 문턱 전부 통과. 불일치가 있으면 C++을 고치고(Kotlin이 정답), 고친 것마다 그 입력을 `tst_default_download_service`나 `tst_item_downloader`의 슬롯으로 남긴다. 문턱이 안 채워지면 생성기를 고친다(문턱을 내리지 않는다).

- [ ] **Step 6: 변이.** (a) `Unavailable`을 실패로 센다, (b) 재시도 횟수를 하나 줄인다, (c) 솎아내기에서 첫 이벤트를 빼먹는다, (d) `Finishing`을 두 번 낸다, (e) 태그의 앨범에서 영상의 앨범을 재생목록 제목보다 앞세운다. 각각 불일치 수를 적고 되돌린다. 0건인 변이가 있으면 생성기가 그 길을 못 가는 것이므로 생성기를 고친다.

- [ ] **Step 7: 커밋.** `git commit -m "Qt: 내려받기 작업을 Kotlin과 대조"`

### Task 5: 실제 실행기로 잇기와 미룬 통합 시험

**Files:**
- Modify: `qt/tests/helpers/test_child.cpp`(모드 추가), `qt/tests/CMakeLists.txt`, `qt/tests/integration/CMakeLists.txt`, `qt/README.md`, `qt/tools/test-classes.json`(통합 시험의 Kotlin 짝)
- Create: `qt/tests/tst_job_with_real_processes.cpp`, `qt/tests/integration/tst_real_download.cpp`

**Interfaces:**
- Consumes: `process::SystemProcessRunner`, `output::LocalFolderSink`, Task 1~2, `tests/integration/integration_support.h`·`ffprobe.h`, `lyrics::LrclibLyricsProvider`, `net::QtHttpClient`, `ytdlp::YtDlpResolver`, `tools::ToolLocator`
- Produces: `xgs-test-child`의 새 모드
  - `fakeytdlp <ms> [args…]`: 인자에서 `-o` 값을 찾아 `<폴더>\<id>.m4a`를 쓰고(틀의 `%%`는 `%`로 읽는다), 진행률 줄 둘을 내고, `<ms>` 동안 자고, 0으로 끝난다
  - `fakeytdlpspawn <pid 파일> <ms> [args…]`: 같은 일을 하되 자기 자신을 `sleep`으로 하나 띄워 그 PID를 적는다(ffmpeg 자손 노릇)
  - `fakeffmpeg [args…]`: 마지막 인자의 경로에 파일을 쓰고 0으로 끝난다

- [ ] **Step 1: 실제 실행기 시험을 쓴다** (`tst_job_with_real_processes.cpp`; 네트워크 없음, 실제 yt-dlp·ffmpeg 없음, 창 없음. 실행기는 `.cmd`를 거절하므로 배치 파일로 도구를 흉내 내지 않는다: 시험용 `ProcessRunner` 감싸개가 명령의 첫 인자를 `xgs-test-child`와 모드로 바꿔 진짜 `SystemProcessRunner`에 넘긴다.)

| 슬롯 | 보는 것 |
|---|---|
| `aWholeJobRunsThroughTheRealRunnerAndTheRealFolderSink` | 곡 3개·동시 수 2 → 출력 폴더에 파일 셋, `Completed` 3·0·0, `<tempRoot>` 아래 비어 있음 |
| `cancellingKillsTheProcessTreesAndTheJobEndsPromptly` | `fakeytdlpspawn`(60초)으로 곡 2개·동시 수 2, 둘 다 `started` 뒤 취소 → 5초 안에 `JobDone(Cancelled)`, PID 파일의 자손 프로세스가 둘 다 없어졌다(`OpenProcess` + `WaitForSingleObject`), `<tempRoot>` 아래 비어 있음 |
| `aWorkFolderAndAnOutputFolderWithPercentSpacesAndHangulWork` | Review Focus 5: `tempRoot`와 출력 폴더 이름이 `100% %(id)s 한글 폴더` → 파일이 그 출력 폴더에 있고 다른 곳에 생긴 폴더가 없다 |
| `twoItemsWithTheSameNameAtTheSameTimeLeaveOneFile` | 진짜 `LocalFolderSink`로 같은 이름 두 곡·동시 수 2: 덮어쓰기 끔은 1·1·0, 켬은 2·0·0, 파일 하나(Q2b 인계 "겹치는 `put`") |
| `aSameVideoTwiceInOneJobDoesNotCollide` | 같은 영상 ID의 곡 둘(순위 다름)·동시 수 2: 둘 다 끝난다(곡마다 작업 폴더, Q2b 인계) |

- [ ] **Step 2: 실패를 보고, `test_child`의 모드를 구현하고, 통과를 본다.** 두 빌드에서 20번씩.

- [ ] **Step 3: 미룬 통합 시험을 쓴다** (`tst_real_download.cpp`). 둘 다 `XGS_INTEGRATION=1`과 `XGS_INTEGRATION_AUDIO=1`이 없으면 `QSKIP`.
  - `downloadsAShortVideoAsM4aWithoutReencoding`: Kotlin `RealYtDlpIntegrationTest.kt`의 같은 이름 시험의 단언 전부(파일 이름 꼴, 5만 바이트 초과, 단계 순서, ffprobe의 태그·스트림·표지). 가사 태그는 개수만 찍는다.
  - `cancellingARealDownloadLeavesNoToolProcessBehind`(Kotlin에 짝 없음): 같은 영상을 받기 시작해 첫 진행률 이벤트 뒤 취소 → `JobDone(Cancelled)`, 그 작업이 띄운 yt-dlp·ffmpeg 프로세스가 남지 않고(작업 시작 뒤에 생긴, 우리 작업 폴더를 명령줄에 가진 프로세스가 0), 작업 폴더가 지워졌다.
  도구는 `%APPDATA%\xGetSongs`의 것을 실행만 한다.

- [ ] **Step 4: 건너뛰는 것을 확인한다.** 변수 없이 `tst_real_download`를 돌려 슬롯 둘이 건너뛰는지 본다. **변수를 켜서 돌리지 않는다**("작성자가 정한 것 15").

- [ ] **Step 5: 커밋.** `git commit -m "Qt: 실제 실행기로 작업 잇기와 내려받기 통합 시험"`

### Task 6: 관문과 보고서

**Files:**
- Create: `docs/superpowers/specs/2026-11-21-qt-engine-job-report.md`
- Modify: `qt/README.md`(관문 문단)

- [ ] **Step 1: 깨끗한 빌드 둘.** `build/qt`와 `build/qt-debug`의 산출물만 지우고(`vcpkg_installed`는 남긴다) 다시 빌드, 경고 0.
- [ ] **Step 2: `ctest` 둘.** 릴리스·디버그 모두 전부 통과. `--rerun-failed`를 쓰지 않는다.
- [ ] **Step 3: 되풀이 실행.** `tst_default_download_service`, `tst_item_downloader`, `tst_job_with_real_processes`, `tst_system_process_runner`(Q2b의 190초 실패가 아직 열려 있다), `tst_local_folder_sink`를 두 빌드에서 20번씩. 출력은 남긴다.
- [ ] **Step 4: 스크립트.** `pwsh qt/tools/test-names.ps1`(빠진 이름 0), `pwsh qt/tools/forbidden-calls.ps1`(0건).
- [ ] **Step 5: 전체 대조.** 모든 종류, 1만 건 × seed 셋, `-DebugTool` 한 번, 10만 건 한 번 → 불일치 0, 문턱 전부 통과.
- [ ] **Step 6: 통합 시험.** `XGS_INTEGRATION=1`로 `tst_real_ffmpeg_tagging`과 `tst_real_ytdlp`를 한 번씩(오디오 없음; Q2c의 것 그대로 통과하는지). `tst_real_lrclib`는 돌리지 않는다(승인분을 다 썼다). `tst_real_download`는 **사용자가 계획 승인 때 허락한 경우에만** `XGS_INTEGRATION_AUDIO=1`로 한 번 돌리고, 아니면 건너뛴 것을 적는다.
- [ ] **Step 7: 보고서.** 앞 보고서들의 꼴(요약, 관문 표와 원본 파일 이름, 대조가 찾은 C++ 버그, 변이 표, Kotlin과 다르게 한 것, 고치지 않은 Kotlin 쪽 동작, 계획과 달라진 것, 확인하지 못한 것, 앞 단계 인계가 어떻게 됐는지 항목별로, Q3·Q4에 넘길 것, 사용자가 정할 것, 판정). 숫자는 전부 원본 출력에서 옮긴다. Q4에 넘길 것에는 적어도 다음을 적는다: 이벤트를 화면 스레드로 넘기는 다리, 설치·업데이트와 내려받기를 겹치지 않게 막기, 임시 폴더의 위치(출력 폴더와 같은 드라이브인지), 앱이 끝날 때 핸들·서비스·클라이언트를 없애는 순서, 멈춘 뒤의 도구 상태는 버리기.
- [ ] **Step 8: 커밋.** `git commit -m "문서: Qt 전환 Q2d(작업·동시성·취소) 보고서"`

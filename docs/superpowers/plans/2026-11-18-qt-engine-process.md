# Qt 전환 Q2b: 프로세스 실행, 도구, yt-dlp 조회, 태그 쓰기, 출력 폴더 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Kotlin `engine`에서 외부 프로그램을 실행하고 파일을 다루는 부분(프로세스 실행과 자손 정리, 도구 찾기와 버전 보고·yt-dlp 설치·업데이트, yt-dlp로 재생목록·영상 조회, ffmpeg로 m4a 태그 쓰기, 출력 폴더에 넣기)을 C++/Qt Core로 옮기고, 지금 단위 테스트 60개를 같은 이름으로 통과시키며, 실제 ffmpeg·yt-dlp 통합 테스트 13개를 통과시킨다.

**Architecture:** Kotlin의 `suspend` 함수는 **멈춤 토큰(`std::stop_token`)을 받는 블로킹 함수**가 된다(작업 스레드에서 부른다; 스레드 풀과 동시성은 Q2d). 프로세스 실행은 `ProcessRunner` 인터페이스 뒤에 두고, 실제 구현은 Win32 `CreateProcessW`로 자식을 처음부터 Job Object 안에서 띄운다. 로직(조회 결과 해석, 버전 판정, 줄 나누기, 경로 문자열)은 가짜 실행기를 양쪽에 끼워 Kotlin과 무작위 대조한다. 예외 대신 결과 값(`Outcome<T>`)을 돌려준다.

**Tech Stack:** C++20, Qt 6.11.3 Core·Test 정적 링크, Win32 API(프로세스, Job Object, 파일 이동), MSVC, CMake + Ninja, PowerShell 7, 대조용 JVM은 `server/build/sidecar-image`.

**Spec:** [설계 문서](../specs/2026-11-15-qt-rewrite-and-playlists-design.md) §3(자손 프로세스 정리, 호환), §4(Q2 행). 앞 단계: [Q2a 보고서](../specs/2026-11-15-qt-engine-logic-report.md) "Q2b에 넘길 것", [Q0 보고서](../specs/2026-11-15-qt-spike-report.md) "이어질 단계에서 조심할 것".

## 이 계획에서 작성자가 정한 것 (사용자가 바꿀 수 있다)

1. **`QProcess`를 쓰지 않고 `CreateProcessW`를 직접 부른다.** 설계 §3은 "`QProcess` + Win32 Job Object"라고 적었지만, Q0 스파이크가 그 조합은 Qt 내부의 문서에 없는 동작(일시 정지한 자식의 핸들 수명)에 기댄다는 것을 찾았고 대안으로 이 방법을 권했다. 직접 부르면 자식이 처음부터 Job 안에서 태어나고, 물려줄 핸들을 정확히 고를 수 있고(동시 실행에서 파이프가 새지 않는다), 명령줄 인용을 우리가 정한다.
2. **블로킹 함수 + 멈춤 토큰.** 코루틴을 흉내 내지 않는다. 취소는 호출한 쪽이 토큰에 멈춤을 요청하면 실행 중인 프로세스 트리를 죽이고 "취소됨"을 돌려주는 것이다.
3. **yt-dlp 내려받기(HTTP)는 주입한다.** `installYtDlp`는 내려받는 함수를 인자로 받고, 실제 HTTP 구현은 네트워크 단계인 Q2c에서 만든다. 이 계획의 코드는 스스로 네트워크 요청을 보내지 않는다(통합 테스트의 실제 yt-dlp만 유튜브에 요청한다).
4. **운영체제 오류 문구는 맞추지 않는다.** Kotlin이 `${e.message}`로 붙이는 JDK 예외 문구(예: 파일을 쓰지 못한 이유)는 Windows의 오류 문구로 바뀐다. 그 앞의 한국어 문구는 그대로다.
5. **주 프로세스가 끝난 뒤 남은 자손은 죽인다.** Kotlin은 취소 때만 자손을 죽인다. Qt 판은 정상 종료 뒤에도 Job을 닫으면서 남은 자손을 정리한다(출력 파이프가 닫힐 때까지 기다리는 것은 Kotlin과 같다).

## Global Constraints

- 정답은 Kotlin이다. `shared/`, `engine/`, `server/`, `diagnostics/`, `shell/`, `sidecar-host/`는 읽기만 한다. C++ 결과가 Kotlin과 다르면 C++을 고친다. Kotlin 쪽이 버그로 보여도 고치지 않고 보고서에 적는다. 예외는 위 "작성자가 정한 것"과 Q2a 보고서의 "Kotlin과 다르게 한 것"뿐이다.
- **JDK(OpenJDK)와 `kotlinx.serialization`의 소스를 보지 않고, 기억에서 옮겨 쓰지 않는다.** `java.nio.file.Path`, `ProcessBuilder`, `BufferedReader`, 형 있는 JSON 디코딩의 동작은 공개 문서와 대조 도구가 관찰한 답으로만 정한다. Windows 명령줄 인용은 Microsoft 문서의 규칙(`CommandLineToArgvW`와 C 런타임의 인자 나누기)에서 만든다.
- 바꾸는 곳은 `qt/`(단 `qt/spike/` 제외), `THIRD-PARTY-NOTICES.md`(필요할 때만), Task 8의 보고서뿐이다.
- `xgscore`는 Qt Core와 Win32 API만 쓴다. 네임스페이스 `xgs`, C++20, `/W4 /WX /permissive- /utf-8`. Q2a의 금지 호출 목록은 그대로다(`qt/tools/forbidden-calls.ps1`). 경로 문자열은 `xgs::paths`(Task 1)로만 만들고 `QDir`·`QFileInfo`의 경로 정리 함수는 쓰지 않는다(`QFileInfo::isFile`/`exists`, `QFile`, `QDir::mkpath`, `QTemporaryDir`는 써도 된다).
- 프로세스는 **셸을 거치지 않는다.** 실행 파일 경로가 `.cmd`·`.bat`으로 끝나면 실행하지 않고 실패를 돌려준다(Windows가 `cmd.exe`를 거쳐 `%`·`&`·`^`를 해석한다).
- 자식 프로세스는 콘솔 창을 띄우지 않는다(`CREATE_NO_WINDOW`). 테스트도 창을 띄우지 않는다.
- 테스트는 Qt Test. Kotlin의 `@Test` 하나가 같은 이름의 슬롯 하나가 되고 단언을 하나도 빼지 않는다(JDK 예외 문구를 비교하는 단언은 한국어 부분만 비교하고 그 사실을 슬롯에 주석으로 남긴다). 기대값·통계 문턱·생성기를 바꿔 통과시키지 않는다.
- 대조는 종류마다 1만 건 × seed 셋과 10만 건 한 번을 돈다(`-DebugTool` 한 번 포함). 이식 Task마다 변이가 대조에 잡히는지 본다.
- 관문은 릴리스와 디버그 두 빌드에서 `ctest`를 돈다.
- **실제 도구를 쓰는 통합 테스트**는 환경 변수 `XGS_INTEGRATION=1`일 때만 돌고 아니면 건너뛴다(`QSKIP`). 도구는 Kotlin 통합 테스트와 같은 곳에서 찾는다. `%APPDATA%\xGetSongs`에서는 **읽기만** 하고(거기 있는 `yt-dlp.exe`를 실행하는 것까지) 아무것도 쓰거나 지우지 않는다. 실행 중인 xGetSongs를 건드리지 않는다. Gradle은 돌리지 않는다.
- 임시 파일은 `QTemporaryDir` 아래에만 만든다.
- 커밋은 `main`에 직접, 메시지는 한국어, 끝에 실행하는 세션의 Co-Authored-By 줄. push하지 않는다.

## Review Focus

테스트 60개가 다루지 않지만 실제로 만날 것이다.

1. **동시에 여러 프로세스를 띄울 때 파이프가 샌다.** 핸들을 통째로 물려주면 한 자식이 다른 자식의 파이프 끝을 쥐어서, 먼저 끝난 프로세스의 출력 읽기가 영영 끝나지 않는다(Q2d는 여러 곡을 동시에 받는다) → Task 2의 `parallelRunsDoNotHoldEachOthersPipes`.
2. **취소했는데 자손이 남는다.** yt-dlp는 ffmpeg를 자식으로 띄운다 → Task 2의 `cancellingKillsGrandchildrenToo`와, Job 없이 같은 일을 하면 자손이 살아남는다는 대조 실행(`withoutAJobTheGrandchildSurvives`).
3. **공백·따옴표·끝의 역슬래시·`%`·`&`·한글이 든 인자**가 자식에 글자 그대로 닿아야 한다(출력 틀 `%(ext)s`, 사용자가 고른 폴더) → Task 2의 `argumentsReachTheChildVerbatim`(무작위 2,000개 목록).
4. **yt-dlp의 출력이 깨끗하지 않다.** 잘못된 UTF-8 바이트, `\r`로만 끝나는 줄, 아주 긴 줄(재생목록 JSON 한 줄 수 MB), 줄 끝 없이 끝나는 출력 → Task 2의 줄 나누기 대조(`W ln`)와 `aLineOfTenMegabytesArrivesWhole`.
5. **조회 응답 JSON의 모양이 기대와 다르다.** 숫자로 온 제목, `null` 항목, `NaN`이 든 모르는 키, 항목 5,000개 → Task 5의 대조 `P`.
6. **긴 경로와 다른 드라이브.** 출력 폴더 경로가 260자를 넘거나 작업 폴더(임시 폴더)와 출력 폴더의 드라이브가 다르다 → Task 6의 `putWorksWhenTheTargetPathIsLongerThan260` (다른 드라이브는 이 PC에 드라이브가 하나면 확인하지 못한 것으로 보고서에 적는다).

---

## File Structure

| 파일 | 하는 일 |
|---|---|
| `qt/core/outcome.h` | `xgs::Outcome<T>`: 성공 값 / 실패 문구 / 취소 |
| `qt/core/paths/paths.h/.cpp` | 경로 문자열: 사용자 글을 Windows 경로로, 자식·형제·파일 이름 |
| `qt/core/fs/file_ops.h/.cpp` | 파일 이동(덮어쓰기 여부), 바이트·글 쓰기, 조용히 지우기 (긴 경로 포함) |
| `qt/core/process/process_runner.h` | `ProcessRunner` 인터페이스, `RunResult` |
| `qt/core/process/line_splitter.h/.cpp` | 바이트 흐름을 줄로 |
| `qt/core/process/command_line.h/.cpp` | 인자 목록을 Windows 명령줄 하나로 |
| `qt/core/process/system_process_runner.h/.cpp` | `CreateProcessW` + Job Object |
| `qt/core/tools/tool_locator.h/.cpp` | `ToolLocator.kt` |
| `qt/core/tools/tool_manager.h/.cpp` | `DefaultToolManager.kt` |
| `qt/core/ytdlp/ytdlp_resolver.h/.cpp` | `YtDlpResolver.kt` |
| `qt/core/tags/m4a_tagger.h/.cpp` | `M4aTagger.kt` |
| `qt/core/output/output_sink.h`, `local_folder_sink.h/.cpp` | `OutputSink.kt`, `LocalFolderSink.kt` |
| `qt/tests/support/` | 정적 라이브러리 `xgstestsupport`: `FakeProcessRunner`, 고정 도구 경로, 태그 파일 되읽기 |
| `qt/tests/helpers/test_child.cpp` | 실행 파일 `xgs-test-child`(Qt 없음): 테스트가 띄우는 자식 |
| `qt/tests/tst_*.cpp` | Kotlin 테스트 클래스마다 하나, 통합 테스트 둘 |
| `qt/tools/differential/*` | 종류 `P`(조회), `W`(경로·줄·도구 상태·파일 이름) |
| `docs/superpowers/specs/2026-11-18-qt-engine-process-report.md` | 결과 보고서 |

**대조 종류**

| 종류 | 사례 | 답 | 통계 문턱 (1만 건 기준) |
|---|---|---|---|
| `W pn` | `text` | `Path.of(text).toString()`의 hex 또는 `ERR` | `ERR` 10% 이상, 입력과 다른 답 20% 이상, 입력과 같은 답 20% 이상 |
| `W pc` | `dir name` | `Path.of(dir).resolve(name).toString()` 또는 `ERR` | 성공 60% 이상 |
| `W pf` `W px` | `path` | `fileName` / `nameWithoutExtension` (`-`는 없음) | 없음 5% 이상, 점이 둘 이상인 이름 10% 이상 |
| `W ln` | `바이트hex 조각크기…` | 줄마다 hex | `\r\n`·`\r`·`\n`이 든 것 각각 20% 이상, 잘못된 UTF-8 10% 이상, 줄 끝 없이 끝남 30% 이상, 조각 경계가 여러 바이트 글자나 `\r\n` 사이에 오는 것 10% 이상 |
| `W fn` | `name` | `0`/`1` (출력 폴더가 받는 파일 이름인가) | 각각 25% 이상 |
| `W st` | 도구 셋의 (있음, 종료 코드, 출력 줄) | `ToolsStatus` 덤프 | 도구마다 찾음·못 찾음 각각 20% 이상, 버전이 문턱 바로 위·아래인 js 런타임 10% 이상 |
| `W up` | (종료 코드, 출력 줄) | `OK 문구` / `ERR 문구` | 각각 30% 이상 |
| `P rs` | `input 종료코드 stdout바이트 stderr줄…` | `OK` + 응답 덤프 + 실행한 명령, 또는 `ERR 문구` | `OK` 40% 이상, `ERR` 20% 이상, 재생목록 30% 이상, 영상 20% 이상, 쓸 수 없는 항목이 든 것 10% 이상, `null` 항목 5% 이상, 형이 어긋난 필드 10% 이상, 표준 밖 JSON 5% 이상, 항목 수가 `MAX_RANK`를 넘는 것 1% 이상 |
| `P fx` | `videoId 종료코드 stdout바이트 stderr줄…` | 메타 덤프 또는 `NULL` | 각각 30% 이상 |

`W`와 `P`의 통계는 op별로 센다. 문턱을 못 넘으면 생성기를 고친다. 문턱을 낮추지 않는다.

---

### Task 1: 결과 값, 경로 문자열, 파일 연산

**Files:**
- Create: `qt/core/outcome.h`, `qt/core/paths/paths.h/.cpp`, `qt/core/fs/file_ops.h/.cpp`, `qt/tests/tst_paths.cpp`, `qt/tests/tst_file_ops.cpp`
- Modify: CMake 둘, `qt/tools/differential/*`, `qt/README.md`

**Interfaces:**
- Produces:
  ```cpp
  namespace xgs {
  enum class OutcomeStatus { Ok, Failed, Cancelled };
  template <class T> struct Outcome {
      OutcomeStatus status = OutcomeStatus::Failed;
      std::optional<T> value;        // Ok일 때만
      QString error;                 // Failed일 때만: 사용자에게 보일 문구
      static Outcome ok(T v); static Outcome failed(QString message); static Outcome cancelled();
      bool isOk() const;
  };
  }
  namespace xgs::paths {
  /// Kotlin의 `Path.of(text).toString()`. `Path.of`가 던지는 글이면 없음. 정리(`.`·`..` 제거)는 하지 않는다.
  std::optional<QString> fromText(const QString& text);
  /// `dir.resolve(name)`: name이 절대 경로면 name. dir은 fromText를 거친 것이어야 한다.
  std::optional<QString> child(const QString& dir, const QString& name);
  std::optional<QString> fileName(const QString& path);          // Path.fileName; 뿌리뿐이면 없음
  QString nameWithoutExtension(const QString& path);             // kotlin.io.path.nameWithoutExtension
  std::optional<QString> sibling(const QString& path, const QString& name);   // resolveSibling
  }
  namespace xgs::fs {
  enum class MoveStatus { Moved, TargetExists, Failed };
  struct MoveResult { MoveStatus status; QString error; };       // error는 Windows의 오류 문구
  /// 다른 드라이브면 복사 뒤 지운다. replace가 false이고 대상이 있으면 TargetExists.
  MoveResult moveFile(const QString& from, const QString& to, bool replace);
  Outcome<std::monostate> writeBytes(const QString& path, const QByteArray& bytes);
  /// BOM 없는 UTF-8. 짝 없는 서로게이트가 있으면 Kotlin `Files.writeString(…, UTF_8)`과 같은 결과(관찰로 정한다).
  Outcome<std::monostate> writeUtf8(const QString& path, const QString& text);
  bool isRegularFile(const QString& path);
  void removeQuietly(const QString& path);
  bool makeDirectories(const QString& path);
  }
  ```
- 260 단위를 넘는 경로는 Win32 호출에 `\\?\` 접두어를 붙여 넘긴다(`file_ops.cpp` 안에서만).

- [ ] **Step 1: 오라클에 `W pn`·`pc`·`pf`·`px`를 더하고 60개 이상을 관찰한다** — 드라이브 경로(`/`·`\`·겹친·섞인 구분자, 끝 구분자), 뿌리(`C:\`, `C:`), 상대 경로, `.`·`..`, UNC(`\\host\share`, 끝 구분자 유무, 불완전한 것), `\\?\`·`\\.\`, 끝의 공백·점, 금지 글자(`* ? " < > |`, 제어 문자, 가운데 `:`), 빈 글, 공백뿐, 따옴표로 싼 것, 아주 긴 것, 한글·이모지·짝 없는 서로게이트. 표는 보고서에 남긴다.
- [ ] **Step 2: 시험을 쓴다** (`tst_paths.cpp` 클래스 `PathsTest`: 관찰한 행을 `_data` 표로, 성공과 `ERR` 각각; `tst_file_ops.cpp` 클래스 `FileOpsTest`)

```cpp
// FileOpsTest
void moveReplacesAnExistingTargetWhenAsked();
void moveReportsAnExistingTargetWhenNotAskedToReplace();      // TargetExists, 원본과 대상 모두 그대로
void moveFailsWithAMessageWhenTheSourceIsMissing();           // Failed, error가 비어 있지 않음
void writeUtf8WritesNoByteOrderMark();                        // "한글\n" → EC 95 9C EA B8 80 0A
void writeUtf8HandlesALoneSurrogateLikeKotlin();              // Step 3에서 관찰한 대로
void pathsLongerThan260Work();                                // 300 단위 경로에 쓰고, 옮기고, 지운다
void removeQuietlyIgnoresAMissingFile();
```
- [ ] **Step 3: `Files.writeString(path, text, UTF_8)`에 짝 없는 서로게이트가 든 글을 주면 무엇을 하는지 오라클로 관찰한다**(예외인가, 무엇이 쓰이는가, 파일이 남는가). 결과가 `writeUtf8`의 계약이다.
- [ ] **Step 4: 실패를 본다 → 구현한다 → 통과를 본다.** `moveFile`은 `MoveFileExW`(`MOVEFILE_COPY_ALLOWED`, replace면 `MOVEFILE_REPLACE_EXISTING`)로.
- [ ] **Step 5: C++ 쪽 대조와 생성기.** 경로 생성기는 Step 1의 범주를 모두 내고 30%는 한 글자를 망가뜨린다.
- [ ] **Step 6: 통과와 변이** — `run.ps1 -Kinds W`(이 Task의 op) 1만 건 × seed 셋, 10만 건. 변이 6개 이상(끝 구분자를 남김, 겹친 구분자를 남김, `/`를 그대로 둠, 뿌리에서 자식을 이을 때 구분자 둘, 끝 점을 뗌, 확장자를 첫 점에서 자름)이 잡힌다.
- [ ] **Step 7: 커밋** — `Qt: 결과 값, 경로 문자열, 파일 연산`

### Task 2: 프로세스 실행

**Files:**
- Create: `qt/core/process/process_runner.h`, `line_splitter.h/.cpp`, `command_line.h/.cpp`, `system_process_runner.h/.cpp`, `qt/tests/helpers/test_child.cpp`, `qt/tests/tst_line_splitter.cpp`, `tst_command_line.cpp`, `tst_system_process_runner.cpp`
- Modify: CMake 둘, `test-classes.json`, `qt/tools/differential/*`
- 원본(읽기만): `engine/.../process/ProcessRunner.kt`, `SystemProcessRunnerTest.kt`(3개)

**Interfaces:**
- Produces:
  ```cpp
  namespace xgs::process {
  enum class RunStatus { Exited, Cancelled, StartFailed };
  struct RunResult { RunStatus status; int exitCode = -1; QString error; };   // error는 StartFailed일 때 Windows의 문구
  using LineHandler = std::function<void(const QString& line)>;
  class ProcessRunner {
  public:
      virtual ~ProcessRunner() = default;
      /// command[0]이 실행 파일. 셸을 거치지 않는다. 줄마다 처리기를 부른다(서로 다른 스레드일 수 있다).
      /// 돌아올 때는 두 처리기가 더 불리지 않는다. stop이 요청되면 프로세스 트리를 죽이고 Cancelled.
      virtual RunResult run(const QStringList& command, const LineHandler& onStdout,
                            const LineHandler& onStderr, std::stop_token stop) = 0;
  };
  class SystemProcessRunner final : public ProcessRunner { /* … */ };
  /// 바이트를 받는 대로 줄로 나눈다. 줄 끝은 \n, \r, \r\n. UTF-8로 풀고 잘못된 열은 Kotlin과 같게(관찰).
  class LineSplitter { public: explicit LineSplitter(LineHandler handler); void feed(QByteArrayView bytes); void finish(); };
  /// 인자 목록을 `CreateProcessW`의 명령줄 하나로. 받는 프로그램의 C 런타임이 다시 나누면 같은 목록이 된다.
  QString windowsCommandLine(const QStringList& arguments);
  }
  ```
- `xgs-test-child <mode> …`(Qt 없이 Win32와 C++만): `argv`(받은 인자를 UTF-16 hex로 한 줄씩 stdout에), `emit <hex> <hex>`(바이트를 stdout과 stderr에 그대로 쓰고 종료 코드 3), `sleep <ms>`(먼저 `started` 한 줄), `spawn <pid 파일> <ms>`(자신을 `sleep`으로 자식 삼아 띄우고 그 PID를 파일에 쓴 뒤 `started`를 찍고 기다린다), `env <이름>`, `stdin`(표준 입력을 끝까지 읽고 읽은 바이트 수를 찍는다).

- [ ] **Step 1: 오라클에 `W ln`을 더한다** — 바이트를 `ByteArrayInputStream`에 넣고 Kotlin `bufferedReader(Charsets.UTF_8).forEachLine`이 주는 줄들. (조각 크기는 C++ 쪽만 쓴다: 조각을 어떻게 나눠도 답이 같아야 한다.)
- [ ] **Step 2: 시험을 쓴다**

```cpp
// LineSplitterTest (C++만)
void splitsOnAllThreeLineEndings();                 // "a\nb\r\nc\rd" → a b c d
void aCarriageReturnAtAChunkEndFollowedByALineFeedIsOneEnding();
void aMultiByteCharacterSplitAcrossChunksIsDecodedWhole();
void theLastLineWithoutAnEndingIsDeliveredByFinish();
void anEmptyInputDeliversNothing();                 // 그리고 "\n" 하나는 빈 줄 하나
void aLineOfTenMegabytesArrivesWhole();
// CommandLineTest (C++만)
void quotesOnlyWhatNeedsQuoting();                  // a → a;  "a b" → "a b";  "" → ""
void doublesBackslashesBeforeAQuoteAndAtTheEndOfAQuotedArgument();
// SystemProcessRunnerTest (Kotlin 3개: 자식은 java 대신 xgs-test-child)
void capturesOutputLinesAndExitCode();
void decodesOutputAsUtf8();
void cancellingTheCallerKillsTheProcess();          // 취소에서 돌아오기까지 10초 미만
// 더하는 것 (test-classes.json의 added)
void argumentsReachTheChildVerbatim();              // 무작위 2,000개 목록: 공백 탭 " \ % & ^ ! ( ) 한글 이모지 빈 인자, 끝의 \ 1~3개
void closesStandardInput();                         // stdin 모드가 0바이트를 읽고 끝난다
void setsThePythonEncodingVariables();              // PYTHONIOENCODING=utf-8, PYTHONUTF8=1, 나머지 환경은 물려받음
void aMissingExecutableIsAStartFailure();           // StartFailed, error가 비어 있지 않음, 처리기는 불리지 않음
void aBatchFileIsNotExecuted();                     // .cmd·.bat(대소문자 무시) → StartFailed
void cancellingKillsGrandchildrenToo();             // spawn 모드: 취소 뒤 5초 안에 손자 PID가 없다
void withoutAJobTheGrandchildSurvives();            // 대조 실행: 같은 자식을 Job 없이 띄우고 자식만 죽이면 손자가 살아 있다(끝에 손자를 직접 죽인다)
void parallelRunsDoNotHoldEachOthersPipes();        // 긴 sleep 하나와 짧은 emit 여덟을 동시에: 짧은 것들이 2초 안에 모두 돌아온다
void aStopRequestedBeforeTheStartRunsNothing();
void opensNoConsoleWindow();                        // 자식 실행 전후로 이 프로세스 트리의 보이는 최상위 창 수가 같다
```
- [ ] **Step 3: 실패를 본다 → 구현한다**

`SystemProcessRunner::run`: 파이프 셋(표준 입력은 만들고 곧 쓰는 쪽을 닫는다), `STARTUPINFOEXW`에 `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`(자식 쪽 파이프 끝 셋만)와 `PROC_THREAD_ATTRIBUTE_JOB_LIST`(`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`를 건 새 Job), 플래그 `CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT`, 환경 블록은 지금 환경에 두 변수를 더한 것. 읽기 스레드 둘이 각자 `LineSplitter`에 먹인다. 호출 스레드는 프로세스 핸들과 멈춤 요청을 함께 기다리고, 멈춤이면 `TerminateJobObject` 뒤 읽기 스레드를 기다려 `Cancelled`. 정상 종료면 두 읽기가 끝(EOF)날 때까지 기다린 뒤 Job을 닫는다. 모든 핸들은 RAII로 닫는다. 종료 코드는 `GetExitCodeProcess`의 값 그대로.

- [ ] **Step 4: `ProcessBuilder`의 인용을 관찰한다(관문 아님)** — Step 2의 무작위 목록 2,000개를 오라클이 `ProcessBuilder`로 `xgs-test-child argv`에 넘겨, 자식이 받은 것이 준 것과 다른 목록을 센다. 다른 것이 있으면 예와 함께 보고서의 "Kotlin과 다르게 한 것"에 적는다(Qt 판은 글자 그대로 전한다).
- [ ] **Step 5: 통과와 변이** — 빌드, 두 빌드의 `ctest`, `run.ps1 -Kinds W`(op `ln`) 1만 × 3과 10만. 변이 6개 이상(`\r`을 줄 끝으로 안 봄, `\r\n`을 둘로 셈, 끝 줄을 버림, 조각 경계에서 글자를 깸, 잘못된 바이트를 지움, 빈 줄을 건너뜀)이 잡힌다. `SystemProcessRunner`의 변이 3개(핸들 목록 없이 통째 상속 → `parallelRuns…` 실패, Job 없이 → `cancellingKillsGrandchildrenToo` 실패, 끝 `\` 두 배 안 함 → `argumentsReach…` 실패)가 테스트에 잡힌다.
- [ ] **Step 6: 커밋** — `Qt: 프로세스 실행(Job Object)과 줄 나누기`

### Task 3: 테스트 지원과 도구 찾기

**Files:**
- Create: `qt/tests/support/fake_process_runner.h/.cpp`, `test_tools.h`, `qt/tests/support/CMakeLists.txt`, `qt/core/tools/tool_locator.h/.cpp`, `qt/tests/tst_tool_locator.cpp`
- Modify: CMake 둘, `test-classes.json`
- 원본(읽기만): `engine/.../tools/ToolLocator.kt`, `ToolLocatorTest.kt`(6개), `engine/src/test/.../testutil/Fakes.kt`

**Interfaces:**
- Consumes: `ToolPaths`(Q2a `models.h`), `xgs::paths`, `xgs::fs::isRegularFile`.
- Produces:
  ```cpp
  namespace xgs::tools {
  class ToolPathProvider { public: virtual ~ToolPathProvider() = default; virtual ToolPaths current() const = 0; };
  class ToolLocator final : public ToolPathProvider {
  public:
      /// pathEnv가 없으면 환경 변수 PATH. 부를 때마다 다시 찾는다.
      explicit ToolLocator(QString appBinDir, std::optional<QString> pathEnv = std::nullopt,
                           std::optional<QString> ffmpegOverride = std::nullopt);
      ToolPaths current() const override;
  };
  }
  namespace xgs::testsupport {      // Fakes.kt의 프로세스 부분
  using ProcessHandler = std::function<int(const QStringList& command, const process::LineHandler& out, const process::LineHandler& err)>;
  class FakeProcessRunner final : public process::ProcessRunner {   // 명령 기록, 동시 실행 최댓값; 처리기가 던지는 StartFailure는 StartFailed가 된다
  public: explicit FakeProcessRunner(ProcessHandler handler); QList<QStringList> commands() const; int maxActive() const; /* … */ };
  struct StartFailure { QString message; };
  ToolPaths testTools();            // C:\tools\yt-dlp.exe, ffmpeg.exe, node.exe
  class FixedTools final : public tools::ToolPathProvider { /* toolsOf(paths) */ };
  }
  ```
- `ToolLocator::current`: 폴더 목록은 `appBinDir` 다음에 PATH를 `;`로 나눈 것(공백뿐인 것 제외, 양끝의 `"`를 떼고 `paths::fromText`가 없음이면 건너뜀). 폴더마다 `<이름>.exe`, `<이름>` 순으로 `paths::child` 뒤 정규 파일인지 본다. `ffmpegOverride`는 정규 파일일 때만 쓴다. js 런타임은 `deno` 다음 `node`.

- [ ] **Step 1: 테스트 6개를 옮긴다** (클래스 `ToolLocatorTest`) → **Step 2: 실패를 본다 → 구현한다 → 통과를 본다** (`ctest`, `test-names.ps1`, `forbidden-calls.ps1`)
- [ ] **Step 3: 커밋** — `Qt: 테스트 지원과 도구 찾기 이식`

### Task 4: 도구 상태, yt-dlp 설치·업데이트

**Files:**
- Create: `qt/core/tools/tool_manager.h/.cpp`, `qt/tests/tst_tool_manager.cpp`
- Modify: CMake 둘, `test-classes.json`, `qt/tools/differential/*`
- 원본(읽기만): `engine/.../tools/DefaultToolManager.kt`, `DefaultToolManagerTest.kt`(15개)

**Interfaces:**
- Consumes: Task 1~3 전부, `ToolInfo`·`ToolsStatus`(Q2a).
- Produces:
  ```cpp
  namespace xgs::tools {
  using Fetch = std::function<Outcome<QByteArray>(const QString& url, std::stop_token stop)>;
  inline constexpr QStringView YtDlpUrl = u"https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe";
  class ToolManager final {
  public:
      ToolManager(const ToolPathProvider& tools, process::ProcessRunner& runner, QString binDir, Fetch fetch);
      ToolsStatus status(std::stop_token stop);
      Outcome<QString> installYtDlp(std::stop_token stop);    // 성공 문구 / 실패 문구(Kotlin ToolException의 문구) / 취소
      Outcome<QString> updateYtDlp(std::stop_token stop);
  };
  }
  ```
- 문구·상수(최소 크기 1,000,000바이트, deno 2.3.0, node 22.0.0, `yt-dlp.new.exe`로 받아 실행해 본 뒤 `yt-dlp.exe`로 바꿈)는 Kotlin 그대로. 버전 비교는 `kt::`의 숫자 판정으로(`Char::isDigit`은 유니코드 숫자다: 관찰로 맞춘다; `toIntOrNull`이 넘치면 0).

- [ ] **Step 1: 테스트 15개를 옮긴다** (클래스 `DefaultToolManagerTest`; Kotlin의 `fetch` 람다가 `IOException`을 던지는 것은 `Outcome::failed(문구)`를 돌려주는 것으로)
- [ ] **Step 2: 실패를 본다 → 구현한다**
- [ ] **Step 3: 대조 `W st`·`W up`** — 오라클과 C++ 모두 가짜 실행기에 사례의 (종료 코드, 출력 줄)을 넣어 `status()`/`updateYtDlp()`를 부른다. 생성기: 버전 줄은 실제 꼴(`2025.01.15`, `ffmpeg version 7.1-full_build-www.gyan.dev Copyright …`, `deno 2.3.1 (stable, release, x86_64-pc-windows-msvc)`, `v22.11.0`)과 그것을 망가뜨린 것(빈 줄이 앞에, 앞뒤 공백, 접두어 없음, 숫자 아닌 부분, 자리 수 부족·초과, 문턱 ±1, 전각·아랍 숫자, 아주 큰 수, 종료 코드 0 아님, 시작 실패), js 런타임 파일 이름 `deno.exe`·`node.exe`·`NODE.EXE`·`node`.
- [ ] **Step 4: 통과와 변이** — 15개 통과, `W st`·`W up` 0 불일치(1만 × 3, 10만). 변이 6개 이상(deno 문턱 2.3.1, 비교를 문자열로, 넘친 부분을 0 대신 실패로, ffmpeg 버전을 첫 공백 대신 끝까지, 첫 줄 대신 마지막 줄, 업데이트 실패 문구에 첫 줄)이 잡힌다.
- [ ] **Step 5: 커밋** — `Qt: 도구 상태와 yt-dlp 설치·업데이트 이식`

### Task 5: yt-dlp 조회

**Files:**
- Create: `qt/core/ytdlp/ytdlp_resolver.h/.cpp`, `qt/tests/tst_ytdlp_resolver.cpp`
- Modify: CMake 둘, `test-classes.json`, `qt/tools/differential/*`, 필요하면 `qt/core/json/json.h/.cpp`
- 원본(읽기만): `engine/.../ytdlp/YtDlpResolver.kt`, `YtDlpResolverTest.kt`(17개)

**Interfaces:**
- Consumes: `classifyInput`·`parseTitle`·`filename::format`(Q1), `ResolveResult`·`ResolvedItem`(Q2a), `resolvePlaylistCommand`·`resolveVideoCommand`·`classifyError`(Q2a), `xgs::json`, Task 2~3.
- Produces:
  ```cpp
  namespace xgs::ytdlp {
  struct VideoMeta { std::optional<QString> title, channel, artist, track; bool operator==(const VideoMeta&) const = default; };
  class VideoMetadataSource { public: virtual ~VideoMetadataSource() = default;
      virtual std::optional<VideoMeta> fetch(const QString& videoId, std::stop_token stop) = 0; };   // 못 가져오면(취소 포함) 없음
  class YtDlpResolver final : public VideoMetadataSource {
  public:
      YtDlpResolver(process::ProcessRunner& runner, const tools::ToolPathProvider& tools);
      Outcome<ResolveResult> resolve(const QString& input, std::stop_token stop);   // 실패 문구는 Kotlin ResolveException의 문구
      std::optional<VideoMeta> fetch(const QString& videoId, std::stop_token stop) override;
  };
  }
  ```
- 명령 조립이 던지는 `std::invalid_argument`는 여기서 잡아 실패로 바꾼다(밖으로 새지 않는다).

- [ ] **Step 1: 오라클에 `P rs`·`P fx`를 더하고 형 있는 디코딩을 관찰한다**

Kotlin은 `Json { ignoreUnknownKeys = true }`로 `RawPlaylist`/`RawEntry`에 **형을 맞춰** 읽는다. 이것은 Q2a의 `parseToJsonElement`와 받아들이는 범위가 다르다. 구현 전에 80개 이상을 넣어 답을 적는다(보고서에 표로): 문자열 자리에 수·불리언·`null`·객체·배열·따옴표 없는 글; `entries`가 없음·`null`·객체·`null`이 든 배열·배열 아닌 것이 든 배열; 모르는 키의 값이 `NaN`·중첩·깨진 것; 따옴표 없는 키; 중복 키; 최상위가 배열·원시값; 앞뒤 공백·줄바꿈·뒤의 쓰레기; 빈 출력; BOM; `\u0000`; 짝 없는 서로게이트 이스케이프; 여러 줄로 온 JSON(실행기가 줄마다 `\n`을 붙인다).
- [ ] **Step 2: 테스트 17개를 옮긴다** (클래스 `YtDlpResolverTest`; `assertFailsWith<ResolveException>`는 `status == Failed`와 문구 비교로) → **실패를 본다**
- [ ] **Step 3: 구현한다** — stdout은 줄마다 `\n`을 붙여 모은다. 형 있는 디코딩은 `xgs::json` 위에 쓰되, Step 1에서 `parseToJsonElement`보다 엄격한 점이 나오면 `xgs::json::parse`에 선택 인자(예: `ParseOptions{ .strict = true }`)를 더해 맞춘다(기본 동작과 종류 `J`·`V`의 답은 바뀌지 않아야 한다). `toItem`·`unavailableReason`·문구는 Kotlin 그대로.
- [ ] **Step 4: 생성기 `P`** — 입력은 Q1의 `C` 생성기(재생목록·영상·거절). stdout은 실제 꼴의 문서(항목 0~40개, 가끔 `MAX_RANK`+1~+5개; 필드마다 있음·없음·`null`·형 어긋남; 제목은 Q1의 `T` 생성기와 `[Private video]`·`[Deleted video]`; `availability` 다섯 값과 다른 값; `channel`/`uploader` 조합; 모르는 키 섞기)를 Q2a의 `J` 문서 생성기로 꾸미고 20%는 한 군데를 망가뜨린다. 종료 코드 0과 아닌 것, stderr는 Q2a의 `E` 생성기. 도구 없음(`ytDlp` 없음)도 섞는다.
- [ ] **Step 5: 통과와 변이** — 17개 통과, `P` 0 불일치(1만 × 3, 10만 × seed 둘). 변이 10개 이상(`uploader` 대체 빼기, `null` 항목을 건너뜀(순위가 당겨짐), 쓸 수 없는 항목에 파일 이름을 줌, `MAX_RANK` 자르기 ±1, `truncated`를 `>=`로, 종료 코드 무시, 형 어긋난 문자열을 받아 줌, 모르는 키에서 실패, 영상 조회에 `alsoVideoId`를 넣음, `fetch`가 실패를 던짐, `availability` 목록에서 하나 빼기)가 잡힌다.
- [ ] **Step 6: 커밋** — `Qt: yt-dlp 조회 이식`

### Task 6: 태그 쓰기와 출력 폴더

**Files:**
- Create: `qt/core/tags/m4a_tagger.h/.cpp`, `qt/core/output/output_sink.h`, `local_folder_sink.h/.cpp`, `qt/tests/support/ffmetadata_reader.h/.cpp`, `qt/tests/tst_m4a_tagger.cpp`, `tst_local_folder_sink.cpp`, `tst_ffmetadata_reader.cpp`
- Modify: CMake 둘, `test-classes.json`, `qt/tools/differential/*`
- 원본(읽기만): `engine/.../tags/M4aTagger.kt`, `engine/.../output/*.kt`, `M4aTaggerTest.kt`(12개), `LocalFolderSinkTest.kt`(6개), `testutil/FfmetadataReader.kt`와 그 테스트(1개)

**Interfaces:**
- Consumes: `renderFfmetadata`·`TrackTags`·`tagCommand`·`Failure`(Q2a), Task 1~3.
- Produces:
  ```cpp
  namespace xgs::tags {
  enum class TagStatus { Tagged, Failed, Cancelled };
  struct TagResult { TagStatus status; ytdlp::Failure failure; };     // failure는 Failed일 때만
  class M4aTagger final {
  public:
      M4aTagger(process::ProcessRunner& runner, const tools::ToolPathProvider& tools);
      /// file을 제자리에서 다시 쓴다. 실패·취소면 file은 그대로다. 임시 파일(<이름>.ffmeta, <이름>.tagged.m4a)은 항상 지운다.
      TagResult tag(const QString& file, const std::optional<QString>& cover, const TrackTags& tags, std::stop_token stop);
  };
  }
  namespace xgs::output {
  enum class PutStatus { Stored, AlreadyExists, Failed };
  struct PutResult { PutStatus status; QString error; };
  class OutputSink { public: virtual ~OutputSink() = default;
      virtual bool exists(const QString& fileName) = 0;                                        // 받지 않는 이름이면 std::invalid_argument
      virtual PutResult put(const QString& fileName, const QString& source, bool overwrite) = 0; };
  class LocalFolderSink final : public OutputSink {
  public: explicit LocalFolderSink(QString directory);   // 폴더를 만든다
      QString directory() const; /* exists, put: put은 한 번에 하나씩(std::mutex) */ };
  bool isAcceptedFileName(const QString& fileName);      // Kotlin LocalFolderSink.resolve의 require와 같은 판정
  }
  ```
- 문구(`ffmpeg를 찾을 수 없습니다.`, `태그를 쓰지 못했습니다: …`, `알 수 없는 오류`, 자세한 내용 200 단위)와 종류(`FATAL`, `OTHER`)는 Kotlin 그대로.

- [ ] **Step 1: 오라클에 `W fn`을 더한다** — `LocalFolderSink(임시 폴더).exists(name)`이 던지면 `0`, 아니면 `1`.
- [ ] **Step 2: 테스트 19개를 옮기고 둘을 더한다** (클래스 `M4aTaggerTest`, `LocalFolderSinkTest`, `FfmetadataReaderTest`; 겹치는 `put` 시험은 스레드로; 취소 시험은 처리기 안에서 멈춤을 요청하고 `Cancelled`와 임시 파일 없음을 본다)

```cpp
// 더하는 것 (added)
void putWorksWhenTheTargetPathIsLongerThan260();       // LocalFolderSinkTest
void aStartFailureOfFfmpegIsAFailureAndKeepsTheOriginal();   // M4aTaggerTest: StartFailed → Failed(OTHER), 원본 그대로, 임시 파일 없음
```
- [ ] **Step 3: 실패를 본다 → 구현한다** → **Step 4: 생성기 `W fn`** — Q1의 파일 이름 생성기 답과 함께 `.`·`..`·빈 글·공백뿐·구분자(`\`, `/`)·드라이브(`C:a`)·끝 공백과 점·금지 글자·예약 이름(`CON`, `nul.m4a`)·아주 긴 이름.
- [ ] **Step 5: 통과와 변이** — 21개 통과, `W fn` 0 불일치. 변이 6개 이상(`.`을 받음, 끝 공백을 받음, 종료 코드 0이면 출력 파일 없이도 성공, 실패 때 원본을 바꿈, 임시 파일을 남김, 자세한 내용 201 단위)이 대조나 테스트에 잡힌다(어느 쪽에 잡혔는지 적는다).
- [ ] **Step 6: 커밋** — `Qt: m4a 태그 쓰기와 출력 폴더 이식`

### Task 7: 실제 도구 통합 테스트

**Files:**
- Create: `qt/tests/integration/tst_real_ffmpeg_tagging.cpp`, `tst_real_ytdlp.cpp`, `ffprobe.h/.cpp`, `qt/tests/integration/CMakeLists.txt`
- Modify: `qt/tests/CMakeLists.txt`, `test-classes.json`, `qt/README.md`
- 원본(읽기만): `engine/src/test/.../integration/RealFfmpegTaggingIntegrationTest.kt`(8개), `RealYtDlpIntegrationTest.kt`(5개), `Ffprobe.kt`

- [ ] **Step 1: 테스트 13개를 옮긴다** (클래스 `RealFfmpegTaggingIntegrationTest`, `RealYtDlpIntegrationTest`). 실제 `SystemProcessRunner`·`ToolLocator`·`M4aTagger`·`YtDlpResolver`·`readVideoInfo`·가사 추출을 쓴다. `XGS_INTEGRATION`이 `1`이 아니거나 도구가 없으면 `QSKIP`(Kotlin의 `assumeTrue`와 같은 조건, 같은 문구). ctest에는 레이블 `integration`으로 등록한다.
- [ ] **Step 2: ffmpeg 쪽을 돌린다** — `XGS_INTEGRATION=1`로 `tst_real_ffmpeg_tagging`. Expected: 8개 통과(네트워크 없음).
- [ ] **Step 3: yt-dlp 쪽을 돌린다** — `tst_real_ytdlp`. 유튜브에 실제 요청을 보낸다(Kotlin 통합 테스트와 같은 영상·재생목록, 오디오는 내려받지 않음). Expected: 5개 통과. 유튜브나 네트워크 문제로 실패하면 한 번만 다시 돌리고, 그래도 안 되면 원인과 함께 멈춰 알린다(통과한 것처럼 적지 않는다).
- [ ] **Step 4: 실제 도구로 Review Focus를 확인한다(시험으로 남긴다, `added`)**

```cpp
// RealYtDlpIntegrationTest
void anOutputFolderWithSpacesKoreanAndAnAmpersandWorks();     // 정보 파일만 받는 명령을 그런 폴더에: 파일이 그 폴더에 생긴다
void aPercentSignInTheOutputFolderIsReportedNotSilentlyMisplaced();
// 관찰: `%`가 든 폴더에 yt-dlp가 어디에 쓰는가. 결과에 따라 downloadCommand 호출 전 `%`를 `%%`로 바꾸는 일을 Q2d에 넘기거나(보고서),
// 이 Task에서 `ytdlp::escapeOutputTemplateDirectory(dir)`를 더한다(더하면 Kotlin과 다르게 한 것에 적는다).
```
- [ ] **Step 5: 커밋** — `Qt: 실제 ffmpeg·yt-dlp 통합 테스트 이식`

### Task 8: 관문과 보고서

**Files:**
- Create: `docs/superpowers/specs/2026-11-18-qt-engine-process-report.md`
- Modify: `qt/README.md`

- [ ] **Step 1: 관문을 처음부터 돌린다**

깨끗한 재빌드(릴리스·디버그) → 두 빌드의 `ctest`(통합 테스트는 건너뜀으로 나온다) → `XGS_INTEGRATION=1`로 통합 테스트 한 번 → `test-names.ps1` → `forbidden-calls.ps1` → `run.ps1 -PerKind 10000` seed 20261115, 1, 987654321(모든 종류) → 같은 것에 `-DebugTool` 한 번 → `run.ps1 -PerKind 100000 -Seed 424242`. 각 단계의 원출력을 파일로 남기고 보고서의 수는 거기서 옮긴다.
Expected: 모두 통과, 빠진 이름 0, 금지 호출 0, 대조 불일치 0, 통합 테스트 13 + 2 통과.

- [ ] **Step 2: 보고서** — Q2a 보고서와 같은 모양: 읽는 법(Task와 종류 글자), 관문 표, 관찰 표(경로, 형 있는 JSON 디코딩, `writeString`, `ProcessBuilder` 인용), 대조가 찾은 C++ 버그, 변이 결과, **Kotlin과 다르게 한 것**(위 "작성자가 정한 것" 다섯과 실행 중 더해진 것, 각각 예와 함께), 고치지 않은 Kotlin 쪽 동작, Review Focus 여섯 가지가 어디서 확인됐는지, 확인하지 못한 것(다른 드라이브로 옮기기, Windows 10, 백신이 파일을 쥐고 있을 때 등), 사용자가 정할 것, Q2c(실제 HTTP 내려받기 구현을 `Fetch`에 끼우기)·Q2d(취소 중 실제 ffmpeg 자손 정리, 작업 폴더, 명령 조립 예외, NaN 백분율)에 넘길 것. 측정하지 않은 것을 측정한 것처럼 적지 않는다. 커밋 해시를 적지 않는다.

- [ ] **Step 3: 커밋** — `문서: Qt 전환 Q2b(프로세스·도구·조회·태그) 보고서`

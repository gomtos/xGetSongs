# Qt Q2b(프로세스 실행·도구·yt-dlp 조회·태그 쓰기·출력 폴더) 보고서

작성일: 2026-11-20
설계: [2026-11-15-qt-rewrite-and-playlists-design.md](2026-11-15-qt-rewrite-and-playlists-design.md) §3, §4(Q2 행)
계획: [2026-11-18-qt-engine-process.md](../plans/2026-11-18-qt-engine-process.md)
이전 단계: [2026-11-15-qt-engine-logic-report.md](2026-11-15-qt-engine-logic-report.md)(Q2a)

환경: Windows 11 Home 10.0.26200, Visual Studio 2022(vcvars64), Qt 6.11.3, 정답 쪽 JVM은 `server/build/sidecar-image`의 OpenJDK 21.0.9와 그 안의 `engine.jar`·`shared-jvm.jar`. Gradle은 돌리지 않았다(`run.ps1`의 오래된 오라클 검사가 다섯 번 모두 통과했다). 시험이 띄운 자식 프로세스는 모두 `CREATE_NO_WINDOW`로 떠서 창이 뜨지 않았다. 실행 중인 xGetSongs 앱은 건드리지 않았고 `%APPDATA%\xGetSongs`에는 아무것도 쓰지 않았다(거기 있는 `yt-dlp.exe`를 실행만 했다).

실제 도구(Task 7이 확인한 것): yt-dlp 2026.08.19(`%APPDATA%\xGetSongs\bin\yt-dlp.exe`), ffmpeg·ffprobe 8.1(`PATH`에 있는 WinGet Gyan.FFmpeg), node v24.11.1(deno 없음). 이번 관문 실행은 같은 곳에서 도구를 찾았지만 버전을 다시 찍지는 않았다.

네트워크: 이번 관문 실행에서 유튜브에 요청을 보낸 것은 통합 시험 `tst_real_ytdlp`를 **한 번** 돌린 것뿐이다. 그 한 번이 yt-dlp를 다섯 번 띄웠다(재생목록 하나, 영상 `kcx0a2OAhN0`, `7mDDM0eBWR0`, `jNQXAC9IVRw` 두 번). 모두 `--skip-download`이고 오디오는 내려받지 않았다. 다시 돌린 적은 없다.

이 문서의 "관문 표"와 "이번 관문 실행"이라고 적은 숫자는 **Task 8이 처음부터 한 번 돌린 관문**의 원본 출력(저장소 밖 작업 폴더, 단계마다 파일 하나)에서 옮겼다: 깨끗한 릴리스·디버그 빌드, 두 빌드의 `ctest`, 실행 파일마다의 결과, 실제 도구 통합 시험 한 번, 두 점검 스크립트, 대조 다섯 번. 그 밖의 숫자(관찰 표, 변이 시험, 되풀이 실행 횟수, Task별 대조 통계)는 각 Task가 자기 작업 중에 잰 것이고 그 자리마다 "Task N"이라고 적었다. Task 8은 그것들을 다시 재지 않았다.

## 읽는 법

**Task 1~8**은 계획서의 여덟 Task다.

| Task | 내용 |
|---|---|
| 1 | 결과 값(`Outcome`), 경로 문자열(`xgs::paths`, Kotlin `Path`처럼), 파일 연산(`xgs::fs`) |
| 2 | 프로세스 실행(`SystemProcessRunner`, Job Object), 줄 나누기(`LineSplitter`), 명령줄 인용(`windowsCommandLine`) |
| 3 | 테스트 지원(가짜 실행기, 고정 도구 경로)과 도구 찾기(`ToolLocator`) |
| 4 | 도구 상태, yt-dlp 설치·업데이트(`ToolManager`) |
| 5 | yt-dlp로 재생목록·영상 조회(`YtDlpResolver`), 형 있는 JSON 읽기 |
| 6 | m4a 태그 쓰기(`M4aTagger`)와 출력 폴더(`LocalFolderSink`) |
| 7 | 실제 ffmpeg·yt-dlp 통합 시험 |
| 8 | 관문과 이 보고서 |

**대조 종류 글자.** Q1·Q2a의 24개(`U X M S C F D G T L Y R N B Z H O J E Q V K A I`, 뜻은 Q2a 보고서의 "읽는 법")에 이번 단계가 둘을 더했다. 둘 다 사례마다 op 하나를 고르고 통계를 op마다 센다. 자세한 생성기 설명은 `qt/README.md`의 "대조 도구" 절에 있다.

| 글자 | op | 비교하는 것 | Task |
|---|---|---|---|
| `W` | `pn` | `Path.of(text).toString()` (던지면 `ERR`) | 1 |
| | `pc` | `Path.of(dir).resolve(name)` | 1 |
| | `pf`, `px` | `fileName`, `nameWithoutExtension` | 1 |
| | `ln` | 자식 출력의 바이트를 줄로 나누기(Kotlin `bufferedReader(UTF_8).forEachLine`) | 2 |
| | `st`, `up` | 도구 상태(`status()`)와 yt-dlp 업데이트(`updateYtDlp()`)를 가짜 실행기로 | 4 |
| | `fn` | 출력 폴더가 그 파일 이름을 받는가 | 6 |
| `P` | `rs`, `fx` | yt-dlp 조회(`resolve`)와 영상 메타데이터(`fetch`)를 가짜 실행기로 | 5 |

## 요약

- 이번 관문 실행에서 **모든 단계가 통과했다**: 릴리스·디버그 깨끗한 빌드(컴파일러 경고 0), 두 빌드의 `ctest` 30개 모두 통과, 이름 대조에서 빠진 Kotlin 이름 0(22개 클래스), 금지 호출 0, 대조 다섯 번 모두 26개 종류 불일치 0과 통계 문턱 통과, 디버그 도구가 릴리스와 26개 종류 모두 같음, 실제 도구 통합 시험은 ffmpeg 쪽 8개 통과, yt-dlp 쪽 5개 통과·2개 건너뜀.
- **하나의 흔들림이 있었다.** `ctest` 다음에 실행 파일마다 결과를 얻으려고 디버그 `tst_local_folder_sink.exe`를 따로 돌렸을 때 `overlappingPutsOfOneNameWithOverwriteAllSucceed`가 한 번 실패했다. 한 번 다시 돌리니 통과했다. Task 7에서도 같은 시험 파일이 한 번 실패한 적이 있다(아래 "확인하지 못한 것"). 원인은 모른다.
- 옮긴 것: 결과 값과 경로 문자열·파일 연산, 프로세스 실행(Job Object 안에서 자식을 낳고 취소 때 프로세스 트리 전체를 죽인다), 도구 찾기, 도구 상태와 yt-dlp 설치·업데이트, yt-dlp 조회, m4a 태그 쓰기, 출력 폴더. Kotlin 단위 테스트 60개(3 + 6 + 15 + 17 + 12 + 6 + 1)가 같은 이름으로 통과한다. 통합 시험 이름 13개도 모두 있지만, 그중 11개만 돌았다(오디오를 내려받는 하나는 따로 켜야 돌고, 하나는 Q2d로 미뤘다; 아래 "계획과 달라진 것").
- **Kotlin과 다르게 한 것**은 계획 작성자가 정한 다섯과 실행 중에 더해진 것들이다(아래). 가장 눈에 띄는 것: Qt 판은 인자를 자식에 글자 그대로 넘긴다. Kotlin의 `ProcessBuilder`는 무작위 인자 목록 2,000개 중 1,421개를 다르게 넘겼다(Task 2의 관찰).
- 이 진술 밖에 있는 것: 실제 HTTP 내려받기(Q2c), 내려받기 작업·동시성·실제 취소 흐름(Q2d), 다른 드라이브로 옮기기, Windows 10, 디버그 빌드의 통합 시험, 오디오를 내려받는 통합 시험 하나, 아래 "확인하지 못한 것".

## 관문 표

### 빌드

`build/qt/vcpkg_installed`, `build/qt-debug/vcpkg_installed`, `C:\dev\vcpkg`는 지우지 않았다. VS 2022 vcvars64 안에서(`VCPKG_ROOT=C:\dev\vcpkg`) 빌드마다 `cmake --preset <프리셋> -S qt`, `cmake --build <폴더> --target clean`, `cmake --build <폴더>`를 이어 돌렸다(릴리스 다음 디버그). 둘 다 성공했다(원본: `02-build-release.txt`, `03-build-debug.txt`).

| 항목 | 릴리스(`qt-release`, `build/qt`) | 디버그(`qt-debug`, `build/qt-debug`) |
|---|---|---|
| 구성 | `Configuring done (7.2s)`, `Generating done (0.9s)` | `Configuring done (6.1s)`, `Generating done (0.7s)` |
| clean | `Cleaning... 131 files.` | `Cleaning... 131 files.` |
| 빌드 단계 | `[1/165]`~`[165/165]` 모두 실행 | `[1/165]`~`[165/165]` 모두 실행 |
| 경고 | 컴파일러 경고 0(`/W4 /WX`). 출력의 경고는 Qt의 CMake 매크로(`Qt6CoreMacros.cmake`)가 구성 때 낸 `CMake Warning` 하나뿐 | 같음 |

Qt는 다시 빌드되지 않았다. 걸린 시간은 재지 않았다.

### 테스트

릴리스 `ctest --test-dir build/qt --output-on-failure`: `100% tests passed, 0 tests failed out of 30`(Total 29.87 sec, 원본 `04-ctest-release.txt`).
디버그 `ctest --test-dir build/qt-debug --output-on-failure`: `100% tests passed, 0 tests failed out of 30`(Total 84.91 sec, 원본 `05-ctest-debug.txt`). Qt의 단언과 `QString`/`QList` 범위 검사가 켜진 빌드다.

두 `ctest`는 `XGS_INTEGRATION` 없이 돌렸다. 그래서 통합 시험 실행 파일 둘은 ctest에서 `Passed`로 나오지만 안의 시험 함수는 모두 건너뜀(`SKIP`)이다(아래 표의 2 통과는 `initTestCase`와 `cleanupTestCase`).

그다음 실행 파일마다 `-o <파일>,txt`로 `Totals:` 줄을 얻고 `-functions`로 시험 함수를 셌다(원본 `06-exes-release.txt`, `07-exes-debug.txt`와 실행 파일마다의 출력). 이번 단계에 생기거나 바뀐 실행 파일(릴리스):

| 실행 파일 | 통과 | 실패 | 건너뜀 | 시험 함수 | 그중 |
|---|---|---|---|---|---|
| `tst_paths` | 270 | 0 | 0 | 7 | Kotlin 짝 없음. 오라클 관찰 행을 데이터 표로(Task 1) |
| `tst_file_ops` | 10 | 0 | 0 | 8 | Kotlin 짝 없음(계획의 7 + Task 6이 더한 `existsSeesFilesAndDirectories`) |
| `tst_line_splitter` | 8 | 0 | 0 | 6 | Kotlin 짝 없음 |
| `tst_command_line` | 4 | 0 | 0 | 2 | Kotlin 짝 없음 |
| `tst_system_process_runner` | 16 | 0 | 0 | 14 | `SystemProcessRunnerTest` 3 + 추가 11. 23.7초 |
| `tst_tool_locator` | 8 | 0 | 0 | 6 | `ToolLocatorTest` 6 |
| `tst_tool_manager` | 20 | 0 | 0 | 18 | `DefaultToolManagerTest` 15 + 추가 3 |
| `tst_ytdlp_resolver` | 22 | 0 | 0 | 20 | `YtDlpResolverTest` 17 + 추가 3 |
| `tst_ffmetadata_reader` | 3 | 0 | 0 | 1 | `FfmetadataReaderTest` 1 |
| `tst_m4a_tagger` | 15 | 0 | 0 | 13 | `M4aTaggerTest` 12 + 추가 1 |
| `tst_local_folder_sink` | 9 | 0 | 0 | 7 | `LocalFolderSinkTest` 6 + 추가 1 |
| `tst_ytdlp_commands` | 14 | 0 | 0 | 12 | Q2a의 11 + 이번 단계의 추가 1(`escapeOutputTemplateDirectoryDoublesEveryPercentSign`) |
| `tst_real_ffmpeg_tagging` | 2 | 0 | 8 | 8 | `RealFfmpegTaggingIntegrationTest` 8(변수 없이 모두 건너뜀) |
| `tst_real_ytdlp` | 2 | 0 | 7 | 7 | `RealYtDlpIntegrationTest` 5 + 추가 2(변수 없이 모두 건너뜀) |
| Q2a까지의 실행 파일 16개 | 648 | 0 | 0 | 381 | Q2a 보고서의 표와 같은 수 |
| **합계(30개)** | **1051** | **0** | **15** | **510** | |

510 = Kotlin 이름 423(Q1 193 + Q2a 157 + 이번 단계의 단위 시험 60과 통합 시험 13) + 일부러 더한 이름 28(Q1 3 + Q2a 3 + 이번 단계 22) + Kotlin 짝 없는 실행 파일 여덟의 함수 59(`tst_smoke` 1, `tst_kt_text` 22, `tst_models` 5, `tst_json` 8, `tst_paths` 7, `tst_file_ops` 8, `tst_line_splitter` 6, `tst_command_line` 2).

**디버그 실행 파일마다의 결과**: 합계 1050 통과, **1 실패**, 15 건너뜀, 함수 510. 실패는 `tst_local_folder_sink`의 `overlappingPutsOfOneNameWithOverwriteAllSucceed` 하나다(원본 `08-FAIL-debug-tst_local_folder_sink.txt`):

```
FAIL!  : LocalFolderSinkTest::overlappingPutsOfOneNameWithOverwriteAllSucceed() Compared lists have different sizes.
   Actual   (failures) size: 1
   Expected (QStringList()) size: 0
C:\Projects\xGetSongs\qt\tests\tst_local_folder_sink.cpp(148) : failure location
Totals: 8 passed, 1 failed, 0 skipped, 0 blacklisted, 905ms
```

이 시험은 덮어쓰기를 켠 `put` 여덟 개를 스레드로 동시에 부르기를 20번 하고, `Stored`가 아닌 답이나 던진 예외를 목록으로 모은다. 8 × 20 = 160번 중 한 번이 `Stored`가 아니었거나 던졌다는 뜻인데, 시험이 목록의 크기만 보여 줘서 그 답이 무엇이었는지(Windows 오류 문구)는 남지 않았다. 지시대로 코드를 고치지 않고 그 실행 파일만 **한 번** 다시 돌렸고 `Totals: 9 passed, 0 failed`였다(원본 `09-rerun-debug-tst_local_folder_sink.txt`). 바로 앞의 디버그 `ctest`에서도 이 실행 파일은 통과했다. 두 `ctest`는 관문 단계로서 통과했으므로 관문을 멈추지 않았다. 아래 "확인하지 못한 것"과 "사용자가 정할 것"에 적었다.

### 실제 도구 통합 시험 (`XGS_INTEGRATION=1`, 릴리스, 한 번)

`XGS_INTEGRATION_AUDIO`는 켜지 않았다(그 변수가 켜지면 오디오를 내려받는 시험이 돈다; 사용자가 허락하지 않았다).

| 실행 파일 | 결과(원본) | 내용 |
|---|---|---|
| `tst_real_ffmpeg_tagging` | `Totals: 10 passed, 0 failed, 0 skipped`, 3419 ms, 종료 코드 0(`10-integration-ffmpeg.txt`) | Kotlin 8개 모두 통과. 네트워크를 쓰지 않는다(무음 m4a와 빨간 그림을 ffmpeg로 만들어 태그를 쓰고 ffprobe로 읽는다) |
| `tst_real_ytdlp` | `Totals: 7 passed, 0 failed, 2 skipped`, 27469 ms, 종료 코드 0(`11-integration-ytdlp.txt`) | 통과 5 = Kotlin 3(재생목록 조회, 앨범 읽기, 설명란 가사) + 더한 2(공백·한글·`&` 폴더, `%` 폴더). 건너뜀 2 = 오디오를 내려받는 `writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum`(`XGS_INTEGRATION_AUDIO=1`이 있어야 돈다), Q2d로 미룬 `downloadsAShortVideoAsM4aWithoutReencoding` |

통과 수의 10과 7은 시험 수에 `initTestCase`·`cleanupTestCase`를 더한 것이다. yt-dlp 쪽 출력에 찍힌 것: 재생목록 100항목, `kcx0a2OAhN0`의 앨범 `Love poem`, `7mDDM0eBWR0` 설명란 80줄에서 가사 54줄(빈 줄이 아닌 것 42줄, 606자). 끝에 시험용 캐시·임시 폴더가 비어 있었다(`the sandbox held: cache [], tmp []`).

계획은 "통합 테스트 13개 통과"와 "오디오는 내려받지 않음"을 함께 적었는데, **계획 작성자의 실수다**: Kotlin `RealYtDlpIntegrationTest`의 다섯 중 둘은 오디오를 내려받는다. 그래서 둘 다 돌 수는 없었다(아래 "계획과 달라진 것").

### 이름 대조 (`pwsh qt/tools/test-names.ps1`, 종료 코드 0, 원본 `12-test-names.txt`)

```
InputClassifierTest: Kotlin 15, Qt 15, missing 0, added 0
FilenameFormatterTest: Kotlin 39, Qt 39, missing 0, added 0
TitleParserTest: Kotlin 41, Qt 42, missing 0, added 1 (leadingNonBmpLettersAreDroppedLikeEmoji)
LyricsExtractorTest: Kotlin 66, Qt 67, missing 0, added 1 (extractOnOneMegabyteDescriptionFinishesQuickly)
LogRedactionTest: Kotlin 32, Qt 33, missing 0, added 1 (redactPathsWithFiveThousandPartsFinishesQuickly)
ErrorClassifierTest: Kotlin 13, Qt 13, missing 0, added 0
ProgressParserTest: Kotlin 7, Qt 7, missing 0, added 0
DownloadConcurrencyTest: Kotlin 3, Qt 3, missing 0, added 0
VideoInfoFileTest: Kotlin 40, Qt 41, missing 0, added 1 (readsATenMegabyteInfoFileQuickly)
YtDlpCommandsTest: Kotlin 10, Qt 12, missing 0, added 2 (downloadRejectsAVideoIdThatIsNotAPlainFileNamePart, escapeOutputTemplateDirectoryDoublesEveryPercentSign)
FfmpegCommandsTest: Kotlin 4, Qt 4, missing 0, added 0
FfmetadataTest: Kotlin 19, Qt 19, missing 0, added 0
LyricsMatcherTest: Kotlin 61, Qt 62, missing 0, added 1 (sigmaFormsAreTheSameLetterInMatching)
SystemProcessRunnerTest: Kotlin 3, Qt 14, missing 0, added 11 (argumentsReachTheChildVerbatim, closesStandardInput, setsThePythonEncodingVariables, aMissingExecutableIsAStartFailure, aBatchFileIsNotExecuted, cancellingKillsGrandchildrenToo, withoutAJobTheGrandchildSurvives, parallelRunsDoNotHoldEachOthersPipes, aStopRequestedBeforeTheStartRunsNothing, opensNoConsoleWindow, aThrowingHandlerKillsTheProcessAndRethrows)
ToolLocatorTest: Kotlin 6, Qt 6, missing 0, added 0
DefaultToolManagerTest: Kotlin 15, Qt 18, missing 0, added 3 (installIsCancelledWhenTheFetchIsCancelled, cancelledInstallLeavesNoTemporaryFile, updateIsCancelledWhenTheRunIsCancelled)
YtDlpResolverTest: Kotlin 17, Qt 20, missing 0, added 3 (aCancelledRunIsCancelledAndFetchesNothing, aPlaylistOfExactly999IsNotTruncated, resolvesAFiveThousandEntryPlaylistQuickly)
FfmetadataReaderTest: Kotlin 1, Qt 1, missing 0, added 0
M4aTaggerTest: Kotlin 12, Qt 13, missing 0, added 1 (aStartFailureOfFfmpegIsAFailureAndKeepsTheOriginal)
LocalFolderSinkTest: Kotlin 6, Qt 7, missing 0, added 1 (putWorksWhenTheTargetPathIsLongerThan260)
RealFfmpegTaggingIntegrationTest: Kotlin 8, Qt 8, missing 0, added 0
RealYtDlpIntegrationTest: Kotlin 5, Qt 7, missing 0, added 2 (anOutputFolderWithSpacesKoreanAndAnAmpersandWorks, aPercentSignInTheOutputFolderIsReportedNotSilentlyMisplaced)
PASSED: every Kotlin test name is a Qt test name in 22 classes; only the 28 intended names were added
```

이 스크립트는 이름만 대조한다. 단언이 Kotlin과 같은지는 각 Task의 구현자와 리뷰어가 보았고 Task 8은 다시 보지 않았다. 이름이 있다고 시험이 돈 것은 아니다: `downloadsAShortVideoAsM4aWithoutReencoding`은 이름과 Kotlin의 건너뛰기 조건만 있고 단언은 Q2d에서 옮긴다.

### 금지 호출 (`pwsh qt/tools/forbidden-calls.ps1`, 종료 코드 0, 원본 `13-forbidden-calls.txt`)

```
looked at 56 files under qt/core for the Q1 list (kt_text.* and kt_tables.inc excluded), 59 for the Q2a list, and 116 text files under qt/ (qt/spike excluded)
PASSED: 0 hits
```

목록은 Q2a와 같다(Qt의 문자·대소문자 함수, Qt의 JSON 클래스, Qt의 수 변환, 경로 정리 함수 `cleanPath`·`absoluteFilePath` 등, 그리고 `qt/`의 모든 텍스트 파일에서 JDK 내부 이름).

### 대조 (`pwsh qt/tools/differential/run.ps1`)

오라클과 비교하는 네 번의 실행과 디버그 도구 자체 점검 한 번이다. 모두 기본 종류 `UXMSCFDGTLYRNBZHOJEQVKAIWP`(Q1의 13개, Q2a의 11개, Q2b의 `W`·`P`) 26개를 한 번의 호출로 돌렸다. 앞의 셋은 `-PerKind 10000`, 넷째는 `-PerKind 100000 -Seed 424242`다. 모두 오래된 오라클 검사를 통과했고 종료 코드 0, 마지막 줄은 `PASSED (kinds UXMSCFDGTLYRNBZHOJEQVKAIWP, <건수> cases per case kind, seed <seed>)`다(원본: `14-diff-10000-seed20261115.txt`, `15-diff-10000-seed1.txt`, `16-diff-10000-seed987654321.txt`, `18-diff-100000-seed424242.txt`). 칸은 "사례 수(표 종류는 코드 포인트·단위·상수 수), 불일치 수"다.

| 종류 | 10000 seed 20261115 | 10000 seed 1 | 10000 seed 987654321 | 100000 seed 424242 |
|---|---|---|---|---|
| U, B, Z | 1112064 코드 포인트씩, 0 | 같음, 0 | 같음, 0 | 같음, 0 |
| X | 65536 단위, 0 | 65536, 0 | 65536, 0 | 65536, 0 |
| M | 11 상수, 0 | 11, 0 | 11, 0 | 11, 0 |
| S C F D G T L Y R N H O J E Q V K A I (19개) | 각 10000건, 0 | 각 10000, 0 | 각 10000, 0 | 각 100000, 0 |
| **W** | 10000건, 0 | 10000, 0 | 10000, 0 | 100000, 0 |
| **P** | 10000건, 0 | 10000, 0 | 10000, 0 | 100000, 0 |
| **불일치 합계** | **0** | **0** | **0** | **0** |
| 걸린 시간 | 2분 15초(15:51:13~15:53:28) | 2분 15초 | 2분 20초 | 14분 5초(16:03:26~16:17:31) |

모든 실행에서 통계 문턱을 넘었다. 실행마다 `(needs …)`가 붙은 통계 줄이 246개 있고(보여 주기만 하는 줄 포함), 그중 문턱이 0보다 큰 228개가 모두 문턱 이상이다. 문턱은 낮추지 않았다. `R`·`N`의 누설 방향 진단(C++ 답이 Kotlin 답보다 덜 가린 사례)은 모든 실행에서 0이다. 측정값을 문턱으로 나눈 값이 가장 작은 셋:

| 실행 | 가장 빠듯한 통계 (측정값, 문턱) |
|---|---|
| 10000, seed 20261115 | W `pc` 성공 64.6% (60%), R 가짜 경로만 든 입력의 Kotlin 답에 `<경로>` 없음 97.2% (90%), P `rs` `OK` 43.3% (40%) |
| 10000, seed 1 | R 96.7% (90%), W `pc` 성공 65.3% (60%), V 길이 있음 32.9% (30%) |
| 10000, seed 987654321 | P `rs` `OK` 42.8% (40%), R 96.7% (90%), V 길이 있음 32.6% (30%) |
| 100000, seed 424242 | V 길이 있음 32.4% (30%), P `rs` `OK` 43.2% (40%), R 97.3% (90%) |

**`W`·`P`의 op별 통계**(문턱이 있는 것; seed 20261115 1만 건과 seed 424242 10만 건, `run.ps1` 출력에서 옮김. 분모는 그 op의 사례 수다. 다른 두 seed도 모두 문턱을 넘었다, 원본 `19-stats-summary.txt`):

| op | 통계 | 문턱 | 1만 건 seed 20261115 | 10만 건 seed 424242 |
|---|---|---|---|---|
| `pn` | `ERR` | 10% | 425/1337 31.8% | 4079/12529 32.6% |
| `pn` | 입력과 다른 답 | 20% | 383/1337 28.6% | 3601/12529 28.7% |
| `pn` | 입력과 같은 답 | 20% | 529/1337 39.6% | 4849/12529 38.7% |
| `pc` | 경로인 답(`ERR` 아님) | 60% | 753/1165 64.6% | 8262/12591 65.6% |
| `pf` | 없음(`-`) | 5% | 102/1243 8.2% | 1032/12663 8.1% |
| `pf` | 점이 둘 이상인 이름 | 10% | 246/1243 19.8% | 2535/12663 20.0% |
| `px` | 빈 답 | 5% | 173/1266 13.7% | 1693/12349 13.7% |
| `px` | 점이 둘 이상인 이름의 답 | 10% | 259/1266 20.5% | 2482/12349 20.1% |
| `ln` | `\r\n`이 든 입력 | 20% | 932/1255 74.3% | 9433/12566 75.1% |
| `ln` | 홀로 선 `\r` | 20% | 827/1255 65.9% | 8091/12566 64.4% |
| `ln` | 홀로 선 `\n` | 20% | 926/1255 73.8% | 9291/12566 73.9% |
| `ln` | UTF-8이 아닌 입력 | 10% | 990/1255 78.9% | 10001/12566 79.6% |
| `ln` | 줄 끝 없이 끝남 | 30% | 496/1255 39.5% | 4817/12566 38.3% |
| `ln` | 멀티바이트 열 안이나 `\r`·`\n` 사이의 조각 경계 | 10% | 878/1255 70.0% | 8800/12566 70.0% |
| `st` | yt-dlp 찾음 / 못 찾음 | 각 20% | 65.5% / 34.5% (1194건) | 64.2% / 35.8% (12494건) |
| `st` | ffmpeg 찾음 / 못 찾음 | 각 20% | 64.9% / 35.1% | 64.4% / 35.6% |
| `st` | js 런타임 찾음 / 못 찾음 | 각 20% | 28.7% / 71.3% | 28.4% / 71.6% |
| `st` | js 버전이 문턱이거나 바로 위·아래 | 10% | 225/1194 18.8% | 2306/12494 18.5% |
| `st` | 시작하지 못하는 도구 | 2% | 240/1194 20.1% | 2510/12494 20.1% |
| `up` | `OK` / `ERR` | 각 30% | 51.4% / 48.6% (1273건) | 52.1% / 47.9% (12544건) |
| `up` | yt-dlp가 시작하지 못함 | 2% | 98/1273 7.7% | 958/12544 7.6% |
| `fn` | 답 `1` / `0` | 각 25% | 54.2% / 45.8% (1267건) | 55.4% / 44.6% (12264건) |
| `rs` | `OK` | 40% | 2151/4962 43.3% | 21722/50332 43.2% |
| `rs` | `ERR` | 20% | 2811/4962 56.7% | 28610/50332 56.8% |
| `rs` | 재생목록 실행 | 30% | 2126/4962 42.8% | 21551/50332 42.8% |
| `rs` | 영상 실행 | 20% | 1890/4962 38.1% | 19097/50332 37.9% |
| `rs` | 답에 쓸 수 없는 항목 | 10% | 1178/4962 23.7% | 11798/50332 23.4% |
| `rs` | `null` 항목 | 5% | 451/4962 9.1% | 4775/50332 9.5% |
| `rs` | 형 어긋난 속성이 든 stdout | 10% | 957/4962 19.3% (그중 훑기가 틀린 것 91, 1.8%) | 9859/50332 19.6% (훑기가 틀린 것 1006, 2.0%) |
| `rs` | 표준 JSON이 아닌 stdout | 5% | 3112/4962 62.7% | 31273/50332 62.1% |
| `rs` | `MAX_RANK`에서 잘린 답 | 1% | 69/4962 1.4% | 708/50332 1.4% |
| `fx` | 메타데이터 / `NULL` | 각 30% | 54.3% / 45.7% (5038건) | 54.1% / 45.9% (49668건) |

"형 어긋남"에서 훑기가 틀린 것을 빼도(1만 건 866/4962 = 17.5%, 10만 건 8853/50332 = 17.6%) 문턱 10%를 넘는다. 이 뺄셈은 Task 8이 출력의 두 수로 한 것이다.

### 디버그 도구 자체 점검 (`run.ps1 -PerKind 10000 -Seed 20261115 -DebugTool`, 종료 코드 0)

종류마다 보통 비교를 한 뒤 같은 사례 파일(표 종류 `U X M B Z`는 같은 표 모드)을 디버그 빌드의 `build/qt-debug/xgs-diff.exe`에도 주어 릴리스 답과 줄마다 비교한다(원본 `17-diff-10000-seed20261115-debugtool.txt`, 5분 23초, 15:58:03~16:03:26). 깨끗한 재빌드 뒤라 디버그 도구는 릴리스 것보다 새것이었다. 26개 종류 모두 `debug tool <줄 수> lines, 0 differences from the release tool`이고(줄 수는 `U`·`B`·`Z` 1,112,064, `X` 65,536, `M` 11, 나머지 10,000), 같은 실행의 오라클 비교도 불일치 0, 통계 문턱도 모두 넘었다(숫자는 같은 seed의 첫 실행과 같다). 마지막 줄 `PASSED (kinds UXMSCFDGTLYRNBZHOJEQVKAIWP, 10000 cases per case kind, seed 20261115, debug tool identical to the release tool)`.

## 관찰 표

모두 해당 Task가 구현 전에 오라클(사이드카 이미지의 JVM에서 Kotlin 원본이나 JDK 공개 API를 부르는 `Oracle.java`, 또는 공개 API만 부르는 일회용 탐침)로 얻은 Kotlin·JVM의 답이다. JDK와 `kotlinx.serialization`의 소스는 보지 않았다. 표기: `«hhhh»`는 UTF-16 단위 hhhh, `ERR`는 던짐, `-`는 없음(null), `""`는 빈 문자열.

### 1. 경로 (`Path.of`, `resolve`, `fileName`, `nameWithoutExtension`, `resolveSibling`; Task 1)

Task 1은 306개(`pn` 214, `pc` 63, `pf` 15, `px` 16)와 `resolveSibling` 22개를 관찰했다. 그중 일부:

| op | 입력 | Kotlin |
|---|---|---|
| pn | `C:/Music/a.m4a` | `C:\Music\a.m4a` |
| pn | `C:\\Music\\\a.m4a` | `C:\Music\a.m4a` |
| pn | `C:\Music\` | `C:\Music` |
| pn | `C:` / `C:a` | `C:` / `C:a` |
| pn | `1:\x`, `«ac00»:\x`, `«ff23»:\x` | `ERR` (드라이브는 ASCII 글자만) |
| pn | `\\`, `\\host`, `\\host\` | `ERR` (UNC는 호스트와 공유 이름이 다 있어야 한다) |
| pn | `\\host\share` | `\\host\share\` |
| pn | `\\?\C:\x` / `\\?\UNC\host\share` | `C:\x` / `\\host\share\` |
| pn | `\\.\C:\x` / `\\.\pipe\x` | `ERR` / `\\.\pipe\x` |
| pn | `a\..\b`, `C:\a\.\..\b` | 그대로(`.`·`..`를 정리하지 않는다) |
| pn | `C:\a.`, `C:\a `, `C:\a \` | `C:\a.`, `C:\a `, `C:\a `(끝 점·공백은 남는다) |
| pn | ` C:\a` | `ERR` (앞 공백 뒤의 `:`) |
| pn | `C:\a*b`, `C:\a«0001»b`, `C:\a:b` | `ERR` |
| pn | `C:\a«007f»b`, `C:\a«feff»`, `C:\a«de00»` | 그대로 |
| pn | `"C:\Music"` | `ERR` (따옴표) |
| pn | `«feff»C:\x` | `ERR` |
| pc | `C:\a` + `\x` | `C:\x` |
| pc | `C:\a` + `C:x` / `c:x` | `C:\a\x` (같은 드라이브, 대소문자 무시) |
| pc | `C:\a` + `D:x` | `D:x` |
| pc | `C:\a` + `C:` | `C:\a\`(다시 정리하지 않는다) |
| pc | `C:\a` + `/` | `C:\` |
| pc | `C:\a` + `\\` | `ERR` |
| pf | `C:\`, `C:`, `\`, `\\h\s\` | `-` |
| pf | `C:\a\` | `a` |
| px | `a.b.c` / `.m4a` / `abc.` / `...` | `a.b` / `""` / `abc` / `..` |
| sibling | `C:\a\b.m4a` + `b.ffmeta` | `C:\a\b.ffmeta` |
| sibling | `C:\` + `x` | `x` (부모가 없으면 이름 그대로) |

규칙(모두 관찰에서 나왔다): `\`·`/`의 연속은 구분자 하나이고 끝 구분자는 떨어지며 출력은 늘 `\`다. 드라이브는 ASCII 글자 + `:`. 앞 구분자 둘 이상은 UNC이고 호스트와 공유 이름이 다 있어야 한다. `\\?\`는 `X:`+구분자나 `UNC\`가 뒤따를 때만 받는다. `.`·`..`·끝 공백·끝 점은 그대로 둔다. `< > : " | ? *`와 U+0020 미만은 `ERR`(드라이브의 `:`는 예외). U+007F·U+0085·U+FEFF·짝 없는 서로게이트는 보통 글자다. `xgs::paths`는 이 규칙대로 만들었고 306개 모두와 이번 관문의 `W pn pc pf px`에서 Kotlin과 같다.

### 2. `Files.writeString(path, text, UTF_8)`와 짝 없는 서로게이트 (Task 1)

| 글 | 결과 | 파일 |
|---|---|---|
| `a«d83d»b`, `a«de00»b`, `«dbff»`, `x«d83d»`, `«de00»«d83d»` | `UnmappableCharacterException: Input length = 1` | 없던 파일은 생기지 않고, 있던 파일(`OLD`)은 그대로 |
| `ok«d83d»«de00»` | 성공 | `6F 6B F0 9F 98 80` |
| `한글\n` | 성공 | `ED 95 9C EA B8 80 0A`(BOM 없음) |

그래서 `fs::writeUtf8`은 짝 없는 서로게이트가 있으면 아무것도 쓰지 않고 실패하며 문구는 `Input length = 1`이다. 태그 쓰기에서 이 문구가 `태그를 쓰지 못했습니다: Input length = 1`로 사용자에게 간다(Task 6이 Kotlin도 같다는 것을 보았다).

### 3. 자식 출력의 UTF-8 줄 나누기 (`bufferedReader(UTF_8).forEachLine`; Task 2)

| 바이트 | 줄(UTF-16) |
|---|---|
| 빈 입력 / `0a` / `61 0d` | 없음 / 빈 줄 하나 / `a` |
| `a\nb\r\nc\rd` | `a` `b` `c` `d` |
| `a\r\r\nb`, `a\n\rb` | `a`, `""`, `b` |
| `80`, `bf 61`, `f5`, `fe`, `ff` | 시작할 수 없는 바이트마다 U+FFFD 하나 |
| `c0 af`, `c1 bf` | U+FFFD 둘 |
| `e3 81 0a 62` | U+FFFD, `b` (잘린 열은 U+FFFD 하나이고 `\n`을 삼키지 않는다) |
| `e0 80 af`, `f0 80 80 80`, `f4 90 80 80` | 바이트마다 U+FFFD |
| `ed a0 80`, `ed bf bf` | U+FFFD **하나**(UTF-8로 쓴 서로게이트는 통째로 하나) |
| `ed a0 41` | U+FFFD, `A` |
| `ef bb bf 61` | U+FEFF, `a` (BOM이 남는다) |

규칙은 "올바른 앞부분이 가장 긴 곳에서 대체"이되 `ED A0..BF`를 올바른 앞부분으로 보고 완성된 서로게이트 열은 U+FFFD 하나로 바꾸는 것이다. `LineSplitter`가 그대로 하고, 조각을 어디서 나눠도(멀티바이트 열 안, `\r`과 `\n` 사이) 답이 같다(`W ln`).

### 4. `ProcessBuilder`의 인용 (Task 2, 관문 아님)

`xgs-diff arglists 2000 20261118`이 만든 무작위 인자 목록 2,000개(공백, 탭, `"`, `\`, `%`, `&`, `^`, `!`, 괄호, 한글, 이모지, 빈 인자, 끝의 `\` 1~3개)를 오라클이 Kotlin과 같은 `ProcessBuilder`로 `xgs-test-child argv`에 넘기고, 자식의 C 런타임이 받은 목록을 준 것과 비교했다. **1,421개가 다르게 도착했다**(시작하지 못한 것 0). Qt의 `SystemProcessRunner`는 같은 2,000개를 모두 글자 그대로 넘긴다(`argumentsReachTheChildVerbatim`). 다르게 도착한 예(인자를 `·`로 나눔):

| 준 것 | 받은 것 |
|---|---|
| `--opt="x y";a b--opt="x y"한글\\,` (하나) | `--opt=x` · `y;a b--opt=x` · `y한글\\,` (셋으로 쪼개지고 `"`가 사라짐) |
| `🎵\"\"🎵` | `🎵""🎵` (`"` 앞의 `\`가 사라짐) |
| ` a%\"\\"&` | ` a%"\&` |
| `%PATH%"` · `한글=🎵` | `%PATH% 한글=🎵` (둘이 하나로 붙음) |
| `&&"`+탭+`!x!)한글;` | `&&` · `!x!)한글;` (탭이 사라지고 둘로) |
| `?=a bé&&\\"é` · `&& \\` · `%PATH%,&&\\\` · `?a b* \` | `?=a bé&&\é &&` · `\\ %PATH%,&&\\\ ?a` · `b*` · `\` (뒤 인자들이 엉킴) |

Kotlin이 실제로 넘기는 인자(도구 경로, 출력 틀 `<폴더>\<ID>.%(ext)s`, `--print` 틀, 태그 파일 경로)에 `"`가 드물어서 지금 Kotlin 앱에서 이것이 문제가 됐는지는 모른다. Qt 판은 이 차이를 따르지 않는다(아래 "Kotlin과 다르게 한 것" 6번).

### 5. 형 있는 JSON 읽기 (`Json { ignoreUnknownKeys = true }.decodeFromString`; Task 5)

Kotlin은 yt-dlp의 `-J` 출력을 `RawPlaylist`/`RawEntry` 클래스에 형을 맞춰 읽는다. 이것은 Q2a의 `parseToJsonElement`와 받아들이는 범위가 다르다(더 엄격한 곳도 더 느슨한 곳도 있다). Task 5가 339개를 관찰했고, 전체 표는 저장소에 넣지 않은 작업 기록(Task 5의 보고서)에만 있다. 대표 행(`fx`는 영상 메타데이터: `M 제목 채널 …` 또는 `NULL`, `rs`는 조회 결과):

| op | stdout | Kotlin |
|---|---|---|
| fx | `{"title":"a"}` | `M a` |
| fx | `{"title":1}`, `{"title":true}`, `{"title":{}}`, `{"title":abc}`, `{"title":NaN}` | `NULL` (문자열 자리에는 문자열이나 `null`만) |
| fx | `{"title":null}` | `M` (제목 없음) |
| fx | `{"title":"a","title":"b"}` | `M b` (중복 키는 뒤 값) |
| fx | `{"title":"a","title":1}` | `NULL` (중복 키도 매번 형을 본다) |
| fx | `{title:"a"}` | `NULL` |
| fx | `{"x":{"y":[1,2,{"z":NaN}]},"title":"a"}` | `M a` (모르는 키의 값은 건너뜀) |
| fx | `{"x":[1,,2],"title":"a"}`, `{"title":"a","x":{y:1}}` | `M a` (모르는 값의 문법은 보지 않는다) |
| fx | `{"x":"\q","title":"a"}` | `NULL` (모르는 값 속 문자열의 이스케이프는 본다) |
| fx | `{"x":[1},"title":"a"}` | `NULL` (괄호 짝은 본다) |
| fx | `{"title":"a" "channel":"b"}` | `M a b` (멤버 사이 쉼표가 빠져도 받는다) |
| fx | `{"title":"a",}` | `NULL` |
| fx | `[]`, `"str"`, `1`, `null`, 빈 출력 | `NULL` (최상위는 객체만) |
| fx | `«feff»{"title":"a"}` | `NULL` (BOM) |
| fx | `{"title":"a"}«00a0»` | `NULL` (U+0020·탭·LF·CR 밖의 공백) |
| fx | `{"title":"a"}\r\n` | `M a` |
| fx | `{"title":"a"} x` | `NULL` |
| fx | `{"title":"\ud800"}` | `M «d800»` |
| fx | `{"channel":null,"uploader":"u"}` | `M - u` (채널이 null이면 업로더) |
| fx | `{"channel":"","uploader":"u"}` | `M - ""` (빈 채널은 업로더로 대체하지 않는다) |
| fx | `{"t\u0069tle":"a"}` | `M a` (키의 이스케이프를 푼다) |
| fx | 모르는 값에 배열 60000단 | `M a` |
| rs | `{"title":"T","entries":[]}` | `OK PLAYLIST title=T items=0` |
| rs | `{"entries":null}`, `{"entries":{}}`, `{"entries":[1]}` | `ERR yt-dlp 응답을 해석할 수 없습니다.` |
| rs | `{"entries":[null]}` | `OK items=1`, 항목 1은 `알 수 없는 항목` |
| rs | `{"entries":[{"id":"a"},]}`, `{"entries":[{"id":"a"}]x]}` | `ERR` (목록에서는 끝 쉼표도, `]`를 쉼표처럼 쓰는 것도 안 된다) |
| rs | `{"entries":[{"id":"a","title":"[private video]"}]}` | 쓸 수 있음(대소문자를 가린다) |
| rs | `{"entries":[{"id":"","title":"A - B"}]}` | 쓸 수 있음, `001 A - B.m4a` |
| rs | `{"entries":[{"id":"a","channel":5}]}` | `ERR` |

규칙(Task 5가 `json.h`의 `ParseOptions` 주석에 적은 것): 공백은 SP·HT·LF·CR뿐, 최상위는 객체이고 앞뒤에 공백 말고는 없다. 멤버 사이에는 `,`, 또는 쉼표 없이 다음 키의 `"`. `,}`·`,,`·앞 쉼표는 오류. 문자열 속성은 문자열이나 정확히 `null`. 목록 속성은 `[`, 쉼표 하나씩으로 나뉜 객체나 `null`, `]`. 중복 키는 다시 읽고 뒤 값이 남는다. 모르는 키의 값은 문자열(이스케이프 검사)이나 리터럴, 또는 문자열과 괄호 짝만 보는 컨테이너로 건너뛴다(깊이 제한 없음). C++은 이것을 `xgs::json`의 선택 모드 `ParseOptions::typed`로 했고, 기본 모드(종류 `J`, `V`)의 답은 바뀌지 않았다(이번 관문에서 `J`, `V` 불일치 0).

### 6. 출력 폴더가 받는 파일 이름 (`LocalFolderSink`; Task 6)

| 이름 | Kotlin |
|---|---|
| `a.m4a`, `a `, `a.`, `CON`, `nul.m4a`, `«feff»`, `a«d800»` | 받음 |
| `...`, `.. `, ` ..` | 받음. 그리고 `exists`가 **참**이다(Windows가 그 이름을 폴더 자신이나 부모로 풀기 때문) |
| 빈 이름, ` `, `.`, `..`, `C:a`, `a\b`, `a/b`, `a:b`, `«3000»` | `IllegalArgumentException` |
| `a*b`, `a«0001»b` | `InvalidPathException` |
| `C:`, `\`, `/` | **`NullPointerException`** (`fileName`이 null인데 `.toString()`을 부른다) |

C++의 `isAcceptedFileName`은 "공백뿐이 아님, `.`·`..`가 아님, `paths::fromText`가 성공, 그 `fileName`이 이름과 같음"이고, 받지 않는 이름이면 `exists`·`put`이 `std::invalid_argument`를 던진다(`W fn` 불일치 0).

### 7. yt-dlp와 출력 폴더의 `%` (Task 7)

yt-dlp 2026.08.19에 `--load-info-json`(손으로 쓴 정보 파일), `--skip-download --print filename -o <폴더>\id.%(ext)s`를 주어 네트워크 없이 관찰했다.

| 폴더 이름(이스케이프 없이) | yt-dlp가 쓴 곳 |
|---|---|
| `C:\o\100% 노래` | 그대로(홀로 선 `%`는 남는다) |
| `a%(id)s b` | `aabcdefghijk b`(틀 필드로 풀린다) |
| `50%%x` | `50%x` |
| `%s %d %` | 그대로 |

yt-dlp는 모든 경우에 종료 코드 0이라서 파일이 **조용히 다른 폴더에** 간다. 모든 `%`를 겹치면(`%%`) 모두 글자 그대로 돌아왔다. 이것으로 `ytdlp::escapeOutputTemplateDirectory`를 더했다(아래 "Kotlin과 다르게 한 것" 13번). 통합 시험 `aPercentSignInTheOutputFolderIsReportedNotSilentlyMisplaced`는 폴더 `100% %(id)s 50%%`를 이스케이프해 넘기고 정보 파일이 바로 그 폴더에 생기는지 본다(이번 관문에서 통과).

### 8. 도구 상태와 설치 (Task 4)

| 입력 | Kotlin |
|---|---|
| `toIntOrNull("２２")` / `("٢٢")` / `("२")` | 22 / 22 / 2 (유니코드 숫자를 받는다) |
| `toIntOrNull("2147483648")`, `("")` | null → 버전 비교에서 0 |
| 실행기가 `status()`에서 `IOException` | `found=false, version=null, path=<경로>` |
| 실행기가 `updateYtDlp`에서 `IOException` | `yt-dlp를 실행할 수 없습니다: <문구>` |
| 받은 파일을 확인 실행할 때 `IOException` | `내려받은 yt-dlp를 실행할 수 없습니다.`, `yt-dlp.new.exe`는 남지 않음 |
| `fetch`가 `IOException("offline")` | `yt-dlp 다운로드에 실패했습니다: offline` |
| 대상 `yt-dlp.exe`가 빈 폴더 | 설치된다(대상이 파일이 된다) |

## 이식 중 대조가 찾은 C++ 버그

- **없다.** Task 1, 2, 4, 5, 6의 C++은 첫 대조 실행부터 Kotlin과 같았다(각 Task 보고서). 관찰을 먼저 하고 그 규칙대로 썼기 때문이다. Task 3, 7은 대조 종류를 더하지 않았다.
- **리뷰가 찾은 C++ 버그 하나(Task 2).** 줄 처리기가 예외를 던지면(메모리 부족 포함) 읽기 스레드에서 `std::terminate`로 프로세스가 죽었다. Kotlin은 예외를 위로 올리고 프로세스 트리를 죽인다. 읽기 스레드가 예외를 잡아 두고, 프로세스 트리를 죽이고, `run`이 부른 스레드에서 다시 던지게 고쳤다(시험 `aThrowingHandlerKillsTheProcessAndRethrows`; 고치기 전에는 그 시험이 0xC0000409로 죽었다). 이 동작은 Kotlin과 같아서 아래 "Kotlin과 다르게 한 것"에는 없다.
- **생성기가 고쳐진 곳**(C++ 버그가 아님): Task 1은 드라이브 대소문자 변이가 3건만 잡혀서 드라이브 상대 이름의 비율을 올렸다. Task 5는 처음 생성기가 `OK` 비율·`null` 항목·형 어긋남 통계를 못 넘어 생성기를 고쳤고, `truncated`의 `>=` 변이가 살아남아 긴 재생목록의 범위를 `MAX_RANK`−1~+5로 넓혔다. 문턱은 낮추지 않았다.
- **이번 관문 실행**에서는 불일치가 없었고 고친 C++도 없다.

## 변이 시험 (각 Task의 실행)

이식 코드를 일부러 틀리게 고쳐 대조나 시험이 잡는지 본 것이다. 모두 시험 뒤 되돌렸고 커밋에 없다. Task 8은 다시 하지 않았다. 숫자는 각 Task의 seed 20261115, 1만 건 실행의 불일치 수다.

| Task | 변이 수 | 잡힘 | 계획의 최소 |
|---|---|---|---|
| 1 | 9 | 9 | 6 |
| 2 | 11 | 11 | 6 + 실행기 3 |
| 4 | 10 | 10 | 6 |
| 5 | 18 | 18 | 10 |
| 6 | 11 | 10 | 6 |
| 합계 | 59 | 58 | |

- **Task 1**: 끝 구분자 남김 `W` 183, 겹친 구분자 남김 469, `/`를 그대로 1352, 뿌리에 자식을 이을 때 구분자 둘 125, 끝 점 떼기 724, 확장자를 첫 점에서 자름 529, 드라이브 글자를 대소문자 구분으로 12. 긴 경로에 `\\?\`를 안 씀(`pathsLongerThan260Work`), 짝 없는 서로게이트를 씀(`writeUtf8HandlesALoneSurrogateLikeKotlin`)은 시험이 잡았다.
- **Task 2**: `\r`을 줄 끝으로 안 봄 1694, `\r\n`을 둘로 1478, 끝 줄 버림 744, 조각 경계에서 글자 깸 1299, 잘못된 바이트 지움 1551, 빈 줄 건너뜀 1056, `ED`를 보통 규칙으로 647, `E0` 너무 긴 꼴 검사 없음 225(이것은 시험이 아니라 대조만 잡는다). 실행기: 핸들을 통째로 물려줌 → `parallelRunsDoNotHoldEachOthersPipes` **10번 중 10번** 실패, Job 없음 → `cancellingKillsGrandchildrenToo` 실패, 끝 `\`를 겹치지 않음 → `argumentsReachTheChildVerbatim` 실패.
- **Task 4**(`W st`·`up`): deno 문턱 2.3.1 45, 버전을 문자열로 비교 293, 넘친 부분을 실패로 14, ffmpeg 버전을 줄 끝까지 652, 첫 줄 대신 마지막 줄 650, 업데이트 실패 문구에 첫 줄 294, ASCII 숫자만 92, 첫 줄 trim 안 함 340, 업데이트가 stderr 무시 404, js 이름 대소문자 구분 240.
- **Task 5**(`P`): 업로더 대체 빼기(조회) 643·(메타데이터) 518, `null` 항목 건너뜀 505, 쓸 수 없는 항목에 파일 이름 1264, `MAX_RANK`−1 83, `MAX_RANK`+1(대조 실행이 파일 이름 조립의 예외로 멈춤, 시험 1개), `truncated`를 `>=`로 14(생성기를 고친 뒤), 종료 코드 무시 781, 형 어긋난 문자열 받음 438, 모르는 키에서 실패 4009, 영상 조회에 `alsoVideoId` 1103, `fetch`가 던짐(대조 실행이 멈춤, 시험 1개), `availability`에서 `subscriber_only` 빼기 222, 빠진 쉼표 거절 347, 컨테이너 안 문자열 이스케이프 무시 10, stdout 줄을 LF로 잇지 않음 193, 끝 쉼표 받음 9, 시작 실패 문구 빼기 173.
- **Task 6**: `.` 받음 `W` 4, 공백뿐인 이름 받음 61, 끝 공백 거절 46, 파일 이름 비교 빼기 147(시험도), 종료 코드 0이면 출력 파일 없이 성공(시험), 실패 때 원본 바꿈(시험), `.tagged.m4a` 남김(시험 3개), `.ffmeta` 남김(시험 5개), 자세한 내용 201 단위(시험), `put`의 잠금 빼기(`overlappingPutsOfOneNameWithOverwriteAllSucceed`; 대조는 0, 예상대로). **잡히지 않은 하나**: `tag`의 `RunStatus::Cancelled` 검사를 빼기. 바로 뒤의 멈춤 검사가 같은 일을 해서(취소는 멈춤 요청으로만 생긴다) 어떤 입력으로도 잡을 수 없는 동치 변이다.
- **계획의 변이 하나는 아무것도 바꾸지 않았다**(Task 6): "끝 공백을 받음". Kotlin이 이미 `a `를 받는다(관찰). 대신 "공백뿐인 이름 받음"과 "끝 공백 거절"을 돌렸고 둘 다 잡혔다.

## Kotlin과 다르게 한 것

다섯은 계획 작성자가 정했고(사용자가 바꿀 수 있다), 나머지는 실행 중에 Task 구현자나 controller가 정했다. **실행 중에 정한 것은 사용자가 아직 보지 않았다.** 바꾸려면 사용자가 정한다.

### 계획 작성자가 정한 것

1. **`QProcess`를 쓰지 않고 `CreateProcessW`를 직접 부른다.** 설계 §3은 "`QProcess` + Win32 Job Object"였지만 Q0 스파이크가 그 조합이 Qt 내부의 문서에 없는 동작에 기댄다는 것을 찾았다. 예: 자식은 처음부터 새 Job 안에서 태어나고(`PROC_THREAD_ATTRIBUTE_JOB_LIST`), 물려받는 핸들은 그 실행의 파이프 끝 셋뿐이며(`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`), 명령줄 인용은 우리 함수 `windowsCommandLine`이 Microsoft 문서의 규칙으로 만든다. Kotlin은 `ProcessBuilder`이고 Job이 없다.
2. **블로킹 함수 + 멈춤 토큰(`std::stop_token`).** Kotlin의 `suspend` 함수와 코루틴 취소 대신이다. 예: 실행 중에 토큰에 멈춤을 요청하면 `run`이 프로세스 트리를 죽이고 `Cancelled`를 돌려준다(Kotlin은 `CancellationException`). 그 결과 `ToolManager::status()`처럼 결과 형에 "취소"가 없는 함수는 다르게 답한다(아래 9번).
3. **yt-dlp 내려받기(HTTP)는 주입한다(`Fetch`).** 이 단계의 코드는 스스로 네트워크 요청을 보내지 않는다. 예: `installYtDlp`는 받은 `Fetch`에 `YtDlpUrl`을 넘기고, `Fetch`가 `Outcome::failed("offline")`이면 `yt-dlp 다운로드에 실패했습니다: offline`(Kotlin과 같은 문구). 실제 HTTP 구현은 Q2c다.
4. **운영체제 오류 문구는 Windows의 문구다.** 그 앞의 한국어는 Kotlin 그대로다. 예: 설치할 폴더 자리에 파일이 있을 때 Kotlin은 `yt-dlp를 설치하지 못했습니다: <경로>`(Task 4가 관찰한 문구는 경로였다), C++은 `yt-dlp를 설치하지 못했습니다: ` 뒤에 `FormatMessageW`의 Windows 문구가 온다. 시험은 한국어 부분만 비교한다.
5. **주 프로세스가 끝난 뒤 남은 자손도 죽인다.** Kotlin은 취소(또는 예외) 때만 `descendants()`를 죽인다. 예: 자식이 손자를 띄워 두고 정상 종료하면 Kotlin에서는 손자가 계속 살고, Qt 판은 Job을 닫으면서 손자를 죽인다. 단 출력 파이프가 닫힐 때까지 기다리는 것은 Kotlin과 같다: 자손이 stdout을 쥐고 있으면 `run`은 그 자손이 끝나거나 멈춤이 요청될 때까지 돌아오지 않는다(시험하지 않았다, 아래 "Q2d에 넘길 것").

### 실행 중에 더해진 것

6. **인자가 자식에 글자 그대로 닿는다**(Task 2). 위 관찰 4: Kotlin의 `ProcessBuilder`는 무작위 목록 2,000개 중 1,421개를 다르게 넘겼고, Qt 판은 2,000개를 모두 그대로 넘긴다. 예: 인자 `--opt="x y"`는 Qt 판에서 그대로 하나, Kotlin에서는 `--opt=x`와 `y` 둘이다.
7. **`.cmd`·`.bat`은 실행하지 않는다**(계획의 Global Constraint, Task 2가 문구를 정함). 실행 파일 이름이 끝의 점·공백을 뗀 뒤 `.cmd`나 `.bat`으로 끝나면(대소문자 무시) 시작하지 않고 `StartFailed`이며 문구는 `배치 파일(.cmd, .bat)은 실행하지 않습니다`다. 빈 명령은 `실행할 프로그램이 없습니다`. 두 문구 모두 Kotlin에 짝이 없어 Task 2 구현자가 쓴 것이다(controller가 받아들임, 사용자 확인 대기). 예: js 런타임이 `node.cmd`이면 Qt 판은 실행하지 않는다. Kotlin에는 이런 검사가 없다.
8. **`command[0]`을 `PATH`에서 찾지 않고 `.exe`도 붙이지 않는다**(Task 2). `CreateProcessW`의 `lpApplicationName`에 첫 인자를 그대로 주기 때문이다. 예: `["yt-dlp", "--version"]`은 Qt 판에서 시작 실패다. 호출하는 쪽은 전체 경로를 넘겨야 한다. Kotlin의 호출하는 쪽도 늘 `ToolLocator`가 찾은 전체 경로를 넘기므로 지금 코드에서는 차이가 드러나지 않는다.
9. **멈춤을 요청한 뒤의 `status()`**(Task 4, controller 결정). 결과 형이 `ToolsStatus`라서 취소를 나타낼 수 없다. 실행이 취소되면 그 도구가 아무것도 찍지 않은 것처럼 답한다. 예: yt-dlp 실행 중 멈춤 → `yt-dlp found=false, version=없음, path=<경로>`. Kotlin은 `CancellationException`을 던진다. 멈춤을 요청한 쪽이 이 답을 버린다는 약속이다(Q2d에 넘김).
10. **취소가 마지막 옮기기와 겹치면 C++은 `Tagged`라고 답한다**(Task 6). `M4aTagger`는 `<이름>.tagged.m4a`를 원래 파일 위로 옮기는 것이 마지막 단계다. Kotlin에서는 그 옮기기 중에 취소가 오면 파일이 이미 바뀌었는데도 "취소됨"이라고 답할 수 있다. C++은 옮기기가 성공했으면 `Tagged`다. 예: 옮기기가 끝난 직후 멈춤 요청 → Kotlin "취소", C++ `Tagged`(파일은 둘 다 태그가 쓰인 것).
11. **`LocalFolderSink`의 생성자는 던지지 않는다**(Task 6). Kotlin은 폴더를 만들지 못하면 생성자에서 던진다. C++은 그때 던지지 않고, 그 뒤 `put`이 Windows 문구와 함께 `Failed`, `exists`가 거짓이다. 예: 폴더를 만들 수 없는 위치를 주면 Kotlin은 sink를 만드는 순간(`Files.createDirectories`) 예외, C++은 sink는 만들어지고 첫 `put`이 `Failed`. 실패하는 위치로 직접 시험하지는 않았다.
12. **뿌리뿐인 이름**(Task 6). `C:`, `\`, `/`를 Kotlin은 `NullPointerException`으로, C++은 `std::invalid_argument`로 거절한다. 둘 다 "받지 않음"이고 대조 `W fn`의 답(`0`)은 같다.
13. **`ytdlp::escapeOutputTemplateDirectory`를 더했다**(Task 7, controller 결정). 출력 폴더의 모든 `%`를 `%%`로 겹친다. 예: `C:\음악\100% %(id)s` → `C:\음악\100%% %%(id)s`. Kotlin에는 없고 Kotlin은 `%`가 든 폴더에서 위 관찰 7처럼 조용히 다른 곳에 쓴다. `downloadCommand` 자체는 Kotlin 그대로 두었고(대조 `K` 불일치 0), 이것을 부르는 것은 Q2d의 몫이다. 아직 아무 제품 코드도 부르지 않는다.
14. **명령 조립의 예외를 결과로 바꾼다**(Task 5, 6). `YtDlpResolver`는 명령 조립이 던지는 `std::invalid_argument`를 잡아 실패(조회)나 없음(메타데이터)으로, `M4aTagger`는 `tagCommand`의 예외를 `Failed`(종류 `OTHER`)로 바꾼다. Kotlin에서는 `IllegalArgumentException`이 위로 올라갔을 자리다. 지금 코드에서는 앞의 검사(yt-dlp 있음, 출력이 `.m4a`) 때문에 닿지 않는다.
15. **취소가 막히지 않게 하는 2초 장치**(Task 2). 멈춤으로 Job을 죽인 뒤 읽기 스레드가 2초 넘게 안 끝나면 `CancelSynchronousIo`로 그 읽기를 끊는다. 예: Job 밖의 다른 프로세스가 파이프 끝을 쥐고 있어도 취소는 돌아온다. Kotlin에는 이런 장치가 없다(정상 종료 쪽은 위 5번처럼 기다린다).

## 고치지 않은 Kotlin 쪽 동작

모두 **Qt 판이 그대로 따른다**(대조나 시험에서 같은 답). 바꿀지는 사용자가 정한다. 바꾸려면 Kotlin 쪽을 먼저 바꾸고 대조를 다시 돌린다. 입력과 답은 각 Task가 관찰한 것이다.

1. **id가 빈 항목이 "쓸 수 있음"이 된다**(Task 5, 관찰 q25). `{"entries":[{"id":"","title":"A - B"}]}` → 쓸 수 있음, 파일 이름 `001 A - B.m4a`. 빈 영상 ID가 내려받기로 넘어간다(Q2a의 영상 ID 거절이 `downloadCommand`에서 막는다).
2. **비공개·삭제 표시와 `availability`가 대소문자를 가린다**(Task 5, p24·p13). 제목 `[private video]`는 비공개가 아니고, `availability` `PRIVATE`는 쓸 수 있음이다.
3. **빈 채널은 업로더로 대체하지 않는다**(Task 5, f41). `"channel":""`이면 `?:`가 null만 보므로 채널이 빈 문자열로 남는다.
4. **형 있는 JSON 읽기의 너그러움**(Task 5). 멤버 사이 쉼표가 빠져도 받고, 모르는 키의 값은 문법을 거의 보지 않고 건너뛴다(`[1,,2]`, `{y:1}`, 컨테이너 안의 U+00A0). `kotlinx.serialization`의 동작이고 그대로 맞췄다.
5. **ffmpeg는 첫 줄이 비지 않으면 "찾음"이다**(Task 4). "찾음"이 "버전이 있음"이고 버전은 첫 줄에서 `ffmpeg version `을 떼고 첫 공백 앞까지다. 예: 첫 줄 `ffprobe version 7` → 찾음, 버전 `ffprobe`.
6. **업데이트 결과의 "마지막 줄"이 스레드 시간에 따라 다르다**(Task 4). `updateYtDlp`는 stdout과 stderr를 두 스레드에서 한 목록에 모으고 마지막 비지 않은 줄을 결과 문구로 쓴다. 실제 프로세스에서는 어느 줄이 마지막인지가 두 스레드의 순서에 달렸다. 대조에서는 가짜 실행기가 한 스레드에서 차례로 부르므로 같다.
7. **점과 공백뿐인 파일 이름을 받는다**(Task 6). `...`, `.. `는 받는 이름이고, Windows가 그것을 폴더 자신이나 부모로 풀어서 `exists`가 참이다. Task 6 구현자의 판단으로는 `put`이 이미 있는 폴더를 대상으로 하므로 실패하고 파일이 폴더 밖으로 나가지 않는다(관찰한 것은 `exists`의 답까지다). `FilenameFormatter`는 이런 이름을 만들지 않는다. 다만 C++에서 생성자가 폴더를 만들지 못했을 때의 경로에는 남은 지적이 있다(아래 "남은 사소한 것").
8. **BOM으로 시작하는 yt-dlp 출력은 읽지 못한다**(Task 5, f27). 형 있는 읽기가 U+FEFF를 공백으로 보지 않는다. yt-dlp는 BOM을 쓰지 않으므로 실제 영향은 없을 것이다.

다음 셋은 Kotlin의 동작을 관찰했지만 Qt 판이 **따르지 않는다**(위 "Kotlin과 다르게 한 것"): 뿌리뿐인 이름의 `NullPointerException`(12번), 마지막 옮기기 중 취소의 답(10번), 정상 종료 뒤 남는 자손(5번). Q2a 보고서의 "고치지 않은 Kotlin 쪽 동작"은 그대로 남아 있다.

## 계획의 Review Focus 여섯 가지

| 갈래 | 확인한 곳 | 결과 |
|---|---|---|
| 1. 동시에 여러 프로세스를 띄울 때 파이프가 샌다 | `parallelRunsDoNotHoldEachOthersPipes`(긴 `sleep` 하나와 짧은 `emit` 여덟을 동시에, 짧은 것들이 2초 안에 모두 돌아와야 한다) | 이번 관문의 두 `ctest`에서 통과. Task 2에서 핸들 목록을 뺀 변이는 10번 중 10번 이 시험에 걸렸다 |
| 2. 취소했는데 자손이 남는다 | `cancellingKillsGrandchildrenToo`(취소 뒤 5초 안에 손자 PID가 없다), 대조 실행 `withoutAJobTheGrandchildSurvives`(Job 없이 자식만 죽이면 손자가 산다), `aThrowingHandlerKillsTheProcessAndRethrows` | 이번 관문에서 통과. 손자는 시험 자식(`xgs-test-child`)이다. yt-dlp가 띄우는 실제 ffmpeg로 취소해 본 적은 없다(Q2d) |
| 3. 공백·따옴표·끝의 `\`·`%`·`&`·한글이 든 인자 | `argumentsReachTheChildVerbatim`(무작위 2,000개 목록), `tst_command_line`, 통합 시험 `anOutputFolderWithSpacesKoreanAndAnAmpersandWorks`와 `aPercentSignInTheOutputFolderIsReportedNotSilentlyMisplaced` | 이번 관문에서 모두 통과. `%`는 인용과 따로 yt-dlp가 틀로 읽는 문제가 있어서 이스케이프 함수를 더했다(위 13번) |
| 4. 깨끗하지 않은 yt-dlp 출력 | 대조 `W ln`(잘못된 UTF-8, 홀로 선 `\r`, 줄 끝 없는 끝, 조각 경계), `aLineOfTenMegabytesArrivesWhole`, `resolvesAFiveThousandEntryPlaylistQuickly`(한 줄 3.5MB) | 이번 관문의 대조에서 불일치 0(아래 통계), 시험 통과 |
| 5. 모양이 다른 조회 응답 JSON | 대조 `P`(숫자로 온 제목, `null` 항목, 모르는 키의 `NaN`·깊은 값, 망가진 문서, `MAX_RANK` 경계), `tst_ytdlp_resolver`, 통합 시험 `resolvesTheReferencePlaylistWithoutDownloadingAnything`(실제 100항목) | 이번 관문에서 불일치 0, 통과 |
| 6. 긴 경로와 다른 드라이브 | `putWorksWhenTheTargetPathIsLongerThan260`, `pathsLongerThan260Work`, `existsSeesFilesAndDirectories` | 긴 경로는 이번 관문에서 통과. **다른 드라이브로 옮기기는 확인하지 못했다**: 이 PC에는 드라이브가 하나뿐이다(`MoveFileExW`의 `MOVEFILE_COPY_ALLOWED`를 쓰지만 그 길을 탄 적이 없다) |

## 계획과 달라진 것

- **통합 시험 수(Task 7, controller 결정 T7-a).** 계획은 "통합 테스트 13개 통과, 오디오는 내려받지 않음"이라고 적었으나 두 문장은 함께 성립하지 않는다. 계획 작성자가 Kotlin `RealYtDlpIntegrationTest`를 잘못 읽은 것이다: 다섯 중 둘이 오디오를 내려받는다. 그래서 `writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum`은 단언까지 모두 옮겼지만 `XGS_INTEGRATION_AUDIO=1`이 더 있어야 돌게 했고(Kotlin에 없는 조건, 돌리지 않았다), `downloadsAShortVideoAsM4aWithoutReencoding`은 내려받기 작업(Q2d)과 LRCLIB(Q2c)이 없어 이름과 건너뛰기만 두었다. 이 단계에서 실제로 돈 Kotlin 통합 시험은 11개(ffmpeg 8 + yt-dlp 3)와 더한 2개다. Task 8 지시서의 "13 + 2 통과"도 같은 실수를 이어받았고 controller가 고쳤다.
- **`writeUtf8WritesNoByteOrderMark`의 기대 바이트(T1-a).** 계획은 `한글\n` → `EC 95 9C …`라고 적었으나 오타다. `한`(U+D55C)은 `ED 95 9C`이고 오라클도 `ED 95 9C EA B8 80 0A`를 썼다. 시험은 관찰한 바이트를 쓴다.
- **Task 6의 변이 하나(T6-a).** "끝 공백을 받음"은 Kotlin이 이미 받으므로 아무것도 바꾸지 않는 변이였다. 가까운 둘로 바꿨다(위 "변이 시험").
- **더한 시험 이름.** 계획이 적은 추가는 `tst_system_process_runner` 10개, `tst_m4a_tagger`·`tst_local_folder_sink` 하나씩, 통합 둘이었다. 실제로는 22개다: 실행기에 하나 더(`aThrowingHandlerKillsTheProcessAndRethrows`, 리뷰가 찾은 버그의 시험), `tst_tool_manager`에 취소 셋, `tst_ytdlp_resolver`에 셋(취소, 정확히 999개, 5,000항목), `tst_ytdlp_commands`에 이스케이프 하나. `tst_file_ops`에도 Kotlin 짝 없는 `existsSeesFilesAndDirectories`가 하나 더 있다.
- **새 함수.** `fs::exists`(Task 6: `LocalFolderSink::exists`가 폴더와 긴 경로도 보려고), `kt::isDigit`·`kt::digitValue`(Task 4: 버전 비교의 유니코드 숫자), `ytdlp::escapeOutputTemplateDirectory`(Task 7), `json::ParseOptions::typed`(Task 5, 계획의 "선택 인자"를 클래스 모양으로 구체화).
- **시험 도우미.** `xgs-test-child`는 계획의 모드에 `console`(자식에 콘솔 창이 있는지 스스로 보고)과 `sleeperr`(stderr로 `started`)를 더했다. 무작위 인자 목록은 시험과 `xgs-diff`가 함께 쓰는 헤더 `qt/tests/helpers/argument_lists.h`에 있다. 시험은 자식 실행 파일을 컴파일 정의 `XGS_TEST_CHILD`로 찾는다(계획이 정하지 않았던 것). 가짜 실행기는 멈춤이 처리기가 끝날 때까지 요청되면 `Cancelled`를 돌려준다(T3-a, 코루틴이 다음 중단점에서 취소되는 것을 흉내).
- **통합 시험의 격리(T7-c).** yt-dlp 시험 클래스는 그동안 `XDG_CACHE_HOME`, `TEMP`, `TMP`를 한 `QTemporaryDir` 안으로 돌리고 끝에 되돌린다. 사용자의 `%USERPROFILE%\.cache\yt-dlp`와 `%TEMP%`에 아무것도 남지 않게 하려는 것으로 Kotlin 시험에는 없다. 켜는 조건도 Kotlin의 Gradle 태그 대신 `XGS_INTEGRATION=1`이다.
- **대조 `P`의 입력.** 계획 Step 4는 입력을 Q1의 `C` 생성기로 만든다고 했으나, `rs` 입력의 18%만 그 생성기이고 나머지는 손으로 짠 재생목록·영상 주소다(조회가 실제로 실행되는 사례를 늘리려는 것). README에 적혀 있다.
- **`OutputSink::exists`가 던지는 것(P2).** 설계의 "예외 대신 결과"와 달리 받지 않는 이름에 `std::invalid_argument`를 던진다. 계획의 인터페이스가 그렇게 정했고 Kotlin의 `require`와 Q2a 명령 조립 함수를 따른다.
- **README.** 이번 관문에서 README가 시험 출력과 달랐던 두 곳을 고쳤다: `tst_system_process_runner`의 추가 이름이 열 개가 아니라 열한 개(던지는 처리기), Kotlin 짝 없는 실행 파일 목록에 `tst_paths`와 `tst_file_ops`가 빠져 있었다. 그리고 "관문" 절이 Q1·Q2a만 말하고 통합 시험 단계가 없어서, Q2b와 "릴리스로 통합 시험 한 번(`XGS_INTEGRATION_AUDIO`는 켜지 않음)"을 한 문장 더했다.

## 확인하지 못한 것

- **다른 드라이브로 옮기기.** 이 PC에는 드라이브가 하나다. 출력 폴더가 작업 폴더와 다른 드라이브일 때 `fs::moveFile`(`MOVEFILE_COPY_ALLOWED`, 복사 뒤 지움)이 맞게 도는지, 도중에 실패하면 무엇이 남는지 시험하지 않았다.
- **Windows 10.** 지원 대상이지만 Windows 11에서만 돌렸다. `PROC_THREAD_ATTRIBUTE_JOB_LIST`(Windows 10부터)와 긴 경로 처리가 Windows 10에서도 같은지 보지 않았다.
- **백신이 파일을 쥐고 있을 때.** controller의 기록은 이 PC의 백신(V3 Lite)이 새 파일을 쥐었을 가능성을 적었지만, 백신이 막 옮긴 파일을 쥐었을 때 `moveFile`·`writeBytes`·`removeQuietly`가 어떻게 되는지는 시험하지 않았다. 아래 흔들림의 원인일 수 있으나 확인하지 못했다.
- **`tst_local_folder_sink`의 설명하지 못한 실패 두 번.** (1) Task 7의 첫 릴리스 `ctest`에서 빌드 직후 한 번 실패했다. 출력이 남지 않아 어느 시험인지 모른다. controller가 그 뒤 90번(6개 병렬 × 15) 돌렸을 때 실패 0이었다(Task 7 때의 측정). (2) 이번 관문에서 디버그 실행 파일을 따로 돌렸을 때 `overlappingPutsOfOneNameWithOverwriteAllSucceed`가 한 번 실패했다(위 "테스트"). 덮어쓰기 `put` 160번 중 한 번이 `Stored`가 아니었는데, 시험이 그 답을 찍지 않아 무엇이었는지 모른다. 한 번 다시 돌리니 통과했고, 같은 관문의 두 `ctest`에서도 통과했다. `put`은 잠금으로 한 번에 하나씩 옮기므로 스레드끼리 겹친 것이라기보다 방금 옮긴 파일을 다른 무언가(백신 등)가 잠깐 쥐었을 가능성이 있지만 추측이다.
- **디버그 빌드의 통합 시험.** 실제 도구로는 릴리스 빌드만 돌렸다.
- **오디오를 내려받는 통합 시험** `writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum`. 단언은 다 옮겼지만 돌린 적이 없다(사용자 허락 필요).
- **`downloadsAShortVideoAsM4aWithoutReencoding`**: 단언을 옮기지 않았다(Q2d).
- **정상 종료 뒤 자손이 stdout을 쥐고 있는 경우.** `run`이 그 자손이 끝날 때까지 기다리는 것(Kotlin과 같음)을 시험하지 않았다. 시험 자식은 손자를 띄울 때 핸들을 물려주지 않는다.
- **실제 ffmpeg 자손의 취소.** 취소 때 손자까지 죽는 것은 시험 자식으로만 보았다. yt-dlp가 띄운 ffmpeg로는 보지 않았다(Q2d).
- **콘솔 창.** `opensNoConsoleWindow`는 이 프로세스 트리의 보이는 창 수와 자식의 `GetConsoleWindow()`를 본다. Windows Terminal이 띄운 창은 그 트리 밖이라 보지 못한다. `CREATE_NO_WINDOW`를 빼는 변이는 창이 뜰 수 있어 하지 않았다(Task 2).
- **`ProcessBuilder` 인용의 원인.** 1,421개가 왜 다르게 갔는지(JDK가 명령줄을 어떻게 만드는지)는 JDK 소스를 보지 않는 규칙 때문에 보지 않았다. 관찰한 결과만 적었다.
- **`writeBytes`의 공유 모드.** C++은 `FILE_SHARE_READ`로 연다. JDK가 어떤 공유 모드로 여는지는 관찰하지 않았다. 숨김·시스템 속성이 있는 파일 위에 쓰는 경우도 보지 않았다(아래 "남은 사소한 것").
- **`%TEMP%`가 아주 긴 PC.** `existsSeesFilesAndDirectories`는 `%TEMP%`가 200 단위를 넘으면 건너뛴다. 그런 PC에서는 돌리지 않았다.
- **단언의 같음.** `test-names.ps1`은 이름만 본다. 단언이 Kotlin과 같은지는 각 Task의 구현자와 리뷰어가 보았고 Task 8은 다시 보지 않았다.
- **seed가 적다.** 이번 관문은 1만 건 seed 셋과 10만 건 seed 하나(424242)다. Task 1은 10만 건을 seed 20261120으로, Task 5는 `P` 10만 건을 seed 31337로도 돌렸다고 적었으나 그것은 그 Task의 실행이다.
- **통계는 근사다.** `P`의 "형 어긋난 속성"은 `run.ps1`의 C# 훑기로 센 근사이고, 훑기가 틀린 비율도 따로 찍는다(아래 표). 답의 비교는 정확하다.
- **Q2a·Q1에서 열려 있는 것**은 그대로다(Q2a 보고서의 "확인하지 못한 것").

## 남은 사소한 것

각 Task의 리뷰가 고치지 않고 남긴 작은 지적들이다(controller의 기록). **모두 아직 고치지 않았다.** 이 보고서 다음의 최종 리뷰가 일부를 고칠 수 있다. "최종 리뷰에서 고칠 것"으로 표시된 것은 그렇게 적었다.

- **파일 연산(Task 1, 6).** `WriteFile`이 0바이트를 썼다고 답하면 `writeBytes`가 끝없이 돈다. 숨김·시스템 파일 위에 쓰면 실패한다(Kotlin 동작은 관찰하지 않음). 긴 경로 판정이 전체 경로가 아니라 받은 문자열 길이로 한다(긴 작업 폴더 아래 상대 경로). `fs::exists` 주석이 `Files.exists`와 같다고 하지만 끊어진 링크에서 다르다.
- **프로세스 실행(Task 2).** `WAIT_FAILED`를 `Cancelled`로 답한다. `GetExitCodeProcess`의 실패를 보지 않는다(종료 코드 0으로 읽힘). 읽기 스레드의 `join`이 예외 처리 중에 던질 수 있다(이론상). `E0` 너무 긴 꼴 검사에 단위 시험이 없다(대조만 본다). `PYTHON*` 변수의 대소문자 무시 교체에 시험이 없다. `parallelRuns…`에 쓰지 않는 타이머가 있고 긴 실행의 `Cancelled`를 단언하지 않는다.
- **도구(Task 3, 4).** `tst_tool_locator`의 기대값을 `paths::child`로 만든다(글자 그대로의 비교가 없음), `QDir`을 간접 포함에 기댄다. 가짜 실행기가 `Cancelled`에서 `RunResult`의 기본값에 기댄다. **최종 리뷰에서 고칠 것**: `W st`의 "문턱 근처" 통계가 문턱과 같은 버전도 센다(±1을 따로 세야 한다), 빈 폴더 위에 설치하는 경우의 시험이 없다(그 코드가 `xgs::fs` 대신 `QDir`·`QFileInfo`를 쓴다). 설치의 두 멈춤 길 중 하나만 시험한다, `installYtDlp` 헤더가 Windows 문구의 차이를 말하지 않는다, 시험 주석 하나가 틀렸다("기본 fetch가 네트워크에서 받는다"), `kt::digitValue`를 Nd 범위 전체로 시험하지 않는다.
- **조회(Task 5).** **최종 리뷰에서 고칠 것**: `P rs`의 "형 어긋남" 통계가 보수적이지 않다(훑기가 틀린 것을 빼고 세야 한다), `null` 항목 통계가 id·제목 없는 객체 항목도 센다. `xgs-diff`의 `answerRs`/`answerFx`가 예외를 잡지 않는다(C++이 예상 밖으로 던지면 비교 줄 대신 도구가 멈춘다). 실행이 끝난 뒤 stdout 글을 잠금 없이 읽는 약속이 주석에 없다.
- **태그·출력 폴더(Task 6).** **최종 리뷰에서 고칠 것**: 생성자가 폴더를 만들지 못했을 때 점·공백뿐인 이름의 `put`이 폴더 밖 파일을 바꿀 수 있다(C++만의 길; `put`/`exists`가 폴더가 없으면 실패해야 한다), `existsSeesFilesAndDirectories`가 단언이 실패하면 260 단위 넘는 파일을 남긴다. 옮긴 취소 시험 끝에 덧붙인 단언이 약하다(쓰지 않음과 쓰고 지움을 가리지 못함). 비어 있는 오류 문구가 `알 수 없는 오류` 대신 빈 자세한 내용이 된다. `tag`의 `Cancelled` 검사가 빠른 길이라는 주석이 없다. 닿을 수 없는 "경로가 아님" 실패 문구가 영어다.
- **통합 시험(Task 7).** **최종 리뷰에서 고칠 것**: `Ffprobe::probe`가 던지는 `std::runtime_error`를 시험이 잡지 않는다(ffprobe 한 번의 실패·시간 초과가 실행 전체를 끝내고 임시 폴더를 남긴다). 시간 초과를 "시간 초과"라고 말하지 않는다, 원래 비어 있던 환경 변수를 지운 것으로 되돌린다, `aPercentSign…IsReportedNotSilentlyMisplaced`라는 이름이 하는 것보다 많이 말한다(계획의 이름), ffmpeg 시험 실패 문구에 Kotlin의 문구·MD5가 없다.
- **경로 시험(Task 1).** `tst_file_ops`의 쓰기 도우미가 도우미 안에서 `QVERIFY`를 쓴다, `pathsLongerThan260Work`는 `%TEMP%`가 238 단위를 넘으면 길이가 음수가 되고 이른 실패 때 폴더가 남는다, `fileNameIsTheLastPart`에 `ERR` 행이 없고 "two dots"라는 행에 점이 하나다.
- **README와 실행 결과의 작은 어긋남**(Task 8이 이번에 고친 둘 말고는 없음).

## Q2a가 넘긴 것은 어떻게 됐나

Q2a 보고서의 "Q2b에 넘길 것"에 대해:

- **경로를 정리해 넘길 책임** → `xgs::paths`가 Kotlin `Path.of`·`resolve`처럼 정리한다(위 관찰 1, 대조 `W pn pc pf px`). 이식 코드는 경로 문자열을 이것으로만 만든다. UNC와 `\\?\`도 Kotlin처럼 받거나 거절한다. 사용자가 고른 폴더를 이것으로 바꿔 넘기는 일은 Q2d·화면 쪽의 몫이다.
- **명령 인자의 인용** → `windowsCommandLine`과 `argumentsReachTheChildVerbatim`(위 6번). `.cmd`·`.bat`은 실행하지 않는다(7번).
- **출력 폴더의 `%`** → 실제 yt-dlp로 관찰했고 이스케이프 함수를 더했다(13번). 부르는 것은 Q2d.
- **태그 파일 쓰기** → BOM 없는 UTF-8, 짝 없는 서로게이트는 Kotlin처럼 실패(관찰 2).
- **stderr 줄 나누기** → `LineSplitter`가 Kotlin과 같이 나눈다(관찰 3, `W ln`).
- **명령 조립의 예외, 동시 다운로드 수, `parseVideoInfo`의 바이트** → 조회·태그 쓰기는 예외를 결과로 바꿨다(14번). 내려받기 쪽은 Q2d에 그대로 넘긴다.

## Q2c에 넘길 것

- **실제 HTTP 내려받기를 `Fetch`에 끼우기.** `ToolManager`는 `Fetch`(`url`, 멈춤 토큰 → `Outcome<QByteArray>`)를 받는다. Q2c는 `YtDlpUrl`(GitHub의 최신 `yt-dlp.exe`)을 받는 구현을 만든다. 실패 문구는 Kotlin `IOException`의 문구 자리(`yt-dlp 다운로드에 실패했습니다: ` 뒤)에 들어가고, 멈춤 요청이면 취소를 돌려줘야 한다(`installIsCancelledWhenTheFetchIsCancelled`가 이 약속을 본다). 크기 검사(1,000,000바이트 미만이면 실패)와 받은 파일의 확인 실행은 이미 `ToolManager`에 있다.
- **LRCLIB.** 미룬 통합 시험 `downloadsAShortVideoAsM4aWithoutReencoding`은 LRCLIB 조회도 쓴다(아래 Q2d와 함께).

## Q2d에 넘길 것

- **미룬 통합 시험의 단언.** `downloadsAShortVideoAsM4aWithoutReencoding`의 단언(Kotlin `RealYtDlpIntegrationTest.kt` 210~240행)을 내려받기 작업(`DefaultDownloadService`, `ItemDownloader`)과 함께 옮긴다. 이 시험은 오디오를 내려받고 LRCLIB에 요청하므로 돌리기 전에 사용자의 허락이 필요하다.
- **`%` 이스케이프 적용.** `downloadCommand`를 부르기 전에 출력 폴더에 `escapeOutputTemplateDirectory`를 쓴다. `downloadCommand`는 Kotlin 그대로라 스스로 하지 않는다.
- **멈춘 뒤의 `status()`.** 멈춤을 요청한 뒤 얻은 `ToolsStatus`는 화면에 보이지 말고 버린다(9번).
- **도구 경로는 전체 경로로.** 실행기는 `PATH`를 찾지 않고 `.exe`도 붙이지 않는다(8번). `ToolLocator`가 찾은 경로를 그대로 넘긴다.
- **취소 중 실제 ffmpeg 자손 정리.** yt-dlp가 ffmpeg를 띄운 상태에서 취소해 프로세스 트리 전체가 죽는지 실제 도구로 확인한다.
- **정상 종료 뒤 stdout을 쥔 자손.** 그런 자손이 있으면 `run`이 기다린다(Kotlin과 같음). 실제 yt-dlp·ffmpeg에서 그런 일이 있는지, 있다면 시간 한도를 둘지 정한다.
- **작업 폴더.** 내려받은 파일을 어디(임시 폴더, 출력 폴더와 같은 드라이브인지)에 두고 `LocalFolderSink::put`으로 옮길지. 다른 드라이브면 복사 뒤 지우기가 되고 이것은 시험하지 않았다.
- **명령 조립 예외를 곡 하나의 실패로.** `downloadCommand`(영상 ID 거절, yt-dlp 없음)의 `std::invalid_argument`를 잡아 그 곡만 실패로(Q2a에서 넘어온 것, 그대로).
- **NaN 백분율.** 진행률 파싱이 NaN을 남길 수 있다(Q2a에서 넘어온 것, 그대로).
- **동시 다운로드 수와 겹치는 `put`.** `automaticConcurrency`가 Kotlin과 같은 값인지(Q2a에서 넘어온 것), 여러 곡이 같은 이름으로 동시에 `put`할 때의 동작(위 흔들림과 함께 보면 좋다).
- **마지막 옮기기 중 취소.** C++은 옮기기가 성공했으면 `Tagged`다(10번). 작업 쪽이 이 답을 취소보다 앞세워 다루는지 정한다.

## 사용자가 정할 것

지금 코드는 아래 "지금"대로 동작하고, 관문은 그 동작으로 통과했다.

1. **오디오를 내려받는 통합 시험을 돌릴지.** `writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum`은 19초짜리 영상("Me at the zoo")의 오디오 약 300KB를 내려받는다(Task 7의 추정). 허락하면 그 시험 하나만 `XGS_INTEGRATION=1`과 `XGS_INTEGRATION_AUDIO=1`로 한 번 돌린다(유튜브 요청 한 번). 지금: 돌리지 않음.
2. **새 한국어 문구 둘의 표현.** `배치 파일(.cmd, .bat)은 실행하지 않습니다`, `실행할 프로그램이 없습니다`(7번). Kotlin에 짝이 없다. 지금: 그 문구.
3. **점·공백뿐인 파일 이름을 거절할지.** Kotlin은 `...`, `.. `를 받고 Windows가 그것을 폴더 자신이나 부모로 푼다(고치지 않은 동작 7). 지금: Kotlin처럼 받음.
4. **따른 Kotlin 동작을 Qt 판에서 고칠지.** id가 빈 항목이 쓸 수 있음(1), `[Private video]`·`availability`의 대소문자 구분(2), 빈 채널이 업로더로 대체되지 않음(3), ffmpeg가 아무 첫 줄에나 "찾음"(5), 업데이트의 마지막 줄이 스레드 시간에 달림(6). 고치면 Kotlin과 다르게 하는 문서화된 차이가 되고 대조에서 그 구역을 빼야 한다. 지금: 모두 Kotlin처럼.
5. **`QProcess`를 쓰지 않은 것(작성자 결정 1).** 설계 §3과 다르다. 지금: `CreateProcessW` + Job Object.
6. **실행 중에 정해진 다른 차이들**(위 9~15번: 멈춘 뒤의 `status()`, 마지막 옮기기 중 취소의 답, 던지지 않는 생성자, 이스케이프 함수, 예외를 결과로, 2초 장치)을 받아들일지.
7. **`tst_local_folder_sink`의 흔들림을 더 볼지.** 원인을 찾으려면 그 시험이 실패한 답(Windows 문구)을 찍게 고치고 많이 되풀이해 보는 일이 필요하다. 지금: 두 번 보였고 원인 모름.
8. Q2a 보고서의 "사용자가 정할 것"(배열 깊이 한계, 영상 ID 규칙, 전각·CJK 괄호 등)과 Q1에서 남은 결정은 그대로 기다린다.

## 라이선스 메모

이 절은 과정의 기록이고 법적 의견이 아니다. 동작은 Kotlin 원본과 테스트, 공개 문서(Microsoft의 명령줄 인자 규칙, Win32 API 문서, Qt 문서), 그리고 오라클·탐침이 공개 API로 관찰한 답으로 정했다(위 관찰 표). Task 1의 보고서는 JDK 소스를 보거나 기억에서 옮기지 않았다고 적었다. 다른 Task의 보고서에는 같은 문장이 따로 없고, 모두 같은 규칙(계획의 Global Constraints) 아래에서 일했다. Task 8도 JDK와 `kotlinx.serialization`의 소스를 보지 않았다. 금지 호출 점검의 JDK 내부 이름 검사는 이번 관문에서 0건이다. `THIRD-PARTY-NOTICES.md`는 이 단계에서 바뀌지 않았다.

## 판정

**Q2b 관문(이 계획의 몫)은 이번 실행에서 충족됐다.** 이식 대상 Kotlin 단위 테스트 60개는 같은 이름으로 통과하고(이름 대조에서 빠진 것 0, 22개 클래스), 깨끗한 릴리스·디버그 빌드에서 `ctest` 30개가 모두 통과했으며, 금지 호출은 0건, 대조는 다섯 번의 실행에서 26개 종류 모두 불일치 0과 통계 문턱 통과, 디버그 도구는 릴리스와 같았고, 실제 도구 통합 시험은 ffmpeg 8개와 yt-dlp 5개가 통과했다(2개는 건너뜀). 관문 사이에 C++을 고치지 않았다.

이 진술이 다루지 않는 것: 오디오를 내려받는 통합 시험과 Q2d로 미룬 통합 시험, 다른 드라이브, Windows 10, 디버그 빌드의 통합 시험, 실제 HTTP·내려받기 작업·실제 취소 흐름(Q2c·Q2d), 그리고 원인을 모르는 `tst_local_folder_sink`의 흔들림(관문 단계인 두 `ctest`에서는 통과했지만 따로 돌린 디버그 실행에서 한 번 실패했다). 사용자가 정할 것은 위에 모았다.

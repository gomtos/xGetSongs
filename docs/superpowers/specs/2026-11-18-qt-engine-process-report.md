# Qt Q2b(프로세스 실행·도구·yt-dlp 조회·태그 쓰기·출력 폴더) 보고서

작성일: 2026-11-20 (같은 날 최종 리뷰를 반영해 다시 냄)
설계: [2026-11-15-qt-rewrite-and-playlists-design.md](2026-11-15-qt-rewrite-and-playlists-design.md) §3, §4(Q2 행)
계획: [2026-11-18-qt-engine-process.md](../plans/2026-11-18-qt-engine-process.md)
이전 단계: [2026-11-15-qt-engine-logic-report.md](2026-11-15-qt-engine-logic-report.md)(Q2a)

환경: Windows 11 Home 10.0.26200, Visual Studio 2022(vcvars64), Qt 6.11.3, 정답 쪽 JVM은 `server/build/sidecar-image`의 OpenJDK 21.0.9와 그 안의 `engine.jar`·`shared-jvm.jar`. Gradle은 돌리지 않았다(`run.ps1`의 오래된 오라클 검사가 다섯 번 모두 통과했다). 시험이 띄운 자식 프로세스는 모두 `CREATE_NO_WINDOW`로 떠서 창이 뜨지 않았다. 실행 중인 xGetSongs 앱은 건드리지 않았고 `%APPDATA%\xGetSongs`에는 아무것도 쓰지 않았다(거기 있는 `yt-dlp.exe`를 실행만 했다).

실제 도구(Task 7이 확인한 것): yt-dlp 2026.08.19(`%APPDATA%\xGetSongs\bin\yt-dlp.exe`), ffmpeg·ffprobe 8.1(`PATH`에 있는 WinGet Gyan.FFmpeg), node v24.11.1(deno 없음). 관문 실행은 같은 곳에서 도구를 찾았지만 버전을 다시 찍지는 않았다.

**관문은 두 번 돌았다.** 처음은 Task 8(이 보고서의 첫 판)이고, 두 번째는 최종 리뷰가 고치라고 한 것들을 고친 뒤(아래 "최종 리뷰 뒤에 고친 것")다. 이 문서의 "관문 표"와 "이번 관문 실행"이라고 적은 숫자는 **두 번째 실행**의 원본 출력(저장소 밖 작업 폴더 `gate2`, 단계마다 파일 하나)에서 옮겼다: 깨끗한 릴리스·디버그 빌드, 두 빌드의 `ctest`, 실행 파일마다의 결과, 실제 도구 통합 시험 한 번, 두 점검 스크립트, 대조 다섯 번. 첫 번째 실행의 숫자는 그렇다고 적은 곳에만 있다("Task 8의 관문"). 그 밖의 숫자(관찰 표, 변이 시험, 되풀이 실행 횟수, Task별 대조 통계)는 각 Task가 자기 작업 중에 잰 것이고 그 자리마다 "Task N"이라고 적었다. 최종 리뷰 뒤의 수정 작업이 잰 것은 "수정 작업"이라고 적었다.

네트워크: 두 번째 관문 실행에서 유튜브에 요청을 보낸 것은 통합 시험 `tst_real_ytdlp`를 **한 번** 돌린 것뿐이다(첫 번째 관문도 한 번이었다). 그 한 번이 yt-dlp를 다섯 번 띄운다: 재생목록 하나는 `--flat-playlist`로 목록만 받고, 영상 넷(`kcx0a2OAhN0`, `7mDDM0eBWR0`, `jNQXAC9IVRw` 두 번)은 `--skip-download`로 정보 파일(`.info.json`)과 썸네일(`.jpg`)만 받는다. 오디오는 내려받지 않았다. 수정 작업은 그 밖에 유튜브에 요청하지 않았고 `yt-dlp -U`(자기 업데이트)도 어디서도 돌리지 않았다.

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

op 이름을 한 줄씩 풀면:
- `pn`(path normalize): 사용자가 준 경로 글을 Kotlin의 `Path.of`처럼 정리한 답.
- `pc`(path child): 폴더 경로 뒤에 이름 하나를 이은 경로.
- `pf`(path file name): 경로의 마지막 부분(파일 이름).
- `px`(path without extension): 파일 이름에서 마지막 점과 확장자를 뗀 것.
- `ln`(lines): 외부 프로그램이 내보낸 바이트를 줄로 나눈 결과.
- `st`(status): yt-dlp·ffmpeg·js 런타임을 찾았는지와 그 버전.
- `up`(update): yt-dlp 업데이트를 부탁했을 때 사용자에게 보일 결과 문구.
- `fn`(file name): 출력 폴더가 그 파일 이름을 받는지(1) 거절하는지(0).
- `rs`(resolve): 재생목록·영상 주소를 yt-dlp로 풀어 만든 곡 목록이나 실패 문구.
- `fx`(fetch): 영상 하나의 제목·채널·가수·곡명.

## 요약

- **두 번째 관문 실행**(최종 리뷰 뒤의 수정을 커밋한 코드로)에서 한 단계를 빼고 모두 통과했다: 릴리스·디버그 깨끗한 빌드(컴파일러 경고 0), 릴리스 `ctest` 30개 모두 통과, 실행 파일마다 돌린 결과 두 빌드 모두 1061 통과·0 실패, 이름 대조에서 빠진 Kotlin 이름 0(22개 클래스, 일부러 더한 이름 36), 금지 호출 0, 대조 다섯 번 모두 26개 종류 불일치 0과 통계 문턱 통과, 디버그 도구가 릴리스와 26개 종류 모두 같음, 실제 도구 통합 시험은 ffmpeg 쪽 8개 통과, yt-dlp 쪽 5개 통과·2개 건너뜀.
- **통과하지 못한 한 단계: 디버그 `ctest`에서 `tst_system_process_runner`가 한 번 실패했다**(30개 중 29개 통과). `ctest` 출력에 Qt Test의 줄이 남지 않아 어느 시험 함수가 왜 실패했는지 모른다. 그 실행 파일은 다른 실행에서 25~45초였는데 이때 190.91초 걸렸다. 그 실행 파일만 한 번 다시 돌리니 19개 모두 통과했다. 아래 "테스트"와 "확인하지 못한 것". 보고서를 낸 뒤(재리뷰 중)에 디버그 실행 파일을 Qt Test 출력을 보관하며 30번 더 돌렸는데 30번 모두 통과했다(27~32초). 재리뷰는 코드를 읽고 두 가지 가능성을 냈으나 가리지 못했다: (가) 방금 빌드한 자식 실행 파일의 시작이 줄줄이 늦어져 시험의 시간 한도에 걸린 것(제품 코드는 옳음), (나) 취소 뒤 파이프 읽기가 풀리지 않아 60초짜리 자식이 스스로 끝날 때까지 기다린 것(제품 코드의 멈춤: 실제 yt-dlp라면 끝나지 않을 수 있다). **원인은 모른다.** Q2d는 취소를 이 실행기 위에 올리므로, Q2d의 첫 일은 `ctest`가 Qt Test 출력을 파일로 남기게 하고 이 시험을 다시 반복하는 것이다.
- **최종 리뷰 뒤에 고친 것**: 파일 옮기기의 짧은 재시도(Kotlin과 다름), 출력 폴더가 디렉터리가 아닐 때의 `put` 실패(고치기 전에는 폴더 밖 파일이 바뀌는 길이 있었다), NUL이 든 명령 거절, 대기 실패를 취소로 답하지 않기, 0바이트 쓰기의 끝없는 되풀이, 멈춤 장치를 `CancelIoEx`로, 빠졌던 시험들(작성자 결정 5, 빈 폴더 위 설치, 2초 장치), 대조 통계 셋. 아래 "최종 리뷰 뒤에 고친 것".
- **`tst_local_folder_sink`의 흔들림**(Task 7과 Task 8에서 한 번씩)은 재현되지 않았다: 다른 구성을 깨끗이 다시 빌드하는 동안 239번 돌려 실패 0. 원인은 모른다. 아래 그 절.
- 옮긴 것: 결과 값과 경로 문자열·파일 연산, 프로세스 실행(Job Object 안에서 자식을 낳고 취소 때 프로세스 트리 전체를 죽인다), 도구 찾기, 도구 상태와 yt-dlp 설치·업데이트, yt-dlp 조회, m4a 태그 쓰기, 출력 폴더. Kotlin 단위 테스트 60개(3 + 6 + 15 + 17 + 12 + 6 + 1)가 같은 이름으로 통과한다. 통합 시험 이름 13개도 모두 있지만, 그중 11개만 돌았다(오디오를 내려받는 하나는 따로 켜야 돌고, 하나는 Q2d로 미뤘다; 아래 "계획과 달라진 것").
- **Kotlin과 다르게 한 것**은 계획 작성자가 정한 다섯과 실행 중에 더해진 것들이다(아래). 가장 눈에 띄는 것: Qt 판은 인자를 자식에 글자 그대로 넘긴다. Kotlin의 `ProcessBuilder`는 무작위 인자 목록 2,000개 중 1,421개를 다르게 넘겼다(Task 2의 관찰).
- 이 진술 밖에 있는 것: 실제 HTTP 내려받기(Q2c), 내려받기 작업·동시성·실제 취소 흐름(Q2d), 다른 드라이브로 옮기기, Windows 10, 디버그 빌드의 통합 시험, 오디오를 내려받는 통합 시험 하나, 아래 "확인하지 못한 것".

## 관문 표

### 빌드

`build/qt/vcpkg_installed`, `build/qt-debug/vcpkg_installed`, `C:\dev\vcpkg`는 지우지 않았다. VS 2022 vcvars64 안에서(`VCPKG_ROOT=C:\dev\vcpkg`) 빌드마다 `cmake --preset <프리셋> -S qt`, `cmake --build <폴더> --target clean`, `cmake --build <폴더>`를 이어 돌렸다(릴리스 다음 디버그). 커밋한 코드 그대로였다(작업 트리에 바뀐 파일 없음, 원본 `00-head.txt`). 둘 다 성공했다(원본: `02-build-release.txt`, `03-build-debug.txt`; 두 빌드 합해 3분 30초).

| 항목 | 릴리스(`qt-release`, `build/qt`) | 디버그(`qt-debug`, `build/qt-debug`) |
|---|---|---|
| 구성 | `Configuring done (12.4s)`, `Generating done (1.1s)` | `Configuring done (12.9s)`, `Generating done (1.1s)` |
| clean | `Cleaning... 131 files.` | `Cleaning... 131 files.` |
| 빌드 단계 | `[1/165]`~`[165/165]` 모두 실행 | `[1/165]`~`[165/165]` 모두 실행 |
| 경고 | 컴파일러 경고 0(`/W4 /WX`). 출력의 경고는 Qt의 CMake 매크로(`Qt6CoreMacros.cmake`)가 구성 때 낸 `CMake Warning` 하나뿐 | 같음 |

Qt는 다시 빌드되지 않았다.

### 테스트

릴리스 `ctest --test-dir build/qt --output-on-failure`: `100% tests passed, 0 tests failed out of 30`(Total 48.32 sec, 원본 `04-ctest-release.txt`).

디버그 `ctest --test-dir build/qt-debug --output-on-failure`: **`97% tests passed, 1 tests failed out of 30`**(Total 243.89 sec, 원본 `05-ctest-debug.txt`). Qt의 단언과 `QString`/`QList` 범위 검사가 켜진 빌드다. 실패한 것은 `tst_system_process_runner`이고 190.91초가 걸렸다(같은 실행 파일이 릴리스 `ctest`에서는 36.37초, 아래 실행 파일마다 돌린 단계의 디버그에서는 31.4초). `ctest`의 출력에는 Qt Test의 줄이 하나도 남지 않아(이 환경에서 Qt Test 실행 파일은 콘솔에 아무것도 찍지 않는다) **어느 시험 함수가 왜 실패했는지 모른다.** 지시대로 C++을 고치지 않고 그 실행 파일만 **한 번** 다시 돌렸다(`-o <파일>,txt`로 결과를 남기게): `Totals: 19 passed, 0 failed`, 28.8초(원본 `05b-rerun-debug-tst_system_process_runner.txt`). 아래 "확인하지 못한 것"에 적었다.

두 `ctest`는 `XGS_INTEGRATION` 없이 돌렸다. 그래서 통합 시험 실행 파일 둘은 ctest에서 `Passed`로 나오지만 안의 시험 함수는 모두 건너뜀(`SKIP`)이다(아래 표의 2 통과는 `initTestCase`와 `cleanupTestCase`).

그다음 실행 파일마다 `-o <파일>,txt`로 `Totals:` 줄을 얻고 `-functions`로 시험 함수를 셌다(원본 `06-exes-release.txt`, `07-exes-debug.txt`와 실행 파일마다의 출력). 이번 단계에 생기거나 바뀐 실행 파일(릴리스):

| 실행 파일 | 통과 | 실패 | 건너뜀 | 시험 함수 | 그중 |
|---|---|---|---|---|---|
| `tst_paths` | 270 | 0 | 0 | 7 | Kotlin 짝 없음. 오라클 관찰 행을 데이터 표로(Task 1) |
| `tst_file_ops` | 12 | 0 | 0 | 10 | Kotlin 짝 없음(계획의 7 + Task 6이 더한 `existsSeesFilesAndDirectories` + 최종 리뷰 뒤의 옮기기 재시도 둘) |
| `tst_line_splitter` | 8 | 0 | 0 | 6 | Kotlin 짝 없음 |
| `tst_command_line` | 4 | 0 | 0 | 2 | Kotlin 짝 없음 |
| `tst_system_process_runner` | 19 | 0 | 0 | 17 | `SystemProcessRunnerTest` 3 + 추가 14. 27.2초 |
| `tst_tool_locator` | 8 | 0 | 0 | 6 | `ToolLocatorTest` 6 |
| `tst_tool_manager` | 22 | 0 | 0 | 20 | `DefaultToolManagerTest` 15 + 추가 5 |
| `tst_ytdlp_resolver` | 23 | 0 | 0 | 21 | `YtDlpResolverTest` 17 + 추가 4 |
| `tst_ffmetadata_reader` | 3 | 0 | 0 | 1 | `FfmetadataReaderTest` 1 |
| `tst_m4a_tagger` | 16 | 0 | 0 | 14 | `M4aTaggerTest` 12 + 추가 2 |
| `tst_local_folder_sink` | 10 | 0 | 0 | 8 | `LocalFolderSinkTest` 6 + 추가 2 |
| `tst_ytdlp_commands` | 14 | 0 | 0 | 12 | Q2a의 11 + 이번 단계의 추가 1(`escapeOutputTemplateDirectoryDoublesEveryPercentSign`) |
| `tst_real_ffmpeg_tagging` | 2 | 0 | 8 | 8 | `RealFfmpegTaggingIntegrationTest` 8(변수 없이 모두 건너뜀) |
| `tst_real_ytdlp` | 2 | 0 | 7 | 7 | `RealYtDlpIntegrationTest` 5 + 추가 2(변수 없이 모두 건너뜀) |
| Q2a까지의 실행 파일 16개 | 648 | 0 | 0 | 381 | Q2a 보고서의 표와 같은 수 |
| **합계(30개)** | **1061** | **0** | **15** | **520** | |

520 = Kotlin 이름 423(Q1 193 + Q2a 157 + 이번 단계의 단위 시험 60과 통합 시험 13) + 일부러 더한 이름 36(Q1 3 + Q2a 3 + 이번 단계 30) + Kotlin 짝 없는 실행 파일 여덟의 함수 61(`tst_smoke` 1, `tst_kt_text` 22, `tst_models` 5, `tst_json` 8, `tst_paths` 7, `tst_file_ops` 10, `tst_line_splitter` 6, `tst_command_line` 2). Task 8의 관문에서는 1051 통과, 510 함수였다. 더한 10은 최종 리뷰 뒤의 시험이다.

**디버그 실행 파일마다의 결과**: 30개 모두 종료 코드 0, 합계 1061 통과, 0 실패, 15 건너뜀, 함수 520(`tst_system_process_runner`도 19 통과, 31.4초). (Task 8의 관문에서는 이 단계에서 `tst_local_folder_sink`가 한 번 실패했다; 아래 "`tst_local_folder_sink`의 흔들림".)

### 실제 도구 통합 시험 (`XGS_INTEGRATION=1`, 릴리스, 한 번)

`XGS_INTEGRATION_AUDIO`는 켜지 않았다(그 변수가 켜지면 오디오를 내려받는 시험이 돈다; 사용자가 허락하지 않았다).

| 실행 파일 | 결과(원본) | 내용 |
|---|---|---|
| `tst_real_ffmpeg_tagging` | `Totals: 10 passed, 0 failed, 0 skipped`, 3620 ms, 종료 코드 0(`10-integration-ffmpeg.txt`) | Kotlin 8개 모두 통과. 네트워크를 쓰지 않는다(무음 m4a와 빨간 그림을 ffmpeg로 만들어 태그를 쓰고 ffprobe로 읽는다) |
| `tst_real_ytdlp` | `Totals: 7 passed, 0 failed, 2 skipped`, 29284 ms, 종료 코드 0(`11-integration-ytdlp.txt`) | 통과 5 = Kotlin 3(재생목록 조회, 앨범 읽기, 설명란 가사) + 더한 2(공백·한글·`&` 폴더, `%` 폴더). 건너뜀 2 = 오디오를 내려받는 `writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum`(`XGS_INTEGRATION_AUDIO=1`이 있어야 돈다), Q2d로 미룬 `downloadsAShortVideoAsM4aWithoutReencoding` |

통과 수의 10과 7은 시험 수에 `initTestCase`·`cleanupTestCase`를 더한 것이다. yt-dlp 쪽은 한 번만 돌렸고 다시 돌린 적은 없다(네트워크 실패가 없었다). 출력에 찍힌 것: 재생목록 100항목, `kcx0a2OAhN0`의 앨범 `Love poem`, `7mDDM0eBWR0` 설명란 80줄에서 가사 54줄(빈 줄이 아닌 것 42줄, 606자), 영상마다 작업 폴더에 남은 것은 `.info.json`과 `.jpg` 둘뿐. 끝에 시험용 캐시·임시 폴더가 비어 있었다(`the sandbox held: cache [], tmp []`).

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
SystemProcessRunnerTest: Kotlin 3, Qt 17, missing 0, added 14 (argumentsReachTheChildVerbatim, closesStandardInput, setsThePythonEncodingVariables, aMissingExecutableIsAStartFailure, aBatchFileIsNotExecuted, cancellingKillsGrandchildrenToo, withoutAJobTheGrandchildSurvives, parallelRunsDoNotHoldEachOthersPipes, aStopRequestedBeforeTheStartRunsNothing, opensNoConsoleWindow, aGrandchildLeftRunningAfterANormalExitIsKilled, aNulInTheCommandIsAStartFailure, aStopReturnsWhenAProcessOutsideTheJobHoldsThePipe, aThrowingHandlerKillsTheProcessAndRethrows)
ToolLocatorTest: Kotlin 6, Qt 6, missing 0, added 0
DefaultToolManagerTest: Kotlin 15, Qt 20, missing 0, added 5 (installReplacesAnEmptyDirectoryAtTheTarget, aRunThatFailsAfterItsStartIsNotACancel, installIsCancelledWhenTheFetchIsCancelled, cancelledInstallLeavesNoTemporaryFile, updateIsCancelledWhenTheRunIsCancelled)
YtDlpResolverTest: Kotlin 17, Qt 21, missing 0, added 4 (aCancelledRunIsCancelledAndFetchesNothing, aRunThatFailsAfterItsStartIsAFailureNotACancel, aPlaylistOfExactly999IsNotTruncated, resolvesAFiveThousandEntryPlaylistQuickly)
FfmetadataReaderTest: Kotlin 1, Qt 1, missing 0, added 0
M4aTaggerTest: Kotlin 12, Qt 14, missing 0, added 2 (aStartFailureOfFfmpegIsAFailureAndKeepsTheOriginal, aRunOfFfmpegThatFailsAfterItsStartIsAFailureAndKeepsTheOriginal)
LocalFolderSinkTest: Kotlin 6, Qt 8, missing 0, added 2 (putFailsWhenTheFolderIsNotADirectory, putWorksWhenTheTargetPathIsLongerThan260)
RealFfmpegTaggingIntegrationTest: Kotlin 8, Qt 8, missing 0, added 0
RealYtDlpIntegrationTest: Kotlin 5, Qt 7, missing 0, added 2 (anOutputFolderWithSpacesKoreanAndAnAmpersandWorks, aPercentSignInTheOutputFolderIsReportedNotSilentlyMisplaced)
PASSED: every Kotlin test name is a Qt test name in 22 classes; only the 36 intended names were added
```

이 스크립트는 이름만 대조한다. 단언이 Kotlin과 같은지는 각 Task의 구현자와 리뷰어가 보았고 Task 8은 다시 보지 않았다. 이름이 있다고 시험이 돈 것은 아니다: `downloadsAShortVideoAsM4aWithoutReencoding`은 이름과 Kotlin의 건너뛰기 조건만 있고 단언은 Q2d에서 옮긴다.

### 금지 호출 (`pwsh qt/tools/forbidden-calls.ps1`, 종료 코드 0, 원본 `13-forbidden-calls.txt`)

```
looked at 56 files under qt/core for the Q1 list (kt_text.* and kt_tables.inc excluded), 59 for the Q2a list, and 116 text files under qt/ (qt/spike excluded)
PASSED: 0 hits
```

목록은 Q2a와 같다(Qt의 문자·대소문자 함수, Qt의 JSON 클래스, Qt의 수 변환, 경로 정리 함수 `cleanPath`·`absoluteFilePath` 등, 그리고 `qt/`의 모든 텍스트 파일에서 JDK 내부 이름).

### 대조 (`pwsh qt/tools/differential/run.ps1`)

오라클과 비교하는 네 번의 실행과 디버그 도구 자체 점검 한 번이다. 모두 기본 종류 `UXMSCFDGTLYRNBZHOJEQVKAIWP`(Q1의 13개, Q2a의 11개, Q2b의 `W`·`P`) 26개를 한 번의 호출로 돌렸다. 앞의 셋은 `-PerKind 10000`, 넷째는 `-PerKind 100000 -Seed 424242`다(seed는 Task 8의 관문과 같다). 모두 오래된 오라클 검사를 통과했고 종료 코드 0, 마지막 줄은 `PASSED (kinds UXMSCFDGTLYRNBZHOJEQVKAIWP, <건수> cases per case kind, seed <seed>)`다(원본: `14-diff-10000-seed20261115.txt`, `15-diff-10000-seed1.txt`, `16-diff-10000-seed987654321.txt`, `18-diff-100000-seed424242.txt`). 칸은 "사례 수(표 종류는 코드 포인트·단위·상수 수), 불일치 수"다.

| 종류 | 10000 seed 20261115 | 10000 seed 1 | 10000 seed 987654321 | 100000 seed 424242 |
|---|---|---|---|---|
| U, B, Z | 1112064 코드 포인트씩, 0 | 같음, 0 | 같음, 0 | 같음, 0 |
| X | 65536 단위, 0 | 65536, 0 | 65536, 0 | 65536, 0 |
| M | 11 상수, 0 | 11, 0 | 11, 0 | 11, 0 |
| S C F D G T L Y R N H O J E Q V K A I (19개) | 각 10000건, 0 | 각 10000, 0 | 각 10000, 0 | 각 100000, 0 |
| **W** | 10000건, 0 | 10000, 0 | 10000, 0 | 100000, 0 |
| **P** | 10000건, 0 | 10000, 0 | 10000, 0 | 100000, 0 |
| **불일치 합계** | **0** | **0** | **0** | **0** |
| 걸린 시간 | 2분 42초(17:34:58~17:37:40) | 2분 56초 | 2분 38초 | 15분 47초(17:48:23~18:04:10) |

모든 실행에서 통계 문턱을 넘었다. 실행마다 `(needs …)`가 붙은 통계 줄이 248개 있고(보여 주기만 하는 줄 포함; Task 8의 관문보다 둘 많은 것은 새로 보여 주는 `W st`의 "문턱과 같은 버전"과 `P rs`의 "읽힌 stdout" 줄이다), 그중 문턱이 0보다 큰 228개가 모두 문턱 이상이다. 요약 스크립트(`19-stats-summary.txt`)는 247개로 센다: 스크립트의 줄 꼴이 `<수> of <수> = …%`인데 `I`의 `cases with Σ, σ or ς: 0 (needs 0)` 한 줄만 그 꼴이 아니어서 빠진다(Task 8의 246과 245도 같은 까닭이다). 문턱은 낮추지 않았다. `R`·`N`의 누설 방향 진단(C++ 답이 Kotlin 답보다 덜 가린 사례)은 모든 실행에서 0이다. 측정값을 문턱으로 나눈 값이 가장 작은 셋:

| 실행 | 가장 빠듯한 통계 (측정값, 문턱) |
|---|---|
| 10000, seed 20261115 | R 가짜 경로만 든 입력의 Kotlin 답에 `<경로>` 없음 97.2% (90%), P `rs` `OK` 43.3% (40%), V 길이 있음 32.6% (30%) |
| 10000, seed 1 | R 96.7% (90%), V 길이 있음 32.9% (30%), I op `norm`의 몫 16.5% (15%) |
| 10000, seed 987654321 | W `pc` 성공 63.7% (60%), P `rs` `OK` 42.8% (40%), R 96.7% (90%) |
| 100000, seed 424242 | V 길이 있음 32.4% (30%), P `rs` `OK` 43.2% (40%), R 97.3% (90%) |

**`W`·`P`의 op별 통계**(문턱이 있는 것; seed 20261115 1만 건과 seed 424242 10만 건, `run.ps1` 출력에서 옮김. 분모는 그 op의 사례 수다. 다른 두 seed도 모두 문턱을 넘었다, 원본 `19-stats-summary.txt`). `W`의 op별 사례 수와 값은 Task 8의 관문과 다르다: `W st`의 생성기를 고쳐서(위 "최종 리뷰 뒤에 고친 것") 같은 seed에서도 `W`의 무작위 순서가 바뀌었기 때문이다. `P`의 생성기는 그대로라 `P`의 사례는 Task 8과 같고, 바뀐 것은 통계 두 줄의 세는 법이다.

| op | 통계 | 문턱 | 1만 건 seed 20261115 | 10만 건 seed 424242 |
|---|---|---|---|---|
| `pn` | `ERR` | 10% | 410/1256 32.6% | 3994/12312 32.4% |
| `pn` | 입력과 다른 답 | 20% | 371/1256 29.5% | 3531/12312 28.7% |
| `pn` | 입력과 같은 답 | 20% | 475/1256 37.8% | 4787/12312 38.9% |
| `pc` | 경로인 답(`ERR` 아님) | 60% | 840/1247 67.4% | 8319/12602 66.0% |
| `pf` | 없음(`-`) | 5% | 93/1245 7.5% | 1092/12667 8.6% |
| `pf` | 점이 둘 이상인 이름 | 10% | 273/1245 21.9% | 2645/12667 20.9% |
| `px` | 빈 답 | 5% | 167/1281 13.0% | 1713/12251 14.0% |
| `px` | 점이 둘 이상인 이름의 답 | 10% | 259/1281 20.2% | 2459/12251 20.1% |
| `ln` | `\r\n`이 든 입력 | 20% | 932/1221 76.3% | 9497/12673 74.9% |
| `ln` | 홀로 선 `\r` | 20% | 801/1221 65.6% | 8221/12673 64.9% |
| `ln` | 홀로 선 `\n` | 20% | 877/1221 71.8% | 9422/12673 74.3% |
| `ln` | UTF-8이 아닌 입력 | 10% | 953/1221 78.1% | 10051/12673 79.3% |
| `ln` | 줄 끝 없이 끝남 | 30% | 447/1221 36.6% | 4764/12673 37.6% |
| `ln` | 멀티바이트 열 안이나 `\r`·`\n` 사이의 조각 경계 | 10% | 855/1221 70.0% | 8971/12673 70.8% |
| `st` | yt-dlp 찾음 / 못 찾음 | 각 20% | 63.9% / 36.1% (1192건) | 64.0% / 36.0% (12458건) |
| `st` | ffmpeg 찾음 / 못 찾음 | 각 20% | 64.8% / 35.2% | 64.1% / 35.9% |
| `st` | js 런타임 찾음 / 못 찾음 | 각 20% | 28.9% / 71.1% | 29.5% / 70.5% |
| `st` | js 버전이 문턱 **바로 위·아래**(새로 세는 법) | 10% | 153/1192 12.8% | 1794/12458 14.4% |
| `st` | (js 버전이 문턱과 같음, 보여 주기만) | - | 102/1192 8.6% | 1048/12458 8.4% |
| `st` | 시작하지 못하는 도구 | 2% | 238/1192 20.0% | 2472/12458 19.8% |
| `up` | `OK` / `ERR` | 각 30% | 49.4% / 50.6% (1244건) | 51.8% / 48.2% (12620건) |
| `up` | yt-dlp가 시작하지 못함 | 2% | 97/1244 7.8% | 955/12620 7.6% |
| `fn` | 답 `1` / `0` | 각 25% | 57.7% / 42.3% (1314건) | 55.5% / 44.5% (12417건) |
| `rs` | `OK` | 40% | 2151/4962 43.3% | 21722/50332 43.2% |
| `rs` | `ERR` | 20% | 2811/4962 56.7% | 28610/50332 56.8% |
| `rs` | 재생목록 실행 | 30% | 2126/4962 42.8% | 21551/50332 42.8% |
| `rs` | 영상 실행 | 20% | 1890/4962 38.1% | 19097/50332 37.9% |
| `rs` | 답에 쓸 수 없는 항목 | 10% | 1178/4962 23.7% | 11798/50332 23.4% |
| `rs` | (stdout을 실제로 읽은 사례, 보여 주기만) | - | 3409/4962 68.7% | 34678/50332 68.9% |
| `rs` | 읽힌 재생목록 stdout의 `entries`에 글자 그대로의 `null` 항목, Kotlin이 받음(새로 세는 법) | 5% | 416/4962 8.4% | 4456/50332 8.9% |
| `rs` | 읽힌 stdout에 형 어긋난 속성, Kotlin이 거절(새로 세는 법) | 10% | 584/4962 11.8% | 5988/50332 11.9% |
| `rs` | (훑기는 형 어긋남을 찾았지만 Kotlin이 받음 = 훑기가 틀림, 보여 주기만) | - | 91/4962 1.8% | 1006/50332 2.0% |
| `rs` | 표준 JSON이 아닌 stdout | 5% | 3112/4962 62.7% | 31273/50332 62.1% |
| `rs` | `MAX_RANK`에서 잘린 답 | 1% | 69/4962 1.4% | 708/50332 1.4% |
| `fx` | 메타데이터 / `NULL` | 각 30% | 54.3% / 45.7% (5038건) | 54.1% / 45.9% (49668건) |

새로 세는 법의 세 통계는 다른 두 seed에서: `st` 바로 위·아래 183/1246 14.7%(seed 1), 153/1228 12.5%(seed 987654321); `rs` `null` 항목 449/5000 9.0%, 445/4988 8.9%; `rs` 형 어긋남 거절 561/5000 11.2%, 567/4988 11.4%. 가장 빠듯한 것은 `rs` 형 어긋남 거절(11.2%, 문턱 10%)이다.

### 디버그 도구 자체 점검 (`run.ps1 -PerKind 10000 -Seed 20261115 -DebugTool`, 종료 코드 0)

종류마다 보통 비교를 한 뒤 같은 사례 파일(표 종류 `U X M B Z`는 같은 표 모드)을 디버그 빌드의 `build/qt-debug/xgs-diff.exe`에도 주어 릴리스 답과 줄마다 비교한다(원본 `17-diff-10000-seed20261115-debugtool.txt`, 5분 9초, 17:43:14~17:48:23). 깨끗한 재빌드 뒤라 디버그 도구는 릴리스 것과 같은 커밋이었다. 26개 종류 모두 `debug tool <줄 수> lines, 0 differences from the release tool`이고(줄 수는 `U`·`B`·`Z` 1,112,064, `X` 65,536, `M` 11, 나머지 10,000), 같은 실행의 오라클 비교도 불일치 0, 통계 문턱도 모두 넘었다(숫자는 같은 seed의 첫 실행과 같다). 마지막 줄 `PASSED (kinds UXMSCFDGTLYRNBZHOJEQVKAIWP, 10000 cases per case kind, seed 20261115, debug tool identical to the release tool)`.

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

Kotlin은 yt-dlp의 `-J` 출력을 `RawPlaylist`/`RawEntry` 클래스에 형을 맞춰 읽는다. 이것은 Q2a의 `parseToJsonElement`와 받아들이는 범위가 다르다(더 엄격한 곳도 더 느슨한 곳도 있다). Task 5가 339개를 관찰했고, 전체 표는 저장소에 넣지 않은 작업 기록(Task 5의 보고서)에만 있다. 대표 행(`fx`는 영상 메타데이터: `M 제목 채널 …` 또는 `NULL`, `rs`는 조회 결과). 한 행은 "yt-dlp가 stdout에 이 글을 냈을 때 Kotlin 앱이 이렇게 답했다"는 뜻이다:

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
| `...`, `.. `, ` ..` | 받음. 그리고 `exists`가 **참**이다(Windows가 그 이름을 폴더 자신으로 풀기 때문; 아래 "고치지 않은 Kotlin 쪽 동작" 7) |
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
- **최종 리뷰 뒤에 고친 C++**: 출력 폴더의 디렉터리 검사(폴더 자리가 보통 파일이면 `.. ` 같은 이름의 `put`이 그 파일을 바꿨다, 위 "최종 리뷰 뒤에 고친 것"), 대기 실패를 취소로 답하던 것, 0바이트 쓰기의 끝없는 되풀이. 대조가 찾은 것은 아니다(리뷰가 코드를 읽고 찾았다).
- **두 번째 관문 실행**에서는 대조 불일치가 없었고, 관문 도중에 고친 C++도 없다.

## 변이 시험 (각 Task의 실행)

이식 코드를 일부러 틀리게 고쳐 대조나 시험이 잡는지 본 것이다. 모두 시험 뒤 되돌렸고 커밋에 없다. Task 8은 다시 하지 않았다. 숫자는 각 Task가 보고한 1만 건 대조 실행의 불일치 수다. 어느 seed였는지는 Task 1과 Task 4만 적었고(둘 다 20261115), Task 2·5·6의 보고서는 seed를 따로 적지 않았다. 최종 리뷰 뒤의 변이 점검은 표 아래에 따로 적었다.

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

## 최종 리뷰 뒤에 고친 것

이 단계 전체를 본 최종 리뷰는 큰 결함은 없지만 다음 단계(Q2d)가 이 층 위에 쌓이기 전에 고칠 것들을 짚었다. controller가 고칠 것을 정했고 한 번에 고친 뒤 관문 전체를 다시 돌렸다(위 "관문 표"가 그 두 번째 실행이다). 고칠 때마다 먼저 시험을 쓰고 그 시험이 실패하는 것을 본 뒤 고쳤다(아래 "실패를 먼저 본 것").

| 고친 것 | 어디 | 시험 |
|---|---|---|
| 흔들린 시험이 실패한 `put`의 결과·Windows 문구·**오류 코드**를 찍는다. 옮기기 결과(`MoveResult`)와 `put` 결과(`PutResult`)에 Win32 오류 코드를 붙였다(Windows 표시 언어와 상관없이 5와 32를 가린다) | `fs::moveFile`, `LocalFolderSink::put`, `overlappingPutsOfOneNameWithOverwriteAllSucceed` | (같은 시험) |
| 옮기기를 짧게 다시 해 본다(위 "Kotlin과 다르게 한 것" 16번) | `fs::moveFile` | `moveRetriesWhileTheTargetIsHeldForAMoment`(다른 스레드가 대상을 `FILE_SHARE_DELETE` 없이 0.3초 쥐었다 놓으면 성공), `moveFailsWithTheCodeWhenTheTargetStaysHeld`(계속 쥐면 1~10초 사이에 실패, 문구와 코드 5나 32, 원본 그대로), `moveFailsWithAMessageWhenTheSourceIsMissing`(없는 원본은 코드 2로 1초 안에 실패) |
| 작성자 결정 5의 시험 | 시험 자식의 새 모드 `spawnexit` | `aGrandchildLeftRunningAfterANormalExitIsKilled` |
| NUL이 든 명령은 시작하지 않음(17번) | `SystemProcessRunner::run` | `aNulInTheCommandIsAStartFailure`(인자 가운데·끝, 프로그램 경로에 NUL; 아무것도 시작되지 않음) |
| 대기 실패는 취소가 아닌 `RunStatus::Failed`(18번) | `SystemProcessRunner::run`, 그리고 `ToolManager`(상태·설치·업데이트), `YtDlpResolver`, `M4aTagger`, 가짜 실행기(`RunFailure`) | `aRunThatFailsAfterItsStartIsNotACancel`, `aRunThatFailsAfterItsStartIsAFailureNotACancel`, `aRunOfFfmpegThatFailsAfterItsStartIsAFailureAndKeepsTheOriginal` |
| 0바이트 쓰기는 실패(19번) | `fs::writeBytes` | 없음(일으킬 방법이 없다) |
| 멈춤의 2초 장치를 `CancelIoEx`로(15번) | `SystemProcessRunner` | `aStopReturnsWhenAProcessOutsideTheJobHoldsThePipe`(시험 자식의 새 모드 `sleepshow`). 바꾼 뒤 실행기 시험 모음을 릴리스·디버그 각 10번 돌려 20번 모두 19개 통과(수정 작업) |
| `ProcessRunner` 계약 문서: 처리기는 부른 스레드에서 불릴 **수도** 있다(가짜가 그렇다), 막히면 안 된다, 멈춤 뒤 마지막 줄은 잘릴 수 있다 | `process_runner.h` | 문서 |
| 출력 폴더가 디렉터리가 아니면 `put` 실패, `exists` 거짓(20번) | `LocalFolderSink` | `putFailsWhenTheFolderIsNotADirectory`(폴더 자리가 보통 파일일 때와 보통 파일 아래일 때, 이름 `.. `·`...`·`a.m4a`; 그 파일은 그대로) |
| 대상 자리의 빈 폴더 위에 설치하는 길의 시험, 그 길에서 `QDir`·`QFileInfo` 대신 `xgs::fs`(새 함수 `fs::removeEmptyDirectory`, 긴 경로도 됨) | `ToolManager::installYtDlp` | `installReplacesAnEmptyDirectoryAtTheTarget`(빈 폴더 → 설치 성공, 대상은 새 파일), 기존 `installReportsAMoveFailureAndKeepsTheOldBinary`(안에 파일이 있는 폴더 → 실패) |
| ffprobe 실패가 그 시험만 실패시킨다(실행 전체를 끝내고 임시 폴더를 남기지 않는다) | `tst_real_ffmpeg_tagging`의 `probe` 도우미 | 아래 "실패를 먼저 본 것" |
| 대조 통계 셋(아래 표) | `run.ps1`, 생성기 `gen.cpp` | 대조 |

새 함수: `fs::isDirectory`, `fs::removeEmptyDirectory`, `fs::errorText`(전에는 파일 안에 숨어 있던 Windows 문구 함수를 밖에 보이게 한 것), `RunStatus::Failed`, 가짜 실행기의 `RunFailure`. 더한 시험 이름은 열 개다: Kotlin 짝이 있는 클래스에 여덟(`test-classes.json`의 `added`: 28 → 36), Kotlin 짝 없는 `tst_file_ops`에 둘.

### 대조 통계 셋

리뷰가 "통계가 이름과 다른 것을 잰다"고 한 셋을 고쳤다. 문턱은 그대로다.

| 통계 | 전 | 후 |
|---|---|---|
| `W st` js 버전이 문턱 근처 | 문턱과 **같은** 버전까지 셌다: Task 8의 관문 seed 20261115에서 225/1194 18.8% | 문턱 바로 위·아래(처음 다른 부분이 1 차이)만 센다. 옛 생성기로는 118/1194 **9.9%로 문턱 10%에 못 미쳤다**(수정 작업, seed 20261115). 문턱이 아니라 **생성기를 고쳤다**: 버전을 문턱 근처에 두는 몫을 50%에서 60%로, 그 안에서 문턱과 같은 값은 3분의 1에서 5분의 1로. 문턱과 같은 버전은 따로 보여 주기만 한다 |
| `P rs` 형 어긋난 속성 | 훑기가 형 어긋남을 찾은 모든 stdout: 957/4962 19.3%(Task 8의 관문, seed 20261115; 그중 훑기가 틀린 것 91) | stdout을 실제로 읽은 사례(yt-dlp가 돌고 종료 코드 0)에서, 훑기가 찾았고 Kotlin이 **거절한** 것만 센다(훑기가 틀린 것, 곧 Kotlin이 받은 것은 빼고 따로 보여 준다) |
| `P rs` `null` 항목 | 답에 id·제목 없는 "알 수 없는 항목"이 있는 사례(`null`이 아닌 빈 객체도 셌다): 451/4962 9.1%(Task 8의 관문) | stdout을 실제로 읽은 재생목록 실행에서 `entries` 안에 글자 그대로의 `null` 항목이 있고 Kotlin이 받은 것만 센다 |

고친 뒤의 값은 위 "`W`·`P`의 op별 통계" 표에 있다.

### 실패를 먼저 본 것

- 옮기기 재시도: 고치기 전 `moveRetriesWhileTheTargetIsHeldForAMoment`가 `code 5`(액세스 거부)로, `moveFailsWithTheCodeWhenTheTargetStaysHeld`가 "0 ms 만에 포기"로 실패했다. 고친 뒤 통과.
- 출력 폴더: 고치기 전 `putFailsWhenTheFolderIsNotADirectory`가 "폴더 자리가 보통 파일 + `.. `: status 0(`Stored`)"으로 실패했다. 곧 **그 파일이 바뀌었다**. 처음에는 폴더를 보통 파일 **아래**에 두었는데 그때는 고치기 전에도 실패하지 않아서(Windows가 `.. `를 폴더 자신으로 풀므로 그 폴더가 없으면 옮기기가 실패한다) 폴더 자리가 보통 파일인 경우를 더했다.
- NUL: 고치기 전 `aNulInTheCommandIsAStartFailure`가 `Exited`(자식이 실제로 시작됨)로 실패했다.
- 대기 실패의 세 호출하는 쪽: 호출하는 쪽의 처리를 잠시 빼면 세 시험이 모두 실패했다(업데이트 문구가 `2026.10.01`, 조회 문구가 `yt-dlp가 비정상 종료했습니다.`, 태그 문구가 `태그를 쓰지 못했습니다: 알 수 없는 오류`). 되돌린 뒤 통과.
- 작성자 결정 5와 2초 장치의 시험은 이미 있던 동작을 처음으로 시험하는 것이라 고치기 전에도 통과했다. 대신 아래 변이 점검으로 그 시험이 동작을 잡는지 보았다.
- ffprobe: `probe` 도우미가 일부러 없는 파일을 읽게 바꿔 실제 ffmpeg 통합 시험을 돌렸다(네트워크 없음): 8개 시험이 모두 각자 `ffprobe: ffprobe failed (1): … No such file or directory`로 실패했고 실행은 끝까지 갔으며(`Totals: 2 passed, 8 failed`) `%TEMP%`에 `xgs-tagging-*` 폴더가 남지 않았다. 되돌렸다.

### 변이 점검 (수정 작업)

규칙에 정해진 셋과 더 본 둘이다. 하나씩 C++을 일부러 틀리게 고쳐 릴리스로 그 시험 하나만 다시 빌드해 돌리고 되돌렸다. 다섯 모두 되돌린 뒤 `git status`에 `qt/`의 바뀐 파일이 없었고 릴리스를 다시 빌드했다. 원본은 작업 폴더 `w\mut-*.txt`.

| 변이 | 잡은 시험 | 결과 |
|---|---|---|
| 옮기기 재시도 빼기(다시 해 볼 오류를 하나도 없게) | `moveRetriesWhileTheTargetIsHeldForAMoment` | 실패: `code 5`(액세스 거부) |
| 출력 폴더의 디렉터리 검사 빼기(`put`) | `putFailsWhenTheFolderIsNotADirectory` | 실패: 폴더 자리가 보통 파일 + `.. ` → `Stored`(파일이 바뀜) |
| Job의 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` 빼기 | `aGrandchildLeftRunningAfterANormalExitIsKilled` | 실패: 손자가 `run`이 돌아온 뒤 5초에도 살아 있음(시험 끝에 시험이 죽였다) |
| (더 본 것) 설치에서 빈 폴더 지우기 빼기 | `installReplacesAnEmptyDirectoryAtTheTarget` | 실패: `yt-dlp를 설치하지 못했습니다: 액세스가 거부되었습니다.`(재시도 뒤) |
| (더 본 것) 2초 장치의 `CancelIoEx` 빼기 | `aStopReturnsWhenAProcessOutsideTheJobHoldsThePipe` | 실패: 멈춤 뒤 10초 안에 돌아오지 않음(시험이 쥔 핸들을 닫아 풀었다) |

첫 변이는 처음에 잘못 만들었다: 세 오류 중 첫째만 막는 꼴이어서 대상 쥠(오류 5)은 여전히 다시 해 보았고 시험이 통과했다. 세 오류 모두 막는 꼴로 다시 했고 위 표가 그 결과다.

## `tst_local_folder_sink`의 흔들림

**무엇이 보였나.** 두 번이다. (1) Task 7의 첫 릴리스 `ctest`에서, 빌드 직후, 이 실행 파일이 한 번 실패했다. 출력이 남지 않아 어느 시험이었는지 모른다. controller가 그 뒤 90번(6개 병렬 × 15) 돌렸을 때 실패 0이었다(Task 7 때의 측정). (2) Task 8의 관문에서 디버그 실행 파일을 따로 돌렸을 때 `overlappingPutsOfOneNameWithOverwriteAllSucceed`(덮어쓰기 `put` 여덟 개를 스레드로 동시에, 20번)가 한 번 실패했다. 160번 중 한 번이 `Stored`가 아니었는데 시험이 그 답을 찍지 않았다. 한 번 다시 돌리니 통과했다.

**최종 리뷰가 본 것.** 우리 코드와 시험에서 경쟁 상태를 찾지 못했다(`put`은 옮기기 내내 잠금을 쥐고, 원본 파일은 스레드가 시작하기 전에 다 써서 닫혀 있다). 리뷰어가 저장소 밖에서 같은 꼴을 2만 번(한 스레드)과 2만 4천 번(여덟 스레드) 해 보았을 때 실패 0이었다. 가장 그럴듯한 원인으로 리뷰어는 환경을 꼽았다: 빌드가 수백 개의 실행 파일을 쓴 직후라 백신의 실시간 검사가 밀려 막 닫힌 파일을 아직 쥐고 있었을 수 있다. 대상이 쥐어져 있으면 바꾸기가 오류 5(액세스 거부)로, 원본이 쥐어져 있으면 32(공유 위반)로 실패한다. Kotlin의 `Files.move`도 똑같이 드러나 있고 다시 해 보지 않는다.

**수정 작업이 한 것.**
1. 실패한 `put`의 상태·Windows 문구·오류 코드를 찍게 고쳤다(위 표 첫 행). 이제 다시 일어나면 5인지 32인지 알 수 있다.
2. 두 번 다 빌드 중이거나 직후였으므로, 다른 구성을 깨끗이 다시 빌드하는 동안 이 실행 파일의 겹치는 `put` 두 시험을 되풀이해 돌렸다(수정 작업, 원본 출력은 작업 폴더 `w\repro-release`, `w\repro-debug`):

| 돈 것 | 그동안의 부하 | 실행 횟수 | 실패 |
|---|---|---|---|
| 릴리스 `tst_local_folder_sink`(두 시험) | 디버그 깨끗한 재빌드 5번(16:39~16:46, 6분 41초) | 153 | 0 |
| 디버그 `tst_local_folder_sink`(두 시험) | 릴리스 깨끗한 재빌드 5번(16:46~16:54, 7분 39초) | 86 | 0 |

한 번 실행이 덮어쓰기 `put` 160번과 덮어쓰기 없는 `put` 160번이므로, 합해 239번 실행, 덮어쓰기 `put` 38,240번에서 **한 번도 재현되지 않았다**(약 14분 20초). Process Monitor는 쓰지 않았고 백신 설정도 건드리지 않았다(시스템·보안 설정은 바꾸지 않는다). 그래서 원인은 여전히 모른다.

곁가지로 본 것: 같은 시간대에 이 수정 작업의 편집 도구가 시험 소스 파일 하나를 바꿔 쓰려다 `EPERM: operation not permitted`로 두 번 실패했다(빌드가 그 파일을 컴파일하던 무렵). 다른 무언가가 막 읽히던 파일을 잠깐 쥐었다는 뜻일 수 있지만 무엇이 쥐었는지는 보지 않았고, 우리 코드의 흔들림과 같은 원인이라는 증거는 아니다.

3. 원인을 확정하지 못했지만, 막 쓴 파일을 다른 프로그램이 잠깐 쥐는 일은 실제 PC에서 흔하고 Q2d는 여러 곡을 동시에 옮기므로, controller의 결정으로 `fs::moveFile`이 그런 실패를 짧게 다시 해 보게 했다(위 "Kotlin과 다르게 한 것" 16번). 이것은 Kotlin과 다르므로 "사용자가 정할 것"에도 적었다.

## Kotlin과 다르게 한 것

다섯은 계획 작성자가 정했고(사용자가 바꿀 수 있다), 나머지는 실행 중에 Task 구현자나 controller가 정했다. **실행 중에 정한 것은 사용자가 아직 보지 않았다.** 바꾸려면 사용자가 정한다.

### 계획 작성자가 정한 것

1. **`QProcess`를 쓰지 않고 `CreateProcessW`를 직접 부른다.** 설계 §3은 "`QProcess` + Win32 Job Object"였지만 Q0 스파이크가 그 조합이 Qt 내부의 문서에 없는 동작에 기댄다는 것을 찾았다. 예: 자식은 처음부터 새 Job 안에서 태어나고(`PROC_THREAD_ATTRIBUTE_JOB_LIST`), 물려받는 핸들은 그 실행의 파이프 끝 셋뿐이며(`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`), 명령줄 인용은 우리 함수 `windowsCommandLine`이 Microsoft 문서의 규칙으로 만든다. Kotlin은 `ProcessBuilder`이고 Job이 없다.
2. **블로킹 함수 + 멈춤 토큰(`std::stop_token`).** Kotlin의 `suspend` 함수와 코루틴 취소 대신이다. 예: 실행 중에 토큰에 멈춤을 요청하면 `run`이 프로세스 트리를 죽이고 `Cancelled`를 돌려준다(Kotlin은 `CancellationException`). 그 결과 `ToolManager::status()`처럼 결과 형에 "취소"가 없는 함수는 다르게 답한다(아래 9번).
3. **yt-dlp 내려받기(HTTP)는 주입한다(`Fetch`).** 이 단계의 코드는 스스로 네트워크 요청을 보내지 않는다. 예: `installYtDlp`는 받은 `Fetch`에 `YtDlpUrl`을 넘기고, `Fetch`가 `Outcome::failed("offline")`이면 `yt-dlp 다운로드에 실패했습니다: offline`(Kotlin과 같은 문구). 실제 HTTP 구현은 Q2c다.
4. **운영체제 오류 문구는 Windows의 문구다.** 그 앞의 한국어는 Kotlin 그대로다. 예: 설치할 폴더 자리에 파일이 있을 때 Kotlin은 `yt-dlp를 설치하지 못했습니다: <경로>`(Task 4가 관찰한 문구는 경로였다), C++은 `yt-dlp를 설치하지 못했습니다: ` 뒤에 `FormatMessageW`의 Windows 문구가 온다. 시험은 한국어 부분만 비교한다.
5. **주 프로세스가 끝난 뒤 남은 자손도 죽인다.** Kotlin은 취소(또는 예외) 때만 `descendants()`를 죽인다(이것은 Kotlin 소스 `ProcessRunner.kt`를 읽고 안 것이고, Kotlin 쪽을 실제로 돌려 본 것은 아니다). 예: 자식이 손자를 띄워 두고 정상 종료하면 Kotlin 소스대로라면 손자가 계속 살고, Qt 판은 Job을 닫으면서 손자를 죽인다. Qt 쪽은 최종 리뷰 뒤에 시험을 더했다: `aGrandchildLeftRunningAfterANormalExitIsKilled`(자식이 파이프를 물려주지 않은 손자를 띄우고 곧 끝나면 `run`은 `Exited`, 손자는 5초 안에 죽는다; 두 번째 관문의 실행 파일별 단계에서 두 빌드 모두 통과. 같은 실행 파일의 디버그 `ctest` 한 번은 어느 시험인지 모른 채 실패했다, 위 관문 표). 단 출력 파이프가 닫힐 때까지 기다리는 것은 Kotlin과 같다: 자손이 stdout을 쥐고 있으면 `run`은 그 자손이 끝나거나 멈춤이 요청될 때까지 돌아오지 않는다(이것은 시험하지 않았다, 아래 "Q2d에 넘길 것"). 이 결정의 가능한 부작용 하나는 아래 "확인하지 못한 것"의 `yt-dlp -U`다.

### 실행 중에 더해진 것

6. **인자가 자식에 글자 그대로 닿는다**(Task 2). 위 관찰 4: Kotlin의 `ProcessBuilder`는 무작위 목록 2,000개 중 1,421개를 다르게 넘겼고, Qt 판은 2,000개를 모두 그대로 넘긴다. 예(관찰 4 표의 첫 행, Task 2가 관찰한 그대로): 인자 하나 `--opt="x y";a b--opt="x y"한글\\,`는 Qt 판에서 그대로 하나로 닿고, Kotlin에서는 `--opt=x` · `y;a b--opt=x` · `y한글\\,` 셋으로 쪼개지고 `"`가 사라졌다.
7. **`.cmd`·`.bat`은 실행하지 않는다**(계획의 Global Constraint, Task 2가 문구를 정함). 실행 파일 이름이 끝의 점·공백을 뗀 뒤 `.cmd`나 `.bat`으로 끝나면(대소문자 무시) 시작하지 않고 `StartFailed`이며 문구는 `배치 파일(.cmd, .bat)은 실행하지 않습니다`다. 빈 명령은 `실행할 프로그램이 없습니다`. 두 문구 모두 Kotlin에 짝이 없어 Task 2 구현자가 쓴 것이다(controller가 받아들임, 사용자 확인 대기). 예: 누군가 `run`에 `C:\tools\yt-dlp.cmd`를 넘기면 Qt 판은 실행하지 않고 위 문구로 실패한다(지금 `ToolLocator`는 `<이름>.exe`와 확장자 없는 `<이름>`만 찾으므로 실제 호출에서 `.cmd`가 넘어갈 길은 없다). Kotlin에는 이런 검사가 없다. 최종 리뷰 뒤에 같은 자리에 검사를 하나 더했다(아래 17번).
8. **`command[0]`을 `PATH`에서 찾지 않고 `.exe`도 붙이지 않는다**(Task 2). `CreateProcessW`의 `lpApplicationName`에 첫 인자를 그대로 주기 때문이다. 예: `["yt-dlp", "--version"]`은 Qt 판에서 시작 실패다. 호출하는 쪽은 전체 경로를 넘겨야 한다. Kotlin의 호출하는 쪽도 늘 `ToolLocator`가 찾은 전체 경로를 넘기므로 지금 코드에서는 차이가 드러나지 않는다.
9. **멈춤을 요청한 뒤의 `status()`**(Task 4, controller 결정). 결과 형이 `ToolsStatus`라서 취소를 나타낼 수 없다. 실행이 취소되면 그 도구가 아무것도 찍지 않은 것처럼 답한다. 예: yt-dlp 실행 중 멈춤 → `yt-dlp found=false, version=없음, path=<경로>`. Kotlin은 `CancellationException`을 던진다. 멈춤을 요청한 쪽이 이 답을 버린다는 약속이다(Q2d에 넘김).
10. **취소가 마지막 옮기기와 겹치면 C++은 `Tagged`라고 답한다**(Task 6). `M4aTagger`는 `<이름>.tagged.m4a`를 원래 파일 위로 옮기는 것이 마지막 단계다. Kotlin에서는 그 옮기기 중에 취소가 오면 파일이 이미 바뀌었는데도 "취소됨"이라고 답할 수 있다(Task 6이 Kotlin 소스를 읽고 짐작한 것이고 실제로 관찰하지는 않았다). C++은 옮기기가 성공했으면 `Tagged`다. 예: 옮기기가 끝난 직후 멈춤 요청 → Kotlin "취소", C++ `Tagged`(파일은 둘 다 태그가 쓰인 것).
11. **`LocalFolderSink`의 생성자는 던지지 않는다**(Task 6). Kotlin은 폴더를 만들지 못하면 생성자에서 던진다. C++은 그때 던지지 않고, 그 뒤 `put`이 Windows 문구와 함께 `Failed`, `exists`가 거짓이다. 예: 폴더를 만들 수 없는 위치를 주면 Kotlin은 sink를 만드는 순간(`Files.createDirectories`) 예외, C++은 sink는 만들어지고 첫 `put`이 `Failed`. 처음에는 이것을 시험하지 않았고, 최종 리뷰 뒤에 고치면서 시험을 더했다(아래 "최종 리뷰 뒤에 고친 것"의 출력 폴더 항목: 고치기 전에는 이 길에서 폴더 밖 파일이 바뀌었다).
12. **뿌리뿐인 이름**(Task 6). `C:`, `\`, `/`를 Kotlin은 `NullPointerException`으로, C++은 `std::invalid_argument`로 거절한다. 둘 다 "받지 않음"이고 대조 `W fn`의 답(`0`)은 같다.
13. **`ytdlp::escapeOutputTemplateDirectory`를 더했다**(Task 7, controller 결정). 출력 폴더의 모든 `%`를 `%%`로 겹친다. 예: `C:\음악\100% %(id)s` → `C:\음악\100%% %%(id)s`. Kotlin에는 없고 Kotlin은 `%`가 든 폴더에서 위 관찰 7처럼 조용히 다른 곳에 쓴다. `downloadCommand` 자체는 Kotlin 그대로 두었고(대조 `K` 불일치 0), 이것을 부르는 것은 Q2d의 몫이다. 아직 아무 제품 코드도 부르지 않는다.
14. **명령 조립의 예외를 결과로 바꾼다**(Task 5, 6). `YtDlpResolver`는 명령 조립이 던지는 `std::invalid_argument`를 잡아 실패(조회)나 없음(메타데이터)으로, `M4aTagger`는 `tagCommand`의 예외를 `Failed`(종류 `OTHER`)로 바꾼다. Kotlin에서는 `IllegalArgumentException`이 위로 올라갔을 자리다. 지금 코드에서는 앞의 검사(yt-dlp 있음, 출력이 `.m4a`) 때문에 닿지 않는다.
15. **취소가 막히지 않게 하는 2초 장치**(Task 2, 최종 리뷰 뒤 고침). 멈춤으로 Job을 죽인 뒤 읽기 스레드가 2초 넘게 안 끝나면 그 파이프의 읽기를 끊는다. 처음에는 `CancelSynchronousIo`로 그 스레드가 하던 입출력을 끊었는데, 그러면 줄 처리기가 스스로 하던 파일 쓰기까지 끊길 수 있어서 `CancelIoEx`로 그 파이프의 읽기만 끊게 바꿨다. 예: Job 밖의 다른 프로세스가 파이프 끝을 쥐고 있어도 취소는 돌아온다. 첫 판에서는 이것이 설계일 뿐 잰 적이 없었다. 최종 리뷰 뒤에 시험 `aStopReturnsWhenAProcessOutsideTheJobHoldsThePipe`를 더했다: 시험 프로세스(Job 밖)가 자식의 stdout 쓰기 끝을 복제해 쥐고 멈춤을 요청하면, 2초 장치를 거쳐 `Cancelled`가 돌아와야 한다(걸린 시간 1.5~8초). 두 번째 관문의 실행 파일별 단계에서 두 빌드 모두 통과했다(같은 실행 파일의 디버그 `ctest` 한 번은 어느 시험인지 모른 채 실패했다). Kotlin에는 이런 장치가 없다(정상 종료 쪽은 위 5번처럼 기다린다).

### 최종 리뷰 뒤에 더해진 것

controller가 최종 리뷰의 지적을 받아 정했다(사용자는 아직 보지 않았다).

16. **파일 옮기기를 짧게 다시 해 본다**(`fs::moveFile`). 옮기기가 "다른 프로세스가 파일을 사용 중"(공유 위반, 32), "액세스가 거부되었습니다"(5), 잠금 위반(33)으로 실패하면 20, 50, 100, 150 ms, 그다음 200 ms씩 쉬며 10번까지 더 해 본다(쉬는 시간 합 1.52초). 그래도 안 되면 마지막 Windows 문구와 오류 코드로 실패한다. 원본이 없거나 대상이 이미 있는 경우(덮어쓰기 아님)는 곧바로 답한다. Kotlin의 `Files.move`는 한 번만 해 본다. 이 함수는 출력 폴더의 `put`, 태그 쓰기의 마지막 옮기기, yt-dlp 설치의 마지막 옮기기가 모두 쓴다. 대가: 계속되는 실패(읽기 전용 대상, 실행 중인 `yt-dlp.exe`를 바꾸려 할 때)는 Kotlin보다 약 1.5초 늦게 보고되고, 출력 폴더에서는 그동안 다른 곡의 `put`이 기다린다. 예: 백신이 막 쓴 파일을 0.3초 쥐었다 놓으면 Kotlin은 실패, Qt 판은 성공(시험 `moveRetriesWhileTheTargetIsHeldForAMoment`). 왜 더했는지는 아래 "`tst_local_folder_sink`의 흔들림" 절.
17. **NUL 문자(U+0000)가 든 명령은 실행하지 않는다**(`SystemProcessRunner::run`). `CreateProcessW`는 NUL에서 글이 끝난 것으로 보므로, NUL 뒤의 인자(주소나 `--` 따위)가 아무 말 없이 사라진다. 이제 명령의 어느 요소에든 NUL이 있으면 시작하지 않고 `StartFailed`, 문구는 `NUL 문자(U+0000)가 든 명령은 실행하지 않습니다`(수정 작업이 쓴 문구, 사용자 확인 대기). 지금의 호출하는 쪽은 입력을 먼저 검사하므로 닿지 않는다. Kotlin에는 이런 검사가 없다.
18. **대기가 실패하면 "취소"가 아니다**(`RunStatus::Failed`). Windows의 대기 함수가 실패했을 때(멈춤 요청 없이) 첫 판은 `Cancelled`를 돌려줬다. Q2d는 `Cancelled`를 "사용자가 멈췄다"로 읽을 것이므로 곡이 말없이 사라질 수 있었다. 이제는 프로세스 트리를 죽이고 새 결과 `RunStatus::Failed`(Windows 문구)를 돌려준다. 모든 호출하는 쪽이 이것을 시작 실패처럼 다룬다: 도구 상태는 "못 찾음", yt-dlp 업데이트·조회는 `yt-dlp를 실행할 수 없습니다: <문구>`, 메타데이터는 없음, 태그 쓰기는 `태그를 쓰지 못했습니다: <문구>`. 시험은 가짜 실행기로 세 호출하는 쪽을 본다. 실제 대기 실패를 일으키는 시험은 없다(아래 "확인하지 못한 것").
19. **0바이트를 쓰고 "성공"한 쓰기는 실패로**(`fs::writeBytes`). 그런 답이 오면 첫 판은 끝없이 다시 썼다. 이제 Windows의 `ERROR_WRITE_FAULT` 문구로 실패한다. 그런 답을 일으킬 방법이 없어서 시험은 없다.
20. **출력 폴더가 디렉터리가 아니면 `put`은 실패, `exists`는 거짓**(`LocalFolderSink`). 위 11번의 길을 막는다. 아래 "최종 리뷰 뒤에 고친 것".

## 고치지 않은 Kotlin 쪽 동작

모두 **Qt 판이 그대로 따른다**(대조나 시험에서 같은 답). 바꿀지는 사용자가 정한다. 바꾸려면 Kotlin 쪽을 먼저 바꾸고 대조를 다시 돌린다. 입력과 답은 각 Task가 관찰한 것이다.

1. **id가 빈 항목이 "쓸 수 있음"이 된다**(Task 5, 관찰 q25). `{"entries":[{"id":"","title":"A - B"}]}` → 쓸 수 있음, 파일 이름 `001 A - B.m4a`. 빈 영상 ID가 내려받기로 넘어간다(Q2a의 영상 ID 거절이 `downloadCommand`에서 막는다).
2. **비공개·삭제 표시와 `availability`가 대소문자를 가린다**(Task 5, p24·p13). 제목 `[private video]`는 비공개가 아니고, `availability` `PRIVATE`는 쓸 수 있음이다.
3. **빈 채널은 업로더로 대체하지 않는다**(Task 5, f41). `"channel":""`이면 `?:`가 null만 보므로 채널이 빈 문자열로 남는다.
4. **형 있는 JSON 읽기의 너그러움**(Task 5). 멤버 사이 쉼표가 빠져도 받고, 모르는 키의 값은 문법을 거의 보지 않고 건너뛴다(`[1,,2]`, `{y:1}`, 컨테이너 안의 U+00A0). `kotlinx.serialization`의 동작이고 그대로 맞췄다.
5. **ffmpeg는 첫 줄이 비지 않으면 "찾음"이다**(Task 4). "찾음"이 "버전이 있음"이고 버전은 첫 줄에서 `ffmpeg version `을 떼고 첫 공백 앞까지다. 예: 첫 줄 `ffprobe version 7` → 찾음, 버전 `ffprobe`.
6. **업데이트 결과의 "마지막 줄"이 스레드 시간에 따라 다르다**(Task 4). `updateYtDlp`는 stdout과 stderr를 두 스레드에서 한 목록에 모으고 마지막 비지 않은 줄을 결과 문구로 쓴다. 실제 프로세스에서는 어느 줄이 마지막인지가 두 스레드의 순서에 달렸다. 대조에서는 가짜 실행기가 한 스레드에서 차례로 부르므로 같다.
7. **점과 공백뿐인 파일 이름을 받는다**(Task 6). `...`, `.. `는 받는 이름이고, Windows가 그 이름을 경로에서 지워 버려서(폴더 자신을 가리키게 된다) `exists`가 참이다. 수정 작업이 Windows의 `GetFullPathNameW`로 본 바로는 `<폴더>\.. `, `<폴더>\...`, `<폴더>\. .`, `<폴더>\....` 모두 `<폴더>\`가 되었다(부모가 되는 것은 정확히 `..`뿐인데, 그것은 거절된다). 그래서 폴더가 있으면 `put`은 폴더를 대상으로 하게 되어 실패한다. 그런데 C++에서 폴더가 없고 그 자리에 같은 이름의 **파일**이 있으면 `put(".. ", …, 덮어쓰기)`가 그 파일을 바꿨다(수정 작업의 시험이 고치기 전에 그렇게 실패했다). 최종 리뷰 뒤에 폴더가 디렉터리가 아니면 `put`이 실패하게 고쳤다(위 20번). `FilenameFormatter`는 이런 이름을 만들지 않는다.
8. **BOM으로 시작하는 yt-dlp 출력은 읽지 못한다**(Task 5, f27). 형 있는 읽기가 U+FEFF를 공백으로 보지 않는다. yt-dlp는 BOM을 쓰지 않으므로 실제 영향은 없을 것이다.

다음 셋은 Kotlin의 동작을 관찰했지만 Qt 판이 **따르지 않는다**(위 "Kotlin과 다르게 한 것"): 뿌리뿐인 이름의 `NullPointerException`(12번), 마지막 옮기기 중 취소의 답(10번), 정상 종료 뒤 남는 자손(5번). Q2a 보고서의 "고치지 않은 Kotlin 쪽 동작"은 그대로 남아 있다.

## 계획의 Review Focus 여섯 가지

| 갈래 | 확인한 곳 | 결과 |
|---|---|---|
| 1. 동시에 여러 프로세스를 띄울 때 파이프가 샌다 | `parallelRunsDoNotHoldEachOthersPipes`(긴 `sleep` 하나와 짧은 `emit` 여덟을 동시에, 짧은 것들이 2초 안에 모두 돌아와야 한다), `aStopReturnsWhenAProcessOutsideTheJobHoldsThePipe`(Job 밖에서 파이프 끝을 쥐어도 멈춤이 돌아온다; 최종 리뷰 뒤에 더함) | 두 번째 관문의 릴리스 `ctest`와 두 빌드의 실행 파일마다 돌린 단계에서 통과. 디버그 `ctest`에서는 이 시험들이 든 실행 파일이 한 번 실패했고 어느 시험인지 모른다(위 "테스트"). Task 2에서 핸들 목록을 뺀 변이는 10번 중 10번 첫 시험에 걸렸다 |
| 2. 취소했는데 자손이 남는다 | `cancellingKillsGrandchildrenToo`(취소 뒤 5초 안에 손자 PID가 없다), 대조 실행 `withoutAJobTheGrandchildSurvives`(Job 없이 자식만 죽이면 손자가 산다), `aThrowingHandlerKillsTheProcessAndRethrows`, 그리고 정상 종료 쪽의 `aGrandchildLeftRunningAfterANormalExitIsKilled`(최종 리뷰 뒤에 더함) | 1번과 같은 단계들에서 통과(디버그 `ctest`의 실패 한 번은 같은 실행 파일). 손자는 시험 자식(`xgs-test-child`)이다. yt-dlp가 띄우는 실제 ffmpeg로 취소해 본 적은 없다(Q2d) |
| 3. 공백·따옴표·끝의 `\`·`%`·`&`·한글이 든 인자 | `argumentsReachTheChildVerbatim`(무작위 2,000개 목록), `tst_command_line`, 통합 시험 `anOutputFolderWithSpacesKoreanAndAnAmpersandWorks`와 `aPercentSignInTheOutputFolderIsReportedNotSilentlyMisplaced` | 이번 관문에서 모두 통과. `%`는 인용과 따로 yt-dlp가 틀로 읽는 문제가 있어서 이스케이프 함수를 더했다(위 13번) |
| 4. 깨끗하지 않은 yt-dlp 출력 | 대조 `W ln`(잘못된 UTF-8, 홀로 선 `\r`, 줄 끝 없는 끝, 조각 경계), `aLineOfTenMegabytesArrivesWhole` | 이번 관문의 대조에서 불일치 0(아래 통계), 시험 통과 |
| 5. 모양이 다른 조회 응답 JSON | 대조 `P`(숫자로 온 제목, `null` 항목, 모르는 키의 `NaN`·깊은 값, 망가진 문서, `MAX_RANK` 경계), `tst_ytdlp_resolver`(그중 `resolvesAFiveThousandEntryPlaylistQuickly`는 5,000항목 재생목록 한 줄; 그 줄이 약 3.5MB라는 것은 Task 5의 보고), 통합 시험 `resolvesTheReferencePlaylistWithoutDownloadingAnything`(실제 100항목) | 이번 관문에서 불일치 0, 통과 |
| 6. 긴 경로와 다른 드라이브 | `putWorksWhenTheTargetPathIsLongerThan260`, `pathsLongerThan260Work`, `existsSeesFilesAndDirectories` | 긴 경로는 이번 관문에서 통과. **다른 드라이브로 옮기기는 확인하지 못했다**: 이 PC에는 드라이브가 하나뿐이다(`MoveFileExW`의 `MOVEFILE_COPY_ALLOWED`를 쓰지만 그 길을 탄 적이 없다). 출력 폴더 자체가 디렉터리가 아닐 때는 최종 리뷰 뒤에 `putFailsWhenTheFolderIsNotADirectory`로 본다 |

## 계획과 달라진 것

- **통합 시험 수(Task 7, controller 결정 T7-a).** 계획은 "통합 테스트 13개 통과, 오디오는 내려받지 않음"이라고 적었으나 두 문장은 함께 성립하지 않는다. 계획 작성자가 Kotlin `RealYtDlpIntegrationTest`를 잘못 읽은 것이다: 다섯 중 둘이 오디오를 내려받는다. 그래서 `writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum`은 단언까지 모두 옮겼지만 `XGS_INTEGRATION_AUDIO=1`이 더 있어야 돌게 했고(Kotlin에 없는 조건, 돌리지 않았다), `downloadsAShortVideoAsM4aWithoutReencoding`은 내려받기 작업(Q2d)과 LRCLIB(Q2c)이 없어 이름과 건너뛰기만 두었다. 이 단계에서 실제로 돈 Kotlin 통합 시험은 11개(ffmpeg 8 + yt-dlp 3)와 더한 2개다. Task 8 지시서의 "13 + 2 통과"도 같은 실수를 이어받았고 controller가 고쳤다.
- **`writeUtf8WritesNoByteOrderMark`의 기대 바이트(T1-a).** 계획은 `한글\n` → `EC 95 9C …`라고 적었으나 오타다. `한`(U+D55C)은 `ED 95 9C`이고 오라클도 `ED 95 9C EA B8 80 0A`를 썼다. 시험은 관찰한 바이트를 쓴다.
- **Task 6의 변이 하나(T6-a).** "끝 공백을 받음"은 Kotlin이 이미 받으므로 아무것도 바꾸지 않는 변이였다. 가까운 둘로 바꿨다(위 "변이 시험").
- **더한 시험 이름.** 계획이 적은 추가는 `tst_system_process_runner` 10개, `tst_m4a_tagger`·`tst_local_folder_sink` 하나씩, 통합 둘이었다. Task 8까지 실제로는 22개였고, 최종 리뷰 뒤에 여덟을 더해 30개다(그 여덟은 위 "최종 리뷰 뒤에 고친 것"). Task 8까지의 22개: 실행기에 하나 더(`aThrowingHandlerKillsTheProcessAndRethrows`, 리뷰가 찾은 버그의 시험), `tst_tool_manager`에 취소 셋, `tst_ytdlp_resolver`에 셋(취소, 정확히 999개, 5,000항목), `tst_ytdlp_commands`에 이스케이프 하나. `tst_file_ops`에도 Kotlin 짝 없는 `existsSeesFilesAndDirectories`가 하나 더 있다.
- **새 함수.** `fs::exists`(Task 6: `LocalFolderSink::exists`가 폴더와 긴 경로도 보려고), `kt::isDigit`·`kt::digitValue`(Task 4: 버전 비교의 유니코드 숫자), `ytdlp::escapeOutputTemplateDirectory`(Task 7), `json::ParseOptions::typed`(Task 5, 계획의 "선택 인자"를 클래스 모양으로 구체화).
- **시험 도우미.** `xgs-test-child`는 계획의 모드에 `console`(자식에 콘솔 창이 있는지 스스로 보고)과 `sleeperr`(stderr로 `started`)를 더했다. 무작위 인자 목록은 시험과 `xgs-diff`가 함께 쓰는 헤더 `qt/tests/helpers/argument_lists.h`에 있다. 시험은 자식 실행 파일을 컴파일 정의 `XGS_TEST_CHILD`로 찾는다(계획이 정하지 않았던 것). 가짜 실행기는 멈춤이 처리기가 끝날 때까지 요청되면 `Cancelled`를 돌려준다(T3-a, 코루틴이 다음 중단점에서 취소되는 것을 흉내).
- **통합 시험의 격리(T7-c).** yt-dlp 시험 클래스는 그동안 `XDG_CACHE_HOME`, `TEMP`, `TMP`를 한 `QTemporaryDir` 안으로 돌리고 끝에 되돌린다. 사용자의 `%USERPROFILE%\.cache\yt-dlp`와 `%TEMP%`에 아무것도 남지 않게 하려는 것으로 Kotlin 시험에는 없다. 켜는 조건도 Kotlin의 Gradle 태그 대신 `XGS_INTEGRATION=1`이다.
- **대조 `P`의 입력.** 계획 Step 4는 입력을 Q1의 `C` 생성기로 만든다고 했으나, `rs` 입력의 18%만 그 생성기이고 나머지는 손으로 짠 재생목록·영상 주소다(조회가 실제로 실행되는 사례를 늘리려는 것). README에 적혀 있다.
- **`OutputSink::exists`가 던지는 것(P2).** 설계의 "예외 대신 결과"와 달리 받지 않는 이름에 `std::invalid_argument`를 던진다. 계획의 인터페이스가 그렇게 정했고 Kotlin의 `require`와 Q2a 명령 조립 함수를 따른다.
- **최종 리뷰 뒤의 수정**(위 "최종 리뷰 뒤에 고친 것"). 계획에 없던 시험 이름 열 개, 새 함수 다섯, 대조 생성기(`W st`의 버전) 한 곳, 대조 통계 셋. README는 이 수정이 틀리게 만든 곳만 고쳤다(더한 시험 이름과 수, 시험 자식의 새 모드, 옮기기 재시도, 실행기의 새 검사, `W st`·`P rs` 통계 설명).
- **README(Task 8).** Task 8의 관문에서 README가 시험 출력과 달랐던 두 곳을 고쳤다: `tst_system_process_runner`의 추가 이름이 열 개가 아니라 열한 개(던지는 처리기), Kotlin 짝 없는 실행 파일 목록에 `tst_paths`와 `tst_file_ops`가 빠져 있었다. 그리고 "관문" 절이 Q1·Q2a만 말하고 통합 시험 단계가 없어서, Q2b와 "릴리스로 통합 시험 한 번(`XGS_INTEGRATION_AUDIO`는 켜지 않음)"을 한 문장 더했다.

## 확인하지 못한 것

- **다른 드라이브로 옮기기.** 이 PC에는 드라이브가 하나다. 출력 폴더가 작업 폴더와 다른 드라이브일 때 `fs::moveFile`(`MOVEFILE_COPY_ALLOWED`, 복사 뒤 지움)이 맞게 도는지, 도중에 실패하면 무엇이 남는지 시험하지 않았다.
- **Windows 10.** 지원 대상이지만 Windows 11에서만 돌렸다. `PROC_THREAD_ATTRIBUTE_JOB_LIST`(Windows 10부터)와 긴 경로 처리가 Windows 10에서도 같은지 보지 않았다.
- **실제 백신이 파일을 쥐었을 때.** 다른 프로세스가 대상 파일을 쥔 경우는 시험이 그 파일을 직접 열어 흉내 냈다(옮기기 재시도 시험). 이 PC의 백신(V3 Lite)이 실제로 막 쓴 파일을 쥐는지, 쥔다면 얼마 동안인지는 보지 않았다(Process Monitor나 백신 설정은 쓰지도 바꾸지도 않았다). `writeBytes`·`removeQuietly`는 다시 해 보지 않는다.
- **`tst_local_folder_sink`의 흔들림의 원인.** 아래 "`tst_local_folder_sink`의 흔들림" 절. 재현되지 않았고 원인은 여전히 모른다.
- **두 번째 관문의 디버그 `ctest`에서 `tst_system_process_runner`가 한 번 실패한 원인.** 위 "테스트". `ctest`의 출력에 Qt Test의 줄이 남지 않아 어느 시험 함수가 실패했는지 모른다. 그 실행 파일만 한 번 다시 돌리니 19개 모두 통과했고, 같은 빌드를 실행 파일마다 돌린 단계에서도 통과했다. 관문 밖에서(관문 결과는 바꾸지 않는다) 원인을 보려고 같은 디버그 빌드로 `ctest -R tst_system_process_runner`를 세 번 더 돌렸는데 모두 통과했다(25.3, 25.9, 26.4초; 수정 작업). 깨끗한 재빌드 직후 처음 실행되는 시험 자식을 백신이 검사하느라 프로세스 시작이 느려졌을 수도 있지만 짐작일 뿐이다. 이 실행 파일은 수정 작업 중에도 릴리스·디버그 각 10번 모두 통과했다.
- **`yt-dlp -U`가 `yt-dlp.exe.old`를 남길 수 있다.** 최종 리뷰어의 기억(yt-dlp의 업데이트 코드)으로는, Windows에서 `yt-dlp -U`는 끝날 때 `cmd /c ping … & del yt-dlp.exe.old` 같은 도우미를 띄워 옛 실행 파일을 지운다. Kotlin에서는 이 도우미가 살아남아 지운다. Qt 판은 yt-dlp가 끝나면 남은 자손을 모두 죽이므로(작성자 결정 5) 도우미도 죽고, 약 18MB의 `yt-dlp.exe.old`가 다음 업데이트까지 bin 폴더에 남을 수 있다. **확인하지 않았다**: 확인하려면 yt-dlp 사본이 새 실행 파일을 내려받아 실행하게 두어야 해서 사용자의 허락이 필요하다(아래 "사용자가 정할 것").
- **대기 실패(`RunStatus::Failed`)의 실제 길.** 호출하는 쪽의 처리는 가짜 실행기로 시험했지만, `SystemProcessRunner` 안에서 Windows 대기 함수가 실제로 실패하는 길은 일으킬 방법이 없어 시험하지 않았다. `writeBytes`의 0바이트 쓰기도 같다.
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

각 Task의 리뷰가 고치지 않고 남긴 작은 지적 가운데, 최종 리뷰가 "열어 두어도 된다"고 한 것들이다. **모두 아직 고치지 않았다.** 최종 리뷰가 "끝내기 전에 고칠 것"으로 고른 것은 고쳤고(아래 "최종 리뷰 뒤에 고친 것"), 지적 둘은 실제 문제가 아니어서 뺐다(Task 5의 "`C` 생성기 18%"는 이미 위 "계획과 달라진 것"에 있고, Task 6의 "빈 오류 문구"는 Windows 문구 함수가 빈 글을 돌려주지 않아 생길 수 없다).

- **파일 연산(Task 1, 6).** 숨김·시스템 파일 위에 쓰면 실패한다(Kotlin 동작은 관찰하지 않음). 긴 경로 판정이 전체 경로가 아니라 받은 문자열 길이로 한다(긴 작업 폴더 아래 상대 경로; 지금 호출하는 쪽은 절대 경로만 넘긴다). `fs::exists` 주석이 `Files.exists`와 같다고 하지만 끊어진 링크에서 다르다. 다른 드라이브로 옮기기는 시험하지 않았다(드라이브가 하나; `subst` 드라이브로 그 길을 탈 수 있을지는 리뷰어의 짐작이고 확인하지 않았다).
- **프로세스 실행(Task 2).** `GetExitCodeProcess`의 실패를 보지 않는다(종료 코드 0으로 읽힘). 읽기 스레드의 `join`이 예외 처리 중에 던질 수 있다(이론상). `E0` 너무 긴 꼴 검사에 단위 시험이 없다(대조만 본다). `PYTHON*` 변수의 대소문자 무시 교체에 시험이 없다. `parallelRuns…`에 쓰지 않는 타이머가 있고 긴 실행의 `Cancelled`를 단언하지 않는다. stdout을 물려받은 자손이 정상 종료 뒤 `run`을 붙잡는 경우는 시험하지 않았다(Q2d 목록). `parallelRunsDoNotHoldEachOthersPipes`는 시간에 기대는 시험이다(Task 2에서 핸들 목록을 뺀 변이를 10번 중 10번 잡았다; 시간에 기대지 않는 꼴도 리뷰어가 제안했다).
- **도구(Task 3, 4).** `tst_tool_locator`의 기대값을 `paths::child`로 만든다(글자 그대로의 비교가 없음), `QDir`을 간접 포함에 기댄다. 가짜 실행기가 `Cancelled`에서 `RunResult`의 기본값에 기댄다. 설치의 두 멈춤 길 중 하나만 시험한다. `installYtDlp` 헤더가 Windows 문구의 차이를 말하지 않는다. 시험 주석 하나가 틀렸다("기본 fetch가 네트워크에서 받는다"). `kt::digitValue`를 Nd 범위 전체로 시험하지 않는다.
- **조회(Task 5).** `xgs-diff`의 `answerRs`/`answerFx`가 예외를 잡지 않는다(C++이 예상 밖으로 던지면 비교 줄 대신 도구가 멈춘다). 실행이 끝난 뒤 stdout 글을 잠금 없이 읽는 약속이 주석에 없다.
- **태그·출력 폴더(Task 6).** 옮긴 취소 시험 끝에 덧붙인 단언이 약하다(쓰지 않음과 쓰고 지움을 가리지 못함). `tag`의 `Cancelled` 검사가 빠른 길이라는 주석이 없다. 닿을 수 없는 "경로가 아님" 실패 문구가 영어다. `existsSeesFilesAndDirectories`가 단언이 실패하면 260 단위 넘는 파일을 남긴다.
- **통합 시험(Task 7).** 시간 초과를 "시간 초과"라고 말하지 않는다. 원래 비어 있던 환경 변수를 지운 것으로 되돌린다. `aPercentSign…IsReportedNotSilentlyMisplaced`라는 이름이 하는 것보다 많이 말한다(계획의 이름). ffmpeg 시험 실패 문구에 Kotlin의 문구·MD5가 없다.
- **경로 시험(Task 1).** `tst_file_ops`의 쓰기 도우미가 도우미 안에서 `QVERIFY`를 쓴다. `pathsLongerThan260Work`는 `%TEMP%`가 238 단위를 넘으면 길이가 음수가 되고 이른 실패 때 폴더가 남는다. `fileNameIsTheLastPart`에 `ERR` 행이 없고 "two dots"라는 행에 점이 하나다.
- **대조(최종 리뷰).** `W`는 op 여덟이 1만 건을 나눠 써서 op마다 약 1,250건이다. 변이 몇은 4~14건의 불일치로만 잡혔다(위 "변이 시험"). 10만 건 실행이 이를 메운다. op 수에 맞춰 건수를 늘리는 것은 하지 않았다.
- **코드 중복(최종 리뷰).** Windows 문구 함수와 `wide`가 `file_ops.cpp`와 `system_process_runner.cpp`에 같은 꼴로 둘 있다.
- **보고서 읽기 쉬움(Task 8).** 관찰 표들이 개발자 말투다.

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
- **받은 yt-dlp의 무결성.** 지금 확인은 크기(1,000,000바이트 이상)와 "실행되어 버전을 찍는다"뿐이다(Kotlin과 같다). 서명·해시 확인을 둘지는 사용자 결정(아래).
- **`yt-dlp -U`와 `yt-dlp.exe.old`.** 위 "확인하지 못한 것". 업데이트를 화면에 잇기 전에 확인하거나, 업데이트 뒤 남은 `yt-dlp.exe.old`를 지우는 일을 둘지 정한다.

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
- **곡마다 작업 폴더 하나.** `M4aTagger`의 임시 파일 이름(`<id>.ffmeta`, `<id>.tagged.m4a`)은 파일 이름 줄기에서 나온다. 같은 영상이 재생목록에 두 번 있으면 한 작업 폴더에서 이름이 부딪힌다(yt-dlp의 출력도 이미 부딪힌다). 곡마다 작업 폴더를 따로 두거나 이름을 겹치지 않게 한다.
- **설치·업데이트를 내려받기와 겹치지 않게.** yt-dlp가 도는 동안 `installYtDlp`는 마지막 옮기기에서 접근 거부로 실패한다(Kotlin도 같고 옛 실행 파일은 그대로; 이제는 약 1.5초 다시 해 본 뒤 실패한다). 설치 둘이 동시에 돌면 `yt-dlp.new.exe`를 함께 써서 서로의 파일을 지울 수 있다. 설치·업데이트를 내려받기와, 그리고 서로와 한 줄로 세운다.
- **멈춘 뒤의 `fetch`.** `VideoMetadataSource::fetch`는 취소되면 그냥 "없음"을 돌려준다. 부르는 쪽이 멈춤 토큰을 스스로 본다.
- **`status()`의 시간.** 도구 셋을 차례로 돌리고 시간 한도가 없다. 어떤 도구 실행에도 시간 한도가 없다(Kotlin도 없다; 아래 사용자 결정).
- **`put`이 실패하면.** 옮기기 재시도 뒤에도 `put`이 실패하면 곡 파일은 작업 폴더에 남기고, 그 곡을 "실패, 다시 시도할 수 있음"으로 보고한다. 잃어버리지 않는다.
- **잘린 마지막 줄.** 멈춤으로 프로세스를 죽인 뒤에는 처리기에 반쪽짜리 마지막 줄(진행률 줄이나 JSON 줄)이 갈 수 있다. 진행률·JSON 파서가 이를 견뎌야 한다.
- **처리기는 막히지 않게.** 줄 처리기는 오래 막히면 안 된다(그동안 그 파이프를 읽지 못하고, 멈춤도 그 처리기가 끝나기를 기다린다). 처리기는 실행기를 부른 스레드에서 불릴 수도 있다(가짜 실행기가 그렇다).
- **다른 곳에서 프로세스를 띄우지 않기.** 내려받는 동안 앱의 다른 곳에서 `QProcess`나 핸들을 모두 물려주는 `CreateProcess`를 쓰면, 그 프로세스가 우리 파이프 끝을 물려받아 정상 종료한 `run`을 붙잡을 수 있다(정상 종료 쪽에는 2초 장치가 없다). 지금 `qt/`에는 그런 코드가 실행기와 시험 말고는 없다.
- **`parseVideoInfo`의 바이트 약속**(Q2a 보고서가 Q2b에 넘긴 것, 그대로). 정보 파일의 바이트를 미리 디코드하거나 BOM을 떼거나 줄바꿈을 바꾸지 않고 그대로 넘기고(`readVideoInfo(path)`가 그렇게 한다), yt-dlp가 그 파일을 다 쓴 뒤에 읽게 순서를 지킨다. Q2b에는 정보 파일을 읽는 작업 코드가 없어서 이 약속은 Q2d로 넘어간다.
- **영상 ID의 `%`.** Q2a의 영상 ID 규칙은 `%`를 받는다. `escapeOutputTemplateDirectory`는 폴더의 `%`만 겹치고, 출력 틀 `<폴더>\<ID>.%(ext)s`의 ID 부분은 겹치지 않는다. `%`가 든 ID가 실제로 오면 yt-dlp가 그것을 틀로 읽을 수 있다. Q2d에서 ID 부분도 겹칠지, 그런 ID를 거절할지 정한다(사용자 결정 후보).

## 사용자가 정할 것

지금 코드는 아래 "지금"대로 동작하고, 두 번째 관문은 그 동작으로 돌았다(결과는 위 "판정").

1. **오디오를 내려받는 통합 시험을 돌릴지.** `writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum`은 19초짜리 영상("Me at the zoo")의 오디오 약 300KB를 내려받는다(Task 7의 추정). 허락하면 그 시험 하나만 `XGS_INTEGRATION=1`과 `XGS_INTEGRATION_AUDIO=1`로 한 번 돌린다(유튜브 요청 한 번). 지금: 돌리지 않음.
2. **새 한국어 문구 둘의 표현.** `배치 파일(.cmd, .bat)은 실행하지 않습니다`, `실행할 프로그램이 없습니다`(7번). Kotlin에 짝이 없다. 지금: 그 문구.
3. **점·공백뿐인 파일 이름을 거절할지.** Kotlin은 `...`, `.. `를 받고 Windows가 그것을 폴더 자신으로 푼다(고치지 않은 동작 7). 지금: Kotlin처럼 받음.
4. **따른 Kotlin 동작을 Qt 판에서 고칠지.** id가 빈 항목이 쓸 수 있음(1), `[Private video]`·`availability`의 대소문자 구분(2), 빈 채널이 업로더로 대체되지 않음(3), ffmpeg가 아무 첫 줄에나 "찾음"(5), 업데이트의 마지막 줄이 스레드 시간에 달림(6). 고치면 Kotlin과 다르게 하는 문서화된 차이가 되고 대조에서 그 구역을 빼야 한다. 지금: 모두 Kotlin처럼.
5. **`QProcess`를 쓰지 않은 것(작성자 결정 1).** 설계 §3과 다르다. 지금: `CreateProcessW` + Job Object.
6. **실행 중에 정해진 다른 차이들**(위 9~15번: 멈춘 뒤의 `status()`, 마지막 옮기기 중 취소의 답, 던지지 않는 생성자, 이스케이프 함수, 예외를 결과로, 2초 장치)을 받아들일지.
7. **옮기기 재시도(위 16번)를 둘지, 그 시간.** Kotlin과 다르다. 막 쓴 파일을 다른 프로그램이 잠깐 쥐어도 곡이 실패하지 않게 하려는 것이고, 대가는 계속되는 실패(읽기 전용 대상, 실행 중인 yt-dlp)가 약 1.5초 늦게 보고되는 것이다. 지금: 10번 더, 쉬는 시간 합 1.52초. 멈춤의 2초 장치(15번)의 2초도 같은 조율 값이다. 지금: 2초.
8. **최종 리뷰 뒤의 다른 차이들**(17~20번: NUL이 든 명령 거절과 그 문구 `NUL 문자(U+0000)가 든 명령은 실행하지 않습니다`, 대기 실패를 취소가 아닌 실패로, 0바이트 쓰기를 실패로, 디렉터리가 아닌 출력 폴더에서 `put` 실패)을 받아들일지. 지금: 그렇게 함.
9. **`yt-dlp -U`의 남은 파일을 확인할지.** 위 "확인하지 못한 것". 확인하려면 yt-dlp 사본 하나로 실제 업데이트를 한 번 돌려야 하고(GitHub에서 새 실행 파일을 내려받아 실행한다), Kotlin 쪽과 비교하려면 두 번. 지금: 확인하지 않음. 남는다면 작성자 결정 5를 그 경우에만 풀지, 남은 파일을 지울지도 정한다.
10. **태그 쓰기 실패 문구의 영어 `Input length = 1`.** 짝 없는 서로게이트가 든 태그를 쓰지 못하면 사용자에게 `태그를 쓰지 못했습니다: Input length = 1`이 보인다(관찰 2). Kotlin에서는 JDK 예외 문구였고, Qt 판은 같은 글을 C++ 코드에 직접 적어 내보낸다. 한국어 문구로 바꿀지. 지금: Kotlin과 같은 영어 글.
11. **`PATH`에서 찾은 도구를 믿는 것.** `PATH`에 놓인 다른 `yt-dlp.exe`나 확장자 없는 `node`도 그대로 실행한다(Kotlin과 같다). 지금: 믿음.
12. **받은 yt-dlp의 확인.** 크기와 "실행된다"만 본다(Kotlin과 같다). 서명이나 해시를 볼지(Q2c). 지금: 보지 않음.
13. **도구 실행의 시간 한도.** 어떤 도구 실행에도 시간 한도가 없다(Kotlin과 같다). Q2d·화면에서 둘지. 지금: 없음.
14. **읽기 전용 대상 위에 덮어쓰기.** 출력 폴더에 같은 이름의 읽기 전용 파일이 있을 때 덮어쓰기 `put`은 (재시도 뒤) 실패한다. Kotlin이 어떻게 하는지는 관찰하지 않았다. 지금: 실패.
15. 최종 리뷰어가 판단하지 않고 넘긴 그 밖의 것들(모두 Kotlin과 같거나 이 계획 밖): 프로세스 트리를 죽이면 yt-dlp가 `%TEMP%`에 푼 `_MEI…` 폴더가 남는 것, 다른 드라이브로 옮길 때 전원이 꺼지면 반쪽 파일이 남을 수 있고 그 복사는 `put` 잠금 아래에서 멈출 수 없는 것, 자식의 작업 폴더가 앱의 것을 물려받는 것, 260 단위 넘는 프로그램 경로, Windows 10과 ARM64, 업데이트 결과의 마지막 줄이 스레드 시간에 달린 것(위 4번에 포함).
16. Q2a 보고서의 "사용자가 정할 것"(배열 깊이 한계, 영상 ID 규칙, 전각·CJK 괄호 등)과 Q1에서 남은 결정은 그대로 기다린다.

## 라이선스 메모

이 절은 과정의 기록이고 법적 의견이 아니다. 동작은 Kotlin 원본과 테스트, 공개 문서(Microsoft의 명령줄 인자 규칙, Win32 API 문서, Qt 문서), 그리고 오라클·탐침이 공개 API로 관찰한 답으로 정했다(위 관찰 표). Task 1의 보고서는 JDK 소스를 보거나 기억에서 옮기지 않았다고 적었다. 다른 Task의 보고서에는 같은 문장이 따로 없고, 모두 같은 규칙(계획의 Global Constraints) 아래에서 일했다. Task 8과 최종 리뷰 뒤의 수정 작업도 JDK와 `kotlinx.serialization`의 소스를 보지 않았다. 수정 작업이 Windows의 경로 풀이를 본 것은 공개 Win32 함수 `GetFullPathNameW`를 직접 불러서다. 금지 호출 점검의 JDK 내부 이름 검사는 두 번째 관문에서도 0건이다. `THIRD-PARTY-NOTICES.md`는 이 단계에서 바뀌지 않았다.

## 판정

**Q2b의 두 번째 관문은 한 단계를 빼고 충족됐다.** 이식 대상 Kotlin 단위 테스트 60개는 같은 이름으로 통과하고(이름 대조에서 빠진 것 0, 22개 클래스), 깨끗한 릴리스·디버그 빌드에서 릴리스 `ctest` 30개가 모두 통과했고, 실행 파일마다 돌린 결과는 두 빌드 모두 1061 통과·0 실패였으며, 금지 호출은 0건, 대조는 다섯 번의 실행에서 26개 종류 모두 불일치 0과 통계 문턱 통과, 디버그 도구는 릴리스와 같았고, 실제 도구 통합 시험은 ffmpeg 8개와 yt-dlp 5개가 통과했다(2개는 건너뜀). 관문 사이에 C++을 고치지 않았다.

**충족되지 않은 한 단계**: 디버그 `ctest`에서 `tst_system_process_runner`가 한 번 실패했고(어느 시험 함수인지 모름), 그 실행 파일만 한 번 다시 돌리니 통과했다. 지시대로 녹색이 될 때까지 되풀이하지 않았다. 이것을 관문 통과로 볼지는 controller와 사용자가 정한다.

이 진술이 다루지 않는 것: 오디오를 내려받는 통합 시험과 Q2d로 미룬 통합 시험, 다른 드라이브, Windows 10, 디버그 빌드의 통합 시험, 실제 HTTP·내려받기 작업·실제 취소 흐름(Q2c·Q2d), `yt-dlp -U`의 남은 파일, 원인을 모르는 `tst_local_folder_sink`의 흔들림(이번에는 재현되지 않았다)과 위 디버그 `ctest`의 실패 한 번. 사용자가 정할 것은 위에 모았다.

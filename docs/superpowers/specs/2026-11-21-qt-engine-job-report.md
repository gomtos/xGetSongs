# Qt Q2d(내려받기 작업·동시성·취소) 보고서

작성일: 2026-11-21
설계: [2026-11-15-qt-rewrite-and-playlists-design.md](2026-11-15-qt-rewrite-and-playlists-design.md) §2, §3, §4(Q2 행)
계획: [2026-11-21-qt-engine-job.md](../plans/2026-11-21-qt-engine-job.md)
이전 단계: [2026-11-20-qt-engine-lrclib-report.md](2026-11-20-qt-engine-lrclib-report.md)(Q2c), [2026-11-18-qt-engine-process-report.md](2026-11-18-qt-engine-process-report.md)(Q2b), [2026-11-15-qt-engine-logic-report.md](2026-11-15-qt-engine-logic-report.md)(Q2a)

환경: Windows 11 Home 10.0.26200, Visual Studio 2022(vcvars64), Qt 6.11.3(정적), 정답 쪽 JVM은 `server/build/sidecar-image`의 OpenJDK 21.0.9와 그 안의 `engine.jar`·`shared-jvm.jar`. Gradle은 돌리지 않았다. 시험은 창을 띄우지 않았다. 실행 중인 xGetSongs 앱은 건드리지 않았고 `%APPDATA%\xGetSongs`에는 아무것도 쓰지 않았다(거기 있는 `ffmpeg`·`yt-dlp.exe`를 통합 시험 둘이 실행만 했다).

**원본 출력.** 관문의 숫자는 모두 저장소 밖 작업 폴더 `C:\Users\gomto\AppData\Local\Temp\claude\46803f77e57fcdfd\004c41bb-2704-4ab0-875b-470a2bebd248\scratchpad\gate\`(아래 "관문 폴더")의 파일에서 옮겼고, 표마다 파일 이름을 적었다. 시각은 그 폴더의 `00-timeline.txt`에 있다. 그 폴더에는 Q2c 관문의 파일(날짜 2026-11-20, 예: `08-FAIL-debug-tst_local_folder_sink.txt`)도 남아 있고 번호가 이 관문의 파일과 겹친다. 이 관문의 파일은 날짜가 2026-11-21인 것이다(Q2c의 파일은 지우지 않았다). 관문 앞 정리(아래)의 실행은 같은 scratchpad의 `step0\`에 있다. 그 밖의 숫자(변이, Task별 대조 통계, Task별 시험 실행)는 각 Task가 자기 작업 중에 잰 것이고 그 자리마다 "Task N"이라고 적었다. 그 원본은 Task 보고서(`.superpowers/sdd/2026-11-21-qt-engine-job/task-N-report.md`)에 있다.

네트워크: 이 단계에서 `lrclib.net`에는 아무 요청도 보내지 않았다(`tst_real_lrclib`를 돌리지 않았다). 유튜브에는 관문에서 `tst_real_ytdlp`를 한 번 돌려 yt-dlp가 메타데이터 요청을 보냈다(오디오 없음). 오디오 내려받기(`XGS_INTEGRATION_AUDIO`), yt-dlp.exe 내려받기(`XGS_INTEGRATION_DOWNLOAD`), `yt-dlp -U`는 어디서도 돌리지 않았다. 사용자는 계획을 승인할 때 오디오 시험을 허락하지 않았다. 그래서 `tst_real_download`의 두 시험은 한 번도 돌지 않았다(아래 "확인하지 못한 것"). 단위 시험과 `ctest`는 네트워크를 쓰지 않고 실제 yt-dlp·ffmpeg를 띄우지 않는다.

## 읽는 법

| Task | 내용 | 커밋 |
|---|---|---|
| 1 | 곡 하나 내려받기(`ItemDownloader`)와 새 시험 15개 | `c5dbad2` |
| 2 | 작업 서비스(`DownloadRequest`, `JobHandle`, `DownloadService`, `DefaultDownloadService`, 이벤트 큐 `JobEvents`), Kotlin 시험 26개와 더한 시험 21개 | `06a3b6f` |
| 3 | `DefaultDownloadServiceTest`의 나머지 Kotlin 시험 77개 | `9e86448` |
| 4 | 작업 전체를 Kotlin과 대조(대조 op `W jb`) | `74dee77`, 리뷰 반영 `7a75710` |
| 5 | 진짜 실행기·진짜 출력 폴더로 작업 잇기(`tst_job_with_real_processes`)와 미룬 통합 시험(`tst_real_download`) | `b4d8adc` |
| 6 | 작업 5 리뷰의 작은 지적(관문 앞 정리), 관문, 이 보고서 | `2c7e1be`, 이 보고서 |
| - | 작업 6 리뷰 반영(관문 뒤) | `a2fd05c` |
| - | 최종 리뷰 반영(아래 "최종 리뷰") | `e921ea1`, 보고서 |

"Kotlin"은 저장소의 `engine/src/main/kotlin/com/xgetsongs/engine/job/ItemDownloader.kt`·`DefaultDownloadService.kt`와 `Services.kt`, 그 시험 `DefaultDownloadServiceTest.kt`다. "작업"은 내려받기 작업 하나(곡 목록 하나를 받는 일), "일꾼"은 곡을 하나씩 가져가 받는 스레드, "조정 스레드"는 작업마다 하나 있는, 일꾼을 띄우고 거두고 끝 이벤트를 내는 스레드다.

## 요약

- **관문은 모든 단계를 통과했다**(커밋 `2c7e1be`의 코드로; 관문 중에 `qt/core`를 고치지 않았다): 릴리스·디버그 깨끗한 빌드(컴파일러 경고 0), 두 빌드의 `ctest` 39개 모두 통과(실행 파일마다 합해 릴리스 1336 통과·0 실패·22 건너뜀, 디버그 1335·0·23, 시험 함수 784), 이름 대조에서 빠진 Kotlin 이름 0(26개 클래스, 일부러 더한 이름 68), 금지 호출 0, 대조 다섯 번 모두 26개 종류 불일치 0과 통계 문턱 260개 통과(디버그 도구도 릴리스와 같음), 되풀이 실행 다섯 시험 × 두 빌드 × 20번 = 200번 모두 통과, 통합 시험은 ffmpeg 8개와 yt-dlp 5개 통과.
- 옮긴 것: 곡 하나 내려받기(`ItemDownloader`), 작업 서비스(`DefaultDownloadService`, `DownloadRequest`, `JobHandle`, 이벤트 큐). Q2a~Q2c의 부품(실행기, 명령 조립, 진행률, 오류 분류, 정보 파일, 태그, 출력 폴더, 가사 공급자)이 한 작업으로 이어졌다. Kotlin `DefaultDownloadServiceTest`의 103개가 같은 이름으로 통과하고, 스레드·취소·큰 재생목록·부품의 예외를 보는 시험 21개, 다운로더 시험 15개, 진짜 실행기와 진짜 출력 폴더로 도는 시험 5개를 더했다. 이것으로 Q2(`engine`)의 이식이 끝났다.
- 코루틴 자리는 스레드다: 작업마다 조정 스레드 하나와 일꾼 `min(동시 수, 곡 수)`개, 취소는 `std::stop_source` 하나. 계획 작성자의 결정 15개를 그대로 따랐다(아래 "Kotlin과 다르게 한 것").
- **대조와 시험은 C++ 버그를 찾지 못했다.** 대조 `W jb`(동시 수 1의 작업 전체를 Kotlin과 무작위 비교)는 처음부터 불일치 0이었고, `qt/core/job`의 변이 여섯은 37~943건의 불일치로 잡혔다.
- **확인하지 못한 가장 큰 것**: 실제 유튜브 영상을 받는 `tst_real_download`의 두 시험은 사용자가 오디오 시험을 허락하지 않아 한 번도 돌지 않았다. 진짜 yt-dlp·ffmpeg로 받는 작업 전체와, 취소했을 때 진짜 도구의 프로세스 트리가 모두 죽는지는 모른다(가짜 도구와 진짜 실행기로는 확인했다). **(고침, 2026-11-21: 그 뒤 사용자가 허락해 돌렸고 통과했다. 아래 "관문 뒤 오디오 시험" 절.)**
- Q2b에서 원인 모르게 한 번 실패했던 `tst_system_process_runner`는 이번에 두 빌드 각 21번(되풀이 20 + `ctest` 1) 통과했다. Q2c에 이어 두 번째로 재현되지 않았고 원인은 여전히 모른다.
- 관문 전에 작업 5 리뷰의 작은 지적 넷을 한 커밋(`2c7e1be`)으로 고쳤다: 실제 내려받기 시험의 실패 문구와 이벤트 출력(곡 이름이 실릴 수 있던 글을 글자 수로), 같은 이름·같은 영상 시험에 "정말 겹쳤다"는 단언(시간이 아니라 사건으로), 프로세스 도우미가 재사용된 PID를 기다리지 않게.
- `lrclib.net`에는 이 단계에서 요청을 보내지 않았다.

## 관문 뒤 오디오 시험 (2026-11-21)

관문과 최종 리뷰가 끝나고 push한 뒤(`c47b7a2`), 사용자가 오디오를 받는 통합 시험을 허락했다. 릴리스 빌드(`build/qt`)로 `XGS_INTEGRATION=1`과 `XGS_INTEGRATION_AUDIO=1`을 주고 한 번씩 돌렸다. 디버그 빌드로는 돌리지 않았다.

| 실행 | 결과 | 본 것 |
|---|---|---|
| `tst_real_download` 전체 | `Totals: 4 passed, 0 failed, 0 skipped`, 35320 ms, 종료 코드 0 | `downloadsAShortVideoAsM4aWithoutReencoding`: 파일 하나(341135바이트), 알린 단계는 DOWNLOADING·FINISHING, 스트림은 aac 오디오와 360×360 mjpeg 표지, 가사 태그 0개. `cancellingARealDownloadLeavesNoToolProcessBehind`: 취소 때 작업의 프로세스 2개, 취소 뒤 0개 |
| `tst_real_ytdlp`의 `writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum` 하나 | `Totals: 3 passed, 0 failed, 0 skipped`, 7673 ms, 종료 코드 0 | 작업 폴더에 정보 파일·썸네일·m4a(309288바이트), album·artist·track은 null, 샌드박스의 캐시와 임시 폴더는 비어 있음 |

실행 뒤 `yt-dlp`·`ffmpeg`·`xgs-test-child` 프로세스는 남지 않았다. 이로써 진짜 yt-dlp·ffmpeg로 받는 작업 전체, 진짜 한 파일짜리 `yt-dlp.exe`의 프로세스 트리 종료, 새 실행 파일에서의 실제 LRCLIB 요청(HTTPS)이 릴리스 빌드에서 한 번 확인됐다. 여전히 돌리지 않은 것: `tst_real_lrclib`, `XGS_INTEGRATION_DOWNLOAD`, `yt-dlp -U`, 그리고 이 두 시험의 디버그 빌드 실행.

## 관문 표

### 빌드 (원본 `build-qt.txt`, `build-qt-debug.txt`, `build-summary.txt`, 미리 보기 `clean-dryrun-qt.txt`·`clean-dryrun-qt-debug.txt`)

코드는 커밋 `2c7e1be` 그대로였다(작업 트리에 바뀐 파일 없음, `01-head.txt`). `build/qt/vcpkg_installed`, `build/qt-debug/vcpkg_installed`, `C:\dev\vcpkg`는 지우지 않았다. 지우기 전에 `ninja -n -t clean`으로 지울 목록을 보았고 거기에 `vcpkg_installed`가 없었다(`clean-dryrun-*.txt`: 두 빌드 모두 `341 files.`, `vcpkg_installed`가 든 줄 0). VS 2022 vcvars64 안에서(`VCPKG_ROOT=C:\dev\vcpkg`) 빌드마다 `cmake --build <폴더> --target clean`, `cmake --preset <프리셋> -S qt`, `cmake --build <폴더>`를 이어 돌렸다(릴리스 다음 디버그).

| 항목 | 릴리스(`build/qt`) | 디버그(`build/qt-debug`) |
|---|---|---|
| clean | `Cleaning... 169 files.` | `Cleaning... 169 files.` |
| 구성 | `Configuring done (9.0s)`, `Generating done (1.4s)` | `Configuring done (8.3s)`, `Generating done (1.4s)` |
| 빌드 단계 | `[1/212]`~`[212/212]` | `[1/212]`~`[212/212]` |
| 걸린 시간(clean·구성·빌드 합) | 147초 | 125초 |
| 경고 | 컴파일러 경고 0(`/W4 /WX`). 출력에 `warning`이 든 줄은 Qt의 CMake 매크로(`Qt6CoreMacros.cmake:3311`)가 구성 때 낸 `CMake Warning` 하나뿐(Q2b·Q2c와 같음) | 같음 |

미리 보기의 341은 `ninja -t clean`이 그래프의 산출물을 모두 센 수이고, 실제 clean의 169는 있던 파일을 지운 수다. Qt는 다시 빌드되지 않았다.

### 테스트

| 실행 | 결과 | 원본 |
|---|---|---|
| 릴리스 `ctest --test-dir build/qt --output-on-failure` | `100% tests passed, 0 tests failed out of 39`, Total 104.50 sec | `04-ctest-release.txt` |
| 디버그 `ctest --test-dir build/qt-debug --output-on-failure` | `100% tests passed, 0 tests failed out of 39`, Total 155.46 sec | `05-ctest-debug.txt` |

두 실행 뒤 `build/qt*/qttest/`를 `04-ctest-release-qttest\`, `05-ctest-debug-qttest\`로 복사했고, 실행 파일마다의 `Totals:` 줄과 `-functions`로 센 시험 함수 수를 `14-totals-per-exe.txt`에 모았다. `--rerun-failed`는 쓰지 않았고 실패가 없었다. 두 `ctest`는 `XGS_INTEGRATION` 없이 돌렸으므로 통합 시험 실행 파일 넷은 `Passed`이지만 안의 시험 함수는 모두 건너뜀이다.

이번 단계에 생기거나 바뀐 실행 파일(릴리스와 디버그가 같은 수):

| 실행 파일 | 통과 | 실패 | 건너뜀 | 시험 함수 | 그중 |
|---|---|---|---|---|---|
| `tst_default_download_service` | 126 | 0 | 0 | 124 | `DefaultDownloadServiceTest` 103 + 더한 21. 릴리스 `ctest` 5.65초 |
| `tst_item_downloader` | 17 | 0 | 0 | 15 | Kotlin 짝 없음 |
| `tst_job_with_real_processes` | 7 | 0 | 0 | 5 | Kotlin 짝 없음. 릴리스 `ctest` 2.49초 |
| `tst_real_download` | 2 | 0 | 2 | 2 | `RealYtDlpIntegrationTest`의 1 + 더한 1(변수 없이 모두 건너뜀) |
| `tst_real_ytdlp` | 2 | 0 | 6 | 6 | 자리표시 슬롯을 지워 7에서 6(아래 "계획과 달라진 것") |
| 그 밖의 34개 | 1182(디버그 1181) | 0 | 14(디버그 15) | 632 | Q2c 최종 리뷰 반영의 표와 같은 수(디버그의 차이는 디버그에서 건너뛰는 `tst_qt_http_client`의 407 시험 하나) |
| **합계(39개)** | **1336**(디버그 **1335**) | **0** | **22**(디버그 **23**) | **784** | `14-totals-per-exe.txt`의 `TOTAL` 줄 |

784 = Q2c 최종의 639 + 이번 단계 146(`tst_default_download_service` 124, `tst_item_downloader` 15, `tst_job_with_real_processes` 5, `tst_real_download` 2) − 1(`tst_real_ytdlp`의 자리표시). 통과 1336 = Q2c 최종 릴리스의 1184 + 152(새 실행 파일 넷의 126 + 17 + 7 + 2), 건너뜀 22 = 21 + 2 − 1.

### 되풀이 실행 (`ctest --test-dir <폴더> --output-on-failure --repeat until-fail:20 -R ^<이름>$`)

다섯 시험을 두 빌드에서 20번씩 돌렸다(실패하면 그 자리에서 멈추고 `qttest/<이름>.txt`를 보관하게 했다; 스크립트는 scratchpad의 `gate-loops.ps1`). **200번 모두 통과, 실패 0.** 보관한 실패 출력은 없다. 칸은 "통과/돌린 수, 한 번에 걸린 시간의 최소~최대, ctest의 Total"이다(요약 `08-loop-summary.txt`).

| 시험 | 릴리스 | 원본 | 디버그 | 원본 |
|---|---|---|---|---|
| `tst_default_download_service` | 20/20, 5.02~5.80초, 107.26초 | `08-loop-release-tst_default_download_service.txt` | 20/20, 5.40~6.55초, 123.73초 | `08-loop-debug-tst_default_download_service.txt` |
| `tst_item_downloader` | 20/20, 0.13~0.15초, 2.78초 | `08-loop-release-tst_item_downloader.txt` | 20/20, 0.15~0.19초, 3.31초 | `08-loop-debug-tst_item_downloader.txt` |
| `tst_job_with_real_processes` | 20/20, 1.91~1.99초, 38.97초 | `08-loop-release-tst_job_with_real_processes.txt` | 20/20, 1.93~2.09초, 39.37초 | `08-loop-debug-tst_job_with_real_processes.txt` |
| `tst_local_folder_sink` | 20/20, 0.56~0.61초, 11.74초 | `08-loop-release-tst_local_folder_sink.txt` | 20/20, 0.66~0.73초, 13.80초 | `08-loop-debug-tst_local_folder_sink.txt` |
| `tst_system_process_runner` | 20/20, 27.37~31.53초, 576.07초 | `08-loop-release-tst_system_process_runner.txt` | 20/20, 26.67~32.61초, 593.55초 | `08-loop-debug-tst_system_process_runner.txt` |

두 `ctest`의 한 번씩을 더하면 각 시험이 빌드마다 21번 통과했다. 관문 앞 정리의 `step0\`에서 `tst_job_with_real_processes`를 두 빌드에서 20번씩 더 돌린 것(모두 통과)은 이 표에 넣지 않았다.

### 이름 대조 (`pwsh qt/tools/test-names.ps1`, 종료 코드 0, 원본 `06-test-names.txt`)

이번 단계에 생기거나 바뀐 줄과 마지막 줄(`DefaultDownloadServiceTest`의 괄호 안 21개 이름은 줄였다; 아래 "Kotlin과 다르게 한 것"과 Task 3 보고서에 모두 있다):

```
RealYtDlpIntegrationTest: Kotlin 5, Qt 8, missing 0, added 3 (anOutputFolderWithSpacesKoreanAndAnAmpersandWorks, aPercentSignInTheOutputFolderIsReportedNotSilentlyMisplaced, cancellingARealDownloadLeavesNoToolProcessBehind)
DefaultDownloadServiceTest: Kotlin 103, Qt 124, missing 0, added 21 (theDefaultRetryDelaysAreTwoAndFourSeconds, …, aPercentThatIsNotFiniteReachesTheReaderAsUnknown)
PASSED: every Kotlin test name is a Qt test name in 26 classes; only the 68 intended names were added
```

`RealYtDlpIntegrationTest`의 Qt 8은 `tst_real_ytdlp` 6과 `tst_real_download` 2를 합친 것이다. 나머지 24개 클래스의 줄은 Q2c와 같다. `tst_item_downloader`와 `tst_job_with_real_processes`는 Kotlin 짝이 없어 README의 목록에 적었다.

### 금지 호출 (`pwsh qt/tools/forbidden-calls.ps1`, 종료 코드 0, 원본 `07-forbidden-calls.txt`)

```
looked at 72 files under qt/core for the Q1 list (kt_text.* and kt_tables.inc excluded), 75 for the Q2a list, and 150 text files under qt/ (qt/spike excluded)
PASSED: 0 hits
```

### 대조 (`pwsh qt/tools/differential/run.ps1`)

다섯 번 모두 기본 종류 `UXMSCFDGTLYRNBZHOJEQVKAIWP` 26개(종류는 Q2c와 같고 `W`에 op `jb`가 더해졌다)를 한 번의 호출로 돌렸다(스크립트 `gate-diff.ps1`). 모두 종료 코드 0, 마지막 줄은 `PASSED (kinds UXMSCFDGTLYRNBZHOJEQVKAIWP, <건수> cases per case kind, seed <seed>)`다.

| 실행 | 원본 | 불일치 | 걸린 시간 |
|---|---|---|---|
| `-PerKind 10000 -Seed 20261115` | `09-diff-10000-seed20261115.txt` | 26개 종류 모두 0 | 237초 |
| `-PerKind 10000 -Seed 1` | `10-diff-10000-seed1.txt` | 모두 0 | 221초 |
| `-PerKind 10000 -Seed 987654321` | `11-diff-10000-seed987654321.txt` | 모두 0 | 225초 |
| `-PerKind 10000 -Seed 20261115 -DebugTool` | `12-diff-10000-seed20261115-debugtool.txt` | 모두 0, 디버그 도구는 26개 종류 모두 `0 differences from the release tool` | 462초 |
| `-PerKind 100000 -Seed 424242` | `13-diff-100000-seed424242.txt` | 모두 0(`W: 100000 cases, 0 mismatches`) | 1696초 |

**통계 문턱.** 실행마다 `(needs …)`가 붙은 줄이 282개이고(Q2c의 261 + `jb`의 21), 그중 문턱이 0보다 큰 260개가 모두 문턱 이상이다(`15-stats-summary.txt`: 다섯 실행 모두 `below=0`). 문턱은 낮추지 않았다. 측정값을 문턱으로 나눈 값이 가장 작은 것들은 Q2c와 같은 통계들이다(R 가짜 경로만 든 입력 96.7~97.3%(90%), P `rs` `OK` 42.8~43.3%(40%), V 길이 있음 32.4~32.9%(30%), I op `norm` 16.4~16.5%(15%)). `jb`의 통계는 그보다 넉넉하다(가장 작은 비는 아래 2% 통계의 seed 1, 2.6%/2% = 1.3).

**`W jb`의 통계**(분모는 `jb` 사례 수; 원본은 위 파일들의 `W: jb:` 줄. seed 20261115는 디버그 도구 실행도 같은 수):

| 통계 | 문턱 | seed 20261115 (994) | seed 1 (1005) | seed 987654321 (993) | 10만 seed 424242 (10149) |
|---|---|---|---|---|---|
| `Completed` | 40% | 76.1% | 77.3% | 74.7% | 75.5% |
| `Failed` | 10% | 23.9% | 22.7% | 25.3% | 24.5% |
| `ItemSkipped`가 있는 사례 | 15% | 36.4% | 38.9% | 36.1% | 36.8% |
| `ItemFailed`가 있는 사례 | 20% | 78.7% | 80.0% | 79.5% | 80.1% |
| 재시도가 일어남 | 10% | 46.7% | 46.6% | 46.4% | 46.4% |
| 두 번 재시도 뒤 포기 | 3% | 16.6% | 16.7% | 17.8% | 17.4% |
| `ItemDone` 가사 `DESCRIPTION` | 5% | 22.3% | 20.1% | 19.1% | 20.9% |
| `ItemDone` 가사 `ONLINE` | 5% | 14.3% | 15.3% | 16.4% | 14.6% |
| `ItemDone` 가사 `NOT_FOUND` | 5% | 20.3% | 17.2% | 18.3% | 17.9% |
| `ItemDone` 가사 `SEARCH_OFF` | 5% | 19.1% | 18.8% | 15.5% | 19.2% |
| `lowConfidence` 다시 파싱 | 10% | 37.7% | 40.9% | 40.3% | 40.4% |
| 한 작업에 같은 파일 이름 둘 이상 | 5% | 20.7% | 20.1% | 19.7% | 20.8% |
| `put`의 `AlreadyExists` | 3% | 11.6% | 10.6% | 11.4% | 11.5% |
| `put`의 `AlreadyExists` 뒤 건너뜀(**계획 밖, 구현자의 문턱**) | 2% | 3.7% | 2.6% | 3.9% | 3.7% |
| `put` 접근 거부 | 3% | 6.1% | 7.1% | 6.9% | 7.3% |
| `put` 디스크 가득 | 3% | 8.0% | 6.2% | 7.2% | 7.2% |
| 진행률 이벤트 셋 이상 | 20% | 90.5% | 90.2% | 87.4% | 89.8% |
| 유한하지 않은 백분율 | 2% | 54.8% | 56.2% | 58.1% | 56.7% |
| 앨범 덮어쓰기(태그를 씀) | 10% | 24.6% | 27.1% | 22.5% | 23.3% |
| 정보 파일이 깨졌거나 없음 | 10% | 41.9% | 38.7% | 39.7% | 41.4% |
| (보이기만) 맞춘 뒤에야 같은 사례: `N` / `AlreadyExists` 꼬리 | - | 545 / 39 | 565 / 44 | 577 / 41 | 5756 / 418 |

이 숫자는 Task 4 리뷰 반영의 표와 하나도 다르지 않다(관문 앞 정리는 대조 도구와 `qt/core`를 바꾸지 않았다). "맞춘 뒤에야 같은 사례"는 두 맞춤(아래 "Kotlin과 다르게 한 것"의 8번과 21번)이 실제 차이를 맞추고 있음을 보여 준다.

### 통합 시험 (`XGS_INTEGRATION=1`, 릴리스, 한 번)

`XGS_INTEGRATION_AUDIO`와 `XGS_INTEGRATION_DOWNLOAD`는 켜지 않았다(실행 전에 그 변수를 지웠다). `tst_real_lrclib`는 돌리지 않았다(Q2c에서 승인분을 다 썼다). `tst_real_download`는 오디오 허락이 없어 돌리지 않았다(위 `ctest`에서 건너뜀으로만 돌았다). 두 실행 파일을 차례로 한 번씩 돌렸고 18:58:22에 끝났다(`19-integration-exits.txt`; 시작 시각은 남기지 않았다). 대조의 첫 실행은 237초 걸려 19:01:12에 끝났으므로 18:57:15쯤 시작했다(`00-timeline.txt`). 두 통합 시험은 그 뒤에 시작했으므로 실행 전체(Qt Test가 잰 시간으로 5505 ms + 35146 ms, 약 41초)가 대조의 첫 실행과 겹쳤다. 대조의 결과는 시간과 상관없고, 두 통합 시험에는 짧은 시간에 기대는 단언이 없다.

| 실행 파일 | 결과 | 원본 | 내용 |
|---|---|---|---|
| `tst_real_ffmpeg_tagging` | `Totals: 10 passed, 0 failed, 0 skipped`, 5505 ms, 종료 코드 0 | `16-integration-ffmpeg.txt` | Kotlin 8개 모두 통과. 네트워크 없음 |
| `tst_real_ytdlp` | `Totals: 7 passed, 0 failed, 1 skipped`, 35146 ms, 종료 코드 0 | `18-integration-ytdlp.txt`(곡 이름을 지운 사본) | Kotlin 3 + 더한 2 통과. 건너뜀 1 = 오디오를 내려받는 `writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum`. Q2c의 둘째 건너뜀(자리표시 `downloadsAShortVideoAsM4aWithoutReencoding`)은 `tst_real_download`로 옮겨 없어졌다 |

`tst_real_ytdlp`는 출력에 재생목록 제목, 곡 이름이 든 파일 이름, 정보 파일의 앨범·가수·곡명 값을 찍는다(Q2b 때부터의 시험 출력). 관문 폴더에는 그 메시지에서 숫자와 정해 둔 영어 낱말 몇 개만 남기고 나머지를 `…`로 바꾼 사본만 두었고(파일 끝에 그렇게 적었다) 원래 출력은 지웠다. 남은 구조: 재생목록 100항목, 찍힌 25항목 중 `ok` 24·`WARN` 1, 설명란 80줄에서 가사 블록 54줄(빈 줄이 아닌 것 42, 606자; Q2b·Q2c와 같은 수), 끝에 시험용 캐시·임시 폴더가 비어 있음(`the sandbox held: cache [ ], tmp [ ]`).

### 관문 뒤 정리

`xgs-test-child`, `xgs-diff`, 대조의 JVM은 남지 않았다(`Get-Process`·`tasklist`; 떠 있는 `java.exe` 둘은 11-20부터 떠 있는 VS Code 자바 확장의 것이라 건드리지 않았다). `%TEMP%`에 관문 중에 생긴 폴더는 빈 `qt_temp-*` 셋뿐이었고 지웠다. 출처를 찾았다: Q2b의 `tst_tool_locator.exe -functions`가 불릴 때마다 빈 `qt_temp-XXXXXX` 하나를 남긴다(시험 객체가 쥔 `QTemporaryDir`를 `-functions`가 치우지 않고 끝나는 것으로 보인다; 코드는 보지 않았다). `test-names.ps1`과 실행 파일마다의 합을 모으는 내 스크립트가 `-functions`를 불렀을 때 생겼다. 다른 후보 셋(`tst_tool_manager`, `tst_video_info_file`, `tst_job_with_real_processes`)은 남기지 않았다. `%TEMP%`에는 앞 단계들의 같은 빈 폴더 31개가 남아 있다(내 것이 아니라 지우지 않았다).

## 대조가 찾은 C++ 버그

없다. Task 4의 첫 실행부터 모든 seed에서 불일치 0이었고, 관문의 다섯 번 실행도 0이다(위 대조 표). Kotlin 시험 103개도 C++ 버그를 찾지 못했다: Task 2의 26개는 시험 쪽 실수 하나(임시 서비스보다 오래 쥔 핸들)를 고친 뒤 통과했고, Task 3의 77개는 처음 빌드에서 두 빌드 모두 통과했다. Task 5의 진짜 실행기 시험도 `qt/core`의 버그를 찾지 못했다(진짜 실행기와 진짜 출력 폴더가 가짜들이 가정한 대로 움직였다). 관문에서도 `qt/core`를 고치지 않았다.

구현 중에 고친 것은 버그라기보다 이식의 실수 하나다: Task 1의 첫 판은 `download`의 콜백 인자 이름을 `emit`으로 지었는데, Qt가 `emit`을 빈 매크로로 정의해서 `emit(Progress{…})`가 아무 일도 하지 않는 식이 되었다. 시험 셋이 빈 이벤트 목록으로 실패해 잡혔고 이름을 `emitEvent`로 바꿨다.

## 변이 시험 (각 Task의 실행, 모두 되돌림)

| Task | 변이 | 잡은 곳 |
|---|---|---|
| 1 | `Progress(Finishing)`을 종료 코드 확인 앞으로 | `twoDownloadsOfTheSameDownloaderCanRunAtOnce` |
| 1 | `escapeOutputTemplateDirectory`를 뺌 | `aPercentSignInTheWorkFolderIsEscapedInTheCommand` |
| 1 | 가사 조회 뒤의 멈춤 확인을 뺌 | **잡히지 않음**. 아래 설명 |
| 2 | 일꾼 수 = 곡 수 | 열 슬롯, 그중 `fiveThousandItemsUseNoMoreThreadsThanTheConcurrency`(시험을 단단히 한 뒤), `concurrencyIsLimitedToTheRequestedNumber`, `itemsStartInListOrder` 등 |
| 2 | 치명적 실패가 다른 일꾼을 멈추지 않음 | `fatalErrorAbortsTheWholeJob`, `accessDeniedFromTheSinkAbortsTheJob`, `diskFullFromTheSinkAbortsTheJob`, `aRealDiskFullReasonInsideAFileSystemExceptionAbortsTheJob`. `aFatalFailureStopsTheOtherRunningItemsWithoutFailingThem`은 가짜가 멈춤을 끝없이 기다려 시간 초과로 끝났다(Task 3이 가짜의 기다림에 30초 한도를 두었다) |
| 2 | `JobDone`을 큐를 닫은 뒤에 넣음 | `JobDone`을 읽는 37개 슬롯 |
| 2 | 재시도 대기를 `sleep_for`로 | `cancellingDuringTheRetryDelayReturnsPromptly`, `cancellingWithItemsInEveryStageEndsPromptly` |
| 2 | `AlreadyExists`에도 디스크 가득 검사 | `aFileNamedLikeADiskFullErrorDoesNotAbortTheJob` |
| 2 | `put` 앞의 멈춤 확인을 뺌 | `aStopBeforeThePutDeliversNothing` |
| 2 | 작업 폴더 지우기를 다시 해 보지 않음 | `theWorkFolderIsRemovedEvenWhenAFileInItIsHeldForAMoment` |
| 2 | 멈춘 뒤의 진행률 줄을 거르는 확인을 뺌(Task 1 리뷰 후속) | `aLineCutShortByTheKillIsIgnored` |
| 3 | 앨범 태그 우선순위: 재생목록 제목을 사용자가 적은 이름보다 먼저 | 2 실패 |
| 3 | 가사 질의의 앨범에 사용자가 적은 이름 | 1 실패 |
| 3 | `SearchOff`와 `NotFound` 맞바꿈 | 12 실패 |
| 3 | `put`의 `AlreadyExists`를 늘 실패로 | `twoItemsWithTheSameNameDownloadedAtTheSameTimeWriteOneFileAndSkipTheOther`, `aNameTakenBetweenTheCheckAndThePutIsSkippedNotFailed` |
| 3 | `nextEvent`가 다시 큐 닫힘을 기다림(Task 2 리뷰 후속을 되돌림) | `nextEventGivesNothingAfterJobDone` |
| 4 | `Unavailable`을 실패로 셈 | 대조 `W` 1만 건(seed 20261115)에서 불일치 178 |
| 4 | 재시도 한 번 적게 | 269 |
| 4 | 진행률 솎아내기가 첫 값을 버림 | 943 |
| 4 | `Finishing`을 두 번 냄 | 871 |
| 4 | 앨범 태그: 영상의 앨범을 재생목록 제목보다 먼저 | 145 |
| 4 | `put` 뒤의 `AlreadyExists`를 늘 실패로(리뷰 반영에서 더함) | 37 |
| 5 | `escapeOutputTemplateDirectory`를 뺌 | `aWorkFolderAndAnOutputFolderWithPercentSpacesAndHangulWork`(`받은 m4a 파일을 찾을 수 없습니다.`) |

Task 4의 수는 리뷰 반영 뒤 마지막 생성기로 잰 것이다(그 전 생성기로는 176·247·912·833·148).

**Task 1의 잡히지 않은 변이.** `ItemDownloader`는 가사 조회 뒤에 멈춤 토큰을 한 번 더 본다(Kotlin에서 취소된 조회가 던질 자리). 이 확인을 빼도, 다음에 부르는 `M4aTagger::tag`가 무엇을 쓰거나 띄우기 전에 토큰을 보고 `Cancelled`를 돌려주므로 결과는 똑같이 `DownloadCancelled`이고 ffmpeg도 불리지 않는다. 밖에서 보이는 차이가 없는 변이라 시험이 잡을 수 없다. 내부를 들여다보는 탐침(예: `ToolPathProvider::current()` 호출 수 세기)을 더하지 않았고, 확인은 태거의 약속에 기대지 않으려고 남겼다.

Task 2의 첫 변이 실행에서 두 시험이 단단해졌다: 5,000곡 시험은 Windows가 스레드 ID를 다시 쓰고 처음 몇 스레드가 목록을 다 끝내서 "곡마다 스레드"를 잡지 못했다(가짜 `exists`가 1ms 자게 해서 잡는다). `JobDone`을 닫은 뒤에 넣는 변이는 처음에 Qt Test 실행 전체를 멈췄다(`done()`이 던짐, 이제 실패를 기록하고 돌아온다).

## 관문 앞 정리 (커밋 `2c7e1be`)

작업 5 리뷰의 작은 지적 넷을 관문 전에 한 커밋으로 고쳤다. 관문은 고친 코드로 돌았다. 모두 시험 쪽이고 `qt/core`는 바뀌지 않았다.

1. **`tst_real_download`의 취소 시험이 내는 실패 문구.** 원본이 짧아 취소가 먹기 전에 내려받기가 끝나면 작업은 `Completed`로 끝난다(작성자 결정 3이 허락한다). 그때 시험은 "진행률 이벤트가 없거나 도는 프로세스를 못 봄"이나 끝 상태 불일치로 실패해서 원인을 가렸다. 이제 `Completed`이면 "취소가 먹기 전에 내려받기가 끝났다, 더 긴 원본이 필요하다"고 밝혀 실패하고, 진행률 이벤트를 봤는지는 따로 단언한다.
2. **`tst_real_download`의 이벤트 출력.** 실패 문구에 넣는 이벤트 요약이 `ItemSkipped`의 이유와 `ItemFailed`의 문구를 그대로 찍었다. 그 글에는 도구의 stderr나 곡 이름이 든 경로가 들어갈 수 있다. 이제 종류, 순위, 글자 수만 찍는다.
3. **같은 이름·같은 영상 시험이 정말 겹쳤는지.** `tst_job_with_real_processes`의 `twoItemsWithTheSameNameAtTheSameTimeLeaveOneFile`과 `aSameVideoTwiceInOneJobDoesNotCollide`는 가짜 yt-dlp가 300ms 자는 것으로 두 곡이 겹치기를 바랐을 뿐, 겹쳤는지는 보지 않았다. 300ms에 기대는 단언을 더하면 느린 PC에서 흔들릴 수 있어서, 시험용 실행기 감싸개가 두 yt-dlp 실행이 서로를 기다린 뒤에 시작하게 했다(조건 변수, 30초는 안전망). 작업은 `ItemStarted`를 다운로더를 부르기 전에 넣으므로, 이렇게 하면 두 `ItemStarted`가 어느 곡이 끝나기 전에 큐에 들어간다. 시험은 이제 두 실행이 정말 만났는지(안전망이 먼저 지나가지 않았는지)와, 두 `ItemStarted`가 첫 `ItemDone`·`ItemSkipped`·`ItemFailed`보다 앞서는지를 단언한다(만남 단언은 Task 6 리뷰 반영에서 더했다). 덮어쓰기를 끈 쪽에서는 두 곡 모두 `exists` 확인을 지난 뒤 받기 시작하므로, "이미 존재"로 건너뛰는 곡은 `put`이 `AlreadyExists`를 받은 곡이다.
4. **`own_processes.h`의 `waitForNoneMentioning`.** 스캔에서 찾은 PID를 다시 열어 기다렸는데, 그 사이 그 PID를 다른 프로세스가 받았을 수 있다. 이제 스캔이 생성 시각과 명령줄을 검사한 바로 그 핸들(`SYNCHRONIZE` 포함)로 기다리고, PID로 다시 열지 않는다. 돌려줄 때는 늘 한 번 더 스캔하므로, 조회 권한으로는 열리지만 `SYNCHRONIZE`로는 열리지 않는 프로세스도 빠뜨리지 않고 남은 것으로 돌려준다(Task 6 리뷰 반영).

확인(`step0\`): 두 빌드에서 바뀐 시험 둘을 경고 없이 빌드했고, `tst_job_with_real_processes`를 두 빌드에서 20번씩 돌려 모두 통과(`Totals: 7 passed, 0 failed`, 한 번 약 2초), `tst_real_download`는 변수 없이 두 빌드 모두 `Totals: 2 passed, 0 failed, 2 skipped`(두 시험 모두 `set XGS_INTEGRATION=1 and XGS_INTEGRATION_AUDIO=1` 문구로 건너뜀).

## 최종 리뷰

범위 `8ffafae..a2fd05c` 전체를 본 최종 리뷰의 판정은 "고치면 준비됨"이었다. 심각(Critical)은 없고, 작업 코드는 Kotlin 원본과 맞는다고 보았다. 지적은 중요 1개와 작은 것 9개이고 모두 한 커밋(`e921ea1`)으로 고쳤다. 아래 "다시 돌린 것"의 수는 모두 그 커밋의 코드로 잰 것이다. 위 "관문 표"는 관문 때(`2c7e1be`)의 기록이라 그대로 두었다.

**중요 1: yt-dlp에 넘기는 URL을 아무 시험도 보지 않았다.** 가짜들은 영상 ID를 `-o` 틀에서만 읽고 URL 인자는 보지 않았다. 그래서 `item_downloader.cpp:119`가 다른 곡의 URL이나 맨 영상 ID를 넘겨도 모든 시험과 대조 `W jb`가 통과하고, 사용자는 맞는 파일 이름에 다른 노래를 받았을 것이다(지금 코드는 맞다). controller 결정대로 단위 시험만 더했다(`W jb`의 답 꼴은 그대로). `tst_item_downloader`의 `aPercentSignInTheWorkFolderIsEscapedInTheCommand`가 이제 기록된 명령 전체를 `downloadCommand(testTools(), watchUrl(id), escapeOutputTemplateDirectory(workDir), id)`와 비교하고, `tst_default_download_service`에 곡 셋을 동시 수 2로 받아 곡마다의 명령이 그 곡의 URL로 끝나고 명령 전체가 그 곡의 `downloadCommand`와 같은지 보는 `everyItemsYtDlpCommandCarriesThatItemsOwnWatchUrl`을 더했다. 변이로 확인했다(릴리스, 바꾼 줄은 둘 다 되돌림):

| 119줄 변이 | 잡은 곳 | 잡지 못한 곳 |
|---|---|---|
| URL 대신 맨 영상 ID | `aPercentSignInTheWorkFolderIsEscapedInTheCommand`(목록이 24번째에서 다름), `everyItemsYtDlpCommandCarriesThatItemsOwnWatchUrl` | `tst_job_with_real_processes`(7 통과) |
| 처음 받은 곡의 URL을 모든 곡에(함수 안 `static`) | `everyItemsYtDlpCommandCarriesThatItemsOwnWatchUrl`, 그리고 `otherFailuresDoNotStopTheRemainingItems`(그 가짜는 명령을 이어 붙인 글에서 첫 곡의 ID를 찾는다) | `tst_item_downloader`(곡 하나뿐이라 볼 수 없다), `tst_job_with_real_processes` |

**작은 지적과 한 일.**

1. `aStopBeforeThePutDeliversNothing`의 주석과 이 보고서가 "흔들리지 않고 덜 증명할 뿐"이라고 과장했다. 그 말은 태거가 느릴 때만 맞다. 파일을 쥔 도우미 스레드가 `fs::moveFile`의 1.52초보다 늦게 깨어나면 태그 쓰기가 멈춤 전에 실패하고 `Cancelled` 단언이 실패한다. 주석과 "확인하지 못한 것"의 시간 문단을 바로잡았다. 시험은 그대로다.
2. `cancellingDuringTheRetryDelayReturnsPromptly`의 2초 안에 작업 폴더 지우기(최대 1.52초 다시 해 보기)도 들어간다. `JobDone`은 지우기 뒤에 큐에 들어가므로 "`JobDone`까지"로 재도 같은 시간이라, 한도를 이웃 시험처럼 5초로 올렸다.
3. 곡의 작업 폴더를 못 만들었는데 오류 코드가 0이면 문구가 `파일 처리 중 오류: ` + 0번 오류의 문구(성공했다는 말)였다. 이제 `파일 처리 중 오류: 작업 폴더를 만들 수 없습니다.`다("Kotlin과 다르게 한 것" 22번). 지금 있는 가짜들로는 이 길에 닿을 수 없어서 시험이 없다.
4. 일꾼 스레드 하나를 띄우지 못하면 예전 `catch`가 요약을 건너뛰어, 이미 뜬 일꾼이 `ItemDone`·`ItemFailed`를 냈어도 0/0/0이었다. 이제 `runWorkers`의 실패를 그 자리에서 받고 요약은 늘 센 값으로 낸다(끝 상태는 전처럼 `Failed`). 스레드 만들기를 실패하게 할 길이 없어 시험은 없다.
5. `start`가 던질 수 있는 것이 적혀 있지 않았고, 빈 `request.sink`는 일꾼 스레드에서 따라가졌다. controller 결정대로 `start`는 빈 `sink`에 아무것도 만들기 전에 `std::invalid_argument`를 던지고(`aRequestWithoutASinkIsRefusedBeforeAnythingIsCreated`: 던지고, 작업 폴더가 생기지 않고, 명령 0개), `download_service.h`에 그것과 `std::system_error`(작업 스레드를 못 띄움), `std::bad_alloc`을 적었다.
6. `fs::makeDirectories(path, errorCode)`에 자기 시험이 없었다. `tst_file_ops`에 `makeDirectoriesReportsTheWindowsErrorOfTheLastFolder`를 더했다(길에 파일이 있으면 거짓과 183, 새로 만들거나 이미 있으면 참과 0).
7. 관문 뒤의 시험 변경(커밋 `a2fd05c`: `tst_job_with_real_processes.cpp`의 만남 단언, `own_processes.h`의 다시 스캔)을 돌렸다는 말이 없었다. 그 커밋 뒤에 그 두 파일을 돌린 기록은 이 보고서에 없다. 최종 머리(`e921ea1`)에서 두 빌드의 `ctest`와 `tst_job_with_real_processes` 20번 되풀이로 돌렸다(아래). `own_processes.h`를 쓰는 다른 시험 `tst_real_download`는 오디오 허락이 없어 건너뜀으로만 돌았다.
8. 미리 멈춘 소스 없이 시작하자마자 취소하는 시험이 없었다. `startingAndCancellingAtOnceEndsWithOneJobDoneCancelled`를 더했다: 서비스 하나로 300번 `start`, 0~2.95ms 바쁜 기다림, `cancel`, 모두 읽기. 매번 `JobDone`이 정확히 하나이고 마지막이며 `Cancelled`이고, `ItemDone`·`ItemSkipped`·`ItemFailed`가 없고, 임시 폴더가 비었는지(아직 안 만들어졌으면 없는지) 본다. 가짜 yt-dlp는 멈출 때까지 기다리므로 마지막 곡이 끝난 뒤의 취소는 생길 수 없고(결정 3이 `Completed`를 허락하는 유일한 경우), 끝 상태는 늘 `Cancelled`여야 한다. 기다림이 없던 첫 판은 300번 모두 작업 스레드가 멈춤을 보기 전에 취소가 닿아(진단 출력으로 셈: `ItemStarted`가 있는 번 0, yt-dlp 실행 0) 미리 멈춘 소스와 다를 바 없었다. 그래서 기다림을 넣었고, 세 번 잰 결과 300번 중 228~234번이 `ItemStarted`까지 갔고 yt-dlp가 179~198번 돌았다(진단은 지웠다).
9. Q4 인계에 "타이머로 `tryNextEvent`를 부르면 `finished()`가 참이 된 뒤에도 이벤트가 남아 있을 수 있으니 한 번 더 비운다"를 더했다("Q3·Q4에 넘길 것"의 첫 줄).

**미룬 작은 것들의 분류(최종 리뷰).** Task 1 M5는 문제 아님. Task 1 M2는 밖에서 차이가 보이지 않는 변이라 적어 둔 채로 둔다. Task 2 m5(`noexcept` 안의 `bad_alloc`)는 적어 둔 채로 둔다. Task 3의 작은 것 5~6은 문제 아님. `run.ps1`의 `Format-JbFolded`가 줄의 앞부분으로 맞추는 것은 사례마다 순위가 서로 달라 실제로는 문제 아님. 2% 문턱은 사용자가 정할 것으로 둔다("사용자가 정할 것" 3). 리뷰가 본 Kotlin과의 세 차이는 타당하고 이 보고서에 적혀 있다. `tst_tool_locator -functions`가 `%TEMP%\qt_temp-*`를 남기는 것은 다음 단계 처음에 고친다.

**다시 돌린 것(최종 머리 `e921ea1`의 코드, 원본은 scratchpad의 `fixwave\`와 `clean-build-*.txt`).**

- 깨끗한 빌드(`--target clean`, 구성, 빌드): 릴리스·디버그 모두 `Cleaning... 169 files.`, `[1/212]`~`[212/212]`, 컴파일러 경고 0(`warning`이 든 줄은 Qt CMake 매크로의 `CMake Warning` 하나뿐), 155초·132초.
- `ctest`: 릴리스 `100% tests passed, 0 tests failed out of 39`(94.32초), 디버그 같음(115.42초). 실행 파일마다 합해 릴리스 1340 통과·0 실패·22 건너뜀, 디버그 1339·0·23. 관문보다 4 많다: `tst_default_download_service` 126→129(시험 함수 124→127), `tst_file_ops` 12→13(함수 10→11). 시험 함수는 784→788.
- 되풀이 20번(`ctest --repeat until-fail:20`): `tst_default_download_service`, `tst_item_downloader`, `tst_job_with_real_processes`를 두 빌드에서 20번씩, **120번 모두 통과**. 한 번에 걸린 시간: 릴리스 6.32~6.83초, 0.13~0.15초, 1.94~2.01초; 디버그 7.57~8.01초, 0.15~0.18초, 2.03~2.09초(새 300번 슬롯으로 `tst_default_download_service`가 관문보다 1초쯤 길다).
- 이름 대조: 종료 코드 0, `DefaultDownloadServiceTest: Kotlin 103, Qt 127, missing 0, added 24`, `PASSED: every Kotlin test name is a Qt test name in 26 classes; only the 71 intended names were added`(관문의 68 + 3).
- 금지 호출: 종료 코드 0, `PASSED: 0 hits`(같은 파일 수 72·75·150).
- 대조(`qt/core`가 바뀌어서 한 번): `pwsh qt/tools/differential/run.ps1 -PerKind 10000 -Seed 20261115`, 종료 코드 0, 220초, 26개 종류 모두 `0 mismatches`, 마지막 줄 `PASSED (kinds UXMSCFDGTLYRNBZHOJEQVKAIWP, 10000 cases per case kind, seed 20261115)`. `(needs …)` 줄 282개 가운데 문턱이 0보다 큰 260개가 모두 문턱 이상(`below=0`). `W jb`의 수는 관문의 같은 seed와 같다(예: `Completed` 756/994 = 76.1%, `Failed` 238/994 = 23.9%). 10만 건 실행은 하지 않았다.
- 이름 대조가 부른 `tst_tool_locator -functions`가 남긴 빈 `qt_temp-*` 하나는 지웠다(전부터 있던 31개는 두었다).

## Kotlin과 다르게 한 것

1~15는 계획 작성자가 정했고(사용자가 바꿀 수 있다), 16부터는 실행 중에 Task 구현자나 controller가 정했다. **실행 중에 정한 것은 사용자가 아직 보지 않았다.**

### 계획 작성자가 정한 것

1. **코루틴 대신 스레드.** 작업 하나는 조정 스레드 하나와 일꾼 `min(max(동시 수, 1), 곡 수)`개로 돌고, 일꾼은 곡을 목록 순서대로 가져간다(커서 하나를 원자적으로 늘림). 곡마다 스레드를 만들지 않는다. 예: 5,000곡에 동시 수 64면 일꾼은 64개다(`fiveThousandItemsUseNoMoreThreadsThanTheConcurrency`). 스레드는 `std::jthread`만 쓴다.
2. **취소된 곡은 이벤트를 내지 않는다**(Kotlin과 같다). C++에서는 예외 대신 결과로 알린다: `download`의 결과에 `DownloadCancelled`가 있고, `prepare` 뒤에는 부른 쪽이 멈춤 토큰을 본다. 예: 내려받는 중 취소하면 그 곡의 `ItemFailed`도 `ItemDone`도 없고 `JobDone(Cancelled)`만 온다(`cancellingStopsRunningDownloadsAndReportsCancelled`).
3. **작업의 끝 상태.** 치명적 실패가 있었으면 `Failed`, 아니고 사용자가 멈춤을 요청했으면 `Cancelled`, 아니면 `Completed`. 예: 접근 거부로 작업이 멈춘 뒤 사용자가 취소해도 `Failed`다(`aUserCancelAfterAFatalFailureIsStillFailed`). 마지막 곡이 끝난 직후의 취소는 Kotlin처럼 어느 쪽으로도 보일 수 있다.
4. **`start`는 멈춤 소스를 받을 수 있다**(기본은 새 것). Kotlin의 "시작 직후 취소" 시험은 코루틴 디스패처 덕에 순서가 정해져 있지만 스레드에서는 경합이라, 이미 멈춘 소스로 시작해 "폴더를 만들지 않고 `JobDone(Cancelled)` 하나"를 본다(`cancellingRightAfterStartStillEndsWithJobDoneCancelled`). 가사 공급자가 취소를 던지는 Kotlin 시험도 공급자 안에서 같은 소스에 멈춤을 요청하고 빈 값을 돌려주는 꼴로 옮겼다(`aCancellationExceptionFromTheProviderIsNotSwallowed`).
5. **출력의 예외를 결과로 읽는 규칙.** Kotlin은 예외의 형으로, C++은 `PutResult`의 상태·문구·Win32 코드로 가른다. `Stored`는 `ItemDone`, `AlreadyExists`는 덮어쓰기가 꺼져 있고 지금 `exists`가 참이면 `ItemSkipped("이미 존재")`, 아니면 `ItemFailed("파일 처리 중 오류: " + 문구)`(문구가 비면 파일 이름), 코드 5는 `출력 폴더에 쓸 권한이 없습니다.`로 작업 중단, 코드 112·39나 문구에 `no space left`·`not enough space`·`disk full`·`공간이 부족`이 있으면 `디스크 공간이 부족합니다.`로 작업 중단, 그 밖의 `Failed`는 `파일 처리 중 오류: ` + 문구. `AlreadyExists`에는 디스크 가득 검사를 하지 않는다. 곡의 작업 폴더를 만들지 못한 것도 같은 `Failed` 줄로 읽는다(코드는 `GetLastError`). 예: 공유 위반(코드 32)은 그 곡만 실패(`aSharingViolationFromTheSinkFailsOnlyThatItem`), 코드 112는 문구 없이도 작업 중단(`aDiskFullCodeFromTheSinkAbortsTheJob`).
6. **실행기가 프로그램을 띄우지 못했거나 놓쳤을 때**(`StartFailed`·`Failed`; Kotlin의 `IOException`) 그 곡은 `파일 처리 중 오류: ` + 실행기의 문구이고, 문구에 디스크 가득 표식이 있으면 작업 중단이다. 예: `aProgramThatCannotBeStartedFailsTheItem`, `aStartFailureThatSaysTheDiskIsFullAbortsTheJob`.
7. **재시도 대기는 끊을 수 있고 주입한다.** 기본은 Kotlin의 2초·4초(`theDefaultRetryDelaysAreTwoAndFourSeconds`). 대기 중 취소되면 곧 돌아온다(`cancellingDuringTheRetryDelayReturnsPromptly`). 시험은 1ms를 넣는다(Kotlin 시험은 가상 시간).
8. **유한하지 않은 백분율은 "모름"으로 보낸다**(Q2a 인계). 솎아내기는 Kotlin의 `toInt()` 수(NaN은 0)로 그대로 하고 이벤트의 `percent`만 비운다. 예: NaN 다음 `0`은 버려지고, `50` 다음 NaN은 "모름"으로 간다(`aPercentThatIsNotFiniteIsReportedAsUnknown`, `aPercentThatIsNotFiniteReachesTheReaderAsUnknown`). 대조는 Kotlin 답의 `N`을 "모름"으로 맞춘 뒤 비교한다(seed 424242 10만 건에서 이 맞춤으로만 같아진 사례가 5756개).
9. **출력 틀의 `%`.** 작업 폴더 경로는 `escapeOutputTemplateDirectory`를 거쳐 넘기고(Q2b 인계), 영상 ID에 `%`가 있으면 아무것도 돌리지 않고 그 곡은 `downloadCommand`의 금지 글자 문구로 실패한다(`FailureKind::Other`). `downloadCommand`가 던지는 `std::invalid_argument`도 그 곡 하나의 실패다(Q2a 인계). 예: `aVideoIdWithAPercentSignFailsTheItemWithoutRunningAnything`, 진짜 실행기로 `100% %(id)s 한글 폴더`(`aWorkFolderAndAnOutputFolderWithPercentSpacesAndHangulWork`).
10. **`put` 직전의 취소.** 태그까지 쓴 곡이라도 `put`을 시작하기 전에 멈춤이 요청됐으면 옮기지 않는다(취소된 곡, 이벤트 없음). `put`이 시작됐으면 끝까지 가고 성공이면 `ItemDone`이다. 예: `aStopBeforeThePutDeliversNothing`(출력 폴더에 파일 없음).
11. **`put`이 실패한 곡의 파일은 Kotlin처럼 작업 폴더와 함께 지운다.** Q2b 보고서는 "남겨 두고 다시 시도"를 제안했지만, 남기면 임시 폴더가 쌓이고 다시 옮기는 화면이 없다. 사용자는 그 곡을 다시 받으면 된다.
12. **작업 폴더 지우기는 다시 해 본다.** 한 번에 안 지워지면 `fs::moveFile`과 같은 쉼으로 모두 1.52초 동안 다시 해 본다(Kotlin은 한 번). 그래도 남으면 조용히 둔다. 예: 작업 폴더의 파일을 공유 없이 잠깐 쥐어도 폴더가 지워진다(`theWorkFolderIsRemovedEvenWhenAFileInItIsHeldForAMoment`). 그 대신 최악의 경우 핸들의 소멸자가 그만큼 기다릴 수 있다.
13. **일꾼 스레드에서 새어 나온 예외는 그 곡의 실패다.** `std::exception`은 `what()`(UTF-8로 읽음), 그 밖은 `알 수 없는 오류`. 가사 공급자가 던진 것은 Kotlin처럼 "가사 없음"이다(`std::bad_alloc`만 다시 던진다, Kotlin이 `OutOfMemoryError`를 잡지 않는 자리). 예: `anExceptionOnAWorkerThreadFailsOnlyThatItem`(던진 `int`는 `알 수 없는 오류`, 다른 곡은 성공).
14. **이 계획에 넣지 않은 것.** yt-dlp 설치·업데이트와 내려받기를 겹치지 않게 하기, `server`의 `JobRegistry`·`ResolveCache`·`JobLog`, 구글 가사, 이벤트를 화면 스레드로 넘기는 다리(여기서는 블로킹 `nextEvent`와 안 막는 `tryNextEvent`만), 도구 실행의 시간 한도. 아래 "Q3·Q4에 넘길 것".
15. **실제로 내려받는 시험은 이 계획에서 돌리지 않는다.** `downloadsAShortVideoAsM4aWithoutReencoding`과 "내려받는 중 취소하면 실제 ffmpeg 자손까지 죽는가"(`cancellingARealDownloadLeavesNoToolProcessBehind`)는 `XGS_INTEGRATION=1`과 `XGS_INTEGRATION_AUDIO=1`이 둘 다 있을 때만 돈다. 사용자가 허락하지 않아 건너뛰는 것만 확인했다.

### 실행 중에 더해진 것

16. **멈춘 뒤 `put` 전에 끝난 곡은 예외로 끝나도 취소된 곡이다**(Task 2). 멈춤이 요청된 뒤, `put` 전에 곡이 예외나 `Downloaded`가 아닌 결과로 끝나면 이벤트를 내지 않는다. Kotlin이라면 취소 중에 던져진 `CancellationException`이 아닌 예외는 실패로 보고될 수 있다. C++의 부품이 멈춤에 던지는 것은 Kotlin의 `CancellationException` 자리라고 보고 결정 2와 맞췄다(헤더에 적음). 예: 내려받는 중 취소했는데 실행기가 그 순간 던지면, Kotlin은 그 곡의 `ItemFailed`를 낼 수 있고 C++은 아무것도 내지 않는다.
17. **첫 치명적 실패가 멈춤을 요청한 뒤의 두 번째 치명적 실패도 취소된 곡이다**(Task 2). 이벤트도 없고 세지도 않는다(`put`이 이미 시작된 곡만 예외). Kotlin에서는 경합이다. 두 곡이 정확히 동시에 치명적으로 실패하면 둘 다 `ItemFailed`이고 둘 다 센다(Kotlin과 같다).
18. **`fs::makeDirectories(path, errorCode)` 겹쳐 쓰기**(Task 2). 곡의 작업 폴더를 만들지 못했을 때 결정 5가 Win32 코드를 요구하는데, 기존 `makeDirectories`는 마지막에 `isDirectory`로 확인하면서 마지막 오류를 덮어썼다. 그래서 마지막 `CreateDirectoryW`의 `GetLastError`를 돌려주는 겹쳐 쓰기를 더했다(`anItemFolderThatCannotBeMadeIsReadLikeASinkFailure`, 최종 리뷰 뒤로는 `tst_file_ops`의 `makeDirectoriesReportsTheWindowsErrorOfTheLastFolder`도: 길에 파일이 있으면 거짓과 183, 성공이면 참과 0).
19. **이미 멈춘 토큰이면 `download`는 첫 일로 `DownloadCancelled`**(controller 결정, Task 1 전). 도구 찾기보다 앞이다. Kotlin이라면 첫 중단점에서 던질 자리다(`aStopBeforeTheRunRunsNothing`).
20. **`JobHandle::join`은 한 스레드에서만**(Task 2). `cancel`은 어느 스레드에서나 안전하다. `nextEvent`는 `JobDone`을 가져간 뒤 큐가 닫히기를 기다리지 않고 곧 "없음"을 준다(Task 3).
21. **`AlreadyExists`의 꼬리 문구.** C++의 `PutResult`가 주는 문구는 Kotlin `FileAlreadyExistsException`의 메시지와 다를 수 있어서, 그 곡이 실패할 때의 `파일 처리 중 오류: …` 꼬리는 Kotlin과 다를 수 있다(결정 5). 대조는 꼬리가 빈 문구가 아닐 때만 꼬리를 빼고 비교한다(seed 424242에서 418개). 빈 문구일 때는 C++이 파일 이름을 쓰고 Kotlin 메시지도 파일 이름이라 그대로 비교한다.
22. **곡의 작업 폴더를 만들지 못했는데 Windows 오류 코드가 없을 때**(최종 리뷰, controller 결정). 경로를 만들 수 없거나 만든 폴더가 곧 사라지면 코드가 0이고, 전에는 0번 오류의 문구("작업을 완료했습니다" 꼴)가 붙어 `파일 처리 중 오류: ` 뒤에 성공했다는 말이 나왔다. 이제 그때의 문구는 `파일 처리 중 오류: 작업 폴더를 만들 수 없습니다.`다. Kotlin이라면 `Files.createDirectories`가 던진 예외의 문구가 붙는다. 지금 있는 가짜들로는 이 길에 닿을 수 없어 시험이 없다(경로는 늘 만들어지고, 폴더가 만든 직후 사라지는 순간을 가짜가 끼어들 자리가 없다).

## 고치지 않은 Kotlin 쪽 동작

C++ 판이 그대로 따른다(대조나 시험에서 같은 답).

1. **마지막 곡이 끝난 직후의 취소는 `Completed`로 보일 수 있다**(결정 3). 사용자가 취소 단추를 눌렀는데 "완료"가 보일 수 있다는 뜻이다. 화면이 이것을 어떻게 보일지는 Q4의 일이다.
2. **`put`이 실패한 곡은 받은 파일을 잃는다**(결정 11). 다시 옮길 길이 없다.
3. **서비스는 동시 수에 위 한도를 두지 않는다**(`theServiceSetsNoUpperLimitOfItsOwn`). 동시 수 64면 yt-dlp 64개가 함께 돈다. 자동 동시 수(`automaticConcurrency`, Q2a)는 서비스가 아니라 부르는 쪽이 정한다.
4. **순위가 999를 넘는 곡은 파일 이름을 만들 수 없어 실패한다**(`filename::format`의 `MaxRank`, Task 2가 5,000곡 시험을 짜며 본 것). 재생목록은 해석 단계에서 999개로 잘리므로(Q2b `aPlaylistOfExactly999IsNotTruncated`) 실제로는 만나지 않는다.
5. **짝 없는 서로게이트가 든 태그 글은 `태그를 쓰지 못했습니다: Input length = 1`로 실패한다**(Task 4가 대조에서 양쪽 같음을 봄, Q2b에서 옮긴 동작).
6. Task 4는 작업 자체에서 새로 이상한 Kotlin 동작을 찾지 못했다. Q2a~Q2c 보고서의 "고치지 않은 Kotlin 쪽 동작"은 그대로 남아 있다.

## 계획과 달라진 것

- **콜백 인자 이름 `emitEvent`**(Task 1). 계획의 `emit`은 Qt 매크로라 쓸 수 없다(위 "대조가 찾은 C++ 버그"의 둘째 문단). 형과 자리는 계획대로다.
- **이미 멈춘 토큰의 순서**(위 19번). 계획의 `download` 순서에는 적혀 있지 않았다.
- **`fs::makeDirectories` 겹쳐 쓰기**(위 18번). Task 2의 파일 목록 밖이다.
- **`test-classes.json`의 `added` 21개는 Task 3이 넣었다**(Task 2는 넣지 않음, controller 지시).
- **대조 `W jb`의 표 밖 것들**(Task 4와 그 리뷰 반영):
  - `put` 대본 항목 `a`: "다른 프로그램이 먼저 같은 이름을 넣어서 `AlreadyExists`"(이름이 출력의 이름 집합에 들어간 뒤 `AlreadyExists`). 처음 판에서는 동시 수 1이라 `exists` 확인과 `put` 사이에 이름이 생길 일이 없어서 결정 5의 "`AlreadyExists`, 덮어쓰기 꺼짐, 지금 `exists`가 참 → 건너뜀" 줄이 한 번도 돌지 않았다. 리뷰가 잡았다.
  - 답 구역 P(`put`마다 순위와 파일 이름, `AlreadyExists` 맞춤이 자기 줄을 찾으려고)와 T(메타데이터 조회마다 영상 ID). 둘 다 비교한다. Kotlin 쪽이 예외로 끝나면 오라클 전체가 멈추지 않고 그 사례에 `THROW`로 답한다.
  - **새 통계 "`put`이 `AlreadyExists`를 받고 곡은 건너뜀", 문턱 2%**. 계획의 표에 없고 문턱은 구현자가 골랐다. Task 4 리뷰 반영의 측정은 2.6~3.9%, 관문은 위 "관문 표"의 `W jb` 통계. 계획 표의 문턱은 하나도 바꾸지 않았다.
  - 생성기는 코드 112·39를 디스크 가득 낱말이 든 문구와만 쓴다. Kotlin 쪽 가짜는 문구로만 디스크 가득을 알 수 있어서(결정 5는 "Kotlin의 문구" ↔ "C++의 코드나 문구"), 코드만 있는 경우는 양쪽에 같은 뜻으로 만들 수 없다. 코드만의 길은 단위 시험 `aDiskFullCodeFromTheSinkAbortsTheJob`이 본다.
  - `put` 대본에서 접근 거부를 6%에서 9%로 올렸다(seed 987654321이 3.7%로 문턱 3%에 가까워서; 문턱은 그대로).
- **`qt/tests/helpers/own_processes.h`**(Task 5, 파일 목록 밖). 명령줄에 시험의 임시 폴더 이름이 든 프로세스만 찾는 도우미(읽기만, 죽이지 않음). 통합 시험의 프로세스 확인이 돌지 않는 시험에만 있지 않게, 단위 시험의 취소 슬롯이 같은 도우미로 "취소 전 가짜 yt-dlp 둘 이상, 취소 뒤 0"을 본다.
- **가짜 yt-dlp(`fakeytdlp`)가 `%(id)s`도 펼친다**(Task 5). 지시는 "`%%`를 `%`로 읽는다"뿐이었지만, 그것만으로는 이스케이프가 빠져도 `%(id)s` 폴더가 우연히 맞는 곳에 생겨 시험이 통과한다. yt-dlp에서 관찰한 대로 `%(id)s`·`%(ext)s`도 펼치게 했고, 이스케이프를 뺀 변이로 시험이 잡는 것을 확인했다.
- **`test-names.ps1`의 `exe`가 배열도 받는다**(Task 5). `RealYtDlpIntegrationTest`의 이식이 `tst_real_ytdlp`와 `tst_real_download` 둘에 나뉘어서다. 배열이면 두 실행 파일의 함수 이름을 합쳐 비교한다. 다른 항목은 결과가 전과 같다. `tst_real_ytdlp`의 자리표시 슬롯 `downloadsAShortVideoAsM4aWithoutReencoding`은 지웠다(합친 목록에 같은 이름이 둘이 되므로). 그래서 `tst_real_ytdlp`의 시험 함수는 7에서 6이 되었다.
- **Task 1의 변이 (c)가 잡히지 않았다**(위 변이 표 아래 설명).
- **관문 앞 정리 커밋**(`2c7e1be`, 위). 계획에 없던 단계이고 controller가 넣었다.
- **README의 관문 문단**(이 보고서 커밋). Q2d를 더하고, `tst_real_download`가 오디오 허락 없이는 건너뛴다는 것, 되풀이 실행을 관문에 적었다.

## 확인하지 못한 것

- **`tst_real_download`의 두 시험은 한 번도 돌지 않았다.** 사용자가 오디오 시험을 허락하지 않았다(결정 15). 그래서 다음이 확인되지 않았다. **(고침, 2026-11-21: 그 뒤 사용자가 허락해 돌렸고 통과했다. 위 "관문 뒤 오디오 시험" 절.)**
  - `downloadsAShortVideoAsM4aWithoutReencoding`: 작업 전체(진짜 yt-dlp, 진짜 ffmpeg, 진짜 LRCLIB 공급자, 진짜 `LocalFolderSink`)가 실제 유튜브 영상 하나를 받아 Kotlin과 같은 파일(`001 <가수> - <제목>.m4a`, 5만 바이트 초과, AAC를 다시 인코딩하지 않음, 정사각 표지, 태그들)을 만드는지, 단계가 `DOWNLOADING`을 지나 `FINISHING` 한 번으로 끝나는지. Kotlin의 단언은 모두 옮겼지만 오늘의 유튜브와 LRCLIB에서 통과하는지 모른다. 이 실행 파일은 `QtHttpClient`를 쓰는 새 실행 파일인데 정적 TLS 플러그인이 들어갔는지(`QSslSocket::supportsSsl()`)도 이 실행 파일에서는 보지 않았다(Q2c 인계, 아래).
  - `cancellingARealDownloadLeavesNoToolProcessBehind`: 진짜 yt-dlp, 한 파일짜리 `yt-dlp.exe`가 풀어 띄우는 자기 사본, 그것이 띄우는 ffmpeg가 명령줄에 작업 폴더 이름을 담는지(시험이 그것으로 찾는다), 취소하면 그 프로세스가 모두 죽는지, 작업 폴더가 지워지는지. 프로세스 트리가 죽는 것은 가짜 손자(`fakeytdlpspawn`)로만 보았다(`cancellingKillsTheProcessTreesAndTheJobEndsPromptly`). 약 300KB 원본이 이 PC와 연결에서 너무 빨리 받아져 취소가 먹기 전에 끝날 수도 있다. 그때 시험은 이제 그렇다고 밝히고 실패한다(관문 앞 정리 1).
- **Q2b의 `tst_system_process_runner` 실패 한 번(190초, 출력 없음).** 이번 관문에서 두 빌드 각 20번 되풀이하고 `ctest`에서 한 번씩 더 돌려 모두 통과했다(한 번 26.67~32.61초; Q2c 관문에 이어 두 번째로 재현되지 않음). 원인은 여전히 모른다.
- **정상 종료 뒤 stdout을 쥔 실제 자손**(Q2b 인계). 진짜 yt-dlp·ffmpeg로 돌리지 않았으므로 그런 일이 있는지 모른다.
- **다른 드라이브로 옮기기**(Q2b 인계). 작업 폴더와 출력 폴더가 다른 드라이브면 `put`은 복사 뒤 지우기가 된다. 시험은 모두 같은 `QTemporaryDir` 안이다.
- **시간에 기대는 시험.** `cancellingDuringTheRetryDelayReturnsPromptly`와 `cancellingWithItemsInEveryStageEndsPromptly`는 일꾼이 재시도 대기에 들어간 것을 알릴 길이 없어 취소 전에 200ms 잔다(느린 PC에서도 통과하고 덜 증명할 뿐). `aStopBeforeThePutDeliversNothing`은 100ms 뒤 파일을 놓는 도우미 스레드에 기댄다. 태거가 느린 쪽이면 태거가 스스로 취소를 알려 같은 기대값이고 덜 증명할 뿐이다. 그러나 파일을 쥔 도우미 스레드가 `fs::moveFile`이 다시 해 보는 1.52초보다 늦게 깨어나면 바꿔 넣기가 먼저 포기하고, 태그 쓰기가 멈춤 전에 실패해 `Cancelled` 단언이 실패한다. 그러니 "흔들리지 않는다"가 아니라 "그 스레드가 1.5초 안에 깨어나는 한 통과한다"이다(최종 리뷰가 바로잡음). 재시도 대기 시험의 한도는 최종 리뷰 뒤 2초에서 5초다(그 시간에 작업 폴더 지우기의 다시 해 보기 1.52초도 들어간다). 최종 리뷰 뒤에 더한 `startingAndCancellingAtOnceEndsWithOneJobDoneCancelled`는 시작과 취소 사이를 0~2.95ms 바쁜 기다림으로 흩뜨린다. 시간은 멈춤이 작업의 어느 때에 닿는지만 바꾸고 단언은 시간과 상관없다(가짜 yt-dlp가 멈출 때까지 기다리므로 끝 상태는 늘 `Cancelled`). 5,000곡 시험의 1ms 잠은 스레드 수 변이를 잡으려는 것이다. Kotlin에서 옮긴 `awaitStarted`는 Kotlin처럼 200ms 동안 더 들어오는 곡이 없는지 본다. 관문의 20번 되풀이에서는 모두 통과했다.
- **단언의 같음.** `test-names.ps1`은 이름만 본다. 단언이 Kotlin과 같은지는 각 Task의 구현자와 리뷰어가 보았고 Task 6은 다시 보지 않았다.
- **대조가 다루지 않는 것.** 대조 `W jb`는 동시 수 1, 취소 없음, 재시도 대기 0이다. 동시성과 취소는 C++ 시험으로만 본다(Kotlin 쪽 동작과 비교하지 않음). 영상 ID는 `[A-Za-z0-9_-]`로만 만들어 결정 9의 `%`는 대조에 없다.
- **이 관문에서 돌리지 않은 다른 실제 서비스 시험.**
  - `tst_real_lrclib`(실제 lrclib.net): Q2c에서 승인된 두 번을 다 써서 돌리지 않았다. 이 단계의 코드는 LRCLIB 공급자를 바꾸지 않았다.
  - `XGS_INTEGRATION_DOWNLOAD` 뒤의 `downloadsTheRealYtDlpExecutable`(GitHub에서 yt-dlp.exe 약 18MB): 허락받지 않아 돌리지 않았다.
  - `yt-dlp -U`(업데이트 뒤 `yt-dlp.exe.old`가 남는지): 허락받지 않아 돌리지 않았다.
  - `tst_real_ytdlp` 안의 `writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum`: 오디오를 내려받으므로 `XGS_INTEGRATION_AUDIO`가 필요하고, 허락이 없어 관문의 실행에서 건너뛰었다(`18-integration-ytdlp.txt`의 `SKIP`).
- **Windows 10, 디버그 빌드의 통합 시험**(통합 시험은 릴리스만), HTTPS 위의 일들(Q2c에서 넘어온 것)은 그대로다.

## 앞 단계 인계 항목별 결과

### Q2a 보고서의 "Q2d에 넘길 것"

| 항목 | 결과 |
|---|---|
| NaN 백분율이 UI에 숫자로 가지 않게 | 했다. 작성자 결정 8: 이벤트의 `percent`를 비운다(`aPercentThatIsNotFiniteIsReportedAsUnknown`, `aPercentThatIsNotFiniteReachesTheReaderAsUnknown`, 대조의 `N` 맞춤) |

### Q2b 보고서의 "Q2d에 넘길 것"

| 항목 | 결과 |
|---|---|
| 미룬 통합 시험의 단언 | 옮겼다(`tst_real_download.cpp`의 `downloadsAShortVideoAsM4aWithoutReencoding`, Kotlin 단언 전부). 돌리지는 않았다(사용자가 허락하지 않음) |
| `%` 이스케이프 적용 | 했다. `ItemDownloader`가 `escapeOutputTemplateDirectory(workDir)`를 넘긴다(결정 9, 변이로 확인: Task 1과 Task 5) |
| 멈춘 뒤의 `status()`는 버리기 | 작업은 `status()`를 부르지 않는다. 화면의 일이라 Q4로 넘긴다(아래) |
| 도구 경로는 전체 경로로 | 했다. `ItemDownloader`는 `ToolPathProvider::current()`의 경로를 그대로 쓴다. 진짜 실행기 시험은 `testTools()`의 경로를 `xgs-test-child` 전체 경로로 바꿔 넘긴다 |
| 취소 중 실제 ffmpeg 자손 정리 | 시험(`cancellingARealDownloadLeavesNoToolProcessBehind`)은 썼고 돌리지 않았다. 가짜 손자로는 확인했다(`cancellingKillsTheProcessTreesAndTheJobEndsPromptly`). 위 "확인하지 못한 것" |
| 정상 종료 뒤 stdout을 쥔 자손 | 확인하지 못했다(실제 도구를 돌리지 않음). 시간 한도는 두지 않았다(결정 14). Q4에서 실제 사용으로 보거나 사용자가 정한다 |
| 작업 폴더의 위치(같은 드라이브인지) | 서비스는 `tempRoot`를 받기만 한다. 어디로 할지는 앱을 짜는 Q4의 일이다(아래) |
| 명령 조립 예외를 곡 하나의 실패로 | 했다(결정 9, `aVideoIdWithAPercentSignFailsTheItemWithoutRunningAnything`, `aVideoIdTheCommandRejectsFailsTheItem`) |
| NaN 백분율(Q2a에서 넘어온 것) | 했다(위 Q2a 줄) |
| 동시 수와 겹치는 `put` | `automaticConcurrency`는 Q2a에서 Kotlin과 같음을 확인했고 서비스는 쓰지 않는다(부르는 쪽이 정함, Q4). 겹치는 `put`은 진짜 `LocalFolderSink`로 시험했다(`twoItemsWithTheSameNameAtTheSameTimeLeaveOneFile`: 덮어쓰기 끔은 1·1·0, 켬은 2·0·0, 파일 하나; 관문 앞 정리로 겹침을 단언) |
| 마지막 옮기기 중 취소(`Tagged`) | 태거가 `Tagged`를 주면 다운로더는 `Downloaded`를 주고, 작업은 `put` 직전에 멈춤을 본다(결정 10): 멈췄으면 옮기지 않고 취소된 곡이다(`aStopBeforeThePutDeliversNothing`) |
| 곡마다 작업 폴더 하나 | 했다. 곡마다 작업 폴더 안에 자기 폴더를 만든다(`aSameVideoTwiceInOneJobDoesNotCollide`, 진짜 실행기) |
| 설치·업데이트를 내려받기와 겹치지 않게 | 하지 않았다(결정 14). 화면이 단추를 막는 것으로 Q4에서 한다 |
| 멈춘 뒤의 `fetch` | 했다. `prepare` 뒤에 작업이 멈춤 토큰을 본다(결정 2). 멈춘 뒤 빈 메타데이터가 오면 첫 해석을 그대로 둔다(`aMetadataSourceThatAnswersNothingAfterAStopKeepsTheFirstParse`) |
| `status()`의 시간, 도구 실행의 시간 한도 | 두지 않았다(결정 14, Kotlin에도 없다). 사용자가 정할 것으로 남는다 |
| `put`이 실패하면 파일을 남기고 "다시 시도 가능"으로 | **다르게 정했다**: Kotlin처럼 지운다(결정 11). 사용자가 정할 것 |
| 잘린 마지막 줄 | 했다. 멈춤이 요청된 뒤에는 stdout 처리기가 줄을 보지 않고 돌아간다(Task 2, `aLineCutShortByTheKillIsIgnored`). JSON 줄은 작업이 읽지 않는다(정보 파일은 끝난 뒤 파일로 읽음) |
| 처리기는 막히지 않게 | 지켰다. 처리기는 진행률을 솎아 이벤트 큐에 넣고(잠금 하나, 기다림 없음) stderr를 잠금 아래 모으는 것뿐이다 |
| 다른 곳에서 프로세스를 띄우지 않기 | `qt/core`에는 실행기 말고 프로세스를 띄우는 코드가 여전히 없다. 앱 전체의 규칙이라 Q4로 넘긴다 |
| `parseVideoInfo`의 바이트 약속 | 지켰다. 실행이 끝난 뒤에 `readVideoInfo(path)`로 파일 바이트를 그대로 읽는다(`theInfoFileIsReadOnlyAfterYtDlpHasExited`) |
| 영상 ID의 `%` | 거절로 정했다(결정 9) |

### Q2c 보고서의 "Q2d에 넘길 것"

| 항목 | 결과 |
|---|---|
| 가사 공급자를 작업에 잇기 | `ItemDownloader`가 `LyricsProvider`를 받는다(설명란에 가사가 없고 조회가 켜져 있으면 묻는다). 빈 값이면 다운로더가 멈춤 토큰으로 취소와 "못 찾음"을 가른다(`cancellingTheJobWhileTheLookupRunsCancelsTheLookupAndReportsCancelled`). `FallbackLyricsProvider`(LRCLIB → 구글)를 실제로 엮고 재생목록 전체가 공급자 하나를 나눠 쓰게 하는 것은 구글 가사(Q3)와 앱 조립(Q4)의 일이다 |
| 미룬 통합 시험 | 옮겼고 돌리지 않았다(위 Q2b 줄과 같음) |
| 클라이언트 하나를 앱 전체가 나눠 쓰기 | 앱 조립의 일이라 Q4로 넘긴다 |
| TLS 플러그인은 실행 파일마다 확인 | 하지 않았다. `tst_real_download`가 `QtHttpClient`를 쓰는 새 실행 파일인데 돌지 않았다. Q4의 앱 실행 파일에서 한다 |
| 클라이언트의 수명 | Q4로 넘긴다("앱이 끝날 때 없애는 순서", 아래) |
| `Unreachable`과 `Failed`의 뜻 | 작업은 HTTP 클라이언트를 직접 부르지 않는다(공급자만 부른다). 바뀐 것 없음 |
| 멈춘 응답 끝내기의 순서 가정(N-1), 400 이상 응답의 안전 검사(N-2), 시간 제한 없는 응답(N-3) | 4xx 본문을 읽는 호출자가 생기지 않았다. 그대로 Q4로 넘긴다 |
| Q2b 보고서의 "Q2d에 넘길 것"은 그대로 | 위 표 |

## Q3·Q4에 넘길 것

- **이벤트를 화면 스레드로 넘기는 다리**(Q4). 작업은 블로킹 `nextEvent`와 안 막는 `tryNextEvent`만 준다. 화면은 읽는 스레드 하나(또는 타이머의 `tryNextEvent`)로 이벤트를 받아 Qt 시그널로 넘긴다. 읽는 쪽은 하나로 정한다(`JobDone`이 아니면 `notify_one`). 아무도 읽지 않아도 작업은 끝나지만(`aJobNobodyReadsStillFinishes`) 큐는 이벤트를 다 들고 있다. 타이머로 `tryNextEvent`를 부르는 쪽은 주의할 것: `finished()`는 `JobDone`이 큐에 들어갔다는 뜻이지 다 읽었다는 뜻이 아니라서, `finished()`가 참이 된 뒤에도 이벤트가 남아 있을 수 있다. 참을 본 뒤 `tryNextEvent`가 없음을 줄 때까지 한 번 더 비운다(`JobDone`을 받을 때까지).
- **설치·업데이트와 내려받기를 겹치지 않게 막기**(Q4). yt-dlp가 도는 동안 `installYtDlp`는 마지막 옮기기에서 실패하고, 설치 둘이 겹치면 서로의 임시 파일을 지울 수 있다(Q2b). 화면이 내려받는 동안 설치·업데이트 단추를 막고, 설치끼리도 한 줄로 세운다.
- **임시 폴더의 위치**(Q4). `DefaultDownloadService`의 `tempRoot`를 출력 폴더와 같은 드라이브에 두면 `put`이 옮기기로 끝난다. 다른 드라이브면 복사 뒤 지우기가 되고 이것은 시험하지 않았다. 실패한 `put`이나 지우지 못한 작업 폴더가 남을 수 있으므로 앱이 시작할 때 오래된 작업 폴더를 치울지도 정한다.
- **앱이 끝날 때 없애는 순서**(Q4). 작업 핸들 → 서비스 → 다운로더 → 공급자·실행기 → `QtHttpClient` → `QCoreApplication`. 핸들의 소멸자는 취소하고 작업의 스레드를 모두 거둔다(`destroyingTheHandleOfARunningJobCancelsAndJoins`). 작업 폴더 지우기를 다시 해 보느라 최악에는 약 1.5초 기다릴 수 있다(결정 12). 서비스와 다운로더는 참조만 쥐므로 핸들보다 먼저 없어지면 안 된다(Task 2의 첫 실패가 그것이었다).
- **멈춘 뒤의 도구 상태는 버리기**(Q4, Q2b 인계). 멈춤을 요청한 뒤 얻은 `ToolsStatus`는 "도구 없음"처럼 보이므로 화면에 보이지 않는다.
- **마지막 곡 직후의 취소가 `Completed`로 보이는 경우**(Q4). 결정 3. 화면이 사용자가 누른 취소를 어떻게 보일지.
- **자동 동시 수**(Q4). 서비스는 위 한도가 없으므로 `automaticConcurrency()`나 설정값을 부르는 쪽이 넘긴다.
- **가사 공급자 엮기**(Q3·Q4). 구글 가사(Q3)를 `FallbackLyricsProvider`에 넣고, 재생목록 전체가 공급자 하나를 나눠 써야 LRCLIB의 쉬는 시간(5분)이 뜻이 있다.
- **TLS 플러그인 확인과 HTTP 클라이언트 하나 나눠 쓰기, 4xx 본문의 순서 가정**(Q4, Q2c 인계 그대로).
- **다른 곳에서 프로세스를 띄우지 않기**(Q4, Q2b 인계 그대로). 앱의 다른 곳에서 핸들을 모두 물려주는 `CreateProcess`나 `QProcess`를 쓰면 우리 파이프 끝을 물려받아 정상 종료한 실행을 붙잡을 수 있다.
- **`server`의 `JobRegistry`·`ResolveCache`·`JobLog`**(Q4·Q5, 결정 14).
- **`tst_tool_locator.exe -functions`가 빈 `%TEMP%\qt_temp-*` 폴더를 남긴다**(Q2b의 시험, 관문에서 봄; 위 "관문 뒤 정리"). 이름 대조를 돌릴 때마다 하나씩 쌓인다. 시험 쪽의 작은 일이고, 최종 리뷰의 분류대로 다음 단계 처음에 고친다.
- **`tst_real_download`를 돌릴 기회**(사용자 허락 뒤). 돌릴 때 취소 시험이 "취소 전에 끝났다"로 실패하면 더 긴 원본이 필요하다.

## 사용자가 정할 것

지금 코드는 아래 "지금"대로 동작하고, 관문은 그 동작으로 돌았다.

1. **계획 작성자의 결정 가운데 실제로 고를 수 있는 것.** 거절하면:
   - 결정 1: 스레드 대신 다른 모델(예: Qt 이벤트 루프와 `QProcess`)로 다시 짠다. 지금: `std::jthread`, 일꾼은 동시 수만큼.
   - 결정 3: 치명적 실패 뒤의 사용자 취소를 `Cancelled`로 보인다. 지금: `Failed`.
   - 결정 5: 출력 결과를 읽는 표(특히 공유 위반 같은 다른 Win32 코드를 작업 중단으로 볼지). 지금: 코드 5는 권한 없음, 112·39와 네 낱말은 디스크 가득으로 중단, 그 밖은 그 곡만 실패.
   - 결정 7: 재시도 대기 2초·4초. 지금: Kotlin과 같다.
   - 결정 8: 유한하지 않은 백분율을 "모름" 대신 0으로. 지금: "모름".
   - 결정 9: `%`가 든 영상 ID를 거절 대신 겹쳐서 받기. 지금: 거절(실제 유튜브 ID에는 `%`가 없다).
   - 결정 10: `put` 직전의 취소에도 옮기기. 지금: 옮기지 않는다.
   - 결정 11: `put`이 실패한 곡의 파일을 남기고 다시 옮기는 화면을 만든다. 지금: 지운다(Kotlin과 같다).
   - 결정 12: 작업 폴더 지우기를 Kotlin처럼 한 번만. 지금: 1.52초 동안 다시 해 본다.
   - 결정 13: 일꾼의 예외를 그 곡의 실패 대신 작업 중단으로. 지금: 그 곡만 실패.
   - 결정 14: 도구 실행의 시간 한도를 둘지(Q2b 결정 13과 같은 질문). 지금: 없음.
   - 결정 2·4·6·15는 Kotlin을 C++ 꼴로 옮기는 방식이라 고를 여지가 작다.
2. **오디오 시험을 돌릴지.** `XGS_INTEGRATION=1`과 `XGS_INTEGRATION_AUDIO=1`로 `tst_real_download`를 한 번(19초 영상의 오디오 약 300KB를 두 번 받고, 첫 시험은 LRCLIB에 요청 몇 개). Q2b의 `writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum`도 같은 변수로 돈다. 지금: 돌리지 않음. **권고(최종 리뷰): 허락되면 `tst_real_download`를 가장 먼저 돌린다.** 진짜 yt-dlp·ffmpeg로 받는 작업 전체와 그 취소를 보는 유일한 시험이다. **(고침, 2026-11-21: 그 뒤 사용자가 허락해 돌렸고 통과했다. 위 "관문 뒤 오디오 시험" 절.)**
3. **대조 통계의 2% 문턱**("`put`이 `AlreadyExists`를 받고 곡은 건너뜀"). 계획에 없던 통계이고 문턱은 구현자가 골랐다. 지금: 2%.
4. **실행 중에 정해진 차이들**(16~22번: 멈춘 뒤의 예외를 취소로, 두 번째 치명적 실패를 취소로, `makeDirectories` 겹쳐 쓰기, 이미 멈춘 토큰의 순서, `join`은 한 스레드, `AlreadyExists` 꼬리 문구, 오류 코드 없는 작업 폴더 실패의 문구)을 받아들일지.
5. Q2c·Q2b·Q2a 보고서의 "사용자가 정할 것"과 Q1에서 남은 결정은 그대로 기다린다.

## 라이선스 메모

이 절은 과정의 기록이고 법적 의견이 아니다. 동작은 저장소의 Kotlin 원본과 시험, 공개 문서, 대조 도구가 관찰한 답으로 정했다. Task 보고서들과 이 Task는 OpenJDK, `kotlinx.coroutines`, `kotlinx.serialization`의 소스를 보지 않았다. 금지 호출 점검의 JDK 내부 이름 검사는 관문에서 0건이다.

## 판정

**Q2d의 관문은 충족됐다.** 이식 대상 Kotlin 시험 103개(`DefaultDownloadServiceTest`)와 `RealYtDlpIntegrationTest`의 남은 1개는 같은 이름으로 있고(이름 대조에서 빠진 것 0, 26개 클래스), 깨끗한 릴리스·디버그 빌드에서 두 `ctest` 39개가 모두 통과했으며(릴리스 1336 통과·0 실패), 금지 호출 0건, 대조는 다섯 번의 실행에서 26개 종류 모두 불일치 0과 통계 문턱 통과, 디버그 도구는 릴리스와 같았고, 되풀이 실행 200번이 모두 통과했고, 실제 도구 통합 시험은 ffmpeg 8개와 yt-dlp 5개가 통과했다. 관문 중에 C++을 고치지 않았다.

다만 `RealYtDlpIntegrationTest`에서 옮긴 `downloadsAShortVideoAsM4aWithoutReencoding`은 "같은 이름으로 있다"까지이고 **통과는 확인하지 못했다**: 사용자가 오디오 시험을 허락하지 않아 건너뛰었다. 이 진술이 다루지 않는 것: 진짜 yt-dlp·ffmpeg로 받는 작업 전체와 그 취소(위 `tst_real_download` 둘), 다른 드라이브로의 `put`, 정상 종료 뒤 stdout을 쥔 실제 자손, Windows 10, 디버그 빌드의 통합 시험, 동시성과 취소를 Kotlin과 비교하는 일(대조는 동시 수 1), 재현되지 않은 Q2b의 실행기 실패 한 번의 원인. 사용자가 정할 것은 위에 모았다. **(고침, 2026-11-21: 그 뒤 사용자가 허락해 돌렸고 통과했다. 위 "관문 뒤 오디오 시험" 절.)**

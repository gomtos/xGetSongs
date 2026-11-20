# Qt Q2c(HTTP 클라이언트·yt-dlp 내려받기·LRCLIB 가사 조회) 보고서

작성일: 2026-11-20
설계: [2026-11-15-qt-rewrite-and-playlists-design.md](2026-11-15-qt-rewrite-and-playlists-design.md) §2, §3, §4(Q2 행)
계획: [2026-11-20-qt-engine-lrclib.md](../plans/2026-11-20-qt-engine-lrclib.md)
이전 단계: [2026-11-18-qt-engine-process-report.md](2026-11-18-qt-engine-process-report.md)(Q2b)

환경: Windows 11 Home 10.0.26200, Visual Studio 2022(vcvars64), Qt 6.11.3(정적, TLS는 Schannel), 정답 쪽 JVM은 `server/build/sidecar-image`의 OpenJDK 21.0.9와 그 안의 `engine.jar`·`shared-jvm.jar`. Gradle은 돌리지 않았다(`run.ps1`의 오래된 오라클 검사가 관문의 대조 다섯 번 모두 통과했다). 시험은 창을 띄우지 않았다(가짜 HTTP 서버는 `127.0.0.1`에만 묶어서 방화벽 상자도 뜨지 않았다). 실행 중인 xGetSongs 앱은 건드리지 않았고 `%APPDATA%\xGetSongs`에는 아무것도 쓰지 않았다(거기 있는 `ffmpeg`·`yt-dlp.exe`를 통합 시험이 실행만 했다).

**원본 출력.** 관문의 숫자는 모두 저장소 밖 작업 폴더 `C:\Users\gomto\AppData\Local\Temp\claude\46803f77e57fcdfd\004c41bb-2704-4ab0-875b-470a2bebd248\scratchpad\q2c\gate\`(아래 "관문 폴더")의 파일에서 옮겼고, 표마다 파일 이름을 적었다. 그 밖의 숫자(관찰 표, 변이, Task별 대조 통계, Task 5의 실제 실행)는 각 Task가 자기 작업 중에 잰 것이고 그 자리마다 "Task N"이라고 적었다. 그 원본은 Task 보고서(`.superpowers/sdd/2026-11-20-qt-engine-lrclib/task-N-report.md`)에 있다.

네트워크: 이 단계 전체에서 `lrclib.net`에 보낸 요청은 30개다: Task 5의 실제 실행 한 번 15개(Task 5 보고서), 관문의 실행 한 번 15개(시험이 스스로 센 수, 아래 "통합 시험"). 유튜브에는 관문에서 `tst_real_ytdlp`를 한 번 돌려 yt-dlp가 요청을 보냈다(오디오 없음, 아래 그 절에 왜 돌렸는지). GitHub의 yt-dlp.exe 내려받기(`XGS_INTEGRATION_DOWNLOAD`), 오디오 내려받기(`XGS_INTEGRATION_AUDIO`), `yt-dlp -U`는 어디서도 돌리지 않았다. 단위 시험과 `ctest`는 `127.0.0.1` 밖으로 아무것도 보내지 않는다(없는 호스트 이름을 찾는 시험도 없다).

## 읽는 법

| Task | 내용 | 커밋 |
|---|---|---|
| 1 | HTTP 클라이언트(`xgs::net::HttpClient`, `QtHttpClient`)와 시험용 가짜 서버(`FakeHttpServer`) | `ba630da`, 리뷰 반영 `a7f6fec` |
| 2 | yt-dlp 내려받기(`httpFetch`, `ToolManager`의 `Fetch`에 끼움) | `3b6abcc` |
| 3 | 가사 공급자(`LyricsProvider`, `FallbackLyricsProvider`)와 `LrclibLyricsProvider` | `c166cd8` |
| 4 | LRCLIB 조회를 Kotlin과 대조(대조 op `W lf`) | `5617c8c`, 리뷰 반영 `d9cb529` |
| 5 | 실제 LRCLIB 통합 시험(`tst_real_lrclib`) | `b788c8a` |
| 6 | 작업별 리뷰의 남은 지적(관문 전 정리), 관문, 이 보고서 | `21334fb`, 이 보고서 |
| 최종 리뷰 반영 | 최종 리뷰의 지적(아래 "최종 리뷰 반영", 2026-11-21) | `cad473f`, 보고서 커밋 |

대조 op `lf`(종류 `W`의 아홉째 op): 질의(가수·제목·앨범·길이), `maxRequests`, 요청 시간 제한, 그리고 요청마다의 답 대본(상태와 본문, 또는 연결 거부·시간 초과·모르는 호스트·그 밖의 IO 오류)을 주고, 두 쪽이 각자의 가짜 HTTP 클라이언트로 `find`를 두 번 부른다. 비교하는 것: 찾은 가사, 둘째 호출의 요청 수(쉬는 시간), `maxRequests + 1`로 같은 대본을 돌린 요청 수(상한), 첫 호출이 클라이언트에 넘긴 주소 문자열 전부. 자세한 것은 `qt/README.md`의 "대조 도구" 절.

"Kotlin `HttpClient`"는 `java.net.http.HttpClient`, "Qt 클라이언트"는 `QtHttpClient`(전용 스레드 하나의 `QNetworkAccessManager`)다.

## 요약

- **관문은 모든 단계를 통과했다**(커밋 `21334fb`의 코드로, 관문 중에 C++을 고치지 않았다. 관문 뒤 최종 리뷰 반영에서 C++을 고쳤고, 무엇을 다시 돌렸는지는 아래 "최종 리뷰 반영"): 릴리스·디버그 깨끗한 빌드(컴파일러 경고 0), 두 빌드의 `ctest` 35개 모두 통과(실행 파일마다 합해 1173 통과·0 실패·21 건너뜀, 시험 함수 628), 이름 대조에서 빠진 Kotlin 이름 0(25개 클래스, 일부러 더한 이름 46), 금지 호출 0, 대조 다섯 번 모두 26개 종류 불일치 0과 통계 문턱 통과(디버그 도구도 릴리스와 같음), 되풀이 실행 네 시험 × 두 빌드 × 20번 = 160번 모두 통과, 통합 시험은 ffmpeg 8개, LRCLIB 5개(Kotlin 4 + TLS), yt-dlp 5개 통과(건너뜀은 변수가 필요한 것과 Q2d로 미룬 것뿐).
- 옮긴 것: 막는(blocking) HTTP 클라이언트(`QtHttpClient`), yt-dlp 내려받기(`httpFetch`), 가사 공급자 인터페이스와 `FallbackLyricsProvider`, `LrclibLyricsProvider`. Kotlin 단위 시험 62개(LRCLIB 59, Fallback 3)와 실제 LRCLIB 통합 시험 4개가 같은 이름으로 통과한다.
- **대조가 C++ 버그 하나를 찾았다**: kotlinx의 `booleanOrNull`은 `TRUE`·`tRuE`·`falſe`(U+017F)도 불리언으로 읽는데 C++은 글자 그대로의 `true`·`false`만 읽었다(Task 3의 짐작 다섯 중 하나가 틀림). 고쳤고 회귀 시험이 있다.
- **Qt 클라이언트는 Kotlin과 다르게 보낸다**: 퍼센트 인코딩된 예약되지 않은 글자를 풀고(`%7E`→`~`), 16진을 대문자로, 경로의 점 조각을 지우고, `Accept-Encoding: gzip, deflate`·고정 `Accept-Language: en`·`Connection: Keep-Alive`를 더한다. 거절한 `https`→`http` 리다이렉트는 Kotlin처럼 3xx가 아니라 `Failed`다. 공급자가 클라이언트에 넘기는 주소는 대조에서 Kotlin과 같다. 아래 "Kotlin과 다르게 한 것" 9~17번. 최종 리뷰 반영에서 더 적은 것: 407은 `Failed`, 리다이렉트는 50번까지, Qt의 압축 해제 안전 검사에 걸린 본문은 `Failed`(23~26번).
- 관문 전에 작업별 리뷰의 남은 지적 열 가지를 한 커밋으로 고쳤다. 그중 대조 통계 하나(별난 대소문자 `instrumental`이 "형이 틀린 필드"를 부풀림)는 따로 세게 했고 문턱은 그대로다. 그 정리에서 새로 본 것: 깨진 청크 크기의 200을 Qt는 오류 없이 빈 본문으로 받는다(Kotlin은 보지 않음).
- lrclib.net 요청은 이 단계 전체에서 30개(Task 5 15, 관문 15). Q2b에서 원인 모르게 한 번 실패했던 `tst_system_process_runner`는 이번에 두 빌드 각 21번(되풀이 20 + `ctest` 1) 통과했고 재현되지 않았다. 원인은 여전히 모른다.
- 이 진술 밖에 있는 것: HTTPS 위의 리다이렉트·시간 제한, 프록시, Windows 10, 실제 yt-dlp.exe 내려받기, `yt-dlp -U`, 오디오 내려받기, 내려받기 작업(Q2d). 아래 "확인하지 못한 것".

## 관문 표

### 빌드

`build/qt/vcpkg_installed`, `build/qt-debug/vcpkg_installed`, `C:\dev\vcpkg`는 지우지 않았다. VS 2022 vcvars64 안에서(`VCPKG_ROOT=C:\dev\vcpkg`) 빌드마다 `cmake --preset <프리셋> -S qt`, `cmake --build <폴더> --target clean`, `cmake --build <폴더>`를 이어 돌렸다(릴리스 다음 디버그). 코드는 커밋 `21334fb` 그대로였다(작업 트리에 바뀐 파일 없음, `01-head.txt`). 둘 다 성공했다(`02-build-release.txt`, `03-build-debug.txt`, 각 1분 40초; 시각은 `00-timeline-build-run.txt`). 그 파일의 두 빌드 뒤 줄들(23:10:34~23:10:40에 `ctest` 둘, 두 스크립트, 되풀이 실행 여덟이 1초 안에 모두 "exit 0", 그리고 끝나지 않은 `09-diff` 시작)은 아무것도 돌리지 않은 첫 시도의 기록이다(과정 메모에 적은 실수). 실제로 돈 단계의 시각은 `00-timeline.txt`에 있다.

| 항목 | 릴리스(`build/qt`) | 디버그(`build/qt-debug`) |
|---|---|---|
| 구성 | `Configuring done (8.5s)`, `Generating done (1.4s)` | `Configuring done (8.4s)`, `Generating done (1.4s)` |
| clean | `Cleaning... 152 files.` | `Cleaning... 152 files.` |
| 빌드 단계 | `[1/191]`~`[191/191]` | `[1/191]`~`[191/191]` |
| 경고 | 컴파일러 경고 0(`/W4 /WX`). 출력의 경고는 Qt의 CMake 매크로(`Qt6CoreMacros.cmake`)가 구성 때 낸 `CMake Warning` 하나뿐(Q2b와 같음) | 같음 |

Qt는 다시 빌드되지 않았다. (관문 폴더의 `superseded-first-build\`는 정리 커밋을 고치기 전에 한 번 돌린 깨끗한 빌드의 출력이다. 그 뒤 관문 전 정리 커밋에 시험 하나를 더해 커밋을 고쳤기 때문에(아래 "관문 전 정리"의 3번) 다시 빌드했고, 이 표는 다시 한 빌드다.)

### 테스트

| 실행 | 결과 | 원본 |
|---|---|---|
| 릴리스 `ctest --test-dir build/qt --output-on-failure` | `100% tests passed, 0 tests failed out of 35`, Total 77.79 sec | `04-ctest-release.txt` |
| 디버그 `ctest --test-dir build/qt-debug --output-on-failure` | `100% tests passed, 0 tests failed out of 35`, Total 103.28 sec | `05-ctest-debug.txt` |

이제 `ctest`는 시험마다 Qt Test의 출력을 `build/qt*/qttest/<이름>.txt`에 남긴다(Task 1, Q2b에서 넘어온 일). 두 실행이 끝난 뒤 그 폴더를 `04-ctest-release-qttest\`, `05-ctest-debug-qttest\`로 복사했고, 실행 파일마다의 `Totals:` 줄과 `-functions`로 센 시험 함수 수를 `14-totals-per-exe.txt`에 모았다. 실패가 없었으므로 따로 보관한 실패 출력은 없다.

두 `ctest`는 `XGS_INTEGRATION` 없이 돌렸다. 그래서 통합 시험 실행 파일 셋은 `Passed`이지만 안의 시험 함수는 모두 건너뜀이다(아래 표의 2 통과는 `initTestCase`·`cleanupTestCase`).

이번 단계에 생기거나 바뀐 실행 파일(릴리스와 디버그가 같은 수):

| 실행 파일 | 통과 | 실패 | 건너뜀 | 시험 함수 | 그중 |
|---|---|---|---|---|---|
| `tst_qt_http_client` | 24 | 0 | 0 | 22 | Kotlin 짝 없음(계획의 20 + 리뷰 반영의 헤더 시험 + 관문 전 정리의 2xx 오류 시험). 릴리스 `ctest` 13.79초. 최종 리뷰 반영 뒤에는 33개(아래 그 절) |
| `tst_http_fetch` | 12 | 0 | 0 | 10 | Kotlin 짝 없음 |
| `tst_fallback_lyrics_provider` | 6 | 0 | 0 | 4 | `FallbackLyricsProviderTest` 3 + 추가 1 |
| `tst_lrclib_lyrics_provider` | 68 | 0 | 0 | 66 | `LrclibLyricsProviderTest` 59 + 추가 7. 릴리스 `ctest` 25.42초 |
| `tst_real_lrclib` | 2 | 0 | 6 | 6 | `RealLrclibIntegrationTest` 4 + 추가 2(변수 없이 모두 건너뜀) |
| `tst_line_splitter`, `tst_kt_text` | Q2b와 같음 | 0 | 0 | 6, 22 | UTF-8 풀기를 `kt::decodeUtf8`로 옮김(Task 3, 코드는 그대로) |
| 그 밖의 30개 | 1061 | 0 | 15 | 520 | Q2b 보고서의 표와 같은 수 |
| **합계(35개)** | **1173** | **0** | **21** | **628** | `14-totals-per-exe.txt`의 `TOTAL` 줄, 두 빌드 같음 |

628 = Q2b의 520 + 이번 단계 108(`tst_qt_http_client` 22, `tst_http_fetch` 10, `tst_fallback_lyrics_provider` 4, `tst_lrclib_lyrics_provider` 66, `tst_real_lrclib` 6). 그중 Kotlin 이름은 66(단위 62 = LRCLIB 59 + Fallback 3, 통합 4), 일부러 더한 이름은 10, Kotlin 짝 없는 실행 파일 둘의 함수 32다.

### 되풀이 실행 (관문에 더한 것)

Q2b에서 `tst_system_process_runner`가 디버그 `ctest`에서 한 번 190초 만에 출력 없이 실패한 일이 있었고(원인 모름), Task 5 중에는 릴리스 `ctest`의 시험 하나가 한 번 실패했다고 구현자가 적었다(확인 안 됨: `LastTestsFailed.log`가 `tst_lrclib_lyrics_provider`를 가리켰지만 날짜가 Task 5 시작 전이었다). 그래서 관문에서 네 시험을 두 빌드에서 `ctest --test-dir <폴더> --output-on-failure --repeat until-fail:20 -R ^<이름>$`로 20번씩 돌렸다(실패하면 그 자리에서 멈추고 `qttest/<이름>.txt`를 보관하게 했다).

**160번 모두 통과, 실패 0.** 실패가 없어서 보관한 실패 출력은 없다. 칸은 "통과/돌린 수, 한 번에 걸린 시간의 최소~최대, ctest의 Total"이다(시각은 `00-timeline.txt`).

| 시험 | 릴리스 | 원본 | 디버그 | 원본 |
|---|---|---|---|---|
| `tst_lrclib_lyrics_provider` | 20/20, 25.33~25.67초, 509.58초 | `08-loop-release-tst_lrclib_lyrics_provider.txt` | 20/20, 25.66~25.96초, 516.44초 | `08-loop-debug-tst_lrclib_lyrics_provider.txt` |
| `tst_qt_http_client` | 20/20, 13.71~13.87초, 275.91초 | `08-loop-release-tst_qt_http_client.txt` | 20/20, 13.78~13.90초, 276.85초 | `08-loop-debug-tst_qt_http_client.txt` |
| `tst_http_fetch` | 20/20, 5.33~5.43초, 107.60초 | `08-loop-release-tst_http_fetch.txt` | 20/20, 5.50~5.63초, 111.07초 | `08-loop-debug-tst_http_fetch.txt` |
| `tst_system_process_runner` | 20/20, 24.13~27.89초, 523.92초 | `08-loop-release-tst_system_process_runner.txt` | 20/20, 23.80~30.21초, 528.93초 | `08-loop-debug-tst_system_process_runner.txt` |

그러니 Q2b의 190초 실패도, Task 5 중의 "한 번 실패"도 이번에는 재현되지 않았다. 재현되지 않았다는 것만 말할 수 있고 원인은 여전히 모른다. (두 `ctest`의 한 번씩을 더하면 각 시험이 빌드마다 21번 통과했다.)

### 이름 대조 (`pwsh qt/tools/test-names.ps1`, 종료 코드 0, 원본 `06-test-names.txt`)

이번 단계에 생긴 줄과 마지막 줄:

```
FallbackLyricsProviderTest: Kotlin 3, Qt 4, missing 0, added 1 (aStopRequestStopsAskingTheRemainingProviders)
LrclibLyricsProviderTest: Kotlin 59, Qt 66, missing 0, added 7 (aCallWaitingForASlotReturnsWhenItIsCancelled, aCancelledCallDoesNotStartTheCoolDown, aNameLongerThanAThousandUnitsIsReadAsMissing, malformedUtf8InABodyIsReadLikeKotlinDoes, manyCallsCancelledAtOnceAllReturn, aRequestTimeoutOfZeroSendsNothingAndStartsTheCoolDown, anInstrumentalFlagIsReadWhateverItsCase)
RealLrclibIntegrationTest: Kotlin 4, Qt 6, missing 0, added 2 (theRealServiceIsReachedOverSchannelTls, downloadsTheRealYtDlpExecutable)
PASSED: every Kotlin test name is a Qt test name in 25 classes; only the 46 intended names were added
```

(위는 관문 때의 출력이다. 최종 리뷰 반영에서 `aRequestTimeoutOfZeroSendsNothingAndStartsTheCoolDown`을 `aRequestTimeoutOfZeroSendsNothing`으로 바꿨다. 쉬는 시간이 시작됐는지는 밖에서 볼 수 없어서다. 아래 "최종 리뷰 반영".) 나머지 22개 클래스의 줄은 Q2b와 같다. 이 스크립트는 이름만 본다. 단언이 Kotlin과 같은지는 각 Task의 구현자와 리뷰어가 보았고 Task 6은 다시 보지 않았다. `tst_qt_http_client`·`tst_http_fetch`는 Kotlin 짝이 없어 `test-classes.json`에 넣지 않았다(controller 결정 T1-b: 기존처럼 README의 목록에 적는다).

### 금지 호출 (`pwsh qt/tools/forbidden-calls.ps1`, 종료 코드 0, 원본 `07-forbidden-calls.txt`)

```
looked at 65 files under qt/core for the Q1 list (kt_text.* and kt_tables.inc excluded), 68 for the Q2a list, and 135 text files under qt/ (qt/spike excluded)
PASSED: 0 hits
```

목록은 Q2b와 같다. Task 1의 첫 판에 `toInt()` 두 곳이 걸려 `QVariant::value<int>()`로 바꿨다(Task 1).

### 대조 (`pwsh qt/tools/differential/run.ps1`)

오라클과 비교하는 네 번의 실행과 디버그 도구 자체 점검 한 번이다. 모두 기본 종류 `UXMSCFDGTLYRNBZHOJEQVKAIWP` 26개(종류는 Q2b와 같고 `W`에 op `lf`가 더해졌다)를 한 번의 호출로 돌렸다. 모두 오래된 오라클 검사를 통과했고 종료 코드 0, 마지막 줄은 `PASSED (kinds UXMSCFDGTLYRNBZHOJEQVKAIWP, <건수> cases per case kind, seed <seed>)`다.

| 실행 | 원본 | 불일치 | 걸린 시간 |
|---|---|---|---|
| `-PerKind 10000 -Seed 20261115` | `09-diff-10000-seed20261115.txt` | 26개 종류 모두 0 | 2분 30초 |
| `-PerKind 10000 -Seed 1` | `10-diff-10000-seed1.txt` | 모두 0 | 2분 27초 |
| `-PerKind 10000 -Seed 987654321` | `11-diff-10000-seed987654321.txt` | 모두 0 | 2분 46초 |
| `-PerKind 10000 -Seed 20261115 -DebugTool` | `12-diff-10000-seed20261115-debugtool.txt` | 모두 0, 디버그 도구는 26개 종류 모두 `0 differences from the release tool` | 5분 5초 |
| `-PerKind 100000 -Seed 424242` | `13-diff-100000-seed424242.txt` | 모두 0 | 15분 48초 |

사례 수는 Q2b와 같다(표 종류 `U`·`B`·`Z` 1,112,064 코드 포인트, `X` 65,536, `M` 11, 나머지 종류마다 1만 또는 10만). 시간은 `00-timeline.txt`.

**통계 문턱.** 실행마다 `(needs …)`가 붙은 줄이 261개이고(Q2b의 248 + `lf`의 13), 그중 문턱이 0보다 큰 240개가 모두 문턱 이상이다(`15-stats-summary.txt`: 다섯 실행 모두 `below=0`). 문턱은 낮추지 않았다. 측정값을 문턱으로 나눈 값이 가장 작은 셋은 Q2b와 같은 통계들이다(R 가짜 경로만 든 입력 96.7~97.3%(90%), P `rs` `OK` 42.8~43.3%(40%), V 길이 있음 32.4~32.9%(30%), seed 1의 I op `norm` 16.5%(15%)). `lf`의 통계는 그보다 넉넉하다.

**`W lf`의 통계**(분모는 `lf` 사례 수; 원본은 위 파일들의 `W: lf:` 줄):

| 통계 | 문턱 | seed 20261115 (1090) | seed 1 (1104) | seed 987654321 (1102) | 10만 seed 424242 (10947) |
|---|---|---|---|---|---|
| 가사 찾음 | 15% | 29.5% | 25.5% | 26.3% | 28.5% |
| 못 찾음 | 30% | 70.5% | 74.5% | 73.7% | 71.5% |
| `/search`까지 감 | 40% | 63.9% | 64.8% | 66.4% | 64.9% |
| 쉬는 시간 시작(관문 때의 세는 법; 최종 리뷰 반영 뒤에는 요청이 시작한 것만 센다, 아래) | 10% | 21.5% | 23.1% | 24.3% | 22.8% |
| (쉬는 시간 규칙과 둘째 호출이 어긋난 사례, 0이어야 함) | - | 0 | 0 | 0 | 0 |
| 상한이 조회를 끊음 | 5% | 13.4% | 14.1% | 11.6% | 13.6% |
| 질의에 ASCII 밖 글자 | 20% | 96.5% | 95.8% | 95.6% | 95.9% |
| 질의에 `& = # + %`나 공백 | 10% | 97.8% | 97.0% | 97.5% | 97.4% |
| 질의에 짝 없는 서로게이트 | 2% | 21.5% | 21.0% | 22.1% | 21.0% |
| 요청된 답에 형이 틀린 필드(**새로 세는 법**) | 10% | 24.5% | 24.0% | 24.5% | 23.4% |
| 요청된 답에 별난 대소문자 `instrumental`(**새 통계**) | 10% | 25.0% | 22.2% | 22.9% | 23.0% |
| 요청된 답의 본문이 JSON 아님 | 5% | 8.9% | 8.2% | 7.6% | 8.7% |
| 후보 둘 이상인 `/search` 답 | 15% | 29.3% | 31.2% | 30.7% | 31.5% |

"형이 틀린 필드"는 Task 4 리뷰 반영 때 35.8~38.5%였다. 관문 전 정리 7번으로 별난 대소문자 `instrumental`을 빼고 세니 23.4~24.5%가 되었고 문턱 10%를 넘는다. "`&` 등이나 공백"은 거의 모든 제목에 공백이 있어 높다(Task 4). "JSON 아님"은 Task 4 리뷰 반영 때 9.2 / 8.2 / 7.7 / 8.9%였고 지금은 8.9 / 8.2 / 7.6 / 8.7%다: 관문 전 정리 6번이 고정 글 한 항목(객체 값 `plainLyrics`)을 올바른 JSON으로 되돌려서 그 항목을 고른 사례가 더는 "JSON 아님"이 아니다. 그 밖의 숫자는 Task 4 리뷰 반영의 표와 같다(생성기의 무작위 순서가 바뀌지 않았다).

### 통합 시험 (`XGS_INTEGRATION=1`, 릴리스, 한 번)

`XGS_INTEGRATION_AUDIO`와 `XGS_INTEGRATION_DOWNLOAD`는 켜지 않았다(실행 전에 그 변수를 지웠다). 세 실행 파일을 차례로 한 번씩 돌렸다(00:31:50~00:32:30, `19-integration-exits.txt`).

| 실행 파일 | 결과 | 원본 | 내용 |
|---|---|---|---|
| `tst_real_ffmpeg_tagging` | `Totals: 10 passed, 0 failed, 0 skipped`, 3480 ms, 종료 코드 0 | `16-integration-ffmpeg.txt` | Kotlin 8개 모두 통과. 네트워크 없음. `%APPDATA%\xGetSongs` 쪽 도구는 실행만 했다 |
| `tst_real_lrclib` | `Totals: 7 passed, 0 failed, 1 skipped`, 5484 ms, 종료 코드 0 | `17-integration-lrclib.txt` | Kotlin 4개 + TLS 시험 통과. 건너뜀 1 = `downloadsTheRealYtDlpExecutable`(변수 없음). `requests sent to the service: 15` |
| `tst_real_ytdlp` | `Totals: 7 passed, 0 failed, 2 skipped`, 30425 ms, 종료 코드 0 | `18-integration-ytdlp.txt`(곡 이름을 지운 사본, 아래) | Kotlin 3 + 더한 2 통과. 건너뜀 2 = 오디오를 내려받는 `writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum`, Q2d로 미룬 `downloadsAShortVideoAsM4aWithoutReencoding` |

통과 수는 시험 수에 `initTestCase`·`cleanupTestCase`를 더한 것이다. `tst_real_lrclib`는 이 단계의 두 번째이자 마지막 승인된 실행이고 **한 번만** 돌았다. 그 출력에는 상태 코드, 키 이름과 값의 종류, 줄 수·글자 수만 있다(Task 6이 파일 전체를 읽어 가사 글자와 곡 이름 값이 없음을 확인했다): 알려진 곡의 가사 `26 lines, 23 non-blank, 462 characters`(Task 5와 같은 수), `/search` HTTP 200에 레코드 15개(15개 모두 가사 있음, 연주곡 0, 매처가 받은 것 11), 키 11개(`id`, `name`, `trackName`, `artistName`, `albumName`, `duration`, `instrumental`, `hasWordSync`, `plainLyrics`, `syncedLyrics`, `lyricsfile`), 없는 곡 검색은 HTTP 200에 본문 2글자, TLS 탐침 HTTP 200.

`tst_real_ytdlp`는 실행 파일의 출력에 재생목록 제목과 항목 25개의 파일 이름(곡 이름), 정보 파일의 앨범·가수·곡명 값을 찍는다(Q2b 때부터의 시험 출력). 이 지시의 규칙(곡 이름 값을 남기지 않음)에 따라 관문 폴더에는 그 값들을 `<… redacted>`로 바꾼 사본만 두었고(파일 끝에 그렇게 적었다) 원래 출력은 지웠다. 남은 구조: 재생목록 100항목, 찍힌 25항목 중 `ok` 24·`WARN` 1, 영상마다 작업 폴더에 남은 것은 `.info.json`과 `.jpg` 둘, 설명란 80줄에서 가사 블록 54줄(빈 줄이 아닌 것 42, 606자; Q2b와 같은 수), 끝에 시험용 캐시·임시 폴더가 비어 있음(`the sandbox held: cache [], tmp []`).

**처음 한 번은 아무 시험도 돌지 않았다.** 첫 시도에서 내 PowerShell 명령의 변수 이름(`$T`와 `$t`, PowerShell은 대소문자를 가리지 않는다)이 겹쳐 `-o` 출력 경로가 틀렸고, 세 실행 파일 모두 `Unable to open file for logging`으로 곧바로 끝났다. Qt Test는 로그 파일을 열지 못하면 시험 함수를 하나도 돌리지 않고 끝난다: 같은 꼴로 `tst_qt_http_client`를 틀린 경로로 부르니 72 ms 만에 종료 코드 1로 끝났다(보통 13.7초). 그러니 그 시도는 lrclib.net에도 유튜브에도 요청을 보내지 않았다. 위 표가 실제로 돈 유일한 실행이다.

## 관찰 표

모두 Task 4가 오라클(사이드카 이미지의 JVM에서 Kotlin 원본과 JDK 공개 API를 부르는 `Oracle.java`, 또는 공개 API만 부르는 일회용 탐침)로 얻은 답이다. JDK와 `kotlinx.serialization`의 소스는 보지 않았다(Task 4 보고서의 자기 점검).

### 1. `URLEncoder.encode(text, UTF_8)` (공급자가 주소를 만들 때)

| 입력 | JVM의 답 |
|---|---|
| ASCII 글자·숫자, `.` `-` `*` `_` | 그대로 |
| 공백 | `+` (공급자가 `%20`으로 바꾼다) |
| `~` `'` `(` `)` `!` `+` NUL | `%7E` `%27` `%28` `%29` `%21` `%2B` `%00` |
| 짝 맞는 서로게이트(이모지) | 그 UTF-8 (`%F0%9F%98%80`) |
| 짝 없는 서로게이트(앞이든 뒤든, 글 속이든) | `%3F` (`a\uD83Db` → `a%3Fb`) |

C++의 인코더는 이것을 공개 문서의 설명으로 손으로 짰고, 짝 없는 서로게이트의 `%3F`는 Task 3의 짐작이었다가 대조로 맞음이 확인됐다. 이것은 **공급자가 클라이언트에 넘기는 주소 문자열**이다. Qt가 실제로 보낼 때는 `%7E`를 `~`로 바꾼다(아래 "Kotlin과 다르게 한 것" 9번). 대조 `lf`는 넘긴 문자열을 비교하고, 전송 때의 다시 쓰기는 `tst_qt_http_client`가 본다.

### 2. JSON 값 읽기 (`JsonPrimitive.booleanOrNull`, `doubleOrNull`, JSON `null`)

| 질문 | JVM의 답 |
|---|---|
| `booleanOrNull`이 받는 것 | `true`·`false`를 대소문자 없이(`String.equals(ignoreCase = true)`와 같다): `TRUE`, `True`, `tRuE`, `FALSE`, `False`, U+017F가 든 `falſe`·`FALſE`(ſ의 대문자가 S). null: `1`, `0`, `yes`, `truex`, `fals`, `nul`, `null`, `t`, 전각 `ｔrue`, 키릴 `е`가 든 `truе`, `TRUİ`. 문자열 값이면 내용을 읽지만(`"TRUE"` → 참), 공급자는 문자열이 아닌 값만 읽는다 |
| `doubleOrNull`이 받는 것 | 내용의 `toDoubleOrNull()`과 같다: `258`, `258.4`, `NaN`, `1e400`(무한), `-0`, `2.58e2`, `Infinity`, `-Infinity`, `+258`, `0258`, `258.`, `.5`, `258f`, `258d`, `00`, `1e-400`(0)은 수, `0x10`, `1_000`, `null`, `true`는 null. `"258"` 같은 문자열도 수지만 공급자는 문자열을 길이로 읽지 않는다 |
| JSON `null`인 필드 | 없는 필드와 같다 |
| 본문의 잘못된 UTF-8(`String(bytes, UTF_8)`) | `kt::decodeUtf8`(Q2b `W ln`에서 `bufferedReader(UTF_8)`로 관찰한 규칙)과 같다 |
| 요청 시간 제한이 0이나 음수 | 요청 없이 그 호출은 없음(둘째 호출도 요청 0개). Kotlin 원본은 이것을 `Stop`으로 다뤄 쉬는 시간을 시작하고(`LrclibLyricsProvider.kt` 158~159행) C++도 같게 했지만, 같은 시간 제한으로는 둘째 호출이 어차피 아무것도 보내지 않으므로 쉬는 시간이 시작됐는지는 밖에서 보이지 않는다 |

### 3. Qt 네트워크 오류가 어디로 가나 (`HttpStatus`, Task 1)

Qt 클라이언트의 답은 `Response`(상태와 본문), `Unreachable`(연결 문제: 공급자에게는 "멈추고 쉬는 시간"), `Failed`(그 밖의 실패: 공급자에게는 그 요청의 "없음"), `Cancelled` 넷이다. 루프백에서 시험으로 본 것과 코드에서 정한 것을 가렸다.

| 상황 | Qt가 주는 것 | 우리 답 | 시험 |
|---|---|---|---|
| 연결 거부 | `ConnectionRefusedError` | `Unreachable` | `aRefusedConnectionIsUnreachable` |
| 전체 시간 초과, 무소식 시간 초과(우리 타이머) | (우리가 `abort`) | `Unreachable` | `aSlowAnswerIsUnreachableAtTheTotalTimeout`, `aBodyThatStalls…`(둘) |
| 본문 중간에 연결이 끊김(200 뒤) | `RemoteHostClosedError` | `Failed` | `aConnectionClosedInTheMiddleOfTheBodyIsAFailureNotATimeout` |
| 200 뒤 gzip 본문이 풀리지 않음 | 내용·프로토콜 범위(201 이상)의 오류 | `Failed` | `anErrorAfterA2xxStatusIsAFailureNotAResponse`(관문 전 정리에서 더함) |
| 4xx·5xx | 201 이상의 오류와 상태 코드 | `Response`(그 코드와 본문) | `aGetReturnsTheStatusAndTheBody`(404) |
| 본문이 한도를 넘음(우리가 끊음) | — | `Response`, `bodyTooLarge`, 빈 본문 | `aBodyOfExactlyTheLimit…`, `aBodyThatNeverEnds…`, gzip은 `aGzipBodyIsMeasuredDecodedAgainstTheLimit`·`anEndlessGzipBodyIsDroppedAtTheLimit`(최종 리뷰 반영) |
| gzip 본문이 Qt의 압축 해제 안전 검사(`decompressedSafetyCheckThreshold`, 기본 10 MiB)에 걸림 | `UnknownContentError`, 그리고 응답을 끝내지 않음 | `Failed`(Qt의 문구, 클라이언트가 끝냄) | `aGzipBodyPastQtsDecompressionSafetyCheckEndsPromptly`(최종 리뷰 반영) |
| 서버가 직접 보낸 407 | `ProxyAuthenticationRequiredError`(105), 상태 코드 없음 | `Failed` | `a407IsAFailureWithQtsTextNotAResponse`(최종 리뷰 반영). 아래 23번 |
| 깨진 주소, `ftp://`, 빈 주소 | (보내기 전에 우리가 거절) | `Failed`, 요청 없음 | `aBrokenOrUnsupportedUrlIsAFailureWithoutARequest` |
| 멈춤 요청(보내기 전, 기다리는 중) | — | `Cancelled` | `aStopRequestedBeforeTheCallSendsNothing`, `stoppingWhileTheServerSleeps…`, `cancellingManyCallsAtOnce…` |
| 클라이언트를 없앰(요청 중) | — | `Failed`(또는 `Cancelled`) | `destroyingTheClientWithACallInFlightEndsTheCall` |
| 모르는 호스트 | `HostNotFoundError` | `Unreachable` | **시험 없음**(DNS를 쓰지 않는 규칙) |
| `TimeoutError`, `TemporaryNetworkFailureError`, `NetworkSessionFailedError` | 그대로 | `Unreachable` | **시험 없음**(코드에서 정함) |
| `UnknownNetworkError`, 응답 전 | 그대로 | `Unreachable` | **시험 없음**. Task 1 구현자는 Qt가 소켓의 "네트워크·호스트에 닿을 수 없음"을 이렇게 알린다고 보았다(기억에 기댄 판단, 확인 안 함) |
| `UnknownNetworkError`, 응답 뒤 | 그대로 | `Failed` | 시험 없음 |
| TLS 오류 | 그대로 | `Failed` | 시험 없음 |
| 리다이렉트 50번 넘음 | `TooManyRedirectsError` | `Failed` | `moreThanFiftyRedirectsAreAFailure`(최종 리뷰 반영, 서버가 요청 51개를 받음). 아래 24번 |
| `https`→`http` 리다이렉트 | `InsecureRedirectError` | `Failed` | **시험 없음**(TLS 서버가 필요). 아래 "Kotlin과 다르게 한 것" 15번 |

관문 전 정리에서 하나 더 관찰했다(시험으로 고정하지 않음): 200과 `Transfer-Encoding: chunked` 뒤에 청크 크기 자리에 `zz`처럼 16진이 아닌 글이 오면, Qt는 오류 없이 **빈 본문의 200**으로 끝냈다(우리 답 `Response`, 본문 0바이트). Kotlin `HttpClient`가 같은 응답에 무엇을 하는지는 보지 않았다. 공급자에게 빈 200은 JSON이 아니어서 그 요청의 "없음"이고, `httpFetch`에서는 0바이트 성공 뒤 `ToolManager`의 크기 검사(1,000,000바이트 미만)가 실패로 만든다. 아래 "확인하지 못한 것".

## 대조가 찾은 C++ 버그

하나다(Task 4).

**`booleanOrNull`의 대소문자.** 첫 시험 실행(seed 5, `W` 2,000건)에서 한 사례가 달랐다: `/get` 레코드의 `"instrumental":tRuE`를 Kotlin은 연주곡으로 보고 거절해 요청 6개 뒤 없음, C++은 연주곡이 아닌 것으로 읽고 요청 1개로 가사를 냈다. Task 3의 짐작 2("`true`·`false` 글자 그대로만")가 틀렸다. JVM에 더 물어보니 kotlinx는 `String.equals(ignoreCase = true)`처럼 읽고, U+017F(ſ)가 든 `falſe`도 거짓이다(위 관찰 2). 고침: `lrclib_lyrics_provider.cpp`의 `flag()`가 `kt::equalsIgnoreCase`로 단위마다 비교한다(길이가 같을 때). 회귀 시험 `anInstrumentalFlagIsReadWhateverItsCase`(`FakeHttpClient`, 네트워크 없음): `TRUE`·`tRuE`는 답 없음, `false`·`FALSE`·`falſe`·`1`·`ｔrue`는 가사. 생성기도 이런 철자를 더 내게 했다. 그 뒤 모든 실행에서 불일치 0.

**Task 3의 짐작 다섯의 결과**(Task 4):

| 짐작 | 결과 |
|---|---|
| 1. 짝 없는 서로게이트는 `%3F` | 맞음 |
| 2. `booleanOrNull`은 `true`·`false` 글자 그대로만 | **틀림**(위 버그) |
| 3. `doubleOrNull` = 내용의 `kt::toDoubleOrNull` | 맞음(시험한 모든 리터럴과 문자열) |
| 4. `String(bytes, UTF_8)` = `kt::decodeUtf8` | 맞음(`W ln`의 잘못된 열 26가지와 날 서로게이트) |
| 5. JSON `null`은 없는 필드 | 맞음 |

## 변이 시험 (각 Task의 실행, 모두 되돌림)

| Task | 변이 | 잡은 곳 |
|---|---|---|
| 1 | 본문 한도 비교 `>` → `>=` | `aBodyOfExactlyTheLimitIsReadAndOneByteMoreIsTooLarge` |
| 1 | `std::stop_callback` 뺌 | `stoppingWhileTheServerSleepsReturnsPromptly`, `cancellingManyCallsAtOnceReturnsThemAllPromptly` |
| 1 | 리다이렉트 정책을 `ManualRedirectPolicy`로 | `aRedirectIsFollowed` |
| 3 | 503을 "없음"으로 | `badGatewayServiceUnavailableAndGatewayTimeoutAreTroubleLikeTooManyRequests` |
| 3 | `/search` 인자 순서를 바꿈 | `theSearchRequestAsksByTrackNameAndArtistName` |
| 3 | 취소에 쉬는 시간을 시작 | `aCancelledCallGivesItsSlotBack`, `aCancelledCallDoesNotStartTheCoolDown` |
| 3 | 취소 길에서 자리를 돌려주지 않음 | `aCancelledCallGivesItsSlotBack` |
| 3 | 이름 길이 상한을 뺌 | `aNameLongerThanAThousandUnitsIsReadAsMissing` |
| 4 | 인코더가 `~`를 그대로 둠 | 대조 `W` 1만 건(seed 20261115)에서 불일치 95 |
| 4 | `booleanOrNull`을 다시 글자 그대로로 | 처음 5, 생성기를 고친 뒤(리뷰 반영) 84 |
| 4 | `pick` 대신 첫 후보 | 307 |
| 4 | `/get` 레코드를 `tidy` 없이 | 106 |
| 6 | 2xx 코드의 201 이상 오류를 답으로(`code >= 400` 조건을 뺌) | `anErrorAfterA2xxStatusIsAFailureNotAResponse`만 잡음(관문 전 정리) |

Task 2는 변이를 적지 않았다. Task 5(실제 서비스 시험)는 변이 대상이 아니다.

## 관문 전 정리 (커밋 `21334fb`)

controller 결정 T6-a: Task 1~5의 리뷰가 미뤄 둔 작은 지적 열 가지를 관문 전에 한 커밋으로 고쳤다. 관문은 고친 코드로 돌았다.

1. `cancellingManyCallsAtOnceReturnsThemAllPromptly`: 앞의 `QVERIFY`가 실패해 일찍 돌아가면 스레드보다 클라이언트가 먼저 없어져, 아직 `get`에 들어가지 않은 스레드가 없어진 클라이언트를 부를 수 있었다. 이제 클라이언트와 스레드 뒤에 선언한 작은 객체가 소멸할 때 멈춤을 요청하고 모든 스레드를 합친 뒤에 클라이언트가 없어진다.
2. 시험의 `Gate`(잠자는 서버 흉내): 처리기가 이미 불렸지만 아직 `wait()`에 닿기 전에 `Gate`가 없어지면 죽은 객체를 만질 수 있었다. 이제 처리기는 공유 상태를 가진 `Waiter`를 값으로 잡고, `Gate`는 소멸할 때 문을 연다(`tst_qt_http_client`, `tst_http_fetch`). `tst_lrclib_lyrics_provider`의 `Gate`는 이미 `shared_ptr`로 처리기가 잡고 `cleanup()`이 서버를 멈추기 전에 열므로 바꾸지 않았다.
3. "2xx 뒤에 오류로 끝난 답은 부분 본문의 `Response`가 아니라 `Failed`"를 고정하는 시험: 기존 `aConnectionClosedInTheMiddleOfTheBody…`는 연결 오류(201 미만)만 덮고, Task 1 리뷰 반영이 고친 "201 이상의 오류 + 2xx 코드" 쪽은 덮지 않았다. 가짜 서버로 결정적으로 만들 수 있는 것을 찾아(풀리지 않는 gzip 본문) `anErrorAfterA2xxStatusIsAFailureNotAResponse`를 더했고, `code >= 400` 조건을 뺀 변이를 이 시험만 잡았다. 처음 함께 넣으려던 "깨진 청크 크기"는 Qt가 오류 없이 빈 200으로 끝내서 뺐다(위 관찰 3).
4. `tst_http_fetch.cpp`: 쓰지 않는 `<QDir>`를 빼고(`<QFileInfo>`를 직접 포함), 수 MB 바이트 배열 비교를 크기와 첫 다른 자리를 찍는 `QVERIFY2`로 바꿨다.
5. `tst_lrclib_lyrics_provider.cpp`의 200 ms 잠 둘: 그대로 두고, 무엇을 위한 것이고 스레드가 늦어도 시험은 통과한다(덜 증명할 뿐)는 주석을 달았다. 공급자에 시험용 갈고리는 넣지 않았다.
6. 대조 생성기 `gen.cpp`: Task 4 리뷰 반영 때 `plainLyrics`의 객체 값 `{"text":"a\nb\nc"}`의 `\n`이 실수로 날 LF 바이트가 되어 본문 전체가 JSON이 아니게 되었다. 이스케이프된 줄바꿈으로 되돌렸다. 문자열 안의 날 제어 문자는 종류 `J`의 생성기가 이미 내므로 따로 항목을 두지 않았다.
7. 대조 통계 `run.ps1`: `TRUE`·`fAlSe` 같은 별난 대소문자의 `instrumental`이 "형이 틀린 필드"로 세어져 그 통계를 부풀렸다(36~38%, 문턱 10%). 이제 따로 센다: "형이 틀린 필드"는 JSON 형이나 리터럴 꼴이 틀린 것만(`true`·`false`는 대소문자를 무시하고(ſ는 s로), `null`은 소문자만 빼고), 새 통계 "`instrumental`이 별난 대소문자인 레코드"는 문턱 10%(README에 적음). 형이 틀린 필드 문턱 10%는 그대로이고, 고친 세는 법으로 23.4~24.5%로 넘는다(아래 대조 표). 생성기의 섞는 비율은 바꿀 필요가 없었다.
8. 대조의 `maxRequests + 1`: `INT_MAX`에서 넘칠 수 있었다. 두 쪽(`Oracle.java`, `main.cpp`) 모두 같은 꼴로 `INT_MAX`이면 그대로 두게 했다(생성기는 그 값을 만들지 않는다는 주석과 함께).
9. `tst_real_lrclib.cpp`: 빈 줄 판정을 `trimmed().isEmpty()`에서 Kotlin `isNotBlank`와 같은 `xgs::kt::isBlank`로 바꿨다(세 곳). 내려받기 시험이 일부러 요청 수 세기를 거치지 않는다(lrclib.net이 아니라 GitHub)는 한 줄 주석을 달았다.
10. `tests/integration/CMakeLists.txt` 머리 주석에 `RealLrclibIntegrationTest`를 더했다. `qt/README.md`와 `tst_real_ytdlp.cpp`의 건너뛰기 문구에서 `downloadsAShortVideoAsM4aWithoutReencoding`이 "LRCLIB 조회(Q2c)가 없어서" 건너뛴다는 말이 낡아, 진짜 이유(내려받기·가사·태그를 잇는 작업은 Q2d)로 고쳤다. 건너뛰기 문구는 지시서가 README만 말했지만 같은 낡은 말이라 함께 고쳤다.

## Kotlin과 다르게 한 것

여덟은 계획 작성자가 정했고(사용자가 바꿀 수 있다), 나머지는 실행 중에 Task 구현자나 controller가 정했다. **실행 중에 정한 것은 사용자가 아직 보지 않았다.**

### 계획 작성자가 정한 것

1. **HTTP는 `QNetworkAccessManager`로 한다.** 전용 스레드 하나(`xgs-http`)가 가진 매니저에 요청을 넘기고 부른 스레드는 기다린다. 연결은 재사용된다. 예: 한꺼번에 16개를 부르면 Qt는 호스트마다 6개만 연결을 열고 나머지는 Qt 안에서 기다린다(Task 1이 본 것). Kotlin은 `java.net.http.HttpClient`다. 이 선택이 아래 9~16번의 전송 차이를 낳았다.
2. **`find`는 블로킹이고, 취소되면 값 없이 돌아온다.** 예: 멈춤을 요청하면 `find`는 3초 안에 빈 `std::optional`을 돌려준다(`cancellingTheCallWhileTheServerSleeps…`). Kotlin은 `CancellationException`을 던진다. 부른 쪽이 멈춤 토큰으로 "못 찾음"과 "취소"를 가린다.
3. **연결 제한 시간을 따로 두지 않는다.** 예: 연결에 12초 걸리는 서버라면 Kotlin은 10초에 연결을 포기하고, Qt 판은 요청 전체 제한 15초까지 기다린다. 둘 다 "연결 문제, 쉬는 시간 시작"이고 최대 5초 늦을 뿐이다. 이 경우를 시험하지는 않았다.
4. **프록시를 쓰지 않는다**(`QNetworkProxy::NoProxy`). 예: 회사 프록시 뒤의 PC에서는 LRCLIB와 yt-dlp 내려받기가 안 된다(Kotlin `HttpClient`도 Windows 프록시 설정을 읽지 않는다). 프록시 환경에서 돌려 보지는 않았다.
5. **후보 이름 길이 상한.** `trackName`·`artistName`·`albumName`이 UTF-16 1,000 단위를 넘으면 없는 필드로 읽는다. 예: 제목 `a`×1,000의 질의에 `a`×1,000 + `.`(1,001 단위)인 레코드는 답이 아니고, 1,000 단위인 레코드는 답이다(`aNameLongerThanAThousandUnitsIsReadAsMissing`). Kotlin에는 상한이 없다.
6. **yt-dlp 내려받기의 안전장치 셋.** 30초 동안 한 바이트도 안 오면 실패, 본문이 200MiB를 넘으면 실패(문구 `내려받는 파일이 너무 큽니다.`), `User-Agent`는 앱 이름(`xGetSongs/1.0 (https://github.com/gomtos/xGetSongs)`). 예: 1,000바이트 한도로 1,001바이트를 받으면 위 문구로 실패(`aBodyLargerThanTheLimitIsAFailure`). Kotlin `httpGet`에는 셋 다 없다(JDK 기본 `User-Agent`).
7. **네트워크 오류 문구는 Qt의 문구다.** 예: 연결이 거부되면 `yt-dlp 다운로드에 실패했습니다: ` 뒤에 JDK 예외 문구 대신 Qt의 `errorString()`이 온다. `HTTP 404` 같은 상태 문구는 Kotlin과 같다(`installYtDlpReportsAFailedDownloadWithTheKoreanPrefix`: `yt-dlp 다운로드에 실패했습니다: HTTP 404`).
8. **실제 서비스에 닿는 시험.** LRCLIB 통합 시험은 `XGS_INTEGRATION=1`일 때만 돌고 이 계획에서 두 번(Task 5, 관문) 돌았다. 가사 글자는 찍지 않는다. 예: 출력은 `26 lines, 23 non-blank, 462 characters` 같은 수뿐이다(Task 5의 실행). yt-dlp.exe를 실제로 내려받는 시험은 `XGS_INTEGRATION_DOWNLOAD=1` 뒤에 있고 돌리지 않았다.

### 실행 중에 더해진 것

controller의 결정(`progress.md`의 "Ruling")과 Task 구현자가 정한 것이다. Qt 클라이언트가 **실제로 보내는 요청**이 Kotlin과 다른 점(9~16번)은 모두 `tst_qt_http_client`가 루프백의 가짜 서버로 본 것이다(15번만 시험 없음).

9. **퍼센트 인코딩된 예약되지 않은 글자를 풀어서 보낸다**(T1-a). `QUrl`은 어떤 모드로 읽어도 `%41`·`%7E` 같은 RFC 3986의 예약되지 않은 글자(A-Z a-z 0-9 `-` `.` `_` `~`)를 푼다. 예: 공급자가 `q=%7E%2A` 를 넘기면 서버는 `q=~%2A`를 받는다(`theQueryStringReachesTheServerByteForByte`). RFC 3986 §2.3은 둘을 같다고 보지만 Kotlin은 `%7E` 그대로 보낸다. `~`가 든 곡 이름이면 실제로 이 차이가 난다. 대조 `lf`가 비교하는 주소(공급자가 넘긴 문자열)는 Kotlin과 같다.
10. **소문자 16진을 대문자로 바꾼다.** 예: `q=%ed%95%9c%2f%3f` → `q=%ED%95%9C%2F%3F`. 공급자는 늘 대문자를 쓰므로 공급자 주소에서는 차이가 없다.
11. **경로의 점 조각을 지운다**(Task 1 리뷰 반영). 예: `/a/../q?x=1` → `/q?x=1`, `/a/%2E%2E/q` → `/q`, 빈 경로 → `/`. LRCLIB 주소에는 점 조각이 없다.
12. **어떤 주소는 보내지 않고 `Failed`.** 날 공백(`?q=a b`), 홀로 선 `%`, 날 `[]{}|^`\`·`"<>`가 든 주소는 엄격 모드에서 깨진 주소라 요청 없이 실패한다(Task 1이 탐침으로 본 것, 시험은 `http://[not a url`·빈 주소·`ftp://`만). 예: `?a=%25&b=%` → 요청 없음, `Failed`. 공급자는 모든 글자를 인코딩하므로 이런 주소를 만들지 않는다. Kotlin 쪽이 같은 주소에 무엇을 하는지는 보지 않았다.
13. **조각(`#…`)은 보내지 않는다.** 예: `/q?q=1#x` → 서버는 `q=1`을 받는다. HTTP에서 옳은 동작이고 Kotlin `HttpClient`도 조각을 보내지 않을 것으로 보지만 Kotlin 쪽은 보지 않았다.
14. **Qt가 더하는 헤더**(Task 1 리뷰 반영). 호출자가 `User-Agent`만 주면 서버가 받는 헤더는 정확히 `Host`, `User-Agent`, `Accept-Language: en`, `Connection: Keep-Alive`, `Accept-Encoding: gzip, deflate`다(`onlyTheseHeadersAreSentBesidesTheCallersOwn`). `Accept-Language`는 처음에 Qt가 시스템 로캘로 `ko-KR,en,*`를 보냈다. 빈 값으로는 뺄 수 없어서 고정 `en`을 보낸다(호출자가 주면 그것). 예: LRCLIB 요청에는 Kotlin이 보내지 않는 `Accept-Encoding: gzip, deflate`, `Accept-Language: en`, `Connection: Keep-Alive`가 붙는다. 처음 관찰 때 있던 `Cache-Control: no-cache`·`Pragma: no-cache`는 우리 캐시 속성에서 온 것이라 그 속성을 빼서 없앴다.
15. **거절한 `https`→`http` 리다이렉트는 `Failed`다**(Task 1 리뷰 반영, 코드 주석). Qt의 `NoLessSafeRedirectPolicy`는 그런 리다이렉트를 `InsecureRedirectError`로 끝낸다. Kotlin의 `Redirect.NORMAL`은 따르지 않고 3xx 응답을 돌려준다. 예: `https://lrclib.net/api/get`이 `http://…`로 보내면 Kotlin 공급자는 3xx를 "200 아님, 다음 요청"으로, Qt 판은 `Failed`를 "그 요청은 없음, 다음 요청"으로 읽는다. 공급자에게는 결과가 같다. `httpFetch`에서는 문구가 다르다(Kotlin `HTTP 3xx`, Qt 판 Qt 오류 문구). **시험 없음**(TLS 서버가 필요하다).
16. **gzip 본문은 푼 크기로 한도를 잰다.** Qt가 `Accept-Encoding: gzip, deflate`를 보내고 받은 본문을 스스로 풀기 때문이다. 예: 서버가 1MB로 압축한 3MB 본문을 주면 2MiB 한도의 LRCLIB 요청은 Qt 판에서 "너무 큼"이다(Kotlin은 압축을 청하지 않으므로 서버가 3MB를 그대로 보내고 역시 너무 큼). 최종 리뷰 반영에서 루프백 시험으로 확인했다: 약 13KB로 압축되어 2MiB를 넘게 풀리는 본문은 2MiB 한도에서 `bodyTooLarge`, 푼 크기 1,000바이트는 1,000 한도에서 읽고 1,001은 너무 큼(`aGzipBodyIsMeasuredDecodedAgainstTheLimit`). 호출자가 `Accept-Encoding: identity`를 주면 압축을 청하지 않는다.
17. **2xx 코드와 함께 온 201 이상의 오류는 `Failed`**(Task 1 리뷰 반영). Qt는 4xx·5xx도 "오류"로 알리므로, 오류여도 상태 코드가 400 이상일 때만 답(`Response`)으로 본다. 예: 200 뒤 풀리지 않는 gzip → `Failed`(관문 전 정리의 새 시험). Kotlin은 같은 경우 `IOException`을 던질 것으로 보지만 보지 않았다.
18. **`UnknownNetworkError`는 응답 전이면 `Unreachable`**(Task 1의 판단). 위 관찰 3. 공급자에게 `Unreachable`은 쉬는 시간을 시작하는 연결 문제다. 예: Qt가 응답 전에 `UnknownNetworkError`를 알리면 LRCLIB 공급자는 그 호출을 멈추고 5분 동안 묻지 않는다. 같은 오류가 응답 머리 뒤에 오면 `Failed`라 그 요청만 없음이고 다음 변형을 묻는다. 확인하지 못했다(일으켜 보지 않았다).
19. **요청 시간 제한이 0 이하면 아무것도 보내지 않는다**(Task 3). Kotlin의 `withTimeoutOrNull`이 그 시간이면 곧바로 null을 주는 것을 옮겼다(C++ 클라이언트는 0을 "제한 없음"으로 읽으므로 따로 처리했다). Kotlin처럼 그것을 `Stop`으로 보고 쉬는 시간도 시작하지만, 같은 시간 제한으로는 다음 호출도 어차피 아무것도 보내지 않으므로 밖에서는 보이지 않는다. 예: `requestTimeout` 0으로 두 번 부르면 두 번 다 요청 0개, 답 없음이고, 대조에서 Kotlin도 같은 답(첫 호출의 주소 없음, 둘째 호출 요청 0개)이다(관찰 2). Kotlin과 다른 것이 아니라 Kotlin을 따르려고 더한 동작이다.
20. **예상 밖의 C++ 예외는 그 호출 전체를 "없음"으로 끝낸다**(Task 4가 헤더 주석에 적음). Kotlin은 `IOException`이 아닌 예외를 그 요청의 "없음"으로 바꾸고 다음 요청으로 간다. 예: 클라이언트나 JSON 읽기가 C++ 예외를 던지면(지금 코드에서 그런 길은 알려진 것이 없다) Qt 판은 남은 변형을 묻지 않는다. 대조는 이런 예외를 만들지 않는다.
21. **멈춤이 요청되면 `FallbackLyricsProvider`는 남은 공급자에게 묻지 않는다**(Task 4, 리뷰 지적). 예: 멈춘 토큰으로 부르면 둘째 공급자는 질의를 받지 않고 답은 없음(`aStopRequestStopsAskingTheRemainingProviders`). Kotlin이라면 취소된 `find`가 던져서 다음으로 가지 않는다. 결과는 같은 셈이다.
22. **새 한국어 문구 `내려받는 파일이 너무 큽니다.`**(Task 2). 위 6번의 200MiB 한도에서 나온다. Kotlin에 짝이 없다. 예: 200MiB를 넘는 yt-dlp.exe 내려받기는 `yt-dlp 다운로드에 실패했습니다: 내려받는 파일이 너무 큽니다.`로 실패한다(시험은 한도를 1,000바이트로 줄인 `aBodyLargerThanTheLimitIsAFailure`).
23. **서버가 보낸 407은 `Failed`**(최종 리뷰 반영에서 관찰). 프록시 없이 닿은 서버가 407을 주면 Qt는 `ProxyAuthenticationRequiredError`(105, 클라이언트가 답으로 보는 201 이상 범위 밖)로 알리고 상태 코드도 주지 않아 Qt 판은 Qt의 문구 `Proxy requires authentication`의 `Failed`다(`Proxy-Authenticate` 머리가 있든 없든, `a407IsAFailureWithQtsTextNotAResponse`). 이것은 릴리스 판의 동작이다. Qt 6.11.3의 디버그 판은 같은 응답에서 자기 단언(`qnetworkaccessauthenticationmanager.cpp`의 `p.type() != QNetworkProxy::NoProxy`)으로 프로세스를 멈춘다(첫 검증의 디버그 `ctest`가 `0xc0000409`로 잡았다). 그래서 이 시험은 디버그 빌드에서 건너뛴다. 앱의 디버그 빌드도 그런 서버를 만나면 멈출 수 있다. Kotlin `HttpClient`는 상태 코드를 응답으로 돌려주고 앱은 `Authenticator`를 두지 않으므로 407 응답을 돌려줄 것으로 보지만 보지 않았다. 예: LRCLIB가 407을 주면 두 쪽 모두 그 요청은 없음(Kotlin: 200 아님, Qt 판: `Failed`). `httpFetch`에서는 Kotlin이라면 `HTTP 407`, Qt 판은 Qt의 문구가 한국어 앞말 뒤에 붙는다.
24. **리다이렉트는 50번까지 따른다.** Qt의 기본 한도(`QNetworkRequest`의 최대 리다이렉트 50)이고 넘으면 `TooManyRedirectsError`로 `Failed`다(`moreThanFiftyRedirectsAreAFailure`: 서버가 요청 51개를 받음). JDK `HttpClient`의 문서에 적힌 기본 한도는 5(`jdk.httpclient.redirects.retrylimit`)이고 넘었을 때 무엇을 돌려주는지는 보지 않았다. 예: 리다이렉트를 6~50번 거치는 주소는 Qt 판에서 끝까지 따라가 답을 받고, Kotlin에서는 한도에 걸린다. 공급자에게는 넘은 경우 두 쪽 모두 그 요청의 없음일 것이고(Kotlin이 3xx를 돌려주든 `IOException`을 던지든), `httpFetch`는 문구가 다르다.
25. **자리를 기다리는 호출은 정해진 순서 없이 자리를 받는다.** C++ 공급자는 `std::condition_variable_any`로 기다리므로 자리가 비면 기다리던 호출 중 어느 것이 먼저 받을지 정해져 있지 않다. Kotlin은 `kotlinx.coroutines.sync.Semaphore`이고 리뷰는 그것이 먼저 온 순서(FIFO)라고 지적했다(이 작업에서는 그 문서를 다시 열어 보지 못했다: 네트워크를 쓰지 않는 규칙). 결과(가사와 요청 수)에는 드러나지 않고, 어느 곡을 먼저 묻는지만 다를 수 있다. 예: `maxConcurrent` 1에서 세 곡이 기다리면 Kotlin은 온 순서로, Qt 판은 운영체제가 깨우는 순서로 묻는다.
26. **Qt의 압축 해제 안전 검사에 걸린 본문은 `Failed`**(최종 리뷰 반영에서 관찰하고 고침). Qt는 gzip 본문을 받은 만큼 한 번에 풀고, 푼 크기가 `QNetworkRequest::decompressedSafetyCheckThreshold`(기본 10 MiB, 시험이 확인)를 넘어 그 검사에 걸리면 `UnknownContentError`(`Decompression failed: The decompressed output exceeds the limits …`)를 알린다. 이때 Qt는 응답을 끝내지 않아서, 고치기 전에는 호출이 전체 시간 제한까지(없으면 영원히) 기다렸다. 이제 클라이언트가 그 응답을 Qt의 오류로 끝낸다(`Failed`). 예: 1MiB 한도로 쉬지 않고 오는 끝없는 gzip 본문은 탐침에서는 Qt가 한 번에 10MiB 넘게 풀어 `Failed`(Qt 문구)였고(시험은 `Failed`와 `bodyTooLarge` 둘 다 받아들이며, 재검증 실행에서 어느 쪽이었는지는 남기지 않았다), 20ms마다 조금씩 오는 같은 본문은 클라이언트의 한도가 먼저 걸려 `bodyTooLarge`다. 공급자에게는 둘 다 그 요청의 없음(Kotlin의 200 + 너무 큼도 없음)이고, `httpFetch`에서는 `내려받는 파일이 너무 큽니다.` 대신 Qt의 문구가 붙는다. 한도가 10MiB보다 큰 `httpFetch`(200MiB)에서는 압축률이 높은 10MiB 넘는 gzip 본문이 한도 안이어도 Qt 판에서 실패할 수 있다(Kotlin은 압축을 청하지 않으므로 서버가 그대로 보낸다). Kotlin에는 이 길이 없다.

## 고치지 않은 Kotlin 쪽 동작

Qt 판이 그대로 따른다(대조나 시험에서 같은 답).

1. **`booleanOrNull`이 `TRUE`·`tRuE`·`falſe`를 불리언으로 읽는다**(Task 4). kotlinx의 동작이고 공급자의 버그는 아니다. `instrumental: TRUE`인 레코드를 연주곡으로 보는 것은 오히려 맞는 쪽이다.
2. **문자열 `"258"`도 `doubleOrNull`로는 수다.** 공급자는 문자열을 길이로 읽지 않으므로 드러나지 않는다.
3. **요청 시간 0에서 바로 쉬는 시간.** 위 19번. 설정 값이 0일 때 쉬는 시간(5분)에 드는 것은 이상해 보일 수 있지만 Kotlin과 같다.
4. Task 4는 공급자 자체에서 버그로 보이는 Kotlin 동작을 찾지 못했다고 적었다.

Q2a·Q2b 보고서의 "고치지 않은 Kotlin 쪽 동작"은 그대로 남아 있다.

## 계획의 작성자 결정 여덟과 Review Focus 여섯 가지

작성자 결정 여덟은 위 "계획 작성자가 정한 것" 1~8에 하나씩 예와 함께 적었다. 사용자가 거절하면 어떻게 되는지는 아래 "사용자가 정할 것" 1번.

| 갈래 | 확인한 곳 | 결과 |
|---|---|---|
| 1. 정적 빌드에 TLS가 안 들어간다 | `theSchannelTlsBackendIsLinkedIn`(네트워크 없이 `QSslSocket::supportsSsl()`과 활성 백엔드 `schannel`), 통합 `theRealServiceIsReachedOverSchannelTls` | 관문의 두 `ctest`에서 통과. 실제 `https://lrclib.net`도 관문의 통합 실행에서 통과(`theRealServiceIsReachedOverSchannelTls`: HTTP 200, 그리고 Kotlin 시험 넷이 같은 클라이언트로 `https`에 닿았다). 링크 줄에 `qschannelbackend.lib`가 저절로 들어간다(Task 1) |
| 2. Qt가 주소를 다시 쓴다 | `theQueryStringReachesTheServerByteForByte`, 대조 `lf`의 주소 비교 | 다시 쓴다: `%7E`→`~` 등(위 9~13번). 시험은 그 다시 쓰기를 그대로 고정한다. 공급자가 넘기는 주소는 대조에서 Kotlin과 같다(불일치 0) |
| 3. 곡 이름의 짝 없는 서로게이트, NUL, `& = # + %` | 대조 `lf`의 질의 통계 | 관문의 모든 대조에서 문턱을 넘고 불일치 0(아래 대조 표: 짝 없는 서로게이트 21.0~22.1%, `& = # + %`·공백 97.0~97.8%) |
| 4. LRCLIB 답의 형이 다르다 | 대조 `lf`의 "형이 틀린 필드"와 새 "별난 대소문자 `instrumental`" 통계 | 이 갈래가 버그 하나를 찾았다(위 `booleanOrNull`). 관문에서 두 통계 모두 문턱 10%를 넘었다(23.4~24.5%, 22.2~25.0%) |
| 5. 여러 곡이 한꺼번에 취소된다 | `cancellingManyCallsAtOnceReturnsThemAllPromptly`(16개, 3초 안, 그 뒤 같은 클라이언트가 정상 응답), `aCallWaitingForASlotReturnsWhenItIsCancelled`, `manyCallsCancelledAtOnceAllReturn`(12개, 서버는 2개만 받음) | 관문의 두 `ctest`와 두 빌드의 20번 되풀이(`tst_qt_http_client`, `tst_lrclib_lyrics_provider`)에서 모두 통과. 실제 Q2d 작업 흐름의 취소는 아직 없다 |
| 6. 서버가 응답 중간에 끊거나 길이를 속인다 | `aConnectionClosedInTheMiddleOfTheBodyIsAFailureNotATimeout`(길이 100을 약속하고 10바이트 뒤 RST), 새 `anErrorAfterA2xxStatusIsAFailureNotAResponse` | 통과. 깨진 청크 크기는 Qt가 빈 200으로 받는다는 것을 관찰했다(위 관찰 3, 고정하지 않음) |

## 계획과 달라진 것

- **`theQueryStringReachesTheServerByteForByte`의 뜻**(T1-a). 계획은 "바이트 그대로 닿는다"를 기대했으나 Qt는 그렇게 보낼 수 없다(`QNetworkRequest`는 `QUrl`만 받는다). Task 1이 BLOCKED로 멈췄고, controller가 `QNetworkAccessManager`를 지키고 다시 쓰기를 시험에 고정하기로 정했다. 시험 이름은 계획대로 두었다.
- **Kotlin 짝 없는 시험의 기록**(T1-b). 계획의 "added" 말은 Kotlin 파일이 있는 경우를 생각한 것이라, `tst_qt_http_client`·`tst_http_fetch`는 `test-classes.json`이 아니라 README 목록에 적었다.
- **`AppUserAgent`의 자리**(P1). Task 2가 `http_client.h`에 `AppUserAgent`를 두었다(그 파일이 Task 2의 파일 목록에 없었다).
- **가짜 서버는 `127.0.0.1`에만 묶는다**(P2). 와일드카드에 묶으면 Windows 방화벽 상자가 뜰 수 있다.
- **더한 시험 이름.** 계획이 적은 수보다 많다: `tst_qt_http_client` 22(계획 20), LRCLIB 추가 7(계획 5: 대조가 찾은 버그의 회귀 시험과 요청 시간 0 시험이 더해졌다), Fallback 추가 1(계획 0, 리뷰 지적).
- **대조 답의 `C` 필드**(T4-a, Task 4 리뷰 반영). "상한이 조회를 끊음" 통계를 답만으로 셀 수 없어서 두 쪽 모두 `maxRequests + 1`로 한 번 더 돌린 요청 수를 답에 넣었다(비교도 한다).
- **Task 3의 작은 지적과 README는 Task 4가 했다**(T3-a). **작업별 리뷰의 남은 지적은 관문 전에 한 커밋으로 고쳤다**(T6-a, 위 "관문 전 정리").
- **관문에서 `tst_real_ytdlp`를 돌렸다.** 지시서는 "이 단계가 프로세스 실행기와 yt-dlp 코드를 바꾸지 않으므로 돌리지 않는다, 바꿨다면 한 번 돌린다"였다. `git diff --stat 2b7495a HEAD -- qt/core/process qt/core/ytdlp qt/core/tools`가 `qt/core/process/line_splitter.cpp`(74줄 중 72줄 빠짐)와 `line_splitter.h`를 보였다: Task 3이 UTF-8 풀기를 `kt::decodeUtf8`로 옮긴 것이다(코드는 그대로 옮겼다고 Task 3이 적었다). 실행기가 yt-dlp의 출력 줄을 푸는 곳이 바뀐 것이므로 지시서의 "바꿨다면"에 해당한다고 보고 한 번 돌렸다(오디오 없음). `qt/core/ytdlp`와 `qt/core/tools`는 바뀌지 않았다.
- **새 대조 통계 하나**(관문 전 정리 7번). 문턱을 낮추지 않고 세는 법을 이름에 맞췄다.
- **시험용 갈고리 하나(최종 리뷰 반영).** `qt_http_client.h`에 시험 전용 `xgs::net::test_hooks::postToNetworkThread`를 더했다(클라이언트의 네트워크 스레드에 일을 넣는다). 소멸자와 겹친 호출을 결정적으로 재현할 다른 길이 없었다. 제품 코드는 부르지 않는다. 최종 리뷰 반영 뒤 `tst_qt_http_client`는 33개다.
- **README(Task 6).** "관문" 절이 Q2b까지만 말해서 Q2c를 더하고, 통합 시험 단계에 `XGS_INTEGRATION_DOWNLOAD`도 켜지 않는다는 것과 `tst_real_lrclib`는 허락한 횟수만 돈다는 것을 한 문장에 더했다(보고서 커밋에 함께).

## 확인하지 못한 것

- **HTTPS의 리다이렉트와 시간 제한.** 리다이렉트·전체/무소식 시간 제한·본문 한도·취소는 모두 루프백의 `http://`로만 보았다. TLS 위에서 같은지는 실제 lrclib.net 몇 요청으로만 보았다(시간 제한이나 리다이렉트는 일어나지 않았다). 거절한 `https`→`http` 리다이렉트(15번)는 시험이 없다.
- **모르는 호스트, `UnknownNetworkError`, `TimeoutError` 등의 실제 길.** 코드에서 `Unreachable`/`Failed`로 정했지만 일으켜 보지 않았다(위 관찰 3).
- **깨진 청크 크기를 Kotlin은 어떻게 하나.** Qt는 빈 200으로 받았다(관찰 3). Kotlin `HttpClient`는 보지 않았다.
- **연결에 10~15초 걸리는 서버**(3번)는 시험하지 않았다. gzip 본문의 크기 한도(16번)는 최종 리뷰 반영에서 루프백으로 시험했다. 다만 Qt의 압축 해제 안전 검사가 정확히 어떤 조건(푼 크기 말고 압축률도 보는지)에서 걸리는지는 두 경우(13KB → 2.1MB는 걸리지 않음, 끝없는 본문은 10MiB를 넘어 걸림)로만 보았다.
- **재사용한 연결.** 가짜 서버는 늘 `Connection: close`로 답하므로 루프백 시험은 연결을 다시 쓰지 않는다. 실제로는 `Connection: Keep-Alive`로 연결을 다시 쓴다. 서버가 닫아 버린 재사용 연결이 `Failed`(그 요청의 없음)가 되는지 Qt가 조용히 다시 보내는지는 보지 않았다.
- **시간에 기대는 시험.** 최종 리뷰 반영에서 셋을 단단하게 했지만(아래), `aConnectionClosedInTheMiddleOfTheBodyIsAFailureNotATimeout`(재설정 전 1초)과 `aCallQueuedWhileTheClientIsDestroyedEndsCleanly`(호출이 줄 서기를 300ms 기다림) 등은 여전히 넉넉한 시간 여유에 기댄다.
- **프록시 환경.** 프록시 뒤의 PC에서 돌려 보지 않았다(작성자 결정 4).
- **Windows 10.** Windows 11에서만 돌렸다. Schannel의 TLS 판·암호 묶음이 Windows 10에서 lrclib.net·GitHub과 맞는지 보지 않았다.
- **실제 yt-dlp.exe 내려받기.** `downloadsTheRealYtDlpExecutable`(약 18MB, `XGS_INTEGRATION_DOWNLOAD=1`)은 돌리지 않았다. GitHub의 리다이렉트(`/latest/download/…` → `objects.githubusercontent.com`)를 Qt 클라이언트가 실제로 따르는지는 루프백의 302로만 보았다(`aRedirectToTheFileIsFollowed`).
- **`yt-dlp -U`**(Q2b에서 넘어온 것). 업데이트 뒤 `yt-dlp.exe.old`가 남는지 보지 않았다.
- **오디오를 내려받는 통합 시험**(Q2b의 `writesTheInfoFileNextToTheM4aAndTheTestVideoHasNoAlbum`)과 **`downloadsAShortVideoAsM4aWithoutReencoding`**(단언은 Q2d).
- **디버그 빌드의 통합 시험.** 실제 서비스로는 릴리스만 돌렸다.
- **Q2b의 `tst_system_process_runner` 실패 한 번의 원인.** 관문에서 두 빌드 각 20번(그리고 `ctest` 한 번씩) 돌려 모두 통과했다(위 "되풀이 실행"). 재현되지 않았고 원인은 모른다.
- **Task 5 중의 "한 번 실패"**: 어느 시험이었는지 확인되지 않았다(구현자가 읽지 않았고, `LastTestsFailed.log`는 Task 5 전의 날짜였다). 관문의 되풀이 실행 결과는 위 표.
- **단언의 같음.** `test-names.ps1`은 이름만 본다.
- **seed가 적다.** 관문의 대조는 1만 건 seed 셋과 10만 건 seed 하나다. `lf`는 `W`의 아홉 op 가운데 하나라 1만 건 실행에서 약 1,100건이다.
- **대조가 다루지 않는 것.** 이름 길이 상한, 취소, 연결 제한 시간(승인된 차이들), C++ 예외(20번)는 대조에 없다. 생성된 이름은 1,000 단위에 닿지 않는다.
- **통계는 근사다.** "형이 틀린 필드", "별난 대소문자", "JSON이 아님", "후보 둘 이상"은 `run.ps1`의 정규식과 C# 훑기로 센다. 답의 비교는 정확하다.
- **Q2b·Q2a·Q1에서 열려 있는 것**은 그대로다.

## 실제 서비스에 보낸 요청 수

| 실행 | lrclib.net 요청 | 원본 |
|---|---|---|
| Task 5(첫 번째 승인된 실행) | 15 | Task 5 보고서(시험이 `cleanupTestCase`에서 찍은 수) |
| 관문(두 번째이자 마지막 승인된 실행) | 15 | `17-integration-lrclib.txt`의 `requests sent to the service:` 줄 |
| **합계** | **30** | |

수는 `CountingClient`가 센 `get` 호출이다(서비스가 답하는지 보는 탐침 포함). 승인된 범위는 실행마다 30개쯤까지였다. 관문의 `tst_real_ytdlp` 한 번은 유튜브에 yt-dlp를 다섯 번 띄웠다(재생목록 목록 하나, 영상 넷의 정보 파일과 썸네일, 오디오 없음; README의 설명).

## Q2d에 넘길 것

- **가사 공급자를 작업에 잇기.** `FallbackLyricsProvider`(설명란 → LRCLIB → 구글)를 내려받기 작업에 잇는다. `find`가 빈 값을 주면 부른 쪽이 멈춤 토큰으로 "취소"와 "못 찾음"을 가린다(작성자 결정 2). 쉬는 시간(5분)은 공급자 객체마다이므로 재생목록 전체가 공급자 하나를 나눠 써야 뜻이 있다.
- **미룬 통합 시험 `downloadsAShortVideoAsM4aWithoutReencoding`.** 단언(Kotlin `RealYtDlpIntegrationTest.kt`)을 내려받기 작업과 함께 옮긴다. 오디오를 내려받고 LRCLIB에 묻는 시험이므로 돌리기 전에 사용자의 허락이 필요하다.
- **클라이언트 하나를 앱 전체가 나눠 쓰기.** `QtHttpClient`는 스레드 하나와 연결 풀을 가진다. LRCLIB 공급자와 yt-dlp 내려받기(`httpFetch`)가 같은 클라이언트를 쓰게 하고, 앱이 끝날 때 하나만 없앤다(없애면 진행 중인 요청은 `Failed`로 끝난다, `destroyingTheClientWithACallInFlightEndsTheCall`).
- **TLS 플러그인은 실행 파일마다 확인한다.** 정적 Schannel 플러그인이 링크됐다는 증거(`theSchannelTlsBackendIsLinkedIn`)는 `tst_qt_http_client.exe`의 것이다. 앱 실행 파일과 `QtHttpClient`를 쓰는 새 실행 파일은 링크 줄이 다르므로, 시작할 때나 시험에서 `QSslSocket::supportsSsl()`과 활성 백엔드 `schannel`을 같은 식으로 확인한다.
- **클라이언트의 수명.** 앱의 `QtHttpClient` 하나는 `QCoreApplication`보다 먼저 없앤다. LRCLIB 공급자와 `httpFetch`는 클라이언트를 참조로 쥐므로 클라이언트보다 먼저 없어지거나 쉬고 있어야 한다(헤더는 "클라이언트가 더 오래 살아야 한다"고만 적는다).
- **`Unreachable`과 `Failed`의 뜻을 작업이 지키기.** 공급자는 `Unreachable`을 쉬는 시간의 이유로 쓴다. 다른 호출자가 새 상태를 쓰면 이 구분을 따른다.
- **멈춘 응답 끝내기의 순서 가정(재리뷰 N-1).** Qt가 오류를 알렸는데 응답을 끝내지 않으면 클라이언트가 줄 세운 확인에서 직접 끝낸다. 보통의 404·429·503도 오류를 알리는 순간에는 아직 끝나지 않았고 `finished`가 뒤따른다(관찰한 것은 순서이지 같은 호출 안이라는 것이 아니다; 코드 주석의 "곧바로 끝낸다"는 관찰보다 세게 적혀 있다). 확인이 먼저 돌아도 추적한 답은 같지만, Qt가 그 순서를 바꾸면 4xx 본문이 잘릴 수 있다. 4xx 본문을 읽는 호출자가 생기면 다시 본다.
- **상태 400 이상인 응답이 안전 검사에 걸리면(재리뷰 N-2)** 답은 `Failed`가 아니라 그 상태의 `Response`이고 본문은 풀린 데까지이며 `bodyTooLarge`는 꺼져 있다. 헤더 주석은 `Failed`라고만 적는다. 지금은 4xx 본문을 읽는 호출자가 없어 보이는 차이가 없다.
- 코드 주석의 "시간 제한이 없으면 끝나지 않는다"는 10초 안에 끝나지 않는 것까지만 관찰했다(재리뷰 N-3).
- Q2b 보고서의 "Q2d에 넘길 것"은 그대로다.

## 사용자가 정할 것

지금 코드는 아래 "지금"대로 동작하고, 관문은 그 동작으로 돌았다.

1. **계획 작성자의 결정 여덟**(위 "계획 작성자가 정한 것"). 거절하면:
   1. `QNetworkAccessManager` 대신 WinHTTP를 직접 쓰게 다시 짠다(Task 1을 다시 하고, 9~16번의 전송 차이는 사라지거나 바뀐다). 지금: Qt.
   2. 취소를 값 없음 대신 예외나 별도 결과로 알리게 바꾼다(공급자와 부른 쪽의 약속이 바뀐다). 지금: 값 없음.
   3. 연결 10초 제한을 따로 둔다(Qt에 그 설정이 없어 연결 단계를 따로 재는 코드가 필요하다). 지금: 요청 전체 15초 하나.
   4. 시스템 프록시를 쓴다(`QNetworkProxyFactory::setUseSystemConfiguration`; Kotlin과 달라진다). 지금: 프록시 없음.
   5. 이름 길이 상한을 없앤다(아주 긴 이름에서 매칭이 느려질 수 있다, Q2a 인계). 지금: 1,000 단위.
   6. 내려받기 안전장치를 뺀다(멈춘 서버를 취소할 때까지 기다리고, 크기 한도 없음, JDK 같은 기본 `User-Agent` 없음). 지금: 30초·200MiB·앱 이름.
   7. 네트워크 오류 문구를 한국어로 바꾼다(지금은 Qt 영어 문구가 한국어 앞말 뒤에 붙는다). 지금: Qt 문구.
   8. 실제 서비스 시험을 더 돌리거나 덜 돌린다. 지금: 두 번 돌았고 더 돌릴 계획 없음.
2. **고정 `Accept-Language: en`**(14번). Qt가 시스템 로캘(`ko-KR,en,*`)을 보내는 것을 막으려고 고정 값을 보낸다. 대안: 그대로 두거나, 로캘을 보내게 두거나(사용자 PC의 언어가 서버에 간다), 다른 값. Kotlin은 이 헤더를 보내지 않는다. 지금: `en`.
3. **실제 yt-dlp.exe 내려받기 시험을 돌릴지.** `XGS_INTEGRATION_DOWNLOAD=1`로 `downloadsTheRealYtDlpExecutable` 하나(약 18MB를 GitHub에서 받아 메모리에서 `MZ`만 보고 버린다, 디스크에 쓰지 않음). 지금: 돌리지 않음.
4. **오디오를 내려받는 통합 시험을 돌릴지**(Q2b에서 넘어온 것, `XGS_INTEGRATION_AUDIO=1`, 19초 영상의 오디오 약 300KB). 지금: 돌리지 않음.
5. **`yt-dlp -U` 확인을 할지**(Q2b에서 넘어온 것). 업데이트 뒤 `yt-dlp.exe.old`가 남는지 보려면 yt-dlp 사본으로 실제 업데이트를 한 번 돌려야 한다. 지금: 확인하지 않음.
6. **새 한국어 문구 `내려받는 파일이 너무 큽니다.`**(22번). 지금: 그 문구.
7. **실행 중에 정해진 다른 차이들**(9~21번과 23~26번: 전송 때의 주소·헤더 다시 쓰기, 거절한 리다이렉트를 `Failed`로, gzip 한도, 2xx 오류를 `Failed`로, `UnknownNetworkError`의 분류, C++ 예외의 처리, Fallback의 멈춤, 407과 리다이렉트 한도, 자리의 순서, Qt의 압축 해제 안전 검사)을 받아들일지.
8. **`~`의 전송**(9번). 곡 이름에 `~`가 있으면 Qt 판은 `%7E` 대신 `~`를 보낸다. 받아들일지, 아니면 Qt 밖의 HTTP 구현이 필요한 일로 볼지. 지금: `~`.
9. Q2b·Q2a 보고서의 "사용자가 정할 것"과 Q1에서 남은 결정은 그대로 기다린다.

## 최종 리뷰 반영 (2026-11-21)

관문(커밋 `21334fb`)과 이 보고서 뒤에 단계 전체의 최종 리뷰가 지적한 것을 고쳤다. 코드 커밋 `cad473f`, `cac544b`. 원본 출력은 `C:\Users\gomto\AppData\Local\Temp\claude\46803f77e57fcdfd\004c41bb-2704-4ab0-875b-470a2bebd248\scratchpad\q2c\gate-fix\`(아래 "고침 폴더")에 있다. 위의 관문 표들은 관문 때의 것이고 바꾸지 않았다.

**고친 것.**

- **소멸자와 네트워크 스레드의 경쟁(I-1).** 네트워크 스레드가 소멸자가 지우는 `context`를 잠금 없이 읽었다. 이제 네트워크 스레드는 `open()`에서 정한 `receiver`만 쓰고, 닫는 중에 실행된 `start`는 요청 없이 `Failed`로 답한다. 새 시험 `aCallQueuedWhileTheClientIsDestroyedEndsCleanly`는 시험용 갈고리(`test_hooks::postToNetworkThread`, 위 "계획과 달라진 것")로 네트워크 스레드를 붙잡고 호출 하나를 줄 세운 채 클라이언트를 없앤다. 고치기 전 코드에서는 Qt 경고 `QObject::connect(QNetworkReplyHttpImpl, Unknown): invalid nullptr parameter` 넷으로 실패했고(고침 폴더 `probe-before-fix.txt`), 닫는 중 검사를 빼도 `receiver`만으로 통과한다(`mutation-no-closing-check.txt`).
- **gzip 본문(I-2, M-5).** 요청 머리는 그대로 두고(`Accept-Encoding: identity`를 보내지 않음) 가짜 서버가 시험 안에서 만든 gzip을 주는 시험 일곱을 더했다: 푼 본문을 돌려줌, 푼 크기로 재는 한도(1,000/1,001바이트, 약 13KB가 2MiB 넘게 풀리는 본문), 조금씩 오는 끝없는 본문은 1MiB에서 `bodyTooLarge`이고 서버가 클라이언트가 떠난 것을 봄, 쉬지 않고 오는 끝없는 본문, 멈춘 본문의 전체·무소식 시간 제한, 멈춤. **이 시험이 클라이언트의 버그를 찾았다**: 쉬지 않고 오는 끝없는 gzip 본문에서 Qt의 압축 해제 안전 검사가 걸리면 Qt는 `errorOccurred`만 내고 응답을 끝내지 않아서, 호출이 전체 시간 제한(시험에서 20초)까지 기다렸고 시간 제한이 없으면 영원히 기다렸을 것이다(`probe-before-fix.txt`: `Unreachable … within 20000 ms`). 이제 클라이언트가 오류 뒤에도 끝나지 않은 응답을 Qt의 오류로 끝낸다(위 26번). 이 처리를 빼면 새 시험이 10초 안에 끝나지 않아 실패한다(`mutation-no-error-handler.txt`). 함께 본 것: 한도를 읽는 `bytesAvailable()`은 푼 바이트를 센다(2MiB 한도에서 처음 본 값 2,115,213, 임시 디버그 출력 `probe-debug-bytes-available.txt`; Qt의 오류 문구는 `probe-debug-safety-check-error.txt`). 다만 Qt는 읽은 만큼을 한 번에 풀므로 한도보다 조금 더(한 번 읽은 만큼) 들고 있을 수 있다.
- **Kotlin과의 차이 넷(M-2)과 시험 셋.** 위 23~26번. 407(`a407IsAFailureWithQtsTextNotAResponse`, 디버그 빌드에서는 건너뜀: 23번)과 리다이렉트 50번(`moreThanFiftyRedirectsAreAFailure`)을 시험으로 고정했고, 오류 처리를 더한 뒤에도 느린 4xx 본문을 통째로 읽는지 `anErrorAnswerWhoseBodyComesSlowlyIsReadWhole`로 본다(Qt는 4xx 오류를 본문이 다 온 뒤에 알린다: `probe-4xx-timing.txt`). 재사용 연결은 "확인하지 못한 것"에 더했다.
- **요청 시간 0과 쉬는 시간(M-1).** 시험 이름을 `aRequestTimeoutOfZeroSendsNothing`으로 바꿨고(관찰 2, 19번의 문장도), 대조 통계 "쉬는 시간 시작"은 "요청이 쉬는 시간을 시작"으로 이름을 바꿔 요청 시간이 0 이하인 사례를 양쪽 어디에도 세지 않는다. 문턱(10%)과 생성기는 그대로다.
- **시간에 기대던 시험 셋(M-3).** `theSlotsAreSharedByEveryCallOfOneProviderButNotByTwoProviders`의 둘째 절반은 두 처리기가 서로 들어올 때까지(최대 5초) 기다린다(단언 `peak == 2`는 그대로). `aConnectionClosedInTheMiddleOfTheBodyIsAFailureNotATimeout`의 재설정 전 기다림은 300ms에서 1초로, `aCallWaitingForASlotReturnsWhenItIsCancelled`의 한계는 1초에서 3초로 늘렸다(주석에 이유).
- **`tst_real_lrclib`의 값 읽기(M-4).** 불리언은 공급자처럼 대소문자 없이, 수는 `kt::toDoubleOrNull`로 읽는다. 이 실행 파일은 실제 서비스로는 다시 돌리지 않았다(`ctest`에서는 건너뜀으로만 돌았다, 아래).
- **Q2d에 넘길 것(M-6).** TLS 플러그인 확인과 클라이언트 수명 두 줄을 더했다.
- **Task 6 리뷰의 작은 지적 1·2·3·4·6.** "JSON 아님"의 문장, `null`의 대소문자, `00-timeline-build-run.txt` 끝의 첫 시도, README의 Q2c, 18·19·22번의 예.

**다시 돌린 것**(코드 커밋 `cac544b`, 작업 트리의 코드 깨끗함: `01-head.txt`; 시각은 고침 폴더 `00-timeline.txt`):

| 점검 | 결과 | 원본 |
|---|---|---|
| 릴리스 깨끗한 빌드 | `Cleaning... 152 files.`, `[191/191]`, 컴파일러 경고 0(`CMake Warning` 하나는 관문과 같은 Qt 매크로의 것) | `02-build-release.txt` |
| 디버그 깨끗한 빌드 | 같음 | `03-build-debug.txt` |
| 릴리스 `ctest` | `100% tests passed, 0 tests failed out of 35`, 83.77 sec | `04-ctest-release.txt` |
| 디버그 `ctest` | `100% tests passed, 0 tests failed out of 35`, 109.69 sec | `05-ctest-debug.txt` |
| 실행 파일마다 합 | 릴리스 1184 통과·0 실패·21 건너뜀, 디버그 1183 통과·0 실패·22 건너뜀(407 시험 하나) | `14-totals-per-exe.txt` |
| `tst_qt_http_client` | 시험 함수 33(관문 22 + 11), 릴리스 35 통과, 디버그 34 통과·1 건너뜀 | `14-tst_qt_http_client-functions.txt`, `14-totals-per-exe.txt` |
| 되풀이 20번 `tst_qt_http_client` | 릴리스 20/20, 18.83~19.11초, 379.22초; 디버그 20/20, 19.02~19.23초, 382.96초 | `08-loop-*-tst_qt_http_client.txt` |
| 되풀이 20번 `tst_lrclib_lyrics_provider` | 릴리스 20/20, 25.24~25.47초, 506.65초; 디버그 20/20, 25.62~25.90초, 514.68초 | `08-loop-*-tst_lrclib_lyrics_provider.txt` |
| 되풀이 20번 `tst_real_lrclib`(`ctest`로, 변수 없이 모두 건너뜀) | 두 빌드 20/20 | `08-loop-*-tst_real_lrclib.txt` |
| `test-names.ps1` | `PASSED: every Kotlin test name is a Qt test name in 25 classes; only the 46 intended names were added` | `06-test-names.txt` |
| `forbidden-calls.ps1` | `PASSED: 0 hits` | `07-forbidden-calls.txt` |
| `run.ps1 -Kinds W -PerKind 10000 -Seed 20261115` | `W: 10000 cases, 0 mismatches`, PASSED. 새 통계 `lf: a request started the cool-down: 190 of 1090 = 17.4% (needs 10%)`, 규칙과 둘째 호출이 어긋난 사례 0. `lf`의 다른 통계는 위 관문 표의 seed 20261115 열과 같다 | `09-diff-W-10000-seed20261115.txt` |
| 같은 실행 `-DebugTool` | 0 mismatches, `debug tool 10000 lines, 0 differences from the release tool` | `10-diff-W-10000-seed20261115-debugtool.txt` |

첫 검증(코드 커밋 `cad473f`)은 디버그 `ctest`에서 `tst_qt_http_client`가 `0xc0000409`로 멈춰 실패했다(Qt 디버그 판의 407 단언, 23번). 407 시험을 디버그에서 건너뛰게 한 `cac544b`로 위 표 전체를 다시 돌렸고, 첫 검증의 출력은 고침 폴더의 `superseded-cad473f\`에 두었다(그때 `ctest` 단계는 PATH에 `ctest`가 없어 한 번 아무것도 돌지 않았고, 그 사실도 그 폴더의 `00-timeline.txt`에 적었다).

**다시 돌리지 않은 것.** 실제 서비스 통합 시험(`tst_real_lrclib`, `tst_real_ytdlp`, `tst_real_ffmpeg_tagging`을 `XGS_INTEGRATION=1`로)은 돌리지 않았다. lrclib.net 요청 수는 30개 그대로다. 대조는 `W` 한 종류의 seed 20261115만 돌렸다(공급자 코드는 바뀌지 않았고 `run.ps1`의 통계만 바뀌었으므로 seed 1·987654321과 10만 건, 다른 종류는 다시 돌리지 않았다). 다른 실행 파일의 되풀이 실행(`tst_http_fetch`, `tst_system_process_runner`)도 다시 하지 않았다(코드가 바뀌지 않음, 두 `ctest`에서는 통과).

## 라이선스 메모

이 절은 과정의 기록이고 법적 의견이 아니다. 동작은 Kotlin 원본과 테스트, 공개 문서(RFC 3986, Qt 문서, `URLEncoder` 공개 설명), 그리고 오라클·탐침이 공개 API로 관찰한 답으로 정했다(위 관찰 표). Task 4의 보고서는 OpenJDK와 `kotlinx.serialization`의 소스를 보지 않았다고 적었다. Task 6도 보지 않았다. 금지 호출 점검의 JDK 내부 이름 검사는 관문에서 0건이다. `THIRD-PARTY-NOTICES.md`는 바꾸지 않았다: 그 파일은 Qt의 고지를 "앱을 포장해 배포할 때" 더한다고 적고 있고 지금 Qt 모듈은 하나도 적혀 있지 않으므로, Qt Network가 더해져 지금 적을 것은 없다(포장 단계에서 Qt 전체의 고지와 함께 적는다).

## 판정

**Q2c의 관문은 충족됐다.** 이식 대상 Kotlin 단위 시험 62개와 통합 시험 4개는 같은 이름으로 통과하고(이름 대조에서 빠진 것 0, 25개 클래스), 깨끗한 릴리스·디버그 빌드에서 두 `ctest` 35개가 모두 통과했으며(1173 통과·0 실패), 금지 호출 0건, 대조는 다섯 번의 실행에서 26개 종류 모두 불일치 0과 통계 문턱 통과, 디버그 도구는 릴리스와 같았고, 되풀이 실행 160번이 모두 통과했고, 실제 통합 시험은 ffmpeg 8개, LRCLIB 5개, yt-dlp 5개가 통과했다. 관문 사이에는 C++을 고치지 않았다. 관문 뒤 최종 리뷰 반영에서 C++을 고쳤고(HTTP 클라이언트의 수신 객체·닫는 중 확인·멈춘 응답 끝내기와 시험), 그 코드로 빌드·`ctest`·되풀이 실행·두 스크립트·`W` 대조를 다시 돌렸다(위 "최종 리뷰 반영"). 실제 서비스 통합 시험은 다시 돌리지 않았다.

이 진술이 다루지 않는 것: HTTPS 위의 리다이렉트·시간 제한(루프백 `http`로만 봄), 거절한 `https`→`http` 리다이렉트, 모르는 호스트와 그 밖의 실제 네트워크 오류 길, 프록시 환경, Windows 10, 실제 yt-dlp.exe 내려받기, `yt-dlp -U`, 오디오를 내려받는 통합 시험과 Q2d로 미룬 통합 시험, 디버그 빌드의 통합 시험, 재현되지 않은 Q2b의 실행기 실패 한 번의 원인. 사용자가 정할 것은 위에 모았다.

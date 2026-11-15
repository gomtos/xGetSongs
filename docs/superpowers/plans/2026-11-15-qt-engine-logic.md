# Qt 전환 Q2a: `engine`의 순수 로직 이식 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Kotlin `engine` 모듈 가운데 프로세스도 네트워크도 쓰지 않는 로직(오류 분류, 진행률 파싱, 영상 정보 파일 읽기, yt-dlp·ffmpeg 명령 조립, 태그 파일 만들기, 동시 다운로드 수, 가사 후보 매칭)과 그것이 쓰는 데이터 형을 C++/Qt Core로 옮기고, 지금 테스트 157개를 같은 이름으로 통과시키며, 무작위 입력으로 Kotlin과 결과가 다르지 않음을 보인다.

**Architecture:** Q1의 방식을 그대로 잇는다. 로직은 `xgscore`(Qt Core만)에 두고, Kotlin/JVM과 답이 달라질 수 있는 기반 동작(코드 포인트 단위 글자 판정, NFKC 정규화, 실수 파싱, JSON 읽기)은 먼저 `xgs::kt`와 새 `xgs::json`에 만들어 대조로 고정한 뒤 이식 코드가 그것만 쓰게 한다. 정규식은 쓰지 않고, Kotlin 정규식 여덟 개는 손으로 쓴 스캐너로 옮겨 대조로 확인한다. 정답은 지금 Kotlin 구현이고 `qt/tools/differential`이 판정한다.

**Tech Stack:** C++20, Qt 6.11.3 Core·Test 정적 링크, MSVC, CMake + Ninja, vcpkg(`VCPKG_ROOT`), PowerShell 7, 대조용 JVM과 `engine`·`kotlinx-serialization` jar는 `server/build/sidecar-image`의 것.

**Spec:** [설계 문서](../specs/2026-11-15-qt-rewrite-and-playlists-design.md) §3, §4(Q2 행). 앞 단계: [Q1 보고서](../specs/2026-11-15-qt-shared-port-report.md) "Q2에 넘길 것", [Q0 보고서](../specs/2026-11-15-qt-spike-report.md) "이어질 단계에서 조심할 것".

## Q2를 넷으로 나눈다 (작성자 판단)

설계의 Q2(`engine`의 프로세스, yt-dlp, 태그, LRCLIB, 작업과 동시성, 취소)는 한 계획으로 하기에 크고 성격이 다른 네 덩어리다. 각각 따로 시험할 수 있는 결과를 낸다.

| 계획 | 내용 | 관문 |
|---|---|---|
| **Q2a (이 문서)** | 순수 로직과 데이터 형 | 테스트 157개 같은 이름으로 통과, 대조 불일치 0 |
| Q2b | 프로세스 실행(Job Object), 도구 찾기·설치, yt-dlp 조회, m4a 태그 쓰기, 출력 폴더 | 이식 테스트 통과, 실제 yt-dlp·ffmpeg 통합 테스트 통과 |
| Q2c | LRCLIB 조회(네트워크), 쉬는 시간 규칙 | 이식 테스트 통과, 실제 LRCLIB 통합 테스트 통과 |
| Q2d | 곡 내려받기, 작업·동시성·취소, 진행 이벤트 | 이식 테스트 통과, 취소 시 ffmpeg 자손까지 5초 안에 정리 |

구글 가사(`engine/lyrics/google`)는 설계대로 Q3이다.

## Global Constraints

- 정답은 Kotlin이다. `shared/`, `engine/`, `server/`, `diagnostics/`, `shell/`, `sidecar-host/`는 읽기만 한다. C++ 결과가 Kotlin과 다르면 C++을 고친다. Kotlin 쪽이 버그로 보여도 고치지 않고 보고서에 적는다. 예외는 아래 "시그마" 한 가지뿐이다.
- **JDK(OpenJDK) 소스를 보지 않고, 기억에서 옮겨 쓰지 않는다.** 동작은 공개 문서와 대조 도구가 관찰한 JVM의 답으로만 정한다. Kotlin 표준 라이브러리(Apache-2.0)의 절차를 따라 쓴 코드는 `THIRD-PARTY-NOTICES.md`에 적는다. `kotlinx.serialization`(Apache-2.0)의 소스도 옮겨 쓰지 않는다: `xgs::json`은 관찰한 답으로 만든다.
- 바꾸는 곳은 `qt/`(단 `qt/spike/` 제외), `THIRD-PARTY-NOTICES.md`, Task 8의 보고서뿐이다.
- `xgscore`는 Qt Core만 쓴다. 이 계획의 코드는 프로세스를 띄우지 않고 네트워크를 쓰지 않는다. 파일은 Task 5의 `VideoInfoFile`만 읽는다. 네임스페이스 `xgs`, C++20, `/W4 /WX /permissive- /utf-8`.
- 이식 코드(`qt/core` 중 `qt/core/text/kt_text.*` 밖)는 Q1의 금지 목록에 더해 `QRegularExpression`, `QJsonDocument`·`QJsonObject`·`QJsonValue`·`QJsonArray`, `toDouble(`, `toInt(`, `toLongLong(`, `QString::number(` 중 실수 형식, `normalized(`, `QDir::`·`QFileInfo::`의 경로 정리 함수(`cleanPath`, `absoluteFilePath`, `canonical*`)를 직접 쓰지 않는다. 같은 일은 `xgs::kt`·`xgs::json`의 함수로 한다. `qt/tools/forbidden-calls.ps1`에 이 이름들을 더한다(Task 1).
- 대소문자 무시는 `kt::CaseMode`로 한다. 길이와 인덱스는 UTF-16 단위다(코드 포인트로 도는 곳은 Kotlin이 그렇게 하는 곳뿐이고 그 자리에 주석을 단다). 결과는 로캘에 기대지 않는다.
- **경로.** Kotlin은 `java.nio.file.Path`를 쓰고 C++은 `QString`을 쓴다. `Path`의 정리 동작(`/`를 `\`로, 겹친 구분자 등)은 옮기지 않는다. 이 계획의 함수는 받은 경로 문자열을 그대로 쓰고, 대조는 **이미 정리된 Windows 절대 경로**(드라이브 + `\`로 나뉜 부분, 끝 구분자 없음, 공백·한글·`.` 포함)만 넣는다. 호출하는 쪽(Q2b)이 경로를 그 모양으로 만들어 넘긴다는 것을 헤더에 적는다.
- **시그마 (사용자 결정, 2026-11-15).** 가사 매칭에서 그리스 문자 `σ`와 `ς`는 같은 글자로 본다. `LyricsMatcher`의 정규화(`words`)가 소문자로 바꾼 뒤 `ς`(U+03C2)를 `σ`(U+03C3)로 바꾼다. 이것은 Kotlin과 다른 **문서화된 차이**다. 그래서 매칭의 대조 사례에는 `Σ`·`σ`·`ς`를 넣지 않고(통계로 0건임을 확인), 이 동작은 C++만의 테스트로 고정한다.
- 테스트는 Qt Test. Kotlin의 `@Test` 하나가 같은 이름의 슬롯 하나가 되고 단언을 하나도 빼지 않는다. 기대값·통계 문턱·생성기를 바꿔 통과시키지 않는다.
- 대조는 종류마다 1만 건 × seed 셋과 10만 건 한 번을 돈다. 이식 Task마다 일부러 틀리게 바꾼 것(변이)이 대조에 잡히는지 본다(Task에 적은 수 이상). 잡히지 않으면 생성기를 강화한다.
- 관문은 릴리스와 디버그 두 빌드에서 `ctest`를 돈다.
- 창을 띄우지 않고 외부로 요청을 보내지 않는다. `%APPDATA%\xGetSongs`와 실행 중인 xGetSongs를 건드리지 않는다. Gradle은 돌리지 않는다(`server/build/sidecar-image`가 있다; 대조 도구의 낡은 오라클 검사가 실패하면 멈추고 알린다).
- 커밋은 `main`에 직접, 메시지는 한국어, 끝에 실행하는 세션의 Co-Authored-By 줄. push하지 않는다.

## Review Focus

테스트 157개가 다루지 않지만 실제로 만날 입력이다.

1. **yt-dlp가 쓰는 JSON은 표준 JSON이 아닐 수 있다.** Python은 `NaN`·`Infinity`를 따옴표 없이 쓴다. `info.json`의 어느 필드에든 그것이 하나 있으면 엄격한 파서는 파일 전체를 버려 앨범과 설명란(가사)을 잃는다 → Task 3의 대조 종류 `J`가 따옴표 없는 리터럴, 중복 키, 깊은 중첩, 잘못된 이스케이프, 짝 없는 서로게이트 이스케이프를 넣고, Task 5의 `V`가 그런 파일에서 Kotlin과 같은 앨범·설명·길이가 나오는지 본다.
2. **UTF-8이 아닌 바이트, BOM, 빈 파일, 아주 큰 파일**(수십 MB의 `info.json`)에서 죽지 않고 Kotlin과 같은 답(대개 "없음")을 낸다 → Task 5의 `V` 생성기와 `readsATenMegabyteInfoFileQuickly`(3초 안).
3. **한글 자모가 풀린 제목, 전각 문자, 결합 문자**가 매칭에서 Kotlin과 같게 정규화된다(NFKC; Qt와 JVM의 유니코드 판이 다르다) → Task 1의 `Z`(코드 포인트 전수)·`H`(무작위 문자열)와 Task 7의 생성기.
4. **숫자가 아닌 진행률 필드**(`NA`, 빈 문자열, `1e3`, `0x10`, `Infinity`, 앞뒤 공백)와 0·음수 전체 크기 → Task 1의 `O`와 Task 4의 `Q`.
5. **태그 값 속의 제어 문자·줄바꿈·끝의 역슬래시**가 다른 태그를 끼워 넣지 못한다 → Task 6의 `A` 생성기가 그것들을 값마다 10% 이상 넣는다.

---

## File Structure

| 파일 | 하는 일 |
|---|---|
| `qt/core/text/kt_text.h/.cpp`, `kt_tables.inc` | 더하기: 코드 포인트 `isLetterOrDigit`, `nfkc`, `toDoubleOrNull`, ASCII 공백 판정 |
| `qt/core/json/json.h/.cpp` | `xgs::json`: `kotlinx.serialization`의 `Json.parseToJsonElement`와 같은 것을 받아들이는 읽기 전용 파서 |
| `qt/core/model/models.h` | `ApiModels.kt`의 도메인 형과 `ToolPaths`, `LyricsQuery` (JSON 없음) |
| `qt/core/ytdlp/error_classifier.h/.cpp` | `ErrorClassifier.kt` |
| `qt/core/ytdlp/progress_parser.h/.cpp` | `ProgressParser.kt` |
| `qt/core/ytdlp/video_info_file.h/.cpp` | `VideoInfoFile.kt` |
| `qt/core/ytdlp/ytdlp_commands.h/.cpp`, `ffmpeg_commands.h/.cpp` | `YtDlpCommands.kt`, `FfmpegCommands.kt` |
| `qt/core/tags/track_tags.h`, `ffmetadata.h/.cpp` | `TrackTags.kt`, `Ffmetadata.kt` |
| `qt/core/job/download_concurrency.h/.cpp` | `DownloadConcurrency.kt` |
| `qt/core/lyrics/lyrics_matcher.h/.cpp` | `LyricsMatcher.kt` |
| `qt/tests/tst_*.cpp` | Kotlin 테스트 클래스마다 하나, 그리고 `tst_json.cpp`, `tst_models.cpp` |
| `qt/tools/differential/*` | 종류 `B Z H O J E Q V K A I` |
| `qt/tools/test-classes.json` | 이름 대조의 대상 목록(Kotlin 파일, 실행 파일, 허용한 추가 이름) |
| `docs/superpowers/specs/2026-11-15-qt-engine-logic-report.md` | 결과 보고서 |

**공통 명령**은 `qt/README.md`에 있다(빌드, `ctest`, `test-names.ps1`, `forbidden-calls.ps1`, `differential/run.ps1`).

**대조 종류 (이 계획이 더하는 것)**

| 종류 | 사례 | 답 | 통계 문턱 (1만 건 기준) |
|---|---|---|---|
| `B` | (표) 모든 코드 포인트 | `Character.isLetterOrDigit(int)`를 0/1 | 1,112,064개 |
| `Z` | (표) 모든 코드 포인트 | 한 코드 포인트 문자열의 NFKC | 1,112,064개 |
| `H` | `H text` | `OK hex` (NFKC) | 답이 입력과 다른 것 40% 이상, 한글 자모가 든 것 10% 이상, 결합 문자가 든 것 10% 이상 |
| `O` | `O text` | `NULL` 또는 실수의 비트 16자리 | `NULL` 30% 이상, 값 30% 이상, 무한·NaN 2% 이상 |
| `J` | `J utf16text` | `ERR` 또는 정규형 덤프 | `ERR` 20% 이상, 성공 40% 이상, 따옴표 없는 리터럴이 든 성공 5% 이상, 중복 키가 든 것 5% 이상 |
| `E` | `E line1 [line2 …]` | `종류 메시지hex` | `FATAL`·`UNAVAILABLE`·`TRANSIENT`·`OTHER` 각각 10% 이상 |
| `Q` | `Q line` | `NULL` / `P -` / `P 비트` | 셋 각각 15% 이상, 값 0과 100 각각 2% 이상 |
| `V` | `V 바이트hex` | `album description duration` | 셋 모두 없음 20% 이상, 앨범 있음 30% 이상, 설명 있음 30% 이상, 길이 있음 30% 이상, UTF-8이 아닌 것 5% 이상 |
| `K` | `K op 인자…` (op: `rp` `rv` `dl` `tag`) | `OK 인자hex…` / `ERR` | op마다 15% 이상, `ERR` 3% 이상, js 런타임 인자가 든 것 20% 이상, 커버가 있는 `tag` 40% 이상 |
| `A` | `A title artist album albumArtist track comment lyrics` | `OK hex` | 역슬래시로 이스케이프된 문자가 든 답 40% 이상, 전각 역슬래시가 든 답 5% 이상, 지워진 제어 문자가 있는 것 10% 이상 |
| `I` | `I op 인자…` (op: `tv` `av` `norm` `match` `pick`) | op별(Task 7) | Task 7에 적음 |

문턱을 못 넘으면 생성기를 고친다. 문턱을 낮추지 않는다.

---

### Task 1: 기반 — `xgs::kt` 더하기와 관문 도구 일반화

**Files:**
- Modify: `qt/core/text/kt_text.h`, `kt_text.cpp`, `kt_tables.inc`, `qt/tests/tst_kt_text.cpp`, `qt/tools/differential/{Oracle.java,main.cpp,gen.cpp,run.ps1,gen-tables.ps1}`, `qt/tools/test-names.ps1`, `qt/tools/forbidden-calls.ps1`, `qt/README.md`
- Create: `qt/tools/test-classes.json`

**Interfaces:**
- Produces (`namespace xgs::kt`):
  ```cpp
  bool isLetterOrDigit(char32_t codePoint);          // Character.isLetterOrDigit(int)
  QString nfkc(const QString& s);                    // java.text.Normalizer.normalize(s, Form.NFKC)
  std::optional<double> toDoubleOrNull(const QString& s);   // Kotlin String.toDoubleOrNull()
  bool isAsciiSpace(QChar c);                        // Java 정규식의 \s (플래그 없음): 공백, \t, \n, U+000B, \f, \r
  QString hexBits(double value);                     // 대조용: 비트 무늬 16자리 (std::bit_cast<quint64>)
  ```
- Produces: `qt/tools/test-classes.json` — 배열, 항목마다 `{ "kotlin": "<저장소 기준 경로>", "exe": "<실행 파일 이름>", "added": ["<Kotlin에 없는 허용 이름>", …] }`. `test-names.ps1`은 이 파일을 읽는다(Q1의 다섯 클래스를 옮겨 넣고, 뒤 Task가 항목을 더한다).
- Produces: `run.ps1`의 낡은 오라클 검사가 `shared/src/commonMain`과 `engine/src/main`을 함께 본다.

- [ ] **Step 1: 오라클에 표 `B`·`Z`와 종류 `H`·`O`를 더한다**

`B`: `Character.isLetterOrDigit(cp)`. `Z`·`H`: `java.text.Normalizer.normalize(s, Normalizer.Form.NFKC)`(공개 API). `O`: Kotlin `StringsKt.toDoubleOrNull(String)`의 결과를 없으면 `NULL`, 있으면 `Double.doubleToRawLongBits`를 16진수 16자리로.

- [ ] **Step 2: 단위 시험을 쓴다 (`tst_kt_text.cpp`에 더한다; 기대값은 Step 1의 오라클 답에서 읽어 적는다)**

```cpp
void codePointIsLetterOrDigitHandlesSupplementaryLetters();   // U+1D49C(𝒜), U+10400, U+1F600(😀), U+20000, U+0661, U+00B2, U+2163
void nfkcComposesHangulJamoAndFoldsFullWidth();
//   nfkc(u"가") == u"가";  nfkc(u"ＡＢＣ１２３") == u"ABC123";  nfkc(u"ﬁ") == u"fi";  nfkc(u"é") == u"é"
//   nfkc(u"①") == u"1";  nfkc(u"㈜") == u"(주)";  nfkc(u"ｶﾞ") == u"ガ"
void nfkcLeavesLoneSurrogatesAlone();                         // 입력 그대로
void toDoubleOrNullAcceptsWhatKotlinAccepts();
//   "1" "1.5" "-0" ".5" "5." "1e3" "1E-3" " 1 " "1f" "1d" "0x1p3" "NaN" "Infinity" "-Infinity" "+1" → 각각 Kotlin의 답
void toDoubleOrNullRejectsWhatKotlinRejects();
//   "" " " "NA" "1,5" "1 2" "abc" "0x10" "١" "1e" "--1" "infinity" "nan" → 각각 Kotlin의 답
void isAsciiSpaceIsExactlySixCharacters();                    // true: ' ' \t \n \x0B \f \r   false: U+00A0 U+3000 U+2028 U+0085 U+001C
```

- [ ] **Step 3: 실패를 본다** — Expected: 선언이 없어 컴파일 실패.

- [ ] **Step 4: 구현한다**

`isLetterOrDigit(char32_t)`: BMP는 기존 단위 함수와 같은 답이어야 하고(서로게이트 단위 자체는 false), BMP 밖은 Qt의 범주로 판정하되 표 `B`에서 JVM과 다른 코드 포인트는 `gen-tables.ps1`로 예외 표에 넣는다.
`nfkc`: `QString::normalized(QString::NormalizationForm_KC, <JVM 21의 유니코드 판에 해당하는 QChar::UnicodeVersion>)`에서 시작해, 표 `Z`와 종류 `H`에서 JVM과 다른 것이 0이 될 때까지 맞춘다(다른 코드 포인트는 예외 표로; 문자열 수준에서만 드러나는 차이가 있으면 그 내용과 처리 방법을 보고서에 적는다). 이 호출은 `kt_text.cpp` 안에만 있다.
`toDoubleOrNull`: 공개 문서(`java.lang.Double.valueOf(String)`의 문법)와 관찰한 답으로 받아들이는 문자열을 정하고, 값은 `std::from_chars`/`strtod` 계열로 구하되 로캘에 기대지 않게 한다. 16진수 실수와 `f`/`d` 접미사, 앞뒤 공백 처리는 종류 `O`가 판정한다.

- [ ] **Step 5: C++ 쪽 `B`·`Z`·`H`·`O`와 생성기**

`H`의 생성기: Q1의 `token()`에 한글 자모(초·중·종성, 호환 자모), 전각·반각 문자, 결합 문자(U+0301, U+0308, U+3099, U+309A), 합자·원문자·분수, 아랍·히브리 표현형, 조합 순서가 뒤섞인 결합 문자열을 더한다. `O`의 생성기: 유효한 십진수·지수·16진수 실수·특수 값과, 그것을 한 글자 망가뜨린 것, 앞뒤 공백(ASCII와 그 밖), 다른 문자 체계의 숫자, 아주 긴 숫자열, 빈 문자열.

- [ ] **Step 6: 관문 도구를 일반화한다**

`test-classes.json`을 만들고 `test-names.ps1`이 그것을 읽게 한다(동작은 그대로: 빠진 이름 0, 추가 이름은 `added`에 적힌 것만, Kotlin 이름이 0개인 클래스는 실패). `forbidden-calls.ps1`에 Global Constraints의 새 금지 이름을 더한다(`kt_text.*`와 `qt/core/json/`은 각자 자기 일에 필요한 것만 예외: `kt_text.cpp`의 `normalized(`, `qt/core/json`에는 예외 없음).

- [ ] **Step 7: 통과를 본다**

Run: 빌드, `ctest`, `pwsh qt/tools/test-names.ps1`, `pwsh qt/tools/forbidden-calls.ps1`, `pwsh qt/tools/differential/run.ps1 -Kinds BZHO`, 그리고 `-Kinds HO -PerKind 100000`
Expected: 모두 통과, `B`·`Z` 각각 1,112,064개 0 불일치, `H`·`O` 0 불일치. 예외 표에 더한 코드 포인트 수를 README에 적는다.

- [ ] **Step 8: 커밋** — `Qt: 코드 포인트 글자 판정, NFKC, 실수 파싱과 관문 도구 일반화`

### Task 2: 데이터 형

**Files:**
- Create: `qt/core/model/models.h`, `qt/tests/tst_models.cpp`
- Modify: `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`
- 원본(읽기만): `shared/src/commonMain/kotlin/com/xgetsongs/shared/api/ApiModels.kt`, `engine/.../tools/ToolLocator.kt`(`ToolPaths`), `engine/.../lyrics/LyricsProvider.kt`(`LyricsQuery`)

**Interfaces:**
- Produces (`namespace xgs`, 모두 `bool operator==(const T&) const = default`가 되는 평범한 구조체; 필드 이름과 기본값은 Kotlin과 같다; Kotlin의 `String?`는 `std::optional<QString>`, `Int?`는 `std::optional<int>`, `Double?`는 `std::optional<double>`, `List<T>`는 `QList<T>`):
  ```cpp
  enum class InputKind { Playlist, Video };
  struct ResolvedItem;      // rank, videoId, title, channel?, artist="", track="", lowConfidence=false, available=true, unavailableReason?, expectedFileName?
  struct ResolveResult;     // Kotlin ResolveResponse: resolveId="", kind, playlistTitle?, items, truncated=false, alsoVideoId?
  struct JobOptions;        // outputDir?, overwrite=false, singleRank=1, includeRank=true, searchLyricsOnline=true, albumName?
  enum class Stage { Downloading, Finishing };
  enum class JobStatus { Completed, Cancelled, Failed };
  struct JobSummary;        // succeeded, skipped, failed
  enum class LyricsOutcome; // Kotlin의 값 그대로, 같은 순서
  struct ItemStarted; struct Progress; struct ItemDone; struct ItemSkipped; struct ItemFailed; struct JobDone;
  using JobEvent = std::variant<ItemStarted, Progress, ItemDone, ItemSkipped, ItemFailed, JobDone>;
  struct ToolInfo;          // found, version?, path?
  struct ToolsStatus;       // ytDlp, ffmpeg, jsRuntime
  struct ToolPaths;         // ytDlp?, ffmpeg?, jsRuntime? (QString 경로)
  struct LyricsQuery;       // artist, title, album?, durationSeconds?
  ```
  `ResolveRequest`, `JobRequest`, `JobCreated`, `ActionResult`, `ErrorResponse`, `ApiJson`, `ApiHeaders`, `sseName`, `BrowserCommand`, `BrowserAnswer`는 만들지 않는다(HTTP 전송과 폴링 채널이 없어진다).

- [ ] **Step 1: 시험을 쓴다** (`tst_models.cpp`, 클래스 `ModelsTest`; Kotlin에 없는 클래스라 이름 대조 대상이 아니다)

```cpp
void defaultsMatchTheKotlinDataClasses();
// JobOptions{} : overwrite false, singleRank 1, includeRank true, searchLyricsOnline true, outputDir·albumName 없음
// ResolvedItem{.rank=1,.videoId=u"v",.title=u"t"} : artist "" , track "", lowConfidence false, available true, 나머지 없음
// ResolveResult{.kind=InputKind::Video} : resolveId "", truncated false, items 비어 있음
void lyricsOutcomeHasTheKotlinValuesInOrder();     // 값의 수와 순서를 ApiModels.kt와 맞춘다
void jobEventHoldsEachAlternative();               // std::holds_alternative를 여섯 형 각각에 대해
```
- [ ] **Step 2: 실패를 본다** → **Step 3: `models.h`를 쓴다** → **Step 4: 통과를 본다** (`ctest`)
- [ ] **Step 5: 커밋** — `Qt: engine이 쓰는 데이터 형`

### Task 3: `xgs::json` — Kotlin이 받아들이는 JSON을 똑같이 받아들이는 파서

**Files:**
- Create: `qt/core/json/json.h`, `qt/core/json/json.cpp`, `qt/tests/tst_json.cpp`
- Modify: `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/tools/differential/{Oracle.java,main.cpp,gen.cpp,run.ps1}`

**Interfaces:**
- Produces:
  ```cpp
  namespace xgs::json {
  struct Value;
  using Object = std::vector<std::pair<QString, Value>>;   // 키의 순서와 중복 처리는 Kotlin(JsonObject)과 같게
  using Array = std::vector<Value>;
  struct Primitive { bool isString; QString content; };     // kotlinx JsonPrimitive: isString, content
  struct Null {};                                            // JsonNull
  struct Value { std::variant<Null, Primitive, Object, Array> data; };
  /// kotlinx.serialization의 기본 `Json.parseToJsonElement(text)`가 예외를 던지는 입력이면 없음.
  std::optional<Value> parse(const QString& text);
  const Value* find(const Object& object, QStringView key);  // 없으면 nullptr
  QString dump(const Value& value);                          // 대조용 정규형 (아래)
  }
  ```
- 정규형 덤프(양쪽이 같은 형식으로 쓴다): `N`(null), `S<hex>`(문자열 원시값), `L<hex>`(문자열이 아닌 원시값의 `content`), `O{<키hex>:<값>,…}`, `A[<값>,…]`. 빈 문자열의 hex는 `.`.

- [ ] **Step 1: 오라클에 종류 `J`를 더한다**

`Json.Default.parseToJsonElement(text)`를 부르고, `JsonNull`·`JsonPrimitive`(`isString`, `content`)·`JsonObject`(`entries`의 순서)·`JsonArray`를 정규형으로 덤프한다. 어떤 예외든 `ERR`.

- [ ] **Step 2: 관찰한다**

구현 전에 손으로 쓴 사례 60개 이상을 오라클에 넣어 답을 적는다(보고서에 표로 남긴다). 적어도 다음을 가른다: 따옴표 없는 값(`NaN`, `Infinity`, `-Infinity`, `abc`, `1e`, `01`, `+1`, `.5`, `1.`, `0x10`, `true`/`TRUE`/`null`/`NULL`), 따옴표 없는 키, 작은따옴표, 끝의 쉼표, 겹친 쉼표, 주석, 빈 입력, 공백만, 값 뒤의 쓰레기, 최상위가 원시값인 것, 중복 키(어느 값이 남고 순서는 어떻게 되는가), 문자열 속 이스케이프(`\u0000`, `\uD83D` 단독, `😀`, `\x`, `\'`, 대문자 `\U`), 문자열 속 생 제어 문자·줄바꿈·탭, 닫히지 않은 문자열·객체·배열, 아주 깊은 중첩(1000, 10000 단계), BOM으로 시작하는 것, 숫자의 범위 밖 값(`1e999`), 공백으로 치는 문자(U+00A0, U+2028 등).

- [ ] **Step 3: 단위 시험을 쓴다** (`tst_json.cpp`, 클래스 `JsonTest`; 기대값은 Step 2의 관찰에서 가져온다)

```cpp
void parsesAnObjectWithEveryKindOfValue();
void keepsTheContentOfUnquotedLiteralsAsWritten();    // NaN, Infinity, 1e3, true 가 L<…>로
void duplicateKeysBehaveLikeKotlin();
void stringEscapesBehaveLikeKotlin();
void rejectsWhatKotlinRejects_data(); void rejectsWhatKotlinRejects();          // Step 2에서 ERR이었던 것 전부를 행으로
void acceptsWhatKotlinAccepts_data(); void acceptsWhatKotlinAccepts();          // Step 2에서 성공한 것 전부를 행으로, 덤프까지 비교
void deepNestingBehavesLikeKotlinAndDoesNotOverflowTheStack();                 // 관찰한 깊이 한계 그대로; 재귀로 스택이 넘치지 않는다
void parsesATenMegabyteDocumentQuickly();             // 10MB 문서를 NDEBUG에서 3000ms 안
```

- [ ] **Step 4: 실패를 본다 → Step 5: 구현한다**

재귀 대신 명시적 스택을 쓰거나 깊이를 세어 스택이 넘치지 않게 한다. 어떤 입력을 받아들이는지는 Step 2의 관찰과 종류 `J`가 정한다. `kotlinx.serialization`의 소스를 보지 않는다.

- [ ] **Step 6: 생성기 `J`와 통계**

문법에서 문서를 만들고(객체·배열·문자열·수·리터럴, 깊이 0~6, 가끔 60) 30%는 한 군데를 망가뜨린다(문자 삭제·삽입·교체, 따옴표 제거, 쉼표 추가, 자르기). Step 2의 모든 범주가 나오게 한다. 문자열에는 Q1의 `token()`과 이스케이프를 섞는다.

- [ ] **Step 7: 통과와 변이**

Run: 빌드, `ctest`, `run.ps1 -Kinds J`, `-Kinds J -PerKind 100000`(seed 둘).
Expected: 모두 0 불일치와 통계 문턱 통과. 변이 8개 이상(예: 중복 키에서 처음 값을 남김, `NaN` 거절, 끝 쉼표 허용, `\u` 대문자 허용, 값 뒤 쓰레기 허용, 깊이 한계 다르게, 생 줄바꿈 허용/거절 반대로, 따옴표 없는 키 반대로)이 모두 잡힌다.

- [ ] **Step 8: 커밋** — `Qt: Kotlin과 같은 것을 받아들이는 JSON 파서`

### Task 4: 오류 분류, 진행률 파싱, 동시 다운로드 수

**Files:**
- Create: `qt/core/ytdlp/error_classifier.h/.cpp`, `qt/core/ytdlp/progress_parser.h/.cpp`, `qt/core/job/download_concurrency.h/.cpp`, `qt/tests/tst_error_classifier.cpp`, `tst_progress_parser.cpp`, `tst_download_concurrency.cpp`
- Modify: CMake 둘, `qt/tools/test-classes.json`, `qt/tools/differential/*`
- 원본(읽기만): `engine/.../ytdlp/ErrorClassifier.kt`, `ProgressParser.kt`, `engine/.../job/DownloadConcurrency.kt`와 그 테스트 셋(13 + 7 + 3개)

**Interfaces:**
- Produces:
  ```cpp
  namespace xgs::ytdlp {
  enum class FailureKind { Unavailable, Transient, Fatal, Other };
  struct Failure { FailureKind kind; QString message; bool operator==(const Failure&) const = default; };
  Failure classifyError(const QStringList& stderrLines);
  inline constexpr QStringView ProgressPrefix = u"XGSP";
  struct DownloadProgress { std::optional<double> percent; };     // Kotlin ProgressUpdate.Downloading
  std::optional<DownloadProgress> parseProgress(const QString& line);
  }
  namespace xgs::job {
  int concurrencyForCores(int cores);       // (cores * 7 / 10), 최소 1
  int automaticConcurrency();               // QThread::idealThreadCount()로; 0 이하가 오면 1
  }
  ```

- [ ] **Step 1: 테스트 23개를 옮긴다** (클래스 `ErrorClassifierTest`, `ProgressParserTest`, `DownloadConcurrencyTest`; `test-classes.json`에 세 항목)
- [ ] **Step 2: 실패를 본다**
- [ ] **Step 3: 구현한다**

`classifyError`: 줄을 `\n`으로 이어 `kt::lowercase`한 글에서 `kt::contains`(대소문자 구분)로 찾는다. 순서는 Kotlin 그대로(FATAL 목록 → 속도 제한 → UNAVAILABLE 목록 → TRANSIENT 목록 → `requested format is not available` → 그 밖). `summarize`: 뒤에서부터 `trimStart` 뒤 `ERROR:`로 시작하는 줄 → 그 줄을 `trim`, 접두어 제거, `trim`; 없으면 뒤에서부터 공백뿐이 아닌 줄의 `trim`; 없으면 `yt-dlp가 비정상 종료했습니다.`; 앞 300 UTF-16 단위만(`take(300)`: 서로게이트 쌍을 가르는지는 Kotlin 그대로).
`parseProgress`: `trim` 뒤 `XGSP|`로 시작해야 하고 `|`로 나눈다(빈 조각 유지). `finished` → 100. `downloading` → 내려받은 양과 전체(없으면 추정치)를 `kt::toDoubleOrNull`로; 둘 다 있고 전체가 0보다 크면 `downloaded * 100.0 / total`을 0~100으로 자른다(곱셈 먼저). NaN이 섞일 때의 `coerceIn`은 Kotlin의 답을 따른다. 그 밖의 상태 → 없음.

- [ ] **Step 4: 대조 종류 `E`·`Q`**

`E`의 생성기: 줄 0~6개. 각 목록의 문구를 대소문자를 섞어 문장 속에 넣고(여러 목록의 문구가 한 사례에 같이 나오게: 순서가 드러난다), `ERROR:` 줄(앞 공백, 여러 개, 빈 것), 공백뿐인 줄, 300 단위를 넘는 줄(자르는 자리에 이모지), 빈 목록. `İ`·`ſ`·`K`(U+212A) 같은 대소문자 함정 글자를 문구 안에 넣는다.
`Q`의 생성기: `XGSP|상태|a|b|c`에서 상태(`finished`, `downloading`, 다른 것, 대소문자 다른 것), 필드 수 1~7, 필드 값은 Task 1의 `O` 생성기, 앞뒤 공백, 접두어가 다른 것, `|`가 없는 것. 답: `NULL` / `P -` / `P <비트16자리>`.

- [ ] **Step 5: 통과와 변이**

Run: 빌드, `ctest`, `test-names.ps1`, `run.ps1 -Kinds EQ`와 10만 건.
Expected: 23개 통과, 0 불일치. 변이 6개 이상(목록 순서 바꾸기, 속도 제한 검사를 UNAVAILABLE 뒤로, `take(300)`을 301로, 추정치 대체 빼기, 나눗셈 먼저, `total > 0`을 `>= 0`으로)이 잡힌다.

- [ ] **Step 6: 커밋** — `Qt: 오류 분류·진행률 파싱·동시 다운로드 수 이식`

### Task 5: 영상 정보 파일

**Files:**
- Create: `qt/core/ytdlp/video_info_file.h/.cpp`, `qt/tests/tst_video_info_file.cpp`
- Modify: CMake 둘, `test-classes.json`, `qt/tools/differential/*`
- 원본(읽기만): `engine/.../ytdlp/VideoInfoFile.kt`, `VideoInfoFileTest.kt`(40개)

**Interfaces:**
- Consumes: `xgs::json`(Task 3), `xgs::kt`.
- Produces:
  ```cpp
  namespace xgs::ytdlp {
  struct VideoInfo { std::optional<QString> album; std::optional<QString> description; std::optional<int> duration;
                     bool operator==(const VideoInfo&) const = default; };
  VideoInfo readVideoInfo(const QString& filePath);          // 절대 던지지 않는다
  VideoInfo parseVideoInfo(const QByteArray& fileBytes);     // 파일 내용에서 (대조와 시험이 쓴다)
  std::optional<QString> readVideoAlbum(const QString& filePath);
  }
  ```

- [ ] **Step 1: 테스트 40개를 옮기고 하나를 더한다** (클래스 `VideoInfoFileTest`; Kotlin의 임시 파일은 `QTemporaryDir`)

```cpp
void readsATenMegabyteInfoFileQuickly();
// {"formats":[…큰 배열…],"album":"A","description":"가사\n…","duration":200} 꼴 10MB 파일. NDEBUG에서 3000ms 안,
// album "A", duration 200.  (test-classes.json의 added에 넣는다)
```
- [ ] **Step 2: 실패를 본다**
- [ ] **Step 3: 구현한다**

바이트를 **엄격한 UTF-8**로 푼다: 잘못된 바이트 열이 있으면 전부 없음(대체 문자를 넣지 않는다). BOM을 Kotlin(`Files.readString`)이 어떻게 다루는지는 종류 `V`가 정한다(Qt의 디코더가 BOM을 떼는지 확인하고 Kotlin과 맞춘다). 그다음 `json::parse`; 실패하거나 최상위가 객체가 아니면 전부 없음. `album`: 문자열 원시값일 때만 → `kt::trim` → 비었거나 `NA`면 없음. `description`: 문자열 원시값일 때만 → 첫 비공백 글자가 있는 줄의 시작부터(그 앞의 마지막 `\n`이나 `\r` 다음) → `kt::trimEnd` → 비면 없음. `duration`: 원시값이고 문자열이 아닐 때 `content`를 `kt::toDoubleOrNull` → 유한해야 하고 → Kotlin `roundToLong`과 같은 반올림(관찰로 정한다: 0.5, 1.5, 2.5, -0.5, 큰 값) → 1 이상 `Int.MAX_VALUE` 이하일 때만.

- [ ] **Step 4: 대조 종류 `V`**

오라클: 사례의 바이트를 임시 파일에 써서 `VideoInfoFile.INSTANCE.read(path)`를 부른다(끝나면 지운다). 답: `album description duration`(문자열은 hex, 없으면 `-`, 길이는 십진수나 `-`).
생성기: Task 3의 문서 생성기로 최상위 객체를 만들고 `album`·`description`·`duration` 키를 여러 모양으로 넣는다 — 문자열, 공백뿐, `NA`, ` NA `, 숫자, null, 불리언, 객체, 배열, 따옴표 없는 리터럴, 중복 키, 중첩 객체 안의 같은 이름 키; `description`은 앞에 빈 줄과 들여쓰기, 뒤에 공백, `\r\n`; `duration`은 정수·소수·`.5` 경계·음수·0·`1e3`·`2147483647.5`·`NaN`·문자열 `"258"`. 그것을 UTF-8로 쓰되 5% 이상은 잘못된 바이트를 넣고, BOM을 앞에 붙인 것, UTF-16으로 쓴 것, 빈 파일을 섞는다.

- [ ] **Step 5: 통과와 변이**

Run: 빌드, `ctest`, `test-names.ps1`, `run.ps1 -Kinds V`와 10만 건(seed 둘).
Expected: 41개 통과, 0 불일치. 변이 8개 이상(중첩 객체의 키도 읽기, 문자열 `"258"` 받기, `NA` 검사 전에 trim 안 하기, 설명 앞 들여쓰기 떼기, 반올림을 버림으로, 잘못된 UTF-8에 대체 문자, 0 받기, 불리언을 수로)이 잡힌다.

- [ ] **Step 6: 커밋** — `Qt: 영상 정보 파일 읽기 이식`

### Task 6: 명령 조립과 태그 파일

**Files:**
- Create: `qt/core/ytdlp/ytdlp_commands.h/.cpp`, `qt/core/ytdlp/ffmpeg_commands.h/.cpp`, `qt/core/tags/track_tags.h`, `qt/core/tags/ffmetadata.h/.cpp`, `qt/tests/tst_ytdlp_commands.cpp`, `tst_ffmpeg_commands.cpp`, `tst_ffmetadata.cpp`
- Modify: CMake 둘, `test-classes.json`, `qt/tools/differential/*`
- 원본(읽기만): `engine/.../ytdlp/YtDlpCommands.kt`, `FfmpegCommands.kt`, `engine/.../tags/Ffmetadata.kt`, `TrackTags.kt`와 테스트 셋(10 + 4 + 19개)

**Interfaces:**
- Consumes: `ToolPaths`(Task 2).
- Produces:
  ```cpp
  namespace xgs::ytdlp {
  /// 모두 인자 목록을 돌려준다(셸 문자열이 아니다). URL은 항상 `--` 뒤 마지막. tools.ytDlp가 없으면 std::invalid_argument.
  QStringList resolvePlaylistCommand(const ToolPaths& tools, const QString& url);
  QStringList resolveVideoCommand(const ToolPaths& tools, const QString& url);
  QStringList downloadCommand(const ToolPaths& tools, const QString& url, const QString& outputDir, const QString& videoId);
  /// output이 `.m4a`(대소문자 무시)로 끝나지 않으면 std::invalid_argument.
  QStringList tagCommand(const QString& ffmpeg, const QString& input, const std::optional<QString>& cover,
                         const QString& metadataFile, const QString& output);
  }
  namespace xgs::tags {
  struct TrackTags { QString title; QString artist; std::optional<QString> album; QString albumArtist; int trackNumber = 0;
                     std::optional<QString> comment; std::optional<QString> lyrics; };
  QString renderFfmetadata(const TrackTags& tags);     // UTF-8로 쓸 글
  }
  ```
- 경로는 Global Constraints의 규칙대로 받은 문자열을 그대로 쓴다. `downloadCommand`의 `-o` 값은 `outputDir + '\\' + videoId + ".%(ext)s"`. js 런타임 이름은 경로의 마지막 `\` 뒤 부분을 `kt::lowercase`하고 끝의 `.exe`를 뗀 것이 `deno`나 `node`일 때만 `--js-runtimes`, `<이름>:<경로>`.

- [ ] **Step 1: 테스트 33개를 옮긴다** (클래스 `YtDlpCommandsTest`, `FfmpegCommandsTest`, `FfmetadataTest`; Kotlin의 `Path.of(...)`는 같은 글자의 `QString`; `assertFailsWith`는 `QVERIFY_THROWS_EXCEPTION(std::invalid_argument, …)`)
- [ ] **Step 2: 실패를 본다 → Step 3: 구현한다**

`renderFfmetadata`: `;FFMETADATA1\n`, 이어서 `title`, `artist`, `album_artist`, (`album`: 공백뿐이 아닐 때만), `track`(십진수), (`comment`), (`lyrics`). 값은 먼저 U+0020 미만 가운데 탭·LF·CR이 아닌 것을 지우고, 끝의 `\` 연속을 같은 수의 `＼`(U+FF3C)로 바꾼 뒤, `=`, `;`, `#`, `\`, LF, CR 앞에 `\`를 붙인다. 각 항목 끝에 `\n`.

- [ ] **Step 4: 대조 종류 `K`·`A`**

오라클 `K`: op `rp`(`resolvePlaylist`), `rv`(`resolveVideo`), `dl`(`download`), `tag`(`FfmpegCommands.tag`). 경로 인자는 `Path.of(text)`로 만든다. 답은 `OK` 뒤에 인자마다 hex, 예외면 `ERR`.
생성기 `K`: 경로는 정리된 Windows 절대 경로만(드라이브 글자 + `\` + 부분 1~4개: ASCII·한글·공백·`.`·`(`·`)`·`&`·`%`, 끝 공백과 끝 점은 넣지 않는다). 도구 경로는 없음을 섞고(`ytDlp` 없음 → `ERR`), js 런타임 파일 이름은 `node.exe`·`NODE.EXE`·`deno`·`Deno.exe`·`bun.exe`·`node.cmd`·`nodejs.exe`. URL과 videoId는 `text()`. `tag`의 출력은 `.m4a`·`.M4A`·`.mp3`·확장자 없음.
생성기 `A`: 각 값에 `text()`와 함께 `=`·`;`·`#`·`\`·LF·CR·탭·NUL·U+001B·U+007F·끝의 `\` 1~3개를 넣고, `album`·`comment`·`lyrics`는 `nullable()`, `track`은 -5~1500.

- [ ] **Step 5: 통과와 변이**

Run: 빌드, `ctest`, `test-names.ps1`, `run.ps1 -Kinds KA`와 10만 건.
Expected: 33개 통과, 0 불일치. 변이 8개 이상(공통 인자 순서, `--` 빼기, js 런타임을 `resolvePlaylist`에도 넣기, `.exe`를 대소문자 구분해 떼기, 커버가 있을 때 메타데이터 번호 1로, 끝 `\` 하나만 바꾸기, U+007F도 지우기, CR을 이스케이프하지 않기)이 잡힌다.

- [ ] **Step 6: 커밋** — `Qt: yt-dlp·ffmpeg 명령 조립과 태그 파일 이식`

### Task 7: 가사 후보 매칭

**Files:**
- Create: `qt/core/lyrics/lyrics_matcher.h/.cpp`, `qt/tests/tst_lyrics_matcher.cpp`
- Modify: CMake 둘, `test-classes.json`, `qt/tools/differential/*`
- 원본(읽기만): `engine/.../lyrics/LyricsMatcher.kt`, `LyricsMatcherTest.kt`(61개)

**Interfaces:**
- Consumes: `LyricsQuery`(Task 2), `kt::nfkc`, `kt::isLetterOrDigit(char32_t)`, `kt::isAsciiSpace`(Task 1).
- Produces:
  ```cpp
  namespace xgs::lyrics {
  struct LyricsCandidate { std::optional<QString> trackName, artistName, albumName; std::optional<double> duration;
                           std::optional<bool> instrumental; std::optional<QString> plainLyrics; };
  QStringList titleVariants(const QString& title);
  QStringList artistVariants(const QString& artist);
  QString normalizeForMatching(const QString& text);          // Kotlin LyricsMatcher.normalize; ς는 σ가 된다
  bool isMatch(const LyricsQuery& query, const LyricsCandidate& candidate);
  /// 고른 후보의 인덱스. 맞는 것이 없으면 -1. (Kotlin pick은 후보를 돌려준다)
  qsizetype pick(const LyricsQuery& query, const QList<LyricsCandidate>& candidates);
  }
  ```

- [ ] **Step 1: 테스트 61개를 옮기고 시그마 시험을 더한다** (클래스 `LyricsMatcherTest`; `pick`이 후보를 돌려주는 Kotlin 단언은 인덱스로 옮긴다)

더하는 것(`test-classes.json`의 `added`):
```cpp
void sigmaFormsAreTheSameLetterInMatching();
// normalizeForMatching(u"ΟΔΟΣ") == normalizeForMatching(u"οδος") == normalizeForMatching(u"οδοσ")
// isMatch({artist "Band", title "ΟΔΟΣ-ΑΘΗΝΑΣ"}, {trackName "οδοσ-αθηνασ", artistName "Band", plainLyrics 3줄}) == true
// (가수는 정규화 뒤 2글자 이상이어야 맞으므로 한 글자 이름을 쓰지 않는다)
// (Kotlin은 이 둘을 다르게 본다. 사용자가 2026-11-15에 같은 글자로 보기로 정했다.)
```

- [ ] **Step 2: 실패를 본다 → Step 3: 구현한다**

정규식 여덟 개를 스캐너로 옮긴다. 뜻은 Java 정규식의 기본(플래그 없음)이다: `\s`는 `kt::isAsciiSpace`, `IGNORE_CASE`는 ASCII 글자만, `[0-9]`는 ASCII 숫자.

| Kotlin | 뜻 |
|---|---|
| `BRACKETED` `\([^()]*\)\|\[[^\[\]]*\]\|\{[^{}]*\}\|<[^<>]*>` | 왼쪽부터, 겹치지 않게: 여는 문자 뒤에 같은 종류의 여닫는 문자가 없는 채로 닫는 문자가 오는 가장 안쪽 쌍. 더 없을 때까지 반복(중첩이 풀린다). 뗀 부분은 나온 순서대로 뒤에 공백을 붙여 모은다 |
| `PARENTHESISED` `\(([^()]*)\)` | 같은 방식으로 `( )`만. 안쪽 글을 `trim`해 모은다 |
| `CREDIT` `\s+(?:feat\.?\|ft\.?\|prod\.?\|narr\.?)\s+.*$` (대소문자 무시, 점이 줄바꿈도 맞음) | 가장 왼쪽에서: ASCII 공백 1개 이상, 낱말(뒤에 점 하나는 있어도 됨), ASCII 공백 1개 이상, 그리고 끝까지 |
| `WHITESPACE` `\s+` | ASCII 공백의 연속을 공백 하나로. 그 뒤의 `.trim()`은 Kotlin의 trim(`kt::trim`) |
| `VERSION_WORD` (전체 일치) | 어간(`ver`, `version`, `japanese`, `english`, `chinese`, `korean`, `inst`, `instrumental`, `remix`, `live`, `acoustic`, `cover`, `edit`, `mix`, `remaster`, `demo`) + 선택 접미사(`ed`, `es`, `s`) + ASCII 숫자 0개 이상이 낱말 전체와 맞는가. 모든 어간과 접미사 조합을 시도한다 |
| `LIVE_LIKE_WORD` (전체 일치) | 어간 `live`, `acoustic` + 선택 접미사(`ed`, `es`, `s`, `d`) + ASCII 숫자 |

`words`: `kt::nfkc` → `kt::lowercase` → **`ς`를 `σ`로** → 코드 포인트로 돌며 `kt::isLetterOrDigit(char32_t)`인 것의 연속을 낱말로. `hasVersionMarker`의 글도 같은 순서(NFKC, 소문자)이고 한국어 낱말은 `kt::contains`. 나머지(`reduce`, `titleKeys`, `titleRank`, `sameArtist`, `containsRun`, `sameAlbum`, `durationDifference`, `rank`)는 Kotlin을 줄 단위로 옮긴다. `pick`은 (제목 등급, 앨범 등급, 길이 차)가 가장 작은 **첫** 후보이고, 길이 차의 비교는 Kotlin `Double.compareTo`의 순서(NaN이 가장 크다, -0.0 < 0.0)를 따른다.

- [ ] **Step 4: 대조 종류 `I`**

op와 답: `tv title` → `OK` + 변형마다 hex; `av artist` → 같음; `norm text` → `OK hex`; `match <query 4필드> <candidate 6필드>` → `0`/`1`; `pick <query 4필드> <n> <candidate 6필드>×n` → 고른 인덱스나 `-1`(오라클은 `indexOfFirst { it === picked }`). 실수는 비트 16자리, 없음은 `-`, 불리언은 `0`/`1`/`-`.
생성기: 제목은 조각을 잇는다 — 본체(`text()`에서 `Σσς`를 뺀 것, 한글·전각·자모 풀린 한글·결합 문자 포함), 괄호 부분 0~3개(네 종류, 중첩, 닫히지 않은 것, 안에 버전 낱말(`Japanese Ver.`, `Remixes`, `Remastered 2011`, `Live`, `Lived`, `Acoustic`, `Verse`, `Discover`, `Ver2`, `inst`, 전각 `Ｌｉｖｅ`) / 한국어 버전 낱말(`일본어 버전`, `라이브`, `반주`) / 보통 글), 크레딧(없음 / ` feat. X` / ` FT X` / ` Prod.X`(공백 없음) / `\tNarr.\nX` / ` with X` / 앞 공백이 U+00A0인 것), 따옴표 열 가지. **후보의 제목은 60%를 질의 제목에서 만든다**(그대로 / 괄호 뗀 것 / 크레딧 뗀 것 / 대소문자·전각·공백만 다른 것 / 버전 괄호를 더한 것). 가수도 같다(괄호 병기, `&`로 이은 여럿, 한 글자, 낱말 경계: `IU`와 `Liu`). 앨범·길이·`instrumental`·`plainLyrics`는 `nullable`과 경계값(길이 차 7.9·8.0·8.1, NaN, 무한, 음수; 가사 공백뿐). `pick`은 후보 0~6개, 같은 등급이 여럿 나오게.
통계 문턱: op마다 15% 이상; `match` 답 `1` 20% 이상·`0` 30% 이상; `pick` 답 `-1` 15% 이상, 0이 아닌 인덱스 15% 이상; `tv` 변형 수 1·2·3·4가 각각 5% 이상; `av` 변형 3개 5% 이상; 버전 낱말이 든 사례 20% 이상; 사례에 `Σσς`가 0건.

- [ ] **Step 5: 통과와 변이**

Run: 빌드, `ctest`, `test-names.ps1`, `run.ps1 -Kinds I`와 10만 건(seed 둘).
Expected: 62개 통과, 0 불일치. 변이 12개 이상(`\s`를 유니코드 공백으로, `IGNORE_CASE`를 유니코드로, 크레딧에 `with` 더하기, 어간에서 `s` 접미사 빼기, `LIVE_LIKE`의 `d` 빼기, 질의의 `live` 예외 빼기, 괄호 중첩을 한 번만 풀기, 가수 변형 4개까지, 최소 가수 길이 1, 길이 차 `>=`, 앨범 등급 순서 바꾸기, 같은 등급에서 마지막 후보 고르기, NFKC 빼기, 낱말 경계 없이 부분 문자열로)이 모두 잡힌다.

- [ ] **Step 6: 커밋** — `Qt: 가사 후보 매칭 이식`

### Task 8: 관문과 보고서

**Files:**
- Create: `docs/superpowers/specs/2026-11-15-qt-engine-logic-report.md`
- Modify: `qt/README.md`, `THIRD-PARTY-NOTICES.md`(Kotlin 표준 라이브러리의 절차를 따른 함수가 늘었으면)

- [ ] **Step 1: 관문을 처음부터 돌린다**

우리 대상의 깨끗한 재빌드(릴리스와 디버그; `build/qt`를 통째로 지우지 않는다) → 두 빌드의 `ctest` → `test-names.ps1` → `forbidden-calls.ps1` → `run.ps1 -PerKind 10000`을 seed 20261115, 1, 987654321로(모든 종류: Q1의 것과 이 계획의 것) → `run.ps1 -PerKind 100000 -Seed 424242`.
Expected: 모두 통과, 빠진 이름 0, 금지 호출 0, 대조 불일치 0. 아니면 C++을 고치고 처음부터 다시.

- [ ] **Step 2: 보고서**

Q1 보고서와 같은 모양으로: 관문 표, `xgs::kt`에 더한 것(코드 포인트 판정·NFKC·실수 파싱에서 Qt와 JVM이 달랐던 것과 예외 표의 크기), `xgs::json`의 관찰 표와 Kotlin이 표준 밖에서 받아들이는 것의 목록, 이식 중 대조가 찾은 C++ 버그, 변이 시험 결과, **Kotlin과 다르게 한 것**(시그마: 사용자 결정, 입력과 두 답의 예), 고치지 않은 Kotlin 쪽 동작(있으면 입력·출력과 함께), 옮기지 않은 것, 라이선스 메모(JDK 소스를 보지 않았다는 각 Task의 진술, Kotlin 표준 라이브러리·`kotlinx.serialization`과의 관계), 계획과 달라진 것, 확인하지 못한 것, Q2b에 넘길 것(경로를 정리해 넘길 책임, `parseVideoInfo`가 받는 바이트, 명령 인자를 `QProcess`에 넘길 때의 인용 문제). 측정하지 않은 것을 측정한 것처럼 적지 않는다. 커밋 해시를 적지 않는다.

- [ ] **Step 3: 커밋** — `문서: Qt 전환 Q2a(engine 순수 로직) 보고서`

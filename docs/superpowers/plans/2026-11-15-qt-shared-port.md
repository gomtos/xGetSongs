# Qt 전환 Q1: `shared` 이식 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Kotlin `shared` 모듈의 로직 다섯 개(입력 판별, 파일명, 제목 파싱, 가사 추출, 로그 가리기)를 C++/Qt Core로 옮기고, 지금 테스트를 같은 이름으로 통과시키며, 무작위 입력으로 Kotlin과 결과가 한 건도 다르지 않음을 보인다.

**Architecture:** `qt/` 아래에 제품용 CMake 프로젝트를 새로 만든다. 로직은 Qt Core만 쓰는 정적 라이브러리 `xgscore`에 두고, Kotlin/JVM의 문자·문자열 의미(공백, 글자·숫자, 대소문자, trim)를 그대로 재현하는 얇은 층 `xgs::kt`를 먼저 만든 뒤 모든 이식 코드가 그 층만 쓰게 한다. 정답은 지금 Kotlin 구현이고, `qt/tools/differential`의 대조 도구가 같은 입력을 JVM의 Kotlin과 C++에 넣어 답을 줄 단위로 비교한다.

**Tech Stack:** C++20, Qt 6.11.3 (Core, Test) 정적 링크, MSVC(VS 2022), CMake + Ninja, vcpkg(`x64-windows-static`, 기준선 `4702e381dbbd9b54d3f7e1eb565cd8981c1814cd`), PowerShell 7, 대조용 JVM은 `server/build/sidecar-image`의 것.

**Spec:** [docs/superpowers/specs/2026-11-15-qt-rewrite-and-playlists-design.md](../specs/2026-11-15-qt-rewrite-and-playlists-design.md) §3, §4(Q1 행, "대조"). 앞 단계의 교훈: [Q0 보고서](../specs/2026-11-15-qt-spike-report.md) "이어질 단계에서 조심할 것".

## Global Constraints

- 정답은 Kotlin이다. `shared/`, `engine/`, `server/`, `diagnostics/`, `shell/`, `sidecar-host/`의 파일은 읽기만 하고 한 줄도 바꾸지 않는다. C++ 결과가 Kotlin과 다르면 C++을 고친다. Kotlin 쪽이 버그로 보여도 고치지 않고 보고서에 적는다.
- 바꾸는 곳은 `qt/`(단 `qt/spike/`는 건드리지 않는다: 사용자의 남은 확인에 쓰인다), 저장소 루트의 `LICENSE`, 설계 문서의 한 줄(Task 1), 그리고 Task 8의 보고서뿐이다.
- `xgscore`는 Qt Core만 쓴다(Gui·Widgets·Network 없음). 네임스페이스는 `xgs`. C++20. 경고는 `/W4 /WX /permissive-`.
- 소스는 BOM 없는 UTF-8이고 컴파일 옵션에 `/utf-8`을 준다. 한글 문자열은 `u"…"`/`QStringLiteral`로 쓰고, 사용자에게 보이는 문구(거절 사유, `재생목록`, `<경로>`, `<파일명>`, `Unknown Artist`)는 Kotlin 소스의 글자를 그대로 옮긴다.
- 이식 코드(`qt/core` 중 `kt_text.*` 밖)는 `QChar::isSpace`, `QChar::isLetterOrNumber`, `QChar::isLetter`, `QChar::isDigit`, `QChar::toLower`, `QChar::toUpper`, `QString::trimmed`, `QString::simplified`, `QString::toLower`, `QString::toUpper`, `Qt::CaseInsensitive`, `QRegularExpression`을 **직접 쓰지 않는다.** 같은 일은 `xgs::kt`의 함수로 한다(Task 8이 grep으로 확인).
- 길이와 인덱스의 단위는 UTF-16 단위다(`QString::size()`). Kotlin의 `String.length`와 같다. 코드 포인트로 세지 않는다.
- 결과는 로캘에 기대지 않는다(터키어 로캘에서도 같다).
- 테스트는 Qt Test. Kotlin의 `@Test` 함수 하나가 같은 이름의 `private slots` 함수 하나가 되고, 원래 테스트의 단언을 하나도 빼지 않고 같은 입력·같은 기대값으로 옮긴다. 테스트를 통과시키려고 기대값을 바꾸지 않는다.
- 지원 Windows는 **Windows 10 이상**(작성자 판단: 사용자가 정하지 않아 둔 값). `_WIN32_WINNT=0x0A00`.
- Qt 의존성은 `qt/vcpkg.json` 하나로 정하고 이 계획에서 한 번만 바꾼다(Task 1). 바꾸면 Qt를 다시 빌드한다(수십 분~1시간 반). `C:\dev\vcpkg`와 그 캐시는 지우지 않는다.
- 대조 도구가 Gradle을 돌려야 하면 `--no-daemon`으로 돌린다. `server/build/sidecar-image`가 이미 있으면 다시 만들지 않는다.
- 이 단계는 창을 띄우지 않는다. 외부로 나가는 요청도 없다. `%APPDATA%\xGetSongs`와 실행 중인 xGetSongs를 건드리지 않는다.
- 커밋은 `main`에 직접, 메시지는 한국어, 끝에 `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`. push하지 않는다.

**이 계획이 설계와 다르게 정한 것 (사용자가 계획을 볼 때 확인할 것)**

- `shared`의 `api/ApiModels.kt`(HTTP로 주고받는 형식)와 그 테스트 18개는 이 단계에서 옮기지 않는다. Qt 판에는 HTTP도 JSON 전송 형식도 없어서 그 테스트(직렬화 모양, SSE 이름)는 지킬 대상이 사라진다. 그 안의 도메인 형(`ResolvedItem`, `JobOptions`, `JobEvent`, `LyricsOutcome`, `ToolsStatus` 등)은 그것을 쓰는 Q2(`engine`)에서 평범한 C++ 구조체로 만든다. 그래서 Q1의 "같은 이름으로 모두 통과"는 **193개**(15 + 39 + 41 + 66 + 32)가 대상이다.
- `qt/spike/`는 Q1에서 지우지 않는다. 사용자의 남은 확인(실제 배율, Defender·SmartScreen PC)이 그 exe와 스크립트를 쓴다.

## Review Focus

테스트 193개가 다루지 않지만 사용자가 만날 입력이다. 각 줄 끝의 Task가 그 시험을 갖는다.

1. **BMP 밖 글자와 짝 없는 서로게이트**(이모지 제목, 깨진 입력)가 어느 함수에 들어가도 죽지 않고 Kotlin과 같은 답을 낸다 → 모든 대조 생성기가 이모지와 짝 없는 서로게이트 토큰을 섞는다(Task 2의 `token()`; Task 3~7이 쓴다).
2. **없음(null)·빈 문자열·공백뿐인 문자열**을 구분한다(`folderName(null)`, `tidy("   ")`, `parse(title, channel = null)`) → 선택 인자를 받는 모든 종류의 생성기가 세 경우를 각각 5% 이상 낸다(Task 4, 5, 6).
3. **아주 긴 입력**: 1MB짜리 설명란, 3만 자를 넘는 가사, 4천 자를 넘는 로그 줄, 부분이 수천 개인 경로에서 멈추거나 스택이 넘치지 않는다 → Task 6의 `extractOnOneMegabyteDescriptionFinishesQuickly`, Task 7의 `redactPathsWithFiveThousandPartsFinishesQuickly`(둘 다 2초 안).
4. **로캘**: 터키어 로캘에서 `I`의 소문자가 `ı`가 되지 않는다 → Task 2의 `lowercaseAndUppercaseDoNotDependOnTheLocale`.
5. **소스 인코딩**: 한글 상수가 시스템 코드 페이지와 상관없이 같은 UTF-16 단위로 컴파일된다 → Task 2의 대조 종류 `M`(상수 덤프)이 모든 사용자 문구를 Kotlin과 16진수로 비교한다.

---

## File Structure

| 파일 | 하는 일 |
|---|---|
| `LICENSE` | MIT, `Copyright (c) 2026 gomtos` |
| `qt/vcpkg.json`, `qt/CMakeLists.txt`, `qt/CMakePresets.json`, `qt/README.md` | 제품 빌드. preset `qt-release`, 빌드 폴더 `build/qt` |
| `qt/core/CMakeLists.txt` | 정적 라이브러리 `xgscore` |
| `qt/core/text/kt_text.h/.cpp`, `kt_tables.inc` | Kotlin/JVM의 문자·문자열 의미 (`xgs::kt`) |
| `qt/core/input/input_classifier.h/.cpp` | `InputClassifier.kt` |
| `qt/core/filename/filename_formatter.h/.cpp` | `FilenameFormatter.kt` |
| `qt/core/title/title_parser.h/.cpp` | `TitleParser.kt` |
| `qt/core/lyrics/lyrics_extractor.h/.cpp` | `LyricsExtractor.kt` |
| `qt/core/log/log_redaction.h/.cpp` | `LogRedaction.kt` |
| `qt/tests/CMakeLists.txt`, `qt/tests/tst_*.cpp` | Kotlin 테스트 클래스 하나에 실행 파일 하나 |
| `qt/tools/differential/Oracle.java` | Kotlin 쪽 답 (JVM, `shared` jar) |
| `qt/tools/differential/main.cpp`, `gen.h/.cpp`, `codec.h` | `xgs-diff.exe`: 사례 만들기와 C++ 쪽 답 |
| `qt/tools/differential/run.ps1` | 사례 → 두 답 → 비교 → 통계 |
| `qt/tools/test-names.ps1` | Kotlin `@Test` 이름과 Qt Test 함수 이름 대조 |
| `docs/superpowers/specs/2026-11-15-qt-shared-port-report.md` | 결과 보고서 |

**공통 명령** (모든 Task가 쓴다; `qt/README.md`에 적는다)

```
cmd /c "call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --preset qt-release -S qt && cmake --build build/qt"
ctest --test-dir build/qt --output-on-failure
pwsh qt/tools/differential/run.ps1 -Kinds <글자들> [-PerKind 10000] [-Seed 20261115]
```

**대조 규약** (Task 2가 만들고 Task 3~7이 종류를 더한다)

- 한 줄에 사례 하나, 탭으로 나눈다. 문자열은 UTF-16 단위마다 16진수 4자리, 빈 문자열은 `.`, 없음(null)은 `-`. (`shell/tools/differential`과 같은 표기.)
- `xgs-diff gen <종류> <개수> <seed>`가 사례를 표준 출력에 쓰고, `xgs-diff answer`가 표준 입력의 사례에 답한다. `Oracle.java`도 같은 사례에 답한다. `run.ps1`은 답의 줄 수가 사례 수와 같은지, 답이 한 줄도 다르지 않은지, 종류별 통계가 아래 문턱을 넘는지 보고, 셋 다 맞을 때만 종료 코드 0을 낸다. 처음 다른 12건은 입력과 두 답을 풀어서 찍는다.
- 난수는 mulberry32(`fuzz.mts`와 같은 식), 같은 seed면 같은 사례가 나온다.

| 종류 | 사례 | 답 | 통계 문턱 (10000건 기준) |
|---|---|---|---|
| `X` | (사례 없음: 0x0000~0xFFFF 전부) | 단위마다 `isWhitespace isLetterOrDigit isLetter uppercaseChar lowercaseChar` | 65536줄 |
| `M` | (사례 없음) | 사용자 문구 상수마다 `이름 16진수` | 상수 11개 |
| `S` | `S op text` (op: `lower` `upper` `trim` `trimStart` `trimEnd` `blank`) | `OK hex` 또는 `0`/`1` | op마다 1000건 이상 |
| `C` | `C raw` | `P id alsoVideoId` / `V id` / `REJECT 이유` | `P`, `V`, 거절 사유 넷 각각 5% 이상 |
| `F` | `F rank includeRank artist title` | `OK hex` / `ERR` | `ERR` 1% 이상, 말줄임표(`…`)가 든 답 5% 이상 |
| `D` | `D title` | `OK hex` | `재생목록`이 답인 것 5% 이상, 장치 이름 `_`가 붙은 것 2% 이상 |
| `G` | `G isPlaylist playlistTitle typed` | `OK hex` / `NULL` | `NULL` 5% 이상 |
| `T` | `T rawTitle channel metaArtist metaTrack` | `OK artist title HIGH\|MEDIUM\|LOW` | 신뢰도 셋 각각 10% 이상 |
| `L` | `L description` | `OK hex` / `NULL` | `OK` 20% 이상, `NULL` 20% 이상 |
| `Y` | `Y text` | `OK hex` / `NULL` | `OK` 20% 이상, `NULL` 10% 이상 |
| `R` | `R text` | `OK hex` | `<경로>`가 든 답 30% 이상, 안 든 답 20% 이상 |
| `N` | `N text name1 [name2 …]` | `OK hex` | `<파일명>`이 든 답 20% 이상 |

문턱을 못 넘으면 생성기가 약한 것이다. 문턱을 낮추지 말고 생성기를 고친다.

---

### Task 1: 뼈대 — LICENSE, 빌드, 빈 라이브러리와 연기 시험

**Files:**
- Create: `LICENSE`, `qt/vcpkg.json`, `qt/CMakeLists.txt`, `qt/CMakePresets.json`, `qt/README.md`, `qt/core/CMakeLists.txt`, `qt/core/version.h`, `qt/core/version.cpp`, `qt/tests/CMakeLists.txt`, `qt/tests/tst_smoke.cpp`
- Modify: `docs/superpowers/specs/2026-11-15-qt-rewrite-and-playlists-design.md` (§3 표에 한 행)

**Interfaces:**
- Produces: CMake 대상 `xgscore`(STATIC, `Qt6::Core` PUBLIC, include 경로 `qt/core`), 함수 `xgs_add_test(<이름> <소스…>)`(`qt/tests/CMakeLists.txt`에 정의: `Qt6::Test`와 `xgscore`를 링크한 콘솔 실행 파일을 만들고 `add_test`로 등록), `QString xgs::version()`(`"0.1.0"`).
- Produces: 빌드 폴더 `build/qt`, 테스트 실행 `ctest --test-dir build/qt --output-on-failure`.

- [ ] **Step 1: `LICENSE`**

MIT 라이선스 전문(표준 문구 그대로), 첫 줄 아래 `Copyright (c) 2026 gomtos`.

- [ ] **Step 2: `qt/vcpkg.json`**

`name` `xgetsongs`, `builtin-baseline` `4702e381dbbd9b54d3f7e1eb565cd8981c1814cd`. `qtbase`는 `default-features: false`에 기능 `gui`, `widgets`, `network`, `thread`, `concurrent`, `testlib`, `freetype`, `harfbuzz`, `png`, `doubleconversion`, `pcre2`. `webview2`도 넣는다. (스파이크의 목록에 `concurrent`와 `testlib`를 더한 것이다. 뒤 단계가 쓸 것까지 지금 정해 Qt를 한 번만 다시 빌드한다. 기능 이름이 포트에 없으면 `C:\dev\vcpkg\ports\qtbase\vcpkg.json`에서 맞는 이름을 찾아 쓰고 README에 적는다.)

- [ ] **Step 3: CMake**

`qt/CMakePresets.json`의 `qt-release`: generator `Ninja`, `binaryDir` `${sourceDir}/../build/qt`, `CMAKE_BUILD_TYPE=Release`, `CMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake`, `VCPKG_TARGET_TRIPLET=x64-windows-static`, `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`.
`qt/CMakeLists.txt`: `project(xgetsongs LANGUAGES CXX)`, C++20, 우리 대상에 `/W4 /WX /permissive- /utf-8`, 정의 `_WIN32_WINNT=0x0A00` `WINVER=0x0A00` `NOMINMAX` `QT_NO_CAST_FROM_ASCII` `QT_NO_CAST_TO_ASCII`, `enable_testing()`, `add_subdirectory(core)`, `add_subdirectory(tests)`.

- [ ] **Step 4: 연기 시험을 먼저 쓴다**

`qt/tests/tst_smoke.cpp`: Qt Test 클래스 `SmokeTest`, 슬롯 `versionIsSet()`이 `QCOMPARE(xgs::version(), QStringLiteral("0.1.0"))`. `xgs_add_test(tst_smoke tst_smoke.cpp)`.

- [ ] **Step 5: 빌드해서 실패를 본다 (Qt 재빌드가 여기서 돈다: 백그라운드로, 걸린 시간을 적는다)**

Expected: 구성은 되고 링크에서 `xgs::version` 미정의로 실패.

- [ ] **Step 6: `xgs::version()`을 구현하고 통과를 본다**

Run: 공통 명령의 빌드와 `ctest`
Expected: `100% tests passed, 0 tests failed out of 1`. `dumpbin /dependents build/qt/tst_smoke.exe`에 `Qt6*.dll`, `VCRUNTIME*.dll`, `MSVCP*.dll`이 없다.

- [ ] **Step 7: 설계 문서와 README**

설계 문서 §3의 표에 한 행을 더한다: `| 지원 Windows | Windows 10 이상 (`_WIN32_WINNT=0x0A00`). Qt 6.11이 지원하는 범위 안에서 정한 값이고 사용자가 바꿀 수 있다 | 작성자 판단 |`.
`qt/README.md`(한국어): 필요한 것(VS 2022, `C:\dev\vcpkg`), 공통 명령 세 가지, 고정한 기준선과 Qt 버전, 켠 qtbase 기능, 이번 재빌드에 걸린 시간.

- [ ] **Step 8: 커밋**

`git add LICENSE qt/vcpkg.json qt/CMakeLists.txt qt/CMakePresets.json qt/README.md qt/core qt/tests docs/superpowers/specs/2026-11-15-qt-rewrite-and-playlists-design.md` → `Qt: 제품 빌드 뼈대와 MIT 라이선스`

### Task 2: Kotlin 문자·문자열 의미 층(`xgs::kt`)과 대조 도구

**Files:**
- Create: `qt/core/text/kt_text.h`, `qt/core/text/kt_text.cpp`, `qt/core/text/kt_tables.inc`, `qt/tests/tst_kt_text.cpp`, `qt/tools/differential/Oracle.java`, `qt/tools/differential/main.cpp`, `qt/tools/differential/gen.h`, `qt/tools/differential/gen.cpp`, `qt/tools/differential/codec.h`, `qt/tools/differential/run.ps1`, `qt/tools/CMakeLists.txt`
- Modify: `qt/CMakeLists.txt`, `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/README.md`

**Interfaces:**
- Produces (`namespace xgs::kt`, 모두 JVM 21의 Kotlin과 같은 답):
  ```cpp
  bool isWhitespace(QChar c);        // Char.isWhitespace(): Zs·Zl·Zp, U+0009..000D, U+001C..001F
  bool isLetter(QChar c);            // Char.isLetter()
  bool isLetterOrDigit(QChar c);     // Char.isLetterOrDigit(): 글자(L*) 또는 Nd. 서로게이트 단위는 false
  bool isHighSurrogate(QChar c);
  bool isSurrogate(QChar c);
  QChar uppercaseChar(QChar c);      // Char.uppercaseChar()
  QChar lowercaseChar(QChar c);      // Char.lowercaseChar()
  bool equalsIgnoreCase(QChar a, QChar b);   // Char.equals(other, ignoreCase = true)
  QString lowercase(const QString& s);       // String.lowercase(): 로캘 무관, 길이가 바뀔 수 있다
  QString uppercase(const QString& s);       // String.uppercase()
  QString trim(const QString& s);            // String.trim()
  QString trimStart(const QString& s);
  QString trimEnd(const QString& s);
  QString trimEndChars(const QString& s, QStringView chars);     // trimEnd(vararg chars)
  QString trimStartChars(const QString& s, QStringView chars);
  bool isBlank(const QString& s);                                // 비었거나 전부 isWhitespace
  bool startsWith(QStringView s, QStringView prefix, bool ignoreCase = false, qsizetype from = 0);
  bool endsWith(QStringView s, QStringView suffix, bool ignoreCase = false);
  bool contains(QStringView s, QStringView needle, bool ignoreCase = false);
  qsizetype indexOf(QStringView s, QStringView needle, qsizetype from = 0, bool ignoreCase = false);   // 없으면 -1
  bool regionMatches(QStringView s, qsizetype at, QStringView other, bool ignoreCase);   // 범위를 벗어나면 false
  QString replace(const QString& s, QStringView oldValue, QStringView newValue, bool ignoreCase);      // 왼쪽부터, 겹치지 않게
  ```
- Produces: `xgs-diff.exe`(`build/qt/xgs-diff.exe`)와 `run.ps1`, 그리고 종류를 더하는 자리:
  - `gen.h`: `using Generator = QString (*)(Rng&)`, `void registerKind(char kind, Generator gen)`, `struct Rng { quint32 seed; double next(); int between(int low, int high); bool chance(double p); }`, 토큰 함수 `QString token(Rng&)`·`QString text(Rng&, int maxTokens = 40)`·`std::optional<QString> nullable(Rng&)`(없음 8%, 빈 문자열 8%, 공백뿐 8%, 나머지는 `text`).
  - `main.cpp`: `using Answerer = QString (*)(const QStringList& fields)`, `void registerAnswer(char kind, Answerer answer)`.
  - `codec.h`: `QString hex(const std::optional<QString>&)`, `std::optional<QString> unhex(QStringView)`.
  - `Oracle.java`: `switch (part[0])`에 종류마다 `case` 하나.
- `token()`의 재료(가중치는 `shell/tools/differential/fuzz.mts`의 `token()`을 따른다): ASCII 글자·숫자, 문장 부호 `` .-_()[]{}'&,;!@#$%^+=~` ``, 금지 문자 `\/:*?"<>|`, 제어 문자(U+0000, U+0001, 탭, LF, CR, U+001B, U+001C, U+001F, U+007F), 공백류(U+00A0, U+3000, U+2003, U+2028, U+FEFF, U+200B, U+180E, U+0085, U+2007, U+202F), 한글, 이모지(😀, 🎵, 🇰🇷, 👨‍👩‍👧, 𝒜), 짝 없는 서로게이트(U+D83D, U+DE00, U+DBFF), 장치 이름(`CON`, `PRN`, `AUX`, `NUL`, `COM1`, `LPT9`, `con`, `Nul`, `com0`, `LPT10`, `COM9`, `lpt1`), 그리고 **대소문자가 까다로운 글자**(`ß`, `İ`, `ı`, `Σ`, `ς`, `σ`, `ǅ`, `ﬁ`, `ŉ`, `Ǆ`, `K`(U+212A), `Å`(U+212B)).

- [ ] **Step 1: 대조 도구의 뼈대와 종류 `X`·`M`·`S`의 Kotlin 쪽을 쓴다**

`Oracle.java`: 인자 `chartable`이면 0x0000~0xFFFF의 각 단위에 대해 한 줄 `w l d u c`(isWhitespace, isLetter, isLetterOrDigit를 0/1로, `uppercaseChar`·`lowercaseChar`를 16진수 4자리로; Kotlin의 `kotlin.text.CharsKt`를 불러 Kotlin의 답을 쓴다), 인자 `constants`이면 상수 11개를 `이름 16진수`로 쓴다: `RejectReason`의 메시지 넷(`EMPTY`, `UNSUPPORTED_HOST`, `MIX_PLAYLIST`, `UNRECOGNIZED`), `FilenameFormatter.UNTITLED_PLAYLIST`, `TitleParser.UNKNOWN_ARTIST`, `LogRedaction.PATH_PLACEHOLDER`, `LogRedaction.FILE_NAME_PLACEHOLDER`, 그리고 `FilenameFormatter.format(1, "a", "")`·`folderName(null)`·`format(1, "a", "b")`의 결과. 인자가 없으면 표준 입력의 사례에 답한다(`S`: Kotlin의 `lowercase()`, `uppercase()`, `trim()`, `trimStart()`, `trimEnd()`, `isBlank()`).
`run.ps1`: `server/build/sidecar-image/runtime/bin/java.exe -cp "server/build/sidecar-image/lib/*" qt/tools/differential/Oracle.java`로 돌린다(이미지가 없으면 `.\gradlew.bat --no-daemon :server:sidecarImage`를 먼저 돌린다고 알리고 돌린다). 입출력은 ASCII. `-Kinds`에 `X`나 `M`이 있으면 사례 없이 두 쪽의 표를 비교한다.

- [ ] **Step 2: 단위 시험을 쓴다 (`qt/tests/tst_kt_text.cpp`, 클래스 `KtTextTest`)**

```cpp
void isWhitespaceMatchesKotlinAtTheEdges();
//   true:  U+0009 U+000A U+000B U+000C U+000D U+001C U+001D U+001E U+001F U+0020 U+00A0 U+1680 U+2003 U+2007 U+2028 U+2029 U+202F U+205F U+3000
//   false: U+0085 U+180E U+200B U+2060 U+FEFF U+3164 'a' U+D83D
void isLetterOrDigitMatchesKotlinAtTheEdges();
//   true: 'a' 'Z' '7' U+AC00(가) U+00E9 U+0661(아랍 숫자)   false: '_' '-' U+00B2(²) U+2163(Ⅳ) U+D83D U+DE00
//   U+3164(한글 채움 문자)도 넣되 기대값은 Kotlin의 답으로 적는다
void trimUsesKotlinWhitespace();
//   kt::trim(u"\u001C a \u00A0") == u"a";  kt::trim(u"\uFEFFa\uFEFF") == u"\uFEFFa\uFEFF";  kt::trim(u"\u0085a") == u"\u0085a"
void trimEndCharsStopsAtTheFirstOtherCharacter();   // trimEndChars(u"a. .", u". ") == u"a"
void isBlankIsTrueForEmptyAndWhitespaceOnly();      // "", " \t\u3000" → true;  "\u200B" → false
void lowercaseAndUppercaseChangeLength();
//   kt::uppercase(u"ß") == u"SS";  kt::lowercase(u"İ") == u"i\u0307";  kt::uppercase(u"ﬁ") == u"FI"
void lowercaseHandlesFinalSigma();                  // kt::lowercase(u"ΟΔΟΣ") == u"οδος";  kt::lowercase(u"Σ") == u"σ"
void lowercaseAndUppercaseDoNotDependOnTheLocale();
//   QLocale::setDefault(QLocale(QLocale::Turkish)); std::setlocale(LC_ALL, "tr-TR");
//   kt::lowercase(u"TITLE") == u"title";  kt::uppercase(u"title") == u"TITLE";  끝에 로캘을 되돌린다
void equalsIgnoreCaseComparesSingleCharacters();    // ('k', U+212A) → true;  ('ß', 'S') → false;  ('I', U+0131) → Kotlin의 답
void startsWithEndsWithContainsIgnoreCase();
//   endsWith(u"Foo - TOPIC", u" - Topic", true);  !endsWith(u"x", u"xx", true);  contains(u"See HTTPS://a", u"https://", true)
void indexOfReturnsMinusOneWhenAbsent();            // indexOf(u"abc", u"c", 3) == -1;  indexOf(u"abc", u"", 3) == 3
void regionMatchesIsFalseOutOfRange();              // regionMatches(u"ab", 1, u"bc", false) == false;  regionMatches(u"ab", -1, u"a", false) == false
void replaceIgnoreCaseIsLeftToRightAndNonOverlapping();   // replace(u"aAaA", u"aa", u"x", true) == u"xx";  replace(u"abc", u"B", u"<n>", true) == u"a<n>c"
```
주석에 "Kotlin의 답"이라고 한 값은 Step 1의 `Oracle.java chartable` 출력에서 읽어 리터럴로 적는다(추측으로 적지 않는다).

- [ ] **Step 3: 빌드해 실패를 본다** — Expected: `kt_text.h` 없음으로 컴파일 실패.

- [ ] **Step 4: `xgs::kt`를 구현한다**

방법: 문자 단위 함수는 `QChar`의 범주로 쓰고(`isLetter` = 범주 Lu·Ll·Lt·Lm·Lo, `isLetterOrDigit` = 그것 또는 Nd, 서로게이트는 false), 종류 `X`의 대조에서 Qt와 JVM이 다른 단위(유니코드 판 차이)는 **JVM의 답을 표로 넣는다**: `Oracle.java chartable`의 출력에서 다른 단위만 뽑아 `kt_tables.inc`에 정렬된 배열로 두고(파일 머리에 만든 방법과 JVM 버전을 주석으로), 함수가 표를 먼저 본다. 문자열 `lowercase`/`uppercase`는 로캘과 무관하게 유니코드 전체 대소문자 대응(길이 변화, 끝 시그마)을 따른다. `QString::toLower`/`toUpper`가 종류 `S`에서 Kotlin과 다르게 나오는 경우는 이 파일 안에서 직접 처리한다.

- [ ] **Step 5: `xgs-diff`의 `X`·`M`·`S`와 생성기 재료를 구현한다**

`xgs-diff chartable`·`constants`는 `Oracle.java`와 같은 형식으로 쓴다(상수 중 아직 없는 모듈의 것은 Task 3~7이 채운다: 이 Task에서는 `M`을 `run.ps1`의 기본 종류에서 빼고 Task 7에서 넣는다). `S`의 생성기는 `text()`로 만든 문자열에 op를 고르게 붙인다.

- [ ] **Step 6: 통과를 본다**

Run: 빌드, `ctest`, `pwsh qt/tools/differential/run.ps1 -Kinds XS`
Expected: `ctest` 전부 통과. 대조 출력에 `X: 65536 units, 0 mismatches`, `S: 10000 cases, 0 mismatches`, 종료 코드 0. `kt_tables.inc`에 든 예외 단위 수를 README에 적는다.

- [ ] **Step 7: 커밋** — `Qt: Kotlin 문자·문자열 의미 층과 대조 도구`

### Task 3: 입력 판별

**Files:**
- Create: `qt/core/input/input_classifier.h`, `qt/core/input/input_classifier.cpp`, `qt/tests/tst_input_classifier.cpp`
- Modify: `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/tools/differential/{main.cpp,gen.cpp,Oracle.java}`
- 원본(읽기만): `shared/src/commonMain/kotlin/com/xgetsongs/shared/input/InputClassifier.kt`, `shared/src/commonTest/kotlin/com/xgetsongs/shared/input/InputClassifierTest.kt`

**Interfaces:**
- Consumes: `xgs::kt`(Task 2), `registerKind`·`registerAnswer`·`token`·`text`(Task 2).
- Produces:
  ```cpp
  namespace xgs {
  enum class RejectReason { Empty, UnsupportedHost, MixPlaylist, Unrecognized };
  QString rejectMessage(RejectReason reason);          // Kotlin RejectReason.message의 글자 그대로
  QString rejectName(RejectReason reason);             // "EMPTY" "UNSUPPORTED_HOST" "MIX_PLAYLIST" "UNRECOGNIZED"
  struct ParsedInput {
      enum class Kind { Playlist, Video };
      Kind kind;
      QString id;
      std::optional<QString> alsoVideoId;              // 재생목록일 때만 있을 수 있다
      QString canonicalUrl() const;                    // https://www.youtube.com/playlist?list=<id> 또는 /watch?v=<id>
      bool operator==(const ParsedInput&) const = default;
  };
  struct ClassifyResult {
      std::optional<ParsedInput> input;                // 있으면 성공
      RejectReason reason = RejectReason::Empty;       // input이 없을 때만 뜻이 있다
      bool ok() const { return input.has_value(); }
  };
  ClassifyResult classifyInput(const QString& raw);
  }
  ```

- [ ] **Step 1: 테스트 15개를 옮긴다**

`InputClassifierTest.kt`의 `@Test` 15개를 같은 이름의 슬롯으로, 단언을 하나도 빼지 않고 옮긴다(클래스 `InputClassifierTest`).

- [ ] **Step 2: 실패를 본다** — Expected: 헤더 없음으로 컴파일 실패.

- [ ] **Step 3: `classifyInput`을 구현한다**

Kotlin 코드를 줄 단위로 따라 옮긴다. 주의할 곳: `isIdChars`는 ASCII 글자·숫자와 `_`, `-`만(코드 128 미만), 호스트는 `kt::lowercase`, 쿼리는 `&`로 나눠 `=`가 있는 조각만 쓰고 **같은 키가 여러 번이면 마지막 값**(Kotlin `associate`), 경로 조각은 빈 것을 버린다.

- [ ] **Step 4: 대조 종류 `C`를 더한다**

생성기는 `fuzz.mts`의 `classifierInput()`을 옮긴다(맨 ID, 허용·비허용 호스트, 스킴 유무, `shorts`/`embed`/`live` 경로, `v`·`list` 매개변수의 순서 섞기와 중복, `RD` 믹스, 조각, 앞뒤 공백). `Oracle.java`의 `C`는 `shell/tools/differential/Oracle.java`의 것과 같다.

- [ ] **Step 5: 통과를 본다**

Run: 빌드, `ctest`, `pwsh qt/tools/differential/run.ps1 -Kinds C`
Expected: `tst_input_classifier` 15개 통과. `C: 10000 cases, 0 mismatches`, 통계 문턱 통과.

- [ ] **Step 6: 커밋** — `Qt: 입력 판별 이식`

### Task 4: 파일명과 폴더 이름

**Files:**
- Create: `qt/core/filename/filename_formatter.h`, `qt/core/filename/filename_formatter.cpp`, `qt/tests/tst_filename_formatter.cpp`
- Modify: `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/tools/differential/{main.cpp,gen.cpp,Oracle.java}`
- 원본(읽기만): `shared/.../filename/FilenameFormatter.kt`, `shared/.../filename/FilenameFormatterTest.kt`

**Interfaces:**
- Consumes: `xgs::kt`, 대조 도구의 등록 함수와 `nullable`.
- Produces:
  ```cpp
  namespace xgs::filename {
  inline constexpr int MinRank = 1, MaxRank = 999, MaxBaseLength = 180, MaxArtistLength = 80, MaxFolderLength = 80;
  QString untitledPlaylist();                          // "재생목록"
  /// rank가 MinRank..MaxRank 밖이면 std::invalid_argument를 던진다 (includeRank가 false여도).
  QString format(int rank, const QString& artist, const QString& title, bool includeRank = true);
  QString folderName(const std::optional<QString>& title);
  std::optional<QString> destinationFolder(bool isPlaylist, const std::optional<QString>& playlistTitle, const std::optional<QString>& typed);
  QString sanitize(const QString& text);
  }
  ```

- [ ] **Step 1: 테스트 39개를 옮긴다** (클래스 `FilenameFormatterTest`; Kotlin의 `assertFailsWith<IllegalArgumentException>`은 `QVERIFY_THROWS_EXCEPTION(std::invalid_argument, …)`)
- [ ] **Step 2: 실패를 본다** — Expected: 컴파일 실패.
- [ ] **Step 3: 구현한다**

Kotlin을 따라 옮긴다. 주의할 곳: 금지 문자 아홉 개는 전각 문자로 바꾸고 U+0020 미만과 U+007F는 지운 뒤 `kt::trim`; `truncate`는 UTF-16 단위로 세고 자르는 자리 앞이 상위 서로게이트면 한 단위 더 줄인 뒤 `kt::trimEnd`하고 `…`를 붙인다; 장치 이름 판정은 첫 `.` 앞(끝의 공백을 뗀 것)을 `kt::uppercase`한 값으로 한다; 순번이 없을 때만, 그리고 가수에 `.`이 있을 때만 가수에 장치 이름 처리를 한다.

- [ ] **Step 4: 대조 종류 `F`·`D`·`G`를 더한다**

생성기는 `fuzz.mts`의 해당 부분을 옮긴다(`F`: rank는 3%가 `0`, `1000`, `-5`, `5000` 중 하나이고 나머지는 1~999, 가수와 제목은 `text()`를 때로 200단위 넘게; `D`·`G`: `nullable()`). `Oracle.java`의 `F`·`D`·`G`는 기존 것과 같다.

- [ ] **Step 5: 통과를 본다**

Run: 빌드, `ctest`, `pwsh qt/tools/differential/run.ps1 -Kinds FDG`
Expected: 39개 통과. 세 종류 모두 `0 mismatches`, 통계 문턱 통과.

- [ ] **Step 6: 커밋** — `Qt: 파일명·폴더 이름 이식`

### Task 5: 제목 파싱

**Files:**
- Create: `qt/core/title/title_parser.h`, `qt/core/title/title_parser.cpp`, `qt/tests/tst_title_parser.cpp`
- Modify: `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/tools/differential/{main.cpp,gen.cpp,Oracle.java}`
- 원본(읽기만): `shared/.../title/TitleParser.kt`, `shared/.../title/TitleParserTest.kt`

**Interfaces:**
- Produces:
  ```cpp
  namespace xgs {
  enum class Confidence { High, Medium, Low };
  struct ParsedTrack { QString artist; QString title; Confidence confidence; bool operator==(const ParsedTrack&) const = default; };
  QString unknownArtist();                             // "Unknown Artist"
  ParsedTrack parseTitle(const QString& rawTitle,
                         const std::optional<QString>& channel = std::nullopt,
                         const std::optional<QString>& metaArtist = std::nullopt,
                         const std::optional<QString>& metaTrack = std::nullopt);
  }
  ```

- [ ] **Step 1: 테스트 41개를 옮긴다** (클래스 `TitleParserTest`)
- [ ] **Step 2: 실패를 본다** — Expected: 컴파일 실패.
- [ ] **Step 3: 구현한다**

Kotlin을 따라 옮긴다. 주의할 곳: `clean`은 결과가 더 바뀌지 않을 때까지 도는 고정점 반복이다(반복 횟수에 제한을 두지 않되 매번 길이가 줄거나 같아야 끝난다는 것에 기댄다); 잡음 토큰은 소문자로 바꾼 목록을 **길이 내림차순**으로 보고(같은 길이끼리는 원래 순서: Kotlin의 `sortedByDescending`은 안정 정렬) 끝 비교는 `kt::endsWith(…, true)`; 구분자는 넷(` - `, ` – `, ` — `, `_ `) 중 위치 1 이후에서 가장 먼저 나온 것; 따옴표는 홑(`'`, `‘`, `’`)과 겹(`"`, `“`, `”`) 두 계열이고 여는 따옴표는 문자열 처음이나 공백 뒤, 닫는 따옴표는 여는 것에서 두 칸 이상 뒤이며 뒤에 글자·숫자가 오지 않는 것; `stripLeadingJunk`는 공백·서로게이트 단위·U+FE0F·U+200D·U+2190~U+2BFF를 건너뛴다.

- [ ] **Step 4: 대조 종류 `T`를 더한다**

생성기: 제목은 조각을 이어 만든다 — 앞머리(없음 / 이모지 / `[MV]`·`[LIVE]`·`[#2024MAMA]` 같은 대괄호 태그 1~2개), 가수(`text(…, 6)`), 구분자(없음 30% / 네 구분자 / 따옴표로 감싼 제목: 여섯 따옴표 문자 중 하나로 열고 같은 계열로 닫거나 때로 닫지 않음), 제목(`text(…, 8)`, 때로 `Don't`·`Girls'`처럼 낱말 안 아포스트로피), 꼬리(없음 / 잡음 토큰 15개 중 하나를 대소문자 섞어 맨몸으로 / 괄호로 감싸 / `[4K]` 같은 대괄호 / ` | 채널 이름`), 그리고 공백을 탭·U+3000·여러 칸으로 흩는다. 채널은 `nullable()`에 30%로 ` - Topic`(대소문자 섞음)이나 `VEVO`를 붙인다. `metaArtist`·`metaTrack`은 `nullable()`. `Oracle.java`의 `T`: `TitleParser.parse` → `OK <artist> <title> <confidence 이름>`.

- [ ] **Step 5: 통과를 본다**

Run: 빌드, `ctest`, `pwsh qt/tools/differential/run.ps1 -Kinds T`
Expected: 41개 통과. `T: 10000 cases, 0 mismatches`, `HIGH`·`MEDIUM`·`LOW` 각각 10% 이상.

- [ ] **Step 6: 커밋** — `Qt: 제목 파싱 이식`

### Task 6: 가사 추출과 다듬기

**Files:**
- Create: `qt/core/lyrics/lyrics_extractor.h`, `qt/core/lyrics/lyrics_extractor.cpp`, `qt/tests/tst_lyrics_extractor.cpp`
- Modify: `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/tools/differential/{main.cpp,gen.cpp,Oracle.java}`
- 원본(읽기만): `shared/.../lyrics/LyricsExtractor.kt`, `shared/.../lyrics/LyricsExtractorTest.kt`

**Interfaces:**
- Produces:
  ```cpp
  namespace xgs::lyrics {
  std::optional<QString> extract(const std::optional<QString>& description);
  std::optional<QString> tidy(const std::optional<QString>& text);
  }
  ```

- [ ] **Step 1: 테스트 66개를 옮기고 하나를 더한다** (클래스 `LyricsExtractorTest`)

더하는 것(Kotlin에 없는 이름이라 이름 대조에서 "추가"로 센다):
```cpp
void extractOnOneMegabyteDescriptionFinishesQuickly();
// "[Lyrics]\n" + ("가사 한 줄 lyric line\n" × 줄 수가 1MB(UTF-16 단위 1,048,576)를 넘을 때까지) 를 extract.
// QElapsedTimer로 2000ms 미만, 결과는 값이 있고 size() <= 30000, 마지막 글자가 '\n'이 아니다.
```

- [ ] **Step 2: 실패를 본다** — Expected: 컴파일 실패.
- [ ] **Step 3: 구현한다**

Kotlin을 따라 옮긴다. 주의할 곳: 순서는 `normalise`(제로폭 다섯 글자 제거 → U+3164를 공백으로 → `\r\n`·`\r`을 `\n`으로 → `\n`·탭을 뺀 제어 문자(U+0020 미만, U+007F~U+009F) 제거) → `\n`으로 나누기(빈 조각 유지) → 제목 줄 찾기; `key`는 `kt::isLetterOrDigit`인 단위만 남겨 `kt::lowercase`; 구분선은 trim 뒤 3단위 이상이고 전부 구분선 문자; 해시태그 줄은 trim 뒤 `#` 다음이 공백이 아닌 것; 끝내는 줄의 판정 순서는 Kotlin의 `isTerminator` 그대로(구분선 → 링크 → 저작권 → 해시태그 → 상용구 → 대괄호 → 괄호); `tidyLines`는 줄마다 `kt::trimEnd` → 앞뒤 빈 줄 제거 → 빈 줄 연속을 2개까지 → 30000 제한(줄 경계에서 자름) → 다시 앞뒤 빈 줄 제거 → 빈 줄이 아닌 줄이 3개 미만이면 없음.

- [ ] **Step 4: 대조 종류 `L`·`Y`를 더한다**

생성기 `L`: 줄을 이어 설명란을 만든다 — 머리말 줄 0~5개(`text()`·링크·해시태그), 제목 줄(60%: `MARKERS` 열 개 중 하나를 `[ ]`·`【 】`·`:`·`-`·맨몸으로 꾸미고 대소문자를 섞음, 때로 U+3164나 제로폭 문자를 끼움; 10%: `Lyrics by X`나 `#Lyrics #가사` 같은 가짜 제목), 구분선 줄 0~2개(구분선 문자 15개를 섞어 2~12단위), 본문 줄 0~12개(`text()`, 15%는 빈 줄이나 공백뿐인 줄, 때로 `(Follow me)`·`[Chorus]`), 끝내는 줄(링크·`©`·`Copyright`·해시태그·`[Rom]`·`(English)`·`(English version part)`·`Provided to YouTube by`·구분선 중 하나 또는 없음), 꼬리말 줄 0~3개. 줄 끝은 `\n`, `\r\n`, `\r`을 섞는다. `nullable()`로 없음·빈·공백도 낸다. 2%는 본문을 3만 단위 넘게 만든다.
생성기 `Y`: 본문 줄만(위의 본문 규칙), 빈 줄 연속 0~5개, 2%는 3만 단위 넘게, `nullable()`.
`Oracle.java`: `L` → `LyricsExtractor.extract`, `Y` → `LyricsExtractor.tidy`, 없음이면 `NULL`.

- [ ] **Step 5: 통과를 본다**

Run: 빌드, `ctest`, `pwsh qt/tools/differential/run.ps1 -Kinds LY`
Expected: 67개 통과. 두 종류 모두 `0 mismatches`, 통계 문턱 통과.

- [ ] **Step 6: 커밋** — `Qt: 가사 추출·다듬기 이식`

### Task 7: 로그 가리기

**Files:**
- Create: `qt/core/log/log_redaction.h`, `qt/core/log/log_redaction.cpp`, `qt/tests/tst_log_redaction.cpp`
- Modify: `qt/core/CMakeLists.txt`, `qt/tests/CMakeLists.txt`, `qt/tools/differential/{main.cpp,gen.cpp,Oracle.java,run.ps1}`
- 원본(읽기만): `shared/.../log/LogRedaction.kt`, `shared/.../log/LogRedactionTest.kt`

**Interfaces:**
- Produces:
  ```cpp
  namespace xgs::log {
  inline constexpr int MaxTextLength = 4000;
  QString pathPlaceholder();        // "<경로>"
  QString fileNamePlaceholder();    // "<파일명>"
  QString redactPaths(const QString& text);
  QString redact(const QString& text, const QStringList& knownNames);
  }
  ```

- [ ] **Step 1: 테스트 32개를 옮기고 하나를 더한다** (클래스 `LogRedactionTest`)

```cpp
void redactPathsWithFiveThousandPartsFinishesQuickly();
// "C:" + ("\\part with space" × 5000) 를 redactPaths. QElapsedTimer로 2000ms 미만이고,
// 결과는 Kotlin이 같은 입력에 낸 답과 같다.
```
그 기대값은 추측으로 적지 않는다. Step 4에서 종류 `R`이 생긴 뒤 이 입력 한 건을 `Oracle.java`에 넣어 받은 답을 리터럴로 적는다(그때까지 이 테스트는 시간 조건만 갖고, Step 5 전에 기대값을 채운다).

- [ ] **Step 2: 실패를 본다** — Expected: 컴파일 실패.
- [ ] **Step 3: 구현한다**

Kotlin의 스캐너를 따라 옮긴다(정규식과 재귀를 쓰지 않는다). 주의할 곳: 먼저 `bound`(4000 단위 초과면 자르고 `…`, 자르는 자리 앞이 상위 서로게이트면 한 단위 덜); 드라이브 시작은 ASCII 글자 + `:` + 구분자이고, 앞 글자가 ASCII 글자·숫자면 드라이브가 아니다(바로 앞에서 경로가 끝난 경우는 예외: `mayFollowLetter`); `file:///`와 `UNC\`의 비교는 `kt::regionMatches(…, true)`; 부분은 `\/:*?"<>|`·CR·LF, ` ->`, 다음 드라이브 시작 앞에서 끝난다; 구분자는 하나 또는 둘; 경로 끝의 ` ,;`는 자리표시 뒤에 그대로 둔다. `redact`는 `redactPaths` 뒤에, 알려진 이름마다 그 이름과 마지막 `.` 앞부분을 모아 → 공백뿐이거나 3단위 미만인 것을 버리고 → 중복 제거(처음 나온 순서 유지) → 길이 내림차순(안정 정렬) → `kt::replace(…, true)`.

- [ ] **Step 4: 대조 종류 `R`·`N`을 더하고 `M`을 기본 종류에 넣는다**

생성기 `R`: 문장 조각을 잇는다 — 보통 글(`text()`), 드라이브 경로(`C:`~`z:` + 구분자 `\`·`/`·`\\` + 부분 1~6개: 공백·한글·점이 든 이름), UNC(`\\host\share\…`, `\\?\C:\…`, `\\?\UNC\host\…`, `\\.\C:\…`), `file:///C:/…`(대소문자 섞음), 가짜(`https://a.b/c`, `Music\a.mp3`, `..\x`, `/usr/lib`, `abC:\x`), 이음(`, `, `; `, ` -> `, `: 이유`, 줄바꿈). 3%는 4000 단위를 넘긴다.
생성기 `N`: `R`의 글에 이름 1~3개를 심는다(이름은 `001 가수 - 제목.m4a` 꼴, 2단위짜리, 공백뿐, 확장자 없는 것, 대소문자를 바꿔 심은 것, 경로 안에 든 것).
`Oracle.java`: `R` → `redactPaths`, `N` → `redact(text, names)`.
`xgs-diff constants`의 상수 11개를 모두 채우고 `run.ps1`의 기본 `-Kinds`를 `XMSCFDGTLYRN`으로 한다.

- [ ] **Step 5: 통과를 본다**

Run: 빌드, `ctest`, `pwsh qt/tools/differential/run.ps1 -Kinds RNM`
Expected: 33개 통과. `R`·`N` `0 mismatches`와 통계 문턱 통과, `M: 11 constants, 0 mismatches`.

- [ ] **Step 6: 커밋** — `Qt: 로그 가리기 이식`

### Task 8: 관문 점검과 보고서

**Files:**
- Create: `qt/tools/test-names.ps1`, `docs/superpowers/specs/2026-11-15-qt-shared-port-report.md`
- Modify: `qt/README.md`

- [ ] **Step 1: `qt/tools/test-names.ps1`**

Kotlin 테스트 파일 다섯 개(`InputClassifierTest`, `FilenameFormatterTest`, `TitleParserTest`, `LyricsExtractorTest`, `LogRedactionTest`)에서 `@Test` 바로 다음의 `fun <이름>(`을 모으고, 대응하는 `build/qt/tst_*.exe -functions`의 출력(끝의 `()`를 뗀 이름)과 비교한다. 클래스마다 `Kotlin N개, Qt M개, 빠진 것 0개, 추가된 것 K개(이름)`를 찍고, 빠진 것이 하나라도 있으면 종료 코드 1. 추가된 것은 이 계획이 더한 둘(`extractOnOneMegabyteDescriptionFinishesQuickly`, `redactPathsWithFiveThousandPartsFinishesQuickly`)뿐이어야 하고 그 밖의 이름이 있으면 종료 코드 1.

- [ ] **Step 2: 금지한 Qt 함수가 이식 코드에 없는지 본다**

Run: `qt/core`에서 `kt_text.cpp`·`kt_text.h`를 뺀 파일을 대상으로 `isSpace\(|isLetterOrNumber\(|isLetter\(|isDigit\(|toLower\(|toUpper\(|trimmed\(|simplified\(|CaseInsensitive|QRegularExpression`를 찾는다(`kt::isLetter(`처럼 `kt::`가 앞에 붙은 것은 뺀다).
Expected: 0건.

- [ ] **Step 3: 관문을 처음부터 다시 돌린다**

Run (깨끗한 빌드 폴더에서): 빌드 → `ctest --test-dir build/qt --output-on-failure` → `pwsh qt/tools/test-names.ps1` → `pwsh qt/tools/differential/run.ps1 -PerKind 10000 -Seed 20261115` → 같은 것을 `-Seed 1`과 `-Seed 987654321`로 두 번 더.
Expected: 테스트 196개(193 + 추가 2 + 연기 1)와 `tst_kt_text`의 것이 모두 통과, 이름 대조에서 빠진 것 0, 대조 세 번 모두 모든 종류 `0 mismatches`와 통계 문턱 통과. 하나라도 아니면 고치고(Kotlin이 아니라 C++을) 처음부터 다시 돌린다.

- [ ] **Step 4: 보고서를 쓴다**

`2026-11-15-qt-spike-report.md`와 같은 모양으로: 관문 표(테스트 수와 결과, 이름 대조, 대조 종류별 사례 수·불일치 수·통계), Qt와 JVM이 달랐던 문자 단위 수와 대표 예(`kt_tables.inc`), `QString::toLower`/`toUpper`와 달랐던 것, Kotlin 쪽에서 버그로 보인 것(있으면: 고치지 않았다고 적는다), 옮기지 않은 것(`ApiModels`와 그 테스트 18개, 이유), Qt 재빌드에 걸린 시간, 계획과 달라진 것, 확인하지 못한 것, 다음 단계(Q2)에 넘길 것. 측정하지 않은 것을 측정한 것처럼 적지 않는다.

- [ ] **Step 5: 커밋** — `Qt: Q1 관문 점검 도구`, `문서: Qt 전환 Q1(shared 이식) 보고서`

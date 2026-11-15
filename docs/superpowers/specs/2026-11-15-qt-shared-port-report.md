# Qt Q1(`shared` 이식) 보고서

작성일: 2026-11-15
설계: [2026-11-15-qt-rewrite-and-playlists-design.md](2026-11-15-qt-rewrite-and-playlists-design.md) §4
계획: [2026-11-15-qt-shared-port.md](../plans/2026-11-15-qt-shared-port.md)
이전 단계: [2026-11-15-qt-spike-report.md](2026-11-15-qt-spike-report.md)

환경: Windows 11 Home 10.0.26200, Visual Studio 2022(vcvars64), Qt 6.11.3, 정답 쪽 JVM은 `server/build/sidecar-image`의 OpenJDK 21.0.9. 창은 띄우지 않았고 네트워크도 쓰지 않았다. Gradle은 돌리지 않았다(사이드카 이미지가 이미 있었다).

이 문서의 "관문 표"에 있는 숫자는 최종 리뷰를 반영한 커밋 `Qt: 최종 리뷰 반영 — CaseMode, 대조 보강, 디버그 빌드, 고지`에서 관문 전체를 처음부터 다시 돌린 결과다(아래 "관문 표"의 순서대로: 깨끗한 릴리스 빌드, 릴리스·디버그 테스트, 두 점검 스크립트, 대조 네 번). 그 실행에서 나오지 않은 숫자는 어느 이전 실행에서 왔는지 그 자리에 적었다. 돌리지 않은 것은 돌리지 않았다고 적었다.

## 요약

- 설계 §4의 Q1 관문 두 조건(이식 대상 `shared` 테스트 193개가 같은 이름으로 모두 통과, 대조 불일치 0)은 이 PC에서 최종 코드로 돌린 범위에서 **충족됐다.**
- 이식한 것은 `shared`의 모듈 다섯(입력 판별, 파일명, 제목 파싱, 가사 추출, 로그 가리기)과 Kotlin/JVM의 문자·문자열 의미를 옮긴 층(`xgs::kt`)이다.
- 이 진술 밖에 있는 것: `ApiModels`와 그 테스트 18개(옮기지 않았다), `engine`의 yt-dlp 출력 파싱과 LRCLIB 매칭(Q2), 다른 Qt·JVM 버전, 아래 "확인하지 못한 것".
- Kotlin 쪽에서 버그로 보이는 동작을 여럿 찾았다. 고치지 않았고 Qt 판이 그대로 따른다. 바꿀지는 사용자가 정한다.
- **사용자가 정할 것 하나(Q2 전):** 끝 시그마(`σ`/`ς`). Q1에서는 출력에 드러나지 않지만, Q2의 가사 매칭은 사용자 글을 소문자로 바꿔 서로 비교하므로 차이가 드러난다(아래 "끝 시그마").
- 대소문자 무시 영역 비교(`regionMatchesImpl`)는 JDK 소스에 기대어 쓴 첫 판을 읽지 않고 지운 뒤, 공개 문서와 오라클로 관찰한 JVM 답만으로 다시 썼다. 로컬 이력은 push 전에 합친다(squash). 그 밖의 사항은 "라이선스 메모".

## 관문 표

### 빌드

Qt는 다시 빌드하지 않았다(`build/qt/vcpkg_installed`는 그대로). VS 2022 vcvars64 안에서 한 명령으로 `cmake --build build/qt --target clean`, `cmake --preset qt-release -S qt`, `cmake --build build/qt`, `ctest --test-dir build/qt --output-on-failure`를 이어 돌렸고(`VCPKG_ROOT=C:\dev\vcpkg`) 종료 코드 0이다. 출력 전체를 남겼다.

| 항목 | 값 (2026-11-15) |
|---|---|
| clean | `Cleaning... 34 files.` |
| 구성 | `Configuring done (9.7s)`, `Generating done (0.5s)` |
| 빌드 단계 | `[1/43]`~`[43/43]` 모두 실행. 컴파일러 경고 0(`/W4 /WX`). 출력의 `warning`은 Qt의 CMake 매크로(`Qt6CoreMacros.cmake`)가 구성 때 낸 `CMake Warning` 한 줄뿐이다 |
| 걸린 시간 | 21:49:20 ~ 21:49:54(구성·빌드·ctest 합쳐 34초) |

디버그 빌드(프리셋 `qt-debug`, `build/qt-debug`, `/MTd`): 이 패스에서 처음 구성했다. vcpkg가 의존성 43개를 바이너리 캐시에서 24초 만에 복원했고(Qt를 다시 빌드하지 않았다) 구성은 60.5초, 빌드는 43단계 모두 통과했다. 관문 실행 때는 그 뒤 코드가 바뀌지 않아 `ninja: no work to do.`였다.

### 테스트 (ctest와 각 실행 파일의 `Totals:` 줄)

릴리스 `ctest --test-dir build/qt --output-on-failure`(깨끗한 빌드 직후): 7개 모두 통과(`100% tests passed, 0 tests failed out of 7`, Total 0.83 sec). 실행 파일마다 `-o <파일>,txt`로 얻은 합계:

| 실행 파일 | 통과 | 실패 | 테스트 함수 | 그중 |
|---|---|---|---|---|
| `tst_smoke` | 3 | 0 | 1 | 연기 시험 |
| `tst_kt_text` | 42 | 0 | 16 | `xgs::kt` 단위 시험 15 + 탐침 데이터 시험 1(행 25) |
| `tst_input_classifier` | 17 | 0 | 15 | `InputClassifierTest` 15 |
| `tst_filename_formatter` | 41 | 0 | 39 | `FilenameFormatterTest` 39 |
| `tst_title_parser` | 44 | 0 | 42 | `TitleParserTest` 41 + 추가 1 |
| `tst_lyrics_extractor` | 69 | 0 | 67 | `LyricsExtractorTest` 66 + 추가 1 |
| `tst_log_redaction` | 35 | 0 | 33 | `LogRedactionTest` 32 + 추가 1 |
| **합계** | **251** | **0** | **213** | 이식 193 + 추가 3 + 연기 1 + `tst_kt_text` 16 |

251을 설명한다. Qt Test는 실행 파일마다 `initTestCase`와 `cleanupTestCase`를 통과로 세므로 실행 파일 7개에서 14가 더해지고, 데이터 시험은 행마다 하나로 센다(`regionMatchesIgnoreCaseMatchesTheObservedJvmAnswers`의 25행은 함수 하나지만 25로 센다). 251 = 213 − 1 + 25 + 14. 브리프의 196(193 + 추가 2 + 연기 1)과 다른 점: 추가 이름이 셋이다(Task 8의 특성 테스트 `leadingNonBmpLettersAreDroppedLikeEmoji`), `tst_kt_text`는 196에 들어 있지 않다.

디버그 `ctest --test-dir build/qt-debug --output-on-failure`: 7개 모두 통과(`100% tests passed, 0 tests failed out of 7`, Total 7.58 sec). Qt의 단언과 `QString`/`QList` 범위 검사가 켜진 빌드다. 범위 밖 접근은 드러나지 않았다. 처음 돌렸을 때 실패가 하나 있었는데 범위 검사가 아니라 시간 한도였다: `extractOnOneMegabyteDescriptionFinishesQuickly`가 최적화 없는 빌드에서 5481 ms 걸려 2초 한도를 넘었다. 이 시험의 시간 한도는 최적화 빌드에 대한 것이라 `NDEBUG`일 때만 확인하게 했다(디버그에서도 추출은 돌고 결과 단언 셋은 그대로 확인하며 걸린 시간을 찍는다). 이것은 이 포트가 더한 시험이고 Kotlin 짝이 없다. 따로, 디버그 `xgs-diff`로 모든 사례 종류를 1만 건씩(seed 20261115) 만들고 답하게 하고 세 표(`chartable`, `unicodecase`, `constants`)를 찍게 했다. 모두 종료 코드 0(단언 없음)이고 릴리스의 출력과 바이트까지 같았다.

### 이름 대조 (`pwsh qt/tools/test-names.ps1`, 종료 코드 0)

```
InputClassifierTest: Kotlin 15, Qt 15, missing 0, added 0
FilenameFormatterTest: Kotlin 39, Qt 39, missing 0, added 0
TitleParserTest: Kotlin 41, Qt 42, missing 0, added 1 (leadingNonBmpLettersAreDroppedLikeEmoji)
LyricsExtractorTest: Kotlin 66, Qt 67, missing 0, added 1 (extractOnOneMegabyteDescriptionFinishesQuickly)
LogRedactionTest: Kotlin 32, Qt 33, missing 0, added 1 (redactPathsWithFiveThousandPartsFinishesQuickly)
PASSED: every Kotlin test name is a Qt test name; only the three intended names were added
```

| 클래스 | Kotlin | Qt | 빠진 것 | 추가된 것 |
|---|---|---|---|---|
| InputClassifierTest | 15 | 15 | 0 | 0 |
| FilenameFormatterTest | 39 | 39 | 0 | 0 |
| TitleParserTest | 41 | 42 | 0 | 1 |
| LyricsExtractorTest | 66 | 67 | 0 | 1 |
| LogRedactionTest | 32 | 33 | 0 | 1 |
| 합계 | 193 | 196 | 0 | 3 |

이 점검은 이름만 대조한다. 단언이 같은지는 이 스크립트가 보지 않는다(Task 6의 리뷰어가 66개를 스크립트와 손으로 비교했다는 기록이 원장에 있다. 나머지 클래스의 단언 대조는 각 Task 리뷰에 맡겼다). 스크립트는 이제 Kotlin 테스트 파일에서 이름이 하나도 안 나오면 실패한다. `tst_kt_text`의 새 이름 `regionMatchesIgnoreCaseMatchesTheObservedJvmAnswers`는 Kotlin 짝이 없는 실행 파일이라 대조 대상이 아니다. 다섯 이식 클래스의 Kotlin 밖 이름은 위의 셋뿐이다.

### 금지 호출 (`pwsh qt/tools/forbidden-calls.ps1`, 종료 코드 0)

```
looked at 13 files under qt/core (kt_text.* and kt_tables.inc excluded) and 37 text files under qt/ (qt/spike excluded)
PASSED: 0 hits
```

`qt/core`(`kt_text.*` 제외)에서 `isSpace(`, `isLetterOrNumber(`, `isLetter(`, `isDigit(`, `toLower(`, `toUpper(`, `trimmed(`, `simplified(`, `CaseInsensitive`, `QRegularExpression`, 그리고 이 패스에서 더한 `isUpper(`, `isLower(`, `isNumber(`, `isPunct(`, `toCaseFolded(`, `localeAwareCompare`, `QLocale`, `normalized(`(앞에 `kt::`가 붙은 것은 제외) 0건. `qt/`(`qt/spike` 제외)의 모든 텍스트 파일(이제 확장자와 상관없이)에서 JDK 내부 이름 0건. 스크립트가 새 패턴을 실제로 잡는지는 이 패스에서 임시 파일 둘(`qt/core/zz_probe.cpp`에 `QLocale`·`c.isUpper()`·`s.normalized(…)`·`kt::isUpper(c)`, `qt/zz_probe.xyz`에 JDK 패키지 이름 하나)로 확인했다: 4건 잡고(`kt::isUpper`는 잡지 않음) 종료 코드 1, 두 파일은 지웠다. 이 확인은 관문 실행 전의 일이다.

### 대조 (`pwsh qt/tools/differential/run.ps1`)

네 번의 실행이다. 모두 모든 종류 `UXMSCFDGTLYRN`을 한 번의 호출로 돌렸다. 앞의 셋은 `-PerKind 10000`, 넷째는 `-PerKind 100000 -Seed 424242`(seed는 Task 8에서 정한 값)이다. `run.ps1`은 이제 종류를 하나씩 돌리고 다음 종류 전에 앞 종류의 데이터를 놓으므로, 10만 건도 묶음으로 나누지 않고 한 번에 돌았다. 네 실행 모두 종료 코드 0이다.

| 종류 | 10000 seed 20261115 | 10000 seed 1 | 10000 seed 987654321 | 100000 seed 424242 |
|---|---|---|---|---|
| U | 1112064 코드 포인트, 0 | 1112064, 0 | 1112064, 0 | 1112064, 0 |
| X | 65536 단위, 0 | 65536, 0 | 65536, 0 | 65536, 0 |
| M | 11 상수, 0 | 11, 0 | 11, 0 | 11, 0 |
| S | 10000건, 0 | 10000, 0 | 10000, 0 | 100000, 0 |
| C | 10000건, 0 | 10000, 0 | 10000, 0 | 100000, 0 |
| F | 10000건, 0 | 10000, 0 | 10000, 0 | 100000, 0 |
| D | 10000건, 0 | 10000, 0 | 10000, 0 | 100000, 0 |
| G | 10000건, 0 | 10000, 0 | 10000, 0 | 100000, 0 |
| T | 10000건, 0 | 10000, 0 | 10000, 0 | 100000, 0 |
| L | 10000건, 0 | 10000, 0 | 10000, 0 | 100000, 0 |
| Y | 10000건, 0 | 10000, 0 | 10000, 0 | 100000, 0 |
| R | 10000건, 0 | 10000, 0 | 10000, 0 | 100000, 0 |
| N | 10000건, 0 | 10000, 0 | 10000, 0 | 100000, 0 |
| **불일치 합계** | **0** | **0** | **0** | **0** |
| 걸린 시간 | 72초 | 66초 | 65초 | 401초 |

이 패스에서 대조에 더한 것:

- **종류 `U`**(사례 없음, 표 종류): U+0000~10FFFF의 각 코드 포인트(서로게이트 D800~DFFF 제외)마다 그 코드 포인트 하나짜리 문자열의 `lowercase()`와 `uppercase()`. Kotlin 쪽은 `S`의 `lower`/`upper`가 쓰는 호출 그대로, C++ 쪽은 `kt::lowercase`/`kt::uppercase`. 1,112,064개 모두 같았다. 시그마 하나짜리 `Σ`는 양쪽 모두 `σ`라 접지 않는다.
- **`S` op `ci-starts-from`**: Kotlin `startsWith(prefix, from, ignoreCase = true)`(`StringsKt.startsWith(String, String, int, boolean)`)와 `kt::startsWith(text, prefix, from, CaseMode::Ignore)`. 접두어는 텍스트의 `from` 자리에서 잘라 내고(가끔 대소문자를 바꾸거나 끝 단위를 바꾸거나 한 조각을 더함), 가끔 비거나 무관하며, `from`은 -2..길이+2, 서로게이트 쌍·짝 없는 서로게이트·BMP 밖 대소문자 글자가 섞인다. 문턱은 op 몫 5% 이상(10000건에 500건), 두 답 각 15% 이상.
- **시그마 접기를 좁혔다.** 전에는 `lower`의 모든 답에서 `ς`를 `σ`로 접었다. 이제 사례의 입력에 `Σ`(U+03A3)가 있을 때만 접는다. 그래서 C++이 글자 그대로의 `ς`를 `σ`로 바꾸는 버그는 이제 잡힌다.
- **오래된 오라클 검사.** `server/build/sidecar-image/lib/shared-jvm.jar`가 `shared/src/commonMain`의 가장 새 파일보다 오래됐으면 `run.ps1`이 사이드카 이미지를 다시 만들라는 말과 함께 실패한다. 이 실행에서는 jar(10-17)가 가장 새 소스(10-16)보다 새로웠다. 검사 논리는 날짜를 2020년으로 바꾼 임시 파일을 jar 자리에 넣어 실패 메시지가 나오는 것을 따로 확인했다(`run.ps1` 자체를 그렇게 돌린 것은 아니다).
- **측정값 출력.** `C`의 갈래별 비율과 `S`의 op별 비율·답 비율을 이제 다른 종류처럼 찍는다. 그래서 1만 건 실행의 통계는 99개에서 133개가 됐다.

모든 실행이 통계 문턱을 넘었다(`PASSED`). 문턱은 낮추지 않았다. 가장 빠듯한 것(측정값을 문턱으로 나눈 값이 가장 작은 것):

| 실행 | 가장 빠듯한 통계 (측정값, 문턱) |
|---|---|
| 10000, seed 20261115 | R: 가짜만 든 입력의 Kotlin 답에 `<경로>`가 없음 97.2% (90%), R: `<경로>` 없는 답 22.6% (20%), L: 공백뿐 설명 6.0% (5%), R: 4000단위 초과 3.6% (3%) |
| 10000, seed 1 | R: 96.7% (90%), R: 22.2% (20%), L: 빈 설명 6.0% (5%), L: null 설명 6.0% (5%) |
| 10000, seed 987654321 | R: 96.7% (90%), R: 22.6% (20%), L: 공백뿐 설명 5.8% (5%), L: null 설명 5.8% (5%) |
| 100000, seed 424242 | R: 97.3% (90%), R: 22.1% (20%), L: 공백뿐 설명 5.9% (5%), L: null 설명 6.0% (5%) |

종류별 대표 통계(10000건, seed 20261115, `run.ps1` 출력에서 옮김):

| 종류 | 통계 |
|---|---|
| S | op 15개가 각 6.2~7.3%(문턱 5%). `ci-starts-from` 646건 중 답 0이 59.6%, 1이 40.4%(문턱 각 15%). `blank` 709건 중 답 1이 36.8%, 0이 63.2%(각 5%). `ci-index`가 찾은 것 35.4%(20%). 불리언 op의 두 답은 모두 31.3% 이상 |
| C | `P` 12.4%, `V` 14.1%, `REJECT EMPTY` 6.6%, `REJECT UNSUPPORTED_HOST` 37.9%, `REJECT MIX_PLAYLIST` 8.8%, `REJECT UNRECOGNIZED` 20.4%(문턱 각 5%) |
| F | `ERR` 2.9%, 말줄임표 든 답 38.0%, 가수 77~83단위 40.3%, 그 자름 자리에 서로게이트 30.1% |
| D | `재생목록`이 답인 것 28.2%, 장치 이름 `_` 6.1% |
| G | `NULL` 14.3% |
| T | HIGH 39.6%, MEDIUM 41.5%, LOW 18.9%, 따옴표 안에서 나온 제목 18.3%, 포트의 답이 `Unknown Artist` 5.1% |
| L | OK 40.0%, NULL 60.0%, 진짜 제목 줄 61.7%, 3만 단위 초과 입력 3.2%, 그중 답이 서로게이트 쌍으로 끝남 0.8% |
| Y | OK 63.3%, NULL 36.7%, 빈 줄 3개 이상 연속 57.0% |
| R | `<경로>`가 든 답 77.4%, 경로 꼴 여덟 가지 각 11.2~32.9%, 4000단위 초과 3.6% |
| N | `<파일명>`이 든 답 64.9%, 이름이 다른 이름의 앞부분 10.3% |

`R`과 `N`의 "누설 방향" 수(C++ 답이 Kotlin 답보다 길거나 자리표시가 적은 사례)는 네 번의 실행에서 모두 0이다. 이 수는 문턱이 아니라 진단이다. 불일치가 0이면 정의상 0이다.

## `xgs::kt` 층

Kotlin/JVM의 `Char`와 `String` 의미를 Qt 위에 옮긴 층이다(`qt/core/text/kt_text.*`). 이식 코드는 `QChar`/`QString`의 공백·글자·대소문자 함수를 직접 쓰지 않고 이 층을 쓴다. 위 `forbidden-calls.ps1`이 그것을 지킨다.

**`CaseMode`.** Kotlin의 `ignoreCase`는 이제 `enum class CaseMode { Sensitive, Ignore }`이다. 전에는 `startsWith(s, prefix, bool ignoreCase, qsizetype from)`와 `indexOf(s, needle, qsizetype from, bool ignoreCase)`가 뒤 인자를 반대 순서로 받아서 `kt::indexOf(s, n, true)`가 "1부터 찾기"로 컴파일됐다. 이제 모든 함수가 Kotlin의 순서 `(…, from = 0, mode = CaseMode::Sensitive)`를 따르고, 위치 자리에 `bool`을 주면 삭제한 오버로드 때문에 컴파일 오류다(이 패스에서 일부러 그렇게 쓴 파일로 오류를 확인했다). 내부의 `regionMatchesImpl`은 `bool`을 그대로 받고 호출하는 쪽에서 바꾼다. 로그 가리기의 네임스페이스는 `xgs::log`에서 `xgs::redaction`으로 바꿨다(뒤 단계의 로그 기능과 뜻이 겹치지 않게).

### Qt와 JVM이 다른 문자 단위: 48개

Qt 6.11.3와 JVM 21.0.9에서 `isWhitespace`, `isLetter`, `isLetterOrDigit`, `uppercaseChar`, `lowercaseChar` 중 하나 이상의 답이 다른 UTF-16 단위가 **48개**다. 함수별로 다른 단위 수는 다음과 같다(한 단위가 여러 함수에서 다를 수 있어서 합이 48보다 크다). 출처는 `qt/core/text/kt_tables.inc`의 머리 주석(`gen-tables.ps1`이 쓴 것)과 Task 2 보고서다.

| 함수 | 다른 단위 수 | 예 (JVM의 답을 표에 둔다) |
|---|---|---|
| `isWhitespace` | 0 | |
| `isLetter` | 16 | U+088F, U+0C5C, U+0CDC, U+1C89, U+1C8A, U+A7CB: JVM은 글자가 아니라 하고 Qt는 글자라 한다(Unicode 16에 새로 생긴 글자) |
| `isLetterOrDigit` | 16 | 위와 같은 단위 |
| `uppercaseChar` | 35 | U+019B: JVM `019B`, Qt `A7DC`. U+1F80: JVM `1F88`, Qt `1F80`(그리스 iota 아래 첨자 글자들) |
| `lowercaseChar` | 9 | U+0130: JVM `0069`, Qt `0130`. U+1C89: JVM `1C89`, Qt `1C8A` |

표에는 이 밖에 유니코드 SpecialCasing 자료의 특수 대문자 102개(`ß`→`SS`, `ﬁ`→`FI` 등)와 특수 소문자 1개(`İ`→`i̇`), BMP 밖 대소문자 대응 520개가 있다. 모두 문자 하나의 속성이고, JVM의 공개 API에 물어서 얻었다. 이분 탐색하는 표 넷은 이제 정렬돼 있는지 컴파일할 때 확인한다(`static_assert`). Qt나 사이드카 이미지의 JVM이 바뀌면 `gen-tables.ps1`을 다시 돌려야 한다. 종류 `U`가 이 표를 코드 포인트 전체에서 시험한다: BMP 밖 글자와 특수 대소문자를 포함해 `lowercase`/`uppercase`가 JVM과 다른 코드 포인트는 없었다.

### `QString::toLower`/`toUpper`와 달랐던 것

`kt_text.cpp`는 이 둘을 쓰지 않는다. 단위별로 비교한 결과(Task 2 보고서, `gen-tables.ps1`의 기록용 계산, 이번에 다시 돌리지 않았다):

- `QString::toUpper`가 `String.uppercase()`와 다른 단위 8개: U+019B, U+0264, U+1C8A, U+A7CD, U+A7CF, U+A7D3, U+A7D5, U+A7DB.
- `QString::toLower`가 `String.lowercase()`와 다른 단위 8개: U+1C89, U+A7CB, U+A7CC, U+A7CE, U+A7D2, U+A7D4, U+A7DA, U+A7DC.
- Qt는 끝 시그마를 하지 않는다(`ΟΔΟΣ`→`οδοσ`). Kotlin은 `ς`를 고른다.
- BMP 밖: Qt는 Unicode 16의 Garay 대소문자 대응을 알고 JVM 21은 모른다.

### 끝 시그마(`σ`/`ς`)

`kt::lowercase`는 대문자 시그마(`Σ`)를 `σ`로 할지 `ς`로 할지 유니코드의 `Final_Sigma` 규칙(Unicode Standard 3.13, SpecialCasing.txt)을 **근사해** 정한다. 앞뒤 글자가 Cased인지는 Qt의 대문자·소문자·제목 글자 범주로 정하고, 무시하는 문장부호(case-ignorable)는 `kt_text.cpp`에 적은 목록으로 정한다. 규칙 전체를 구현한 것이 아니고, 문맥에 따라 JVM의 답과 다르다.

**관찰한 차이.** 최종 리뷰가 오라클과 `kt::lowercase`에 같은 입력을 주고 본 것이다(Kotlin → C++, 이번에 다시 재지 않았다):

| 입력 | Kotlin(JVM) | C++ |
|---|---|---|
| `ΑΣ-Α` | `ασ-α` | `ας-α` |
| `ΑΣ:Α` | `ας:α` | `ασ:α` |
| `a-Σ` | `a-ς` | `a-σ` |
| `ΟΔΟΣ-ΑΘΗΝΑΣ (Live)` | `οδοσ-αθηνας (live)` | `οδος-αθηνας (live)` |

그 밖에 리뷰가 본 시그마 문맥 30가지는 두 답이 같았다. Task 2가 따로 관찰한 JVM의 답: `a-Σ`→`ς`, `a,Σ`→`σ`, `가Σ`→`σ`, `a_Σ`→`ς`.

**Q1에서는 드러나지 않는다.** `qt/core`에서 `kt::lowercase`/`kt::uppercase`를 부르는 다섯 곳은 그 결과를 시그마가 없는 정해진 집합(잡음 토큰, 가사 표지, 구간 접두어, 호스트 이름, 장치 이름)과 비교할 뿐이다. 최종 리뷰가 이것을 확인했다. 그래서 대조 종류 `S`의 `lower`는 입력에 `Σ`가 있는 사례에서만 두 답의 `ς`(U+03C2)를 `σ`(U+03C3)로 접어서 비교한다(`run.ps1`의 `$Folding`, 한 곳이다). 종류 `U`는 코드 포인트 하나짜리라 접지 않는다.

**Q2에서는 드러난다 — 사용자가 정할 것.** 다음 단계의 `engine` 코드는 사용자 글을 소문자로 바꾼 결과끼리 비교한다: `engine/.../lyrics/LyricsMatcher.kt`, 그리고 `engine/.../lyrics/google/GoogleSearchQuery.kt`의 캐시 키. 위 표처럼 Kotlin과 C++의 소문자가 다르면, 예를 들어 그리스어 제목의 매칭 점수나 캐시 키가 Kotlin 앱과 달라질 수 있다. Q2가 매처를 옮기기 전에 정해야 한다:

1. 매칭에서 `σ`와 `ς`를 같은 글자로 본다(Kotlin과 다르다는 것을 문서에 적고 대조에서 그만큼 접는다), 또는
2. JVM이 고르는 형태를 관찰로 재현한다(오라클로 더 많은 문맥을 물어 규칙을 정한다. JDK 소스는 보지 않는다).

### 대소문자 무시 영역 비교 (관찰한 규칙)

`startsWith`·`endsWith`·`contains`·`indexOf`·`regionMatches`·`replace`의 `CaseMode::Ignore`가 쓰는 비교다(`kt_text.cpp`의 `regionMatchesImpl`). 공개 문서가 정하는 것: 오프셋이 음수이거나 영역이 문자열 끝을 넘으면 거짓, 길이가 0 이하면 참, 두 글자는 같거나 대문자가 같거나 대문자의 소문자가 같으면 같은 글자(로캘과 무관), JDK 16부터는 코드 포인트로 비교한다. 문서가 정하지 않은 서로게이트의 처리는 Task 8b가 오라클에 탐침 25건을 물어 정했다. 그 25건의 입력과 JVM 답은 이제 `qt/tests/tst_kt_text.cpp`의 데이터 시험 `regionMatchesIgnoreCaseMatchesTheObservedJvmAnswers`에 있고(탐침 번호를 행 이름으로), `regionMatchesImpl` 위의 주석이 그 시험을 가리킨다. 관찰한 규칙은 이렇다.

- 두 영역을 각자의 위치로 걷는다. 두 단위가 같으면 양쪽이 한 칸씩 간다.
- 다르면 각 쪽이 자기 위치의 글자를 읽는다. 서로게이트 짝은 두 반쪽이 모두 **자기 영역 안에** 있을 때만 한 코드 포인트다. 위치가 높은 반쪽이면 앞으로 읽고(두 칸 간다), 낮은 반쪽이고 바로 앞이 높은 반쪽이면 뒤로 읽는다(한 칸 간다). 그 밖의 단위는 짝 없는 서로게이트를 포함해 그 자체가 한 글자다. 두 글자가 대소문자 무시로 같아야 하고, 각 쪽은 자기 글자만큼 간다. 그래서 한쪽은 두 칸, 다른 쪽은 한 칸 갈 수 있다.
- 어느 한쪽 영역이 끝나면 다른 쪽에 단위가 남아 있어도 참이다.

예(JVM의 답): `"😀".startsWith("\uD83D", true)`는 참(영역 밖의 낮은 반쪽은 짝을 만들지 않는다), `"\uD83D😀".startsWith("😀", true)`는 거짓, `"𐐀y".endsWith("𐐨", true)`는 거짓(영역 앞의 높은 반쪽도 짝을 만들지 않는다), `"\uD83D😀C".indexOf("😀B", 0, true)`는 0. 처음부터 양쪽을 코드 포인트로 나눠 비교하는 규칙(문서만 읽으면 떠오르는 것)은 탐침 5와 6의 JVM 답과 맞지 않는다. 이것은 Task 8b의 추론이다(그 규칙을 구현해 돌려 본 것이 아니다).

이력: 첫 판에서 대조가 서로게이트 버그를 두 번 찾아 고쳤고(Task 3의 종류 `S`, Task 7의 종류 `N` 10만 건), 그때 더한 회귀 시험 둘과 `S` 생성기 보강은 그대로 남아 새 판도 통과한다. 첫 판은 라이선스 이유로 Task 8b에서 읽지 않고 버린 뒤 다시 썼다(아래 "라이선스 메모").

## 대조와 디버그 빌드가 찾은 것

- Task 4, 종류 `D`, `-PerKind 100000 -Seed 4242`에서 1건(이전 실행): 입력 `LPT9\u00A0.…`에서 Kotlin은 `_`를 붙이지 않았고 C++은 붙였다. Kotlin의 `trimEnd(' ')`는 U+0020만 떼는데 C++이 `kt::trimEnd`(모든 공백)를 썼다. `kt::trimEndChars(…, u" ")`로 고쳤다. 10000건에서는 안 나타났다.
- 그 밖에 Task 3, 5, 6, 7의 `qt/core` 이식 코드 자체에서는 대조 불일치가 한 번도 나오지 않았다(각 Task 보고서).
- **이 패스의 종류 `U`**: `xgs::kt`에서는 아무것도 드러내지 않았다(1,112,064개 모두 같음). 처음 돌렸을 때 1건(U+FEFF)이 달랐는데 원인은 대조 도구 자신이었다: `xgs-diff unicodecase`가 한 코드 포인트짜리 문자열을 `QString::fromUcs4`로 만들었고, 그 함수가 맨 앞의 U+FEFF를 바이트 순서 표시로 보고 지워서 빈 문자열의 답을 찍었다. 도구가 문자열을 단위로 직접 만들게 고친 뒤 0이다. `xgs::kt`는 바꾸지 않았다.
- **이 패스의 디버그 빌드**: 범위 밖 접근이나 단언 실패는 없었다(위 "테스트"). 시간 한도 하나만 최적화 빌드 전용으로 바꿨다.

## 변이 시험 (Task 5~7, 이전 실행)

이식 코드를 일부러 틀리게 고쳐서 대조가 잡는지 본 것이다. 모두 시험 뒤 원상 복구했고 커밋에 없다. 이번 패스에서 다시 하지 않았다.

| Task | 변이 수 | 잡힘 | 비고 |
|---|---|---|---|
| 5 (제목 파싱) | 2 | 2 | `endsWith(…, true)`를 `lowercase(s).endsWith`로, `stripLeadingJunk`에서 서로게이트 조건 제거 |
| 6 (가사) | 9 | 9 | 변이 열 개를 생각했고 그중 M9는 관찰할 수 없는 차이라 돌리지 않았다 |
| 7 (로그 가리기) | 15 | 15 | |
| 합계 | 26 | 26 | |

생성기가 이 시험 때문에 강해진 곳: Task 6은 변이 M5·M7(제로폭 문자 제거와 줄바꿈 통일의 순서)을 처음 생성기가 못 잡았을 것이라고 판단해, 시험을 돌리기 전에 `\r`과 `\n` 사이에 문자가 끼는 홀수 줄바꿈을 3% 더했다(그 뒤 M5·M7 모두 잡혔다). Task 7은 변이가 아니라 첫 판 비교 함수의 `N` 불일치(위 "대소문자 무시 영역 비교"의 이력) 때문에 종류 `S`를 보강했다. 어느 쪽이든 문턱을 낮추거나 생성기를 좁히지 않았다.

## 고치지 않은, Kotlin 쪽 버그로 보이는 동작

아래는 모두 **Qt 판이 그대로 따른다.** 바꿀지는 사용자가 정한다. 바꾸려면 Kotlin 쪽을 먼저 바꾸고 대조를 다시 돌린다.

1. **`TitleParser`: BMP 밖 글자로 시작하는 제목의 첫 글자들이 사라진다.** `stripLeadingJunk`가 서로게이트 단위를 모두 이모지 같은 잡것으로 본다. Task 8에서 오라클로 쟀다: `parseTitle("𝒜rtist - Song")`(채널 없음) → 가수 `rtist`, 제목 `Song`, 신뢰도 HIGH. 특성 테스트 `leadingNonBmpLettersAreDroppedLikeEmoji`가 이것을 고정한다. CJK 확장 B 한자로 시작하는 가수 이름도 같다고 Task 5가 적었다(그 한자 예는 Task 5의 기록이고 다시 재지 않았다).
2. **`TitleParser`: 같은 잡음 목록을 두 곳이 다른 비교로 본다**(Task 5 기록, 다시 재지 않았다). `stripTrailingNoiseToken`은 글자별 `ignoreCase`, `stripTrailingNoiseParen`은 `lowercase()` 뒤 비교다. 그래서 `MUSİC VIDEO`는 맨몸으로 끝에 있으면 지워지고 괄호 안에 있으면 남는다고 Task 5가 적었다.

`LogRedaction` 다섯 가지(3~6은 Task 7 기록이고 다시 재지 않았다. 입력과 답은 Task 7 보고서에서 옮겼다):

3. **단어에 붙은 드라이브 경로는 가려지지 않는다.** `Access is deniedC:\Users\me\001 A - B.mp3`에 `redactPaths`를 쓰면 그대로 남는다(`https://`를 피하려는 규칙 때문으로 보인다). 숫자 뒤도 같다: `errno2C:\Users\me\a.mp3`.
4. **파일 이름 안의 ` ->`에서 경로가 끝나고 뒤가 새어 나온다.** `C:\Music\A -> B.mp3` → `<경로> -> B.mp3`.
5. **확장자가 둘인 이름은 일부만 가려진다.** 알려진 이름이 `001 A - B.f251.webm`이면 지워지는 것은 그것과 `001 A - B.f251`뿐이다. 텍스트의 `001 A - B.m4a`나 `001 A - B`는 남는다.
6. **대소문자 무시 비교가 글자 단위라서 길이가 바뀌는 대응을 못 찾는다.** `Straße`와 `STRASSE`는 같은 이름으로 취급되지 않는다.
7. **`\\host`만 있고 뒤에 부분이 없으면 호스트 이름이 남는다.** `redactPaths("cannot reach \\MYNAS today")`는 입력을 그대로 돌려주고, `redactPaths("\\host -> \\h2\s")`는 `\\host -> <경로>`다(이 패스에서 오라클과 `xgs-diff`로 쟀다, 두 쪽 같음).

3~7은 사용자의 로그에 경로나 파일 이름이 새어 나갈 수 있는 쪽이다.

**최종 리뷰에서 본 것, 다시 재지 않음** (판단하지 않은 Kotlin 동작. Qt 판은 대조상 같다):

- `C:\\\x`(드라이브 뒤에 구분자가 셋)는 가려지지 않는다.
- 첫 가사 줄이 30000 단위를 넘으면 `extract`가 null이다.
- 이름이 U+200B 하나뿐인 폴더 이름이 그대로 나온다.
- `COM¹`은 장치 이름으로 보지 않아 `_`가 붙지 않는다.

## 옮기지 않은 것

- `shared`의 `api/ApiModels.kt`(HTTP로 주고받는 형식)와 그 테스트 18개. Qt 판에는 HTTP도 JSON 전송 형식도 없어서, 그 테스트(직렬화 모양, SSE 이름)가 지킬 대상이 사라진다. 이 결정은 사용자가 계획을 승인할 때 확인했다. 그래서 Q1의 "같은 이름으로 모두 통과"는 193개(15 + 39 + 41 + 66 + 32)가 대상이다.
- `ApiModels` 안의 도메인 형(`ResolvedItem`, `JobOptions`, `JobEvent`, `LyricsOutcome`, `ToolsStatus` 등)은 그것을 쓰는 Q2(`engine`)에서 평범한 C++ 구조체로 만든다.
- 설계 §4가 대조 대상으로 든 yt-dlp 출력 파싱과 LRCLIB 매칭은 `engine`에 있어 Q2에서 한다.

## 라이선스 메모

프로젝트는 MIT이고(루트 `LICENSE`, Task 1) OpenJDK 파생 코드는 둘 수 없다는 판정이 있다. 이 절은 과정의 기록이고 법적 의견이 아니다.

- **제거한 것.** Task 2는 처음에 JDK 21의 단어 경계 반복자(break iterator) 알고리즘과 단어 경계 표, 그리고 JDK의 끝 시그마 선택 부분을 옮겨 `qt/core/text`에 넣었다. 리뷰 전에 둘 다 제거했고(표 3074줄→694줄, 종류 `W`와 오라클의 `breaktable` 모드 포함), 끝 시그마는 유니코드 규칙의 근사로 바꿨다. 지금 `forbidden-calls.ps1`이 `qt/`(`qt/spike` 제외)에서 JDK 내부 이름을 찾고 0건이다. 이 스크립트는 이름으로만 찾으므로 이름 없이 옮겨 쓴 코드는 잡지 못한다.
- **`kt_text.cpp`의 나머지.** 클린룸으로 다시 쓴 것은 `regionMatchesImpl`과 그 도우미(`sameIgnoringCase`, `characterInRegion`)뿐이다. `kt_text.cpp`의 나머지(`lowercase`, `uppercase`, 끝 시그마 판정, Cased·case-ignorable 판정, 코드 포인트 도우미, 그 밖의 문자열 함수)는 Task 2에서, 바로 앞서 위의 단어 경계 반복자와 끝 시그마 선택의 이식을 쓰고 지운 같은 에이전트가 썼다. 내용으로 보면 이 함수들은 공개된 유니코드 규칙과 공개 API의 동작을 구현한다. 최종 리뷰는 그 안에서 JDK에 고유한 표현을 찾지 못했다. 이것은 리뷰어의 판단이고 보증이 아니다.
- **Kotlin 표준 라이브러리를 따른 곳.** `kt::replace`와 `kt::indexOf`의 대소문자 무시 루프는 Kotlin 표준 라이브러리(kotlin-stdlib, Apache-2.0)의 알고리즘을 따른다. 두 곳의 주석과 저장소 루트의 `THIRD-PARTY-NOTICES.md`에 출처를 적었다. 같은 파일에 `kt_tables.inc`의 유니코드 문자 데이터(Unicode License V3) 고지도 있다. Qt와 WebView2의 고지는 앱을 포장할 때 더한다.
- **JVM을 통해 얻은 유니코드 데이터.** `kt_tables.inc`의 표(문자 속성 48개, 특수 대소문자, BMP 밖 대응)는 JVM의 공개 API에 문자마다 물어서 얻은 유니코드 문자 데이터(Unicode Character Database)라는 판정이다. JDK 내부 자료인 `isCased` 구간 표는 그 판정에 들지 않아 지웠다.
- **대소문자 무시 영역 비교를 다시 썼다 (Task 8b).** `regionMatchesImpl`과 그 도우미는 지금 클린룸 방식으로 다시 쓴 것이다. 옛 함수와 도우미는 화면에 띄우지 않고 `sed`로 지웠고, 다시 쓴 에이전트는 옛 코드도 JDK 소스도 읽지 않았다고 진술했다. 근거는 `String.regionMatches`(와 Kotlin의 `regionMatches`·`startsWith`·`endsWith`·`indexOf`·`contains`·`replace`)의 공개 API 문서, 그리고 오라클로 물어 관찰한 JVM 답이다. 탐침 25건의 입력과 JVM 답은 이제 `tst_kt_text.cpp`의 데이터 시험에 있다. 그 밖에 `tst_kt_text.cpp`에 있던 JVM 답(회귀 시험)과 대조 `S`·`N`의 결과를 판정으로 썼다. **명세가 흘러든 길:** 다시 쓴 에이전트는 `tst_kt_text.cpp`와 `gen.cpp`를 읽었다. 그 주석은 동작을 말로 설명하고, JDK 소스를 읽은 에이전트들이 썼다. 이것은 기능의 설명이지 코드가 아니다. 이번 최종 리뷰 반영에서도 `regionMatchesImpl`의 몸체는 바꾸지 않았다(위 주석이 가리키는 곳만 바꿨다).
- **로컬 이력.** 첫 판 비교 함수는 push하지 않은 Q1의 로컬 커밋에만 있다. 계획: push 전에 Q1의 커밋을 합친다(squash). 그 밖에, 앞서 이미 합치기로 대체된 로컬 커밋 넷(단어 경계 반복자 이식, 그 표, 처음 주석이 든 것)은 `main`에서 닿지 않지만 로컬 `.git`의 객체 저장소와 reflog에는 만료될 때까지 남아 있다. 보통의 `git push origin main`은 그것들을 보내지 않는다. 그러나 폴더를 복사하거나, 번들을 만들거나, 미러 push를 하면 함께 간다. 그것들을 만료시키고 지울지(reflog expire, prune)는 사용자가 정한다. git이 무시하는 `.superpowers/` 작업 파일은 옛 코드를 인용하므로 절대 커밋하지 않는다.
- **한계.** AI 모델은 자신이 무엇을 기억하고 무엇을 기억하지 않는지 증명할 수 없다. 그래서 위의 것은 과정상의 안전장치이지 법적 보증이 아니고, 이 보고서는 법적으로 충분한지 판단하지 않는다.

## Qt 재빌드 시간

Task 1에서 `qt/vcpkg.json`을 바꿔 Qt를 한 번 다시 빌드했다: 17:16:25 ~ 17:42:34, 약 26분(구성 1555초). 스파이크 때(약 89분)와 달리 의존성은 vcpkg 바이너리 캐시에서 왔고 qtbase만 새로 빌드됐다(Task 1 보고서, `qt/README.md`). 이 패스의 디버그 빌드 폴더는 캐시에서 복원만 했다(24초, 구성 전체 60.5초). 우리 코드만 처음부터 다시 빌드하고 ctest까지 돌리는 데 이 패스에서 34초가 걸렸다(위 "빌드").

## 계획과 달라진 것

- **실행 파일 위치.** 계획은 `build/qt/tst_*.exe`를 가정했는데 첫 구현은 `build/qt/tests/`에 만들었다. 리뷰가 지적해 `CMAKE_RUNTIME_OUTPUT_DIRECTORY`를 `build/qt`로 고쳤다(Task 1).
- **Task 3에 더한 것.** 계획에 없던 `S` op(`ci-starts`, `ci-ends`, `ci-contains`, `ci-index`, `ci-replace`, `eqic`, `index`, `replace`), 필드 수가 모자라면 `?`로 답하는 가드, `/wd4702`를 `xgs_add_test`로 모은 것. 이 op들이 첫 판 비교 함수의 서로게이트 버그 하나를 찾았다. 최종 리뷰 반영에서 `ci-starts-from`을 더해 op가 15개가 됐다.
- **`S`의 문턱.** op가 6개에서 14개(지금 15개)로 늘어서 op마다 최소 사례 수를 10%에서 5%로 낮췄다(프로토콜 표의 "op마다 1000건 이상"은 10000건 기준이라 op가 한 종류를 나누면 못 지킨다). 불리언 op는 두 답이 각각 15% 이상, `ci-index`는 -1이 아닌 답이 20% 이상, `blank`는 두 답이 각각 5% 이상.
- **종류 `W`를 더했다가 뺐다.** Task 2가 끝 시그마 때문에 옮긴 단어 경계를 대조하려고 `W`를 더했고, 라이선스 판정으로 그 이식과 함께 지웠다. 기본 종류는 `XSW`였다가 `XS`로, 다음에 `XMSCFDGTLYRN`으로, 최종 리뷰 반영에서 `U`가 더해져 `UXMSCFDGTLYRN`이 됐다.
- **`S`의 `lower` 비교.** 입력에 `Σ`가 있을 때만 시그마 형태를 접어서 비교한다(위).
- **`X`의 열 순서.** 코드와 계획 Task 2는 `isWhitespace isLetter isLetterOrDigit uppercaseChar lowercaseChar`다. 이 계획의 대조 규약 표에는 `isWhitespace isLetterOrDigit isLetter`로 적혀 있어 표가 틀렸다. 두 쪽이 같은 순서라 결과에는 영향이 없다.
- **100000건 실행.** 계획은 1만 건 세 번이었다. controller가 모든 종류를 10만 건으로 한 번 더 돌리라고 했다. Task 8·8b에서는 메모리 때문에 묶음 다섯으로 돌렸고, 최종 리뷰 반영에서 `run.ps1`을 종류별로 처리하게 고쳐 한 번에 돌린다.
- **테스트 수.** 계획은 196개였다. 실제는 213개 함수(데이터 행을 세면 237, + Qt의 init/cleanup 14)다(위 "테스트").
- **특성 테스트.** 이름 대조가 허용하는 추가 이름이 둘에서 셋이 됐다(`leadingNonBmpLettersAreDroppedLikeEmoji`).
- **점검 스크립트가 셋이 됐다.** 계획의 `test-names.ps1`에 더해 `forbidden-calls.ps1`을 만들어 금지 호출 검사를 반복할 수 있게 했고, JDK 내부 이름 검사를 넣었다. 최종 리뷰 반영에서 금지 목록을 넓히고(대소문자·범주·접기·정규화·로캘) JDK 이름 검사를 모든 텍스트 파일로 넓혔다.
- **Task 8에서 고친 작은 것.** (a) `S`의 `blank`에 두 답의 하한(각 5%), (b) 빈 사례 줄이 있으면 `run.ps1`이 던지지 않고 실패 메시지를 낸다, (c) `N` 생성기가 "이름이 다른 이름의 앞부분" 모양을 더 자주 낸다(문턱 3%는 그대로), (d) 누설 방향 수를 진단으로 표시, (e) `Final_Sigma` 설명을 "따른다"에서 "근사한다"로, (f) `filename_formatter.cpp`에 `#include <string>`, (g) 특성 테스트 하나.
- **커밋 꼬리말.** 계획은 `Co-Authored-By: Claude Fable 5.2`를 적었으나 하위 세션들은 각자의 세션 줄을 썼다. 줄이 섞여 있다. push 전 합치기 때 정리할 수 있다.
- **대소문자 무시 영역 비교를 다시 썼다 (Task 8b).** 계획에 없던 일이다. 첫 판이 JDK 소스에 기대어 쓰였다는 것이 문제가 되어, 옛 함수와 도우미를 읽지 않고 지우고 공개 문서와 관찰한 JVM 답만으로 다시 썼다(위 "라이선스 메모"). 시험, 생성기, 문턱은 바꾸지 않았다. 다시 쓰는 동안 따로 돌린 실행(Task 8b): 종류 `S` 10만 건 seed 20261115·777·31337, 종류 `N` 10만 건 seed 20261115·777, 모두 불일치 0.
- **최종 리뷰 반영(이 패스).** 계획에 없던 일이다: `CaseMode`와 인자 순서, `xgs::redaction`, 표 정렬 `static_assert`, 탐침 25건의 데이터 시험, 대조 보강(`U`, `ci-starts-from`, 좁힌 시그마 접기, 오래된 jar 검사, 종류별 처리, 측정값 출력), 두 점검 스크립트 강화, 디버그 프리셋 `qt-debug`, 프리셋의 `$env{VCPKG_ROOT}`, `THIRD-PARTY-NOTICES.md`와 출처 주석, 디버그에서 1MB 추출 시험의 시간 한도를 최적화 빌드 전용으로 한 것.

## 확인하지 못한 것

- **BMP 밖 글자의 대소문자 무시 비교.** 대조 알파벳에 있는 BMP 밖 대소문자 글자는 Deseret(U+10400/U+10428) 한 쌍뿐이고 이모지가 더해진다. Osage, Adlam 같은 글자의 대소문자 무시 비교는 대조하지 않았고, 탐침에 Adlam 한 쌍(U+1E900/U+1E922)이 있을 뿐이다. 종류 `U`가 그 글자들의 `lowercase`/`uppercase`(표의 520개 대응)를 코드 포인트 전체에서 확인했지만, 비교(`regionMatchesImpl`)를 그 글자들로 시험한 것은 아니다.
- **비교 규칙의 탐침은 25건이다.** 다시 쓴 비교 함수의 서로게이트 규칙은 탐침 25건, `tst_kt_text`의 JVM 답, 대조 `S`·`N`의 무작위 사례로만 정하고 확인했다. 사례가 닿지 않는 모양에서 JVM과 다를 가능성은 남는다.
- **다른 Qt·JVM 버전.** Qt 6.11.3과 JVM 21.0.9에서만 돌렸다. 둘 중 하나가 바뀌면 48개 예외 목록이 달라질 수 있어 `gen-tables.ps1`을 다시 돌리고 `U`·`X`·`S`를 다시 돌려야 한다. 그 점검 자체는 돌려 보지 않았다.
- **끝 시그마.** 근사이고 대조에서 접어서 비교한다. Q2 전에 사용자가 정할 일이다(위 "끝 시그마").
- **`ApiModels`와 그 테스트 18개.** 옮기지 않았다(위).
- **다른 컴파일러, Windows 10.** MSVC 2022만 썼고 이 PC는 Windows 11 하나다. 디버그 빌드는 이 패스에서 돌렸다.
- **터키어 로캘.** 단위 시험 `lowercaseAndUppercaseDoNotDependOnTheLocale`은 통과하지만, 그 시험이 `setlocale`의 반환값을 확인하지 않아서 이 PC에서 터키어 로캘이 실제로 켜졌는지는 확인하지 못했다.
- **seed가 적다.** 이 패스의 관문에서 1만 건은 seed 셋, 10만 건은 seed 하나(424242)로만 돌렸다(Task 8b에서 `S`는 10만 건 seed 셋, `N`은 둘을 더 돌렸다). 이전 Task들의 다른 seed 실행은 이전 실행이다.
- **단언의 같음.** `test-names.ps1`은 이름만 본다. 193개 테스트 각각의 단언이 Kotlin과 같은지는 Task별 리뷰가 보았고 다시 보지 않았다.
- **통계는 근사다.** 여러 통계는 `run.ps1`에서 규칙을 다시 쓴 근사(예: 가짜만 든 입력, 줄바꿈 종류)로 센다. 답의 비교는 정확하다.
- **긴 입력의 성능은 두 시험뿐이다.** 1MB 설명란(`extractOnOneMegabyteDescriptionFinishesQuickly`)과 부분 5000개 경로(`redactPathsWithFiveThousandPartsFinishesQuickly`), 둘 다 최적화 빌드에서 2초 문턱이다. 이번 실행에서 `tst_lyrics_extractor` 전체가 0.42초(ctest, 릴리스)였다. 디버그에서는 1MB 추출이 5.5초 걸린다.
- **Q0 보고서에서 아직 열려 있는 것.** 이 단계는 건드리지 않았다: Windows의 실제 150%·200% 배율 확인, Defender·SmartScreen이 켜진 PC에서의 반응, 현행 exe와 나란히 비교, ffmpeg 자손까지 포함한 프로세스 정리, 시작 시간을 `Process.Start`부터 재는 것, `Report::put`이 `QSaveFile::commit()`의 반환값을 버리는 문제, `JobProcess`가 Qt 내부 수명 동작에 기대는 문제.

## 남긴 것

고치지 않고 남긴 작은 지적들이다(원장의 "minor (deferred)"와 최종 리뷰가 미룬 것).

- `webview2`는 매니페스트(`qt/vcpkg.json`)에만 있고 Q3까지 안 쓴다. `find_package`가 `Core Test`만 찾는다(필요할 때 늘린다). `if(MSVC)` 보호 없음.
- `unhex`(`codec.h`)가 16진수 해석 실패를 무시한다.
- 로캘 시험이 `setlocale`의 반환값을 보지 않는다.
- `Final_Sigma`의 case-ignorable 목록에 WordBreak MidLetter 문자 일부가 빠져 있다(설명은 "근사"로 낮췄고 목록은 그대로).
- `C`의 문턱에 "alsoVideoId가 있는 `P`"의 하한이 없다. `Oracle.java`에 짧은 줄 가드가 없다.
- 통계가 근사다: Task 4의 "자름 자리의 서로게이트"는 자르지 않은 한계 근처 텍스트도 세고, Task 6의 줄바꿈 종류 통계는 홀수 줄바꿈을 두 번 세며 `$sawFake`가 이름보다 넓게 세고, Task 7의 가짜만 든 사례 통계도 근사다. Task 5의 `$QuoteClass`는 백틱을 포함한다.
- `N`은 빈 이름 목록을 한 번도 보내지 않는다.
- 시험 하나에 `\u0000`/`\u001F`/`\u007F`가 `\x`가 아닌 universal-character-name으로 들어 있다(Task 4). `lyrics_extractor.cpp`의 주석에 "trimmed"라는 낱말이 있다(금지 호출 패턴은 `trimmed(`라 걸리지 않는다).

최종 리뷰 반영에서 해결해 이 목록에서 뺀 것: 표의 정렬 확인(`static_assert`), `C`·`S` 통계의 측정값 출력, 10만 건 한 번 실행.

## Q2에 넘길 것

- **끝 시그마를 정한다.** `σ`/`ς`를 매칭에서 같다고 볼지, JVM의 선택을 관찰로 재현할지(위 "끝 시그마"). `LyricsMatcher.kt`와 `GoogleSearchQuery.kt`의 캐시 키를 옮기기 전에.
- **`xgs::kt`에 더할 것.** 매처를 옮기기 전에 코드 포인트 단위 `isLetterOrDigit`이 필요하고, NFKC 정규화와 정규식의 방침을 정해야 한다. Qt와 JVM의 유니코드 판이 달라서 Qt의 `normalized()`나 `QRegularExpression`을 그대로 쓰면 JVM과 답이 갈릴 수 있다(둘 다 지금 `forbidden-calls.ps1`이 이식 코드에서 막는다).
- **`CaseMode`를 쓴다.** 대소문자 무시는 `kt::…(…, from, kt::CaseMode::Ignore)`. 위치 자리에 `bool`을 주면 컴파일되지 않는다.
- **디버그 테스트를 돌린다.** 관문에 `ctest --test-dir build/qt-debug --output-on-failure`가 들어 있다(`qt/README.md`).
- **`forbidden-calls.ps1`을 `xgs::kt`와 함께 넓힌다.** 최종 리뷰의 제안이다: `xgs::kt`가 새 의미(정규화, 코드 포인트 범주 등)를 얻으면 그에 해당하는 Qt 함수를 금지 목록에 더한다.
- **대조 종류를 더하는 법.** 생성기(`gen.cpp`의 `registerGenerators()`에서 `registerKind`), C++ 답(`main.cpp`의 `registerAnswers()`에서 `registerAnswer`, 상수는 `registerConstants()`), Kotlin 답(`Oracle.java`의 `switch (part[0])`에 `case`), 통계 문턱(`run.ps1`의 `$Statistics`; 항목이 없으면 그 종류가 실패한다. 표 종류는 `Table`). 생성기는 이모지, 짝 없는 서로게이트, null·빈 문자열·공백뿐 문자열(각 5% 이상), 긴 입력을 섞어야 한다. 문턱을 못 넘으면 문턱이 아니라 생성기를 고친다.
- **10만 건 실행.** 새 종류는 1만 건뿐 아니라 10만 건으로도 돌린다(이번 단계에서 1만 건에서는 없던 불일치가 둘 나왔다). 이제 모든 종류를 한 번에 돌릴 수 있다.
- **JDK 소스를 보지 않는다.** 차이는 오라클(JVM의 관찰된 답)로만 정한다.
- **`gen-tables.ps1`.** Qt나 사이드카 이미지의 JVM이 바뀌면 다시 돌리고 `U`·`X`·`S`를 돌린다. `shared`가 바뀌면 사이드카 이미지를 다시 만든다(`run.ps1`이 오래된 jar를 거부한다).
- **도메인 형.** `ApiModels`의 `ResolvedItem`, `JobOptions`, `JobEvent`, `LyricsOutcome`, `ToolsStatus` 등을 `engine`을 이식하면서 평범한 구조체로 만든다. `ApiModels`의 직렬화 테스트는 옮기지 않는다.
- **위의 Kotlin 동작 7가지(와 최종 리뷰가 본 넷).** Qt 판은 지금 그대로 따른다. 사용자가 바꾸기로 하면 Kotlin 쪽과 대조 양쪽을 같이 바꾼다.
- **Q0에서 열려 있는 것**(위 "확인하지 못한 것"의 마지막 항목): 실제 배율, Defender·SmartScreen, ffmpeg 자손 정리, 저장 반환값 확인, 시작 시간 측정 방식.

## 판정

**Q1 관문(설계 §4)은 충족됐다.** 이식 대상 `shared` 테스트 193개는 같은 이름으로 모두 통과하고(이름 대조에서 빠진 것 0), 대조는 네 번의 실행(1만 건 seed 셋, 10만 건 seed 하나, 모두 한 번의 호출로 모든 종류)에서 모든 종류가 불일치 0이며 통계 문턱도 넘었다. 릴리스와 디버그 테스트가 모두 통과했다. 이 실행은 최종 리뷰를 반영한 커밋 `Qt: 최종 리뷰 반영 — CaseMode, 대조 보강, 디버그 빌드, 고지`에서 처음부터 돌린 것이다.

이 진술이 다루지 않는 것: `ApiModels`와 그 테스트 18개, `engine`의 yt-dlp 출력 파싱과 LRCLIB 매칭(Q2), 끝 시그마(`σ`/`ς`) 형태(근사이고 대조에서 접어서 비교한다), BMP 밖 대소문자 글자의 대소문자 무시 비교 대부분, 다른 Qt·JVM 버전, 단언이 Kotlin과 같은지의 재확인, 다시 쓴 비교가 법적으로 충분한지(위 "라이선스 메모"), Q0의 열린 확인들. 정해야 할 것(사용자): 끝 시그마(Q2 전), Kotlin 쪽 동작들을 바꿀지, `main`에서 닿지 않는 로컬 커밋을 만료·정리할지. 계획된 것: push 전에 Q1의 커밋을 합친다.

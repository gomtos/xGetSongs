# xGetSongs Qt 판

C++20 / Qt 6 Widgets로 다시 쓰는 xGetSongs. 이 폴더가 제품 빌드이고, `qt/spike/`는 버려도 되는 스파이크다.

## 필요한 것

- Visual Studio 2022 (MSVC, 함께 오는 CMake와 Ninja)
- vcpkg (저장소 밖). 프리셋은 그 위치를 환경 변수 `VCPKG_ROOT`에서 읽는다. 이 PC에서는 `C:\dev\vcpkg`. 기준선 커밋 `4702e381dbbd9b54d3f7e1eb565cd8981c1814cd`(`qt/vcpkg.json`의 `builtin-baseline`)
- Qt 6.11.3 (vcpkg 포트 `qtbase`), 트리플렛 `x64-windows-static`(Qt와 CRT 정적 링크)
- 지원 Windows: Windows 10 이상 (`_WIN32_WINNT=0x0A00`)

## 공통 명령

저장소 루트에서 실행한다. vcpkg 의존성은 매니페스트 모드라 빌드 폴더의 `vcpkg_installed`(`build/qt/vcpkg_installed`)에 설치된다. 첫 명령은 그 호출에만 `VCPKG_ROOT`를 정한다(`&&` 앞에 공백을 두지 않는다. 두면 값 끝에 공백이 붙는다).

```
cmd /c "set VCPKG_ROOT=C:\dev\vcpkg&& call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --preset qt-release -S qt && cmake --build build/qt"
ctest --test-dir build/qt --output-on-failure
pwsh qt/tools/differential/run.ps1 -Kinds <글자들> [-PerKind 10000] [-Seed 20261115]
```

세 번째는 대조 도구다(아래). 관문 점검(아래 "Q1 관문")은 이 명령들에 두 스크립트를 더한다. 실행 파일(테스트 포함)은 `build/qt/` 바로 아래에 생긴다(`CMAKE_RUNTIME_OUTPUT_DIRECTORY`).

### 디버그 빌드

프리셋 `qt-debug`는 `build/qt-debug`에 Debug(`/MTd`)로 빌드한다. Qt의 단언(`Q_ASSERT`)과 `QString`/`QList` 범위 검사가 켜져 있어서, 릴리스에서는 조용히 지나가는 범위 밖 접근이 테스트를 멈춘다. 의존성은 그 폴더의 `vcpkg_installed`에 바이너리 캐시에서 다시 설치된다. 관문에서 릴리스 테스트 다음에 돌린다.

```
cmd /c "set VCPKG_ROOT=C:\dev\vcpkg&& call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --preset qt-debug -S qt && cmake --build build/qt-debug"
ctest --test-dir build/qt-debug --output-on-failure
```

대조 도구와 두 관문 스크립트는 `build/qt`(릴리스)의 실행 파일을 쓴다.

## Q1 관문

Q1(`shared` 이식)의 관문은 설계 §4의 두 조건이다: 이식 대상 Kotlin 테스트가 같은 이름으로 모두 통과하고, 대조에서 불일치가 0이다. 빌드한 뒤 저장소 루트에서 차례로 돌린다. 하나라도 실패하면 C++을 고치고 처음부터 다시 돌린다(Kotlin 코드, 기대값, 문턱은 바꾸지 않는다).

```
ctest --test-dir build/qt --output-on-failure
ctest --test-dir build/qt-debug --output-on-failure
pwsh qt/tools/test-names.ps1
pwsh qt/tools/forbidden-calls.ps1
pwsh qt/tools/differential/run.ps1 -PerKind 10000 -Seed 20261115
pwsh qt/tools/differential/run.ps1 -PerKind 10000 -Seed 1
pwsh qt/tools/differential/run.ps1 -PerKind 10000 -Seed 987654321
pwsh qt/tools/differential/run.ps1 -PerKind 100000 -Seed <아무 값>
```

- `test-names.ps1`: `qt/tools/test-classes.json`에 적힌 Kotlin 테스트 클래스마다 `@Test` 함수 이름과 `build/qt/<exe>.exe -functions`의 이름을 비교한다. 이 파일은 배열이고 항목마다 `kotlin`(저장소 기준 Kotlin 테스트 파일), `exe`(Qt 테스트 실행 파일 이름), `added`(Kotlin에 없지만 일부러 더한 이름)를 둔다. 테스트 클래스를 이식하면 여기에 항목을 더한다. 빠진 이름이 있거나, 그 클래스의 `added` 밖의 이름이 더해졌거나, `added`의 이름이 그 실행 파일에 없거나, Kotlin 테스트 파일에서 이름이 하나도 안 나오면 종료 코드 1이다. 지금 `added`는 Q1의 세 이름(`extractOnOneMegabyteDescriptionFinishesQuickly`, `redactPathsWithFiveThousandPartsFinishesQuickly`, `leadingNonBmpLettersAreDroppedLikeEmoji`)과 Q2a의 `readsATenMegabyteInfoFileQuickly`다. `tst_kt_text`(Kotlin 짝이 없음)는 대조하지 않는다.
- `forbidden-calls.ps1`: `qt/core`(`kt_text.*` 제외)에서 `QChar`/`QString`의 공백·글자·숫자·문장부호·대소문자·대소문자 접기·`trimmed`·`simplified`·`QRegularExpression`·`QLocale`·`localeAwareCompare` 호출(앞에 `kt::`가 붙은 것은 제외)을 찾고(Q1 목록), `qt/core` 전체(`kt_text.*`와 `qt/core/json` 포함)에서 Qt의 JSON 클래스(`QJsonDocument`·`QJsonObject`·`QJsonValue`·`QJsonArray`), `toDouble(`·`toInt(`·`toLongLong(`, 실수 형식(`'e'`·`'f'`·`'g'`)을 준 `QString::number(`, 정규화 `normalized(`, 경로 정리(`cleanPath`·`absoluteFilePath`·`absolutePath`·`canonicalPath`·`canonicalFilePath`)를 찾는다(Q2a 목록; 예외는 `kt::nfkc`를 구현하는 `kt_text.cpp`의 `normalized(` 하나). 형식 인자 없이 실수를 넘긴 `QString::number(`는 정수와 구별할 수 없어 리뷰에서 본다. 그리고 `qt/`(`qt/spike` 제외)의 모든 텍스트 파일(확장자와 상관없이, NUL 바이트가 든 파일은 이진으로 보고 건너뛰며 알린다)에서 JDK 내부 클래스·메서드 이름(스크립트의 `$internals`에 있다. 이 README에는 적지 않는다. 적으면 스크립트가 걸린다)을 찾는다. 하나라도 있으면 종료 코드 1이다. 이 프로젝트는 MIT 라이선스라서 OpenJDK에서 나온 코드를 넣지 않는다.
- 사례 10만 건(`-PerKind 100000`)은 1만 건에서는 안 나오던 불일치를 드러낸 적이 있다(Task 3의 `regionMatches`, Task 4의 `trimEnd(' ')`). 모든 종류를 한 번은 이 크기로 돌린다. `run.ps1`은 종류를 하나씩 돌리고 다음 종류 전에 앞 종류의 데이터를 놓으므로 한 번에 모든 종류를 돌려도 된다(전에는 사례를 다 들고 있어서 묶어 돌려야 했다).
- 깨끗한 빌드: `build/qt/vcpkg_installed`(Qt)를 지우지 말고 우리 쪽만 지운다: `cmake --build build/qt --target clean`.

## 켠 qtbase 기능

`default-features: false`에 `gui`, `widgets`, `network`, `thread`, `concurrent`, `testlib`, `freetype`, `harfbuzz`, `png`, `doubleconversion`, `pcre2`. 기능 이름은 모두 `C:\dev\vcpkg\ports\qtbase\vcpkg.json`에 그대로 있었다. 그 밖의 의존성은 `webview2`.
`openssl`, `sql*`, `dbus`, `opengl`, `icu`는 켜지 않았다(TLS는 Schannel). 스파이크의 목록에 `concurrent`와 `testlib`를 더해 Qt를 한 번만 다시 빌드한다.

## 이번 재빌드에 걸린 시간

약 26분 (구성 1555초; 17:16~17:42). 스파이크 때(약 89분)와 달리 의존성은 바이너리 캐시에서 왔고 qtbase만 새로 빌드됐다.

## 구성

- `core/`: 정적 라이브러리 `xgscore`(Qt Core만, 네임스페이스 `xgs`)
  - `core/text/`: `xgs::kt`, Kotlin/JVM 21의 문자·문자열 의미(공백, 글자, 대소문자, trim, 대소문자 무시 비교, 코드 포인트 글자 판정, NFKC, `toDoubleOrNull`, 정규식 `\s`의 ASCII 공백). 이식 코드는 `QChar`/`QString`의 공백·대소문자 함수 대신 이것을 쓴다. Kotlin의 `ignoreCase`는 `kt::CaseMode`(`Sensitive`/`Ignore`)이고 인자 순서는 Kotlin과 같다: `(…, from = 0, mode = CaseMode::Sensitive)`
  - `core/log/`: `xgs::redaction`(`LogRedaction.kt`의 이식. 로그 기능 자체는 뒤 단계에서 온다)
  - `core/json/`: `xgs::json`, kotlinx.serialization의 기본 `Json.parseToJsonElement`가 받아들이는 것을 똑같이 받아들이는 읽기 전용 파서(따옴표 없는 `NaN` 같은 리터럴, 중복 키는 뒤 값이 앞 자리에, 배열 항목 뒤의 `]`+값은 쉼표처럼). 동작은 대조 도구로 관찰한 Kotlin의 답으로만 정했다(`json.h` 머리 주석). 배열이 1000개를 넘게 열리면 오류다(Kotlin은 그 사이 어딘가에서 JVM 상태에 따라 스택이 넘친다). 읽기·복사·소멸·덤프 모두 재귀가 없다
- `tests/`: Qt Test. `xgs_add_test(<이름> <소스…>)`로 실행 파일 하나를 만들고 ctest에 등록한다
- `tools/differential/`: 대조 도구. `Oracle.java`(Kotlin 쪽, 사이드카 이미지의 JVM과 jar)와 `xgs-diff.exe`(C++ 쪽)에 같은 사례를 주고 답을 줄마다 비교한다

## 대조 도구

`run.ps1`이 종류마다 사례를 만들고(`xgs-diff gen`), 두 쪽의 답을 비교하고, 종류별 통계 문턱을 본다(측정값도 찍는다). 줄 수가 다르거나, 한 줄이라도 다르거나, 문턱을 못 넘거나, 통계가 정의되지 않은 종류가 있으면 종료 코드 1이다. 기본 종류는 모든 종류(`UXMSCFDGTLYRNBZHOJEQVKA`)다. 시작하기 전에 오라클이 쓰는 `server/build/sidecar-image/lib/shared-jvm.jar`가 `shared/src/commonMain`의 가장 새 파일보다, 또는 `lib/engine.jar`가 `engine/src/main`의 가장 새 파일보다 오래됐으면 사이드카 이미지를 다시 만들라는 말과 함께 실패한다(`run.ps1`의 `$OracleJars`).

- `U`: U+0000~10FFFF의 각 코드 포인트(서로게이트 D800~DFFF 제외, 1,112,064개)마다 그 코드 포인트 하나짜리 문자열의 `lowercase()`와 `uppercase()`

- `X`: 0000~FFFF의 각 단위에 대해 `isWhitespace isLetter isLetterOrDigit uppercaseChar lowercaseChar`
- `M`: 사용자 문구 상수 11개(거절 사유 넷, `재생목록`, `Unknown Artist`, `<경로>`, `<파일명>`, 파일 이름 셋). `xgs-diff constants`는 `registerConstants()`에 등록된 것을 `Oracle.java constants`와 같은 이름으로 쓴다
- `S`: `lowercase`, `uppercase`, `trim`, `trimStart`, `trimEnd`, `isBlank`, `ignoreCase` 계열(`ci-starts`, `ci-starts-from`(`startsWith(prefix, from, ignoreCase = true)`), `ci-ends`, `ci-contains`, `ci-index`, `ci-replace`, `eqic`), 그리고 `index`, `replace`
- `C`: `InputClassifier.classify` (`P id alsoVideoId`, `V id`, `REJECT NAME`)
- `F`: `FilenameFormatter.format` (`OK hex` / `ERR`), `D`: `folderName` (`OK hex`), `G`: `destinationFolder` (`OK hex` / `NULL`). 생성기는 80·180 단위 한계의 ±3 길이와 그 자름 자리의 이모지·짝 없는 서로게이트를 낸다. `run.ps1`은 통계의 실제 측정값을 `Write-Host`로 보여 준다
- `T`: `TitleParser.parse` (`OK artist title HIGH|MEDIUM|LOW`). 생성기는 제목을 조각(앞머리 이모지·대괄호 태그, 가수, 구분자 또는 따옴표, 제목, 잡음 토큰·괄호·대괄호·` | ` 꼬리)으로 잇고 공백을 탭·U+3000으로 흩는다. 잡음 토큰에는 `İ`·`ſ`·`K` 같은 닮은꼴을 섞는다(글자별 `ignoreCase` 비교는 맞고 `lowercase()`는 다른 글자). 통계는 따옴표 안에서 나온 제목, Topic 채널, 끝 잡음 토큰, 앞 대괄호 태그, C++ 답의 `Unknown Artist`도 본다(통계 검사는 사례, Kotlin 답, C++ 답을 받는다)
- `L`: `LyricsExtractor.extract`, `Y`: `LyricsExtractor.tidy` (둘 다 `OK hex` / `NULL`). `L`의 설명란은 머리말 줄, 제목 줄(열 가지 표지를 괄호·장식·대소문자·닮은꼴로 꾸미고 제로폭 문자나 U+3164를 끼움) 또는 가짜 제목, 구분선, 본문, 일곱 종류의 끝내는 줄(또는 비슷하지만 아닌 줄, 또는 없음), 꼬리말로 짓고 `\n`·`\r\n`·`\r`(가끔 `\r`과 `\n` 사이에 제로폭 문자나 제어 문자)로 잇는다. 4%는 본문이 3만 단위를 넘고, 그중 다수는 이은 길이가 30000±3에 떨어지며 자름 자리 양쪽 줄 끝에 서로게이트 쌍이 있다. 통계는 `LyricsExtractor`의 규칙을 통계용으로만 다시 써서(답 비교에는 쓰지 않음) 진짜·가짜 제목, 줄바꿈 종류, 블록을 끝낸 줄의 종류를 센다
- `R`: `LogRedaction.redactPaths`, `N`: `LogRedaction.redact(text, names)` (둘 다 `OK hex`). 로그 줄은 조각(보통 글, 드라이브 경로(`\`·`/`·겹친·섞인 구분자), `\\host\share`·`\\?\C:\`·`\\?\UNC\`·`\\.\C:\`와 깨진 접두어, 대소문자를 섞은 `file:///`(`İ`·`ı` 닮은꼴 포함), 가리면 안 되는 가짜(`https://…`, `Music\a.mp3`, `..\x`, `/usr/lib`, `abC:\x` 등))을 `, `·`; `·` -> `·`: 이유`·줄바꿈·빈 문자열로 잇는다. 12%는 가짜와 안전한 말만, 4%는 4000 단위를 넘는다(3999번 단위의 서로게이트, 자름 자리를 넘는 경로). `N`은 이름(`001 가수 - 제목.m4a` 꼴, 3단위 미만, 공백뿐, 확장자 없음, 자리표시와 겹치는 `파일명`, 다른 이름의 앞부분)을 그대로·확장자 없이·대소문자를 바꿔·경로 안에 심는다. 통계는 경로 꼴마다, 이음마다, 가짜만 든 사례에서 Kotlin 답에 `<경로>`가 없는 비율, 그리고 C++ 답이 Kotlin 답보다 덜 가린 사례 수(누설 방향)를 본다
- `B`: U+0000~10FFFF의 각 코드 포인트(서로게이트 제외, 1,112,064개)마다 `Character.isLetterOrDigit(int)`와 `kt::isLetterOrDigit(char32_t)`
- `Z`: 같은 코드 포인트마다 그 코드 포인트 하나짜리 문자열의 NFKC(`java.text.Normalizer.normalize(s, Form.NFKC)`와 `kt::nfkc`)
- `H`: 문자열의 NFKC (`OK hex`). 생성기는 `token()`에 한글 자모(초·중·종성 조합, 완성형 + 종성, 낱자모, 호환 자모, 옛 자모), 전각·반각 문자, 바탕 글자 뒤에 결합 등급이 섞인 결합 문자 1~4개(U+0301, U+0308, U+3099, U+309A 등), 홀로 선 결합 문자, 합자·원문자·괄호 글자·분수·윗첨자, 아랍·히브리 표현형, 정준 단일 분해·조합 제외 글자, CJK 호환 한자, 그리고 Qt의 유니코드에만 있는(15.0 뒤에 생긴) 분해·결합 문자와 그 조각을 섞는다. 통계는 답이 입력과 다른 것, 한글 자모가 든 입력, 결합 문자가 든 입력을 본다
- `O`: `String.toDoubleOrNull()` (`NULL` / 실수 비트 16자리). 생성기는 십진·16진 실수(지수, 접미사 `f`/`F`/`d`/`D`, 부호, 앞자리 0이 많은 지수, 범위를 한참 넘는 지수), `NaN`·`Infinity`, 수백~천 자리 숫자열(최대값·비정규 범위 근처 포함), 경계 문자열 목록, 그것을 한 글자 바꾸거나 넣거나 빼거나 겹친 것, 다른 문자 체계의 숫자, `text()`, 빈 문자열을 내고, 20%는 앞뒤에 U+0020 이하 글자나 그 밖의 공백을 붙인다. 통계는 `NULL`, 값, 무한·NaN을 본다
- `J`: `Json.Default.parseToJsonElement` (`ERR` / 정규형 덤프: `N`, `S<hex>`, `L<hex>`, `O{<키hex>:<값>,…}`, `A[<값>,…]`, 빈 문자열은 `.`). 생성기는 문법에서 문서(객체·배열·문자열·리터럴, 깊이 0~6, 가끔 60단 사슬)를 짓고 30%는 한 군데를 망가뜨린다(문자 삭제·삽입·교체, 키 따옴표 제거, 쉼표 추가, 자르기, 꼬리 쓰레기, 앞 BOM·주석, 작은따옴표, Kotlin이 건너뛰지 않는 공백, 쉼표를 `]`로). 문자열에는 `token()`과 올바른·틀린 이스케이프, 짝 없는 서로게이트 이스케이프, 생 제어 문자를 섞는다. 드물게 깊은 문서: 배열 1000단(그 사이에 객체 0~1개), 객체 1001~3000단, 배열 60000단(Kotlin은 늘 스택 넘침). 배열 1001~59999단은 Kotlin의 답이 JVM 상태에 따라 달라서 만들지 않는다. 통계는 `ERR`, 성공, 비표준 리터럴이 든 성공, 중복 키, 범주별 입력(다른 공백, 서로게이트 이스케이프, 틀린 이스케이프, 1000단 이상, 값 앞의 `]`, 60000단)을 본다
- `E`: `ErrorClassifier.classify` (`<FATAL|UNAVAILABLE|TRANSIENT|OTHER> <메시지 hex>`; 사례는 줄마다 필드 하나, 줄이 없으면 빈 필드 하나). 생성기는 줄 0~6개를 잇는다: 잡음, 목록의 문구를 문장에 넣은 줄(대소문자를 섞고, `İ`·`ſ`·`K`(U+212A) 같은 닮은꼴을 끼우고, 문구를 한 글자 틀리게 한 비슷한 말도 섞는다. 여러 목록의 문구가 한 사례에 같이 나와 검사 순서가 드러난다), `ERROR:` 줄(앞 공백, 여러 개, 접두어만 있는 것), 공백뿐인 줄, 300 단위 자름 자리에 이모지가 오는 긴 줄. 통계는 네 종류 답이 각각 10% 이상인지 본다
- `Q`: `ProgressParser.parse` (`NULL` / `P -` / `P <백분율 double의 비트 16자리>`). 생성기는 `XGSP|상태|…`(상태는 `finished`, `downloading`, 다른 것·대소문자 다른 것)에 내려받은 양·전체·추정 전체를 넣되, 필드 값은 종류 `O`의 생성기에서 가져오거나 백분율이 나오게 짓는다(전체와 그 몫, `NA`, 0, 음수, 전체 0). 접두어가 다르거나 `|`가 없는 것, 앞뒤 공백도 섞는다. 통계는 `NULL`, `P -`, 값이 각각 15% 이상, 값 0과 값 100이 각각 2% 이상인지 본다
- `V`: `VideoInfoFile.read` (`album description duration`: 문자열은 hex, 없으면 `-`, 길이는 십진수나 `-`; Kotlin이 던지면 `THROW <클래스>`). 사례는 파일의 바이트(바이트마다 hex 두 자리, 없으면 `.`)다. 오라클은 그 바이트를 JVM 임시 폴더의 임시 파일 하나에 사례마다 다시 써서 읽고 끝에 지운다. C++ 쪽은 `parseVideoInfo(bytes)`를 바로 부른다. 생성기는 종류 `J`의 문서 생성기로 최상위 객체를 짓고 `album`·`description`·`duration`을 여러 모양으로 넣는다(공백뿐·`NA`·` NA `·이스케이프로 쓴 `NA`인 문자열, 앞에 빈 줄과 들여쓰기·뒤에 공백·`\r\n`·`\r`이 있는 설명, `.5` 경계·Int와 Long 끝·`NaN`·`258f`·`0x1p8`·문자열 `"258"`인 길이, null·불리언·객체·배열·따옴표 없는 리터럴, 중복 키, 한 글자를 이스케이프한 키, 비슷한 키, `formats`·`meta` 안의 같은 이름 키). 그것을 UTF-8로 쓰되 8%는 UTF-8이 아닌 바이트 열(겉도는 이어짐 바이트, 너무 긴 꼴, 서로게이트, U+10FFFF 너머, 잘린 열)을 대개 문자열 값 안에 넣고, 3%는 BOM을 앞에 붙이고, 3%는 UTF-16으로 쓰고, 8%는 드물지만 올바른 열(비문자 U+FFFE·U+FFFF·U+FDD0, U+10FFFF, 가운데의 U+FEFF)을 넣고, 2%는 빈 파일이다. 배열은 몇 단만 겹친다(종류 `J`의 1001단 이상 구역에 들어가지 않는다). 통계는 셋 다 없음 20%, 앨범·설명·길이가 각각 있음 30%, UTF-8이 아닌 입력 5% 이상인지 본다
- `K`: `YtDlpCommands`(`rp`·`rv`·`dl`)와 `FfmpegCommands.tag`(`tag`)의 인자 목록 (`OK <인자마다 hex>` / `ERR`; 경로 필드는 hex, 없으면 `-`). 경로는 `Path.of(text)`로 만드는데 `toString()`이 글자와 다르면 오라클이 `BADPATH`라고 답해 생성기 문제가 드러난다. 생성기는 정리된 Windows 절대 경로(드라이브 글자 + 부분 1~4개: ASCII·한글·공백·`.`·`(`·`)`·`&`·`%`, 끝 공백·끝 점 없음)만 낸다. videoId는 `text()`다. `downloadCommand`는 빈 id와 `\ / : * ? " < >` `|`와 U+0000~001F가 든 id를 거절한다(Kotlin은 `* ? " < > |`는 던지고, `:`는 혼자일 때, 제어 문자는 다른 글자와 함께일 때 던진다). Kotlin이 던지지 않는데 C++만 던지는 입력은 생성기가 낸다: 빈 id는 `id`로 바꾸고, `\ / :`는 지우고(Kotlin은 폴더를 바꾸거나 `a:b`를 드라이브 상대 경로로 읽는다), 제어 문자만 든 id(Kotlin은 받는다)는 제어 문자를 지운다. 그 밖에는 모두 낸다. 도구 경로는 없음을 섞고 js 런타임 파일 이름은 `node.exe`·`NODE.EXE`·`deno`·`Deno.exe`·`bun.exe`·`node.cmd` 따위다. 통계는 op마다 15%, `ERR` 3%, js 런타임 인자가 든 답 20%, 커버가 있는 `tag` 40%를 본다
- `A`: `Ffmetadata.render` (`OK hex`; 필드는 제목·가수·앨범·앨범 가수·번호(십진)·댓글·가사). 값마다 `text()`에 60%는 `=`·`;`·`#`·`\`·LF·CR·탭·NUL·U+001B·U+007F 따위를 1~3개 끼우고 25%는 끝에 `\`를 1~3개 붙인다. 통계는 백슬래시로 이스케이프된 글자가 든 답 40%, 전각 백슬래시(U+FF3C)가 든 답 5%, 제어 문자가 지워진 사례 10%를 본다

**끝 시그마 계약**: `kt::lowercase`는 Kotlin과 같다. 단 대문자 시그마(`Σ`)가 `σ`가 될지 `ς`가 될지는 유니코드의 `Final_Sigma` 규칙(Unicode Standard 3.13, SpecialCasing.txt)을 근사한다. 앞뒤 글자가 Cased인지는 Qt의 대문자·소문자·제목 글자 범주로 정하고, 무시하는 문장부호(case-ignorable)는 `kt_text.cpp`에 적은 목록으로 정하므로 규칙 전체를 구현한 것은 아니다(Kotlin/JVM은 단어 경계로 정해 일부 문맥에서 다르다). Q1의 이식 코드에서 `lowercase()`의 결과는 정해진 ASCII·한글·CJK 집합(잡음 토큰, 가사 표지, 구간 접두어, 호스트 이름, 장치 이름)과 비교될 뿐이라 이 차이는 어떤 출력에도 드러나지 않는다(다음 단계의 `engine`은 사용자 글을 소문자로 바꿔 서로 비교하므로 그 전에 정해야 한다). 그래서 대조 종류 `S`의 `lower`는 입력에 `Σ`(U+03A3)가 있는 사례에서만 두 답의 `ς`(U+03C2)를 `σ`(U+03C3)로 접어 비교한다(`run.ps1`의 `$Folding`). 입력에 `Σ`가 없으면 `ς`도 그대로 비교한다.

종류를 더하는 곳: 생성기(`gen.cpp`의 `registerGenerators()`에서 `registerKind`), C++ 답(`main.cpp`의 `registerAnswers()`에서 `registerAnswer`), Kotlin 답(`Oracle.java`의 `switch (part[0])`에 `case`), 통계 문턱(`run.ps1`의 `$Statistics`). 표 종류(`U`, `X`, `M`, `B`, `Z`)는 `$Statistics`에 `Table`(두 쪽이 찍는 표의 이름)을 둔다.

### `core/text/kt_tables.inc`

`pwsh qt/tools/differential/gen-tables.ps1`이 만든다(손으로 고치지 않는다). 내용은 JVM 21이 구현한 유니코드 판의 문자 데이터(Unicode Character Database)이고, JVM의 공개 API에 물어 얻는다. Qt 6.11.3의 유니코드 데이터와 JVM 21.0.9가 다른 단위 **48개**(isWhitespace 0, isLetter 16, isLetterOrDigit 16, uppercaseChar 35, lowercaseChar 9)는 JVM의 답을 표로 둔다. 그 밖에 유니코드 SpecialCasing 자료(JVM의 공개 API로 얻은 것)의 특수 대문자 102개(`ß`→`SS` 등)와 특수 소문자 1개(`İ`→`i̇`), BMP 밖 대소문자 대응 520개가 들어 있다. BMP 밖에서 `Character.isLetterOrDigit(int)`가 Qt의 범주(L*, Nd)와 다른 코드 포인트 **9,642개**(42개 범위, 종류 `B`의 예외; 대부분 Qt의 유니코드 17.0에만 있는 글자)는 JVM의 답을 범위로 둔다. NFKC(`Z`)의 예외는 **0개**다: `kt::nfkc`는 `QString::normalized`에 JVM 21의 유니코드 판(15.0, `QChar::Unicode_15_0`)을 주며, 그러면 15.0 뒤에 생긴 코드 포인트를 JVM이 미할당 코드 포인트를 다루듯 그대로 둔다(판을 주지 않으면 `Z` 40개, `H` 1만 건 중 1,396건이 다르다). `kt_text.cpp`는 이분 탐색하는 표마다 정렬돼 있는지 컴파일할 때 확인한다(`static_assert`). Qt나 사이드카 이미지의 JVM이 바뀌면 다시 만들고 `U`·`X`·`S`·`B`·`Z`·`H`를 돌린다. 이 자료의 고지는 저장소 루트의 `THIRD-PARTY-NOTICES.md`에 있다.

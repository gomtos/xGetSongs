# 설치 없는 단일 exe, m4a 출력, 시작 시 구성요소 설치 설계

작성일: 2026-10-09 (같은 날 개정: mp3를 m4a로 바꾸고 ffmpeg를 없앴다. 근거는 8절)

## 1. 목표

Windows에서 exe 하나를 더블클릭하면 설치 없이 실행되는 xGetSongs를 만든다. JRE는 exe 안에 들어 있고, 앱이 쓰는 외부 도구(yt-dlp, JS 런타임)는 앱이 시작할 때 확인해서 없으면 사용자 동의를 받아 내려받는다.

ffmpeg가 필요 없도록 출력 형식을 mp3에서 **m4a**로 바꾼다. YouTube가 주는 AAC 오디오를 재인코딩 없이 그대로 저장하고, MP4 태그와 커버는 앱이 직접 쓴다.

세 부분으로 나뉘고 구현 계획도 따로 쓴다. 순서는 1부, 2부, 3부다. 1부가 ffmpeg 관련 코드를 먼저 없애야 2부가 ffmpeg 자리를 만들지 않고, 3부의 검증이 1부와 2부의 실제 동작을 그대로 쓰기 때문이다.

- 1부: m4a 전환 (4절)
- 2부: 시작 시 구성요소 점검과 설치 (5절)
- 3부: 단일 exe 포장 (6절)

## 2. 확정된 결정

| 항목 | 결정 |
|---|---|
| 출력 형식 | m4a (YouTube의 AAC 원본, itag 140). mp3와 ID3는 만들지 않는다 |
| 배포 형태 | 설치 없는 단일 exe. 설치형 exe/MSI는 만들지 않는다 |
| 외부 도구 | yt-dlp와 JS 런타임(Deno 2.3+ 또는 Node 22+). ffmpeg는 쓰지 않는다 |
| 점검과 설치 | 앱이 시작할 때 점검하고, 없으면 동의를 받아 내려받는다. 매 실행마다 점검한다 |
| 저장 위치 | `%APPDATA%\xGetSongs\bin` (`ToolLocator`가 이미 PATH보다 먼저 찾는다) |
| 거절(`나중에`) | 앱은 열리지만 두 가지가 갖춰질 때까지 조회와 다운로드를 막는다 |
| JS 런타임 | 없으면 Deno를 받는다. PATH에 조건을 만족하는 Node나 Deno가 있으면 설치하지 않는다 |
| m4a 파일 브랜드 | `ftyp`를 `M4A `로 바꾼다(조각난 구조는 그대로) |
| 커버 | 썸네일 JPG를 앱이 직접 받아 가운데 정사각형으로 잘라 넣는다 |
| 패키징 | Compose의 `createDistributable`(내부적으로 `jpackage --type app-image`)을 기본으로 쓰고, 직접 `jpackage`는 폴백으로 둔다 |
| 패키징용 JDK | Temurin 21 zip을 프로젝트 밖 폴더에 풀어 쓴다. 시스템에 설치하지 않는다 |

## 3. 범위 밖 (YAGNI)

- mp3 출력, 출력 형식 선택 옵션, 이미 받은 mp3 파일의 변환이나 삭제
- Opus/WebM 출력, 조각나지 않은 MP4의 오프셋 보정(지원하지 않는 구조는 실패로 처리한다)
- 코드 서명(SmartScreen 경고 제거), 앱 아이콘(`.ico`), 앱 자체 자동 업데이트
- 내려받기의 바이트 단위 진행률과 중단 버튼
- 다른 OS용 포장

## 4. 1부: m4a 전환

### 4.1 yt-dlp 명령

다운로드 명령(`YtDlpCommands.download`)을 다음으로 바꾼다.

- 오디오만 받는다: `-f "bestaudio[ext=m4a]"`. 샘플 영상에서 itag 140(`mp4a.40.2`, 약 129kbps)이 골라지는 것을 확인했다. DRC 변형(`140-drc`)은 고르지 않는다.
- `--fixup never`: ffmpeg로 컨테이너를 고치는 후처리와 그 경고를 끈다.
- `-x`, `--audio-format`, `--audio-quality`, `--write-thumbnail`, `--convert-thumbnails`는 뺀다. `--write-info-json`은 유지한다(앨범과 썸네일 후보를 읽는다).
- 출력은 `<작업 폴더>/<videoId>.m4a`와 `<videoId>.info.json`이다.
- 나머지(`--ignore-config`, `--no-warnings`, `--encoding utf-8`, `--js-runtimes`, `--no-playlist`, `--newline`, 진행 템플릿, `--`로 끝나는 URL)는 그대로다.
- 후처리가 없으므로 `XGSPP` 후처리 진행 줄은 더 나오지 않는다. 진행 단계 `Stage.CONVERTING`(변환 중)은 `TAGGING`(태그 쓰는 중: 커버 받기와 태그 쓰기)으로 바꾼다.
- m4a 오디오 형식이 없는 영상은 그 항목을 실패로 처리하고 사유를 "m4a 오디오 형식이 없습니다"로 적는다. yt-dlp의 오류 문구와 `ErrorClassifier`의 대응은 구현 계획에서 정한다.
- PATH에 ffmpeg가 있어도 영향이 없다(후처리를 시키지 않고 `--fixup never`를 쓴다).

### 4.2 MP4 태그 쓰기 (`Mp4Tagger`)

`jaudiotagger 3.0.1`은 조각난 DASH m4a에 쓰지 못한다(8절). 그래서 태그 쓰기를 직접 구현한다. 실험에서 모든 항목이 읽힘을 확인한 방식이다.

| 항목 | MP4 상자 | 값 |
|---|---|---|
| 제목 | `©nam` | 파싱한 제목 |
| 가수 | `©ART` | 파싱한 가수 |
| 앨범 가수 | `aART` | 파싱한 가수 |
| 앨범 | `©alb` | 영상 자체의 앨범, 없으면 재생목록 제목, 단일 영상에 앨범 정보가 없으면 쓰지 않음(기존 규칙) |
| 트랙 번호 | `trkn` | 순위(총 개수는 0) |
| 주석 | `©cmt` | 영상 URL |
| 가사 | `©lyr` | 가사가 있을 때만(없으면 쓰지 않음). 언어 표시는 없다 |
| 커버 | `covr` | JPEG(데이터 형식 13) |

- 값은 원문 그대로다. 금지 문자 치환이나 길이 축약은 하지 않고, 줄바꿈과 탭을 뺀 제어 문자만 지운다. ffmetadata 때문에 있던 값 끝 `\`의 전각 치환은 없어진다.
- 쓰는 방식: 최상위 상자를 순서대로 읽고, `moov` 안의 기존 `udta`를 새 `udta`(`meta` → `hdlr`(`mdir`) + `ilst`)로 바꿔 `moov`를 다시 만든다. `ftyp`는 주 브랜드 `M4A `, 호환 브랜드 `M4A `, `mp42`, `isom`, `iso6`로 바꾼다. 나머지 상자(`sidx`, `moof`, `mdat`)는 바이트 그대로 둔다. 같은 폴더의 임시 파일에 쓰고 원본을 교체한다.
- 안전한 이유: `moof`의 `tfhd`는 `default-base-is-moof` 기준이고 `sidx`의 첫 오프셋은 0이다. 그래서 `moov`가 커져도 다른 오프셋이 어긋나지 않는다.
- 지원하는 구조: `ftyp`, `moov`(안에 `mvex`)가 앞에 있고 `moof`가 하나 이상인 조각난 MP4. 이 구조가 아니거나, 어떤 `moof`의 `tfhd`에 절대 기준 오프셋(`base-data-offset-present`, 0x000001)이 있으면 파일을 건드리지 않고 그 항목을 실패로 처리한다(사유: "지원하지 않는 m4a 구조").
- 태그 쓰기에 실패한 항목은 지금처럼 실패이고 "실패 항목 재시도"로 다시 받을 수 있다.

### 4.3 커버

- 후보는 info.json의 `thumbnails` 중 `vi/` 경로의 JPG로, 파일 이름이 `maxresdefault.jpg`, `hq720.jpg`, `mqdefault.jpg`인 것을 이 순서로 시도한다(셋 다 16:9). 4:3 위아래 검은 띠가 있는 `sddefault`, `hqdefault` 등과 WebP(`vi_webp/`)는 쓰지 않는다.
- 주소는 `https://i.ytimg.com/`로 시작하는 것만 받는다(그 밖의 호스트는 무시한다).
- 받은 이미지는 `ImageIO`로 읽어 가운데 정사각형으로 자르고 JPEG(품질 0.95)로 다시 쓴다. 샘플 영상에서 1278×720을 720×720(135KB)으로 만든 것을 확인했다.
- 모든 후보가 실패하거나 이미지를 읽지 못하면 커버 없이 태그만 쓴다(지금과 같다).

### 4.4 파일명과 기타

- 확장자가 `.m4a`가 된다. 파일명 규칙(순위, 가수, 제목, 금지 문자 처리)은 그대로다. 이미 있는 `.mp3`는 건드리지 않고, 같은 곡을 다시 받으면 `.m4a`가 옆에 따로 생긴다.
- 가사 검색과 "가사 결과 표시"는 그대로다. 태그 쓰기 대상이 ID3에서 `©lyr`로 바뀔 뿐이다.
- Windows 탐색기의 비트 전송률은 조각난 m4a에서 0kbps로 보인다(겉모양만의 문제).

### 4.5 삭제와 교체

- 삭제: `Id3Tagger`, `Id3Frames`, `Ffmetadata`, `FfmpegCommands`, `ItemDownloader`의 "ffmpeg를 찾을 수 없습니다" 검사, `ToolLocator`/`ToolPaths`의 ffmpeg 탐색, `ToolsStatus.ffmpeg`와 도구 패널의 ffmpeg 표시와 안내문, `DefaultToolManager`의 ffmpeg 버전 조회.
- 새로 만들기: `Mp4Tagger`, 커버 받기(`CoverFetcher`).
- `TrackTags`는 형식 중립 모델로 두고 ID3 전용 주석을 고친다.

### 4.6 테스트

- `Mp4TaggerTest`: 작은 조각난 m4a 고정 파일(테스트 자원)에 태그를 쓰고, 테스트 안의 독립된 읽기로 모든 항목과 브랜드를 확인한다. `sidx`/`moof`/`mdat` 영역의 바이트가 그대로인지도 확인한다. 조각나지 않은 파일과 절대 기준 오프셋 파일은 실패하고 원본이 바뀌지 않아야 한다. 기존 `udta`가 있는 파일, 가사·앨범·커버가 없는 경우, 빈 값도 확인한다.
- `CoverFetcherTest`: 로컬 HTTP 서버로 첫 후보가 404일 때 다음 후보로 넘어가는지, 4:3 후보를 요청하지 않는지, 허용되지 않은 호스트를 무시하는지, JPEG가 아닌 응답을 건너뛰는지, 정사각형 자르기를 확인한다.
- `YtDlpCommandsTest`, `ItemDownloader`/`DefaultDownloadService` 테스트를 새 명령과 흐름에 맞춘다.
- 통합 테스트(`:engine:integrationTest`): 실제 yt-dlp로 m4a를 받아 태그를 쓰고 자체 읽기로 확인한다. 선택한 형식이 `140`인지도 확인한다.
- 삭제한 코드의 테스트(`RealFfmpegTaggingIntegrationTest`, `Mp3Probe`, `Id3*Test`, `FfmpegCommandsTest`, `FfmetadataTest`)는 지운다.

## 5. 2부: 시작 시 구성요소 점검과 설치

### 5.1 점검 대상과 출처

| 구성요소 | 없을 때 받을 곳 | 받는 크기 (2026-10-09 측정) |
|---|---|---|
| yt-dlp | `github.com/yt-dlp/yt-dlp` 최신 `yt-dlp.exe` | 17.8MB |
| JS 런타임 | `github.com/denoland/deno` 최신 `deno-x86_64-pc-windows-msvc.zip`에서 `deno.exe` | 42.6MB |

합계는 약 60MB이고, 동의 창에는 "약 18MB / 약 43MB, 합계 약 61MB"로 표기한다(받을 때마다 조금 달라질 수 있어 반올림해서 보여 준다).

"없음"의 기준은 기존 `ToolsStatus`의 `found=false`다. 그래서 PATH에 있는 Node가 22 미만이면 JS 런타임이 없는 것으로 보고 Deno를 제안한다. `ToolLocator`는 appBinDir를 먼저, 그다음 PATH를 보고 `deno`를 `node`보다 먼저 찾으므로 받은 Deno가 쓰인다.

### 5.2 흐름

1. 앱이 뜨면 기존 `refreshTools()`가 상태를 읽는다. 읽는 동안 조회는 막혀 있다.
2. 빠진 것이 없으면 아무것도 보이지 않는다.
3. 빠진 것이 있으면 모달을 띄운다. 내용은 빠진 구성요소마다 이름, 받을 곳(호스트), 대략의 크기, 저장 위치(`%APPDATA%\xGetSongs\bin`)다. 버튼은 `설치`와 `나중에`다. 동의 없이 받지 않는다.
4. `설치`를 누르면 yt-dlp, Deno 순서로 빠진 것만 하나씩 설치하고 "2/2 Deno 설치 중…"처럼 단계를 보여 준다.
5. 한 단계가 실패해도 나머지는 계속한다. 끝나면 다시 점검한다. 모두 있으면 모달을 닫고, 실패한 것이 있으면 사유 목록과 `다시 시도`(실패한 것만)와 `닫기`를 보여 준다.
6. 설치 중에 창을 닫으면 임시 파일은 정리되고 다음 실행에서 다시 묻는다.

### 5.3 `나중에` 또는 `닫기`를 고른 경우

- 앱은 열린다. 두 가지가 모두 갖춰질 때까지 `조회`(와 그 뒤의 다운로드)를 막고, 입력창 근처에 이유와 `구성요소 설치` 버튼을 보여 준다. 이 버튼이 모달을 다시 연다.
- 도구 패널은 기존처럼 상태를 보여 주고, 빠진 것마다 설치 버튼을 둔다. 기존 yt-dlp `설치`/`업데이트` 동작은 그대로다.

### 5.4 구조

- 엔진: `ToolManager`에 `installDeno()`를 추가한다. `ToolException`(사용자에게 보일 한국어 메시지)으로 실패를 알린다.
- 서버: `POST /tools/deno/install`을 추가한다. 여러 설치의 순서 제어는 서버가 아니라 앱(`AppStateHolder`)이 맡아서, 서버 API는 도구 하나당 요청 하나로 둔다.
- 앱: `XgsApi`/`HttpXgsApi`에 호출을 추가한다. `AppStateHolder`가 점검 결과에서 모달 상태(확인 대기, 설치 중 n/m, 결과)를 만들고 `canResolve`에 구성요소 조건을 더한다. 상태 논리는 모두 `commonMain`의 `AppStateHolder`에 두고, 화면은 `SetupDialog` 하나가 그리기만 한다.
- Deno 설치 절차는 기존 `installYtDlp`와 같다: `deno.new.exe`로 두고 `--version`이 기존 파서로 2.3.0 이상으로 읽혀야 `deno.exe`로 교체하며, 실패하면 기존 파일을 그대로 두고 정리는 `NonCancellable`로 한다. zip에서는 이름이 `deno.exe`인 항목만 꺼낸다(경로 조작 위험 없음). 받은 파일은 메모리에 올리지 않고 임시 파일로 저장한다.
- 받는 도중 데이터가 30초 넘게 오지 않으면 실패로 처리한다.
- 로그에는 설치 시작, 완료, 실패(사유)를 도구 이름과 함께 INFO/WARN으로 남긴다. 오류 문구 속의 경로는 기존 규칙대로 가려서 적는다.

### 5.5 테스트

- 엔진 단위 테스트(테스트에서 만든 zip과 가짜 fetch/runner 사용): Deno 정상 설치, 다운로드 실패, zip에 대상 항목 없음 또는 둘 이상, 실행 확인 실패나 버전 미달일 때 기존 파일 보존, 임시 파일 정리.
- 서버 경로 테스트와 `XgsApi` 계약 테스트(`HttpXgsApiTest`, `ContractSupport`)에 새 호출을 추가한다.
- `AppStateHolderTest`: 모두 있으면 모달 없음, 빠진 목록이 정확함, 설치 순서, 일부 실패해도 나머지 계속, 실패한 것만 재시도, 거절하면 조회 막힘, 설치 뒤 풀림, 도구 확인 중에는 조회 막힘.
- 통합 테스트(네트워크가 있을 때만): 임시 폴더에 yt-dlp와 Deno를 실제로 받아 실행 확인까지 한다.

## 6. 3부: 단일 exe 포장

### 6.1 빌드 환경

jpackage와 `jmods`가 있는 JDK 21이 필요하다. 지금 쓰는 JetBrains Runtime(`C:\bin\Java\jbr-21.0.9`)에는 둘 다 없고, `~/.gradle/jdks`의 Temurin 17은 앱이 21용이라 쓸 수 없다. WiX는 필요 없다(앱 이미지만 만든다).

- Temurin 21 zip(`api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jdk/hotspot/normal/eclipse`, 2026-10-09 기준 `21.0.12.1`, 205MB)을 `C:\bin\Java\jdk-21.0.12.1+1`처럼 프로젝트 밖에 푼다. 받기 전에 파일명, 출처, 크기를 알리고 허락을 받는다.
- 시스템 PATH와 `JAVA_HOME`은 바꾸지 않는다. 포장 스크립트가 환경 변수 `XGS_JDK`로 이 폴더를 받아, 그 Gradle 호출에만 `JAVA_HOME`으로 쓴다. `XGS_JDK\bin\jpackage.exe`가 없으면 이유를 알려 주고 멈춘다.
- Gradle은 `--no-daemon`으로 부른다.

### 6.2 앱 이미지와 jpackage

기본은 Compose의 `:app:createDistributable`이다. 이 태스크는 내부적으로 JDK의 `jpackage --type app-image`를 부르고, 결과는 `app/build/compose/binaries/main/app/xGetSongs/`(`xGetSongs.exe`, `app/`, `runtime/`)다. jpackage 자체에는 단일 파일 출력이 없으므로(app-image는 폴더, exe/msi는 설치 프로그램) 6.4의 감싸기 단계가 어떤 경우에도 필요하다.

- 런타임 모듈: jlink 런타임은 필요한 모듈만 담는데 플러그인이 자동으로 찾지 못한다. 지금 `modules(...)`는 3개뿐이고 패키지 실행은 한 번도 검증된 적이 없으므로 가장 큰 위험이다. `suggestModules`로 후보를 얻고 필요하면 `includeAllModules = true`로 시작해서 줄여 가며, 최종 목록을 `build.gradle.kts`에 명시한다.
- 검증용 콘솔 빌드: 검증할 때는 `windows { console = true }`로 콘솔이 보이는 빌드를 먼저 만들어 시작 오류(모듈 누락 등)를 보고, 최종 빌드에서는 끈다.
- **직접 `jpackage` 폴백**: Compose 태스크로 풀 수 없을 때만 쓴다. 조건은 (a) 플러그인의 jlink가 필요한 모듈이나 JavaFX 구성을 만들지 못할 때, (b) Compose DSL로 필요한 jpackage 옵션을 줄 수 없을 때다. 방법은 `packageUberJarForCurrentOS`와 직접 만든 jlink 런타임으로 `jpackage --type app-image --runtime-image <jlink 런타임> --input <폴더> --main-jar <jar> --main-class com.xgetsongs.app.MainKt [--java-options …] [--app-content …] [--win-console] [--icon …] --dest <폴더>`를 부르는 것이다. `--jlink-options`의 기본값은 `--strip-native-commands --strip-debug --no-man-pages --no-header-files`이고 `--runtime-image`를 주면 적용되지 않는다. Compose DSL이 이 옵션들을 어디까지 노출하는지는 구현 때 확인한다(`modules`, `includeAllModules`, `windows { console, iconFile }`는 문서에서 확인했다).

### 6.3 깨끗한 PC 흉내 검증

`APPDATA`를 빈 임시 폴더로 지정하고, PATH에서 Node, Deno, yt-dlp를 뺀 채 `xGetSongs.exe`를 실행한다.

- 창이 뜨고, 내장 서버가 뜨고, 로그가 남는다.
- 2부의 모달이 뜨고 yt-dlp와 Deno를 실제로 받아 설치한다(GitHub로의 HTTPS가 패키지 런타임에서 되는지 함께 확인된다).
- 실제 곡 하나를 m4a로 받아 태그와 커버까지 확인한다. 만든 파일의 재생 확인은 이미 사용자 기기에서 한 방식과 같다.

### 6.4 단일 exe로 감싸기

앱 이미지 폴더를 exe 하나로 묶는다. 후보 둘을 같은 이미지로 만들어 비교하고 정한다.

| 후보 | 방식 | 확인할 점 |
|---|---|---|
| 7-Zip SFX | 실행할 때마다 임시 폴더에 풀고, 끝나면 지운다 | 7-Zip에는 `7z.sfx`/`7zCon.sfx`만 있고 풀고 실행해 주는 설치용 모듈이 없어 7-Zip Extra에서 따로 받아야 한다 (받기 전에 허락) |
| warp-packer | 처음 한 번만 풀고 캐시를 다시 쓴다 | 유지보수 상태 불명. 받기 전에 출처와 크기를 알리고 허락을 받는다 |

판정은 이 PC에서 두 후보를 같은 조건으로 재서 한다. 두 번째 이후 실행에서 창이 뜨기까지 걸린 시간(실행 시각부터 로그의 시작 줄 시각까지, 3회 중앙값)이 5초 이하면 7-Zip SFX를 택한다. 넘으면 warp-packer를 택한다. 함께 기록할 것: 첫 실행 시간, Windows Defender 반응, 비정상 종료 뒤 남는 임시 폴더. 둘 다 6.3의 검증을 통과하지 못하면 이 스펙으로 돌아와 다시 정한다(Nucleus의 `TargetFormat.Portable`은 문서에 출력 형태 설명이 없어 이번에는 쓰지 않는다).

### 6.5 산출물

- `dist/xGetSongs.exe` 하나. `dist/`는 `.gitignore`에 넣는다.
- 루트의 `package-exe.bat` 하나가 JDK 확인, `createDistributable`, 감싸기를 순서대로 하고(기존 `run.bat`과 같은 위치와 방식), README에 빌드와 배포 방법을 적는다.

### 6.6 알려진 한계

- 서명이 없어서 처음 받은 PC에서 SmartScreen이 "알 수 없는 게시자" 경고를 낼 수 있다.
- 아이콘이 없어서 기본 Java 아이콘이 나온다.
- 압축을 푸는 방식이라 첫 실행은 느릴 수 있다.
- 앱을 새 버전으로 바꾸려면 새 exe로 교체한다. yt-dlp는 기존 업데이트 버튼으로 따로 갱신한다.

## 7. 동시 진행 중인 작업과의 관계

같은 저장소에서 구글 가사 조회(`docs/superpowers/specs/2026-10-08-google-lyrics-card-design.md`)가 진행되었고, `engine`이 JavaFX 21.0.12(`base`, `graphics`, `controls`, `media`, `web`의 `win` 분류) 의존성을 이미 갖고 있다.

- 3부에서 JavaFX는 클래스패스로 올라가며 네이티브 라이브러리를 jar에서 꺼내 쓰는 구조일 가능성이 있다. 앱 이미지에서 숨김 웹뷰가 실제로 뜨는지 검증하고, jlink 런타임에 JavaFX가 요구하는 JDK 모듈이 더 필요한지 본다. 앱 이미지와 exe의 크기도 JavaFX만큼 커진다(6.4의 판정 시간에 이미 반영된다). 구글 가사 조회가 포함돼 있으면 6.3의 검증에 가사 조회 한 번을 더한다.
- 1부는 `ItemDownloader.kt`, `TrackTags.kt`, `YtDlpCommands.kt`, `Services.kt`처럼 그 작업과 겹칠 수 있는 파일을 고친다. 구현은 그 작업이 커밋된 뒤의 `main`에서 시작하고, 겹치는 파일은 먼저 현재 내용을 확인한다.

그 작업의 코드는 이 스펙이 직접 바꾸지 않는다(가사를 받아 태그에 넣는 연결 부분만 형식에 맞게 바뀐다).

## 8. 근거: m4a 실험 (2026-10-09)

Blender Foundation의 CC-BY 영상(Big Buck Bunny)을 임시 폴더에서 받아 확인했다.

- ffmpeg를 PATH에서 뺀 채 `-f 140 --fixup never`로 받은 m4a는 조각난 MP4다: `ftyp` 주 브랜드 `dash`, `moov`(안에 `mvex`), `sidx` 1개, `moof`/`mdat` 64쌍. `sidx`의 첫 오프셋은 0이고 `tfhd` 플래그는 `0x02002a`(`default-base-is-moof`이며 절대 오프셋 없음)다.
- 원본에 `jaudiotagger 3.0.1`로 태그를 쓰면 `incorrect offsets written` 오류로 실패한다. ffmpeg로 일반 MP4로 고친 파일에는 성공한다.
- 원본에 `moov`의 `udta/meta/ilst`를 직접 넣으면(약 40줄) 모든 항목과 커버(720×720, 첨부 이미지)가 읽히고, 오디오 MD5가 원본과 같고, 디코드 오류가 없고, 300초 지점으로 이동이 된다. `ftyp`만 `M4A `로 바꾼 파일도 같다.
- Windows 탐색기가 제목, 가수, 앨범, 앨범 가수, 주석, 길이를 읽고 미디어 엔진이 파일을 연다.
- 사용자가 세 파일(고친 파일, 원본에 직접 태그, 브랜드 변경)을 자기 기기와 플레이어에서 재생해 모두 정상임을 확인했다.
- 같은 곡(10분 34초)에서 원본 m4a는 10.3MB, 현재 방식의 mp3 V0는 18.0MB(1.75배)이고 변환에 9.9초가 걸렸다.
- info.json의 JPG 썸네일 후보는 `maxresdefault`(1280×720), `hq720`(1280×720), `mqdefault`(320×180)이 16:9이고 나머지는 4:3이다. yt-dlp가 지금 고르는 것은 WebP다.

## 9. 문서 갱신

구현 중에 함께 고친다.

- `README.md`: 첫머리의 mp3 설명, 파일명 형식과 예시, "ID3 태그"와 가사 설명(USLT 언급), "필요한 것" 표(ffmpeg 삭제, 시작 시 점검과 설치), "알려진 제한"의 ffmpeg와 `packageMsi` 항목, 로그 설명에서 변환 단계, 단일 exe 사용법과 빌드 방법.
- `2026-10-04-xgetsongs-design.md`: 1절과 6.3의 파일명 형식, 4절의 다운로드 엔진(ffmpeg)과 배포 행, 6.4의 ID3 태그, 8절의 ffmpeg 오류 처리, 9절의 테스트, 11절의 검증 항목을 이 스펙과 맞춘다.

## 10. 구현 시 검증할 사항

각 항목은 결과에 따라 이 스펙을 고치거나 선택을 정한다.

1. **`bestaudio[ext=m4a]`가 다른 영상에서도 itag 140을 고르는가**: 라이브, 연령 제한, 음원 전용 영상 등. m4a가 없을 때 yt-dlp의 오류 문구와 `ErrorClassifier` 대응.
2. **다른 영상의 m4a도 같은 조각난 구조인가**: 4.2의 구조 검사가 그렇지 않은 파일을 안전하게 실패시키는지 포함한다.
3. **Deno zip의 실제 구조**(`deno.exe`의 위치)와 `--version` 출력이 기존 파서와 맞는지.
4. **VC++ 재배포 패키지가 없는 PC에서 `yt-dlp.exe`가 실행되는지**. 안 되면 설치 실패 문구에 그 가능성을 안내한다.
5. **jlink 모듈 목록**(HTTPS, logback, Netty, JavaFX를 포함해)과 JavaFX 네이티브 처리.
6. **7-Zip 설치용 모듈과 warp-packer의 입수와 동작**, Defender와 SmartScreen 반응.
7. **Compose DSL이 노출하는 jpackage 옵션의 범위**(6.2의 폴백이 필요한지 판단).

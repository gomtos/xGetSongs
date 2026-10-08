# 설치 없는 단일 exe와 시작 시 구성요소 설치 설계

작성일: 2026-10-09

## 1. 목표

Windows에서 exe 하나를 더블클릭하면 설치 없이 실행되는 xGetSongs를 만든다. JRE는 exe 안에 들어 있고, 앱이 쓰는 외부 도구(yt-dlp, ffmpeg, JS 런타임)는 앱이 시작할 때 확인해서 없으면 사용자 동의를 받아 내려받는다.

두 부분으로 나뉘며 구현 계획도 따로 쓴다. 1부(구성요소 설치)를 먼저 만든다. 2부(포장)의 앱 이미지 검증이 1부의 첫 실행 흐름을 그대로 쓰기 때문이다.

## 2. 확정된 결정

| 항목 | 결정 |
|---|---|
| 배포 형태 | 설치 없는 단일 exe. 설치형 exe/MSI는 만들지 않는다 |
| 외부 도구 | exe에 넣지 않고 앱이 시작할 때 점검해서 없으면 동의를 받아 내려받는다 |
| 점검 대상 | yt-dlp, ffmpeg, JS 런타임(Deno 2.3+ 또는 Node 22+). 매 실행마다 점검한다 |
| 저장 위치 | `%APPDATA%\xGetSongs\bin` (`ToolLocator`가 이미 PATH보다 먼저 찾는다) |
| 거절(`나중에`) | 앱은 열리지만 세 가지가 갖춰질 때까지 조회와 다운로드를 막는다 |
| ffmpeg 받는 방식 | gyan.dev essentials zip에서 `ffmpeg.exe`만 Range 요청으로 받는다. 직접 빌드한 최소 ffmpeg는 이번 범위 밖이다 |
| JS 런타임 | 없으면 Deno를 받는다. PATH에 조건을 만족하는 Node나 Deno가 있으면 설치하지 않는다 |
| 패키징용 JDK | Temurin 21 zip을 프로젝트 밖 폴더에 풀어 쓴다. 시스템에 설치하지 않는다 |

## 3. 범위 밖 (YAGNI)

- 코드 서명(SmartScreen 경고 제거), 앱 아이콘(`.ico`), 앱 자체 자동 업데이트
- 직접 빌드한 최소 ffmpeg (mp3 인코더만 남긴 빌드). 크기가 문제가 될 때 별도 작업으로 한다
- 내려받기의 바이트 단위 진행률과 중단 버튼
- 다른 OS용 포장

## 4. 1부: 시작 시 구성요소 점검과 설치

### 4.1 점검 대상과 출처

| 구성요소 | 없을 때 받을 곳 | 받는 크기 (2026-10-09 측정) |
|---|---|---|
| yt-dlp | `github.com/yt-dlp/yt-dlp` 최신 `yt-dlp.exe` | 17.8MB |
| ffmpeg | `gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip`에서 `bin/ffmpeg.exe`만 | 37.4MB (전체 zip은 114.8MB) |
| JS 런타임 | `github.com/denoland/deno` 최신 `deno-x86_64-pc-windows-msvc.zip`에서 `deno.exe` | 42.6MB |

합계는 약 98MB이고, 동의 창에는 "약 18MB / 약 37MB / 약 43MB, 합계 약 100MB"로 표기한다(받을 때마다 조금 달라질 수 있는 값이라 반올림해서 보여 준다).

"없음"의 기준은 기존 `ToolsStatus`의 `found=false`다. 그래서 PATH에 있는 Node가 22 미만이면 JS 런타임이 없는 것으로 보고 Deno를 제안한다. `ToolLocator`는 appBinDir를 먼저, 그다음 PATH를 보고 `deno`를 `node`보다 먼저 찾으므로 받은 Deno가 쓰인다.

### 4.2 흐름

1. 앱이 뜨면 기존 `refreshTools()`가 상태를 읽는다. 읽는 동안 조회는 막혀 있다.
2. 빠진 것이 없으면 아무것도 보이지 않는다.
3. 빠진 것이 있으면 모달을 띄운다. 내용은 빠진 구성요소마다 이름, 받을 곳(호스트), 대략의 크기, 저장 위치(`%APPDATA%\xGetSongs\bin`)다. 버튼은 `설치`와 `나중에`다. 동의 없이 받지 않는다.
4. `설치`를 누르면 yt-dlp, ffmpeg, Deno 순서로 빠진 것만 하나씩 설치하고 "2/3 ffmpeg 설치 중…"처럼 단계를 보여 준다.
5. 한 단계가 실패해도 나머지는 계속한다. 끝나면 다시 점검한다. 모두 있으면 모달을 닫고, 실패한 것이 있으면 사유 목록과 `다시 시도`(실패한 것만)와 `닫기`를 보여 준다.
6. 설치 중에 창을 닫으면 임시 파일은 정리되고 다음 실행에서 다시 묻는다.

### 4.3 `나중에` 또는 `닫기`를 고른 경우

- 앱은 열린다. 세 가지가 모두 갖춰질 때까지 `조회`(와 그 뒤의 다운로드)를 막고, 입력창 근처에 이유와 `구성요소 설치` 버튼을 보여 준다. 이 버튼이 모달을 다시 연다.
- 도구 패널은 기존처럼 상태를 보여 주고, 빠진 것마다 설치 버튼을 둔다. 기존 yt-dlp `설치`/`업데이트` 동작은 그대로다. ffmpeg 안내 문구의 `winget` 예시는 설치 버튼 안내로 바꾼다.
- 엔진 쪽 방어(첫 항목에서 ffmpeg가 없으면 실패)는 그대로 둔다.

### 4.4 구조

- 엔진: `ToolManager`에 `installFfmpeg()`와 `installDeno()`를 추가한다. 둘 다 `ToolException`(사용자에게 보일 한국어 메시지)으로 실패를 알린다.
- 서버: `POST /tools/ffmpeg/install`, `POST /tools/deno/install`을 추가한다. 여러 설치의 순서 제어는 서버가 아니라 앱(`AppStateHolder`)이 맡아서, 서버 API는 도구 하나당 요청 하나로 둔다.
- 앱: `XgsApi`/`HttpXgsApi`에 두 호출을 추가한다. `AppStateHolder`가 점검 결과에서 모달 상태(확인 대기, 설치 중 n/m, 결과)를 만들고 `canResolve`에 구성요소 조건을 더한다. 상태 논리는 모두 `commonMain`의 `AppStateHolder`에 두고, 화면은 `SetupDialog` 하나가 그리기만 한다.
- 공통 설치 절차: 임시 이름으로 저장 → 실행 확인 → 교체. 기존 `installYtDlp`의 방식(`*.new.exe`로 두고 `--version`이 돼야 `*.exe`로 교체, 실패하면 기존 파일을 그대로 둠, 정리는 `NonCancellable`)을 ffmpeg와 Deno에도 쓴다. yt-dlp의 설치 코드는 그대로 둔다.
- 받는 도중 데이터가 30초 넘게 오지 않으면 실패로 처리한다.
- 로그에는 설치 시작, 완료, 실패(사유)를 도구 이름과 함께 INFO/WARN으로 남긴다. 오류 문구 속의 경로는 기존 규칙대로 가려서 적는다.

### 4.5 ffmpeg 부분 내려받기

gyan.dev는 Range 요청을 지원한다(`Accept-Ranges: bytes`). 전체 zip(114.8MB) 대신 `ffmpeg.exe` 항목만 받는다.

1. 고정 주소 `ffmpeg-release-essentials.zip`의 리다이렉트를 따라가 최종 주소(버전이 들어간 파일)를 얻고, 이후 요청은 모두 그 주소로 보낸다.
2. 파일 끝에서 End of Central Directory를 읽고, 이어서 중앙 디렉터리(약 6KB)를 읽는다. 오프셋은 고정값을 쓰지 않는다.
3. 이름이 `/bin/ffmpeg.exe`로 끝나는 항목을 찾는다. 정확히 하나여야 한다. 압축 방식, 압축 크기, 원본 크기, CRC32, 로컬 헤더 위치를 읽는다.
4. 로컬 헤더를 읽어 데이터 시작 위치를 구하고, 압축 데이터만 Range로 받아 풀면서(`Inflater`) `ffmpeg.new.exe`에 쓴다. 푸는 동안 CRC32와 크기를 세어서 디렉터리의 값과 대조한다. 어긋나면 실패다.
5. 실행 확인: `ffmpeg.new.exe -version`의 첫 줄이 `ffmpeg version`으로 시작해야 하고, `-hide_banner -encoders` 출력에 `libmp3lame`이 있어야 한다. 하나라도 어긋나면 실패다.
6. 통과하면 `ffmpeg.exe`로 교체한다.

대체 경로(Range를 지원하지 않거나, 중앙 디렉터리 구조를 읽지 못하거나, Zip64일 때): 전체 zip을 임시 파일로 받고 `ZipInputStream`으로 같은 항목만 꺼내 5번부터 같게 처리한다.

ffprobe는 기본으로 받지 않는다(8절의 확인 항목 1).

### 4.6 Deno 설치

zip을 받고(`deno.exe` 하나, 42.6MB) 이름이 `deno.exe`인 항목만 꺼내 `deno.new.exe`로 두고, `--version`이 기존 파서(`deno 2.x.y …`의 둘째 토큰)로 2.3.0 이상으로 읽혀야 교체한다.

### 4.7 테스트

- 엔진 단위 테스트 (테스트에서 만든 zip과 가짜 fetch/runner 사용):
  - 정상 설치
  - 다운로드 실패
  - zip에 대상 항목 없음 또는 둘 이상
  - CRC 불일치 또는 크기 불일치
  - 실행 확인 실패나 `libmp3lame` 없음일 때 기존 파일 보존
  - Range 미지원일 때 대체 경로
  - 임시 파일 정리
- 서버 경로 테스트와 `XgsApi` 계약 테스트(`HttpXgsApiTest`, `ContractSupport`)에 새 호출을 추가한다.
- `AppStateHolderTest`:
  - 모두 있으면 모달 없음
  - 빠진 목록이 정확함
  - 설치 순서
  - 일부 실패해도 나머지 계속
  - 실패한 것만 재시도
  - 거절하면 조회 막힘
  - 설치 뒤 풀림
  - 도구 확인 중에는 조회 막힘
- 통합 테스트(`:engine:integrationTest`, 네트워크가 있을 때만): 임시 폴더에 세 가지를 실제로 받아 실행 확인까지 하고, 받은 ffmpeg로 기존 `RealFfmpegTaggingIntegrationTest`를 통과시킨다.

## 5. 2부: 단일 exe 포장

### 5.1 빌드 환경

jpackage와 `jmods`가 있는 JDK 21이 필요하다. 지금 쓰는 JetBrains Runtime(`C:\bin\Java\jbr-21.0.9`)에는 둘 다 없고, `~/.gradle/jdks`의 Temurin 17은 앱이 21용이라 쓸 수 없다. WiX는 필요 없다(앱 이미지만 만든다).

- Temurin 21 zip(`api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jdk/hotspot/normal/eclipse`, 2026-10-09 기준 `21.0.12.1`, 205MB)을 `C:\bin\Java\jdk-21.0.12.1+1`처럼 프로젝트 밖에 푼다. 받기 전에 파일명, 출처, 크기를 알리고 허락을 받는다.
- 시스템 PATH와 `JAVA_HOME`은 바꾸지 않는다. 포장 스크립트가 환경 변수 `XGS_JDK`로 이 폴더를 받아, 그 Gradle 호출에만 `JAVA_HOME`으로 쓴다. `XGS_JDK\bin\jpackage.exe`가 없으면 이유를 알려 주고 멈춘다.
- Gradle은 `--no-daemon`으로 부른다.

### 5.2 앱 이미지와 검증

`:app:createDistributable`이 `app/build/compose/binaries/main/app/xGetSongs/` 폴더(`xGetSongs.exe`, `app/`, `runtime/`)를 만든다. jlink 런타임은 필요한 모듈만 담는데 플러그인이 자동으로 찾지 못한다. 지금 `modules(...)`는 3개뿐이고 패키지 실행은 한 번도 검증된 적이 없으므로 이 단계가 가장 큰 위험이다.

- `suggestModules`로 후보를 얻고, 필요하면 `includeAllModules = true`로 시작해서 줄여 가며 최종 모듈 목록을 `build.gradle.kts`에 명시한다.
- 깨끗한 PC를 흉내 내서 검증한다. `APPDATA`를 빈 임시 폴더로 지정하고, PATH에서 ffmpeg, Node, Deno, yt-dlp를 뺀 채 `xGetSongs.exe`를 실행한다.
  - 창이 뜨고, 내장 서버가 뜨고, 로그가 남는다.
  - 1부 모달이 뜨고 세 가지를 실제로 받아 설치한다(GitHub와 gyan.dev로의 HTTPS가 패키지 런타임에서 되는지 함께 확인된다).
  - 실제 곡 하나를 mp3로 받아 태그까지 확인한다.

### 5.3 단일 exe로 감싸기

앱 이미지 폴더를 exe 하나로 묶는다. 후보 둘을 같은 이미지로 만들어 비교하고 정한다.

| 후보 | 방식 | 확인할 점 |
|---|---|---|
| 7-Zip SFX | 실행할 때마다 임시 폴더에 풀고, 끝나면 지운다 | 7-Zip에는 `7z.sfx`/`7zCon.sfx`만 있고 풀고 실행해 주는 설치용 모듈이 없어 7-Zip Extra에서 따로 받아야 한다 (받기 전에 허락) |
| warp-packer | 처음 한 번만 풀고 캐시를 다시 쓴다 | 유지보수 상태 불명. 받기 전에 출처와 크기를 알리고 허락을 받는다 |

판정은 이 PC에서 두 후보를 같은 조건으로 재서 한다. 두 번째 이후 실행에서 창이 뜨기까지 걸린 시간(실행 시각부터 로그의 시작 줄 시각까지, 3회 중앙값)이 5초 이하면 7-Zip SFX를 택한다. 넘으면 warp-packer를 택한다. 함께 기록할 것: 첫 실행 시간, Windows Defender 반응, 비정상 종료 뒤 남는 임시 폴더. 둘 다 5.2의 검증을 통과하지 못하면 이 스펙으로 돌아와 다시 정한다(Nucleus의 `TargetFormat.Portable`은 문서에 출력 형태 설명이 없어 이번에는 쓰지 않는다).

### 5.4 산출물

- `dist/xGetSongs.exe` 하나. `dist/`는 `.gitignore`에 넣는다.
- 루트의 `package-exe.bat` 하나가 JDK 확인, `createDistributable`, 감싸기를 순서대로 하고(기존 `run.bat`과 같은 위치와 방식), README에 빌드와 배포 방법을 적는다.

### 5.5 알려진 한계

- 서명이 없어서 처음 받은 PC에서 SmartScreen이 "알 수 없는 게시자" 경고를 낼 수 있다.
- 아이콘이 없어서 기본 Java 아이콘이 나온다.
- 압축을 푸는 방식이라 첫 실행은 느릴 수 있다.
- 앱을 새 버전으로 바꾸려면 새 exe로 교체한다. yt-dlp는 기존 업데이트 버튼으로 따로 갱신한다.

## 6. 동시 진행 중인 작업과의 관계

같은 저장소에서 구글 가사 조회(`docs/superpowers/specs/2026-10-08-google-lyrics-card-design.md`)가 진행 중이고, `engine`이 JavaFX 21.0.12(`base`, `graphics`, `controls`, `media`, `web`의 `win` 분류) 의존성을 이미 갖고 있다. 이 때문에 2부에서 다음을 추가로 본다.

- JavaFX는 클래스패스로 올라가며 네이티브 라이브러리를 jar에서 꺼내 쓰는 구조일 가능성이 있다. 앱 이미지에서 숨김 웹뷰가 실제로 뜨는지 검증한다.
- jlink 런타임에 JavaFX가 요구하는 JDK 모듈이 더 필요할 수 있다.
- 앱 이미지와 exe의 크기가 JavaFX만큼 커진다. 5.3의 판정 시간에 이미 반영된다.
- 구글 가사 조회가 구현에 포함돼 있으면 5.2의 검증에 가사 조회 한 번을 더한다.

그 작업의 코드는 이 스펙이 건드리지 않는다.

## 7. 문서 갱신

구현 중에 함께 고친다.

- `README.md`: "필요한 것" 표를 "앱이 시작할 때 점검하고 설치"로 바꾸고, "실행"에 단일 exe 사용법과 빌드 방법을 더하고, "알려진 제한"의 `packageMsi` 항목과 ffmpeg 항목을 정리한다.
- `2026-10-04-xgetsongs-design.md`: 4절 배포 행("Compose `packageMsi`/exe")과 11절의 검증 항목을 이 스펙과 맞춘다.

## 8. 구현 시 검증할 사항

각 항목은 결과에 따라 이 스펙을 고치거나 선택을 정한다.

1. **ffprobe가 필요한가**: 실제 yt-dlp로 `-x --audio-format mp3` 변환이 ffprobe 없이 되는지 본다. 안 되면 ffprobe도 4.5와 같은 방식으로 받는다(+37.3MB). 둘 다 필요하면 `essentials.7z`(세 파일 35.4MB)를 다시 비교한다.
2. **Deno zip의 실제 구조**(`deno.exe`의 위치)와 `--version` 출력이 기존 파서와 맞는지.
3. **VC++ 재배포 패키지가 없는 PC에서 `yt-dlp.exe`가 실행되는지**. 안 되면 설치 실패 문구에 그 가능성을 안내한다.
4. **리다이렉트 뒤의 최종 주소에서도 Range가 되는지**, 그리고 Range가 안 될 때의 대체 경로가 실제로 동작하는지.
5. **essentials 빌드에 `libmp3lame`이 있는지** (4.5의 실행 확인이 이를 매번 검사한다).
6. **jlink 모듈 목록**(HTTPS, logback, Netty, JavaFX를 포함해)과 JavaFX 네이티브 처리.
7. **7-Zip 설치용 모듈과 warp-packer의 입수와 동작**, Defender와 SmartScreen 반응.

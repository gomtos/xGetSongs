# xGetSongs 설계

작성일: 2026-10-04

## 1. 목표

YouTube 재생목록 ID를 입력하면 목록의 모든 영상에서 오디오를 추출해 mp3로 저장하고, 영상 주소 1개를 입력하면 그 영상의 오디오만 mp3로 저장하는 앱.

- 파일명 형식: `{순위번호 001~999} {가수명} - {제목}.mp3`
  - 예: `001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).mp3`
- 1단계는 Windows 데스크톱 앱, 이후 웹으로 이전한다.

## 2. 확정된 결정

| 항목 | 결정 |
|---|---|
| 1단계 형태 | Windows 데스크톱 앱 |
| 기술 스택 | Kotlin Multiplatform (Kotlin 2.x, Gradle Kotlin DSL, JDK 21) |
| 아키텍처 | 항상 클라이언트-서버. 데스크톱 앱이 Ktor 서버를 localhost로 내장 실행 |
| 순위번호 | 플레이리스트 내 위치(1부터). 단일 영상은 기본 `001`이고 사용자가 1~999 지정 가능 |
| 가수명/제목 표기 | 원문 그대로. 괄호와 병기 표기를 보존하고 따옴표와 노이즈만 제거 |
| 가수명 추출 실패 시 | 자동 추출 + 폴백. 폴백은 채널명을 쓰되 미리보기에 ⚠ 표시 |
| 미리보기 | 다운로드 전 목록에서 결과 파일명을 읽기 전용으로 확인 |
| 웹 이전 전제 | 본인/지인용 자체 호스팅(인증 뒤). 불특정 다수 공개 서비스는 범위 밖 |

## 3. 범위 밖 (YAGNI)

- 미리보기에서의 파일명 수정, 항목별 선택 다운로드, 태그 값을 사용자가 고치는 기능(태그는 자동으로만 채운다)
- Android/iOS 타깃, 다국어 UI
- 앱 재시작 후 작업 복구(작업 큐 영속화)
- 불특정 다수 대상의 공개 웹 서비스

## 4. 기술 스택

| 영역 | 선택 |
|---|---|
| UI | Compose Multiplatform. 1단계는 desktop(JVM), 웹 이전 시 `wasmJs` 타깃 추가 |
| 서버/통신 | Ktor 서버 + Ktor 클라이언트, kotlinx.serialization, SSE |
| 다운로드 엔진 | `yt-dlp` + `ffmpeg` (외부 프로세스 호출). YouTube를 읽으려면 yt-dlp가 JavaScript 런타임(Deno 2.3+ 또는 Node 22+)도 필요로 한다. |
| 테스트 | kotlin.test, Ktor test host |
| 배포 | 데스크톱은 Compose `packageMsi`/exe, 웹은 서버 Docker 이미지 |

개발 환경 현황: JDK 21, Node, Python 3.13, ffmpeg가 설치돼 있고 yt-dlp는 없다.

## 5. 모듈 구조

```
xGetSongs/
├─ shared/   (commonMain, 순수 Kotlin, I/O 없음)
│    모델·API DTO, InputClassifier, TitleParser, FilenameFormatter
├─ engine/   (JVM)
│    YtDlpRunner, PlaylistResolver, DownloadJob, OutputSink 인터페이스 + LocalFolderSink
├─ server/   (JVM, Ktor)
│    API 라우트. engine을 호스팅
└─ app/      (Compose Multiplatform)
     UI + ApiClient. desktopMain이 내장 서버를 localhost로 실행
```

의존 방향: `app → shared`, `server → engine → shared`. 데스크톱 타깃(`desktopMain`)만 `server`를 참조해 내장 실행한다.

`shared`는 파일 시스템과 네트워크를 모르는 순수 로직이다. 파서를 빠르고 결정적으로 테스트할 수 있고, Wasm에서도 그대로 재사용된다. 로컬 경로 같은 데스크톱 전용 가정은 `OutputSink` 뒤로만 숨긴다.

## 6. 핵심 동작

데이터 흐름: `입력 → 판별 → resolve → 파싱 → 미리보기 → 다운로드`

### 6.1 입력 판별 (InputClassifier)

| 입력 | 판정 |
|---|---|
| `PL`/`UU`/`LL`/`FL`/`OL`로 시작하고 12자 이상인 ID | 재생목록 (11자 영상 ID가 `PL`로 시작하는 경우와 구분하기 위해 길이를 함께 본다) |
| URL에 `list=`가 있음 | 재생목록 (`v=`도 있으면 미리보기에 "이 영상만" 전환 버튼 표시) |
| `youtu.be/ID`, `watch?v=`, `/shorts/ID`, 11자 영상 ID | 단일 영상 |
| `RD…` 믹스 목록, YouTube 외 도메인 | 거부하고 오류 안내 |

판별 후에는 원문을 버리고 ID만 추출해 정규 URL(`https://www.youtube.com/playlist?list=ID` 또는 `https://www.youtube.com/watch?v=ID`)로 다시 조립한다. 이 URL만 yt-dlp에 **인자 배열로** 전달하고 셸은 쓰지 않는다. 인자 주입과 SSRF를 막기 위해서다.

### 6.2 제목 파싱 (TitleParser)

`(제목, 채널명, 메타데이터) → (가수, 곡, 신뢰도)`를 돌려주는 순수 함수. 순서는 다음과 같다.

1. **정리**
   - 앞쪽의 이모지와 `[MV]`, `[LIVE]` 같은 태그를 제거한다. 제목 맨 앞의 `[...]` 블록은 내용과 상관없이 모두 태그로 보며, 그 뒤에 내용이 남을 때만 제거한다.
   - ` | ` 이후는 통째로 버리고, 끝의 `[…]` 블록도 버린다.
   - 끝의 노이즈 토큰은 표(데이터)로 관리한다. 초기 목록은 `Official Music Video`, `Official MV`, `Official M/V`, `Official Video`, `Official Audio`, `M/V`, `MV`, `Music Video`, `Lyric Video`, `Performance Video`, `Special Video`, `Special Clip`, `Live Clip`, `Audio`, `Visualizer`이다.
2. **분리:** 첫 구분자(` - `, ` – `, `_ `)와 첫 여는 따옴표(`'` `‘` `“`) 중 **먼저 나오는 쪽**을 기준으로 나눈다.
   - 구분자가 먼저면 `가수 - 제목` 형태다. 제목을 감싼 따옴표는 벗긴다.
   - 따옴표가 먼저면 `가수 '제목'` 형태다. 닫는 따옴표 뒤는 전부 버린다.
   - 여는 따옴표는 앞에 공백이 있거나 문장 시작일 때만 인정한다. `Girls' Generation`의 아포스트로피가 여기서 걸러진다.
3. **메타데이터 보완:** 분리에 실패했을 때만 yt-dlp의 `artist`/`track`을 쓴다. 제목이 곡명뿐인 YouTube Music Topic 채널이 해당한다. 채널명이 ` - Topic`으로 끝나는 자동 생성 채널의 제목은 ` - `가 있어도 분리하지 않는다(곡명의 일부이기 때문).
4. **채널명 폴백:** 채널명에서 ` - Topic`과 `VEVO`를 떼어 가수명으로 쓰고, 제목은 정리된 전체 제목을 쓴다. 채널명도 없으면 가수명은 `Unknown Artist`다. 신뢰도를 낮음으로 표시하고 미리보기에 ⚠를 보인다. 다운로드 시작 시 yt-dlp의 전체 메타데이터로 한 번 더 파싱하므로 최종 파일명이 미리보기와 달라질 수 있다.

가수명과 제목은 원문 그대로 둔다. 괄호, 병기, 공백 표기를 바꾸지 않는다.

**테스트 픽스처 (참조 플레이리스트 `Melon Daily Top 100`에서 확인한 제목)**

| 원제목 | 가수 | 제목 |
|---|---|---|
| `소연 (SOYEON) '퇴사할게여 (Narr. 기안84)' Official Music Video` | `소연 (SOYEON)` | `퇴사할게여 (Narr. 기안84)` |
| `RESCENE (리센느) ‘LOVE ATTACK’ Official MV` | `RESCENE (리센느)` | `LOVE ATTACK` |
| `아이오아이 (I.O.I) - 갑자기 (Suddenly) MV` | `아이오아이 (I.O.I)` | `갑자기 (Suddenly)` |
| `ATEEZ(에이티즈) - 'BAD' Official MV` | `ATEEZ(에이티즈)` | `BAD` |
| `[MV] 태연 (TAEYEON)_ 만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1` | `태연 (TAEYEON)` | `만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1` |
| `WOODZ 'Drowning' Live Clip` | `WOODZ` | `Drowning` |
| `🎤진영&최유리 - 생각을 멈추다 보면 \| 들어봐! 유리의 숲2 EP.01 진영 편` | `진영&최유리` | `생각을 멈추다 보면` |
| `성시경 - 너의 모든 순간 [유희열의 스케치북/You Heeyeol’s Sketchbook] \| KBS 210528 방송` | `성시경` | `너의 모든 순간` |
| `Girls' Generation-HRS 소녀시대-효리수 'Skibidi' Performance Video` | `Girls' Generation-HRS 소녀시대-효리수` | `Skibidi` |
| `볼빨간사춘기 BOL4 '여름아 부탁해' Special Clip (with 적재)` | `볼빨간사춘기 BOL4` | `여름아 부탁해` |
| ``AKMU - '어떻게 이별까지 사랑하겠어, 널 사랑하는 거지(How can I love the heartbreak, you`re the one I love)' M/V`` | `AKMU` | ``어떻게 이별까지 사랑하겠어, 널 사랑하는 거지(How can I love the heartbreak, you`re the one I love)`` |

위 표는 설계 단계에서 규칙에 손으로 대입한 기대값이다. 참조 목록 100개 중 앞 30개를 확인했으며, 구현 시 테스트로 고정한다.

### 6.3 파일명 생성 (FilenameFormatter)

형식: `{순위 3자리} {가수} - {제목}.mp3`

- **순번 옵션:** 파일명에 순위를 넣을지 고를 수 있다(기본은 넣음). 끄면 `{가수} - {제목}.mp3`이고 길이 제한은 같다(가수 80자, 이름 180자). 순위가 없으면 같은 가수와 제목의 영상이 한 재생목록에서 같은 이름이 될 수 있으며, 이때는 "기존 파일" 규칙(건너뛰기 또는 덮어쓰기)을 따른다. ID3 트랙 번호는 이 옵션과 상관없이 순위를 쓴다.
- **순위:** 플레이리스트 위치(1부터)다. 비공개나 삭제된 영상은 건너뛰되 번호는 비워 둔다. 999개를 넘는 목록은 앞 999개만 처리하고 경고를 표시한다.
- **금지 문자:** Windows 금지 문자 `\ / : * ? " < > |`는 모양이 같은 전각 문자(`＼ ／ ： ＊ ？ ＂ ＜ ＞ ｜`)로 바꾼다. 제어 문자는 제거한다. 끝의 공백과 마침표는 제거한다. 파일명이 숫자로 시작하므로 `CON`, `NUL` 같은 예약어는 발생하지 않는다.
- **길이:** 확장자를 뺀 이름을 180자로 제한한다. 넘으면 순위와 가수를 보존하고 제목 끝을 `…`으로 줄인다. 가수명 자체가 80자를 넘으면 가수명도 `…`으로 줄인다. 제목이 비어 있으면 `untitled`를 쓴다.
- **이름 결정 주체:** yt-dlp는 임시 이름(`{영상ID}.mp3`)으로 받고, 엔진이 최종 이름으로 `OutputSink`에 넘긴다.
- **기존 파일:** 같은 이름이 이미 있으면 기본은 건너뛰기이고, UI에서 덮어쓰기를 선택할 수 있다.
- **mp3 품질:** VBR 최고 품질(`--audio-quality 0`)로 고정이다. 품질을 고르는 설정은 이후 단계로 미룬다.

### 6.4 저장 폴더와 ID3 태그

- **저장 폴더:** 재생목록은 `<출력 폴더>\<재생목록 이름>\` 아래에 저장한다. 폴더 이름은 파일명과 같은 규칙으로 정리하고(금지 문자는 전각 문자로, 제어 문자와 끝의 공백·마침표 제거) 80자로 제한한다. 첫 마침표 앞부분이 `CON`, `NUL`, `COM1` 같은 Windows 예약어이면 그 뒤에 `_`를 붙이고(`CON` → `CON_`, `con.txt` → `con_.txt`), 이름이 비면 `재생목록`을 쓴다. 단일 영상은 폴더를 만들지 않고 출력 폴더에 바로 저장한다. 같은 이름의 재생목록은 같은 폴더를 쓰고, 같은 파일명은 6.3의 "기존 파일" 규칙을 따른다.
- **태그 쓰기:** yt-dlp가 mp3를 만든 뒤 엔진이 ffmpeg로 스트림 복사(재인코딩 없음)하면서 ID3v2.3을 쓴다(한글처럼 ASCII가 아닌 값은 UTF-16, ASCII뿐인 값은 ISO-8859-1로 ffmpeg가 정한다). 값은 명령줄이 아니라 ffmetadata 파일로 넘겨 따옴표 같은 문자가 깨지지 않게 한다. ID3v1은 쓰지 않는다. 태그를 쓰지 못하면 그 항목은 실패로 처리하며 "실패한 것만 다시"로 재시도한다.
- **값:** 제목(TIT2)은 파싱한 제목, 가수(TPE1)와 앨범 아티스트(TPE2)는 파싱한 가수다. 파일명과 달리 금지 문자를 바꾸거나 길이를 줄이지 않은 원문이다. 앨범(TALB)은 재생목록 제목이고 단일 영상에는 쓰지 않는다. 트랙 번호(TRCK)는 순위(앞의 0 없는 정수), 주석(COMM)은 영상 URL이다. 연도는 쓰지 않는다.
- **커버:** yt-dlp가 받은 썸네일을 JPEG로 바꿔 가운데를 정사각형으로 잘라 넣는다(APIC, 앞표지). 썸네일이 없으면 커버 없이 태그만 쓴다.

## 7. 작업(Job)과 API

**상태**
- 작업: `Resolving → Ready → Running → Completed / Cancelled / Failed`
- 항목: `Pending → Downloading(%) → Converting → Done(최종 파일명)` 또는 `Skipped(사유)`, `Failed(사유)`

**API (Ktor)**

| 엔드포인트 | 역할 |
|---|---|
| `POST /resolve` | 입력 판별과 목록 조회. 항목별 `{순위, 영상ID, 제목, 예상 파일명, ⚠, 사용 가능 여부}` 반환 |
| `POST /jobs` | 다운로드 시작. 서버는 `POST /resolve` 결과를 `resolveId`로 최근 20개까지 보관하고, 이 요청은 `resolveId`, 옵션(출력 폴더, 덮어쓰기, 파일명에 순위 포함 여부, 단일 영상의 순위 번호, 동시 개수)과 선택적 `ranks`(실패 항목 재시도용)를 받는다. `jobId` 반환 |
| `GET /jobs/{id}/events` | SSE 진행 이벤트: `item-started`, `progress`, `item-done`, `item-skipped`, `item-failed`, `job-done` |
| `DELETE /jobs/{id}` | 취소. 실행 중인 yt-dlp 프로세스를 종료하고 임시 파일을 지운다. 완료된 파일은 유지 |
| `GET /tools` | yt-dlp, ffmpeg, JS 런타임의 존재 여부와 버전 |
| `POST /tools/yt-dlp/install` | yt-dlp를 앱 전용 폴더에 내려받는다(UI가 먼저 사용자 동의를 받는다) |
| `POST /tools/yt-dlp/update` | `yt-dlp -U` 실행 |

**동작 규칙**
- 동시 다운로드는 기본 2개이고 설정에서 1~4개로 바꿀 수 있다.
- 진행률은 yt-dlp의 `--progress-template`로 받아 퍼센트를 계산한다. ffmpeg 변환 구간은 퍼센트 없이 "변환 중"으로 표시한다.
- 작업 종료 시 `성공 N / 건너뜀 N / 실패 N` 요약을 보여주고, 실패 항목만 다시 시도하는 기능을 제공한다.
- 미리보기의 파일명은 resolve 시점의 예상값이다. 다운로드 시점에 얻은 메타데이터로 이름이 달라지면 `item-started` 이벤트로 최종 이름을 UI에 알린다.
- 작업의 이벤트 스트림은 구독자 한 명만 가질 수 있다. 연결이 끊기면 같은 작업에 다시 붙을 수 없고 취소만 가능하다(재연결 지원은 5단계에서 검토).

## 8. 오류 처리

| 상황 | 처리 |
|---|---|
| 잘못된 입력 | 입력창 아래에 즉시 오류 안내 |
| 재생목록이 비공개이거나 없음 | resolve 단계에서 중단하고 사유 표시 |
| 항목이 삭제, 비공개, 지역 제한, 연령 제한 | `Skipped(사유)` 처리, 순위 번호는 비워 둠, 나머지는 계속 진행 |
| 일시적 네트워크 오류나 429 | 지수 백오프로 자동 재시도 2회, 그래도 실패하면 `Failed` |
| yt-dlp 비정상 종료 | stderr 마지막 몇 줄을 항목 오류에 첨부 |
| 디스크 부족이나 쓰기 권한 없음 | 작업 전체를 중단 |
| yt-dlp, ffmpeg 또는 JS 런타임 없음 | 시작 시 `/tools`로 점검하고 설치 안내 표시 |

- 작업별 임시 폴더에서만 작업하고 성공했을 때만 `OutputSink`로 옮긴다. 앱 시작 시 이전에 남은 임시 파일을 정리한다.
- 한 항목의 실패는 전체를 멈추지 않는다. 전체 중단은 디스크와 권한 오류뿐이다.
- yt-dlp는 YouTube 변경으로 자주 깨지므로 앱 전용 폴더에 바이너리를 두고 "업데이트" 기능을 제공한다. ffmpeg는 앱 전용 폴더(`bin`)에 있으면 그것을, 없으면 PATH의 것을 쓴다. 경로를 따로 지정하는 설정은 이후 단계로 미룬다. 바이너리를 내려받기 전에 사용자 동의를 받는다.

## 9. 테스트 전략

| 계층 | 방법 |
|---|---|
| `shared` | 가장 두껍게. 표 기반 단위 테스트에 6.2의 픽스처와 합성 엣지 케이스(따옴표 안의 대시, 전각 변환, 999개 초과, 길이 초과, 예약 문자)를 쓴다. |
| `engine` | 프로세스 실행기를 가짜로 주입해 진행률 파싱, 취소, 재시도를 테스트한다. yt-dlp 출력 샘플은 픽스처로 저장한다. |
| `server` | Ktor `testApplication`으로 API 계약(SSE 이벤트 순서, 취소)을 검증한다. 엔진은 가짜를 쓴다. |
| UI | 화면 상태를 StateHolder로 분리해 JVM 단위 테스트로 검증한다. Compose UI 테스트는 최소한만 둔다. |
| 통합 | 실제 yt-dlp와 ffmpeg로 짧은 영상 1개와 작은 목록을 받는다. 외부 의존이 커서 기본 테스트에서 제외하고 수동 태그로만 실행한다. |

구현은 TDD 순서(`shared` → `engine` → `server` → `app`)로 진행한다.

## 10. 웹 이전

### 10.1 단계

| 단계 | 작업 |
|---|---|
| 1 | Windows 데스크톱 앱 + 내장 Ktor 서버 (전체 구현) |
| 2 | 서버를 단독 실행(JAR/Docker)으로 분리. `main()`만 분리하면 된다 |
| 3 | `DownloadStreamSink` 추가: 서버 임시 저장 → `GET /jobs/{id}/files/{name}`, 재생목록은 `archive.zip` 스트리밍, TTL(예: 1시간) 후 정리 |
| 4 | `app`에 `wasmJs` 타깃 추가. 폴더 선택과 내장 서버 기동은 `PlatformCapabilities`(`expect/actual`) 뒤로 분리 |
| 5 | 다중 사용자 대응: 세션별 작업 소유, 사용자별 동시 작업 제한, 서버 전체 yt-dlp 프로세스 수 제한, 요청 속도 제한 |

### 10.2 1단계부터 지킬 규칙

- `shared`에는 `java.*`를 쓰지 않고 순수 Kotlin만 사용한다. `jvm()` 타깃 하나뿐일 때는 컴파일러가 이를 막아 주지 않으므로 Gradle 태스크 `checkCommonPurity`가 검사한다.
- UI는 서버 API로만 데이터를 가져온다. 로컬 경로는 UI 상태에 직접 두지 않고 capability 뒤에 둔다.
- 내장 서버는 `127.0.0.1`에만 바인딩하고, 기동 시 생성한 임의 토큰 헤더를 요구하며 `Origin`을 검사한다. 서버가 `outputDir`을 받아 로컬 디스크에 쓰기 때문에, 이 보호가 없으면 방문한 웹페이지가 localhost 서버에 요청을 보내 파일을 쓸 수 있다.
- 서버에 `LOCAL`과 `HOSTED` 모드를 두고 `outputDir` 옵션은 `LOCAL`에서만 허용한다.

### 10.3 위험과 제약

1. **Compose for Web은 Beta다.** 확인한 최신 정보는 1.9.0(2025년 9월) Beta이고, 이후 stable 여부는 확인하지 못했다. Wasm은 Skia 캔버스로 글자를 그리므로 한글 표시를 위해 CJK 폰트 번들(수 MB)이 필요할 수 있다. 4단계 전에 현재 버전에서 검증한다. 최악의 경우 웹 UI만 HTML/React로 대체할 수 있다(API 계약이 클라이언트에 의존하지 않기 때문).
2. **결과물 전달 방식이 다르다.** 100곡 zip이 수백 MB~1GB가 될 수 있어 서버 임시 디스크 한도, 스트리밍 zip, TTL 정리가 필요하다.
3. **데이터센터 IP는 YouTube가 봇으로 차단하는 경우가 흔하다.** 쿠키 인증이 필요할 수 있다. 클라우드 호스팅 전에 실제로 확인한다.
4. **공개 서비스는 권장하지 않는다.** YouTube 서비스 약관은 허가 없는 다운로드를 금지하고 음악은 저작권 대상이다. 본인과 지인용으로 인증 뒤에 자체 호스팅하는 형태를 전제로 한다.

### 10.4 이전 비용 요약

- 그대로 재사용: `shared`와 `engine` 전부, `server`의 기존 API, `app` UI 대부분
- 새로 만들 것: `DownloadStreamSink`, 파일/zip 라우트와 TTL 정리, 세션 인증과 쿼터, `wasmJs` 타깃과 폰트, 배포 설정

## 11. 구현 시 검증할 사항

- `--flat-playlist -J` 출력은 yt-dlp 2026.08.19로 참조 재생목록(100개)에서 확인했다. 각 항목은 객체이며 `id`, `title`, `channel`, `uploader`, `availability`(공개 영상은 `None`), `duration`을 가진다. 평탄 출력에는 `artist`/`track`이 없으므로 이 값은 다운로드 시점의 전체 메타데이터에서만 얻는다.
  - 아직 확인하지 못한 것: 참조 재생목록에 삭제·비공개 항목이 없어 그 표현은 실제 출력으로 보지 못했다. 코드는 제목 `[Private video]`/`[Deleted video]`와 `availability` 값으로 판별한다. 해당 항목을 만나면 이 줄을 갱신한다.
  - 확인 결과로 바뀐 규칙: 제목 앞의 `[LIVE]`, `[Ballad]`, `[선공개]`처럼 목록에 없는 대괄호 태그가 가수명에 섞였으므로, 제목 맨 앞의 `[...]` 블록은 모두 태그로 보고 제거한다(남는 내용이 있을 때만).
- Compose Multiplatform for Web의 현재 안정성 수준과 CJK 폰트 처리 방식.
- 대상 호스팅 환경에서 YouTube가 서버 IP 요청을 허용하는지.

# 출력 형식을 mp3에서 m4a 원본으로 바꾸기

작성일 2026-10-10. 사용자가 준 "yt-dlp로 원음 품질 그대로 받기" 가이드(재인코딩 없이 스트림을 그대로 저장)를 이 앱에 맞춰 반영한다.

## 목표

YouTube가 주는 AAC 오디오(itag 140)를 재인코딩 없이 `.m4a`로 저장한다. mp3 출력은 없앤다. 파일 안의 태그와 커버는 지금처럼 ffmpeg가 스트림 복사 한 번으로 쓴다.

## 배경과 한계

- 지금 앱은 `-x --audio-format mp3 --audio-quality 0`만 쓰고 형식을 고르지 않는다. 그래서 YouTube의 최고 스트림인 Opus 251(138kbps, 48kHz)을 받아 디코드한 뒤 MP3 V0(약 245kbps)로 다시 인코딩한다.
- 새 방식은 AAC 140(130kbps, 44.1kHz)을 그대로 복사한다. **음질이 좋아지는 변경이 아니다.** 소스가 Opus에서 AAC로 바뀌므로 소리는 같은 수준이고 이론상 약간 낮을 수 있다. MP3 V0가 충분히 높은 비트레이트라 두 방식의 차이는 귀로 구분하기 어렵다. 2026-10-10에 사용자가 이 점을 알고 선택했다(대안이던 Opus `.opus` 저장과 mp3 유지는 채택하지 않았다).
- 얻는 것은 파일 크기(2026-10-09 실험의 10분 34초 곡에서 mp3 18.0MB, m4a 10.3MB), mp3 변환 시간(그 곡에서 9.9초), 재인코딩이 없다는 것, 직접 만든 ID3 코드의 제거다. 에뮬레이터 재생은 사용자가 확인했다(샘플 m4a).

## ffmpeg와 yt-dlp로 확인한 사실 (2026-10-10, 영상 `kcx0a2OAhN0`)

- `-f bestaudio[ext=m4a]`는 itag 140을 고른다.
- yt-dlp가 받은 원본은 조각난 DASH MP4다(`major_brand=dash`). `--fixup never`로 컨테이너 수정을 끄고 그대로 ffmpeg 태그 단계에 넣어도, 결과는 일반 `M4A ` 컨테이너이고 오디오 스트림 MD5가 원본과 같다. 그러므로 yt-dlp의 `FixupM4a`는 필요 없다.
- ffmpeg의 MP4 쓰기는 `title`, `artist`, `album_artist`, `album`, `track`, `comment`, `lyrics`를 각각 `©nam`, `©ART`, `aART`, `©alb`, `trkn`, `©cmt`, `©lyr`로 쓴다. ID3와 달리 코멘트와 가사도 ffmpeg가 직접 쓴다. 한글과 여러 줄 가사가 ffprobe에서 같은 값으로 읽힌다.
- 커버는 지금과 같은 `-disposition:v attached_pic` mjpeg 입력으로 붙는다(720×720).

## 설계

### 다운로드 명령 (`YtDlpCommands.download`)

- `-x --audio-format mp3 --audio-quality 0`을 `-f bestaudio[ext=m4a] --fixup never`로 바꾼다. 출력 템플릿은 그대로여서 파일은 `<작업 폴더>/<영상ID>.m4a`다.
- `--write-thumbnail --convert-thumbnails jpg`, `--write-info-json`, `--ffmpeg-location`은 그대로다. ffmpeg는 썸네일 변환과 태그 쓰기에 계속 필요하다.
- 후처리 진행 템플릿(`XGSPP`)은 뺀다. 아래 "진행 단계"를 본다.

### 태그 쓰기 (`Id3Tagger` → `M4aTagger`)

- 같은 흐름이다: ffmetadata 파일을 UTF-8로 쓰고, ffmpeg 한 번으로 `<이름>.tagged.m4a`를 만든 뒤 원본을 교체하고, 임시 파일은 항상 지운다. 실패하면 원본은 그대로이고 그 항목은 실패이며 "실패한 것만 다시"로 재시도한다. 실패 문구는 "태그를 쓰지 못했습니다: …"로 한다("ID3" 삭제).
- `FfmpegCommands.tag`: 출력은 `.m4a`여야 한다(ffmpeg가 확장자로 muxer를 고른다). `-id3v2_version`을 뺀다. 나머지(입력 순서, 커버 자르기, `-map_metadata`)는 같다.
- `Ffmetadata.render`: `comment`와 `lyrics`도 렌더링한다. 이스케이프 규칙은 그대로이고 가사의 줄바꿈은 LF다(ID3 때의 CRLF 변환은 없다).
- `Id3Frames`(COMM·USLT를 직접 이어 붙이던 코드)는 지운다. `TrackTags`의 설명은 ID3 프레임 이름 대신 MP4 항목 이름으로 고친다. 값의 규칙은 그대로다: 앨범은 폴더 이름과 같고, 앨범 아티스트는 모든 곡에서 `Various Artists`이며, 트랙 번호는 순위다.

### 진행 단계

- 지금의 "변환 중"은 yt-dlp의 후처리 시작 줄을 신호로 쓴다. 변환이 없어지면 이 신호는 `MoveFiles` 같은 무관한 후처리에 걸려 의미가 없고, 태그 쓰기와 가사 검색 구간에는 표시가 없다.
- `ItemDownloader`가 yt-dlp가 성공으로 끝난 직후 직접 `Progress(rank, FINISHING)`을 보낸다. 태그 쓰기와 가사 검색이 이 단계다.
- 이름을 바꾼다: `Stage.CONVERTING` → `Stage.FINISHING`, `ItemStatus.Converting` → `ItemStatus.Finishing`, 화면 문구 "mp3 변환 중…" → "마무리 중…". 서버와 앱은 함께 배포되므로 호환 문제가 없다.
- `ProgressParser`는 다운로드 줄만 해석하고 `ProgressUpdate.Converting`과 후처리 파싱을 지운다. `ProgressThrottle`은 다운로드 퍼센트만 거른다.

### 파일 이름

- `FilenameFormatter`의 확장자만 `.m4a`로 바꾼다. 규칙(순위, 길이 제한, 금지 문자, 예약어)은 그대로다. `ItemDownloader`가 받은 파일을 찾는 이름과 "찾을 수 없음" 문구도 `m4a`로 바꾼다.
- 출력 폴더에 이미 있는 `.mp3`는 건드리지 않는다. "기존 파일" 규칙은 이름이 같을 때만 작동하므로, 같은 곡을 다시 받으면 덮어쓰기가 아니라 `.m4a`가 옆에 따로 생긴다.

### 오류

- m4a 형식이 없는 영상은 yt-dlp가 `Requested format is not available`로 실패한다. `ErrorClassifier`가 이를 "m4a 오디오 형식이 없습니다."라는 문구의 `OTHER` 실패로 분류한다. YouTube 쪽 일시 문제일 수도 있어서 건너뜀(`UNAVAILABLE`)이 아니라 실패로 두고 재시도할 수 있게 한다. 다른 분류(비공개, 지역 제한, 일시 오류)가 먼저 적용된다.

## 테스트

- `YtDlpCommandsTest`: `-f bestaudio[ext=m4a]`와 `--fixup never`가 있고 `-x`, `mp3`, `--audio-quality`, 후처리 템플릿이 없다.
- `FfmpegCommandsTest`: `.m4a` 출력, `-id3v2_version` 없음, `.mp3` 출력은 거부.
- `FfmetadataTest`: `comment`와 `lyrics`가 렌더링되고 여러 줄 가사와 `=`, `;`, `#`, `\`가 이스케이프된다.
- `M4aTaggerTest`(`Id3TaggerTest`에서 바꿈): 실패 시 원본 유지, 임시 파일 정리, ffmpeg 없음.
- `ProgressParserTest`, `DefaultDownloadServiceTest`: 후처리 줄 해석 테스트를 지우고, 다운로드가 끝난 뒤 `FINISHING`이 한 번 나오는지 본다. 썸네일 변환이 "마무리"로 보이지 않는 테스트는 후처리 파싱 자체가 없어져 필요 없다.
- `ErrorClassifierTest`: 형식 없음 → "m4a 오디오 형식이 없습니다.", 다른 분류가 먼저 적용되는 경우.
- 앱·서버·공유 모듈 테스트: `Stage`와 `ItemStatus` 이름, "마무리 중…" 문구, 파일명 확장자 기대값(`.mp3` → `.m4a`)을 맞춘다. 확장자와 상관없는 예시 이름(로그 가리기, 탐색기 열기)은 바꾸지 않는다.
- 통합 테스트(`:engine:integrationTest`): 실제 yt-dlp로 받아 `.m4a`가 생기고 info의 `format_id`가 `140`이며 코덱이 `aac`인지, 실제 ffmpeg 태그 후 한글 제목·가사·코멘트·앨범·트랙·커버(정사각형)가 ffprobe로 읽히고 오디오 스트림 MD5가 태그 전과 같은지 확인한다(`RealFfmpegTaggingIntegrationTest`, `RealYtDlpIntegrationTest`).
- 삭제: `Id3Frames`, `Id3FramesTest`, `testutil/Id3v2Tag.kt`, `Fakes`의 mp3 태그 도우미, `Mp3Probe.kt`의 mp3 전용 부분(`Ffprobe`는 남긴다).

## 문서

- `README.md`: 첫머리, 파일명 형식과 예시, "ID3 태그"를 m4a 태그로, "필요한 것"의 ffmpeg 용도(태그·커버 쓰기와 썸네일 변환), "알려진 제한"의 mp3 품질 줄, 같은 곡을 다시 받으면 `.m4a`가 새로 생긴다는 설명.
- `2026-10-04-xgetsongs-design.md`: 1절 목표, 6.3의 파일명과 품질 줄, 6.4의 태그 쓰기와 COMM·USLT, 7절 항목 상태와 진행률 설명, 9·11절의 검증 항목.
- 지난 스펙·계획서(`2026-10-09-id3v24-utf8-*`, 그 외 `docs/superpowers/` 이력)는 고치지 않는다. 이 스펙이 ID3 쓰기를 대체한다.

## 위험

- m4a를 못 읽는 오래된 기기나 차량 오디오에서는 재생되지 않고, mp3로 돌아갈 선택지가 없다. 사용자 에뮬레이터에서 재생은 확인했다.
- 플레이어에 따라 `©lyr` 가사나 커버를 보여 주지 않을 수 있다. 구현 뒤 새로 받은 곡을 사용자가 쓰는 플레이어에서 한 번 확인한다.
- YouTube가 일부 영상에서 itag 140을 주지 않을 수 있다. 그 항목은 실패로 표시된다(위 "오류").
- 폴더에 mp3와 m4a가 섞이고, 같은 곡이 두 형식으로 남을 수 있다.

## 범위 밖

- 출력 형식을 고르는 설정, Opus `.opus` 저장, mp3 변환 토글, 기존 mp3 파일 변환, ffmpeg 제거, 단일 exe와 시작 시 구성요소 설치(계속 보류).

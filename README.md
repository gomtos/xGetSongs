# xGetSongs

YouTube 재생목록 ID를 입력하면 목록의 모든 영상에서 오디오를 추출해 mp3로 저장하고, 영상 주소 1개를 입력하면 그 영상의 오디오만 mp3로 저장하는 Windows 데스크톱 앱입니다. 나중에 웹으로 옮길 수 있게 설계했습니다.

파일명 형식: `{순위번호 001~999} {가수명} - {제목}.mp3`

예: `001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).mp3`

설계 문서: [docs/superpowers/specs/2026-10-04-xgetsongs-design.md](docs/superpowers/specs/2026-10-04-xgetsongs-design.md)

## 필요한 것

| 도구 | 용도 | 설치 |
|---|---|---|
| JDK 21 | 빌드와 실행 | |
| yt-dlp | 영상에서 오디오 추출 | 앱의 "yt-dlp 설치" 버튼 또는 `winget install yt-dlp.yt-dlp` |
| ffmpeg | mp3 변환 | `winget install Gyan.FFmpeg` |
| Node.js 22+ 또는 Deno 2.3+ | yt-dlp가 YouTube를 읽는 데 필요 | `winget install OpenJS.NodeJS` |

앱 첫 화면 위쪽에서 세 도구의 설치 여부와 버전을 확인할 수 있습니다.

## 실행

```powershell
.\gradlew.bat :app:run
```

## 테스트

```powershell
.\gradlew.bat check                    # 전체 단위 테스트
.\gradlew.bat :engine:integrationTest  # 실제 yt-dlp와 YouTube를 쓰는 통합 테스트 (도구가 없으면 건너뜀)
```

## 구조

```
shared/   순수 Kotlin: 입력 판별, 제목 파싱, 파일명 생성, API 모델 (웹/Wasm에서도 재사용)
engine/   yt-dlp·ffmpeg 호출, 재생목록 조회, 다운로드 작업 관리, 출력 저장소
server/   Ktor API 서버 (데스크톱 앱이 127.0.0.1에 내장 실행)
app/      Compose Multiplatform UI (지금은 desktop 타깃만)
```

## 알려진 제한 (1단계)

- mp3 품질은 VBR 최고 품질(`--audio-quality 0`)로 고정입니다. ffmpeg는 PATH 또는 `%APPDATA%\xGetSongs\bin`에서 찾으며, 따로 경로를 지정하는 설정은 아직 없습니다.
- 앱을 두 번 실행하지 마세요. 시작할 때 임시 폴더(`work/`)를 비우므로 먼저 실행한 앱의 진행 중 항목이 실패할 수 있습니다.
- 이벤트 연결이 끊기면 같은 작업에 다시 붙을 수 없고 취소만 됩니다.
- 요약의 "건너뜀" 개수에는 조회 단계에서 이미 사용할 수 없던 영상이 빠집니다(목록의 각 행에는 표시됩니다).
- ffmpeg가 없으면 항목마다 내려받은 뒤 실패합니다. 도구 패널의 안내를 먼저 확인하세요.
- `packageMsi`로 만든 설치 파일은 아직 검증하지 않았습니다. 실행은 `:app:run`을 쓰세요.

## 주의

- 내려받은 콘텐츠의 저작권과 YouTube 서비스 약관은 사용자의 책임입니다. 본인이 사용할 목적으로만 쓰세요.
- 내장 서버는 `127.0.0.1`에만 열리며, 실행할 때마다 새로 만든 토큰이 있어야 접근할 수 있습니다.

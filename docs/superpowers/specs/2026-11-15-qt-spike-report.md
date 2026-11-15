# Qt 스파이크(Q0) 보고서

작성일: 2026-11-15
설계: [2026-11-15-qt-rewrite-and-playlists-design.md](2026-11-15-qt-rewrite-and-playlists-design.md)
분석: [2026-11-06-qt-cpp-rewrite-analysis.md](2026-11-06-qt-cpp-rewrite-analysis.md) §9
계획: [2026-11-15-qt-spike.md](../plans/2026-11-15-qt-spike.md)

환경: Windows 11 Home 10.0.26200, Visual Studio 2022(vcvars64), vcpkg 2026-10-29 빌드, Qt 6.11.3. 측정은 같은 PC에서 스파이크 exe(`build/qt-spike/xgs-qt-spike.exe`)를 `qt/spike/check/*.ps1`로 돌려서 했다. 점검은 가짜 깨끗한 PC(`%TEMP%\xgs-qt-spike-<id>`, 빈 APPDATA·LOCALAPPDATA·TEMP, 최소 PATH)에서 돌렸고 사용자의 앱 데이터는 건드리지 않았다. 스파이크 코드는 버리는 코드다.

이 문서에서 MB는 MiB(1,048,576바이트)다. 지금 exe 90,381,205바이트 = 86.2MiB(앞선 문서들과 같은 단위)와 같은 단위로 맞추려고 그렇게 했다. 바이트 값을 같이 적는다.

## 요약

- 측정한 범위에서 Qt로 가는 것을 막는 결과는 없다.
- 하지만 관문 8개가 모두 통과한 것은 아니다. Q4(Defender·SmartScreen)는 이 PC에서 판정하지 못했고, Q6(입력·배율)은 입력만 확인되고 배율은 아직 확인하지 않았다.
- 그래서 결론은 조건부로 계속이다. 자세한 것은 마지막 "다음 단계(Q1)로 가도 되는지"에 있다.

## 관문

분석 문서 §9의 Q1~Q8이다. 멈춤 관문은 Q4, Q5, Q6이다.

| 번호 | 조건 | 결과 | 근거 (측정값, 어디서 나왔나) |
|---|---|---|---|
| Q1 | 깨끗한 Windows에서 exe가 뜨고 창이 보인다 | **통과** | 가짜 PC에서 10번(5라운드 x 첫 실행·두 번째 실행) 실행해서 모두 종료 코드 0, 모두 창이 보인 시각(`processStartToShownMs`)을 exe가 보고 파일에 남겼다. `start-time.json`의 `allExitZero: true`, 남은 폴더·프로세스 0. 창을 눈으로 본 것은 아니고 exe가 Show 이벤트와 첫 페인트를 기록한 값이다 |
| Q2 | 시작 시간, 첫 실행과 두 번째 실행 각각 5회 중앙값이 현행(3.7초 / 1.2초)보다 나쁘지 않다 | **통과** (같은 양이 아님, 아래 참고) | 첫 실행 중앙값 153ms, 두 번째 141ms. `start-time.json` |
| Q3 | exe 크기, 기록만 (추정 10~18MB) | **기록** | 21,746,176바이트(20.7MiB). 추정보다 크다 (아래 참고) |
| Q4 | Defender·SmartScreen이 막거나 경고하는가 | **판정 불가 (이 PC에서 확인하지 못함)** | 이 PC는 Windows Defender가 꺼져 있다(WinDefend 서비스 중지, AhnLab V3 Lite 등록). 통과도 실패도 아니다. 관찰한 것은 아래 "Q4" 절에 있다 |
| Q5 | Job Object로 yt-dlp 자손까지 5초 안에 정리 | **통과** | 시나리오 4개 모두 0까지 119~219ms, 2초 뒤에도 0 |
| Q6 | 한글 조합 입력, 붙여넣기, 선택, 우클릭 메뉴, 150%·200% 배율에서 글꼴 | **입력은 통과, 배율은 확인하지 않음 (사용자 확인 남음)** | 사용자가 직접 확인한 것과 아직 확인하지 않은 것은 아래 "Q6" 절에 있다 |
| Q7 | 숨긴 WebView2로 페이지를 열고 스크립트 결과를 받는다 | **통과** | 점검 페이지에서 `text` = `card of first`, `ticks` 14, 대기 815ms. 보이는 최상위 창 0개 |
| Q8 | `QNetworkAccessManager`로 lrclib.net을 Schannel로 부른다 | **통과** | `backend` = `schannel`, `ok` true, `status` 200 (1회 실행) |

## 시작 시간 (Q2)

가짜 PC에서 라운드마다 exe를 새로 복사해 첫 실행, 같은 파일로 두 번째 실행을 했다. 5라운드.

| 라운드 | 첫 실행 (ms) | 두 번째 실행 (ms) |
|---|---|---|
| 1 | 176 | 175 |
| 2 | 150 | 141 |
| 3 | 144 | 138 |
| 4 | 153 | 134 |
| 5 | 185 | 151 |
| 중앙값 | **153** | **141** |

현행 앱과 비교한다.

| | 첫 실행 | 두 번째 실행 | 잰 구간 |
|---|---|---|---|
| 스파이크 Qt exe | 153ms | 141ms | 프로세스 생성 → 첫 페인트 → idle (exe가 스스로 잼) |
| 현행 앱 | 3.7초 | 1.2초 | 사이드카 준비까지 (5단계 측정) |

두 값은 같은 양이 아니다. 스파이크의 값은 거의 빈 창이 뜨고 이벤트 루프가 한가해질 때까지이고, 현행의 값은 창이 뜬 뒤 사이드카가 준비될 때까지다. 실제 앱은 시작할 때 설정 읽기, 도구 확인, 목록 불러오기 같은 일을 더 한다. 그래서 153ms가 Qt 앱의 시작 시간은 아니다. 이 측정이 보여 주는 것은 Qt와 정적 링크 자체의 시작 비용이 작다는 것 정도다.

이 시작 시간은 Task 3 시점의 빌드(21,306,880바이트)로 잰 것이다. 그 뒤 WebView2와 TLS 점검 코드가 더해진 빌드로는 다시 재지 않았다. 5회 중앙값 한 세트만 있고, 콜드 스타트(재부팅 직후)는 재지 않았다.

덧붙임: 두 번째 실행 값(141ms)이 첫 실행 값(153ms)과 거의 같다. 이 PC에서는 새 exe 파일의 첫 검사 비용이 보이지 않았다. 이 PC의 실시간 검사기(V3 Lite)가 그 구간에서 무엇을 했는지는 확인하지 않았다.

## 빌드 환경

- Qt 6.11.3 (vcpkg 포트 `qtbase`, port-version 1). vcpkg 기준선 커밋 `4702e381dbbd9b54d3f7e1eb565cd8981c1814cd`. 저장소 밖 `C:\dev\vcpkg`에 둔다.
- 트리플렛 `x64-windows-static`: Qt와 CRT(/MT)를 정적으로 링크한다. TLS는 Schannel이다(OpenSSL 없음).
- 켠 qtbase 기능(`default-features: false`): `gui`, `widgets`, `network`, `thread`, `freetype`, `harfbuzz`, `png`, `doubleconversion`, `pcre2`. 켜지 않은 것: `openssl`, `sql*`, `dbus`, `opengl`, `icu`.
- `gui` 기능이 시스템 `opengl` 포트(헤더)를 의존성으로 끌어온다. qtbase의 `opengl` 기능은 꺼져 있다.
- vcpkg에는 `schannel` 기능이 따로 없다. openssl을 끄면 Windows에서 Schannel이 쓰인다.
- 그 밖의 의존성: `webview2`. 정적 로더 `WebView2LoaderStatic.lib`를 링크해서 `WebView2Loader.dll`이 필요 없다.

Qt 빌드의 비용이다.

| 항목 | 값 | 출처 |
|---|---|---|
| 처음 빌드에 걸린 시간 | 약 89분 (5,347초, 그중 qtbase 구성이 5,328초) | Task 1 보고, README |
| `C:\dev\vcpkg` 디스크 | 약 37GB (그중 buildtrees 약 30GB) | Task 1 보고 |
| vcpkg 바이너리 캐시 (`%LOCALAPPDATA%\vcpkg\archives`) | 약 1.2GB | Task 1 보고 |
| 이후 빌드 | 바이너리 캐시를 쓰므로 빠르다고 적었으나, 이후 재빌드 시간은 따로 재지 않았다 | |

buildtrees는 `--clean-buildtrees-after-build`로 줄일 수 있다(줄여 본 적은 없다). vcpkg 클론은 `--depth 1`인 얕은 클론이다. 기준선을 올리려면 `git fetch --unshallow`가 필요할 수 있다.

## exe 크기 (Q3)

| 빌드 | 바이트 | MiB |
|---|---|---|
| 첫 빌드 (빈 창) | 21,302,784 | 20.3 |
| 시작 시간을 잰 빌드 | 21,306,880 | 20.3 |
| 마지막 빌드 (WebView2·TLS 점검 포함) | 21,746,176 | 20.7 |
| 현행 `xGetSongs.exe` | 90,381,205 | 86.2 |

마지막 빌드는 현행의 약 24%다. 분석 문서의 추정(10~18MB)보다 크다. 추정이 틀렸고, 이유는 따로 조사하지 않았다. Qt Widgets를 정적으로 링크하고 `/MT`로 CRT를 넣었으며 `gui`가 Direct3D 쪽(`d3d11`, `dxgi`, `d3d12`, `DWrite`)을 쓰기 때문일 수 있지만, 부분별 크기를 재서 확인한 것은 아니다. 이 크기는 앱 코드가 거의 없는 스파이크의 값이다. 실제 앱이 되면 더 커진다.

`dumpbin /dependents`로 본 의존성은 Windows 시스템 DLL뿐이다. `Qt6*.dll`, `VCRUNTIME*`, `MSVCP*`, `libssl`, `libcrypto`, `WebView2Loader.dll`은 없다. 다만 **`icuuc.dll`을 가져온다.** 이것은 Windows 10 1903 이상에 들어 있는 시스템 ICU다. 그래서 이 exe는 Windows 10 1903 이상이라는 OS 버전에 기대는 exe다.

## Q4: Defender와 SmartScreen

**결과: 판정 불가 (이 PC에서 확인하지 못함).** 통과라고도 실패라고도 적지 않는다.

이 PC에서 확인한 것:

- `Get-MpComputerStatus`: `AMServiceEnabled` False, `AntivirusEnabled` False, `RealTimeProtectionEnabled` False. 서비스 WinDefend는 중지돼 있다.
- Windows 보안 센터에는 AhnLab V3 Lite와 Windows Defender가 등록돼 있다. 타사 백신(V3 Lite)이 켜져 있어서 Defender가 꺼진 것으로 보인다.
- `MpCmdRun.exe -Scan`은 `Failed with hr = 0x80004005`, 로그는 `Product/Feature disabled`였다. 종료 코드 2는 위협 발견이 아니라 검사 실패다. 이 PC에서 Defender 검사는 돌지 않았다.
- Defender 설정은 바꾸지 않았다(제외 항목 추가, 시료 제출 없음).

실제로 관찰한 것:

- 인터넷 표식(`Zone.Identifier`, `ZoneId=3`)을 붙인 exe를 `--exit-after-show`로 실행했더니 종료 코드 0이었고, 실행 뒤 파일의 길이와 SHA-256이 원본과 같았다.
- 사용자가 탐색기에서 표식이 붙은 exe를 더블클릭했을 때 경고가 뜨지 않았고, 입력란이 있는 창이 열렸다.

관찰하지 못한 것:

- Defender 자체의 반응(검사, 격리, 차단). 실시간 보호가 꺼져 있어서 위의 "실행"은 Defender 점검으로서 아무것도 증명하지 않는다.
- V3 Lite의 수동 검사. 사용자에게 부탁했지만 결과를 받지 못했다.
- 현행 exe(`xGetSongs-현행.exe`, 같은 표식)와 나란히 비교한 경고 차이. 사용자가 비교 결과를 알려 주지 않았다. 점검 스크립트는 현행 exe를 같은 표식으로 복사만 했고 실행도 검사도 하지 않았다.

설계 §4에서 Q4의 실패는 Defender가 exe를 격리·삭제·실행 차단하는 것이다. 서명하지 않은 exe에 뜨는 "알 수 없는 게시자" 경고는 현행 exe에도 뜨므로 실패가 아니다. 이 PC에서는 막는 일이 한 번도 관찰되지 않았지만, 막을 수 있는 검사기(Defender)가 꺼져 있었으므로 "막지 않았다"가 "막지 않을 것이다"를 뜻하지 않는다.

`defender.ps1`은 수정 라운드를 한 번 거쳤다(판정을 PASS·FAIL·판정 불가로 나누고, 차단돼도 결과를 남기게). 수정된 복사·검사·실행 경로 전체는 다시 돌리지 않았고 정적 확인과 판정표 자체 시험(9건 일치)으로만 확인했다.

## Q5: 자손 프로세스 정리

`JobProcess`는 이름 없는 Job을 만들고 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`를 건다. 자식은 `QProcess`로 `CREATE_SUSPENDED` 상태로 만들고, Job에 넣은 뒤 재개한다. 취소는 `TerminateJobObject`다.

시작 방식은 계획이 허용한 두 방법 중 첫 번째, `QProcess::setCreateProcessArgumentsModifier`로 `CREATE_SUSPENDED`를 주는 방법이다. `CreateProcessW`를 직접 부르는 방법으로 가지 않았다.

| 시나리오 | 내용 | 찾은 프로세스 | 0까지 | 2초 뒤 |
|---|---|---|---|---|
| A 취소 | `--spawn-tree 120`, exe가 1.5초 뒤 스스로 취소 | 5개 (`cmd.exe` x2, `conhost.exe`, `PING.EXE` x2) | 219ms | 0 |
| B 강제 종료 | 같은 트리, `PING.EXE`가 보이면 스파이크를 `Stop-Process -Force` | 5개 (같음) | 131ms | 0 |
| C yt-dlp 취소 | `yt-dlp -J <영상>`, 700ms 뒤 취소 | 3개 (`conhost.exe`, `yt-dlp.exe` x2) | 167ms | 0 |
| D yt-dlp 강제 종료 | 같은 명령, 스파이크를 `Stop-Process -Force` | 3개 (같음) | 119ms | 0 |

기준은 5초 안에 자손 0, 2초 뒤에도 0이다. 4개 모두 통과했다. 실행 뒤 남은 폴더와 스파이크 프로세스는 0이었다.

읽는 법:

- "0까지"는 처음으로 0을 본 시각이라 상한이다. 스크립트가 100ms마다 센다.
- C·D의 `yt-dlp.exe` 둘은 PyInstaller onefile의 부트로더와 실제 파이썬 프로세스다.
- B·D는 취소 코드를 거치지 않고 스파이크가 죽어서 Job 핸들이 닫히는 것만으로 자손이 사라졌다.
- 이 시나리오에는 **ffmpeg 자손이 없다.** 실제 다운로드(yt-dlp가 ffmpeg를 부르는 경우)는 재지 않았다.
- 점검 스크립트는 트리를 동작 직전까지 모으므로, 마지막 스냅숏과 동작 사이에 새로 생긴 프로세스는 놓칠 수 있다. A·B의 트리는 그 전에 모두 떠 있었고, C·D는 yt-dlp가 있음을 확인한 뒤 동작했다.

## Q6: 한글 입력과 배율 (사용자 직접 확인)

사용자가 표식이 붙은 exe를 탐색기에서 열고 입력란에서 확인한 것:

| 항목 | 결과 |
|---|---|
| 한글 조합 입력 | 된다 |
| 조합 중인 글자가 제자리에 보임 | 보인다 |
| 붙여넣기 (Ctrl+V) | 된다 |
| 마우스 선택 | 된다 |
| 우클릭 메뉴 | 된다 |
| 150%·200% 배율에서 글꼴이 흐리지 않다 | **확인하지 않음 (사용자 확인 남음)** |

배율은 사용자가 보고하지 않았다. 사용자가 보낸 화면 캡처에서 100% 배율로 보이긴 하지만, 그것으로 150%·200%가 어떨지는 말할 수 없다. 추정하지 않는다. 설계 §4에 따르면 Q6은 멈춤 관문이므로 이 확인은 Q1 계획 전에 끝나야 한다.

## Q7과 Q8

**Q7, 숨긴 WebView2.** 같은 프로세스의 GUI 스레드에서 WebView2를 띄운다. 부모 창은 한 번도 보이지 않는 최상위 창이다(`WS_POPUP`, 도구 창, 화면 밖 -32000, 800x600, `ShowWindow` 호출 없음). 컨트롤러는 `put_IsVisible(TRUE)`로 두어 페이지 타이머가 돈다. InPrivate 프로필이고 팝업과 다운로드는 거절한다.

| 실행 | 결과 |
|---|---|
| `/first` (점검 페이지) | `{"ok":true,"text":"card of first","ticks":14,"waitedMs":815}`, 종료 코드 0 |
| 런타임 없음 흉내 (`WEBVIEW2_BROWSER_EXECUTABLE_FOLDER`를 빈 폴더로) | `ok:false`, 오류 `0x80070002`, 종료 코드 0, 로더가 띄우는 상자 없음 |

`ticks` 14가 약 815ms 동안 쌓였다. 숨긴 창에서도 페이지의 50ms 타이머가 거의 그대로 돌았다는 뜻이다. **숨겨져 있었다는 것은 눈으로 본 것이 아니다.** 스파이크와 가짜 PC의 `msedgewebview2`가 가진 보이는 최상위 창을 100ms마다 `EnumWindows`로 세어 0개임을 확인했다. 100ms 사이에 잠깐 떴다 사라지는 창은 못 잡을 수 있다. 구글은 부르지 않았다. 각각 한 번씩만 돌렸다.

**Q8, Schannel TLS.** `--tls-check`가 `https://lrclib.net/api/search?track_name=test&artist_name=test`를 한 번 불렀다. 결과 `{"backend":"schannel","ok":true,"status":200}`. 요청은 이 한 번이다.

## 계획과 달라진 것

- 계획의 Task 4가 가리킨 `engine/src/integrationTest`는 없었다. 영상 주소는 `engine/src/test/.../integration/RealYtDlpIntegrationTest.kt`의 것(`https://www.youtube.com/watch?v=jNQXAC9IVRw`)을 썼다. 계획의 경로 오타이고 의도는 같다.
- `report.cpp`의 버그를 Task 4에서 찾아 고쳤다. `Report::put`이 읽기용 파일을 연 채로 `QSaveFile`이 바꾸려 해서, Windows에서 두 번째 `put`이 조용히 사라졌다. 보고 키가 둘 이상인 첫 작업이 Task 4라서 거기서 드러났고, 첫 실행에서 A·C가 "동작 시점이 오지 않음"으로 나온 원인이었다.
- `--spawn-tree`는 계획의 `ping` 하나보다 큰 트리를 만든다. `cmd.exe`에서 `start /b`로 `PING.EXE`를 띄우고, `cmd.exe` 아래 `cmd.exe` 아래 `PING.EXE`도 띄워서 두 갈래·세 단계다.
- `yt-dlp`가 PATH에 없었다. 앱의 사본(`%APPDATA%\xGetSongs\bin\yt-dlp.exe`)을 읽기만 해서 가짜 PC로 복사해 썼다. 그 폴더에는 아무것도 쓰지 않았다.
- Defender 점검 스크립트에 수정 라운드가 한 번 필요했다. 첫 버전은 검사 실패(종료 코드 2)를 "위협 발견"으로 적었고, 막혔을 때 결과를 남기지 못했다. 판정을 PASS·FAIL·판정 불가로 나누고 차단돼도 결과를 남기게 고쳤다.
- 숨긴 WebView2의 평가식은 텍스트 하나가 아니라 `{text, ticks}` 객체다. 같은 시점의 두 값을 한 번에 읽으려는 것이다.

## 이어질 단계에서 조심할 것

- **`JobProcess`는 Qt 내부의 수명 동작에 기댄다.** modifier에 넘어오는 `PROCESS_INFORMATION*`가 `start()`가 돌아온 뒤에도 `cleanup()` 때까지 살아 있고 `hThread`가 열려 있다는 것을 Qt 6.11.3 소스(`qprocess_win.cpp`)를 읽어서 확인했다. 문서에 보장된 동작이 아니다. Qt를 올릴 때마다 다시 확인해야 한다. 자식은 일시 정지 상태라 스스로 끝날 수 없다는 점이 이 방식을 안전하게 한다.
- 숨긴 WebView2의 숨김 판정은 최상위 창 열거(100ms 간격)로 한 것이고 눈으로 본 것이 아니다. 숨긴 부모 창은 STA 안에서 Qt 이벤트 루프가 메시지를 돌린다. 점검 모드에서는 Qt 메인 창을 아예 만들지 않았으므로, 보이는 Qt 창이 있는 앱 안에서의 WebView2는 직접 재지 않았다. 실제 프로필(InPrivate가 아닌 것)도 쓰지 않았다. 상속된 `WEBVIEW2_*` 환경 변수는 지우지 않는다.
- vcpkg는 약 37GB를 쓰고 첫 Qt 빌드는 약 89분 걸린다. vcpkg 클론은 얕다.
- 이 문서의 시작 시간은 거의 빈 창의 값이다.
- Task 4~6의 점검 스크립트에 남긴 작은 미흡점(버리는 코드라 고치지 않았다): 점검 스크립트의 `Get-Content`가 exe의 보고 파일 교체와 겹치면 거짓 오류가 날 수 있다, 한 번 던지면 스크립트 전체가 멈춘다, `defender.ps1`에서 `Get-MpThreatDetection` 실패가 0건으로 센다, `Get-Content`가 try 밖에 있다. 시작 시간 점검은 시계가 거칠고, 보고 파일 쓰기 실패가 조용하며, 한 번도 페인트되지 않을 때의 대체 시간 제한이 없다.

## 확인하지 못한 것

이 스파이크가 아예 다루지 않은 것:

- 메모리 사용량.
- Windows 10. 이 PC는 Windows 11 하나뿐이다. exe가 `icuuc.dll`을 가져오므로 Windows 10 1903 미만에서는 뜨지 않을 것으로 보이지만 확인하지 않았다.
- 다른 백신. Defender는 꺼져 있었고 V3 Lite의 반응은 알지 못한다.
- 150%·200% 배율(사용자 확인 남음).
- Defender가 켜진 PC에서의 반응, 같은 조건의 현행 exe와의 SmartScreen 비교.
- 콜드 스타트(재부팅 직후)와 시작 시간의 반복 측정. 5회 중앙값 한 세트뿐이다.
- 실제 구글 가사 페이지. 점검 페이지만 썼다.
- 실제 다운로드 작업 중의 동시성과 취소, ffmpeg 자손까지 포함한 정리.
- 실제 앱 수준의 시작 시간과 크기. 이 값들은 거의 빈 앱의 것이다.
- `icu`, `openssl`, `opengl`을 끈 구성에서 실제 앱 기능이 모두 되는지(예: 정규식 의미 차이, 한글 외 입력기).
- TLS는 lrclib.net 한 번의 호출뿐이다.

## 판정

### 다음 단계(Q1)로 가도 되는지

**조건부로 가도 된다.** 측정한 것 중에 계속하는 것을 막는 것은 없다. 하지만 관문이 모두 통과한 것은 아니다.

- Q5(자손 정리): 통과. 멈춤 관문 하나가 닫혔다.
- Q6(한글 입력·붙여넣기·선택·우클릭): 입력은 통과. 150%·200% 배율은 아직 확인하지 않았다.
- Q4(Defender·SmartScreen): 판정하지 못했다. 이 PC에서는 막는 일이 관찰되지 않았지만 Defender가 꺼져 있었다.
- Q1, Q2, Q7, Q8: 통과. Q3: 기록(추정보다 큼).

남은 두 가지:

1. 사용자가 150%와 200% 배율에서 창을 열어 글자가 흐린지 본다.
2. Defender가 켜진 PC에서 표식이 붙은 exe의 반응을 본다(`qt/spike/check/defender.ps1`, 같은 PC에서 Defender를 켤 수 없으면 다른 PC). 가능하면 현행 exe와 나란히 비교한다.

둘 중 하나가 나쁘게 나오면 설계 §4에 따라 **Q1 계획을 쓰지 않고 멈춰서 사용자에게 알린다.** 배율에서 글꼴이 흐리면 Q6 실패, Defender가 exe를 격리·삭제·실행 차단하면 Q4 실패다. "알 수 없는 게시자" 경고만 뜨는 것은 지금 exe에도 뜨므로 실패가 아니다.

다음 단계는 분석 문서 §9의 Q1(`shared` 이식과 테스트 이식)이다. 위 두 확인이 끝나기 전에 시작하면 Q1 이후의 작업이 접힐 수 있다는 위험이 남는다.

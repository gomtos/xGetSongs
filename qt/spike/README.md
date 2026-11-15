# xgs-qt-spike

버려도 되는 스파이크다. C++/Qt 전환 여부를 정하려고 만든다. 저장소의 다른 코드와 무관하다.

## 빌드 환경

- vcpkg는 저장소 밖 `C:\dev\vcpkg`에 둔다. 기준선 커밋: `4702e381dbbd9b54d3f7e1eb565cd8981c1814cd` (`vcpkg.json`의 `builtin-baseline`).
- Qt 버전: 6.11.3 (vcpkg 포트 `qtbase`, port-version 1).
- 트리플렛 `x64-windows-static`: Qt와 CRT(/MT)를 정적으로 링크한다. TLS는 Schannel(OpenSSL 없음).
- 켠 qtbase 기능(`default-features: false`): `gui`, `widgets`, `network`, `thread`, `freetype`, `harfbuzz`, `png`, `doubleconversion`, `pcre2`.
  `openssl`, `sql*`, `dbus`, `opengl`, `icu`은 켜지 않았다. vcpkg에는 `schannel` 기능이 따로 없고, openssl을 끄면 Windows에서 Schannel이 쓰인다.
- 그 밖의 의존성: `webview2`. 정적 로더 `WebView2LoaderStatic.lib`를 링크하므로 `WebView2Loader.dll`이 필요 없다.

## 빌드 명령

VS 2022의 개발자 환경(`vcvars64.bat`)에서, 저장소 루트에서 실행한다.

```
cmake --preset spike-release -S qt/spike
cmake --build build/qt-spike
```

결과물: `build/qt-spike/xgs-qt-spike.exe`. 처음에는 Qt를 소스에서 빌드하느라 오래 걸리고, 이후에는 vcpkg 바이너리 캐시로 빠르다.

## 첫 빌드 기록

- 걸린 시간: 약 89분 (Qt와 의존성을 소스에서 빌드; 대부분 qtbase 구성 5328초 안에 있음).
- `C:\dev\vcpkg` 디스크 사용량: 약 37 GB (그중 buildtrees 약 30 GB). 바이너리 캐시(`%LOCALAPPDATA%\vcpkg\archives`)는 약 1.2 GB.
- `xgs-qt-spike.exe`: 21,302,784 바이트. `dumpbin /dependents`에 Qt6*.dll, VCRUNTIME, MSVCP, libssl, libcrypto는 없다 (Windows 시스템 DLL만; `icuuc.dll`은 Windows 10+ 내장).

## 점검: 깨끗한 PC에서 시작 시간과 크기

```
pwsh qt/spike/check/start-time.ps1
```

가짜 깨끗한 PC(`%TEMP%\xgs-qt-spike-<id>`, 빈 APPDATA·LOCALAPPDATA·TEMP, 최소 PATH)에서 exe를 5번 새로 복사해 첫 실행과 두 번째 실행을 재고, 창이 10번 잠깐 뜬다. 기준: 첫 실행 중앙값 3700ms 이하, 두 번째 1200ms 이하. 결과는 `build/qt-spike/start-time.json`에 남는다.

## 점검: 자손 프로세스 정리 (Job Object)

```
pwsh qt/spike/check/descendants.ps1
```

`--spawn-tree`·`--spawn-cmd`로 띄운 자식은 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` Job에 들어간다(`CREATE_SUSPENDED`로 만들고 Job에 넣은 뒤 재개). 시나리오 넷을 각각 가짜 PC에서 돌리고 창이 4번 이상 잠깐 뜬다.

- A 취소: `--spawn-tree 120 --cancel-after 1500`, exe가 스스로 취소
- B 강제 종료: `--spawn-tree 120`, `PING.EXE`가 보이면 스파이크를 `Stop-Process -Force`
- C yt-dlp 취소: `--spawn-cmd "yt-dlp -J <영상>" --cancel-after 700`
- D yt-dlp 강제 종료: `--spawn-cmd "yt-dlp -J <영상>"`, yt-dlp 아래에 프로세스가 생기면 스파이크를 `Stop-Process -Force`

기준: 동작 뒤 5초 안에 살아 있는 자손이 0, 2초 뒤에도 0. 자손은 동작 전에 자식 pid부터 `ParentProcessId`를 따라 모으고 생성 시각을 같이 적어 pid 재사용을 거른다. yt-dlp는 `PATH`에서 찾고, 없으면 `%APPDATA%\xGetSongs` 아래의 것을 읽기만 해서 가짜 PC로 복사해 쓴다. 못 찾으면 C·D는 `건너뜀`이다.

## 점검: 숨긴 WebView2

```
pwsh qt/spike/check/webview.ps1
```

`--webview-check <url>`은 창을 띄우지 않고 프로세스 안의 WebView2로 페이지를 연다. 부모 HWND는 한 번도 보이지 않는 최상위 창(`WS_POPUP`, 도구 창, 화면 밖 -32000, 800×600)이고, 컨트롤러는 `put_IsVisible(TRUE)`로 두어 페이지 타이머가 돈다. InPrivate 프로필, 팝업·다운로드는 거절, 사용자 데이터 폴더는 `%LOCALAPPDATA%\xgs-qt-spike\webview`. 페이지를 연 뒤 200ms마다 `div[data-lyricid]`를 보고, 10초가 지나면 `timeout`.

점검은 `node shell/tools/browser-check/fixture.mjs <빈 포트>`를 띄우고 가짜 PC에서 두 번 돈다. 1) `/first`: `ok:true`, `text`가 `card of first`, `ticks > 0`. 2) 런타임 없음 흉내(`WEBVIEW2_BROWSER_EXECUTABLE_FOLDER`=빈 폴더): 종료 코드 0, `ok:false`. 실행 중에는 스파이크와 가짜 PC의 msedgewebview2가 가진 보이는 최상위 창을 세며 기준은 0개다. 끝나면 가짜 PC 폴더를 가리키는 msedgewebview2가 끝나기를 기다린다. 결과는 `build/qt-spike/webview.json`.

## 점검: Schannel TLS

```
pwsh qt/spike/check/tls.ps1
```

`--tls-check`는 창 없이 `GET https://lrclib.net/api/search?track_name=test&artist_name=test`를 한 번 부른다(User-Agent `xgs-qt-spike`, 15초 제한). 기준: `backend`가 `schannel`, `ok:true`, `status` 200. lrclib.net이 응답하지 않으면(시간 초과, 5xx) HTTP 쪽은 `건너뜀`. 결과는 `build/qt-spike/tls.json`.
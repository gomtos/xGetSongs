# xgs-qt-spike

버려도 되는 스파이크다. C++/Qt 전환 여부를 정하려고 만든다. 저장소의 다른 코드와 무관하다.

## 빌드 환경

- vcpkg는 저장소 밖 `C:\dev\vcpkg`에 둔다. 기준선 커밋: `4702e381dbbd9b54d3f7e1eb565cd8981c1814cd` (`vcpkg.json`의 `builtin-baseline`).
- Qt 버전: 6.11.3 (vcpkg 포트 `qtbase`, port-version 1).
- 트리플렛 `x64-windows-static`: Qt와 CRT(/MT)를 정적으로 링크한다. TLS는 Schannel(OpenSSL 없음).
- 켠 qtbase 기능(`default-features: false`): `gui`, `widgets`, `network`, `thread`, `freetype`, `harfbuzz`, `png`, `doubleconversion`, `pcre2`.
  `openssl`, `sql*`, `dbus`, `opengl`, `icu`은 켜지 않았다. vcpkg에는 `schannel` 기능이 따로 없고, openssl을 끄면 Windows에서 Schannel이 쓰인다.
- 그 밖의 의존성: `webview2` (Task 3 이후에 쓴다).

## 빌드 명령

VS 2022의 개발자 환경(`vcvars64.bat`)에서, 저장소 루트에서 실행한다.

```
cmake --preset spike-release -S qt/spike
cmake --build build/qt-spike
```

결과물: `build/qt-spike/xgs-qt-spike.exe`. 처음에는 Qt를 소스에서 빌드하느라 오래 걸리고, 이후에는 vcpkg 바이너리 캐시로 빠르다.

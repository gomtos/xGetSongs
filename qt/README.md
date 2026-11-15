# xGetSongs Qt 판

C++20 / Qt 6 Widgets로 다시 쓰는 xGetSongs. 이 폴더가 제품 빌드이고, `qt/spike/`는 버려도 되는 스파이크다.

## 필요한 것

- Visual Studio 2022 (MSVC, 함께 오는 CMake와 Ninja)
- vcpkg: `C:\dev\vcpkg` (저장소 밖). 기준선 커밋 `4702e381dbbd9b54d3f7e1eb565cd8981c1814cd`(`qt/vcpkg.json`의 `builtin-baseline`)
- Qt 6.11.3 (vcpkg 포트 `qtbase`), 트리플렛 `x64-windows-static`(Qt와 CRT 정적 링크)
- 지원 Windows: Windows 10 이상 (`_WIN32_WINNT=0x0A00`)

## 공통 명령

저장소 루트에서 실행한다. vcpkg 의존성은 매니페스트 모드라 `build/qt/vcpkg_installed`에 설치된다.

```
cmd /c "call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cmake --preset qt-release -S qt && cmake --build build/qt"
ctest --test-dir build/qt --output-on-failure
pwsh qt/tools/differential/run.ps1 -Kinds <글자들> [-PerKind 10000] [-Seed 20261115]
```

세 번째는 대조 도구(Task 2 이후)다. 테스트 실행 파일은 `build/qt/tests/` 아래에 생긴다.

## 켠 qtbase 기능

`default-features: false`에 `gui`, `widgets`, `network`, `thread`, `concurrent`, `testlib`, `freetype`, `harfbuzz`, `png`, `doubleconversion`, `pcre2`. 기능 이름은 모두 `C:\dev\vcpkg\ports\qtbase\vcpkg.json`에 그대로 있었다. 그 밖의 의존성은 `webview2`.
`openssl`, `sql*`, `dbus`, `opengl`, `icu`는 켜지 않았다(TLS는 Schannel). 스파이크의 목록에 `concurrent`와 `testlib`를 더해 Qt를 한 번만 다시 빌드한다.

## 이번 재빌드에 걸린 시간

약 26분 (구성 1555초; 17:16~17:42). 스파이크 때(약 89분)와 달리 의존성은 바이너리 캐시에서 왔고 qtbase만 새로 빌드됐다.

## 구성

- `core/`: 정적 라이브러리 `xgscore`(Qt Core만, 네임스페이스 `xgs`)
- `tests/`: Qt Test. `xgs_add_test(<이름> <소스…>)`로 실행 파일 하나를 만들고 ctest에 등록한다

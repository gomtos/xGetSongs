# shell — xGetSongs의 Tauri 앱

화면(TypeScript, 프레임워크 없음)과 그것을 여는 Rust 셸이다. 다운로드와 태그 같은 일은 하지 않는다: `../server`의 Kotlin 사이드카가 한다. 셸은 사이드카를 띄우고(`../sidecar-host`) 화면이 `invoke`로 부르는 명령을 사이드카의 HTTP·SSE 호출로 바꿔 준다. 앱 전체의 설명은 [../README.md](../README.md), 설계는 [../docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md](../docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md)에 있다.

## 만들기와 실행

- exe 한 파일로 묶기: 저장소 루트의 `package-exe.bat`.
- 셸만 빌드: `npm run tauri build -- --no-bundle` → `src-tauri\target\release\shell.exe`. 사이드카를 찾을 수 있어야 실행된다(아래 "환경 변수").
- 사이드카 없이 화면만 보기: `npm run dev`로 Vite를 띄우고 브라우저에서 `http://localhost:1420/?demo`를 연다. 가짜 서버(`src/demo.ts`)가 조회·다운로드·진행을 흉내 낸다.

## 시험

```powershell
node --test "tests/*.test.ts"   # 화면 로직, 브리지 계약, 이름 점검
npx tsc --noEmit                # 타입 검사
```

`tests/kotlin-coverage.test.ts`는 `shared`의 Kotlin 시험이 가진 이름이 TypeScript 포팅(`filename.test.ts`, `input.test.ts`)에도 있는지 본다. `tools/differential`은 같은 무작위 입력을 Kotlin 원본(사이드카 이미지의 jar)과 TypeScript 포팅에 넣어 답을 비교한다(`node tools/differential/fuzz.mts`, 먼저 `gradlew :server:sidecarImage`).

## 환경 변수

사이드카를 어디서 찾을지와 어디에 쓸지는 `../sidecar-host/src/config.rs`가 정한다. 개발에 쓰는 것:

| 변수 | 뜻 |
|---|---|
| `XGS_SIDECAR_DIR` | 사이드카 이미지 폴더(`runtime\bin\java.exe`와 `lib`가 있는 곳). 보통 `..\server\build\sidecar-image` |
| `XGS_SIDECAR_LIB` | 이미지 대신 `installDist`의 `lib` 폴더(개발용). `XGS_SIDECAR_JAVA`로 java를 고른다 |
| `XGS_SIDECAR_APPDATA` | 앱 데이터 폴더. 기본은 `%APPDATA%\xGetSongs`. **개발과 점검에는 임시 폴더를 준다**: 사이드카가 시작할 때 그 안의 `work\`를 비운다 |
| `XGS_SIDECAR_LOGDIR` | 로그 폴더 |

없으면 셸은 자기 exe가 지닌 런타임(`package-exe.bat`으로 묶은 exe)을, 그것도 없으면 exe 옆의 `sidecar` 폴더를 쓴다. 빈 값은 없는 것으로 친다.

## 점검 페이지 (개발자용)

`check.html`은 제품이 아니다. 브리지의 명령을 실제 사이드카에 부르고 화면이 본 것을 `XGS_BRIDGE_CHECK_FILE`이 가리키는 파일에 `key=value` 줄로 적는다. `npm run tauri build -- --no-bundle --config check.conf.json`으로 만든 점검용 빌드(식별자가 `com.xgetsongs.shell.check`라 사용자가 쓰는 앱과 섞이지 않는다)만 연다. 점검 스크립트(창을 닫고, 두 번째 실행을 하고, 풀다 죽이는 것 등)는 저장소에 없고 각 단계의 계획서(`docs/superpowers/plans`)에 들어 있다. 점검용 빌드는 일반 빌드와 같은 `target\release\shell.exe`를 덮어쓰므로 점검 뒤에는 일반 빌드를 다시 만든다.

점검용 명령(`check_record`, `check_env`, `check_ping`, `check_close`, `check_wait`, `check_burst`)은 `XGS_BRIDGE_CHECK_FILE`이 없으면 거부한다. 같은 변수가 있을 때만 `XGS_FAKE_NO_WEBVIEW2`가 WebView2가 없는 척한다.

## 권장 개발 환경

[VS Code](https://code.visualstudio.com/) + [Tauri](https://marketplace.visualstudio.com/items?itemName=tauri-apps.tauri-vscode) + [rust-analyzer](https://marketplace.visualstudio.com/items?itemName=rust-lang.rust-analyzer)

# Tauri Rust 브리지 (2단계) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 웹뷰가 Tauri `invoke`와 `Channel`만으로 서버의 모든 기능(도구 상태, 설치·갱신, 조회, 작업 시작·취소, 작업 이벤트)을 쓰게 하고, 서버의 포트와 토큰은 Rust 안에서만 쓴다.

**Architecture:** 서버와 `Guard.kt`는 그대로 둔다. `sidecar-host` 크레이트(Tauri 없음)가 `Bridge`(서버 호출 6개 + 이벤트 스트림), SSE 해석기, `Lifecycle`(시작 대기와 종료)을 갖고, 가짜 서버로 계약을 시험한다. `shell`의 Tauri 명령 8개는 이것을 `async` + `spawn_blocking`으로 감싼 얇은 껍데기이고, 이벤트는 `Channel`로 흘려보낸다. 스파이크의 잠금을 쥔 채 HTTP를 부르는 명령, 토큰을 화면에 돌려주는 `Probe`, 토큰이 든 `spike-ready.json`은 없앤다.

**Tech Stack:** Rust 1.99 (`ureq` 2, `serde`/`serde_json`), Tauri 2 (`Channel`, `tauri::async_runtime::spawn_blocking`), TypeScript 6 + Node 24 `node --test`, PowerShell 확인 스크립트. Kotlin 변경은 없다.

**Spec:** [docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md](../specs/2026-10-10-tauri-kotlin-sidecar-design.md) §2.2(왜 직접 붙지 않는가), §4(구조), §5의 2단계. 서버 계약은 `shared/src/commonMain/kotlin/com/xgetsongs/shared/api/ApiModels.kt`와 `server/src/main/kotlin/com/xgetsongs/server/Application.kt`, 지금 클라이언트의 동작은 `app/src/commonMain/kotlin/com/xgetsongs/app/api/XgsApi.kt`다.

## Global Constraints

- 서버(`server/`, `engine/`, `shared/`)와 `Guard.kt`(Origin 거부, Host 검사, 토큰)는 바꾸지 않는다. 이 단계에 Kotlin 변경은 없다.
- 포트와 토큰은 Rust 안에서만 쓴다. 명령의 결과, 오류 메시지, 이벤트, 로그, 파일 어디에도 없다. `Handshake`의 `Debug` 출력도 토큰을 가린다.
- 서버가 주는 오류 문구(`ErrorResponse.message`)는 한국어 그대로 사용자에게 간다. Rust가 만드는 문구도 한국어이고, 서버와의 연결이 끊겼을 때의 문구는 지금 앱과 같은 `서버와의 연결이 끊어졌습니다.`다.
- 시간 제한은 지금 앱의 클라이언트(`requestTimeout = 0`, 제한 없음)를 따르되, 바로 답이 오는 호출(`tools`, `start_job`, `cancel_job`)만 전체 60초로 막는다. 연결은 5초. `resolve`, `install_yt_dlp`, `update_yt_dlp`와 이벤트 스트림은 yt-dlp를 기다리므로 제한이 없다.
- Tauri 명령은 8개다: `sidecar_ready`, `tools`, `install_yt_dlp`, `update_yt_dlp`, `resolve`, `start_job`, `watch_job`, `cancel_job`. 서버 호출은 7개이고 `sidecar_ready`가 8번째다. 점검용 `check_record`, `check_ping`, `check_burst`는 제품 기능이 아니다. `check_record`와 `check_burst`는 환경 변수 `XGS_BRIDGE_CHECK_FILE`이 있을 때만 동작하고(없으면 거절한다), `check_ping`은 아무것도 하지 않는다.
- `sidecar-host`는 Tauri에 의존하지 않는다(테스트 루프를 빠르게 두기 위해).
- 작업 번호(`jobId`)는 서버가 만든 UUID뿐이다. 화면이 보낸 값이 서버의 다른 경로를 부르면 안 되므로 영문자·숫자·`-`만, 64자 이하로 받는다.
- 사이드카의 앱 데이터 폴더는 임시 폴더만 쓴다. 사용자의 `%APPDATA%\xGetSongs`는 건드리지 않는다(서버가 시작할 때 `<app-data>\work`를 지운다). 열려 있는 Gradle 데몬도 건드리지 않고, Gradle을 부를 일이 있으면 `--no-daemon`으로 부른다.
- 코드와 코드 주석은 영어, 사용자에게 보이는 문구와 커밋 메시지는 한국어. `main`에 직접 커밋한다. 커밋 메시지 끝에는 이 세션이 정한 `Co-Authored-By` 줄을 붙인다.
- Bash 도구는 명령 텍스트의 백슬래시를 한 번 더 해석한다. 백슬래시가 든 코드는 Write·Edit 도구로 쓰고, `cargo`와 `npm`은 PowerShell 도구로 부른다. 이 PC의 도구 셸에는 `cargo`가 PATH에 없으므로 PowerShell 호출마다 `$env:Path += ";$env:USERPROFILE\.cargo\bin"`를 먼저 쓴다.
- 소프트웨어 설치와 내려받기는 사용자에게 알리고 허락을 받은 뒤에 한다. 이 계획은 새 설치가 필요 없다. yt-dlp는 이 PC에 없고 설치하지 않으므로 실제 조회·다운로드는 이 단계에서 돌리지 않는다(`## 이 단계가 확인하지 않은 것`).

## Review Focus

계획의 테스트가 직접 다루지 않아 사용자에게 먼저 문제가 될 것, 가능성이 높은 순서. 각 줄 끝이 그것을 다루는 테스트다:

1. **한글·따옴표·이모지가 든 입력이 깨진다**(조회 주소, 앨범 이름, 출력 폴더). 요청 본문은 UTF-8 바이트 그대로 가고 `Content-Length`가 글자 수가 아니라 바이트 수와 맞아야 한다. → Task 2의 `a_resolve_sends_the_input_as_utf8_json_with_the_length_in_bytes`, `a_job_request_is_passed_on_as_it_is`
2. **진행률 이벤트가 깨지거나 늦게 온다.** 이벤트가 글자 한가운데나 `\r\n` 사이에서 끊겨 와도 온전해야 하고, 모아서 한꺼번에 오지 않고 오는 즉시 화면에 가야 하며, `job-done`에서 끝나야 한다(서버가 연결을 늦게 닫아도 기다리지 않는다). → Task 1의 `the_same_events_come_out_wherever_the_bytes_are_cut`, `a_crlf_split_between_two_pushes_is_one_line_end`, Task 3의 `a_korean_character_split_between_two_chunks_is_whole`, `an_event_arrives_when_it_is_sent_not_when_the_stream_ends`, `the_call_ends_at_job_done_without_waiting_for_the_stream_to_close`
3. **화면이 보낸 작업 번호로 서버의 다른 경로를 부른다**(`../tools`, `a/b`, `a?b`, 줄바꿈). 요청이 서버에 가기 전에 거절돼야 한다. → Task 2의 `a_job_id_that_could_reach_another_route_is_refused_without_a_request`, Task 3의 `a_job_id_that_could_reach_another_route_is_refused_for_the_stream_too`
4. **사이드카가 뜨는 중에 부르거나(2초 가까이 걸린다), 창을 시작 직후에 닫는다.** 앞의 호출은 기다렸다가 진행하고, 뒤의 경우 사이드카가 고아로 남거나 Job Object에 죽임을 당해 다음 시작이 크래시 경고를 내면 안 된다(`exit` 줄을 받고 정상 종료해야 한다). → Task 4의 `a_call_that_comes_while_the_sidecar_starts_waits_and_then_gets_the_means_to_call_it`, `closing_while_the_sidecar_is_still_starting_waits_for_it_and_ends_it_properly`, `a_sidecar_that_comes_after_the_wait_for_it_was_given_up_is_still_ended_properly`, Task 8의 B7
5. **서버가 죽었거나 멈췄다**(응답 없음, 연결 거부, 스트림 도중 끊김). 화면이 영원히 기다리지 않고 한국어 메시지를 받아야 한다. → Task 2의 `a_server_that_is_not_there_is_a_lost_connection`, `a_server_that_stops_answering_does_not_hold_a_short_call_forever`, Task 3의 `a_stream_that_ends_without_job_done_is_a_lost_connection_after_the_events_it_sent`, `a_connection_that_breaks_in_the_middle_of_the_stream_is_a_lost_connection`

## 이 단계의 관문

설계 §5의 "브리지 계약 테스트"를 아래 관문으로 풀어 쓴다. 하나라도 실패하면 원인을 분석해 한 번 고쳐 다시 시도하고, 그래도 실패하면 보고서에 적고 멈춘다.

| 번호 | 조건 | 확인 |
|---|---|---|
| B1 | 가짜 서버로 시험한 브리지 계약(요청 형식, 오류 매핑, 스트림, 시작 대기·종료)이 모두 통과한다 | Task 1~4의 `cargo test` |
| B2 | 실제 사이드카 이미지에 대한 계약 테스트가 통과한다: 실제 서버의 오류 문구, SSE `error` 이벤트, 토큰 거부, 종료 기록, 로그에 토큰 없음 | Task 5 |
| B3 | 웹뷰에서 명령 8개가 모두 동작하고 실제 서버의 문구를 그대로 받는다 | Task 8의 점검 페이지 기록 |
| B4 | 사이드카를 기다리는 동안 창이 멈추지 않는다: 사이드카가 필요 없는 명령의 왕복 최악 250ms 이하, 프레임 간격 최악 250ms 이하 | 점검 페이지 `worstPingMs`, `worstFrameGapMs` |
| B5 | 이벤트 5,000개를 `Channel`로 보냈을 때 하나도 잃지 않고 순서가 맞고 5초 안에 끝난다(진행률 초당 1,000회가 5초 이어지는 부하) | 점검 페이지 `burst*` |
| B6 | 토큰이 화면에 보이지 않는다: 어떤 명령의 결과·오류에도, 셸이 쓰는 어떤 파일에도 없고, 셸 소스의 명령 시그니처에 토큰이 없다 | Task 7 Step 7, Task 8 Step 6 |
| B7 | 수명이 스파이크·1단계와 같다: 창을 닫으면 3초 안에 사이드카가 없고 마커가 `reason=user`, 시작 직후 닫아도 15초 안에 고아 없이 `reason=user`, 셸을 강제 종료하면 5초 안에 사이드카가 없다 | Task 8 Step 4·5 |
| 악화 한도 | 창이 뜬 뒤 사이드카가 준비될 때까지(`waitedMs`, 3회 중앙값) 10초 이하(설계 §5.1과 같다) | 점검 페이지 `waitedMs` |

## File Structure

| 파일 | 책임 |
|---|---|
| `sidecar-host/src/error.rs` (새) | `BridgeError`(`{message}`), 한국어 고정 문구 |
| `sidecar-host/src/sse.rs` (새) | 바이트 조각을 SSE 이벤트로 자르는 해석기 |
| `sidecar-host/src/bridge.rs` (새) | `Bridge`: 서버 호출 6개와 `watch_job`. 토큰을 헤더에 달고 오류를 `BridgeError`로 바꾼다 |
| `sidecar-host/src/lifecycle.rs` (새) | `Lifecycle`: 시작 결과를 쥐고, 호출이 준비를 기다리게 하고, 종료한다. 잠금을 쥔 채 호출하지 않는다 |
| `sidecar-host/src/fake_server.rs` (새, 테스트 전용) | 스크립트대로 답하고 요청을 기록하는 가짜 서버 |
| `sidecar-host/src/handshake.rs` (수정) | `Debug`가 토큰을 가린다 |
| `sidecar-host/src/http.rs` (삭제) | 스파이크의 토큰 달린 `http_get`. `Bridge`가 대신한다 |
| `sidecar-host/tests/real_sidecar.rs` (새) | 실제 이미지로 시험하는 계약 테스트(`#[ignore]`) |
| `shell/src-tauri/src/lib.rs` (교체) | 명령 8개 + 점검용 3개, `Lifecycle` 관리, 종료 연동. `Probe`, `spike_record`, `spike-ready.json`, 잠금 쥔 HTTP는 없다 |
| `shell/src/bridge/{types,bridge,tauri}.ts` (새) | 화면이 쓰는 호출 래퍼와 서버 모델 타입. `bridge.ts`는 `invoke`와 `Channel`을 주입받아 Node에서 시험한다 |
| `shell/tests/bridge.test.ts` (새) | 래퍼 테스트 |
| `shell/check.html`, `shell/src/check.ts`, `shell/check.conf.json` (새, 스파이크 페이지 대체) | 개발자용 점검 페이지: 실제 사이드카에 명령을 모두 불러 보고 본 것을 파일에 적는다 |
| `docs/superpowers/specs/2026-10-11-tauri-rust-bridge-report.md` (새) | 종단 확인 결과 |

---

### Task 1: 오류 형식, SSE 해석기, 토큰을 가리는 Handshake

**Files:**
- Modify: `sidecar-host/Cargo.toml`, `sidecar-host/src/lib.rs`, `sidecar-host/src/handshake.rs`
- Create: `sidecar-host/src/error.rs`, `sidecar-host/src/sse.rs`

**Interfaces:**
- Consumes: 기존 `Handshake { port: u16, token: String }`
- Produces (Task 2~4가 쓴다), 크레이트 `xgs_sidecar`:
  - `error::BridgeError { pub message: String }` — `new(impl Into<String>)`, `lost()`, `Serialize`(`{"message": ...}`), `Display`, `Clone`, `PartialEq`, `Debug`
  - `error::{LOST, UNREADABLE_ANSWER, UNREADABLE_EVENT, BAD_JOB_ID, LISTENER_GONE}: &str` — 한국어 고정 문구
  - `sse::SseEvent { pub name: String, pub data: String }`, `sse::SseParser` — `new()`, `push(&mut self, bytes: &[u8]) -> Vec<SseEvent>`
  - `Handshake`는 이제 `Clone`, `PartialEq`이고 `Debug`가 토큰 대신 `"<hidden>"`을 찍는다

- [ ] **Step 1: 의존성을 더하고 `Handshake`의 실패하는 테스트를 쓴다**

`sidecar-host/Cargo.toml`의 `[dependencies]`에 `serde`와 `serde_json`을 더한다(`ureq` 줄은 그대로):

```toml
serde = { version = "1", features = ["derive"] }
serde_json = "1"
```

`sidecar-host/src/handshake.rs`의 `mod tests`에서 `fn tolerates_the_line_ending()` 바로 앞에 더한다:

```rust
    #[test]
    fn debug_output_never_holds_the_token() {
        let text = format!("{:?}", Handshake { port: 8080, token: "secret-token".to_string() });

        assert!(!text.contains("secret-token"), "{text}");
        assert!(text.contains("8080"), "{text}");
    }
```

- [ ] **Step 2: 실패를 확인한다**

PowerShell 도구에서 (`sidecar-host` 폴더에서):

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test handshake:: 2>&1 | Select-Object -Last 15
```

Expected: `debug_output_never_holds_the_token`이 FAIL — 출력에 `secret-token`이 그대로 찍힌다(지금은 `#[derive(Debug)]`다). `1 failed; 5 passed`.

- [ ] **Step 3: `Debug`를 직접 구현한다**

`sidecar-host/src/handshake.rs`에서 `Handshake`의 `#[derive(Debug, Clone, PartialEq)]`를 아래로 바꾸고, 구조체 바로 뒤에 `impl`을 더한다:

```rust
#[derive(Clone, PartialEq)]
pub struct Handshake {
    pub port: u16,
    pub token: String,
}

/// The token is a secret: whatever prints a handshake (a failed `assert_eq!`, a `{:?}` in a log line) must not print it.
impl std::fmt::Debug for Handshake {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.debug_struct("Handshake").field("port", &self.port).field("token", &"<hidden>").finish()
    }
}
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test handshake:: 2>&1 | Select-Object -Last 8
```

Expected: `6 passed`.

- [ ] **Step 5: `BridgeError`의 테스트를 먼저 쓴다**

`sidecar-host/src/error.rs`를 아래 테스트만으로 만든다(구현은 아직 없다):

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_serializes_like_the_servers_error_response() {
        let json = serde_json::to_string(&BridgeError::new("작업을 찾을 수 없습니다.")).unwrap();

        assert_eq!(json, r#"{"message":"작업을 찾을 수 없습니다."}"#);
    }

    #[test]
    fn it_displays_as_its_message() {
        assert_eq!(BridgeError::lost().to_string(), LOST);
    }
}
```

`sidecar-host/src/lib.rs`에 `pub mod error;`를 더한다(`pub mod config;` 다음 줄에). 실행한다:

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test error:: 2>&1 | Select-Object -Last 15
```

Expected: 컴파일 오류 — `cannot find type BridgeError in this scope`(와 `LOST`). 

- [ ] **Step 6: `BridgeError`를 구현한다**

`error.rs`의 맨 위(테스트 모듈 앞)에 구현을 쓴다:

```rust
use serde::Serialize;
use std::fmt;

/// The server cannot be reached, or it stopped answering.
pub const LOST: &str = "서버와의 연결이 끊어졌습니다.";
/// The server answered with something that is not the JSON the contract describes.
pub const UNREADABLE_ANSWER: &str = "서버 응답을 해석할 수 없습니다.";
/// An event of a job's stream carried data that is not JSON.
pub const UNREADABLE_EVENT: &str = "서버 이벤트를 해석할 수 없습니다.";
/// A job ID that cannot be one the server made (see `valid_job_id`).
pub const BAD_JOB_ID: &str = "작업 번호가 올바르지 않습니다.";
/// The page that was to receive the events is gone.
pub const LISTENER_GONE: &str = "이벤트를 받는 화면이 없어 중단했습니다.";

/// What a bridge call reports when it fails: one message fit for the user, in the shape of the server's own
/// `ErrorResponse` (`{"message": ...}`). It never holds the token.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct BridgeError {
    pub message: String,
}

impl BridgeError {
    pub fn new(message: impl Into<String>) -> BridgeError {
        BridgeError { message: message.into() }
    }

    pub fn lost() -> BridgeError {
        BridgeError::new(LOST)
    }
}

impl fmt::Display for BridgeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.message)
    }
}
```

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test error:: 2>&1 | Select-Object -Last 8
```

Expected: `2 passed`.

- [ ] **Step 7: SSE 해석기의 실패하는 테스트를 쓴다**

`sidecar-host/src/sse.rs`를 아래 뼈대와 테스트로 만든다. 뼈대는 `todo!()`라서 테스트가 컴파일은 되고 실패한다:

```rust
#[derive(Debug, Clone, PartialEq)]
pub struct SseEvent {
    pub name: String,
    pub data: String,
}

#[derive(Default)]
pub struct SseParser {}

impl SseParser {
    pub fn new() -> SseParser {
        SseParser::default()
    }

    pub fn push(&mut self, _bytes: &[u8]) -> Vec<SseEvent> {
        todo!()
    }
}
```

뼈대 바로 아래에 테스트를 붙인다:

```rust
#[cfg(test)]
mod tests {
    use super::*;

    fn event(name: &str, data: &str) -> SseEvent {
        SseEvent { name: name.to_string(), data: data.to_string() }
    }

    fn parse_all(bytes: &[u8]) -> Vec<SseEvent> {
        SseParser::new().push(bytes)
    }

    #[test]
    fn it_reads_an_event_with_crlf_line_ends() {
        assert_eq!(
            parse_all("event: progress\r\ndata: {\"rank\":1}\r\n\r\n".as_bytes()),
            vec![event("progress", "{\"rank\":1}")]
        );
    }

    #[test]
    fn it_reads_an_event_with_lf_line_ends() {
        assert_eq!(parse_all(b"event: a\ndata: b\n\n"), vec![event("a", "b")]);
    }

    #[test]
    fn it_reads_several_events_from_one_push() {
        assert_eq!(
            parse_all(b"event: a\ndata: 1\n\nevent: b\ndata: 2\n\n"),
            vec![event("a", "1"), event("b", "2")]
        );
    }

    #[test]
    fn the_same_events_come_out_wherever_the_bytes_are_cut() {
        let stream = "event: item-started\r\ndata: {\"fileName\":\"001 가수 - 제목.m4a\"}\r\ndata: 둘째 줄\r\n\r\n: ping\r\n\r\nevent: job-done\r\ndata: {}\r\n\r\n"
            .as_bytes();
        let whole = parse_all(stream);
        assert_eq!(whole.len(), 2);

        for cut in 0..=stream.len() {
            let mut parser = SseParser::new();
            let mut events = parser.push(&stream[..cut]);
            events.extend(parser.push(&stream[cut..]));
            assert_eq!(events, whole, "cut at byte {cut}");
        }
    }

    #[test]
    fn a_crlf_split_between_two_pushes_is_one_line_end() {
        // Read as two line ends, the CR and the LF would put a blank line between the two data lines and cut the event in two.
        let mut parser = SseParser::new();
        let mut events = parser.push(b"event: a\r\ndata: 1\r");
        events.extend(parser.push(b"\ndata: 2\r\n\r\n"));

        assert_eq!(events, vec![event("a", "1\n2")]);
    }

    #[test]
    fn data_lines_are_joined_with_a_line_feed() {
        assert_eq!(parse_all(b"data: one\ndata: two\n\n"), vec![event("", "one\ntwo")]);
    }

    #[test]
    fn comments_and_unknown_fields_are_ignored() {
        assert_eq!(parse_all(b": keep-alive\nid: 7\nretry: 100\nevent: a\ndata: 1\n\n"), vec![event("a", "1")]);
    }

    #[test]
    fn a_space_after_the_colon_is_optional() {
        assert_eq!(parse_all(b"event:a\ndata:{\"x\":1}\n\n"), vec![event("a", "{\"x\":1}")]);
    }

    #[test]
    fn an_event_without_a_name_has_an_empty_name() {
        assert_eq!(parse_all(b"data: 1\n\n"), vec![event("", "1")]);
    }

    #[test]
    fn a_block_without_data_is_not_an_event_and_does_not_leak_its_name_into_the_next() {
        assert_eq!(parse_all(b"event: lonely\n\ndata: 1\n\n"), vec![event("", "1")]);
    }

    #[test]
    fn an_event_that_never_got_its_blank_line_is_not_delivered() {
        assert_eq!(parse_all(b"event: a\ndata: 1\n"), Vec::<SseEvent>::new());
    }

    #[test]
    fn bytes_that_are_not_utf8_do_not_stop_the_parser() {
        let events = parse_all(b"event: a\ndata: \xff\xfe\n\nevent: b\ndata: 2\n\n");

        assert_eq!(events.len(), 2);
        assert_eq!(events[1], event("b", "2"));
    }
}
```

`lib.rs`에 `pub mod sse;`를 더한다(`pub mod sidecar;` 다음 줄에). 실행한다:

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test sse:: 2>&1 | Select-Object -Last 25
```

Expected: `12 failed` — 모두 `not yet implemented`.

- [ ] **Step 8: 해석기를 구현한다**

`sse.rs`에서 뼈대(첫 줄부터 `#[cfg(test)]` 앞까지)를 아래 구현으로 바꾼다. 테스트 모듈은 그대로 둔다:

```rust
/// One event of a server-sent-events stream: its `event:` name (empty when the stream did not name it) and its `data:`
/// lines joined with `\n`.
#[derive(Debug, Clone, PartialEq)]
pub struct SseEvent {
    pub name: String,
    pub data: String,
}

/// Cuts the bytes of an SSE stream into events. Bytes may come in any pieces: a line is decoded only when it is complete, so
/// a character or a `\r\n` that is split between two reads comes out whole.
#[derive(Default)]
pub struct SseParser {
    /// The start of a line that has not ended yet.
    pending: Vec<u8>,
    name: String,
    /// `None` until the event under construction has a `data:` line.
    data: Option<String>,
}

impl SseParser {
    pub fn new() -> SseParser {
        SseParser::default()
    }

    /// Feeds [bytes] of the stream and returns the events they finish.
    pub fn push(&mut self, bytes: &[u8]) -> Vec<SseEvent> {
        let mut buffer = std::mem::take(&mut self.pending);
        buffer.extend_from_slice(bytes);
        let mut events = Vec::new();
        let mut start = 0;
        let mut index = 0;
        while index < buffer.len() {
            let byte = buffer[index];
            if byte != b'\n' && byte != b'\r' {
                index += 1;
                continue;
            }
            if byte == b'\r' && index + 1 == buffer.len() {
                break; // a CR at the very end may be the first half of CRLF: wait for the next byte
            }
            let next = if byte == b'\r' && buffer[index + 1] == b'\n' { index + 2 } else { index + 1 };
            if let Some(event) = self.line(&buffer[start..index]) {
                events.push(event);
            }
            start = next;
            index = next;
        }
        self.pending = buffer.split_off(start);
        events
    }

    fn line(&mut self, line: &[u8]) -> Option<SseEvent> {
        if line.is_empty() {
            // A blank line ends the event. One without a data line is not an event.
            let name = std::mem::take(&mut self.name);
            return self.data.take().map(|data| SseEvent { name, data });
        }
        if line[0] == b':' {
            return None; // a comment (servers send them to keep a stream alive)
        }
        let (field, value) = match line.iter().position(|byte| *byte == b':') {
            Some(colon) => {
                let value = &line[colon + 1..];
                (&line[..colon], value.strip_prefix(b" ").unwrap_or(value))
            }
            None => (line, &line[line.len()..]),
        };
        let value = String::from_utf8_lossy(value).into_owned();
        match field {
            b"event" => self.name = value,
            b"data" => match &mut self.data {
                Some(data) => {
                    data.push('\n');
                    data.push_str(&value);
                }
                None => self.data = Some(value),
            },
            _ => {} // `id`, `retry` and unknown fields are of no use to the bridge
        }
        None
    }
}
```

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test sse:: 2>&1 | Select-Object -Last 8
```

Expected: `12 passed`.

- [ ] **Step 9: 테스트가 정말 무는지 한 번 확인한다 (CR 처리)**

`sse.rs`의 `push`에서 `if byte == b'\r' && index + 1 == buffer.len() {`를 `if false && byte == b'\r' && index + 1 == buffer.len() {`로 잠시 바꾸고 `cargo test sse::`를 돌린다.

Expected: `a_crlf_split_between_two_pushes_is_one_line_end`와 `the_same_events_come_out_wherever_the_bytes_are_cut`가 FAIL(범위 밖 인덱스 패닉). 확인한 뒤 **원래대로 되돌리고** 다시 돌려 `12 passed`를 본다.

- [ ] **Step 10: 크레이트 전체와 커밋**

`lib.rs`에 `pub use error::BridgeError;`를 더한다(`pub use config::SidecarConfig;` 다음 줄에).

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test 2>&1 | Select-String "test result|FAILED|warning|error"
```

Expected: `test result: ok. 40 passed` (기존 25 + handshake 1 + error 2 + sse 12), 경고 없음.

```bash
git add sidecar-host
git commit -m "feat(sidecar-host): 브리지 오류 형식과 SSE 해석기를 더하고 핸드셰이크의 Debug에서 토큰을 가린다"
```

---

### Task 2: 가짜 서버와 서버 호출 6개

**Files:**
- Create: `sidecar-host/src/fake_server.rs`(테스트 전용), `sidecar-host/src/bridge.rs`
- Modify: `sidecar-host/src/lib.rs`

**Interfaces:**
- Consumes: Task 1의 `BridgeError`, `LOST`, `UNREADABLE_ANSWER`, `BAD_JOB_ID`, `Handshake`
- Produces (Task 3~7이 쓴다):
  - `Bridge` — `Clone`, `Debug`(토큰 가림). `Bridge::new(Handshake) -> Bridge`, `with_short_timeout(self, Duration) -> Bridge`(테스트용)
  - `tools(&self) -> Result<Value, BridgeError>`, `install_yt_dlp(&self)`, `update_yt_dlp(&self)`, `resolve(&self, input: &str)`, `start_job(&self, request: &Value)` — 모두 `Result<Value, BridgeError>`
  - `cancel_job(&self, job_id: &str) -> Result<(), BridgeError>`
  - 테스트 전용 `fake_server`: `FakeServer::{start(Vec<Vec<Step>>), one(Vec<Step>), requests() -> Vec<Recorded>, closed_port() -> u16}`, `Step::{Write(Vec<u8>), Pause(Duration)}`, `Recorded { head: String(소문자), body: Vec<u8> }`, `reply`, `json_reply`, `empty_reply`, `stream_head`, `chunk`, `last_chunk`, `sse_event`

- [ ] **Step 1: 가짜 서버를 만든다 (자기 테스트가 있다)**

`sidecar-host/src/fake_server.rs`를 쓴다. 테스트 도구이므로 구현과 테스트를 함께 둔다:

```rust
//! A scripted HTTP server for the tests: it answers each connection with the next script and remembers what it was asked.
//! The scripts are written with the wire format of the real server (Ktor): JSON answers with a `Content-Length`, a stream
//! as `Transfer-Encoding: chunked`.

use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::{Arc, Mutex};
use std::time::Duration;

pub enum Step {
    Write(Vec<u8>),
    Pause(Duration),
}

/// What one connection sent: the request line and the headers (in lower case), and the body.
#[derive(Debug, Clone)]
pub struct Recorded {
    pub head: String,
    pub body: Vec<u8>,
}

pub struct FakeServer {
    pub port: u16,
    requests: Arc<Mutex<Vec<Recorded>>>,
}

impl FakeServer {
    /// One script per connection, in the order the connections come. A connection that has no script is closed once its
    /// request is read, which is what a server that dies mid-call looks like.
    pub fn start(scripts: Vec<Vec<Step>>) -> FakeServer {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let requests = Arc::new(Mutex::new(Vec::new()));
        let recorded = Arc::clone(&requests);
        std::thread::spawn(move || {
            let mut scripts = scripts.into_iter();
            for stream in listener.incoming() {
                let Ok(stream) = stream else { break };
                let script = scripts.next().unwrap_or_default();
                let recorded = Arc::clone(&recorded);
                std::thread::spawn(move || serve(stream, script, recorded));
            }
        });
        FakeServer { port, requests }
    }

    /// A server with one script.
    pub fn one(script: Vec<Step>) -> FakeServer {
        FakeServer::start(vec![script])
    }

    pub fn requests(&self) -> Vec<Recorded> {
        self.requests.lock().unwrap().clone()
    }

    /// A port nothing listens on.
    pub fn closed_port() -> u16 {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        listener.local_addr().unwrap().port()
    }
}

fn serve(mut stream: TcpStream, script: Vec<Step>, recorded: Arc<Mutex<Vec<Recorded>>>) {
    let (head, body) = read_request(&mut stream);
    recorded.lock().unwrap().push(Recorded { head, body });
    for step in script {
        match step {
            Step::Write(bytes) => {
                if stream.write_all(&bytes).is_err() {
                    return;
                }
                let _ = stream.flush();
            }
            Step::Pause(time) => std::thread::sleep(time),
        }
    }
    // Dropping the stream closes the connection: an answer without a length ends here.
}

fn read_request(stream: &mut TcpStream) -> (String, Vec<u8>) {
    let mut data = Vec::new();
    let mut buffer = [0u8; 1024];
    let head_end = loop {
        if let Some(position) = data.windows(4).position(|window| window == b"\r\n\r\n") {
            break position + 4;
        }
        let read = stream.read(&mut buffer).unwrap_or(0);
        if read == 0 {
            break data.len();
        }
        data.extend_from_slice(&buffer[..read]);
    };
    let head = String::from_utf8_lossy(&data[..head_end]).to_lowercase();
    let wanted = head
        .lines()
        .find_map(|line| line.strip_prefix("content-length:"))
        .and_then(|value| value.trim().parse::<usize>().ok())
        .unwrap_or(0);
    while data.len() < head_end + wanted {
        let read = stream.read(&mut buffer).unwrap_or(0);
        if read == 0 {
            break;
        }
        data.extend_from_slice(&buffer[..read]);
    }
    (head, data[head_end..].to_vec())
}

/// An answer with a body and a `Content-Length`, e.g. `reply(200, "OK", "application/json", "{}")`.
pub fn reply(status: u16, reason: &str, content_type: &str, body: &str) -> Vec<Step> {
    let head = format!(
        "HTTP/1.1 {status} {reason}\r\nContent-Type: {content_type}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
        body.len()
    );
    vec![Step::Write(head.into_bytes()), Step::Write(body.as_bytes().to_vec())]
}

pub fn json_reply(status: u16, reason: &str, body: &str) -> Vec<Step> {
    reply(status, reason, "application/json", body)
}

/// An answer with no body (`204 No Content`).
pub fn empty_reply(status: u16, reason: &str) -> Vec<Step> {
    vec![Step::Write(format!("HTTP/1.1 {status} {reason}\r\nConnection: close\r\n\r\n").into_bytes())]
}

/// The head of an SSE answer: 200, `text/event-stream`, chunked. Chunks follow ([chunk]) until [last_chunk].
pub fn stream_head() -> Step {
    Step::Write(
        b"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n".to_vec(),
    )
}

/// One chunk of a chunked body.
pub fn chunk(bytes: &[u8]) -> Step {
    let mut framed = format!("{:x}\r\n", bytes.len()).into_bytes();
    framed.extend_from_slice(bytes);
    framed.extend_from_slice(b"\r\n");
    Step::Write(framed)
}

pub fn last_chunk() -> Step {
    Step::Write(b"0\r\n\r\n".to_vec())
}

/// One SSE event as the server writes it.
pub fn sse_event(name: &str, data: &str) -> Step {
    chunk(format!("event: {name}\r\ndata: {data}\r\n\r\n").as_bytes())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_answers_and_remembers_the_request() {
        let server = FakeServer::one(json_reply(200, "OK", "{}"));

        let body = ureq::post(&format!("http://127.0.0.1:{}/x", server.port))
            .set("X-Test", "abc")
            .send_string("body")
            .unwrap()
            .into_string()
            .unwrap();

        assert_eq!(body, "{}");
        let requests = server.requests();
        assert_eq!(requests.len(), 1);
        assert!(requests[0].head.starts_with("post /x http/1.1"), "{}", requests[0].head);
        assert_eq!(requests[0].body, b"body");
    }
}
```

`lib.rs`에 더한다(`pub mod error;` 아래): `#[cfg(test)] mod fake_server;`

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test fake_server 2>&1 | Select-Object -Last 8
```

Expected: `1 passed`.

- [ ] **Step 2: `Bridge`의 실패하는 테스트를 쓴다**

`sidecar-host/src/bridge.rs`를 아래 뼈대와 테스트로 만든다. 뼈대의 `use`와 필드가 쓰이지 않아 경고가 나는 것은 지금은 괜찮다:

```rust
use crate::error::{BridgeError, BAD_JOB_ID, UNREADABLE_ANSWER};
use crate::handshake::Handshake;
use serde_json::{json, Value};
use std::time::Duration;

#[derive(Debug, Clone)]
pub struct Bridge {
    handshake: Handshake,
}

impl Bridge {
    pub fn new(handshake: Handshake) -> Bridge {
        Bridge { handshake }
    }

    pub fn with_short_timeout(self, _timeout: Duration) -> Bridge {
        self
    }

    pub fn tools(&self) -> Result<Value, BridgeError> {
        todo!()
    }

    pub fn install_yt_dlp(&self) -> Result<Value, BridgeError> {
        todo!()
    }

    pub fn update_yt_dlp(&self) -> Result<Value, BridgeError> {
        todo!()
    }

    pub fn resolve(&self, _input: &str) -> Result<Value, BridgeError> {
        todo!()
    }

    pub fn start_job(&self, _request: &Value) -> Result<Value, BridgeError> {
        todo!()
    }

    pub fn cancel_job(&self, _job_id: &str) -> Result<(), BridgeError> {
        todo!()
    }
}
```

뼈대 바로 아래에 테스트를 붙인다. 서버의 응답 문구는 2026-10-11에 실제 사이드카에서 받은 그대로다:

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use crate::error::LOST;
    use crate::fake_server::*;
    use std::time::Instant;

    const TOKEN: &str = "tok-0123456789";

    fn bridge(server: &FakeServer) -> Bridge {
        Bridge::new(Handshake { port: server.port, token: TOKEN.to_string() })
    }

    fn assert_no_token(error: &BridgeError) {
        assert!(!error.message.contains(TOKEN), "the token is in the message: {}", error.message);
    }

    // ---- what is sent ----

    #[test]
    fn a_get_carries_the_token_and_the_answer_comes_back_as_json() {
        let server = FakeServer::one(json_reply(200, "OK", r#"{"ytDlp":{"found":false,"version":null,"path":null}}"#));

        let tools = bridge(&server).tools().unwrap();

        assert_eq!(tools["ytDlp"]["found"], false);
        let sent = &server.requests()[0];
        assert!(sent.head.starts_with("get /tools http/1.1"), "{}", sent.head);
        assert!(sent.head.contains(&format!("x-xgs-token: {TOKEN}")), "{}", sent.head);
    }

    #[test]
    fn a_resolve_sends_the_input_as_utf8_json_with_the_length_in_bytes() {
        let server = FakeServer::one(json_reply(200, "OK", r#"{"resolveId":"r1","kind":"VIDEO","items":[]}"#));
        let input = "https://www.youtube.com/watch?v=abc \"한글\" \\ 🎵";

        bridge(&server).resolve(input).unwrap();

        let sent = &server.requests()[0];
        assert!(sent.head.starts_with("post /resolve http/1.1"), "{}", sent.head);
        assert!(sent.head.contains("content-type: application/json"), "{}", sent.head);
        assert!(sent.head.contains(&format!("content-length: {}", sent.body.len())), "{}", sent.head);
        assert!(sent.body.len() > input.chars().count(), "the test input has multi-byte characters");
        let body: Value = serde_json::from_slice(&sent.body).unwrap();
        assert_eq!(body["input"], input);
    }

    #[test]
    fn a_job_request_is_passed_on_as_it_is() {
        let server = FakeServer::one(json_reply(201, "Created", r#"{"jobId":"j-1"}"#));
        let request = json!({"resolveId":"r1","options":{"outputDir":"C:\\음악","albumName":"앨범 \"1\""},"ranks":[1,3]});

        let created = bridge(&server).start_job(&request).unwrap();

        assert_eq!(created["jobId"], "j-1");
        let sent = &server.requests()[0];
        assert!(sent.head.starts_with("post /jobs http/1.1"), "{}", sent.head);
        assert_eq!(serde_json::from_slice::<Value>(&sent.body).unwrap(), request);
    }

    #[test]
    fn the_calls_without_a_body_are_posts_with_an_empty_body() {
        let server = FakeServer::start(vec![
            json_reply(200, "OK", r#"{"message":"설치했습니다."}"#),
            json_reply(200, "OK", r#"{"message":"최신 버전입니다."}"#),
        ]);

        assert_eq!(bridge(&server).install_yt_dlp().unwrap()["message"], "설치했습니다.");
        assert_eq!(bridge(&server).update_yt_dlp().unwrap()["message"], "최신 버전입니다.");

        let sent = server.requests();
        assert!(sent[0].head.starts_with("post /tools/yt-dlp/install http/1.1"), "{}", sent[0].head);
        assert!(sent[1].head.starts_with("post /tools/yt-dlp/update http/1.1"), "{}", sent[1].head);
        assert!(sent.iter().all(|request| request.body.is_empty()));
        assert!(sent.iter().all(|request| request.head.contains("content-length: 0")), "{sent:?}");
    }

    #[test]
    fn a_cancel_is_a_delete_that_answers_no_content() {
        let server = FakeServer::one(empty_reply(204, "No Content"));

        bridge(&server).cancel_job("3f2b8c1e-0000-4000-8000-123456789abc").unwrap();

        let sent = &server.requests()[0];
        assert!(sent.head.starts_with("delete /jobs/3f2b8c1e-0000-4000-8000-123456789abc http/1.1"), "{}", sent.head);
    }

    // ---- what comes back when it goes wrong ----

    #[test]
    fn the_servers_own_message_is_the_error() {
        let server =
            FakeServer::one(json_reply(422, "Unprocessable Entity", r#"{"message":"재생목록 ID 또는 영상 주소로 인식할 수 없습니다."}"#));

        let error = bridge(&server).resolve("한글").unwrap_err();

        assert_eq!(error.message, "재생목록 ID 또는 영상 주소로 인식할 수 없습니다.");
    }

    #[test]
    fn an_expired_resolve_and_an_unknown_job_say_what_the_server_says() {
        let server = FakeServer::start(vec![
            json_reply(404, "Not Found", r#"{"message":"조회 결과가 만료되었습니다. 다시 조회하세요."}"#),
            json_reply(404, "Not Found", r#"{"message":"작업을 찾을 수 없습니다."}"#),
        ]);

        assert_eq!(
            bridge(&server).start_job(&json!({"resolveId":"x"})).unwrap_err().message,
            "조회 결과가 만료되었습니다. 다시 조회하세요."
        );
        assert_eq!(bridge(&server).cancel_job("abc").unwrap_err().message, "작업을 찾을 수 없습니다.");
    }

    #[test]
    fn an_error_without_a_message_is_reported_by_its_status() {
        let server = FakeServer::start(vec![
            reply(500, "Internal Server Error", "text/plain", "boom"),
            empty_reply(404, "Not Found"),
            json_reply(500, "Internal Server Error", r#"{"detail":"no message field"}"#),
        ]);

        assert_eq!(bridge(&server).tools().unwrap_err().message, "서버 오류 (500)");
        assert_eq!(bridge(&server).tools().unwrap_err().message, "서버 오류 (404)");
        assert_eq!(bridge(&server).tools().unwrap_err().message, "서버 오류 (500)");
    }

    #[test]
    fn an_answer_that_is_not_json_is_unreadable() {
        let server = FakeServer::one(reply(200, "OK", "text/html", "<html>proxy</html>"));

        assert_eq!(bridge(&server).tools().unwrap_err().message, UNREADABLE_ANSWER);
    }

    #[test]
    fn a_server_that_is_not_there_is_a_lost_connection() {
        let bridge = Bridge::new(Handshake { port: FakeServer::closed_port(), token: TOKEN.to_string() });

        let error = bridge.tools().unwrap_err();

        assert_eq!(error.message, LOST);
        assert_no_token(&error);
    }

    #[test]
    fn a_server_that_hangs_up_without_an_answer_is_a_lost_connection() {
        let server = FakeServer::one(Vec::new());

        assert_eq!(bridge(&server).resolve("x").unwrap_err().message, LOST);
    }

    #[test]
    fn a_server_that_stops_answering_does_not_hold_a_short_call_forever() {
        let server = FakeServer::one(vec![Step::Pause(Duration::from_secs(5))]);
        let started = Instant::now();

        let error = bridge(&server).with_short_timeout(Duration::from_millis(300)).tools().unwrap_err();

        assert_eq!(error.message, LOST);
        assert!(started.elapsed() < Duration::from_secs(3), "it gave up at the limit: {:?}", started.elapsed());
    }

    #[test]
    fn a_resolve_waits_as_long_as_the_server_works() {
        let mut script = vec![Step::Pause(Duration::from_millis(600))];
        script.extend(json_reply(200, "OK", r#"{"resolveId":"r1","kind":"VIDEO","items":[]}"#));
        let server = FakeServer::one(script);

        let resolved = bridge(&server).with_short_timeout(Duration::from_millis(200)).resolve("x").unwrap();

        assert_eq!(resolved["resolveId"], "r1");
    }

    #[test]
    fn a_big_answer_with_multi_byte_text_comes_through_whole() {
        let titles = "가나다라마바사 ".repeat(120_000);
        let body = json!({"resolveId":"r1","kind":"PLAYLIST","items":[{"title":titles}]}).to_string();
        assert!(body.len() > 2_000_000);
        let server = FakeServer::one(json_reply(200, "OK", &body));

        let resolved = bridge(&server).resolve("PL123").unwrap();

        assert_eq!(resolved["items"][0]["title"].as_str().unwrap(), titles);
    }

    #[test]
    fn no_error_of_any_call_holds_the_token() {
        let server = FakeServer::start(vec![
            json_reply(401, "Unauthorized", r#"{"message":"인증 토큰이 올바르지 않습니다."}"#),
            reply(500, "Internal Server Error", "text/plain", "boom"),
            reply(200, "OK", "text/html", "nope"),
            Vec::new(),
        ]);
        let bridge = bridge(&server);

        for error in [
            bridge.tools().unwrap_err(),
            bridge.tools().unwrap_err(),
            bridge.tools().unwrap_err(),
            bridge.tools().unwrap_err(),
        ] {
            assert_no_token(&error);
        }
        assert!(!format!("{bridge:?}").contains(TOKEN), "Debug output of the bridge");
    }

    // ---- the job ID ----

    /// IDs the page must not be able to use: each could change which route of the server a path reaches.
    const BAD_JOB_IDS: [&str; 9] = ["", "../tools", "a/b", "a?b=c", "a#b", "a%2fb", "a b", "a\r\nX-Evil: 1", "한글"];

    #[test]
    fn a_job_id_that_could_reach_another_route_is_refused_without_a_request() {
        let server = FakeServer::start(Vec::new());
        let bridge = bridge(&server);

        for bad in BAD_JOB_IDS.into_iter().chain(["a".repeat(65).as_str()]) {
            assert_eq!(bridge.cancel_job(bad).unwrap_err().message, BAD_JOB_ID, "cancel {bad:?}");
        }

        assert!(server.requests().is_empty(), "nothing reached the server: {:?}", server.requests());
    }
}
```

`lib.rs`에 `pub mod bridge;`(맨 위, `pub mod config;` 앞)와 `pub use bridge::Bridge;`(`pub use config::SidecarConfig;` 앞)를 더한다.

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test bridge:: 2>&1 | Select-Object -Last 30
```

Expected: `16 failed`(모두 `not yet implemented`), 통과 0.

- [ ] **Step 3: 구현한다**

`bridge.rs`에서 뼈대(첫 줄부터 `#[cfg(test)]` 앞까지)를 아래 구현으로 바꾼다. 테스트 모듈은 그대로 둔다:

```rust
//! The calls the page can make to the server, and nothing else. Each is one HTTP request to the sidecar with the token the
//! page never sees; what comes back is the server's JSON as it is (the shapes are `shared/.../ApiModels.kt`), and a failure
//! is one `BridgeError` with a message fit for the user.

use crate::error::{BridgeError, BAD_JOB_ID, UNREADABLE_ANSWER};
use crate::handshake::Handshake;
use serde_json::{json, Value};
use std::time::Duration;

/// Loopback: a server that is there accepts at once.
const CONNECT_TIMEOUT: Duration = Duration::from_secs(5);
/// How long a call that is answered at once may take. The calls that wait for yt-dlp (resolve, install, update) and the
/// event stream have no limit, as in the app's own client (`requestTimeout = 0`).
const SHORT_CALL_TIMEOUT: Duration = Duration::from_secs(60);

#[derive(Debug, Clone)]
pub struct Bridge {
    handshake: Handshake,
    short_timeout: Duration,
}

impl Bridge {
    pub fn new(handshake: Handshake) -> Bridge {
        Bridge { handshake, short_timeout: SHORT_CALL_TIMEOUT }
    }

    /// For tests: the limit of the calls that are answered at once.
    pub fn with_short_timeout(mut self, timeout: Duration) -> Bridge {
        self.short_timeout = timeout;
        self
    }

    /// `GET /tools`
    pub fn tools(&self) -> Result<Value, BridgeError> {
        self.json("GET", "/tools", None, Limit::Short)
    }

    /// `POST /tools/yt-dlp/install`
    pub fn install_yt_dlp(&self) -> Result<Value, BridgeError> {
        self.json("POST", "/tools/yt-dlp/install", None, Limit::None)
    }

    /// `POST /tools/yt-dlp/update`
    pub fn update_yt_dlp(&self) -> Result<Value, BridgeError> {
        self.json("POST", "/tools/yt-dlp/update", None, Limit::None)
    }

    /// `POST /resolve`
    pub fn resolve(&self, input: &str) -> Result<Value, BridgeError> {
        self.json("POST", "/resolve", Some(&json!({ "input": input })), Limit::None)
    }

    /// `POST /jobs`: [request] is a `JobRequest`, passed on as it is (the server checks it).
    pub fn start_job(&self, request: &Value) -> Result<Value, BridgeError> {
        self.json("POST", "/jobs", Some(request), Limit::Short)
    }

    /// `DELETE /jobs/{id}`
    pub fn cancel_job(&self, job_id: &str) -> Result<(), BridgeError> {
        check_job_id(job_id)?;
        self.send("DELETE", &format!("/jobs/{job_id}"), None, Limit::Short).map(|_| ())
    }

    fn json(&self, method: &str, path: &str, body: Option<&Value>, limit: Limit) -> Result<Value, BridgeError> {
        let text = self.send(method, path, body, limit)?.into_string().map_err(|_| BridgeError::lost())?;
        serde_json::from_str(&text).map_err(|_| BridgeError::new(UNREADABLE_ANSWER))
    }

    fn send(&self, method: &str, path: &str, body: Option<&Value>, limit: Limit) -> Result<ureq::Response, BridgeError> {
        let mut agent = ureq::AgentBuilder::new().timeout_connect(CONNECT_TIMEOUT);
        if let Limit::Short = limit {
            agent = agent.timeout(self.short_timeout);
        }
        let url = format!("http://127.0.0.1:{}{}", self.handshake.port, path);
        let request = agent.build().request(method, &url).set("X-XGS-Token", &self.handshake.token);
        let result = match (method, body) {
            (_, Some(body)) => request.set("Content-Type", "application/json").send_string(&body.to_string()),
            ("POST", None) => request.send_string(""),
            (_, None) => request.call(),
        };
        match result {
            Ok(response) => Ok(response),
            Err(ureq::Error::Status(status, response)) => Err(failure(status, response)),
            Err(ureq::Error::Transport(_)) => Err(BridgeError::lost()),
        }
    }
}

#[derive(Clone, Copy)]
enum Limit {
    Short,
    None,
}

/// A job ID is what the server made: a UUID. Anything else is refused here, so that the ID in a path cannot reach another
/// route of the server (`../tools`, `x/events`, `x?y`).
fn check_job_id(job_id: &str) -> Result<(), BridgeError> {
    let valid = !job_id.is_empty()
        && job_id.len() <= 64
        && job_id.chars().all(|character| character.is_ascii_alphanumeric() || character == '-');
    if valid {
        Ok(())
    } else {
        Err(BridgeError::new(BAD_JOB_ID))
    }
}

/// The message of a failed answer: the server's own (`{"message": ...}`), else the status.
fn failure(status: u16, response: ureq::Response) -> BridgeError {
    let message = response.into_string().ok().and_then(|text| error_message(&text));
    BridgeError::new(message.unwrap_or_else(|| format!("서버 오류 ({status})")))
}

fn error_message(text: &str) -> Option<String> {
    serde_json::from_str::<Value>(text).ok()?.get("message")?.as_str().map(str::to_string)
}
```

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test bridge:: 2>&1 | Select-Object -Last 30
```

Expected: `16 passed`. 컴파일 경고가 4건 나온다 — `fake_server`의 스트림 도우미(`stream_head`, `chunk`, `last_chunk`, `sse_event`)가 Task 3에서 쓰이기 전까지는 쓰이지 않기 때문이다(`Read`와 `SseParser`도 Task 3에서 쓰므로 지금은 가져오지 않았다).

- [ ] **Step 4: 크레이트 전체와 커밋**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test 2>&1 | Select-String "test result|FAILED|warning|error"
```

Expected: `test result: ok. 57 passed`(40 + fake_server 1 + bridge 16), 경고는 위의 4건뿐.

```bash
git add sidecar-host
git commit -m "feat(sidecar-host): 서버 호출 6개를 가짜 서버로 시험한 브리지로 더한다"
```

---

### Task 3: 작업 이벤트 스트림 (`watch_job`)

**Files:**
- Modify: `sidecar-host/src/bridge.rs`

**Interfaces:**
- Consumes: Task 1의 `SseParser`, `UNREADABLE_EVENT`, `LISTENER_GONE`, Task 2의 `Bridge`, `FakeServer`와 `stream_head`/`chunk`/`sse_event`/`last_chunk`
- Produces (Task 4~7이 쓴다): `Bridge::watch_job(&self, job_id: &str, on_event: impl FnMut(Value) -> bool) -> Result<(), BridgeError>` — 이벤트의 `data` JSON을 오는 즉시 순서대로 `on_event`에 준다. `on_event`가 false를 돌려주면 `LISTENER_GONE`으로 끝낸다. `job-done` 이벤트를 준 직후 `Ok(())`로 끝나고 스트림이 닫히기를 기다리지 않는다. `error` 이벤트는 그 메시지를 `Err`로, 데이터가 JSON이 아니면 `UNREADABLE_EVENT`, `job-done` 없이 끝나거나 끊기면 `LOST`

- [ ] **Step 1: 뼈대와 실패하는 테스트를 더한다**

`bridge.rs`의 `use` 세 줄을 아래로 바꾼다(`SseParser`와 `Read`는 구현을 쓰기 전까지 경고가 난다):

```rust
use crate::error::{BridgeError, BAD_JOB_ID, LISTENER_GONE, UNREADABLE_ANSWER, UNREADABLE_EVENT};
use crate::handshake::Handshake;
use crate::sse::SseParser;
use serde_json::{json, Value};
use std::io::Read;
```

`impl Bridge` 안, `cancel_job` 뒤에 뼈대를 더한다:

```rust
    pub fn watch_job(&self, _job_id: &str, _on_event: impl FnMut(Value) -> bool) -> Result<(), BridgeError> {
        todo!()
    }
```

`mod tests`의 맨 끝(마지막 `}` 앞)에 스트림 테스트를 붙인다:

```rust
    // ---- the event stream ----

    #[test]
    fn a_job_id_that_could_reach_another_route_is_refused_for_the_stream_too() {
        let server = FakeServer::start(Vec::new());
        let bridge = bridge(&server);

        for bad in BAD_JOB_IDS.into_iter().chain(["a".repeat(65).as_str()]) {
            assert_eq!(bridge.watch_job(bad, |_| true).unwrap_err().message, BAD_JOB_ID, "watch {bad:?}");
        }

        assert!(server.requests().is_empty(), "nothing reached the server: {:?}", server.requests());
    }

    fn collect(bridge: &Bridge, job_id: &str) -> (Vec<Value>, Result<(), BridgeError>) {
        let mut events = Vec::new();
        let result = bridge.watch_job(job_id, |event| {
            events.push(event);
            true
        });
        (events, result)
    }

    #[test]
    fn the_events_arrive_in_order_and_the_call_ends_with_job_done() {
        let server = FakeServer::one(vec![
            stream_head(),
            sse_event("item-started", r#"{"type":"item-started","rank":1,"videoId":"v1","fileName":"001 가수 - 제목.m4a"}"#),
            sse_event("progress", r#"{"type":"progress","rank":1,"stage":"DOWNLOADING","percent":42.5}"#),
            sse_event("item-done", r#"{"type":"item-done","rank":1,"fileName":"001 가수 - 제목.m4a","lyrics":"ONLINE"}"#),
            sse_event("job-done", r#"{"type":"job-done","status":"COMPLETED","summary":{"succeeded":1,"skipped":0,"failed":0}}"#),
            last_chunk(),
        ]);

        let (events, result) = collect(&bridge(&server), "j-1");

        result.unwrap();
        let types: Vec<&str> = events.iter().map(|event| event["type"].as_str().unwrap()).collect();
        assert_eq!(types, ["item-started", "progress", "item-done", "job-done"]);
        assert_eq!(events[0]["fileName"], "001 가수 - 제목.m4a");
        assert_eq!(events[1]["percent"], 42.5);
        let sent = &server.requests()[0];
        assert!(sent.head.starts_with("get /jobs/j-1/events http/1.1"), "{}", sent.head);
        assert!(sent.head.contains("x-xgs-token"), "{}", sent.head);
    }

    #[test]
    fn an_event_arrives_when_it_is_sent_not_when_the_stream_ends() {
        let server = FakeServer::one(vec![
            stream_head(),
            sse_event("progress", r#"{"type":"progress","rank":1,"stage":"DOWNLOADING","percent":1.0}"#),
            Step::Pause(Duration::from_millis(1500)),
            sse_event("job-done", r#"{"type":"job-done","status":"COMPLETED","summary":{"succeeded":0,"skipped":0,"failed":0}}"#),
            last_chunk(),
        ]);
        let started = Instant::now();
        let mut first_at = None;

        bridge(&server)
            .watch_job("j-1", |event| {
                if event["type"] == "progress" {
                    first_at = Some(started.elapsed());
                }
                true
            })
            .unwrap();

        assert!(first_at.unwrap() < Duration::from_millis(1000), "first event after {first_at:?}");
    }

    #[test]
    fn the_call_ends_at_job_done_without_waiting_for_the_stream_to_close() {
        let server = FakeServer::one(vec![
            stream_head(),
            sse_event("job-done", r#"{"type":"job-done","status":"CANCELLED","summary":{"succeeded":0,"skipped":0,"failed":0}}"#),
            Step::Pause(Duration::from_secs(5)),
            last_chunk(),
        ]);
        let started = Instant::now();

        let (events, result) = collect(&bridge(&server), "j-1");

        result.unwrap();
        assert_eq!(events.len(), 1);
        assert!(started.elapsed() < Duration::from_secs(3), "waited {:?}", started.elapsed());
    }

    #[test]
    fn a_korean_character_split_between_two_chunks_is_whole() {
        let line = "event: item-done\r\ndata: {\"type\":\"item-done\",\"rank\":1,\"fileName\":\"가나다.m4a\"}\r\n\r\n".as_bytes();
        let cut = line.iter().position(|byte| *byte == 0xEB).unwrap() + 4; // after the first byte of the third character
        assert!(std::str::from_utf8(&line[..cut]).is_err(), "the cut is inside a character");
        let server = FakeServer::one(vec![
            stream_head(),
            chunk(&line[..cut]),
            Step::Pause(Duration::from_millis(200)),
            chunk(&line[cut..]),
            sse_event("job-done", r#"{"type":"job-done","status":"COMPLETED","summary":{"succeeded":1,"skipped":0,"failed":0}}"#),
            last_chunk(),
        ]);

        let (events, result) = collect(&bridge(&server), "j-1");

        result.unwrap();
        assert_eq!(events[0]["fileName"], "가나다.m4a");
    }

    #[test]
    fn keep_alive_comments_and_unknown_events_do_not_disturb_the_stream() {
        let server = FakeServer::one(vec![
            stream_head(),
            chunk(b": keep-alive\r\n\r\n"),
            sse_event("something-new", r#"{"type":"something-new","x":1}"#),
            sse_event("job-done", r#"{"type":"job-done","status":"COMPLETED","summary":{"succeeded":0,"skipped":0,"failed":0}}"#),
            last_chunk(),
        ]);

        let (events, result) = collect(&bridge(&server), "j-1");

        result.unwrap();
        assert_eq!(events.len(), 2);
        assert_eq!(events[0]["type"], "something-new");
    }

    #[test]
    fn an_error_event_is_the_error_and_no_event_is_delivered() {
        let server = FakeServer::one(vec![
            stream_head(),
            sse_event("error", r#"{"message":"작업을 찾을 수 없습니다."}"#),
            last_chunk(),
        ]);

        let (events, result) = collect(&bridge(&server), "nope");

        assert_eq!(result.unwrap_err().message, "작업을 찾을 수 없습니다.");
        assert!(events.is_empty());
    }

    #[test]
    fn a_stream_that_ends_without_job_done_is_a_lost_connection_after_the_events_it_sent() {
        let server = FakeServer::one(vec![
            stream_head(),
            sse_event("progress", r#"{"type":"progress","rank":1,"stage":"DOWNLOADING","percent":10.0}"#),
            last_chunk(),
        ]);

        let (events, result) = collect(&bridge(&server), "j-1");

        assert_eq!(events.len(), 1);
        assert_eq!(result.unwrap_err().message, LOST);
    }

    #[test]
    fn a_connection_that_breaks_in_the_middle_of_the_stream_is_a_lost_connection() {
        let server = FakeServer::one(vec![
            stream_head(),
            sse_event("progress", r#"{"type":"progress","rank":1,"stage":"DOWNLOADING","percent":10.0}"#),
            Step::Write(b"5\r\nevent".to_vec()), // a chunk that is cut off, then the connection closes
        ]);

        let (events, result) = collect(&bridge(&server), "j-1");

        assert_eq!(events.len(), 1);
        assert_eq!(result.unwrap_err().message, LOST);
    }

    #[test]
    fn event_data_that_is_not_json_is_unreadable() {
        let server = FakeServer::one(vec![stream_head(), sse_event("progress", "not json"), last_chunk()]);

        let (_, result) = collect(&bridge(&server), "j-1");

        assert_eq!(result.unwrap_err().message, UNREADABLE_EVENT);
    }

    #[test]
    fn a_stream_that_cannot_be_opened_reports_the_servers_message() {
        let server =
            FakeServer::one(json_reply(401, "Unauthorized", r#"{"message":"인증 토큰이 올바르지 않습니다."}"#));

        let (events, result) = collect(&bridge(&server), "j-1");

        assert!(events.is_empty());
        assert_eq!(result.unwrap_err().message, "인증 토큰이 올바르지 않습니다.");
    }

    #[test]
    fn the_call_stops_when_nobody_listens_any_more() {
        let server = FakeServer::one(vec![
            stream_head(),
            sse_event("progress", r#"{"type":"progress","rank":1,"stage":"DOWNLOADING","percent":1.0}"#),
            sse_event("progress", r#"{"type":"progress","rank":1,"stage":"DOWNLOADING","percent":2.0}"#),
            sse_event("job-done", r#"{"type":"job-done","status":"COMPLETED","summary":{"succeeded":0,"skipped":0,"failed":0}}"#),
            last_chunk(),
        ]);
        let mut delivered = 0;

        let result = bridge(&server).watch_job("j-1", |_| {
            delivered += 1;
            false
        });

        assert_eq!(result.unwrap_err().message, LISTENER_GONE);
        assert_eq!(delivered, 1, "no event after the listener said it was gone");
    }
```

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test bridge:: 2>&1 | Select-String "test result|FAILED"
```

Expected: `12 failed`(스트림 테스트 12개, `not yet implemented`), `16 passed`.

- [ ] **Step 2: 구현한다**

뼈대 `watch_job`을 아래로 바꾼다:

```rust
    /// `GET /jobs/{id}/events`: calls [on_event] with the data of each event, in order and as soon as it arrives, and returns
    /// after the `job-done` event. [on_event] returns false when nobody listens any more; the call then stops.
    ///
    /// Fails when the server sends an `error` event, when its data cannot be read, and when the stream ends or breaks
    /// before `job-done`.
    pub fn watch_job(&self, job_id: &str, mut on_event: impl FnMut(Value) -> bool) -> Result<(), BridgeError> {
        check_job_id(job_id)?;
        let response = self.send("GET", &format!("/jobs/{job_id}/events"), None, Limit::None)?;
        let mut reader = response.into_reader();
        let mut parser = SseParser::new();
        let mut buffer = [0u8; 8192];
        loop {
            let read = match reader.read(&mut buffer) {
                // The stream ended or broke, and `job-done` (which returns below) did not come: it was cut off.
                Ok(0) | Err(_) => return Err(BridgeError::lost()),
                Ok(read) => read,
            };
            for event in parser.push(&buffer[..read]) {
                if event.name == "error" {
                    return Err(BridgeError::new(error_message(&event.data).unwrap_or_else(|| UNREADABLE_EVENT.to_string())));
                }
                let data: Value =
                    serde_json::from_str(&event.data).map_err(|_| BridgeError::new(UNREADABLE_EVENT))?;
                if !on_event(data) {
                    return Err(BridgeError::new(LISTENER_GONE));
                }
                if event.name == "job-done" {
                    return Ok(());
                }
            }
        }
    }
```

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test bridge:: 2>&1 | Select-String "test result|FAILED|warning"
```

Expected: `test result: ok. 28 passed`, 경고 없음.

- [ ] **Step 3: 크레이트 전체와 커밋**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test 2>&1 | Select-String "test result|FAILED|warning|error"
```

Expected: `test result: ok. 69 passed`.

```bash
git add sidecar-host
git commit -m "feat(sidecar-host): 작업 이벤트를 오는 즉시 넘기고 job-done에서 끝나는 watch_job을 더한다"
```

---

### Task 4: `Lifecycle` — 시작 대기와 종료

**Files:**
- Create: `sidecar-host/src/lifecycle.rs`
- Modify: `sidecar-host/src/lib.rs`

**Interfaces:**
- Consumes: Task 2의 `Bridge`, 기존 `Sidecar`(`handshake: Handshake` 공개 필드, `spawn`, `shutdown(&mut self, Duration) -> bool`), Task 1의 `BridgeError`
- Produces (Task 7이 쓴다): `Lifecycle`(`Clone`) — `Lifecycle::start(impl FnOnce() -> Result<Sidecar, String> + Send + 'static) -> Lifecycle`(자기 스레드에서 실행), `wait_ready(&self, timeout: Duration) -> Result<Bridge, BridgeError>`, `shutdown(&self, grace: Duration, start_wait: Duration)`. 호출은 잠금을 쥔 채 실행되지 않는다(`Bridge`는 포트와 토큰의 복사본이다). 시작 중에 `shutdown`하면 `start_wait`까지 기다렸다가 `exit` 줄로 정상 종료하고, 기다림을 포기한 뒤에 도착한 사이드카도 스스로 정상 종료시킨다

- [ ] **Step 1: 뼈대와 실패하는 테스트를 쓴다**

`sidecar-host/src/lifecycle.rs`를 아래 뼈대와 테스트로 만든다. 테스트는 PowerShell을 사이드카 대역으로 쓰므로 Windows 전용이다(`sidecar.rs`의 테스트와 같다):

```rust
use crate::bridge::Bridge;
use crate::error::BridgeError;
use crate::sidecar::Sidecar;
use std::time::{Duration, Instant};

#[derive(Clone)]
pub struct Lifecycle {}

impl Lifecycle {
    pub fn start(_start: impl FnOnce() -> Result<Sidecar, String> + Send + 'static) -> Lifecycle {
        todo!()
    }

    pub fn wait_ready(&self, _timeout: Duration) -> Result<Bridge, BridgeError> {
        todo!()
    }

    pub fn shutdown(&self, _grace: Duration, _start_wait: Duration) {
        todo!()
    }
}
```

뼈대 바로 아래에 테스트를 붙인다:

```rust
#[cfg(all(test, windows))]
mod tests {
    use super::*;
    use std::path::{Path, PathBuf};
    use std::process::Command;

    const READY: &str = "[Console]::Out.WriteLine('XGS-READY 4321 tok-lifecycle'); [Console]::Out.Flush();";

    fn log_path(name: &str) -> PathBuf {
        std::env::temp_dir().join(format!("xgs-lifecycle-test-{}-{}.log", std::process::id(), name))
    }

    /// A stand-in for the sidecar: PowerShell running [script].
    fn start_stand_in(script: String, name: &str) -> Lifecycle {
        let log = log_path(name);
        Lifecycle::start(move || {
            let mut command = Command::new("powershell");
            command.args(["-NoProfile", "-NonInteractive", "-Command", &script]);
            Sidecar::spawn(command, &log, Duration::from_secs(30))
        })
    }

    /// A stand-in that, once ready, writes the first line it reads from stdin to [result] (`eof` when the pipe just closes).
    fn script_that_records_the_first_stdin_line(result: &Path, delay_seconds: u32) -> String {
        format!(
            "Start-Sleep -Seconds {delay_seconds}; {READY} $line = [Console]::In.ReadLine(); if ($null -eq $line) {{ $line = 'eof' }}; [IO.File]::WriteAllText('{}', [string]$line)",
            result.display()
        )
    }

    fn read_when_written(path: &Path) -> String {
        let deadline = Instant::now() + Duration::from_secs(15);
        while Instant::now() < deadline {
            let content = std::fs::read_to_string(path).unwrap_or_default();
            if !content.is_empty() {
                return content;
            }
            std::thread::sleep(Duration::from_millis(50));
        }
        String::new()
    }

    #[test]
    fn a_call_that_comes_while_the_sidecar_starts_waits_and_then_gets_the_means_to_call_it() {
        let lifecycle = start_stand_in(format!("Start-Sleep -Seconds 1; {READY} Start-Sleep -Seconds 30"), "waits");

        let started = Instant::now();
        let bridge = lifecycle.wait_ready(Duration::from_secs(20)).unwrap();

        assert!(started.elapsed() >= Duration::from_millis(500), "it did wait: {:?}", started.elapsed());
        assert!(!format!("{bridge:?}").contains("tok-lifecycle"), "the token is not printed");
        lifecycle.shutdown(Duration::from_millis(300), Duration::from_secs(5));
    }

    #[test]
    fn several_calls_that_wait_are_all_released_when_the_sidecar_is_ready() {
        let lifecycle = start_stand_in(format!("Start-Sleep -Seconds 1; {READY} Start-Sleep -Seconds 30"), "released");

        let waiters: Vec<_> = (0..3)
            .map(|_| {
                let lifecycle = lifecycle.clone();
                std::thread::spawn(move || lifecycle.wait_ready(Duration::from_secs(20)).is_ok())
            })
            .collect();

        assert!(waiters.into_iter().all(|waiter| waiter.join().unwrap()));
        lifecycle.shutdown(Duration::from_millis(300), Duration::from_secs(5));
    }

    #[test]
    fn a_sidecar_that_could_not_start_is_reported_with_its_reason() {
        let lifecycle = Lifecycle::start(|| Err("no runtime".to_string()));

        let error = lifecycle.wait_ready(Duration::from_secs(10)).unwrap_err();

        assert!(error.message.contains("서버를 시작하지 못했습니다"), "{error}");
        assert!(error.message.contains("no runtime"), "{error}");
    }

    #[test]
    fn a_call_gives_up_waiting_after_its_limit() {
        let lifecycle = Lifecycle::start(|| {
            std::thread::sleep(Duration::from_secs(3));
            Err("late".to_string())
        });
        let started = Instant::now();

        let error = lifecycle.wait_ready(Duration::from_millis(300)).unwrap_err();

        assert!(error.message.contains("아직 준비되지 않았습니다"), "{error}");
        assert!(started.elapsed() < Duration::from_secs(2), "{:?}", started.elapsed());
    }

    #[test]
    fn after_the_shutdown_the_sidecar_got_the_exit_line_and_calls_are_refused() {
        let result = log_path("exit-line");
        let _ = std::fs::remove_file(&result);
        let lifecycle = start_stand_in(script_that_records_the_first_stdin_line(&result, 0), "exit-line-err");
        lifecycle.wait_ready(Duration::from_secs(20)).unwrap();

        lifecycle.shutdown(Duration::from_secs(10), Duration::from_secs(5));

        assert_eq!(read_when_written(&result), "exit");
        assert_eq!(lifecycle.wait_ready(Duration::from_secs(1)).unwrap_err().message, "서버가 종료되었습니다.");
        let _ = std::fs::remove_file(&result);
    }

    #[test]
    fn closing_while_the_sidecar_is_still_starting_waits_for_it_and_ends_it_properly() {
        let result = log_path("closed-while-starting");
        let _ = std::fs::remove_file(&result);
        let lifecycle = start_stand_in(script_that_records_the_first_stdin_line(&result, 2), "closed-while-starting-err");

        lifecycle.shutdown(Duration::from_secs(10), Duration::from_secs(20));

        assert_eq!(read_when_written(&result), "exit", "it was told the user closed the app, not left to the Job Object");
        let _ = std::fs::remove_file(&result);
    }

    #[test]
    fn a_sidecar_that_comes_after_the_wait_for_it_was_given_up_is_still_ended_properly() {
        let result = log_path("late");
        let _ = std::fs::remove_file(&result);
        let lifecycle = start_stand_in(script_that_records_the_first_stdin_line(&result, 2), "late-err");

        lifecycle.shutdown(Duration::from_secs(10), Duration::from_millis(100));

        assert_eq!(lifecycle.wait_ready(Duration::from_secs(1)).unwrap_err().message, "서버가 종료되었습니다.");
        assert_eq!(read_when_written(&result), "exit");
        let _ = std::fs::remove_file(&result);
    }

    #[test]
    fn a_shutdown_that_comes_twice_does_no_harm() {
        let lifecycle = start_stand_in(format!("{READY} Start-Sleep -Seconds 30"), "twice");
        lifecycle.wait_ready(Duration::from_secs(20)).unwrap();

        lifecycle.shutdown(Duration::from_millis(300), Duration::from_secs(5));
        lifecycle.shutdown(Duration::from_millis(300), Duration::from_secs(5));
    }
}
```

`lib.rs`에 `pub mod lifecycle;`(`pub mod job;` 다음)와 `pub use lifecycle::Lifecycle;`(`pub use handshake::...` 다음)를 더한다.

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test lifecycle:: 2>&1 | Select-String "test result|FAILED"
```

Expected: `8 failed`(`not yet implemented`).

- [ ] **Step 2: 구현한다**

`lifecycle.rs`에서 뼈대(첫 줄부터 `#[cfg(all(test, windows))]` 앞까지)를 아래 구현으로 바꾼다. 테스트 모듈은 그대로 둔다:

```rust
//! Where the sidecar is in its life, shared by the thread that starts it, the calls that wait for it and the exit of the app.
//! Starting takes seconds (a JVM), so nothing waits for it on the UI thread. No lock is held while a call runs: a call gets
//! its own `Bridge` (a copy of the port and the token) and the state is free again.

use crate::bridge::Bridge;
use crate::error::BridgeError;
use crate::sidecar::Sidecar;
use std::sync::{Arc, Condvar, Mutex, MutexGuard, PoisonError};
use std::time::{Duration, Instant};

enum Phase {
    Starting,
    Ready(Sidecar),
    Failed(String),
    Stopped,
}

struct Shared {
    phase: Mutex<Phase>,
    changed: Condvar,
}

#[derive(Clone)]
pub struct Lifecycle {
    shared: Arc<Shared>,
}

impl Lifecycle {
    /// Runs [start] on a thread of its own and keeps its result.
    pub fn start(start: impl FnOnce() -> Result<Sidecar, String> + Send + 'static) -> Lifecycle {
        let lifecycle =
            Lifecycle { shared: Arc::new(Shared { phase: Mutex::new(Phase::Starting), changed: Condvar::new() }) };
        let background = lifecycle.clone();
        std::thread::spawn(move || background.finish_start(start()));
        lifecycle
    }

    fn lock(&self) -> MutexGuard<'_, Phase> {
        self.shared.phase.lock().unwrap_or_else(PoisonError::into_inner)
    }

    fn finish_start(&self, result: Result<Sidecar, String>) {
        let late = {
            let mut phase = self.lock();
            let late = match (&*phase, result) {
                (Phase::Starting, Ok(sidecar)) => {
                    *phase = Phase::Ready(sidecar);
                    None
                }
                (Phase::Starting, Err(reason)) => {
                    *phase = Phase::Failed(reason);
                    None
                }
                // The app was closed while this was starting and gave up waiting: nobody will use this sidecar.
                (_, Ok(sidecar)) => Some(sidecar),
                (_, Err(_)) => None,
            };
            self.shared.changed.notify_all();
            late
        };
        if let Some(mut sidecar) = late {
            sidecar.shutdown(Duration::from_secs(3));
        }
    }

    /// Waits up to [timeout] for the sidecar to be ready and returns the means to call it.
    pub fn wait_ready(&self, timeout: Duration) -> Result<Bridge, BridgeError> {
        let deadline = Instant::now() + timeout;
        let mut phase = self.lock();
        loop {
            match &*phase {
                Phase::Ready(sidecar) => return Ok(Bridge::new(sidecar.handshake.clone())),
                Phase::Failed(reason) => {
                    return Err(BridgeError::new(format!("서버를 시작하지 못했습니다: {reason}")));
                }
                Phase::Stopped => return Err(BridgeError::new("서버가 종료되었습니다.")),
                Phase::Starting => {}
            }
            let left = deadline.saturating_duration_since(Instant::now());
            if left.is_zero() {
                return Err(BridgeError::new("서버가 아직 준비되지 않았습니다."));
            }
            phase = self.shared.changed.wait_timeout(phase, left).unwrap_or_else(PoisonError::into_inner).0;
        }
    }

    /// Ends the sidecar the way the user asked for it (an `exit` line, then [grace] to end by itself). A sidecar that is
    /// still starting is waited for, up to [start_wait], so that it is not left to the Job Object, which kills it without
    /// an exit record and makes the next start report a crash.
    pub fn shutdown(&self, grace: Duration, start_wait: Duration) {
        let sidecar = {
            let deadline = Instant::now() + start_wait;
            let mut phase = self.lock();
            while matches!(*phase, Phase::Starting) {
                let left = deadline.saturating_duration_since(Instant::now());
                if left.is_zero() {
                    break;
                }
                phase = self.shared.changed.wait_timeout(phase, left).unwrap_or_else(PoisonError::into_inner).0;
            }
            let before = std::mem::replace(&mut *phase, Phase::Stopped);
            self.shared.changed.notify_all();
            match before {
                Phase::Ready(sidecar) => Some(sidecar),
                _ => None,
            }
        };
        if let Some(mut sidecar) = sidecar {
            sidecar.shutdown(grace);
        }
    }
}
```

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test lifecycle:: 2>&1 | Select-String "test result|FAILED|warning"
```

Expected: `test result: ok. 8 passed`(PowerShell 대역을 띄우므로 10초쯤 걸린다), 경고 없음.

- [ ] **Step 3: 크레이트 전체와 커밋**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test 2>&1 | Select-String "test result|FAILED|warning|error"; cargo clippy --all-targets 2>&1 | Select-String "warning|error"
```

Expected: `test result: ok. 77 passed`. clippy는 기존 `job.rs`의 `unnecessary_mut_passed` 한 건만 말한다(이 단계의 변경이 아니므로 그대로 둔다).

```bash
git add sidecar-host
git commit -m "feat(sidecar-host): 시작 대기와 종료를 한곳에서 맡고 시작 직후 닫아도 정상 종료시키는 Lifecycle을 더한다"
```

---

### Task 5: 실제 사이드카 이미지로 계약을 시험한다

**Files:**
- Create: `sidecar-host/tests/real_sidecar.rs`

**Interfaces:**
- Consumes: Task 2~3의 `Bridge`, 기존 `Sidecar::spawn`, `SidecarConfig::from_lookup`, 1단계의 사이드카 이미지(`server/build/sidecar-image`)
- Produces: 실제 서버의 오류 문구와 SSE `error` 이벤트, 토큰 거부, 종료 기록, 로그에 토큰이 없음을 확인하는 `#[ignore]` 테스트. 서버의 문구가 바뀌면 이 테스트가 브리지가 알게 되는 곳이다

이 테스트는 구현 뒤에 쓴다. 바꿀 수 없는 실제 서버에 대해 앞 Task들의 가정(문구, 상태 코드, SSE 바이트 형식)이 맞는지를 보는 특성화 테스트이기 때문이다. 실패하면 브리지나 가짜 서버 스크립트가 실제 서버와 다르다는 뜻이므로, 어느 쪽이 틀렸는지 확인하고 고친다.

- [ ] **Step 1: 사이드카 이미지가 있는지 확인한다**

```powershell
Test-Path C:\Projects\xGetSongs\server\build\sidecar-image\runtime\bin\java.exe
```

Expected: `True`. `False`이면 이미지를 만든다(Gradle은 `--no-daemon`):

```powershell
Set-Location C:\Projects\xGetSongs; .\gradlew.bat :server:sidecarImage --no-daemon 2>&1 | Select-Object -Last 8
```

Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 2: 테스트를 쓴다**

```rust
//! The bridge against the real sidecar: the image that `gradlew :server:sidecarImage` makes. It starts a JVM (about two
//! seconds), so it is ignored by default. Run it with
//!
//!     $env:XGS_SIDECAR_DIR = "<repo>\server\build\sidecar-image"; cargo test --test real_sidecar -- --ignored
//!
//! The expected messages are what the server says today (`server/.../Application.kt`, `engine/...`): if the server's text
//! changes, this test is where the bridge finds out.

use serde_json::{json, Value};
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};
use xgs_sidecar::{Bridge, Handshake, Sidecar, SidecarConfig};

fn work_folder() -> PathBuf {
    let folder = std::env::temp_dir().join(format!("xgs-real-sidecar-test-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&folder);
    folder
}

fn start(folder: &Path) -> Sidecar {
    let lookup = |key: &str| -> Option<OsString> {
        match key {
            "XGS_SIDECAR_DIR" => std::env::var_os("XGS_SIDECAR_DIR"),
            // Never the user's real app data: the server clears the `work` folder of the one it is given.
            "XGS_SIDECAR_APPDATA" => Some(folder.join("data").into_os_string()),
            "XGS_SIDECAR_LOGDIR" => Some(folder.join("log").into_os_string()),
            _ => None,
        }
    };
    let config = SidecarConfig::from_lookup(lookup, None).expect("set XGS_SIDECAR_DIR to server/build/sidecar-image");
    Sidecar::spawn(config.command(), &config.stderr_log(), Duration::from_secs(60)).expect("the sidecar starts")
}

fn read_when_present(path: &Path) -> String {
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        if let Ok(content) = std::fs::read_to_string(path) {
            if !content.is_empty() {
                return content;
            }
        }
        if Instant::now() > deadline {
            return String::new();
        }
        std::thread::sleep(Duration::from_millis(50));
    }
}

#[test]
#[ignore = "starts the real sidecar: set XGS_SIDECAR_DIR to server/build/sidecar-image"]
fn the_bridge_speaks_the_contract_of_the_real_server() {
    let folder = work_folder();
    let mut sidecar = start(&folder);
    let token = sidecar.handshake.token.clone();
    let bridge = Bridge::new(sidecar.handshake.clone());

    // GET /tools
    let tools = bridge.tools().expect("tools");
    for tool in ["ytDlp", "ffmpeg", "jsRuntime"] {
        assert!(tools[tool]["found"].is_boolean(), "{tool} has found: {tools}");
    }

    // POST /resolve: the messages the server gives for input it cannot use
    assert_eq!(bridge.resolve("한글").unwrap_err().message, "재생목록 ID 또는 영상 주소로 인식할 수 없습니다.");
    assert_eq!(bridge.resolve("").unwrap_err().message, "재생목록 ID 또는 영상 주소를 입력하세요.");

    // POST /jobs
    assert_eq!(
        bridge.start_job(&json!({ "resolveId": "nope" })).unwrap_err().message,
        "조회 결과가 만료되었습니다. 다시 조회하세요."
    );
    assert_eq!(bridge.start_job(&json!({})).unwrap_err().message, "요청 형식이 올바르지 않습니다.");

    // DELETE /jobs/{id} and GET /jobs/{id}/events for a job that does not exist (the SSE `error` event of the real writer)
    assert_eq!(bridge.cancel_job("nope").unwrap_err().message, "작업을 찾을 수 없습니다.");
    let mut delivered: Vec<Value> = Vec::new();
    let watched = bridge.watch_job("nope", |event| {
        delivered.push(event);
        true
    });
    assert_eq!(watched.unwrap_err().message, "작업을 찾을 수 없습니다.");
    assert!(delivered.is_empty());

    // A wrong token is refused by the server's guard, and the message does not give the right one away.
    let stranger = Bridge::new(Handshake { port: sidecar.handshake.port, token: "wrong".to_string() });
    let refused = stranger.tools().unwrap_err();
    assert_eq!(refused.message, "인증 토큰이 올바르지 않습니다.");
    assert!(!refused.message.contains(&token));
    assert!(!format!("{bridge:?}").contains(&token), "Debug output of the bridge");

    // The user closes the app: the sidecar ends on its own and leaves the same record as the Compose app does.
    assert!(sidecar.shutdown(Duration::from_secs(10)), "it ended on its own");
    let marker = read_when_present(&folder.join("log").join("last-run.txt"));
    assert!(marker.contains("state=exited"), "{marker}");
    assert!(marker.contains("reason=user"), "{marker}");

    // The token is nowhere in what the sidecar wrote.
    let mut files = 0;
    for entry in std::fs::read_dir(folder.join("log")).unwrap().flatten() {
        if entry.path().is_file() {
            files += 1;
            let content = std::fs::read(entry.path()).unwrap();
            assert!(
                !String::from_utf8_lossy(&content).contains(&token),
                "the token is in {}",
                entry.path().display()
            );
        }
    }
    assert!(files >= 2, "the log folder holds the log and the marker, not {files} files");
    let _ = std::fs::remove_dir_all(&folder);
}
```

- [ ] **Step 3: 실제 사이드카로 돌린다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; $env:XGS_SIDECAR_DIR = "C:\Projects\xGetSongs\server\build\sidecar-image"; cargo test --test real_sidecar -- --ignored 2>&1 | Select-Object -Last 12
```

Expected: `test the_bridge_speaks_the_contract_of_the_real_server ... ok`, `1 passed`(JVM을 띄우므로 3초쯤).

- [ ] **Step 4: 기본 실행에서는 건너뛰는지 확인하고 커밋한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; Remove-Item Env:XGS_SIDECAR_DIR -ErrorAction SilentlyContinue; cargo test 2>&1 | Select-String "test result|ignored"
```

Expected: 단위 테스트 `77 passed`, 통합 테스트 `0 passed; 0 failed; 1 ignored`.

```bash
git add sidecar-host
git commit -m "test(sidecar-host): 실제 사이드카 이미지로 브리지 계약을 시험한다"
```

---

### Task 6: 화면이 쓰는 호출 래퍼와 서버 모델 타입 (TypeScript)

**Files:**
- Create: `shell/src/bridge/types.ts`, `shell/src/bridge/bridge.ts`, `shell/src/bridge/tauri.ts`, `shell/tests/bridge.test.ts`

**Interfaces:**
- Consumes: Task 7이 등록할 Tauri 명령 이름과 인자: `sidecar_ready`, `tools`, `install_yt_dlp`, `update_yt_dlp`, `resolve {input}`, `start_job {request}`, `cancel_job {jobId}`, `watch_job {jobId, onEvent: Channel}` (Tauri가 JS의 `camelCase` 인자를 Rust의 `snake_case`로 바꾼다)
- Produces (3단계 프런트엔드 이식과 Task 8의 점검 페이지가 쓴다):
  - `types.ts`: `ToolsStatus`, `ResolveResponse`, `ResolvedItem`, `JobRequest`, `JobOptions`, `JobCreated`, `JobEvent`(판별 유니온, `type` 필드), `ActionResult` 등 `ApiModels.kt`의 거울
  - `bridge.ts`: `createBridge({invoke, channel, lastEventWaitMs?})` → `{ready, tools, installYtDlp, updateYtDlp, resolve, startJob, cancelJob, watchJob}`, `errorMessage(reason: unknown): string`, `BridgeError {message}`
  - `tauri.ts`: `bridge` — 실제 `invoke`와 `Channel`로 만든 인스턴스

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`shell/tests/bridge.test.ts`:

```typescript
import assert from "node:assert/strict";
import { test } from "node:test";
import { createBridge, errorMessage, type ChannelLike } from "../src/bridge/bridge.ts";
import type { JobEvent } from "../src/bridge/types.ts";

interface Call {
  command: string;
  args?: Record<string, unknown>;
}

/** A bridge over a fake `invoke`: every call is recorded, and [answer] says how it ends. */
function fakeBridge(answer: (call: Call) => Promise<unknown>, lastEventWaitMs = 200) {
  const calls: Call[] = [];
  const channels: ChannelLike<JobEvent>[] = [];
  const bridge = createBridge({
    invoke: ((command: string, args?: Record<string, unknown>) => {
      const call = { command, args };
      calls.push(call);
      return answer(call);
    }) as never,
    channel: (<T>() => {
      const channel: ChannelLike<T> = { onmessage: () => {} };
      channels.push(channel as unknown as ChannelLike<JobEvent>);
      return channel;
    }) as never,
    lastEventWaitMs,
  });
  return { bridge, calls, channels };
}

const progress = (percent: number): JobEvent => ({ type: "progress", rank: 1, stage: "DOWNLOADING", percent });
const jobDone: JobEvent = { type: "job-done", status: "COMPLETED", summary: { succeeded: 1, skipped: 0, failed: 0 } };
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

test("each call is the command of the Rust bridge with its arguments", async () => {
  const { bridge, calls } = fakeBridge(async () => ({}));

  await bridge.ready();
  await bridge.tools();
  await bridge.installYtDlp();
  await bridge.updateYtDlp();
  await bridge.resolve("PLabc 한글");
  await bridge.startJob({ resolveId: "r1", options: { albumName: "앨범" }, ranks: [1, 3] });
  await bridge.cancelJob("j-1");

  assert.deepEqual(calls, [
    { command: "sidecar_ready", args: undefined },
    { command: "tools", args: undefined },
    { command: "install_yt_dlp", args: undefined },
    { command: "update_yt_dlp", args: undefined },
    { command: "resolve", args: { input: "PLabc 한글" } },
    { command: "start_job", args: { request: { resolveId: "r1", options: { albumName: "앨범" }, ranks: [1, 3] } } },
    { command: "cancel_job", args: { jobId: "j-1" } },
  ]);
});

test("watchJob gives the events to the listener in order and resolves after job-done", async () => {
  const { bridge, calls, channels } = fakeBridge(async () => {
    channels[0].onmessage(progress(10));
    channels[0].onmessage(progress(20));
    channels[0].onmessage(jobDone);
  });
  const seen: JobEvent[] = [];

  await bridge.watchJob("j-1", (event) => seen.push(event));

  assert.deepEqual(seen, [progress(10), progress(20), jobDone]);
  assert.equal(calls[0].command, "watch_job");
  assert.equal(calls[0].args?.jobId, "j-1");
  assert.equal(calls[0].args?.onEvent, channels[0], "the channel is what the Rust side sends to");
});

test("watchJob waits for job-done when the command ended before the page handled it", async () => {
  const { bridge, channels } = fakeBridge(async () => {
    setTimeout(() => channels[0].onmessage(jobDone), 50);
  });
  const seen: JobEvent[] = [];

  await bridge.watchJob("j-1", (event) => seen.push(event));

  assert.deepEqual(seen, [jobDone]);
});

test("watchJob rejects when the command ended and job-done never reached the page", async () => {
  const { bridge } = fakeBridge(async () => {}, 100);

  await assert.rejects(bridge.watchJob("j-1", () => {}), { message: "이벤트를 모두 받지 못했습니다." });
});

test("watchJob rejects with the message of the failure, without waiting for the last event", async () => {
  const { bridge } = fakeBridge(async () => {
    throw { message: "작업을 찾을 수 없습니다." };
  }, 5000);
  const started = Date.now();

  await assert.rejects(bridge.watchJob("nope", () => {}), { message: "작업을 찾을 수 없습니다." });

  assert.ok(Date.now() - started < 1000);
});

test("a late job-done after the wait was given up does no harm", async () => {
  const { bridge, channels } = fakeBridge(async () => {}, 50);
  await assert.rejects(bridge.watchJob("j-1", () => {}));

  channels[0].onmessage(jobDone);
  await sleep(20);
});

test("errorMessage reads a BridgeError, an Error and a string", () => {
  assert.equal(errorMessage({ message: "서버와의 연결이 끊어졌습니다." }), "서버와의 연결이 끊어졌습니다.");
  assert.equal(errorMessage(new Error("boom")), "boom");
  assert.equal(errorMessage("plain"), "plain");
  assert.equal(errorMessage(42), "42");
  assert.equal(errorMessage(null), "null");
});
```

- [ ] **Step 2: 실패를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test "tests/*.test.ts" 2>&1 | Select-Object -Last 20
```

Expected: `bridge.test.ts`가 `ERR_MODULE_NOT_FOUND`(`../src/bridge/bridge.ts`)로 실패. 기존 테스트 파일들은 통과.

- [ ] **Step 3: 타입과 래퍼를 쓴다**

`shell/src/bridge/types.ts`:

```typescript
// What the sidecar's server says, as the bridge passes it on. It mirrors
// shared/src/commonMain/kotlin/com/xgetsongs/shared/api/ApiModels.kt: the server writes every field, null included
// (`encodeDefaults = true`), and a request may leave out the fields that have a default.

export type InputKind = "PLAYLIST" | "VIDEO";
export type Stage = "DOWNLOADING" | "FINISHING";
export type JobStatus = "COMPLETED" | "CANCELLED" | "FAILED";
export type LyricsOutcome = "DESCRIPTION" | "ONLINE" | "NOT_FOUND" | "SEARCH_OFF";

export interface ToolInfo {
  found: boolean;
  version: string | null;
  path: string | null;
}

export interface ToolsStatus {
  ytDlp: ToolInfo;
  ffmpeg: ToolInfo;
  jsRuntime: ToolInfo;
}

export interface ActionResult {
  message: string;
}

export interface ResolvedItem {
  rank: number;
  videoId: string;
  title: string;
  channel: string | null;
  artist: string;
  track: string;
  lowConfidence: boolean;
  available: boolean;
  unavailableReason: string | null;
  expectedFileName: string | null;
}

export interface ResolveResponse {
  resolveId: string;
  kind: InputKind;
  playlistTitle: string | null;
  items: ResolvedItem[];
  truncated: boolean;
  alsoVideoId: string | null;
}

export interface JobOptions {
  outputDir: string | null;
  overwrite: boolean;
  singleRank: number;
  includeRank: boolean;
  searchLyricsOnline: boolean;
  albumName: string | null;
}

export interface JobRequest {
  resolveId: string;
  options?: Partial<JobOptions>;
  /** Only these ranks (to retry the failed ones); absent or null means every available item. */
  ranks?: number[] | null;
}

export interface JobCreated {
  jobId: string;
}

export interface JobSummary {
  succeeded: number;
  skipped: number;
  failed: number;
}

/** An event of a job. `type` is the SSE event name and the serial name of the Kotlin class. */
export type JobEvent =
  | { type: "item-started"; rank: number; videoId: string; fileName: string }
  | { type: "progress"; rank: number; stage: Stage; percent: number | null }
  | { type: "item-done"; rank: number; fileName: string; lyrics: LyricsOutcome | null }
  | { type: "item-skipped"; rank: number; reason: string }
  | { type: "item-failed"; rank: number; message: string }
  | { type: "job-done"; status: JobStatus; summary: JobSummary };
```

`shell/src/bridge/bridge.ts`:

```typescript
import type {
  ActionResult,
  JobCreated,
  JobEvent,
  JobRequest,
  ResolveResponse,
  ToolsStatus,
} from "./types.ts";

/** What a rejected bridge call carries: one message fit for the user, as the Rust side wrote it. */
export interface BridgeError {
  message: string;
}

/** The message of whatever a call rejected with: a `BridgeError`, an `Error`, or a plain string. */
export function errorMessage(reason: unknown): string {
  if (typeof reason === "string") return reason;
  if (typeof reason === "object" && reason !== null && "message" in reason) {
    const { message } = reason as { message: unknown };
    if (typeof message === "string") return message;
  }
  return String(reason);
}

/** `invoke` of `@tauri-apps/api/core`, or a stand-in in a test. */
export type Invoke = <T>(command: string, args?: Record<string, unknown>) => Promise<T>;

/** The part of a Tauri `Channel` the bridge uses. */
export interface ChannelLike<T> {
  onmessage: (message: T) => void;
}

export interface BridgeDeps {
  invoke: Invoke;
  channel: <T>() => ChannelLike<T>;
  /** How long `watchJob` waits for `job-done` to be delivered after the command has ended (default 3000). */
  lastEventWaitMs?: number;
}

const LAST_EVENT_WAIT_MS = 3000;
const MISSING_LAST_EVENT = "이벤트를 모두 받지 못했습니다.";

/**
 * The calls the page can make to the server. Each one is a Tauri command of the Rust bridge, which holds the port and the
 * token: nothing here, and nothing the commands return, tells the page where the server is or how to get in.
 */
export function createBridge(deps: BridgeDeps) {
  const { invoke } = deps;
  return {
    /** Resolves when the sidecar is ready; rejects with the reason when it could not start. */
    ready: () => invoke<void>("sidecar_ready"),
    tools: () => invoke<ToolsStatus>("tools"),
    installYtDlp: () => invoke<ActionResult>("install_yt_dlp"),
    updateYtDlp: () => invoke<ActionResult>("update_yt_dlp"),
    resolve: (input: string) => invoke<ResolveResponse>("resolve", { input }),
    startJob: (request: JobRequest) => invoke<JobCreated>("start_job", { request }),
    cancelJob: (jobId: string) => invoke<void>("cancel_job", { jobId }),

    /**
     * Calls [onEvent] with each event of the job, in order, and resolves after `job-done` has been delivered. Rejects with the
     * server's message when it sends an `error` event, and when the connection is lost before `job-done`.
     */
    watchJob(jobId: string, onEvent: (event: JobEvent) => void): Promise<void> {
      const channel = deps.channel<JobEvent>();
      let delivered: () => void = () => {};
      const jobDone = new Promise<void>((resolve) => {
        delivered = resolve;
      });
      channel.onmessage = (event) => {
        onEvent(event);
        if (event.type === "job-done") delivered();
      };
      // The command ends after the last event was sent, but the page may still be handling it: wait for `job-done`.
      return invoke<void>("watch_job", { jobId, onEvent: channel }).then(() =>
        within(jobDone, deps.lastEventWaitMs ?? LAST_EVENT_WAIT_MS),
      );
    },
  };
}

export type Bridge = ReturnType<typeof createBridge>;

function within(promise: Promise<void>, ms: number): Promise<void> {
  return new Promise<void>((resolve, reject) => {
    const timer = setTimeout(() => reject({ message: MISSING_LAST_EVENT } satisfies BridgeError), ms);
    promise.then(
      () => {
        clearTimeout(timer);
        resolve();
      },
      (reason) => {
        clearTimeout(timer);
        reject(reason);
      },
    );
  });
}
```

`shell/src/bridge/tauri.ts`:

```typescript
import { Channel, invoke } from "@tauri-apps/api/core";
import { createBridge } from "./bridge.ts";

/** The bridge of this window: the Tauri commands of the Rust side. */
export const bridge = createBridge({ invoke, channel: <T>() => new Channel<T>() });
```

- [ ] **Step 4: 통과를 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; node --test "tests/*.test.ts" 2>&1 | Select-Object -Last 14; npx tsc --noEmit; "tsc exit $LASTEXITCODE"
```

Expected: `pass 18`(기존 11 + 새 7), `fail 0`, `tsc exit 0`.

- [ ] **Step 5: Commit**

```bash
git add shell/src/bridge shell/tests/bridge.test.ts
git commit -m "feat(shell): 화면이 쓰는 브리지 래퍼와 서버 모델 타입을 더한다"
```

---

### Task 7: Tauri 명령 8개로 스파이크를 바꾸고 점검 페이지를 만든다

**Files:**
- Replace: `shell/src-tauri/src/lib.rs`
- Delete: `sidecar-host/src/http.rs`, `shell/spike.html`, `shell/src/spike.ts`, `shell/spike.conf.json`
- Modify: `sidecar-host/src/lib.rs`, `shell/vite.config.ts`
- Create: `shell/check.html`, `shell/src/check.ts`, `shell/check.conf.json`

**Interfaces:**
- Consumes: Task 2~4의 `Bridge`, `BridgeError`, `Lifecycle`, 기존 `Sidecar`, `SidecarConfig`, `job`; Task 6의 `bridge`, `errorMessage`
- Produces: 명령 `sidecar_ready`, `tools`, `install_yt_dlp`, `update_yt_dlp`, `resolve(input)`, `start_job(request)`, `cancel_job(jobId)`, `watch_job(jobId, onEvent)`와 점검용 `check_record(key, value)`, `check_ping()`, `check_burst(count, onEvent)`. 점검 페이지 `check.html`(창 URL을 `check.conf.json`으로 바꿔 연다)

이 Task의 Rust 코드(명령 껍데기)는 단위 테스트가 없다. 로직은 모두 Task 1~4의 테스트가 지키고 껍데기는 컴파일러와 Task 8의 종단 확인이 지킨다.

- [ ] **Step 1: 스파이크를 걷어낸다**

```bash
git rm sidecar-host/src/http.rs shell/spike.html shell/src/spike.ts shell/spike.conf.json
```

`sidecar-host/src/lib.rs`에서 `pub mod http;`와 `pub use http::http_get;` 두 줄을 지운다. 최종 모양은:

```rust
pub mod bridge;
pub mod config;
pub mod error;
pub mod handshake;
pub mod job;
pub mod lifecycle;
pub mod sidecar;
pub mod sse;

#[cfg(test)]
mod fake_server;

pub use bridge::Bridge;
pub use config::SidecarConfig;
pub use error::BridgeError;
pub use handshake::{parse_handshake, Handshake};
pub use lifecycle::Lifecycle;
pub use sidecar::Sidecar;
```

- [ ] **Step 2: 점검 페이지 파일을 만든다**

`shell/check.html`:

```html
<!doctype html>
<html lang="ko">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1.0" />
    <title>xGetSongs 브리지 점검</title>
    <script type="module" src="/src/check.ts" defer></script>
  </head>
  <body>
    <main>
      <h1>브리지 점검</h1>
      <p id="status">사이드카를 기다리는 중…</p>
      <pre id="out"></pre>
    </main>
  </body>
</html>
```

`shell/check.conf.json`:

```json
{
  "app": {
    "windows": [
      {
        "title": "xGetSongs 브리지 점검",
        "width": 1000,
        "height": 760,
        "url": "check.html"
      }
    ]
  }
}
```

`shell/src/check.ts`:

```typescript
// A developer page, not part of the product: it runs every command of the bridge against the real sidecar and reports what the
// page saw to the file named by XGS_BRIDGE_CHECK_FILE (see the plan 2026-10-11-tauri-rust-bridge.md). Open it with
// `--config check.conf.json`.
import { Channel, invoke } from "@tauri-apps/api/core";
import { errorMessage } from "./bridge/bridge.ts";
import { bridge } from "./bridge/tauri.ts";

const status = document.querySelector<HTMLParagraphElement>("#status")!;
const out = document.querySelector<HTMLPreElement>("#out")!;

const pageStart = performance.now();
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));
const lines: string[] = [];

/** Shows the result on the page and writes it to the check file. */
async function record(key: string, value: unknown) {
  const text = typeof value === "string" ? value : JSON.stringify(value);
  lines.push(`${key}=${text}`);
  out.textContent = lines.join("\n");
  await invoke("check_record", { key, value: text });
}

/** What a call rejected with, as the message the page would show. */
async function rejection(run: () => Promise<unknown>): Promise<string> {
  try {
    await run();
    return "(no error)";
  } catch (reason) {
    return errorMessage(reason);
  }
}

async function main() {
  // While the sidecar starts, the window must stay alive: count the frames, and time a command that needs no sidecar.
  let worstFrameGapMs = 0;
  let lastFrame = performance.now();
  let framing = true;
  const onFrame = (now: number) => {
    worstFrameGapMs = Math.max(worstFrameGapMs, now - lastFrame);
    lastFrame = now;
    if (framing) requestAnimationFrame(onFrame);
  };
  requestAnimationFrame(onFrame);
  let worstPingMs = 0;
  let pings = 0;
  const pinger = setInterval(async () => {
    const sent = performance.now();
    await invoke("check_ping");
    worstPingMs = Math.max(worstPingMs, performance.now() - sent);
    pings += 1;
  }, 50);

  await bridge.ready();
  const waitedMs = Math.round(performance.now() - pageStart);
  clearInterval(pinger);
  framing = false;
  await record("waitedMs", waitedMs);
  await record("worstFrameGapMs", Math.round(worstFrameGapMs));
  await record("worstPingMs", Math.round(worstPingMs));
  await record("pings", pings);
  status.textContent = "사이드카 준비됨";

  const tools = await bridge.tools();
  await record("tools", Object.keys(tools).join(","));
  await record("toolsFound", `${typeof tools.ytDlp.found},${typeof tools.ffmpeg.found},${typeof tools.jsRuntime.found}`);

  await record("resolveEmpty", await rejection(() => bridge.resolve("")));
  await record("resolveUnknown", await rejection(() => bridge.resolve("한글")));
  await record("startJobExpired", await rejection(() => bridge.startJob({ resolveId: "nope" })));
  await record("cancelUnknown", await rejection(() => bridge.cancelJob("nope")));
  await record("cancelBadId", await rejection(() => bridge.cancelJob("../tools")));
  let eventsOfUnknownJob = 0;
  await record(
    "watchUnknown",
    await rejection(() => bridge.watchJob("nope", () => (eventsOfUnknownJob += 1))),
  );
  await record("watchUnknownEvents", eventsOfUnknownJob);

  // The event path of the web view without a download: 5,000 numbered events, as fast as Rust can send them.
  const total = 5000;
  const channel = new Channel<{ seq: number }>();
  let received = 0;
  let outOfOrder = 0;
  let expected = 0;
  channel.onmessage = ({ seq }) => {
    if (seq !== expected) outOfOrder += 1;
    expected = seq + 1;
    received += 1;
  };
  const burstStart = performance.now();
  await invoke("check_burst", { count: total, onEvent: channel });
  const commandEndedMs = Math.round(performance.now() - burstStart);
  const receivedWhenTheCommandEnded = received;
  await sleep(500);
  await record("burstCommandMs", commandEndedMs);
  await record("burstReceivedWhenTheCommandEnded", receivedWhenTheCommandEnded);
  await record("burstReceived", received);
  await record("burstOutOfOrder", outOfOrder);

  await record("done", 1);
}

main().catch(async (reason) => {
  status.textContent = `실패: ${errorMessage(reason)}`;
  await invoke("check_record", { key: "error", value: errorMessage(reason) }).catch(() => {});
});
```

`shell/vite.config.ts`의 `rollupOptions.input`에서 `spike:` 줄을 아래로 바꾸고 위쪽 주석도 맞춘다:

```typescript
        check: fileURLToPath(new URL("./check.html", import.meta.url)),
```

주석은 `// Two pages: the prototype (index.html) and the bridge check page (check.html).`

- [ ] **Step 3: Tauri 명령으로 `lib.rs`를 교체한다**

`shell/src-tauri/src/lib.rs`를 아래로 통째로 바꾼다:

```rust
use serde_json::{json, Value};
use std::io::Write;
use std::time::Duration;
use tauri::ipc::Channel;
use tauri::{Manager, RunEvent, State};
use xgs_sidecar::{job, Bridge, BridgeError, Lifecycle, Sidecar, SidecarConfig};

/// How long a call waits for a sidecar that is still starting. Starting itself gives up after [SIDECAR_START_LIMIT].
const READY_WAIT: Duration = Duration::from_secs(90);
const SIDECAR_START_LIMIT: Duration = Duration::from_secs(60);
/// When the app closes: how long the sidecar gets to end by itself, and how long one that is still starting is waited for.
const STOP_GRACE: Duration = Duration::from_secs(3);
const STOP_START_WAIT: Duration = Duration::from_secs(10);

/// Runs [call] once the sidecar is ready, on a thread meant for waiting: a call lasts as long as the sidecar works (a
/// resolve takes as long as yt-dlp does), and the thread of the window must stay free.
async fn with_bridge<T, F>(lifecycle: &Lifecycle, call: F) -> Result<T, BridgeError>
where
    T: Send + 'static,
    F: FnOnce(Bridge) -> Result<T, BridgeError> + Send + 'static,
{
    let lifecycle = lifecycle.clone();
    tauri::async_runtime::spawn_blocking(move || call(lifecycle.wait_ready(READY_WAIT)?))
        .await
        .map_err(|error| BridgeError::new(format!("내부 오류: {error}")))?
}

/// Resolves when the sidecar is ready, so the page can show "starting" until then.
#[tauri::command]
async fn sidecar_ready(lifecycle: State<'_, Lifecycle>) -> Result<(), BridgeError> {
    with_bridge(&lifecycle, |_| Ok(())).await
}

#[tauri::command]
async fn tools(lifecycle: State<'_, Lifecycle>) -> Result<Value, BridgeError> {
    with_bridge(&lifecycle, |bridge| bridge.tools()).await
}

#[tauri::command]
async fn install_yt_dlp(lifecycle: State<'_, Lifecycle>) -> Result<Value, BridgeError> {
    with_bridge(&lifecycle, |bridge| bridge.install_yt_dlp()).await
}

#[tauri::command]
async fn update_yt_dlp(lifecycle: State<'_, Lifecycle>) -> Result<Value, BridgeError> {
    with_bridge(&lifecycle, |bridge| bridge.update_yt_dlp()).await
}

#[tauri::command]
async fn resolve(lifecycle: State<'_, Lifecycle>, input: String) -> Result<Value, BridgeError> {
    with_bridge(&lifecycle, move |bridge| bridge.resolve(&input)).await
}

#[tauri::command]
async fn start_job(lifecycle: State<'_, Lifecycle>, request: Value) -> Result<Value, BridgeError> {
    with_bridge(&lifecycle, move |bridge| bridge.start_job(&request)).await
}

#[tauri::command]
async fn cancel_job(lifecycle: State<'_, Lifecycle>, job_id: String) -> Result<(), BridgeError> {
    with_bridge(&lifecycle, move |bridge| bridge.cancel_job(&job_id)).await
}

/// Sends the events of the job to [on_event] as they come and resolves after `job-done`. It rejects when the stream fails
/// (the server's `error` event, a cut connection) or when the page that listens is gone.
#[tauri::command]
async fn watch_job(
    lifecycle: State<'_, Lifecycle>,
    job_id: String,
    on_event: Channel<Value>,
) -> Result<(), BridgeError> {
    with_bridge(&lifecycle, move |bridge| bridge.watch_job(&job_id, |event| on_event.send(event).is_ok())).await
}

/// Check mode is on when the check script names a file in `XGS_BRIDGE_CHECK_FILE`; a normal run has no such variable and
/// the two commands below refuse.
fn check_file() -> Result<std::path::PathBuf, String> {
    std::env::var_os("XGS_BRIDGE_CHECK_FILE").map(Into::into).ok_or_else(|| "not in check mode".to_string())
}

/// The check page (`check.html`) reports what it saw as one `key=value` line of the check file.
#[tauri::command]
fn check_record(key: String, value: String) -> Result<(), String> {
    let mut file = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(check_file()?)
        .map_err(|error| error.to_string())?;
    writeln!(file, "{key}={}", value.replace(['\r', '\n'], " ")).map_err(|error| error.to_string())
}

/// Does nothing. The check page times how long a round trip to Rust takes while the sidecar starts: a window whose thread is
/// busy answers late.
#[tauri::command]
fn check_ping() {}

/// Sends [count] numbered progress events to [on_event] as fast as it can, to check the event path of the webview (order,
/// loss, speed) without a download.
#[tauri::command]
async fn check_burst(count: u32, on_event: Channel<Value>) -> Result<(), String> {
    check_file()?;
    tauri::async_runtime::spawn_blocking(move || {
        for seq in 0..count {
            let event = json!({"type":"progress","rank":1,"stage":"DOWNLOADING","percent":f64::from(seq % 100),"seq":seq});
            on_event.send(event).map_err(|error| error.to_string())?;
        }
        Ok(())
    })
    .await
    .map_err(|error| error.to_string())?
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    // First of all: whatever happens to this process, the sidecar and its children go with it. (Whether this worked is not
    // recorded anywhere yet; the sidecar also ends by itself when its stdin closes.)
    let _ = job::kill_children_when_this_process_ends();

    let config = SidecarConfig::from_env();
    let lifecycle = Lifecycle::start(move || {
        let config = config?;
        Sidecar::spawn(config.command(), &config.stderr_log(), SIDECAR_START_LIMIT)
    });

    let app = tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(lifecycle)
        .invoke_handler(tauri::generate_handler![
            sidecar_ready,
            tools,
            install_yt_dlp,
            update_yt_dlp,
            resolve,
            start_job,
            cancel_job,
            watch_job,
            check_record,
            check_ping,
            check_burst
        ])
        .build(tauri::generate_context!())
        .expect("error while building tauri application");

    app.run(|handle, event| {
        if let RunEvent::Exit = event {
            handle.state::<Lifecycle>().shutdown(STOP_GRACE, STOP_START_WAIT);
        }
    });
}
```

- [ ] **Step 4: Rust 쪽을 컴파일하고 시험한다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test 2>&1 | Select-String "test result|FAILED|warning|error"
```

Expected: 단위 `75 passed`(http의 테스트 2개가 빠졌다), 통합 `1 ignored`.

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\shell\src-tauri; cargo check 2>&1 | Select-Object -Last 8; cargo clippy 2>&1 | Select-String "warning|error"
```

Expected: `Finished`(처음에는 의존성을 컴파일하느라 몇 분 걸린다), clippy 경고 없음.

- [ ] **Step 5: 화면 쪽을 컴파일하고 시험한다**

```powershell
Set-Location C:\Projects\xGetSongs\shell; npx tsc --noEmit; "tsc exit $LASTEXITCODE"; node --test "tests/*.test.ts" 2>&1 | Select-String "^ℹ (pass|fail)"; npm run build 2>&1 | Select-Object -Last 8
```

Expected: `tsc exit 0`, `ℹ pass 18`, `ℹ fail 0`, vite 빌드 성공(`dist/check.html`과 `dist/index.html`이 보인다).

- [ ] **Step 6: 스파이크의 흔적이 없는지 확인한다**

```powershell
Set-Location C:\Projects\xGetSongs; Select-String -Path shell\src-tauri\src\*.rs, sidecar-host\src\*.rs, shell\src\*.ts, shell\src\bridge\*.ts, shell\vite.config.ts -Pattern "Probe|sidecar_probe|http_get|spike_record|spike-ready|spike-failed|spike-job|SpikeFolder|spike\.html|spike\.ts" | Select-Object -First 10; Get-ChildItem shell -Filter "spike*" | Select-Object Name
```

Expected: 출력 없음. (`sidecar-host/src/config.rs`의 기본 앱 데이터 폴더 이름 `xgs-spike-appdata`와 몇몇 주석의 "spike"라는 말은 일부러 그대로 둔다. 이름만의 문제이고 1단계의 테스트와 보고서가 이 이름을 쓴다.)

- [ ] **Step 7: 토큰이 명령 시그니처에 없는지 확인한다 (B6의 소스 쪽 증거)**

```powershell
Set-Location C:\Projects\xGetSongs; Select-String -Path shell\src-tauri\src\lib.rs -Pattern "token" -CaseSensitive:$false
```

Expected: 출력 없음 — 셸의 Rust 코드는 `token`이라는 말을 한 번도 쓰지 않는다(토큰은 `sidecar-host`의 `Bridge` 안에만 있다). 출력이 있으면 어떤 줄인지 읽고 화면으로 새는 길인지 판단해 없앤다.

- [ ] **Step 8: Commit**

```bash
git add -A shell sidecar-host
git commit -m "feat(shell): 스파이크를 브리지 명령 8개로 바꾸고 토큰을 화면에서 없앤다"
```

---

### Task 8: 셸에서 실제 사이드카로 종단 확인하고 보고서를 쓴다

**Files:**
- Create: `docs/superpowers/specs/2026-10-11-tauri-rust-bridge-report.md`
- Modify: `docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md`(§5 표의 2단계 행, §4 표의 `sidecar-host` 행)

이 Task는 코드를 바꾸지 않고 측정한다. 확인 스크립트는 저장소 밖(`%TEMP%\xgs-bridge-check`)에 두고 커밋하지 않는다. 지금 열려 있는 사용자의 앱이나 Gradle 데몬은 건드리지 않는다.

- [ ] **Step 1: 확인용 도우미를 만들고 점검용 셸을 빌드한다**

`%TEMP%\xgs-bridge-check\helpers.ps1`을 아래 내용으로 만든다(`New-Item -ItemType Directory -Force "$env:TEMP\xgs-bridge-check"` 다음 Write 도구로):

```powershell
# Dot-sourced by the steps that run the check build of the shell. $ShellExe can be set before the dot-source.
Remove-Item Env:XGS_SIDECAR_LIB, Env:XGS_SIDECAR_JAVA -ErrorAction SilentlyContinue
if (-not $ShellExe) { $ShellExe = "C:\Projects\xGetSongs\shell\src-tauri\target\release\shell.exe" }
$Image = "C:\Projects\xGetSongs\server\build\sidecar-image"

function Get-Sidecar {
    Get-CimInstance Win32_Process -Filter "Name='java.exe'" | Where-Object { $_.CommandLine -like '*SidecarMainKt*' }
}

# A fresh app data folder, log folder and record file: never the user's real ones.
function New-CheckRun {
    $base = Join-Path $env:TEMP ("xgs-bridge-check-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
    New-Item -ItemType Directory $base | Out-Null
    [pscustomobject]@{ Base = $base; AppData = "$base\data"; LogDir = "$base\log"; Record = "$base\check-record.txt" }
}

function Start-CheckShell($run) {
    $env:XGS_SIDECAR_DIR = $Image
    $env:XGS_SIDECAR_APPDATA = $run.AppData
    $env:XGS_SIDECAR_LOGDIR = $run.LogDir
    $env:XGS_BRIDGE_CHECK_FILE = $run.Record
    Start-Process -FilePath $ShellExe -PassThru
}

# The lines the check page wrote, as a hashtable (a key that was written twice keeps its last value).
function Read-CheckRecord($run) {
    $values = [ordered]@{}
    if (Test-Path $run.Record) {
        foreach ($line in Get-Content $run.Record -Encoding UTF8) {
            $key, $value = $line -split '=', 2
            $values[$key] = $value
        }
    }
    $values
}

# Waits until the page wrote done= or error=, at most $Seconds.
function Wait-CheckRecord($run, [int]$Seconds = 90) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do {
        Start-Sleep -Milliseconds 200
        $record = Read-CheckRecord $run
    } while ((Get-Date) -lt $deadline -and -not ($record.Contains("done") -or $record.Contains("error")))
    $record
}

# Waits until the shell has its real window (one with a title; a handle found earlier belongs to a window that cannot be
# closed), and returns how many ms that took.
function Wait-CheckWindow($process, [int]$Seconds = 10) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while (-not $process.MainWindowTitle -and $watch.Elapsed.TotalSeconds -lt $Seconds) { Start-Sleep -Milliseconds 5; $process.Refresh() }
    [int]$watch.ElapsedMilliseconds
}

# Closes the window and returns how many ms it took until the shell and the sidecar were both gone (null: after 20 s).
function Close-CheckShell($process) {
    [void](Wait-CheckWindow $process)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    [void]$process.CloseMainWindow()
    while ($watch.Elapsed.TotalSeconds -lt 20 -and ((Get-Sidecar) -or -not $process.HasExited)) { Start-Sleep -Milliseconds 50 }
    if ((Get-Sidecar) -or -not $process.HasExited) { return $null }
    [int]$watch.Elapsed.TotalMilliseconds
}
```

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\shell; npm run tauri build -- --no-bundle --config check.conf.json 2>&1 | Select-Object -Last 6; Get-Item src-tauri\target\release\shell.exe | Select-Object Length, LastWriteTime
```

Expected: 빌드 성공, `shell.exe`가 방금 시각(LTO 때문에 5분 안팎).

- [ ] **Step 2: 주된 흐름을 3번 돌린다 (B3, B4, B5, 악화 한도)**

```powershell
. "$env:TEMP\xgs-bridge-check\helpers.ps1"
$expected = [ordered]@{
    tools = 'ffmpeg,jsRuntime,ytDlp'   # serde_json keeps object keys sorted, not in the server's order
    toolsFound = 'boolean,boolean,boolean'
    resolveEmpty = '재생목록 ID 또는 영상 주소를 입력하세요.'
    resolveUnknown = '재생목록 ID 또는 영상 주소로 인식할 수 없습니다.'
    startJobExpired = '조회 결과가 만료되었습니다. 다시 조회하세요.'
    cancelUnknown = '작업을 찾을 수 없습니다.'
    cancelBadId = '작업 번호가 올바르지 않습니다.'
    watchUnknown = '작업을 찾을 수 없습니다.'
    watchUnknownEvents = '0'
    burstReceived = '5000'
    burstOutOfOrder = '0'
    done = '1'
}
$runs = foreach ($n in 1..3) {
    $run = New-CheckRun
    $process = Start-CheckShell $run
    $record = Wait-CheckRecord $run 90
    $closedMs = Close-CheckShell $process
    [pscustomobject]@{ N = $n; Run = $run; Record = $record; ClosedMs = $closedMs }
}
foreach ($r in $runs) {
    "--- run $($r.N): closedMs=$($r.ClosedMs)"
    foreach ($key in $expected.Keys) {
        $actual = $r.Record[$key]
        "{0,-6} {1,-22} {2}" -f $(if ($actual -eq $expected[$key]) { 'PASS' } else { 'FAIL' }), $key, $actual
    }
    "waitedMs=$($r.Record['waitedMs']) worstPingMs=$($r.Record['worstPingMs']) worstFrameGapMs=$($r.Record['worstFrameGapMs']) pings=$($r.Record['pings'])"
    "burstCommandMs=$($r.Record['burstCommandMs']) receivedWhenTheCommandEnded=$($r.Record['burstReceivedWhenTheCommandEnded'])"
}
$runs | ForEach-Object { $_.Run.Base } | Set-Content "$env:TEMP\xgs-bridge-check\runs.txt"
```

Expected: 3번 모두 모든 키가 `PASS`, `closedMs`가 숫자(3000 이하)다. `waitedMs` 3회 중앙값 10,000 이하(악화 한도), `worstPingMs`와 `worstFrameGapMs` 250 이하(B4), `burstReceived=5000`·`burstOutOfOrder=0`·`burstCommandMs` 5000 이하(B5)를 표로 기록한다. `burstReceivedWhenTheCommandEnded`가 5000보다 작으면 "명령이 끝난 뒤에도 마지막 이벤트가 아직 처리 중일 수 있다"는 증거이고, `watchJob`이 `job-done`을 기다리는 설계가 필요했다는 뜻이므로 보고서에 그대로 적는다.

계획을 쓰며 같은 코드로 만든 스크래치 빌드에서 이 단계를 한 번 돌려 본 값(참고용이다. 기대값이 아니라 이 PC에서 이 정도가 나온다는 기록이다): `waitedMs` 1,016 / 1,016 / 1,038, `worstPingMs` 6 / 19 / 5, `worstFrameGapMs` 123 / 72 / 89, `burstCommandMs` 283 / 307 / 280, `burstReceivedWhenTheCommandEnded` 5000(세 번 모두), 창 닫기 → 정리 942 / 966 / 967ms. `waitedMs`가 1단계의 1.95초보다 짧은 것은 사이드카를 창을 만들기 전에(`run()`의 맨 앞에서) 띄우기 때문이다.

하나라도 `FAIL`이면 그 키의 기록(`$r.Record`)을 읽고 원인이 코드인지 기대값인지 가려 고친 뒤 처음부터 다시 돌린다.

- [ ] **Step 3: 마커와 로그를 본다**

```powershell
foreach ($base in Get-Content "$env:TEMP\xgs-bridge-check\runs.txt") {
    "--- $base"
    Get-Content "$base\log\last-run.txt"
    Get-ChildItem "$base\log", "$base\data" -File | Select-Object Name, Length | Format-Table -AutoSize | Out-String
}
```

Expected: 3번 모두 `state=exited`와 `reason=user`.

- [ ] **Step 4: 창을 닫을 때와 시작 직후 닫을 때 (B7)**

```powershell
. "$env:TEMP\xgs-bridge-check\helpers.ps1"
# 시작 직후 닫기: 제목이 있는 창이 생기자마자 닫는다(사이드카는 아직 뜨는 중이다). 3번.
foreach ($n in 1..3) {
    $run = New-CheckRun
    $process = Start-CheckShell $run
    $windowMs = Wait-CheckWindow $process
    $closedMs = Close-CheckShell $process
    $marker = (Get-Content "$($run.LogDir)\last-run.txt" -ErrorAction SilentlyContinue) -join ' | '
    "early close $n : window after $windowMs ms, everything gone after $closedMs ms, left over: $(@(Get-Sidecar).Count) :: $marker"
}
```

창을 기다릴 때 `MainWindowHandle`만 보면 안 된다. 프로세스를 띄운 지 40ms쯤에 이미 핸들이 잡히는데 그것은 닫기를 받지 않는 창이라서, 그 핸들로 `CloseMainWindow()`를 부르면 아무 일도 일어나지 않고 셸과 사이드카가 그대로 남는다(계획을 쓰며 스크래치에서 실제로 겪었다). 도우미의 `Wait-CheckWindow`는 제목이 생긴 창을 기다린다.

Expected: 3번 모두 `closedMs`가 숫자이고 15,000 이하(사이드카가 뜨기를 기다렸다가 정상 종료하므로 3초 안팎이다. 스크래치 예행에서 2,835 / 3,023 / 2,733ms), 남은 사이드카 0, 마커 `state=exited`와 `reason=user`. `reason=other`나 `state=running`이면 시작 중 종료가 Job Object에 맡겨진 것이므로 `Lifecycle::shutdown`을 고친다.

Step 2의 `closedMs`(주된 흐름 뒤 창 닫기)가 3000 이하이고 마커가 `reason=user`인 것이 "창을 닫을 때"의 증거다.

- [ ] **Step 5: 셸을 강제 종료한다 (B7의 M3)**

```powershell
. "$env:TEMP\xgs-bridge-check\helpers.ps1"
foreach ($n in 1..3) {
    $run = New-CheckRun
    $process = Start-CheckShell $run
    Wait-CheckRecord $run 90 | Out-Null
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Stop-Process -Id $process.Id -Force
    while ((Get-Sidecar) -and $watch.Elapsed.TotalSeconds -lt 10) { Start-Sleep -Milliseconds 25 }
    "force kill $n : sidecar gone after $([int]$watch.Elapsed.TotalMilliseconds) ms, left over: $(@(Get-Sidecar).Count)"
}
```

Expected: 3번 모두 5,000ms 이하이고 남은 사이드카 0(스크래치 예행에서 139 / 141 / 130ms). (자손 `yt-dlp.exe`는 이 PC에 없어서 이 단계에서 다시 확인하지 못한다. Job Object 코드는 바꾸지 않았다.)

- [ ] **Step 6: 토큰이 어디에도 없는지 본다 (B6)**

사이드카의 토큰은 43자의 base64url 문자열이다. 점검 페이지가 본 모든 값과 셸이 쓴 모든 파일에서 그런 문자열을 찾는다. 먼저 이 검색이 실제로 찾아내는지 대조 파일로 확인한다:

```powershell
$pattern = '(?<![A-Za-z0-9_-])[A-Za-z0-9_-]{43}(?![A-Za-z0-9_-])'
# A control with the same shape as the real token: 32 random bytes as base64url without padding (43 characters).
$fake = [Convert]::ToBase64String([byte[]](1..32 | ForEach-Object { Get-Random -Maximum 256 })).TrimEnd('=').Replace('+', '-').Replace('/', '_')
$control = "$env:TEMP\xgs-bridge-check\control.txt"
Set-Content $control "token=$fake" -Encoding UTF8
"control (length $($fake.Length)) hits: $(@(Select-String -Path $control -Pattern $pattern).Count)"
foreach ($base in Get-Content "$env:TEMP\xgs-bridge-check\runs.txt") {
    $hits = Get-ChildItem $base -Recurse -File | ForEach-Object { Select-String -Path $_.FullName -Pattern $pattern }
    "{0}: {1} hits in {2} files; spike files: {3}" -f (Split-Path $base -Leaf), @($hits).Count, @(Get-ChildItem $base -Recurse -File).Count, @(Get-ChildItem "$base\data" -Filter 'spike-*' -ErrorAction SilentlyContinue).Count
    $hits | Select-Object -First 3 | ForEach-Object { "   $($_.Path):$($_.LineNumber)" }
}
```

Expected: `control hits: 1`(검색이 찾아낸다), 실행마다 `0 hits`, `spike files: 0`(`spike-ready.json` 같은 파일이 더는 없다). 히트가 있으면 그 줄을 읽고 토큰인지 판단한다(토큰이면 새는 길을 막는 것이 이 단계의 실패다).

- [ ] **Step 7: 전체 시험을 한 번 더 돌린다**

```powershell
$env:Path += ";$env:USERPROFILE\.cargo\bin"; Set-Location C:\Projects\xGetSongs\sidecar-host; cargo test 2>&1 | Select-String "test result"; $env:XGS_SIDECAR_DIR = "C:\Projects\xGetSongs\server\build\sidecar-image"; cargo test --test real_sidecar -- --ignored 2>&1 | Select-String "test result"; Remove-Item Env:XGS_SIDECAR_DIR; Set-Location C:\Projects\xGetSongs\shell; node --test "tests/*.test.ts" 2>&1 | Select-String "^ℹ (pass|fail)"; npx tsc --noEmit; "tsc exit $LASTEXITCODE"
```

Expected: 단위 `75 passed`, 통합 `1 passed`, 화면 `pass 18`·`fail 0`, `tsc exit 0`. Kotlin은 바꾸지 않았으므로 Gradle 전체 테스트는 다시 돌리지 않는다.

- [ ] **Step 8: 보고서를 쓴다**

`docs/superpowers/specs/2026-10-11-tauri-rust-bridge-report.md`를 아래 틀로 만들고 측정값으로 채운다:

```markdown
# Tauri Rust 브리지(2단계) 보고서

작성일: 2026-10-11
설계: [2026-10-10-tauri-kotlin-sidecar-design.md](2026-10-10-tauri-kotlin-sidecar-design.md) §5의 2단계
계획: [2026-10-11-tauri-rust-bridge.md](../plans/2026-10-11-tauri-rust-bridge.md)

환경: (Windows, Rust, Tauri 버전, 사이드카 이미지, 셸은 `--config check.conf.json`으로 만든 릴리스 빌드, 앱 데이터는 실행마다 새 임시 폴더)

## 관문

| 번호 | 조건 | 결과 | 근거 |
|---|---|---|---|
| B1 | 가짜 서버 계약 테스트 | (통과/실패) | `cargo test` N개 |
| B2 | 실제 사이드카 계약 테스트 | | `real_sidecar` |
| B3 | 명령 8개가 웹뷰에서 동작 | | 점검 페이지 기록 3회 |
| B4 | 기다리는 동안 창이 멈추지 않음 | | `worstPingMs`, `worstFrameGapMs` |
| B5 | 이벤트 5,000개 | | `burst*` |
| B6 | 토큰이 화면에 보이지 않음 | | 소스 검색, 파일 검색 |
| B7 | 닫기·시작 직후 닫기·강제 종료 | | 측정값 |
| 악화 한도 | `waitedMs` 중앙값 ≤ 10초 | | |

## 측정

(표: waitedMs, readyMs 대신 페이지가 본 값, worstPingMs, worstFrameGapMs, pings, burstCommandMs, burstReceivedWhenTheCommandEnded, 창 닫기 → 정리, 시작 직후 닫기 → 정리, 강제 종료 → 정리. 1단계 값과 비교.)

## 막힌 점과 고친 것

(계획과 달랐던 점. 없으면 "없음".)

## 판정

(통과 / 재시도 후 통과 / 중단. 통과이면 다음은 설계 §5의 3단계 "프런트엔드 이식"이다.)

## 이 단계가 확인하지 않은 것

- 실제 조회와 다운로드. yt-dlp가 이 PC에 없고 설치하지 않았다. 그래서 실제 작업의 `item-started`·`progress`·`item-done`·`job-done` 이벤트가 브리지와 `Channel`을 지나는 것은 확인하지 못했다. 서버가 이벤트를 쓰는 방식(SSE 형식과 JSON 모양)은 `error` 이벤트로 실제 서버에서 확인했고, 나머지 이벤트의 JSON은 서버의 `RoutesTest`와 `ApiModelsTest`가 지킨다. 3단계의 첫 실제 다운로드에서 확인한다.
- 사이드카가 실행 중에 죽는 경우. 그 뒤의 호출은 `서버와의 연결이 끊어졌습니다.`를 받고 다시 시작하지 않는다(화면이 안내하고 앱을 다시 시작하게 할지는 3~4단계에서 정한다).
- Job Object를 만들었는지는 아무 데도 기록하지 않는다(스파이크의 `spike-job.txt`를 없앴다). 4단계에서 셸의 로그와 함께 다룬다.
- 점검 페이지와 점검용 명령 3개는 개발자용으로 남아 있다. 6단계에서 지울지 정한다.
- 이월: `sidecar-stderr.log`는 여전히 크기 제한이 없다, 로그 폴더가 있는데 쓸 수 없는 경우의 폴백이 없다, 프로토타입의 `탐색기에서 보기`는 실행 중 비활성이다, TypeScript 모델(`types.ts`)이 `ApiModels.kt`와 같은지는 손으로 지킨다(3단계에서 계약 테스트나 생성으로 바꿀지 정한다).
```

- [ ] **Step 9: 설계 문서를 갱신한다**

`docs/superpowers/specs/2026-10-10-tauri-kotlin-sidecar-design.md`의 §5 표에서 2단계 행을 아래로 바꾼다:

```markdown
| 2 | Rust 브리지: 명령 8개와 SSE → 이벤트. 토큰을 Rust 안에 숨긴다 | 브리지 계약 테스트 ([계획서](../plans/2026-10-11-tauri-rust-bridge.md), [보고서](2026-10-11-tauri-rust-bridge-report.md): 통과). 서버 변경 없음 |
```

§4 표의 `sidecar-host/` 행에서 "토큰 달린 HTTP GET"을 "서버 호출 6개와 이벤트 스트림(`Bridge`), 시작 대기·종료(`Lifecycle`)"로 바꾼다.

- [ ] **Step 10: Commit**

```bash
git add docs
git commit -m "문서: Rust 브리지(2단계)의 종단 확인 결과와 설계의 2단계 행"
```

---

## Self-Review

**1. Spec coverage** (설계 §2.2, §4, §5 2단계)

- "명령 8개": `sidecar_ready`, `tools`, `install_yt_dlp`, `update_yt_dlp`, `resolve`, `start_job`, `watch_job`, `cancel_job` → Task 2(서버 호출 6개), Task 3(`watch_job`), Task 4(`sidecar_ready`가 기다리는 `Lifecycle`), Task 7(Tauri 명령).
- "SSE → 이벤트": `SseParser`(Task 1), `watch_job`(Task 3), `Channel`(Task 7), 화면 래퍼 `watchJob`(Task 6), 이벤트 경로의 부하와 순서(Task 8의 `check_burst`, B5).
- "토큰을 Rust 안에 숨긴다": `Handshake`의 `Debug`(Task 1), `Bridge`의 오류에 토큰 없음(Task 2의 `no_error_of_any_call_holds_the_token`), 실제 서버로 확인(Task 5), 소스와 파일 검색(Task 7 Step 7, Task 8 Step 6).
- 스파이크의 잠금 쥔 HTTP와 토큰 노출 해소: `Lifecycle`이 `Bridge`(복사본)를 내주고(Task 4), `Probe`/`spike-ready.json`/`spike_record`를 없앤다(Task 7). 창이 멈추지 않음은 B4.
- "브리지 계약 테스트": B1(가짜 서버), B2(실제 서버).
- 이 계획 밖(의도적): 실제 조회·다운로드 흐름(yt-dlp 없음), 사이드카 재시작 정책, 셸 로그, 프런트엔드 이식(3단계), 폴더 선택·`settings.json`(4단계).

**2. Placeholder scan:** 코드 단계는 컴파일하고 시험한 실제 코드를 담았다. 계획을 쓰며 스크래치 복사본에서 이 계획의 Rust 단계를 순서대로 적용해 각 RED·GREEN 지점의 개수를 확인했고(위 Expected의 숫자가 그 결과다), 실제 사이드카 계약 테스트 1개, 셸 `cargo check`, `tsc`, `node --test` 18개를 통과시켰다. 같은 코드로 점검용 셸 릴리스 빌드를 만들어 Task 8의 Step 2·4·5·6을 한 번씩 돌려 봤다(그때 확인용 스크립트의 결함 두 가지, 너무 일찍 잡은 창 핸들과 길이 44짜리 대조 문자열을 고쳐 계획에 반영했다). 보고서의 빈 칸은 측정값을 채우는 기록 칸이다.

**3. Type consistency:** `Bridge::new(Handshake)`, `with_short_timeout(self, Duration)`, `watch_job(&self, &str, impl FnMut(Value) -> bool)`(Task 2~3)는 `Lifecycle::wait_ready -> Result<Bridge, BridgeError>`(Task 4)와 Tauri 명령(Task 7)이 같은 이름과 시그니처로 쓴다. `BridgeError { message }`의 JSON(`{"message": ...}`)은 `bridge.ts`의 `BridgeError`와 `errorMessage`(Task 6)가 읽는 모양이다. 명령 이름과 인자(`jobId`↔`job_id`, `onEvent`↔`on_event`, `request`, `input`)는 Task 6의 래퍼, Task 7의 `lib.rs`, Task 8의 점검 페이지가 같다. 환경 변수 `XGS_SIDECAR_DIR`/`_APPDATA`/`_LOGDIR`(1단계)와 `XGS_BRIDGE_CHECK_FILE`(Task 7·8)은 Rust·스크립트에서 같다. 서버 오류 문구는 Task 2(가짜 서버 스크립트), Task 5(실제 서버 단언), Task 8(`$expected`)에서 같은 글자다.

**4. Review Focus:** 다섯 항목 모두 위 Review Focus 절에 적은 Task의 테스트에 대응한다(4번의 시작 직후 닫기는 Task 8 Step 4가 실제 셸로 한 번 더 확인한다).

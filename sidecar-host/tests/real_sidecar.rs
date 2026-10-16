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

fn work_folder(name: &str) -> PathBuf {
    let folder = std::env::temp_dir().join(format!("xgs-real-sidecar-test-{}-{name}", std::process::id()));
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
    let folder = work_folder("contract");
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

    // POST /tools/yt-dlp/update: with no yt-dlp there is nothing to update, and the server says so without any download.
    // (Skipped when yt-dlp is found, because the call would then really update it. The install call is never made here:
    // it downloads a file.)
    if tools["ytDlp"]["found"] == false {
        assert_eq!(bridge.update_yt_dlp().unwrap_err().message, "yt-dlp가 설치되어 있지 않습니다.");
    }

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

#[test]
#[ignore = "starts the real sidecar: set XGS_SIDECAR_DIR to server/build/sidecar-image"]
fn the_real_server_answers_an_empty_browser_poll_with_no_content() {
    let folder = work_folder("empty-poll");
    let mut sidecar = start(&folder);
    let bridge = Bridge::new(sidecar.handshake.clone());

    // Nobody asked the hidden browser for anything: the poll ends after its wait with 204, which the bridge reads as "no command".
    let started = Instant::now();
    let command = bridge.next_browser_command(Duration::from_millis(200)).expect("the poll");

    assert_eq!(command, None);
    assert!(started.elapsed() >= Duration::from_millis(150), "it waited: {:?}", started.elapsed());
    assert!(sidecar.shutdown(Duration::from_secs(10)), "it ended on its own");
    let _ = std::fs::remove_dir_all(&folder);
}

#[test]
#[ignore = "starts the real sidecar: set XGS_SIDECAR_DIR to server/build/sidecar-image"]
fn an_answer_nobody_waits_for_is_refused_with_the_servers_message() {
    let folder = work_folder("unknown-answer");
    let mut sidecar = start(&folder);
    let bridge = Bridge::new(sidecar.handshake.clone());

    let refused = bridge.answer_browser_command(&json!({"id": "nobody-asked", "ok": true, "value": null, "error": null}));

    assert_eq!(refused.unwrap_err().message, "기다리는 명령이 없습니다.");
    assert!(sidecar.shutdown(Duration::from_secs(10)), "it ended on its own");
    let _ = std::fs::remove_dir_all(&folder);
}

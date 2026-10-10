//! A real resolve, one real download and a real cancel through the bridge, with the real yt-dlp, ffmpeg and the network. It is
//! ignored by default. The yt-dlp is a COPY of the one the app installed (the original and the user's app data are never
//! touched): the sidecar looks for it in `<app data>\bin`, and this test gives it a temporary app data folder.
//!
//!     $env:XGS_SIDECAR_DIR = "<repo>\server\build\sidecar-image"
//!     $env:XGS_REAL_YTDLP  = "$env:APPDATA\xGetSongs\bin\yt-dlp.exe"
//!     $env:XGS_REAL_URL    = "https://www.youtube.com/watch?v=<id>&list=<playlist id>"
//!     cargo test --test real_download -- --ignored --nocapture
//!
//! Only ONE song is downloaded (the video of `v=`, else the first available item) and a second job is cancelled as soon as
//! it reports progress, so a playlist of any size is safe to use. Nothing is printed that is not a count, a type or a time.

use serde_json::{json, Value};
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::sync::mpsc;
use std::time::{Duration, Instant};
use xgs_sidecar::{Bridge, Sidecar, SidecarConfig};

fn require(variable: &str) -> String {
    std::env::var(variable).unwrap_or_else(|_| panic!("set {variable} (see the top of tests/real_download.rs)"))
}

/// The `v=` of a watch URL.
fn video_id_of(url: &str) -> Option<String> {
    let query = url.split_once('?')?.1;
    query.split('&').find_map(|pair| pair.strip_prefix("v=")).map(str::to_string)
}

fn start(folder: &Path) -> Sidecar {
    let lookup = |key: &str| -> Option<OsString> {
        match key {
            "XGS_SIDECAR_DIR" => std::env::var_os("XGS_SIDECAR_DIR"),
            "XGS_SIDECAR_APPDATA" => Some(folder.join("data").into_os_string()),
            "XGS_SIDECAR_LOGDIR" => Some(folder.join("log").into_os_string()),
            _ => None,
        }
    };
    let config = SidecarConfig::from_lookup(lookup, None).expect("set XGS_SIDECAR_DIR to server/build/sidecar-image");
    Sidecar::spawn(config.command(), &config.stderr_log(), Duration::from_secs(60)).expect("the sidecar starts")
}

/// Every file below [folder], with its size.
fn files_below(folder: &Path) -> Vec<(PathBuf, u64)> {
    let mut found = Vec::new();
    let mut pending = vec![folder.to_path_buf()];
    while let Some(directory) = pending.pop() {
        if let Ok(entries) = std::fs::read_dir(&directory) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.is_dir() {
                    pending.push(path);
                } else if let Ok(metadata) = entry.metadata() {
                    found.push((path, metadata.len()));
                }
            }
        }
    }
    found
}

/// The fields every event carries on the wire, as `shell/src/bridge/types.ts` (`JobEvent`) declares them: the server writes
/// every field, null included. The keys of a JSON object come out sorted.
fn assert_wire_shape(events: &[Value]) {
    for event in events {
        let expected: &[&str] = match event["type"].as_str().unwrap_or("?") {
            "item-started" => &["fileName", "rank", "type", "videoId"],
            "progress" => &["percent", "rank", "stage", "type"],
            "item-done" => &["fileName", "lyrics", "rank", "type"],
            "item-skipped" => &["rank", "reason", "type"],
            "item-failed" => &["message", "rank", "type"],
            "job-done" => &["status", "summary", "type"],
            other => panic!("an event type the page does not know: {other}: {event}"),
        };
        let keys: Vec<&str> = event.as_object().expect("an object").keys().map(String::as_str).collect();
        assert_eq!(keys, expected, "the fields of {event}");
    }
}

fn types_of(events: &[Value]) -> Vec<&str> {
    events.iter().map(|event| event["type"].as_str().unwrap_or("?")).collect()
}

#[test]
#[ignore = "uses the network, yt-dlp and ffmpeg: set XGS_SIDECAR_DIR, XGS_REAL_YTDLP and XGS_REAL_URL"]
fn a_real_resolve_a_real_download_and_a_real_cancel_come_through_the_bridge() {
    let url = require("XGS_REAL_URL");
    let ytdlp = PathBuf::from(require("XGS_REAL_YTDLP"));
    let folder = std::env::temp_dir().join(format!("xgs-real-download-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&folder);
    std::fs::create_dir_all(folder.join("data").join("bin")).unwrap();
    std::fs::copy(&ytdlp, folder.join("data").join("bin").join("yt-dlp.exe")).expect("copy yt-dlp");

    let mut sidecar = start(&folder);
    let bridge = Bridge::new(sidecar.handshake.clone());

    // GET /tools: the copy is found.
    let tools = bridge.tools().expect("tools");
    assert_eq!(tools["ytDlp"]["found"], true, "the copied yt-dlp is found: {tools}");
    assert_eq!(tools["ffmpeg"]["found"], true, "ffmpeg is found: {tools}");
    println!("tools: yt-dlp {} found, ffmpeg found, js runtime found={}", tools["ytDlp"]["version"], tools["jsRuntime"]["found"]);

    // POST /resolve: the whole URL (a playlist is listed, nothing is downloaded).
    let started = Instant::now();
    let resolved = bridge.resolve(&url).expect("resolve");
    let items = resolved["items"].as_array().expect("items");
    println!(
        "resolve: kind={} items={} truncated={} alsoVideoId={} in {} ms",
        resolved["kind"], items.len(), resolved["truncated"], resolved["alsoVideoId"], started.elapsed().as_millis()
    );
    assert!(!items.is_empty());
    let resolve_id = resolved["resolveId"].as_str().expect("resolveId").to_string();
    let available: Vec<&Value> = items.iter().filter(|item| item["available"] == true).collect();
    let target = video_id_of(&url)
        .and_then(|id| available.iter().find(|item| item["videoId"] == id.as_str()).copied())
        .or_else(|| available.first().copied())
        .expect("an available item");
    let rank = target["rank"].as_i64().expect("rank");
    println!("target: rank {rank} of {} items, {} available", items.len(), available.len());

    // POST /jobs + GET /jobs/{id}/events: ONE song, the events as the page would get them.
    let out = folder.join("out");
    let request = json!({"resolveId": resolve_id, "options": {"outputDir": out, "searchLyricsOnline": false}, "ranks": [rank]});
    let created = bridge.start_job(&request).expect("start_job");
    let job_id = created["jobId"].as_str().expect("jobId").to_string();
    let mut events: Vec<Value> = Vec::new();
    let begun = Instant::now();
    bridge
        .watch_job(&job_id, |event| {
            events.push(event);
            true
        })
        .expect("watch_job");
    assert_wire_shape(&events);
    let types = types_of(&events);
    println!("download: {} events in {} ms; first {:?}, last {:?}", events.len(), begun.elapsed().as_millis(), types.first(), types.last());
    assert_eq!(types.first(), Some(&"item-started"), "{types:?}");
    assert!(types.contains(&"progress"), "a download reports progress: {types:?}");
    assert!(types.contains(&"item-done"), "the item finished: {types:?}");
    let done = events.last().unwrap();
    assert_eq!(done["type"], "job-done");
    assert_eq!(done["status"], "COMPLETED", "{done}");
    assert_eq!(done["summary"], json!({"succeeded": 1, "skipped": 0, "failed": 0}), "{done}");
    let progress: Vec<f64> = events.iter().filter(|e| e["type"] == "progress").filter_map(|e| e["percent"].as_f64()).collect();
    println!("progress: {} events with a percent, up to {:?}", progress.len(), progress.iter().cloned().fold(None, |a: Option<f64>, b| Some(a.map_or(b, |a| a.max(b)))));
    let files = files_below(&out);
    println!("files: {} written, sizes {:?}", files.len(), files.iter().map(|(_, size)| *size).collect::<Vec<_>>());
    assert_eq!(files.len(), 1, "exactly one file: {files:?}");
    assert!(files[0].0.extension().is_some_and(|extension| extension == "m4a"), "{:?}", files[0].0);
    assert!(files[0].1 > 100_000, "a song is more than 100 KB: {} bytes", files[0].1);

    // A second job over other available items, cancelled as soon as it reports progress (DELETE /jobs/{id}).
    let others: Vec<i64> = available.iter().filter_map(|item| item["rank"].as_i64()).filter(|other| *other != rank).take(3).collect();
    assert!(others.len() >= 2, "the playlist has other available items to start a job on");
    let out_cancelled = folder.join("out-cancelled");
    let request = json!({"resolveId": resolved["resolveId"], "options": {"outputDir": out_cancelled, "searchLyricsOnline": false}, "ranks": others});
    let job_id = bridge.start_job(&request).expect("start_job (to cancel)")["jobId"].as_str().unwrap().to_string();
    let (sender, receiver) = mpsc::channel::<Value>();
    let watcher = {
        let bridge = bridge.clone();
        let job_id = job_id.clone();
        std::thread::spawn(move || bridge.watch_job(&job_id, |event| sender.send(event).is_ok()))
    };
    let mut seen: Vec<Value> = Vec::new();
    let mut cancel_sent: Option<Instant> = None;
    let deadline = Instant::now() + Duration::from_secs(300);
    loop {
        let wait = deadline.saturating_duration_since(Instant::now());
        let event = receiver.recv_timeout(wait).expect("the cancelled job keeps sending events until job-done");
        let is_progress = event["type"] == "progress";
        let is_done = event["type"] == "job-done";
        seen.push(event);
        if is_progress && cancel_sent.is_none() {
            bridge.cancel_job(&job_id).expect("cancel_job");
            cancel_sent = Some(Instant::now());
        }
        if is_done {
            break;
        }
    }
    let cancelled_after = cancel_sent.expect("a progress event came before the end").elapsed();
    watcher.join().unwrap().expect("watch_job of the cancelled job");
    assert_wire_shape(&seen);
    let done = seen.last().unwrap();
    println!("cancel: {} events, job-done status {} {}, {} ms after the cancel call", seen.len(), done["status"], done["summary"], cancelled_after.as_millis());
    assert_eq!(done["status"], "CANCELLED", "{done}");
    assert!(cancelled_after < Duration::from_secs(30), "the cancel took {cancelled_after:?}");
    // And the job is gone from the server: a second cancel is "not found".
    assert_eq!(bridge.cancel_job(&job_id).unwrap_err().message, "작업을 찾을 수 없습니다.");

    assert!(sidecar.shutdown(Duration::from_secs(10)), "the sidecar ended on its own");
    let _ = std::fs::remove_dir_all(&folder);
}

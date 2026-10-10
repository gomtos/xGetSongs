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

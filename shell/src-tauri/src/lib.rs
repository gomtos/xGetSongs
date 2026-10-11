use serde_json::{json, Value};
use std::io::Write;
use std::time::Duration;
use tauri::ipc::Channel;
use tauri::{Emitter, Manager, RunEvent, State, WindowEvent};
use tauri_plugin_dialog::DialogExt;
use xgs_sidecar::{
    app_data_dir, folder, job, Bridge, BridgeError, CloseDecision, CloseGate, Lifecycle, SettingsFile, Sidecar, SidecarConfig,
    UserSettings,
};

/// How long a call waits for a sidecar that is still starting. Starting itself gives up after [SIDECAR_START_LIMIT].
const READY_WAIT: Duration = Duration::from_secs(90);
const SIDECAR_START_LIMIT: Duration = Duration::from_secs(60);
/// When the app closes: how long the sidecar gets to end by itself, and how long one that is still starting is waited for.
const STOP_GRACE: Duration = Duration::from_secs(3);
const STOP_START_WAIT: Duration = Duration::from_secs(10);
/// How long the page gets to finish up (write the options) when the window is closed, before the window closes anyway.
const CLOSE_WAIT: Duration = Duration::from_millis(1500);
/// What the shell tells the page when the user closes the window; the page answers with `close_window`.
const CLOSE_REQUESTED_EVENT: &str = "xgs-close-requested";

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

/// The options as the file in the app data folder holds them (the defaults when it holds none that can be used). The page asks once,
/// before it builds its screen.
#[tauri::command]
async fn load_settings(file: State<'_, SettingsFile>) -> Result<UserSettings, String> {
    let file = file.inner().clone();
    tauri::async_runtime::spawn_blocking(move || file.load()).await.map_err(|error| error.to_string())
}

/// Writes the options. Nothing fails the page: a profile that cannot be written to means the options are not kept.
#[tauri::command]
async fn save_settings(file: State<'_, SettingsFile>, settings: UserSettings) -> Result<(), String> {
    let file = file.inner().clone();
    tauri::async_runtime::spawn_blocking(move || file.save(&settings)).await.map_err(|error| error.to_string())
}

/// Opens the system's folder chooser over the window, starting in [initial] when that is a folder. Resolves with the chosen path, or
/// with nothing when the user cancels.
#[tauri::command]
async fn pick_folder(window: tauri::WebviewWindow, initial: String) -> Option<String> {
    tauri::async_runtime::spawn_blocking(move || {
        let mut dialog = window.dialog().file().set_title("출력 폴더 선택").set_parent(&window);
        if let Some(directory) = folder::existing_directory(&initial) {
            dialog = dialog.set_directory(directory);
        }
        dialog.blocking_pick_folder().and_then(|picked| picked.into_path().ok()).map(|path| path.to_string_lossy().into_owned())
    })
    .await
    .ok()
    .flatten()
}

/// Shows the folder in the file manager (the nearest one that exists). Returns at once; whatever goes wrong is swallowed.
#[tauri::command]
fn open_folder(path: String) {
    folder::open_in_explorer(&path);
}

/// The page has a listener for [CLOSE_REQUESTED_EVENT]: from now on closing the window asks the page first.
#[tauri::command]
fn page_listens_for_close(gate: State<'_, CloseGate>) {
    gate.page_listens();
}

/// The page has finished up: close the window now (the app ends with its last window).
#[tauri::command]
fn close_window(window: tauri::WebviewWindow) {
    let _ = window.destroy();
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

/// What the check script told the page to use: an address of a video, an address of a playlist, a folder for the files. Only
/// these names, and only in check mode.
#[tauri::command]
fn check_env(name: String) -> Result<Option<String>, String> {
    check_file()?;
    const ALLOWED: [&str; 4] = ["XGS_CHECK_VIDEO_URL", "XGS_CHECK_PLAYLIST_URL", "XGS_CHECK_OUTPUT_DIR", "XGS_CHECK_DESKTOP"];
    if !ALLOWED.contains(&name.as_str()) {
        return Err(format!("{name} is not one of the values of the check"));
    }
    Ok(std::env::var(&name).ok())
}

/// Closes the window as the user would (a request to close, which the page may answer first). Check mode only.
#[tauri::command]
fn check_close(window: tauri::WebviewWindow) -> Result<(), String> {
    check_file()?;
    window.close().map_err(|error| error.to_string())
}

/// Waits until the check script creates the file `<check file>.signal-<name>`: the script does something outside the window (it works
/// a system dialog, looks at the file manager) and tells the page when it is done. Check mode only; gives up after a minute.
#[tauri::command]
async fn check_wait(name: String) -> Result<(), String> {
    let mut signal = check_file()?.into_os_string();
    if name.is_empty() || !name.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-') {
        return Err(format!("{name} is not a name of a signal"));
    }
    signal.push(format!(".signal-{name}"));
    let signal = std::path::PathBuf::from(signal);
    tauri::async_runtime::spawn_blocking(move || {
        for _ in 0..1200 {
            if signal.exists() {
                return Ok(());
            }
            std::thread::sleep(Duration::from_millis(50));
        }
        Err(format!("no signal {} after a minute", signal.display()))
    })
    .await
    .map_err(|error| error.to_string())?
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

    let app = tauri::Builder::default()
        // First of all: a second copy of the app ends here, while the plugins are set up, and brings the window of the first one to the
        // front. Nothing that touches the app data folder may have started by then: the sidecar clears its `work` folder when it starts.
        .plugin(tauri_plugin_single_instance::init(|app, _args, _cwd| {
            if let Some(window) = app.get_webview_window("main") {
                let _ = window.unminimize();
                let _ = window.show();
                let _ = window.set_focus();
            }
        }))
        .plugin(tauri_plugin_opener::init())
        // Used from Rust only (`pick_folder`); the page is given no `dialog:*` permission. Note that the plugin's init script replaces
        // `window.alert` and `window.confirm` of every page with calls to a command that this app does not allow: `alert()` does nothing
        // and `confirm()` returns a promise. The page uses neither; do not start to.
        .plugin(tauri_plugin_dialog::init())
        .manage(CloseGate::default())
        .on_window_event(|window, event| {
            if let WindowEvent::CloseRequested { api, .. } = event {
                match window.state::<CloseGate>().on_close_requested() {
                    CloseDecision::Close => {}
                    CloseDecision::Wait => api.prevent_close(),
                    CloseDecision::Ask => {
                        // The page writes the options that are still waiting for their quiet period and then asks for the window to be
                        // closed. A page that does not answer must not keep the window open.
                        api.prevent_close();
                        let _ = window.emit(CLOSE_REQUESTED_EVENT, ());
                        let window = window.clone();
                        std::thread::spawn(move || {
                            std::thread::sleep(CLOSE_WAIT);
                            let _ = window.destroy();
                        });
                    }
                }
            }
        })
        .invoke_handler(tauri::generate_handler![
            sidecar_ready,
            tools,
            install_yt_dlp,
            update_yt_dlp,
            resolve,
            start_job,
            cancel_job,
            watch_job,
            load_settings,
            save_settings,
            pick_folder,
            open_folder,
            page_listens_for_close,
            close_window,
            check_record,
            check_ping,
            check_env,
            check_close,
            check_wait,
            check_burst
        ])
        .build(tauri::generate_context!())
        .expect("error while building tauri application");

    // Only now: a second copy of the app has ended inside `build` (the single-instance plugin), so what starts here is the one sidecar
    // of the one app. (Started before the `Builder`, as it was, a second copy would start a sidecar too, and the sidecar clears the
    // `work` folder of the app data folder when it starts: the downloads of the first copy would fail.)
    let config = SidecarConfig::from_env();
    app.manage(Lifecycle::start(move || {
        let config = config?;
        Sidecar::spawn(config.command(), &config.stderr_log(), SIDECAR_START_LIMIT)
    }));
    app.manage(SettingsFile::new(app_data_dir(|key| std::env::var_os(key)).join("settings.json")));

    app.run(|handle, event| {
        if let RunEvent::Exit = event
            && let Some(lifecycle) = handle.try_state::<Lifecycle>()
        {
            lifecycle.shutdown(STOP_GRACE, STOP_START_WAIT);
        }
    });
}

use serde::Serialize;
use std::io::Write;
use std::path::PathBuf;
use std::sync::Mutex;
use std::time::{Duration, Instant};
use tauri::{Manager, RunEvent};
use xgs_sidecar::{http_get, job, Sidecar, SidecarConfig};

/// Where the sidecar is in its life. Starting takes seconds (a JVM), so it is not waited for on the UI thread.
enum Phase {
    Starting,
    Ready { sidecar: Sidecar, ready_ms: u128 },
    Failed(String),
    Stopped,
}

struct SidecarState(Mutex<Phase>);

/// The sidecar's app data folder, where the spike leaves its result files for the scripted checks of the plan.
struct SpikeFolder(PathBuf);

/// What the page shows. The token is here for the spike's own checks (the direct-fetch demo, a curl by hand) only: the real
/// bridge keeps it in Rust.
#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct Probe {
    port: u16,
    token: String,
    ready_ms: u128,
    tools_ms: u128,
    tools: String,
}

/// Asks the sidecar for its tools. `Err("starting")` until the sidecar has said it is ready.
#[tauri::command]
fn sidecar_probe(state: tauri::State<'_, SidecarState>) -> Result<Probe, String> {
    let phase = state.0.lock().map_err(|_| "the state is poisoned".to_string())?;
    match &*phase {
        Phase::Starting => Err("starting".to_string()),
        Phase::Failed(reason) => Err(reason.clone()),
        Phase::Stopped => Err("stopped".to_string()),
        Phase::Ready { sidecar, ready_ms } => {
            let started = Instant::now();
            let tools = http_get(&sidecar.handshake, "/tools")?;
            Ok(Probe {
                port: sidecar.handshake.port,
                token: sidecar.handshake.token.clone(),
                ready_ms: *ready_ms,
                tools_ms: started.elapsed().as_millis(),
                tools,
            })
        }
    }
}

/// The page reports what it measured as one `key=value` line of `spike-record.txt`.
#[tauri::command]
fn spike_record(folder: tauri::State<'_, SpikeFolder>, key: String, value: String) -> Result<(), String> {
    let mut file = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(folder.0.join("spike-record.txt"))
        .map_err(|error| error.to_string())?;
    writeln!(file, "{key}={}", value.replace(['\r', '\n'], " ")).map_err(|error| error.to_string())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    // First of all: whatever happens to this process, the sidecar and its children go with it.
    let job_result = job::kill_children_when_this_process_ends();

    let folder = SidecarConfig::from_env()
        .map(|config| config.app_data)
        .unwrap_or_else(|_| std::env::temp_dir().join("xgs-spike-appdata"));
    let _ = std::fs::create_dir_all(&folder);
    // The results of an earlier run must not be mistaken for this one.
    for stale in ["spike-ready.json", "spike-failed.txt", "spike-record.txt"] {
        let _ = std::fs::remove_file(folder.join(stale));
    }
    let _ = std::fs::write(
        folder.join("spike-job.txt"),
        match &job_result {
            Ok(()) => "ok".to_string(),
            Err(reason) => format!("failed: {reason}"),
        },
    );

    let app = tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(SidecarState(Mutex::new(Phase::Starting)))
        .manage(SpikeFolder(folder))
        .setup(|app| {
            let handle = app.handle().clone();
            std::thread::spawn(move || {
                let started = Instant::now();
                let result = SidecarConfig::from_env().and_then(|config| {
                    Sidecar::spawn(config.command(), &config.stderr_log(), Duration::from_secs(60))
                });
                let folder = handle.state::<SpikeFolder>().0.clone();
                let phase = match result {
                    Ok(sidecar) => {
                        let ready_ms = started.elapsed().as_millis();
                        let ready = format!(
                            r#"{{"port":{},"token":"{}","readyMs":{}}}"#,
                            sidecar.handshake.port, sidecar.handshake.token, ready_ms
                        );
                        let _ = std::fs::write(folder.join("spike-ready.json"), ready);
                        Phase::Ready { sidecar, ready_ms }
                    }
                    Err(reason) => {
                        let _ = std::fs::write(folder.join("spike-failed.txt"), &reason);
                        Phase::Failed(reason)
                    }
                };
                if let Ok(mut state) = handle.state::<SidecarState>().0.lock() {
                    *state = phase;
                }
            });
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![sidecar_probe, spike_record])
        .build(tauri::generate_context!())
        .expect("error while building tauri application");

    app.run(|handle, event| {
        if let RunEvent::Exit = event {
            let state = handle.state::<SidecarState>();
            let mut phase = state.0.lock().unwrap();
            if let Phase::Ready { sidecar, .. } = &mut *phase {
                sidecar.shutdown(Duration::from_secs(3));
            }
            *phase = Phase::Stopped;
        }
    });
}

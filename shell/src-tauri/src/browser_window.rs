//! The hidden browser of the Google lyrics lookup: a WebView2 window that is never shown. The sidecar's `BrowserChannel` queues what
//! it wants done (open an address, run a script, go back to the blank page) and the agent of `xgs_sidecar::browser` brings it here.
//!
//! The window is made on the first `load` and reused; it is private (nothing is kept on disk), shows nothing, opens no other window and
//! takes no download. It is in no capability file, so the page in it cannot use the commands of the app (`tests/capabilities.test.ts`
//! keeps it so), and the agent refuses addresses that are not `https://` (see `xgs_sidecar::browser::allowed_url`).

use std::sync::mpsc;
use std::time::Duration;
use tauri::webview::NewWindowResponse;
use tauri::{AppHandle, WebviewUrl, WebviewWindow, WebviewWindowBuilder};
use webview2_com::ExecuteScriptCompletedHandler;
use windows_core::HSTRING;
use xgs_sidecar::browser::{allowed_navigation, allowed_url, Page};

/// What the labels of the hidden windows start with (`xgs-browser-1`, `xgs-browser-2`, ...). None of them may be in a capability file.
/// Every window gets a label of its own: one that was destroyed a moment ago still holds its label, and a new window made at once
/// (the lookup closes a page that failed and loads the next) would be refused.
pub const BROWSER_LABEL: &str = "xgs-browser";

/// How long a script may take to answer. A page that never gives the thread back (a loop that does not end) never answers; the agent
/// then closes the window.
const SCRIPT_LIMIT: Duration = Duration::from_secs(5);

pub struct BrowserWindow {
    handle: AppHandle,
    window: Option<WebviewWindow>,
    made: u32,
}

impl BrowserWindow {
    pub fn new(handle: AppHandle) -> BrowserWindow {
        BrowserWindow { handle, window: None, made: 0 }
    }

    /// Whether the window is visible; None when there is no window. (The checks look at it: it must be false.)
    pub fn is_visible(&self) -> Option<bool> {
        self.window.as_ref()?.is_visible().ok()
    }
}

impl Page for BrowserWindow {
    fn load(&mut self, url: &str) -> Result<(), String> {
        if !allowed_url(url) {
            return Err("url not allowed".to_string());
        }
        let address: tauri::Url = url.parse().map_err(|_| "url not allowed".to_string())?;
        if let Some(window) = &self.window {
            return window.navigate(address).map_err(|error| error.to_string());
        }
        self.made += 1;
        let label = format!("{BROWSER_LABEL}-{}", self.made);
        let window = WebviewWindowBuilder::new(&self.handle, label, WebviewUrl::External(address))
            .title("xGetSongs")
            .visible(false)
            .focused(false)
            .skip_taskbar(true)
            .incognito(true)
            .on_navigation(|address| allowed_navigation(address.as_str()))
            .on_new_window(|_, _| NewWindowResponse::Deny)
            .on_download(|_, _| false)
            .build()
            .map_err(|error| error.to_string())?;
        self.window = Some(window);
        Ok(())
    }

    fn evaluate(&mut self, script: &str) -> Result<String, String> {
        let window = self.window.as_ref().ok_or_else(|| "no window".to_string())?;
        let (answer, answered) = mpsc::channel::<Result<String, String>>();
        let failed = answer.clone();
        let script = HSTRING::from(script);
        // Runs on the main thread. WebView2 reports the result of the script to the handler, also on that thread.
        window
            .with_webview(move |platform| unsafe {
                let started = platform.controller().CoreWebView2().and_then(|core| {
                    core.ExecuteScript(
                        &script,
                        &ExecuteScriptCompletedHandler::create(Box::new(move |code, result| {
                            let _ = answer.send(code.map(|()| result).map_err(|error| error.to_string()));
                            Ok(())
                        })),
                    )
                });
                if let Err(error) = started {
                    let _ = failed.send(Err(error.to_string()));
                }
            })
            .map_err(|error| error.to_string())?;
        answered.recv_timeout(SCRIPT_LIMIT).map_err(|_| "script timed out".to_string())?
    }

    fn reset(&mut self) -> Result<(), String> {
        match &self.window {
            Some(window) => {
                let blank: tauri::Url = "about:blank".parse().map_err(|_| "no blank page".to_string())?;
                window.navigate(blank).map_err(|error| error.to_string())
            }
            None => Ok(()),
        }
    }

    fn close(&mut self) {
        if let Some(window) = self.window.take() {
            let _ = window.destroy();
        }
    }
}

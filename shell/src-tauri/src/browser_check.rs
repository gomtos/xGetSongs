//! Check mode only (the `check_browser` command in lib.rs refuses without `XGS_BRIDGE_CHECK_FILE`): runs the hidden browser window
//! against the fixed pages that `tools/browser-check/fixture.mjs` serves, with no Google and no sidecar involved, and reports one line
//! per thing the window must do: `name=ok <detail>` or `name=FAIL <what was seen>`.

use crate::browser_window::{BrowserWindow, BROWSER_LABEL};
use serde_json::Value;
use std::time::{Duration, Instant};
use tauri::{AppHandle, Manager};
use xgs_sidecar::browser::{decode_script_result, Page};
use xgs_sidecar::CloseGate;

/// One look at the page: whether the card shows, the counter of its timer (-1: the page has none) and the start of its text.
const LOOK: &str = "JSON.stringify({ card: !!document.querySelector('div[data-lyricid]'), ticks: window.__ticks === undefined ? -1 : window.__ticks, text: (document.body ? document.body.innerText : '').substring(0, 300) })";

fn look(window: &mut BrowserWindow) -> Result<Value, String> {
    let answer = window.evaluate(LOOK)?;
    let text = decode_script_result(&answer).ok_or_else(|| "the script gave nothing".to_string())?;
    serde_json::from_str(&text).map_err(|error| error.to_string())
}

/// Looks every 100 ms until the page shows its card (10 s at most) and returns that look.
fn wait_for_card(window: &mut BrowserWindow) -> Result<Value, String> {
    let began = Instant::now();
    let mut last = "no look yet".to_string();
    while began.elapsed() < Duration::from_secs(10) {
        std::thread::sleep(Duration::from_millis(100));
        match look(window) {
            Ok(seen) if seen["card"] == true => return Ok(seen),
            Ok(seen) => last = seen.to_string(),
            Err(error) => last = error,
        }
    }
    Err(format!("no card after 10 s ({last})"))
}

fn windows_besides_the_app_and_the_browser(app: &AppHandle) -> usize {
    app.webview_windows().keys().filter(|label| label.as_str() != "main" && !label.starts_with(BROWSER_LABEL)).count()
}

fn built_fast(window: &mut BrowserWindow, url: &str) -> Result<String, String> {
    let began = Instant::now();
    window.load(url)?;
    let built = began.elapsed().as_millis();
    if window.is_visible() != Some(false) {
        return Err(format!("the window is visible: {:?}", window.is_visible()));
    }
    if built > 3000 {
        return Err(format!("built after {built} ms"));
    }
    Ok(format!("{built} ms, hidden"))
}

fn timers_run_hidden(window: &mut BrowserWindow) -> Result<String, String> {
    let before = wait_for_card(window)?["ticks"].as_i64().ok_or_else(|| "the page has no timer".to_string())?;
    std::thread::sleep(Duration::from_millis(500));
    let after = look(window)?["ticks"].as_i64().ok_or_else(|| "the page has no timer".to_string())?;
    // The page's timer fires every 50 ms: ten ticks in 500 ms when nothing holds it back.
    if after - before < 8 {
        return Err(format!("{} ticks in 500 ms", after - before));
    }
    Ok(format!("{} ticks in 500 ms", after - before))
}

fn window_is_reused(window: &mut BrowserWindow, url: &str) -> Result<String, String> {
    window.load(url)?;
    let seen = wait_for_card(window)?;
    if !seen["text"].as_str().unwrap_or("").contains("card of second") {
        return Err(format!("the second page did not show: {seen}"));
    }
    Ok("the same window showed the second page".to_string())
}

fn cookie_does_not_survive_the_window(window: &mut BrowserWindow, url: &str) -> Result<String, String> {
    // The first page set a cookie that lasts an hour. A private window keeps it only as long as the window lives.
    window.close();
    window.load(url)?;
    let seen = wait_for_card(window)?;
    let text = seen["text"].as_str().unwrap_or("");
    if !text.contains("cookie[]") {
        return Err(format!("the cookie came back: {text}"));
    }
    Ok("the new window had no cookie".to_string())
}

fn popup_and_download_are_refused(app: &AppHandle, window: &mut BrowserWindow, url: &str) -> Result<String, String> {
    window.load(url)?;
    std::thread::sleep(Duration::from_secs(3));
    let others = windows_besides_the_app_and_the_browser(app);
    if others != 0 {
        return Err(format!("{others} other windows opened"));
    }
    // WebView2 blocks `window.open` that no click asked for, so this page cannot make a popup open: the denial of new windows is declared
    // in browser_window.rs and not exercised here. The download is: the script looks that no file was saved.
    Ok("no other window (the script checks that the download left no file)".to_string())
}

fn hung_page_is_replaced(window: &mut BrowserWindow, hang_url: &str, first_url: &str) -> Result<String, String> {
    window.load(hang_url)?;
    std::thread::sleep(Duration::from_millis(1500)); // the page starts its endless loop 300 ms after it loaded
    let began = Instant::now();
    if let Ok(value) = window.evaluate("1 + 1") {
        return Err(format!("a page that never answers answered: {value}"));
    }
    let waited = began.elapsed().as_millis();
    if waited > 8000 {
        return Err(format!("it took {waited} ms to give up"));
    }
    // What the agent does with a page that failed: close it, and the next load makes a new window.
    window.close();
    window.load(first_url)?;
    wait_for_card(window)?;
    Ok(format!("gave up after {waited} ms, the next window works"))
}

/// A close request that reaches the hidden window (a tool that closes windows, a stray WM_CLOSE) is not a request to close the app: the
/// window just goes, and the question to the page that the close gate keeps for the main window stays unasked. Leaves the gate listening,
/// as the real page does, so a gate that was used up shows (the page was asked although the user closed nothing).
fn close_request_on_the_hidden_window_is_harmless(app: &AppHandle, window: &mut BrowserWindow) -> Result<String, String> {
    let label = window.label().ok_or_else(|| "no window".to_string())?;
    let gate = app.state::<CloseGate>();
    gate.page_listens();
    window.request_close()?;
    std::thread::sleep(Duration::from_millis(500));
    let still_there = app.get_webview_window(&label).is_some();
    window.close(); // forget the handle (and finish the window off if it is still there)
    if still_there {
        return Err("the hidden window did not close when asked to".to_string());
    }
    if app.get_webview_window("main").is_none() {
        return Err("the main window is gone".to_string());
    }
    if gate.was_asked() {
        return Err("the close gate of the main window was used up: the page was asked to close the app".to_string());
    }
    Ok("the hidden window closed and the page of the main window was not asked anything".to_string())
}

fn verdict(name: &str, result: Result<String, String>) -> String {
    match result {
        Ok(detail) => format!("{name}=ok {detail}"),
        Err(seen) => format!("{name}=FAIL {seen}"),
    }
}

/// Runs every step against the fixture server at [base] (an origin such as `http://127.0.0.1:18765`). With [keep_open] a window is left
/// behind (on a good page), so that the caller can close the main window and see that the app ends all the same.
pub fn run(app: &AppHandle, base: &str, keep_open: bool) -> Vec<String> {
    let mut window = BrowserWindow::new(app.clone());
    let first = format!("{base}/first");
    let second = format!("{base}/second");
    let mut lines = vec![
        verdict("browserBuiltFast", built_fast(&mut window, &first)),
        verdict("timersRunHidden", timers_run_hidden(&mut window)),
        verdict("windowIsReused", window_is_reused(&mut window, &second)),
        verdict("cookieDoesNotSurviveTheWindow", cookie_does_not_survive_the_window(&mut window, &second)),
        verdict("popupAndDownloadAreRefused", popup_and_download_are_refused(app, &mut window, &format!("{base}/popup"))),
        verdict("hungPageIsReplaced", hung_page_is_replaced(&mut window, &format!("{base}/hang"), &first)),
        verdict("closeRequestOnTheHiddenWindowIsHarmless", close_request_on_the_hidden_window_is_harmless(app, &mut window)),
    ];
    if keep_open {
        // The window is not closed: it stays, as when the app is closed while the lookup uses it.
        lines.push(verdict("windowLeftOpen", window.load(&first).map(|()| "a window stays when the check ends".to_string())));
    } else {
        window.close();
    }
    lines
}

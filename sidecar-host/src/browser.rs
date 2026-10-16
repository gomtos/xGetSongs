//! The agent of the shell's hidden browser. The server (the sidecar) cannot open a web view itself, so it queues commands for
//! one (`BrowserChannel` in the engine): open an address, run a script, go back to the blank page. This agent takes them with a
//! long poll (`GET /browser/next`), has a [Page] carry them out and reports each (`POST /browser/answer`). It knows nothing about
//! Google: what to look for in the page, and when it is done, is decided on the server's side.
//!
//! The shell's release profile aborts on a panic, so nothing here may panic: every failure is a value.

use crate::bridge::Bridge;
use crate::error::BridgeError;
use serde::Deserialize;
use serde_json::{json, Value};
use std::time::{Duration, Instant};

/// The window the commands act on. The shell implements it with a hidden WebView2 window.
pub trait Page {
    /// Opens [url], making the window when there is none, and returns once the load has started.
    fn load(&mut self, url: &str) -> Result<(), String>;
    /// Runs [script] in the page and returns what WebView2 answers: the result as JSON text (`"\"abc\""` for a string, `null`).
    fn evaluate(&mut self, script: &str) -> Result<String, String>;
    /// Back to `about:blank`; the window stays.
    fn reset(&mut self) -> Result<(), String>;
    /// Destroys the window and everything it holds. Does nothing when there is none.
    fn close(&mut self);
}

#[derive(Debug, Clone, Copy)]
pub struct AgentSettings {
    /// How long one poll waits for a command (the server cuts it at 30 s).
    pub poll_wait: Duration,
    /// An opened page that has had no command for this long is closed, so that its processes are given back.
    pub idle_close: Duration,
    /// The pause after a poll that failed (the connection was lost).
    pub retry_pause: Duration,
}

impl Default for AgentSettings {
    fn default() -> AgentSettings {
        AgentSettings { poll_wait: Duration::from_secs(25), idle_close: Duration::from_secs(60), retry_pause: Duration::from_secs(1) }
    }
}

/// The commands of `shared/.../ApiModels.kt` (`BrowserCommand`); `contract/browser-protocol.json` holds examples of both sides.
#[derive(Debug, Deserialize, PartialEq)]
#[serde(tag = "type", rename_all = "lowercase")]
enum Command {
    Load { id: String, url: String },
    Evaluate { id: String, script: String },
    Reset { id: String },
}

/// The report on one command (`BrowserAnswer`).
#[derive(Debug, PartialEq)]
struct Answer {
    id: String,
    ok: bool,
    value: Option<String>,
    error: Option<String>,
}

impl Answer {
    fn ok(id: &str, value: Option<String>) -> Answer {
        Answer { id: id.to_string(), ok: true, value, error: None }
    }

    fn failed(id: &str, error: &str) -> Answer {
        Answer { id: id.to_string(), ok: false, value: None, error: Some(error.to_string()) }
    }

    fn to_json(&self) -> Value {
        json!({ "id": self.id, "ok": self.ok, "value": self.value, "error": self.error })
    }
}

/// Takes commands from the server and carries them out on [page] until [get_bridge] fails, which means the app is ending or the
/// sidecar will not come. A poll that fails (the connection was lost) is tried again after the pause.
pub fn serve(get_bridge: impl Fn() -> Result<Bridge, BridgeError>, page: &mut impl Page, settings: &AgentSettings) {
    let mut opened = false;
    let mut last_command = Instant::now();
    loop {
        let Ok(bridge) = get_bridge() else { return };
        match bridge.next_browser_command(settings.poll_wait) {
            Ok(Some(raw)) => {
                last_command = Instant::now();
                if let Some(answer) = handle(&raw, page, &mut opened) {
                    // A refusal (the server gave up waiting for it) or a lost connection changes nothing here: the next poll finds out.
                    let _ = bridge.answer_browser_command(&answer.to_json());
                }
            }
            Ok(None) => {
                if opened && last_command.elapsed() >= settings.idle_close {
                    page.close();
                    opened = false;
                }
            }
            Err(_) => std::thread::sleep(settings.retry_pause),
        }
    }
}

/// Carries out one command. None when it cannot even be answered (it has no `id`). A page that fails is closed: the next `load`
/// makes a new window, which is what a window stuck on a page that never answers needs.
fn handle(raw: &Value, page: &mut impl Page, opened: &mut bool) -> Option<Answer> {
    let Ok(command) = serde_json::from_value::<Command>(raw.clone()) else {
        let id = raw.get("id").and_then(Value::as_str)?;
        return Some(Answer::failed(id, "unknown command"));
    };
    let (id, result) = match command {
        Command::Load { id, url } => {
            if !allowed_url(&url) {
                return Some(Answer::failed(&id, "url not allowed"));
            }
            let result = page.load(&url).map(|()| {
                *opened = true;
                None
            });
            (id, result)
        }
        Command::Evaluate { id, script } => {
            let result = page.evaluate(&script).map(|text| decode_script_result(&text));
            (id, result)
        }
        Command::Reset { id } => {
            let result = page.reset().map(|()| None);
            (id, result)
        }
    };
    Some(match result {
        Ok(value) => Answer::ok(&id, value),
        Err(error) => {
            page.close();
            *opened = false;
            Answer::failed(&id, &error)
        }
    })
}

/// What the browser may open: `https://` addresses, and `http://` only to the machine itself (the fixed pages of the checks).
pub fn allowed_url(url: &str) -> bool {
    if url.chars().any(|character| character.is_control() || character.is_whitespace()) {
        return false;
    }
    let (secure, rest) = match (url.strip_prefix("https://"), url.strip_prefix("http://")) {
        (Some(rest), _) => (true, rest),
        (None, Some(rest)) => (false, rest),
        (None, None) => return false,
    };
    let authority = rest.split(['/', '?', '#']).next().unwrap_or("");
    if authority.is_empty() || authority.contains('@') {
        return false;
    }
    if secure {
        return true;
    }
    let host = match authority.rsplit_once(':') {
        Some((host, port)) if !port.is_empty() && port.chars().all(|character| character.is_ascii_digit()) => host,
        _ => authority,
    };
    host == "127.0.0.1" || host == "localhost"
}

/// Where a navigation may go: [allowed_url], and the blank page.
pub fn allowed_navigation(url: &str) -> bool {
    url == "about:blank" || allowed_url(url)
}

/// WebView2 answers a script with JSON text. A string is the text itself, `null` (and `undefined`) is nothing, anything else is kept as
/// the JSON it is. Text that is not JSON is nothing.
pub fn decode_script_result(json_text: &str) -> Option<String> {
    match serde_json::from_str::<Value>(json_text).ok()? {
        Value::Null => None,
        Value::String(text) => Some(text),
        other => Some(other.to_string()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fake_server::*;
    use crate::handshake::Handshake;
    use std::cell::Cell;
    use std::collections::VecDeque;

    const TOKEN: &str = "tok-browser-0123";
    const CONTRACT: &str = include_str!("../../contract/browser-protocol.json");

    /// A page that remembers what it was asked and answers from a script.
    #[derive(Default)]
    struct FakePage {
        calls: Vec<String>,
        evaluations: VecDeque<Result<String, String>>,
        load_error: Option<String>,
    }

    impl Page for FakePage {
        fn load(&mut self, url: &str) -> Result<(), String> {
            self.calls.push(format!("load {url}"));
            self.load_error.clone().map_or(Ok(()), Err)
        }

        fn evaluate(&mut self, script: &str) -> Result<String, String> {
            self.calls.push(format!("evaluate {script}"));
            self.evaluations.pop_front().unwrap_or_else(|| Ok("null".to_string()))
        }

        fn reset(&mut self) -> Result<(), String> {
            self.calls.push("reset".to_string());
            Ok(())
        }

        fn close(&mut self) {
            self.calls.push("close".to_string());
        }
    }

    /// A way to get the bridge that works [times] times and then says the app is over.
    fn bridge_times(server: &FakeServer, times: usize) -> impl Fn() -> Result<Bridge, BridgeError> {
        let port = server.port;
        let left = Cell::new(times);
        move || {
            if left.get() == 0 {
                return Err(BridgeError::new("the app ended"));
            }
            left.set(left.get() - 1);
            Ok(Bridge::new(Handshake { port, token: TOKEN.to_string() }))
        }
    }

    fn quick() -> AgentSettings {
        AgentSettings { poll_wait: Duration::from_millis(50), idle_close: Duration::from_secs(3600), retry_pause: Duration::from_millis(1) }
    }

    fn command_reply(command: &Value) -> Vec<Step> {
        json_reply(200, "OK", &command.to_string())
    }

    fn posted(server: &FakeServer, index: usize) -> Value {
        serde_json::from_slice(&server.requests()[index].body).unwrap()
    }

    // ---- which addresses the browser may open ----

    #[test]
    fn https_addresses_are_allowed() {
        assert!(allowed_url("https://www.google.com/search?q=a+b&hl=en"));
        assert!(allowed_url("https://consent.google.com"));
        assert!(!allowed_url("https://"));
        assert!(!allowed_url("https:///path"));
        assert!(!allowed_url("https://user@evil.example/"));
    }

    #[test]
    fn plain_http_is_allowed_for_the_loopback_only() {
        assert!(allowed_url("http://127.0.0.1:8080/x"));
        assert!(allowed_url("http://127.0.0.1/"));
        assert!(allowed_url("http://localhost/"));
        assert!(allowed_url("http://localhost:3000"));
        assert!(!allowed_url("http://www.google.com/"));
        assert!(!allowed_url("http://127.0.0.1.evil.com/"));
        assert!(!allowed_url("http://127.0.0.1@evil.com/"));
    }

    #[test]
    fn other_schemes_are_refused() {
        for url in ["file:///c:/windows/win.ini", "javascript:alert(1)", "data:text/html,x", "ftp://example.test/", "about:blank", "", "google.com"] {
            assert!(!allowed_url(url), "{url}");
        }
    }

    #[test]
    fn a_control_character_or_a_space_refuses_the_address() {
        for url in ["https://example.test/\n", "https://example.test/a b", "https://exa\tmple.test/"] {
            assert!(!allowed_url(url), "{url:?}");
        }
    }

    #[test]
    fn navigation_may_also_go_to_the_blank_page() {
        assert!(allowed_navigation("about:blank"));
        assert!(allowed_navigation("https://www.google.com/search?q=a"));
        assert!(!allowed_navigation("about:srcdoc"));
        assert!(!allowed_navigation("file:///c:/x"));
    }

    // ---- what a script gave ----

    #[test]
    fn a_string_result_loses_its_json_quotes_and_escapes() {
        assert_eq!(decode_script_result(r#""a\nb \"c\" 한글""#), Some("a\nb \"c\" 한글".to_string()));
    }

    #[test]
    fn a_null_result_is_none() {
        assert_eq!(decode_script_result("null"), None);
    }

    #[test]
    fn another_json_value_is_kept_as_text() {
        assert_eq!(decode_script_result("42"), Some("42".to_string()));
        assert_eq!(decode_script_result(r#"{"a":1}"#), Some(r#"{"a":1}"#.to_string()));
    }

    #[test]
    fn text_that_is_not_json_is_none() {
        assert_eq!(decode_script_result("not json"), None);
        assert_eq!(decode_script_result(""), None);
    }

    // ---- the contract file the server's tests read too ----

    #[test]
    fn the_commands_of_the_contract_file_parse() {
        let contract: Value = serde_json::from_str(CONTRACT).unwrap();
        let commands: Vec<Command> =
            contract["commands"].as_array().unwrap().iter().map(|entry| serde_json::from_value(entry.clone()).unwrap()).collect();

        assert_eq!(
            commands,
            vec![
                Command::Load { id: "c-1".into(), url: "https://www.google.com/search?q=IU+Love+poem+lyrics&hl=en".into() },
                Command::Evaluate { id: "c-2".into(), script: "document.title".into() },
                Command::Reset { id: "c-3".into() },
            ]
        );
    }

    #[test]
    fn the_answers_of_the_contract_file_are_what_the_agent_writes() {
        let contract: Value = serde_json::from_str(CONTRACT).unwrap();
        let answers = contract["answers"].as_array().unwrap();

        assert_eq!(answers.len(), 3);
        assert_eq!(Answer::ok("c-1", None).to_json(), answers[0]);
        assert_eq!(Answer::ok("c-2", Some("한글 \"title\"\n".to_string())).to_json(), answers[1]);
        assert_eq!(Answer::failed("c-3", "window gone").to_json(), answers[2]);
    }

    // ---- the loop ----

    #[test]
    fn a_load_command_opens_the_page_and_is_answered_ok() {
        let server = FakeServer::start(vec![
            command_reply(&json!({"type":"load","id":"c-1","url":"https://www.google.com/search?q=x"})),
            empty_reply(204, "No Content"),
        ]);
        let mut page = FakePage::default();

        serve(bridge_times(&server, 1), &mut page, &quick());

        assert_eq!(page.calls, ["load https://www.google.com/search?q=x"]);
        let requests = server.requests();
        assert!(requests[0].head.starts_with("get /browser/next?wait=50 http/1.1"), "{}", requests[0].head);
        assert!(requests[1].head.starts_with("post /browser/answer http/1.1"), "{}", requests[1].head);
        assert!(requests[1].head.contains(&format!("x-xgs-token: {TOKEN}")), "{}", requests[1].head);
        assert_eq!(posted(&server, 1), json!({"id":"c-1","ok":true,"value":null,"error":null}));
    }

    #[test]
    fn an_evaluate_command_answers_the_decoded_value() {
        let server = FakeServer::start(vec![
            command_reply(&json!({"type":"evaluate","id":"c-2","script":"document.title"})),
            empty_reply(204, "No Content"),
        ]);
        let mut page = FakePage::default();
        page.evaluations.push_back(Ok(serde_json::to_string("two\n한글").unwrap()));

        serve(bridge_times(&server, 1), &mut page, &quick());

        assert_eq!(page.calls, ["evaluate document.title"]);
        assert_eq!(posted(&server, 1), json!({"id":"c-2","ok":true,"value":"two\n한글","error":null}));
    }

    #[test]
    fn a_command_the_page_refuses_is_answered_with_its_error_and_the_page_is_closed() {
        let server = FakeServer::start(vec![
            command_reply(&json!({"type":"evaluate","id":"c-3","script":"while(true){}"})),
            empty_reply(204, "No Content"),
        ]);
        let mut page = FakePage::default();
        page.evaluations.push_back(Err("script timed out".to_string()));

        serve(bridge_times(&server, 1), &mut page, &quick());

        assert_eq!(posted(&server, 1), json!({"id":"c-3","ok":false,"value":null,"error":"script timed out"}));
        assert_eq!(page.calls, ["evaluate while(true){}", "close"]);
    }

    #[test]
    fn a_load_of_a_forbidden_address_is_refused_without_touching_the_page() {
        let server = FakeServer::start(vec![
            command_reply(&json!({"type":"load","id":"c-4","url":"file:///c:/windows/win.ini"})),
            empty_reply(204, "No Content"),
        ]);
        let mut page = FakePage::default();

        serve(bridge_times(&server, 1), &mut page, &quick());

        assert_eq!(posted(&server, 1), json!({"id":"c-4","ok":false,"value":null,"error":"url not allowed"}));
        assert!(page.calls.is_empty(), "{:?}", page.calls);
    }

    #[test]
    fn a_command_of_an_unknown_type_is_answered_with_an_error() {
        let server = FakeServer::start(vec![
            command_reply(&json!({"type":"explode","id":"c-9"})),
            empty_reply(204, "No Content"),
            command_reply(&json!({"type":"explode"})),
        ]);
        let mut page = FakePage::default();

        serve(bridge_times(&server, 2), &mut page, &quick());

        assert_eq!(posted(&server, 1), json!({"id":"c-9","ok":false,"value":null,"error":"unknown command"}));
        assert_eq!(server.requests().len(), 3, "no id, nobody to answer: only the two polls and one answer");
        assert!(page.calls.is_empty(), "{:?}", page.calls);
    }

    #[test]
    fn an_idle_page_is_closed_after_the_idle_time() {
        let server = FakeServer::start(vec![
            command_reply(&json!({"type":"load","id":"c-1","url":"https://www.google.com/"})),
            empty_reply(204, "No Content"),
            empty_reply(204, "No Content"),
        ]);
        let mut page = FakePage::default();
        let settings = AgentSettings { idle_close: Duration::ZERO, ..quick() };

        serve(bridge_times(&server, 2), &mut page, &settings);

        assert_eq!(page.calls, ["load https://www.google.com/", "close"]);
    }

    #[test]
    fn a_page_that_was_never_opened_is_not_closed_when_idle() {
        let server = FakeServer::start(vec![empty_reply(204, "No Content"), empty_reply(204, "No Content")]);
        let mut page = FakePage::default();
        let settings = AgentSettings { idle_close: Duration::ZERO, ..quick() };

        serve(bridge_times(&server, 2), &mut page, &settings);

        assert!(page.calls.is_empty(), "{:?}", page.calls);
        assert_eq!(server.requests().len(), 2);
    }

    #[test]
    fn the_loop_ends_when_the_server_is_not_coming_back() {
        let server = FakeServer::start(Vec::new());
        let mut page = FakePage::default();

        serve(bridge_times(&server, 0), &mut page, &quick());

        assert!(server.requests().is_empty());
        assert!(page.calls.is_empty());
    }

    #[test]
    fn a_lost_connection_is_retried_after_the_pause() {
        let server = FakeServer::start(vec![Vec::new(), empty_reply(204, "No Content")]);
        let mut page = FakePage::default();

        serve(bridge_times(&server, 2), &mut page, &quick());

        assert_eq!(server.requests().len(), 2, "the first poll was cut off, the second one went through");
    }
}

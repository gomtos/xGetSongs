//! The calls the page can make to the server, and nothing else. Each is one HTTP request to the sidecar with the token the
//! page never sees; what comes back is the server's JSON as it is (the shapes are `shared/.../ApiModels.kt`), and a failure
//! is one `BridgeError` with a message fit for the user.

use crate::error::{BridgeError, BAD_JOB_ID, LISTENER_GONE, UNREADABLE_ANSWER, UNREADABLE_EVENT};
use crate::handshake::Handshake;
use crate::sse::SseParser;
use serde_json::{json, Value};
use std::io::Read;
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
}

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

use crate::handshake::Handshake;
use std::time::Duration;

/// GET `http://127.0.0.1:<port><path>` with the sidecar's token. The body on success, a message without the token otherwise.
pub fn http_get(handshake: &Handshake, path: &str) -> Result<String, String> {
    let url = format!("http://127.0.0.1:{}{}", handshake.port, path);
    ureq::get(&url)
        .set("X-XGS-Token", &handshake.token)
        .timeout(Duration::from_secs(30))
        .call()
        .map_err(|error| error.to_string())?
        .into_string()
        .map_err(|error| error.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};
    use std::net::TcpListener;

    /// A one-shot server: answers the first request with [response] and returns the request it got, in lower case.
    fn serve_once(response: &'static str) -> (u16, std::thread::JoinHandle<String>) {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let handle = std::thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            let mut request = Vec::new();
            let mut buffer = [0u8; 512];
            while !request.windows(4).any(|window| window == b"\r\n\r\n") {
                let read = stream.read(&mut buffer).unwrap();
                if read == 0 {
                    break;
                }
                request.extend_from_slice(&buffer[..read]);
            }
            stream.write_all(response.as_bytes()).unwrap();
            String::from_utf8_lossy(&request).to_lowercase()
        });
        (port, handle)
    }

    #[test]
    fn sends_the_token_header_to_the_loopback_port_and_returns_the_body() {
        let (port, server) = serve_once("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");

        let body = http_get(&Handshake { port, token: "tok".to_string() }, "/tools").unwrap();

        assert_eq!(body, "ok");
        let request = server.join().unwrap();
        assert!(request.starts_with("get /tools http/1.1"), "{request}");
        assert!(request.contains("x-xgs-token: tok"), "{request}");
    }

    #[test]
    fn an_error_status_is_an_error() {
        let (port, server) =
            serve_once("HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");

        let result = http_get(&Handshake { port, token: "wrong".to_string() }, "/tools");

        assert!(result.is_err());
        let message = result.unwrap_err();
        assert!(!message.contains("wrong"), "the token is not repeated in the error: {message}");
        server.join().unwrap();
    }
}

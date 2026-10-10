/// One event of a server-sent-events stream: its `event:` name (empty when the stream did not name it) and its `data:`
/// lines joined with `\n`.
#[derive(Debug, Clone, PartialEq)]
pub struct SseEvent {
    pub name: String,
    pub data: String,
}

/// Cuts the bytes of an SSE stream into events. Bytes may come in any pieces: a line is decoded only when it is complete, so
/// a character or a `\r\n` that is split between two reads comes out whole.
#[derive(Default)]
pub struct SseParser {
    /// The start of a line that has not ended yet.
    pending: Vec<u8>,
    name: String,
    /// `None` until the event under construction has a `data:` line.
    data: Option<String>,
}

impl SseParser {
    pub fn new() -> SseParser {
        SseParser::default()
    }

    /// Feeds [bytes] of the stream and returns the events they finish.
    pub fn push(&mut self, bytes: &[u8]) -> Vec<SseEvent> {
        let mut buffer = std::mem::take(&mut self.pending);
        buffer.extend_from_slice(bytes);
        let mut events = Vec::new();
        let mut start = 0;
        let mut index = 0;
        while index < buffer.len() {
            let byte = buffer[index];
            if byte != b'\n' && byte != b'\r' {
                index += 1;
                continue;
            }
            if byte == b'\r' && index + 1 == buffer.len() {
                break; // a CR at the very end may be the first half of CRLF: wait for the next byte
            }
            let next = if byte == b'\r' && buffer[index + 1] == b'\n' { index + 2 } else { index + 1 };
            if let Some(event) = self.line(&buffer[start..index]) {
                events.push(event);
            }
            start = next;
            index = next;
        }
        self.pending = buffer.split_off(start);
        events
    }

    fn line(&mut self, line: &[u8]) -> Option<SseEvent> {
        if line.is_empty() {
            // A blank line ends the event. One without a data line is not an event.
            let name = std::mem::take(&mut self.name);
            return self.data.take().map(|data| SseEvent { name, data });
        }
        if line[0] == b':' {
            return None; // a comment (servers send them to keep a stream alive)
        }
        let (field, value) = match line.iter().position(|byte| *byte == b':') {
            Some(colon) => {
                let value = &line[colon + 1..];
                (&line[..colon], value.strip_prefix(b" ").unwrap_or(value))
            }
            None => (line, &line[line.len()..]),
        };
        let value = String::from_utf8_lossy(value).into_owned();
        match field {
            b"event" => self.name = value,
            b"data" => match &mut self.data {
                Some(data) => {
                    data.push('\n');
                    data.push_str(&value);
                }
                None => self.data = Some(value),
            },
            _ => {} // `id`, `retry` and unknown fields are of no use to the bridge
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn event(name: &str, data: &str) -> SseEvent {
        SseEvent { name: name.to_string(), data: data.to_string() }
    }

    fn parse_all(bytes: &[u8]) -> Vec<SseEvent> {
        SseParser::new().push(bytes)
    }

    #[test]
    fn it_reads_an_event_with_crlf_line_ends() {
        assert_eq!(
            parse_all("event: progress\r\ndata: {\"rank\":1}\r\n\r\n".as_bytes()),
            vec![event("progress", "{\"rank\":1}")]
        );
    }

    #[test]
    fn it_reads_an_event_with_lf_line_ends() {
        assert_eq!(parse_all(b"event: a\ndata: b\n\n"), vec![event("a", "b")]);
    }

    #[test]
    fn it_reads_several_events_from_one_push() {
        assert_eq!(
            parse_all(b"event: a\ndata: 1\n\nevent: b\ndata: 2\n\n"),
            vec![event("a", "1"), event("b", "2")]
        );
    }

    #[test]
    fn the_same_events_come_out_wherever_the_bytes_are_cut() {
        let stream = "event: item-started\r\ndata: {\"fileName\":\"001 가수 - 제목.m4a\"}\r\ndata: 둘째 줄\r\n\r\n: ping\r\n\r\nevent: job-done\r\ndata: {}\r\n\r\n"
            .as_bytes();
        let whole = parse_all(stream);
        assert_eq!(whole.len(), 2);

        for cut in 0..=stream.len() {
            let mut parser = SseParser::new();
            let mut events = parser.push(&stream[..cut]);
            events.extend(parser.push(&stream[cut..]));
            assert_eq!(events, whole, "cut at byte {cut}");
        }
    }

    #[test]
    fn a_crlf_split_between_two_pushes_is_one_line_end() {
        // Read as two line ends, the CR and the LF would put a blank line between the two data lines and cut the event in two.
        let mut parser = SseParser::new();
        let mut events = parser.push(b"event: a\r\ndata: 1\r");
        events.extend(parser.push(b"\ndata: 2\r\n\r\n"));

        assert_eq!(events, vec![event("a", "1\n2")]);
    }

    #[test]
    fn data_lines_are_joined_with_a_line_feed() {
        assert_eq!(parse_all(b"data: one\ndata: two\n\n"), vec![event("", "one\ntwo")]);
    }

    #[test]
    fn comments_and_unknown_fields_are_ignored() {
        assert_eq!(parse_all(b": keep-alive\nid: 7\nretry: 100\nevent: a\ndata: 1\n\n"), vec![event("a", "1")]);
    }

    #[test]
    fn a_space_after_the_colon_is_optional() {
        assert_eq!(parse_all(b"event:a\ndata:{\"x\":1}\n\n"), vec![event("a", "{\"x\":1}")]);
    }

    #[test]
    fn an_event_without_a_name_has_an_empty_name() {
        assert_eq!(parse_all(b"data: 1\n\n"), vec![event("", "1")]);
    }

    #[test]
    fn a_block_without_data_is_not_an_event_and_does_not_leak_its_name_into_the_next() {
        assert_eq!(parse_all(b"event: lonely\n\ndata: 1\n\n"), vec![event("", "1")]);
    }

    #[test]
    fn an_event_that_never_got_its_blank_line_is_not_delivered() {
        assert_eq!(parse_all(b"event: a\ndata: 1\n"), Vec::<SseEvent>::new());
    }

    #[test]
    fn bytes_that_are_not_utf8_do_not_stop_the_parser() {
        let events = parse_all(b"event: a\ndata: \xff\xfe\n\nevent: b\ndata: 2\n\n");

        assert_eq!(events.len(), 2);
        assert_eq!(events[1], event("b", "2"));
    }
}

/// The first word of the line the sidecar prints when its server listens.
pub const HANDSHAKE_PREFIX: &str = "XGS-READY";

#[derive(Debug, Clone, PartialEq)]
pub struct Handshake {
    pub port: u16,
    pub token: String,
}

/// Reads `XGS-READY <port> <token>`, exactly. Anything else (a log line, a JVM warning, a line that only contains the
/// prefix somewhere in the middle) is not a handshake.
pub fn parse_handshake(line: &str) -> Option<Handshake> {
    let mut parts = line.split_whitespace();
    if parts.next()? != HANDSHAKE_PREFIX {
        return None;
    }
    let port = parts.next()?.parse::<u16>().ok().filter(|port| *port != 0)?;
    let token = parts.next()?.to_string();
    if parts.next().is_some() {
        return None;
    }
    Some(Handshake { port, token })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_the_port_and_the_token() {
        assert_eq!(
            parse_handshake("XGS-READY 51234 abc_DEF-123"),
            Some(Handshake { port: 51234, token: "abc_DEF-123".to_string() })
        );
    }

    #[test]
    fn tolerates_the_line_ending() {
        assert_eq!(
            parse_handshake("XGS-READY 80 t\r\n"),
            Some(Handshake { port: 80, token: "t".to_string() })
        );
    }

    #[test]
    fn a_line_that_does_not_start_with_the_prefix_is_not_a_handshake() {
        assert_eq!(parse_handshake("12:00:00.000 INFO  LocalServer - XGS-READY 80 t"), None);
        assert_eq!(parse_handshake("[0.002s][warning][cds] something about sharing"), None);
        assert_eq!(parse_handshake(""), None);
    }

    #[test]
    fn a_bad_port_is_refused() {
        assert_eq!(parse_handshake("XGS-READY 0 t"), None);
        assert_eq!(parse_handshake("XGS-READY 70000 t"), None);
        assert_eq!(parse_handshake("XGS-READY abc t"), None);
    }

    #[test]
    fn missing_or_extra_fields_are_refused() {
        assert_eq!(parse_handshake("XGS-READY"), None);
        assert_eq!(parse_handshake("XGS-READY 80"), None);
        assert_eq!(parse_handshake("XGS-READY 80 t extra"), None);
    }
}

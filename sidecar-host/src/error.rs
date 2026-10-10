use serde::Serialize;
use std::fmt;

/// The server cannot be reached, or it stopped answering.
pub const LOST: &str = "서버와의 연결이 끊어졌습니다.";
/// The server answered with something that is not the JSON the contract describes.
pub const UNREADABLE_ANSWER: &str = "서버 응답을 해석할 수 없습니다.";
/// An event of a job's stream carried data that is not JSON.
pub const UNREADABLE_EVENT: &str = "서버 이벤트를 해석할 수 없습니다.";
/// A job ID that cannot be one the server made (see `check_job_id` in bridge.rs).
pub const BAD_JOB_ID: &str = "작업 번호가 올바르지 않습니다.";
/// The page that was to receive the events is gone.
pub const LISTENER_GONE: &str = "이벤트를 받는 화면이 없어 중단했습니다.";

/// What a bridge call reports when it fails: one message fit for the user, in the shape of the server's own
/// `ErrorResponse` (`{"message": ...}`). It never holds the token.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct BridgeError {
    pub message: String,
}

impl BridgeError {
    pub fn new(message: impl Into<String>) -> BridgeError {
        BridgeError { message: message.into() }
    }

    pub fn lost() -> BridgeError {
        BridgeError::new(LOST)
    }
}

impl fmt::Display for BridgeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.message)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_serializes_like_the_servers_error_response() {
        let json = serde_json::to_string(&BridgeError::new("작업을 찾을 수 없습니다.")).unwrap();

        assert_eq!(json, r#"{"message":"작업을 찾을 수 없습니다."}"#);
    }

    #[test]
    fn it_displays_as_its_message() {
        assert_eq!(BridgeError::lost().to_string(), LOST);
    }
}

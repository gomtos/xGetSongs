pub mod bridge;
pub mod config;
pub mod error;
#[cfg(test)]
mod fake_server;
pub mod handshake;
pub mod http;
pub mod job;
pub mod sidecar;
pub mod sse;

pub use bridge::Bridge;
pub use config::SidecarConfig;
pub use error::BridgeError;
pub use handshake::{parse_handshake, Handshake};
pub use http::http_get;
pub use sidecar::Sidecar;

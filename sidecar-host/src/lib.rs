pub mod bridge;
pub mod config;
pub mod error;
pub mod folder;
pub mod handshake;
pub mod job;
pub mod lifecycle;
pub mod settings;
pub mod sidecar;
pub mod sse;

#[cfg(test)]
mod fake_server;

pub use bridge::Bridge;
pub use config::SidecarConfig;
pub use error::BridgeError;
pub use handshake::{parse_handshake, Handshake};
pub use lifecycle::Lifecycle;
pub use settings::{SettingsFile, UserSettings};
pub use sidecar::Sidecar;

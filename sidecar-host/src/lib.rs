pub mod bridge;
pub mod close_gate;
pub mod config;
pub mod error;
pub mod folder;
pub mod handshake;
pub mod job;
pub mod lifecycle;
pub mod payload;
pub mod settings;
pub mod sidecar;
pub mod sse;
pub mod webview2;

#[cfg(test)]
mod fake_server;

pub use bridge::Bridge;
pub use close_gate::{CloseDecision, CloseGate};
pub use config::{app_data_dir, SidecarConfig};
pub use error::BridgeError;
pub use handshake::{parse_handshake, Handshake};
pub use lifecycle::Lifecycle;
pub use settings::{SettingsFile, UserSettings};
pub use sidecar::Sidecar;

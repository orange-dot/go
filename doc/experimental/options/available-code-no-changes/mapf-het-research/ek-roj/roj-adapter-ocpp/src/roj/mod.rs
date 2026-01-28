//! ROJ protocol integration
//!
//! This module bridges OCPP messages to ROJ consensus:
//! - `translator`: OCPP <-> ROJ message translation
//! - `state_bridge`: Query ROJ state for OCPP responses

pub mod translator;
pub mod state_bridge;

pub use translator::{OcppToRoj, RojToOcpp, CommitEffect};
pub use state_bridge::StateBridge;

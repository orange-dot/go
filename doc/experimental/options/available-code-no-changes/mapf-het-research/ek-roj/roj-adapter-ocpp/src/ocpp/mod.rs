//! OCPP 2.0.1 protocol implementation
//!
//! This module provides the OCPP protocol layer for the ROJ adapter:
//! - `types`: OCPP message types and data structures
//! - `messages`: JSON-RPC framing (CALL, CALLRESULT, CALLERROR)
//! - `client`: WebSocket client for CSMS connection
//! - `session`: Session state machine for CP lifecycle

pub mod types;
pub mod messages;
pub mod client;
pub mod session;

pub use types::*;
pub use messages::*;
pub use client::{OcppClient, OcppClientConfig, IncomingRequest};
pub use session::{Session, SessionState, SessionEvent};

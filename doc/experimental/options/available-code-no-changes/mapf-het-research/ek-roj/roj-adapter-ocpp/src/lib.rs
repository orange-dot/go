//! # ROJ OCPP Adapter
//!
//! OCPP 2.0.1 adapter for the ROJ distributed consensus protocol.
//!
//! This crate bridges OCPP (Open Charge Point Protocol) commands from a Central System
//! Management System (CSMS) to ROJ distributed consensus, enabling federated control
//! of EV charging infrastructure.
//!
//! ## Architecture
//!
//! ```text
//! OCPP CSMS (Backend)
//!       │ WebSocket JSON-RPC
//!       ▼
//! ┌─────────────────────────────────┐
//! │    roj-adapter-ocpp             │
//! │  ┌───────────┐  ┌────────────┐  │
//! │  │ OCPP WS   │◄►│ Translator │  │
//! │  │ Client    │  │ OCPP↔ROJ   │  │
//! │  └───────────┘  └────────────┘  │
//! └─────────────┬───────────────────┘
//!               │ UDP JSON
//!               ▼
//! ┌─────────────────────────────────┐
//! │    roj-core-rs                  │
//! │  Discovery │ Transport │ Consensus
//! └─────────────────────────────────┘
//! ```
//!
//! ## OCPP → ROJ Mapping
//!
//! | OCPP Action | ROJ Key | Consensus? |
//! |-------------|---------|------------|
//! | SetChargingProfile | `power_limit:{station}:{evse}` | Yes |
//! | RequestStartTransaction | `session:{station}:{evse}` | Yes |
//! | RequestStopTransaction | `session:{station}:{evse}` | Yes |
//! | ReserveNow | `reservation:{station}:{evse}` | Yes |
//! | CancelReservation | `reservation:{station}:{evse}` | Yes |
//! | BootNotification | (ANNOUNCE) | No |
//!
//! ## Usage
//!
//! ```no_run
//! use roj_adapter_ocpp::{Adapter, AdapterConfig};
//!
//! #[tokio::main]
//! async fn main() -> Result<(), Box<dyn std::error::Error>> {
//!     let config = AdapterConfig::new(
//!         "alpha",
//!         "CS001",
//!         "ws://localhost:8180/steve/websocket/CentralSystemService",
//!     );
//!
//!     let adapter = Adapter::new(config).await?;
//!     adapter.run().await?;
//!
//!     Ok(())
//! }
//! ```
//!
//! ## Demo Scenario
//!
//! 1. CSMS sends SetChargingProfile to node "alpha"
//! 2. Alpha translates to ROJ PROPOSE: `power_limit:CS001:1 = {limit_kw: 15}`
//! 3. Beta and Gamma receive PROPOSE, vote ACCEPT
//! 4. Alpha sees 2/3 threshold reached, broadcasts COMMIT
//! 5. All nodes apply committed power limit
//! 6. Alpha sends StatusNotification back to CSMS

pub mod ocpp;
pub mod roj;
pub mod config;
pub mod adapter;

pub use config::AdapterConfig;
pub use adapter::{Adapter, AdapterBuilder};

// Re-export key types
pub use ocpp::{
    Action, Call, CallResult, OcppClient, OcppClientConfig,
    ConnectorStatus, ChargingProfile, GenericStatus,
};
pub use roj::{OcppToRoj, RojToOcpp, StateBridge};

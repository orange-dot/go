//! ROJ Core Library
//!
//! Distributed consensus protocol for modular EV charging infrastructure.
//! Implements:
//! - K-threshold voting with mDNS discovery and UDP transport
//! - Raft-style leader election (<100ms target)
//! - Log replication with persistence
//! - Partition handling with epoch reconciliation
//! - Stigmergy-based thermal optimization (88% variance reduction target)
//!
//! ## Features
//!
//! - `elle` - Enable Elle history recording for consistency testing

pub mod types;
pub mod discovery;
pub mod transport;
pub mod consensus;
pub mod election;
pub mod log;
pub mod storage;
pub mod partition;
pub mod stigmergy;

// Elle history recording (feature-gated)
#[cfg(feature = "elle")]
pub mod history;

pub use types::*;
pub use discovery::Discovery;
pub use transport::Transport;
pub use consensus::Consensus;
pub use election::{Election, ElectionMessage, Role, PersistentState};
pub use log::{ReplicatedLog, LogEntry, LogMessage, Snapshot};
pub use storage::{Storage, StorageConfig, RecoveredState, MemoryStorage};
pub use partition::{PartitionHandler, PartitionState, PartitionMessage, Epoch};
pub use stigmergy::{StigmergyController, StigmergyMessage, ThermalTag, PowerAdjustment};

#[cfg(feature = "elle")]
pub use history::{HistoryRecorder, ElleEvent, ElleOp, EventType, key_to_numeric, value_to_numeric};

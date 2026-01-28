//! # EK-KOR v2: Field-Centric Coordination RTOS
//!
//! A novel real-time operating system for distributed coordination
//! of modular power electronics (EK3 charger modules).
//!
//! ## Key Innovations
//!
//! ### 1. Potential Field Scheduling
//!
//! No central scheduler. Modules publish decaying gradient fields;
//! neighbors sample and self-organize based on field gradients.
//!
//! ```ignore
//! // Each module publishes its state
//! module.update_field(load, thermal, power);
//!
//! // Kernel computes gradients automatically
//! let gradient = module.get_gradient(FieldComponent::Load);
//! if gradient > threshold {
//!     // Neighbors are overloaded, I should take more work
//! }
//! ```
//!
//! ### 2. Topological k-Neighbor Coordination
//!
//! Each module tracks exactly k=7 neighbors regardless of physical topology.
//! Enables scale-free coordination from 3 to 909 modules.
//!
//! ### 3. Threshold Consensus
//!
//! Distributed voting with density-dependent thresholds.
//! Supermajority for safety-critical decisions.
//!
//! ### 4. Adaptive Mesh Reformation
//!
//! Kernel-integrated failure detection with automatic neighbor reelection.
//!
//! ## Quick Start
//!
//! ```ignore
//! use ekk::prelude::*;
//!
//! let mut module = Module::new(42, "charger-42", Position::new(1, 2, 0));
//! module.add_task("charge", charge_fn, ptr::null_mut(), 0, 1000)?;
//! module.start()?;
//!
//! loop {
//!     let now = hal.time_us();
//!     module.tick(&mut field_region, now)?;
//! }
//! ```
//!
//! ## Patent Claims
//!
//! 1. "A distributed real-time operating system using potential field
//!    scheduling wherein processing elements coordinate through indirect
//!    communication via shared decaying gradient fields"
//!
//! 2. "A topological coordination protocol for modular power electronics
//!    where each module maintains fixed-cardinality neighbor relationships
//!    independent of physical network topology"
//!
//! 3. "A threshold-based consensus mechanism for mixed-criticality embedded
//!    systems using density-dependent activation functions"
//!
//! ## References
//!
//! - Khatib, O. (1986): Real-time obstacle avoidance using potential fields
//! - Cavagna, A. & Giardina, I. (2010): Scale-free correlations in starlings
//! - Vicsek, T. (1995): Self-propelled particle model
//!
//! ## License
//!
//! MIT License - Copyright (c) 2026 Elektrokombinacija

#![cfg_attr(not(feature = "std"), no_std)]
#![warn(missing_docs)]
#![warn(rustdoc::missing_doc_code_examples)]

// Core modules
pub mod types;
pub mod field;
pub mod topology;
pub mod consensus;
pub mod heartbeat;
pub mod module;
pub mod hal;
pub mod gossip;

// Re-exports for convenience
pub use types::*;
pub use field::{FieldEngine, FieldRegion, DecayModel, FieldConfig};
pub use topology::{Topology, TopologyConfig, DistanceMetric, DiscoveryMessage};
pub use consensus::{Consensus, ConsensusConfig, Ballot, ProposalType, VoteMessage, ProposalMessage};
pub use heartbeat::{Heartbeat, HeartbeatConfig, HeartbeatMessage};
pub use module::{Module, ModuleCallbacks, ModuleStatus, InternalTask, TaskState, TaskFn};
pub use hal::{Hal, MsgType, ReceivedMessage, CriticalSection};
pub use gossip::{GossipContext, VersionVector, VVOrder, EventV2, LWWTimestamp, GossipStats};

/// Prelude - commonly used items
pub mod prelude {
    pub use crate::types::*;
    pub use crate::field::{FieldEngine, FieldRegion};
    pub use crate::topology::Topology;
    pub use crate::consensus::Consensus;
    pub use crate::heartbeat::Heartbeat;
    pub use crate::module::Module;
    pub use crate::hal::Hal;
    pub use crate::gossip::{GossipContext, VersionVector};
}

// ============================================================================
// Version
// ============================================================================

/// Major version
pub const VERSION_MAJOR: u32 = 2;
/// Minor version
pub const VERSION_MINOR: u32 = 0;
/// Patch version
pub const VERSION_PATCH: u32 = 0;
/// Version string
pub const VERSION_STRING: &str = "2.0.0";

/// Get version as packed integer (major << 16 | minor << 8 | patch)
pub const fn version() -> u32 {
    (VERSION_MAJOR << 16) | (VERSION_MINOR << 8) | VERSION_PATCH
}

// ============================================================================
// System Initialization
// ============================================================================

/// Global field region
///
/// In embedded systems, this would be in a known memory location.
/// For std, we use a static.
#[cfg(feature = "std")]
static mut FIELD_REGION: Option<FieldRegion> = None;

/// Initialize EK-KOR v2 system
///
/// Call once at startup before creating modules.
#[cfg(feature = "std")]
pub fn init() -> Result<()> {
    unsafe {
        FIELD_REGION = Some(FieldRegion::new());
    }
    Ok(())
}

/// Get global field region
#[cfg(feature = "std")]
pub fn get_field_region() -> &'static mut FieldRegion {
    unsafe { FIELD_REGION.as_mut().expect("EKK not initialized") }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_version() {
        assert_eq!(version(), 0x020000);
        assert_eq!(VERSION_STRING, "2.0.0");
    }

    #[test]
    fn test_init() {
        init().unwrap();
        let region = get_field_region();
        assert!(region.get(1).is_some());
    }
}

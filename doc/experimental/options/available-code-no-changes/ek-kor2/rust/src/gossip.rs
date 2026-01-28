//! # Event Gossip Protocol for Federated Event Sourcing
//!
//! Epidemic gossip protocol for event synchronization between modules.
//! Based on version vectors for causal ordering and LWW for conflict resolution.
//!
//! ## Key Features
//!
//! - **Version Vectors**: Causal event ordering without centralized clocks
//! - **Last-Writer-Wins (LWW)**: Deterministic conflict resolution
//! - **k-Neighbor Gossip**: Matches EKK topology (k=7)
//! - **Hop-Limited Propagation**: TTL prevents infinite loops
//! - **Gap Detection**: Automatic recovery of missing events
//!
//! ## Example
//!
//! ```ignore
//! use ekk::gossip::{GossipContext, VersionVector};
//!
//! let mut ctx = GossipContext::new(1);  // Module ID = 1
//! ctx.add_neighbor(2);
//! ctx.add_neighbor(3);
//!
//! // Emit local event
//! ctx.emit(0x01, &[1, 2, 3, 4]).unwrap();
//!
//! // Periodic tick sends pending events
//! ctx.tick(1000000);  // 1 second
//! ```

use crate::types::{ModuleId, TimeUs, Result, Error};

/// Maximum entries in version vector
pub const VV_MAX_ENTRIES: usize = 8;

/// Maximum hops for event propagation (TTL)
pub const GOSSIP_MAX_HOPS: u8 = 3;

/// Gossip tick interval in microseconds
pub const GOSSIP_TICK_US: u64 = 10_000;

/// Maximum events per gossip batch
pub const GOSSIP_BATCH_SIZE: usize = 2;

// ============================================================================
// Version Vector
// ============================================================================

/// Version vector entry
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct VVEntry {
    /// Module this entry tracks
    pub module_id: ModuleId,
    /// Highest sequence seen from this module
    pub sequence: u32,
}

/// Version vector for causal ordering
///
/// Sparse representation - only tracks modules we've seen events from.
#[derive(Debug, Clone, Default)]
pub struct VersionVector {
    entries: [VVEntry; VV_MAX_ENTRIES],
    count: usize,
}

/// Version vector comparison result
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VVOrder {
    /// Identical vectors
    Equal,
    /// Self happened-before other
    Before,
    /// Other happened-before self
    After,
    /// Concurrent (neither dominates)
    Concurrent,
}

impl VersionVector {
    /// Create empty version vector
    pub fn new() -> Self {
        Self::default()
    }

    /// Get sequence for a module
    pub fn get(&self, module_id: ModuleId) -> u32 {
        for i in 0..self.count {
            if self.entries[i].module_id == module_id {
                return self.entries[i].sequence;
            }
        }
        0
    }

    /// Set/update sequence for a module
    pub fn set(&mut self, module_id: ModuleId, seq: u32) -> Result<()> {
        // Find existing entry
        for i in 0..self.count {
            if self.entries[i].module_id == module_id {
                self.entries[i].sequence = seq;
                return Ok(());
            }
        }

        // Add new entry
        if self.count >= VV_MAX_ENTRIES {
            return Err(Error::NoMemory);
        }

        self.entries[self.count] = VVEntry {
            module_id,
            sequence: seq,
        };
        self.count += 1;
        Ok(())
    }

    /// Increment local sequence
    pub fn increment(&mut self, module_id: ModuleId) -> u32 {
        let current = self.get(module_id);
        let next = current + 1;
        let _ = self.set(module_id, next);
        next
    }

    /// Compare two version vectors
    pub fn compare(&self, other: &VersionVector) -> VVOrder {
        let mut self_dominates = true;
        let mut other_dominates = true;

        // Check all entries from self
        for i in 0..self.count {
            let self_seq = self.entries[i].sequence;
            let other_seq = other.get(self.entries[i].module_id);

            if self_seq < other_seq {
                self_dominates = false;
            }
            if self_seq > other_seq {
                other_dominates = false;
            }
        }

        // Check entries in other not in self
        for i in 0..other.count {
            let other_seq = other.entries[i].sequence;
            let self_seq = self.get(other.entries[i].module_id);

            if self_seq < other_seq {
                self_dominates = false;
            }
            if self_seq > other_seq {
                other_dominates = false;
            }
        }

        match (self_dominates, other_dominates) {
            (true, true) => VVOrder::Equal,
            (true, false) => VVOrder::After,
            (false, true) => VVOrder::Before,
            (false, false) => VVOrder::Concurrent,
        }
    }

    /// Merge remote VV into self (element-wise max)
    pub fn merge(&mut self, other: &VersionVector) {
        for i in 0..other.count {
            let mod_id = other.entries[i].module_id;
            let other_seq = other.entries[i].sequence;
            let self_seq = self.get(mod_id);

            if other_seq > self_seq {
                let _ = self.set(mod_id, other_seq);
            }
        }
    }
}

// ============================================================================
// LWW Timestamp
// ============================================================================

/// Last-Writer-Wins timestamp for conflict resolution
///
/// When concurrent writes occur, the one with higher timestamp wins.
/// Origin ID is used as deterministic tiebreaker when timestamps equal.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LWWTimestamp {
    /// Timestamp (higher wins)
    pub timestamp_us: u32,
    /// Tiebreaker (higher wins)
    pub origin_id: ModuleId,
}

impl LWWTimestamp {
    /// Create new LWW timestamp
    pub fn new(timestamp_us: u32, origin_id: ModuleId) -> Self {
        Self { timestamp_us, origin_id }
    }

    /// Check if self is newer than other
    pub fn is_newer(&self, other: &LWWTimestamp) -> bool {
        if self.timestamp_us != other.timestamp_us {
            return self.timestamp_us > other.timestamp_us;
        }
        // Tiebreaker: higher origin_id wins
        self.origin_id > other.origin_id
    }
}

impl PartialOrd for LWWTimestamp {
    fn partial_cmp(&self, other: &Self) -> Option<core::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for LWWTimestamp {
    fn cmp(&self, other: &Self) -> core::cmp::Ordering {
        match self.timestamp_us.cmp(&other.timestamp_us) {
            core::cmp::Ordering::Equal => self.origin_id.cmp(&other.origin_id),
            ord => ord,
        }
    }
}

// ============================================================================
// Event V2
// ============================================================================

/// Extended event format for gossip protocol
///
/// Adds origin tracking for causality and deduplication.
#[derive(Debug, Clone, Copy, Default)]
pub struct EventV2 {
    /// Local sequence (for compat)
    pub sequence: u32,
    /// Event timestamp
    pub timestamp_us: u32,
    /// Event type code
    pub event_type: u8,
    /// Event flags
    pub flags: u8,
    /// Original emitter module
    pub origin_id: ModuleId,
    /// Hops from origin (TTL)
    pub hop_count: u8,
    /// Sequence at origin module
    pub origin_seq: u32,
    /// Event payload (20 bytes)
    pub payload: [u8; 20],
}

/// Event type codes
pub mod event_types {
    /// State transition event
    pub const STATE_TRANSITION: u8 = 0x01;
    /// Field published event
    pub const FIELD_PUBLISHED: u8 = 0x02;
    /// Neighbor joined event
    pub const NEIGHBOR_JOINED: u8 = 0x03;
    /// Neighbor left event
    pub const NEIGHBOR_LEFT: u8 = 0x04;
    /// Consensus start event
    pub const CONSENSUS_START: u8 = 0x05;
    /// Consensus vote event
    pub const CONSENSUS_VOTE: u8 = 0x06;
    /// Consensus result event
    pub const CONSENSUS_RESULT: u8 = 0x07;
    /// User-defined events start here
    pub const USER_DEFINED: u8 = 0x80;
}

/// Event flags
pub mod event_flags {
    /// Event requires acknowledgment
    pub const REQUIRES_ACK: u8 = 0x01;
    /// Priority event
    pub const PRIORITY: u8 = 0x02;
    /// Event is replayed (from storage)
    pub const REPLAYED: u8 = 0x04;
}

impl EventV2 {
    /// Create LWW timestamp from event
    pub fn lww_timestamp(&self) -> LWWTimestamp {
        LWWTimestamp::new(self.timestamp_us, self.origin_id)
    }
}

// ============================================================================
// VV Summary (compressed for messages)
// ============================================================================

/// Compressed version vector summary
///
/// Compact VV for message space constraints.
/// Stores only sequence numbers (mod 256) for k neighbors.
#[derive(Debug, Clone, Copy, Default)]
pub struct VVSummary {
    /// Packed sequences (mod 256)
    pub seqs: [u8; 7],
}

impl VVSummary {
    /// Create summary from version vector
    pub fn from_vv(vv: &VersionVector, neighbor_ids: &[ModuleId]) -> Self {
        let mut summary = Self::default();
        for (i, &mod_id) in neighbor_ids.iter().take(7).enumerate() {
            summary.seqs[i] = (vv.get(mod_id) & 0xFF) as u8;
        }
        summary
    }

    /// Expand summary to version vector
    pub fn to_vv(&self, neighbor_ids: &[ModuleId]) -> VersionVector {
        let mut vv = VersionVector::new();
        for (i, &mod_id) in neighbor_ids.iter().take(7).enumerate() {
            if self.seqs[i] > 0 {
                let _ = vv.set(mod_id, self.seqs[i] as u32);
            }
        }
        vv
    }
}

// ============================================================================
// Gossip Statistics
// ============================================================================

/// Gossip protocol statistics
#[derive(Debug, Clone, Copy, Default)]
pub struct GossipStats {
    /// Events sent via gossip
    pub events_sent: u32,
    /// Events received via gossip
    pub events_received: u32,
    /// Duplicate events filtered
    pub duplicates: u32,
    /// Sequence gaps detected
    pub gaps_detected: u32,
    /// Gaps successfully filled
    pub gaps_filled: u32,
    /// Events dropped at hop limit
    pub ttl_expired: u32,
}

// ============================================================================
// Neighbor Gossip State
// ============================================================================

/// Per-neighbor gossip state
#[derive(Debug, Clone, Copy, Default)]
struct NeighborGossip {
    /// Neighbor module ID
    id: ModuleId,
    /// Last sequence seen from this neighbor
    last_seen_seq: u32,
    /// Our sync cursor to this neighbor
    cursor: u32,
    /// Last anti-entropy sync time
    last_sync: TimeUs,
}

// ============================================================================
// Gossip Context
// ============================================================================

/// Gossip protocol context
pub struct GossipContext {
    /// This module's ID
    my_id: ModuleId,
    /// Our version vector
    vv: VersionVector,
    /// Next local sequence number
    local_sequence: u32,

    /// Neighbor tracking
    neighbors: [NeighborGossip; 7],
    neighbor_count: usize,

    /// Event buffer for outgoing gossip
    pending_events: [EventV2; 8],
    pending_count: usize,

    /// Gap detection buffer
    buffered_events: [EventV2; 4],
    buffered_count: usize,

    /// Last gossip tick time
    last_gossip_tick: TimeUs,
    /// Last sync time
    last_sync: TimeUs,

    /// Statistics
    stats: GossipStats,
}

impl GossipContext {
    /// Create new gossip context
    pub fn new(my_id: ModuleId) -> Self {
        let mut ctx = Self {
            my_id,
            vv: VersionVector::new(),
            local_sequence: 1,
            neighbors: [NeighborGossip::default(); 7],
            neighbor_count: 0,
            pending_events: [EventV2::default(); 8],
            pending_count: 0,
            buffered_events: [EventV2::default(); 4],
            buffered_count: 0,
            last_gossip_tick: 0,
            last_sync: 0,
            stats: GossipStats::default(),
        };
        let _ = ctx.vv.set(my_id, 0);
        ctx
    }

    /// Add neighbor to gossip
    pub fn add_neighbor(&mut self, neighbor_id: ModuleId) -> Result<()> {
        // Check if already exists
        for i in 0..self.neighbor_count {
            if self.neighbors[i].id == neighbor_id {
                return Err(Error::AlreadyExists);
            }
        }

        // Check capacity
        if self.neighbor_count >= 7 {
            return Err(Error::NoMemory);
        }

        // Add neighbor
        self.neighbors[self.neighbor_count] = NeighborGossip {
            id: neighbor_id,
            last_seen_seq: 0,
            cursor: 0,
            last_sync: 0,
        };
        self.neighbor_count += 1;

        Ok(())
    }

    /// Remove neighbor from gossip
    pub fn remove_neighbor(&mut self, neighbor_id: ModuleId) -> Result<()> {
        for i in 0..self.neighbor_count {
            if self.neighbors[i].id == neighbor_id {
                // Shift remaining neighbors
                for j in i..self.neighbor_count - 1 {
                    self.neighbors[j] = self.neighbors[j + 1];
                }
                self.neighbor_count -= 1;
                return Ok(());
            }
        }
        Err(Error::NotFound)
    }

    /// Emit local event (will be gossiped)
    pub fn emit(&mut self, event_type: u8, payload: &[u8], now: TimeUs) -> Result<()> {
        if payload.len() > 20 {
            return Err(Error::InvalidArg);
        }

        // Check buffer space
        if self.pending_count >= 8 {
            return Err(Error::NoMemory);
        }

        // Create event
        let mut event = EventV2::default();
        event.sequence = self.local_sequence;
        event.timestamp_us = now as u32;
        event.event_type = event_type;
        event.flags = 0;
        event.origin_id = self.my_id;
        event.hop_count = 0;
        event.origin_seq = self.local_sequence;

        let len = payload.len().min(20);
        event.payload[..len].copy_from_slice(&payload[..len]);

        // Update local state
        self.pending_events[self.pending_count] = event;
        self.pending_count += 1;
        self.local_sequence += 1;
        self.vv.increment(self.my_id);

        Ok(())
    }

    /// Handle received event from gossip
    ///
    /// Returns Ok if event is new and stored.
    /// Returns Err(AlreadyExists) if duplicate.
    /// Returns Err(NotFound) if gap detected (event buffered).
    pub fn handle_event(&mut self, event: &EventV2, _sender: ModuleId) -> Result<()> {
        self.stats.events_received += 1;

        // Check for duplicate via version vector
        let known_seq = self.vv.get(event.origin_id);
        if event.origin_seq <= known_seq {
            self.stats.duplicates += 1;
            return Err(Error::AlreadyExists);
        }

        // Check for gap
        if event.origin_seq > known_seq + 1 {
            self.stats.gaps_detected += 1;

            // Buffer the event
            if self.buffered_count < 4 {
                self.buffered_events[self.buffered_count] = *event;
                self.buffered_count += 1;
            }

            return Err(Error::NotFound);  // Gap detected
        }

        // Check hop limit
        if event.hop_count >= GOSSIP_MAX_HOPS {
            self.stats.ttl_expired += 1;
            // Still process locally, just don't forward
        }

        // Update version vector
        let _ = self.vv.set(event.origin_id, event.origin_seq);

        // Process buffered events that might now be valid
        self.process_buffered_events();

        Ok(())
    }

    /// Process buffered events that can now be applied
    fn process_buffered_events(&mut self) {
        let mut i = 0;
        while i < self.buffered_count {
            let buffered = &self.buffered_events[i];
            let expected = self.vv.get(buffered.origin_id) + 1;

            if buffered.origin_seq == expected {
                // Can now process this buffered event
                let _ = self.vv.set(buffered.origin_id, buffered.origin_seq);

                // Remove from buffer
                for j in i..self.buffered_count - 1 {
                    self.buffered_events[j] = self.buffered_events[j + 1];
                }
                self.buffered_count -= 1;
                // Don't increment i - check same position again
            } else {
                i += 1;
            }
        }
    }

    /// Get number of pending events
    pub fn pending_count(&self) -> usize {
        self.pending_count
    }

    /// Get pending events for sending
    pub fn pending_events(&self) -> &[EventV2] {
        &self.pending_events[..self.pending_count]
    }

    /// Clear pending events after sending
    pub fn clear_pending(&mut self) {
        self.pending_count = 0;
    }

    /// Get gossip statistics
    pub fn stats(&self) -> &GossipStats {
        &self.stats
    }

    /// Reset statistics
    pub fn reset_stats(&mut self) {
        self.stats = GossipStats::default();
    }

    /// Get version vector
    pub fn version_vector(&self) -> &VersionVector {
        &self.vv
    }

    /// Get local sequence
    pub fn local_sequence(&self) -> u32 {
        self.local_sequence
    }

    /// Get neighbor IDs
    pub fn neighbor_ids(&self) -> impl Iterator<Item = ModuleId> + '_ {
        self.neighbors[..self.neighbor_count].iter().map(|n| n.id)
    }

    /// Merge remote version vector
    pub fn merge_vv(&mut self, remote: &VersionVector) {
        self.vv.merge(remote);
    }
}

// ============================================================================
// Tests
// ============================================================================

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_version_vector_basic() {
        let mut vv = VersionVector::new();

        assert_eq!(vv.get(1), 0);

        vv.set(1, 5).unwrap();
        assert_eq!(vv.get(1), 5);

        vv.set(2, 3).unwrap();
        assert_eq!(vv.get(2), 3);

        let next = vv.increment(1);
        assert_eq!(next, 6);
        assert_eq!(vv.get(1), 6);
    }

    #[test]
    fn test_version_vector_compare() {
        let mut a = VersionVector::new();
        let mut b = VersionVector::new();

        // Both empty - equal
        assert_eq!(a.compare(&b), VVOrder::Equal);

        // a > b
        a.set(1, 5).unwrap();
        b.set(1, 3).unwrap();
        assert_eq!(a.compare(&b), VVOrder::After);

        // b > a
        b.set(1, 7).unwrap();
        assert_eq!(a.compare(&b), VVOrder::Before);

        // Concurrent
        a.set(2, 10).unwrap();
        b.set(3, 10).unwrap();
        assert_eq!(a.compare(&b), VVOrder::Concurrent);
    }

    #[test]
    fn test_version_vector_merge() {
        let mut local = VersionVector::new();
        let mut remote = VersionVector::new();

        local.set(1, 5).unwrap();
        local.set(2, 3).unwrap();

        remote.set(1, 3).unwrap();  // local wins
        remote.set(2, 7).unwrap();  // remote wins
        remote.set(3, 4).unwrap();  // new entry

        local.merge(&remote);

        assert_eq!(local.get(1), 5);  // kept local
        assert_eq!(local.get(2), 7);  // took remote
        assert_eq!(local.get(3), 4);  // added new
    }

    #[test]
    fn test_lww_timestamp() {
        let a = LWWTimestamp::new(1000, 1);
        let b = LWWTimestamp::new(2000, 1);

        assert!(!a.is_newer(&b));
        assert!(b.is_newer(&a));

        // Same timestamp, tiebreaker
        let c = LWWTimestamp::new(1000, 5);
        assert!(c.is_newer(&a));  // higher origin_id wins
    }

    #[test]
    fn test_gossip_context_emit() {
        let mut ctx = GossipContext::new(1);

        ctx.emit(0x01, &[1, 2, 3, 4], 1000000).unwrap();

        assert_eq!(ctx.pending_count(), 1);
        assert_eq!(ctx.local_sequence(), 2);
        assert_eq!(ctx.version_vector().get(1), 1);
    }

    #[test]
    fn test_gossip_duplicate_detection() {
        let mut ctx = GossipContext::new(2);
        ctx.add_neighbor(1).unwrap();

        // Receive event with origin_seq = 1
        let mut event = EventV2::default();
        event.origin_id = 1;
        event.origin_seq = 1;

        assert!(ctx.handle_event(&event, 1).is_ok());

        // Same event again - should be duplicate
        assert_eq!(ctx.handle_event(&event, 1), Err(Error::AlreadyExists));
        assert_eq!(ctx.stats().duplicates, 1);
    }

    #[test]
    fn test_gossip_gap_detection() {
        let mut ctx = GossipContext::new(2);
        ctx.add_neighbor(1).unwrap();

        // Receive event with origin_seq = 3 (gap: missing 1, 2)
        let mut event = EventV2::default();
        event.origin_id = 1;
        event.origin_seq = 3;

        assert_eq!(ctx.handle_event(&event, 1), Err(Error::NotFound));
        assert_eq!(ctx.stats().gaps_detected, 1);
    }

    #[test]
    fn test_vv_summary() {
        let mut vv = VersionVector::new();
        vv.set(1, 100).unwrap();
        vv.set(2, 200).unwrap();
        vv.set(3, 300).unwrap();

        let neighbor_ids = [1, 2, 3];
        let summary = VVSummary::from_vv(&vv, &neighbor_ids);

        assert_eq!(summary.seqs[0], 100);
        assert_eq!(summary.seqs[1], 200);
        assert_eq!(summary.seqs[2], 44);  // 300 mod 256

        let restored = summary.to_vv(&neighbor_ids);
        assert_eq!(restored.get(1), 100);
        assert_eq!(restored.get(2), 200);
        assert_eq!(restored.get(3), 44);  // Lossy compression
    }
}

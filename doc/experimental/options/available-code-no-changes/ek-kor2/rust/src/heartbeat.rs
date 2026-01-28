//! EK-KOR v2 - Heartbeat and Liveness Detection
//!
//! # Novelty: Kernel-Integrated Failure Detection
//!
//! Unlike traditional RTOS where failure detection is application
//! responsibility, EK-KOR v2 integrates heartbeat monitoring into
//! the kernel. This enables:
//! - Automatic neighbor health tracking
//! - Immediate callback on neighbor loss
//! - Triggering of mesh reformation
//!
//! ## Patent Claims (dependent)
//! - Adaptive mesh reformation upon node failure detection
//! - Heartbeat-based liveness with configurable timeout

use crate::types::*;
use heapless::Vec;

// ============================================================================
// Configuration
// ============================================================================

/// Heartbeat configuration
#[derive(Debug, Clone, Copy)]
pub struct HeartbeatConfig {
    /// Heartbeat send period (microseconds)
    pub period: TimeUs,
    /// Missed beats before failure
    pub timeout_count: u8,
    /// Automatically broadcast heartbeats
    pub auto_broadcast: bool,
    /// Track RTT to neighbors
    pub track_latency: bool,
}

impl Default for HeartbeatConfig {
    fn default() -> Self {
        Self {
            period: HEARTBEAT_PERIOD_US,
            timeout_count: HEARTBEAT_TIMEOUT_COUNT,
            auto_broadcast: true,
            track_latency: false,
        }
    }
}

// ============================================================================
// Neighbor Tracking
// ============================================================================

/// Per-neighbor heartbeat tracking
#[derive(Debug, Clone, Copy, Default)]
pub struct HeartbeatNeighbor {
    /// Neighbor ID
    pub id: ModuleId,
    /// Current health state
    pub health: HealthState,
    /// Last heartbeat received
    pub last_seen: TimeUs,
    /// Consecutive missed heartbeats
    pub missed_count: u8,
    /// Last seen sequence number
    pub sequence: u8,
    /// Average RTT (if tracking)
    pub avg_latency: TimeUs,
}

impl HeartbeatNeighbor {
    /// Create new neighbor tracking entry
    pub fn new(id: ModuleId) -> Self {
        Self {
            id,
            health: HealthState::Unknown,
            ..Default::default()
        }
    }
}

// ============================================================================
// Heartbeat Engine
// ============================================================================

/// Heartbeat engine state
pub struct Heartbeat {
    /// This module's ID
    my_id: ModuleId,
    /// Tracked neighbors
    neighbors: Vec<HeartbeatNeighbor, MAX_MODULES>,
    /// Last heartbeat sent
    last_send: TimeUs,
    /// Outgoing sequence number
    send_sequence: u8,
    /// Configuration
    config: HeartbeatConfig,

    /// Callbacks
    on_alive: Option<fn(ModuleId)>,
    on_suspect: Option<fn(ModuleId)>,
    on_dead: Option<fn(ModuleId)>,
}

impl Heartbeat {
    /// Create new heartbeat engine
    pub fn new(my_id: ModuleId, config: Option<HeartbeatConfig>) -> Self {
        Self {
            my_id,
            neighbors: Vec::new(),
            last_send: 0,
            send_sequence: 0,
            config: config.unwrap_or_default(),
            on_alive: None,
            on_suspect: None,
            on_dead: None,
        }
    }

    /// Add neighbor to track
    pub fn add_neighbor(&mut self, neighbor_id: ModuleId) -> Result<()> {
        if neighbor_id == self.my_id || neighbor_id == INVALID_MODULE_ID {
            return Err(Error::InvalidArg);
        }

        // Check if already tracking
        if self.neighbors.iter().any(|n| n.id == neighbor_id) {
            return Err(Error::AlreadyExists);
        }

        let neighbor = HeartbeatNeighbor::new(neighbor_id);
        self.neighbors.push(neighbor).map_err(|_| Error::NoMemory)
    }

    /// Remove neighbor from tracking
    pub fn remove_neighbor(&mut self, neighbor_id: ModuleId) -> Result<()> {
        if let Some(pos) = self.neighbors.iter().position(|n| n.id == neighbor_id) {
            self.neighbors.remove(pos);
            Ok(())
        } else {
            Err(Error::NotFound)
        }
    }

    /// Process received heartbeat
    pub fn received(
        &mut self,
        sender_id: ModuleId,
        sequence: u8,
        now: TimeUs,
    ) -> Result<()> {
        for neighbor in self.neighbors.iter_mut() {
            if neighbor.id == sender_id {
                let old_health = neighbor.health;

                neighbor.last_seen = now;
                neighbor.missed_count = 0;
                neighbor.sequence = sequence;
                neighbor.health = HealthState::Alive;

                // Callback if state changed
                if old_health != HealthState::Alive {
                    if let Some(callback) = self.on_alive {
                        callback(sender_id);
                    }
                }

                return Ok(());
            }
        }

        // Unknown sender - could add them
        Err(Error::NotFound)
    }

    /// Periodic tick
    ///
    /// Returns number of neighbors whose state changed.
    pub fn tick(&mut self, now: TimeUs) -> u32 {
        let mut changed = 0u32;

        // Check each neighbor
        for neighbor in self.neighbors.iter_mut() {
            if neighbor.health == HealthState::Dead {
                continue;
            }

            let elapsed = now.saturating_sub(neighbor.last_seen);
            let timeout = self.config.period * self.config.timeout_count as u64;
            let suspect_threshold = self.config.period * 2;

            let old_health = neighbor.health;

            if elapsed > timeout {
                neighbor.health = HealthState::Dead;
                neighbor.missed_count = self.config.timeout_count;

                if old_health != HealthState::Dead {
                    changed += 1;
                    if let Some(callback) = self.on_dead {
                        callback(neighbor.id);
                    }
                }
            } else if elapsed > suspect_threshold {
                neighbor.health = HealthState::Suspect;
                neighbor.missed_count = (elapsed / self.config.period) as u8;

                if old_health != HealthState::Suspect {
                    changed += 1;
                    if let Some(callback) = self.on_suspect {
                        callback(neighbor.id);
                    }
                }
            }
        }

        // Check if we should send heartbeat
        if self.config.auto_broadcast {
            let send_due = now.saturating_sub(self.last_send) >= self.config.period;
            if send_due {
                self.last_send = now;
                self.send_sequence = self.send_sequence.wrapping_add(1);
                // HAL will handle actual send
            }
        }

        changed
    }

    /// Get time to send next heartbeat (for non-auto mode)
    pub fn should_send(&self, now: TimeUs) -> bool {
        now.saturating_sub(self.last_send) >= self.config.period
    }

    /// Mark heartbeat as sent
    pub fn mark_sent(&mut self, now: TimeUs) {
        self.last_send = now;
        self.send_sequence = self.send_sequence.wrapping_add(1);
    }

    /// Get current sequence number
    pub fn sequence(&self) -> u8 {
        self.send_sequence
    }

    /// Get neighbor health
    pub fn get_health(&self, neighbor_id: ModuleId) -> HealthState {
        self.neighbors
            .iter()
            .find(|n| n.id == neighbor_id)
            .map(|n| n.health)
            .unwrap_or(HealthState::Unknown)
    }

    /// Get time since last heartbeat from neighbor
    pub fn time_since(&self, neighbor_id: ModuleId, now: TimeUs) -> Option<TimeUs> {
        self.neighbors
            .iter()
            .find(|n| n.id == neighbor_id)
            .map(|n| now.saturating_sub(n.last_seen))
    }

    /// Set callbacks
    pub fn set_callbacks(
        &mut self,
        on_alive: Option<fn(ModuleId)>,
        on_suspect: Option<fn(ModuleId)>,
        on_dead: Option<fn(ModuleId)>,
    ) {
        self.on_alive = on_alive;
        self.on_suspect = on_suspect;
        self.on_dead = on_dead;
    }

    /// Create heartbeat message
    pub fn create_message(&self, state: ModuleState, load_percent: u8, thermal_percent: u8) -> HeartbeatMessage {
        HeartbeatMessage {
            sender_id: self.my_id,
            sequence: self.send_sequence,
            state,
            neighbor_count: self.neighbors.len() as u8,
            load_percent,
            thermal_percent,
            flags: 0,
        }
    }
}

// ============================================================================
// Messages
// ============================================================================

/// Heartbeat message (broadcast periodically)
#[derive(Debug, Clone, Copy)]
#[repr(C, packed)]
pub struct HeartbeatMessage {
    /// Sender's module ID
    pub sender_id: ModuleId,
    /// Monotonic sequence number
    pub sequence: u8,
    /// Sender's state
    pub state: ModuleState,
    /// Sender's neighbor count
    pub neighbor_count: u8,
    /// Load 0-100%
    pub load_percent: u8,
    /// Thermal 0-100%
    pub thermal_percent: u8,
    /// Reserved flags
    pub flags: u8,
}

// Static assert for message size
const _: () = assert!(core::mem::size_of::<HeartbeatMessage>() == 7);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_heartbeat_tracking() {
        let mut hb = Heartbeat::new(1, None);

        hb.add_neighbor(2).unwrap();
        hb.add_neighbor(3).unwrap();

        // Simulate heartbeat received
        hb.received(2, 1, 1000).unwrap();
        assert_eq!(hb.get_health(2), HealthState::Alive);

        // Simulate timeout
        hb.tick(1000 + HEARTBEAT_PERIOD_US * 6);
        assert_eq!(hb.get_health(2), HealthState::Dead);
    }

    #[test]
    fn test_heartbeat_suspect() {
        let mut hb = Heartbeat::new(1, None);
        hb.add_neighbor(2).unwrap();

        hb.received(2, 1, 1000).unwrap();

        // Simulate 3 missed heartbeats (suspect threshold = 2)
        hb.tick(1000 + HEARTBEAT_PERIOD_US * 3);
        assert_eq!(hb.get_health(2), HealthState::Suspect);
    }
}

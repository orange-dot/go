//! EK-KOR v2 - Topological k-Neighbor Coordination
//!
//! # Novelty: Topological Coordination (k=7 Neighbors)
//!
//! Instead of fixed addressing or distance-based coordination, each module
//! maintains exactly k logical neighbors regardless of physical topology.
//! This enables scale-free fault propagation and cohesion at any scale.
//!
//! ## Theoretical Basis
//! - Cavagna, A. & Giardina, I. (2010): Scale-free correlations in starlings
//! - Topological interaction maintains cohesion independent of density
//!
//! ## Patent Claims
//! 2. "A topological coordination protocol for modular power electronics
//!    where each module maintains fixed-cardinality neighbor relationships
//!    independent of physical network topology"

use crate::types::*;
use heapless::Vec;

// ============================================================================
// Configuration
// ============================================================================

/// Distance metric for neighbor selection
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum DistanceMetric {
    /// Based on module ID proximity
    #[default]
    Logical,
    /// Based on position coordinates (Euclidean)
    Physical,
    /// Based on communication latency
    Latency,
    /// Application-defined metric
    Custom,
}

/// Topology configuration
#[derive(Debug, Clone, Copy)]
pub struct TopologyConfig {
    /// Target neighbor count (default 7)
    pub k_neighbors: usize,
    /// How to measure distance
    pub metric: DistanceMetric,
    /// How often to broadcast discovery (microseconds)
    pub discovery_period: TimeUs,
    /// Delay before reelecting neighbors (microseconds)
    pub reelection_delay: TimeUs,
    /// Minimum neighbors before DEGRADED state
    pub min_neighbors: usize,
}

impl Default for TopologyConfig {
    fn default() -> Self {
        Self {
            k_neighbors: K_NEIGHBORS,
            metric: DistanceMetric::Logical,
            discovery_period: 1_000_000, // 1 second
            reelection_delay: 100_000,   // 100ms
            min_neighbors: 3,
        }
    }
}

// ============================================================================
// Known Module Entry
// ============================================================================

/// Information about a known module (not necessarily a neighbor)
#[derive(Debug, Clone, Copy, Default)]
struct KnownModule {
    id: ModuleId,
    position: Position,
    distance: i32,
    last_seen: TimeUs,
}

// ============================================================================
// Topology State
// ============================================================================

/// Topology state for a module
pub struct Topology {
    /// This module's ID
    my_id: ModuleId,
    /// This module's position
    my_position: Position,

    /// Current k-neighbors (sorted by distance)
    neighbors: Vec<Neighbor, K_NEIGHBORS>,

    /// All discovered modules
    known: Vec<KnownModule, MAX_MODULES>,

    /// Last discovery broadcast time
    last_discovery: TimeUs,
    /// Last neighbor reelection time
    last_reelection: TimeUs,

    /// Configuration
    config: TopologyConfig,

    /// Callback for topology changes
    on_change: Option<fn(&[Neighbor], &[Neighbor])>,
}

impl Topology {
    /// Create new topology state
    pub fn new(my_id: ModuleId, my_position: Position, config: Option<TopologyConfig>) -> Self {
        Self {
            my_id,
            my_position,
            neighbors: Vec::new(),
            known: Vec::new(),
            last_discovery: 0,
            last_reelection: 0,
            config: config.unwrap_or_default(),
            on_change: None,
        }
    }

    /// Get this module's ID
    pub fn my_id(&self) -> ModuleId {
        self.my_id
    }

    /// Get current neighbor count
    pub fn neighbor_count(&self) -> usize {
        self.neighbors.len()
    }

    /// Get neighbors slice
    pub fn neighbors(&self) -> &[Neighbor] {
        &self.neighbors
    }

    /// Check if a module is a neighbor
    pub fn is_neighbor(&self, module_id: ModuleId) -> bool {
        self.neighbors.iter().any(|n| n.id == module_id)
    }

    /// Get neighbor by ID
    pub fn get_neighbor(&self, module_id: ModuleId) -> Option<&Neighbor> {
        self.neighbors.iter().find(|n| n.id == module_id)
    }

    /// Get mutable neighbor by ID
    pub fn get_neighbor_mut(&mut self, module_id: ModuleId) -> Option<&mut Neighbor> {
        self.neighbors.iter_mut().find(|n| n.id == module_id)
    }

    /// Process discovery message from another module
    pub fn on_discovery(
        &mut self,
        sender_id: ModuleId,
        sender_position: Position,
        now: TimeUs,
    ) -> Result<bool> {
        if sender_id == self.my_id || sender_id == INVALID_MODULE_ID {
            return Err(Error::InvalidArg);
        }

        let distance = self.compute_distance(sender_id, sender_position);

        // Update or add to known list
        let mut found = false;
        for known in self.known.iter_mut() {
            if known.id == sender_id {
                known.position = sender_position;
                known.distance = distance;
                known.last_seen = now;
                found = true;
                break;
            }
        }

        if !found {
            let entry = KnownModule {
                id: sender_id,
                position: sender_position,
                distance,
                last_seen: now,
            };
            let _ = self.known.push(entry); // Ignore if full
        }

        // Check if we should reelect neighbors
        let should_reelect = !self.is_neighbor(sender_id)
            && self.neighbors.len() < self.config.k_neighbors;

        if should_reelect {
            self.reelect();
            return Ok(true);
        }

        Ok(false)
    }

    /// Mark a neighbor as lost
    pub fn on_neighbor_lost(&mut self, lost_id: ModuleId) -> Result<()> {
        // Remove from neighbors
        if let Some(pos) = self.neighbors.iter().position(|n| n.id == lost_id) {
            self.neighbors.remove(pos);
            // Trigger reelection to find replacement
            self.reelect();
            Ok(())
        } else {
            Err(Error::NotFound)
        }
    }

    /// Force neighbor reelection
    ///
    /// Recomputes k-nearest neighbors from all known modules.
    pub fn reelect(&mut self) -> usize {
        let old_neighbors = self.neighbors.clone();

        // Sort known modules by distance
        self.known.sort_by(|a, b| a.distance.cmp(&b.distance));

        // Clear and rebuild neighbor list
        self.neighbors.clear();

        for known in self.known.iter() {
            if self.neighbors.len() >= self.config.k_neighbors {
                break;
            }
            if known.id != self.my_id && known.id != INVALID_MODULE_ID {
                let neighbor = Neighbor {
                    id: known.id,
                    health: HealthState::Unknown,
                    last_seen: known.last_seen,
                    logical_distance: known.distance,
                    ..Default::default()
                };
                let _ = self.neighbors.push(neighbor);
            }
        }

        // Call callback if neighbors changed
        if let Some(callback) = self.on_change {
            callback(&old_neighbors, &self.neighbors);
        }

        self.neighbors.len()
    }

    /// Periodic tick
    ///
    /// Returns true if topology changed.
    pub fn tick(&mut self, now: TimeUs) -> bool {
        // Check if discovery broadcast needed
        let discovery_due = now.saturating_sub(self.last_discovery) > self.config.discovery_period;

        if discovery_due {
            self.last_discovery = now;
            // HAL will handle actual broadcast
        }

        false
    }

    /// Compute distance to another module
    fn compute_distance(&self, id: ModuleId, position: Position) -> i32 {
        match self.config.metric {
            DistanceMetric::Logical => {
                // Simple ID distance
                (self.my_id as i32 - id as i32).abs()
            }
            DistanceMetric::Physical => {
                // Euclidean distance (squared to avoid sqrt)
                self.my_position.distance_squared(&position)
            }
            DistanceMetric::Latency => {
                // Would need HAL measurement - use ID as fallback
                (self.my_id as i32 - id as i32).abs()
            }
            DistanceMetric::Custom => {
                // Application provides this
                0
            }
        }
    }

    /// Set topology change callback
    pub fn set_on_change(&mut self, callback: fn(&[Neighbor], &[Neighbor])) {
        self.on_change = Some(callback);
    }

    /// Create discovery message
    pub fn create_discovery_message(&self) -> DiscoveryMessage {
        DiscoveryMessage {
            sender_id: self.my_id,
            position: self.my_position,
            neighbor_count: self.neighbors.len() as u8,
            state: ModuleState::Active,
            sequence: 0,
        }
    }
}

// ============================================================================
// Messages
// ============================================================================

/// Discovery message (broadcast periodically)
#[derive(Debug, Clone, Copy)]
#[repr(C, packed)]
pub struct DiscoveryMessage {
    pub sender_id: ModuleId,
    pub position: Position,
    pub neighbor_count: u8,
    pub state: ModuleState,
    pub sequence: u16,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_topology_discovery() {
        let mut topo = Topology::new(1, Position::new(0, 0, 0), None);

        // Discover some modules
        topo.on_discovery(2, Position::new(1, 0, 0), 1000).unwrap();
        topo.on_discovery(3, Position::new(2, 0, 0), 1000).unwrap();
        topo.on_discovery(4, Position::new(3, 0, 0), 1000).unwrap();

        assert_eq!(topo.neighbor_count(), 3);
        assert!(topo.is_neighbor(2));
        assert!(topo.is_neighbor(3));
    }

    #[test]
    fn test_k_neighbor_limit() {
        let config = TopologyConfig {
            k_neighbors: 3,
            ..Default::default()
        };
        let mut topo = Topology::new(1, Position::new(0, 0, 0), Some(config));

        // Discover more than k modules
        for i in 2..10 {
            topo.on_discovery(i, Position::new(i as i16, 0, 0), 1000).unwrap();
        }

        // Should only have k neighbors
        assert_eq!(topo.neighbor_count(), 3);
    }
}

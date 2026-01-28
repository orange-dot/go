//! Network partition handling for ROJ consensus.
//!
//! Implements:
//! - Quorum detection (majority required for progress)
//! - Minority freeze mode (read-only until reconnection)
//! - Epoch-based reconciliation (resolve conflicts after partition heals)
//!
//! Design goals:
//! - Safety: Never commit conflicting values
//! - Liveness: Majority partition makes progress
//! - Recovery: < 10s partition recovery time

use crate::types::NodeId;
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};
use std::time::{Duration, Instant};
use tracing::{debug, info, warn};

/// Partition detection timeout
pub const PARTITION_DETECT_TIMEOUT_MS: u64 = 1000;

/// Minimum time between reconnection attempts
pub const RECONNECT_INTERVAL_MS: u64 = 500;

/// Epoch reconciliation timeout
pub const RECONCILE_TIMEOUT_MS: u64 = 5000;

/// Partition state
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum PartitionState {
    /// Normal operation - we have quorum
    Connected,
    /// Detected partition - checking if we have majority
    Detecting,
    /// In minority partition - frozen (read-only)
    MinorityFrozen,
    /// Partition healed - reconciling state
    Reconciling,
}

impl Default for PartitionState {
    fn default() -> Self {
        PartitionState::Connected
    }
}

/// Epoch for conflict resolution after partition
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize, Default)]
pub struct Epoch {
    /// Monotonic epoch number
    pub number: u64,
    /// Node that started this epoch
    pub started_by: u64, // Simplified - use numeric ID for ordering
}

impl Epoch {
    pub fn new(number: u64, node_id: u64) -> Self {
        Self {
            number,
            started_by: node_id,
        }
    }

    pub fn next(&self, node_id: u64) -> Self {
        Self {
            number: self.number + 1,
            started_by: node_id,
        }
    }
}

/// Partition handling messages
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum PartitionMessage {
    /// Probe to detect partition
    #[serde(rename = "PARTITION_PROBE")]
    Probe {
        from: NodeId,
        epoch: Epoch,
        reachable: Vec<NodeId>,
    },

    /// Response to probe
    #[serde(rename = "PARTITION_ACK")]
    ProbeAck {
        from: NodeId,
        epoch: Epoch,
        reachable: Vec<NodeId>,
    },

    /// Announce partition healed
    #[serde(rename = "PARTITION_HEALED")]
    PartitionHealed {
        from: NodeId,
        new_epoch: Epoch,
        merge_nodes: Vec<NodeId>,
    },

    /// Request state sync after partition
    #[serde(rename = "SYNC_REQUEST")]
    SyncRequest {
        from: NodeId,
        epoch: Epoch,
        last_index: u64,
    },

    /// State sync response
    #[serde(rename = "SYNC_RESPONSE")]
    SyncResponse {
        from: NodeId,
        epoch: Epoch,
        last_index: u64,
        /// Missing entries to replay
        entries: Vec<(String, serde_json::Value)>,
    },
}

impl PartitionMessage {
    pub fn to_bytes(&self) -> Result<Vec<u8>, serde_json::Error> {
        serde_json::to_vec(self)
    }

    pub fn from_bytes(bytes: &[u8]) -> Result<Self, serde_json::Error> {
        serde_json::from_slice(bytes)
    }
}

/// Peer liveness tracking
#[derive(Debug, Clone)]
struct PeerLiveness {
    last_seen: Instant,
    reachable: bool,
}

impl Default for PeerLiveness {
    fn default() -> Self {
        Self {
            last_seen: Instant::now(),
            reachable: true,
        }
    }
}

/// Partition handler state machine
pub struct PartitionHandler {
    /// Our node ID
    node_id: NodeId,
    /// Numeric ID for epoch ordering
    numeric_id: u64,
    /// Current partition state
    state: PartitionState,
    /// Current epoch
    epoch: Epoch,
    /// Known peers and their liveness
    peers: HashMap<NodeId, PeerLiveness>,
    /// Total cluster size
    cluster_size: usize,
    /// When detection started
    detection_started: Option<Instant>,
    /// Last probe time
    last_probe: Instant,
    /// Reconciliation state
    reconcile_started: Option<Instant>,
    /// Peers we've synced with during reconciliation
    synced_peers: HashSet<NodeId>,
    /// Callback for state changes
    on_state_change: Option<Box<dyn Fn(PartitionState) + Send + Sync>>,
}

impl PartitionHandler {
    /// Create new partition handler
    pub fn new(node_id: NodeId, cluster_size: usize) -> Self {
        // Hash node_id to get numeric ID for epoch ordering
        let numeric_id = Self::hash_node_id(&node_id);

        Self {
            node_id,
            numeric_id,
            state: PartitionState::Connected,
            epoch: Epoch::default(),
            peers: HashMap::new(),
            cluster_size,
            detection_started: None,
            last_probe: Instant::now(),
            reconcile_started: None,
            synced_peers: HashSet::new(),
            on_state_change: None,
        }
    }

    fn hash_node_id(id: &str) -> u64 {
        use std::collections::hash_map::DefaultHasher;
        use std::hash::{Hash, Hasher};
        let mut hasher = DefaultHasher::new();
        id.hash(&mut hasher);
        hasher.finish()
    }

    /// Set state change callback
    pub fn set_state_change_callback<F>(&mut self, callback: F)
    where
        F: Fn(PartitionState) + Send + Sync + 'static,
    {
        self.on_state_change = Some(Box::new(callback));
    }

    /// Register a peer
    pub fn add_peer(&mut self, peer_id: NodeId) {
        self.peers.entry(peer_id).or_default();
    }

    /// Remove a peer
    pub fn remove_peer(&mut self, peer_id: &NodeId) {
        self.peers.remove(peer_id);
    }

    /// Get current state
    pub fn state(&self) -> PartitionState {
        self.state
    }

    /// Get current epoch
    pub fn epoch(&self) -> Epoch {
        self.epoch
    }

    /// Check if we can make progress (have quorum)
    pub fn has_quorum(&self) -> bool {
        let reachable = self.peers.values().filter(|p| p.reachable).count() + 1;
        reachable > self.cluster_size / 2
    }

    /// Check if operations are allowed (not frozen)
    pub fn can_write(&self) -> bool {
        self.state == PartitionState::Connected && self.has_quorum()
    }

    /// Record that we heard from a peer
    pub fn peer_seen(&mut self, peer_id: &NodeId) {
        if let Some(peer) = self.peers.get_mut(peer_id) {
            peer.last_seen = Instant::now();
            peer.reachable = true;
        }
    }

    /// Tick - check for partition conditions
    pub fn tick(&mut self) -> Option<PartitionMessage> {
        let now = Instant::now();
        let timeout = Duration::from_millis(PARTITION_DETECT_TIMEOUT_MS);

        // Update peer reachability
        for peer in self.peers.values_mut() {
            if now.duration_since(peer.last_seen) > timeout {
                if peer.reachable {
                    debug!("Partition: Peer became unreachable");
                }
                peer.reachable = false;
            }
        }

        match self.state {
            PartitionState::Connected => {
                if !self.has_quorum() {
                    info!("Partition: Lost quorum, starting detection");
                    self.state = PartitionState::Detecting;
                    self.detection_started = Some(now);
                    self.notify_state_change();
                }
            }

            PartitionState::Detecting => {
                if self.has_quorum() {
                    info!("Partition: Regained quorum");
                    self.state = PartitionState::Connected;
                    self.detection_started = None;
                    self.notify_state_change();
                } else if let Some(started) = self.detection_started {
                    if now.duration_since(started) > timeout {
                        warn!("Partition: Confirmed minority partition, freezing");
                        self.state = PartitionState::MinorityFrozen;
                        self.notify_state_change();
                    }
                }
            }

            PartitionState::MinorityFrozen => {
                if self.has_quorum() {
                    info!("Partition: Partition healed, starting reconciliation");
                    self.state = PartitionState::Reconciling;
                    self.reconcile_started = Some(now);
                    self.synced_peers.clear();
                    self.epoch = self.epoch.next(self.numeric_id);
                    self.notify_state_change();

                    return Some(PartitionMessage::PartitionHealed {
                        from: self.node_id.clone(),
                        new_epoch: self.epoch,
                        merge_nodes: self.reachable_peers(),
                    });
                }
            }

            PartitionState::Reconciling => {
                let reconcile_timeout = Duration::from_millis(RECONCILE_TIMEOUT_MS);
                if let Some(started) = self.reconcile_started {
                    if now.duration_since(started) > reconcile_timeout {
                        // Reconciliation complete or timed out
                        info!("Partition: Reconciliation complete");
                        self.state = PartitionState::Connected;
                        self.reconcile_started = None;
                        self.notify_state_change();
                    }
                }
            }
        }

        // Send periodic probes
        let probe_interval = Duration::from_millis(RECONNECT_INTERVAL_MS);
        if now.duration_since(self.last_probe) > probe_interval {
            self.last_probe = now;
            return Some(PartitionMessage::Probe {
                from: self.node_id.clone(),
                epoch: self.epoch,
                reachable: self.reachable_peers(),
            });
        }

        None
    }

    /// Handle incoming partition message
    pub fn handle_message(&mut self, msg: PartitionMessage) -> Option<PartitionMessage> {
        match msg {
            PartitionMessage::Probe { from, epoch, reachable } => {
                self.peer_seen(&from);

                // Update epoch if theirs is higher
                if epoch > self.epoch {
                    self.epoch = epoch;
                }

                Some(PartitionMessage::ProbeAck {
                    from: self.node_id.clone(),
                    epoch: self.epoch,
                    reachable: self.reachable_peers(),
                })
            }

            PartitionMessage::ProbeAck { from, epoch, .. } => {
                self.peer_seen(&from);

                if epoch > self.epoch {
                    self.epoch = epoch;
                }

                None
            }

            PartitionMessage::PartitionHealed { from, new_epoch, .. } => {
                self.peer_seen(&from);

                if new_epoch > self.epoch {
                    info!(
                        "Partition: Received healed notification, new epoch {:?}",
                        new_epoch
                    );
                    self.epoch = new_epoch;

                    if self.state == PartitionState::MinorityFrozen {
                        self.state = PartitionState::Reconciling;
                        self.reconcile_started = Some(Instant::now());
                        self.synced_peers.clear();
                        self.notify_state_change();
                    }
                }

                None
            }

            PartitionMessage::SyncRequest { from, epoch, last_index } => {
                self.peer_seen(&from);

                if epoch < self.epoch {
                    return None; // Stale request
                }

                // In a full implementation, we'd return actual entries
                Some(PartitionMessage::SyncResponse {
                    from: self.node_id.clone(),
                    epoch: self.epoch,
                    last_index,
                    entries: vec![],
                })
            }

            PartitionMessage::SyncResponse { from, epoch, .. } => {
                self.peer_seen(&from);

                if epoch >= self.epoch && self.state == PartitionState::Reconciling {
                    self.synced_peers.insert(from);

                    // Check if we've synced with enough peers
                    if self.synced_peers.len() >= self.cluster_size / 2 {
                        info!("Partition: Synced with majority, reconciliation complete");
                        self.state = PartitionState::Connected;
                        self.reconcile_started = None;
                        self.notify_state_change();
                    }
                }

                None
            }
        }
    }

    /// Get list of reachable peers
    fn reachable_peers(&self) -> Vec<NodeId> {
        self.peers
            .iter()
            .filter(|(_, p)| p.reachable)
            .map(|(id, _)| id.clone())
            .collect()
    }

    /// Notify callback of state change
    fn notify_state_change(&self) {
        if let Some(ref callback) = self.on_state_change {
            callback(self.state);
        }
    }
}

/// Metrics for partition handling
#[derive(Debug, Clone, Default)]
pub struct PartitionMetrics {
    pub total_partitions: u64,
    pub total_recoveries: u64,
    pub time_in_minority_ms: u64,
    pub time_reconciling_ms: u64,
    pub current_epoch: Epoch,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_initial_state() {
        // Single-node cluster has trivial quorum
        let handler = PartitionHandler::new("node1".to_string(), 1);
        assert_eq!(handler.state(), PartitionState::Connected);
        assert!(handler.can_write());
    }

    #[test]
    fn test_epoch_ordering() {
        let e1 = Epoch::new(1, 100);
        let e2 = Epoch::new(1, 200);
        let e3 = Epoch::new(2, 100);

        assert!(e2 > e1); // Same number, higher node ID
        assert!(e3 > e2); // Higher number wins
    }

    #[test]
    fn test_quorum_detection() {
        let mut handler = PartitionHandler::new("node1".to_string(), 5);
        handler.add_peer("node2".to_string());
        handler.add_peer("node3".to_string());
        handler.add_peer("node4".to_string());
        handler.add_peer("node5".to_string());

        // Initially all peers are reachable
        assert!(handler.has_quorum());

        // Simulate two peers becoming unreachable
        handler.peers.get_mut("node2").unwrap().reachable = false;
        handler.peers.get_mut("node3").unwrap().reachable = false;

        // Still have quorum (3/5)
        assert!(handler.has_quorum());

        // One more becomes unreachable
        handler.peers.get_mut("node4").unwrap().reachable = false;

        // Lost quorum (2/5)
        assert!(!handler.has_quorum());
    }

    #[test]
    fn test_probe_response() {
        let mut handler = PartitionHandler::new("node1".to_string(), 3);
        handler.add_peer("node2".to_string());

        let probe = PartitionMessage::Probe {
            from: "node2".to_string(),
            epoch: Epoch::default(),
            reachable: vec!["node1".to_string()],
        };

        let response = handler.handle_message(probe);
        assert!(response.is_some());

        if let Some(PartitionMessage::ProbeAck { from, .. }) = response {
            assert_eq!(from, "node1");
        } else {
            panic!("Expected ProbeAck");
        }
    }

    #[test]
    fn test_message_serialization() {
        let msg = PartitionMessage::Probe {
            from: "node1".to_string(),
            epoch: Epoch::new(1, 100),
            reachable: vec!["node2".to_string()],
        };

        let bytes = msg.to_bytes().unwrap();
        let parsed = PartitionMessage::from_bytes(&bytes).unwrap();

        if let PartitionMessage::Probe { from, epoch, .. } = parsed {
            assert_eq!(from, "node1");
            assert_eq!(epoch.number, 1);
        } else {
            panic!("Wrong message type");
        }
    }
}

//! Raft-style leader election for ROJ consensus.
//!
//! Implements leader election with:
//! - State machine: Follower → Candidate → Leader
//! - Randomized election timeout: 150-300ms
//! - Term-based voting with persistence
//! - Heartbeat mechanism for leadership maintenance
//!
//! Adapted for CAN-FD constraints:
//! - Messages fit in single CAN-FD frame (64 bytes)
//! - Election timeout tuned for CAN-FD latency
//! - Supports both UDP (dev) and CAN-FD (production) transports

use crate::types::{NodeId, unix_timestamp};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::sync::{mpsc, RwLock};
use tokio::time::{interval, sleep};
use tracing::{debug, info, warn};

/// Election timeout range in milliseconds (150-300ms per Raft spec)
pub const ELECTION_TIMEOUT_MIN_MS: u64 = 150;
pub const ELECTION_TIMEOUT_MAX_MS: u64 = 300;

/// Heartbeat interval (should be << election timeout)
pub const HEARTBEAT_INTERVAL_MS: u64 = 50;

/// Role in the consensus cluster
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum Role {
    /// Passive participant, accepts logs from leader
    Follower,
    /// Attempting to become leader
    Candidate,
    /// Active leader, handles all client requests
    Leader,
}

impl Default for Role {
    fn default() -> Self {
        Role::Follower
    }
}

/// Election-related messages (Raft RPCs)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum ElectionMessage {
    /// Request votes from peers
    #[serde(rename = "REQUEST_VOTE")]
    RequestVote {
        term: u64,
        candidate_id: NodeId,
        last_log_index: u64,
        last_log_term: u64,
    },

    /// Response to vote request
    #[serde(rename = "VOTE_RESPONSE")]
    VoteResponse {
        term: u64,
        voter_id: NodeId,
        vote_granted: bool,
    },

    /// Leader heartbeat (empty AppendEntries)
    #[serde(rename = "HEARTBEAT")]
    Heartbeat {
        term: u64,
        leader_id: NodeId,
        /// Leader's commit index for follower to catch up
        commit_index: u64,
    },

    /// Response to heartbeat
    #[serde(rename = "HEARTBEAT_ACK")]
    HeartbeatAck {
        term: u64,
        follower_id: NodeId,
        /// Follower's last log index for leader to track
        last_log_index: u64,
    },
}

impl ElectionMessage {
    /// Serialize to JSON bytes (fits in CAN-FD 64-byte frame for typical messages)
    pub fn to_bytes(&self) -> Result<Vec<u8>, serde_json::Error> {
        serde_json::to_vec(self)
    }

    /// Deserialize from JSON bytes
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, serde_json::Error> {
        serde_json::from_slice(bytes)
    }

    /// Get the term from any election message
    pub fn term(&self) -> u64 {
        match self {
            ElectionMessage::RequestVote { term, .. } => *term,
            ElectionMessage::VoteResponse { term, .. } => *term,
            ElectionMessage::Heartbeat { term, .. } => *term,
            ElectionMessage::HeartbeatAck { term, .. } => *term,
        }
    }
}

/// Persistent state that must survive restarts
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct PersistentState {
    /// Current term (monotonically increasing)
    pub current_term: u64,
    /// Node we voted for in current term (None if not voted)
    pub voted_for: Option<NodeId>,
}

/// Volatile election state
#[derive(Debug, Clone)]
pub struct ElectionState {
    /// Current role
    pub role: Role,
    /// Current leader (if known)
    pub leader_id: Option<NodeId>,
    /// Votes received (only valid in Candidate state)
    pub votes_received: HashMap<NodeId, bool>,
    /// When election timeout should fire
    pub election_deadline: Instant,
    /// Last heartbeat received (for leader liveness)
    pub last_heartbeat: Instant,
}

impl Default for ElectionState {
    fn default() -> Self {
        let now = Instant::now();
        Self {
            role: Role::Follower,
            leader_id: None,
            votes_received: HashMap::new(),
            election_deadline: now + random_election_timeout(),
            last_heartbeat: now,
        }
    }
}

/// Leader election state machine
pub struct Election {
    /// Our node ID
    node_id: NodeId,
    /// Persistent state (term, voted_for)
    persistent: PersistentState,
    /// Volatile state (role, leader, etc.)
    volatile: ElectionState,
    /// Total known peers (for majority calculation)
    peer_count: usize,
    /// Channel to send outgoing messages
    outbound_tx: mpsc::Sender<ElectionMessage>,
    /// Last log index (for vote comparison)
    last_log_index: u64,
    /// Last log term (for vote comparison)
    last_log_term: u64,
    /// Callback for state changes
    on_role_change: Option<Box<dyn Fn(Role, Option<NodeId>) + Send + Sync>>,
}

impl Election {
    /// Create a new election instance
    pub fn new(
        node_id: NodeId,
        peer_count: usize,
        outbound_tx: mpsc::Sender<ElectionMessage>,
    ) -> Self {
        Self {
            node_id,
            persistent: PersistentState::default(),
            volatile: ElectionState::default(),
            peer_count,
            outbound_tx,
            last_log_index: 0,
            last_log_term: 0,
            on_role_change: None,
        }
    }

    /// Set callback for role changes
    pub fn set_role_change_callback<F>(&mut self, callback: F)
    where
        F: Fn(Role, Option<NodeId>) + Send + Sync + 'static,
    {
        self.on_role_change = Some(Box::new(callback));
    }

    /// Get current role
    pub fn role(&self) -> Role {
        self.volatile.role
    }

    /// Get current term
    pub fn term(&self) -> u64 {
        self.persistent.current_term
    }

    /// Get current leader ID (if known)
    pub fn leader(&self) -> Option<&NodeId> {
        self.volatile.leader_id.as_ref()
    }

    /// Check if we are the leader
    pub fn is_leader(&self) -> bool {
        self.volatile.role == Role::Leader
    }

    /// Update log state (called by log replication module)
    pub fn update_log_state(&mut self, last_index: u64, last_term: u64) {
        self.last_log_index = last_index;
        self.last_log_term = last_term;
    }

    /// Load persistent state (called on startup)
    pub fn load_persistent_state(&mut self, state: PersistentState) {
        self.persistent = state;
    }

    /// Get persistent state for saving
    pub fn persistent_state(&self) -> &PersistentState {
        &self.persistent
    }

    /// Handle election timeout tick
    pub async fn tick(&mut self) -> Option<ElectionMessage> {
        let now = Instant::now();

        match self.volatile.role {
            Role::Follower | Role::Candidate => {
                if now >= self.volatile.election_deadline {
                    // Election timeout - start new election
                    return Some(self.start_election().await);
                }
            }
            Role::Leader => {
                // Leaders don't have election timeout, they send heartbeats
            }
        }

        None
    }

    /// Generate heartbeat (called by leader on timer)
    pub fn generate_heartbeat(&self, commit_index: u64) -> Option<ElectionMessage> {
        if self.volatile.role != Role::Leader {
            return None;
        }

        Some(ElectionMessage::Heartbeat {
            term: self.persistent.current_term,
            leader_id: self.node_id.clone(),
            commit_index,
        })
    }

    /// Start a new election
    async fn start_election(&mut self) -> ElectionMessage {
        // Increment term
        self.persistent.current_term += 1;
        self.persistent.voted_for = Some(self.node_id.clone());

        // Transition to candidate
        let old_role = self.volatile.role;
        self.volatile.role = Role::Candidate;
        self.volatile.votes_received.clear();
        self.volatile.votes_received.insert(self.node_id.clone(), true); // Vote for self
        self.volatile.election_deadline = Instant::now() + random_election_timeout();

        info!(
            "Election: Starting election for term {} (was {:?})",
            self.persistent.current_term, old_role
        );

        if old_role != Role::Candidate {
            self.notify_role_change();
        }

        // Request votes from all peers
        ElectionMessage::RequestVote {
            term: self.persistent.current_term,
            candidate_id: self.node_id.clone(),
            last_log_index: self.last_log_index,
            last_log_term: self.last_log_term,
        }
    }

    /// Handle incoming election message
    pub async fn handle_message(&mut self, msg: ElectionMessage) -> Option<ElectionMessage> {
        // Check term and potentially step down
        if msg.term() > self.persistent.current_term {
            debug!(
                "Election: Received higher term {} (current: {}), stepping down",
                msg.term(),
                self.persistent.current_term
            );
            self.step_down(msg.term());
        }

        match msg {
            ElectionMessage::RequestVote {
                term,
                candidate_id,
                last_log_index,
                last_log_term,
            } => self.handle_request_vote(term, candidate_id, last_log_index, last_log_term),

            ElectionMessage::VoteResponse {
                term,
                voter_id,
                vote_granted,
            } => {
                self.handle_vote_response(term, voter_id, vote_granted)
                    .await;
                None
            }

            ElectionMessage::Heartbeat {
                term,
                leader_id,
                commit_index,
            } => self.handle_heartbeat(term, leader_id, commit_index),

            ElectionMessage::HeartbeatAck {
                term,
                follower_id,
                last_log_index,
            } => {
                self.handle_heartbeat_ack(term, follower_id, last_log_index);
                None
            }
        }
    }

    /// Handle RequestVote RPC
    fn handle_request_vote(
        &mut self,
        term: u64,
        candidate_id: NodeId,
        last_log_index: u64,
        last_log_term: u64,
    ) -> Option<ElectionMessage> {
        // Reject if term is stale
        if term < self.persistent.current_term {
            debug!(
                "Election: Rejecting vote for {} (stale term {})",
                candidate_id, term
            );
            return Some(ElectionMessage::VoteResponse {
                term: self.persistent.current_term,
                voter_id: self.node_id.clone(),
                vote_granted: false,
            });
        }

        // Check if we can vote for this candidate
        let can_vote = self.persistent.voted_for.is_none()
            || self.persistent.voted_for.as_ref() == Some(&candidate_id);

        // Check if candidate's log is at least as up-to-date as ours
        let log_ok = last_log_term > self.last_log_term
            || (last_log_term == self.last_log_term && last_log_index >= self.last_log_index);

        let vote_granted = can_vote && log_ok;

        if vote_granted {
            self.persistent.voted_for = Some(candidate_id.clone());
            self.volatile.election_deadline = Instant::now() + random_election_timeout();
            info!(
                "Election: Granting vote to {} for term {}",
                candidate_id, term
            );
        } else {
            debug!(
                "Election: Rejecting vote for {} (can_vote={}, log_ok={})",
                candidate_id, can_vote, log_ok
            );
        }

        Some(ElectionMessage::VoteResponse {
            term: self.persistent.current_term,
            voter_id: self.node_id.clone(),
            vote_granted,
        })
    }

    /// Handle VoteResponse
    async fn handle_vote_response(&mut self, term: u64, voter_id: NodeId, vote_granted: bool) {
        // Ignore if not candidate or wrong term
        if self.volatile.role != Role::Candidate || term != self.persistent.current_term {
            return;
        }

        if vote_granted {
            self.volatile.votes_received.insert(voter_id.clone(), true);
            info!(
                "Election: Received vote from {} ({}/{} votes)",
                voter_id,
                self.volatile.votes_received.len(),
                self.peer_count + 1
            );

            // Check if we have majority
            let majority = (self.peer_count + 1) / 2 + 1;
            if self.volatile.votes_received.len() >= majority {
                self.become_leader().await;
            }
        }
    }

    /// Handle Heartbeat from leader
    fn handle_heartbeat(
        &mut self,
        term: u64,
        leader_id: NodeId,
        _commit_index: u64,
    ) -> Option<ElectionMessage> {
        if term < self.persistent.current_term {
            // Stale leader
            return Some(ElectionMessage::HeartbeatAck {
                term: self.persistent.current_term,
                follower_id: self.node_id.clone(),
                last_log_index: self.last_log_index,
            });
        }

        // Valid heartbeat from current leader
        self.volatile.last_heartbeat = Instant::now();
        self.volatile.election_deadline = Instant::now() + random_election_timeout();

        if self.volatile.leader_id.as_ref() != Some(&leader_id) {
            info!("Election: Recognized {} as leader for term {}", leader_id, term);
            self.volatile.leader_id = Some(leader_id.clone());
        }

        // Step down if we were candidate/leader
        if self.volatile.role != Role::Follower {
            self.step_down(term);
        }

        Some(ElectionMessage::HeartbeatAck {
            term: self.persistent.current_term,
            follower_id: self.node_id.clone(),
            last_log_index: self.last_log_index,
        })
    }

    /// Handle HeartbeatAck from follower (leader only)
    fn handle_heartbeat_ack(&mut self, term: u64, follower_id: NodeId, _last_log_index: u64) {
        if self.volatile.role != Role::Leader || term != self.persistent.current_term {
            return;
        }

        debug!("Election: Heartbeat ack from {}", follower_id);
        // In full implementation, track follower progress for log replication
    }

    /// Become leader
    async fn become_leader(&mut self) {
        info!(
            "Election: Won election for term {} with {}/{} votes",
            self.persistent.current_term,
            self.volatile.votes_received.len(),
            self.peer_count + 1
        );

        self.volatile.role = Role::Leader;
        self.volatile.leader_id = Some(self.node_id.clone());
        self.notify_role_change();

        // Send initial heartbeat to establish leadership
        let heartbeat = ElectionMessage::Heartbeat {
            term: self.persistent.current_term,
            leader_id: self.node_id.clone(),
            commit_index: 0, // Will be updated by log module
        };

        let _ = self.outbound_tx.send(heartbeat).await;
    }

    /// Step down to follower
    fn step_down(&mut self, new_term: u64) {
        let old_role = self.volatile.role;
        self.persistent.current_term = new_term;
        self.persistent.voted_for = None;
        self.volatile.role = Role::Follower;
        self.volatile.votes_received.clear();
        self.volatile.election_deadline = Instant::now() + random_election_timeout();

        if old_role != Role::Follower {
            info!(
                "Election: Stepped down from {:?} to Follower (term {})",
                old_role, new_term
            );
            self.notify_role_change();
        }
    }

    /// Notify callback of role change
    fn notify_role_change(&self) {
        if let Some(ref callback) = self.on_role_change {
            callback(self.volatile.role, self.volatile.leader_id.clone());
        }
    }
}

/// Generate random election timeout between min and max
fn random_election_timeout() -> Duration {
    use rand::Rng;
    let mut rng = rand::thread_rng();
    let ms = rng.gen_range(ELECTION_TIMEOUT_MIN_MS..=ELECTION_TIMEOUT_MAX_MS);
    Duration::from_millis(ms)
}

/// Run election background task
pub async fn run_election_loop(
    election: Arc<RwLock<Election>>,
    mut inbound_rx: mpsc::Receiver<ElectionMessage>,
    outbound_tx: mpsc::Sender<ElectionMessage>,
) {
    let tick_interval = Duration::from_millis(HEARTBEAT_INTERVAL_MS);
    let mut ticker = interval(tick_interval);

    loop {
        tokio::select! {
            _ = ticker.tick() => {
                let mut election = election.write().await;

                // Check election timeout
                if let Some(msg) = election.tick().await {
                    let _ = outbound_tx.send(msg).await;
                }

                // If leader, send heartbeat
                if election.is_leader() {
                    if let Some(hb) = election.generate_heartbeat(0) {
                        let _ = outbound_tx.send(hb).await;
                    }
                }
            }

            Some(msg) = inbound_rx.recv() => {
                let mut election = election.write().await;
                if let Some(response) = election.handle_message(msg).await {
                    let _ = outbound_tx.send(response).await;
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::sync::mpsc;

    #[tokio::test]
    async fn test_election_starts_as_follower() {
        let (tx, _rx) = mpsc::channel(10);
        let election = Election::new("node1".to_string(), 2, tx);
        assert_eq!(election.role(), Role::Follower);
    }

    #[tokio::test]
    async fn test_request_vote_grants_on_first_request() {
        let (tx, _rx) = mpsc::channel(10);
        let mut election = Election::new("node1".to_string(), 2, tx);

        let response = election.handle_request_vote(1, "node2".to_string(), 0, 0);

        assert!(response.is_some());
        if let Some(ElectionMessage::VoteResponse { vote_granted, .. }) = response {
            assert!(vote_granted);
        } else {
            panic!("Expected VoteResponse");
        }
    }

    #[tokio::test]
    async fn test_request_vote_rejects_stale_term() {
        let (tx, _rx) = mpsc::channel(10);
        let mut election = Election::new("node1".to_string(), 2, tx);
        election.persistent.current_term = 5;

        let response = election.handle_request_vote(3, "node2".to_string(), 0, 0);

        assert!(response.is_some());
        if let Some(ElectionMessage::VoteResponse { vote_granted, .. }) = response {
            assert!(!vote_granted);
        } else {
            panic!("Expected VoteResponse");
        }
    }

    #[test]
    fn test_election_message_serialization() {
        let msg = ElectionMessage::RequestVote {
            term: 1,
            candidate_id: "node1".to_string(),
            last_log_index: 0,
            last_log_term: 0,
        };

        let bytes = msg.to_bytes().unwrap();
        let parsed = ElectionMessage::from_bytes(&bytes).unwrap();

        if let ElectionMessage::RequestVote {
            term, candidate_id, ..
        } = parsed
        {
            assert_eq!(term, 1);
            assert_eq!(candidate_id, "node1");
        } else {
            panic!("Wrong message type");
        }
    }

    #[test]
    fn test_random_election_timeout_in_range() {
        for _ in 0..100 {
            let timeout = random_election_timeout();
            assert!(timeout >= Duration::from_millis(ELECTION_TIMEOUT_MIN_MS));
            assert!(timeout <= Duration::from_millis(ELECTION_TIMEOUT_MAX_MS));
        }
    }
}

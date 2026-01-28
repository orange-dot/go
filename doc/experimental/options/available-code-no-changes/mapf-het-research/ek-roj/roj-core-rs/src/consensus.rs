//! K-threshold voting consensus for ROJ

use crate::types::{Message, NodeId, PeerInfo, ProposalState, Vote, unix_timestamp};
use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::sync::RwLock;
use tracing::{info, warn};
use uuid::Uuid;

#[cfg(feature = "elle")]
use crate::history::{HistoryRecorder, ElleOp, key_to_numeric, value_to_numeric};

/// Vote threshold as fraction (2/3 majority)
const VOTE_THRESHOLD: f64 = 2.0 / 3.0;

/// Timeout for proposals in seconds
const PROPOSAL_TIMEOUT_SECS: u64 = 10;

/// Consensus state machine
pub struct Consensus {
    node_id: NodeId,
    /// Active proposals awaiting votes
    proposals: HashMap<String, ProposalState>,
    /// Committed state (key -> value)
    state: HashMap<String, serde_json::Value>,
    /// Reference to discovered peers
    peers: Arc<RwLock<HashMap<NodeId, PeerInfo>>>,
    /// Elle history recorder (feature-gated)
    #[cfg(feature = "elle")]
    history: Option<Arc<HistoryRecorder>>,
    /// Mapping from proposal_id to Elle invoke index
    #[cfg(feature = "elle")]
    pending_invokes: HashMap<String, u64>,
}

impl Consensus {
    /// Create a new consensus instance
    pub fn new(node_id: NodeId, peers: Arc<RwLock<HashMap<NodeId, PeerInfo>>>) -> Self {
        Self {
            node_id,
            proposals: HashMap::new(),
            state: HashMap::new(),
            peers,
            #[cfg(feature = "elle")]
            history: None,
            #[cfg(feature = "elle")]
            pending_invokes: HashMap::new(),
        }
    }

    /// Set Elle history recorder
    #[cfg(feature = "elle")]
    pub fn set_history(&mut self, history: Arc<HistoryRecorder>) {
        self.history = Some(history);
    }

    /// Get Elle history recorder
    #[cfg(feature = "elle")]
    pub fn history(&self) -> Option<&Arc<HistoryRecorder>> {
        self.history.as_ref()
    }

    /// Get the current committed state
    pub fn get_state(&self) -> &HashMap<String, serde_json::Value> {
        &self.state
    }

    /// Create a new proposal
    pub fn create_proposal(&mut self, key: String, value: serde_json::Value) -> Message {
        let proposal_id = Uuid::new_v4().to_string()[..8].to_string();
        let timestamp = unix_timestamp();

        let state = ProposalState::new(
            proposal_id.clone(),
            key.clone(),
            value.clone(),
            timestamp,
        );
        self.proposals.insert(proposal_id.clone(), state);

        // Elle: Record invoke event for this append operation
        #[cfg(feature = "elle")]
        if let Some(ref history) = self.history {
            let elle_key = key_to_numeric(&key);
            let elle_value = value_to_numeric(&value);
            let invoke_idx = history.invoke(vec![ElleOp::append(elle_key, elle_value)]);
            self.pending_invokes.insert(proposal_id.clone(), invoke_idx);
        }

        info!(
            "Consensus: Proposing {}={} (id={})",
            key, value, proposal_id
        );

        Message::Propose {
            proposal_id,
            from: self.node_id.clone(),
            key,
            value,
            timestamp,
        }
    }

    /// Handle an incoming proposal and return a vote
    pub fn handle_proposal(
        &mut self,
        proposal_id: String,
        from: NodeId,
        key: String,
        value: serde_json::Value,
        timestamp: u64,
    ) -> Message {
        info!(
            "Consensus: Received PROPOSE {}={} from {}",
            key, value, from
        );

        // Store the proposal
        let state = ProposalState::new(
            proposal_id.clone(),
            key.clone(),
            value.clone(),
            timestamp,
        );
        self.proposals.insert(proposal_id.clone(), state);

        // Simple acceptance: accept all proposals for demo
        // Production would validate against business rules
        let vote = Vote::Accept;

        info!("Consensus: VOTE {:?} for {} (2/3 threshold)", vote, proposal_id);

        Message::Vote {
            proposal_id,
            from: self.node_id.clone(),
            vote,
        }
    }

    /// Handle an incoming vote, returns COMMIT message if threshold reached
    pub async fn handle_vote(
        &mut self,
        proposal_id: String,
        from: NodeId,
        vote: Vote,
    ) -> Option<Message> {
        info!(
            "Consensus: Received VOTE {:?} from {} for {}",
            vote, from, proposal_id
        );

        let peers = self.peers.read().await;
        let total_peers = peers.len() + 1; // Include ourselves

        let proposal = self.proposals.get_mut(&proposal_id)?;
        proposal.votes.insert(from, vote);

        let accept_count = proposal.accept_count();
        let threshold = ((total_peers as f64) * VOTE_THRESHOLD).ceil() as usize;

        info!(
            "Consensus: {}/{} votes ({} needed for threshold)",
            accept_count, total_peers, threshold
        );

        if accept_count >= threshold {
            let key = proposal.key.clone();
            let value = proposal.value.clone();
            let voters: Vec<NodeId> = proposal
                .votes
                .iter()
                .filter(|(_, v)| **v == Vote::Accept)
                .map(|(k, _)| k.clone())
                .collect();

            // Commit to local state
            self.state.insert(key.clone(), value.clone());
            info!("Consensus: COMMIT {}={}", key, value);

            // Elle: Record ok event for this committed operation
            #[cfg(feature = "elle")]
            if let Some(ref history) = self.history {
                if let Some(invoke_idx) = self.pending_invokes.remove(&proposal_id) {
                    let elle_key = key_to_numeric(&key);
                    let elle_value = value_to_numeric(&value);
                    history.ok(invoke_idx, vec![ElleOp::append(elle_key, elle_value)]);
                }
            }

            // Remove completed proposal
            self.proposals.remove(&proposal_id);

            Some(Message::Commit {
                proposal_id,
                key,
                value,
                voters,
            })
        } else {
            None
        }
    }

    /// Handle an incoming commit message
    pub fn handle_commit(
        &mut self,
        proposal_id: String,
        key: String,
        value: serde_json::Value,
        voters: Vec<NodeId>,
    ) {
        info!(
            "Consensus: COMMIT {}={} (voters: {:?})",
            key, value, voters
        );

        // Apply to local state
        self.state.insert(key, value);

        // Clean up proposal if we had it
        self.proposals.remove(&proposal_id);
    }

    /// Clean up expired proposals
    pub fn cleanup_expired(&mut self) {
        let now = unix_timestamp();
        let expired: Vec<String> = self
            .proposals
            .iter()
            .filter(|(_, p)| now - p.timestamp > PROPOSAL_TIMEOUT_SECS)
            .map(|(id, _)| id.clone())
            .collect();

        for id in expired {
            warn!("Consensus: Proposal {} expired", id);

            // Elle: Record fail event for expired proposals
            #[cfg(feature = "elle")]
            if let Some(ref history) = self.history {
                if let Some(invoke_idx) = self.pending_invokes.remove(&id) {
                    history.fail(invoke_idx);
                }
            }

            self.proposals.remove(&id);
        }
    }

    /// Get addresses of all known peers for broadcasting
    pub async fn peer_addresses(&self) -> Vec<SocketAddr> {
        self.peers
            .read()
            .await
            .values()
            .map(|p| p.addr)
            .collect()
    }
}

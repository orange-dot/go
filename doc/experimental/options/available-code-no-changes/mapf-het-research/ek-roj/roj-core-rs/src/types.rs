//! Core types for ROJ protocol

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::net::SocketAddr;
use std::time::{SystemTime, UNIX_EPOCH};

/// Unique identifier for a ROJ node
pub type NodeId = String;

/// Implementation language of a node
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Language {
    Rust,
    Go,
    C,
}

impl std::fmt::Display for Language {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Language::Rust => write!(f, "rust"),
            Language::Go => write!(f, "go"),
            Language::C => write!(f, "c"),
        }
    }
}

/// Vote decision on a proposal
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Vote {
    Accept,
    Reject,
}

/// Information about a discovered peer
#[derive(Debug, Clone)]
pub struct PeerInfo {
    pub node_id: NodeId,
    pub lang: Language,
    pub addr: SocketAddr,
    pub capabilities: Vec<String>,
    pub version: String,
    pub last_seen: SystemTime,
}

/// ROJ protocol messages
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum Message {
    /// Node presence announcement
    #[serde(rename = "ANNOUNCE")]
    Announce {
        node_id: NodeId,
        lang: Language,
        capabilities: Vec<String>,
        version: String,
    },

    /// Initiate consensus on a key-value change
    #[serde(rename = "PROPOSE")]
    Propose {
        proposal_id: String,
        from: NodeId,
        key: String,
        value: serde_json::Value,
        timestamp: u64,
    },

    /// Vote on a proposal
    #[serde(rename = "VOTE")]
    Vote {
        proposal_id: String,
        from: NodeId,
        vote: Vote,
    },

    /// Finalize a proposal when threshold reached
    #[serde(rename = "COMMIT")]
    Commit {
        proposal_id: String,
        key: String,
        value: serde_json::Value,
        voters: Vec<NodeId>,
    },
}

impl Message {
    /// Serialize message to JSON bytes
    pub fn to_bytes(&self) -> Result<Vec<u8>, serde_json::Error> {
        serde_json::to_vec(self)
    }

    /// Deserialize message from JSON bytes
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, serde_json::Error> {
        serde_json::from_slice(bytes)
    }
}

/// Current Unix timestamp in seconds
pub fn unix_timestamp() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs()
}

/// Proposal state during voting
#[derive(Debug, Clone)]
pub struct ProposalState {
    pub proposal_id: String,
    pub key: String,
    pub value: serde_json::Value,
    pub timestamp: u64,
    pub votes: HashMap<NodeId, Vote>,
}

impl ProposalState {
    pub fn new(proposal_id: String, key: String, value: serde_json::Value, timestamp: u64) -> Self {
        Self {
            proposal_id,
            key,
            value,
            timestamp,
            votes: HashMap::new(),
        }
    }

    /// Count accepting votes
    pub fn accept_count(&self) -> usize {
        self.votes.values().filter(|v| **v == Vote::Accept).count()
    }

    /// Count rejecting votes
    pub fn reject_count(&self) -> usize {
        self.votes.values().filter(|v| **v == Vote::Reject).count()
    }

    /// Check if threshold is reached (2/3 majority)
    pub fn threshold_reached(&self, total_peers: usize) -> bool {
        let threshold = ((total_peers as f64) * 2.0 / 3.0).ceil() as usize;
        self.accept_count() >= threshold
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_message_serialization() {
        let msg = Message::Announce {
            node_id: "alpha".to_string(),
            lang: Language::Rust,
            capabilities: vec!["consensus".to_string()],
            version: "0.1.0".to_string(),
        };

        let bytes = msg.to_bytes().unwrap();
        let parsed = Message::from_bytes(&bytes).unwrap();

        match parsed {
            Message::Announce { node_id, lang, .. } => {
                assert_eq!(node_id, "alpha");
                assert_eq!(lang, Language::Rust);
            }
            _ => panic!("Wrong message type"),
        }
    }

    #[test]
    fn test_proposal_threshold() {
        let mut state = ProposalState::new(
            "test".to_string(),
            "key".to_string(),
            serde_json::Value::Null,
            0,
        );

        // 3 peers, need 2 accepts (2/3 of 3 = 2)
        state.votes.insert("a".to_string(), Vote::Accept);
        assert!(!state.threshold_reached(3));

        state.votes.insert("b".to_string(), Vote::Accept);
        assert!(state.threshold_reached(3));
    }
}

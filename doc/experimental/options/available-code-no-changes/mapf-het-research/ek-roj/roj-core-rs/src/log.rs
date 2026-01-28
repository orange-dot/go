//! Raft-style log replication for ROJ consensus.
//!
//! Implements:
//! - AppendEntries RPC with batching
//! - Committed index tracking
//! - Log compaction via snapshots
//! - Match index tracking per follower
//!
//! Designed for CAN-FD:
//! - Entries fit in CAN-FD frames (64 bytes each)
//! - Batching optimized for bus utilization
//! - Snapshot for efficient state transfer

use crate::types::NodeId;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::time::Instant;
use tracing::{debug, info, warn};

#[cfg(feature = "elle")]
use std::sync::Arc;
#[cfg(feature = "elle")]
use crate::history::{HistoryRecorder, ElleOp, key_to_numeric, value_to_numeric};

/// Maximum entries per AppendEntries batch
pub const MAX_BATCH_SIZE: usize = 8;

/// Log entry types
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum EntryType {
    /// Normal data entry
    Data,
    /// Configuration change (add/remove node)
    Config,
    /// No-op entry (for new leader)
    Noop,
}

/// Single log entry
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LogEntry {
    /// Index in the log (1-based, 0 = empty log)
    pub index: u64,
    /// Term when entry was created
    pub term: u64,
    /// Entry type
    pub entry_type: EntryType,
    /// Key for the entry
    pub key: String,
    /// Value (JSON-serialized)
    pub value: serde_json::Value,
    /// Timestamp (unix seconds)
    pub timestamp: u64,
}

impl LogEntry {
    /// Create a new data entry
    pub fn new_data(index: u64, term: u64, key: String, value: serde_json::Value) -> Self {
        Self {
            index,
            term,
            entry_type: EntryType::Data,
            key,
            value,
            timestamp: crate::types::unix_timestamp(),
        }
    }

    /// Create a no-op entry (used by new leaders)
    pub fn new_noop(index: u64, term: u64) -> Self {
        Self {
            index,
            term,
            entry_type: EntryType::Noop,
            key: String::new(),
            value: serde_json::Value::Null,
            timestamp: crate::types::unix_timestamp(),
        }
    }

    /// Create a config entry
    pub fn new_config(index: u64, term: u64, key: String, value: serde_json::Value) -> Self {
        Self {
            index,
            term,
            entry_type: EntryType::Config,
            key,
            value,
            timestamp: crate::types::unix_timestamp(),
        }
    }
}

/// AppendEntries RPC message
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum LogMessage {
    /// Leader sends entries to followers
    #[serde(rename = "APPEND_ENTRIES")]
    AppendEntries {
        term: u64,
        leader_id: NodeId,
        /// Index of log entry immediately preceding new ones
        prev_log_index: u64,
        /// Term of prev_log_index entry
        prev_log_term: u64,
        /// Log entries to store (empty for heartbeat)
        entries: Vec<LogEntry>,
        /// Leader's commit index
        leader_commit: u64,
    },

    /// Follower response to AppendEntries
    #[serde(rename = "APPEND_ENTRIES_RESPONSE")]
    AppendEntriesResponse {
        term: u64,
        follower_id: NodeId,
        /// True if follower contained entry matching prev_log_index/term
        success: bool,
        /// Follower's last log index (for nextIndex adjustment)
        match_index: u64,
    },

    /// Request snapshot from leader
    #[serde(rename = "INSTALL_SNAPSHOT")]
    InstallSnapshot {
        term: u64,
        leader_id: NodeId,
        /// Last included index in snapshot
        last_included_index: u64,
        /// Last included term in snapshot
        last_included_term: u64,
        /// Snapshot data (key-value state)
        data: HashMap<String, serde_json::Value>,
    },

    /// Response to InstallSnapshot
    #[serde(rename = "INSTALL_SNAPSHOT_RESPONSE")]
    InstallSnapshotResponse {
        term: u64,
        follower_id: NodeId,
        /// Last index in received snapshot
        last_included_index: u64,
    },
}

impl LogMessage {
    /// Serialize to bytes
    pub fn to_bytes(&self) -> Result<Vec<u8>, serde_json::Error> {
        serde_json::to_vec(self)
    }

    /// Deserialize from bytes
    pub fn from_bytes(bytes: &[u8]) -> Result<Self, serde_json::Error> {
        serde_json::from_slice(bytes)
    }

    /// Get term from message
    pub fn term(&self) -> u64 {
        match self {
            LogMessage::AppendEntries { term, .. } => *term,
            LogMessage::AppendEntriesResponse { term, .. } => *term,
            LogMessage::InstallSnapshot { term, .. } => *term,
            LogMessage::InstallSnapshotResponse { term, .. } => *term,
        }
    }
}

/// Replication state for a single follower (leader-side)
#[derive(Debug, Clone)]
pub struct FollowerState {
    /// Next index to send to this follower
    pub next_index: u64,
    /// Highest index known to be replicated
    pub match_index: u64,
    /// Last time we heard from this follower
    pub last_contact: Instant,
}

impl Default for FollowerState {
    fn default() -> Self {
        Self {
            next_index: 1,
            match_index: 0,
            last_contact: Instant::now(),
        }
    }
}

/// Snapshot for log compaction
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Snapshot {
    /// Last included index
    pub last_included_index: u64,
    /// Last included term
    pub last_included_term: u64,
    /// State machine state
    pub state: HashMap<String, serde_json::Value>,
}

/// Replicated log with Raft semantics
pub struct ReplicatedLog {
    /// Our node ID
    node_id: NodeId,
    /// Log entries (index 0 is dummy)
    entries: Vec<LogEntry>,
    /// Index of highest committed entry
    commit_index: u64,
    /// Index of highest entry applied to state machine
    last_applied: u64,
    /// Current term (shared with election)
    current_term: u64,
    /// State machine (key-value store)
    state: HashMap<String, serde_json::Value>,
    /// Follower replication state (leader only)
    followers: HashMap<NodeId, FollowerState>,
    /// Latest snapshot
    snapshot: Option<Snapshot>,
    /// Callback when entry is committed
    on_commit: Option<Box<dyn Fn(&LogEntry) + Send + Sync>>,
    /// Elle history recorder (feature-gated)
    #[cfg(feature = "elle")]
    history: Option<Arc<HistoryRecorder>>,
    /// Mapping from log index to Elle invoke index
    #[cfg(feature = "elle")]
    pending_invokes: HashMap<u64, u64>,
}

impl ReplicatedLog {
    /// Create a new replicated log
    pub fn new(node_id: NodeId) -> Self {
        Self {
            node_id,
            entries: vec![LogEntry::new_noop(0, 0)], // Dummy entry at index 0
            commit_index: 0,
            last_applied: 0,
            current_term: 0,
            state: HashMap::new(),
            followers: HashMap::new(),
            snapshot: None,
            on_commit: None,
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

    /// Set commit callback
    pub fn set_commit_callback<F>(&mut self, callback: F)
    where
        F: Fn(&LogEntry) + Send + Sync + 'static,
    {
        self.on_commit = Some(Box::new(callback));
    }

    /// Get last log index
    pub fn last_index(&self) -> u64 {
        self.entries.last().map(|e| e.index).unwrap_or(0)
    }

    /// Get last log term
    pub fn last_term(&self) -> u64 {
        self.entries.last().map(|e| e.term).unwrap_or(0)
    }

    /// Get commit index
    pub fn commit_index(&self) -> u64 {
        self.commit_index
    }

    /// Get current state
    pub fn state(&self) -> &HashMap<String, serde_json::Value> {
        &self.state
    }

    /// Update current term (from election module)
    pub fn set_term(&mut self, term: u64) {
        self.current_term = term;
    }

    /// Get entry at index
    pub fn get_entry(&self, index: u64) -> Option<&LogEntry> {
        if let Some(snap) = &self.snapshot {
            if index <= snap.last_included_index {
                return None; // Entry is in snapshot
            }
        }

        let base_index = self.snapshot.as_ref().map(|s| s.last_included_index).unwrap_or(0);
        let actual_index = (index - base_index) as usize;

        self.entries.get(actual_index)
    }

    /// Get term at index
    pub fn get_term(&self, index: u64) -> Option<u64> {
        if index == 0 {
            return Some(0);
        }

        if let Some(snap) = &self.snapshot {
            if index == snap.last_included_index {
                return Some(snap.last_included_term);
            }
        }

        self.get_entry(index).map(|e| e.term)
    }

    /// Append a new entry (leader only)
    pub fn append(&mut self, key: String, value: serde_json::Value) -> LogEntry {
        let index = self.last_index() + 1;
        let entry = LogEntry::new_data(index, self.current_term, key.clone(), value.clone());
        self.entries.push(entry.clone());

        // Elle: Record invoke event for this append operation
        #[cfg(feature = "elle")]
        if let Some(ref history) = self.history {
            let elle_key = key_to_numeric(&key);
            let elle_value = value_to_numeric(&value);
            let invoke_idx = history.invoke(vec![ElleOp::append(elle_key, elle_value)]);
            self.pending_invokes.insert(index, invoke_idx);
        }

        debug!("Log: Appended entry {} (term {})", index, self.current_term);
        entry
    }

    /// Initialize follower tracking when becoming leader
    pub fn init_leader(&mut self, peers: &[NodeId]) {
        self.followers.clear();
        let last = self.last_index() + 1;
        for peer in peers {
            self.followers.insert(
                peer.clone(),
                FollowerState {
                    next_index: last,
                    match_index: 0,
                    last_contact: Instant::now(),
                },
            );
        }

        // Append noop to commit entries from previous terms
        let noop = LogEntry::new_noop(self.last_index() + 1, self.current_term);
        self.entries.push(noop);
    }

    /// Generate AppendEntries for a follower
    pub fn generate_append_entries(&self, follower_id: &NodeId) -> Option<LogMessage> {
        let follower = self.followers.get(follower_id)?;

        let prev_log_index = follower.next_index.saturating_sub(1);
        let prev_log_term = self.get_term(prev_log_index).unwrap_or(0);

        // Collect entries starting from next_index
        let mut entries = Vec::new();
        let base_index = self.snapshot.as_ref().map(|s| s.last_included_index).unwrap_or(0);

        for i in follower.next_index..=self.last_index() {
            if entries.len() >= MAX_BATCH_SIZE {
                break;
            }
            if let Some(entry) = self.get_entry(i) {
                entries.push(entry.clone());
            }
        }

        Some(LogMessage::AppendEntries {
            term: self.current_term,
            leader_id: self.node_id.clone(),
            prev_log_index,
            prev_log_term,
            entries,
            leader_commit: self.commit_index,
        })
    }

    /// Handle AppendEntries from leader (follower side)
    pub fn handle_append_entries(
        &mut self,
        term: u64,
        leader_id: NodeId,
        prev_log_index: u64,
        prev_log_term: u64,
        entries: Vec<LogEntry>,
        leader_commit: u64,
    ) -> LogMessage {
        // Reply false if term < currentTerm
        if term < self.current_term {
            return LogMessage::AppendEntriesResponse {
                term: self.current_term,
                follower_id: self.node_id.clone(),
                success: false,
                match_index: self.last_index(),
            };
        }

        self.current_term = term;

        // Reply false if log doesn't contain entry at prevLogIndex with prevLogTerm
        if prev_log_index > 0 {
            match self.get_term(prev_log_index) {
                None => {
                    debug!(
                        "Log: Missing entry at {} (have up to {})",
                        prev_log_index,
                        self.last_index()
                    );
                    return LogMessage::AppendEntriesResponse {
                        term: self.current_term,
                        follower_id: self.node_id.clone(),
                        success: false,
                        match_index: self.last_index(),
                    };
                }
                Some(t) if t != prev_log_term => {
                    debug!(
                        "Log: Term mismatch at {} (have {}, want {})",
                        prev_log_index, t, prev_log_term
                    );
                    // Delete conflicting entry and all following
                    let base = self.snapshot.as_ref().map(|s| s.last_included_index).unwrap_or(0);
                    let idx = (prev_log_index - base) as usize;
                    self.entries.truncate(idx);
                    return LogMessage::AppendEntriesResponse {
                        term: self.current_term,
                        follower_id: self.node_id.clone(),
                        success: false,
                        match_index: self.last_index(),
                    };
                }
                _ => {}
            }
        }

        // Append any new entries not already in the log
        for entry in entries {
            if entry.index > self.last_index() {
                self.entries.push(entry);
            } else if let Some(existing) = self.get_entry(entry.index) {
                if existing.term != entry.term {
                    // Conflict: delete existing and all following
                    let base = self.snapshot.as_ref().map(|s| s.last_included_index).unwrap_or(0);
                    let idx = (entry.index - base) as usize;
                    self.entries.truncate(idx);
                    self.entries.push(entry);
                }
            }
        }

        // Update commit index
        if leader_commit > self.commit_index {
            self.commit_index = std::cmp::min(leader_commit, self.last_index());
            self.apply_committed();
        }

        debug!(
            "Log: AppendEntries success (commit={}, last={})",
            self.commit_index,
            self.last_index()
        );

        LogMessage::AppendEntriesResponse {
            term: self.current_term,
            follower_id: self.node_id.clone(),
            success: true,
            match_index: self.last_index(),
        }
    }

    /// Handle AppendEntriesResponse (leader side)
    pub fn handle_append_entries_response(
        &mut self,
        term: u64,
        follower_id: NodeId,
        success: bool,
        match_index: u64,
    ) {
        if term > self.current_term {
            return; // Stale - election module will handle step down
        }

        let follower = match self.followers.get_mut(&follower_id) {
            Some(f) => f,
            None => return,
        };

        follower.last_contact = Instant::now();

        if success {
            follower.match_index = match_index;
            follower.next_index = match_index + 1;
            debug!(
                "Log: {} match_index={}, next_index={}",
                follower_id, follower.match_index, follower.next_index
            );

            // Check if we can advance commit index
            self.try_advance_commit();
        } else {
            // Decrement next_index and retry
            follower.next_index = match_index + 1;
            debug!(
                "Log: {} rejected, adjusting next_index to {}",
                follower_id, follower.next_index
            );
        }
    }

    /// Try to advance commit index based on follower match indices
    fn try_advance_commit(&mut self) {
        // Find index N such that majority have match_index >= N
        for n in (self.commit_index + 1)..=self.last_index() {
            if let Some(entry) = self.get_entry(n) {
                // Only commit entries from current term
                if entry.term != self.current_term {
                    continue;
                }

                // Count replicas (including self)
                let count = 1
                    + self
                        .followers
                        .values()
                        .filter(|f| f.match_index >= n)
                        .count();

                let majority = (self.followers.len() + 1) / 2 + 1;

                if count >= majority {
                    self.commit_index = n;
                    info!("Log: Advanced commit index to {}", n);
                }
            }
        }

        self.apply_committed();
    }

    /// Apply committed entries to state machine
    fn apply_committed(&mut self) {
        while self.last_applied < self.commit_index {
            self.last_applied += 1;
            // Clone entry to avoid borrow conflict
            let entry = self.get_entry(self.last_applied).cloned();
            if let Some(entry) = entry {
                if entry.entry_type == EntryType::Data && !entry.key.is_empty() {
                    self.state.insert(entry.key.clone(), entry.value.clone());
                    info!("Log: Applied {}={}", entry.key, entry.value);

                    // Elle: Record ok event for this applied operation
                    #[cfg(feature = "elle")]
                    if let Some(ref history) = self.history {
                        if let Some(invoke_idx) = self.pending_invokes.remove(&entry.index) {
                            let elle_key = key_to_numeric(&entry.key);
                            let elle_value = value_to_numeric(&entry.value);
                            // Include read result showing current state for the key
                            let current_values: Vec<i64> = self
                                .state
                                .get(&entry.key)
                                .map(|v| vec![value_to_numeric(v)])
                                .unwrap_or_default();
                            history.ok(
                                invoke_idx,
                                vec![
                                    ElleOp::append(elle_key, elle_value),
                                    ElleOp::read(elle_key, Some(current_values)),
                                ],
                            );
                        }
                    }
                }
                if let Some(ref callback) = self.on_commit {
                    callback(&entry);
                }
            }
        }
    }

    /// Create a snapshot (for log compaction)
    pub fn create_snapshot(&mut self) -> Snapshot {
        let snap = Snapshot {
            last_included_index: self.last_applied,
            last_included_term: self.get_term(self.last_applied).unwrap_or(0),
            state: self.state.clone(),
        };

        // Trim log entries before snapshot
        let base = self.snapshot.as_ref().map(|s| s.last_included_index).unwrap_or(0);
        let keep_from = (self.last_applied - base) as usize;
        if keep_from > 0 && keep_from < self.entries.len() {
            self.entries = self.entries.split_off(keep_from);
            // Ensure first entry is at correct index
            if let Some(first) = self.entries.first_mut() {
                first.index = self.last_applied + 1;
            }
        }

        self.snapshot = Some(snap.clone());
        info!(
            "Log: Created snapshot at index {} (kept {} entries)",
            self.last_applied,
            self.entries.len()
        );

        snap
    }

    /// Install snapshot from leader
    pub fn install_snapshot(&mut self, snapshot: Snapshot) -> LogMessage {
        if snapshot.last_included_index <= self.commit_index {
            // We already have this or more
            return LogMessage::InstallSnapshotResponse {
                term: self.current_term,
                follower_id: self.node_id.clone(),
                last_included_index: snapshot.last_included_index,
            };
        }

        // Reset state to snapshot
        self.state = snapshot.state.clone();
        self.commit_index = snapshot.last_included_index;
        self.last_applied = snapshot.last_included_index;

        // Reset log
        self.entries = vec![LogEntry::new_noop(
            snapshot.last_included_index,
            snapshot.last_included_term,
        )];

        self.snapshot = Some(snapshot.clone());

        info!(
            "Log: Installed snapshot at index {}",
            snapshot.last_included_index
        );

        LogMessage::InstallSnapshotResponse {
            term: self.current_term,
            follower_id: self.node_id.clone(),
            last_included_index: snapshot.last_included_index,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_append_and_get() {
        let mut log = ReplicatedLog::new("node1".to_string());
        log.set_term(1);

        let entry = log.append("key1".to_string(), serde_json::json!("value1"));
        assert_eq!(entry.index, 1);
        assert_eq!(entry.term, 1);

        let got = log.get_entry(1).unwrap();
        assert_eq!(got.key, "key1");
    }

    #[test]
    fn test_log_message_serialization() {
        let msg = LogMessage::AppendEntries {
            term: 1,
            leader_id: "leader".to_string(),
            prev_log_index: 0,
            prev_log_term: 0,
            entries: vec![],
            leader_commit: 0,
        };

        let bytes = msg.to_bytes().unwrap();
        let parsed = LogMessage::from_bytes(&bytes).unwrap();

        if let LogMessage::AppendEntries { term, leader_id, .. } = parsed {
            assert_eq!(term, 1);
            assert_eq!(leader_id, "leader");
        } else {
            panic!("Wrong message type");
        }
    }

    #[test]
    fn test_handle_append_entries() {
        let mut log = ReplicatedLog::new("follower".to_string());

        let response = log.handle_append_entries(
            1,
            "leader".to_string(),
            0,
            0,
            vec![LogEntry::new_data(
                1,
                1,
                "k1".to_string(),
                serde_json::json!("v1"),
            )],
            1,
        );

        if let LogMessage::AppendEntriesResponse { success, match_index, .. } = response {
            assert!(success);
            assert_eq!(match_index, 1);
        } else {
            panic!("Expected AppendEntriesResponse");
        }

        assert_eq!(log.last_index(), 1);
        assert_eq!(log.state().get("k1"), Some(&serde_json::json!("v1")));
    }
}

//! In-process multi-node cluster for testing
//!
//! Provides an in-memory cluster of ROJ nodes that communicate
//! via channels instead of network, enabling deterministic testing.

use crate::fault_injection::ByzantineBehavior;
use roj_core::{HistoryRecorder, ElleOp, key_to_numeric, value_to_numeric};
use roj_core::types::{NodeId, Vote};
use serde_json::Value;
use std::collections::{HashMap, HashSet};
use std::sync::Arc;
use tokio::sync::mpsc;
use tracing::{debug, info, warn};

/// Message between nodes in the cluster
#[derive(Debug, Clone)]
pub enum ClusterMessage {
    /// Propose a key-value change
    Propose {
        from: NodeId,
        proposal_id: String,
        key: String,
        value: Value,
    },
    /// Vote on a proposal
    Vote {
        from: NodeId,
        proposal_id: String,
        vote: Vote,
    },
    /// Commit notification
    Commit {
        proposal_id: String,
        key: String,
        value: Value,
    },
}

/// State for a single node in the cluster
pub struct ClusterNode {
    pub id: NodeId,
    pub state: HashMap<String, Value>,
    pub pending_proposals: HashMap<String, PendingProposal>,
    pub history: Arc<HistoryRecorder>,
    pub tx: mpsc::Sender<ClusterMessage>,
    pub rx: mpsc::Receiver<ClusterMessage>,
    /// Mapping from proposal_id to Elle invoke index
    pending_invokes: HashMap<String, u64>,
    /// Whether this node is partitioned (cannot communicate)
    pub partitioned: bool,
    /// Whether this node is crashed (stopped processing)
    pub crashed: bool,
    /// Byzantine behavior for this node (Honest by default)
    pub byzantine_behavior: ByzantineBehavior,
    /// Set of nodes detected as Byzantine (equivocation detected)
    pub known_byzantine: HashSet<NodeId>,
    /// Track votes per proposal per node (for equivocation detection)
    pub vote_history: HashMap<String, HashMap<NodeId, Vote>>,
}

/// A pending proposal awaiting votes
#[derive(Debug, Clone)]
pub struct PendingProposal {
    pub key: String,
    pub value: Value,
    pub votes: HashMap<NodeId, Vote>,
    pub committed: bool,
}

impl ClusterNode {
    /// Create a new cluster node
    pub fn new(id: NodeId, process_id: u64) -> (Self, mpsc::Sender<ClusterMessage>) {
        let (tx, rx) = mpsc::channel(100);
        let history = Arc::new(HistoryRecorder::new(process_id));

        let node = Self {
            id,
            state: HashMap::new(),
            pending_proposals: HashMap::new(),
            history,
            tx: tx.clone(),
            rx,
            pending_invokes: HashMap::new(),
            partitioned: false,
            crashed: false,
            byzantine_behavior: ByzantineBehavior::Honest,
            known_byzantine: HashSet::new(),
            vote_history: HashMap::new(),
        };

        (node, tx)
    }

    /// Set Byzantine behavior for this node
    pub fn set_byzantine(&mut self, behavior: ByzantineBehavior) {
        self.byzantine_behavior = behavior;
        if behavior != ByzantineBehavior::Honest {
            warn!("Node {}: Byzantine behavior set to {:?}", self.id, behavior);
        }
    }

    /// Check if a node is known to be Byzantine
    pub fn is_known_byzantine(&self, node_id: &NodeId) -> bool {
        self.known_byzantine.contains(node_id)
    }

    /// Record a detected Byzantine node
    pub fn mark_byzantine(&mut self, node_id: NodeId) {
        if self.known_byzantine.insert(node_id.clone()) {
            warn!("Node {}: Detected Byzantine behavior from {}", self.id, node_id);
        }
    }

    /// Create a proposal
    pub fn propose(&mut self, key: String, value: Value) -> (String, ClusterMessage) {
        let proposal_id = format!("{}_{}", self.id, uuid::Uuid::new_v4().to_string()[..8].to_string());

        // Record Elle invoke
        let elle_key = key_to_numeric(&key);
        let elle_value = value_to_numeric(&value);
        let invoke_idx = self.history.invoke(vec![ElleOp::append(elle_key, elle_value)]);
        self.pending_invokes.insert(proposal_id.clone(), invoke_idx);

        // Store pending proposal with own vote already counted
        let mut votes = HashMap::new();
        votes.insert(self.id.clone(), Vote::Accept); // Proposer votes for own proposal

        self.pending_proposals.insert(
            proposal_id.clone(),
            PendingProposal {
                key: key.clone(),
                value: value.clone(),
                votes,
                committed: false,
            },
        );

        let msg = ClusterMessage::Propose {
            from: self.id.clone(),
            proposal_id: proposal_id.clone(),
            key,
            value,
        };

        debug!("Node {}: Created proposal {}", self.id, proposal_id);
        (proposal_id, msg)
    }

    /// Handle incoming message (honest behavior)
    pub fn handle_message(&mut self, msg: ClusterMessage, total_nodes: usize) -> Vec<ClusterMessage> {
        self.handle_message_with_behavior(msg, total_nodes, ByzantineBehavior::Honest)
    }

    /// Handle incoming message with specified Byzantine behavior
    pub fn handle_message_with_behavior(
        &mut self,
        msg: ClusterMessage,
        total_nodes: usize,
        behavior: ByzantineBehavior,
    ) -> Vec<ClusterMessage> {
        if self.crashed {
            return vec![];
        }

        // Byzantine silent: don't respond to anything
        if behavior == ByzantineBehavior::Silent {
            return vec![];
        }

        let mut responses = vec![];

        match msg {
            ClusterMessage::Propose { from, proposal_id, key, value } => {
                debug!("Node {}: Received PROPOSE {} from {}", self.id, proposal_id, from);

                // Ignore proposals from known Byzantine nodes
                if self.is_known_byzantine(&from) {
                    debug!("Node {}: Ignoring proposal from known Byzantine node {}", self.id, from);
                    return vec![];
                }

                // Store the proposal if we don't have it
                if !self.pending_proposals.contains_key(&proposal_id) {
                    self.pending_proposals.insert(
                        proposal_id.clone(),
                        PendingProposal {
                            key: key.clone(),
                            value: value.clone(),
                            votes: HashMap::new(),
                            committed: false,
                        },
                    );
                    // Initialize vote history for this proposal
                    self.vote_history.insert(proposal_id.clone(), HashMap::new());
                }

                // Byzantine false commit: immediately broadcast commit without voting
                if behavior == ByzantineBehavior::FalseCommit {
                    warn!("Node {}: Byzantine false commit for {}", self.id, proposal_id);
                    return vec![ClusterMessage::Commit {
                        proposal_id,
                        key,
                        value,
                    }];
                }

                // Vote accept (simple policy for testing)
                responses.push(ClusterMessage::Vote {
                    from: self.id.clone(),
                    proposal_id,
                    vote: Vote::Accept,
                });
            }

            ClusterMessage::Vote { from, proposal_id, vote } => {
                debug!("Node {}: Received VOTE {:?} from {} for {}", self.id, vote, from, proposal_id);

                // Ignore votes from known Byzantine nodes
                if self.is_known_byzantine(&from) {
                    debug!("Node {}: Ignoring vote from known Byzantine node {}", self.id, from);
                    return vec![];
                }

                // Check for equivocation: has this node already voted differently?
                if let Some(vote_map) = self.vote_history.get(&proposal_id) {
                    if let Some(existing_vote) = vote_map.get(&from) {
                        if *existing_vote != vote {
                            // Equivocation detected!
                            warn!(
                                "Node {}: Equivocation detected from {}: had {:?}, now {:?}",
                                self.id, from, existing_vote, vote
                            );
                            self.mark_byzantine(from.clone());
                            return vec![]; // Ignore this vote
                        }
                    }
                }

                // Record vote in history
                self.vote_history
                    .entry(proposal_id.clone())
                    .or_insert_with(HashMap::new)
                    .insert(from.clone(), vote.clone());

                if let Some(proposal) = self.pending_proposals.get_mut(&proposal_id) {
                    proposal.votes.insert(from, vote);

                    // Check if we have quorum (2/3 majority) - only count non-Byzantine votes
                    let honest_accept_count = proposal
                        .votes
                        .iter()
                        .filter(|(node_id, v)| {
                            **v == Vote::Accept && !self.known_byzantine.contains(*node_id)
                        })
                        .count();

                    let threshold = ((total_nodes as f64) * 2.0 / 3.0).ceil() as usize;

                    if honest_accept_count >= threshold && !proposal.committed {
                        proposal.committed = true;

                        // Commit locally
                        let key = proposal.key.clone();
                        let value = proposal.value.clone();
                        self.state.insert(key.clone(), value.clone());

                        // Record Elle ok
                        if let Some(invoke_idx) = self.pending_invokes.remove(&proposal_id) {
                            let elle_key = key_to_numeric(&key);
                            let elle_value = value_to_numeric(&value);
                            self.history.ok(invoke_idx, vec![ElleOp::append(elle_key, elle_value)]);
                        }

                        info!("Node {}: COMMIT {}={}", self.id, key, value);

                        // Broadcast commit
                        responses.push(ClusterMessage::Commit {
                            proposal_id,
                            key,
                            value,
                        });
                    }
                }
            }

            ClusterMessage::Commit { proposal_id, key, value } => {
                debug!("Node {}: Received COMMIT {} = {}", self.id, key, value);

                // Apply commit
                self.state.insert(key.clone(), value.clone());

                // Mark proposal as committed
                if let Some(proposal) = self.pending_proposals.get_mut(&proposal_id) {
                    proposal.committed = true;
                }
            }
        }

        responses
    }

    /// Generate Byzantine equivocating votes (conflicting votes to different targets)
    pub fn generate_equivocating_votes(
        &self,
        proposal_id: &str,
        total_nodes: usize,
    ) -> Vec<(usize, ClusterMessage)> {
        let mut votes = vec![];

        for target_idx in 0..total_nodes {
            // Send Accept to even-indexed nodes, Reject to odd-indexed
            let vote = if target_idx % 2 == 0 {
                Vote::Accept
            } else {
                Vote::Reject
            };

            votes.push((
                target_idx,
                ClusterMessage::Vote {
                    from: self.id.clone(),
                    proposal_id: proposal_id.to_string(),
                    vote,
                },
            ));
        }

        votes
    }

    /// Mark a proposal as failed (timeout)
    pub fn fail_proposal(&mut self, proposal_id: &str) {
        if let Some(invoke_idx) = self.pending_invokes.remove(proposal_id) {
            self.history.fail(invoke_idx);
            warn!("Node {}: Proposal {} failed", self.id, proposal_id);
        }
        self.pending_proposals.remove(proposal_id);
    }

    /// Get history JSON
    pub fn export_history(&self) -> String {
        self.history.export_json()
    }
}

/// Multi-node test cluster
pub struct Cluster {
    pub nodes: Vec<ClusterNode>,
    /// Senders for each node (indexed by position)
    pub senders: Vec<mpsc::Sender<ClusterMessage>>,
    /// Partition matrix: partitions[i][j] = true means i cannot reach j
    pub partitions: Vec<Vec<bool>>,
}

impl Cluster {
    /// Create a new cluster with the given number of nodes
    pub fn new(num_nodes: usize) -> Self {
        let mut nodes = Vec::with_capacity(num_nodes);
        let mut senders = Vec::with_capacity(num_nodes);

        for i in 0..num_nodes {
            let node_id = format!("node_{}", i);
            let (node, tx) = ClusterNode::new(node_id, i as u64);
            nodes.push(node);
            senders.push(tx);
        }

        let partitions = vec![vec![false; num_nodes]; num_nodes];

        Self {
            nodes,
            senders,
            partitions,
        }
    }

    /// Check if node i can reach node j
    pub fn can_reach(&self, from: usize, to: usize) -> bool {
        !self.partitions[from][to] && !self.nodes[from].partitioned && !self.nodes[to].partitioned
    }

    /// Create a network partition between two groups
    pub fn partition(&mut self, group_a: &[usize], group_b: &[usize]) {
        for &a in group_a {
            for &b in group_b {
                self.partitions[a][b] = true;
                self.partitions[b][a] = true;
            }
        }
        info!("Network partition created: {:?} <-> {:?}", group_a, group_b);
    }

    /// Heal all partitions
    pub fn heal_partitions(&mut self) {
        for row in &mut self.partitions {
            for cell in row {
                *cell = false;
            }
        }
        for node in &mut self.nodes {
            node.partitioned = false;
        }
        info!("All partitions healed");
    }

    /// Crash a node
    pub fn crash_node(&mut self, node_idx: usize) {
        self.nodes[node_idx].crashed = true;
        info!("Node {} crashed", node_idx);
    }

    /// Recover a crashed node
    pub fn recover_node(&mut self, node_idx: usize) {
        self.nodes[node_idx].crashed = false;
        info!("Node {} recovered", node_idx);
    }

    /// Broadcast a message from a node to all reachable nodes
    pub async fn broadcast(&self, from_idx: usize, msg: ClusterMessage) {
        self.broadcast_with_delay(from_idx, msg, None).await;
    }

    /// Broadcast a message with an optional delay before delivery
    pub async fn broadcast_with_delay(&self, from_idx: usize, msg: ClusterMessage, delay: Option<std::time::Duration>) {
        if let Some(d) = delay {
            tokio::time::sleep(d).await;
        }

        let num_nodes = self.nodes.len();

        for to_idx in 0..num_nodes {
            if to_idx != from_idx && self.can_reach(from_idx, to_idx) {
                let _ = self.senders[to_idx].send(msg.clone()).await;
            }
        }
    }

    /// Process one round of messages for all nodes
    pub async fn process_round(&mut self) -> usize {
        let num_nodes = self.nodes.len();
        let mut total_processed = 0;

        // Collect all pending messages
        let mut all_messages: Vec<(usize, ClusterMessage)> = Vec::new();

        for (idx, node) in self.nodes.iter_mut().enumerate() {
            while let Ok(msg) = node.rx.try_recv() {
                all_messages.push((idx, msg));
            }
        }

        // Process messages and collect responses
        let mut responses: Vec<(usize, ClusterMessage)> = Vec::new();

        for (node_idx, msg) in all_messages {
            let node = &mut self.nodes[node_idx];
            let node_responses = node.handle_message(msg, num_nodes);
            total_processed += 1;

            for response in node_responses {
                responses.push((node_idx, response));
            }
        }

        // Broadcast responses
        for (from_idx, msg) in responses {
            self.broadcast(from_idx, msg).await;
        }

        total_processed
    }

    /// Run until no more messages are pending
    pub async fn run_to_completion(&mut self, max_rounds: usize) -> usize {
        let mut total_rounds = 0;

        for _ in 0..max_rounds {
            let processed = self.process_round().await;
            total_rounds += 1;

            if processed == 0 {
                break;
            }
        }

        total_rounds
    }

    /// Merge all node histories into a single history
    pub fn merge_histories(&self) -> String {
        let mut all_events: Vec<roj_core::ElleEvent> = Vec::new();

        for node in &self.nodes {
            all_events.extend(node.history.events());
        }

        // Sort by time
        all_events.sort_by_key(|e| e.time);

        // Re-index
        for (i, event) in all_events.iter_mut().enumerate() {
            event.index = i as u64;
        }

        serde_json::to_string_pretty(&all_events).unwrap_or_else(|_| "[]".to_string())
    }

    /// Get summary statistics
    pub fn stats(&self) -> ClusterStats {
        let mut commits = 0;
        let mut failures = 0;
        let mut events = 0;

        for node in &self.nodes {
            events += node.history.len();
            for proposal in node.pending_proposals.values() {
                if proposal.committed {
                    commits += 1;
                }
            }
            failures += node.pending_invokes.len(); // Pending = will fail
        }

        ClusterStats {
            nodes: self.nodes.len(),
            events,
            commits,
            failures,
        }
    }
}

/// Cluster statistics
#[derive(Debug, Clone)]
pub struct ClusterStats {
    pub nodes: usize,
    pub events: usize,
    pub commits: usize,
    pub failures: usize,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_cluster_basic() {
        let mut cluster = Cluster::new(3);

        // Node 0 proposes
        let (proposal_id, msg) = cluster.nodes[0].propose(
            "key1".to_string(),
            serde_json::json!("value1"),
        );

        // Broadcast proposal
        cluster.broadcast(0, msg).await;

        // Process messages
        cluster.run_to_completion(10).await;

        // Check all nodes have the value
        for node in &cluster.nodes {
            assert_eq!(node.state.get("key1"), Some(&serde_json::json!("value1")));
        }
    }

    #[tokio::test]
    async fn test_cluster_partition() {
        // Use 6 nodes so 4-node majority (ceil(6 * 2/3) = 4) can commit
        let mut cluster = Cluster::new(6);

        // Partition into 2 and 4 (4 is enough for 2/3 quorum of 6)
        cluster.partition(&[0, 1], &[2, 3, 4, 5]);

        // Node 0 (minority) proposes
        let (_, msg) = cluster.nodes[0].propose(
            "key1".to_string(),
            serde_json::json!("value1"),
        );
        cluster.broadcast(0, msg).await;

        // Process - should not commit (no quorum - only 2 nodes)
        cluster.run_to_completion(10).await;

        // Minority nodes should not have committed
        assert!(cluster.nodes[0].pending_proposals.values().all(|p| !p.committed));

        // Node 2 (majority) proposes
        let (_, msg) = cluster.nodes[2].propose(
            "key2".to_string(),
            serde_json::json!("value2"),
        );
        cluster.broadcast(2, msg).await;

        // Process - should commit (majority has quorum - 4 nodes >= ceil(6 * 2/3))
        cluster.run_to_completion(10).await;

        // Majority nodes should have committed
        assert_eq!(cluster.nodes[2].state.get("key2"), Some(&serde_json::json!("value2")));
        assert_eq!(cluster.nodes[3].state.get("key2"), Some(&serde_json::json!("value2")));
    }
}

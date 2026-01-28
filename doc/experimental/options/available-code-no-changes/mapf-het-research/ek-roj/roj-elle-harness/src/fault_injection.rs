//! Fault injection for consistency testing
//!
//! Provides mechanisms to inject various faults into the cluster:
//! - Network partitions
//! - Node crashes
//! - Message loss
//! - Message delays
//! - Byzantine faults (equivocation, false commits, silent nodes)

use crate::cluster::Cluster;
use rand::Rng;
use std::collections::HashMap;
use std::time::Duration;
use tracing::{info, warn};

/// Byzantine node behavior types
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ByzantineBehavior {
    /// Normal (honest) behavior
    Honest,
    /// Send conflicting votes to different nodes
    Equivocating,
    /// Broadcast false commits without quorum
    FalseCommit,
    /// Don't respond to proposals (silent)
    Silent,
    /// Send invalid/malformed messages
    Malformed,
}

/// Fault injection configuration
#[derive(Debug, Clone)]
pub struct FaultConfig {
    /// Probability of message loss (0.0-1.0)
    pub message_loss_rate: f64,
    /// Whether to inject network partitions
    pub enable_partitions: bool,
    /// Whether to inject node crashes
    pub enable_crashes: bool,
    /// Minimum partition duration in milliseconds
    pub min_partition_duration_ms: u64,
    /// Maximum partition duration in milliseconds
    pub max_partition_duration_ms: u64,
    /// Minimum message delay in milliseconds (0 = no delay)
    pub min_delay_ms: u64,
    /// Maximum message delay in milliseconds
    pub max_delay_ms: u64,
    /// Byzantine nodes: node_idx -> behavior
    pub byzantine_nodes: HashMap<usize, ByzantineBehavior>,
}

impl Default for FaultConfig {
    fn default() -> Self {
        Self {
            message_loss_rate: 0.0,
            enable_partitions: false,
            enable_crashes: false,
            min_partition_duration_ms: 100,
            max_partition_duration_ms: 500,
            min_delay_ms: 0,
            max_delay_ms: 0,
            byzantine_nodes: HashMap::new(),
        }
    }
}

impl FaultConfig {
    /// No faults (happy path)
    pub fn none() -> Self {
        Self::default()
    }

    /// Light faults (5% message loss)
    pub fn light() -> Self {
        Self {
            message_loss_rate: 0.05,
            ..Default::default()
        }
    }

    /// Medium faults (10% message loss, partitions)
    pub fn medium() -> Self {
        Self {
            message_loss_rate: 0.10,
            enable_partitions: true,
            ..Default::default()
        }
    }

    /// Heavy faults (20% message loss, partitions, crashes)
    pub fn heavy() -> Self {
        Self {
            message_loss_rate: 0.20,
            enable_partitions: true,
            enable_crashes: true,
            ..Default::default()
        }
    }

    /// Slow network (100-500ms message delays)
    pub fn slow_network() -> Self {
        Self {
            min_delay_ms: 100,
            max_delay_ms: 500,
            ..Default::default()
        }
    }

    /// Byzantine with equivocating node
    pub fn bft_equivocation(byzantine_node: usize) -> Self {
        let mut byzantine_nodes = HashMap::new();
        byzantine_nodes.insert(byzantine_node, ByzantineBehavior::Equivocating);
        Self {
            byzantine_nodes,
            ..Default::default()
        }
    }

    /// Byzantine minority (f < n/3)
    pub fn bft_minority(num_byzantine: usize, total_nodes: usize) -> Self {
        let mut byzantine_nodes = HashMap::new();
        // Make the first `num_byzantine` nodes Byzantine
        for i in 0..num_byzantine.min(total_nodes) {
            byzantine_nodes.insert(i, ByzantineBehavior::Equivocating);
        }
        Self {
            byzantine_nodes,
            ..Default::default()
        }
    }

    /// Byzantine false commit node
    pub fn bft_false_commit(byzantine_node: usize) -> Self {
        let mut byzantine_nodes = HashMap::new();
        byzantine_nodes.insert(byzantine_node, ByzantineBehavior::FalseCommit);
        Self {
            byzantine_nodes,
            ..Default::default()
        }
    }

    /// Check if a node is Byzantine
    pub fn is_byzantine(&self, node_idx: usize) -> bool {
        self.byzantine_nodes.contains_key(&node_idx)
    }

    /// Get Byzantine behavior for a node
    pub fn get_byzantine_behavior(&self, node_idx: usize) -> ByzantineBehavior {
        self.byzantine_nodes
            .get(&node_idx)
            .copied()
            .unwrap_or(ByzantineBehavior::Honest)
    }
}

/// Fault injector for a cluster
pub struct FaultInjector {
    config: FaultConfig,
    rng: rand::rngs::ThreadRng,
}

impl FaultInjector {
    /// Create a new fault injector
    pub fn new(config: FaultConfig) -> Self {
        Self {
            config,
            rng: rand::thread_rng(),
        }
    }

    /// Check if a message should be dropped
    pub fn should_drop_message(&mut self) -> bool {
        self.rng.gen::<f64>() < self.config.message_loss_rate
    }

    /// Inject a random partition into the cluster
    pub fn inject_partition(&mut self, cluster: &mut Cluster) {
        if !self.config.enable_partitions {
            return;
        }

        let num_nodes = cluster.nodes.len();
        if num_nodes < 3 {
            return;
        }

        // Create a random partition
        let split_point = self.rng.gen_range(1..num_nodes);
        let group_a: Vec<usize> = (0..split_point).collect();
        let group_b: Vec<usize> = (split_point..num_nodes).collect();

        cluster.partition(&group_a, &group_b);
        info!("Injected partition: {:?} | {:?}", group_a, group_b);
    }

    /// Inject a leader crash (node 0 is assumed to be initial leader)
    pub fn inject_leader_crash(&mut self, cluster: &mut Cluster) {
        if !self.config.enable_crashes {
            return;
        }

        cluster.crash_node(0);
        info!("Injected leader crash (node 0)");
    }

    /// Inject a random node crash
    pub fn inject_random_crash(&mut self, cluster: &mut Cluster) {
        if !self.config.enable_crashes {
            return;
        }

        let node_idx = self.rng.gen_range(0..cluster.nodes.len());
        cluster.crash_node(node_idx);
        info!("Injected random crash (node {})", node_idx);
    }

    /// Heal all faults
    pub fn heal_all(&mut self, cluster: &mut Cluster) {
        cluster.heal_partitions();
        for i in 0..cluster.nodes.len() {
            cluster.recover_node(i);
        }
        info!("Healed all faults");
    }

    /// Get random partition duration
    pub fn random_partition_duration(&mut self) -> Duration {
        let ms = self.rng.gen_range(
            self.config.min_partition_duration_ms..=self.config.max_partition_duration_ms,
        );
        Duration::from_millis(ms)
    }

    /// Get message delay (if configured)
    pub fn get_message_delay(&mut self) -> Option<Duration> {
        if self.config.min_delay_ms == 0 && self.config.max_delay_ms == 0 {
            return None;
        }
        let ms = if self.config.min_delay_ms == self.config.max_delay_ms {
            self.config.min_delay_ms
        } else {
            self.rng.gen_range(self.config.min_delay_ms..=self.config.max_delay_ms)
        };
        Some(Duration::from_millis(ms))
    }

    /// Check if delays are enabled
    pub fn delays_enabled(&self) -> bool {
        self.config.min_delay_ms > 0 || self.config.max_delay_ms > 0
    }

    /// Check if a node is Byzantine
    pub fn is_byzantine(&self, node_idx: usize) -> bool {
        self.config.is_byzantine(node_idx)
    }

    /// Get Byzantine behavior for a node
    pub fn get_byzantine_behavior(&self, node_idx: usize) -> ByzantineBehavior {
        self.config.get_byzantine_behavior(node_idx)
    }

    /// Check if there are any Byzantine nodes configured
    pub fn has_byzantine_nodes(&self) -> bool {
        !self.config.byzantine_nodes.is_empty()
    }

    /// Get count of Byzantine nodes
    pub fn byzantine_count(&self) -> usize {
        self.config.byzantine_nodes.len()
    }
}

/// Predefined fault scenarios
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FaultScenario {
    /// No faults
    None,
    /// Network partition (2-3 split for 5 nodes)
    Partition,
    /// Leader crashes mid-test
    LeaderCrash,
    /// Random message loss (10%)
    MessageLoss,
    /// Cascading failures
    Cascade,
    /// Slow network (100-500ms delays)
    SlowNetwork,
    /// Byzantine equivocation (1 node sends conflicting votes)
    BftEquivocation,
    /// Byzantine minority (f < n/3 nodes)
    BftMinority,
    /// Byzantine at threshold (f = n/3 nodes)
    BftThreshold,
    /// Byzantine false commit (node claims commit without quorum)
    BftFalseCommit,
}

impl FaultScenario {
    /// Get fault config for this scenario
    pub fn config(&self) -> FaultConfig {
        match self {
            FaultScenario::None => FaultConfig::none(),
            FaultScenario::Partition => FaultConfig {
                enable_partitions: true,
                ..FaultConfig::default()
            },
            FaultScenario::LeaderCrash => FaultConfig {
                enable_crashes: true,
                ..FaultConfig::default()
            },
            FaultScenario::MessageLoss => FaultConfig {
                message_loss_rate: 0.10,
                ..FaultConfig::default()
            },
            FaultScenario::Cascade => FaultConfig::heavy(),
            FaultScenario::SlowNetwork => FaultConfig::slow_network(),
            // For BFT scenarios with 5 nodes (can tolerate f=1)
            FaultScenario::BftEquivocation => FaultConfig::bft_equivocation(0),
            // For BFT with 7 nodes (can tolerate f=2), use 1 Byzantine
            FaultScenario::BftMinority => FaultConfig::bft_minority(1, 7),
            // For BFT with 7 nodes, use f=2 (at threshold)
            FaultScenario::BftThreshold => FaultConfig::bft_minority(2, 7),
            FaultScenario::BftFalseCommit => FaultConfig::bft_false_commit(0),
        }
    }

    /// Get recommended number of nodes for this scenario
    pub fn recommended_nodes(&self) -> usize {
        match self {
            FaultScenario::None => 3,
            FaultScenario::Partition => 5,
            FaultScenario::LeaderCrash => 5,
            FaultScenario::MessageLoss => 5,
            FaultScenario::Cascade => 7,
            FaultScenario::SlowNetwork => 5,
            FaultScenario::BftEquivocation => 5,
            FaultScenario::BftMinority => 7,
            FaultScenario::BftThreshold => 7,
            FaultScenario::BftFalseCommit => 5,
        }
    }

    /// Check if this is a Byzantine scenario
    pub fn is_byzantine(&self) -> bool {
        matches!(
            self,
            FaultScenario::BftEquivocation
                | FaultScenario::BftMinority
                | FaultScenario::BftThreshold
                | FaultScenario::BftFalseCommit
        )
    }

    /// Get maximum Byzantine nodes this scenario can tolerate
    pub fn max_byzantine_tolerated(&self) -> usize {
        let n = self.recommended_nodes();
        // BFT requires f < n/3, so max f = floor((n-1)/3)
        (n - 1) / 3
    }
}

impl std::str::FromStr for FaultScenario {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "none" | "happy" => Ok(FaultScenario::None),
            "partition" => Ok(FaultScenario::Partition),
            "leader-crash" | "leader_crash" => Ok(FaultScenario::LeaderCrash),
            "message-loss" | "message_loss" => Ok(FaultScenario::MessageLoss),
            "cascade" => Ok(FaultScenario::Cascade),
            "slow-network" | "slow_network" => Ok(FaultScenario::SlowNetwork),
            "bft-equivocation" | "bft_equivocation" => Ok(FaultScenario::BftEquivocation),
            "bft-minority" | "bft_minority" => Ok(FaultScenario::BftMinority),
            "bft-threshold" | "bft_threshold" => Ok(FaultScenario::BftThreshold),
            "bft-false-commit" | "bft_false_commit" => Ok(FaultScenario::BftFalseCommit),
            _ => Err(format!("Unknown fault scenario: {}", s)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_fault_config_presets() {
        let none = FaultConfig::none();
        assert_eq!(none.message_loss_rate, 0.0);
        assert!(!none.enable_partitions);
        assert_eq!(none.min_delay_ms, 0);
        assert_eq!(none.max_delay_ms, 0);

        let heavy = FaultConfig::heavy();
        assert!(heavy.message_loss_rate > 0.0);
        assert!(heavy.enable_partitions);
        assert!(heavy.enable_crashes);

        let slow = FaultConfig::slow_network();
        assert_eq!(slow.min_delay_ms, 100);
        assert_eq!(slow.max_delay_ms, 500);
        assert_eq!(slow.message_loss_rate, 0.0);
    }

    #[test]
    fn test_fault_scenario_parse() {
        assert_eq!("happy".parse::<FaultScenario>().unwrap(), FaultScenario::None);
        assert_eq!("partition".parse::<FaultScenario>().unwrap(), FaultScenario::Partition);
        assert_eq!("leader-crash".parse::<FaultScenario>().unwrap(), FaultScenario::LeaderCrash);
        assert_eq!("slow-network".parse::<FaultScenario>().unwrap(), FaultScenario::SlowNetwork);
        assert_eq!("bft-equivocation".parse::<FaultScenario>().unwrap(), FaultScenario::BftEquivocation);
        assert_eq!("bft-minority".parse::<FaultScenario>().unwrap(), FaultScenario::BftMinority);
    }

    #[test]
    fn test_byzantine_config() {
        let config = FaultConfig::bft_equivocation(0);
        assert!(config.is_byzantine(0));
        assert!(!config.is_byzantine(1));
        assert_eq!(config.get_byzantine_behavior(0), ByzantineBehavior::Equivocating);
        assert_eq!(config.get_byzantine_behavior(1), ByzantineBehavior::Honest);

        let minority_config = FaultConfig::bft_minority(2, 7);
        assert!(minority_config.is_byzantine(0));
        assert!(minority_config.is_byzantine(1));
        assert!(!minority_config.is_byzantine(2));
    }

    #[test]
    fn test_bft_thresholds() {
        // 5 nodes: can tolerate f=1 ((5-1)/3 = 1)
        assert_eq!(FaultScenario::BftEquivocation.max_byzantine_tolerated(), 1);
        // 7 nodes: can tolerate f=2 ((7-1)/3 = 2)
        assert_eq!(FaultScenario::BftMinority.max_byzantine_tolerated(), 2);
    }

    #[tokio::test]
    async fn test_fault_injector() {
        let config = FaultConfig {
            message_loss_rate: 0.5,
            ..Default::default()
        };
        let mut injector = FaultInjector::new(config);

        // Test message dropping (probabilistic)
        let mut dropped = 0;
        for _ in 0..100 {
            if injector.should_drop_message() {
                dropped += 1;
            }
        }

        // Should be around 50% (with some variance)
        assert!(dropped > 20 && dropped < 80);
    }

    #[test]
    fn test_message_delay() {
        // No delay configured
        let config = FaultConfig::none();
        let mut injector = FaultInjector::new(config);
        assert!(injector.get_message_delay().is_none());
        assert!(!injector.delays_enabled());

        // Slow network configured
        let config = FaultConfig::slow_network();
        let mut injector = FaultInjector::new(config);
        assert!(injector.delays_enabled());

        // Check delay is within range
        for _ in 0..10 {
            let delay = injector.get_message_delay().unwrap();
            assert!(delay >= Duration::from_millis(100));
            assert!(delay <= Duration::from_millis(500));
        }
    }
}

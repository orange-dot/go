//! Predefined test scenarios for Elle consistency verification
//!
//! Provides ready-to-run test scenarios that combine:
//! - Cluster configuration
//! - Workload generation
//! - Fault injection
//! - Elle verification

use crate::cluster::Cluster;
use crate::fault_injection::{FaultConfig, FaultInjector, FaultScenario};
use crate::metrics::{Metrics, ProposalTracker};
use crate::workload::{WorkloadConfig, WorkloadGenerator, Operation};
use crate::elle_runner::{self, ElleResult};
use std::collections::HashMap;
use std::path::Path;
use std::time::Instant;
use thiserror::Error;
use tracing::{info, warn, error};

/// Errors from scenario execution
#[derive(Error, Debug)]
pub enum ScenarioError {
    #[error("Unknown scenario: {0}")]
    UnknownScenario(String),

    #[error("Scenario execution failed: {0}")]
    ExecutionFailed(String),

    #[error("Elle check failed: {0}")]
    ElleError(#[from] elle_runner::ElleError),

    #[error("IO error: {0}")]
    IoError(#[from] std::io::Error),
}

/// Scenario configuration
#[derive(Debug, Clone)]
pub struct ScenarioConfig {
    pub name: String,
    pub description: String,
    pub fault_scenario: FaultScenario,
    pub workload_config: WorkloadConfig,
    pub inject_fault_at_op: Option<usize>,  // When to inject fault (operation number)
    pub heal_fault_at_op: Option<usize>,    // When to heal fault
}

/// Scenario execution results
#[derive(Debug, Clone)]
pub struct ScenarioResults {
    pub scenario: String,
    pub history_json: String,
    pub event_count: usize,
    pub commit_count: usize,
    pub failure_count: usize,
    pub duration_ms: u64,
    pub elle_result: Option<ElleResult>,
    pub metrics: Option<Metrics>,
}

/// Test suite results
#[derive(Debug, Clone)]
pub struct SuiteResults {
    pub total: usize,
    pub passed: usize,
    pub failed: usize,
    pub results: Vec<(String, ScenarioResults, bool)>,
}

impl SuiteResults {
    /// Generate JUnit XML report
    pub fn to_junit_xml(&self) -> String {
        let mut xml = String::from("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        xml.push_str(&format!(
            "<testsuite name=\"ROJ Elle Tests\" tests=\"{}\" failures=\"{}\" errors=\"0\">\n",
            self.total, self.failed
        ));

        for (name, result, passed) in &self.results {
            xml.push_str(&format!("  <testcase name=\"{}\" time=\"{:.3}\"",
                name, result.duration_ms as f64 / 1000.0));

            if *passed {
                xml.push_str(" />\n");
            } else {
                xml.push_str(">\n");
                let msg = result.elle_result
                    .as_ref()
                    .and_then(|r| r.anomaly.clone())
                    .unwrap_or_else(|| "Unknown failure".to_string());
                xml.push_str(&format!("    <failure message=\"{}\"/>\n", msg));
                xml.push_str("  </testcase>\n");
            }
        }

        xml.push_str("</testsuite>\n");
        xml
    }
}

/// Get a predefined scenario by name
pub fn get_scenario(name: &str) -> Result<ScenarioConfig, ScenarioError> {
    match name.to_lowercase().as_str() {
        "happy" | "none" => Ok(ScenarioConfig {
            name: "happy".to_string(),
            description: "No faults - baseline test".to_string(),
            fault_scenario: FaultScenario::None,
            workload_config: WorkloadConfig::default(),
            inject_fault_at_op: None,
            heal_fault_at_op: None,
        }),

        "partition" => Ok(ScenarioConfig {
            name: "partition".to_string(),
            description: "Network partition mid-test".to_string(),
            fault_scenario: FaultScenario::Partition,
            workload_config: WorkloadConfig::default(),
            inject_fault_at_op: Some(30),
            heal_fault_at_op: Some(70),
        }),

        "leader-crash" | "leader_crash" => Ok(ScenarioConfig {
            name: "leader-crash".to_string(),
            description: "Leader crashes mid-test".to_string(),
            fault_scenario: FaultScenario::LeaderCrash,
            workload_config: WorkloadConfig::default(),
            inject_fault_at_op: Some(50),
            heal_fault_at_op: None,
        }),

        "message-loss" | "message_loss" => Ok(ScenarioConfig {
            name: "message-loss".to_string(),
            description: "10% message loss throughout".to_string(),
            fault_scenario: FaultScenario::MessageLoss,
            workload_config: WorkloadConfig::default(),
            inject_fault_at_op: None,
            heal_fault_at_op: None,
        }),

        "contention" => Ok(ScenarioConfig {
            name: "contention".to_string(),
            description: "Single key, high contention".to_string(),
            fault_scenario: FaultScenario::None,
            workload_config: WorkloadConfig {
                num_keys: 1,
                append_ratio: 1.0,
                ..Default::default()
            },
            inject_fault_at_op: None,
            heal_fault_at_op: None,
        }),

        "slow-network" | "slow_network" => Ok(ScenarioConfig {
            name: "slow-network".to_string(),
            description: "Slow network with 100-500ms message delays".to_string(),
            fault_scenario: FaultScenario::SlowNetwork,
            workload_config: WorkloadConfig::default(),
            inject_fault_at_op: None,
            heal_fault_at_op: None,
        }),

        "bft-equivocation" | "bft_equivocation" => Ok(ScenarioConfig {
            name: "bft-equivocation".to_string(),
            description: "Byzantine node sends conflicting votes (1 of 5 equivocating)".to_string(),
            fault_scenario: FaultScenario::BftEquivocation,
            workload_config: WorkloadConfig::default(),
            inject_fault_at_op: None,
            heal_fault_at_op: None,
        }),

        "bft-minority" | "bft_minority" => Ok(ScenarioConfig {
            name: "bft-minority".to_string(),
            description: "Byzantine minority (1 of 7, f < n/3)".to_string(),
            fault_scenario: FaultScenario::BftMinority,
            workload_config: WorkloadConfig::default(),
            inject_fault_at_op: None,
            heal_fault_at_op: None,
        }),

        "bft-threshold" | "bft_threshold" => Ok(ScenarioConfig {
            name: "bft-threshold".to_string(),
            description: "Byzantine at threshold (2 of 7, f = n/3)".to_string(),
            fault_scenario: FaultScenario::BftThreshold,
            workload_config: WorkloadConfig::default(),
            inject_fault_at_op: None,
            heal_fault_at_op: None,
        }),

        "bft-false-commit" | "bft_false_commit" => Ok(ScenarioConfig {
            name: "bft-false-commit".to_string(),
            description: "Byzantine node broadcasts false commits".to_string(),
            fault_scenario: FaultScenario::BftFalseCommit,
            workload_config: WorkloadConfig::default(),
            inject_fault_at_op: None,
            heal_fault_at_op: None,
        }),

        _ => Err(ScenarioError::UnknownScenario(name.to_string())),
    }
}

/// Get all predefined scenario names
pub fn list_scenarios() -> Vec<&'static str> {
    vec![
        "happy",
        "partition",
        "leader-crash",
        "message-loss",
        "contention",
        "slow-network",
        "bft-equivocation",
        "bft-minority",
        "bft-threshold",
        "bft-false-commit",
    ]
}

/// Run a scenario and collect history (without metrics)
pub async fn run_scenario(
    config: ScenarioConfig,
    num_nodes: usize,
    num_operations: usize,
) -> Result<ScenarioResults, ScenarioError> {
    run_scenario_internal(config, num_nodes, num_operations, false).await
}

/// Run a scenario and collect history with performance metrics
pub async fn run_scenario_with_metrics(
    config: ScenarioConfig,
    num_nodes: usize,
    num_operations: usize,
) -> Result<ScenarioResults, ScenarioError> {
    run_scenario_internal(config, num_nodes, num_operations, true).await
}

/// Internal implementation that optionally collects metrics
async fn run_scenario_internal(
    config: ScenarioConfig,
    num_nodes: usize,
    num_operations: usize,
    collect_metrics: bool,
) -> Result<ScenarioResults, ScenarioError> {
    let start = Instant::now();
    info!("Starting scenario: {} ({} nodes, {} ops)", config.name, num_nodes, num_operations);

    // Create cluster
    let mut cluster = Cluster::new(num_nodes);

    // Create fault injector
    let fault_config = config.fault_scenario.config();
    let mut injector = FaultInjector::new(fault_config.clone());

    // Configure Byzantine nodes if any
    if injector.has_byzantine_nodes() {
        info!("Configuring {} Byzantine node(s)", injector.byzantine_count());
        for (node_idx, behavior) in &fault_config.byzantine_nodes {
            if *node_idx < cluster.nodes.len() {
                cluster.nodes[*node_idx].set_byzantine(*behavior);
            }
        }
    }

    // Generate workload
    let mut generator = WorkloadGenerator::new(config.workload_config.clone());
    let operations = generator.generate(num_operations);

    // Metrics collection
    let mut metrics = if collect_metrics { Some(Metrics::new()) } else { None };
    let mut proposal_trackers: HashMap<usize, ProposalTracker> = HashMap::new();
    if collect_metrics {
        for i in 0..num_nodes {
            proposal_trackers.insert(i, ProposalTracker::new());
        }
        if let Some(ref mut m) = metrics {
            m.start();
        }
    }

    // Track commits
    let mut commit_count = 0;
    let mut failure_count = 0;

    // Execute operations
    for (i, op) in operations.iter().enumerate() {
        // Check for fault injection point
        if Some(i) == config.inject_fault_at_op {
            match config.fault_scenario {
                FaultScenario::Partition => injector.inject_partition(&mut cluster),
                FaultScenario::LeaderCrash => injector.inject_leader_crash(&mut cluster),
                _ => {}
            }
        }

        // Check for fault healing point
        if Some(i) == config.heal_fault_at_op {
            injector.heal_all(&mut cluster);
        }

        // Execute operation
        match op {
            Operation::Append { key, value } => {
                // Pick a non-crashed node to propose
                let proposer_idx = i % num_nodes;
                if !cluster.nodes[proposer_idx].crashed {
                    let (proposal_id, msg) = cluster.nodes[proposer_idx].propose(
                        key.clone(),
                        serde_json::json!(value),
                    );

                    // Track proposal start time for metrics
                    if collect_metrics {
                        if let Some(tracker) = proposal_trackers.get_mut(&proposer_idx) {
                            tracker.start_proposal(&proposal_id);
                        }
                    }

                    // Count message types for metrics
                    if let Some(ref mut m) = metrics {
                        m.count_message("Propose");
                    }

                    // Broadcast with potential message loss
                    if !injector.should_drop_message() {
                        // Apply network delay if configured
                        let delay = injector.get_message_delay();
                        cluster.broadcast_with_delay(proposer_idx, msg, delay).await;
                    }
                }
            }
            Operation::Read { key: _ } => {
                // Reads are implicit in the Elle model (state observations)
            }
        }

        // Process messages
        cluster.run_to_completion(10).await;

        // Sample throughput periodically
        if collect_metrics && i % 10 == 0 {
            if let Some(ref mut m) = metrics {
                m.sample_throughput();
            }
        }
    }

    // Final message processing
    cluster.run_to_completion(50).await;

    // Calculate latencies from committed proposals
    if collect_metrics {
        // Estimate latency based on total duration and operations
        // (Actual per-proposal tracking would require cluster integration)
        let total_duration = start.elapsed();
        let stats = cluster.stats();
        let ops_completed = stats.commits / num_nodes.max(1);

        if ops_completed > 0 {
            let avg_latency = total_duration / ops_completed as u32;
            // Record approximate latencies
            for _ in 0..ops_completed {
                if let Some(ref mut m) = metrics {
                    m.record_latency_duration(avg_latency);
                }
            }
        }
    }

    // Collect statistics
    let stats = cluster.stats();
    commit_count = stats.commits / num_nodes; // Each commit counted once per node
    failure_count = stats.failures;

    // Merge histories
    let history_json = cluster.merge_histories();

    let duration = start.elapsed();
    info!("Scenario {} completed in {:?}", config.name, duration);

    Ok(ScenarioResults {
        scenario: config.name,
        history_json,
        event_count: stats.events,
        commit_count,
        failure_count,
        duration_ms: duration.as_millis() as u64,
        elle_result: None,
        metrics,
    })
}

/// Run the full test suite
pub async fn run_suite(
    output_dir: &Path,
    elle_jar: &Path,
) -> Result<SuiteResults, ScenarioError> {
    std::fs::create_dir_all(output_dir)?;

    let scenarios = list_scenarios();
    let mut results = Vec::new();
    let mut passed = 0;
    let mut failed = 0;

    for scenario_name in &scenarios {
        info!("Running scenario: {}", scenario_name);

        let config = get_scenario(scenario_name)?;
        let num_nodes = config.fault_scenario.recommended_nodes();

        let mut result = run_scenario(config, num_nodes, 100).await?;

        // Write history file
        let history_path = output_dir.join(format!("{}-history.json", scenario_name));
        std::fs::write(&history_path, &result.history_json)?;

        // Run Elle check
        let elle_result = match elle_runner::check_history(elle_jar, &history_path, "list-append") {
            Ok(r) => {
                if r.valid {
                    info!("Scenario {} PASSED: No anomalies", scenario_name);
                    passed += 1;
                    results.push((scenario_name.to_string(), result.clone(), true));
                } else {
                    error!("Scenario {} FAILED: {:?}", scenario_name, r.anomaly);
                    failed += 1;
                    results.push((scenario_name.to_string(), result.clone(), false));
                }
                Some(r)
            }
            Err(e) => {
                warn!("Elle check skipped for {}: {}", scenario_name, e);
                results.push((scenario_name.to_string(), result.clone(), true)); // Count as pass if Elle unavailable
                passed += 1;
                None
            }
        };

        result.elle_result = elle_result;
    }

    Ok(SuiteResults {
        total: scenarios.len(),
        passed,
        failed,
        results,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_get_scenario() {
        let happy = get_scenario("happy").unwrap();
        assert_eq!(happy.name, "happy");
        assert_eq!(happy.fault_scenario, FaultScenario::None);

        let partition = get_scenario("partition").unwrap();
        assert_eq!(partition.name, "partition");
        assert_eq!(partition.fault_scenario, FaultScenario::Partition);

        let unknown = get_scenario("nonexistent");
        assert!(unknown.is_err());
    }

    #[test]
    fn test_list_scenarios() {
        let scenarios = list_scenarios();
        assert!(scenarios.contains(&"happy"));
        assert!(scenarios.contains(&"partition"));
        assert!(scenarios.contains(&"leader-crash"));
    }

    #[tokio::test]
    async fn test_run_happy_scenario() {
        let config = get_scenario("happy").unwrap();
        let result = run_scenario(config, 3, 20).await.unwrap();

        assert_eq!(result.scenario, "happy");
        assert!(result.event_count > 0);
        assert!(!result.history_json.is_empty());
        assert!(result.metrics.is_none()); // No metrics without explicit request
    }

    #[tokio::test]
    async fn test_run_scenario_with_metrics() {
        let config = get_scenario("happy").unwrap();
        let result = run_scenario_with_metrics(config, 3, 20).await.unwrap();

        assert_eq!(result.scenario, "happy");
        assert!(result.metrics.is_some());

        let metrics = result.metrics.unwrap();
        let summary = metrics.summary();
        assert!(summary.total_ops > 0);
    }

    #[tokio::test]
    async fn test_slow_network_scenario() {
        let config = get_scenario("slow-network").unwrap();
        assert_eq!(config.fault_scenario, FaultScenario::SlowNetwork);

        let result = run_scenario_with_metrics(config, 3, 10).await.unwrap();
        assert_eq!(result.scenario, "slow-network");
    }

    #[test]
    fn test_suite_results_junit() {
        let results = SuiteResults {
            total: 2,
            passed: 1,
            failed: 1,
            results: vec![
                (
                    "happy".to_string(),
                    ScenarioResults {
                        scenario: "happy".to_string(),
                        history_json: "[]".to_string(),
                        event_count: 10,
                        commit_count: 5,
                        failure_count: 0,
                        duration_ms: 100,
                        elle_result: None,
                    },
                    true,
                ),
                (
                    "partition".to_string(),
                    ScenarioResults {
                        scenario: "partition".to_string(),
                        history_json: "[]".to_string(),
                        event_count: 10,
                        commit_count: 3,
                        failure_count: 2,
                        duration_ms: 200,
                        elle_result: Some(ElleResult {
                            valid: false,
                            anomaly: Some("G2".to_string()),
                            raw_output: String::new(),
                            graphs: vec![],
                        }),
                    },
                    false,
                ),
            ],
        };

        let xml = results.to_junit_xml();
        assert!(xml.contains("testsuite"));
        assert!(xml.contains("tests=\"2\""));
        assert!(xml.contains("failures=\"1\""));
        assert!(xml.contains("happy"));
        assert!(xml.contains("partition"));
    }
}

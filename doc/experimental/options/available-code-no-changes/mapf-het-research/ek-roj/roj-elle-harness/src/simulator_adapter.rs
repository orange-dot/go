//! Simulator adapter for Elle testing
//!
//! Connects to the Go simulator's ROJ cluster via HTTP API,
//! allowing Elle consistency testing against realistic CAN bus
//! simulation instead of in-memory channels.

use crate::elle_runner::{self, ElleResult};
use futures_util::{SinkExt, StreamExt};
use roj_core::ElleEvent;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::Path;
use std::pin::Pin;
use std::time::Duration;
use thiserror::Error;
use tokio_tungstenite::{connect_async, tungstenite::Message};
use tracing::{debug, info, warn, error};

/// Default simulator API base URL
pub const DEFAULT_SIMULATOR_URL: &str = "http://localhost:8001";

/// Errors from simulator adapter
#[derive(Error, Debug)]
pub enum SimulatorError {
    #[error("Failed to connect to simulator at {0}: {1}")]
    ConnectionFailed(String, String),

    #[error("Simulator API error: {0}")]
    ApiError(String),

    #[error("Request failed: {0}")]
    RequestFailed(#[from] reqwest::Error),

    #[error("Invalid response: {0}")]
    InvalidResponse(String),

    #[error("Scenario execution failed: {0}")]
    ScenarioFailed(String),

    #[error("Elle error: {0}")]
    ElleError(#[from] elle_runner::ElleError),

    #[error("IO error: {0}")]
    IoError(#[from] std::io::Error),
}

/// Simulator cluster status
#[derive(Debug, Clone, Deserialize)]
pub struct ClusterStatus {
    pub running: bool,
    pub nodes: usize,
    #[serde(rename = "activeNodes")]
    pub active_nodes: usize,
    #[serde(rename = "partitionedPairs")]
    pub partitioned_pairs: usize,
}

/// Simulator cluster statistics
#[derive(Debug, Clone, Deserialize)]
pub struct ClusterStats {
    #[serde(rename = "NumNodes")]
    pub num_nodes: usize,
    #[serde(rename = "ActiveNodes")]
    pub active_nodes: usize,
    #[serde(rename = "PartitionedPairs")]
    pub partitioned_pairs: usize,
    #[serde(rename = "TotalEvents")]
    pub total_events: usize,
    #[serde(rename = "TotalProposals")]
    pub total_proposals: usize,
    #[serde(rename = "TotalCommits")]
    pub total_commits: usize,
}

/// Proposal request
#[derive(Debug, Clone, Serialize)]
struct ProposeRequest {
    #[serde(rename = "nodeId")]
    node_id: usize,
    key: String,
    value: serde_json::Value,
}

/// Crash/recover request
#[derive(Debug, Clone, Serialize)]
struct NodeRequest {
    #[serde(rename = "nodeId")]
    node_id: usize,
}

/// Partition request
#[derive(Debug, Clone, Serialize)]
struct PartitionRequest {
    #[serde(rename = "groupA")]
    group_a: Vec<usize>,
    #[serde(rename = "groupB")]
    group_b: Vec<usize>,
}

/// Scenario request
#[derive(Debug, Clone, Serialize)]
struct ScenarioRequest {
    scenario: String,
    nodes: usize,
    operations: usize,
}

/// Elle event from simulator (matching Go format)
#[derive(Debug, Clone, Deserialize)]
pub struct SimulatorElleEvent {
    pub index: u64,
    #[serde(rename = "type")]
    pub event_type: String,
    pub f: String,
    pub process: u64,
    pub time: u64,
    pub value: Vec<Vec<serde_json::Value>>,
}

/// Simulator adapter client
pub struct SimulatorAdapter {
    client: reqwest::Client,
    base_url: String,
}

impl SimulatorAdapter {
    /// Create a new simulator adapter
    pub fn new(base_url: &str) -> Self {
        let client = reqwest::Client::builder()
            .timeout(Duration::from_secs(30))
            .build()
            .expect("Failed to create HTTP client");

        Self {
            client,
            base_url: base_url.trim_end_matches('/').to_string(),
        }
    }

    /// Create adapter with default URL
    pub fn default() -> Self {
        Self::new(DEFAULT_SIMULATOR_URL)
    }

    /// Check if simulator is reachable
    pub async fn health_check(&self) -> Result<bool, SimulatorError> {
        let url = format!("{}/health", self.base_url);

        match self.client.get(&url).send().await {
            Ok(resp) => Ok(resp.status().is_success()),
            Err(e) => {
                warn!("Simulator health check failed: {}", e);
                Ok(false)
            }
        }
    }

    /// Wait for simulator to be ready
    pub async fn wait_for_ready(&self, timeout: Duration) -> Result<(), SimulatorError> {
        let start = std::time::Instant::now();

        while start.elapsed() < timeout {
            if self.health_check().await? {
                info!("Simulator is ready at {}", self.base_url);
                return Ok(());
            }
            tokio::time::sleep(Duration::from_millis(500)).await;
        }

        Err(SimulatorError::ConnectionFailed(
            self.base_url.clone(),
            "Timeout waiting for simulator".to_string(),
        ))
    }

    /// Get cluster status
    pub async fn get_status(&self) -> Result<ClusterStatus, SimulatorError> {
        let url = format!("{}/api/roj/status", self.base_url);
        let resp = self.client.get(&url).send().await?;

        if !resp.status().is_success() {
            return Err(SimulatorError::ApiError(format!(
                "Status check failed: {}",
                resp.status()
            )));
        }

        let status: ClusterStatus = resp.json().await?;
        Ok(status)
    }

    /// Get cluster statistics
    pub async fn get_stats(&self) -> Result<ClusterStats, SimulatorError> {
        let url = format!("{}/api/roj/stats", self.base_url);
        let resp = self.client.get(&url).send().await?;

        if !resp.status().is_success() {
            return Err(SimulatorError::ApiError(format!(
                "Stats failed: {}",
                resp.status()
            )));
        }

        let stats: ClusterStats = resp.json().await?;
        Ok(stats)
    }

    /// Submit a proposal through a node
    pub async fn propose(
        &self,
        node_id: usize,
        key: &str,
        value: serde_json::Value,
    ) -> Result<String, SimulatorError> {
        let url = format!("{}/api/roj/propose", self.base_url);
        let req = ProposeRequest {
            node_id,
            key: key.to_string(),
            value,
        };

        let resp = self.client.post(&url).json(&req).send().await?;

        if !resp.status().is_success() {
            return Err(SimulatorError::ApiError(format!(
                "Propose failed: {}",
                resp.status()
            )));
        }

        let result: HashMap<String, serde_json::Value> = resp.json().await?;
        let proposal_id = result
            .get("proposalId")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_string();

        debug!("Proposed via node {}: {} = {:?} -> {}", node_id, key, req.value, proposal_id);
        Ok(proposal_id)
    }

    /// Get node state
    pub async fn get_node_state(
        &self,
        node_id: usize,
    ) -> Result<HashMap<String, serde_json::Value>, SimulatorError> {
        let url = format!("{}/api/roj/state?nodeId={}", self.base_url, node_id);
        let resp = self.client.get(&url).send().await?;

        if !resp.status().is_success() {
            return Err(SimulatorError::ApiError(format!(
                "Get state failed: {}",
                resp.status()
            )));
        }

        let result: HashMap<String, serde_json::Value> = resp.json().await?;
        let state = result
            .get("state")
            .and_then(|v| v.as_object())
            .map(|m| m.iter().map(|(k, v)| (k.clone(), v.clone())).collect())
            .unwrap_or_default();

        Ok(state)
    }

    /// Crash a node
    pub async fn crash_node(&self, node_id: usize) -> Result<(), SimulatorError> {
        let url = format!("{}/api/roj/crash", self.base_url);
        let req = NodeRequest { node_id };

        let resp = self.client.post(&url).json(&req).send().await?;

        if !resp.status().is_success() {
            return Err(SimulatorError::ApiError(format!(
                "Crash failed: {}",
                resp.status()
            )));
        }

        info!("Crashed node {}", node_id);
        Ok(())
    }

    /// Recover a node
    pub async fn recover_node(&self, node_id: usize) -> Result<(), SimulatorError> {
        let url = format!("{}/api/roj/recover", self.base_url);
        let req = NodeRequest { node_id };

        let resp = self.client.post(&url).json(&req).send().await?;

        if !resp.status().is_success() {
            return Err(SimulatorError::ApiError(format!(
                "Recover failed: {}",
                resp.status()
            )));
        }

        info!("Recovered node {}", node_id);
        Ok(())
    }

    /// Create a network partition
    pub async fn partition(&self, group_a: Vec<usize>, group_b: Vec<usize>) -> Result<(), SimulatorError> {
        let url = format!("{}/api/roj/partition", self.base_url);
        let req = PartitionRequest { group_a: group_a.clone(), group_b: group_b.clone() };

        let resp = self.client.post(&url).json(&req).send().await?;

        if !resp.status().is_success() {
            return Err(SimulatorError::ApiError(format!(
                "Partition failed: {}",
                resp.status()
            )));
        }

        info!("Created partition: {:?} <-> {:?}", group_a, group_b);
        Ok(())
    }

    /// Heal all partitions
    pub async fn heal_partitions(&self) -> Result<(), SimulatorError> {
        let url = format!("{}/api/roj/heal", self.base_url);

        let resp = self.client.post(&url).send().await?;

        if !resp.status().is_success() {
            return Err(SimulatorError::ApiError(format!(
                "Heal failed: {}",
                resp.status()
            )));
        }

        info!("Healed all partitions");
        Ok(())
    }

    /// Get Elle history
    pub async fn get_history(&self) -> Result<Vec<SimulatorElleEvent>, SimulatorError> {
        let url = format!("{}/api/roj/history", self.base_url);
        let resp = self.client.get(&url).send().await?;

        if !resp.status().is_success() {
            return Err(SimulatorError::ApiError(format!(
                "Get history failed: {}",
                resp.status()
            )));
        }

        let events: Vec<SimulatorElleEvent> = resp.json().await?;
        Ok(events)
    }

    /// Get history as JSON string
    pub async fn get_history_json(&self) -> Result<String, SimulatorError> {
        let url = format!("{}/api/roj/history", self.base_url);
        let resp = self.client.get(&url).send().await?;

        if !resp.status().is_success() {
            return Err(SimulatorError::ApiError(format!(
                "Get history failed: {}",
                resp.status()
            )));
        }

        let json = resp.text().await?;
        Ok(json)
    }

    /// Clear history
    pub async fn clear_history(&self) -> Result<(), SimulatorError> {
        let url = format!("{}/api/roj/history/clear", self.base_url);

        let resp = self.client.post(&url).send().await?;

        if !resp.status().is_success() {
            return Err(SimulatorError::ApiError(format!(
                "Clear history failed: {}",
                resp.status()
            )));
        }

        debug!("Cleared history");
        Ok(())
    }

    /// Run a predefined scenario
    pub async fn run_scenario(
        &self,
        scenario: &str,
        nodes: usize,
        operations: usize,
    ) -> Result<(), SimulatorError> {
        let url = format!("{}/api/roj/scenario/run", self.base_url);
        let req = ScenarioRequest {
            scenario: scenario.to_string(),
            nodes,
            operations,
        };

        let resp = self.client.post(&url).json(&req).send().await?;

        if !resp.status().is_success() {
            return Err(SimulatorError::ApiError(format!(
                "Run scenario failed: {}",
                resp.status()
            )));
        }

        info!("Started scenario '{}' with {} nodes, {} operations", scenario, nodes, operations);
        Ok(())
    }

    /// Run scenario and wait for completion, then return history
    pub async fn run_scenario_and_collect(
        &self,
        scenario: &str,
        nodes: usize,
        operations: usize,
        wait_time: Duration,
    ) -> Result<String, SimulatorError> {
        // Clear history first
        self.clear_history().await?;

        // Start scenario
        self.run_scenario(scenario, nodes, operations).await?;

        // Wait for completion
        tokio::time::sleep(wait_time).await;

        // Collect history
        self.get_history_json().await
    }

    /// Get WebSocket URL for streaming
    pub fn websocket_url(&self) -> String {
        let ws_url = self.base_url
            .replace("http://", "ws://")
            .replace("https://", "wss://");
        format!("{}/api/roj/history/stream", ws_url)
    }

    /// Stream history events via WebSocket
    ///
    /// Returns a receiver that yields events as they occur.
    /// The connection will remain open until the returned receiver is dropped.
    pub async fn stream_history(&self) -> Result<tokio::sync::mpsc::Receiver<SimulatorElleEvent>, SimulatorError> {
        let url = self.websocket_url();
        info!("Connecting to WebSocket: {}", url);

        let (ws_stream, _) = connect_async(&url)
            .await
            .map_err(|e| SimulatorError::ConnectionFailed(url.clone(), e.to_string()))?;

        let (mut _write, mut read) = ws_stream.split();
        let (tx, rx) = tokio::sync::mpsc::channel(100);

        // Spawn a task to read from WebSocket and forward to channel
        tokio::spawn(async move {
            while let Some(msg) = read.next().await {
                match msg {
                    Ok(Message::Text(text)) => {
                        if let Ok(event) = serde_json::from_str::<SimulatorElleEvent>(&text) {
                            if tx.send(event).await.is_err() {
                                break; // Receiver dropped
                            }
                        }
                    }
                    Ok(Message::Close(_)) => break,
                    Err(e) => {
                        warn!("WebSocket error: {}", e);
                        break;
                    }
                    _ => {}
                }
            }
        });

        Ok(rx)
    }

    /// Stream history events with a callback
    ///
    /// Streams events until the callback returns false or an error occurs.
    pub async fn stream_history_with_callback<F>(&self, mut callback: F) -> Result<(), SimulatorError>
    where
        F: FnMut(SimulatorElleEvent) -> bool,
    {
        let url = self.websocket_url();
        info!("Connecting to WebSocket: {}", url);

        let (ws_stream, _) = connect_async(&url)
            .await
            .map_err(|e| SimulatorError::ConnectionFailed(url.clone(), e.to_string()))?;

        let (mut _write, mut read) = ws_stream.split();

        while let Some(msg) = read.next().await {
            match msg {
                Ok(Message::Text(text)) => {
                    if let Ok(event) = serde_json::from_str::<SimulatorElleEvent>(&text) {
                        if !callback(event) {
                            break;
                        }
                    }
                }
                Ok(Message::Close(_)) => break,
                Err(e) => {
                    warn!("WebSocket error: {}", e);
                    break;
                }
                _ => {}
            }
        }

        Ok(())
    }
}

/// Results from simulator-based scenario
#[derive(Debug, Clone)]
pub struct SimulatorScenarioResults {
    pub scenario: String,
    pub history_json: String,
    pub event_count: usize,
    pub duration_ms: u64,
    pub elle_result: Option<ElleResult>,
}

/// Run a scenario using the simulator and check with Elle
pub async fn run_simulator_scenario(
    adapter: &SimulatorAdapter,
    scenario: &str,
    nodes: usize,
    operations: usize,
    elle_jar: Option<&Path>,
) -> Result<SimulatorScenarioResults, SimulatorError> {
    let start = std::time::Instant::now();

    // Estimate wait time based on operations
    let wait_time = Duration::from_millis((operations as u64 * 15) + 1000);

    // Run scenario
    let history_json = adapter
        .run_scenario_and_collect(scenario, nodes, operations, wait_time)
        .await?;

    // Parse history to count events
    let events: Vec<SimulatorElleEvent> = serde_json::from_str(&history_json)
        .unwrap_or_default();

    let duration_ms = start.elapsed().as_millis() as u64;

    // Check with Elle if JAR available
    let elle_result = if let Some(jar_path) = elle_jar {
        // Write history to temp file
        let temp_path = std::env::temp_dir().join(format!("roj-sim-{}.json", scenario));
        std::fs::write(&temp_path, &history_json)?;

        match elle_runner::check_history(jar_path, &temp_path, "list-append") {
            Ok(result) => {
                if result.valid {
                    info!("Simulator scenario '{}' PASSED: No anomalies", scenario);
                } else {
                    error!("Simulator scenario '{}' FAILED: {:?}", scenario, result.anomaly);
                }
                Some(result)
            }
            Err(e) => {
                warn!("Elle check failed: {}", e);
                None
            }
        }
    } else {
        None
    };

    Ok(SimulatorScenarioResults {
        scenario: scenario.to_string(),
        history_json,
        event_count: events.len(),
        duration_ms,
        elle_result,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_default_adapter() {
        let adapter = SimulatorAdapter::default();
        assert_eq!(adapter.base_url, DEFAULT_SIMULATOR_URL);
    }

    #[test]
    fn test_custom_url() {
        let adapter = SimulatorAdapter::new("http://localhost:9000/");
        assert_eq!(adapter.base_url, "http://localhost:9000");
    }
}

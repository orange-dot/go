//! Performance metrics collection for Elle testing
//!
//! Collects latency histograms and throughput metrics for analysis.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::time::{Duration, Instant};

/// Performance metrics collection
#[derive(Debug, Clone, Default)]
pub struct Metrics {
    /// Proposal-to-commit latencies in microseconds
    pub latencies_us: Vec<u64>,
    /// Operations per second samples
    pub throughput_samples: Vec<f64>,
    /// Message counts by type
    pub message_counts: HashMap<String, u64>,
    /// Start time for throughput calculation
    start_time: Option<Instant>,
    /// Operation count for throughput
    op_count: usize,
}

/// Summary statistics
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MetricsSummary {
    pub min_latency_us: u64,
    pub max_latency_us: u64,
    pub avg_latency_us: f64,
    pub p50_latency_us: u64,
    pub p95_latency_us: u64,
    pub p99_latency_us: u64,
    pub total_ops: usize,
    pub avg_throughput_ops_sec: f64,
}

/// Full metrics export format
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MetricsExport {
    pub scenario: String,
    pub latencies_us: Vec<u64>,
    pub throughput_samples: Vec<f64>,
    pub message_counts: HashMap<String, u64>,
    pub summary: MetricsSummary,
}

impl Metrics {
    /// Create new metrics collector
    pub fn new() -> Self {
        Self {
            latencies_us: Vec::new(),
            throughput_samples: Vec::new(),
            message_counts: HashMap::new(),
            start_time: None,
            op_count: 0,
        }
    }

    /// Start timing for throughput measurement
    pub fn start(&mut self) {
        self.start_time = Some(Instant::now());
        self.op_count = 0;
    }

    /// Record a latency measurement
    pub fn record_latency(&mut self, proposal_time: Instant, commit_time: Instant) {
        let latency = commit_time.duration_since(proposal_time);
        self.latencies_us.push(latency.as_micros() as u64);
        self.op_count += 1;
    }

    /// Record latency from duration
    pub fn record_latency_duration(&mut self, latency: Duration) {
        self.latencies_us.push(latency.as_micros() as u64);
        self.op_count += 1;
    }

    /// Record throughput sample
    pub fn record_throughput(&mut self, ops: usize, duration: Duration) {
        if duration.as_secs_f64() > 0.0 {
            let ops_per_sec = ops as f64 / duration.as_secs_f64();
            self.throughput_samples.push(ops_per_sec);
        }
    }

    /// Sample current throughput (call periodically)
    pub fn sample_throughput(&mut self) {
        if let Some(start) = self.start_time {
            let elapsed = start.elapsed();
            if elapsed.as_secs_f64() > 0.0 {
                let ops_per_sec = self.op_count as f64 / elapsed.as_secs_f64();
                self.throughput_samples.push(ops_per_sec);
            }
        }
    }

    /// Increment message count
    pub fn count_message(&mut self, msg_type: &str) {
        *self.message_counts.entry(msg_type.to_string()).or_insert(0) += 1;
    }

    /// Calculate percentile from sorted values
    fn percentile(sorted: &[u64], p: f64) -> u64 {
        if sorted.is_empty() {
            return 0;
        }
        let idx = ((sorted.len() as f64 - 1.0) * p / 100.0).round() as usize;
        sorted[idx.min(sorted.len() - 1)]
    }

    /// Calculate summary statistics
    pub fn summary(&self) -> MetricsSummary {
        let mut sorted = self.latencies_us.clone();
        sorted.sort();

        let min_latency_us = sorted.first().copied().unwrap_or(0);
        let max_latency_us = sorted.last().copied().unwrap_or(0);

        let avg_latency_us = if sorted.is_empty() {
            0.0
        } else {
            sorted.iter().sum::<u64>() as f64 / sorted.len() as f64
        };

        let p50_latency_us = Self::percentile(&sorted, 50.0);
        let p95_latency_us = Self::percentile(&sorted, 95.0);
        let p99_latency_us = Self::percentile(&sorted, 99.0);

        let avg_throughput_ops_sec = if self.throughput_samples.is_empty() {
            // Calculate from total if no samples
            if let Some(start) = self.start_time {
                let elapsed = start.elapsed().as_secs_f64();
                if elapsed > 0.0 {
                    self.op_count as f64 / elapsed
                } else {
                    0.0
                }
            } else {
                0.0
            }
        } else {
            self.throughput_samples.iter().sum::<f64>() / self.throughput_samples.len() as f64
        };

        MetricsSummary {
            min_latency_us,
            max_latency_us,
            avg_latency_us,
            p50_latency_us,
            p95_latency_us,
            p99_latency_us,
            total_ops: self.latencies_us.len(),
            avg_throughput_ops_sec,
        }
    }

    /// Export as JSON string
    pub fn export_json(&self, scenario: &str) -> String {
        let export = MetricsExport {
            scenario: scenario.to_string(),
            latencies_us: self.latencies_us.clone(),
            throughput_samples: self.throughput_samples.clone(),
            message_counts: self.message_counts.clone(),
            summary: self.summary(),
        };
        serde_json::to_string_pretty(&export).unwrap_or_else(|_| "{}".to_string())
    }

    /// Export as CSV string
    pub fn export_csv(&self) -> String {
        let mut csv = String::from("index,latency_us\n");
        for (i, lat) in self.latencies_us.iter().enumerate() {
            csv.push_str(&format!("{},{}\n", i, lat));
        }
        csv
    }
}

/// Proposal timing tracker for a single node
#[derive(Debug, Clone, Default)]
pub struct ProposalTracker {
    /// Map of proposal_id to start time
    start_times: HashMap<String, Instant>,
}

impl ProposalTracker {
    pub fn new() -> Self {
        Self {
            start_times: HashMap::new(),
        }
    }

    /// Record when a proposal was started
    pub fn start_proposal(&mut self, proposal_id: &str) {
        self.start_times.insert(proposal_id.to_string(), Instant::now());
    }

    /// Complete a proposal and return the latency
    pub fn complete_proposal(&mut self, proposal_id: &str) -> Option<Duration> {
        self.start_times
            .remove(proposal_id)
            .map(|start| start.elapsed())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread::sleep;

    #[test]
    fn test_metrics_basic() {
        let mut metrics = Metrics::new();

        // Record some latencies
        metrics.record_latency_duration(Duration::from_micros(100));
        metrics.record_latency_duration(Duration::from_micros(200));
        metrics.record_latency_duration(Duration::from_micros(300));

        let summary = metrics.summary();
        assert_eq!(summary.total_ops, 3);
        assert_eq!(summary.min_latency_us, 100);
        assert_eq!(summary.max_latency_us, 300);
        assert!((summary.avg_latency_us - 200.0).abs() < 1.0);
    }

    #[test]
    fn test_percentiles() {
        let mut metrics = Metrics::new();

        // Add 100 values: 1, 2, 3, ..., 100
        for i in 1..=100 {
            metrics.record_latency_duration(Duration::from_micros(i));
        }

        let summary = metrics.summary();
        assert_eq!(summary.p50_latency_us, 50);
        assert_eq!(summary.p95_latency_us, 95);
        assert_eq!(summary.p99_latency_us, 99);
    }

    #[test]
    fn test_proposal_tracker() {
        let mut tracker = ProposalTracker::new();

        tracker.start_proposal("prop1");
        sleep(Duration::from_millis(10));

        let latency = tracker.complete_proposal("prop1");
        assert!(latency.is_some());
        assert!(latency.unwrap() >= Duration::from_millis(10));

        // Non-existent proposal
        assert!(tracker.complete_proposal("prop2").is_none());
    }

    #[test]
    fn test_export_json() {
        let mut metrics = Metrics::new();
        metrics.record_latency_duration(Duration::from_micros(1000));

        let json = metrics.export_json("test");
        assert!(json.contains("\"scenario\": \"test\""));
        assert!(json.contains("\"total_ops\": 1"));
    }
}

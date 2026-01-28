//! Workload generation for consistency testing
//!
//! Generates append and read operations for Elle list-append model.

use rand::Rng;
use serde::{Deserialize, Serialize};

/// A single operation in the workload
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum Operation {
    /// Append a value to a key
    Append { key: String, value: i64 },
    /// Read the current value of a key
    Read { key: String },
}

/// Workload configuration
#[derive(Debug, Clone)]
pub struct WorkloadConfig {
    /// Number of distinct keys
    pub num_keys: usize,
    /// Ratio of append operations (0.0-1.0)
    pub append_ratio: f64,
    /// Starting value for appends
    pub start_value: i64,
}

impl Default for WorkloadConfig {
    fn default() -> Self {
        Self {
            num_keys: 5,
            append_ratio: 0.8,
            start_value: 1,
        }
    }
}

/// Workload generator
pub struct WorkloadGenerator {
    config: WorkloadConfig,
    rng: rand::rngs::ThreadRng,
    next_value: i64,
}

impl WorkloadGenerator {
    /// Create a new workload generator
    pub fn new(config: WorkloadConfig) -> Self {
        let start_value = config.start_value;
        Self {
            config,
            rng: rand::thread_rng(),
            next_value: start_value,
        }
    }

    /// Generate the next operation
    pub fn next(&mut self) -> Operation {
        let key = format!("key_{}", self.rng.gen_range(0..self.config.num_keys));

        if self.rng.gen::<f64>() < self.config.append_ratio {
            let value = self.next_value;
            self.next_value += 1;
            Operation::Append { key, value }
        } else {
            Operation::Read { key }
        }
    }

    /// Generate a batch of operations
    pub fn generate(&mut self, count: usize) -> Vec<Operation> {
        (0..count).map(|_| self.next()).collect()
    }

    /// Generate operations distributed across nodes
    pub fn generate_per_node(&mut self, count: usize, num_nodes: usize) -> Vec<Vec<Operation>> {
        let mut per_node: Vec<Vec<Operation>> = vec![Vec::new(); num_nodes];
        let ops_per_node = count / num_nodes;
        let remainder = count % num_nodes;

        for i in 0..num_nodes {
            let ops_count = ops_per_node + if i < remainder { 1 } else { 0 };
            per_node[i] = self.generate(ops_count);
        }

        per_node
    }
}

/// Predefined workload patterns
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WorkloadPattern {
    /// All appends (write-heavy)
    WriteHeavy,
    /// 80% appends, 20% reads (typical)
    Mixed,
    /// 50% appends, 50% reads (balanced)
    Balanced,
    /// 20% appends, 80% reads (read-heavy)
    ReadHeavy,
    /// All operations on single key (contention)
    SingleKey,
}

impl WorkloadPattern {
    /// Get workload config for this pattern
    pub fn config(&self) -> WorkloadConfig {
        match self {
            WorkloadPattern::WriteHeavy => WorkloadConfig {
                append_ratio: 1.0,
                ..Default::default()
            },
            WorkloadPattern::Mixed => WorkloadConfig::default(),
            WorkloadPattern::Balanced => WorkloadConfig {
                append_ratio: 0.5,
                ..Default::default()
            },
            WorkloadPattern::ReadHeavy => WorkloadConfig {
                append_ratio: 0.2,
                ..Default::default()
            },
            WorkloadPattern::SingleKey => WorkloadConfig {
                num_keys: 1,
                append_ratio: 0.8,
                ..Default::default()
            },
        }
    }
}

impl std::str::FromStr for WorkloadPattern {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "write-heavy" | "write_heavy" => Ok(WorkloadPattern::WriteHeavy),
            "mixed" => Ok(WorkloadPattern::Mixed),
            "balanced" => Ok(WorkloadPattern::Balanced),
            "read-heavy" | "read_heavy" => Ok(WorkloadPattern::ReadHeavy),
            "single-key" | "single_key" => Ok(WorkloadPattern::SingleKey),
            _ => Err(format!("Unknown workload pattern: {}", s)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_workload_generator_appends() {
        let config = WorkloadConfig {
            append_ratio: 1.0,
            num_keys: 2,
            start_value: 100,
        };
        let mut gen = WorkloadGenerator::new(config);

        let ops = gen.generate(10);

        assert_eq!(ops.len(), 10);
        for op in ops {
            assert!(matches!(op, Operation::Append { .. }));
        }
    }

    #[test]
    fn test_workload_generator_mixed() {
        let config = WorkloadConfig {
            append_ratio: 0.5,
            num_keys: 3,
            ..Default::default()
        };
        let mut gen = WorkloadGenerator::new(config);

        let ops = gen.generate(100);

        let appends = ops.iter().filter(|op| matches!(op, Operation::Append { .. })).count();
        let reads = ops.iter().filter(|op| matches!(op, Operation::Read { .. })).count();

        // Should be roughly 50/50 (with some variance)
        assert!(appends > 30 && appends < 70);
        assert!(reads > 30 && reads < 70);
    }

    #[test]
    fn test_workload_per_node() {
        let config = WorkloadConfig::default();
        let mut gen = WorkloadGenerator::new(config);

        let per_node = gen.generate_per_node(100, 5);

        assert_eq!(per_node.len(), 5);
        let total: usize = per_node.iter().map(|v| v.len()).sum();
        assert_eq!(total, 100);
    }

    #[test]
    fn test_workload_value_sequence() {
        let config = WorkloadConfig {
            append_ratio: 1.0,
            start_value: 1,
            ..Default::default()
        };
        let mut gen = WorkloadGenerator::new(config);

        let ops = gen.generate(5);
        let values: Vec<i64> = ops
            .iter()
            .filter_map(|op| {
                if let Operation::Append { value, .. } = op {
                    Some(*value)
                } else {
                    None
                }
            })
            .collect();

        assert_eq!(values, vec![1, 2, 3, 4, 5]);
    }
}

//! Stigmergy-based thermal optimization for ROJ.
//!
//! Implements:
//! - Thermal tags with exponential decay
//! - Tag broadcast to k=7 neighbors
//! - Rank-based power adjustment
//! - Temperature variance minimization
//!
//! Target: 88% temperature variance reduction through distributed coordination.

use crate::types::NodeId;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::time::{Duration, Instant};
use tracing::{debug, info};

/// Number of neighbors for gradient computation (scale-free topology)
pub const K_NEIGHBORS: usize = 7;

/// Thermal tag decay rate (exponential decay constant)
pub const TAG_DECAY_RATE: f64 = 0.1; // Per second

/// Maximum tag age before removal
pub const MAX_TAG_AGE_SECS: f64 = 30.0;

/// Power adjustment step size
pub const POWER_ADJUSTMENT_STEP: f64 = 0.05; // 5% per iteration

/// Target temperature for normalization (Celsius)
pub const TARGET_TEMP_C: f64 = 45.0;

/// Temperature variance threshold for convergence
pub const VARIANCE_THRESHOLD: f64 = 5.0;

/// Thermal tag deposited by modules
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ThermalTag {
    /// Module that deposited the tag
    pub source: NodeId,
    /// Current temperature reading (Celsius)
    pub temperature: f64,
    /// Power output at time of tagging (0-1 normalized)
    pub power_level: f64,
    /// When the tag was created
    pub created_at: f64, // Unix timestamp
    /// Initial tag strength (decays over time)
    pub initial_strength: f64,
}

impl ThermalTag {
    /// Create a new thermal tag
    pub fn new(source: NodeId, temperature: f64, power_level: f64) -> Self {
        Self {
            source,
            temperature,
            power_level,
            created_at: crate::types::unix_timestamp() as f64,
            initial_strength: 1.0,
        }
    }

    /// Calculate current strength with exponential decay
    pub fn strength(&self, current_time: f64) -> f64 {
        let age = current_time - self.created_at;
        if age < 0.0 || age > MAX_TAG_AGE_SECS {
            return 0.0;
        }
        self.initial_strength * (-TAG_DECAY_RATE * age).exp()
    }

    /// Check if tag has expired
    pub fn is_expired(&self, current_time: f64) -> bool {
        (current_time - self.created_at) > MAX_TAG_AGE_SECS
    }
}

/// Stigmergy messages for inter-module coordination
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum StigmergyMessage {
    /// Broadcast thermal tag to neighbors
    #[serde(rename = "THERMAL_TAG")]
    ThermalTagBroadcast {
        from: NodeId,
        tag: ThermalTag,
    },

    /// Request current thermal state from neighbor
    #[serde(rename = "THERMAL_QUERY")]
    ThermalQuery {
        from: NodeId,
    },

    /// Response with thermal state
    #[serde(rename = "THERMAL_STATE")]
    ThermalState {
        from: NodeId,
        temperature: f64,
        power_level: f64,
        rank: u8,
    },
}

impl StigmergyMessage {
    pub fn to_bytes(&self) -> Result<Vec<u8>, serde_json::Error> {
        serde_json::to_vec(self)
    }

    pub fn from_bytes(bytes: &[u8]) -> Result<Self, serde_json::Error> {
        serde_json::from_slice(bytes)
    }
}

/// Neighbor thermal state
#[derive(Debug, Clone)]
pub struct NeighborState {
    pub node_id: NodeId,
    pub temperature: f64,
    pub power_level: f64,
    pub rank: u8,
    pub last_update: Instant,
}

/// Power adjustment recommendation
#[derive(Debug, Clone, Copy)]
pub enum PowerAdjustment {
    /// Increase power by given amount (0-1)
    Increase(f64),
    /// Decrease power by given amount (0-1)
    Decrease(f64),
    /// No change needed
    Hold,
}

/// Stigmergy controller for thermal optimization
pub struct StigmergyController {
    /// Our node ID
    node_id: NodeId,
    /// Our current temperature (Celsius)
    temperature: f64,
    /// Our current power level (0-1)
    power_level: f64,
    /// Our rank in thermal gradient (0=coolest, 255=hottest)
    rank: u8,
    /// Neighbor k=7 selection
    neighbors: HashMap<NodeId, NeighborState>,
    /// Received thermal tags (for gradient computation)
    tags: Vec<ThermalTag>,
    /// Last power adjustment time
    last_adjustment: Instant,
    /// Temperature history for variance computation
    temp_history: Vec<(f64, f64)>, // (timestamp, temperature)
    /// Callback for power adjustment
    on_power_adjust: Option<Box<dyn Fn(PowerAdjustment) + Send + Sync>>,
}

impl StigmergyController {
    /// Create new stigmergy controller
    pub fn new(node_id: NodeId) -> Self {
        Self {
            node_id,
            temperature: TARGET_TEMP_C,
            power_level: 0.5,
            rank: 128,
            neighbors: HashMap::new(),
            tags: Vec::new(),
            last_adjustment: Instant::now(),
            temp_history: Vec::new(),
            on_power_adjust: None,
        }
    }

    /// Set power adjustment callback
    pub fn set_power_callback<F>(&mut self, callback: F)
    where
        F: Fn(PowerAdjustment) + Send + Sync + 'static,
    {
        self.on_power_adjust = Some(Box::new(callback));
    }

    /// Update our temperature reading
    pub fn set_temperature(&mut self, temp: f64) {
        self.temperature = temp;
        self.temp_history.push((
            crate::types::unix_timestamp() as f64,
            temp,
        ));

        // Keep only recent history (last 60 seconds)
        let cutoff = crate::types::unix_timestamp() as f64 - 60.0;
        self.temp_history.retain(|(t, _)| *t > cutoff);

        // Update rank based on temperature
        self.update_rank();
    }

    /// Update our power level
    pub fn set_power_level(&mut self, level: f64) {
        self.power_level = level.clamp(0.0, 1.0);
    }

    /// Add or update a neighbor
    pub fn add_neighbor(&mut self, node_id: NodeId, temp: f64, power: f64, rank: u8) {
        self.neighbors.insert(
            node_id.clone(),
            NeighborState {
                node_id,
                temperature: temp,
                power_level: power,
                rank,
                last_update: Instant::now(),
            },
        );

        // Keep only k neighbors (k=7)
        if self.neighbors.len() > K_NEIGHBORS {
            // Remove oldest
            if let Some(oldest) = self
                .neighbors
                .iter()
                .min_by_key(|(_, s)| s.last_update)
                .map(|(k, _)| k.clone())
            {
                self.neighbors.remove(&oldest);
            }
        }
    }

    /// Generate thermal tag for broadcast
    pub fn generate_tag(&self) -> ThermalTag {
        ThermalTag::new(
            self.node_id.clone(),
            self.temperature,
            self.power_level,
        )
    }

    /// Handle incoming thermal tag
    pub fn handle_tag(&mut self, tag: ThermalTag) {
        // Update neighbor state
        self.add_neighbor(
            tag.source.clone(),
            tag.temperature,
            tag.power_level,
            self.compute_rank_for_temp(tag.temperature),
        );

        // Store tag for gradient computation
        self.tags.push(tag);

        // Cleanup old tags
        let now = crate::types::unix_timestamp() as f64;
        self.tags.retain(|t| !t.is_expired(now));
    }

    /// Handle incoming message
    pub fn handle_message(&mut self, msg: StigmergyMessage) -> Option<StigmergyMessage> {
        match msg {
            StigmergyMessage::ThermalTagBroadcast { tag, .. } => {
                self.handle_tag(tag);
                None
            }

            StigmergyMessage::ThermalQuery { from } => {
                Some(StigmergyMessage::ThermalState {
                    from: self.node_id.clone(),
                    temperature: self.temperature,
                    power_level: self.power_level,
                    rank: self.rank,
                })
            }

            StigmergyMessage::ThermalState { from, temperature, power_level, rank } => {
                self.add_neighbor(from, temperature, power_level, rank);
                None
            }
        }
    }

    /// Compute thermal gradient from neighbors
    pub fn compute_gradient(&self) -> f64 {
        if self.neighbors.is_empty() {
            return 0.0;
        }

        // Gradient = (1/k) * Σ(neighbor_temp - my_temp)
        let sum: f64 = self
            .neighbors
            .values()
            .map(|n| n.temperature - self.temperature)
            .sum();

        sum / self.neighbors.len() as f64
    }

    /// Compute weighted gradient using tag strengths
    pub fn compute_weighted_gradient(&self) -> f64 {
        let now = crate::types::unix_timestamp() as f64;
        let mut sum = 0.0;
        let mut total_weight = 0.0;

        for tag in &self.tags {
            let weight = tag.strength(now);
            if weight > 0.01 {
                sum += weight * (tag.temperature - self.temperature);
                total_weight += weight;
            }
        }

        if total_weight > 0.0 {
            sum / total_weight
        } else {
            self.compute_gradient()
        }
    }

    /// Determine power adjustment based on gradient
    pub fn compute_power_adjustment(&self) -> PowerAdjustment {
        let gradient = self.compute_weighted_gradient();

        // Negative gradient = neighbors are cooler = I should reduce power
        // Positive gradient = neighbors are hotter = I can increase power

        if gradient < -2.0 {
            // Neighbors significantly cooler - reduce our power
            let amount = (POWER_ADJUSTMENT_STEP * (-gradient / 10.0).min(1.0)).min(0.2);
            PowerAdjustment::Decrease(amount)
        } else if gradient > 2.0 {
            // Neighbors significantly hotter - we can take more load
            let amount = (POWER_ADJUSTMENT_STEP * (gradient / 10.0).min(1.0)).min(0.2);
            PowerAdjustment::Increase(amount)
        } else {
            PowerAdjustment::Hold
        }
    }

    /// Tick - perform periodic optimization
    pub fn tick(&mut self) -> Option<StigmergyMessage> {
        let now = Instant::now();

        // Adjust power periodically (every 1 second)
        if now.duration_since(self.last_adjustment) > Duration::from_secs(1) {
            self.last_adjustment = now;

            let adjustment = self.compute_power_adjustment();
            match adjustment {
                PowerAdjustment::Increase(amount) => {
                    let new_power = (self.power_level + amount).min(1.0);
                    debug!(
                        "Stigmergy: Increasing power from {:.2} to {:.2}",
                        self.power_level, new_power
                    );
                    self.power_level = new_power;
                }
                PowerAdjustment::Decrease(amount) => {
                    let new_power = (self.power_level - amount).max(0.0);
                    debug!(
                        "Stigmergy: Decreasing power from {:.2} to {:.2}",
                        self.power_level, new_power
                    );
                    self.power_level = new_power;
                }
                PowerAdjustment::Hold => {}
            }

            if let Some(ref callback) = self.on_power_adjust {
                callback(adjustment);
            }
        }

        // Generate tag for broadcast
        Some(StigmergyMessage::ThermalTagBroadcast {
            from: self.node_id.clone(),
            tag: self.generate_tag(),
        })
    }

    /// Compute rank (0-255) based on temperature
    fn compute_rank_for_temp(&self, temp: f64) -> u8 {
        // Map temperature to rank (30°C = 0, 60°C = 255)
        let normalized = ((temp - 30.0) / 30.0).clamp(0.0, 1.0);
        (normalized * 255.0) as u8
    }

    /// Update our rank based on temperature
    fn update_rank(&mut self) {
        self.rank = self.compute_rank_for_temp(self.temperature);
    }

    /// Get current temperature variance across known nodes
    pub fn compute_variance(&self) -> f64 {
        let mut temps = vec![self.temperature];
        temps.extend(self.neighbors.values().map(|n| n.temperature));

        if temps.len() < 2 {
            return 0.0;
        }

        let mean: f64 = temps.iter().sum::<f64>() / temps.len() as f64;
        let variance: f64 = temps.iter().map(|t| (t - mean).powi(2)).sum::<f64>()
            / temps.len() as f64;

        variance.sqrt() // Return standard deviation
    }

    /// Check if thermal optimization has converged
    pub fn is_converged(&self) -> bool {
        self.compute_variance() < VARIANCE_THRESHOLD
    }

    /// Get metrics
    pub fn metrics(&self) -> StigmergyMetrics {
        StigmergyMetrics {
            temperature: self.temperature,
            power_level: self.power_level,
            rank: self.rank,
            neighbor_count: self.neighbors.len(),
            gradient: self.compute_weighted_gradient(),
            variance: self.compute_variance(),
            converged: self.is_converged(),
        }
    }
}

/// Metrics for stigmergy controller
#[derive(Debug, Clone)]
pub struct StigmergyMetrics {
    pub temperature: f64,
    pub power_level: f64,
    pub rank: u8,
    pub neighbor_count: usize,
    pub gradient: f64,
    pub variance: f64,
    pub converged: bool,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_thermal_tag_decay() {
        let tag = ThermalTag::new("node1".to_string(), 50.0, 0.5);
        let now = tag.created_at;

        // Initial strength should be 1.0
        assert!((tag.strength(now) - 1.0).abs() < 0.01);

        // After 10 seconds, should decay to ~0.37
        let expected = (-TAG_DECAY_RATE * 10.0).exp();
        assert!((tag.strength(now + 10.0) - expected).abs() < 0.01);

        // After MAX_TAG_AGE, should be 0
        assert_eq!(tag.strength(now + MAX_TAG_AGE_SECS + 1.0), 0.0);
    }

    #[test]
    fn test_gradient_computation() {
        let mut controller = StigmergyController::new("node1".to_string());
        controller.set_temperature(50.0);

        // Add cooler neighbors
        controller.add_neighbor("n2".to_string(), 40.0, 0.5, 100);
        controller.add_neighbor("n3".to_string(), 45.0, 0.5, 110);

        // Gradient should be negative (neighbors cooler)
        let gradient = controller.compute_gradient();
        assert!(gradient < 0.0);
    }

    #[test]
    fn test_power_adjustment() {
        let mut controller = StigmergyController::new("node1".to_string());
        controller.set_temperature(55.0);
        controller.set_power_level(0.8);

        // Add much cooler neighbors
        for i in 0..5 {
            controller.add_neighbor(
                format!("n{}", i),
                40.0,
                0.3,
                80,
            );
        }

        // Should recommend decreasing power
        let adjustment = controller.compute_power_adjustment();
        match adjustment {
            PowerAdjustment::Decrease(_) => {}
            _ => panic!("Expected Decrease adjustment"),
        }
    }

    #[test]
    fn test_variance_computation() {
        let mut controller = StigmergyController::new("node1".to_string());
        controller.set_temperature(45.0);

        // Add neighbors with varying temps
        controller.add_neighbor("n1".to_string(), 40.0, 0.5, 80);
        controller.add_neighbor("n2".to_string(), 50.0, 0.5, 160);
        controller.add_neighbor("n3".to_string(), 45.0, 0.5, 120);

        let variance = controller.compute_variance();
        assert!(variance > 0.0);
    }

    #[test]
    fn test_message_serialization() {
        let tag = ThermalTag::new("node1".to_string(), 45.0, 0.7);
        let msg = StigmergyMessage::ThermalTagBroadcast {
            from: "node1".to_string(),
            tag,
        };

        let bytes = msg.to_bytes().unwrap();
        let parsed = StigmergyMessage::from_bytes(&bytes).unwrap();

        if let StigmergyMessage::ThermalTagBroadcast { from, tag } = parsed {
            assert_eq!(from, "node1");
            assert!((tag.temperature - 45.0).abs() < 0.01);
        } else {
            panic!("Wrong message type");
        }
    }
}

//! EK-KOR v2 - Coordination Field Primitives
//!
//! # Novelty: Potential Field Scheduling
//!
//! Replaces traditional priority-based scheduling with gradient-mediated
//! coordination. Modules publish decaying potential fields; neighbors
//! sample these fields and compute gradients to self-organize.
//!
//! ## Theoretical Basis
//! - Khatib, O. (1986): Real-time obstacle avoidance using potential fields
//! - Extended from spatial path planning to temporal scheduling
//!
//! ## Patent Claims
//! 1. "A distributed real-time operating system using potential field
//!    scheduling wherein processing elements coordinate through indirect
//!    communication via shared decaying gradient fields"

use crate::types::*;
use core::sync::atomic::{AtomicU32, Ordering};

// ============================================================================
// Field Configuration
// ============================================================================

/// Field decay model
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum DecayModel {
    /// f(t) = f0 * exp(-t/tau)
    #[default]
    Exponential,
    /// f(t) = f0 * (1 - t/tau), clamped to 0
    Linear,
    /// f(t) = f0 if t < tau, else 0
    Step,
}

/// Field configuration per component
#[derive(Debug, Clone, Copy)]
pub struct FieldConfig {
    /// Decay time constant (seconds as Q16.16)
    pub decay_tau: Fixed,
    /// Decay function
    pub decay_model: DecayModel,
    /// Floor (clamp)
    pub min_value: Fixed,
    /// Ceiling (clamp)
    pub max_value: Fixed,
    /// Value when no data
    pub default_value: Fixed,
}

impl Default for FieldConfig {
    fn default() -> Self {
        Self {
            decay_tau: Fixed::from_num(0.1), // 100ms
            decay_model: DecayModel::Exponential,
            min_value: Fixed::ZERO,
            max_value: Fixed::ONE,
            default_value: Fixed::ZERO,
        }
    }
}

// ============================================================================
// Field Region (Shared Memory)
// ============================================================================

/// Number of words needed for update flags bitmap
const UPDATE_FLAGS_WORDS: usize = (MAX_MODULES + 31) / 32;

/// Coordination field with seqlock for lock-free consistency
///
/// Uses the classic seqlock pattern:
/// - Writer increments sequence to ODD before write (write in progress)
/// - Writer increments sequence to EVEN after write (write complete)
/// - Reader checks sequence before/after read; retries if mismatched or odd
#[derive(Debug)]
pub struct CoordField {
    /// The actual field data
    pub field: Field,
    /// Sequence counter (odd = write in progress)
    pub sequence: AtomicU32,
}

impl CoordField {
    pub const fn new() -> Self {
        Self {
            field: Field::new(),
            sequence: AtomicU32::new(0),
        }
    }
}

impl Default for CoordField {
    fn default() -> Self {
        Self::new()
    }
}

/// Shared field region (one per cluster)
///
/// This is the "environment" through which modules coordinate.
/// In Rust, we use atomic operations for thread-safe updates.
pub struct FieldRegion {
    /// Published fields from all modules (with seqlock)
    fields: [CoordField; MAX_MODULES],
    /// Bitmask of updated modules (atomic for lock-free updates)
    /// Uses array to support more than 32 modules
    update_flags: [AtomicU32; UPDATE_FLAGS_WORDS],
    /// Last garbage collection timestamp
    last_gc: TimeUs,
}

impl FieldRegion {
    /// Create a new field region
    pub const fn new() -> Self {
        // Initialize arrays using const initialization
        const COORD_FIELD_INIT: CoordField = CoordField {
            field: Field::new(),
            sequence: AtomicU32::new(0),
        };
        const UPDATE_FLAGS_INIT: AtomicU32 = AtomicU32::new(0);

        Self {
            fields: [COORD_FIELD_INIT; MAX_MODULES],
            update_flags: [UPDATE_FLAGS_INIT; UPDATE_FLAGS_WORDS],
            last_gc: 0,
        }
    }

    /// Get coord field for a module (with bounds check)
    pub fn get_coord(&self, module_id: ModuleId) -> Option<&CoordField> {
        if module_id as usize >= MAX_MODULES {
            return None;
        }
        Some(&self.fields[module_id as usize])
    }

    /// Get field for a module (with bounds check)
    pub fn get(&self, module_id: ModuleId) -> Option<&Field> {
        if module_id as usize >= MAX_MODULES {
            return None;
        }
        Some(&self.fields[module_id as usize].field)
    }

    /// Get mutable field for a module
    pub fn get_mut(&mut self, module_id: ModuleId) -> Option<&mut Field> {
        if module_id as usize >= MAX_MODULES {
            return None;
        }
        Some(&mut self.fields[module_id as usize].field)
    }

    /// Set update flag for a module
    fn set_update_flag(&self, module_id: ModuleId) {
        let word = (module_id as usize) / 32;
        let bit = 1u32 << ((module_id as usize) % 32);
        if word < UPDATE_FLAGS_WORDS {
            self.update_flags[word].fetch_or(bit, Ordering::Release);
        }
    }
}

impl Default for FieldRegion {
    fn default() -> Self {
        Self::new()
    }
}

// ============================================================================
// Field Engine
// ============================================================================

/// Field engine for coordination
pub struct FieldEngine {
    /// Configuration per component
    config: [FieldConfig; FIELD_COUNT],
}

impl FieldEngine {
    /// Create a new field engine with default config
    pub fn new() -> Self {
        Self {
            config: [FieldConfig::default(); FIELD_COUNT],
        }
    }

    /// Publish module's coordination field
    ///
    /// Updates the shared field region with this module's current state.
    /// Uses seqlock pattern for lock-free consistency:
    /// 1. Increment sequence to ODD (write in progress)
    /// 2. Memory barrier
    /// 3. Copy field data
    /// 4. Memory barrier
    /// 5. Increment sequence to EVEN (write complete)
    pub fn publish(
        &self,
        region: &mut FieldRegion,
        module_id: ModuleId,
        field: &Field,
        now: TimeUs,
    ) -> Result<()> {
        if module_id as usize >= MAX_MODULES || module_id == INVALID_MODULE_ID {
            return Err(Error::InvalidArg);
        }

        let idx = module_id as usize;

        // Increment sequence to ODD (write in progress)
        region.fields[idx].sequence.fetch_add(1, Ordering::SeqCst);

        // Copy field data
        let stored = &mut region.fields[idx].field;
        stored.components = field.components;
        stored.timestamp = now;
        stored.source = module_id;
        let current_seq = region.fields[idx].sequence.load(Ordering::SeqCst);
        stored.sequence = current_seq as u8;

        // Increment sequence to EVEN (write complete)
        region.fields[idx].sequence.fetch_add(1, Ordering::SeqCst);

        // Mark as updated (atomic) - use array index
        region.set_update_flag(module_id);

        Ok(())
    }

    /// Sample a specific module's field with decay applied (single attempt)
    ///
    /// This is a single-attempt read that may fail with Error::Busy if a write
    /// is in progress (odd sequence) or torn read detected. For automatic retry,
    /// use `sample_consistent()`.
    pub fn sample(
        &self,
        region: &FieldRegion,
        target_id: ModuleId,
        now: TimeUs,
    ) -> Result<Field> {
        let coord = region.get_coord(target_id).ok_or(Error::InvalidArg)?;

        // Read sequence before
        let seq_before = coord.sequence.load(Ordering::SeqCst);
        if seq_before & 1 != 0 {
            // Write in progress (odd sequence), try later
            return Err(Error::Busy);
        }

        let field = &coord.field;

        if field.source == INVALID_MODULE_ID {
            return Err(Error::NotFound);
        }

        let elapsed = now.saturating_sub(field.timestamp);
        let max_age = FIELD_DECAY_TAU_US * 5; // 5 tau = ~99% decay

        if elapsed > max_age {
            return Err(Error::FieldExpired);
        }

        // Copy field data
        let mut result = *field;

        // Read sequence after
        let seq_after = coord.sequence.load(Ordering::SeqCst);
        if seq_after != seq_before {
            // Torn read, caller should retry
            return Err(Error::Busy);
        }

        self.apply_decay(&mut result, elapsed);
        Ok(result)
    }

    /// Sample a specific module's field with automatic retry on torn reads
    ///
    /// Attempts up to max_retries reads until consistent.
    /// Applies decay after successful read.
    pub fn sample_consistent(
        &self,
        region: &FieldRegion,
        target_id: ModuleId,
        now: TimeUs,
        max_retries: u32,
    ) -> Result<Field> {
        for _ in 0..max_retries {
            match self.sample(region, target_id, now) {
                Ok(field) => return Ok(field),
                Err(Error::Busy) => continue, // Retry on torn read
                Err(e) => return Err(e),      // Other errors are final
            }
        }
        Err(Error::Busy) // All retries failed
    }

    /// Sample all k-neighbors and compute aggregate
    ///
    /// Returns weighted average of neighbor fields.
    /// Weights are computed based on:
    /// - Health state (healthy=1.0, suspect=0.5, dead=0.0)
    /// - Logical distance: 1 / (1 + distance/256) - closer neighbors weighted higher
    pub fn sample_neighbors(
        &self,
        region: &FieldRegion,
        neighbors: &[Neighbor],
        now: TimeUs,
    ) -> Field {
        let mut aggregate = Field::new();
        let mut total_weight = Fixed::ZERO;

        for neighbor in neighbors {
            if neighbor.health == HealthState::Dead || neighbor.health == HealthState::Unknown {
                continue;
            }

            // Try sample with automatic retry
            if let Ok(field) = self.sample_consistent(region, neighbor.id, now, 3) {
                // Weight based on health
                let health_weight = match neighbor.health {
                    HealthState::Alive => Fixed::ONE,
                    HealthState::Suspect => Fixed::from_num(0.5),
                    _ => Fixed::ZERO,
                };

                if health_weight <= Fixed::ZERO {
                    continue;
                }

                // Distance factor: 1 / (1 + distance/256)
                // Closer neighbors weighted higher (matching C implementation)
                let distance_factor = if neighbor.logical_distance > 0 {
                    let dist_scaled = Fixed::from_num(neighbor.logical_distance as f32 / 256.0);
                    Fixed::ONE / (Fixed::ONE + dist_scaled)
                } else {
                    Fixed::ONE
                };

                let weight = health_weight * distance_factor;

                for i in 0..FIELD_COUNT {
                    aggregate.components[i] += field.components[i] * weight;
                }
                total_weight += weight;
            }
        }

        // Normalize
        if total_weight > Fixed::ZERO {
            for i in 0..FIELD_COUNT {
                aggregate.components[i] /= total_weight;
            }
        }

        aggregate
    }

    /// Compute gradient for a specific field component
    ///
    /// Returns the direction of decreasing potential:
    /// - Positive: neighbors have higher values (I should increase activity)
    /// - Negative: neighbors have lower values (I should decrease activity)
    pub fn gradient(
        &self,
        my_field: &Field,
        neighbor_aggregate: &Field,
        component: FieldComponent,
    ) -> Fixed {
        let my_val = my_field.get(component);
        let neighbor_val = neighbor_aggregate.get(component);
        neighbor_val - my_val
    }

    /// Compute gradient vector for all components
    pub fn gradient_all(
        &self,
        my_field: &Field,
        neighbor_aggregate: &Field,
    ) -> [Fixed; FIELD_COUNT] {
        let mut gradients = [Fixed::ZERO; FIELD_COUNT];
        for (i, component) in FieldComponent::ALL.iter().enumerate() {
            gradients[i] = self.gradient(my_field, neighbor_aggregate, *component);
        }
        gradients
    }

    /// Apply decay to a field based on elapsed time and configuration
    ///
    /// Uses the configured decay model for each component:
    /// - Exponential: f(t) = f0 * exp(-t/tau)
    /// - Linear: f(t) = f0 * (1 - t/tau), clamped to 0
    /// - Step: f(t) = f0 if t < tau, else 0
    ///
    /// Also applies min/max clamping from config.
    pub fn apply_decay(&self, field: &mut Field, elapsed_us: TimeUs) {
        for i in 0..FIELD_COUNT {
            let config = &self.config[i];
            let tau_us = (config.decay_tau.to_num::<f32>() * 1_000_000.0) as u64;
            let tau = if tau_us > 0 { tau_us } else { FIELD_DECAY_TAU_US };

            let t = elapsed_us as f32;
            let tau_f = tau as f32;

            let decay_factor = match config.decay_model {
                DecayModel::Exponential => {
                    // Piecewise approximation of exp(-t/tau) for embedded
                    // More accurate than simple linear approximation
                    if t < tau_f {
                        Fixed::from_num(1.0 - t / tau_f * 0.632) // exp(-1) ≈ 0.368
                    } else if t < tau_f * 2.0 {
                        Fixed::from_num(0.368 - (t - tau_f) / tau_f * 0.233) // exp(-2) ≈ 0.135
                    } else if t < tau_f * 3.0 {
                        Fixed::from_num(0.135 - (t - tau_f * 2.0) / tau_f * 0.086) // exp(-3) ≈ 0.049
                    } else if t < tau_f * 5.0 {
                        Fixed::from_num(0.049 * (1.0 - (t - tau_f * 3.0) / (tau_f * 2.0)))
                    } else {
                        Fixed::ZERO
                    }
                }
                DecayModel::Linear => {
                    // f(t) = f0 * (1 - t/tau), clamped to 0
                    if t < tau_f {
                        Fixed::from_num(1.0 - t / tau_f)
                    } else {
                        Fixed::ZERO
                    }
                }
                DecayModel::Step => {
                    // f(t) = f0 if t < tau, else 0
                    if t < tau_f {
                        Fixed::ONE
                    } else {
                        Fixed::ZERO
                    }
                }
            };

            let decay_factor = decay_factor.max(Fixed::ZERO);

            // Apply decay
            field.components[i] *= decay_factor;

            // Apply min/max clamping from config
            if field.components[i] < config.min_value {
                field.components[i] = config.min_value;
            }
            if field.components[i] > config.max_value {
                field.components[i] = config.max_value;
            }
        }
    }

    /// Garbage collect expired fields
    pub fn gc(&self, region: &mut FieldRegion, now: TimeUs, max_age_us: TimeUs) -> u32 {
        let mut expired = 0u32;

        for i in 0..MAX_MODULES {
            let coord = &mut region.fields[i];
            if coord.field.source != INVALID_MODULE_ID {
                if now.saturating_sub(coord.field.timestamp) > max_age_us {
                    coord.field.clear();
                    expired += 1;
                }
            }
        }

        region.last_gc = now;
        expired
    }

    /// Set configuration for a specific field component
    pub fn set_config(&mut self, component: FieldComponent, config: FieldConfig) {
        let idx = component as usize;
        if idx < FIELD_COUNT {
            self.config[idx] = config;
        }
    }

    /// Get configuration for a specific field component
    pub fn get_config(&self, component: FieldComponent) -> &FieldConfig {
        &self.config[component as usize]
    }
}

impl Default for FieldEngine {
    fn default() -> Self {
        Self::new()
    }
}

// ============================================================================
// Field Arithmetic
// ============================================================================

impl Field {
    /// Add two fields component-wise
    pub fn add(&self, other: &Field) -> Field {
        let mut result = *self;
        for i in 0..FIELD_COUNT {
            result.components[i] += other.components[i];
        }
        result
    }

    /// Scale field by factor
    pub fn scale(&self, factor: Fixed) -> Field {
        let mut result = *self;
        for i in 0..FIELD_COUNT {
            result.components[i] *= factor;
        }
        result
    }

    /// Linear interpolation between two fields
    ///
    /// result = self * (1 - t) + other * t
    pub fn lerp(&self, other: &Field, t: Fixed) -> Field {
        let mut result = Field::new();
        let one_minus_t = Fixed::ONE - t;

        for i in 0..FIELD_COUNT {
            result.components[i] = self.components[i] * one_minus_t
                + other.components[i] * t;
        }
        result
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_field_publish_sample() {
        let engine = FieldEngine::new();
        let mut region = FieldRegion::new();

        let field = Field::with_values(
            Fixed::from_num(0.5),
            Fixed::from_num(0.3),
            Fixed::from_num(0.8),
        );

        engine.publish(&mut region, 1, &field, 1000).unwrap();

        let sampled = engine.sample(&region, 1, 1000).unwrap();
        assert_eq!(sampled.get(FieldComponent::Load), Fixed::from_num(0.5));
    }

    #[test]
    fn test_gradient_calculation() {
        let engine = FieldEngine::new();

        let my_field = Field::with_values(
            Fixed::from_num(0.3),
            Fixed::ZERO,
            Fixed::ZERO,
        );

        let neighbor_field = Field::with_values(
            Fixed::from_num(0.7),
            Fixed::ZERO,
            Fixed::ZERO,
        );

        let gradient = engine.gradient(&my_field, &neighbor_field, FieldComponent::Load);
        assert!(gradient > Fixed::ZERO); // Neighbors have higher load
    }
}

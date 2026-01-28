//! Property tests for types module
//!
//! Tests mathematical invariants for:
//! - Position: distance symmetry, triangle inequality
//! - Field: component access, validity, clearing
//! - Fixed-point: roundtrip conversion
//! - Neighbor: health state logic

use ekk::types::*;
use fixed::types::I16F16 as Fixed;
use proptest::prelude::*;

// ============================================================================
// Strategies
// ============================================================================

/// Valid module IDs (1-254, excluding 0=invalid and 255=broadcast)
fn valid_module_id() -> impl Strategy<Value = ModuleId> {
    1u8..=254u8
}

/// Positions within reasonable i16 range
fn position_strategy() -> impl Strategy<Value = Position> {
    (-1000i16..1000i16, -1000i16..1000i16, -1000i16..1000i16)
        .prop_map(|(x, y, z)| Position::new(x, y, z))
}

/// Fixed-point values in [-1.0, 1.0)
fn fixed_bounded() -> impl Strategy<Value = Fixed> {
    (-1.0f32..1.0f32).prop_map(|f| Fixed::from_num(f))
}

/// Field component values in [0, 1]
fn field_value() -> impl Strategy<Value = Fixed> {
    (0.0f32..1.0f32).prop_map(|f| Fixed::from_num(f))
}

// ============================================================================
// Position Property Tests
// ============================================================================

proptest! {
    /// Distance is symmetric: d(a,b) == d(b,a)
    #[test]
    fn position_distance_symmetric(
        p1 in position_strategy(),
        p2 in position_strategy()
    ) {
        prop_assert_eq!(
            p1.distance_squared(&p2),
            p2.distance_squared(&p1),
            "Distance must be symmetric"
        );
    }

    /// Distance is non-negative
    #[test]
    fn position_distance_non_negative(
        p1 in position_strategy(),
        p2 in position_strategy()
    ) {
        prop_assert!(
            p1.distance_squared(&p2) >= 0,
            "Distance squared must be non-negative"
        );
    }

    /// Distance to self is zero
    #[test]
    fn position_distance_to_self_zero(p in position_strategy()) {
        prop_assert_eq!(
            p.distance_squared(&p),
            0,
            "Distance to self must be zero"
        );
    }

    /// Relaxed triangle inequality for squared distances
    /// d(a,c)^2 <= 2*(d(a,b)^2 + d(b,c)^2)
    #[test]
    fn position_triangle_inequality(
        p1 in position_strategy(),
        p2 in position_strategy(),
        p3 in position_strategy()
    ) {
        let d12 = p1.distance_squared(&p2) as i64;
        let d23 = p2.distance_squared(&p3) as i64;
        let d13 = p1.distance_squared(&p3) as i64;

        prop_assert!(
            d13 <= 2 * (d12 + d23),
            "Triangle inequality violated: d13={}, 2*(d12+d23)={}",
            d13, 2 * (d12 + d23)
        );
    }
}

// ============================================================================
// Field Property Tests
// ============================================================================

proptest! {
    /// Field component set/get roundtrip
    #[test]
    fn field_set_get_roundtrip(
        component_idx in 0usize..FIELD_COUNT,
        value in field_value()
    ) {
        let component = FieldComponent::ALL[component_idx];
        let mut field = Field::new();
        field.set(component, value);
        prop_assert_eq!(field.get(component), value);
    }

    /// Field with_values sets correct components
    #[test]
    fn field_with_values_correct(
        load in field_value(),
        thermal in field_value(),
        power in field_value()
    ) {
        let field = Field::with_values(load, thermal, power);
        prop_assert_eq!(field.get(FieldComponent::Load), load);
        prop_assert_eq!(field.get(FieldComponent::Thermal), thermal);
        prop_assert_eq!(field.get(FieldComponent::Power), power);
    }

    /// Field validity depends on source and age
    #[test]
    fn field_validity_logic(
        now in 1_000_000u64..10_000_000u64,
        age in 0u64..1_000_000u64,
        source in valid_module_id()
    ) {
        let max_age = 500_000u64;
        let timestamp = now.saturating_sub(age);

        let mut field = Field::new();
        field.timestamp = timestamp;
        field.source = source;

        let expected = age < max_age;
        prop_assert_eq!(
            field.is_valid(now, max_age),
            expected,
            "age={}, max_age={}", age, max_age
        );
    }

    /// Invalid source always makes field invalid
    #[test]
    fn field_invalid_source_always_invalid(
        now in 1_000_000u64..10_000_000u64,
        max_age in 100_000u64..1_000_000u64
    ) {
        let mut field = Field::new();
        field.timestamp = now; // Fresh
        field.source = INVALID_MODULE_ID;

        prop_assert!(!field.is_valid(now, max_age));
    }

    /// Field clear resets everything
    #[test]
    fn field_clear_resets_all(
        load in field_value(),
        thermal in field_value(),
        power in field_value(),
        source in valid_module_id()
    ) {
        let mut field = Field::with_values(load, thermal, power);
        field.source = source;
        field.timestamp = 12345;
        field.sequence = 42;

        field.clear();

        prop_assert_eq!(field.source, INVALID_MODULE_ID);
        prop_assert_eq!(field.timestamp, 0);
        prop_assert_eq!(field.sequence, 0);
        for i in 0..FIELD_COUNT {
            prop_assert_eq!(field.components[i], Fixed::ZERO);
        }
    }
}

// ============================================================================
// Fixed-Point Property Tests
// ============================================================================

proptest! {
    /// Fixed-point roundtrip within epsilon
    #[test]
    fn fixed_roundtrip_approximate(f in -100.0f32..100.0f32) {
        let fixed = Fixed::from_num(f);
        let back: f32 = fixed.to_num();
        let epsilon = 0.0001;
        prop_assert!(
            (f - back).abs() < epsilon,
            "Roundtrip error: {} -> {} (diff={})",
            f, back, (f - back).abs()
        );
    }
}

// ============================================================================
// Neighbor Property Tests
// ============================================================================

proptest! {
    /// Neighbor is_healthy correct for all states
    #[test]
    fn neighbor_health_states(id in valid_module_id()) {
        let mut neighbor = Neighbor::new(id);

        neighbor.health = HealthState::Unknown;
        prop_assert!(!neighbor.is_healthy(), "Unknown != healthy");

        neighbor.health = HealthState::Alive;
        prop_assert!(neighbor.is_healthy(), "Alive == healthy");

        neighbor.health = HealthState::Suspect;
        prop_assert!(neighbor.is_healthy(), "Suspect == healthy");

        neighbor.health = HealthState::Dead;
        prop_assert!(!neighbor.is_healthy(), "Dead != healthy");
    }
}

// ============================================================================
// Threshold Constants Test
// ============================================================================

#[test]
fn threshold_constants_ordering() {
    let majority: f32 = threshold::SIMPLE_MAJORITY.to_num();
    let supermajority: f32 = threshold::SUPERMAJORITY.to_num();
    let unanimous: f32 = threshold::UNANIMOUS.to_num();

    assert!(majority >= 0.0 && majority <= 1.0);
    assert!(supermajority >= 0.0 && supermajority <= 1.0);
    assert!(unanimous >= 0.0 && unanimous <= 1.0);
    assert!(majority < supermajority);
    assert!(supermajority <= unanimous);
}

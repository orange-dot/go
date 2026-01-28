//! Property tests for field module
//!
//! Tests mathematical invariants for:
//! - Gradient: antisymmetry, direction
//! - Publish/Sample: identity at t=0
//! - Decay: monotonicity, bounds
//! - Field arithmetic: add, scale, lerp

use ekk::field::*;
use ekk::types::*;
use fixed::types::I16F16 as Fixed;
use proptest::prelude::*;

// ============================================================================
// Strategies
// ============================================================================

fn valid_module_id() -> impl Strategy<Value = ModuleId> {
    1u8..=254u8
}

fn field_value() -> impl Strategy<Value = Fixed> {
    (0.0f32..1.0f32).prop_map(|f| Fixed::from_num(f))
}

fn field_strategy() -> impl Strategy<Value = Field> {
    (field_value(), field_value(), field_value())
        .prop_map(|(load, thermal, power)| Field::with_values(load, thermal, power))
}

fn timestamp() -> impl Strategy<Value = TimeUs> {
    1_000_000u64..100_000_000u64
}

// ============================================================================
// Gradient Property Tests
// ============================================================================

proptest! {
    /// CORE INVARIANT: Gradient is antisymmetric
    /// gradient(a, b) == -gradient(b, a)
    #[test]
    fn gradient_antisymmetric(
        a_val in field_value(),
        b_val in field_value(),
        component_idx in 0usize..FIELD_COUNT
    ) {
        let component = FieldComponent::ALL[component_idx];
        let engine = FieldEngine::new();

        let mut field_a = Field::new();
        field_a.set(component, a_val);

        let mut field_b = Field::new();
        field_b.set(component, b_val);

        let grad_ab = engine.gradient(&field_a, &field_b, component);
        let grad_ba = engine.gradient(&field_b, &field_a, component);

        let sum = grad_ab + grad_ba;
        let epsilon = Fixed::from_num(0.0001);
        prop_assert!(
            sum.abs() < epsilon,
            "Antisymmetry violated: grad(a,b)={} + grad(b,a)={} = {}",
            grad_ab, grad_ba, sum
        );
    }

    /// Gradient direction matches value ordering
    #[test]
    fn gradient_direction_correct(
        my_val in field_value(),
        neighbor_val in field_value(),
        component_idx in 0usize..FIELD_COUNT
    ) {
        let component = FieldComponent::ALL[component_idx];
        let engine = FieldEngine::new();

        let mut my_field = Field::new();
        my_field.set(component, my_val);

        let mut neighbor_field = Field::new();
        neighbor_field.set(component, neighbor_val);

        let gradient = engine.gradient(&my_field, &neighbor_field, component);

        if neighbor_val > my_val {
            prop_assert!(gradient > Fixed::ZERO, "Should be positive when neighbor > self");
        } else if neighbor_val < my_val {
            prop_assert!(gradient < Fixed::ZERO, "Should be negative when neighbor < self");
        } else {
            let epsilon = Fixed::from_num(0.0001);
            prop_assert!(gradient.abs() < epsilon, "Should be ~zero when equal");
        }
    }

    /// gradient_all matches individual gradient calls
    #[test]
    fn gradient_all_consistent(
        my_field in field_strategy(),
        neighbor_field in field_strategy()
    ) {
        let engine = FieldEngine::new();
        let all = engine.gradient_all(&my_field, &neighbor_field);

        for (i, component) in FieldComponent::ALL.iter().enumerate() {
            let individual = engine.gradient(&my_field, &neighbor_field, *component);
            prop_assert_eq!(all[i], individual, "Mismatch at component {}", i);
        }
    }
}

// ============================================================================
// Publish/Sample Property Tests
// ============================================================================

proptest! {
    /// Publish then immediate sample preserves values (no decay at t=0)
    #[test]
    fn publish_sample_identity(
        module_id in valid_module_id(),
        field in field_strategy(),
        now in timestamp()
    ) {
        let engine = FieldEngine::new();
        let mut region = FieldRegion::new();

        engine.publish(&mut region, module_id, &field, now).unwrap();
        let sampled = engine.sample(&region, module_id, now).unwrap();

        for i in 0..FIELD_COUNT {
            let diff = (sampled.components[i] - field.components[i]).abs();
            let epsilon = Fixed::from_num(0.001);
            prop_assert!(
                diff < epsilon,
                "Component {} mismatch: input={}, sampled={}",
                i, field.components[i], sampled.components[i]
            );
        }
    }

    /// Invalid module ID rejected
    #[test]
    fn publish_rejects_invalid_id(field in field_strategy(), now in timestamp()) {
        let engine = FieldEngine::new();
        let mut region = FieldRegion::new();
        let result = engine.publish(&mut region, INVALID_MODULE_ID, &field, now);
        prop_assert!(matches!(result, Err(Error::InvalidArg)));
    }

    /// Unpublished module returns NotFound
    #[test]
    fn sample_unpublished_not_found(module_id in valid_module_id(), now in timestamp()) {
        let engine = FieldEngine::new();
        let region = FieldRegion::new();
        let result = engine.sample(&region, module_id, now);
        prop_assert!(matches!(result, Err(Error::NotFound)));
    }
}

// ============================================================================
// Decay Property Tests
// ============================================================================

proptest! {
    /// CORE INVARIANT: Decay is monotonically decreasing
    /// sample(t2) <= sample(t1) when t2 > t1
    #[test]
    fn decay_monotonic(
        module_id in valid_module_id(),
        component_idx in 0usize..FIELD_COUNT,
        value in (0.1f32..1.0f32).prop_map(|f| Fixed::from_num(f)),
        t1_offset in 0u64..50_000u64,
        t2_offset in 50_001u64..100_000u64
    ) {
        let engine = FieldEngine::new();
        let mut region = FieldRegion::new();

        let mut field = Field::new();
        field.set(FieldComponent::ALL[component_idx], value);

        let publish_time = 1_000_000u64;
        engine.publish(&mut region, module_id, &field, publish_time).unwrap();

        let t1 = publish_time + t1_offset;
        let t2 = publish_time + t2_offset;

        if let (Ok(s1), Ok(s2)) = (
            engine.sample(&region, module_id, t1),
            engine.sample(&region, module_id, t2)
        ) {
            let v1 = s1.components[component_idx];
            let v2 = s2.components[component_idx];
            prop_assert!(
                v2 <= v1,
                "Decay not monotonic: v({})={} > v({})={}",
                t1, v1, t2, v2
            );
        }
    }

    /// Values stay within configured bounds after decay
    #[test]
    fn decay_respects_bounds(
        module_id in valid_module_id(),
        field in field_strategy(),
        elapsed in 0u64..200_000u64
    ) {
        let engine = FieldEngine::new();
        let mut region = FieldRegion::new();

        let publish_time = 1_000_000u64;
        engine.publish(&mut region, module_id, &field, publish_time).unwrap();

        if let Ok(sampled) = engine.sample(&region, module_id, publish_time + elapsed) {
            for i in 0..FIELD_COUNT {
                let config = engine.get_config(FieldComponent::ALL[i]);
                prop_assert!(sampled.components[i] >= config.min_value);
                prop_assert!(sampled.components[i] <= config.max_value);
            }
        }
    }
}

// ============================================================================
// Field Arithmetic Property Tests
// ============================================================================

proptest! {
    /// Field addition is commutative
    #[test]
    fn field_add_commutative(f1 in field_strategy(), f2 in field_strategy()) {
        let sum1 = f1.add(&f2);
        let sum2 = f2.add(&f1);

        for i in 0..FIELD_COUNT {
            let diff = (sum1.components[i] - sum2.components[i]).abs();
            let epsilon = Fixed::from_num(0.0001);
            prop_assert!(diff < epsilon, "Add not commutative at {}", i);
        }
    }

    /// Scale by 1.0 is identity
    #[test]
    fn field_scale_one_identity(field in field_strategy()) {
        let scaled = field.scale(Fixed::ONE);
        for i in 0..FIELD_COUNT {
            prop_assert_eq!(scaled.components[i], field.components[i]);
        }
    }

    /// Scale by 0.0 gives zero
    #[test]
    fn field_scale_zero_gives_zero(field in field_strategy()) {
        let scaled = field.scale(Fixed::ZERO);
        for i in 0..FIELD_COUNT {
            prop_assert_eq!(scaled.components[i], Fixed::ZERO);
        }
    }

    /// Lerp endpoints: lerp(f1, f2, 0) == f1, lerp(f1, f2, 1) == f2
    #[test]
    fn field_lerp_endpoints(f1 in field_strategy(), f2 in field_strategy()) {
        let lerp0 = f1.lerp(&f2, Fixed::ZERO);
        let lerp1 = f1.lerp(&f2, Fixed::ONE);

        let epsilon = Fixed::from_num(0.0001);
        for i in 0..FIELD_COUNT {
            prop_assert!((lerp0.components[i] - f1.components[i]).abs() < epsilon);
            prop_assert!((lerp1.components[i] - f2.components[i]).abs() < epsilon);
        }
    }
}

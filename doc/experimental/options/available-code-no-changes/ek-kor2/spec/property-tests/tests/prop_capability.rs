//! Property tests for capability module (MAPF-HET Integration)
//!
//! Tests mathematical invariants for:
//! - can_perform: bitwise semantics, edge cases
//! - Capability flags: non-overlapping, complete coverage

use ekk::types::*;
use ekk::types::capability::*;
use proptest::prelude::*;

// ============================================================================
// Strategies
// ============================================================================

fn capability() -> impl Strategy<Value = Capability> {
    any::<u16>()
}

fn capability_subset() -> impl Strategy<Value = (Capability, Capability)> {
    // Generate (superset, subset) where subset is a subset of superset
    capability().prop_flat_map(|have| {
        let subset = have;
        (Just(have), (0u16..=subset))
    }).prop_map(|(have, factor)| {
        // ensure need is a subset of have
        (have, have & factor)
    })
}

// ============================================================================
// can_perform Property Tests
// ============================================================================

proptest! {
    /// CORE INVARIANT: can_perform(have, need) == ((have & need) == need)
    #[test]
    fn can_perform_bitwise_correct(
        have in capability(),
        need in capability()
    ) {
        let result = can_perform(have, need);
        let expected = (have & need) == need;

        prop_assert_eq!(
            result, expected,
            "can_perform({:#06x}, {:#06x}) = {}, expected {}",
            have, need, result, expected
        );
    }

    /// Subset always succeeds: if need ⊆ have, then can_perform(have, need) == true
    #[test]
    fn can_perform_subset_succeeds(
        (have, need) in capability_subset()
    ) {
        prop_assert!(
            can_perform(have, need),
            "Subset should succeed: have={:#06x}, need={:#06x}",
            have, need
        );
    }

    /// Empty need always succeeds: can_perform(any, 0) == true
    #[test]
    fn can_perform_empty_need_succeeds(have in capability()) {
        prop_assert!(
            can_perform(have, 0),
            "Empty need should always succeed: have={:#06x}",
            have
        );
    }

    /// Self always succeeds: can_perform(x, x) == true
    #[test]
    fn can_perform_self_succeeds(caps in capability()) {
        prop_assert!(
            can_perform(caps, caps),
            "Self should always succeed: caps={:#06x}",
            caps
        );
    }

    /// Superset fails when missing bits: if need has bits not in have, fails
    #[test]
    fn can_perform_missing_fails(
        have in capability(),
        extra_bit in 0u8..16u8
    ) {
        let extra_mask: u16 = 1 << extra_bit;
        // Only test when have doesn't already have this bit
        if (have & extra_mask) == 0 {
            let need = have | extra_mask;
            prop_assert!(
                !can_perform(have, need),
                "Missing bit should fail: have={:#06x}, need={:#06x}",
                have, need
            );
        }
    }
}

// ============================================================================
// Capability Flag Tests
// ============================================================================

proptest! {
    /// Standard flags are single bits
    #[test]
    fn standard_flags_are_single_bits(_dummy in Just(())) {
        let flags = [
            THERMAL_OK,
            POWER_HIGH,
            GATEWAY,
            V2G,
            RESERVED_4,
            RESERVED_5,
            RESERVED_6,
            RESERVED_7,
            CUSTOM_0,
            CUSTOM_1,
            CUSTOM_2,
            CUSTOM_3,
        ];

        for flag in flags.iter() {
            // Each flag should have exactly one bit set
            prop_assert_eq!(
                flag.count_ones(), 1,
                "Flag {:#06x} should have exactly 1 bit set",
                flag
            );
        }
    }

    /// Standard flags are non-overlapping
    #[test]
    fn standard_flags_non_overlapping(_dummy in Just(())) {
        let flags = [
            THERMAL_OK,
            POWER_HIGH,
            GATEWAY,
            V2G,
            RESERVED_4,
            RESERVED_5,
            RESERVED_6,
            RESERVED_7,
            CUSTOM_0,
            CUSTOM_1,
            CUSTOM_2,
            CUSTOM_3,
        ];

        for i in 0..flags.len() {
            for j in (i + 1)..flags.len() {
                let overlap = flags[i] & flags[j];
                prop_assert_eq!(
                    overlap, 0,
                    "Flags {:#06x} and {:#06x} overlap: {:#06x}",
                    flags[i], flags[j], overlap
                );
            }
        }
    }
}

// ============================================================================
// Specific Flag Tests
// ============================================================================

#[test]
fn thermal_ok_is_bit_0() {
    assert_eq!(THERMAL_OK, 1 << 0);
    assert_eq!(THERMAL_OK, 0x0001);
}

#[test]
fn power_high_is_bit_1() {
    assert_eq!(POWER_HIGH, 1 << 1);
    assert_eq!(POWER_HIGH, 0x0002);
}

#[test]
fn gateway_is_bit_2() {
    assert_eq!(GATEWAY, 1 << 2);
    assert_eq!(GATEWAY, 0x0004);
}

#[test]
fn v2g_is_bit_3() {
    assert_eq!(V2G, 1 << 3);
    assert_eq!(V2G, 0x0008);
}

#[test]
fn custom_flags_start_at_bit_8() {
    assert_eq!(CUSTOM_0, 1 << 8);
    assert_eq!(CUSTOM_1, 1 << 9);
    assert_eq!(CUSTOM_2, 1 << 10);
    assert_eq!(CUSTOM_3, 1 << 11);
}

// ============================================================================
// Combination Tests
// ============================================================================

#[test]
fn can_combine_multiple_flags() {
    let combined = THERMAL_OK | V2G | GATEWAY;
    assert_eq!(combined, 0b1101); // bits 0, 2, 3
}

#[test]
fn can_perform_combined_flags() {
    let module_caps = THERMAL_OK | POWER_HIGH | V2G;  // 0b1011
    let task_needs = THERMAL_OK | V2G;                 // 0b1001

    assert!(can_perform(module_caps, task_needs));
}

#[test]
fn cannot_perform_missing_combined_flag() {
    let module_caps = THERMAL_OK | POWER_HIGH;  // 0b0011
    let task_needs = THERMAL_OK | V2G;          // 0b1001 - requires V2G

    assert!(!can_perform(module_caps, task_needs));
}

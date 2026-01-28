//! Property tests for heartbeat module
//!
//! Tests state machine invariants for:
//! - State transitions: Unknown -> Alive -> Suspect -> Dead
//! - Timing: suspect/dead thresholds
//! - Recovery: Dead -> Alive on heartbeat

use ekk::heartbeat::*;
use ekk::types::*;
use proptest::prelude::*;

// ============================================================================
// Strategies
// ============================================================================

fn valid_module_id() -> impl Strategy<Value = ModuleId> {
    1u8..=254u8
}

fn different_module_ids() -> impl Strategy<Value = (ModuleId, ModuleId)> {
    (1u8..=127u8, 128u8..=254u8)
}

fn timestamp() -> impl Strategy<Value = TimeUs> {
    1_000_000u64..100_000_000u64
}

// ============================================================================
// State Transition Property Tests
// ============================================================================

proptest! {
    /// CORE INVARIANT: Valid state transitions only
    /// Unknown -> Alive (on heartbeat)
    /// Alive -> Suspect (on missed heartbeats)
    /// Suspect -> Dead (on timeout)
    /// Dead -> Alive (on heartbeat recovery)
    #[test]
    fn state_transitions_valid(
        (my_id, neighbor_id) in different_module_ids(),
        now in timestamp()
    ) {
        let config = HeartbeatConfig {
            period: 10_000,        // 10ms
            timeout_count: 5,     // 5 missed = 50ms to dead
            auto_broadcast: false,
            track_latency: false,
        };
        let mut hb = Heartbeat::new(my_id, Some(config));
        hb.add_neighbor(neighbor_id).unwrap();

        // Initial state is Unknown
        prop_assert_eq!(hb.get_health(neighbor_id), HealthState::Unknown);

        // Receive heartbeat -> Alive
        hb.received(neighbor_id, 1, now).unwrap();
        prop_assert_eq!(hb.get_health(neighbor_id), HealthState::Alive);

        // Miss some heartbeats (2 periods) -> Suspect
        hb.tick(now + 25_000); // 2.5 periods
        prop_assert_eq!(hb.get_health(neighbor_id), HealthState::Suspect);

        // Miss more heartbeats (5+ periods) -> Dead
        hb.tick(now + 60_000); // 6 periods
        prop_assert_eq!(hb.get_health(neighbor_id), HealthState::Dead);

        // Receive heartbeat again -> Alive (recovery)
        hb.received(neighbor_id, 2, now + 70_000).unwrap();
        prop_assert_eq!(hb.get_health(neighbor_id), HealthState::Alive);
    }

    /// Fresh heartbeat resets missed count
    #[test]
    fn heartbeat_resets_missed_count(
        (my_id, neighbor_id) in different_module_ids(),
        now in timestamp()
    ) {
        let config = HeartbeatConfig {
            period: 10_000,
            timeout_count: 5,
            auto_broadcast: false,
            track_latency: false,
        };
        let mut hb = Heartbeat::new(my_id, Some(config));
        hb.add_neighbor(neighbor_id).unwrap();

        // Get to Suspect state
        hb.received(neighbor_id, 1, now).unwrap();
        hb.tick(now + 25_000);
        prop_assert_eq!(hb.get_health(neighbor_id), HealthState::Suspect);

        // Fresh heartbeat should reset to Alive
        hb.received(neighbor_id, 2, now + 30_000).unwrap();
        prop_assert_eq!(hb.get_health(neighbor_id), HealthState::Alive);

        // And now it takes full timeout to become Suspect again
        hb.tick(now + 35_000); // Only 5ms elapsed
        prop_assert_eq!(hb.get_health(neighbor_id), HealthState::Alive);
    }
}

// ============================================================================
// Neighbor Management Property Tests
// ============================================================================

proptest! {
    /// Cannot add self as neighbor
    #[test]
    fn cannot_add_self(my_id in valid_module_id()) {
        let mut hb = Heartbeat::new(my_id, None);
        let result = hb.add_neighbor(my_id);
        prop_assert!(matches!(result, Err(Error::InvalidArg)));
    }

    /// Cannot add invalid module ID
    #[test]
    fn cannot_add_invalid_id(my_id in valid_module_id()) {
        let mut hb = Heartbeat::new(my_id, None);
        let result = hb.add_neighbor(INVALID_MODULE_ID);
        prop_assert!(matches!(result, Err(Error::InvalidArg)));
    }

    /// Cannot add duplicate neighbor
    #[test]
    fn cannot_add_duplicate(
        (my_id, neighbor_id) in different_module_ids()
    ) {
        let mut hb = Heartbeat::new(my_id, None);
        hb.add_neighbor(neighbor_id).unwrap();
        let result = hb.add_neighbor(neighbor_id);
        prop_assert!(matches!(result, Err(Error::AlreadyExists)));
    }

    /// Remove unknown neighbor fails
    #[test]
    fn remove_unknown_fails(
        (my_id, neighbor_id) in different_module_ids()
    ) {
        let mut hb = Heartbeat::new(my_id, None);
        let result = hb.remove_neighbor(neighbor_id);
        prop_assert!(matches!(result, Err(Error::NotFound)));
    }

    /// Add then remove succeeds
    #[test]
    fn add_remove_succeeds(
        (my_id, neighbor_id) in different_module_ids()
    ) {
        let mut hb = Heartbeat::new(my_id, None);
        hb.add_neighbor(neighbor_id).unwrap();
        hb.remove_neighbor(neighbor_id).unwrap();

        // Can add again after removal
        hb.add_neighbor(neighbor_id).unwrap();
    }
}

// ============================================================================
// Timing Property Tests
// ============================================================================

proptest! {
    /// Suspect threshold is 2 * period
    #[test]
    fn suspect_threshold_correct(
        (my_id, neighbor_id) in different_module_ids(),
        period in 5_000u64..50_000u64,
        now in timestamp()
    ) {
        let config = HeartbeatConfig {
            period,
            timeout_count: 5,
            auto_broadcast: false,
            track_latency: false,
        };
        let mut hb = Heartbeat::new(my_id, Some(config));
        hb.add_neighbor(neighbor_id).unwrap();
        hb.received(neighbor_id, 1, now).unwrap();

        // Just before 2*period - should still be Alive
        hb.tick(now + period * 2 - 1);
        prop_assert_eq!(hb.get_health(neighbor_id), HealthState::Alive);

        // Just after 2*period - should be Suspect
        hb.tick(now + period * 2 + 1);
        prop_assert_eq!(hb.get_health(neighbor_id), HealthState::Suspect);
    }

    /// Dead threshold is timeout_count * period
    #[test]
    fn dead_threshold_correct(
        (my_id, neighbor_id) in different_module_ids(),
        period in 5_000u64..20_000u64,
        timeout_count in 3u8..10u8,
        now in timestamp()
    ) {
        let config = HeartbeatConfig {
            period,
            timeout_count,
            auto_broadcast: false,
            track_latency: false,
        };
        let mut hb = Heartbeat::new(my_id, Some(config));
        hb.add_neighbor(neighbor_id).unwrap();
        hb.received(neighbor_id, 1, now).unwrap();

        let timeout = period * timeout_count as u64;

        // Just before timeout - should be Suspect
        hb.tick(now + timeout - 1);
        prop_assert!(
            hb.get_health(neighbor_id) != HealthState::Dead,
            "Should not be Dead before timeout"
        );

        // Just after timeout - should be Dead
        hb.tick(now + timeout + 1);
        prop_assert_eq!(hb.get_health(neighbor_id), HealthState::Dead);
    }
}

// ============================================================================
// Sequence Number Property Tests
// ============================================================================

proptest! {
    /// Sequence number increments on mark_sent
    #[test]
    fn sequence_increments(
        my_id in valid_module_id(),
        now in timestamp(),
        count in 1usize..100usize
    ) {
        let mut hb = Heartbeat::new(my_id, None);
        let initial = hb.sequence();

        for i in 0..count {
            hb.mark_sent(now + i as u64 * 10_000);
        }

        let expected = initial.wrapping_add(count as u8);
        prop_assert_eq!(hb.sequence(), expected);
    }
}

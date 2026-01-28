//! Property tests for deadline/slack module (MAPF-HET Integration)
//!
//! Tests mathematical invariants for:
//! - Slack computation: correctness, criticality detection
//! - Slack normalization: bounds, monotonicity
//! - Critical threshold: consistency

use ekk::types::*;
use fixed::types::I16F16 as Fixed;
use proptest::prelude::*;

// ============================================================================
// Strategies
// ============================================================================

fn timestamp() -> impl Strategy<Value = TimeUs> {
    0u64..200_000_000u64
}

fn duration() -> impl Strategy<Value = TimeUs> {
    1_000u64..50_000_000u64
}

fn deadline_strategy() -> impl Strategy<Value = (TimeUs, TimeUs, TimeUs)> {
    // Generate (now, duration_est, deadline) where deadline > now + duration_est (usually)
    (timestamp(), duration()).prop_flat_map(|(now, dur)| {
        let min_deadline = now.saturating_add(dur);
        (
            Just(now),
            Just(dur),
            min_deadline..min_deadline.saturating_add(200_000_000),
        )
    })
}

// ============================================================================
// Slack Computation Property Tests
// ============================================================================

proptest! {
    /// CORE INVARIANT: Slack = deadline - (now + duration_est)
    /// Verified by reconstruction
    #[test]
    fn slack_computation_correct(
        now in timestamp(),
        duration_est in duration(),
        deadline_offset in 0u64..150_000_000u64
    ) {
        let deadline = now.saturating_add(duration_est).saturating_add(deadline_offset);
        let mut dl = Deadline::new(deadline, duration_est);
        dl.compute_slack(now);

        let expected_slack_us = deadline as i64 - (now as i64 + duration_est as i64);
        let expected_normalized = (expected_slack_us as f32 / SLACK_NORMALIZE_US as f32)
            .clamp(0.0, 1.0);

        let actual_normalized: f32 = dl.slack.to_num();
        let diff = (expected_normalized - actual_normalized).abs();

        prop_assert!(
            diff < 0.001,
            "Slack mismatch: expected={}, actual={}, diff={}",
            expected_normalized, actual_normalized, diff
        );
    }

    /// Critical flag set correctly when slack < threshold
    #[test]
    fn critical_detection_correct(
        now in timestamp(),
        duration_est in duration(),
        slack_us in 0u64..20_000_000u64
    ) {
        let deadline = now.saturating_add(duration_est).saturating_add(slack_us);
        let mut dl = Deadline::new(deadline, duration_est);
        dl.compute_slack(now);

        let expected_critical = slack_us < SLACK_THRESHOLD_US;
        prop_assert_eq!(
            dl.critical, expected_critical,
            "Critical flag: slack_us={}, threshold={}, expected={}, actual={}",
            slack_us, SLACK_THRESHOLD_US, expected_critical, dl.critical
        );
    }

    /// Slack is monotonically decreasing as time advances
    #[test]
    fn slack_decreases_with_time(
        duration_est in duration(),
        deadline in 50_000_000u64..150_000_000u64,
        t1 in 0u64..25_000_000u64,
        t2_offset in 1_000u64..10_000_000u64
    ) {
        let t2 = t1.saturating_add(t2_offset);

        let mut dl1 = Deadline::new(deadline, duration_est);
        let mut dl2 = Deadline::new(deadline, duration_est);

        dl1.compute_slack(t1);
        dl2.compute_slack(t2);

        prop_assert!(
            dl2.slack <= dl1.slack,
            "Slack should decrease: slack({})={} > slack({})={}",
            t1, dl1.slack, t2, dl2.slack
        );
    }

    /// Normalized slack stays within [0, 1] bounds
    #[test]
    fn slack_normalized_bounded(
        (now, duration_est, deadline) in deadline_strategy()
    ) {
        let mut dl = Deadline::new(deadline, duration_est);
        dl.compute_slack(now);

        let slack_val: f32 = dl.slack.to_num();
        prop_assert!(
            slack_val >= 0.0 && slack_val <= 1.0,
            "Slack out of bounds: {}",
            slack_val
        );
    }

    /// Past deadline gives zero slack (not negative)
    #[test]
    fn past_deadline_zero_slack(
        deadline in 10_000_000u64..50_000_000u64,
        duration_est in duration(),
        past_offset in 1_000u64..10_000_000u64
    ) {
        let now = deadline.saturating_add(past_offset);
        let mut dl = Deadline::new(deadline, duration_est);
        dl.compute_slack(now);

        let slack_val: f32 = dl.slack.to_num();
        prop_assert!(
            slack_val >= 0.0,
            "Past deadline should give non-negative slack (clamped): {}",
            slack_val
        );
    }
}

// ============================================================================
// Deadline Struct Property Tests
// ============================================================================

proptest! {
    /// is_past_due returns correct result
    #[test]
    fn is_past_due_correct(
        deadline in timestamp(),
        now in timestamp()
    ) {
        let dl = Deadline::new(deadline, 0);
        let expected = now >= deadline;
        prop_assert_eq!(
            dl.is_past_due(now), expected,
            "is_past_due({}): deadline={}, expected={}, actual={}",
            now, deadline, expected, dl.is_past_due(now)
        );
    }

    /// New deadline initializes with zero slack, not critical
    #[test]
    fn new_deadline_initial_state(
        deadline in timestamp(),
        duration_est in duration()
    ) {
        let dl = Deadline::new(deadline, duration_est);

        prop_assert_eq!(dl.deadline, deadline);
        prop_assert_eq!(dl.duration_est, duration_est);
        prop_assert_eq!(dl.slack, Fixed::ZERO);
        prop_assert_eq!(dl.critical, false);
    }
}

// ============================================================================
// Constants Sanity Tests
// ============================================================================

#[test]
fn slack_threshold_reasonable() {
    // 10 seconds in microseconds
    assert_eq!(SLACK_THRESHOLD_US, 10_000_000);
}

#[test]
fn slack_normalize_reasonable() {
    // 100 seconds in microseconds
    assert_eq!(SLACK_NORMALIZE_US, 100_000_000);
}

#[test]
fn threshold_less_than_normalize() {
    assert!(SLACK_THRESHOLD_US < SLACK_NORMALIZE_US);
}

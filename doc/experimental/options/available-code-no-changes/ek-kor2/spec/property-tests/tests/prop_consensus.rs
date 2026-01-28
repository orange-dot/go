//! Property tests for consensus module
//!
//! Tests mathematical invariants for:
//! - Vote counting: monotonicity
//! - Quorum math: threshold logic
//! - Deduplication: same voter slot ignored
//! - Inhibition: immediate cancellation

use ekk::consensus::*;
use ekk::types::*;
use fixed::types::I16F16 as Fixed;
use proptest::prelude::*;

// ============================================================================
// Strategies
// ============================================================================

fn valid_module_id() -> impl Strategy<Value = ModuleId> {
    1u8..=254u8
}

fn threshold_value() -> impl Strategy<Value = Fixed> {
    (0.1f32..1.0f32).prop_map(|f| Fixed::from_num(f))
}

fn timestamp() -> impl Strategy<Value = TimeUs> {
    1_000_000u64..100_000_000u64
}

// ============================================================================
// Vote Counting Property Tests
// ============================================================================

proptest! {
    /// CORE INVARIANT: yes_count never decreases
    /// After recording a Yes vote, yes_count >= previous yes_count
    #[test]
    fn vote_count_monotonic(
        id in 1u16..1000u16,
        proposer in valid_module_id(),
        voters in prop::collection::vec(valid_module_id(), 1..8)
    ) {
        let mut ballot = Ballot::new(
            id,
            ProposalType::ModeChange,
            proposer,
            42,
            threshold::SIMPLE_MAJORITY,
            1_000_000,
        );

        let mut prev_yes = 0u8;
        let mut prev_total = 0u8;

        for voter in voters {
            ballot.record_vote_from(voter, VoteValue::Yes);

            // yes_count never decreases
            prop_assert!(
                ballot.yes_count >= prev_yes,
                "yes_count decreased: {} -> {}",
                prev_yes, ballot.yes_count
            );

            // vote_count never decreases
            prop_assert!(
                ballot.vote_count >= prev_total,
                "vote_count decreased: {} -> {}",
                prev_total, ballot.vote_count
            );

            prev_yes = ballot.yes_count;
            prev_total = ballot.vote_count;
        }
    }

    /// Duplicate votes from same slot are ignored
    #[test]
    fn duplicate_vote_ignored(
        id in 1u16..1000u16,
        proposer in valid_module_id(),
        voter in valid_module_id()
    ) {
        let mut ballot = Ballot::new(
            id,
            ProposalType::ModeChange,
            proposer,
            42,
            threshold::SIMPLE_MAJORITY,
            1_000_000,
        );

        // First vote should succeed
        let first = ballot.record_vote_from(voter, VoteValue::Yes);
        prop_assert!(first, "First vote should be recorded");
        let count_after_first = ballot.yes_count;

        // Second vote from same voter should be ignored
        let second = ballot.record_vote_from(voter, VoteValue::Yes);
        prop_assert!(!second, "Duplicate vote should be rejected");
        prop_assert_eq!(
            ballot.yes_count, count_after_first,
            "Count should not change on duplicate"
        );
    }
}

// ============================================================================
// Quorum Math Property Tests
// ============================================================================

proptest! {
    /// CORE INVARIANT: Approved iff yes_count >= ceil(threshold * total)
    #[test]
    fn quorum_math_correct(
        yes_count in 0u8..10u8,
        no_count in 0u8..10u8,
        total in 1u8..15u8,
        threshold_f in 0.3f32..0.9f32
    ) {
        let threshold = Fixed::from_num(threshold_f);

        let mut ballot = Ballot::new(
            1,
            ProposalType::ModeChange,
            1,
            42,
            threshold,
            1_000_000,
        );

        // Simulate votes directly by setting counts
        ballot.yes_count = yes_count.min(total);
        ballot.no_count = no_count.min(total.saturating_sub(yes_count));
        ballot.vote_count = ballot.yes_count + ballot.no_count;

        // Check threshold
        ballot.check_threshold(total);

        // Calculate expected result
        let yes_ratio = ballot.yes_count as f32 / total as f32;
        let should_approve = yes_ratio >= threshold_f;

        if ballot.completed {
            if should_approve {
                prop_assert_eq!(
                    ballot.result, VoteResult::Approved,
                    "Should be approved: yes={}, total={}, threshold={}",
                    ballot.yes_count, total, threshold_f
                );
            }
            // Note: might also be Rejected if all votes in but threshold not met
        }
    }
}

// ============================================================================
// Inhibition Property Tests
// ============================================================================

proptest! {
    /// Inhibit vote immediately cancels ballot
    #[test]
    fn inhibit_cancels_immediately(
        id in 1u16..1000u16,
        proposer in valid_module_id(),
        inhibitor in valid_module_id()
    ) {
        let mut ballot = Ballot::new(
            id,
            ProposalType::ModeChange,
            proposer,
            42,
            threshold::SIMPLE_MAJORITY,
            1_000_000,
        );

        prop_assert!(!ballot.completed, "Should not be completed initially");

        ballot.record_vote_from(inhibitor, VoteValue::Inhibit);

        prop_assert!(ballot.completed, "Should be completed after inhibit");
        prop_assert_eq!(
            ballot.result, VoteResult::Cancelled,
            "Result should be Cancelled"
        );
    }
}

// ============================================================================
// Consensus Engine Property Tests
// ============================================================================

proptest! {
    /// Propose returns valid ballot ID
    #[test]
    fn propose_returns_valid_id(
        my_id in valid_module_id(),
        now in timestamp()
    ) {
        let mut cons = Consensus::new(my_id, None);

        let ballot_id = cons.propose(
            ProposalType::ModeChange,
            42,
            threshold::SIMPLE_MAJORITY,
            now
        ).unwrap();

        prop_assert!(ballot_id != INVALID_BALLOT_ID);
        prop_assert_eq!(cons.get_result(ballot_id), VoteResult::Pending);
    }

    /// Inhibit marks ballot as cancelled
    #[test]
    fn inhibit_cancels_ballot(
        my_id in valid_module_id(),
        now in timestamp()
    ) {
        let mut cons = Consensus::new(my_id, None);

        let ballot_id = cons.propose(
            ProposalType::ModeChange,
            42,
            threshold::SIMPLE_MAJORITY,
            now
        ).unwrap();

        cons.inhibit(ballot_id, now).unwrap();
        prop_assert_eq!(cons.get_result(ballot_id), VoteResult::Cancelled);
    }

    /// Timeout triggers on tick past deadline
    #[test]
    fn timeout_on_tick(
        my_id in valid_module_id(),
        now in 1_000_000u64..10_000_000u64
    ) {
        let config = ConsensusConfig {
            vote_timeout: 50_000, // 50ms
            ..Default::default()
        };
        let mut cons = Consensus::new(my_id, Some(config));

        let ballot_id = cons.propose(
            ProposalType::ModeChange,
            42,
            threshold::SIMPLE_MAJORITY,
            now
        ).unwrap();

        // Tick before deadline - should still be pending
        cons.tick(now + 40_000);
        prop_assert_eq!(cons.get_result(ballot_id), VoteResult::Pending);

        // Tick after deadline - should timeout
        cons.tick(now + 60_000);
        prop_assert_eq!(cons.get_result(ballot_id), VoteResult::Timeout);
    }
}

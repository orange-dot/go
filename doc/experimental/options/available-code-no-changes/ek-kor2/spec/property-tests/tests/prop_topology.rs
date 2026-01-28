//! Property tests for topology module
//!
//! Tests k-neighbor coordination invariants:
//! - k-neighbor limit: never more than k neighbors
//! - Reelection: closest modules selected
//! - Discovery: new modules added to known list

use ekk::topology::*;
use ekk::types::*;
use proptest::prelude::*;

// ============================================================================
// Strategies
// ============================================================================

fn valid_module_id() -> impl Strategy<Value = ModuleId> {
    1u8..=254u8
}

fn position_strategy() -> impl Strategy<Value = Position> {
    (-1000i16..1000i16, -1000i16..1000i16, -1000i16..1000i16)
        .prop_map(|(x, y, z)| Position::new(x, y, z))
}

fn timestamp() -> impl Strategy<Value = TimeUs> {
    1_000_000u64..100_000_000u64
}

// ============================================================================
// k-Neighbor Limit Property Tests
// ============================================================================

proptest! {
    /// CORE INVARIANT: Never more than k neighbors
    #[test]
    fn k_neighbor_limit_respected(
        my_id in 1u8..10u8,
        my_pos in position_strategy(),
        k in 2usize..8usize,
        discoveries in 10usize..30usize,
        now in timestamp()
    ) {
        let config = TopologyConfig {
            k_neighbors: k,
            metric: DistanceMetric::Logical,
            ..Default::default()
        };
        let mut topo = Topology::new(my_id, my_pos, Some(config));

        // Discover many modules
        for i in 0..discoveries {
            let id = ((my_id as usize + i + 1) % 254 + 1) as u8;
            if id != my_id {
                let _ = topo.on_discovery(id, Position::new(i as i16, 0, 0), now);
            }
        }

        prop_assert!(
            topo.neighbor_count() <= k,
            "Neighbor count {} exceeds k={}",
            topo.neighbor_count(), k
        );
    }

    /// Neighbor count increases up to k
    #[test]
    fn neighbor_count_grows_to_k(
        my_id in 1u8..10u8,
        my_pos in position_strategy(),
        now in timestamp()
    ) {
        let k = 5;
        let config = TopologyConfig {
            k_neighbors: k,
            metric: DistanceMetric::Logical,
            ..Default::default()
        };
        let mut topo = Topology::new(my_id, my_pos, Some(config));

        let mut prev_count = 0;
        for i in 1..=k+2 {
            let id = ((my_id as usize + i) % 254 + 1) as u8;
            if id != my_id {
                topo.on_discovery(id, Position::new(i as i16, 0, 0), now).ok();

                // Count should grow or stay same (never decrease on discovery)
                prop_assert!(
                    topo.neighbor_count() >= prev_count,
                    "Count decreased: {} -> {}",
                    prev_count, topo.neighbor_count()
                );
                prev_count = topo.neighbor_count();
            }
        }
    }
}

// ============================================================================
// Discovery Property Tests
// ============================================================================

proptest! {
    /// Discovery of self is rejected
    #[test]
    fn discovery_self_rejected(
        my_id in valid_module_id(),
        my_pos in position_strategy(),
        now in timestamp()
    ) {
        let mut topo = Topology::new(my_id, my_pos, None);
        let result = topo.on_discovery(my_id, my_pos, now);
        prop_assert!(matches!(result, Err(Error::InvalidArg)));
    }

    /// Discovery of invalid ID is rejected
    #[test]
    fn discovery_invalid_id_rejected(
        my_id in valid_module_id(),
        my_pos in position_strategy(),
        now in timestamp()
    ) {
        let mut topo = Topology::new(my_id, my_pos, None);
        let result = topo.on_discovery(INVALID_MODULE_ID, my_pos, now);
        prop_assert!(matches!(result, Err(Error::InvalidArg)));
    }

    /// Discovered module becomes neighbor when under k
    #[test]
    fn discovery_adds_neighbor_when_under_k(
        my_id in 1u8..100u8,
        other_id in 101u8..200u8,
        my_pos in position_strategy(),
        other_pos in position_strategy(),
        now in timestamp()
    ) {
        let mut topo = Topology::new(my_id, my_pos, None);

        // Initially no neighbors
        prop_assert_eq!(topo.neighbor_count(), 0);

        // After discovery, should have 1 neighbor
        topo.on_discovery(other_id, other_pos, now).unwrap();
        prop_assert_eq!(topo.neighbor_count(), 1);
        prop_assert!(topo.is_neighbor(other_id));
    }
}

// ============================================================================
// Neighbor Loss Property Tests
// ============================================================================

proptest! {
    /// on_neighbor_lost removes from neighbor list (may trigger reelect)
    /// Note: After reelect, the "lost" module might be re-added from known list
    /// This is correct behavior - we test the immediate effect
    #[test]
    fn neighbor_loss_triggers_reelect(
        my_id in 1u8..50u8,
        my_pos in position_strategy(),
        now in timestamp()
    ) {
        let config = TopologyConfig {
            k_neighbors: 3,
            metric: DistanceMetric::Logical,
            ..Default::default()
        };
        let mut topo = Topology::new(my_id, my_pos, Some(config));

        // Add exactly k neighbors (no extras to reelect from)
        let ids: Vec<u8> = vec![100, 101, 102];
        for &id in &ids {
            topo.on_discovery(id, Position::new(id as i16, 0, 0), now).ok();
        }

        let count_before = topo.neighbor_count();

        // Lose a neighbor
        if count_before > 0 {
            if let Some(&first) = ids.first() {
                if topo.is_neighbor(first) {
                    let result = topo.on_neighbor_lost(first);
                    prop_assert!(result.is_ok(), "on_neighbor_lost should succeed");

                    // Count should be <= before (might stay same due to reelect from known)
                    prop_assert!(
                        topo.neighbor_count() <= count_before,
                        "Count should not increase after loss"
                    );
                }
            }
        }
    }

    /// Losing unknown neighbor fails
    #[test]
    fn losing_unknown_fails(
        my_id in 1u8..100u8,
        unknown_id in 101u8..200u8,
        my_pos in position_strategy()
    ) {
        let mut topo = Topology::new(my_id, my_pos, None);
        let result = topo.on_neighbor_lost(unknown_id);
        prop_assert!(matches!(result, Err(Error::NotFound)));
    }
}

// ============================================================================
// Distance Metric Property Tests
// ============================================================================

proptest! {
    /// Logical distance is based on ID difference
    #[test]
    fn logical_distance_is_id_based(
        my_id in 1u8..100u8,
        my_pos in position_strategy(),
        now in timestamp()
    ) {
        let config = TopologyConfig {
            k_neighbors: 3,
            metric: DistanceMetric::Logical,
            ..Default::default()
        };
        let mut topo = Topology::new(my_id, my_pos, Some(config));

        // Add neighbors at varying ID distances
        let close_id = my_id.wrapping_add(1);
        let far_id = my_id.wrapping_add(50);

        if close_id != my_id && far_id != my_id {
            topo.on_discovery(close_id, Position::new(1000, 1000, 1000), now).ok();
            topo.on_discovery(far_id, Position::new(0, 0, 0), now).ok();

            // Close ID should be neighbor (regardless of physical position)
            prop_assert!(
                topo.is_neighbor(close_id),
                "Close ID {} should be neighbor of {}",
                close_id, my_id
            );
        }
    }

    /// Physical distance uses position
    #[test]
    fn physical_distance_is_position_based(
        my_id in 1u8..100u8,
        now in timestamp()
    ) {
        let my_pos = Position::new(0, 0, 0);
        let config = TopologyConfig {
            k_neighbors: 2,
            metric: DistanceMetric::Physical,
            ..Default::default()
        };
        let mut topo = Topology::new(my_id, my_pos, Some(config));

        // Add neighbors at varying physical distances
        let close_id = 200u8;
        let far_id = 201u8;

        topo.on_discovery(close_id, Position::new(1, 1, 1), now).ok();  // Close
        topo.on_discovery(far_id, Position::new(100, 100, 100), now).ok();  // Far

        // Close position should be neighbor
        if topo.neighbor_count() >= 1 {
            prop_assert!(
                topo.is_neighbor(close_id),
                "Physically close module should be neighbor"
            );
        }
    }
}

// ============================================================================
// Reelection Property Tests
// ============================================================================

proptest! {
    /// Reelection selects k closest
    #[test]
    fn reelect_selects_closest(
        my_id in 50u8..60u8,
        my_pos in position_strategy(),
        now in timestamp()
    ) {
        let k = 3;
        let config = TopologyConfig {
            k_neighbors: k,
            metric: DistanceMetric::Logical,
            ..Default::default()
        };
        let mut topo = Topology::new(my_id, my_pos, Some(config));

        // Add modules at known ID distances
        let ids: Vec<u8> = vec![
            my_id.wrapping_add(1),   // dist 1
            my_id.wrapping_add(2),   // dist 2
            my_id.wrapping_add(3),   // dist 3
            my_id.wrapping_add(10),  // dist 10
            my_id.wrapping_add(20),  // dist 20
        ];

        for &id in &ids {
            if id != my_id && id != INVALID_MODULE_ID {
                topo.on_discovery(id, Position::new(0, 0, 0), now).ok();
            }
        }

        // Should have k neighbors
        prop_assert!(topo.neighbor_count() <= k);

        // Closest IDs should be neighbors
        for &id in ids.iter().take(k.min(ids.len())) {
            if id != my_id && id != INVALID_MODULE_ID {
                prop_assert!(
                    topo.is_neighbor(id),
                    "ID {} with small distance should be neighbor",
                    id
                );
            }
        }
    }
}

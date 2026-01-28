# EK-KOR2 Property-Based Tests

Standalone property-based test suite using [proptest](https://proptest-rs.github.io/proptest/).

**This project is completely isolated from the main `ekk` crate and does not affect any existing code or tests.**

## Usage

```bash
cd spec/property-tests

# Run all property tests
cargo test

# Run specific module tests
cargo test types
cargo test field
cargo test consensus
cargo test heartbeat
cargo test topology

# Run with verbose output
cargo test -- --nocapture

# More test cases (default: 256)
PROPTEST_CASES=1000 cargo test
```

## Test Modules

| Module | File | Key Properties |
|--------|------|----------------|
| types | `tests/prop_types.rs` | Position distance symmetry, Field validity, Fixed-point roundtrip |
| field | `tests/prop_field.rs` | Gradient antisymmetry, Decay monotonicity, Publish/sample identity |
| consensus | `tests/prop_consensus.rs` | Vote count monotonicity, Quorum math, Inhibition |
| heartbeat | `tests/prop_heartbeat.rs` | State transitions, Timing thresholds |
| topology | `tests/prop_topology.rs` | k-neighbor limit, Distance metrics |

## Mathematical Invariants Tested

### Gradient Antisymmetry
```
gradient(a, b) == -gradient(b, a)
```

### Decay Monotonicity
```
sample(t2) <= sample(t1)  when t2 > t1
```

### Vote Count Monotonicity
```
yes_count(t2) >= yes_count(t1)  when t2 > t1
```

### State Transition Validity
```
Unknown -> Alive -> Suspect -> Dead (valid transitions only)
```

### k-Neighbor Invariant
```
neighbor_count <= k  (always)
```

## Why Property Tests?

Property tests verify **mathematical invariants** without needing expected outputs:

| Unit Test | Property Test |
|-----------|---------------|
| `gradient(0.3, 0.7) == 0.4` | `gradient(a, b) == -gradient(b, a)` for all a, b |
| Specific input/output | Universal property |
| May miss edge cases | Random exploration |
| Fragile to spec changes | Robust invariants |

## Integration with Test Pyramid

```
┌─────────────────────────────────────────┐
│  Layer 5: Mutation Testing              │
│  Layer 4: LLM Oracle                    │
│  Layer 3: Python Reference              │
│  Layer 2: Golden Outputs                │
│  Layer 1: Property Tests  <-- THIS      │
│  Layer 0: Cross-Validation (C vs Rust)  │
└─────────────────────────────────────────┘
```

Property tests complement (don't replace) the existing cross-validation system.

## Adding New Properties

1. Identify a mathematical invariant in the spec
2. Add test to appropriate `tests/prop_*.rs` file
3. Use proptest strategies for random input generation

Example:
```rust
proptest! {
    #[test]
    fn my_invariant(x in 0.0f32..1.0f32, y in 0.0f32..1.0f32) {
        // Property that should always hold
        prop_assert!(some_function(x, y) == expected_property);
    }
}
```

# EK-KOR v2 Development Guide

## Parallel Development Strategy

This project develops the same RTOS in both C and Rust simultaneously.
The goal is cross-validation and learning both languages.

## Golden Rules

### 1. Spec is Truth

```
spec/test-vectors/*.json  →  THE TRUTH
c/src/*.c                 →  implementation of truth
rust/src/*.rs             →  implementation of truth
```

If C and Rust disagree, check the spec. If spec is ambiguous, fix the spec first.

### 2. Implement in Pairs

Always implement the same function in both languages before moving on:

```
Day 1 AM:  Implement field_publish() in C
Day 1 PM:  Implement field_publish() in Rust
Day 2:    Run test vectors, compare, debug
Day 3:    Next function
```

### 3. Test Vectors are Shared

```json
// spec/test-vectors/field_001.json
{
  "name": "field_publish_basic",
  "input": {
    "module_id": 42,
    "load": 0.5,
    "thermal": 0.3,
    "power": 0.8,
    "timestamp": 1000
  },
  "expected": {
    "stored_source": 42,
    "stored_load": 0.5,
    "return": "OK"
  }
}
```

Both C and Rust tests read this JSON and verify their implementation.

## Development Environment

### C Setup

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake

# Windows (MSYS2)
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake

# Build
cd c
mkdir build && cd build
cmake ..
make
ctest
```

### Rust Setup

```bash
# Install Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# Build and test
cd rust
cargo build
cargo test

# For embedded (later)
rustup target add thumbv7em-none-eabihf
```

### Python Tools

```bash
pip install json5 tabulate matplotlib
```

## Module Implementation Checklist

For each module (field, topology, consensus, heartbeat, module):

### Phase 1: Understand

- [ ] Read spec/api.md section for this module
- [ ] Study state machine diagram
- [ ] Review test vectors
- [ ] Understand all edge cases

### Phase 2: C Implementation

- [ ] Create `c/src/ekk_<module>.c`
- [ ] Implement all functions from header
- [ ] Write unit tests in `c/test/test_<module>.c`
- [ ] Run test vectors
- [ ] All tests pass

### Phase 3: Rust Implementation

- [ ] Implement in `rust/src/<module>.rs`
- [ ] Write inline `#[test]` functions
- [ ] Run test vectors
- [ ] All tests pass

### Phase 4: Cross-Validation

- [ ] Run `python tools/run_tests.py <module>`
- [ ] Both implementations produce identical output
- [ ] Document any discrepancies and their resolution

## Code Style

### C Style

```c
// MISRA-C inspired, but practical

// Naming: snake_case with ekk_ prefix
ekk_error_t ekk_field_publish(ekk_module_id_t module_id, const ekk_field_t *field);

// Braces: Allman style
if (condition)
{
    do_something();
}

// Comments: Doxygen
/**
 * @brief Publish module's coordination field
 * @param module_id Module identifier
 * @param field Field values to publish
 * @return EKK_OK on success
 */

// No magic numbers
#define EKK_FIELD_DECAY_TAU_US  100000  /* 100ms */
```

### Rust Style

```rust
// Standard Rust style (rustfmt)

// Naming: snake_case for functions, CamelCase for types
pub fn field_publish(module_id: ModuleId, field: &Field) -> Result<()>

// Documentation
/// Publish module's coordination field
///
/// # Arguments
/// * `module_id` - Module identifier
/// * `field` - Field values to publish
///
/// # Returns
/// * `Ok(())` on success

// Use clippy
#![warn(clippy::all)]
```

## Testing Strategy

### Unit Tests

Each function has dedicated tests:

```c
// C: c/test/test_field.c
void test_field_publish_basic(void) {
    ekk_field_t field = {0};
    field.components[EKK_FIELD_LOAD] = EKK_FLOAT_TO_FIXED(0.5f);

    ekk_error_t err = ekk_field_publish(42, &field);

    TEST_ASSERT_EQUAL(EKK_OK, err);
}
```

```rust
// Rust: rust/src/field.rs
#[test]
fn test_field_publish_basic() {
    let mut region = FieldRegion::new();
    let field = Field::with_values(Fixed::from_num(0.5), Fixed::ZERO, Fixed::ZERO);

    let result = engine.publish(&mut region, 42, &field, 1000);

    assert!(result.is_ok());
}
```

### Test Vector Tests

```python
# tools/run_tests.py
def run_test_vector(vector_file, language):
    vector = json.load(open(vector_file))

    if language == 'c':
        output = run_c_test(vector)
    else:
        output = run_rust_test(vector)

    assert output == vector['expected']
```

## Debugging Tips

### When C and Rust Disagree

1. **Print intermediate values**
   ```c
   printf("C: field.load = %d\n", field.components[0]);
   ```
   ```rust
   println!("Rust: field.load = {:?}", field.components[0]);
   ```

2. **Check fixed-point conversion**
   - C: `EKK_FLOAT_TO_FIXED(0.5)` = 32768
   - Rust: `Fixed::from_num(0.5)` = should also be 32768

3. **Check struct layout**
   ```c
   printf("sizeof(ekk_field_t) = %zu\n", sizeof(ekk_field_t));
   ```
   ```rust
   println!("size_of::<Field>() = {}", std::mem::size_of::<Field>());
   ```

4. **Binary comparison**
   - Dump structs to binary files
   - Compare with hex diff

### Common Pitfalls

| Issue | C | Rust |
|-------|---|------|
| Integer overflow | Silent wraparound | Panic in debug |
| Null pointer | Crash | `Option<T>` |
| Array bounds | Undefined behavior | Panic |
| Uninitialized | Garbage values | Compile error |

## Progress Tracking

Use git branches:

```
main                 # Stable, both languages work
├── c/field          # C field implementation
├── rust/field       # Rust field implementation
├── c/topology       # ...
└── rust/topology
```

Merge to main only when:
1. Both implementations complete
2. All test vectors pass
3. Cross-validation passes

## Weekly Milestones

### Week 1-2: Field Module
- [ ] C: ekk_field_init
- [ ] C: ekk_field_publish
- [ ] C: ekk_field_sample
- [ ] C: ekk_field_gradient
- [ ] Rust: same functions
- [ ] Cross-validation passes

### Week 3-4: Topology Module
- [ ] C: ekk_topology_init
- [ ] C: ekk_topology_on_discovery
- [ ] C: ekk_topology_reelect
- [ ] C: ekk_topology_distance
- [ ] Rust: same functions
- [ ] Cross-validation passes

### Week 5-6: Heartbeat Module
- [ ] C: ekk_heartbeat_init
- [ ] C: ekk_heartbeat_received
- [ ] C: ekk_heartbeat_tick
- [ ] Rust: same functions
- [ ] Cross-validation passes

### Week 7-8: Consensus Module
- [ ] C: ekk_consensus_propose
- [ ] C: ekk_consensus_vote
- [ ] C: ekk_consensus_on_vote
- [ ] C: ekk_consensus_tick
- [ ] Rust: same functions
- [ ] Cross-validation passes

### Week 9-10: Module Integration
- [ ] C: ekk_module_init
- [ ] C: ekk_module_tick
- [ ] C: Full integration test
- [ ] Rust: same
- [ ] Multi-module simulation

### Week 11+: Hardware
- [ ] C: STM32G474 HAL
- [ ] Rust: STM32G474 HAL
- [ ] Hardware test on real board

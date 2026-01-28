# EK-KOR2 Test Vector System

## Overview

The EK-KOR2 project uses a **JSON-based test vector system** for cross-language validation between C and Rust implementations. Test vectors serve as the single source of truth - if implementations disagree, the spec (test vectors) determines correct behavior.

```
spec/test-vectors/*.json  →  THE TRUTH
c/src/*.c                 →  implementation of truth
rust/src/*.rs             →  implementation of truth
```

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Test Vector System                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   spec/test-vectors/*.json                                      │
│         │                                                       │
│         ├──────────────────┬────────────────────┐              │
│         ▼                  ▼                    ▼              │
│   ┌───────────┐     ┌───────────┐     ┌─────────────────┐     │
│   │ C Harness │     │Rust Harness│     │ Python Runner  │     │
│   │test_harness│     │test_harness│     │ run_tests.py   │     │
│   └─────┬─────┘     └─────┬─────┘     └────────┬────────┘     │
│         │                 │                    │               │
│         ▼                 ▼                    ▼               │
│   ┌───────────┐     ┌───────────┐     ┌─────────────────┐     │
│   │ JSON Out  │     │ JSON Out  │     │ Cross-Validate  │     │
│   └───────────┘     └───────────┘     │ & Report        │     │
│                                       └─────────────────┘     │
└─────────────────────────────────────────────────────────────────┘
```

## Components

### 1. Test Vector Files (`spec/test-vectors/`)

JSON files that define inputs, expected outputs, and optional setup/teardown:

| File Pattern | Module | Count |
|-------------|--------|-------|
| `field_*.json` | Field coordination | 7 |
| `topology_*.json` | Neighbor topology | 4 |
| `consensus_*.json` | Voting/consensus | 5 |
| `heartbeat_*.json` | Health monitoring | 4 |
| `auth_*.json` | Authentication (Chaskey MAC) | 1 |
| `spsc_*.json` | Lock-free queue | 1 |
| `q15_*.json` | Fixed-point types | 1 |

### 2. C Test Harness (`c/test/test_harness.c`)

- Reads JSON test vectors using cJSON library
- Executes tests against C implementation
- Outputs JSON results to stdout
- Supports all modules: field, topology, consensus, heartbeat, auth, spsc, types

```bash
# Build
cd ek-kor2/c && mkdir build && cd build
cmake .. && make

# Run single test
./test_harness ../spec/test-vectors/field_001_publish_basic.json

# Run all field tests
./test_harness ../spec/test-vectors/field_*.json
```

### 3. Rust Test Harness (`rust/src/bin/test_harness.rs`)

- Mirrors C harness functionality
- Uses serde_json for parsing
- Same JSON output format for cross-validation

```bash
# Build
cd ek-kor2/rust
cargo build --bin test_harness

# Run
./target/debug/test_harness ../spec/test-vectors/field_001_publish_basic.json
```

### 4. Python Cross-Validator (`tools/run_tests.py`)

- Runs both C and Rust harnesses
- Compares outputs for identical behavior
- Reports discrepancies

```bash
# Run all tests (both languages)
python tools/run_tests.py

# Run specific module
python tools/run_tests.py field

# Run single language
python tools/run_tests.py --lang c
python tools/run_tests.py --lang rust

# Verbose output
python tools/run_tests.py -v
```

## Test Vector Format

### Basic Structure

```json
{
  "id": "field_001",
  "name": "field_publish_basic",
  "module": "field",
  "function": "field_publish",
  "description": "Basic field publish operation",

  "input": {
    "module_id": 42,
    "field": {
      "load": 0.5,
      "thermal": 0.3,
      "power": 0.8
    },
    "timestamp": 1000000
  },

  "expected": {
    "return": "OK",
    "region_state": {
      "fields[42].source": 42,
      "fields[42].components[0]": 32768
    }
  },

  "notes": [
    "Fixed-point: 0.5 = 32768 (0x8000)",
    "Seqlock pattern: seq goes 0->1->2"
  ]
}
```

### Multi-Step Tests

For stateful operations (voting, heartbeat transitions):

```json
{
  "id": "consensus_002",
  "name": "consensus_vote_approved",
  "module": "consensus",
  "function": "consensus_on_vote",

  "setup": {
    "init": {"my_id": 1},
    "propose": {
      "proposal_type": "ModeChange",
      "data": 42,
      "threshold": 0.67
    }
  },

  "steps": [
    {
      "input": {"voter_id": 2, "vote": "Yes", "total_voters": 7},
      "expected": {"result": "Pending", "yes_count": 1}
    },
    {
      "input": {"voter_id": 3, "vote": "Yes", "total_voters": 7},
      "expected": {"result": "Pending", "yes_count": 2}
    },
    {
      "input": {"voter_id": 5, "vote": "Yes", "total_voters": 7},
      "expected": {"result": "Approved", "yes_count": 5}
    }
  ]
}
```

### Setup Section

Optional pre-test initialization:

```json
"setup": {
  "init": {
    "my_id": 1,
    "my_position": {"x": 0, "y": 0, "z": 0},
    "metric": "Logical"
  },
  "discoveries": [
    {"sender_id": 2, "sender_position": {"x": 1, "y": 0, "z": 0}},
    {"sender_id": 3, "sender_position": {"x": 2, "y": 0, "z": 0}}
  ],
  "publish": {
    "module_id": 42,
    "field": {"load": 0.5},
    "timestamp": 1000000
  }
}
```

## Modules Covered

### Field Module
- `field_publish` - Publish coordination field
- `field_sample` - Read field with decay
- `field_gradient` - Calculate gradient between fields

### Topology Module
- `topology_on_discovery` - Handle neighbor discovery
- `topology_reelect` - Re-select k-nearest neighbors
- `topology_on_neighbor_lost` - Handle neighbor loss

### Consensus Module
- `consensus_propose` - Create new ballot
- `consensus_on_vote` - Process incoming vote
- `consensus_vote` - Cast own vote
- `consensus_inhibit` - Safety inhibit mechanism
- `consensus_tick` - Timeout processing

### Heartbeat Module
- `heartbeat_received` - Process incoming heartbeat
- `heartbeat_tick` - State machine transitions (Alive→Suspect→Dead)

### Types Module
- `ekk_fixed_to_q15` - Q16.16 to Q1.15 conversion
- `ekk_q15_to_fixed` - Q1.15 to Q16.16 conversion
- `ekk_q15_mul` - Q15 multiplication
- `ekk_q15_add_sat` / `ekk_q15_sub_sat` - Saturating arithmetic

### Auth Module
- `ekk_auth_compute` - Chaskey MAC computation
- `ekk_auth_verify` - MAC verification
- `ekk_auth_is_required` - Message type auth requirements

### SPSC Module
- `ekk_spsc_init` - Initialize lock-free queue
- `ekk_spsc_push` / `ekk_spsc_pop` - Queue operations
- `ekk_spsc_is_empty` - Queue state check

## Fixed-Point Representation

The system uses Q16.16 fixed-point internally:

| Float | Fixed (dec) | Fixed (hex) |
|-------|-------------|-------------|
| 0.0 | 0 | 0x00000000 |
| 0.25 | 16384 | 0x00004000 |
| 0.3 | 19661 | 0x00004CCD |
| 0.5 | 32768 | 0x00008000 |
| 0.8 | 52429 | 0x0000CCCD |
| 1.0 | 65536 | 0x00010000 |

Test vectors specify expected fixed-point values for exact validation.

## Error Handling

Standard error codes across both implementations:

| Error | C Constant | Rust Enum |
|-------|-----------|-----------|
| Success | `EKK_OK` | `Ok(())` |
| Invalid argument | `EKK_ERR_INVALID_ARG` | `InvalidArg` |
| Not found | `EKK_ERR_NOT_FOUND` | `NotFound` |
| No memory | `EKK_ERR_NO_MEMORY` | `NoMemory` |
| Timeout | `EKK_ERR_TIMEOUT` | `Timeout` |
| Busy | `EKK_ERR_BUSY` | `Busy` |
| Already exists | `EKK_ERR_ALREADY_EXISTS` | `AlreadyExists` |
| No quorum | `EKK_ERR_NO_QUORUM` | `NoQuorum` |
| Inhibited | `EKK_ERR_INHIBITED` | `Inhibited` |
| Field expired | `EKK_ERR_FIELD_EXPIRED` | `FieldExpired` |

## Development Workflow

### Adding New Test Vectors

1. Create JSON file in `spec/test-vectors/`:
   ```
   <module>_<number>_<description>.json
   ```

2. Define required fields:
   - `id`, `name`, `module`, `function`
   - `input` - test inputs
   - `expected` - expected results
   - Optional: `setup`, `steps`, `notes`

3. Add handler in both test harnesses if new function

4. Run cross-validation:
   ```bash
   python tools/run_tests.py <module>
   ```

### Debugging Discrepancies

When C and Rust disagree:

1. **Print intermediate values**
   ```c
   printf("C: field.load = %d\n", field.components[0]);
   ```
   ```rust
   println!("Rust: field.load = {:?}", field.components[0]);
   ```

2. **Check fixed-point conversion**
   - Both should produce identical integer values

3. **Verify struct layouts**
   ```c
   printf("sizeof(ekk_field_t) = %zu\n", sizeof(ekk_field_t));
   ```

4. **Check test vector spec** - if ambiguous, fix spec first

## Output Format

Both harnesses output JSON arrays:

```json
[
  {
    "id": "field_001",
    "module": "field",
    "function": "field_publish",
    "passed": true,
    "return": "OK",
    "region_state": {
      "fields[42].source": 42,
      "fields[42].sequence": 2
    }
  }
]
```

Failed tests include error details:

```json
{
  "id": "field_002",
  "passed": false,
  "error": "Return code mismatch",
  "expected": "OK",
  "actual": "ERR_INVALID_ARG"
}
```

## Integration with CI/CD

```yaml
# Example GitHub Actions
test-vectors:
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4

    - name: Build C harness
      run: |
        cd ek-kor2/c && mkdir build && cd build
        cmake .. && make

    - name: Build Rust harness
      run: |
        cd ek-kor2/rust
        cargo build --bin test_harness

    - name: Run cross-validation
      run: |
        cd ek-kor2
        python tools/run_tests.py
```

---

## Single-Implementation Testing

When only **one implementation** (C or Rust) is available, cross-validation isn't possible. The following multi-layer approach provides alternative validation:

```
┌──────────────────────────────────────────────────────────────┐
│               MULTI-LAYER TEST PYRAMID                        │
├──────────────────────────────────────────────────────────────┤
│                                                               │
│  LAYER 5: MUTATION TESTING (meta-validation)                 │
│     - Verifies that tests actually catch bugs                │
│     - cargo-mutants for Rust                                 │
│                                                               │
│  LAYER 4: LLM ORACLE (AI-assisted)                           │
│     - Generates expected outputs from spec                   │
│     - Suggests new edge-case test vectors                    │
│     - Analyzes test failures                                 │
│                                                               │
│  LAYER 3: PYTHON REFERENCE (executable spec)                 │
│     - Lightweight impl of critical functions                 │
│     - Replaces second implementation when unavailable        │
│                                                               │
│  LAYER 2: GOLDEN OUTPUTS (regression testing)                │
│     - Captures verified outputs as reference                 │
│     - Detects unintentional changes                          │
│                                                               │
│  LAYER 1: PROPERTY TESTS (mathematical invariants)           │
│     - No expected output needed - just properties            │
│     - Discovers edge cases automatically                     │
│                                                               │
│  LAYER 0: CROSS-VALIDATION (C vs Rust) - PRIMARY             │
│     - Two independent implementations validate each other    │
│     - This is the gold standard when available               │
│                                                               │
└──────────────────────────────────────────────────────────────┘
```

### Why Cross-Validation Works

| Scenario | One Impl | Two Impl |
|----------|----------|----------|
| Spec ambiguous ("majority = ?") | Impl picks one interpretation | Disagreement reveals problem |
| Fixed-point error (0.3 → 19660 vs 19661) | Passes silently | Difference catches bug |
| Race condition in seqlock | May pass | Different behavior reveals |

**Core insight:** Two independent implementations validate each other.

### Layer 1: Property Tests

Mathematical invariants that don't need expected outputs:

```bash
cd spec/property-tests
cargo test
```

Key properties tested:
- `gradient(a,b) == -gradient(b,a)` (antisymmetry)
- `sample(t2) <= sample(t1)` when `t2 > t1` (decay monotonicity)
- `yes_count` never decreases (vote monotonicity)
- `neighbor_count <= k` (k-neighbor limit)

**When to use:** Always. Complements all other layers.

### Layer 2: Golden Outputs

Capture verified outputs for regression testing:

```bash
# Capture golden outputs from working implementation
python tools/golden_capture.py --lang rust

# Later, validate current output against golden
python tools/golden_validate.py --lang rust
```

Golden files stored in: `spec/golden-outputs/*.golden.json`

**When to use:** After verifying correctness once, to catch regressions.

### Layer 3: Python Reference

Lightweight Python implementation for critical functions:

```bash
# Run single test vector
python tools/reference_impl.py spec/test-vectors/field_001.json

# Validate all vectors
python tools/reference_impl.py --validate
```

Implemented functions:
- `gradient()` - field gradient calculation
- `quorum_check()` - consensus threshold math
- `heartbeat_state_transition()` - state machine
- `Q16_16` class - fixed-point arithmetic

**When to use:** When neither C nor Rust implementation is available.

### Layer 4: LLM Oracle

AI-assisted test generation and analysis:

```bash
# Set API key
export ANTHROPIC_API_KEY=your_key

# Generate expected output from spec
python tools/llm_oracle.py expected spec/api.md spec/test-vectors/field_001.json

# Generate new test vector
python tools/llm_oracle.py generate spec/api.md field_gradient

# Analyze test failure
python tools/llm_oracle.py analyze spec/api.md expected.json actual.json
```

**When to use:** Generating edge cases, understanding spec ambiguities, hackathon demos.

### Layer 5: Mutation Testing

Verify test quality by injecting bugs:

```bash
cargo install cargo-mutants
cd rust
cargo mutants --package ekk
```

If tests pass with a mutation, tests are weak for that code path.

**When to use:** To measure and improve test coverage quality.

### Decision Matrix

| Situation | Recommended Layers |
|-----------|-------------------|
| Both C and Rust available | Layer 0 (cross-validation) + Layer 1 (properties) |
| Only Rust available | Layer 1 + Layer 2 + Layer 3 |
| Only C available | Layer 2 + Layer 3 |
| New feature, unclear spec | Layer 4 (LLM) + Layer 1 |
| Regression after refactor | Layer 2 (golden) |
| Hackathon demo | Layer 1 + Layer 4 |

---

## Summary

| Component | Purpose | Location |
|-----------|---------|----------|
| Test Vectors | Single source of truth | `spec/test-vectors/*.json` |
| C Harness | C implementation tests | `c/test/test_harness.c` |
| Rust Harness | Rust implementation tests | `rust/src/bin/test_harness.rs` |
| Python Runner | Cross-validation | `tools/run_tests.py` |
| Golden Capture | Capture reference outputs | `tools/golden_capture.py` |
| Golden Validate | Validate vs reference | `tools/golden_validate.py` |
| Python Reference | Lightweight impl | `tools/reference_impl.py` |
| LLM Oracle | AI-assisted testing | `tools/llm_oracle.py` |
| Property Tests | Mathematical invariants | `spec/property-tests/` |
| API Spec | Function specifications | `spec/api.md` |
| State Machines | State diagrams | `spec/state-machines.md` |

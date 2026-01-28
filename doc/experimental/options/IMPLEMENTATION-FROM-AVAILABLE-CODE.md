# Implementation Plan: Using Available Code

This document describes how to leverage the existing EK-KOR2 and MAPF-HET
implementations to accelerate the experimental Go runtime options.

## Available Code Summary

### EK-KOR2 (`available-code-no-changes/ek-kor2/`)
- **C implementation**: 56 source files, production-quality embedded code
- **Rust implementation**: 31 source files, parallel implementation
- **Test vectors**: 54 JSON files with expected inputs/outputs
- **EKKL spec**: Domain-specific language for formal specification

### MAPF-HET (`available-code-no-changes/mapf-het-research/`)
- **Go implementation**: 62 source files, directly portable
- **Algorithm implementations**: CBS, potential fields, A*, MCTS
- **Core types**: Robot, Task, Workspace, Solution
- **Integration bridge**: EK-KOR2 adapter code

## Porting Strategy

### Principle: Port Algorithms, Rewrite Tests

1. **Port algorithms** from existing Go/C/Rust to `runtime/`
2. **Convert test vectors** from JSON to Go native tests
3. **Use float64** internally with Q15 comparison for validation
4. **Keep reference** implementations external for verification

## Phase 1: Core Infrastructure

### 1.1 Fixed-Point Utilities

**Source**: `ek-kor2/c/include/ekk/ekk_types.h`

**Target**: `runtime/fixedpoint.go`

```go
// runtime/fixedpoint.go

package runtime

// Fixed-point conversion for test vector validation
// Internal calculations use float64 for simplicity

const (
    q15Scale  = 32768      // 2^15
    q31Scale  = 2147483648 // 2^31
)

// floatToQ15 converts float64 [-1, 1) to Q15
func floatToQ15(f float64) int16 {
    if f >= 1.0 {
        return 0x7FFF
    }
    if f < -1.0 {
        return -0x8000
    }
    return int16(f * q15Scale)
}

// q15ToFloat converts Q15 to float64
func q15ToFloat(q int16) float64 {
    return float64(q) / q15Scale
}

// floatToQ31 converts float64 [-1, 1) to Q31
func floatToQ31(f float64) int32 {
    if f >= 1.0 {
        return 0x7FFFFFFF
    }
    if f < -1.0 {
        return -0x80000000
    }
    return int32(f * q31Scale)
}

// q31ToFloat converts Q31 to float64
func q31ToFloat(q int32) float64 {
    return float64(q) / q31Scale
}
```

### 1.2 Test Vector Infrastructure

**Source**: `ek-kor2/spec/test-vectors/*.json`

**Target**: `runtime/testdata/` + `runtime/testvector_test.go`

```go
// runtime/testvector_test.go

package runtime_test

import (
    "encoding/json"
    "os"
    "path/filepath"
    "testing"
)

// TestVector represents a generic test case from EK-KOR2
type TestVector struct {
    Name     string          `json:"name"`
    Category string          `json:"category"`
    Input    json.RawMessage `json:"input"`
    Expected json.RawMessage `json:"expected"`
}

// loadTestVectors loads all JSON test vectors matching pattern
func loadTestVectors(t *testing.T, pattern string) []TestVector {
    t.Helper()

    matches, err := filepath.Glob(pattern)
    if err != nil {
        t.Fatalf("glob %s: %v", pattern, err)
    }

    var vectors []TestVector
    for _, path := range matches {
        data, err := os.ReadFile(path)
        if err != nil {
            t.Fatalf("read %s: %v", path, err)
        }

        var v TestVector
        if err := json.Unmarshal(data, &v); err != nil {
            t.Fatalf("parse %s: %v", path, err)
        }
        vectors = append(vectors, v)
    }

    return vectors
}
```

## Phase 2: Potential Field Implementation (Options A, G, E)

### 2.1 Core Field Types

**Source**:
- `mapf-het-research/internal/algo/potential_field.go`
- `ek-kor2/c/src/ekk_field.c`
- `ek-kor2/rust/src/field.rs`

**Target**: `runtime/sched_field.go`

```go
// runtime/sched_field.go

package runtime

import (
    "math"
    "sync/atomic"
)

// Field represents a coordination field for stigmergic scheduling
// Inspired by EK-KOR2 ekk_field and MAPF-HET potential_field
type schedField struct {
    // Primary field components (float64 internally)
    load    float64 // Load/utilization [0, 1]
    thermal float64 // Thermal pressure [0, 1]
    power   float64 // Power/resource usage [0, 1]
    slack   float64 // Deadline slack [0, 1]

    // Metadata
    tstamp  int64  // Last update timestamp (nanotime)
    seq     uint32 // Sequence number for consistency
}

// Field decay constants (from EK-KOR2 spec)
const (
    fieldDecayLoad    = 0.95  // 5% decay per update
    fieldDecayThermal = 0.90  // 10% decay per update
    fieldUpdatePeriod = 1e6   // 1ms in nanoseconds
)

// decay applies exponential decay to field components
// Matches: ek-kor2/c/src/ekk_field.c:ekk_field_decay()
func (f *schedField) decay(now int64) {
    elapsed := now - f.tstamp
    if elapsed < fieldUpdatePeriod {
        return
    }

    // Number of decay periods
    periods := float64(elapsed) / fieldUpdatePeriod

    // Exponential decay: value *= decay^periods
    f.load *= math.Pow(fieldDecayLoad, periods)
    f.thermal *= math.Pow(fieldDecayThermal, periods)
    f.tstamp = now
}

// gradient computes the field gradient toward neighbors
// Matches: mapf-het-research/internal/algo/potential_field.go:ComputeGradient()
func (f *schedField) gradient(neighbors []*schedField) (dLoad, dThermal float64) {
    if len(neighbors) == 0 {
        return 0, 0
    }

    var sumLoad, sumThermal float64
    for _, n := range neighbors {
        sumLoad += n.load - f.load
        sumThermal += n.thermal - f.thermal
    }

    k := float64(len(neighbors))
    return sumLoad / k, sumThermal / k
}

// urgency computes urgency score from field components
// Matches: mapf-het-research/internal/algo/potential_field.go:Urgency()
func (f *schedField) urgency() float64 {
    // Higher urgency = lower slack, higher load
    // Formula from MAPF-HET Section V-E
    if f.slack <= 0 {
        return 1.0 // Maximum urgency
    }
    return (1.0 - f.slack) * (0.7 + 0.3*f.load)
}
```

### 2.2 Port Test Vectors

**Source**: `ek-kor2/spec/test-vectors/field_*.json`

**Target**: `runtime/testdata/field/` + tests

Example test vector (`field_decay_basic.json`):
```json
{
  "name": "field_decay_basic",
  "category": "field",
  "input": {
    "load_q15": 16384,
    "thermal_q15": 8192,
    "elapsed_ms": 100
  },
  "expected": {
    "load_q15": 15565,
    "thermal_q15": 7373
  }
}
```

Test implementation:
```go
// runtime/sched_field_test.go

func TestFieldDecay(t *testing.T) {
    vectors := loadTestVectors(t, "testdata/field/field_decay_*.json")

    for _, v := range vectors {
        t.Run(v.Name, func(t *testing.T) {
            var input struct {
                LoadQ15    int16 `json:"load_q15"`
                ThermalQ15 int16 `json:"thermal_q15"`
                ElapsedMs  int64 `json:"elapsed_ms"`
            }
            json.Unmarshal(v.Input, &input)

            var expected struct {
                LoadQ15    int16 `json:"load_q15"`
                ThermalQ15 int16 `json:"thermal_q15"`
            }
            json.Unmarshal(v.Expected, &expected)

            // Create field with Q15 input converted to float64
            f := &schedField{
                load:    q15ToFloat(input.LoadQ15),
                thermal: q15ToFloat(input.ThermalQ15),
                tstamp:  0,
            }

            // Apply decay
            f.decay(input.ElapsedMs * 1e6)

            // Compare with Q15 tolerance
            gotLoad := floatToQ15(f.load)
            gotThermal := floatToQ15(f.thermal)

            if abs(int(gotLoad-expected.LoadQ15)) > 1 {
                t.Errorf("load: got Q15=%d, want Q15=%d", gotLoad, expected.LoadQ15)
            }
            if abs(int(gotThermal-expected.ThermalQ15)) > 1 {
                t.Errorf("thermal: got Q15=%d, want Q15=%d", gotThermal, expected.ThermalQ15)
            }
        })
    }
}
```

## Phase 3: Topology Implementation (Option A)

### 3.1 k-Neighbor Selection

**Source**:
- `ek-kor2/c/src/ekk_topology.c`
- `ek-kor2/rust/src/topology.rs`

**Target**: `runtime/sched_topology.go`

```go
// runtime/sched_topology.go

package runtime

// topologyK is the number of topological neighbors (from starling research)
const topologyK = 7

// schedTopology manages k-neighbor relationships for Ps
type schedTopology struct {
    neighbors [topologyK]*p // Fixed-size neighbor array
    distances [topologyK]int32 // Logical distances (for selection)
}

// selectNeighbors chooses k topological neighbors for a P
// Algorithm from: ek-kor2/c/src/ekk_topology.c:ekk_topology_select()
func (t *schedTopology) selectNeighbors(self *p, allPs []*p) {
    // Clear existing
    for i := range t.neighbors {
        t.neighbors[i] = nil
        t.distances[i] = 0x7FFFFFFF
    }

    // Priority 1: Same NUMA node
    // Priority 2: Shared L3 cache
    // Priority 3: Any other P

    for _, other := range allPs {
        if other == self || other == nil {
            continue
        }

        dist := t.logicalDistance(self, other)

        // Insert into neighbor list if closer than worst
        t.insertIfCloser(other, dist)
    }
}

// logicalDistance computes logical distance between Ps
// Lower = closer/better for neighbor selection
func (t *schedTopology) logicalDistance(a, b *p) int32 {
    dist := int32(0)

    // NUMA penalty: +1000 per NUMA hop
    if a.numaNode != b.numaNode {
        dist += 1000
    }

    // Cache group penalty: +100 if different L3
    if a.cacheGroup != b.cacheGroup {
        dist += 100
    }

    // ID distance: +1 per ID difference (for determinism)
    idDiff := int32(a.id) - int32(b.id)
    if idDiff < 0 {
        idDiff = -idDiff
    }
    dist += idDiff

    return dist
}

// insertIfCloser adds p to neighbors if closer than current worst
func (t *schedTopology) insertIfCloser(p *p, dist int32) {
    // Find worst (highest distance) neighbor
    worstIdx := 0
    for i := 1; i < topologyK; i++ {
        if t.distances[i] > t.distances[worstIdx] {
            worstIdx = i
        }
    }

    // Replace if better
    if dist < t.distances[worstIdx] {
        t.neighbors[worstIdx] = p
        t.distances[worstIdx] = dist
    }
}
```

### 3.2 Topology Test Vectors

**Source**: `ek-kor2/spec/test-vectors/topology_*.json`

```go
// runtime/sched_topology_test.go

func TestTopologySelection(t *testing.T) {
    vectors := loadTestVectors(t, "testdata/topology/topology_select_*.json")

    for _, v := range vectors {
        t.Run(v.Name, func(t *testing.T) {
            // Parse input: list of P configurations
            // Parse expected: which Ps should be selected as neighbors
            // Run selection, verify results
        })
    }
}
```

## Phase 4: CBS Conflict Resolution (Option F)

### 4.1 Reference vs Implementation

The CBS algorithm is complex. Strategy:
- **Keep `mapf-het-research/internal/algo/cbs.go` as reference**
- **Port simplified version** for scheduler conflicts
- **Validate against reference** using shared test cases

**Source**: `mapf-het-research/internal/algo/cbs.go` (850 lines)

**Target**: `runtime/sched_cbs.go` (simplified, ~200 lines)

```go
// runtime/sched_cbs.go

package runtime

// Simplified CBS for scheduler conflict resolution
// Full algorithm in: available-code-no-changes/mapf-het-research/internal/algo/cbs.go

// conflictNode represents a node in the CBS search tree
type conflictNode struct {
    constraints []constraint
    cost        int64
    parent      *conflictNode
}

// constraint represents a scheduling constraint
type constraint struct {
    g        *g           // Affected goroutine
    resource interface{} // Contested resource
    time     int64       // Time of constraint
}

// detectConflict finds the first conflict in current schedule
// Simplified from: cbs.go:findFirstConflict()
func detectConflict(gs []*g) *conflict {
    // Check for mutex conflicts
    // Check for channel conflicts
    // Return first found or nil
    return nil
}

// resolveConflict generates child nodes for conflict resolution
// Simplified from: cbs.go:resolveConflict()
func resolveConflict(node *conflictNode, c *conflict) []*conflictNode {
    // Create constraint for g1
    // Create constraint for g2
    // Return both as children
    return nil
}
```

## Phase 5: Consensus/Supervision (Option B)

### 5.1 Heartbeat Detection

**Source**: `ek-kor2/c/src/ekk_heartbeat.c`

**Target**: `runtime/supervised_heartbeat.go`

```go
// runtime/supervised_heartbeat.go

package runtime

// heartbeatConfig from EK-KOR2 spec
const (
    heartbeatInterval   = 100 * 1e6  // 100ms in ns
    heartbeatMissed     = 3          // Failures after 3 missed
    heartbeatSuspectMs  = 300        // Suspect after 300ms
)

type heartbeatTracker struct {
    lastSeen    int64
    missedCount int32
    state       heartbeatState
}

type heartbeatState int32

const (
    heartbeatHealthy heartbeatState = iota
    heartbeatSuspect
    heartbeatFailed
)

// update processes a received heartbeat
// Matches: ekk_heartbeat.c:ekk_heartbeat_received()
func (h *heartbeatTracker) update(now int64) {
    h.lastSeen = now
    h.missedCount = 0
    h.state = heartbeatHealthy
}

// check evaluates heartbeat status
// Matches: ekk_heartbeat.c:ekk_heartbeat_check()
func (h *heartbeatTracker) check(now int64) heartbeatState {
    elapsed := now - h.lastSeen

    if elapsed > heartbeatSuspectMs*1e6 {
        h.missedCount++
        if h.missedCount >= heartbeatMissed {
            h.state = heartbeatFailed
        } else {
            h.state = heartbeatSuspect
        }
    }

    return h.state
}
```

## Test Vector Migration Plan

### Step 1: Copy JSON Files

```bash
mkdir -p runtime/testdata/{field,topology,consensus,heartbeat}

cp available-code-no-changes/ek-kor2/spec/test-vectors/field_*.json \
   runtime/testdata/field/

cp available-code-no-changes/ek-kor2/spec/test-vectors/topology_*.json \
   runtime/testdata/topology/

cp available-code-no-changes/ek-kor2/spec/test-vectors/consensus_*.json \
   runtime/testdata/consensus/

cp available-code-no-changes/ek-kor2/spec/test-vectors/heartbeat_*.json \
   runtime/testdata/heartbeat/
```

### Step 2: Create Test Loader

```go
// runtime/testdata_test.go

package runtime_test

// Generic test vector loader for all categories
func TestAllVectors(t *testing.T) {
    categories := []struct {
        name    string
        pattern string
        runner  func(*testing.T, TestVector)
    }{
        {"field", "testdata/field/*.json", runFieldTest},
        {"topology", "testdata/topology/*.json", runTopologyTest},
        {"consensus", "testdata/consensus/*.json", runConsensusTest},
        {"heartbeat", "testdata/heartbeat/*.json", runHeartbeatTest},
    }

    for _, cat := range categories {
        t.Run(cat.name, func(t *testing.T) {
            vectors := loadTestVectors(t, cat.pattern)
            for _, v := range vectors {
                t.Run(v.Name, func(t *testing.T) {
                    cat.runner(t, v)
                })
            }
        })
    }
}
```

## Validation Against Reference

### Running Reference Implementation

Keep the original code runnable for comparison:

```bash
# Run EK-KOR2 Rust reference
cd available-code-no-changes/ek-kor2/rust
cargo test

# Run MAPF-HET Go reference
cd available-code-no-changes/mapf-het-research
go test ./internal/algo/...

# Compare outputs
cd available-code-no-changes/ek-kor2
python tools/compare_outputs.py \
    --reference rust/target/debug/test-output.json \
    --implementation ../../runtime/testdata/output.json
```

### Continuous Validation

```go
// runtime/reference_test.go

//go:build reference

package runtime_test

import (
    "os/exec"
    "testing"
)

func TestAgainstReference(t *testing.T) {
    if testing.Short() {
        t.Skip("skipping reference test in short mode")
    }

    // Run reference implementation
    cmd := exec.Command("cargo", "test", "--", "--test-threads=1")
    cmd.Dir = "available-code-no-changes/ek-kor2/rust"
    output, err := cmd.CombinedOutput()
    if err != nil {
        t.Fatalf("reference failed: %v\n%s", err, output)
    }

    // Compare results
    // ...
}
```

## Summary: File Mapping

| Source | Target | Option | Priority |
|--------|--------|--------|----------|
| `ek-kor2/c/src/ekk_field.c` | `runtime/sched_field.go` | A, G, E | P0 |
| `mapf-het/internal/algo/potential_field.go` | `runtime/sched_field.go` | A, G, E | P0 |
| `ek-kor2/c/src/ekk_topology.c` | `runtime/sched_topology.go` | A | P0 |
| `ek-kor2/spec/test-vectors/*.json` | `runtime/testdata/` | All | P0 |
| `mapf-het/internal/algo/cbs.go` | Reference (external) | F | P1 |
| `ek-kor2/c/src/ekk_consensus.c` | `runtime/supervised/` | B | P1 |
| `ek-kor2/c/src/ekk_heartbeat.c` | `runtime/supervised/` | B | P1 |
| `mapf-het/ek-roj/` | Reference (external) | C | P2 |

## Next Steps

1. **Create `runtime/testdata/`** directory structure
2. **Copy test vectors** from ek-kor2
3. **Implement `runtime/fixedpoint.go`** for Q15/Q31 conversion
4. **Port `sched_field.go`** with test validation
5. **Port `sched_topology.go`** with test validation
6. **Benchmark** against existing scheduler

# Option H: Dimensional Goroutine Classification

## Summary

Classify goroutines by their "dimensionality" (type of work) and apply
specialized scheduling strategies, inspired by MD-MAPF's dimensional conflict
taxonomy where agents operating in different spaces (1D rail, 2D floor, 3D air)
have fundamentally different scheduling needs.


## Status (Research Sketch)

- Unverified ideas only; all numbers are hypotheses.
- Cross-option dependencies are intentional and noted below.
- Any public API should live under golang.org/x/exp (not the standard library).

## Inspiration

From MAPF-HET paper Section III-A (MD-MAPF):

> "The dimensionality function κ(a) → {1,2,3} determines the operational space
> of each agent... This formalizes that rail robots access only rail vertices,
> mobile robots access floor vertices, and drones access airspace vertices."

The insight: **agents with different operational characteristics benefit from
specialized handling**, not one-size-fits-all scheduling.


## Dependencies

- None (standalone).
- Used by Option J for correlation visualization.

## Current Go Scheduler

Go's scheduler treats all goroutines uniformly:

```go
// runtime/runtime2.go (simplified)
type g struct {
    // No classification of goroutine type
    // All scheduled identically
}
```

**Limitations**:
- CPU-bound goroutines get same treatment as I/O-bound
- Network goroutines don't benefit from batching
- Sync-heavy goroutines cause lock convoys
- No workload-aware optimization

## Proposed Design

### Goroutine Dimensional Classification

```go
// runtime/runtime2.go addition

type goroutineDim uint8

const (
    // Primary dimensions (mutually exclusive)
    gDimUnknown goroutineDim = iota
    gDimCompute              // CPU-bound computation
    gDimIO                   // File/disk I/O
    gDimNetwork              // Network operations
    gDimSync                 // Synchronization-heavy
    gDimGC                   // GC-related work

    // Modifier flags (combinable)
    gDimFlagShort   = 0x10  // Short-lived
    gDimFlagLong    = 0x20  // Long-running
    gDimFlagLatency = 0x40  // Latency-sensitive
    gDimFlagBatch   = 0x80  // Batchable
)

type g struct {
    // ... existing fields ...

    dim       goroutineDim  // Current classification
    dimConf   uint8         // Confidence (0-100)
    dimHist   [8]uint8      // Recent dimension history
    dimSample uint32        // Samples for classification
}
```

### Automatic Classification

```go
// runtime/dim_classify.go

// Classifier tracks goroutine behavior to determine dimension
type dimClassifier struct {
    syscallCount   uint32  // System calls made
    networkOps     uint32  // Network operations
    fileOps        uint32  // File operations
    lockAcquires   uint32  // Lock acquisitions
    channelOps     uint32  // Channel operations
    computeCycles  uint64  // CPU cycles without blocking
}

func (gp *g) updateClassification() {
    if gp.dimSample < minSamples {
        return
    }

    // Calculate ratios
    total := float32(gp.dimSample)
    syscallRatio := float32(gp.classifier.syscallCount) / total
    networkRatio := float32(gp.classifier.networkOps) / total
    fileRatio := float32(gp.classifier.fileOps) / total
    syncRatio := float32(gp.classifier.lockAcquires+gp.classifier.channelOps) / total

    // Classify based on dominant behavior
    switch {
    case networkRatio > 0.5:
        gp.dim = gDimNetwork
        gp.dimConf = uint8(networkRatio * 100)
    case fileRatio > 0.5:
        gp.dim = gDimIO
        gp.dimConf = uint8(fileRatio * 100)
    case syncRatio > 0.3:
        gp.dim = gDimSync
        gp.dimConf = uint8(syncRatio * 100)
    case syscallRatio < 0.1:
        gp.dim = gDimCompute
        gp.dimConf = uint8((1 - syscallRatio) * 100)
    default:
        gp.dim = gDimUnknown
        gp.dimConf = 0
    }

    // Add modifier flags
    if gp.runTime < shortThreshold {
        gp.dim |= gDimFlagShort
    }
    if gp.runTime > longThreshold {
        gp.dim |= gDimFlagLong
    }
}
```

### Dimension-Specific Scheduling

```go
// runtime/proc_dim.go

// Scheduling strategy per dimension
func (pp *p) scheduleByDimension(gp *g) scheduleDecision {
    baseDim := gp.dim & 0x0F  // Mask off flags

    switch baseDim {
    case gDimCompute:
        return pp.scheduleCompute(gp)
    case gDimIO:
        return pp.scheduleIO(gp)
    case gDimNetwork:
        return pp.scheduleNetwork(gp)
    case gDimSync:
        return pp.scheduleSync(gp)
    default:
        return pp.scheduleDefault(gp)
    }
}

// Compute-bound: longer time slices, core affinity
func (pp *p) scheduleCompute(gp *g) scheduleDecision {
    return scheduleDecision{
        timeSlice:    20 * time.Millisecond,  // Longer slice
        coreAffinity: pp.id,                   // Keep on same core
        preemptible:  true,                    // But still preemptible
    }
}

// I/O-bound: quick preemption, yield on block
func (pp *p) scheduleIO(gp *g) scheduleDecision {
    return scheduleDecision{
        timeSlice:   2 * time.Millisecond,   // Short slice
        parkQuickly: true,                    // Park fast on I/O
        preemptible: true,
    }
}

// Network-bound: batching, cooperative with netpoller
func (pp *p) scheduleNetwork(gp *g) scheduleDecision {
    // Find other network goroutines to batch
    batch := pp.findNetworkBatch(gp)

    return scheduleDecision{
        timeSlice:   5 * time.Millisecond,
        batchWith:   batch,
        netpollHint: true,
    }
}

// Sync-heavy: priority inheritance, quick context switch
func (pp *p) scheduleSync(gp *g) scheduleDecision {
    return scheduleDecision{
        timeSlice:         3 * time.Millisecond,
        priorityInherit:   true,
        quickContextSwitch: true,
    }
}
```

### Cross-Dimensional Conflict Resolution

```go
// runtime/dim_conflict.go

// Conflict matrix: how to resolve conflicts between dimensions
var dimConflictStrategy = [5][5]conflictStrategy{
    //                 Unknown  Compute  IO      Network  Sync
    /* Unknown */    {deflt,   yield,   yield,  yield,   yield},
    /* Compute */    {cont,    split,   cont,   cont,    yield},
    /* IO */         {cont,    yield,   batch,  cont,    yield},
    /* Network */    {cont,    yield,   cont,   batch,   yield},
    /* Sync */       {cont,    cont,    cont,   cont,    prio},
}

type conflictStrategy uint8
const (
    deflt conflictStrategy = iota  // Default handling
    cont                           // Continue current
    yield                          // Yield to other
    split                          // Split time evenly
    batch                          // Batch together
    prio                           // Priority inheritance
)

func resolveByDimension(g1, g2 *g) conflictStrategy {
    d1 := g1.dim & 0x0F
    d2 := g2.dim & 0x0F
    return dimConflictStrategy[d1][d2]
}
```

### User Hints API

```go
// golang.org/x/exp/sched (experimental user hint API)

// Hint allows users to declare goroutine characteristics
type GoroutineHint uint8

const (
    HintCompute GoroutineHint = iota
    HintIO
    HintNetwork
    HintSync
    HintLatencySensitive
    HintBatchable
)

// SetGoroutineHint sets classification for current goroutine
func SetGoroutineHint(hint GoroutineHint) {
    gp := getg()
    gp.dim = goroutineDim(hint)
    gp.dimConf = 100  // User-provided = high confidence
}

// Usage:
// import "golang.org/x/exp/sched"
// sched.SetGoroutineHint(sched.HintNetwork)
// go handleConnection(conn)
```

## Files to Modify

| File | Changes |
|------|---------|
| `runtime/runtime2.go` | Add `dim`, `dimConf`, `dimHist`, classifier to `g` |
| `runtime/dim_classify.go` | New: automatic classification |
| `runtime/proc_dim.go` | New: dimension-specific scheduling |
| `runtime/dim_conflict.go` | New: cross-dimensional conflict resolution |
| `runtime/dim_hint.go` | New: user hint API |
| `runtime/proc.go` | Integrate dimensional scheduling |
| `runtime/sema.go` | Track lock operations for classification |
| `runtime/chan.go` | Track channel operations for classification |
| `runtime/netpoll.go` | Track network operations for classification |

## GOEXPERIMENT Flag

```go
// internal/goexperiment/flags.go
var DimensionalScheduler = false  // GOEXPERIMENT=dimscheduler
```

## New API (golang.org/x/exp/sched)

```
package sched
func SetGoroutineHint(GoroutineHint)
type GoroutineHint uint8
const (
    HintCompute GoroutineHint
    HintIO GoroutineHint
    HintNetwork GoroutineHint
    HintSync GoroutineHint
    HintLatencySensitive GoroutineHint
    HintBatchable GoroutineHint
)
```

## Metrics

```
/sched/dim/goroutines-by-class:gauge       # Goroutines per dimension
/sched/dim/classifications:counter         # Classification events
/sched/dim/reclassifications:counter       # Dimension changes
/sched/dim/hint-overrides:counter          # User hints applied
/sched/dim/conflict-resolutions:histogram  # Resolution by strategy
```

## Testing Strategy

### Unit Tests
```go
func TestComputeClassification(t *testing.T) {
    done := make(chan bool)

    go func() {
        // Pure computation
        sum := 0
        for i := 0; i < 1000000; i++ {
            sum += i
        }
        done <- true
    }()

    <-done

    // Verify classified as compute
    // (implementation would expose test hook)
}

func TestNetworkClassification(t *testing.T) {
    // Make network calls
    // Verify classified as network
}
```

### Benchmarks
```go
func BenchmarkMixedWorkload(b *testing.B) {
    // Mix of compute, I/O, network goroutines
    // Measure throughput with/without dimensional scheduling
}
```

## Expected Benefits (Hypotheses)

All values below are hypotheses and **not verified**.

| Workload | Current | With Dimensional | Improvement |
|----------|---------|------------------|-------------|
| Compute + I/O mixed | I/O starved | Both progress | 30% throughput |
| Network server | Random scheduling | Batched network | 20% latency |
| Lock-heavy | Convoys | Priority inheritance | 40% latency |
| Mixed heterogeneous | Uniform treatment | Specialized | 25% overall |

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Misclassification | Confidence threshold, reclassification |
| Gaming the system | Hints are advisory, not mandatory |
| Overhead of tracking | Sample-based, not every operation |
| Complexity | Fall back to default for unknown |

## Comparison with MD-MAPF

| MD-MAPF | Go Dimensional |
|---------|----------------|
| κ(a) → {1,2,3} | dim → {Compute,IO,Network,Sync} |
| Vertex compatibility δ(v) | Resource affinity |
| 6 conflict classes | 5×5 conflict matrix |
| 5.6× speedup | Target: 25% improvement |

## Future Extensions

1. **Learned classification**: ML model predicts dimension from early behavior
2. **Dynamic reclassification**: Goroutines can change dimension mid-execution
3. **Hierarchical dimensions**: Sub-categories within each dimension
4. **Cross-machine awareness**: Distributed dimensional coordination (Option C)

## References

- MAPF-HET paper Section III: Problem Formulation
- MD-MAPF dimensionality function κ(a)
- Linux CFS scheduler classes (SCHED_FIFO, SCHED_RR, SCHED_OTHER)

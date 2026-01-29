# Option A: Stigmergic Scheduler

## Summary

Replace or augment Go's work-stealing scheduler with stigmergy-based load
balancing. Each P maintains "heat tags" representing utilization metrics.
Goroutines flow toward cooler Ps through emergent behavior without explicit
centralized coordination.


## Status (Research Sketch)

- Unverified ideas only; all numbers are hypotheses.
- Cross-option dependencies are intentional and noted below.
- Any public API should live under golang.org/x/exp (not the standard library).

## Inspiration

From ROJ paper Section VII (Emergent Load Balancing):

> "Each module maintains a local 'heat tag' that decays exponentially...
> Cooler modules (lower tags) increase their power share; hotter modules
> decrease. This creates emergent thermal migration without centralized
> coordination."

The paper demonstrates 88% reduction in temperature variance vs 14% for
traditional approaches.


## Dependencies

- None (standalone).
- Optional: Option J for observability/metrics.
- Optional: Option C can consume heat tags for cluster scheduling.

## Current Go Scheduler

Go's current scheduler uses work stealing (runtime/proc.go):

1. Each P has a local run queue
2. When a P's queue is empty, it steals from random other Ps
3. Global run queue provides overflow
4. Randomized stealing prevents convoy effects

**Limitations**:
- Random stealing ignores NUMA topology
- No awareness of thermal/power states
- Cache locality not considered in steal decisions

## Proposed Design

### New P Fields

```go
// runtime/runtime2.go
type p struct {
    // ... existing fields ...

    // Stigmergic load balancing
    heatTag     uint64    // Decaying utilization metric (fixed-point)
    neighbors   [7]*p     // Topological neighbors (k=7 from starling research)
    lastUpdate  int64     // Timestamp of last heat tag update

    // NUMA awareness
    numaNode    int32     // NUMA node this P is bound to
    cacheGroup  int32     // Shared L3 cache group
}
```

### Heat Tag Update Algorithm

```go
// runtime/proc.go
const (
    heatDecayShift = 5        // Decay factor: 31/32 per update
    heatUpdateHz   = 1000     // Update frequency
    heatScaleFactor = 1000    // Fixed-point scaling
)

// Called periodically by sysmon or per-P timer
func (pp *p) updateHeatTag() {
    now := nanotime()
    elapsed := now - pp.lastUpdate
    if elapsed < 1e9/heatUpdateHz {
        return
    }
    pp.lastUpdate = now

    // Exponential decay
    pp.heatTag = pp.heatTag - (pp.heatTag >> heatDecayShift)

    // Add current load contribution
    runqLen := uint64(pp.runqsize())
    schedLatency := uint64(pp.schedtick - pp.lastSchedtick) // proxy for busyness

    contribution := (runqLen*100 + schedLatency) * heatScaleFactor
    pp.heatTag += contribution

    // Optional: factor in CPU temperature if available
    // pp.heatTag += readCPUTemp(pp.numaNode) * tempWeight
}
```

### Stigmergic Work Stealing

```go
// runtime/proc.go
func (pp *p) findStealTarget() *p {
    // First: check topological neighbors (cache-aware)
    var hottest *p
    var hottestTag uint64

    for _, neighbor := range pp.neighbors {
        if neighbor == nil {
            continue
        }
        tag := atomic.Load64(&neighbor.heatTag)
        if tag > hottestTag && neighbor.runqsize() > 0 {
            hottest = neighbor
            hottestTag = tag
        }
    }

    // If neighbor is significantly hotter, steal from them
    myTag := atomic.Load64(&pp.heatTag)
    if hottest != nil && hottestTag > myTag*12/10 { // 20% hotter threshold
        return hottest
    }

    // Fallback to random stealing (existing behavior)
    return pp.randomStealTarget()
}
```

### Neighbor Topology Initialization

```go
// runtime/proc.go
func initPNeighbors() {
    // Build neighbor graph based on:
    // 1. Same NUMA node (highest priority)
    // 2. Shared L3 cache
    // 3. Physical proximity

    allp := allp[:gomaxprocs]

    for i, pp := range allp {
        neighbors := make([]*p, 0, 7)

        // Same NUMA node first
        for _, other := range allp {
            if other != pp && other.numaNode == pp.numaNode {
                neighbors = append(neighbors, other)
                if len(neighbors) >= 4 {
                    break
                }
            }
        }

        // Fill remaining with other nodes
        for _, other := range allp {
            if other != pp && other.numaNode != pp.numaNode {
                neighbors = append(neighbors, other)
                if len(neighbors) >= 7 {
                    break
                }
            }
        }

        copy(pp.neighbors[:], neighbors)
    }
}
```

## Files to Modify

| File | Changes |
|------|---------|
| `runtime/runtime2.go` | Add `heatTag`, `neighbors`, fields to `p` struct |
| `runtime/proc.go` | `updateHeatTag()`, `findStealTarget()`, `initPNeighbors()` |
| `runtime/os_*.go` | NUMA node detection per platform |
| `runtime/sysmon.go` | Periodic heat tag updates |
| `runtime/debug.go` | Debug/trace output for heat tags |
| `runtime/metrics.go` | Export heat metrics |

## GOEXPERIMENT Flag

```go
// internal/goexperiment/flags.go
var Stigmergy = false  // GOEXPERIMENT=stigmergy
```

Enabled via:
```bash
GOEXPERIMENT=stigmergy go build
```

## Metrics & Observability

New runtime/metrics:

```
/sched/stigmergy/heat-variance:gauge     # Variance across P heat tags
/sched/stigmergy/neighbor-steals:counter # Steals from neighbors vs random
/sched/stigmergy/thermal-migrations:counter # Goroutines moved for thermal
```

## Testing Strategy

### Unit Tests
- `runtime/stigmergy_test.go` - Heat tag decay, neighbor selection

### Benchmarks
```bash
# Compare schedulers
GOEXPERIMENT=stigmergy go test -bench=. runtime
go test -bench=. runtime

# NUMA-specific benchmarks
numactl --cpunodebind=0-1 go test -bench=BenchmarkSchedNUMA
```

### Stress Tests
- `x/benchmarks` suite with high GOMAXPROCS
- Synthetic hot/cold workload patterns
- NUMA memory access patterns

### Hardware Targets
- AMD EPYC (8 NUMA nodes)
- Intel Xeon Scalable (multi-socket)
- Apple M1/M2 (performance/efficiency cores)

## Expected Benefits (Hypotheses)

All values below are hypotheses and **not verified**.

| Metric | Current | Expected | Improvement |
|--------|---------|----------|-------------|
| Cross-NUMA steals | ~50% | ~15% | 70% reduction |
| Cache miss rate | baseline | -10-20% | Better locality |
| Thermal variance | baseline | -30-50% | More even load |
| Tail latency (p99) | baseline | -5-15% | Fewer cold steals |

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Overhead of heat tag updates | Update only every 1ms, use atomics |
| Neighbor graph staleness | Rebuild on GOMAXPROCS change |
| Worse performance on small GOMAXPROCS | Fall back to random at GOMAXPROCS < 8 |
| Platform-specific NUMA detection | Graceful fallback to non-NUMA mode |

## Alternatives Considered

1. **Pure NUMA binding**: Too rigid, doesn't adapt to dynamic loads
2. **Centralized load balancer**: Adds contention, against Go philosophy
3. **Machine learning scheduler**: Too complex, unpredictable latency

## References

- ROJ Paper Section VII: Emergent Load Balancing
- Starling flock research (k=7 neighbors): Ballerini et al., PNAS 2008
- Go scheduler design: golang.org/s/go11sched
- NUMA-aware scheduling: golang/go#18802

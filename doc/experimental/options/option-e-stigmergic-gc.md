# Option E: Stigmergic Garbage Collector

## Summary

Apply stigmergy (indirect coordination through environmental signals) to Go's
garbage collector. Instead of explicit stop-the-world coordination, each P
independently decides when to do GC work based on local "memory pressure tags"
that propagate through the system, creating emergent GC behavior.

## Inspiration

From ROJ paper Section VII (Emergent Load Balancing):

> "Beyond droop, ROJ implements thermal-aware load balancing using stigmergy—
> a form of indirect coordination through environmental signals. Each module
> maintains a local 'heat tag' that decays exponentially."

The paper shows 88% reduction in temperature variance through emergent behavior.
Applied to GC, this could mean smoother memory management without coordinated
pauses.

## Current Go GC

Go's GC (runtime/mgc.go) uses:

1. **Pacer**: Decides when to start GC based on heap growth
2. **Concurrent marking**: Background goroutines mark live objects
3. **Write barrier**: Track pointer writes during marking
4. **STW phases**: Brief stop-the-world for mark start/end
5. **Assist**: Goroutines help with GC when allocating

**Coordination points**:
- `gcStart()`: Global decision to begin GC
- `markDone()`: Wait for all Ps to finish marking
- `gcMarkTermination`: STW to finalize marking

## Proposed Design

### Stigmergic Pressure Tags

```go
// runtime/mgc_stigmergy.go

type stigmergicGC struct {
    // Per-P state
    pressureTag  float64   // Local memory pressure (0.0 - 1.0)
    lastUpdate   int64     // Timestamp
    gcWork       uint64    // GC work done this cycle

    // Neighbor communication
    neighbors    [7]*p
    neighborTags [7]float64
}

const (
    pressureDecay     = 0.95  // Decay factor per update
    pressureUpdateHz  = 100   // Update frequency
    pressureThreshold = 0.7   // Start assisting above this
    pressureCritical  = 0.9   // Force GC above this
)
```

### Pressure Tag Calculation

```go
// runtime/mgc_stigmergy.go

func (pp *p) updatePressureTag() {
    // Decay existing pressure
    pp.stigmergy.pressureTag *= pressureDecay

    // Calculate local pressure contribution
    heapLive := atomic.Load64(&memstats.heap_live)
    heapGoal := atomic.Load64(&memstats.gc_goal)

    localPressure := float64(heapLive) / float64(heapGoal)

    // Factor in allocation rate
    allocRate := pp.mcache.allocRate()  // bytes/sec
    allocPressure := allocRate / targetAllocRate

    // Combined local pressure
    contribution := (localPressure*0.7 + allocPressure*0.3) * 0.05
    pp.stigmergy.pressureTag += contribution

    // Clamp to [0, 1]
    if pp.stigmergy.pressureTag > 1.0 {
        pp.stigmergy.pressureTag = 1.0
    }
}

func (pp *p) broadcastPressureTag() {
    tag := pp.stigmergy.pressureTag

    for i, neighbor := range pp.stigmergy.neighbors {
        if neighbor == nil {
            continue
        }
        // Non-blocking send of our pressure
        atomic.StoreFloat64(&neighbor.stigmergy.neighborTags[pp.neighborIndex], tag)
    }
}

func (pp *p) aggregatePressure() float64 {
    // Local pressure
    total := pp.stigmergy.pressureTag
    count := 1.0

    // Neighbor pressure (weighted less)
    for _, neighborTag := range pp.stigmergy.neighborTags {
        if neighborTag > 0 {
            total += neighborTag * 0.3  // Neighbors count less
            count += 0.3
        }
    }

    return total / count
}
```

### Emergent GC Decisions

```go
// runtime/mgc_stigmergy.go

// Called periodically by each P (not globally coordinated)
func (pp *p) stigmergicGCCheck() {
    aggregatePressure := pp.aggregatePressure()

    if aggregatePressure > pressureCritical {
        // Critical: Force local GC work
        pp.doGCAssist(forcedAssistAmount)

        // Propagate urgency to neighbors
        pp.stigmergy.pressureTag = 1.0
        pp.broadcastPressureTag()

    } else if aggregatePressure > pressureThreshold {
        // High pressure: Proportional assist
        assistAmount := (aggregatePressure - pressureThreshold) /
                       (pressureCritical - pressureThreshold)
        pp.doGCAssist(uint64(assistAmount * maxAssistAmount))

    } else {
        // Low pressure: Minimal or no GC work
        // Let allocation proceed unimpeded
    }
}

func (pp *p) doGCAssist(amount uint64) {
    if !gcMarkWorkAvailable() {
        return
    }

    // Do marking work locally
    // This doesn't require global coordination
    gcDrain(&pp.gcw, amount)

    // Track work for pressure decay
    pp.stigmergy.gcWork += amount
}
```

### Emergent GC Phases

Replace explicit phase transitions with emergent behavior:

```go
// runtime/mgc_stigmergy.go

type gcPhaseEmergent int

const (
    gcPhaseIdle gcPhaseEmergent = iota
    gcPhaseEmergentMark    // Enough Ps doing GC work
    gcPhaseEmergentSweep   // Mark complete, sweeping
)

// Phase emerges from collective behavior
func determineEmergentPhase() gcPhaseEmergent {
    // Count Ps actively doing GC work
    activeGC := 0
    totalP := 0

    for _, pp := range allp {
        if pp == nil {
            continue
        }
        totalP++

        if pp.stigmergy.pressureTag > pressureThreshold {
            activeGC++
        }
    }

    // Phase emerges from majority behavior
    ratio := float64(activeGC) / float64(totalP)

    if ratio > 0.5 {
        return gcPhaseEmergentMark
    } else if markComplete() {
        return gcPhaseEmergentSweep
    }
    return gcPhaseIdle
}
```

### Reduced STW

The goal is to minimize or eliminate STW phases:

```go
// runtime/mgc_stigmergy.go

// Traditional GC has these STW points:
// 1. Mark start: Enable write barrier (currently STW)
// 2. Mark termination: Finalize mark (currently STW)

// Stigmergic approach:
// 1. Write barrier always enabled when any P has high pressure
// 2. Mark termination via quiescent detection (no explicit coordination)

func (pp *p) shouldEnableWriteBarrier() bool {
    // Enable write barrier if ANY neighbor has high pressure
    // This is conservative but avoids coordination
    for _, tag := range pp.stigmergy.neighborTags {
        if tag > pressureThreshold {
            return true
        }
    }
    return pp.stigmergy.pressureTag > pressureThreshold
}

// Quiescent detection: mark is complete when no P has pending work
func isMarkQuiescent() bool {
    for _, pp := range allp {
        if pp == nil {
            continue
        }
        if !pp.gcw.empty() {
            return false
        }
        if pp.stigmergy.pressureTag > pressureThreshold {
            return false
        }
    }
    return true
}
```

## Integration with Current GC

This can be implemented incrementally:

### Phase 1: Stigmergic Assist (Low Risk)

Keep current GC structure but add stigmergic assist decisions:

```go
// runtime/mgc.go modification

func mallocgc(...) {
    // ... existing allocation code ...

    // Replace fixed assist check with stigmergic
    if GOEXPERIMENT.stigmergyGC {
        if gp.m.p.ptr().shouldStigmergicAssist() {
            // Do proportional assist based on pressure
            gcAssistAlloc1(gp, assistWorkPerByte)
        }
    } else {
        // Existing assist logic
    }
}
```

### Phase 2: Pressure-Based Pacing

Replace pacer's global heap trigger with distributed pressure:

```go
// runtime/mgcpacer.go modification

func (c *gcControllerState) shouldTriggerGC() bool {
    if GOEXPERIMENT.stigmergyGC {
        // Trigger when aggregate pressure exceeds threshold
        totalPressure := 0.0
        count := 0
        for _, pp := range allp {
            if pp != nil {
                totalPressure += pp.stigmergy.pressureTag
                count++
            }
        }
        return (totalPressure / float64(count)) > pressureThreshold
    }

    // Existing trigger logic
    return c.heapLive >= c.trigger
}
```

### Phase 3: Reduced STW (High Risk)

Experimental: try to eliminate STW phases entirely.

## Files to Modify

| File | Changes |
|------|---------|
| `runtime/runtime2.go` | Add `stigmergy` field to `p` struct |
| `runtime/mgc.go` | Hook stigmergic checks into GC |
| `runtime/mgcpacer.go` | Pressure-based trigger option |
| `runtime/malloc.go` | Stigmergic assist decisions |
| `runtime/mgc_stigmergy.go` | New file: stigmergic GC implementation |
| `runtime/mbarrier.go` | Dynamic write barrier |

## GOEXPERIMENT Flag

```go
// internal/goexperiment/flags.go
var StigmergyGC = false  // GOEXPERIMENT=stigmergygc
```

## Metrics

New runtime/metrics:

```
/gc/stigmergy/pressure-mean:gauge        # Mean pressure across Ps
/gc/stigmergy/pressure-variance:gauge    # Pressure variance
/gc/stigmergy/emergent-assists:counter   # Assists triggered by pressure
/gc/stigmergy/neighbor-propagations:counter # Pressure tag broadcasts
```

## Testing Strategy

### Correctness
- Memory corruption tests (verify no objects collected early)
- Finalizer ordering tests
- Race detector integration

### Performance
```bash
# Compare GC behavior
GOEXPERIMENT=stigmergygc go test -bench=. -benchmem

# GC trace comparison
GODEBUG=gctrace=1 go run benchmark.go
GODEBUG=gctrace=1 GOEXPERIMENT=stigmergygc go run benchmark.go
```

### Stress Tests
- High allocation rate
- Many goroutines
- Variable allocation patterns
- Memory pressure scenarios

## Expected Benefits

| Metric | Current GC | Stigmergic GC | Improvement |
|--------|------------|---------------|-------------|
| STW pause p99 | ~1ms | ~0.1ms | 90% reduction |
| Pause variance | High | Low | Smoother |
| Throughput | baseline | -5% to +5% | Similar |
| Latency jitter | High | Low | More predictable |

## Risks & Mitigations

| Risk | Severity | Mitigation |
|------|----------|------------|
| Memory corruption | Critical | Extensive testing, gradual rollout |
| Worse throughput | Medium | Fall back to traditional GC |
| Unbounded heap growth | High | Hard limits as safety net |
| Coordination failure | High | Traditional GC as fallback |

## Comparison with Other Approaches

| Approach | STW | Throughput | Complexity |
|----------|-----|------------|------------|
| Current Go GC | ~1ms | High | Medium |
| Stigmergic GC | ~0.1ms | Medium-High | High |
| Shenandoah (Java) | <1ms | Medium | Very High |
| ZGC (Java) | <1ms | High | Very High |
| Reference counting | None | Low | Low |

## Research Questions

1. **Is quiescent detection reliable?** Can we guarantee marking completes?

2. **Write barrier overhead?** Always-on write barrier may hurt throughput.

3. **Pressure propagation speed?** How fast does pressure information spread?

4. **Adversarial workloads?** Can allocation patterns defeat stigmergy?

## Future Work

- Machine learning for pressure prediction
- Hardware-assisted pressure detection (performance counters)
- Integration with NUMA-aware allocation
- Formal verification of correctness

## References

- ROJ Paper Section VII: Emergent Load Balancing
- Go GC Pacer: golang.org/s/go15gcpacer
- Stigmergy: Bonabeau et al., "Swarm Intelligence" (1999)
- Go GC Guide: tip.golang.org/doc/gc-guide

# Option I: Cooperative Scheduling Mode

## Summary

Add an optional cooperative scheduling mode to Go, inspired by MAPF-HET's
planning-execution separation. In this mode, goroutines yield voluntarily at
safe points rather than being preempted, enabling predictable execution and
easier reasoning about concurrent behavior.


## Status (Research Sketch)

- Unverified ideas only; all numbers are hypotheses.
- Cross-option dependencies are intentional and noted below.
- Any public API should live under golang.org/x/exp (not the standard library).

## Inspiration

From MAPF-HET paper Section V-F (Planning-Execution Bridge):

> "HYBRID-CBS combines global CBS planning with local potential field execution,
> enabling real-time adaptation while maintaining optimality guarantees."

The key insight: **planned execution with local adaptation** can outperform
purely reactive preemptive scheduling for certain workloads.

Also from ROJ paper's JEZGRO microkernel:

> "Hybrid privilege: Hard real-time LLC control runs in kernel space; other
> services run isolated."


## Dependencies

- Optional: Option G for potential-field urgency signals.
- Optional: Option F/H for urgency definitions and conflict strategies.

## Current Go Scheduling Model

Go uses **preemptive scheduling** since Go 1.14:

- Goroutines can be preempted at almost any safe point
- `runtime.asyncPreempt` injects preemption signals
- Prevents infinite loops from blocking other goroutines
- But adds unpredictability and overhead

**When Preemption Hurts**:
- Real-time applications need predictable timing
- Some algorithms work better with atomic execution phases
- Preemption overhead in tight loops
- Debugging concurrent issues harder with preemption

## Proposed Design

### Cooperative Mode Flag

```go
// runtime/proc.go

type schedMode uint8

const (
    schedPreemptive schedMode = iota  // Default: preemptive
    schedCooperative                  // Voluntary yields only
    schedHybrid                       // Cooperative with timeout fallback
)

var globalSchedMode schedMode = schedPreemptive
```

### Yield Points

```go
// runtime/cooperative.go

// Yield is the voluntary yield point in cooperative mode
// In preemptive mode, this is a no-op
func Yield() {
    if globalSchedMode == schedPreemptive {
        return  // Preemption handles scheduling
    }

    gp := getg()

    // Update potential field before yielding
    if goexperiment.PotentialChannels {
        gp.updateCooperativeUrgency()
    }

    // Voluntary schedule point
    mcall(gosched_m)
}

// YieldIfNeeded yields only if beneficial
// Checks urgency of waiting goroutines
func YieldIfNeeded() {
    if globalSchedMode == schedPreemptive {
        return
    }

    gp := getg()
    pp := gp.m.p.ptr()

    // Check if someone more urgent is waiting
    if pp.runqhead != pp.runqtail {
        next := pp.runq[pp.runqhead%uint32(len(pp.runq))]
        if next.urgency > gp.urgency {
            mcall(gosched_m)
        }
    }
}
```

### Safe Point Annotations

```go
// runtime/cooperative.go

// SafePoint marks a location where preemption is allowed
// even in cooperative mode (for emergency preemption)
//
//go:nosplit
func SafePoint() {
    gp := getg()

    // Check for pending signals
    if gp.preemptStop || gp.preempt {
        mcall(preemptPark)
    }

    // In hybrid mode, check timeout
    if globalSchedMode == schedHybrid {
        if nanotime()-gp.lastYield > cooperativeTimeout {
            mcall(gosched_m)
        }
    }
}
```

### Cooperative Regions

```go
// runtime/cooperative.go

// CooperativeRegion marks a critical section where
// preemption should be avoided if possible
type CooperativeRegion struct {
    gp       *g
    startTime int64
}

func EnterCooperativeRegion() CooperativeRegion {
    gp := getg()

    // Hint to scheduler: avoid preemption
    gp.cooperativeRegion = true

    return CooperativeRegion{
        gp:        gp,
        startTime: nanotime(),
    }
}

func (cr CooperativeRegion) Exit() {
    cr.gp.cooperativeRegion = false

    // Yield if we ran too long
    if nanotime()-cr.startTime > regionTimeout {
        Yield()
    }
}

// Usage:
// region := runtime.EnterCooperativeRegion()
// defer region.Exit()
// // ... critical work ...
```

### Hybrid Mode: Planning with Adaptation

```go
// runtime/hybrid_sched.go

// HybridScheduler combines planning with reactive execution
type hybridScheduler struct {
    plan       *schedulePlan    // Current plan
    planExpiry int64            // When plan expires
    deviation  float32          // Cumulative deviation from plan
}

type schedulePlan struct {
    assignments map[*g]*p       // G -> P assignment
    ordering    [][]*g          // Per-P execution order
    validUntil  int64           // Plan validity window
}

func (hs *hybridScheduler) computePlan() *schedulePlan {
    // Collect current state
    var allGs []*g
    forEachG(func(gp *g) {
        if gp.status == _Grunnable {
            allGs = append(allGs, gp)
        }
    })

    // Simple plan: assign by urgency
    plan := &schedulePlan{
        assignments: make(map[*g]*p),
        ordering:    make([][]*g, gomaxprocs),
        validUntil:  nanotime() + planWindow,
    }

    // Sort by urgency
    sort.Slice(allGs, func(i, j int) bool {
        return allGs[i].urgency > allGs[j].urgency
    })

    // Round-robin assignment
    for i, gp := range allGs {
        p := allp[i%gomaxprocs]
        plan.assignments[gp] = p
        plan.ordering[p.id] = append(plan.ordering[p.id], gp)
    }

    return plan
}

func (hs *hybridScheduler) shouldReplan() bool {
    // Replan if:
    // 1. Plan expired
    if nanotime() > hs.planExpiry {
        return true
    }

    // 2. Too much deviation
    if hs.deviation > deviationThreshold {
        return true
    }

    // 3. New high-urgency goroutine
    // (detected via potential field)

    return false
}
```

### Integration with Potential Fields

```go
// runtime/cooperative.go

// In cooperative mode, yield decisions use potential fields
func (gp *g) shouldYieldCooperative() bool {
    if globalSchedMode == schedPreemptive {
        return false
    }

    pp := gp.m.p.ptr()

    // Check aggregate urgency of waiting goroutines
    var waitingUrgency int32
    for i := uint32(0); i < pp.runqtail-pp.runqhead; i++ {
        idx := (pp.runqhead + i) % uint32(len(pp.runq))
        waitingUrgency += pp.runq[idx].urgency
    }

    // Yield if waiting urgency significantly exceeds ours
    return waitingUrgency > gp.urgency*3
}
```

## Experimental API (golang.org/x/exp/sched)

```
// SetSchedulingMode sets the global scheduling mode
// Must be called before any goroutines start significant work
func SetSchedulingMode(mode SchedulingMode)

type SchedulingMode int
const (
    ModePreemptive  SchedulingMode = iota  // Default
    ModeCooperative                         // Voluntary yields
    ModeHybrid                              // Cooperative with timeout
)

// Yield voluntarily yields the processor
func Yield()

// YieldIfNeeded yields if higher-urgency goroutines are waiting
func YieldIfNeeded()

// EnterCooperativeRegion begins a critical section
func EnterCooperativeRegion() CooperativeRegion

// SafePoint marks a location where preemption is safe
func SafePoint()
```

## Files to Modify

| File | Changes |
|------|---------|
| `runtime/runtime2.go` | Add `cooperativeRegion`, `lastYield` to `g` |
| `runtime/cooperative.go` | New: yield points, regions, mode management |
| `runtime/hybrid_sched.go` | New: hybrid planning scheduler |
| `runtime/proc.go` | Integrate cooperative checks |
| `runtime/preempt.go` | Respect cooperative regions |
| `runtime/export_test.go` | Expose for testing |

## GOEXPERIMENT Flag

```go
// internal/goexperiment/flags.go
var CooperativeScheduler = false  // GOEXPERIMENT=cooperative
```

## New API (golang.org/x/exp/sched)

```
package sched
func SetSchedulingMode(SchedulingMode)
func Yield()
func YieldIfNeeded()
func EnterCooperativeRegion() CooperativeRegion
func SafePoint()
type SchedulingMode int
const (
    ModePreemptive SchedulingMode
    ModeCooperative SchedulingMode
    ModeHybrid SchedulingMode
)
type CooperativeRegion struct
func (CooperativeRegion) Exit()
```

## Metrics

```
/sched/cooperative/voluntary-yields:counter   # Yield() calls
/sched/cooperative/forced-preemptions:counter # Emergency preemptions
/sched/cooperative/region-entries:counter     # Cooperative regions entered
/sched/cooperative/region-timeouts:counter    # Regions that exceeded timeout
/sched/hybrid/plans-computed:counter          # Plans generated
/sched/hybrid/plan-deviations:histogram       # Deviation from plan
```

## Testing Strategy

### Unit Tests
```go
func TestCooperativeYield(t *testing.T) {
    sched.SetSchedulingMode(sched.ModeCooperative)
    defer sched.SetSchedulingMode(sched.ModePreemptive)

    order := make([]int, 0, 2)
    done := make(chan bool)

    go func() {
        order = append(order, 1)
        sched.Yield()
        order = append(order, 3)
        done <- true
    }()

    go func() {
        order = append(order, 2)
        done <- true
    }()

    <-done
    <-done

    // With cooperative scheduling, order should be [1, 2, 3]
    // (first yields after 1, second runs, first continues)
}
```

### Benchmarks
```go
func BenchmarkCooperativeVsPreemptive(b *testing.B) {
    modes := []runtime.SchedulingMode{
        sched.ModePreemptive,
        sched.ModeCooperative,
        sched.ModeHybrid,
    }

    for _, mode := range modes {
        b.Run(mode.String(), func(b *testing.B) {
            sched.SetSchedulingMode(mode)
            defer sched.SetSchedulingMode(sched.ModePreemptive)

            b.RunParallel(func(pb *testing.PB) {
                for pb.Next() {
                    // Workload
                    sched.YieldIfNeeded()
                }
            })
        })
    }
}
```

## Expected Benefits (Hypotheses)

All values below are hypotheses and **not verified**.

| Scenario | Preemptive | Cooperative | Improvement |
|----------|------------|-------------|-------------|
| Tight loop throughput | 100% | 105-110% | 5-10% |
| Real-time latency | Unpredictable | Bounded | Predictability |
| Context switch overhead | High | Minimal | 20-30% |
| Debugging | Hard | Easier | Qualitative |

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Infinite loop hangs | Hybrid mode has timeout fallback |
| User forgets to yield | Compiler warnings, lint rules |
| Starvation | Urgency-based yielding |
| Compatibility | Default remains preemptive |

## Use Cases

1. **Real-time systems**: Audio/video processing with bounded latency
2. **Game engines**: Frame-locked execution with explicit yield points
3. **Embedded Go**: Resource-constrained systems (like ROJ's STM32)
4. **Deterministic testing**: Reproducible concurrent behavior

## Comparison with Other Systems

| System | Model | Notes |
|--------|-------|-------|
| Go (current) | Preemptive | Signal-based preemption |
| Rust async | Cooperative | Explicit .await |
| Java virtual threads | Hybrid | Preemptive with yield hints |
| MAPF-HET | Hybrid | Planning + local adaptation |
| ROJ/JEZGRO | Cooperative | Real-time on MCU |

## References

- MAPF-HET paper Section V-F: Planning-Execution Bridge
- ROJ paper Section III-C: JEZGRO Microkernel
- Go 1.14 release notes: Asynchronous preemption
- Rust async/await model

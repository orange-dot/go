# Option F: Conflict-Based Search Scheduler

## Summary

Apply Conflict-Based Search (CBS) concepts from MAPF to Go's scheduler for
principled resolution of goroutine conflicts over shared resources (locks,
channels, I/O, memory).


## Status (Research Sketch)

- Unverified ideas only; all numbers are hypotheses.
- Cross-option dependencies are intentional and noted below.
- Any public API should live under golang.org/x/exp (not the standard library).

## Inspiration

From MAPF-HET paper Section IV (Algorithm Design):

> "CBS introduced two-level search: high-level conflict detection and low-level
> single-agent planning... The six-class taxonomy provides specialized
> resolution strategies that exploit dimensional structure, achieving 5.6×
> speedup over baseline."

The key insight: **typed conflicts enable specialized resolution strategies**.


## Dependencies

- Optional: Option G for channel-level urgency signals.
- Optional: Option H for dimension-aware conflict strategies.

## Current Go Scheduler Conflict Handling

Go's scheduler handles contention through:

1. **Lock contention**: Goroutines park on semaphore, FIFO wake
2. **Channel blocking**: Sudog queues with FIFO ordering
3. **Work stealing**: Random selection, no conflict awareness
4. **Preemption**: Time-based, ignores conflict state

**Limitations**:
- No awareness of conflict *type* (mutex vs channel vs I/O)
- No specialized resolution per conflict class
- Convoy effects from uniform FIFO treatment
- No global view of conflict graph

## Proposed Design

### Conflict Classification

```go
// runtime/conflict.go

type conflictClass int

const (
    // Resource conflicts (mutex-like)
    conflictMutex conflictClass = iota  // sync.Mutex, sync.RWMutex
    conflictCond                         // sync.Cond
    conflictPool                         // sync.Pool contention

    // Communication conflicts (channel-like)
    conflictChanSend                     // Blocked on channel send
    conflictChanRecv                     // Blocked on channel receive
    conflictSelect                       // Blocked in select

    // I/O conflicts
    conflictFileIO                       // File operations
    conflictNetIO                        // Network operations
    conflictSyscall                      // General syscalls

    // Memory conflicts
    conflictAlloc                        // Memory allocation contention
    conflictGC                           // GC-related blocking
)

// Conflict represents contention between goroutines
type conflict struct {
    g1, g2    *g             // Conflicting goroutines
    resource  unsafe.Pointer // The contested resource
    class     conflictClass  // Type of conflict
    time      int64          // When detected
    priority1 int32          // g1's priority score
    priority2 int32          // g2's priority score
}
```

### Two-Level Search Architecture

```go
// runtime/cbs.go

// High-level: Detect and classify conflicts
type conflictDetector struct {
    conflicts  []conflict      // Active conflicts
    history    ringBuffer      // Recent conflict history
    patterns   map[uint64]int  // Conflict frequency by resource
}

func (cd *conflictDetector) detectConflicts() []conflict {
    var conflicts []conflict

    // Scan waiting goroutines for conflicts
    forEachG(func(gp *g) {
        if gp.waitreason == waitReasonSemacquire {
            // Mutex conflict
            conflicts = append(conflicts, conflict{
                g1:    gp,
                g2:    findHolder(gp.waitlock),
                class: conflictMutex,
            })
        }
        // ... other conflict types
    })

    return conflicts
}

// Low-level: Resolve conflicts by class
func resolveByClass(c conflict) resolution {
    switch c.class {
    case conflictMutex:
        return resolveMutexConflict(c)
    case conflictChanSend, conflictChanRecv:
        return resolveChannelConflict(c)
    case conflictNetIO:
        return resolveIOConflict(c)
    default:
        return defaultResolution(c)
    }
}
```

### Class-Specific Resolution Strategies

```go
// runtime/cbs_resolve.go

// Strategy for mutex conflicts: priority inheritance
func resolveMutexConflict(c conflict) resolution {
    holder := c.g2
    waiter := c.g1

    // If waiter has higher urgency, boost holder
    if waiter.urgency > holder.urgency {
        // Temporary priority inheritance
        holder.inheritedPriority = waiter.urgency
        // Ensure holder runs to release lock
        if holder.status == _Grunnable {
            prioritize(holder)
        }
    }

    return resolution{
        action:  resBoostHolder,
        target:  holder,
        timeout: 1 * time.Millisecond,
    }
}

// Strategy for channel conflicts: potential field
func resolveChannelConflict(c conflict) resolution {
    ch := (*hchan)(c.resource)

    // Apply urgency-based ordering
    // Higher urgency = served first
    if c.g1.urgency > c.g2.urgency {
        return resolution{
            action: resPrioritizeSender,
            target: c.g1,
        }
    }

    return resolution{
        action: resDefault,
    }
}

// Strategy for I/O conflicts: batching
func resolveIOConflict(c conflict) resolution {
    // I/O conflicts benefit from batching
    // Group nearby I/O operations together
    return resolution{
        action: resBatchIO,
        batch:  findRelatedIO(c),
    }
}
```

### Urgency Calculation (Slack-Field Mapping)

```go
// runtime/urgency.go

// Urgency score based on wait time and deadline hints
func (gp *g) calculateUrgency() int32 {
    waitTime := nanotime() - gp.waitstart

    // Base urgency from wait time
    urgency := int32(waitTime / urgencyScale)

    // Boost for frequently-blocked goroutines
    if gp.blockCount > blockThreshold {
        urgency += blockBoost
    }

    // User-provided deadline hints (future API)
    if gp.deadline > 0 {
        slack := gp.deadline - nanotime()
        if slack < criticalSlack {
            urgency += criticalBoost
        }
    }

    return min(urgency, maxUrgency)
}

const (
    urgencyScale   = 1000000  // 1ms per urgency point
    blockThreshold = 10       // Frequent blocker threshold
    blockBoost     = 50       // Boost for frequent blockers
    criticalSlack  = 10000000 // 10ms critical threshold
    criticalBoost  = 100      // Boost for critical deadlines
    maxUrgency     = 1000     // Cap urgency
)
```

### Integration with Scheduler

```go
// runtime/proc.go modification

func schedule() {
    // ... existing setup ...

    // CBS conflict check (periodic, not every schedule)
    if fastrand()%cbsCheckFrequency == 0 {
        conflicts := globalCBS.detectConflicts()
        for _, c := range conflicts {
            res := resolveByClass(c)
            applyResolution(res)
        }
    }

    // ... existing scheduling logic ...
}
```

## Files to Modify

| File | Changes |
|------|---------|
| `runtime/runtime2.go` | Add `urgency`, `inheritedPriority`, `blockCount` to `g` |
| `runtime/conflict.go` | New: conflict types and detection |
| `runtime/cbs.go` | New: CBS two-level search |
| `runtime/cbs_resolve.go` | New: class-specific resolution |
| `runtime/urgency.go` | New: urgency calculation |
| `runtime/proc.go` | Integrate CBS checks into scheduler |
| `runtime/sema.go` | Hook conflict detection on lock acquisition |
| `runtime/chan.go` | Hook conflict detection on channel ops |

## GOEXPERIMENT Flag

```go
// internal/goexperiment/flags.go
var CBSScheduler = false  // GOEXPERIMENT=cbsscheduler
```

## Metrics

```
/sched/cbs/conflicts-detected:counter     # Total conflicts detected
/sched/cbs/conflicts-by-class:histogram   # Conflicts per class
/sched/cbs/resolutions-applied:counter    # Resolutions executed
/sched/cbs/priority-inversions:counter    # Inversions detected
/sched/cbs/urgency-boosts:counter         # Urgency boosts applied
```

## Testing Strategy

### Unit Tests
```go
func TestConflictDetection(t *testing.T) {
    // Create known conflict scenario
    var mu sync.Mutex
    mu.Lock()

    done := make(chan bool)
    go func() {
        mu.Lock() // Will conflict
        mu.Unlock()
        done <- true
    }()

    // Verify conflict detected
    runtime.GC() // Trigger conflict scan
    // Check metrics
}
```

### Benchmarks
```go
func BenchmarkMutexContention(b *testing.B) {
    var mu sync.Mutex
    b.RunParallel(func(pb *testing.PB) {
        for pb.Next() {
            mu.Lock()
            // Simulate work
            mu.Unlock()
        }
    })
}
```

Compare with/without GOEXPERIMENT=cbsscheduler.

## Expected Benefits (Hypotheses)

All values below are hypotheses and **not verified**.

| Scenario | Current | With CBS | Improvement |
|----------|---------|----------|-------------|
| Mutex convoy | Severe degradation | Priority inheritance | 50-80% latency reduction |
| Channel starvation | Random unfairness | Urgency-based | Bounded wait time |
| I/O batching | None | Automatic | 20-40% throughput |
| Priority inversion | Unbounded | Detected & resolved | Predictable |

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Conflict detection overhead | Only check periodically (1/64 schedules) |
| CBS complexity explosion | Bound search depth, timeout resolution |
| Starvation from prioritization | Age-based urgency ensures progress |
| Overhead for simple cases | Fast path for no-conflict case |

## Comparison with MAPF-HET

| MAPF-HET | Go CBS Scheduler |
|----------|------------------|
| Vertex/edge conflicts | Mutex/channel conflicts |
| 6 dimensional classes | 10 conflict classes |
| Spatial resolution | Temporal resolution |
| 5.6× speedup | Target: 2× latency reduction |

## References

- MAPF-HET paper Section IV: Algorithm Design
- Sharon et al., "Conflict-based search for optimal MAPF" (2015)
- Sha et al., "Priority Inheritance Protocols" (1990)

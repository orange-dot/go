# Option G: Potential Field Channel Scheduling

## Summary

Apply potential field concepts from MAPF-HET and ROJ to channel operations.
Goroutines waiting on channels create "urgency potentials" that guide the
scheduler to serve high-urgency operations first, achieving emergent fairness
without explicit priority levels.

## Inspiration

From MAPF-HET paper Section V-B (Potential Field Scheduler):

> "The potential field scheduler replaces priority-based scheduling with
> gradient-mediated coordination... Deadline attraction: Udeadline = kd·(slack)^-1"

From ROJ paper Section VII (Emergent Load Balancing):

> "Cooler modules (lower tags) increase their power share; hotter modules
> decrease. This creates emergent thermal migration without centralized
> coordination."

## Current Go Channel Scheduling

Go channels use simple FIFO queues:

```go
// runtime/chan.go (simplified)
type hchan struct {
    sendq    waitq  // FIFO queue of waiting senders
    recvq    waitq  // FIFO queue of waiting receivers
}
```

**Limitations**:
- Pure FIFO ignores urgency differences
- Long-waiting goroutines have no advantage
- Can cause starvation in high-contention scenarios
- No feedback between channel usage patterns

## Proposed Design

### Potential Field Structure

```go
// runtime/chan.go modification

type hchan struct {
    // ... existing fields ...

    // Potential field for scheduling
    sendPotential   int32    // Aggregate sender urgency (Q15 fixed-point)
    recvPotential   int32    // Aggregate receiver urgency
    lastUpdate      int64    // Timestamp of last potential update
    contentionLevel uint32   // Historical contention metric
}

// Extended sudog with urgency
type sudog struct {
    // ... existing fields ...

    waitstart  int64   // When started waiting
    urgency    int32   // Calculated urgency (Q15)
    potential  int32   // Local potential contribution
}
```

### Urgency Calculation

```go
// runtime/chan_potential.go

const (
    potentialDecay    = 0.95   // Decay factor per update
    potentialUpdateNs = 1e6    // Update every 1ms
    urgencyPerMs      = 100    // Urgency increase per ms waiting
    maxUrgency        = 10000  // Cap urgency
    q15Scale          = 32768  // Q15 fixed-point scale
)

// Calculate urgency based on wait time
func (sg *sudog) calculateUrgency() int32 {
    waitNs := nanotime() - sg.waitstart
    waitMs := waitNs / 1e6

    // Base urgency from wait time
    urgency := int32(waitMs * urgencyPerMs)

    // Boost for repeated blocking (anti-starvation)
    gp := sg.g
    if gp.chanBlockCount > 10 {
        urgency += int32(gp.chanBlockCount * 10)
    }

    // Cap urgency
    if urgency > maxUrgency {
        urgency = maxUrgency
    }

    return urgency
}

// Update channel's aggregate potential
func (c *hchan) updatePotential() {
    now := nanotime()
    if now-c.lastUpdate < potentialUpdateNs {
        return
    }
    c.lastUpdate = now

    // Decay existing potential
    c.sendPotential = int32(float32(c.sendPotential) * potentialDecay)
    c.recvPotential = int32(float32(c.recvPotential) * potentialDecay)

    // Sum urgencies from waiting goroutines
    var sendSum, recvSum int32

    for sg := c.sendq.first; sg != nil; sg = sg.next {
        sg.urgency = sg.calculateUrgency()
        sendSum += sg.urgency
    }

    for sg := c.recvq.first; sg != nil; sg = sg.next {
        sg.urgency = sg.calculateUrgency()
        recvSum += sg.urgency
    }

    c.sendPotential += sendSum
    c.recvPotential += recvSum

    // Update contention level
    if c.sendq.first != nil || c.recvq.first != nil {
        c.contentionLevel++
    }
}
```

### Urgency-Based Selection

```go
// runtime/chan_potential.go

// Select highest-urgency waiter instead of FIFO
func (q *waitq) dequeueByUrgency() *sudog {
    if q.first == nil {
        return nil
    }

    // Fast path: only one waiter
    if q.first.next == nil {
        sg := q.first
        q.first = nil
        q.last = nil
        return sg
    }

    // Find highest urgency waiter
    var best *sudog
    var bestPrev *sudog
    var prev *sudog

    for sg := q.first; sg != nil; prev, sg = sg, sg.next {
        if best == nil || sg.urgency > best.urgency {
            best = sg
            bestPrev = prev
        }
    }

    // Remove best from queue
    if bestPrev == nil {
        q.first = best.next
    } else {
        bestPrev.next = best.next
    }
    if q.last == best {
        q.last = bestPrev
    }
    best.next = nil

    return best
}
```

### Integration with Channel Operations

```go
// runtime/chan.go modification

func chansend(c *hchan, ep unsafe.Pointer, block bool, callerpc uintptr) bool {
    // ... existing setup ...

    // Update potential field (periodic)
    if goexperiment.PotentialChannels {
        c.updatePotential()
    }

    // Check for waiting receiver
    if sg := c.recvq.dequeueByUrgency(); sg != nil {
        // Send to highest-urgency receiver
        send(c, sg, ep, func() { unlock(&c.lock) }, 3)
        return true
    }

    // ... rest of existing logic ...
}

func chanrecv(c *hchan, ep unsafe.Pointer, block bool) (selected, received bool) {
    // ... existing setup ...

    // Update potential field (periodic)
    if goexperiment.PotentialChannels {
        c.updatePotential()
    }

    // Check for waiting sender
    if sg := c.sendq.dequeueByUrgency(); sg != nil {
        // Receive from highest-urgency sender
        recv(c, sg, ep, func() { unlock(&c.lock) }, 3)
        return true, true
    }

    // ... rest of existing logic ...
}
```

### Gradient-Based Select

```go
// runtime/select.go modification

func selectgo(cas0 *scase, order0 *uint16, pc0 *uintptr, nsends, nrecvs int, block bool) (int, bool) {
    // ... existing setup ...

    if goexperiment.PotentialChannels {
        // Sort cases by channel potential (descending)
        // Higher potential = more urgent waiters = prioritize
        sortCasesByPotential(scases, pollorder)
    }

    // ... rest of select logic ...
}

func sortCasesByPotential(cases []scase, order []uint16) {
    // Simple insertion sort (small N)
    for i := 1; i < len(order); i++ {
        j := i
        for j > 0 {
            ci := cases[order[j]]
            cj := cases[order[j-1]]

            potI := channelPotential(ci.c, ci.kind)
            potJ := channelPotential(cj.c, cj.kind)

            if potI > potJ {
                order[j], order[j-1] = order[j-1], order[j]
                j--
            } else {
                break
            }
        }
    }
}

func channelPotential(c *hchan, kind uint16) int32 {
    if c == nil {
        return 0
    }
    switch kind {
    case caseSend:
        return c.recvPotential  // Senders care about receiver urgency
    case caseRecv:
        return c.sendPotential  // Receivers care about sender urgency
    default:
        return 0
    }
}
```

## Files to Modify

| File | Changes |
|------|---------|
| `runtime/chan.go` | Add potential fields to `hchan`, modify send/recv |
| `runtime/runtime2.go` | Add `chanBlockCount` to `g`, `urgency` to `sudog` |
| `runtime/chan_potential.go` | New: urgency calculation, dequeue by urgency |
| `runtime/select.go` | Sort cases by potential |

## GOEXPERIMENT Flag

```go
// internal/goexperiment/flags.go
var PotentialChannels = false  // GOEXPERIMENT=potentialchannels
```

## Metrics

```
/sync/channels/potential-updates:counter    # Potential field updates
/sync/channels/urgency-selections:counter   # Non-FIFO selections
/sync/channels/max-wait-time:histogram      # Wait time distribution
/sync/channels/contention-level:gauge       # Per-channel contention
```

## Testing Strategy

### Unit Tests
```go
func TestUrgencyBasedSelection(t *testing.T) {
    ch := make(chan int)

    // Start slow receiver first
    go func() {
        time.Sleep(10 * time.Millisecond)
        <-ch
    }()

    // Start fast receiver second
    go func() {
        <-ch // Should be served second despite arriving second
    }()

    time.Sleep(20 * time.Millisecond)

    // Send - should go to long-waiting receiver first
    ch <- 1
    ch <- 2
}
```

### Fairness Benchmark
```go
func BenchmarkChannelFairness(b *testing.B) {
    ch := make(chan int)
    var waitTimes []time.Duration

    // Multiple receivers with different start times
    for i := 0; i < 10; i++ {
        go func(delay time.Duration) {
            time.Sleep(delay)
            start := time.Now()
            <-ch
            waitTimes = append(waitTimes, time.Since(start))
        }(time.Duration(i) * time.Millisecond)
    }

    time.Sleep(50 * time.Millisecond)

    // Send to all
    for i := 0; i < 10; i++ {
        ch <- i
    }

    // Analyze wait time variance
    // Lower variance = better fairness
}
```

## Expected Benefits

| Scenario | Current (FIFO) | With Potential | Improvement |
|----------|----------------|----------------|-------------|
| Long waiter | May wait indefinitely | Prioritized | Bounded wait |
| High contention | Random unfairness | Emergent fairness | Lower variance |
| Select with multiple channels | Random order | Urgency order | Better responsiveness |
| Producer-consumer imbalance | Buffering helps | Field balances | Smoother flow |

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Dequeue overhead | Only scan when queue > 1 |
| Priority inversion | Age-based urgency ensures progress |
| Pathological ordering | Cap urgency, ensure minimum progress |
| Memory overhead | 8 bytes per channel (potential fields) |

## Comparison with Papers

| MAPF-HET | Go Potential Channels |
|----------|----------------------|
| Deadline slack → field | Wait time → urgency |
| τ_normalize = 100s | urgencyPerMs = 100 |
| Gradient coordination | Aggregate potential |
| 99.9% deadline compliance | Target: bounded wait time |

## Future Extensions

1. **Cross-channel gradients**: Channels that feed each other share potential
2. **Adaptive decay**: τ adjusts based on contention level
3. **User hints**: API for deadline-aware channels
4. **Metrics integration**: Expose potential via runtime/metrics

## References

- MAPF-HET paper Section V-B: Potential Field Scheduler
- ROJ paper Section VII: Emergent Load Balancing
- Khatib, "Real-time obstacle avoidance" (1986) - original potential fields

# Option B: Supervised Goroutines (Byzantine Fault Tolerance)

## Summary

Add fault isolation and automatic recovery to goroutines, inspired by JEZGRO's
reincarnation server and Erlang/OTP's supervisor pattern. Panicking goroutines
can be automatically restarted without crashing the entire program.

## Inspiration

From ROJ paper Section III-C (JEZGRO Microkernel):

> "Inspired by MINIX, JEZGRO provides fault isolation on resource-constrained
> MCUs using the ARM Cortex-M Memory Protection Unit (MPU)... Reincarnation
> server: Automatically restarts crashed services without system reset."

And Section V-D (Recovery):

> "Quarantined modules can rejoin after: Fresh firmware upload and self-test,
> 24-hour probationary period with reduced trust"

## Current Go Behavior

Go's current panic/recover model:

1. Panic unwinds the stack of the current goroutine
2. Deferred functions execute
3. If no recover, program crashes
4. `recover()` must be in deferred function to catch panic

**Limitations**:
- No automatic restart of failed goroutines
- No isolation between goroutines (shared memory)
- Server must implement its own supervision
- Single panic can crash entire long-running program

## Proposed Design

### Level 1: Supervision Groups (Library Level)

New `runtime/supervised` or `x/sync/supervised` package:

```go
package supervised

// Strategy defines how to handle goroutine failures
type Strategy int

const (
    // OneForOne: Only restart the failed goroutine
    OneForOne Strategy = iota
    // OneForAll: Restart all goroutines in the group
    OneForAll
    // RestForOne: Restart the failed goroutine and all started after it
    RestForOne
)

// Group manages a set of supervised goroutines
type Group struct {
    strategy    Strategy
    maxRestarts int           // Max restarts within window
    window      time.Duration // Time window for restart counting

    mu          sync.Mutex
    children    []*child
    restarts    map[*child][]time.Time
    ctx         context.Context
    cancel      context.CancelFunc
}

type child struct {
    id       int
    fn       func(context.Context)
    restarts int
    state    childState
}

type childState int

const (
    stateRunning childState = iota
    stateRestarting
    stateFailed  // Exceeded max restarts
    stateStopped
)

// New creates a supervision group
func New(opts ...Option) *Group {
    g := &Group{
        strategy:    OneForOne,
        maxRestarts: 3,
        window:      time.Minute,
        restarts:    make(map[*child][]time.Time),
    }
    g.ctx, g.cancel = context.WithCancel(context.Background())
    for _, opt := range opts {
        opt(g)
    }
    return g
}

// Go starts a supervised goroutine
func (g *Group) Go(fn func(context.Context)) {
    g.mu.Lock()
    c := &child{
        id:    len(g.children),
        fn:    fn,
        state: stateRunning,
    }
    g.children = append(g.children, c)
    g.mu.Unlock()

    g.runChild(c)
}

func (g *Group) runChild(c *child) {
    go func() {
        defer func() {
            if r := recover(); r != nil {
                g.handlePanic(c, r)
            }
        }()
        c.fn(g.ctx)
    }()
}

func (g *Group) handlePanic(c *child, panicValue interface{}) {
    g.mu.Lock()
    defer g.mu.Unlock()

    // Record restart
    now := time.Now()
    g.restarts[c] = append(g.restarts[c], now)

    // Count recent restarts
    cutoff := now.Add(-g.window)
    recent := 0
    for _, t := range g.restarts[c] {
        if t.After(cutoff) {
            recent++
        }
    }

    if recent > g.maxRestarts {
        c.state = stateFailed
        g.onChildFailed(c, panicValue)
        return
    }

    // Apply restart strategy
    switch g.strategy {
    case OneForOne:
        c.state = stateRestarting
        g.runChild(c)

    case OneForAll:
        for _, other := range g.children {
            // Cancel and restart all
            other.state = stateRestarting
        }
        g.restartAll()

    case RestForOne:
        for i := c.id; i < len(g.children); i++ {
            g.children[i].state = stateRestarting
        }
        g.restartFrom(c.id)
    }
}
```

### Level 2: Runtime Supervision (Deep Integration)

Add supervision primitives to the runtime itself:

```go
// runtime/supervisor.go

// Supervisor manages goroutine lifecycle at runtime level
type supervisor struct {
    id          uint64
    strategy    supervisorStrategy
    maxRestarts int32
    window      int64 // nanoseconds

    // Lock-free child tracking
    children    atomic.Pointer[[]supervisedG]

    // Metrics
    totalRestarts uint64
    totalFailures uint64
}

type supervisedG struct {
    gp       *g
    fn       func()
    restarts int32
    lastPanic int64
}

// runtime/panic.go modification
func gopanic(e interface{}) {
    gp := getg()

    // Check if goroutine is supervised
    if sup := gp.supervisor; sup != nil {
        // Don't crash, let supervisor handle
        sup.handlePanic(gp, e)
        // Park this G, supervisor will decide fate
        gopark(nil, nil, waitReasonSupervisorDecision, traceEvGoBlock, 1)
        return
    }

    // Normal panic handling
    // ... existing code ...
}
```

### Level 3: Memory Isolation (Hardware-Assisted)

For true Byzantine fault tolerance, isolate goroutine groups using MPU/MMU:

```go
// runtime/isolation.go (future, requires OS support)

// IsolationDomain provides memory isolation for a group of goroutines
type IsolationDomain struct {
    id       uint64
    heapBase uintptr
    heapSize uintptr
    // Memory protection key (Intel PKU) or ARM memory domain
    mpuKey   uint32
}

// Goroutines in different domains cannot access each other's memory
// Communication only via channels (copied, not shared)
```

## API Design

### Simple API (covers 90% of use cases)

```go
import "runtime/supervised"

func main() {
    sup := supervised.New(
        supervised.WithStrategy(supervised.OneForOne),
        supervised.WithMaxRestarts(5, time.Minute),
    )

    // Start supervised workers
    for i := 0; i < 10; i++ {
        id := i
        sup.Go(func(ctx context.Context) {
            worker(ctx, id)
        })
    }

    // Wait for shutdown signal
    <-signalChan
    sup.Shutdown(5 * time.Second)
}

func worker(ctx context.Context, id int) {
    for {
        select {
        case <-ctx.Done():
            return
        default:
            // Do work, may panic
            processItem(id)
        }
    }
}
```

### Advanced API (supervision trees)

```go
sup := supervised.New()

// Create child supervisors (supervision tree)
dbSup := sup.Supervisor("database",
    supervised.WithStrategy(supervised.OneForAll),
)
dbSup.Go(connectionPool)
dbSup.Go(queryExecutor)

httpSup := sup.Supervisor("http",
    supervised.WithStrategy(supervised.OneForOne),
)
for i := 0; i < numWorkers; i++ {
    httpSup.Go(httpHandler)
}
```

## Files to Modify

| File | Changes |
|------|---------|
| `runtime/runtime2.go` | Add `supervisor` field to `g` struct |
| `runtime/panic.go` | Check for supervision before crashing |
| `runtime/supervisor.go` | New file: supervisor implementation |
| `runtime/proc.go` | Supervisor lifecycle tied to P |
| `runtime/supervised/*.go` | New package: user-facing API |

## Backward Compatibility

- Default behavior unchanged (no supervisor = current panic behavior)
- Supervised goroutines opt-in via new API
- No changes to existing programs

## Metrics

```
/runtime/supervised/restarts:counter      # Total goroutine restarts
/runtime/supervised/failures:counter      # Goroutines exceeded restart limit
/runtime/supervised/active:gauge          # Currently supervised goroutines
/runtime/supervised/groups:gauge          # Active supervision groups
```

## Testing Strategy

### Correctness Tests
```go
func TestSupervisorRestart(t *testing.T) {
    restarts := atomic.Int32{}

    sup := supervised.New(supervised.WithMaxRestarts(3, time.Minute))
    sup.Go(func(ctx context.Context) {
        restarts.Add(1)
        panic("intentional")
    })

    time.Sleep(100 * time.Millisecond)
    if got := restarts.Load(); got != 3 {
        t.Errorf("expected 3 restarts, got %d", got)
    }
}
```

### Stress Tests
- 1000 supervised goroutines, random panics
- Verify no memory leaks after restarts
- Verify metrics accuracy

### Integration Tests
- HTTP server with supervised handlers
- Database connection pool recovery
- Message queue consumer recovery

## Expected Benefits

| Scenario | Current | With Supervision |
|----------|---------|------------------|
| Worker panic | Program crashes | Worker restarts |
| Memory corruption | Undefined behavior | Domain isolated |
| Cascading failure | Full outage | Partial degradation |
| Recovery time | Manual restart | Automatic, < 1ms |

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Infinite restart loops | Max restarts with time window |
| Resource leaks | Cleanup hooks, defer still runs |
| Shared state corruption | Recommend isolated state per goroutine |
| Performance overhead | Supervision check only on panic path |

## Comparison with Erlang/OTP

| Feature | Erlang/OTP | Go Supervised |
|---------|------------|---------------|
| Process isolation | Full (separate heaps) | Shared (with optional domains) |
| Message passing | Required | Channels (optional) |
| Supervision trees | Built-in | Proposed |
| Hot code reload | Yes | No (out of scope) |
| Restart strategies | 4 types | 3 types (same) |

## Future Extensions

1. **Probationary period**: Like ROJ's 24-hour reduced trust after recovery
2. **Health checks**: Active probing, not just panic detection
3. **Circuit breakers**: Automatic backoff on repeated failures
4. **Distributed supervision**: Across machines (ties to Option C)

## References

- ROJ Paper Section III-C: JEZGRO Microkernel
- ROJ Paper Section V-D: Recovery
- Erlang/OTP Supervisor: erlang.org/doc/design_principles/sup_princ.html
- MINIX Reincarnation Server: minix3.org

# Option D: CRDTs in the Runtime

## Summary

Add Conflict-Free Replicated Data Types (CRDTs) to Go's standard library and
runtime. CRDTs are data structures that can be modified concurrently without
coordination and always converge to a consistent state. This eliminates many
race conditions by construction.


## Status (Research Sketch)

- Unverified ideas only; all numbers are hypotheses.
- Cross-option dependencies are intentional and noted below.
- Any public API should live under golang.org/x/exp (not the standard library).

## Inspiration

From ROJ paper Section IV-C (Event Gossip with Version Vectors):

> "For state propagation, we use epidemic gossip with version vectors for
> causal ordering. Each module maintains: VV[module_id] = highest_sequence_seen"

Version vectors are a form of CRDT (specifically, a vector clock for causal
ordering). The paper demonstrates that these primitives work efficiently on
resource-constrained systems.


## Dependencies

- None (standalone).
- Optional: Option C can use CRDTs for replicated state.

## What Are CRDTs?

CRDTs are data structures with a mathematically proven property: concurrent
updates always converge to the same final state, regardless of the order in
which updates are applied.

Two types:
1. **State-based (CvRDT)**: Merge complete states
2. **Operation-based (CmRDT)**: Apply operations (requires causal delivery)

## Proposed Package: `golang.org/x/exp/crdt`

```go
package crdt

// GCounter is a grow-only counter
// Supports: Increment, Value
// Converges: Always (max of each node's count)
type GCounter struct { ... }

// PNCounter is a positive-negative counter
// Supports: Increment, Decrement, Value
// Converges: Always
type PNCounter struct { ... }

// GSet is a grow-only set
// Supports: Add, Contains, Elements
// Converges: Always (union)
type GSet[T comparable] struct { ... }

// TwoPhaseSet supports add and remove (remove is permanent)
// Supports: Add, Remove, Contains
// Converges: Always
type TwoPhaseSet[T comparable] struct { ... }

// ORSet is an observed-remove set (add-wins)
// Supports: Add, Remove, Contains (remove can be undone by add)
// Converges: Always
type ORSet[T comparable] struct { ... }

// LWWRegister is a last-writer-wins register
// Supports: Set, Get
// Converges: Based on timestamp (requires synchronized clocks)
type LWWRegister[T any] struct { ... }

// MVRegister is a multi-value register
// Supports: Set, Get (may return multiple concurrent values)
// Converges: Always (but may have conflicts to resolve)
type MVRegister[T any] struct { ... }

// VersionVector tracks causality across nodes
// Supports: Increment, Merge, Compare
// Converges: Always
type VersionVector struct { ... }
```

## Implementation Details

### GCounter (Grow-Only Counter)

```go
// golang.org/x/exp/crdt/gcounter.go

// GCounter is a conflict-free grow-only counter.
// It can be incremented from multiple goroutines and nodes
// without locks, and always converges to the correct total.
type GCounter struct {
    nodeID uint64
    counts sync.Map  // map[uint64]uint64 - nodeID -> count
}

// NewGCounter creates a counter for the given node ID.
// Each goroutine/node should have a unique ID.
func NewGCounter(nodeID uint64) *GCounter {
    return &GCounter{nodeID: nodeID}
}

// Increment adds 1 to this node's count.
// Safe to call concurrently from the same node.
func (gc *GCounter) Increment() {
    gc.IncrementBy(1)
}

// IncrementBy adds delta to this node's count.
func (gc *GCounter) IncrementBy(delta uint64) {
    for {
        val, _ := gc.counts.LoadOrStore(gc.nodeID, uint64(0))
        current := val.(uint64)
        if gc.counts.CompareAndSwap(gc.nodeID, current, current+delta) {
            return
        }
    }
}

// Value returns the total count across all nodes.
func (gc *GCounter) Value() uint64 {
    var total uint64
    gc.counts.Range(func(key, value interface{}) bool {
        total += value.(uint64)
        return true
    })
    return total
}

// Merge combines another GCounter's state into this one.
// Takes the maximum count for each node.
func (gc *GCounter) Merge(other *GCounter) {
    other.counts.Range(func(key, value interface{}) bool {
        nodeID := key.(uint64)
        otherCount := value.(uint64)

        for {
            current, loaded := gc.counts.LoadOrStore(nodeID, otherCount)
            if !loaded {
                return true  // Inserted new value
            }

            currentCount := current.(uint64)
            if otherCount <= currentCount {
                return true  // Our value is already >=
            }

            if gc.counts.CompareAndSwap(nodeID, currentCount, otherCount) {
                return true
            }
            // CAS failed, retry
        }
        return true
    })
}

// State returns a snapshot for serialization.
func (gc *GCounter) State() map[uint64]uint64 {
    state := make(map[uint64]uint64)
    gc.counts.Range(func(key, value interface{}) bool {
        state[key.(uint64)] = value.(uint64)
        return true
    })
    return state
}
```

### ORSet (Observed-Remove Set)

```go
// golang.org/x/exp/crdt/orset.go

// ORSet is an add-wins observed-remove set.
// Elements can be added and removed concurrently.
// If add and remove happen concurrently, add wins.
type ORSet[T comparable] struct {
    nodeID  uint64
    counter uint64  // Unique tag generator

    // elements maps value -> set of (nodeID, tag) pairs
    // An element is present if it has at least one tag
    elements sync.Map  // map[T]map[elementTag]bool
}

type elementTag struct {
    nodeID uint64
    tag    uint64
}

func NewORSet[T comparable](nodeID uint64) *ORSet[T] {
    return &ORSet[T]{nodeID: nodeID}
}

// Add inserts an element with a unique tag.
func (s *ORSet[T]) Add(elem T) {
    tag := elementTag{
        nodeID: s.nodeID,
        tag:    atomic.AddUint64(&s.counter, 1),
    }

    for {
        tags, _ := s.elements.LoadOrStore(elem, &sync.Map{})
        tagsMap := tags.(*sync.Map)
        tagsMap.Store(tag, true)

        // Verify it's still the same map
        current, _ := s.elements.Load(elem)
        if current == tags {
            return
        }
        // Map was replaced, retry
    }
}

// Remove removes all observed tags for an element.
// Concurrent adds will re-add with new tags.
func (s *ORSet[T]) Remove(elem T) {
    tags, ok := s.elements.Load(elem)
    if !ok {
        return
    }

    // Clear all tags (but don't delete the map - concurrent adds might be happening)
    tagsMap := tags.(*sync.Map)
    tagsMap.Range(func(key, value interface{}) bool {
        tagsMap.Delete(key)
        return true
    })
}

// Contains checks if an element is in the set.
func (s *ORSet[T]) Contains(elem T) bool {
    tags, ok := s.elements.Load(elem)
    if !ok {
        return false
    }

    hasTag := false
    tags.(*sync.Map).Range(func(key, value interface{}) bool {
        hasTag = true
        return false  // Stop after first
    })
    return hasTag
}

// Merge combines another ORSet's state.
func (s *ORSet[T]) Merge(other *ORSet[T]) {
    other.elements.Range(func(key, value interface{}) bool {
        elem := key.(T)
        otherTags := value.(*sync.Map)

        tags, _ := s.elements.LoadOrStore(elem, &sync.Map{})
        myTags := tags.(*sync.Map)

        otherTags.Range(func(tagKey, tagValue interface{}) bool {
            myTags.Store(tagKey, true)
            return true
        })

        return true
    })
}
```

### VersionVector

```go
// golang.org/x/exp/crdt/versionvector.go

// VersionVector tracks causality across concurrent operations.
// Used for determining happens-before relationships.
type VersionVector struct {
    nodeID  uint64
    clocks  sync.Map  // map[uint64]uint64
}

func NewVersionVector(nodeID uint64) *VersionVector {
    return &VersionVector{nodeID: nodeID}
}

// Increment advances this node's clock.
func (vv *VersionVector) Increment() uint64 {
    for {
        val, _ := vv.clocks.LoadOrStore(vv.nodeID, uint64(0))
        current := val.(uint64)
        next := current + 1
        if vv.clocks.CompareAndSwap(vv.nodeID, current, next) {
            return next
        }
    }
}

// Merge combines another vector, taking max of each component.
func (vv *VersionVector) Merge(other *VersionVector) {
    other.clocks.Range(func(key, value interface{}) bool {
        nodeID := key.(uint64)
        otherClock := value.(uint64)

        for {
            current, _ := vv.clocks.LoadOrStore(nodeID, uint64(0))
            currentClock := current.(uint64)

            if otherClock <= currentClock {
                return true
            }

            if vv.clocks.CompareAndSwap(nodeID, currentClock, otherClock) {
                return true
            }
        }
        return true
    })
}

// Compare returns the causal relationship between two vectors.
type CausalOrder int

const (
    Equal      CausalOrder = iota  // Same state
    Before                         // vv happened before other
    After                          // vv happened after other
    Concurrent                     // No causal relationship
)

func (vv *VersionVector) Compare(other *VersionVector) CausalOrder {
    lessOrEqual := true
    greaterOrEqual := true

    // Check all keys in both vectors
    allKeys := make(map[uint64]bool)
    vv.clocks.Range(func(key, _ interface{}) bool {
        allKeys[key.(uint64)] = true
        return true
    })
    other.clocks.Range(func(key, _ interface{}) bool {
        allKeys[key.(uint64)] = true
        return true
    })

    for nodeID := range allKeys {
        myVal, _ := vv.clocks.Load(nodeID)
        otherVal, _ := other.clocks.Load(nodeID)

        myClock := uint64(0)
        if myVal != nil {
            myClock = myVal.(uint64)
        }
        otherClock := uint64(0)
        if otherVal != nil {
            otherClock = otherVal.(uint64)
        }

        if myClock > otherClock {
            lessOrEqual = false
        }
        if myClock < otherClock {
            greaterOrEqual = false
        }
    }

    if lessOrEqual && greaterOrEqual {
        return Equal
    }
    if lessOrEqual {
        return Before
    }
    if greaterOrEqual {
        return After
    }
    return Concurrent
}
```

## Usage Examples

### Lock-Free Distributed Counter

```go
import "golang.org/x/exp/crdt"

// Each worker has its own node ID
func worker(id uint64, counter *crdt.GCounter, done chan bool) {
    localCounter := crdt.NewGCounter(id)

    for i := 0; i < 1000; i++ {
        localCounter.Increment()
    }

    // Merge into shared counter
    counter.Merge(localCounter)
    done <- true
}

func main() {
    counter := crdt.NewGCounter(0)
    done := make(chan bool)

    for i := uint64(1); i <= 10; i++ {
        go worker(i, counter, done)
    }

    for i := 0; i < 10; i++ {
        <-done
    }

    fmt.Println(counter.Value())  // Always 10000
}
```

### Collaborative Set

```go
import "golang.org/x/exp/crdt"

type SharedState struct {
    ActiveUsers *crdt.ORSet[string]
}

func (s *SharedState) UserJoined(userID string) {
    s.ActiveUsers.Add(userID)
}

func (s *SharedState) UserLeft(userID string) {
    s.ActiveUsers.Remove(userID)
}

// Even if UserJoined and UserLeft are called concurrently
// from different goroutines, the set converges correctly.
// If they happen "at the same time", Add wins (user stays).
```

### Causal Message Ordering

```go
import "golang.org/x/exp/crdt"

type Message struct {
    Content string
    Vector  *crdt.VersionVector
}

type CausalBroadcast struct {
    nodeID  uint64
    vector  *crdt.VersionVector
    pending []*Message
    deliver chan *Message
}

func (cb *CausalBroadcast) Send(content string) {
    cb.vector.Increment()
    msg := &Message{
        Content: content,
        Vector:  cb.vector.Clone(),
    }
    // Broadcast to all nodes...
}

func (cb *CausalBroadcast) Receive(msg *Message) {
    // Buffer until causally ready
    cb.pending = append(cb.pending, msg)
    cb.tryDeliver()
}

func (cb *CausalBroadcast) tryDeliver() {
    for {
        delivered := false
        for i, msg := range cb.pending {
            if msg.Vector.Compare(cb.vector) != crdt.After {
                // Message is causally ready
                cb.vector.Merge(msg.Vector)
                cb.deliver <- msg
                cb.pending = append(cb.pending[:i], cb.pending[i+1:]...)
                delivered = true
                break
            }
        }
        if !delivered {
            return
        }
    }
}
```

## Files to Create

| File | Contents |
|------|----------|
| `x/exp/crdt/doc.go` | Package documentation |
| `x/exp/crdt/gcounter.go` | Grow-only counter |
| `x/exp/crdt/pncounter.go` | Positive-negative counter |
| `x/exp/crdt/gset.go` | Grow-only set |
| `x/exp/crdt/twophaseset.go` | Two-phase set |
| `x/exp/crdt/orset.go` | Observed-remove set |
| `x/exp/crdt/lwwregister.go` | Last-writer-wins register |
| `x/exp/crdt/mvregister.go` | Multi-value register |
| `x/exp/crdt/versionvector.go` | Version vector |
| `x/exp/crdt/*_test.go` | Tests for each type |

## API Compatibility

- No stdlib API changes; experimental API lives in `golang.org/x/exp/crdt`.
- No updates to `api/go1.*.txt`.

### Experimental API Surface (x/exp)

```
package crdt
func NewGCounter(uint64) *GCounter
func (*GCounter) Increment()
func (*GCounter) IncrementBy(uint64)
func (*GCounter) Value() uint64
func (*GCounter) Merge(*GCounter)
...
```

## Testing Strategy

### Unit Tests
- Each CRDT type has comprehensive tests
- Concurrent access tests with race detector
- Merge commutativity/associativity tests

### Property-Based Tests
```go
func TestGCounterConvergence(t *testing.T) {
    rapid.Check(t, func(t *rapid.T) {
        // Generate random operations
        ops := rapid.SliceOf(rapid.Uint64()).Draw(t, "ops")

        // Apply in different orders
        c1 := crdt.NewGCounter(1)
        c2 := crdt.NewGCounter(2)

        for _, op := range ops {
            c1.IncrementBy(op)
        }
        for i := len(ops) - 1; i >= 0; i-- {
            c2.IncrementBy(ops[i])
        }

        // Must converge
        c1.Merge(c2)
        c2.Merge(c1)

        if c1.Value() != c2.Value() {
            t.Fatalf("did not converge: %d != %d", c1.Value(), c2.Value())
        }
    })
}
```

### Benchmarks
```go
func BenchmarkGCounterIncrement(b *testing.B) {
    c := crdt.NewGCounter(1)
    b.RunParallel(func(pb *testing.PB) {
        for pb.Next() {
            c.Increment()
        }
    })
}
```

## Expected Benefits (Hypotheses)

All values below are hypotheses and **not verified**.

| Scenario | Without CRDTs | With CRDTs |
|----------|---------------|------------|
| Concurrent counters | Mutex or atomic + careful design | Just use GCounter |
| Distributed set | Complex merge logic | ORSet handles it |
| Causal ordering | Implement Lamport/vector clocks | VersionVector built-in |
| Race conditions | Possible if not careful | Impossible by construction |

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Memory overhead | Document costs, provide `Compact()` methods |
| Not all data fits CRDTs | Clear documentation on when to use |
| Tombstone accumulation | Periodic garbage collection, TTL options |
| Learning curve | Good docs, examples, comparison with sync.Mutex |

## References

- ROJ Paper Section IV-C: Version Vector Gossip
- Shapiro et al., "Conflict-Free Replicated Data Types" (2011)
- Riak DT: docs.riak.com/riak/kv/latest/developing/data-types/
- Automerge: automerge.org
- Yjs: yjs.dev

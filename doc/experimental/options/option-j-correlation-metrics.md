# Option J: Scale-Free Correlation Metrics

## Summary

Implement observability features based on the papers' k=7 topological
coordination and scale-free correlation theory. Expose metrics that help
developers understand how information and state propagate through their
concurrent Go programs.


## Status (Research Sketch)

- Unverified ideas only; all numbers are hypotheses.
- Cross-option dependencies are intentional and noted below.
- Any public API should live under golang.org/x/exp (not the standard library).

## Inspiration

From MAPF-HET paper Section V-C (Topological k=7 Coordination):

> "The breakthrough insight from Cavagna and Giardina's study of starling flocks
> reveals that scale-free correlations emerge when individuals interact with a
> fixed number of topological neighbors regardless of distance."

> "Information propagates in O(log N) hops regardless of physical network
> topology."

And from the theoretical analysis (Section VI-D):

> "With k = 7 topological neighbor coordination, the correlation length ξ
> scales linearly with system size N: ξ ∝ N"


## Dependencies

- Option H for goroutine dimension labels.
- Optional: Option F/G/I for urgency signals.

## Current Go Observability

Go provides through `runtime/metrics`:

- Goroutine counts
- GC statistics
- Memory allocation
- CPU scheduling time

**Missing**:
- How goroutines interact (communication patterns)
- Information propagation characteristics
- Correlation between goroutine behaviors
- Concurrency topology analysis

## Proposed Design

### Correlation Metrics Package

```go
// golang.org/x/exp/correlation/metrics.go

// CorrelationSnapshot captures the current state of goroutine interactions
type CorrelationSnapshot struct {
    Timestamp         int64
    GoroutineCount    int
    ActiveChannels    int
    PropagationHops   float64  // Avg hops for info to spread
    CorrelationLength float64  // ξ from scale-free theory
    ClusterCoeff      float64  // Local clustering coefficient
    NetworkDiameter   int      // Max shortest path
    CommunityCount    int      // Detected communities
}

// ChannelTopology represents the channel communication graph
type ChannelTopology struct {
    Nodes []GoroutineNode
    Edges []ChannelEdge
}

type GoroutineNode struct {
    ID           uint64
    Dimension    uint8    // From Option H classification
    Urgency      int32    // Current urgency
    Neighbors    int      // Connected goroutines
    InDegree     int      // Receiving channels
    OutDegree    int      // Sending channels
}

type ChannelEdge struct {
    From      uint64  // Sender goroutine
    To        uint64  // Receiver goroutine
    ChannelID uint64  // Channel identifier
    Messages  uint64  // Message count
    Latency   float64 // Avg message latency
}
```

### Tracking Infrastructure

```go
// golang.org/x/exp/correlation/track.go

// Correlation tracker maintains the interaction graph
type correlationTracker struct {
    // Interaction graph (sparse representation)
    interactions sync.Map  // map[uint64][]interaction

    // Recent propagation events
    propagations ringBuffer

    // Computed metrics (cached)
    cachedMetrics CorrelationSnapshot
    cacheTime     int64

    // Sampling control
    sampleRate uint32
}

type interaction struct {
    fromG     uint64
    toG       uint64
    channel   uint64
    timestamp int64
    hops      int  // Propagation depth
}

var globalCorrelation correlationTracker

// Track channel send
func trackChannelSend(from, to *g, ch *hchan) {
    if !goexperiment.CorrelationMetrics {
        return
    }

    // Sample to reduce overhead
    if fastrand()%globalCorrelation.sampleRate != 0 {
        return
    }

    globalCorrelation.recordInteraction(interaction{
        fromG:     from.goid,
        toG:       to.goid,
        channel:   uintptr(unsafe.Pointer(ch)),
        timestamp: nanotime(),
    })
}
```

### Metric Computation

```go
// golang.org/x/exp/correlation/compute.go

// ComputeCorrelationMetrics calculates current metrics
func ComputeCorrelationMetrics() CorrelationSnapshot {
    if !goexperiment.CorrelationMetrics {
        return CorrelationSnapshot{}
    }

    // Use cached if recent
    if nanotime()-globalCorrelation.cacheTime < cacheValidityNs {
        return globalCorrelation.cachedMetrics
    }

    // Build interaction graph
    graph := globalCorrelation.buildGraph()

    // Compute metrics
    snap := CorrelationSnapshot{
        Timestamp:         nanotime(),
        GoroutineCount:    graph.nodeCount(),
        ActiveChannels:    graph.edgeCount(),
        PropagationHops:   computePropagationHops(graph),
        CorrelationLength: computeCorrelationLength(graph),
        ClusterCoeff:      computeClusteringCoeff(graph),
        NetworkDiameter:   computeDiameter(graph),
        CommunityCount:    detectCommunities(graph),
    }

    globalCorrelation.cachedMetrics = snap
    globalCorrelation.cacheTime = nanotime()

    return snap
}

// Propagation hops: average shortest path
func computePropagationHops(g *interactionGraph) float64 {
    // Sample-based BFS from random nodes
    var totalHops int
    var samples int

    for i := 0; i < 100 && i < g.nodeCount(); i++ {
        node := g.randomNode()
        hops := g.bfsDepth(node)
        totalHops += hops
        samples++
    }

    if samples == 0 {
        return 0
    }
    return float64(totalHops) / float64(samples)
}

// Correlation length: how far correlations extend
func computeCorrelationLength(g *interactionGraph) float64 {
    // Based on Cavagna et al.'s method
    // ξ = slope of log(correlation) vs log(distance)

    // Sample goroutine pairs
    var correlations []float64
    var distances []float64

    for i := 0; i < 1000 && i < g.nodeCount()*g.nodeCount(); i++ {
        n1 := g.randomNode()
        n2 := g.randomNode()
        if n1.id == n2.id {
            continue
        }

        // "Distance" = shortest path in interaction graph
        dist := g.shortestPath(n1, n2)
        if dist == -1 {
            continue
        }

        // "Correlation" = similarity of urgency patterns
        corr := urgencyCorrelation(n1, n2)

        distances = append(distances, float64(dist))
        correlations = append(correlations, corr)
    }

    // Fit power law: corr ~ dist^(-1/ξ)
    return fitCorrelationLength(distances, correlations)
}

// Clustering coefficient: local density of interactions
func computeClusteringCoeff(g *interactionGraph) float64 {
    var sum float64
    var count int

    g.forEachNode(func(n *graphNode) {
        neighbors := n.neighbors()
        if len(neighbors) < 2 {
            return
        }

        // Count edges between neighbors
        var edges int
        for i, n1 := range neighbors {
            for _, n2 := range neighbors[i+1:] {
                if g.hasEdge(n1, n2) {
                    edges++
                }
            }
        }

        possible := len(neighbors) * (len(neighbors) - 1) / 2
        if possible > 0 {
            sum += float64(edges) / float64(possible)
            count++
        }
    })

    if count == 0 {
        return 0
    }
    return sum / float64(count)
}
```

### Exposed Metrics

```go
// runtime/metrics integration

func init() {
    // Register correlation metrics
    metrics.Register("/correlation/propagation-hops", metrics.Float64)
    metrics.Register("/correlation/correlation-length", metrics.Float64)
    metrics.Register("/correlation/clustering-coefficient", metrics.Float64)
    metrics.Register("/correlation/network-diameter", metrics.Int64)
    metrics.Register("/correlation/community-count", metrics.Int64)
    metrics.Register("/correlation/active-channels", metrics.Int64)
}
```

### Visualization Support

```go
// golang.org/x/exp/correlation/export.go

// ExportTopologyDOT exports the interaction graph in DOT format
func ExportTopologyDOT(w io.Writer) error {
    snap := ComputeCorrelationMetrics()
    graph := globalCorrelation.buildGraph()

    fmt.Fprintln(w, "digraph goroutines {")
    fmt.Fprintln(w, "  rankdir=LR;")

    // Nodes colored by dimension
    graph.forEachNode(func(n *graphNode) {
        color := dimensionColor(n.dimension)
        fmt.Fprintf(w, "  g%d [label=\"G%d\\n(urg:%d)\", color=%s];\n",
            n.id, n.id, n.urgency, color)
    })

    // Edges with message count
    graph.forEachEdge(func(e *graphEdge) {
        fmt.Fprintf(w, "  g%d -> g%d [label=\"%d msgs\"];\n",
            e.from, e.to, e.messages)
    })

    fmt.Fprintln(w, "}")
    return nil
}

// ExportTopologyJSON exports for web visualization
func ExportTopologyJSON(w io.Writer) error {
    // D3.js compatible format
    // ...
}
```

### Analysis Tools

```go
// golang.org/x/exp/correlation/cmd/correlate/correlate.go

// correlate - analyze goroutine interactions
//
// Usage:
//   correlate -pprof=/path/to/correlation.prof
//   correlate -live localhost:6060
//
// Output:
//   - Propagation analysis
//   - Bottleneck detection
//   - Community detection
//   - Scale-free validation
```

## Files to Create/Modify

| File | Changes |
|------|---------|
| `runtime/correlation_track.go` | New: interaction tracking |
| `runtime/correlation_compute.go` | New: metric computation |
| `runtime/correlation_export.go` | New: visualization export |
| `runtime/chan.go` | Add tracking hooks |
| `runtime/metrics.go` | Register correlation metrics |
| `golang.org/x/exp/correlation/cmd/correlate/` | New: analysis tool |

## GOEXPERIMENT Flag

```go
// internal/goexperiment/flags.go
var CorrelationMetrics = false  // GOEXPERIMENT=correlationmetrics
```

## New API (golang.org/x/exp/correlation)

```
package correlation
func Snapshot() CorrelationSnapshot
func ExportDOT(io.Writer) error
func ExportJSON(io.Writer) error
type CorrelationSnapshot struct
type ChannelTopology struct
```

## Metrics Exposed

```
/correlation/propagation-hops:gauge        # Avg hops for info spread
/correlation/correlation-length:gauge      # Scale-free ξ parameter
/correlation/clustering-coefficient:gauge  # Local density
/correlation/network-diameter:gauge        # Max shortest path
/correlation/community-count:gauge         # Detected communities
/correlation/active-channels:gauge         # Channels in use
/correlation/interactions-tracked:counter  # Total interactions
```

## Testing Strategy

### Unit Tests
```go
func TestCorrelationTracking(t *testing.T) {
    ch := make(chan int)

    go func() { ch <- 1 }()
    go func() { <-ch }()

    time.Sleep(10 * time.Millisecond)

    snap := correlation.Snapshot()
    if snap.ActiveChannels < 1 {
        t.Error("Expected at least one active channel")
    }
}

func TestScaleFreeProperty(t *testing.T) {
    // Create network known to be scale-free
    // Verify ξ scales with N
}
```

### Benchmarks
```go
func BenchmarkCorrelationOverhead(b *testing.B) {
    ch := make(chan int, 100)

    b.RunParallel(func(pb *testing.PB) {
        for pb.Next() {
            ch <- 1
            <-ch
        }
    })
    // Measure overhead with/without tracking
}
```

## Expected Benefits (Hypotheses)

All values below are hypotheses and **not verified**.

| Use Case | Current | With Correlation | Value |
|----------|---------|------------------|-------|
| Debug concurrency | Blind | Topology visible | Qualitative |
| Find bottlenecks | Guess | Diameter analysis | Quantitative |
| Validate design | Manual | Scale-free check | Automated |
| Optimize communication | Trial/error | Community detection | Guided |

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Tracking overhead | Sampling, GOEXPERIMENT guard |
| Memory for graph | Bounded history, GC integration |
| Computation cost | Caching, lazy evaluation |
| Privacy concerns | Only internal Go use, no goroutine names |

## Use Cases

1. **Performance debugging**: Find communication bottlenecks
2. **Architecture validation**: Verify expected topology
3. **Capacity planning**: Understand scaling behavior
4. **Research**: Study concurrent Go program properties

## Comparison with Papers

| Paper Metric | Go Equivalent |
|--------------|---------------|
| k=7 neighbors | Avg goroutine connectivity |
| ξ ∝ N | Correlation length vs G count |
| O(log N) propagation | Propagation hops |
| 42% bandwidth | Tracking overhead target |

## Future Extensions

1. **Real-time visualization**: Web UI for live topology
2. **Anomaly detection**: Alert on unexpected patterns
3. **Historical analysis**: Time-series correlation data
4. **Distributed tracing integration**: Connect with OpenTelemetry

## References

- MAPF-HET paper Section V-C: Topological k=7 Coordination
- MAPF-HET paper Section VI-D: Scale-Free Correlation
- Cavagna et al., "Scale-free correlations in starling flocks" (2010)
- ROJ paper Section III-D: ROJ Coordination Layer

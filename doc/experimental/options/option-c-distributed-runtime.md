# Option C: Distributed Go Runtime

## Summary

Extend Go's runtime to operate across multiple machines as a single logical
runtime. Goroutines can migrate between nodes. The scheduler becomes distributed
using Raft consensus. This is the most radical option—essentially creating a
distributed operating system in Go.

## Inspiration

From ROJ paper:

**Section IV (Consensus Protocol)**:
> "Standard Raft relies on point-to-point RPCs with acknowledgments... We adapt
> Raft by exploiting CAN-FD's inherent broadcast and priority-based arbitration."

**Section VI (Network Partition Handling)**:
> "Only the partition containing a quorum may make consensus decisions. Minority
> partitions enter freeze mode."

**Section III-D (ROJ Coordination Layer)**:
> "Each module maintains connections to k=7 topological neighbors, inspired by
> starling flock research."

## Vision

```go
package main

import "runtime/distributed"

func main() {
    // Join a cluster of Go runtimes
    cluster := distributed.Join(
        "node1.example.com:9000",
        "node2.example.com:9000",
        "node3.example.com:9000",
    )
    defer cluster.Leave()

    // Goroutines can run on any node
    for i := 0; i < 1000; i++ {
        go processItem(i)  // Might run on node1, node2, or node3
    }

    // Channels work across nodes transparently
    ch := make(chan Result, 100)  // Distributed channel

    go func() {
        for result := range ch {
            fmt.Println(result)  // Receives from any node
        }
    }()
}
```

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    Distributed Go Runtime                     │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │   Node 1    │  │   Node 2    │  │   Node 3    │         │
│  │  ┌───────┐  │  │  ┌───────┐  │  │  ┌───────┐  │         │
│  │  │ Ps,Gs │  │  │  │ Ps,Gs │  │  │  │ Ps,Gs │  │         │
│  │  └───────┘  │  │  └───────┘  │  │  └───────┘  │         │
│  │      │      │  │      │      │  │      │      │         │
│  │  ┌───────┐  │  │  ┌───────┐  │  │  ┌───────┐  │         │
│  │  │ Local │  │  │  │ Local │  │  │  │ Local │  │         │
│  │  │ Sched │  │  │  │ Sched │  │  │  │ Sched │  │         │
│  │  └───┬───┘  │  │  └───┬───┘  │  │  └───┬───┘  │         │
│  └──────┼──────┘  └──────┼──────┘  └──────┼──────┘         │
│         │                │                │                 │
│  ┌──────┴────────────────┴────────────────┴──────┐         │
│  │           Distributed Coordination Layer        │         │
│  │  ┌────────┐  ┌─────────┐  ┌────────────────┐  │         │
│  │  │  Raft  │  │ Version │  │   Distributed  │  │         │
│  │  │Consensus│  │ Vectors │  │    Channels    │  │         │
│  │  └────────┘  └─────────┘  └────────────────┘  │         │
│  └───────────────────────────────────────────────┘         │
└─────────────────────────────────────────────────────────────┘
```

## Component Design

### 1. Cluster Membership (Raft-based)

```go
// runtime/distributed/cluster.go

type Cluster struct {
    nodeID      NodeID
    nodes       map[NodeID]*Node
    leader      NodeID

    // Raft state
    currentTerm uint64
    votedFor    NodeID
    log         *RaftLog

    // Connections
    neighbors   [7]*Node  // k=7 from ROJ
    transport   Transport
}

type Node struct {
    ID       NodeID
    Addr     string
    State    NodeState  // Follower, Candidate, Leader
    LastSeen time.Time

    // Resource info for scheduling
    NumP      int32
    MemFree   uint64
    CPULoad   float64
    HeatTag   uint64  // From Option A
}

// Leader election adapted for Go runtime
func (c *Cluster) runElection() {
    c.currentTerm++
    c.votedFor = c.nodeID
    votes := 1

    // Request votes from all nodes
    for _, node := range c.nodes {
        if node.ID == c.nodeID {
            continue
        }

        resp := c.requestVote(node, &RequestVoteArgs{
            Term:        c.currentTerm,
            CandidateID: c.nodeID,
            LastLogIdx:  c.log.LastIndex(),
            LastLogTerm: c.log.LastTerm(),
        })

        if resp.VoteGranted {
            votes++
        }
    }

    if votes > len(c.nodes)/2 {
        c.becomeLeader()
    }
}
```

### 2. Goroutine Migration

```go
// runtime/distributed/migration.go

// SerializedG represents a goroutine that can move between nodes
type SerializedG struct {
    ID          uint64
    Stack       []byte          // Serialized stack
    StackSize   uintptr
    PC          uintptr         // Program counter
    SP          uintptr         // Stack pointer
    Locals      []byte          // Local variables
    Defers      []SerializedDefer
    SourceNode  NodeID

    // For causality
    VectorClock map[NodeID]uint64
}

// Migrate moves a goroutine to another node
func (c *Cluster) Migrate(gp *g, target NodeID) error {
    // 1. Pause the goroutine
    casgstatus(gp, _Grunning, _Gwaiting)

    // 2. Serialize state
    sg := &SerializedG{
        ID:         gp.goid,
        Stack:      serializeStack(gp),
        PC:         gp.sched.pc,
        SP:         gp.sched.sp,
        Locals:     serializeLocals(gp),
        SourceNode: c.nodeID,
    }

    // 3. Send to target
    if err := c.transport.Send(target, MsgMigrate, sg); err != nil {
        // Rollback: resume locally
        casgstatus(gp, _Gwaiting, _Grunnable)
        return err
    }

    // 4. Await confirmation
    ack := c.transport.AwaitAck(sg.ID, 5*time.Second)
    if !ack {
        // Target didn't accept, resume locally
        casgstatus(gp, _Gwaiting, _Grunnable)
        return ErrMigrationFailed
    }

    // 5. Clean up local state
    gp.dead = true
    return nil
}

// Receive handles incoming migrated goroutines
func (c *Cluster) receiveMigration(sg *SerializedG) {
    // Allocate new G
    gp := malg(int(sg.StackSize))

    // Deserialize state
    deserializeStack(gp, sg.Stack)
    gp.sched.pc = sg.PC
    gp.sched.sp = sg.SP
    gp.sourceNode = sg.SourceNode
    gp.vectorClock = sg.VectorClock

    // Make runnable
    casgstatus(gp, _Gidle, _Grunnable)
    runqput(getg().m.p.ptr(), gp, false)

    // Send ack
    c.transport.SendAck(sg.SourceNode, sg.ID)
}
```

### 3. Distributed Channels

```go
// runtime/distributed/channel.go

// DistributedChan extends channels to work across nodes
type DistributedChan struct {
    localChan  chan interface{}  // Local buffer
    id         ChannelID
    cluster    *Cluster

    // Version vector for causal ordering
    vectorClock map[NodeID]uint64

    // Subscribers on other nodes
    subscribers map[NodeID]bool
}

func (dc *DistributedChan) Send(v interface{}) {
    // Increment local vector clock
    dc.vectorClock[dc.cluster.nodeID]++

    msg := &ChannelMessage{
        ChannelID:   dc.id,
        Value:       serialize(v),
        VectorClock: dc.vectorClock,
        Sender:      dc.cluster.nodeID,
    }

    // Send to local channel
    select {
    case dc.localChan <- v:
    default:
    }

    // Replicate to subscribers (gossip)
    for nodeID := range dc.subscribers {
        dc.cluster.transport.Send(nodeID, MsgChanSend, msg)
    }
}

func (dc *DistributedChan) Recv() interface{} {
    // Receive respects causal order via vector clocks
    for {
        v := <-dc.localChan
        msg := v.(*ChannelMessage)

        // Check causality
        if dc.isCausallyReady(msg.VectorClock) {
            dc.mergeVectorClock(msg.VectorClock)
            return deserialize(msg.Value)
        }

        // Not ready, buffer and wait
        dc.buffer(msg)
    }
}
```

### 4. Distributed GC

```go
// runtime/distributed/gc.go

// Distributed GC requires consensus on GC phases
type DistributedGC struct {
    cluster     *Cluster
    localGC     *gcController

    // Consensus state
    gcEpoch     uint64
    gcPhase     gcPhase
    pendingAcks map[NodeID]bool
}

func (dgc *DistributedGC) startGCCycle() {
    if dgc.cluster.leader != dgc.cluster.nodeID {
        // Only leader initiates GC
        return
    }

    dgc.gcEpoch++

    // Phase 1: Propose GC start
    proposal := &GCProposal{
        Epoch: dgc.gcEpoch,
        Phase: gcPhaseMarkStart,
    }

    // Replicate via Raft
    dgc.cluster.propose(proposal)

    // Wait for quorum
    dgc.awaitQuorum()

    // Phase 2: Mark (concurrent)
    dgc.broadcastPhase(gcPhaseMark)
    dgc.localGC.mark()
    dgc.awaitPhaseComplete(gcPhaseMark)

    // Phase 3: Sweep
    dgc.broadcastPhase(gcPhaseSweep)
    dgc.localGC.sweep()
    dgc.awaitPhaseComplete(gcPhaseSweep)
}
```

### 5. Partition Handling

From ROJ Section VI:

```go
// runtime/distributed/partition.go

type PartitionState int

const (
    PartitionHealthy PartitionState = iota
    PartitionSuspecting
    PartitionMajority
    PartitionMinority
    PartitionReconciling
)

func (c *Cluster) handlePartition() {
    reachable := c.countReachableNodes()
    total := len(c.nodes)

    if reachable > total/2 {
        // We have quorum
        c.partitionState = PartitionMajority
        // Continue normal operation
    } else {
        // Minority partition: freeze
        c.partitionState = PartitionMinority
        c.enterFreezeMode()
    }
}

func (c *Cluster) enterFreezeMode() {
    // Stop accepting new work
    c.frozen = true

    // Goroutines continue but can't migrate
    // Channels work locally only
    // GC runs locally only

    // Keep probing for partition heal
    go c.probeForHeal()
}

func (c *Cluster) reconcile(otherPartition *Cluster) {
    c.partitionState = PartitionReconciling

    // Higher epoch wins
    if otherPartition.epoch > c.epoch {
        // We defer to other partition
        c.syncStateFrom(otherPartition)
    }

    // Gradually reintegrate
    // (10% capacity increase per second, from ROJ)
    c.gradualReintegration()
}
```

## API Design

### Simple Usage

```go
import "runtime/distributed"

func main() {
    // Auto-discover cluster via DNS or config
    distributed.AutoJoin()

    // Use go keyword normally - runtime handles distribution
    go expensiveComputation()

    // Channels work across nodes
    results := make(chan int, 100)
    go producer(results)
    go consumer(results)
}
```

### Explicit Control

```go
import "runtime/distributed"

func main() {
    cluster := distributed.New(distributed.Config{
        NodeID:   "node1",
        BindAddr: ":9000",
        Seeds:    []string{"node2:9000", "node3:9000"},

        // Raft tuning
        ElectionTimeout:  300 * time.Millisecond,
        HeartbeatTimeout: 100 * time.Millisecond,
    })

    // Pin goroutine to local node
    distributed.Local(func() {
        // This won't migrate
        handleLocalIO()
    })

    // Hint: prefer specific node
    distributed.Prefer("node2", func() {
        // Scheduler prefers node2 but may migrate
        processData()
    })

    // Require: must run on specific node
    distributed.On("node3", func() {
        // Must run on node3, waits if unavailable
        accessLocalGPU()
    })
}
```

## Files to Modify/Create

| File | Changes |
|------|---------|
| `runtime/runtime2.go` | Add distributed fields to `g`, `p` |
| `runtime/proc.go` | Distributed scheduler hooks |
| `runtime/chan.go` | Distributed channel support |
| `runtime/mgc.go` | Distributed GC coordination |
| `runtime/distributed/*.go` | New package (large) |
| `cmd/go/internal/work` | Build support for distributed |

## Challenges

### 1. Shared Memory Semantics
- Go assumes shared memory; distributed systems don't have it
- Options:
  - Distributed shared memory (complex, slow)
  - Transform code to message-passing (compiler work)
  - Restrict to channel-only communication

### 2. Stack Serialization
- Go stacks contain pointers
- Need relocation when deserializing on new node
- Garbage collector integration required

### 3. Reflection & Unsafe
- `reflect` and `unsafe` break serialization
- Must detect and pin these goroutines

### 4. CGO
- C code can't migrate
- Pin goroutines that enter CGO

### 5. Performance
- Network latency orders of magnitude slower than local
- Must be very selective about what migrates

## Expected Benefits

| Scenario | Single Machine | Distributed |
|----------|----------------|-------------|
| Max goroutines | ~millions | Billions |
| Memory limit | Single machine | Cluster aggregate |
| Fault tolerance | None | Node failures tolerated |
| Scaling | Vertical only | Horizontal |
| Programming model | Same | Same (that's the magic) |

## Comparison with Alternatives

| System | Transparency | Performance | Maturity |
|--------|--------------|-------------|----------|
| Distributed Go | High | Medium | Experimental |
| Kubernetes + Go | Low | High | Production |
| Akka (Scala) | Medium | High | Production |
| Erlang/OTP | High | Medium | Production |
| Orleans (.NET) | High | High | Production |

## Research Questions

1. **Can we prove safety?** Raft provides consensus, but integration with Go semantics needs formal verification.

2. **What's the migration cost?** Stack serialization overhead vs. network latency.

3. **How to handle pointers?** Pointer-heavy code may not be migratable.

4. **GC interaction?** Cross-node garbage requires distributed tracing.

## Timeline

This is a multi-year research project:

- Year 1: Cluster membership, leader election, basic migration
- Year 2: Distributed channels, partition handling
- Year 3: Distributed GC, performance optimization
- Year 4: Production hardening, formal verification

## References

- ROJ Paper Sections IV, VI: Consensus and Partition Handling
- Raft: raft.github.io
- Orleans: dotnet.github.io/orleans/
- Erlang Distribution: erlang.org/doc/reference_manual/distributed.html
- PGAS Languages: upc.lbl.gov (Partitioned Global Address Space)

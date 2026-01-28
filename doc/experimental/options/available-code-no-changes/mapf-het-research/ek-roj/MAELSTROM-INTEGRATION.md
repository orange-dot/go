# Maelstrom Integration for ROJ Consensus

This document describes the Maelstrom distributed systems testing integration for ROJ consensus.

## Overview

### What is Maelstrom?

[Maelstrom](https://github.com/jepsen-io/maelstrom) is a workbench for learning distributed systems by writing your own. It's a **Clojure/JVM application** that:

1. Spawns your nodes as child processes
2. Simulates a network between them (with configurable faults)
3. Generates workload-specific operations (echo, broadcast, transactions, etc.)
4. Records a history of all operations and results
5. Uses **checker libraries** to verify correctness properties

Maelstrom is built on [Jepsen](https://jepsen.io/) and uses its checker ecosystem:
- **[Knossos](https://github.com/jepsen-io/knossos)** - Verifies linearizability (single-object consistency)
- **[Elle](https://github.com/jepsen-io/elle)** - Verifies transactional isolation (G0, G1, G2, etc.)

### Why Use Maelstrom for ROJ?

| Benefit | Description |
|---------|-------------|
| **Standardized workloads** | Echo, broadcast, g-counter, lin-kv, txn-list-append |
| **Network fault injection** | Message loss, delay, partitions |
| **Community benchmarks** | Compare against [Fly.io Gossip Glomers](https://fly.io/dist-sys/) solutions |
| **Rigorous checking** | Jepsen's checkers find subtle consistency bugs |

### Tool Comparison

| Tool | Role | What It Does |
|------|------|--------------|
| **Maelstrom** | Test harness | Orchestrates nodes, simulates network, generates workloads |
| **Knossos** | Checker (used by Maelstrom) | Verifies linearizability of single-object histories |
| **Elle** | Checker (used by Maelstrom/Jepsen) | Verifies transactional isolation via cycle detection |
| **TLA+** | Formal specification | Proves safety/liveness properties mathematically |

Note: Maelstrom requires **Java 11+** to run. Your nodes (written in Go, Rust, etc.) communicate with the Maelstrom harness via JSON over stdin/stdout.

## Architecture

### Track A: Direct Testing (`roj-maelstrom/`)

```
┌─────────────────────────────────────────────────────────────┐
│                     Maelstrom Harness                       │
│  (Simulates network, injects faults, checks correctness)    │
└──────────────────────────┬──────────────────────────────────┘
                           │ JSON/STDIN/STDOUT
         ┌─────────────────┼─────────────────┐
         │                 │                 │
    ┌────▼────┐       ┌────▼────┐       ┌────▼────┐
    │ ROJ Go  │◄─────►│ ROJ Go  │◄─────►│ ROJ Go  │
    │ Node 0  │       │ Node 1  │       │ Node 2  │
    └─────────┘       └─────────┘       └─────────┘
         │                 │                 │
         └─── roj_propose, roj_vote, roj_commit ──┘
                    (internal ROJ messages)
```

### Track B: Simulator Bridge (`maelstrom-sim-bridge/`)

```
┌─────────────────────────────────────────────────────────────┐
│                     Maelstrom Harness                       │
└──────────────────────────┬──────────────────────────────────┘
                           │ JSON/STDIN/STDOUT
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                   maelstrom-sim-bridge                      │
│   Translates Maelstrom RPC to Simulator HTTP calls          │
└──────────────────────────┬──────────────────────────────────┘
                           │ HTTP
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                    Go Simulator :8001                       │
│  ROJ Cluster + CAN-FD Bus Simulation + Elle History         │
└─────────────────────────────────────────────────────────────┘
```

## Implementation Details

### Project Structure

```
ek-roj/
├── roj-maelstrom/                    # Track A: Direct Maelstrom testing
│   ├── go.mod
│   ├── go.sum
│   ├── README.md
│   ├── cmd/
│   │   ├── maelstrom-echo/
│   │   │   └── main.go               # Echo workload (~30 LOC)
│   │   └── maelstrom-broadcast/
│   │       └── main.go               # Broadcast with ROJ consensus (~240 LOC)
│   ├── internal/
│   │   └── consensus/
│   │       └── state.go              # Ballot state machine
│   └── scripts/
│       ├── run-echo.sh
│       ├── run-broadcast.sh
│       ├── run-all-tests.sh
│       └── test-local.sh
│
└── maelstrom-sim-bridge/             # Track B: Simulator bridge
    ├── go.mod
    ├── go.sum
    ├── README.md
    ├── main.go                       # Bridge binary
    ├── internal/
    │   ├── adapter/
    │   │   └── simulator.go          # HTTP client for simulator API
    │   └── protocol/
    │       └── messages.go           # Maelstrom message types
    └── scripts/
        └── run-with-simulator.sh
```

### ROJ Consensus Protocol

The broadcast workload maps Maelstrom messages to ROJ's 2/3 threshold voting:

| Maelstrom Message | ROJ Action |
|-------------------|------------|
| `broadcast` | Create proposal, send `roj_propose` to neighbors |
| `roj_propose` | Store proposal, vote `accept`, gossip to neighbors |
| `roj_vote` | Collect votes, commit if ≥ 2/3 threshold |
| `roj_commit` | Apply committed value, propagate to neighbors |
| `read` | Return all committed values |
| `topology` | Store neighbor information |

### Threshold Calculation

ROJ uses 2/3 threshold voting: `ceil(n * 2/3)` where n = total nodes

| Nodes | Threshold | Votes Needed |
|-------|-----------|--------------|
| 1 | 1 | 1 |
| 3 | 2 | 2 |
| 5 | 4 | 4 |
| 7 | 5 | 5 |

This matches the test vectors in `ek-kor2/spec/test-vectors/consensus_002_vote_approved.json`.

### Simulator Bridge Message Translation

| Maelstrom RPC | Simulator HTTP Endpoint |
|---------------|-------------------------|
| `broadcast` | `POST /api/roj/propose` |
| `read` | `GET /api/roj/state?nodeId=N` |
| `topology` | Internal tracking (no-op) |

## Usage

### Prerequisites

- Go 1.21+
- Java 11+ (Maelstrom is a Clojure/JVM application)
- [Maelstrom v0.2.4+](https://github.com/jepsen-io/maelstrom/releases)

### Building

```bash
cd ek-roj/roj-maelstrom
go build -o bin/maelstrom-echo ./cmd/maelstrom-echo
go build -o bin/maelstrom-broadcast ./cmd/maelstrom-broadcast

cd ../maelstrom-sim-bridge
go build -o bin/maelstrom-sim-bridge .
```

### Running Tests

#### Echo Workload (Infrastructure Validation)

```bash
maelstrom test -w echo --bin ./bin/maelstrom-echo \
    --node-count 1 --time-limit 10
```

#### Broadcast Workload (ROJ Consensus)

```bash
# 3 nodes (quorum = 2)
maelstrom test -w broadcast --bin ./bin/maelstrom-broadcast \
    --node-count 3 --time-limit 20 --rate 10

# 5 nodes (quorum = 4)
maelstrom test -w broadcast --bin ./bin/maelstrom-broadcast \
    --node-count 5 --time-limit 30 --rate 100
```

#### Via Go Simulator (CAN-FD Simulation)

```bash
# Terminal 1: Start simulator
cd simulator/engine && go run ./cmd/simulator

# Terminal 2: Run Maelstrom with bridge
maelstrom test -w broadcast --bin ./bin/maelstrom-sim-bridge \
    --node-count 5 --time-limit 60
```

### Viewing Results

```bash
maelstrom serve  # Web UI at http://localhost:8080
cat store/latest/jepsen.log
```

## Test Results

### Local Protocol Tests

| Test | Status |
|------|--------|
| Echo workload responds | PASS |
| Broadcast initializes | PASS |
| Topology accepted | PASS |
| Broadcast acknowledged | PASS |
| Read responds | PASS |
| Simulator bridge builds | PASS |

### Expected Maelstrom Results

- [ ] Echo: 100% response rate, `echo_ok` for all messages
- [ ] Broadcast 1-node: Trivial quorum passes
- [ ] Broadcast 3-node: 2/3 threshold (2 votes) works
- [ ] Broadcast 5-node: 2/3 threshold (4 votes) works
- [ ] All reads return consistent committed set
- [ ] Maelstrom `:valid?` = `true`, no anomalies

## Dependencies

### Go Modules

**roj-maelstrom:**
```
github.com/jepsen-io/maelstrom/demo/go v0.0.0-20251128144731-cb7f07239012
github.com/google/uuid v1.6.0
```

**maelstrom-sim-bridge:**
```
github.com/jepsen-io/maelstrom/demo/go v0.0.0-20251128144731-cb7f07239012
```

### External

- Java 11+ runtime (Maelstrom is a Clojure/JVM application)
- Maelstrom binary (EPL-1.0 license)

## Platform Notes

### Windows Limitation

Full Maelstrom harness testing requires symlink support. On Windows without Developer Mode or admin privileges, Jepsen fails with:

```
java.nio.file.FileSystemException: store\current: A required privilege is not held by the client.
```

**Workarounds:**
1. Enable Developer Mode in Windows Settings
2. Run as Administrator
3. Use WSL2 with a Linux distribution
4. Test on Linux/macOS

The binaries themselves work correctly with the Maelstrom protocol regardless of platform.

## Related Documentation

- [Elle Integration](./ELLE-INTEGRATION.md) - Transactional consistency testing
- [ROJ Consensus Spec](../../tehnika/inzenjersko/en/formal/roj-consensus.tla) - TLA+ specification
- [Test Vectors](../../ek-kor2/spec/test-vectors/) - Consensus test cases
- [Simulator ROJ API](../../simulator/engine/internal/roj/) - Go simulator implementation

## References

- [Maelstrom](https://github.com/jepsen-io/maelstrom) - Distributed systems testing workbench
- [Fly.io Gossip Glomers](https://fly.io/dist-sys/) - Maelstrom challenge series
- [Jepsen](https://jepsen.io/) - Distributed systems correctness testing

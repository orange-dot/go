# ROJ Maelstrom Testing

Maelstrom distributed systems testing for ROJ consensus. This complements the existing [Elle/Jepsen integration](../ELLE-INTEGRATION.md) with lightweight, in-process testing using standardized workloads.

## Why Maelstrom?

| Tool | Best For | Checks |
|------|----------|--------|
| **Elle** | Transactional consistency | G0, G1a/b/c, G2, lost updates |
| **Maelstrom** | Message-level correctness | Linearizability, broadcast, total order |
| **TLA+** | Formal verification | Safety invariants, liveness |

Maelstrom provides:
- **Faster iteration** - No JVM startup for basic tests
- **Different failure models** - Message loss/delay vs transactional anomalies
- **Community benchmarks** - Fly.io Gossip Glomers comparisons
- **Standardized workloads** - Echo, broadcast, lin-kv, etc.

## Prerequisites

- Go 1.21+
- Java 11+ (for Maelstrom checker)
- [Maelstrom binary](https://github.com/jepsen-io/maelstrom/releases) in PATH

## Quick Start

```bash
# Build binaries
go build -o bin/maelstrom-echo ./cmd/maelstrom-echo
go build -o bin/maelstrom-broadcast ./cmd/maelstrom-broadcast

# Run echo workload (infrastructure validation)
maelstrom test -w echo --bin ./bin/maelstrom-echo \
    --node-count 1 --time-limit 10

# Run broadcast workload with ROJ consensus
maelstrom test -w broadcast --bin ./bin/maelstrom-broadcast \
    --node-count 5 --time-limit 30 --rate 100

# View results
maelstrom serve  # Web UI at http://localhost:8080
```

## Workloads

### Echo Workload

Validates Maelstrom harness integration before adding ROJ logic.

```bash
./scripts/run-echo.sh
```

Handles `echo` -> `echo_ok` message flow.

### Broadcast Workload

Tests broadcast dissemination with ROJ 2/3 threshold voting.

```bash
./scripts/run-broadcast.sh [node-count] [rate] [time-limit]

# Examples
./scripts/run-broadcast.sh           # Default: 3 nodes, 10/sec, 20s
./scripts/run-broadcast.sh 5 100 30  # 5 nodes, 100/sec, 30s
```

Message mapping:
- `broadcast` -> ROJ PROPOSE (initiate consensus)
- `roj_propose` -> Disseminate to neighbors
- `roj_vote` -> Collect votes with 2/3 threshold
- `roj_commit` -> Commit when threshold reached
- `read` -> Return committed message set
- `topology` -> Store neighbor information

## Architecture

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

## ROJ Consensus Protocol

### 2/3 Threshold Voting

ROJ uses a simple 2/3 threshold voting protocol:

1. **PROPOSE**: Initiator broadcasts proposal to neighbors
2. **VOTE**: Each node votes `accept` or `reject`
3. **COMMIT**: When ≥ 2/3 of nodes accept, value is committed
4. **PROPAGATE**: Committed values are gossiped to all nodes

Threshold calculation: `ceil(n * 2/3)` where n = total nodes

| Nodes | Threshold |
|-------|-----------|
| 1 | 1 |
| 3 | 2 |
| 5 | 4 |
| 7 | 5 |

### Test Vectors

Threshold behavior matches `ek-kor2/spec/test-vectors/consensus_002_vote_approved.json`:

```json
{
  "total_nodes": 5,
  "votes": ["accept", "accept", "accept", "accept"],
  "threshold": 0.666,
  "expected_outcome": "committed"
}
```

## Project Structure

```
roj-maelstrom/
├── go.mod
├── README.md
├── cmd/
│   ├── maelstrom-echo/
│   │   └── main.go        # Echo workload (~30 LOC)
│   └── maelstrom-broadcast/
│       └── main.go        # Broadcast with ROJ consensus (~200 LOC)
├── internal/
│   └── consensus/
│       └── state.go       # Ballot state machine (reused patterns)
└── scripts/
    ├── run-echo.sh
    ├── run-broadcast.sh
    └── run-all-tests.sh
```

## Running All Tests

```bash
./scripts/run-all-tests.sh
```

Runs:
- Echo (1 node)
- Broadcast 1-node (trivial quorum)
- Broadcast 3-node (quorum = 2)
- Broadcast 5-node (quorum = 4)
- Broadcast 5-node high rate (1000 msg/sec)

## Success Criteria

- [ ] Echo: 100% response rate, `echo_ok` for all messages
- [ ] Broadcast 1-node: Trivial quorum passes
- [ ] Broadcast 3-node: 2/3 threshold (2 votes) works
- [ ] Broadcast 5-node: 2/3 threshold (4 votes) works
- [ ] All reads return consistent committed set
- [ ] Maelstrom `:valid?` = `true`, no anomalies

## Related Documentation

- [Elle Integration](../ELLE-INTEGRATION.md) - Transactional consistency testing
- [ROJ Consensus Spec](../../../tehnika/inzenjersko/en/formal/roj-consensus.tla) - TLA+ specification
- [Test Vectors](../../../ek-kor2/spec/test-vectors/) - Consensus test cases
- [Maelstrom](https://github.com/jepsen-io/maelstrom) - Distributed systems testing library

## Simulator Bridge

For testing against the Go simulator with realistic CAN-FD timing, see:
- [maelstrom-sim-bridge](../maelstrom-sim-bridge/) - Bridge to simulator HTTP API

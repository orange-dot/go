# Maelstrom Simulator Bridge

Bridge between Maelstrom distributed systems testing and the Go simulator ROJ implementation. This enables running Maelstrom workloads against realistic CAN-FD bus simulation.

## Why Use the Bridge?

| Approach | Pros | Cons |
|----------|------|------|
| **Direct (roj-maelstrom)** | Fast, in-process | Simplified network model |
| **Bridge (this)** | Realistic CAN timing, shared infrastructure | Requires simulator running |

The bridge is useful for:
- Testing with realistic CAN-FD bus latency and ordering
- Reusing existing simulator infrastructure
- Validating consistency between pure Go and simulated implementations
- Exporting Elle history from tests

## Prerequisites

- Go 1.21+
- Java 11+ (for Maelstrom checker)
- [Maelstrom binary](https://github.com/jepsen-io/maelstrom/releases) in PATH
- Go simulator running on port 8001

## Quick Start

```bash
# Terminal 1: Start the Go simulator
cd simulator/engine
go run ./cmd/simulator

# Terminal 2: Run Maelstrom with the bridge
cd mapf-het-research/ek-roj/maelstrom-sim-bridge
go build -o bin/maelstrom-sim-bridge .

maelstrom test -w broadcast --bin ./bin/maelstrom-sim-bridge \
    --node-count 5 --time-limit 60 --rate 10
```

Or use the helper script:
```bash
./scripts/run-with-simulator.sh [node-count] [rate] [time-limit]
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Maelstrom Harness                       │
│  (Simulates network, injects faults, checks correctness)    │
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

## Message Translation

| Maelstrom RPC | Simulator HTTP Endpoint |
|---------------|-------------------------|
| `broadcast` | `POST /api/roj/propose` |
| `read` | `GET /api/roj/state?nodeId=N` |
| `topology` | Internal tracking (no-op) |

## Configuration

Environment variables:

| Variable | Default | Description |
|----------|---------|-------------|
| `SIMULATOR_URL` | `http://localhost:8001` | Simulator API base URL |

## Simulator API Reference

The bridge uses these simulator endpoints:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | Health check |
| `/api/roj/propose` | POST | Submit proposal via node |
| `/api/roj/state` | GET | Get node committed state |
| `/api/roj/history` | GET | Get Elle history (JSON) |
| `/api/roj/history/clear` | POST | Clear history |

See [ELLE-INTEGRATION.md](../ELLE-INTEGRATION.md) for full API documentation.

## Exporting Elle History

After running a test, export the Elle history for further analysis:

```bash
# Export history
curl http://localhost:8001/api/roj/history > elle-history.json

# Verify with Elle
roj-elle check --history elle-history.json --model list-append
```

## Project Structure

```
maelstrom-sim-bridge/
├── go.mod
├── README.md
├── main.go                    # Main bridge binary
├── internal/
│   ├── adapter/
│   │   └── simulator.go       # HTTP client for simulator API
│   └── protocol/
│       └── messages.go        # Maelstrom message types
└── scripts/
    └── run-with-simulator.sh  # Test runner script
```

## Troubleshooting

### Simulator not available

```
ERROR: Simulator not running at http://localhost:8001
```

Start the simulator:
```bash
cd simulator/engine && go run ./cmd/simulator
```

### Empty reads

If reads return empty, check simulator logs for proposal failures. The bridge returns empty arrays on errors rather than failing.

### Missing values

Maelstrom's checker will report missing values if proposals don't commit. This could indicate:
- Network issues between bridge and simulator
- Simulator consensus failures
- CAN bus message loss (realistic fault)

## Related Documentation

- [roj-maelstrom](../roj-maelstrom/) - Direct Maelstrom testing (no simulator)
- [Elle Integration](../ELLE-INTEGRATION.md) - Full Elle testing documentation
- [Simulator ROJ API](../../../../simulator/engine/internal/roj/) - Go simulator implementation

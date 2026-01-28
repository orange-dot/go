# ROJ-OCPP Adapter

OCPP 2.0.1 adapter for the ROJ distributed consensus protocol. Bridges OCPP
commands from a Central System Management System (CSMS) to ROJ federation,
enabling consistent state across multiple charging points.

## Architecture

```
OCPP CSMS (Backend)
      │ WebSocket JSON-RPC
      ▼
┌─────────────────────────────────┐
│    roj-adapter-ocpp             │
│  ┌───────────┐  ┌────────────┐  │
│  │ OCPP WS   │◄►│ Translator │  │
│  │ Client    │  │ OCPP↔ROJ   │  │
│  └───────────┘  └────────────┘  │
└─────────────┬───────────────────┘
              │ UDP JSON
              ▼
┌─────────────────────────────────┐
│    roj-core-rs                  │
│  Discovery │ Transport │ Consensus
└─────────────────────────────────┘
              │
   ┌──────────┼──────────┐
 alpha      beta      gamma
 (Rust)     (Go)       (C)
```

## OCPP → ROJ Message Mapping

| OCPP Action | ROJ Key | ROJ Value | Consensus? |
|-------------|---------|-----------|------------|
| SetChargingProfile | `power_limit:{station}:{evse}` | `{limit_kw, stack_level}` | Yes |
| RequestStartTransaction | `session:{station}:{evse}` | `{active:true, id_token}` | Yes |
| RequestStopTransaction | `session:{station}:{evse}` | `{active:false}` | Yes |
| ReserveNow | `reservation:{station}:{evse}` | `{expiry, id_token}` | Yes |
| CancelReservation | `reservation:{station}:{evse}` | null | Yes |
| BootNotification | (ANNOUNCE only) | - | No |
| MeterValues | (state query) | - | No |
| StatusNotification | (derived from state) | - | No |

## Usage

### As Library

```rust
use roj_adapter_ocpp::{Adapter, AdapterConfig};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let config = AdapterConfig::new(
        "alpha",                    // node ID
        "CS001",                    // station ID
        "ws://localhost:8180/ocpp", // CSMS URL
    )
    .with_vendor("Elektrokombinacija", "EK3-OCPP")
    .with_evse_count(2)
    .with_roj_port(9990);

    let adapter = Adapter::new(config).await?;
    adapter.run().await?;

    Ok(())
}
```

### As CLI

```bash
# Start node with defaults
roj-ocpp-node --name alpha --station CS001

# Custom CSMS and port
roj-ocpp-node --name alpha --station CS001 \
    --ocpp-url ws://csms.example.com/ocpp \
    --roj-port 9991

# With static peers (no mDNS)
roj-ocpp-node --name alpha --station CS001 \
    --no-mdns \
    --peer 192.168.1.10:9990 \
    --peer 192.168.1.11:9990
```

## Demo Scenario

### Prerequisites

- Docker (for SteVe OCPP CSMS)
- Rust toolchain

### Steps

1. **Start SteVe OCPP CSMS** (mock backend):
   ```bash
   docker run -d -p 8180:8180 --name steve steve-community/steve
   ```

2. **Start ROJ-OCPP nodes** (in separate terminals):
   ```bash
   # Terminal 1
   cargo run -p roj-ocpp-node -- --name alpha --roj-port 9990

   # Terminal 2
   cargo run -p roj-ocpp-node -- --name beta --roj-port 9991

   # Terminal 3
   cargo run -p roj-ocpp-node -- --name gamma --roj-port 9992
   ```

3. **Send OCPP command from CSMS** (e.g., SetChargingProfile):
   - Alpha receives SetChargingProfile from CSMS
   - Alpha translates to ROJ PROPOSE: `power_limit:CS001:1 = {limit_kw: 15}`
   - Beta and Gamma vote ACCEPT
   - All nodes COMMIT the same power limit
   - All nodes have consistent state

### Expected Output

```
# On alpha (receiving node):
INFO Received OCPP SetChargingProfile for EVSE 1
INFO Translating to ROJ PROPOSE: power_limit:CS001:1 = 15.0 kW
INFO Broadcasting proposal to 2 peers

# On beta and gamma:
INFO Received PROPOSE from alpha: power_limit:CS001:1 = 15.0
INFO Voting ACCEPT (2/3 threshold)

# On all nodes:
INFO COMMIT power_limit:CS001:1 = 15.0 kW (voters: [alpha, beta, gamma])
INFO Power limit changed on EVSE 1: 15.0 kW
```

## Module Structure

```
roj-adapter-ocpp/
├── src/
│   ├── lib.rs              # Library exports
│   ├── adapter.rs          # Main adapter loop
│   ├── config.rs           # Configuration
│   ├── ocpp/
│   │   ├── mod.rs
│   │   ├── types.rs        # OCPP 2.0.1 message types
│   │   ├── messages.rs     # JSON-RPC framing
│   │   ├── client.rs       # WebSocket client
│   │   └── session.rs      # Session state machine
│   └── roj/
│       ├── mod.rs
│       ├── translator.rs   # OCPP↔ROJ mapping
│       └── state_bridge.rs # State queries
└── Cargo.toml
```

## Dependencies

- `roj-core`: ROJ consensus protocol
- `tokio-tungstenite`: WebSocket client
- `serde` / `serde_json`: JSON serialization
- `chrono`: Datetime handling

## Testing

```bash
# Unit tests
cargo test -p roj-adapter-ocpp

# Integration test (requires running CSMS)
cargo run --example ocpp_integration_test
```

## License

AGPL-3.0 - See LICENSE file

# EK-ROJ Code Structure Plan

**Status:** PLANNING
**Location:** `d:\work\autobusi-punjaci\mapf-het-research\ek-roj\`
**Goal:** Minimum viable demo for NLnet credibility

---

## Target: What We Need to Demo

```
DEMO SCENARIO:
═══════════════════════════════════════════════════════════════

Terminal 1:                Terminal 2:                Terminal 3:
┌──────────────┐          ┌──────────────┐          ┌──────────────┐
│  roj-node    │          │  roj-node    │          │  roj-node    │
│  "alpha"     │◄────────►│  "beta"      │◄────────►│  "gamma"     │
│              │          │              │          │              │
│  mDNS disco  │          │  mDNS disco  │          │  mDNS disco  │
│  consensus   │          │  consensus   │          │  consensus   │
└──────────────┘          └──────────────┘          └──────────────┘

$ roj-node --name alpha
[INFO] Starting ROJ node "alpha"
[INFO] mDNS: Announcing _roj._udp.local
[INFO] mDNS: Discovered peer "beta" at 192.168.1.42:9990
[INFO] mDNS: Discovered peer "gamma" at 192.168.1.43:9990
[INFO] Consensus: Forming k=3 neighborhood (demo mode)
[INFO] Consensus: Proposal from beta - VOTE: accept
[INFO] Consensus: Committed state #1
```

---

## Proposed Folder Structure

```
ek-roj/
├── Cargo.toml                 # Workspace root
├── README.md                  # Project overview
├── LICENSE                    # AGPL-3.0
│
├── roj-core/                  # Core protocol library
│   ├── Cargo.toml
│   └── src/
│       ├── lib.rs
│       ├── discovery/         # Peer discovery
│       │   ├── mod.rs
│       │   └── mdns.rs        # mDNS implementation
│       ├── trust/             # Web-of-trust (stub for demo)
│       │   └── mod.rs
│       ├── consensus/         # k-threshold voting
│       │   ├── mod.rs
│       │   ├── message.rs     # Consensus messages
│       │   └── voter.rs       # Voting logic
│       ├── transport/         # Network transport
│       │   ├── mod.rs
│       │   └── udp.rs         # Simple UDP for demo
│       └── types.rs           # Common types (NodeId, etc.)
│
├── roj-node/                  # Demo CLI node
│   ├── Cargo.toml
│   └── src/
│       └── main.rs            # CLI entry point
│
└── examples/
    └── three_nodes.sh         # Script to run 3-node demo
```

---

## Minimum Code for Demo (~1,000 LoC)

### Phase 1: Discovery (mDNS) - ~300 LoC

| File | LoC | Description |
|------|-----|-------------|
| `discovery/mod.rs` | 30 | Module exports |
| `discovery/mdns.rs` | 250 | mDNS announce + discover |
| `types.rs` | 50 | NodeId, PeerInfo |

**Dependencies:**
- `mdns-sd` crate (pure Rust mDNS)

**Demo output:**
```
[INFO] mDNS: Announcing _roj._udp.local
[INFO] mDNS: Discovered peer "beta" at 192.168.1.42:9990
```

### Phase 2: Transport (UDP) - ~200 LoC

| File | LoC | Description |
|------|-----|-------------|
| `transport/mod.rs` | 20 | Module exports |
| `transport/udp.rs` | 180 | UDP send/recv with framing |

**Dependencies:**
- `tokio` (async runtime)

### Phase 3: Consensus Messages - ~250 LoC

| File | LoC | Description |
|------|-----|-------------|
| `consensus/mod.rs` | 30 | Module exports |
| `consensus/message.rs` | 120 | PROPOSE, VOTE, COMMIT messages |
| `consensus/voter.rs` | 100 | k-threshold voting logic |

**Demo output:**
```
[INFO] Consensus: Received PROPOSE from beta
[INFO] Consensus: VOTE accept (threshold 2/3)
[INFO] Consensus: COMMIT state #1
```

### Phase 4: CLI Node - ~200 LoC

| File | LoC | Description |
|------|-----|-------------|
| `roj-node/src/main.rs` | 200 | CLI args, logging, main loop |

**Dependencies:**
- `clap` (CLI parsing)
- `tracing` (logging)

---

## Dependencies (Cargo.toml)

```toml
[workspace]
members = ["roj-core", "roj-node"]

[workspace.dependencies]
tokio = { version = "1", features = ["full"] }
mdns-sd = "0.10"
serde = { version = "1", features = ["derive"] }
serde_json = "1"
tracing = "0.1"
tracing-subscriber = "0.3"
clap = { version = "4", features = ["derive"] }
thiserror = "1"
```

---

## Implementation Order

### Today (Jan 23) - Core Demo

1. [ ] Create folder structure + Cargo.toml
2. [ ] Implement `types.rs` (NodeId, PeerInfo)
3. [ ] Implement `discovery/mdns.rs` (announce + discover)
4. [ ] Implement `transport/udp.rs` (basic send/recv)
5. [ ] Implement `consensus/message.rs` (PROPOSE, VOTE, COMMIT)
6. [ ] Implement `consensus/voter.rs` (k-threshold logic)
7. [ ] Implement `roj-node/main.rs` (CLI)
8. [ ] Test 3-node demo on localhost

### Tomorrow (Jan 24) - Polish

- [ ] Add README.md with usage
- [ ] Add LICENSE (AGPL-3.0)
- [ ] Clean up logging output
- [ ] Test on multiple machines (if time)

---

## Message Format (JSON for simplicity)

```json
// ANNOUNCE (mDNS TXT record or UDP broadcast)
{
  "type": "ANNOUNCE",
  "node_id": "alpha",
  "capabilities": ["consensus", "discovery"],
  "version": "0.1.0"
}

// PROPOSE
{
  "type": "PROPOSE",
  "proposal_id": "abc123",
  "from": "beta",
  "data": { "key": "value" },
  "timestamp": 1706025600
}

// VOTE
{
  "type": "VOTE",
  "proposal_id": "abc123",
  "from": "alpha",
  "vote": "accept",  // or "reject"
  "signature": "..."  // stub for demo
}

// COMMIT
{
  "type": "COMMIT",
  "proposal_id": "abc123",
  "state_hash": "sha256:...",
  "votes": ["alpha", "gamma"]
}
```

---

## Success Criteria

**Demo works when:**

1. ✅ 3 nodes start on same LAN
2. ✅ Nodes discover each other via mDNS
3. ✅ One node proposes state change
4. ✅ Other nodes vote (threshold voting)
5. ✅ Commit is broadcast when threshold reached
6. ✅ All nodes show consistent state

**Stretch goals:**

- [ ] Node join/leave handling
- [ ] Basic persistence (state survives restart)
- [ ] Simple web UI showing node status

---

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| mDNS doesn't work on Windows | Test on Linux VM, or use UDP broadcast fallback |
| Tokio complexity | Keep async minimal, use blocking where OK |
| Time pressure | Focus on demo, skip error handling edge cases |

---

*Plan created: January 23, 2026*

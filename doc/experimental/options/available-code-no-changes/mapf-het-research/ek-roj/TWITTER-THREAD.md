# EK-ROJ: Distributed Consensus for Heterogeneous EV Charging Networks

Distributed consensus in heterogeneous embedded networks remains an open problem. Most protocols assume homogeneous implementations. EK-ROJ explores k-threshold voting across Rust, Go, and C nodes using mDNS discovery and UDP transport.

## Protocol

Four message types over UDP (port 9990): ANNOUNCE (periodic heartbeat with node metadata), PROPOSE (state change request with unique ID), VOTE (accept/reject from each peer), and COMMIT (broadcast when threshold reached).

Nodes discover each other via mDNS, then any node can initiate a proposal. Each peer votes accept or reject. When ⌈n × 0.67⌉ votes are collected, the proposer broadcasts COMMIT and all nodes apply the state change.

Message format is JSON with schema validation (JSON Schema v7). Each message contains type, node ID, and payload specific to message type.

## Architecture

Peer discovery via mDNS service `_roj._udp.local`. TXT records carry language identifier (rust/go/c), version (semver), and capability list.

Three reference implementations exist: Rust (~300 LoC) using Tokio async runtime with mdns-sd and RwLock-protected state; Go (~250 LoC) using goroutines with hashicorp/mdns and channel-based dispatch; C (~400 LoC) using select() event loop with static allocation for embedded targets.

## Implementation Constraints

The C implementation targets STM32G474 microcontrollers. Constraints include static allocation (no malloc at runtime), maximum 16 concurrent proposals, 64 key-value state entries, and select()-based event loop on POSIX or polling on Windows.

Rust uses Tokio async runtime with RwLock-protected peer state and structured logging via tracing. Go uses one goroutine per subsystem with channel-based event dispatch.

## Consensus Properties

Liveness: any node can propose. Safety: ⌈n × 0.67⌉ threshold required for commit. Partition tolerance: proposals timeout after 10 seconds.

Not Byzantine fault tolerant in current form due to absence of signature verification.

## Limitations

No persistent state. No cryptographic authentication. Fixed quorum formula. Best-effort UDP delivery. mDNS assumes LAN broadcast domain.

Suitable for controlled environments such as test rigs and single-site deployments.

## Application

Target application is modular EV charging coordination. N modules agree on power distribution without central controller. Protocol enables consensus across mixed-vendor firmware implementations.

---

Related work: Raft, PBFT, CRDTs

#DistributedSystems #Consensus #EmbeddedSystems #Rust #Golang #IoT

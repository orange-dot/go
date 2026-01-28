# EK-KOR v2 Gossip Protocol Specification

## Overview

The EK-KOR Gossip Protocol enables federated event sourcing across distributed
EK3 charger modules. Events are propagated using epidemic (gossip) dissemination
with causal ordering via version vectors.

## Key Properties

| Property | Value |
|----------|-------|
| **Delivery** | At-least-once |
| **Ordering** | Causal (version vectors) |
| **Conflict Resolution** | Last-Writer-Wins (LWW) |
| **Topology** | k=7 neighbors (matches EKK) |
| **TTL** | 3 hops max |
| **Message Size** | ≤64 bytes (CAN-FD compatible) |

## Architecture

```
┌──────────────────────────────────────────────────────────────────────────┐
│                        EVENT GOSSIP MESH                                  │
│                                                                          │
│   ┌─────────┐        ┌─────────┐        ┌─────────┐        ┌─────────┐  │
│   │Module 1 │◄──────►│Module 2 │◄──────►│Module 3 │◄──────►│Module 4 │  │
│   │ VV[1]=5 │        │ VV[2]=3 │        │ VV[3]=7 │        │ VV[4]=2 │  │
│   │ k=7     │        │ k=7     │        │ k=7     │        │ k=7     │  │
│   └────┬────┘        └────┬────┘        └────┬────┘        └────┬────┘  │
│        │                  │                  │                  │       │
│        └──────────────────┼──────────────────┼──────────────────┘       │
│                           │                  │                          │
│                     CAN-FD @ 5 Mbps                                     │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

## Data Structures

### Event V2 (36 bytes)

Extended event format with origin tracking for causal deduplication:

```c
typedef struct EKK_PACKED {
    uint32_t sequence;        // Local sequence (compat)
    uint32_t timestamp_us;    // Event timestamp
    uint8_t  event_type;      // Event type code
    uint8_t  flags;           // Event flags
    uint8_t  origin_id;       // Original emitter
    uint8_t  hop_count;       // Hops from origin (TTL)
    uint32_t origin_seq;      // Sequence at origin
    uint8_t  payload[20];     // Event payload
} ekk_event_v2_t;             // 36 bytes
```

### Version Vector

Tracks causal dependencies using per-module sequence counters:

```
VV[module_id] = highest_sequence_seen_from_module
```

**Comparison**:
- VV_a > VV_b: a happened after b (a dominates)
- VV_a < VV_b: a happened before b (b dominates)
- VV_a || VV_b: concurrent (neither dominates)

**Merge**: Element-wise maximum
```
merged[i] = max(local[i], remote[i])
```

### LWW Timestamp

Last-Writer-Wins conflict resolution:

```c
typedef struct {
    uint32_t timestamp_us;    // Higher wins
    uint8_t  origin_id;       // Tiebreaker (higher wins)
} ekk_lww_timestamp_t;
```

## Message Types

### 0x60: EVENT_GOSSIP

Batch of events for gossip propagation:

```c
typedef struct EKK_PACKED {
    uint8_t msg_type;         // 0x60
    uint8_t source_module;    // Sender ID
    uint8_t event_count;      // 1-2 events
    uint8_t vv_summary[7];    // Compressed VV
    ekk_event_v2_t events[2]; // Event batch
} ekk_gossip_msg_t;
```

### 0x61: EVENT_ACK

Acknowledgment of received events:

```c
typedef struct EKK_PACKED {
    uint8_t msg_type;         // 0x61
    uint8_t source_module;    // Acking module
    uint8_t ack_count;        // Number of acks
    uint32_t acked_seqs[4];   // Acked sequences
} ekk_gossip_ack_t;
```

### 0x62: EVENT_REQUEST

Request for missing events (gap fill):

```c
typedef struct EKK_PACKED {
    uint8_t msg_type;         // 0x62
    uint8_t requester;        // Requesting module
    uint8_t target_origin;    // Origin to request from
    uint8_t _reserved;
    uint32_t from_seq;        // First missing
    uint32_t to_seq;          // Last missing
} ekk_gossip_request_t;
```

## Protocol Flow

### 1. Event Emission

```
Module 1 emits event:
  1. Create event with origin_id=1, origin_seq=local_seq++
  2. Update VV[1] = origin_seq
  3. Add to pending buffer
  4. On next tick: send to all k neighbors
```

### 2. Event Reception

```
Module 2 receives event from Module 1:
  1. Check VV[origin_id] vs origin_seq
     - If origin_seq <= VV[origin_id]: DUPLICATE, discard
     - If origin_seq > VV[origin_id] + 1: GAP, buffer & request
     - Else: NEW, accept
  2. Update VV[origin_id] = origin_seq
  3. Store event locally
  4. If hop_count < MAX_HOPS: forward to neighbors (except sender)
  5. Send ACK to sender
```

### 3. Gap Recovery

```
Gap detected (received seq=5, expected seq=3):
  1. Buffer event with seq=5
  2. Send EVENT_REQUEST for seq 3,4 to sender
  3. When missing events arrive, unbuffer seq=5
```

### 4. Anti-Entropy Sync

Periodic full sync to repair diverged replicas:

```
Every 10 seconds:
  1. Send full VV to random neighbor
  2. Compare VVs:
     - For entries where local > remote: send events
     - For entries where remote > local: request events
```

## Hop Limiting (TTL)

Prevents infinite propagation in mesh networks:

```
On receive:
  if hop_count >= MAX_HOPS:
    Store locally but do NOT forward

On forward:
  event.hop_count++
```

With k=7 neighbors and MAX_HOPS=3:
- Worst case reach: 7³ = 343 modules
- In practice, VV deduplication prevents most re-forwards

## Conflict Resolution (LWW)

When concurrent events modify same state:

```c
bool ekk_lww_is_newer(a, b) {
    if (a.timestamp != b.timestamp)
        return a.timestamp > b.timestamp;
    return a.origin_id > b.origin_id;  // Deterministic tiebreak
}
```

**Properties**:
- Deterministic: Same inputs → same winner
- Total ordering: Any two events comparable
- Commutative: Order of comparison doesn't matter

## Event Types

| Code | Name | Description |
|------|------|-------------|
| 0x01 | STATE_TRANSITION | Module state change |
| 0x02 | FIELD_PUBLISHED | Field value update |
| 0x03 | NEIGHBOR_JOINED | New neighbor detected |
| 0x04 | NEIGHBOR_LEFT | Neighbor lost |
| 0x05 | CONSENSUS_START | Ballot initiated |
| 0x06 | CONSENSUS_VOTE | Vote cast |
| 0x07 | CONSENSUS_RESULT | Ballot result |
| 0x80+ | USER_DEFINED | Application events |

## Performance Characteristics

| Metric | Value |
|--------|-------|
| Propagation latency | O(hops × tick_interval) |
| Storage per event | 36 bytes (event) + 16 bytes (index) |
| VV overhead | 8 bytes per tracked module |
| Bandwidth per tick | ≤ 164 bytes (2 events + header) |

## Failure Modes

### Network Partition

- Events buffered locally
- On reconnect: anti-entropy sync repairs divergence
- No events lost (at-least-once delivery)

### Module Crash

- Events in SRAM may be lost
- Flash-backed events survive
- VV recovers from last persisted state

### Byzantine Behavior

- Not tolerated (trusted network assumption)
- For untrusted: add HMAC to events (ekk_auth.h)

## References

- Demers et al. (1987): "Epidemic Algorithms for Replicated Database Maintenance"
- Shapiro et al. (2011): CRDTs - Conflict-free Replicated Data Types
- Lamport (1978): Time, Clocks, and the Ordering of Events

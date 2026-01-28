# EK-KOR v2 Complete Implementation Plan

## Overview

Full implementation upgrade of EK-KOR v2 based on technical refinements from:
- `opus-init-research.md` - Academic research synthesis
- `ek-kor-rtos-technical-report.md` - Production architecture specification

**Current State:**
- C: Complete API (8 headers, ~250 functions) - **Phase 1 mostly complete**
- Rust: ~85% complete algorithms - **STM32G474 HAL skeleton only**
- Spec: Complete with 22 JSON test vectors

**Target State:**
- Both C and Rust fully implemented and cross-validated
- All research refinements integrated
- Production-ready for STM32G474 deployment

---

## Key Improvements from Research

| Feature | Current | Target |
|---------|---------|--------|
| Gradient storage | Q16.16 | Q15 (SIMD-optimized) |
| Field consistency | Atomic flags | Sequence counter pattern |
| IPC mechanism | None | SPSC ring buffers (<100ns) |
| Authentication | None | Chaskey MAC (1-2μs) |
| Node roles | Single type | ChargerModule / SegmentGateway |
| Memory layout | Generic | CCM/SRAM1/SRAM2 sections |
| Message loop | Missing | Full RX/TX integration |

---

# Phase 1: C Core Implementation [MOSTLY COMPLETE]

**Goal:** Implement all C source files to achieve API parity with headers

**Status:** C code builds on both MSVC (Windows) and GCC (Linux/Docker), tests pass.

## 1.1 Type System & Utilities

**File:** `c/src/ekk_types.c`

Contents:
- Static assertions for struct sizes
- Q16.16 ↔ Q15 conversion functions
- Fixed-point multiply/divide implementations
- String conversion helpers for debug output

```c
// Fixed-point multiplication (Q16.16 × Q16.16 → Q16.16)
ekk_fixed_t ekk_fixed_mul(ekk_fixed_t a, ekk_fixed_t b) {
    int64_t result = ((int64_t)a * (int64_t)b) >> 16;
    return (ekk_fixed_t)result;
}

// Exponential decay approximation (piecewise linear)
ekk_fixed_t ekk_fixed_exp_decay(ekk_time_us_t elapsed, ekk_time_us_t tau) {
    if (elapsed < tau) {
        return EKK_FIXED_ONE - ekk_fixed_div(elapsed << 16, tau << 16);
    } else if (elapsed < tau * 2) {
        return EKK_FIXED_HALF - ekk_fixed_div((elapsed - tau) << 16, (tau * 2) << 16);
    } else if (elapsed < tau * 3) {
        return EKK_FIXED_QUARTER - ekk_fixed_div((elapsed - tau*2) << 16, (tau * 4) << 16);
    }
    return 0;
}
```

## 1.2 Field Module

**File:** `c/src/ekk_field.c`

Contents:
- Field region initialization
- Atomic field publish with sequence counter
- Field sampling with decay calculation
- Gradient computation (single and batch)
- Neighbor field aggregation
- Garbage collection for stale fields

Key functions:
```c
ekk_error_t ekk_field_publish(ekk_module_id_t id, const ekk_field_t *field);
ekk_error_t ekk_field_sample(ekk_module_id_t target, ekk_field_t *out);
ekk_error_t ekk_field_sample_neighbors(const ekk_neighbor_t *neighbors,
                                        uint8_t count, ekk_field_t *aggregate);
ekk_fixed_t ekk_field_gradient(const ekk_field_t *my_field,
                                const ekk_field_t *neighbor_agg,
                                ekk_field_component_t component);
```

## 1.3 Heartbeat Module

**File:** `c/src/ekk_heartbeat.c`

Contents:
- Per-neighbor health state tracking
- State machine: Unknown → Alive → Suspect → Dead
- Missed heartbeat counting
- Callback invocation on state transitions
- Heartbeat message creation

Key functions:
```c
ekk_error_t ekk_heartbeat_add_neighbor(ekk_heartbeat_t *hb, ekk_module_id_t id);
ekk_error_t ekk_heartbeat_received(ekk_heartbeat_t *hb, ekk_module_id_t sender,
                                    uint8_t sequence, ekk_time_us_t now);
ekk_error_t ekk_heartbeat_tick(ekk_heartbeat_t *hb, ekk_time_us_t now);
ekk_health_state_t ekk_heartbeat_get_health(ekk_heartbeat_t *hb, ekk_module_id_t id);
```

## 1.4 Topology Module

**File:** `c/src/ekk_topology.c`

Contents:
- Discovery message processing
- Distance calculation (logical, physical, latency, custom)
- k-nearest neighbor selection algorithm
- Reelection on neighbor loss
- Topology change callbacks

Key algorithm (reelection):
```c
uint32_t ekk_topology_reelect(ekk_topology_t *topo) {
    // 1. Compute distances to all known modules
    for (int i = 0; i < topo->known_count; i++) {
        topo->known[i].distance = compute_distance(topo, topo->known[i].id);
    }

    // 2. Sort by distance (insertion sort for small N)
    sort_by_distance(topo->known, topo->known_count);

    // 3. Take k nearest as new neighbors
    uint32_t changes = 0;
    for (int i = 0; i < EKK_K_NEIGHBORS && i < topo->known_count; i++) {
        if (topo->neighbors[i].id != topo->known[i].id) {
            changes++;
        }
        topo->neighbors[i] = topo->known[i];
    }

    return changes;
}
```

## 1.5 Consensus Module

**File:** `c/src/ekk_consensus.c`

Contents:
- Ballot creation and management
- Vote collection and counting
- Threshold evaluation (simple, supermajority, unanimous)
- Mutual inhibition logic
- Timeout handling
- Result callbacks

Key functions:
```c
ekk_error_t ekk_consensus_propose(ekk_consensus_t *cons,
                                   ekk_proposal_type_t type,
                                   uint32_t data, ekk_fixed_t threshold,
                                   ekk_ballot_id_t *ballot_id);
ekk_error_t ekk_consensus_on_vote(ekk_consensus_t *cons,
                                   ekk_ballot_id_t ballot_id,
                                   ekk_module_id_t voter,
                                   ekk_vote_value_t vote,
                                   ekk_time_us_t now);
ekk_error_t ekk_consensus_inhibit(ekk_consensus_t *cons, ekk_ballot_id_t ballot_id);
ekk_error_t ekk_consensus_tick(ekk_consensus_t *cons, ekk_time_us_t now);
```

## 1.6 Module Integration

**File:** `c/src/ekk_module.c`

Contents:
- Module initialization
- Main tick loop with 8 phases
- Internal task management
- Gradient-based task selection
- State machine transitions
- Callback orchestration

Main tick sequence:
```c
ekk_error_t ekk_module_tick(ekk_module_t *mod, ekk_time_us_t now) {
    // Phase 1: Process incoming messages
    process_rx_messages(mod, now);

    // Phase 2: Update heartbeats, detect failures
    ekk_heartbeat_tick(&mod->heartbeat, now);

    // Phase 3: Sample neighbor fields
    ekk_field_t neighbor_agg;
    ekk_field_sample_neighbors(mod->topology.neighbors,
                                EKK_K_NEIGHBORS, &neighbor_agg);

    // Phase 4: Compute gradients
    for (int i = 0; i < EKK_FIELD_COUNT; i++) {
        mod->gradients[i] = ekk_field_gradient(&mod->my_field,
                                                &neighbor_agg, i);
    }

    // Phase 5: Update consensus (check timeouts)
    ekk_consensus_tick(&mod->consensus, now);

    // Phase 6: Update topology (send discovery if due)
    ekk_topology_tick(&mod->topology, now);

    // Phase 7: Select and run task based on gradients
    ekk_internal_task_t *task = select_task(mod);
    if (task) {
        task->function(task->arg);
    }

    // Phase 8: Publish updated field
    ekk_field_publish(mod->id, &mod->my_field);

    // Phase 9: Update state from topology
    update_module_state(mod);

    return EKK_OK;
}
```

## 1.7 System Initialization

**File:** `c/src/ekk_init.c`

Contents:
- Global field region allocation
- HAL initialization
- Default configuration

```c
static ekk_field_region_t g_field_region;

ekk_error_t ekk_init(void) {
    ekk_error_t err = ekk_hal_init();
    if (err != EKK_OK) return err;

    ekk_field_init(&g_field_region);
    return EKK_OK;
}

ekk_field_region_t* ekk_get_field_region(void) {
    return &g_field_region;
}
```

## 1.8 POSIX HAL

**File:** `c/src/hal/ekk_hal_posix.c`

Contents:
- `clock_gettime()` for microsecond timestamps
- `usleep()` for delays
- Message queues using pthread mutexes (for testing only)
- Atomic operations via GCC builtins
- Printf for debug output

---

# Phase 2: Cross-Validation Infrastructure

**Goal:** Ensure C and Rust implementations produce identical outputs

## 2.1 Test Harness for C

**File:** `c/test/test_harness.c`

Contents:
- JSON parsing for test vectors
- Function dispatch based on test `function` field
- Output comparison against expected values
- Result reporting

## 2.2 Test Harness for Rust

**File:** `rust/tests/test_harness.rs`

Contents:
- JSON deserialization with serde
- Test vector execution
- Output serialization for comparison

## 2.3 Cross-Validation Runner

**File:** `tools/run_tests.py` (enhance existing)

Workflow:
1. Build C test harness
2. Build Rust test harness
3. For each JSON test vector:
   - Execute C implementation, capture output
   - Execute Rust implementation, capture output
   - Compare outputs
4. Report discrepancies

## 2.4 Output Comparison Tool

**File:** `tools/compare_outputs.py` (enhance existing)

Features:
- Recursive JSON comparison
- Floating-point tolerance for Q16.16 values
- Detailed diff reporting

---

# Phase 3: Q15 Gradient Optimization

**Goal:** Add Q15 type for SIMD-friendly gradient storage

## 3.1 C Type Additions

**File:** `c/include/ekk/ekk_types.h` (modify)

```c
// Q15 fixed-point: 1 sign bit, 15 fractional bits
// Range: [-1.0, +0.99997] with ~0.00003 resolution
typedef int16_t ekk_q15_t;

#define EKK_Q15_ONE      0x7FFF   // +0.99997
#define EKK_Q15_HALF     0x4000   // +0.5
#define EKK_Q15_ZERO     0x0000
#define EKK_Q15_NEG_ONE  0x8000   // -1.0

// Conversion: Q16.16 → Q15 (loses precision, saturates)
static inline ekk_q15_t ekk_fixed_to_q15(ekk_fixed_t f) {
    int32_t shifted = f >> 1;  // Q16.16 → Q1.15 range
    if (shifted > 0x7FFF) return 0x7FFF;
    if (shifted < -0x8000) return -0x8000;
    return (ekk_q15_t)shifted;
}

// Conversion: Q15 → Q16.16
static inline ekk_fixed_t ekk_q15_to_fixed(ekk_q15_t q) {
    return ((ekk_fixed_t)q) << 1;
}

// SIMD-friendly gradient array
typedef struct {
    ekk_q15_t components[EKK_FIELD_COUNT];
} ekk_gradient_t;
```

## 3.2 Rust Type Additions

**File:** `rust/src/types.rs` (modify)

```rust
/// Q15 fixed-point: 1 sign + 15 fractional bits
/// Range: [-1.0, +0.99997]
pub type Q15 = i16;

pub const Q15_ONE: Q15 = 0x7FFF;
pub const Q15_HALF: Q15 = 0x4000;
pub const Q15_ZERO: Q15 = 0;
pub const Q15_NEG_ONE: Q15 = -0x8000;

/// Convert Q16.16 Fixed to Q15 (saturating)
pub fn fixed_to_q15(f: Fixed) -> Q15 {
    let bits = f.to_bits() >> 1;
    bits.clamp(-0x8000, 0x7FFF) as Q15
}

/// Convert Q15 to Q16.16 Fixed
pub fn q15_to_fixed(q: Q15) -> Fixed {
    Fixed::from_bits((q as i32) << 1)
}

/// Gradient array using Q15 for SIMD optimization
#[derive(Clone, Copy, Debug, Default)]
pub struct Gradient {
    pub components: [Q15; FIELD_COUNT],
}
```

## 3.3 Module Updates

Modify `ekk_module_t` / `Module` to store gradients as `ekk_gradient_t` / `Gradient`:

```c
// In ekk_module_t:
ekk_gradient_t gradients;  // Was: ekk_fixed_t gradients[EKK_FIELD_COUNT]
```

## 3.4 SIMD Optimization (C only, Cortex-M4)

**File:** `c/src/ekk_field.c`

```c
#ifdef __ARM_FEATURE_SIMD32
// Compute 2 Q15 gradients in parallel using SIMD
void ekk_gradient_simd2(const ekk_q15_t *my, const ekk_q15_t *neighbor,
                         ekk_q15_t *out) {
    uint32_t my_pair = *(uint32_t*)my;
    uint32_t neigh_pair = *(uint32_t*)neighbor;
    uint32_t result = __SSUB16(neigh_pair, my_pair);
    *(uint32_t*)out = result;
}
#endif
```

---

# Phase 4: Sequence Counter & Consistent Reads

**Goal:** Add atomic consistency pattern for field reads

## 4.1 CoordinationField Structure

**C (`c/include/ekk/ekk_field.h` modify):**

```c
// Enhanced field with sequence counter for lock-free consistency
typedef struct {
    ekk_field_t field;
    volatile uint32_t sequence;  // Odd = write in progress, Even = stable
} ekk_coord_field_t;

// Field region now uses CoordinationField
typedef struct {
    ekk_coord_field_t fields[EKK_MAX_MODULES];
    volatile uint32_t update_flags[(EKK_MAX_MODULES + 31) / 32];
    ekk_time_us_t last_gc;
} ekk_field_region_t;
```

## 4.2 Consistent Read Implementation

**C (`c/src/ekk_field.c`):**

```c
// Write with sequence increment
ekk_error_t ekk_field_publish(ekk_module_id_t id, const ekk_field_t *field) {
    ekk_coord_field_t *cf = &g_region.fields[id];

    // Increment sequence to odd (write in progress)
    uint32_t seq = cf->sequence;
    cf->sequence = seq + 1;
    __DMB();  // Data memory barrier

    // Copy field data
    cf->field = *field;

    __DMB();
    // Increment sequence to even (write complete)
    cf->sequence = seq + 2;

    // Set update flag
    uint32_t word = id / 32;
    uint32_t bit = 1u << (id % 32);
    g_region.update_flags[word] |= bit;

    return EKK_OK;
}

// Read with consistency check
bool ekk_field_read_consistent(ekk_module_id_t id, ekk_field_t *out) {
    ekk_coord_field_t *cf = &g_region.fields[id];

    uint32_t seq_before = cf->sequence;
    if (seq_before & 1) return false;  // Write in progress

    __DMB();
    *out = cf->field;
    __DMB();

    uint32_t seq_after = cf->sequence;
    return (seq_before == seq_after);
}

// Sample with retry on inconsistency
ekk_error_t ekk_field_sample(ekk_module_id_t id, ekk_field_t *out,
                              ekk_time_us_t now) {
    for (int retry = 0; retry < 3; retry++) {
        if (ekk_field_read_consistent(id, out)) {
            ekk_field_apply_decay(out, now);
            return EKK_OK;
        }
    }
    return EKK_ERR_BUSY;  // Writer too active
}
```

## 4.3 Rust Implementation

**File:** `rust/src/field.rs` (modify)

```rust
use core::sync::atomic::{AtomicU32, Ordering};

pub struct CoordinationField {
    field: Field,
    sequence: AtomicU32,
}

impl CoordinationField {
    pub fn publish(&self, field: &Field) {
        let seq = self.sequence.load(Ordering::Relaxed);
        self.sequence.store(seq + 1, Ordering::Release);

        cortex_m::asm::dmb();
        // Note: Field copy is not atomic, but sequence protects consistency
        unsafe {
            core::ptr::write_volatile(&self.field as *const _ as *mut _, *field);
        }
        cortex_m::asm::dmb();

        self.sequence.store(seq + 2, Ordering::Release);
    }

    pub fn read_consistent(&self) -> Option<Field> {
        let seq_before = self.sequence.load(Ordering::Acquire);
        if seq_before & 1 != 0 { return None; }

        cortex_m::asm::dmb();
        let field = unsafe { core::ptr::read_volatile(&self.field) };
        cortex_m::asm::dmb();

        let seq_after = self.sequence.load(Ordering::Acquire);
        if seq_before == seq_after { Some(field) } else { None }
    }
}
```

---

# Phase 5: SPSC Ring Buffer

**Goal:** Add zero-copy IPC for CAN message queues

## 5.1 C Implementation

**File:** `c/include/ekk/ekk_spsc.h` (new)

```c
#ifndef EKK_SPSC_H
#define EKK_SPSC_H

#include "ekk_types.h"

// Power-of-2 capacity ring buffer
typedef struct {
    void *buffer;
    volatile uint32_t head;  // Write index (producer only)
    volatile uint32_t tail;  // Read index (consumer only)
    uint32_t mask;           // capacity - 1 (for fast modulo)
    uint32_t item_size;
} ekk_spsc_t;

// Initialize with pre-allocated buffer (capacity must be power of 2)
ekk_error_t ekk_spsc_init(ekk_spsc_t *q, void *buffer,
                           uint32_t capacity, uint32_t item_size);

// Producer: push item (returns ERR_NO_MEMORY if full)
ekk_error_t ekk_spsc_push(ekk_spsc_t *q, const void *item);

// Consumer: pop item (returns ERR_NOT_FOUND if empty)
ekk_error_t ekk_spsc_pop(ekk_spsc_t *q, void *item);

// Query
uint32_t ekk_spsc_len(const ekk_spsc_t *q);
bool ekk_spsc_is_empty(const ekk_spsc_t *q);
bool ekk_spsc_is_full(const ekk_spsc_t *q);

#endif
```

## 5.2 Rust Implementation

**File:** `rust/src/spsc.rs` (new)

Lock-free single-producer single-consumer ring buffer.

## 5.3 HAL Integration

Add to HAL trait:
```rust
fn get_can_rx_queue(&self) -> &SpscRingBuffer<CanFrame, 32>;
fn get_can_tx_queue(&self) -> &SpscRingBuffer<CanFrame, 32>;
```

---

# Phase 6: Chaskey MAC Authentication

**Goal:** Add lightweight message authentication

## 6.1 Chaskey Algorithm Implementation

**File:** `c/include/ekk/ekk_auth.h` (new)

- 128-bit block cipher based on permutation
- 12 rounds for security
- ~1-2μs on Cortex-M4 @ 170MHz

## 6.2 Message Type Configuration

```c
// Configure which message types require authentication
#define EKK_AUTH_REQUIRED_EMERGENCY    1
#define EKK_AUTH_REQUIRED_VOTE         1
#define EKK_AUTH_REQUIRED_PROPOSAL     1
#define EKK_AUTH_REQUIRED_HEARTBEAT    0  // Optional
#define EKK_AUTH_REQUIRED_DISCOVERY    0  // Optional
```

---

# Phase 7: Module Role Support

**Goal:** Support ChargerModule and SegmentGateway roles

## 7.1 Role Enumeration

**C (`ekk_types.h`):**
```c
typedef enum {
    EKK_ROLE_CHARGER_MODULE = 0,
    EKK_ROLE_SEGMENT_GATEWAY = 1,
} ekk_module_role_t;
```

## 7.2 Compile-Time Selection

**CMake:**
```cmake
option(EKK_ROLE_GATEWAY "Build as segment gateway" OFF)
```

---

# Phase 8: STM32G474 HAL Implementation

**Goal:** Complete embedded HAL for target hardware

## 8.1 Memory Layout

```ld
MEMORY {
    FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    CCM    (rwx) : ORIGIN = 0x10000000, LENGTH = 32K
    SRAM1  (rwx) : ORIGIN = 0x20000000, LENGTH = 80K
    SRAM2  (rwx) : ORIGIN = 0x20014000, LENGTH = 16K
}
```

## 8.2 Timer Implementation

TIM2 as 32-bit microsecond counter.

## 8.3 FDCAN Implementation

FDCAN1 initialization for segment bus (1Mbps/5Mbps).

## 8.4 MPU Configuration

Memory protection for kernel/task isolation.

---

# Phase 9: Extended Test Coverage

**Goal:** Comprehensive test vectors and verification

## 9.1 New Test Vector Files

| File | Tests |
|------|-------|
| `spsc_001_basic.json` | Push, pop, empty, full |
| `spsc_002_wraparound.json` | Index wraparound |
| `field_008_consistent.json` | Sequence counter pattern |
| `auth_001_chaskey.json` | MAC computation |
| `q15_001_conversion.json` | Q16.16 ↔ Q15 |

---

# Phase 10: Documentation & Finalization

**Goal:** Complete documentation and prepare for deployment

## 10.1 API Documentation

- Update `spec/api.md` with all new types and functions
- Add `spec/auth.md` for authentication protocol
- Add `spec/gateway.md` for segment gateway specification

---

# Verification Checklist

## Build & Test
```bash
# Build C (Windows)
cd c/build && cmake --build . && ctest -C Debug

# Build C (Docker/Linux)
docker run --rm -v ${PWD}:/workspace ekk-build

# Run Rust tests
cd rust && cargo test
```

## Success Criteria

- [x] C code compiles on MSVC (Windows)
- [x] C code compiles on GCC (Linux/Docker)
- [x] Basic tests pass
- [x] Q15 type system implemented (Phase 3)
- [x] Sequence counter lock-free pattern (Phase 4)
- [x] SPSC ring buffer implemented (Phase 5)
- [x] Chaskey MAC authentication implemented (Phase 6)
- [x] Module role support (Phase 7)
- [x] STM32G474 HAL implemented (Phase 8)
- [x] Extended test vectors added (Phase 9)
- [ ] All 25+ test vectors pass in C
- [ ] All test vectors pass in Rust
- [ ] C and Rust outputs match for all vectors
- [ ] SPSC latency < 100ns (benchmarked)
- [ ] Chaskey MAC computes in < 2μs (benchmarked)

---

# Related Documents

- **GUI Plan:** `gui/PLAN.md` - Electron + React desktop visualization
- **Renode Setup:** `renode/` - STM32G474 emulation scripts
- **Docker Build:** `Dockerfile` - Linux build environment

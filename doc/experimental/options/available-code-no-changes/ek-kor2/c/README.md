# EK-KOR v2: Field-Centric Coordination RTOS

A novel real-time operating system for distributed coordination of modular power electronics.

## Key Innovations

### 1. Potential Field Scheduling

No central scheduler. Modules publish decaying gradient fields; neighbors sample and self-organize.

```c
// Each module publishes its state
ekk_module_update_field(&mod, load, thermal, power);

// Kernel computes gradients automatically
ekk_fixed_t gradient = ekk_module_get_gradient(&mod, EKK_FIELD_LOAD);
if (gradient > THRESHOLD) {
    // Neighbors are overloaded, I should take more work
}
```

**Patent Claim:** "A distributed real-time operating system using potential field scheduling wherein processing elements coordinate through indirect communication via shared decaying gradient fields"

### 2. Topological k-Neighbor Coordination

Each module tracks exactly k=7 neighbors regardless of physical topology. Enables scale-free coordination.

```c
// Kernel maintains neighbor graph automatically
ekk_topology_t topo;
ekk_topology_init(&topo, my_id, position, NULL);

// Neighbors are elected based on logical distance
uint32_t count = ekk_topology_get_neighbors(&topo, neighbors, 7);
```

**Patent Claim:** "A topological coordination protocol for modular power electronics where each module maintains fixed-cardinality neighbor relationships independent of physical network topology"

### 3. Threshold Consensus

Distributed voting with density-dependent thresholds.

```c
// Propose mode change requiring supermajority
ekk_ballot_id_t ballot;
ekk_consensus_propose(&cons, EKK_PROPOSAL_MODE_CHANGE, NEW_MODE,
                      EKK_THRESHOLD_SUPERMAJORITY, &ballot);

// Check result
if (ekk_consensus_get_result(&cons, ballot) == EKK_VOTE_APPROVED) {
    // Consensus reached!
}
```

**Patent Claim:** "A threshold-based consensus mechanism for mixed-criticality embedded systems using density-dependent activation functions"

### 4. Adaptive Mesh Reformation

Kernel-integrated failure detection with automatic neighbor reelection.

```c
// Kernel automatically:
// - Sends heartbeats every 10ms
// - Detects failures (5 missed = 50ms)
// - Calls on_neighbor_lost() callback
// - Triggers topology reelection
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      MODULE LAYER                           │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐        │
│  │ Module  │──│ Module  │──│ Module  │──│ Module  │        │
│  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘        │
│       │ k=7        │            │            │              │
├───────┴────────────┴────────────┴────────────┴──────────────┤
│                   COORDINATION FIELD                         │
│  Shared memory: load_potential[], thermal_gradient[]        │
├─────────────────────────────────────────────────────────────┤
│                    KERNEL PRIMITIVES                         │
│  field_publish() │ neighbor_select() │ vote_cast()          │
├─────────────────────────────────────────────────────────────┤
│                         HAL                                  │
│  STM32G474 │ TriCore TC397XP │ POSIX Simulation             │
└─────────────────────────────────────────────────────────────┘
```

## Quick Start

```c
#include <ekk/ekk.h>

ekk_module_t my_module;

void charge_task(void *arg) {
    // Charging logic
}

int main(void) {
    ekk_init();

    ekk_position_t pos = {.x = 1, .y = 2, .z = 0};
    ekk_module_init(&my_module, 42, "charger-42", pos);
    ekk_module_add_task(&my_module, "charge", charge_task, NULL, 0, 1000, NULL);
    ekk_module_start(&my_module);

    while (1) {
        ekk_module_tick(&my_module, ekk_hal_time_us());
    }
}
```

## Building

```bash
mkdir build && cd build
cmake .. -DEKK_PLATFORM=posix
make
ctest
```

### Platform Options

- `posix` - Linux/macOS simulation (default)
- `stm32g474` - STM32G474 Cortex-M4
- `tricore` - Infineon TriCore TC397XP

### Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `EKK_K_NEIGHBORS` | 7 | Topological neighbor count |
| `EKK_MAX_MODULES` | 256 | Maximum modules in cluster |
| `EKK_FIELD_DECAY_TAU_US` | 100000 | Field decay time constant |
| `EKK_HEARTBEAT_PERIOD_US` | 10000 | Heartbeat period |

## Comparison with Traditional RTOS

| Aspect | Traditional RTOS | EK-KOR v2 |
|--------|-----------------|-----------|
| Primary unit | Task | Module |
| Scheduling | Central scheduler | Gradient fields |
| Communication | Message passing | Shared fields |
| Topology | Unknown to kernel | k-neighbors |
| Consensus | None | Kernel primitive |
| Failure detection | Application | Kernel service |

## Theoretical Basis

- **Potential Fields:** Khatib, O. (1986) - Extended from spatial to temporal scheduling
- **Topological Coordination:** Cavagna & Giardina (2010) - Scale-free correlations in starlings
- **Threshold Consensus:** Density-dependent activation from quorum sensing research

## License

MIT License - Copyright (c) 2026 Elektrokombinacija

## Files

```
ek-kor2/
├── include/ekk/
│   ├── ekk.h           # Master include
│   ├── ekk_types.h     # Base types
│   ├── ekk_field.h     # Coordination fields
│   ├── ekk_topology.h  # k-neighbor management
│   ├── ekk_consensus.h # Threshold voting
│   ├── ekk_heartbeat.h # Liveness detection
│   ├── ekk_module.h    # Module abstraction
│   └── ekk_hal.h       # Hardware abstraction
├── src/                # Implementation
├── test/               # Unit tests
├── examples/           # Usage examples
└── docs/               # Documentation
```

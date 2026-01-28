# MAPF-HET Integration into EK-KOR2 Kernel

This document describes the integration of Multi-Agent Path Finding for Heterogeneous agents (MAPF-HET) concepts into the EK-KOR2 kernel.

## Overview

MAPF-HET research focuses on coordinating heterogeneous robots with different capabilities and deadlines. EK-KOR2 adapts two key concepts for modular power electronics coordination:

1. **Deadline-Aware Scheduling** via slack field component
2. **Capability-Based Task Assignment** via bitmask matching

Both systems use potential fields for coordination - MAPF-HET for robot path planning, EK-KOR2 for load balancing - making the concepts naturally compatible.

## What Was Integrated

### 1. Slack Field Component

**Purpose:** Enables deadline-aware task selection via the existing gradient mechanism.

**Implementation:**
- Added `EKK_FIELD_SLACK = 5` as 6th field component
- Modules publish their minimum deadline slack
- Gradient mechanism naturally prioritizes tight-deadline modules

**Key Types:**
```c
// C
typedef struct {
    ekk_time_us_t deadline;      // Absolute deadline
    ekk_time_us_t duration_est;  // Estimated duration
    ekk_fixed_t slack;           // Computed slack (normalized)
    bool critical;               // slack < 10 seconds
} ekk_deadline_t;

// Rust
pub struct Deadline {
    pub deadline: TimeUs,
    pub duration_est: TimeUs,
    pub slack: Fixed,
    pub critical: bool,
}
```

**Algorithm (from MAPF-HET deadline_cbs.go:231-270):**
```
slack_us = deadline - (now + duration_estimate)
critical = slack_us < SLACK_THRESHOLD_US (10 seconds)
slack_normalized = clamp(slack_us / SLACK_NORMALIZE_US, 0.0, 1.0)
```

### 2. Capability Bitmask

**Purpose:** Enable capability-based task assignment for heterogeneous module coordination.

**Implementation:**
- Added `ekk_capability_t` (uint16 bitmask)
- Tasks can require specific capabilities
- Task selection filters by capability match

**Standard Flags:**
| Bit | Flag | Description |
|-----|------|-------------|
| 0 | `EKK_CAP_THERMAL_OK` | Within thermal limits |
| 1 | `EKK_CAP_POWER_HIGH` | High power mode available |
| 2 | `EKK_CAP_GATEWAY` | Can aggregate/route messages |
| 3 | `EKK_CAP_V2G` | Bidirectional power capable |
| 8-11 | `EKK_CAP_CUSTOM_0-3` | Application-defined |

**Matching Function:**
```c
static inline bool ekk_can_perform(ekk_capability_t have, ekk_capability_t need) {
    return (have & need) == need;
}
```

## What Was NOT Integrated

These MAPF-HET concepts were evaluated but not integrated:

| Concept | Reason |
|---------|--------|
| CBS Tree Search | O(2^n) memory, too complex for embedded kernel |
| LogNormal Distributions | Requires floating-point, non-deterministic |
| Space-Time A* | Algorithm varies by use case |
| Workspace Graph | EK-KOR2 topology already serves this role |
| Dimensional Classification | Physical robotics concept, not applicable |

## What Was Already Implemented

These concepts were already present in EK-KOR2 before this integration:

| Concept | Status | Location |
|---------|--------|----------|
| Topological neighbor selection | ✅ DONE | `02-roj-intelligence.md` - SelectNeighbors |
| Byzantine fault tolerance | ✅ DONE | `02-roj-intelligence.md` - BFT section |
| Network partition handling | ✅ DONE | Technical report §5.7 |
| Split-brain prevention | ✅ DONE | Minority freeze, epoch detection |
| Reconciliation protocol | ✅ DONE | 3-phase: leader, state, reintegration |

## Files Modified

### C Implementation

| File | Changes |
|------|---------|
| `c/include/ekk/ekk_types.h` | `EKK_FIELD_SLACK`, `ekk_capability_t`, `ekk_deadline_t` |
| `c/include/ekk/ekk_module.h` | Deadline/capability in task struct, new APIs |
| `c/src/ekk_module.c` | `compute_slack()`, capability filtering, updated task selection |

### Rust Implementation

| File | Changes |
|------|---------|
| `rust/src/types.rs` | `FIELD_COUNT=6`, `FieldComponent::Slack`, `Capability`, `Deadline` |
| `rust/src/module.rs` | Deadline/capability fields, `compute_slack()`, updated task selection |

### Specifications

| File | Changes |
|------|---------|
| `spec/api.md` | New types, constants, functions documented |
| `spec/test-vectors/deadline_*.json` | Slack computation test vectors |
| `spec/test-vectors/capability_*.json` | Capability matching test vectors |
| `spec/test-vectors/field_008_*.json` | Slack gradient test vector |

### Property Tests

| File | Purpose |
|------|---------|
| `spec/property-tests/tests/prop_deadline.rs` | Slack computation invariants |
| `spec/property-tests/tests/prop_capability.rs` | Capability matching invariants |

## API Summary

### New Types

```c
// Capability bitmask
typedef uint16_t ekk_capability_t;

// Deadline information
typedef struct {
    ekk_time_us_t deadline;
    ekk_time_us_t duration_est;
    ekk_fixed_t slack;
    bool critical;
} ekk_deadline_t;
```

### New Functions

```c
// Deadline/Slack
ekk_error_t ekk_module_compute_slack(ekk_module_t *mod, ekk_time_us_t now);
ekk_error_t ekk_module_set_task_deadline(ekk_module_t *mod, ekk_task_id_t task_id,
                                          ekk_time_us_t deadline, ekk_time_us_t duration_est);
ekk_error_t ekk_module_clear_task_deadline(ekk_module_t *mod, ekk_task_id_t task_id);

// Capability
static inline bool ekk_can_perform(ekk_capability_t have, ekk_capability_t need);
ekk_error_t ekk_module_set_capabilities(ekk_module_t *mod, ekk_capability_t caps);
ekk_capability_t ekk_module_get_capabilities(const ekk_module_t *mod);
ekk_error_t ekk_module_set_task_capabilities(ekk_module_t *mod, ekk_task_id_t task_id,
                                              ekk_capability_t caps);
```

### New Constants

```c
#define EKK_FIELD_SLACK           5
#define EKK_FIELD_COUNT           6

#define EKK_SLACK_THRESHOLD_US    10000000  // 10 seconds
#define EKK_SLACK_NORMALIZE_US    100000000 // 100 seconds (internal)

#define EKK_CAP_THERMAL_OK        (1 << 0)
#define EKK_CAP_POWER_HIGH        (1 << 1)
#define EKK_CAP_GATEWAY           (1 << 2)
#define EKK_CAP_V2G               (1 << 3)
#define EKK_CAP_CUSTOM_0          (1 << 8)
// ... etc
```

## Usage Examples

### Setting Task Deadline

```c
// Create a task that must complete within 30 seconds
ekk_task_id_t task_id;
ekk_module_add_task(&mod, "charge_session", charge_task, NULL, 0, 0, &task_id);

ekk_time_us_t now = ekk_hal_time_us();
ekk_time_us_t deadline = now + 30000000;  // 30 seconds from now
ekk_time_us_t duration = 5000000;          // Estimated 5 seconds to complete

ekk_module_set_task_deadline(&mod, task_id, deadline, duration);
```

### Setting Module Capabilities

```c
// Module is thermally OK and has V2G capability
ekk_module_set_capabilities(&mod, EKK_CAP_THERMAL_OK | EKK_CAP_V2G);
```

### Setting Task Requirements

```c
// Task requires V2G capability
ekk_module_set_task_capabilities(&mod, task_id, EKK_CAP_V2G);
```

### Slack Gradient Usage

```c
// In tick loop, slack is published automatically when compute_slack is called
ekk_module_compute_slack(&mod, now);

// Slack gradient shows pressure from neighbors
ekk_fixed_t slack_grad = ekk_module_get_gradient(&mod, EKK_FIELD_SLACK);

// Negative gradient = neighbors have less slack (tighter deadlines)
// This module should consider helping neighbors by taking work
if (slack_grad < EKK_FLOAT_TO_FIXED(-0.2)) {
    // Neighbors are deadline-constrained, offer assistance
}
```

## Relationship to Existing EK-KOR2 Features

The MAPF-HET integration complements existing features:

| Existing Feature | MAPF-HET Complement |
|-----------------|---------------------|
| Field-based load balancing | Slack field adds deadline awareness |
| Topological neighbors | Capabilities add heterogeneity |
| Priority-based task selection | Critical deadlines override priority |
| Byzantine fault tolerance | Capability filtering prevents mis-assignment |

## Testing

### Unit Tests

Run test vectors:
```bash
cd ek-kor2/tools
python golden_validate.py
```

### Property Tests

```bash
cd ek-kor2/spec/property-tests
cargo test
```

### Integration Test Scenario

1. Create 3 modules with different deadlines (tight, medium, loose)
2. Verify slack field propagates correctly via gradient mechanism
3. Verify module with tightest deadline gets task priority
4. Verify capability filtering in neighbor selection

## Future Considerations

### Phase 2: Fine-Grained Constraints (Deferred)

The plan identified a potential Phase 2 enhancement:

```c
typedef struct {
    ekk_module_id_t module;
    ekk_time_us_t start_time;
    ekk_time_us_t end_time;
    uint8_t constraint_type;  // POWER_LIMIT, WAIT, INHIBIT
} ekk_constraint_t;
```

This was deferred because:
- Existing partition freeze mode provides similar functionality
- Global freeze + deadline awareness may be sufficient
- Complexity cost may outweigh benefits

Re-evaluate if more granular scheduling constraints are needed.

## References

- MAPF-HET Research: `mapf-het-research/` directory
- Original Algorithm: `deadline_cbs.go:231-270`
- EK-KOR2 Architecture: `docs/ek-kor-rtos-technical-report.md`
- ROJ Intelligence: `tehnika/konceptualno/en/02-roj-intelligence.md`

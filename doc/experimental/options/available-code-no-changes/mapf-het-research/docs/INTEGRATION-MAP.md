# MAPF-HET Integration Map

Mapping MAPF-HET algorithm research to EK ecosystem components.

---

## 1. Component Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                        MAPF-HET RESEARCH                             │
│                    (Algorithm Development)                           │
│                                                                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │
│  │   CBS-HET    │  │  Prioritized │  │    MCTS      │               │
│  │  (Optimal)   │  │  (Fast)      │  │ (Stochastic) │               │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘               │
│         └─────────────────┴─────────────────┘                        │
│                           │                                          │
└───────────────────────────┼──────────────────────────────────────────┘
                            │
            ┌───────────────┼───────────────┐
            ▼               ▼               ▼
┌───────────────────┐ ┌───────────────┐ ┌───────────────────┐
│   EK-KOR2/JEZGRO  │ │  GO SIMULATOR │ │  RENODE EMULATOR  │
│   (RTOS Core)     │ │  (Physics)    │ │  (MCU Hardware)   │
└───────────────────┘ └───────────────┘ └───────────────────┘
```

---

## 2. MAPF-HET → EK-KOR2 Mapping

### 2.1 Core Concepts

| MAPF-HET Concept | EK-KOR2 Equivalent | Notes |
|------------------|-------------------|-------|
| Robot agent | `ekk_module_t` | Each module is an agent in the field |
| Robot Type A (mobile) | `EKK_ROLE_CHARGER_MODULE` | Mobile robots for module ops |
| Robot Type B (rail) | `EKK_ROLE_SEGMENT_GATEWAY` | Rail robots for full pack swap |
| Task assignment | Gradient-based task selection | `ekk_module_tick()` Phase 7 |
| Path planning | Field gradient following | Potential field navigation |
| Collision avoidance | k=7 neighbor coordination | Topological conflict resolution |
| Makespan objective | `FIELD_LOAD` balancing | Minimize max completion via load balance |

### 2.2 State Mapping

| MAPF-HET State | EK-KOR2 State | File |
|----------------|---------------|------|
| Robot position | `ekk_field_t.position` | `ekk_field.h` |
| Task status | Internal task queue | `ekk_module.c` |
| Path | Gradient vector sequence | `ekk_gradient_t` |
| Collision | Constraint via neighbor field | `ekk_field_sample_neighbors()` |

### 2.3 Algorithm Integration Points

**CBS-HET conflict resolution → EK-KOR2 consensus:**
```
MAPF-HET Conflict         →    EK-KOR2 Consensus Ballot
─────────────────────          ──────────────────────────
Robot1 @ vertex V @ time T     Proposal: INHIBIT_ROBOT_1
Robot2 @ vertex V @ time T     Voters: k-neighbors
                               Threshold: supermajority
                               Result: One robot waits
```

**Prioritized planning → Field gradient priority:**
```
MAPF-HET Priority         →    EK-KOR2 Field Component
─────────────────────          ──────────────────────────
Type B first (rail)            Higher LOAD field value
More tasks assigned            Higher POWER field value
Tighter deadline               Higher THERMAL urgency
```

---

## 3. MAPF-HET → Go Simulator Mapping

### 3.1 Physics Models Needed

| MAPF-HET Requirement | Simulator Component | Status |
|---------------------|---------------------|--------|
| Robot kinematics | `internal/models/` | **NEW** |
| Rail dynamics (1D) | `internal/models/` | **NEW** |
| Collision detection | `internal/engine/` | **NEW** |
| Task duration stochastic | Battery swap timing | Exists (modify) |

### 3.2 New Packages for Go Simulator

```
simulator/engine/internal/
├── robot/                    # NEW: Robot physics
│   ├── mobile.go             # Type A: holonomic, 0.5 m/s
│   ├── rail.go               # Type B: 1D rail, 2.0 m/s
│   └── collision.go          # Collision detection
├── workspace/                # NEW: Environment
│   ├── graph.go              # Workspace graph
│   └── vertex.go             # Vertex/edge definitions
└── pathfinding/              # NEW: Path planning
    ├── astar.go              # Space-time A*
    └── constraint.go         # Constraint representation
```

### 3.3 Integration with Existing Models

```go
// Extend existing Station with robot coordination
type Station struct {
    // Existing fields...

    // MAPF-HET integration
    Robots      []*robot.Robot      // Mobile + rail robots
    Workspace   *workspace.Graph    // Station layout graph
    Planner     pathfinding.Solver  // CBS-HET or Prioritized
    ActivePaths map[RobotID]Path    // Current robot paths
}

// Extend Robot state machine (existing)
const (
    RobotIdle     RobotState = "idle"
    RobotMoving   RobotState = "moving"    // MAPF-HET: following path
    RobotGripping RobotState = "gripping"
    RobotSwapping RobotState = "swapping"
    RobotWaiting  RobotState = "waiting"   // MAPF-HET: conflict wait
)
```

---

## 4. MAPF-HET → Renode Emulator Mapping

### 4.1 Hardware-in-Loop Testing

```
┌─────────────────────────────────────────────────────────────┐
│                    RENODE MULTI-MCU                          │
│                                                              │
│  ┌──────────────┐    CAN-FD     ┌──────────────┐            │
│  │ STM32G474 #1 │◄────────────►│ STM32G474 #2 │            │
│  │  (Robot A1)  │               │  (Robot A2)  │            │
│  └──────────────┘               └──────────────┘            │
│         │                              │                     │
│         │          CAN-FD              │                     │
│         └──────────────┬───────────────┘                     │
│                        │                                     │
│                        ▼                                     │
│                 ┌──────────────┐                             │
│                 │ STM32G474 #3 │                             │
│                 │  (Robot B1)  │                             │
│                 └──────────────┘                             │
│                                                              │
│  Test: MAPF-HET path coordination via CAN-FD messages       │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 Existing Renode Scripts

| Script | Purpose | MAPF-HET Use |
|--------|---------|--------------|
| `ekk_multi.resc` | Multi-module coordination | Add robot nodes |
| `stm32g474.repl` | STM32G474 peripheral model | Robot MCU |
| `hax_multi.resc` | HAX demo setup | Path visualization |

### 4.3 New Renode Configuration

```python
# mapf_het_test.resc

# Create robot MCUs
mach create "robot_a1"
machine LoadPlatformDescription @stm32g474.repl
machine LoadELF @mapf_het_robot.elf

mach create "robot_a2"
machine LoadPlatformDescription @stm32g474.repl
machine LoadELF @mapf_het_robot.elf

mach create "robot_b1"
machine LoadPlatformDescription @stm32g474.repl
machine LoadELF @mapf_het_robot.elf

# Connect via CAN bus
connector Connect sysbus.fdcan1 can_bus
emulation CreateCANHub "can_bus"

# Inject path commands via Python
python "
def inject_path(robot_id, path):
    # Send path via CAN message
    pass
"
```

---

## 5. JEZGRO RTOS Integration

### 5.1 Task Model Mapping

| MAPF-HET Task | JEZGRO Service | Priority |
|---------------|----------------|----------|
| SWAP_BATTERY | `SwapService` | High (EDF deadline) |
| SWAP_MODULE | `ModuleService` | Medium |
| DIAGNOSE | `DiagService` | Low |
| CLEAN | `MaintenanceService` | Background |

### 5.2 IPC for Path Commands

```c
// JEZGRO message types for MAPF-HET
typedef enum {
    MSG_PATH_ASSIGN = 0x30,    // Assign path to robot
    MSG_PATH_UPDATE = 0x31,    // Update path (replanning)
    MSG_CONFLICT_WAIT = 0x32,  // Wait at vertex
    MSG_TASK_COMPLETE = 0x33,  // Task finished
} mapf_msg_type_t;

// Path segment message
typedef struct {
    ekk_module_id_t robot_id;
    uint8_t segment_count;
    struct {
        uint16_t vertex_id;
        uint32_t arrival_time_us;
    } segments[8];
} mapf_path_msg_t;
```

### 5.3 MPU Isolation for Path Planner

```
┌─────────────────────────────────────────────────────────┐
│                    JEZGRO MPU REGIONS                    │
├─────────────────────────────────────────────────────────┤
│  REGION 0: Kernel (privileged)                          │
│  REGION 1: Path Planner Service (unprivileged)          │
│            - CBS-HET algorithm                          │
│            - Workspace graph (read-only)                │
│            - Path output buffer                         │
│  REGION 2: Robot Control Service (unprivileged)         │
│            - Motor control                              │
│            - Path execution                             │
│  REGION 3: Shared IPC (read-write)                      │
│            - Path messages                              │
│            - Conflict notifications                     │
└─────────────────────────────────────────────────────────┘
```

---

## 6. Development Phases

### Phase 1: Pure Algorithm (Current)

```
mapf-het-research/     # Standalone Go
├── internal/core/     # Domain models
├── internal/algo/     # CBS, Prioritized
└── cmd/mapfhet/       # CLI tests
```

**No dependencies on ek-kor2 or simulator.**

### Phase 2: Go Simulator Integration

```
simulator/engine/internal/
├── robot/             # NEW
├── workspace/         # NEW
├── pathfinding/       # Copy from mapf-het-research
└── models/            # Existing (extend)
```

**Add robot physics to existing simulator.**

### Phase 3: EK-KOR2 Protocol

```
ek-kor2/
├── spec/
│   └── mapf-protocol.md     # NEW: CAN message spec
├── c/include/ekk/
│   └── ekk_mapf.h           # NEW: Path message types
└── rust/src/
    └── mapf.rs              # NEW: Rust equivalent
```

**Define CAN-FD message protocol for path coordination.**

### Phase 4: Renode HIL Testing

```
ek-kor2/renode/
├── mapf_het_test.resc       # Multi-robot test
├── mapf_path_inject.py      # Path injection script
└── mapf_validation.robot    # Robot Framework tests
```

**Hardware-in-loop validation with emulated MCUs.**

---

## 7. Data Flow

```
┌────────────────────────────────────────────────────────────────┐
│                    OFFLINE PLANNING                             │
│                                                                 │
│  Tasks + Deadline ──► CBS-HET ──► Paths + Schedule             │
│        │                   │              │                     │
│        │    (Go or Rust)   │              │                     │
│        ▼                   ▼              ▼                     │
│  ┌──────────────────────────────────────────────────────┐      │
│  │              PATH DATABASE (Redis or RAM)             │      │
│  │  robot_1: [(v0,t0), (v1,t1), (v2,t2), ...]           │      │
│  │  robot_2: [(v0,t0), (v3,t1), (v4,t2), ...]           │      │
│  └──────────────────────────────────────────────────────┘      │
└────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌────────────────────────────────────────────────────────────────┐
│                    ONLINE EXECUTION                             │
│                                                                 │
│  ┌───────────┐    CAN-FD     ┌───────────┐                     │
│  │  Robot 1  │◄─────────────►│  Robot 2  │                     │
│  │  (MCU)    │               │  (MCU)    │                     │
│  └───────────┘               └───────────┘                     │
│       │                           │                             │
│       │  EK-KOR2 Field Exchange   │                             │
│       └───────────┬───────────────┘                             │
│                   │                                             │
│                   ▼                                             │
│  Conflict detected? ──► Consensus vote ──► Replan if needed    │
│                                                                 │
└────────────────────────────────────────────────────────────────┘
```

---

## 8. Test Scenarios

### 8.1 Unit Tests (mapf-het-research)

| Test | Description |
|------|-------------|
| `TestCanPerform` | Capability matrix |
| `TestSpaceTimeAStar` | Single-robot pathfinding |
| `TestCBSConflict` | Two-robot conflict resolution |
| `TestPrioritizedOrder` | Priority computation |

### 8.2 Integration Tests (Go Simulator)

| Scenario | Robots | Tasks | Expected |
|----------|--------|-------|----------|
| `simple_2x2` | 2 | 2 | No conflicts |
| `conflict_vertex` | 2 | 2 | CBS resolves |
| `swap_station` | 4 | 8 | Full workflow |
| `deadline_tight` | 4 | 8 | Meets 300s deadline |

### 8.3 HIL Tests (Renode)

| Test | MCUs | Validation |
|------|------|------------|
| `path_broadcast` | 3 | CAN message delivery |
| `conflict_consensus` | 3 | Vote convergence |
| `replan_on_fault` | 3 | Path update after MCU reset |

---

## 9. Open Questions

1. **Centralized vs Distributed Planning**
   - Phase 1-2: Centralized (coordinator computes all paths)
   - Phase 3+: Distributed? (each robot runs local planner)

2. **Replanning Trigger**
   - Robot fault → immediate replan
   - Task delay → threshold-based replan
   - New task arrival → append or full replan?

3. **Physics Fidelity**
   - Do we need accurate robot dynamics for algorithm dev?
   - Or is discrete grid sufficient?

---

## References

- `docs/` - Problem formulation
- `ek-kor2/IMPLEMENTATION-PLAN.md` - EK-KOR2 architecture
- `simulator/README.md` - Go physics simulator
- `tehnika/inzenjersko/en/16-jezgro.md` - JEZGRO RTOS spec

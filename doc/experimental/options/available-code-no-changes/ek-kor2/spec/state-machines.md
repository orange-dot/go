# EK-KOR v2 State Machines

Visual and formal definitions of all state machines in the system.

## Module State Machine

```
                              ┌──────────────┐
                              │     Init     │
                              └──────┬───────┘
                                     │ start()
                                     ▼
                              ┌──────────────┐
                    ┌─────────│ Discovering  │─────────┐
                    │         └──────┬───────┘         │
                    │                │                 │
                    │                │ neighbors >= k/2│
                    │                ▼                 │
                    │         ┌──────────────┐         │
                    │    ┌───►│    Active    │◄───┐    │
                    │    │    └──────┬───────┘    │    │
                    │    │           │            │    │
                    │    │ neighbors │ neighbors  │    │
                    │    │ >= k/2    │ < k/2      │    │
                    │    │           ▼            │    │
                    │    │    ┌──────────────┐    │    │
                    │    └────│   Degraded   │────┘    │
                    │         └──────┬───────┘         │
                    │                │                 │
                    │                │ neighbors == 0  │
                    │                ▼                 │
                    │         ┌──────────────┐         │
                    └────────►│   Isolated   │◄────────┘
                              └──────┬───────┘
                                     │ neighbors > 0
                                     ▼
                              ┌──────────────┐
                              │  Reforming   │────────┐
                              └──────┬───────┘        │
                                     │ neighbors >= k/2
                                     │                │
                                     ▼                │
                              ┌──────────────┐        │
                              │    Active    │◄───────┘
                              └──────────────┘


                    Any State ──────────────────► Shutdown
                                   stop()
```

### State Descriptions

| State | Entry Condition | Behavior |
|-------|-----------------|----------|
| Init | Module created | No coordination, waiting for start() |
| Discovering | start() called | Broadcasting discovery, collecting neighbors |
| Active | neighbors >= k/2 | Normal operation, full coordination |
| Degraded | neighbors < k/2 | Reduced reliability, seeking more neighbors |
| Isolated | neighbors == 0 | No coordination possible, aggressive discovery |
| Reforming | Isolated + found neighbor | Rebuilding topology |
| Shutdown | stop() called | Notify neighbors, clean shutdown |

### Transition Table

| From | Event | Condition | To |
|------|-------|-----------|-----|
| Init | start() | - | Discovering |
| Discovering | tick() | neighbors >= k/2 | Active |
| Discovering | tick() | neighbors == 0, timeout | Isolated |
| Active | neighbor_lost() | neighbors < k/2 | Degraded |
| Active | neighbor_lost() | neighbors == 0 | Isolated |
| Degraded | discovery() | neighbors >= k/2 | Active |
| Degraded | neighbor_lost() | neighbors == 0 | Isolated |
| Isolated | discovery() | neighbors > 0 | Reforming |
| Reforming | tick() | neighbors >= k/2 | Active |
| Reforming | tick() | neighbors == 0 | Isolated |
| Any | stop() | - | Shutdown |

---

## Neighbor Health State Machine

```
                              ┌──────────────┐
                              │   Unknown    │
                              └──────┬───────┘
                                     │ heartbeat_received
                                     ▼
                    ┌─────────┌──────────────┐
                    │         │    Alive     │◄────────┐
                    │         └──────┬───────┘         │
                    │                │                 │
                    │                │ elapsed > 2*period
                    │                ▼                 │
                    │         ┌──────────────┐         │
                    │         │   Suspect    │─────────┤
                    │         └──────┬───────┘         │
                    │                │                 │ heartbeat_received
                    │                │ elapsed > timeout
                    │                ▼                 │
                    │         ┌──────────────┐         │
                    └────────►│     Dead     │─────────┘
                              └──────────────┘
                    (never transitions back except via heartbeat)
```

### Timing Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| period | 10ms | Heartbeat period |
| suspect_threshold | 2 * period = 20ms | Time before Suspect |
| timeout | 5 * period = 50ms | Time before Dead |

### State Transitions

| From | Event | Condition | To | Action |
|------|-------|-----------|-----|--------|
| Unknown | heartbeat_received | - | Alive | on_alive() |
| Alive | tick() | elapsed > 2*period | Suspect | on_suspect() |
| Alive | heartbeat_received | - | Alive | reset timer |
| Suspect | tick() | elapsed > timeout | Dead | on_dead() |
| Suspect | heartbeat_received | - | Alive | on_alive() |
| Dead | heartbeat_received | - | Alive | on_alive() |

---

## Consensus Ballot State Machine

```
                              ┌──────────────┐
                              │   Created    │
                              └──────┬───────┘
                                     │ propose()
                                     ▼
         ┌────────────────────┌──────────────┐────────────────────┐
         │                    │   Pending    │                    │
         │                    └──────┬───────┘                    │
         │                           │                            │
         │              ┌────────────┼────────────┐               │
         │              │            │            │               │
         │              ▼            ▼            ▼               │
         │       yes_ratio >=   vote_count   elapsed >            │
         │       threshold      >= total     timeout              │
         │              │            │            │               │
         │              ▼            ▼            ▼               │
         │       ┌──────────┐ ┌──────────┐ ┌──────────┐          │
         │       │ Approved │ │ Rejected │ │ Timeout  │          │
         │       └──────────┘ └──────────┘ └──────────┘          │
         │                                                        │
         │                    inhibit()                           │
         │                        │                               │
         │                        ▼                               │
         │                 ┌──────────────┐                       │
         └────────────────►│  Cancelled   │◄──────────────────────┘
                           └──────────────┘
```

### Threshold Calculation

```
yes_ratio = yes_count / total_voters

if yes_ratio >= threshold:
    result = Approved
elif vote_count >= total_voters:
    result = Rejected  # All voted, threshold not met
elif now >= deadline:
    result = Timeout
```

### Example Scenarios

**Scenario 1: Successful Supermajority (k=7)**

| Step | yes | no | abstain | total | ratio | result |
|------|-----|-----|---------|-------|-------|--------|
| 0 | 0 | 0 | 0 | 7 | 0.00 | Pending |
| 1 | 1 | 0 | 0 | 7 | 0.14 | Pending |
| 2 | 2 | 0 | 0 | 7 | 0.29 | Pending |
| 3 | 3 | 1 | 0 | 7 | 0.43 | Pending |
| 4 | 4 | 1 | 0 | 7 | 0.57 | Pending |
| 5 | 5 | 1 | 0 | 7 | 0.71 | **Approved** (≥0.67) |

**Scenario 2: Rejection**

| Step | yes | no | abstain | total | ratio | result |
|------|-----|-----|---------|-------|-------|--------|
| 0-3 | 2 | 3 | 0 | 7 | 0.29 | Pending |
| 4-7 | 3 | 4 | 0 | 7 | 0.43 | **Rejected** (all voted) |

**Scenario 3: Inhibition**

| Step | Event | Result |
|------|-------|--------|
| 0 | propose(ModeChange, A) | Pending |
| 1 | propose(ModeChange, B) | Pending |
| 2 | inhibit(ballot_A) | **Cancelled** |
| 3 | vote continues on B | ... |

---

## Task State Machine (per module)

```
                              ┌──────────────┐
                              │     Idle     │◄─────────────────┐
                              └──────┬───────┘                  │
                                     │ add_task() or           │
                                     │ task_ready()            │
                                     ▼                         │
                              ┌──────────────┐                  │
               ┌──────────────│    Ready     │──────────────┐   │
               │              └──────┬───────┘              │   │
               │                     │                      │   │
               │                     │ selected by          │   │
               │ task_block()        │ scheduler            │ task_block()
               │                     ▼                      │   │
               │              ┌──────────────┐              │   │
               │              │   Running    │──────────────┤   │
               │              └──────┬───────┘              │   │
               │                     │                      │   │
               │                     │ function returns     │   │
               │                     ▼                      ▼   │
               │              ┌──────────────┐       ┌──────────┴─┐
               │              │  Periodic?   │       │  Blocked   │
               │              └──────┬───────┘       └──────┬─────┘
               │                     │                      │
               │          ┌──────────┴──────────┐           │
               │          │ yes                 │ no        │ event or
               │          ▼                     ▼           │ task_ready()
               │   ┌──────────────┐      ┌──────────────┐   │
               │   │    Ready     │      │     Idle     │   │
               │   │ (next_run    │      └──────────────┘   │
               │   │  scheduled)  │                         │
               │   └──────────────┘                         │
               │          ▲                                 │
               └──────────┴─────────────────────────────────┘
```

### Task Selection Algorithm

```python
def select_task(module):
    # Get load gradient
    load_gradient = module.gradients[LOAD]

    # If we're overloaded (neighbors have lower load), throttle
    if load_gradient < -0.2:
        return None  # Go idle

    # Priority boost based on gradient
    # If neighbors are overloaded, we should do more work
    priority_boost = 0 if load_gradient <= 0 else -1  # Lower number = higher priority

    # Find highest priority ready task
    best_task = None
    best_priority = 256

    for task in module.tasks:
        if task.state == Ready:
            effective_priority = task.priority + priority_boost
            if effective_priority < best_priority:
                best_task = task
                best_priority = effective_priority

    return best_task
```

---

## Topology Reelection Algorithm

```
┌────────────────────────────────────────────────────────┐
│                    TRIGGER                              │
│  - discovery_received AND sender could be closer       │
│  - neighbor_lost                                        │
│  - periodic (every discovery_period)                   │
└─────────────────────────┬──────────────────────────────┘
                          │
                          ▼
┌────────────────────────────────────────────────────────┐
│              COLLECT KNOWN MODULES                      │
│  known_modules = all modules we've heard from          │
│  (discovery messages + heartbeats)                      │
└─────────────────────────┬──────────────────────────────┘
                          │
                          ▼
┌────────────────────────────────────────────────────────┐
│              COMPUTE DISTANCES                          │
│  for each known module:                                 │
│      distance = compute_distance(my_position, their_pos)│
└─────────────────────────┬──────────────────────────────┘
                          │
                          ▼
┌────────────────────────────────────────────────────────┐
│              SORT BY DISTANCE                           │
│  sorted_modules = sort(known_modules, by=distance)     │
└─────────────────────────┬──────────────────────────────┘
                          │
                          ▼
┌────────────────────────────────────────────────────────┐
│              SELECT k NEAREST                           │
│  new_neighbors = sorted_modules[0:K_NEIGHBORS]         │
└─────────────────────────┬──────────────────────────────┘
                          │
                          ▼
┌────────────────────────────────────────────────────────┐
│              NOTIFY CHANGES                             │
│  if new_neighbors != old_neighbors:                    │
│      invoke on_topology_changed(old, new)              │
└────────────────────────────────────────────────────────┘
```

### Distance Metrics

**Logical Distance:**
```
distance(a, b) = abs(a.id - b.id)
```

**Physical Distance (squared Euclidean):**
```
distance(a, b) = (a.x - b.x)² + (a.y - b.y)² + (a.z - b.z)²
```

---

## Field Decay Visualization

```
Field Value
    │
1.0 ┼────╮
    │     ╲
    │      ╲
0.5 ┼       ╲
    │        ╲
    │         ╲
    │          ╲____
0.0 ┼──────────────────────────────► Time
    0   τ   2τ   3τ   4τ   5τ

τ = FIELD_DECAY_TAU_US = 100ms

At t=τ:   value = 0.368 (1/e)
At t=2τ:  value = 0.135
At t=3τ:  value = 0.050
At t=5τ:  value = 0.007 (effectively expired)
```

### Decay Formula

```
decayed_value = original_value * exp(-elapsed / τ)
```

Approximation for embedded (no floating point):
```
if elapsed < τ:
    factor = 1.0 - elapsed/τ
elif elapsed < 2τ:
    factor = 0.5 - (elapsed-τ)/(2τ)
elif elapsed < 3τ:
    factor = 0.25 - (elapsed-2τ)/(4τ)
else:
    factor = 0  # Expired
```

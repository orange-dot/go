# EK-KOR v2 API Specification

Language-agnostic specification for both C and Rust implementations.

## Table of Contents

1. [Constants](#constants)
2. [Types](#types)
3. [Field Module](#field-module)
4. [Topology Module](#topology-module)
5. [Heartbeat Module](#heartbeat-module)
6. [Consensus Module](#consensus-module)
7. [Module (Integration)](#module-integration)

---

## Constants

### Configuration Constants

| Name | Value | Description |
|------|-------|-------------|
| `K_NEIGHBORS` | 7 | Number of topological neighbors |
| `MAX_MODULES` | 256 | Maximum modules in cluster |
| `MAX_TASKS_PER_MODULE` | 8 | Maximum internal tasks per module |
| `FIELD_COUNT` | 6 | Number of field components (including Slack) |
| `FIELD_DECAY_TAU_US` | 100000 | Field decay time constant (100ms) |
| `HEARTBEAT_PERIOD_US` | 10000 | Heartbeat period (10ms) |
| `HEARTBEAT_TIMEOUT_COUNT` | 5 | Missed heartbeats before dead |
| `VOTE_TIMEOUT_US` | 50000 | Vote collection timeout (50ms) |
| `MAX_BALLOTS` | 4 | Maximum concurrent ballots |
| `SLACK_THRESHOLD_US` | 10000000 | Critical slack threshold (10 seconds) |
| `SLACK_NORMALIZE_US` | 100000000 | Slack normalization constant (100 seconds) |

### Special Values

| Name | Value | Description |
|------|-------|-------------|
| `INVALID_MODULE_ID` | 0 | Invalid/unassigned module |
| `INVALID_BALLOT_ID` | 0 | Invalid/unassigned ballot |
| `BROADCAST_ID` | 255 | Broadcast address |

---

## Types

### Basic Types

| Type | C | Rust | Description |
|------|---|------|-------------|
| ModuleId | `uint8_t` | `u8` | Module identifier (1-254) |
| TaskId | `uint8_t` | `u8` | Task identifier within module |
| BallotId | `uint16_t` | `u16` | Consensus ballot identifier |
| TimeUs | `uint64_t` | `u64` | Timestamp in microseconds |
| Fixed | `int32_t` (Q16.16) | `I16F16` | Fixed-point value |

### Fixed-Point Arithmetic

Q16.16 format: 16 bits integer, 16 bits fraction.

| Value | Fixed Representation |
|-------|---------------------|
| 0.0 | 0x00000000 |
| 0.5 | 0x00008000 |
| 1.0 | 0x00010000 |
| -1.0 | 0xFFFF0000 |
| 0.67 | 0x0000AB85 |

Conversion:
- Float to Fixed: `(int32_t)(float_val * 65536.0f)`
- Fixed to Float: `(float)fixed_val / 65536.0f`

### Enumerations

#### ModuleState

| Value | Name | Description |
|-------|------|-------------|
| 0 | Init | Initializing, not in mesh |
| 1 | Discovering | Finding neighbors |
| 2 | Active | Normal operation |
| 3 | Degraded | Some neighbors lost |
| 4 | Isolated | No neighbors |
| 5 | Reforming | Rebuilding mesh |
| 6 | Shutdown | Graceful shutdown |

#### HealthState

| Value | Name | Description |
|-------|------|-------------|
| 0 | Unknown | Never seen |
| 1 | Alive | Recent heartbeat |
| 2 | Suspect | Missed 1-2 heartbeats |
| 3 | Dead | Timeout exceeded |

#### VoteValue

| Value | Name | Description |
|-------|------|-------------|
| 0 | Abstain | No vote cast |
| 1 | Yes | Approve proposal |
| 2 | No | Reject proposal |
| 3 | Inhibit | Block competing proposal |

#### VoteResult

| Value | Name | Description |
|-------|------|-------------|
| 0 | Pending | Voting in progress |
| 1 | Approved | Threshold reached |
| 2 | Rejected | Threshold not reached |
| 3 | Timeout | Voting timed out |
| 4 | Cancelled | Inhibited |

#### FieldComponent

| Value | Name | Description |
|-------|------|-------------|
| 0 | Load | Computational load (0.0-1.0) |
| 1 | Thermal | Temperature (normalized) |
| 2 | Power | Power consumption (normalized) |
| 3 | Custom0 | Application-defined |
| 4 | Custom1 | Application-defined |
| 5 | Slack | Deadline slack (MAPF-HET, 0.0=critical, 1.0=max) |

#### Error

| Value | Name | Description |
|-------|------|-------------|
| 0 | OK | Success |
| -1 | InvalidArg | Invalid argument |
| -2 | NoMemory | Out of memory |
| -3 | Timeout | Operation timed out |
| -4 | Busy | Resource busy |
| -5 | NotFound | Item not found |
| -6 | AlreadyExists | Already exists |
| -7 | NoQuorum | Quorum not reached |
| -8 | Inhibited | Proposal inhibited |
| -9 | NeighborLost | Neighbor was lost |
| -10 | FieldExpired | Field too old |
| -11 | HalFailure | HAL error |

### Capability Type (MAPF-HET)

Type: `uint16` bitmask

#### Standard Capability Flags

| Bit | Name | Description |
|-----|------|-------------|
| 0 | THERMAL_OK | Within thermal limits |
| 1 | POWER_HIGH | High power mode available |
| 2 | GATEWAY | Can aggregate/route messages |
| 3 | V2G | Bidirectional power capable |
| 4-7 | Reserved | Reserved for future use |
| 8-11 | CUSTOM_0-3 | Application-defined |

#### can_perform Function

```
can_perform(have: Capability, need: Capability) -> bool
```

**Formula:** `(have & need) == need`

**Usage:** Task assignment - module can only execute task if it has all required capabilities.

### Deadline Type (MAPF-HET)

#### Deadline Structure

| Field | Type | Description |
|-------|------|-------------|
| deadline | TimeUs | Absolute deadline timestamp |
| duration_est | TimeUs | Estimated task duration |
| slack | Fixed | Computed slack (normalized 0.0-1.0) |
| critical | bool | True if slack < SLACK_THRESHOLD_US |

#### Slack Computation

```
slack_us = deadline - (now + duration_est)
critical = slack_us < SLACK_THRESHOLD_US
slack_normalized = clamp(slack_us / SLACK_NORMALIZE_US, 0.0, 1.0)
```

**Interpretation:**
- `slack_normalized = 0.0`: At or past deadline (critical)
- `slack_normalized = 1.0`: Maximum slack (100+ seconds)
- `critical = true`: Less than 10 seconds remaining

---

## Field Module

### Data Structures

#### Field

| Field | Type | Description |
|-------|------|-------------|
| components | Fixed[6] | Field values for each component (includes Slack) |
| timestamp | TimeUs | When published |
| source | ModuleId | Publishing module |
| sequence | uint8 | Monotonic sequence number |

#### FieldRegion

Shared memory region containing fields from all modules.

| Field | Type | Description |
|-------|------|-------------|
| fields | Field[MAX_MODULES] | Published fields |
| update_flags | uint32 | Bitmask of recently updated |

### Functions

#### field_init

Initialize field engine.

```
field_init(region: &mut FieldRegion) -> Error
```

**Preconditions:** None
**Postconditions:** Region zeroed, ready for use

#### field_publish

Publish module's coordination field.

```
field_publish(
    region: &mut FieldRegion,
    module_id: ModuleId,
    field: &Field,
    now: TimeUs
) -> Error
```

**Preconditions:**
- `module_id` in range [1, MAX_MODULES-1]
- `field` is valid

**Postconditions:**
- `region.fields[module_id]` updated
- `timestamp` set to `now`
- `sequence` incremented

**Errors:**
- `InvalidArg` if module_id is 0 or >= MAX_MODULES

#### field_sample

Sample a module's field with decay applied.

```
field_sample(
    region: &FieldRegion,
    target_id: ModuleId,
    now: TimeUs
) -> Result<Field, Error>
```

**Preconditions:**
- `target_id` in valid range

**Postconditions:**
- Returns decayed field values

**Decay Formula:**
```
decay_factor = exp(-(now - timestamp) / FIELD_DECAY_TAU_US)
sampled_value = stored_value * decay_factor
```

**Errors:**
- `NotFound` if module never published
- `FieldExpired` if age > 5 * FIELD_DECAY_TAU_US

#### field_gradient

Compute gradient for a component.

```
field_gradient(
    my_field: &Field,
    neighbor_aggregate: &Field,
    component: FieldComponent
) -> Fixed
```

**Formula:**
```
gradient = neighbor_aggregate.components[component] - my_field.components[component]
```

**Interpretation:**
- Positive: neighbors have higher values
- Negative: neighbors have lower values
- Zero: balanced

---

## Topology Module

### Data Structures

#### Position

| Field | Type | Description |
|-------|------|-------------|
| x | int16 | X coordinate |
| y | int16 | Y coordinate |
| z | int16 | Z coordinate |

#### Neighbor

| Field | Type | Description |
|-------|------|-------------|
| id | ModuleId | Neighbor's ID |
| health | HealthState | Current health |
| last_seen | TimeUs | Last heartbeat time |
| last_field | Field | Last received field |
| logical_distance | int32 | Distance metric |
| missed_heartbeats | uint8 | Consecutive missed |
| capabilities | Capability | Neighbor's capabilities (MAPF-HET) |

### Functions

#### topology_init

Initialize topology state.

```
topology_init(
    topo: &mut Topology,
    my_id: ModuleId,
    my_position: Position
) -> Error
```

#### topology_on_discovery

Process discovery message.

```
topology_on_discovery(
    topo: &mut Topology,
    sender_id: ModuleId,
    sender_position: Position,
    now: TimeUs
) -> Result<bool, Error>
```

**Returns:** `true` if topology changed (reelection triggered)

**Algorithm:**
1. Add/update sender in known list
2. Compute distance to sender
3. If sender could be closer than current furthest neighbor, trigger reelection
4. Return whether topology changed

#### topology_reelect

Force neighbor reelection.

```
topology_reelect(topo: &mut Topology) -> uint32
```

**Algorithm:**
1. Sort all known modules by distance
2. Take k closest as neighbors
3. Return new neighbor count

**Distance Metrics:**

| Metric | Formula |
|--------|---------|
| Logical | `abs(my_id - other_id)` |
| Physical | `(dx² + dy² + dz²)` (squared Euclidean) |

#### topology_on_neighbor_lost

Handle neighbor loss.

```
topology_on_neighbor_lost(
    topo: &mut Topology,
    lost_id: ModuleId
) -> Error
```

**Algorithm:**
1. Remove lost_id from neighbors
2. Trigger reelection to find replacement

---

## Heartbeat Module

### Data Structures

#### HeartbeatMessage

| Field | Type | Size | Description |
|-------|------|------|-------------|
| sender_id | ModuleId | 1 | Sender's ID |
| sequence | uint8 | 1 | Sequence number |
| state | ModuleState | 1 | Sender's state |
| neighbor_count | uint8 | 1 | Sender's neighbors |
| load_percent | uint8 | 1 | Load 0-100 |
| thermal_percent | uint8 | 1 | Thermal 0-100 |
| flags | uint8 | 1 | Reserved |

Total: 7 bytes

### Functions

#### heartbeat_init

Initialize heartbeat engine.

```
heartbeat_init(
    hb: &mut Heartbeat,
    my_id: ModuleId
) -> Error
```

#### heartbeat_add_neighbor

Add neighbor to track.

```
heartbeat_add_neighbor(
    hb: &mut Heartbeat,
    neighbor_id: ModuleId
) -> Error
```

#### heartbeat_received

Process received heartbeat.

```
heartbeat_received(
    hb: &mut Heartbeat,
    sender_id: ModuleId,
    sequence: uint8,
    now: TimeUs
) -> Error
```

**Algorithm:**
1. Find sender in tracked neighbors
2. Update last_seen to now
3. Reset missed_count to 0
4. Set health to Alive
5. If was Dead/Suspect, invoke on_alive callback

#### heartbeat_tick

Periodic tick.

```
heartbeat_tick(
    hb: &mut Heartbeat,
    now: TimeUs
) -> uint32  // Number of state changes
```

**Algorithm:**
```
for each tracked neighbor:
    elapsed = now - neighbor.last_seen

    if elapsed > HEARTBEAT_PERIOD_US * HEARTBEAT_TIMEOUT_COUNT:
        neighbor.health = Dead
        invoke on_dead callback
    else if elapsed > HEARTBEAT_PERIOD_US * 2:
        neighbor.health = Suspect
        invoke on_suspect callback
```

---

## Consensus Module

### Data Structures

#### Ballot

| Field | Type | Description |
|-------|------|-------------|
| id | BallotId | Unique ballot ID |
| proposal_type | uint8 | What we're voting on |
| proposer | ModuleId | Who proposed |
| data | uint32 | Proposal-specific data |
| threshold | Fixed | Required approval ratio |
| deadline | TimeUs | When voting ends |
| vote_count | uint8 | Votes received |
| yes_count | uint8 | Approvals |
| no_count | uint8 | Rejections |
| result | VoteResult | Final result |
| completed | bool | Voting finished |

#### Proposal Types

| Value | Name | Description |
|-------|------|-------------|
| 0 | ModeChange | Change operational mode |
| 1 | PowerLimit | Set power limit |
| 2 | Shutdown | Graceful shutdown |
| 3 | Reformation | Mesh reformation |
| 16-19 | Custom0-3 | Application-defined |

#### Standard Thresholds

| Name | Value | Fixed |
|------|-------|-------|
| SimpleMajority | 0.50 | 0x8000 |
| Supermajority | 0.67 | 0xAB85 |
| Unanimous | 1.00 | 0x10000 |

### Functions

#### consensus_propose

Propose a vote.

```
consensus_propose(
    cons: &mut Consensus,
    proposal_type: uint8,
    data: uint32,
    threshold: Fixed,
    now: TimeUs
) -> Result<BallotId, Error>
```

**Algorithm:**
1. Allocate new ballot ID
2. Create ballot with deadline = now + VOTE_TIMEOUT_US
3. Add to active ballots
4. Return ballot ID

**Errors:**
- `Busy` if MAX_BALLOTS reached

#### consensus_vote

Cast a vote.

```
consensus_vote(
    cons: &mut Consensus,
    ballot_id: BallotId,
    vote: VoteValue
) -> Error
```

#### consensus_on_vote

Process incoming vote.

```
consensus_on_vote(
    cons: &mut Consensus,
    voter_id: ModuleId,
    ballot_id: BallotId,
    vote: VoteValue,
    total_voters: uint8
) -> Error
```

**Algorithm:**
```
ballot = find_ballot(ballot_id)
record_vote(ballot, vote)

yes_ratio = ballot.yes_count / total_voters
if yes_ratio >= ballot.threshold:
    ballot.result = Approved
    ballot.completed = true
else if ballot.vote_count >= total_voters:
    ballot.result = Rejected
    ballot.completed = true
```

#### consensus_inhibit

Inhibit a proposal.

```
consensus_inhibit(
    cons: &mut Consensus,
    ballot_id: BallotId,
    now: TimeUs
) -> Error
```

**Effect:** Immediately cancels the ballot.

#### consensus_tick

Check timeouts.

```
consensus_tick(
    cons: &mut Consensus,
    now: TimeUs
) -> uint32  // Completed ballots count
```

---

## Module (Integration)

### Data Structures

#### Module

Complete module state combining all subsystems.

| Field | Type | Description |
|-------|------|-------------|
| id | ModuleId | Module ID |
| name | string | Debug name |
| state | ModuleState | Current state |
| my_field | Field | Published field |
| neighbor_aggregate | Field | Aggregated neighbor fields |
| gradients | Fixed[5] | Current gradients |
| topology | Topology | Neighbor management |
| consensus | Consensus | Voting engine |
| heartbeat | Heartbeat | Liveness detection |
| tasks | Task[MAX_TASKS] | Internal tasks |
| active_task | TaskId? | Currently running |

### Functions

#### module_init

Initialize module.

```
module_init(
    mod: &mut Module,
    id: ModuleId,
    name: string,
    position: Position
) -> Error
```

#### module_start

Start operation.

```
module_start(mod: &mut Module) -> Error
```

**State transition:** Init → Discovering

#### module_tick

Main coordination loop.

```
module_tick(
    mod: &mut Module,
    region: &mut FieldRegion,
    now: TimeUs
) -> Error
```

**Algorithm:**
```
1. heartbeat.tick(now)
   - Update neighbor health
   - Detect failures → topology.on_neighbor_lost()

2. Sample neighbor fields
   neighbor_aggregate = field_sample_neighbors(region, topology.neighbors, now)

3. Compute gradients
   for each component:
       gradients[c] = field_gradient(my_field, neighbor_aggregate, c)

4. consensus.tick(now)
   - Check vote timeouts

5. topology.tick(now)
   - Send discovery if due

6. Select and run task
   task_id = select_task_based_on_gradients()
   if task_id is valid:
       run_task(task_id)

7. Publish updated field
   field_publish(region, my_id, my_field, now)

8. Update state based on neighbor count
```

#### module_update_field

Update published field values.

```
module_update_field(
    mod: &mut Module,
    load: Fixed,
    thermal: Fixed,
    power: Fixed
) -> Error
```

#### module_get_gradient

Get current gradient.

```
module_get_gradient(
    mod: &Module,
    component: FieldComponent
) -> Fixed
```

### Deadline / Slack Operations (MAPF-HET)

#### module_compute_slack

Compute slack for all tasks with deadlines.

```
module_compute_slack(
    mod: &mut Module,
    now: TimeUs
) -> Error
```

**Algorithm:**
```
min_slack = MAX_VALUE
for each task with deadline:
    task.slack = deadline - (now + duration_est)
    task.critical = slack < SLACK_THRESHOLD_US
    min_slack = min(min_slack, task.slack)

mod.my_field.components[Slack] = normalize(min_slack)
```

**Effect:** Updates:
- Each task's `deadline.slack` and `deadline.critical`
- Module's `my_field.components[Slack]` with minimum slack

#### module_set_task_deadline

Set deadline for a task.

```
module_set_task_deadline(
    mod: &mut Module,
    task_id: TaskId,
    deadline: TimeUs,
    duration_est: TimeUs
) -> Error
```

**Errors:** `NotFound` if task_id invalid

#### module_clear_task_deadline

Clear deadline for a task.

```
module_clear_task_deadline(
    mod: &mut Module,
    task_id: TaskId
) -> Error
```

### Capability Operations (MAPF-HET)

#### module_set_capabilities

Set module's current capabilities.

```
module_set_capabilities(
    mod: &mut Module,
    caps: Capability
) -> Error
```

**Usage:** Call when thermal state changes, configuration updates, etc.

#### module_get_capabilities

Get module's current capabilities.

```
module_get_capabilities(
    mod: &Module
) -> Capability
```

#### module_set_task_capabilities

Set required capabilities for a task.

```
module_set_task_capabilities(
    mod: &mut Module,
    task_id: TaskId,
    caps: Capability
) -> Error
```

**Effect:** Task will only be selected if `can_perform(module.capabilities, task.required_caps)` is true.

---

## Task Selection with MAPF-HET

The enhanced task selection algorithm considers deadlines and capabilities:

```
select_task(mod):
    best = None

    for each ready task:
        # Capability check (MAPF-HET)
        if task.required_caps != 0:
            if not can_perform(mod.capabilities, task.required_caps):
                continue

        # Critical deadline check (MAPF-HET)
        is_critical = task.has_deadline and task.deadline.critical

        # Selection rules:
        # 1. Critical tasks beat non-critical
        # 2. Among same criticality, lower priority wins
        if is_critical and not best.is_critical:
            best = task
        elif is_critical == best.is_critical:
            if task.priority < best.priority:
                best = task

    return best
```

---

## Message Formats

All messages are packed (no padding).

### Heartbeat Message (7 bytes)

```
Offset  Size  Field
0       1     sender_id
1       1     sequence
2       1     state
3       1     neighbor_count
4       1     load_percent
5       1     thermal_percent
6       1     flags
```

### Discovery Message (10 bytes)

```
Offset  Size  Field
0       1     sender_id
1       2     position.x
3       2     position.y
5       2     position.z
7       1     neighbor_count
8       1     state
9       1     sequence (low byte)
```

### Vote Message (8 bytes)

```
Offset  Size  Field
0       1     voter_id
1       2     ballot_id
3       1     vote
4       4     timestamp
```

### Proposal Message (12 bytes)

```
Offset  Size  Field
0       1     proposer_id
1       2     ballot_id
3       1     proposal_type
4       4     data
8       4     threshold (Fixed)
```

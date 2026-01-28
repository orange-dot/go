# MAPF-HET Algorithm Specifications

**Document:** Formal Specification of MAPF-HET Algorithms
**Version:** 1.0
**Date:** 2026-01-21

---

## Table of Contents

1. [Introduction and Motivation](#1-introduction-and-motivation)
2. [Mixed-Dimensional MAPF (MD-MAPF)](#2-mixed-dimensional-mapf-md-mapf)
3. [Dimensional Conflict Decomposition](#3-dimensional-conflict-decomposition)
4. [Energy-Constrained CBS (E-CBS)](#4-energy-constrained-cbs-e-cbs)
5. [Layered Airspace Model](#5-layered-airspace-model)
6. [Hybrid Planning with Potential Fields](#6-hybrid-planning-with-potential-fields)
7. [Formal Definitions](#7-formal-definitions)
8. [Conclusion](#8-conclusion)

---

## 1. Introduction and Motivation

### 1.1 Problem Setting

Classical MAPF (Multi-Agent Path Finding) assumes homogeneous agents operating in the same space. However, real-world systems such as automated warehouses, ports, and electric vehicle charging stations require coordination of heterogeneous agents with fundamentally different kinematic properties.

Consider a robotic battery swap station for electric buses. In such a system, the following agent types coexist:

- **Type A (Mobile Robots):** Omnidirectional movement in 2D plane, speed 0.5 m/s
- **Type B (Rail Robots):** Constrained to 1D path along rails, speed 2.0 m/s
- **Type C (Drones):** Movement in 3D space with discrete altitudes, speed 15.0 m/s

Standard MAPF algorithms cannot efficiently model interactions between agents of different dimensionalities.

### 1.2 System Components

The MAPF-HET system consists of:

1. **MD-MAPF Formalization** — Extension of the MAPF problem to heterogeneous agents with mixed dimensionality
2. **Dimensional Conflict Decomposition** — Classification and resolution of conflicts according to dimensional interaction
3. **E-CBS Algorithm** — CBS with energy constraints and automatic charging station waypoint insertion
4. **Discrete Layered Airspace** — Model for vertical separation of drones

---

## 2. Mixed-Dimensional MAPF (MD-MAPF)

### 2.1 Formal Definition

**Definition 2.1 (MD-MAPF Instance).**
A mixed-dimensional MAPF problem is defined as a tuple:

```
I = (G, A, T, κ, δ)
```

where:
- `G = (V, E)` — Workspace graph with vertices `V` and edges `E`
- `A = {a₁, a₂, ..., aₙ}` — Set of agents
- `T = {t₁, t₂, ..., tₘ}` — Set of tasks
- `κ: A → {1, 2, 3}` — Agent dimensionality function
- `δ: V → P({1, 2, 3})` — Available dimensions per vertex function

**Definition 2.2 (Agent Dimensionality).**
For agent `a ∈ A`, dimensionality `κ(a)` determines the space in which the agent operates:

| κ(a) | Type | Motion Space |
|------|------|--------------|
| 1 | Rail (B) | 1D — linear path |
| 2 | Mobile (A) | 2D — planar surface |
| 3 | Drone (C) | 3D — volumetric space |

**Definition 2.3 (Vertex Compatibility).**
Agent `a` can occupy vertex `v` iff `κ(a) ∈ δ(v)`.

This formally expresses that:
- Rail robots can only access vertices along rails
- Mobile robots can access floor vertices
- Drones can access airspace vertices of the appropriate layer

### 2.2 Space-Time Representation

**Definition 2.4 (Space-Time State).**
For an agent of dimensionality `d`, the state in the space-time graph is:

```
s = (v, t, λ)  where  v ∈ V, t ∈ ℝ≥0, λ ∈ Λ
```

- For `d = 1`: `λ` is position on rail segment (real number)
- For `d = 2`: `λ = ∅` (not needed)
- For `d = 3`: `λ ∈ {0, 5, 10, 15}` is altitude layer in meters

### 2.3 Motion Semantics

**Axiom 2.1 (Dimensionality Conservation).**
An agent cannot change its dimensionality during execution:

```
∀a ∈ A, ∀t₁, t₂ ∈ ℝ≥0: κ(a, t₁) = κ(a, t₂)
```

**Axiom 2.2 (Inter-Dimensional Transition).**
An agent of dimension `d₁` can interact with an agent of dimension `d₂` only at vertices where their dimensions intersect:

```
interact(a₁, a₂, v) ⟹ κ(a₁) ∈ δ(v) ∧ κ(a₂) ∈ δ(v)
```

---

## 3. Dimensional Conflict Decomposition

### 3.1 Motivation

In standard CBS algorithm, all conflicts are treated uniformly. However, the nature of a conflict fundamentally depends on the dimensionality of the involved agents. A conflict between two rail robots on the same rail has a completely different structure than a conflict between a drone and a mobile robot at a handoff point.

### 3.2 Conflict Taxonomy

**Definition 3.1 (Dimensional Conflict).**
Let `a₁, a₂ ∈ A` be two agents with dimensionalities `d₁ = κ(a₁)` and `d₂ = κ(a₂)`. The conflict between them is classified according to the pair `(d₁, d₂)`:

| Class | (d₁, d₂) | Name | Description |
|-------|----------|------|-------------|
| C₁ | (1, 1) | LINEAR | Conflict on same rail segment |
| C₂ | (2, 2) | PLANAR | Conflict in 2D plane |
| C₃ | (1, 2) | CROSSING | Conflict at rail-floor intersection |
| C₄ | (3, 3) | AERIAL | Conflict in same airspace layer |
| C₅ | (3, 3) | VERTICAL | Conflict in vertical corridor |
| C₆ | (3, 1) ∪ (3, 2) | AIR-GROUND | Conflict at handoff point |

### 3.3 Resolution Strategies

For each conflict class, we define a specific resolution strategy:

**Strategy 3.1 (LINEAR — C₁).**
For a conflict between two rail robots on segment `S`:

```
resolve_linear(a₁, a₂, S, t) =
  if position(a₁, S) < position(a₂, S):
    constrain(a₂, wait_at_junction, t)
  else:
    constrain(a₁, wait_at_junction, t)
```

Rationale: Rail robots cannot pass each other on a segment. One must wait at a junction.

**Strategy 3.2 (PLANAR — C₂).**
Standard CBS resolution with two children:

```
resolve_planar(a₁, a₂, v, t) =
  child₁ = add_constraint(a₁, v, t)
  child₂ = add_constraint(a₂, v, t)
  return {child₁, child₂}
```

**Strategy 3.3 (CROSSING — C₃).**
For a conflict between rail and mobile robot:

```
resolve_crossing(a_rail, a_mobile, v, t) =
  // Priority to rail robot (higher inertia)
  child₁ = add_constraint(a_mobile, v, [t-ε, t+ε])
  // Alternative: rail waits
  child₂ = add_constraint(a_rail, v, [t-ε, t+ε])
  return {child₁, child₂}
```

**Strategy 3.4 (AERIAL — C₄).**
For conflict in same airspace layer:

```
resolve_aerial(a₁, a₂, v, λ, t) =
  // Similar to PLANAR, but in 3D
  child₁ = add_constraint(a₁, v, λ, t)
  child₂ = add_constraint(a₂, v, λ, t)
  return {child₁, child₂}
```

**Strategy 3.5 (VERTICAL — C₅).**
For conflict in vertical corridor:

```
resolve_vertical(a₁, a₂, corridor, λ₁, λ₂, t) =
  // One drone must change layer or wait
  if λ₁ < λ₂:
    child₁ = add_constraint(a₂, corridor, [λ₁, λ₂], t)
  else:
    child₁ = add_constraint(a₁, corridor, [λ₂, λ₁], t)
  return {child₁}
```

Rationale: A vertical corridor has capacity for one drone. Sequential passage is mandatory.

**Strategy 3.6 (AIR-GROUND — C₆).**
For conflict at handoff point:

```
resolve_air_ground(a_drone, a_ground, v, t) =
  // Synchronization: drone must land before interaction
  landing_time = compute_landing_time(a_drone, v)
  child₁ = add_constraint(a_ground, v, [t, landing_time])
  child₂ = add_constraint(a_drone, v, [t, t + service_time])
  return {child₁, child₂}
```

### 3.4 MIXED-CBS Algorithm

```
Algorithm: MIXED-CBS
Input: MD-MAPF instance I
Output: Solution π or FAILURE

1.  root ← compute_initial_assignment(I)
2.  root.paths ← plan_all_paths(root, I)
3.  OPEN ← {root}
4.
5.  while OPEN ≠ ∅:
6.      N ← extract_min(OPEN)
7.
8.      conflict ← find_first_conflict(N.paths)
9.      if conflict = ∅:
10.         return N  // Solution found
11.
12.     dim ← classify_dimension(conflict)
13.     children ← resolve_by_dimension(N, conflict, dim)
14.
15.     for each child in children:
16.         child.paths ← replan_affected(child, conflict.agents)
17.         if child.paths ≠ ∅:
18.             OPEN ← OPEN ∪ {child}
19.
20. return FAILURE
```

### 3.5 Theoretical Properties

**Theorem 3.1 (Completeness).**
MIXED-CBS is complete: if a solution exists, the algorithm will find it.

*Proof (sketch).* Dimensional decomposition does not eliminate valid solutions — each resolution strategy generates constraints that cover all possible conflict resolutions. By induction on search tree depth, every branch leads to either a valid solution or an empty path set. ∎

**Theorem 3.2 (Makespan Optimality).**
MIXED-CBS returns a solution with minimal makespan.

*Proof (sketch).* Best-first search by cost function (makespan) guarantees that the first conflict-free node found is optimal. Dimensional decomposition does not alter the cost function. ∎

---

## 4. Energy-Constrained CBS (E-CBS)

### 4.1 Motivation

Drones have limited battery capacity. A path that is spatially and temporally valid may be energy-infeasible. An algorithm that integrates energy constraints into the planning process is required.

### 4.2 Energy Model

**Definition 4.1 (Drone Energy State).**
We extend the drone state with an energy component:

```
s = (v, t, λ, e)  where  e ∈ [0, E_max]
```

where `e` is remaining energy in Wh, and `E_max` is battery capacity.

**Definition 4.2 (Energy Consumption).**
For action `α` with duration `Δt` and distance `Δd`:

```
consume(α, Δt, Δd) = P(α) · Δt / 3600  [Wh]
```

where `P(α)` is power in W:

| Action α | P(α) [W] |
|----------|----------|
| HOVER | 50 |
| MOVE_HORIZONTAL | 75 |
| CLIMB | 125 |
| DESCEND | 40 |

**Definition 4.3 (Path Energy Validity).**
Path `π = [(v₀, t₀), (v₁, t₁), ..., (vₖ, tₖ)]` is energy-valid iff:

```
∀i ∈ [1, k]: e_i = e_{i-1} - consume(action(v_{i-1}, v_i), t_i - t_{i-1}, dist(v_{i-1}, v_i)) > 0
```

with recharging at stations:

```
if is_charging_pad(v_i): e_i = E_max
```

### 4.3 Automatic Charging Station Insertion

**Definition 4.4 (Energy Violation).**
An energy violation is a triple:

```
ε = (a, t_depleted, v_depleted)
```

indicating that agent `a` will run out of energy at time `t_depleted` at position `v_depleted`.

**Algorithm 4.1 (Energy Violation Resolution).**

```
resolve_energy_violation(node, ε):
  pad ← find_nearest_charging_pad(ε.v_depleted)
  t_reach ← ε.t_depleted - safety_margin

  constraint ← EnergyConstraint(
    robot = ε.a,
    must_reach_by = t_reach,
    pad = pad
  )

  child ← copy(node)
  child.energy_constraints ← child.energy_constraints ∪ {constraint}
  child.goals[ε.a] ← insert_waypoint(pad, before_tasks)

  return {child}
```

### 4.4 E-CBS Algorithm

```
Algorithm: E-CBS (Energy-Constrained CBS)
Input: MD-MAPF instance I with energy parameters
Output: Energy-valid solution π or FAILURE

1.  root ← compute_initial_assignment(I)
2.  root.paths ← plan_all_paths_3d(root, I)
3.  OPEN ← {root}
4.
5.  while OPEN ≠ ∅:
6.      N ← extract_min(OPEN)
7.
8.      // First check energy validity
9.      energy_violation ← simulate_energy(N.paths, I)
10.     if energy_violation ≠ ∅:
11.         children ← resolve_energy_violation(N, energy_violation)
12.         for each child in children:
13.             child.paths ← replan_affected(child, energy_violation.agent)
14.             if child.paths ≠ ∅:
15.                 OPEN ← OPEN ∪ {child}
16.         continue
17.
18.     // Then check spatial conflicts
19.     conflict ← find_first_conflict(N.paths)
20.     if conflict = ∅:
21.         return N
22.
23.     children ← resolve_conflict(N, conflict)
24.     for each child in children:
25.         child.paths ← replan_affected(child, conflict.agents)
26.         if child.paths ≠ ∅ and verify_must_reach_by(child):
27.             OPEN ← OPEN ∪ {child}
28.
29. return FAILURE
```

### 4.5 Separation of Horizontal and Vertical Consumption

**Lemma 4.1.**
During a layer change, total energy consumption is:

```
E_total = E_horizontal + E_vertical
```

where:

```
E_horizontal = P(MOVE_HORIZONTAL) · d_xy / v_horizontal / 3600
E_vertical = P(CLIMB|DESCEND) · |Δz| / v_vertical / 3600
```

This prevents double-counting of the vertical component that would occur if using 3D Euclidean distance.

---

## 5. Layered Airspace Model

### 5.1 Structure

**Definition 5.1 (Airspace Layer).**
Airspace is divided into discrete layers:

```
Λ = {λ₀, λ₁, λ₂, λ₃} = {0m, 5m, 10m, 15m}
```

with semantics:
- `λ₀` (Ground): Landing, charging, handoff
- `λ₁` (Handoff): Interaction with ground robots
- `λ₂` (Work): Task execution
- `λ₃` (Transit): Fast transit without conflicts

**Definition 5.2 (Vertical Corridor).**
A vertical corridor is a set of vertices `C ⊂ V` such that:

```
∀v ∈ C: is_corridor(v) = true
∀λ ∈ Λ: ∃! v ∈ C: layer(v) = λ
```

Corridors are the only points where drones can change layers.

### 5.2 Transition Matrices

**Definition 5.3 (Permitted Layer Transition).**
Matrix of permitted transitions:

```
        λ₀  λ₁  λ₂  λ₃
    λ₀   ✓   ✓   ✗   ✗
T = λ₁   ✓   ✓   ✓   ✗
    λ₂   ✗   ✓   ✓   ✓
    λ₃   ✗   ✗   ✓   ✓
```

Rationale: A drone cannot skip layers (e.g., directly from ground to work layer).

### 5.3 Corridor Capacity

**Axiom 5.1 (Corridor Exclusivity).**
At any time, at most one drone can be in a vertical corridor:

```
∀C ∈ Corridors, ∀t: |{a ∈ A : position(a, t) ∈ C}| ≤ 1
```

This implies that vertical conflicts require sequentialization.

---

## 6. Hybrid Planning with Potential Fields

### 6.1 Concept

HYBRID-CBS combines:
1. **CBS for global planning** — Guarantees optimality and completeness
2. **Potential field for local execution** — Reacts to deviations in real-time

### 6.2 Field Definition

**Definition 6.1 (Potential Field).**
A potential field is a function `Φ: V → ℝ` defined as:

```
Φ(v) = Φ_load(v) - Φ_repulsive(v) + Φ_thermal(v)
```

where:
- `Φ_load(v)` — Attraction toward tasks (higher load = higher attraction)
- `Φ_repulsive(v)` — Repulsion from other robots
- `Φ_thermal(v)` — Repulsion from overheated zones

**Definition 6.2 (Modified Heuristic).**
The A* heuristic is modified:

```
h'(v) = h(v) - λ · Φ(v)
```

where `λ ∈ [0, 1]` is the field influence weight factor.

### 6.3 Deviation Detection

**Definition 6.3 (Deviation).**
Deviation of agent `a` at time `t` is:

```
dev(a, t) = dist(actual_pos(a, t), planned_pos(a, t))
```

**Rule 6.1 (Local Replanning).**
If `dev(a, t) > threshold`, trigger local replanning for horizon `H`:

```
if dev(a, t) > threshold:
  local_path ← field_guided_astar(actual_pos(a, t), planned_pos(a, t + H))
  merge(path[a], local_path)
```

---

## 7. Formal Definitions

### 7.1 Notation

| Symbol | Meaning |
|--------|---------|
| `G = (V, E)` | Workspace graph |
| `A` | Set of agents |
| `T` | Set of tasks |
| `κ(a)` | Agent dimensionality |
| `δ(v)` | Permitted dimensions at vertex |
| `π_a` | Path of agent `a` |
| `λ` | Altitude layer |
| `e` | Remaining energy |
| `Φ(v)` | Vertex potential |

### 7.2 System Invariants

**Invariant 7.1 (Conflict-Freedom).**
```
∀a₁, a₂ ∈ A, a₁ ≠ a₂, ∀t: position(a₁, t) ≠ position(a₂, t) ∨ is_shared(position(a₁, t))
```

**Invariant 7.2 (Energy Sustainability).**
```
∀a ∈ A, ∀t: energy(a, t) > 0 ∨ is_charging_pad(position(a, t))
```

**Invariant 7.3 (Dimensional Consistency).**
```
∀a ∈ A, ∀t: κ(a) ∈ δ(position(a, t))
```

---

## 8. Summary

This document specifies the four main algorithms in the MAPF-HET system:

1. **MD-MAPF** — MAPF formulation for heterogeneous agents of different dimensionalities

2. **Dimensional Conflict Decomposition** — Taxonomy of six conflict classes with specific resolution strategies

3. **E-CBS** — CBS extension with energy constraints and automatic charging station insertion

4. **Layered Airspace** — Discretization of 3D space into layers with corridors for vertical transitions

---

## Implementation References

- `internal/algo/mixed_cbs.go` — MIXED-CBS implementation
- `internal/algo/energy_cbs.go` — E-CBS implementation
- `internal/algo/astar3d.go` — 3D A* with energy
- `internal/core/airspace.go` — Airspace model
- `internal/algo/hybrid_cbs.go` — Hybrid planning
- `internal/algo/potential_field.go` — Potential field

---

*Document prepared for internal documentation of the Elektrokombinacija project.*

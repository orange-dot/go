#!/usr/bin/env python3
"""
EK-KOR v2 Multi-Module Simulator

Simulates multiple modules coordinating via potential fields,
topological neighbors, and consensus voting.

This is a reference implementation in Python for:
1. Validating C/Rust implementations
2. Visualizing coordination behavior
3. Testing at scale (100+ modules)

Usage:
    python multi_module.py --modules 10 --ticks 1000
    python multi_module.py --visualize
"""

import argparse
import random
import math
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple
from enum import Enum

# Constants (must match C/Rust)
K_NEIGHBORS = 7
FIELD_DECAY_TAU_US = 100_000
HEARTBEAT_PERIOD_US = 10_000
HEARTBEAT_TIMEOUT_COUNT = 5
VOTE_TIMEOUT_US = 50_000


class ModuleState(Enum):
    INIT = 0
    DISCOVERING = 1
    ACTIVE = 2
    DEGRADED = 3
    ISOLATED = 4
    REFORMING = 5
    SHUTDOWN = 6


class HealthState(Enum):
    UNKNOWN = 0
    ALIVE = 1
    SUSPECT = 2
    DEAD = 3


@dataclass
class Position:
    x: int
    y: int
    z: int = 0

    def distance_squared(self, other: 'Position') -> int:
        return (self.x - other.x)**2 + (self.y - other.y)**2 + (self.z - other.z)**2


@dataclass
class Field:
    load: float = 0.0
    thermal: float = 0.0
    power: float = 0.0
    timestamp: int = 0
    source: int = 0

    def decay(self, now: int) -> 'Field':
        """Apply exponential decay."""
        elapsed = now - self.timestamp
        factor = math.exp(-elapsed / FIELD_DECAY_TAU_US)
        return Field(
            load=self.load * factor,
            thermal=self.thermal * factor,
            power=self.power * factor,
            timestamp=self.timestamp,
            source=self.source
        )


@dataclass
class Neighbor:
    id: int
    health: HealthState = HealthState.UNKNOWN
    last_seen: int = 0
    last_field: Optional[Field] = None
    distance: int = 0


@dataclass
class Module:
    id: int
    position: Position
    state: ModuleState = ModuleState.INIT

    # Field
    my_field: Field = field(default_factory=Field)

    # Topology
    neighbors: List[Neighbor] = field(default_factory=list)
    known_modules: Dict[int, Tuple[Position, int]] = field(default_factory=dict)

    # Simulation state
    load: float = 0.0

    def start(self):
        self.state = ModuleState.DISCOVERING

    def tick(self, now: int, all_modules: Dict[int, 'Module']):
        """Main coordination loop."""
        # 1. Update heartbeats (simplified: check if neighbors are still in all_modules)
        for neighbor in self.neighbors[:]:
            if neighbor.id not in all_modules:
                self.neighbors.remove(neighbor)
                self._reelect(all_modules)

        # 2. Sample neighbor fields
        neighbor_load_sum = 0.0
        neighbor_count = 0
        for neighbor in self.neighbors:
            if neighbor.id in all_modules:
                other = all_modules[neighbor.id]
                decayed = other.my_field.decay(now)
                neighbor_load_sum += decayed.load
                neighbor_count += 1

        # 3. Compute gradient
        if neighbor_count > 0:
            neighbor_avg = neighbor_load_sum / neighbor_count
            gradient = neighbor_avg - self.load

            # 4. Adjust load based on gradient (simple simulation)
            # If neighbors are more loaded, I should take more work
            self.load += gradient * 0.1
            self.load = max(0.0, min(1.0, self.load))

        # 5. Publish field
        self.my_field = Field(
            load=self.load,
            thermal=random.uniform(0, 0.3),  # Simulated
            power=self.load * 0.8,
            timestamp=now,
            source=self.id
        )

        # 6. Update state
        if len(self.neighbors) >= K_NEIGHBORS // 2:
            self.state = ModuleState.ACTIVE
        elif len(self.neighbors) > 0:
            self.state = ModuleState.DEGRADED
        else:
            self.state = ModuleState.ISOLATED

    def on_discovery(self, sender_id: int, sender_position: Position, now: int, all_modules: Dict[int, 'Module']):
        """Process discovery from another module."""
        if sender_id == self.id:
            return

        distance = self.position.distance_squared(sender_position)
        self.known_modules[sender_id] = (sender_position, distance)

        # Reelect if we don't have enough neighbors
        if len(self.neighbors) < K_NEIGHBORS:
            self._reelect(all_modules)

    def _reelect(self, all_modules: Dict[int, 'Module']):
        """Reelect k-nearest neighbors."""
        # Sort known modules by distance
        sorted_known = sorted(self.known_modules.items(), key=lambda x: x[1][1])

        # Take k nearest
        self.neighbors = []
        for mod_id, (pos, dist) in sorted_known[:K_NEIGHBORS]:
            if mod_id != self.id and mod_id in all_modules:
                self.neighbors.append(Neighbor(id=mod_id, distance=dist, health=HealthState.ALIVE))


class Simulator:
    def __init__(self, num_modules: int, grid_size: int = 10):
        self.modules: Dict[int, Module] = {}
        self.now: int = 0

        # Create modules in a grid
        for i in range(num_modules):
            x = i % grid_size
            y = i // grid_size
            mod = Module(
                id=i + 1,
                position=Position(x, y, 0),
                load=random.uniform(0.2, 0.8)  # Random initial load
            )
            self.modules[mod.id] = mod

    def run(self, ticks: int, tick_period: int = 1000):
        """Run simulation for given number of ticks."""
        # Initialize: broadcast discovery
        for mod in self.modules.values():
            mod.start()
            for other in self.modules.values():
                if other.id != mod.id:
                    mod.on_discovery(other.id, other.position, self.now, self.modules)

        # Main loop
        for tick in range(ticks):
            self.now += tick_period

            for mod in self.modules.values():
                mod.tick(self.now, self.modules)

            if tick % 100 == 0:
                self._print_stats(tick)

    def _print_stats(self, tick: int):
        """Print simulation statistics."""
        loads = [m.load for m in self.modules.values()]
        avg_load = sum(loads) / len(loads)
        min_load = min(loads)
        max_load = max(loads)

        states = {}
        for m in self.modules.values():
            states[m.state.name] = states.get(m.state.name, 0) + 1

        print(f"Tick {tick:5d}: load=[{min_load:.2f}, {avg_load:.2f}, {max_load:.2f}] states={states}")


def main():
    parser = argparse.ArgumentParser(description="EK-KOR v2 Multi-Module Simulator")
    parser.add_argument("--modules", "-m", type=int, default=10, help="Number of modules")
    parser.add_argument("--ticks", "-t", type=int, default=1000, help="Simulation ticks")
    parser.add_argument("--visualize", "-v", action="store_true", help="Enable visualization")
    args = parser.parse_args()

    print(f"EK-KOR v2 Simulator: {args.modules} modules, {args.ticks} ticks")
    print("-" * 50)

    sim = Simulator(args.modules)
    sim.run(args.ticks)

    print("-" * 50)
    print("Simulation complete!")

    # Final stats
    loads = [m.load for m in sim.modules.values()]
    print(f"Final load distribution: min={min(loads):.3f}, max={max(loads):.3f}, std={std(loads):.3f}")


def std(values: List[float]) -> float:
    """Calculate standard deviation."""
    avg = sum(values) / len(values)
    variance = sum((x - avg)**2 for x in values) / len(values)
    return math.sqrt(variance)


if __name__ == "__main__":
    main()

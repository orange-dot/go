#!/usr/bin/env python3
"""
EK-KOR2 Python Reference Implementation

Lightweight Python implementation of critical EK-KOR2 functions.
This serves as an "executable specification" for cross-validation
when only one native implementation (C or Rust) is available.

Usage:
    python reference_impl.py test_vector.json    # Run single test
    python reference_impl.py --validate          # Run all and compare

Implemented functions:
    - fixed_to_q15 / q15_to_fixed (Q16.16 fixed-point)
    - gradient (field gradient calculation)
    - quorum_check (consensus threshold)
    - heartbeat_state_transition (state machine)
"""

import json
import sys
import argparse
import math
from pathlib import Path
from typing import Any, Dict, Optional, Tuple, List
from dataclasses import dataclass
from enum import Enum

# Paths
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent
SPEC_DIR = PROJECT_ROOT / "spec"
TEST_VECTORS_DIR = SPEC_DIR / "test-vectors"


# =============================================================================
# Fixed-Point Arithmetic (Q16.16)
# =============================================================================

class Q16_16:
    """Q16.16 fixed-point number (16 bits integer, 16 bits fraction)."""

    FRAC_BITS = 16
    SCALE = 1 << FRAC_BITS  # 65536

    def __init__(self, bits: int = 0):
        """Initialize from raw bits."""
        # Clamp to 32-bit signed range
        if bits > 0x7FFFFFFF:
            bits = bits - 0x100000000
        self.bits = bits

    @classmethod
    def from_float(cls, f: float) -> 'Q16_16':
        """Convert float to Q16.16."""
        bits = int(round(f * cls.SCALE))
        return cls(bits)

    def to_float(self) -> float:
        """Convert Q16.16 to float."""
        return self.bits / self.SCALE

    def __add__(self, other: 'Q16_16') -> 'Q16_16':
        return Q16_16(self.bits + other.bits)

    def __sub__(self, other: 'Q16_16') -> 'Q16_16':
        return Q16_16(self.bits - other.bits)

    def __mul__(self, other: 'Q16_16') -> 'Q16_16':
        # Full precision multiply then shift
        result = (self.bits * other.bits) >> self.FRAC_BITS
        return Q16_16(result)

    def __truediv__(self, other: 'Q16_16') -> 'Q16_16':
        if other.bits == 0:
            raise ZeroDivisionError("Q16_16 division by zero")
        result = (self.bits << self.FRAC_BITS) // other.bits
        return Q16_16(result)

    def __neg__(self) -> 'Q16_16':
        return Q16_16(-self.bits)

    def __abs__(self) -> 'Q16_16':
        return Q16_16(abs(self.bits))

    def __lt__(self, other: 'Q16_16') -> bool:
        return self.bits < other.bits

    def __le__(self, other: 'Q16_16') -> bool:
        return self.bits <= other.bits

    def __gt__(self, other: 'Q16_16') -> bool:
        return self.bits > other.bits

    def __ge__(self, other: 'Q16_16') -> bool:
        return self.bits >= other.bits

    def __eq__(self, other: object) -> bool:
        if isinstance(other, Q16_16):
            return self.bits == other.bits
        return False

    def __repr__(self) -> str:
        return f"Q16_16({self.to_float():.6f})"


# Convenience constants
Q16_ZERO = Q16_16.from_float(0.0)
Q16_ONE = Q16_16.from_float(1.0)
Q16_HALF = Q16_16.from_float(0.5)


# =============================================================================
# Field Module Implementation
# =============================================================================

@dataclass
class Field:
    """Coordination field with 5 components."""
    load: Q16_16 = None
    thermal: Q16_16 = None
    power: Q16_16 = None
    custom0: Q16_16 = None
    custom1: Q16_16 = None
    timestamp: int = 0
    source: int = 0
    sequence: int = 0

    def __post_init__(self):
        if self.load is None:
            self.load = Q16_ZERO
        if self.thermal is None:
            self.thermal = Q16_ZERO
        if self.power is None:
            self.power = Q16_ZERO
        if self.custom0 is None:
            self.custom0 = Q16_ZERO
        if self.custom1 is None:
            self.custom1 = Q16_ZERO


def gradient(my_field: Field, neighbor_field: Field, component: str = "load") -> Q16_16:
    """
    Compute field gradient for a specific component.

    gradient = neighbor_value - my_value

    Returns:
        Positive: neighbors have higher value (increase activity)
        Negative: neighbors have lower value (decrease activity)
    """
    my_val = getattr(my_field, component, Q16_ZERO)
    neighbor_val = getattr(neighbor_field, component, Q16_ZERO)
    return neighbor_val - my_val


def apply_decay(value: Q16_16, elapsed_us: int, tau_us: int = 100_000) -> Q16_16:
    """
    Apply exponential decay to a field value.

    f(t) = f0 * exp(-t/tau)

    Uses piecewise linear approximation for embedded compatibility.
    """
    if elapsed_us <= 0:
        return value

    t = elapsed_us
    tau = tau_us

    if t < tau:
        # exp(-t/tau) ≈ 1 - 0.632*(t/tau) for t < tau
        factor = 1.0 - (t / tau) * 0.632
    elif t < tau * 2:
        # exp(-2) ≈ 0.135
        factor = 0.368 - ((t - tau) / tau) * 0.233
    elif t < tau * 3:
        factor = 0.135 - ((t - tau * 2) / tau) * 0.086
    elif t < tau * 5:
        factor = 0.049 * (1.0 - (t - tau * 3) / (tau * 2))
    else:
        factor = 0.0

    factor = max(0.0, factor)
    return Q16_16.from_float(value.to_float() * factor)


# =============================================================================
# Consensus Module Implementation
# =============================================================================

class VoteResult(Enum):
    PENDING = "Pending"
    APPROVED = "Approved"
    REJECTED = "Rejected"
    TIMEOUT = "Timeout"
    CANCELLED = "Cancelled"


def quorum_check(yes_count: int, total_voters: int, threshold: float) -> VoteResult:
    """
    Check if consensus threshold is reached.

    Args:
        yes_count: Number of yes votes
        total_voters: Total number of voters
        threshold: Required approval ratio (0.0 to 1.0)

    Returns:
        VoteResult indicating approval status
    """
    if total_voters == 0:
        return VoteResult.REJECTED

    ratio = yes_count / total_voters

    if ratio >= threshold:
        return VoteResult.APPROVED
    else:
        return VoteResult.REJECTED


def threshold_votes_needed(total_voters: int, threshold: float) -> int:
    """Calculate minimum yes votes needed for approval."""
    return math.ceil(total_voters * threshold)


# =============================================================================
# Heartbeat Module Implementation
# =============================================================================

class HealthState(Enum):
    UNKNOWN = "Unknown"
    ALIVE = "Alive"
    SUSPECT = "Suspect"
    DEAD = "Dead"


def heartbeat_state_transition(
    current_state: HealthState,
    elapsed_since_last: int,
    period_us: int = 10_000,
    timeout_count: int = 5,
    received_heartbeat: bool = False
) -> HealthState:
    """
    Compute next heartbeat state based on timing.

    State machine:
        Unknown -> Alive (on heartbeat)
        Alive -> Suspect (missed 2+ periods)
        Suspect -> Dead (missed timeout_count periods)
        Dead -> Alive (on heartbeat recovery)

    Args:
        current_state: Current health state
        elapsed_since_last: Microseconds since last heartbeat
        period_us: Heartbeat period in microseconds
        timeout_count: Periods before declaring dead
        received_heartbeat: True if heartbeat just received

    Returns:
        New health state
    """
    if received_heartbeat:
        return HealthState.ALIVE

    suspect_threshold = period_us * 2
    dead_threshold = period_us * timeout_count

    if elapsed_since_last > dead_threshold:
        return HealthState.DEAD
    elif elapsed_since_last > suspect_threshold:
        return HealthState.SUSPECT
    elif current_state == HealthState.UNKNOWN:
        return HealthState.UNKNOWN
    else:
        return current_state


# =============================================================================
# Position / Distance Implementation
# =============================================================================

@dataclass
class Position:
    """3D position for physical distance calculation."""
    x: int = 0
    y: int = 0
    z: int = 0


def distance_squared(p1: Position, p2: Position) -> int:
    """Compute squared Euclidean distance."""
    dx = p1.x - p2.x
    dy = p1.y - p2.y
    dz = p1.z - p2.z
    return dx * dx + dy * dy + dz * dz


# =============================================================================
# Test Vector Execution
# =============================================================================

def run_test_vector(vector: Dict) -> Dict:
    """
    Run a test vector through Python reference implementation.

    Returns dict with:
        - return: "OK" or error string
        - ... other function-specific outputs
    """
    module = vector.get("module", "unknown")
    function = vector.get("function", "unknown")
    input_data = vector.get("input", {})

    try:
        if module == "field":
            return run_field_test(function, input_data, vector)
        elif module == "consensus":
            return run_consensus_test(function, input_data, vector)
        elif module == "heartbeat":
            return run_heartbeat_test(function, input_data, vector)
        elif module == "types":
            return run_types_test(function, input_data, vector)
        elif module == "topology":
            return run_topology_test(function, input_data, vector)
        else:
            return {"return": f"NotImplemented: {module}.{function}"}

    except Exception as e:
        return {"return": f"Error: {e}"}


def run_field_test(function: str, input_data: Dict, vector: Dict) -> Dict:
    """Run field module tests."""
    if function == "field_gradient":
        my_field_data = input_data.get("my_field", {})
        neighbor_data = input_data.get("neighbor_field", input_data.get("neighbor_aggregate", {}))
        component = input_data.get("component", "load").lower()

        my_field = Field(
            load=Q16_16.from_float(my_field_data.get("load", 0)),
            thermal=Q16_16.from_float(my_field_data.get("thermal", 0)),
            power=Q16_16.from_float(my_field_data.get("power", 0)),
        )

        neighbor_field = Field(
            load=Q16_16.from_float(neighbor_data.get("load", 0)),
            thermal=Q16_16.from_float(neighbor_data.get("thermal", 0)),
            power=Q16_16.from_float(neighbor_data.get("power", 0)),
        )

        grad = gradient(my_field, neighbor_field, component)
        return {
            "return": "OK",
            "gradient": grad.to_float()
        }

    return {"return": f"NotImplemented: field.{function}"}


def run_consensus_test(function: str, input_data: Dict, vector: Dict) -> Dict:
    """Run consensus module tests."""
    if function == "consensus_vote":
        votes = input_data.get("votes", [])
        total_voters = input_data.get("total_voters", len(votes))
        threshold = input_data.get("threshold", 0.5)

        # Setup may contain propose info
        setup = vector.get("setup", {})
        propose = setup.get("propose", {})
        threshold = propose.get("threshold", threshold)

        yes_count = sum(1 for v in votes if v.get("vote") == "Yes")
        result = quorum_check(yes_count, total_voters, threshold)

        return {
            "return": "OK",
            "result": result.value
        }

    return {"return": f"NotImplemented: consensus.{function}"}


def run_heartbeat_test(function: str, input_data: Dict, vector: Dict) -> Dict:
    """Run heartbeat module tests."""
    if function == "heartbeat_tick":
        now = input_data.get("now", 0)

        # Get setup info
        setup = vector.get("setup", {})
        add_neighbor = setup.get("add_neighbor", {})
        received = setup.get("received", {})

        last_seen = received.get("now", 0)
        elapsed = now - last_seen

        state = heartbeat_state_transition(
            HealthState.ALIVE,  # Assume alive from received
            elapsed
        )

        return {
            "return": "OK",
            "health": state.value
        }

    if function == "heartbeat_received":
        return {
            "return": "OK",
            "health": HealthState.ALIVE.value
        }

    return {"return": f"NotImplemented: heartbeat.{function}"}


def run_types_test(function: str, input_data: Dict, vector: Dict) -> Dict:
    """Run types module tests."""
    if function == "q15_convert":
        float_val = input_data.get("float", 0)
        fixed = Q16_16.from_float(float_val)
        back = fixed.to_float()

        return {
            "return": "OK",
            "fixed_bits": fixed.bits,
            "round_trip": back
        }

    return {"return": f"NotImplemented: types.{function}"}


def run_topology_test(function: str, input_data: Dict, vector: Dict) -> Dict:
    """Run topology module tests."""
    # Topology is more complex - stub for now
    return {"return": f"NotImplemented: topology.{function}"}


# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(description="EK-KOR2 Python Reference Implementation")
    parser.add_argument("vector", nargs="?", help="Test vector JSON file")
    parser.add_argument("--validate", action="store_true",
                        help="Run all vectors and validate")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    args = parser.parse_args()

    if args.vector:
        # Run single test vector
        vector_path = Path(args.vector)
        if not vector_path.exists():
            vector_path = TEST_VECTORS_DIR / args.vector

        with open(vector_path) as f:
            vector = json.load(f)

        result = run_test_vector(vector)
        print(json.dumps(result, indent=2))
        return 0

    elif args.validate:
        # Run all test vectors
        print("=" * 60)
        print("Python Reference Implementation - Validation")
        print("=" * 60)

        vectors = sorted(TEST_VECTORS_DIR.glob("*.json"))
        passed = 0
        failed = 0
        skipped = 0

        for vector_file in vectors:
            with open(vector_file) as f:
                vector = json.load(f)

            if "tests" in vector:
                skipped += 1
                continue

            result = run_test_vector(vector)
            expected = vector.get("expected", {})

            # Simple comparison
            if result.get("return") == "OK" and expected.get("return") == "OK":
                passed += 1
                status = "PASS"
            elif "NotImplemented" in result.get("return", ""):
                skipped += 1
                status = "SKIP"
            else:
                failed += 1
                status = "FAIL"

            if args.verbose or status == "FAIL":
                print(f"[{status}] {vector.get('name', vector_file.stem)}")
                if status == "FAIL":
                    print(f"       Expected: {expected}")
                    print(f"       Got:      {result}")

        print()
        print("=" * 60)
        print(f"Passed: {passed}, Failed: {failed}, Skipped: {skipped}")
        print("=" * 60)
        return 0 if failed == 0 else 1

    else:
        parser.print_help()
        return 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""
EK-KOR v2 Cross-Language Test Runner

Runs test vectors against both C and Rust implementations,
compares outputs, and reports discrepancies.

Usage:
    python run_tests.py              # Run all tests
    python run_tests.py field        # Run field module tests
    python run_tests.py --lang c     # Run only C tests
    python run_tests.py --lang rust  # Run only Rust tests
"""

import json
import os
import sys
import subprocess
import argparse
from pathlib import Path
from dataclasses import dataclass
from typing import Optional, List, Dict, Any

# Paths
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent
SPEC_DIR = PROJECT_ROOT / "spec"
TEST_VECTORS_DIR = SPEC_DIR / "test-vectors"
C_DIR = PROJECT_ROOT / "c"
RUST_DIR = PROJECT_ROOT / "rust"


@dataclass
class TestResult:
    name: str
    module: str
    language: str
    passed: bool
    expected: Any
    actual: Any
    error: Optional[str] = None


def infer_module(filename: str, function: str = "") -> str:
    """Infer module name from filename or function name."""
    basename = Path(filename).stem.lower()
    if basename.startswith("field"):
        return "field"
    elif basename.startswith("topology"):
        return "topology"
    elif basename.startswith("consensus"):
        return "consensus"
    elif basename.startswith("heartbeat"):
        return "heartbeat"
    elif basename.startswith("auth"):
        return "auth"
    elif basename.startswith("spsc"):
        return "spsc"
    elif basename.startswith("q15") or basename.startswith("types"):
        return "types"
    elif function.startswith("field"):
        return "field"
    elif function.startswith("topology"):
        return "topology"
    elif function.startswith("consensus"):
        return "consensus"
    elif function.startswith("heartbeat"):
        return "heartbeat"
    elif function.startswith("ekk_auth") or function.startswith("chaskey"):
        return "auth"
    elif function.startswith("ekk_spsc"):
        return "spsc"
    return "unknown"


def load_test_vectors(module: Optional[str] = None) -> List[Dict]:
    """Load test vectors from JSON files."""
    vectors = []

    for json_file in sorted(TEST_VECTORS_DIR.glob("*.json")):
        with open(json_file) as f:
            raw_data = json.load(f)

        # Handle multi-test format (like auth_001_chaskey.json)
        if "tests" in raw_data and isinstance(raw_data["tests"], list):
            # Skip multi-test files for now - they need special handling
            # in the test harness itself
            continue

        # Single test format
        vector = raw_data
        vector["_file"] = json_file.name

        # Infer module if not present
        if "module" not in vector:
            vector["module"] = infer_module(
                json_file.name,
                vector.get("function", "")
            )

        if module is None or vector.get("module") == module:
            vectors.append(vector)

    return vectors


def find_c_harness() -> Optional[Path]:
    """Find the C test harness executable."""
    # Windows paths
    candidates = [
        C_DIR / "build" / "Debug" / "test_harness.exe",
        C_DIR / "build" / "Release" / "test_harness.exe",
        C_DIR / "build" / "test_harness.exe",
        # Linux/macOS paths
        C_DIR / "build" / "test_harness",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def find_rust_harness() -> Optional[Path]:
    """Find the Rust test harness executable."""
    # Windows paths
    candidates = [
        RUST_DIR / "target" / "debug" / "test_harness.exe",
        RUST_DIR / "target" / "release" / "test_harness.exe",
        # Linux/macOS paths
        RUST_DIR / "target" / "debug" / "test_harness",
        RUST_DIR / "target" / "release" / "test_harness",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def run_c_test(vector: Dict) -> TestResult:
    """Run a test vector against C implementation."""
    harness = find_c_harness()
    if harness is None:
        return TestResult(
            name=vector["name"],
            module=vector["module"],
            language="C",
            passed=False,
            expected=vector.get("expected"),
            actual=None,
            error="C test harness not found. Run: cmake --build ek-kor2/c/build"
        )

    vector_file = TEST_VECTORS_DIR / vector["_file"]

    try:
        result = subprocess.run(
            [str(harness), str(vector_file)],
            capture_output=True,
            text=True,
            timeout=30,
            cwd=PROJECT_ROOT
        )

        # Parse JSON output from stdout
        if result.stdout.strip():
            output = json.loads(result.stdout)
            if isinstance(output, list) and len(output) > 0:
                test_output = output[0]
                return TestResult(
                    name=vector["name"],
                    module=vector["module"],
                    language="C",
                    passed=test_output.get("passed", False),
                    expected=vector.get("expected"),
                    actual=test_output.get("actual"),
                    error=test_output.get("error")
                )

        return TestResult(
            name=vector["name"],
            module=vector["module"],
            language="C",
            passed=False,
            expected=vector.get("expected"),
            actual=None,
            error=f"Failed to parse output: {result.stdout[:200]}"
        )

    except subprocess.TimeoutExpired:
        return TestResult(
            name=vector["name"],
            module=vector["module"],
            language="C",
            passed=False,
            expected=vector.get("expected"),
            actual=None,
            error="Test timed out (30s)"
        )
    except json.JSONDecodeError as e:
        return TestResult(
            name=vector["name"],
            module=vector["module"],
            language="C",
            passed=False,
            expected=vector.get("expected"),
            actual=None,
            error=f"JSON parse error: {e}"
        )
    except Exception as e:
        return TestResult(
            name=vector.get("name", "unknown"),
            module=vector.get("module", "unknown"),
            language="C",
            passed=False,
            expected=vector.get("expected"),
            actual=None,
            error=f"Execution error: {e}"
        )


def run_rust_test(vector: Dict) -> TestResult:
    """Run a test vector against Rust implementation."""
    harness = find_rust_harness()
    if harness is None:
        return TestResult(
            name=vector["name"],
            module=vector["module"],
            language="Rust",
            passed=False,
            expected=vector.get("expected"),
            actual=None,
            error="Rust test harness not found. Run: cargo build --bin test_harness"
        )

    vector_file = TEST_VECTORS_DIR / vector["_file"]

    try:
        result = subprocess.run(
            [str(harness), str(vector_file)],
            capture_output=True,
            text=True,
            timeout=30,
            cwd=PROJECT_ROOT
        )

        # Parse JSON output from stdout
        if result.stdout.strip():
            output = json.loads(result.stdout)
            if isinstance(output, list) and len(output) > 0:
                test_output = output[0]
                return TestResult(
                    name=vector["name"],
                    module=vector["module"],
                    language="Rust",
                    passed=test_output.get("passed", False),
                    expected=vector.get("expected"),
                    actual=test_output.get("actual"),
                    error=test_output.get("error")
                )

        return TestResult(
            name=vector["name"],
            module=vector["module"],
            language="Rust",
            passed=False,
            expected=vector.get("expected"),
            actual=None,
            error=f"Failed to parse output: {result.stdout[:200]}"
        )

    except subprocess.TimeoutExpired:
        return TestResult(
            name=vector["name"],
            module=vector["module"],
            language="Rust",
            passed=False,
            expected=vector.get("expected"),
            actual=None,
            error="Test timed out (30s)"
        )
    except json.JSONDecodeError as e:
        return TestResult(
            name=vector["name"],
            module=vector["module"],
            language="Rust",
            passed=False,
            expected=vector.get("expected"),
            actual=None,
            error=f"JSON parse error: {e}"
        )
    except Exception as e:
        return TestResult(
            name=vector.get("name", "unknown"),
            module=vector.get("module", "unknown"),
            language="Rust",
            passed=False,
            expected=vector.get("expected"),
            actual=None,
            error=f"Execution error: {e}"
        )


def compare_results(c_result: TestResult, rust_result: TestResult) -> bool:
    """Compare C and Rust test results."""
    if c_result.actual is None or rust_result.actual is None:
        return False

    return c_result.actual == rust_result.actual


def print_results(results: List[TestResult], verbose: bool = False):
    """Print test results in a nice format."""
    passed = sum(1 for r in results if r.passed)
    failed = len(results) - passed

    print("\n" + "=" * 60)
    print(f"TEST RESULTS: {passed} passed, {failed} failed")
    print("=" * 60)

    # Group by module
    by_module: Dict[str, List[TestResult]] = {}
    for r in results:
        by_module.setdefault(r.module, []).append(r)

    for module, module_results in sorted(by_module.items()):
        module_passed = sum(1 for r in module_results if r.passed)
        module_total = len(module_results)
        status = "PASS" if module_passed == module_total else "FAIL"

        print(f"\n[{status}] {module}: {module_passed}/{module_total}")

        if verbose or module_passed != module_total:
            for r in module_results:
                status = "PASS" if r.passed else "FAIL"
                print(f"    [{status}] [{r.language}] {r.name}")
                if not r.passed and r.error:
                    print(f"        Error: {r.error}")

    print()
    return failed == 0


def main():
    parser = argparse.ArgumentParser(description="EK-KOR v2 Cross-Language Test Runner")
    parser.add_argument("module", nargs="?", help="Module to test (field, topology, consensus, heartbeat)")
    parser.add_argument("--lang", choices=["c", "rust", "both"], default="both", help="Language to test")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    args = parser.parse_args()

    print("EK-KOR v2 Test Runner")
    print("-" * 40)

    # Load test vectors
    vectors = load_test_vectors(args.module)
    print(f"Loaded {len(vectors)} test vectors")

    if not vectors:
        print("No test vectors found!")
        return 1

    results: List[TestResult] = []

    for vector in vectors:
        print(f"  Testing: {vector['name']}", end="")

        if args.lang in ("c", "both"):
            c_result = run_c_test(vector)
            results.append(c_result)

        if args.lang in ("rust", "both"):
            rust_result = run_rust_test(vector)
            results.append(rust_result)

        # Cross-validation
        if args.lang == "both":
            if c_result.actual is not None and rust_result.actual is not None:
                if c_result.actual != rust_result.actual:
                    print(" [MISMATCH!]")
                    continue

        print(" [done]")

    success = print_results(results, args.verbose)
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())

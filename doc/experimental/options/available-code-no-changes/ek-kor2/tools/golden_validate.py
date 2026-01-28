#!/usr/bin/env python3
"""
Golden Output Validation Tool

Validates test harness outputs against saved golden reference files.
Use this for regression testing when only one implementation is available.

Usage:
    python golden_validate.py                  # Validate all tests
    python golden_validate.py --lang c         # Validate C outputs
    python golden_validate.py field            # Validate field module only
    python golden_validate.py --update         # Update golden files on pass

The validation compares:
1. Current harness output vs golden output
2. Reports any discrepancies
"""

import json
import sys
import subprocess
import argparse
from pathlib import Path
from typing import Optional, Dict, Any, List
from dataclasses import dataclass

# Paths
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent
SPEC_DIR = PROJECT_ROOT / "spec"
TEST_VECTORS_DIR = SPEC_DIR / "test-vectors"
GOLDEN_DIR = SPEC_DIR / "golden-outputs"
C_DIR = PROJECT_ROOT / "c"
RUST_DIR = PROJECT_ROOT / "rust"


@dataclass
class ValidationResult:
    test_id: str
    module: str
    passed: bool
    golden_file: str
    error: Optional[str] = None
    diff: Optional[Dict] = None


def find_harness(lang: str) -> Optional[Path]:
    """Find test harness executable."""
    if lang == "c":
        candidates = [
            C_DIR / "build" / "Debug" / "test_harness.exe",
            C_DIR / "build" / "Release" / "test_harness.exe",
            C_DIR / "build" / "test_harness.exe",
            C_DIR / "build" / "test_harness",
        ]
    else:
        candidates = [
            RUST_DIR / "target" / "debug" / "test_harness.exe",
            RUST_DIR / "target" / "release" / "test_harness.exe",
            RUST_DIR / "target" / "debug" / "test_harness",
            RUST_DIR / "target" / "release" / "test_harness",
        ]

    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def run_harness(harness: Path, vector_file: Path) -> Optional[Dict]:
    """Run test harness and return output."""
    try:
        result = subprocess.run(
            [str(harness), str(vector_file)],
            capture_output=True,
            text=True,
            timeout=30,
            cwd=PROJECT_ROOT
        )

        if result.stdout.strip():
            output = json.loads(result.stdout)
            if isinstance(output, list) and len(output) > 0:
                return output[0]
        return None

    except Exception as e:
        return None


def compare_values(expected: Any, actual: Any, path: str = "") -> List[str]:
    """Deep compare values, return list of differences."""
    diffs = []

    if type(expected) != type(actual):
        diffs.append(f"{path}: type mismatch ({type(expected).__name__} vs {type(actual).__name__})")
        return diffs

    if isinstance(expected, dict):
        all_keys = set(expected.keys()) | set(actual.keys())
        for key in all_keys:
            key_path = f"{path}.{key}" if path else key
            if key not in expected:
                diffs.append(f"{key_path}: unexpected key in actual")
            elif key not in actual:
                diffs.append(f"{key_path}: missing in actual")
            else:
                diffs.extend(compare_values(expected[key], actual[key], key_path))

    elif isinstance(expected, list):
        if len(expected) != len(actual):
            diffs.append(f"{path}: array length mismatch ({len(expected)} vs {len(actual)})")
        else:
            for i, (e, a) in enumerate(zip(expected, actual)):
                diffs.extend(compare_values(e, a, f"{path}[{i}]"))

    elif isinstance(expected, (int, float)):
        # Numeric tolerance
        if isinstance(expected, float) or isinstance(actual, float):
            tolerance = max(abs(expected) * 0.01, 0.0001)  # 1% or 0.0001
            if abs(float(expected) - float(actual)) > tolerance:
                diffs.append(f"{path}: {expected} != {actual} (diff={abs(expected-actual)})")
        elif expected != actual:
            diffs.append(f"{path}: {expected} != {actual}")

    elif expected != actual:
        diffs.append(f"{path}: {expected!r} != {actual!r}")

    return diffs


def validate_golden(vector_file: Path, lang: str) -> ValidationResult:
    """Validate a test vector against its golden output."""
    golden_file = GOLDEN_DIR / f"{vector_file.stem}.{lang}.golden.json"

    # Load test vector
    with open(vector_file) as f:
        vector = json.load(f)

    test_id = vector.get("id", vector_file.stem)
    module = vector.get("module", "unknown")

    if not golden_file.exists():
        return ValidationResult(
            test_id=test_id,
            module=module,
            passed=False,
            golden_file=golden_file.name,
            error="Golden file not found"
        )

    # Load golden
    with open(golden_file) as f:
        golden = json.load(f)

    # Run harness
    harness = find_harness(lang)
    if harness is None:
        return ValidationResult(
            test_id=test_id,
            module=module,
            passed=False,
            golden_file=golden_file.name,
            error=f"{lang} harness not found"
        )

    result = run_harness(harness, vector_file)
    if result is None:
        return ValidationResult(
            test_id=test_id,
            module=module,
            passed=False,
            golden_file=golden_file.name,
            error="Harness returned no output"
        )

    # Compare actual outputs
    golden_actual = golden.get("actual", {})
    current_actual = result.get("actual", result)

    diffs = compare_values(golden_actual, current_actual)

    if diffs:
        return ValidationResult(
            test_id=test_id,
            module=module,
            passed=False,
            golden_file=golden_file.name,
            diff={"differences": diffs}
        )

    return ValidationResult(
        test_id=test_id,
        module=module,
        passed=True,
        golden_file=golden_file.name
    )


def main():
    parser = argparse.ArgumentParser(description="Golden Output Validation Tool")
    parser.add_argument("module", nargs="?", help="Module to validate")
    parser.add_argument("--lang", choices=["c", "rust"], default="rust",
                        help="Language to validate (default: rust)")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    args = parser.parse_args()

    print("=" * 60)
    print("Golden Output Validation Tool")
    print("=" * 60)
    print(f"Language: {args.lang}")
    print(f"Module filter: {args.module or 'all'}")
    print()

    # Find test vectors with golden files
    vectors = sorted(TEST_VECTORS_DIR.glob("*.json"))

    if args.module:
        vectors = [v for v in vectors if v.stem.startswith(args.module)]

    # Filter to only those with golden files
    vectors_with_golden = []
    for v in vectors:
        golden_file = GOLDEN_DIR / f"{v.stem}.{args.lang}.golden.json"
        if golden_file.exists():
            vectors_with_golden.append(v)

    print(f"Found {len(vectors_with_golden)} test vectors with golden files")
    print()

    if not vectors_with_golden:
        print("No golden files found. Run golden_capture.py first.")
        return 1

    results: List[ValidationResult] = []

    for vector_file in vectors_with_golden:
        result = validate_golden(vector_file, args.lang)
        results.append(result)

        status = "PASS" if result.passed else "FAIL"
        print(f"[{status}] {result.test_id}")

        if not result.passed and (result.error or result.diff):
            if result.error:
                print(f"       Error: {result.error}")
            if result.diff and args.verbose:
                for diff in result.diff.get("differences", []):
                    print(f"       - {diff}")

    # Summary
    passed = sum(1 for r in results if r.passed)
    failed = len(results) - passed

    print()
    print("=" * 60)
    print(f"Results: {passed} passed, {failed} failed")
    print("=" * 60)

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

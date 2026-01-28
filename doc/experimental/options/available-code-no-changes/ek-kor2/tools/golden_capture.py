#!/usr/bin/env python3
"""
Golden Output Capture Tool

Captures test harness outputs and saves them as golden reference files.
These golden files can be used for regression testing when only one
implementation (C or Rust) is available.

Usage:
    python golden_capture.py                  # Capture all tests
    python golden_capture.py --lang rust      # Capture Rust outputs only
    python golden_capture.py field            # Capture field module only
    python golden_capture.py --dry-run        # Show what would be captured

The golden outputs are saved to spec/golden-outputs/*.json
"""

import json
import os
import sys
import subprocess
import argparse
from pathlib import Path
from datetime import datetime
from typing import Optional, List, Dict, Any

# Paths
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent
SPEC_DIR = PROJECT_ROOT / "spec"
TEST_VECTORS_DIR = SPEC_DIR / "test-vectors"
GOLDEN_DIR = SPEC_DIR / "golden-outputs"
C_DIR = PROJECT_ROOT / "c"
RUST_DIR = PROJECT_ROOT / "rust"


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
        print(f"  Error running harness: {e}", file=sys.stderr)
        return None


def capture_golden(vector_file: Path, lang: str, dry_run: bool = False) -> bool:
    """Capture golden output for a single test vector."""
    harness = find_harness(lang)
    if harness is None:
        print(f"  SKIP: {lang} harness not found")
        return False

    # Load test vector to get metadata
    with open(vector_file) as f:
        vector = json.load(f)

    if "tests" in vector:
        print(f"  SKIP: Multi-test format not supported")
        return False

    test_id = vector.get("id", vector_file.stem)
    test_name = vector.get("name", test_id)
    module = vector.get("module", "unknown")

    # Run harness
    result = run_harness(harness, vector_file)
    if result is None:
        print(f"  FAIL: No output from harness")
        return False

    # Build golden output
    golden = {
        "_meta": {
            "vector_file": vector_file.name,
            "captured_from": lang,
            "captured_at": datetime.utcnow().isoformat() + "Z",
            "test_id": test_id,
            "test_name": test_name,
            "module": module,
        },
        "expected": vector.get("expected", {}),
        "actual": result.get("actual", result),
        "passed": result.get("passed", False),
    }

    # Save golden
    golden_file = GOLDEN_DIR / f"{vector_file.stem}.{lang}.golden.json"

    if dry_run:
        print(f"  Would save: {golden_file.name}")
        return True

    GOLDEN_DIR.mkdir(parents=True, exist_ok=True)
    with open(golden_file, 'w') as f:
        json.dump(golden, f, indent=2)

    print(f"  Saved: {golden_file.name}")
    return True


def main():
    parser = argparse.ArgumentParser(description="Golden Output Capture Tool")
    parser.add_argument("module", nargs="?", help="Module to capture (field, topology, etc.)")
    parser.add_argument("--lang", choices=["c", "rust"], default="rust",
                        help="Language to capture from (default: rust)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Show what would be captured without saving")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    args = parser.parse_args()

    print("=" * 60)
    print("Golden Output Capture Tool")
    print("=" * 60)
    print(f"Language: {args.lang}")
    print(f"Module filter: {args.module or 'all'}")
    print(f"Dry run: {args.dry_run}")
    print()

    # Find test vectors
    vectors = sorted(TEST_VECTORS_DIR.glob("*.json"))

    if args.module:
        vectors = [v for v in vectors if v.stem.startswith(args.module)]

    print(f"Found {len(vectors)} test vectors")
    print()

    captured = 0
    failed = 0

    for vector_file in vectors:
        print(f"Processing: {vector_file.name}")
        if capture_golden(vector_file, args.lang, args.dry_run):
            captured += 1
        else:
            failed += 1

    print()
    print("=" * 60)
    print(f"Captured: {captured}, Failed: {failed}")
    print("=" * 60)

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

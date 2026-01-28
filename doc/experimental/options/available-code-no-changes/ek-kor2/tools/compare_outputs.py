#!/usr/bin/env python3
"""
EK-KOR v2 Output Comparison Tool

Compares outputs from C and Rust implementations
to find discrepancies.

Usage:
    python compare_outputs.py c_output.json rust_output.json
    python compare_outputs.py --interactive
"""

import json
import sys
import argparse
from pathlib import Path
from typing import Any, List, Tuple

def compare_values(c_val: Any, rust_val: Any, path: str = "") -> List[Tuple[str, Any, Any]]:
    """
    Recursively compare two values.
    Returns list of (path, c_value, rust_value) for differences.
    """
    differences = []

    if type(c_val) != type(rust_val):
        differences.append((path, c_val, rust_val))
        return differences

    if isinstance(c_val, dict):
        all_keys = set(c_val.keys()) | set(rust_val.keys())
        for key in all_keys:
            new_path = f"{path}.{key}" if path else key
            c_sub = c_val.get(key, "<missing>")
            rust_sub = rust_val.get(key, "<missing>")
            differences.extend(compare_values(c_sub, rust_sub, new_path))

    elif isinstance(c_val, list):
        if len(c_val) != len(rust_val):
            differences.append((path + ".length", len(c_val), len(rust_val)))
        for i, (c_item, rust_item) in enumerate(zip(c_val, rust_val)):
            differences.extend(compare_values(c_item, rust_item, f"{path}[{i}]"))

    elif isinstance(c_val, float):
        # Allow small floating point differences
        if abs(c_val - rust_val) > 0.0001:
            differences.append((path, c_val, rust_val))

    else:
        if c_val != rust_val:
            differences.append((path, c_val, rust_val))

    return differences


def format_diff(differences: List[Tuple[str, Any, Any]]) -> str:
    """Format differences for display."""
    if not differences:
        return "No differences found!"

    lines = ["Differences found:", ""]
    for path, c_val, rust_val in differences:
        lines.append(f"  {path}:")
        lines.append(f"    C:    {c_val}")
        lines.append(f"    Rust: {rust_val}")
        lines.append("")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Compare C and Rust outputs")
    parser.add_argument("c_output", nargs="?", help="C output JSON file")
    parser.add_argument("rust_output", nargs="?", help="Rust output JSON file")
    parser.add_argument("--interactive", "-i", action="store_true", help="Interactive mode")
    args = parser.parse_args()

    if args.interactive:
        print("Interactive comparison mode")
        print("Enter C output (JSON), then Ctrl+D:")
        c_input = sys.stdin.read()
        print("\nEnter Rust output (JSON), then Ctrl+D:")
        rust_input = sys.stdin.read()

        c_data = json.loads(c_input)
        rust_data = json.loads(rust_input)
    else:
        if not args.c_output or not args.rust_output:
            parser.error("Both c_output and rust_output required (or use --interactive)")

        with open(args.c_output) as f:
            c_data = json.load(f)
        with open(args.rust_output) as f:
            rust_data = json.load(f)

    differences = compare_values(c_data, rust_data)
    print(format_diff(differences))

    return 0 if not differences else 1


if __name__ == "__main__":
    sys.exit(main())

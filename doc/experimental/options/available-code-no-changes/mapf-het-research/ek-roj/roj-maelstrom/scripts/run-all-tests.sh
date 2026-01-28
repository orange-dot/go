#!/bin/bash
# Run all Maelstrom workloads at different scales.
#
# Prerequisites:
#   - Maelstrom binary in PATH or MAELSTROM_PATH env var
#   - Java 11+ runtime
#
# Usage:
#   ./scripts/run-all-tests.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Find Maelstrom binary
MAELSTROM="${MAELSTROM_PATH:-maelstrom}"

# Build binaries
echo "Building binaries..."
cd "$PROJECT_DIR"
go build -o bin/maelstrom-echo ./cmd/maelstrom-echo
go build -o bin/maelstrom-broadcast ./cmd/maelstrom-broadcast

PASSED=0
FAILED=0

run_test() {
    local name="$1"
    shift
    echo ""
    echo "========================================"
    echo "Running: $name"
    echo "========================================"
    if "$@"; then
        echo "PASSED: $name"
        ((PASSED++))
    else
        echo "FAILED: $name"
        ((FAILED++))
    fi
}

# Echo workload
run_test "Echo (1 node)" \
    "$MAELSTROM" test -w echo \
    --bin "$PROJECT_DIR/bin/maelstrom-echo" \
    --node-count 1 \
    --time-limit 10

# Broadcast - 1 node (trivial quorum)
run_test "Broadcast 1-node (trivial quorum)" \
    "$MAELSTROM" test -w broadcast \
    --bin "$PROJECT_DIR/bin/maelstrom-broadcast" \
    --node-count 1 \
    --time-limit 10 \
    --rate 10

# Broadcast - 3 nodes (quorum = 2)
run_test "Broadcast 3-node (quorum=2)" \
    "$MAELSTROM" test -w broadcast \
    --bin "$PROJECT_DIR/bin/maelstrom-broadcast" \
    --node-count 3 \
    --time-limit 20 \
    --rate 10

# Broadcast - 5 nodes (quorum = 4)
run_test "Broadcast 5-node (quorum=4)" \
    "$MAELSTROM" test -w broadcast \
    --bin "$PROJECT_DIR/bin/maelstrom-broadcast" \
    --node-count 5 \
    --time-limit 30 \
    --rate 100

# Broadcast - 5 nodes high rate
run_test "Broadcast 5-node high rate" \
    "$MAELSTROM" test -w broadcast \
    --bin "$PROJECT_DIR/bin/maelstrom-broadcast" \
    --node-count 5 \
    --time-limit 30 \
    --rate 1000

echo ""
echo "========================================"
echo "SUMMARY"
echo "========================================"
echo "Passed: $PASSED"
echo "Failed: $FAILED"
echo ""

if [ "$FAILED" -gt 0 ]; then
    echo "Some tests failed!"
    exit 1
else
    echo "All tests passed!"
fi

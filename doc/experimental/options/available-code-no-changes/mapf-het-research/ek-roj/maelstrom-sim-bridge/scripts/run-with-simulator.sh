#!/bin/bash
# Run Maelstrom broadcast workload via the Go simulator bridge.
#
# Prerequisites:
#   - Go simulator running on port 8001
#   - Maelstrom binary in PATH or MAELSTROM_PATH env var
#   - Java 11+ runtime
#
# Usage:
#   ./scripts/run-with-simulator.sh [node-count] [rate] [time-limit]
#
# To start the simulator:
#   cd simulator/engine && go run ./cmd/simulator

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
REPO_ROOT="$(dirname "$(dirname "$(dirname "$PROJECT_DIR")")")"

# Parse arguments
NODE_COUNT="${1:-5}"
RATE="${2:-10}"
TIME_LIMIT="${3:-60}"

# Find Maelstrom binary
MAELSTROM="${MAELSTROM_PATH:-maelstrom}"

# Simulator URL
SIMULATOR_URL="${SIMULATOR_URL:-http://localhost:8001}"

# Check simulator is running
echo "Checking simulator at $SIMULATOR_URL..."
if ! curl -s "$SIMULATOR_URL/health" > /dev/null 2>&1; then
    echo "ERROR: Simulator not running at $SIMULATOR_URL"
    echo ""
    echo "Start the simulator first:"
    echo "  cd $REPO_ROOT/simulator/engine"
    echo "  go run ./cmd/simulator"
    exit 1
fi

echo "Simulator is running"

# Build the bridge binary
echo "Building maelstrom-sim-bridge..."
cd "$PROJECT_DIR"
go build -o bin/maelstrom-sim-bridge .

echo ""
echo "Running broadcast workload via simulator bridge"
echo "================================================"
echo "  Simulator: $SIMULATOR_URL"
echo "  Nodes:     $NODE_COUNT"
echo "  Rate:      $RATE msg/sec"
echo "  Time:      $TIME_LIMIT seconds"
echo ""

export SIMULATOR_URL

"$MAELSTROM" test -w broadcast \
    --bin "$PROJECT_DIR/bin/maelstrom-sim-bridge" \
    --node-count "$NODE_COUNT" \
    --time-limit "$TIME_LIMIT" \
    --rate "$RATE"

echo ""
echo "Broadcast workload passed!"
echo ""
echo "View results:"
echo "  maelstrom serve   # Web UI at http://localhost:8080"
echo "  cat store/latest/jepsen.log"
echo ""
echo "Export Elle history from simulator:"
echo "  curl $SIMULATOR_URL/api/roj/history > elle-history.json"

#!/bin/bash
# Run Maelstrom broadcast workload with ROJ consensus.
#
# Prerequisites:
#   - Maelstrom binary in PATH or MAELSTROM_PATH env var
#   - Java 11+ runtime
#
# Usage:
#   ./scripts/run-broadcast.sh [node-count] [rate] [time-limit]
#
# Examples:
#   ./scripts/run-broadcast.sh           # Default: 3 nodes, 10/sec, 20s
#   ./scripts/run-broadcast.sh 5 100 30  # 5 nodes, 100/sec, 30s

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Parse arguments
NODE_COUNT="${1:-3}"
RATE="${2:-10}"
TIME_LIMIT="${3:-20}"

# Find Maelstrom binary
MAELSTROM="${MAELSTROM_PATH:-maelstrom}"

# Build the broadcast binary
echo "Building maelstrom-broadcast..."
cd "$PROJECT_DIR"
go build -o bin/maelstrom-broadcast ./cmd/maelstrom-broadcast

echo ""
echo "Running broadcast workload with ROJ consensus"
echo "=============================================="
echo "  Nodes:      $NODE_COUNT"
echo "  Rate:       $RATE msg/sec"
echo "  Time limit: $TIME_LIMIT seconds"
echo ""

# Calculate expected quorum threshold
THRESHOLD=$(echo "scale=0; ($NODE_COUNT * 2 + 2) / 3" | bc)
echo "  2/3 threshold: $THRESHOLD votes needed for commit"
echo ""

"$MAELSTROM" test -w broadcast \
    --bin "$PROJECT_DIR/bin/maelstrom-broadcast" \
    --node-count "$NODE_COUNT" \
    --time-limit "$TIME_LIMIT" \
    --rate "$RATE"

echo ""
echo "Broadcast workload passed!"
echo ""
echo "View results:"
echo "  maelstrom serve   # Web UI at http://localhost:8080"
echo "  cat store/latest/jepsen.log"

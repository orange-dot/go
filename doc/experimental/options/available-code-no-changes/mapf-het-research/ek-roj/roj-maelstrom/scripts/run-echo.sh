#!/bin/bash
# Run Maelstrom echo workload to validate infrastructure.
#
# Prerequisites:
#   - Maelstrom binary in PATH or MAELSTROM_PATH env var
#   - Java 11+ runtime
#
# Usage:
#   ./scripts/run-echo.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Find Maelstrom binary
MAELSTROM="${MAELSTROM_PATH:-maelstrom}"

# Build the echo binary
echo "Building maelstrom-echo..."
cd "$PROJECT_DIR"
go build -o bin/maelstrom-echo ./cmd/maelstrom-echo

# Run the echo test
echo ""
echo "Running echo workload (1 node, 10 seconds)..."
echo "=============================================="
"$MAELSTROM" test -w echo \
    --bin "$PROJECT_DIR/bin/maelstrom-echo" \
    --node-count 1 \
    --time-limit 10

echo ""
echo "Echo workload passed!"
echo ""
echo "View results:"
echo "  maelstrom serve   # Web UI at http://localhost:8080"
echo "  cat store/latest/jepsen.log"

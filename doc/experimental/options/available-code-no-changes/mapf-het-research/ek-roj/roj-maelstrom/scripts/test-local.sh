#!/bin/bash
# Local test script for Maelstrom binaries without Maelstrom harness.
# Tests the protocol message handling directly.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

echo "=== ROJ Maelstrom Local Tests ==="
echo ""

# Test 1: Echo workload
echo "Test 1: Echo Workload"
echo "  Input: init, echo(hello)"
RESULT=$(printf '%s\n' \
    '{"src":"c0","dest":"n0","body":{"type":"init","node_id":"n0","node_ids":["n0"]}}' \
    '{"src":"c0","dest":"n0","body":{"type":"echo","echo":"hello","msg_id":1}}' \
    | ./bin/maelstrom-echo.exe 2>/dev/null | grep -c echo_ok || true)

if [ "$RESULT" -ge 1 ]; then
    echo "  PASS: Received echo_ok response"
else
    echo "  FAIL: No echo_ok response"
    exit 1
fi
echo ""

# Test 2: Broadcast init and topology
echo "Test 2: Broadcast Init and Topology"
echo "  Input: init, topology"
RESULT=$(printf '%s\n' \
    '{"src":"c0","dest":"n0","body":{"type":"init","node_id":"n0","node_ids":["n0","n1","n2"]}}' \
    '{"src":"c0","dest":"n0","body":{"type":"topology","topology":{"n0":["n1","n2"],"n1":["n0","n2"],"n2":["n0","n1"]}}}' \
    | timeout 2 ./bin/maelstrom-broadcast.exe 2>/dev/null)

if echo "$RESULT" | grep -q "topology_ok"; then
    echo "  PASS: Received topology_ok response"
else
    echo "  FAIL: No topology_ok response"
    exit 1
fi
echo ""

# Test 3: Broadcast message creates proposal
echo "Test 3: Broadcast Creates ROJ Proposal"
echo "  Input: init, topology, broadcast(42)"
RESULT=$(printf '%s\n' \
    '{"src":"c0","dest":"n0","body":{"type":"init","node_id":"n0","node_ids":["n0","n1","n2"]}}' \
    '{"src":"c0","dest":"n0","body":{"type":"topology","topology":{"n0":["n1","n2"],"n1":["n0","n2"],"n2":["n0","n1"]}}}' \
    '{"src":"c0","dest":"n0","body":{"type":"broadcast","message":42}}' \
    | timeout 2 ./bin/maelstrom-broadcast.exe 2>/dev/null)

if echo "$RESULT" | grep -q "roj_propose"; then
    echo "  PASS: Sent roj_propose to neighbors"
else
    echo "  FAIL: No roj_propose sent"
    exit 1
fi

if echo "$RESULT" | grep -q "broadcast_ok"; then
    echo "  PASS: Received broadcast_ok response"
else
    echo "  FAIL: No broadcast_ok response"
    exit 1
fi
echo ""

# Test 4: Commit propagation
echo "Test 4: Commit Propagation"
echo "  Input: init, topology, roj_commit(99), read"
RESULT=$(printf '%s\n' \
    '{"src":"c0","dest":"n0","body":{"type":"init","node_id":"n0","node_ids":["n0","n1","n2"]}}' \
    '{"src":"c0","dest":"n0","body":{"type":"topology","topology":{"n0":["n1","n2"],"n1":["n0","n2"],"n2":["n0","n1"]}}}' \
    '{"src":"n1","dest":"n0","body":{"type":"roj_commit","value":99}}' \
    '{"src":"c0","dest":"n0","body":{"type":"read"}}' \
    | timeout 2 ./bin/maelstrom-broadcast.exe 2>/dev/null)

if echo "$RESULT" | grep -q '"messages":\[99\]'; then
    echo "  PASS: Read returns committed value [99]"
else
    echo "  FAIL: Committed value not found in read response"
    echo "  Got: $RESULT"
    exit 1
fi
echo ""

# Test 5: Empty read before commits
echo "Test 5: Empty Read Before Commits"
echo "  Input: init, topology, read"
RESULT=$(printf '%s\n' \
    '{"src":"c0","dest":"n0","body":{"type":"init","node_id":"n0","node_ids":["n0","n1","n2"]}}' \
    '{"src":"c0","dest":"n0","body":{"type":"topology","topology":{"n0":["n1","n2"],"n1":["n0","n2"],"n2":["n0","n1"]}}}' \
    '{"src":"c0","dest":"n0","body":{"type":"read"}}' \
    | timeout 2 ./bin/maelstrom-broadcast.exe 2>/dev/null)

if echo "$RESULT" | grep -q '"messages":\[\]'; then
    echo "  PASS: Read returns empty [] before commits"
else
    echo "  FAIL: Read should return empty array"
    exit 1
fi
echo ""

echo "=== All Tests Passed ==="

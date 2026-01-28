#!/bin/bash
#
# ROJ-OCPP Federation Demo
#
# Demonstrates OCPP 2.0.1 commands achieving consensus across multiple nodes.
#
# Prerequisites:
#   - Docker (for SteVe OCPP CSMS)
#   - Rust toolchain (for building nodes)
#
# Usage:
#   ./ocpp_federation_demo.sh
#

set -e

# Configuration
STEVE_PORT=8180
ROJ_BASE_PORT=9990
NODES=("alpha" "beta" "gamma")
STATIONS=("CS001" "CS001" "CS001")  # All represent same station for federation demo

echo "╔══════════════════════════════════════════════════════════════════╗"
echo "║              ROJ-OCPP Federation Demo                            ║"
echo "╠══════════════════════════════════════════════════════════════════╣"
echo "║  This demo shows OCPP commands reaching consensus across nodes   ║"
echo "╚══════════════════════════════════════════════════════════════════╝"
echo

# Check for Docker
if ! command -v docker &> /dev/null; then
    echo "Error: Docker is required for this demo"
    echo "Install Docker and try again"
    exit 1
fi

# Step 1: Build the nodes
echo "=== Step 1: Building ROJ-OCPP nodes ==="
cd "$(dirname "$0")/.."
cargo build --release -p roj-ocpp-node
echo "Build complete."
echo

# Step 2: Start SteVe OCPP CSMS (mock backend)
echo "=== Step 2: Starting SteVe OCPP CSMS ==="
echo "Pulling SteVe Docker image..."

# Check if SteVe is already running
if docker ps | grep -q steve; then
    echo "SteVe is already running"
else
    # Try to start existing container or create new one
    if docker ps -a | grep -q steve; then
        echo "Starting existing SteVe container..."
        docker start steve
    else
        echo "Creating new SteVe container..."
        # Note: Using a simplified mock since SteVe requires more setup
        # In production, use the official SteVe image or another OCPP test server
        echo "For full testing, please set up SteVe manually:"
        echo "  https://github.com/steve-community/steve"
        echo
        echo "For this demo, we'll run nodes in standalone mode."
    fi
fi
echo

# Step 3: Start ROJ-OCPP nodes in separate terminals
echo "=== Step 3: Starting ROJ-OCPP nodes ==="
echo
echo "Please run the following commands in separate terminals:"
echo

for i in "${!NODES[@]}"; do
    name="${NODES[$i]}"
    station="${STATIONS[$i]}"
    port=$((ROJ_BASE_PORT + i))

    echo "Terminal $((i+1)) - Node $name:"
    echo "  cargo run --release -p roj-ocpp-node -- \\"
    echo "    --name $name \\"
    echo "    --station $station \\"
    echo "    --roj-port $port \\"
    echo "    --log-level info"
    echo
done

echo "=== Demo Scenario ==="
echo
echo "Once all nodes are running and connected to CSMS:"
echo
echo "1. In the CSMS web interface, send SetChargingProfile to any station"
echo "   - The receiving node will PROPOSE a power_limit change"
echo "   - Other nodes will VOTE accept"
echo "   - All nodes will COMMIT the same value"
echo
echo "2. Send RequestStartTransaction from CSMS"
echo "   - Session start reaches consensus across all nodes"
echo "   - Connector status changes to Occupied on all nodes"
echo
echo "3. Send ReserveNow from CSMS"
echo "   - Reservation reaches consensus"
echo "   - Connector status changes to Reserved on all nodes"
echo
echo "=== Expected Output ==="
echo
echo "On node alpha (receiving SetChargingProfile):"
echo "  INFO Translating SetChargingProfile: power_limit:CS001:1 = 22.0 kW"
echo "  INFO Creating ROJ PROPOSE: id=abc123, key=power_limit:CS001:1"
echo "  INFO Broadcasting proposal to 2 peers"
echo
echo "On nodes beta and gamma:"
echo "  INFO Received PROPOSE abc123 from alpha: power_limit:CS001:1=22.0"
echo "  INFO Voting ACCEPT on proposal abc123"
echo
echo "On all nodes (after consensus):"
echo "  INFO COMMIT power_limit:CS001:1 = 22.0 kW (voters: [alpha, beta, gamma])"
echo "  INFO Applying commit: power_limit:CS001:1 = {\"limit_kw\":22.0,...}"
echo

# Optional: Interactive mode
read -p "Press Enter to clean up (or Ctrl+C to keep running)..."

echo "=== Cleanup ==="
docker stop steve 2>/dev/null || true
echo "Demo complete."

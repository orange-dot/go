// maelstrom-sim-bridge bridges Maelstrom protocol to the Go simulator HTTP API.
//
// This allows running Maelstrom workloads against the existing Go simulator,
// which provides realistic CAN-FD bus simulation instead of in-memory channels.
//
// Architecture:
//
//	┌─────────────────────────────────────────────────────────────┐
//	│                     Maelstrom Harness                       │
//	│  (Simulates network, injects faults, checks correctness)    │
//	└──────────────────────────┬──────────────────────────────────┘
//	                           │ JSON/STDIN/STDOUT
//	                           ▼
//	┌─────────────────────────────────────────────────────────────┐
//	│                   maelstrom-sim-bridge                      │
//	│   Translates Maelstrom RPC to Simulator HTTP calls          │
//	└──────────────────────────┬──────────────────────────────────┘
//	                           │ HTTP
//	                           ▼
//	┌─────────────────────────────────────────────────────────────┐
//	│                    Go Simulator :8001                       │
//	│  ROJ Cluster + CAN-FD Bus Simulation + Elle History         │
//	└─────────────────────────────────────────────────────────────┘
//
// Usage:
//
//	# Start the Go simulator first
//	cd simulator/engine && go run ./cmd/simulator
//
//	# Run Maelstrom with the bridge
//	maelstrom test -w broadcast --bin ./maelstrom-sim-bridge \
//	    --node-count 5 --time-limit 60
//
// Environment variables:
//
//	SIMULATOR_URL: Base URL of the simulator (default: http://localhost:8001)
package main

import (
	"encoding/json"
	"fmt"
	"log"
	"os"
	"sync"

	maelstrom "github.com/jepsen-io/maelstrom/demo/go"

	"github.com/elektrokombinacija/maelstrom-sim-bridge/internal/adapter"
	"github.com/elektrokombinacija/maelstrom-sim-bridge/internal/protocol"
)

func main() {
	// Get simulator URL from environment
	simulatorURL := os.Getenv("SIMULATOR_URL")
	if simulatorURL == "" {
		simulatorURL = "http://localhost:8001"
	}

	// Create simulator client
	client := adapter.NewSimulatorClient(simulatorURL)

	// Check simulator is running
	if err := client.HealthCheck(); err != nil {
		log.Fatalf("Simulator not available at %s: %v", simulatorURL, err)
	}

	// Clear history for fresh test
	if err := client.ClearHistory(); err != nil {
		log.Printf("Warning: failed to clear history: %v", err)
	}

	n := maelstrom.NewNode()

	// Track committed messages (local cache)
	var mu sync.RWMutex
	committed := make(map[int]struct{})

	// Message counter for unique keys
	var msgCounter int

	// Handle topology message
	n.Handle("topology", func(msg maelstrom.Message) error {
		// We don't need topology for the bridge - the simulator handles routing
		return n.Reply(msg, map[string]any{
			"type": "topology_ok",
		})
	})

	// Handle broadcast - translate to simulator propose
	n.Handle("broadcast", func(msg maelstrom.Message) error {
		var body protocol.BroadcastRequest
		if err := json.Unmarshal(msg.Body, &body); err != nil {
			return err
		}

		value := body.Message

		// Check local cache first
		mu.RLock()
		_, exists := committed[value]
		mu.RUnlock()
		if exists {
			return n.Reply(msg, map[string]any{"type": "broadcast_ok"})
		}

		// Map Maelstrom node ID to simulator node index
		nodeIdx := protocol.NodeIDToIndex(n.ID())

		// Create unique key for this message
		mu.Lock()
		msgCounter++
		key := fmt.Sprintf("msg_%d_%d", nodeIdx, msgCounter)
		mu.Unlock()

		// Submit proposal to simulator
		_, err := client.Propose(nodeIdx, key, value)
		if err != nil {
			log.Printf("Propose failed: %v", err)
			// Still ack - Maelstrom expects broadcast_ok even on failures
			// The checker will detect missing values
		}

		// Mark as committed locally (optimistic)
		mu.Lock()
		committed[value] = struct{}{}
		mu.Unlock()

		return n.Reply(msg, map[string]any{
			"type": "broadcast_ok",
		})
	})

	// Handle read - get committed values from simulator
	n.Handle("read", func(msg maelstrom.Message) error {
		nodeIdx := protocol.NodeIDToIndex(n.ID())

		// Get state from simulator
		state, err := client.GetState(nodeIdx)
		if err != nil {
			log.Printf("GetState failed: %v", err)
			// Return empty on error
			return n.Reply(msg, map[string]any{
				"type":     "read_ok",
				"messages": []int{},
			})
		}

		// Extract message values from state
		// State is map[key]value, we need the unique values
		values := make([]int, 0, len(state.State))
		seen := make(map[int]struct{})

		for _, v := range state.State {
			// Value could be int, float64, or other types
			var intVal int
			switch val := v.(type) {
			case float64:
				intVal = int(val)
			case int:
				intVal = val
			case int64:
				intVal = int(val)
			default:
				continue
			}

			if _, exists := seen[intVal]; !exists {
				values = append(values, intVal)
				seen[intVal] = struct{}{}
			}
		}

		// Update local cache
		mu.Lock()
		for _, v := range values {
			committed[v] = struct{}{}
		}
		mu.Unlock()

		return n.Reply(msg, map[string]any{
			"type":     "read_ok",
			"messages": values,
		})
	})

	// Run the node
	if err := n.Run(); err != nil {
		log.Fatal(err)
	}
}

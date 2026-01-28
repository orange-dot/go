// Package protocol defines Maelstrom message types for the simulator bridge.
package protocol

// BroadcastRequest is the Maelstrom broadcast message body.
type BroadcastRequest struct {
	Type    string `json:"type"`
	Message int    `json:"message"`
}

// BroadcastResponse is the Maelstrom broadcast_ok message body.
type BroadcastResponse struct {
	Type string `json:"type"`
}

// ReadRequest is the Maelstrom read message body.
type ReadRequest struct {
	Type string `json:"type"`
}

// ReadResponse is the Maelstrom read_ok message body.
type ReadResponse struct {
	Type     string `json:"type"`
	Messages []int  `json:"messages"`
}

// TopologyRequest is the Maelstrom topology message body.
type TopologyRequest struct {
	Type     string              `json:"type"`
	Topology map[string][]string `json:"topology"`
}

// TopologyResponse is the Maelstrom topology_ok message body.
type TopologyResponse struct {
	Type string `json:"type"`
}

// NodeMapping maps Maelstrom node IDs (n0, n1, ...) to simulator node indices (0, 1, ...).
func NodeIDToIndex(nodeID string) int {
	if len(nodeID) < 2 {
		return 0
	}
	// Parse "n0", "n1", etc.
	idx := 0
	for i := 1; i < len(nodeID); i++ {
		idx = idx*10 + int(nodeID[i]-'0')
	}
	return idx
}

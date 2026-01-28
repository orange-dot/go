// maelstrom-broadcast implements the Maelstrom broadcast workload with ROJ consensus.
//
// This maps Maelstrom broadcast messages to ROJ's 2/3 threshold voting protocol:
//   - broadcast -> ROJ PROPOSE (initiate consensus)
//   - roj_propose -> Disseminate to neighbors and vote
//   - roj_vote -> Collect votes, check 2/3 threshold
//   - roj_commit -> Apply committed value
//   - read -> Return committed message set
//   - topology -> Store neighbor information
//
// Usage:
//
//	maelstrom test -w broadcast --bin ./maelstrom-broadcast \
//	    --node-count 5 --time-limit 30 --rate 100
package main

import (
	"encoding/json"
	"log"
	"sync"
	"time"

	maelstrom "github.com/jepsen-io/maelstrom/demo/go"

	"github.com/elektrokombinacija/roj-maelstrom/internal/consensus"
)

func main() {
	n := maelstrom.NewNode()

	// State is initialized lazily with sync.Once to handle concurrent message delivery
	var state *consensus.State
	var stateOnce sync.Once

	getState := func() *consensus.State {
		stateOnce.Do(func() {
			state = consensus.New(n.ID())
		})
		return state
	}

	// Handle topology message
	n.Handle("topology", func(msg maelstrom.Message) error {
		var body struct {
			Type     string              `json:"type"`
			Topology map[string][]string `json:"topology"`
		}
		if err := json.Unmarshal(msg.Body, &body); err != nil {
			return err
		}

		// Get our neighbors from the topology
		neighbors := body.Topology[n.ID()]

		// Count total nodes
		totalNodes := len(body.Topology)

		getState().SetTopology(neighbors, totalNodes)

		return n.Reply(msg, map[string]any{
			"type": "topology_ok",
		})
	})

	// Handle broadcast - client wants to broadcast a value
	n.Handle("broadcast", func(msg maelstrom.Message) error {
		var body struct {
			Type    string `json:"type"`
			Message int    `json:"message"`
		}
		if err := json.Unmarshal(msg.Body, &body); err != nil {
			return err
		}

		value := body.Message
		s := getState()

		// If already committed, just ack
		if s.IsCommitted(value) {
			return n.Reply(msg, map[string]any{
				"type": "broadcast_ok",
			})
		}

		// Create a proposal and start consensus
		proposalID := s.CreateProposal(value)
		if proposalID == "" {
			// Already committed (race condition)
			return n.Reply(msg, map[string]any{
				"type": "broadcast_ok",
			})
		}

		// Send ROJ_PROPOSE to all neighbors
		neighbors := s.GetNeighbors()
		for _, neighbor := range neighbors {
			if err := n.Send(neighbor, map[string]any{
				"type":        "roj_propose",
				"proposal_id": proposalID,
				"value":       value,
			}); err != nil {
				log.Printf("Failed to send propose to %s: %v", neighbor, err)
			}
		}

		return n.Reply(msg, map[string]any{
			"type": "broadcast_ok",
		})
	})

	// Handle ROJ propose message from peer
	n.Handle("roj_propose", func(msg maelstrom.Message) error {
		var body struct {
			Type       string `json:"type"`
			ProposalID string `json:"proposal_id"`
			Value      int    `json:"value"`
		}
		if err := json.Unmarshal(msg.Body, &body); err != nil {
			return err
		}

		s := getState()

		// Process the proposal
		shouldVote := s.HandlePropose(body.ProposalID, body.Value, msg.Src)

		if shouldVote {
			// Send vote back to proposer
			if err := n.Send(msg.Src, map[string]any{
				"type":        "roj_vote",
				"proposal_id": body.ProposalID,
				"vote":        string(consensus.VoteAccept),
			}); err != nil {
				log.Printf("Failed to send vote to %s: %v", msg.Src, err)
			}

			// Also propagate to our neighbors (gossip)
			neighbors := s.GetNeighbors()
			for _, neighbor := range neighbors {
				if neighbor != msg.Src {
					if err := n.Send(neighbor, map[string]any{
						"type":        "roj_propose",
						"proposal_id": body.ProposalID,
						"value":       body.Value,
					}); err != nil {
						log.Printf("Failed to propagate propose to %s: %v", neighbor, err)
					}
				}
			}
		}

		return nil
	})

	// Handle ROJ vote message
	n.Handle("roj_vote", func(msg maelstrom.Message) error {
		var body struct {
			Type       string `json:"type"`
			ProposalID string `json:"proposal_id"`
			Vote       string `json:"vote"`
		}
		if err := json.Unmarshal(msg.Body, &body); err != nil {
			return err
		}

		s := getState()

		// Process the vote
		committed, value := s.HandleVote(
			body.ProposalID,
			msg.Src,
			consensus.Vote(body.Vote),
		)

		if committed {
			// Broadcast commit to all neighbors
			neighbors := s.GetNeighbors()
			for _, neighbor := range neighbors {
				if err := n.Send(neighbor, map[string]any{
					"type":  "roj_commit",
					"value": value,
				}); err != nil {
					log.Printf("Failed to send commit to %s: %v", neighbor, err)
				}
			}
		}

		return nil
	})

	// Handle ROJ commit message
	n.Handle("roj_commit", func(msg maelstrom.Message) error {
		var body struct {
			Type  string `json:"type"`
			Value int    `json:"value"`
		}
		if err := json.Unmarshal(msg.Body, &body); err != nil {
			return err
		}

		s := getState()

		// Apply commit
		if !s.IsCommitted(body.Value) {
			s.HandleCommit(body.Value)

			// Propagate commit to neighbors (in case they missed it)
			neighbors := s.GetNeighbors()
			for _, neighbor := range neighbors {
				if neighbor != msg.Src {
					if err := n.Send(neighbor, map[string]any{
						"type":  "roj_commit",
						"value": body.Value,
					}); err != nil {
						log.Printf("Failed to propagate commit to %s: %v", neighbor, err)
					}
				}
			}
		}

		return nil
	})

	// Handle read - return all committed values
	n.Handle("read", func(msg maelstrom.Message) error {
		committed := getState().GetCommitted()
		return n.Reply(msg, map[string]any{
			"type":     "read_ok",
			"messages": committed,
		})
	})

	// Periodic cleanup of expired proposals
	go func() {
		ticker := time.NewTicker(5 * time.Second)
		defer ticker.Stop()
		for range ticker.C {
			// getState() is safe to call - it uses sync.Once
			getState().CleanupExpired()
		}
	}()

	// Run the node
	if err := n.Run(); err != nil {
		log.Fatal(err)
	}
}

// Package consensus provides k-threshold voting for ROJ protocol
package consensus

import (
	"encoding/json"
	"log"
	"sync"
	"time"

	"github.com/google/uuid"

	"github.com/elektrokombinacija/roj-node-go/transport"
)

const (
	VoteThreshold      = 2.0 / 3.0
	ProposalTimeoutSec = 10
)

// Vote represents a vote decision
type Vote string

const (
	VoteAccept Vote = "accept"
	VoteReject Vote = "reject"
)

// ProposalState tracks an in-progress proposal
type ProposalState struct {
	ProposalID string
	Key        string
	Value      json.RawMessage
	Timestamp  int64
	Votes      map[string]Vote
}

// Consensus manages the voting state machine
type Consensus struct {
	nodeID    string
	proposals map[string]*ProposalState
	state     map[string]json.RawMessage
	peerCount func() int
	mux       sync.Mutex
}

// New creates a new Consensus instance
func New(nodeID string, peerCountFn func() int) *Consensus {
	return &Consensus{
		nodeID:    nodeID,
		proposals: make(map[string]*ProposalState),
		state:     make(map[string]json.RawMessage),
		peerCount: peerCountFn,
	}
}

// GetState returns the current committed state
func (c *Consensus) GetState() map[string]json.RawMessage {
	c.mux.Lock()
	defer c.mux.Unlock()

	result := make(map[string]json.RawMessage, len(c.state))
	for k, v := range c.state {
		result[k] = v
	}
	return result
}

// CreateProposal creates a new proposal message
func (c *Consensus) CreateProposal(key string, value interface{}) *transport.Message {
	c.mux.Lock()
	defer c.mux.Unlock()

	proposalID := uuid.New().String()[:8]
	timestamp := time.Now().Unix()

	valueBytes, _ := json.Marshal(value)

	state := &ProposalState{
		ProposalID: proposalID,
		Key:        key,
		Value:      valueBytes,
		Timestamp:  timestamp,
		Votes:      make(map[string]Vote),
	}
	c.proposals[proposalID] = state

	log.Printf("[INFO] Consensus: Proposing %s=%v (id=%s)", key, value, proposalID)

	return &transport.Message{
		Type:       "PROPOSE",
		ProposalID: proposalID,
		From:       c.nodeID,
		Key:        key,
		Value:      valueBytes,
		Timestamp:  timestamp,
	}
}

// HandleProposal processes an incoming proposal and returns a vote
func (c *Consensus) HandleProposal(msg *transport.Message) *transport.Message {
	c.mux.Lock()
	defer c.mux.Unlock()

	log.Printf("[INFO] Consensus: Received PROPOSE %s=%s from %s",
		msg.Key, string(msg.Value), msg.From)

	// Store the proposal
	state := &ProposalState{
		ProposalID: msg.ProposalID,
		Key:        msg.Key,
		Value:      msg.Value,
		Timestamp:  msg.Timestamp,
		Votes:      make(map[string]Vote),
	}
	c.proposals[msg.ProposalID] = state

	// Simple acceptance for demo
	vote := VoteAccept
	log.Printf("[INFO] Consensus: VOTE %s for %s (2/3 threshold)", vote, msg.ProposalID)

	return &transport.Message{
		Type:       "VOTE",
		ProposalID: msg.ProposalID,
		From:       c.nodeID,
		Vote:       string(vote),
	}
}

// HandleVote processes an incoming vote, returns COMMIT if threshold reached
func (c *Consensus) HandleVote(msg *transport.Message) *transport.Message {
	c.mux.Lock()
	defer c.mux.Unlock()

	log.Printf("[INFO] Consensus: Received VOTE %s from %s for %s",
		msg.Vote, msg.From, msg.ProposalID)

	proposal, ok := c.proposals[msg.ProposalID]
	if !ok {
		log.Printf("[WARN] Unknown proposal: %s", msg.ProposalID)
		return nil
	}

	proposal.Votes[msg.From] = Vote(msg.Vote)

	// Count accepts
	acceptCount := 0
	for _, v := range proposal.Votes {
		if v == VoteAccept {
			acceptCount++
		}
	}

	totalPeers := c.peerCount() + 1 // Include ourselves
	threshold := int(float64(totalPeers)*VoteThreshold + 0.5)

	log.Printf("[INFO] Consensus: %d/%d votes (%d needed for threshold)",
		acceptCount, totalPeers, threshold)

	if acceptCount >= threshold {
		// Commit locally
		c.state[proposal.Key] = proposal.Value
		log.Printf("[INFO] Consensus: COMMIT %s=%s", proposal.Key, string(proposal.Value))

		// Collect voters
		var voters []string
		for nodeID, vote := range proposal.Votes {
			if vote == VoteAccept {
				voters = append(voters, nodeID)
			}
		}

		// Clean up
		delete(c.proposals, msg.ProposalID)

		return &transport.Message{
			Type:       "COMMIT",
			ProposalID: msg.ProposalID,
			Key:        proposal.Key,
			Value:      proposal.Value,
			Voters:     voters,
		}
	}

	return nil
}

// HandleCommit processes an incoming commit message
func (c *Consensus) HandleCommit(msg *transport.Message) {
	c.mux.Lock()
	defer c.mux.Unlock()

	log.Printf("[INFO] Consensus: COMMIT %s=%s (voters: %v)",
		msg.Key, string(msg.Value), msg.Voters)

	// Apply to local state
	c.state[msg.Key] = msg.Value

	// Clean up proposal if we had it
	delete(c.proposals, msg.ProposalID)
}

// CleanupExpired removes proposals that have timed out
func (c *Consensus) CleanupExpired() {
	c.mux.Lock()
	defer c.mux.Unlock()

	now := time.Now().Unix()
	for id, proposal := range c.proposals {
		if now-proposal.Timestamp > ProposalTimeoutSec {
			log.Printf("[WARN] Consensus: Proposal %s expired", id)
			delete(c.proposals, id)
		}
	}
}

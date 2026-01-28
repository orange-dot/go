// Package consensus provides k-threshold voting for ROJ protocol over Maelstrom.
//
// This implements the same 2/3 threshold voting used in roj-node-go and the
// Go simulator, adapted for Maelstrom's message-passing model.
package consensus

import (
	"sync"
	"time"

	"github.com/google/uuid"
)

// VoteThreshold is the fraction of nodes required for consensus (2/3).
const VoteThreshold = 2.0 / 3.0

// ProposalTimeoutSec is how long to wait before expiring a proposal.
const ProposalTimeoutSec = 10

// Vote represents a vote decision.
type Vote string

const (
	VoteAccept Vote = "accept"
	VoteReject Vote = "reject"
)

// ProposalState tracks an in-progress proposal.
type ProposalState struct {
	ProposalID string
	Value      int // The message being proposed
	Timestamp  time.Time
	Votes      map[string]Vote // nodeID -> vote
	Committed  bool
}

// State manages the ROJ consensus state machine for a single node.
type State struct {
	mu sync.RWMutex

	nodeID     string
	proposals  map[string]*ProposalState
	committed  map[int]struct{} // Set of committed messages
	neighbors  []string         // Topology neighbors
	totalNodes int              // Total cluster size
}

// New creates a new consensus State.
func New(nodeID string) *State {
	return &State{
		nodeID:    nodeID,
		proposals: make(map[string]*ProposalState),
		committed: make(map[int]struct{}),
		neighbors: make([]string, 0),
	}
}

// SetTopology sets the neighbors and total node count.
func (s *State) SetTopology(neighbors []string, totalNodes int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.neighbors = neighbors
	s.totalNodes = totalNodes
}

// GetNeighbors returns the current topology neighbors.
func (s *State) GetNeighbors() []string {
	s.mu.RLock()
	defer s.mu.RUnlock()
	result := make([]string, len(s.neighbors))
	copy(result, s.neighbors)
	return result
}

// CreateProposal creates a new proposal for a message value.
// Returns the proposal ID.
func (s *State) CreateProposal(value int) string {
	s.mu.Lock()
	defer s.mu.Unlock()

	// Don't propose already-committed values
	if _, exists := s.committed[value]; exists {
		return ""
	}

	proposalID := uuid.New().String()[:8]

	proposal := &ProposalState{
		ProposalID: proposalID,
		Value:      value,
		Timestamp:  time.Now(),
		Votes:      make(map[string]Vote),
		Committed:  false,
	}
	// Vote for our own proposal
	proposal.Votes[s.nodeID] = VoteAccept

	s.proposals[proposalID] = proposal
	return proposalID
}

// HandlePropose processes an incoming proposal from another node.
// Returns true if we should vote (haven't seen this value).
func (s *State) HandlePropose(proposalID string, value int, from string) bool {
	s.mu.Lock()
	defer s.mu.Unlock()

	// Already committed this value
	if _, exists := s.committed[value]; exists {
		return false
	}

	// Store proposal if we don't have it
	if _, exists := s.proposals[proposalID]; !exists {
		s.proposals[proposalID] = &ProposalState{
			ProposalID: proposalID,
			Value:      value,
			Timestamp:  time.Now(),
			Votes:      make(map[string]Vote),
			Committed:  false,
		}
	}

	return true
}

// HandleVote processes an incoming vote and returns true if quorum reached.
// If quorum is reached, the proposal is marked committed.
func (s *State) HandleVote(proposalID string, from string, vote Vote) (committed bool, value int) {
	s.mu.Lock()
	defer s.mu.Unlock()

	proposal, exists := s.proposals[proposalID]
	if !exists {
		return false, 0
	}

	if proposal.Committed {
		return false, 0
	}

	proposal.Votes[from] = vote

	// Count accepts
	acceptCount := 0
	for _, v := range proposal.Votes {
		if v == VoteAccept {
			acceptCount++
		}
	}

	// Calculate threshold: ceiling of (totalNodes * 2/3)
	threshold := int(float64(s.totalNodes)*VoteThreshold + 0.999)

	if acceptCount >= threshold {
		proposal.Committed = true
		s.committed[proposal.Value] = struct{}{}
		return true, proposal.Value
	}

	return false, 0
}

// HandleCommit processes a commit notification from another node.
func (s *State) HandleCommit(value int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.committed[value] = struct{}{}
}

// GetCommitted returns all committed message values.
func (s *State) GetCommitted() []int {
	s.mu.RLock()
	defer s.mu.RUnlock()

	result := make([]int, 0, len(s.committed))
	for v := range s.committed {
		result = append(result, v)
	}
	return result
}

// IsCommitted checks if a value has been committed.
func (s *State) IsCommitted(value int) bool {
	s.mu.RLock()
	defer s.mu.RUnlock()
	_, exists := s.committed[value]
	return exists
}

// CleanupExpired removes proposals that have timed out.
func (s *State) CleanupExpired() {
	s.mu.Lock()
	defer s.mu.Unlock()

	now := time.Now()
	for id, proposal := range s.proposals {
		if !proposal.Committed && now.Sub(proposal.Timestamp) > time.Duration(ProposalTimeoutSec)*time.Second {
			delete(s.proposals, id)
		}
	}
}

// GetProposal returns a proposal by ID.
func (s *State) GetProposal(proposalID string) *ProposalState {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.proposals[proposalID]
}

// NodeID returns this node's ID.
func (s *State) NodeID() string {
	return s.nodeID
}

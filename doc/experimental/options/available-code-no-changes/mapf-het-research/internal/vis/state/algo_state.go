package state

import (
	"sync"

	"github.com/elektrokombinacija/mapf-het-research/internal/algo"
	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// CBSNodeInfo represents a node in the CBS constraint tree for visualization.
type CBSNodeInfo struct {
	ID          int
	ParentID    int // -1 for root
	Constraints []algo.Constraint
	Cost        float64
	IsOpen      bool
	IsSolution  bool
	Conflict    *algo.Conflict
	Paths       map[core.RobotID]core.Path
}

// AlgoState manages algorithm execution state.
type AlgoState struct {
	mu sync.Mutex

	Active   bool
	Paused   bool
	Stepping bool

	// CBS tree nodes
	Nodes       []*CBSNodeInfo
	CurrentNode int // Index of current node being expanded
	OpenSet     []int
	ClosedSet   []int

	// Statistics
	NodesExpanded   int
	ConflictsFound  int
	CurrentConflict *algo.Conflict

	// For step-by-step execution
	stepChan chan struct{}
	doneChan chan *core.Solution
}

// NewAlgoState creates a new algorithm state.
func NewAlgoState() *AlgoState {
	return &AlgoState{
		Nodes:     make([]*CBSNodeInfo, 0),
		OpenSet:   make([]int, 0),
		ClosedSet: make([]int, 0),
		stepChan:  make(chan struct{}, 1),
		doneChan:  make(chan *core.Solution, 1),
	}
}

// Start begins algorithm execution.
func (a *AlgoState) Start() {
	a.mu.Lock()
	defer a.mu.Unlock()

	a.Active = true
	a.Paused = false
	a.Stepping = false
	a.Nodes = make([]*CBSNodeInfo, 0)
	a.OpenSet = make([]int, 0)
	a.ClosedSet = make([]int, 0)
	a.NodesExpanded = 0
	a.ConflictsFound = 0
	a.CurrentConflict = nil
	a.CurrentNode = -1
}

// Stop ends algorithm execution.
func (a *AlgoState) Stop() {
	a.mu.Lock()
	defer a.mu.Unlock()

	a.Active = false
	a.Paused = false
	a.Stepping = false
}

// Pause pauses execution.
func (a *AlgoState) Pause() {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.Paused = true
}

// Resume resumes execution.
func (a *AlgoState) Resume() {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.Paused = false
	// Signal step channel in case waiting
	select {
	case a.stepChan <- struct{}{}:
	default:
	}
}

// Step enables step mode and signals one step.
func (a *AlgoState) Step() {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.Paused = true
	a.Stepping = true
	select {
	case a.stepChan <- struct{}{}:
	default:
	}
}

// WaitForStep blocks until a step is allowed.
func (a *AlgoState) WaitForStep() {
	a.mu.Lock()
	paused := a.Paused
	a.mu.Unlock()

	if !paused {
		return
	}

	<-a.stepChan
}

// ShouldPause returns whether the algorithm should pause.
func (a *AlgoState) ShouldPause() bool {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.Paused
}

// AddNode adds a CBS node to the tree.
func (a *AlgoState) AddNode(node *CBSNodeInfo) {
	a.mu.Lock()
	defer a.mu.Unlock()

	node.ID = len(a.Nodes)
	a.Nodes = append(a.Nodes, node)
	a.OpenSet = append(a.OpenSet, node.ID)
}

// ExpandNode marks a node as being expanded.
func (a *AlgoState) ExpandNode(nodeID int) {
	a.mu.Lock()
	defer a.mu.Unlock()

	a.CurrentNode = nodeID
	a.NodesExpanded++

	// Move from open to closed
	for i, id := range a.OpenSet {
		if id == nodeID {
			a.OpenSet = append(a.OpenSet[:i], a.OpenSet[i+1:]...)
			break
		}
	}
	a.ClosedSet = append(a.ClosedSet, nodeID)

	if nodeID < len(a.Nodes) {
		a.Nodes[nodeID].IsOpen = false
	}
}

// RecordConflict records a detected conflict.
func (a *AlgoState) RecordConflict(conflict *algo.Conflict) {
	a.mu.Lock()
	defer a.mu.Unlock()

	a.ConflictsFound++
	a.CurrentConflict = conflict
}

// MarkSolution marks a node as the solution.
func (a *AlgoState) MarkSolution(nodeID int) {
	a.mu.Lock()
	defer a.mu.Unlock()

	if nodeID < len(a.Nodes) {
		a.Nodes[nodeID].IsSolution = true
	}
}

// GetNodes returns a copy of the nodes slice.
func (a *AlgoState) GetNodes() []*CBSNodeInfo {
	a.mu.Lock()
	defer a.mu.Unlock()

	result := make([]*CBSNodeInfo, len(a.Nodes))
	copy(result, a.Nodes)
	return result
}

// GetCurrentNode returns the current node index.
func (a *AlgoState) GetCurrentNode() int {
	a.mu.Lock()
	defer a.mu.Unlock()
	return a.CurrentNode
}

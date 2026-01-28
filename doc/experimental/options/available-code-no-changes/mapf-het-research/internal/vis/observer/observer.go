// Package observer provides an observer pattern for algorithm visualization.
package observer

import (
	"github.com/elektrokombinacija/mapf-het-research/internal/algo"
	"github.com/elektrokombinacija/mapf-het-research/internal/core"
	"github.com/elektrokombinacija/mapf-het-research/internal/vis/state"
)

// Observer is the interface for observing algorithm execution.
type Observer interface {
	// OnNodeExpanded is called when a CBS node is expanded.
	OnNodeExpanded(node *state.CBSNodeInfo)

	// OnConflictDetected is called when a conflict is detected.
	OnConflictDetected(conflict *algo.Conflict)

	// OnConstraintAdded is called when a constraint is added to a node.
	OnConstraintAdded(node *state.CBSNodeInfo, constraint algo.Constraint)

	// OnSolutionFound is called when a solution is found.
	OnSolutionFound(solution *core.Solution)

	// ShouldPause returns true if the algorithm should pause.
	ShouldPause() bool

	// WaitForStep blocks until the observer allows the next step.
	WaitForStep()
}

// AlgoStateObserver adapts AlgoState to the Observer interface.
type AlgoStateObserver struct {
	state *state.AlgoState
}

// NewAlgoStateObserver creates a new observer backed by AlgoState.
func NewAlgoStateObserver(as *state.AlgoState) *AlgoStateObserver {
	return &AlgoStateObserver{state: as}
}

// OnNodeExpanded is called when a CBS node is expanded.
func (o *AlgoStateObserver) OnNodeExpanded(node *state.CBSNodeInfo) {
	o.state.AddNode(node)
	o.state.ExpandNode(node.ID)
}

// OnConflictDetected is called when a conflict is detected.
func (o *AlgoStateObserver) OnConflictDetected(conflict *algo.Conflict) {
	o.state.RecordConflict(conflict)
}

// OnConstraintAdded is called when a constraint is added to a node.
func (o *AlgoStateObserver) OnConstraintAdded(node *state.CBSNodeInfo, constraint algo.Constraint) {
	// Constraint is already part of node.Constraints
	// This callback is for visualization updates
}

// OnSolutionFound is called when a solution is found.
func (o *AlgoStateObserver) OnSolutionFound(solution *core.Solution) {
	currentNode := o.state.GetCurrentNode()
	if currentNode >= 0 {
		o.state.MarkSolution(currentNode)
	}
}

// ShouldPause returns true if the algorithm should pause.
func (o *AlgoStateObserver) ShouldPause() bool {
	return o.state.ShouldPause()
}

// WaitForStep blocks until the observer allows the next step.
func (o *AlgoStateObserver) WaitForStep() {
	o.state.WaitForStep()
}

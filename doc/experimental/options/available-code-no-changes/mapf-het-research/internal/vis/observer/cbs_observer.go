package observer

import (
	"container/heap"

	"github.com/elektrokombinacija/mapf-het-research/internal/algo"
	"github.com/elektrokombinacija/mapf-het-research/internal/core"
	"github.com/elektrokombinacija/mapf-het-research/internal/vis/state"
)

// ObservableCBS wraps CBS algorithm with observer callbacks.
type ObservableCBS struct {
	MaxTime  float64
	observer Observer
}

// NewObservableCBS creates a new observable CBS solver.
func NewObservableCBS(maxTime float64, observer Observer) *ObservableCBS {
	return &ObservableCBS{
		MaxTime:  maxTime,
		observer: observer,
	}
}

func (c *ObservableCBS) Name() string { return "CBS-HET (Observable)" }

// cbsNodeObs represents a node in the CBS constraint tree.
type cbsNodeObs struct {
	id          int
	parentID    int
	constraints []algo.Constraint
	solution    *core.Solution
	cost        float64
	index       int
}

type cbsHeapObs []*cbsNodeObs

func (h cbsHeapObs) Len() int           { return len(h) }
func (h cbsHeapObs) Less(i, j int) bool { return h[i].cost < h[j].cost }
func (h cbsHeapObs) Swap(i, j int) {
	h[i], h[j] = h[j], h[i]
	h[i].index = i
	h[j].index = j
}
func (h *cbsHeapObs) Push(x any) {
	n := x.(*cbsNodeObs)
	n.index = len(*h)
	*h = append(*h, n)
}
func (h *cbsHeapObs) Pop() any {
	old := *h
	n := len(old)
	x := old[n-1]
	old[n-1] = nil
	*h = old[0 : n-1]
	return x
}

// Solve implements the CBS algorithm with observer callbacks.
func (c *ObservableCBS) Solve(inst *core.Instance) *core.Solution {
	// Step 1: Compute initial assignment (greedy)
	assignment := c.computeAssignment(inst)
	if assignment == nil {
		return nil
	}

	nodeIDCounter := 0

	// Step 2: Initialize root node
	root := &cbsNodeObs{
		id:          nodeIDCounter,
		parentID:    -1,
		constraints: nil,
		solution:    core.NewSolution(),
	}
	root.solution.Assignment = assignment
	nodeIDCounter++

	// Plan initial paths
	if !c.planAllPaths(inst, root) {
		return nil
	}
	root.cost = root.solution.Makespan

	// Notify observer of root node
	c.notifyNodeExpanded(root, nil)

	// Step 3: CBS main loop
	open := &cbsHeapObs{}
	heap.Init(open)
	heap.Push(open, root)

	for open.Len() > 0 {
		// Check if should pause
		if c.observer != nil && c.observer.ShouldPause() {
			c.observer.WaitForStep()
		}

		node := heap.Pop(open).(*cbsNodeObs)

		// Notify observer
		c.notifyNodeExpanded(node, nil)

		// Find first conflict
		conflict := algo.FindFirstConflict(node.solution.Paths)
		if conflict == nil {
			// No conflicts - solution found
			node.solution.Feasible = true
			if c.observer != nil {
				c.observer.OnSolutionFound(node.solution)
			}
			return node.solution
		}

		// Notify observer of conflict
		if c.observer != nil {
			c.observer.OnConflictDetected(conflict)
		}

		// Branch: create child nodes with new constraints
		for _, robotID := range []core.RobotID{conflict.Robot1, conflict.Robot2} {
			constraint := algo.Constraint{
				Robot:    robotID,
				Vertex:   conflict.Vertex,
				Time:     conflict.Time,
				EndTime:  conflict.EndTime,
				IsEdge:   conflict.IsEdge,
				EdgeFrom: conflict.EdgeFrom,
				EdgeTo:   conflict.EdgeTo,
			}

			child := &cbsNodeObs{
				id:       nodeIDCounter,
				parentID: node.id,
				constraints: append(
					append([]algo.Constraint{}, node.constraints...),
					constraint,
				),
				solution: core.NewSolution(),
			}
			child.solution.Assignment = assignment
			nodeIDCounter++

			// Re-plan path for constrained robot
			if c.planAllPaths(inst, child) {
				child.cost = child.solution.Makespan
				heap.Push(open, child)

				// Notify observer of constraint
				if c.observer != nil {
					c.observer.OnConstraintAdded(c.nodeToInfo(child, conflict), constraint)
				}
			}
		}
	}

	return nil // No solution found
}

func (c *ObservableCBS) notifyNodeExpanded(node *cbsNodeObs, conflict *algo.Conflict) {
	if c.observer == nil {
		return
	}
	info := c.nodeToInfo(node, conflict)
	c.observer.OnNodeExpanded(info)
}

func (c *ObservableCBS) nodeToInfo(node *cbsNodeObs, conflict *algo.Conflict) *state.CBSNodeInfo {
	pathsCopy := make(map[core.RobotID]core.Path)
	for rid, path := range node.solution.Paths {
		pathCopy := make(core.Path, len(path))
		copy(pathCopy, path)
		pathsCopy[rid] = pathCopy
	}

	return &state.CBSNodeInfo{
		ID:          node.id,
		ParentID:    node.parentID,
		Constraints: node.constraints,
		Cost:        node.cost,
		IsOpen:      true,
		IsSolution:  false,
		Conflict:    conflict,
		Paths:       pathsCopy,
	}
}

// computeAssignment uses greedy assignment respecting capabilities.
func (c *ObservableCBS) computeAssignment(inst *core.Instance) core.Assignment {
	assignment := make(core.Assignment)

	for _, task := range inst.Tasks {
		assigned := false
		for _, robot := range inst.Robots {
			if core.CanPerform(robot.Type, task.Type) {
				assignment[task.ID] = robot.ID
				assigned = true
				break
			}
		}
		if !assigned {
			return nil // No capable robot
		}
	}

	return assignment
}

// planAllPaths plans paths for all robots.
func (c *ObservableCBS) planAllPaths(inst *core.Instance, node *cbsNodeObs) bool {
	node.solution.Paths = make(map[core.RobotID]core.Path)
	node.solution.Schedule = make(core.Schedule)

	for _, robot := range inst.Robots {
		// Collect goals with task info
		goalsWithInfo := algo.CollectGoalsWithInfo(node.solution.Assignment, robot.ID, inst)

		// Filter constraints for this robot
		var robotConstraints []algo.Constraint
		for _, con := range node.constraints {
			if con.Robot == robot.ID {
				robotConstraints = append(robotConstraints, con)
			}
		}

		// Plan path with task durations
		path := algo.SpaceTimeAStarWithDurations(
			inst.Workspace,
			robot,
			robot.Start,
			goalsWithInfo,
			robotConstraints,
			c.MaxTime,
		)

		if path == nil && len(goalsWithInfo) > 0 {
			return false
		}

		node.solution.Paths[robot.ID] = path
	}

	algo.PopulateSchedule(node.solution, inst)
	node.solution.ComputeMakespan(inst)
	return true
}

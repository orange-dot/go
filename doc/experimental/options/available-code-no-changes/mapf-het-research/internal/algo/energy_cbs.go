// Package algo - ENERGY-CBS: CBS with battery constraints for aerial drones.
package algo

import (
	"container/heap"
	"math"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// EnergyCBS extends CBS with battery constraints for TypeC drones.
// Ensures drones reach charging pads before battery depletes.
type EnergyCBS struct {
	MaxTime  float64
	Airspace *core.Airspace
}

// NewEnergyCBS creates an ENERGY-CBS solver.
func NewEnergyCBS(maxTime float64) *EnergyCBS {
	return &EnergyCBS{
		MaxTime:  maxTime,
		Airspace: nil,
	}
}

func (e *EnergyCBS) Name() string { return "ENERGY-CBS" }

// EnergyConstraint forces drone to reach charging pad by specified time.
type EnergyConstraint struct {
	Robot       core.RobotID
	MustReachBy float64       // Time by which must reach pad
	Pad         core.VertexID // Which charging pad
}

// energyCBSNode represents a node in the constraint tree.
type energyCBSNode struct {
	constraints       []Constraint
	energyConstraints []EnergyConstraint
	solution          *core.Solution
	cost              float64
	index             int
}

type energyCBSHeap []*energyCBSNode

func (h energyCBSHeap) Len() int           { return len(h) }
func (h energyCBSHeap) Less(i, j int) bool { return h[i].cost < h[j].cost }
func (h energyCBSHeap) Swap(i, j int) {
	h[i], h[j] = h[j], h[i]
	h[i].index = i
	h[j].index = j
}
func (h *energyCBSHeap) Push(x any) {
	n := x.(*energyCBSNode)
	n.index = len(*h)
	*h = append(*h, n)
}
func (h *energyCBSHeap) Pop() any {
	old := *h
	n := len(old)
	x := old[n-1]
	old[n-1] = nil
	*h = old[0 : n-1]
	return x
}

// Solve implements the ENERGY-CBS algorithm.
func (e *EnergyCBS) Solve(inst *core.Instance) *core.Solution {
	// Build airspace
	e.Airspace = core.BuildAirspace(inst.Workspace)

	// Compute assignment
	assignment := e.computeAssignment(inst)
	if assignment == nil {
		return nil
	}

	// Initialize root
	root := &energyCBSNode{
		constraints:       nil,
		energyConstraints: nil,
		solution:          core.NewSolution(),
	}
	root.solution.Assignment = assignment

	if !e.planAllPaths(inst, root) {
		return nil
	}
	root.cost = root.solution.Makespan

	// CBS main loop
	open := &energyCBSHeap{}
	heap.Init(open)
	heap.Push(open, root)

	iterations := 0
	maxIterations := 10000

	for open.Len() > 0 && iterations < maxIterations {
		iterations++
		node := heap.Pop(open).(*energyCBSNode)

		// Check energy feasibility for all drones
		energyViolation := e.checkEnergyFeasibility(inst, node)
		if energyViolation != nil {
			// Add charging waypoint constraint
			children := e.resolveEnergyViolation(inst, node, energyViolation)
			for _, child := range children {
				if e.planAllPaths(inst, child) {
					child.cost = child.solution.Makespan
					heap.Push(open, child)
				}
			}
			continue
		}

		// Find spatial conflicts
		conflict := FindFirstConflict(node.solution.Paths)
		if conflict == nil {
			node.solution.Feasible = true
			return node.solution
		}

		// Standard CBS branching for spatial conflicts
		children := e.resolveConflict(node, conflict)
		for _, child := range children {
			if e.planAllPaths(inst, child) {
				child.cost = child.solution.Makespan
				heap.Push(open, child)
			}
		}
	}

	return nil
}

// EnergyViolation represents a drone that will run out of battery.
type EnergyViolation struct {
	Robot      *core.Robot
	DepletedAt float64       // Time when battery depletes
	Position   core.VertexID // Position at depletion
}

// checkEnergyFeasibility verifies all drone paths are energy-feasible.
func (e *EnergyCBS) checkEnergyFeasibility(inst *core.Instance, node *energyCBSNode) *EnergyViolation {
	for _, robot := range inst.Robots {
		if robot.Type != core.TypeC {
			continue
		}

		path := node.solution.Paths[robot.ID]
		if path == nil {
			continue
		}

		violation := e.simulateEnergy(inst, robot, path)
		if violation != nil {
			return violation
		}
	}
	return nil
}

// simulateEnergy simulates battery consumption along path.
// Uses edge distances from workspace for consistency with path planning.
func (e *EnergyCBS) simulateEnergy(inst *core.Instance, robot *core.Robot, path core.Path) *EnergyViolation {
	energy := robot.CurrentBattery

	for i := 1; i < len(path); i++ {
		prevV := path[i-1].V
		currV := path[i].V

		prev := inst.Workspace.Vertices[prevV]
		curr := inst.Workspace.Vertices[currV]

		if prev == nil || curr == nil {
			continue
		}

		// Calculate distance using edge data for consistency with planner.
		// Fall back to Euclidean for wait segments or missing edges.
		var dist float64
		if prevV == currV {
			// Wait segment - calculate hover time
			hoverTime := path[i].T - path[i-1].T
			consumption := robot.EnergyForTime(hoverTime, core.ActionHover)
			energy -= consumption

			// Check if at charging pad - recharge
			if curr.IsPad && curr.Layer == core.LayerGround {
				energy = robot.BatteryCapacity
			}

			if energy <= 0 {
				return &EnergyViolation{
					Robot:      robot,
					DepletedAt: path[i].T,
					Position:   path[i].V,
				}
			}
			continue
		}

		// Calculate energy consumption separating horizontal and vertical components
		// to avoid double-counting vertical energy.
		var consumption float64

		if prev.Layer != curr.Layer {
			// Layer change: calculate horizontal and vertical energy separately
			// Horizontal distance (2D)
			horizontalDist := euclideanDist2D(prev.Pos, curr.Pos)
			if horizontalDist > 0.01 {
				consumption += robot.EnergyForDistance(horizontalDist, core.ActionMoveHorizontal)
			}
			// Vertical energy uses dedicated layer change calculation with climb speed
			consumption += robot.EnergyForLayerChange(prev.Layer, curr.Layer)
		} else {
			// Same layer: use edge distance or 2D Euclidean
			edge := inst.Workspace.GetEdge(prevV, currV)
			if edge != nil && edge.Distance() > 0 {
				dist = edge.Distance()
			} else {
				dist = euclideanDist2D(prev.Pos, curr.Pos)
			}

			if dist < 0.01 {
				// Effectively hovering at same position
				hoverTime := path[i].T - path[i-1].T
				consumption = robot.EnergyForTime(hoverTime, core.ActionHover)
			} else {
				consumption = robot.EnergyForDistance(dist, core.ActionMoveHorizontal)
			}
		}

		energy -= consumption

		// Check if at charging pad - recharge
		if curr.IsPad && curr.Layer == core.LayerGround {
			energy = robot.BatteryCapacity
		}

		// Check depletion
		if energy <= 0 {
			return &EnergyViolation{
				Robot:      robot,
				DepletedAt: path[i].T,
				Position:   path[i].V,
			}
		}
	}

	return nil
}

// resolveEnergyViolation adds charging waypoint for depleted drone.
func (e *EnergyCBS) resolveEnergyViolation(inst *core.Instance, node *energyCBSNode, violation *EnergyViolation) []*energyCBSNode {
	var children []*energyCBSNode

	// Find nearest charging pad
	nearestPad := e.findNearestPad(inst, violation.Position)
	if nearestPad == 0 {
		return children
	}

	// Add energy constraint: must reach pad before depletion
	// Give some buffer time
	reachBy := violation.DepletedAt - 5.0
	if reachBy < 0 {
		reachBy = 0
	}

	child := &energyCBSNode{
		constraints:       append([]Constraint{}, node.constraints...),
		energyConstraints: append([]EnergyConstraint{}, node.energyConstraints...),
		solution:          core.NewSolution(),
	}
	child.solution.Assignment = node.solution.Assignment
	child.energyConstraints = append(child.energyConstraints, EnergyConstraint{
		Robot:       violation.Robot.ID,
		MustReachBy: reachBy,
		Pad:         nearestPad,
	})

	children = append(children, child)

	return children
}

// findNearestPad finds closest charging pad to a position.
func (e *EnergyCBS) findNearestPad(inst *core.Instance, pos core.VertexID) core.VertexID {
	if e.Airspace != nil {
		return e.Airspace.GetNearestPad(inst.Workspace, pos)
	}

	// Fallback: search workspace
	posVertex := inst.Workspace.Vertices[pos]
	if posVertex == nil {
		return 0
	}

	nearestPad := core.VertexID(0)
	nearestDist := math.Inf(1)

	for vid, v := range inst.Workspace.Vertices {
		if v.IsPad {
			dist := euclideanDist3D(posVertex.Pos, v.Pos)
			if dist < nearestDist {
				nearestDist = dist
				nearestPad = vid
			}
		}
	}

	return nearestPad
}

// resolveConflict generates child nodes with standard CBS branching.
func (e *EnergyCBS) resolveConflict(node *energyCBSNode, c *Conflict) []*energyCBSNode {
	var children []*energyCBSNode

	for _, robotID := range []core.RobotID{c.Robot1, c.Robot2} {
		child := &energyCBSNode{
			constraints: append(
				append([]Constraint{}, node.constraints...),
				Constraint{
					Robot:    robotID,
					Vertex:   c.Vertex,
					Time:     c.Time,
					EndTime:  c.EndTime,
					IsEdge:   c.IsEdge,
					EdgeFrom: c.EdgeFrom,
					EdgeTo:   c.EdgeTo,
				},
			),
			energyConstraints: append([]EnergyConstraint{}, node.energyConstraints...),
			solution:          core.NewSolution(),
		}
		child.solution.Assignment = node.solution.Assignment
		children = append(children, child)
	}

	return children
}

// computeAssignment assigns tasks to robots.
func (e *EnergyCBS) computeAssignment(inst *core.Instance) core.Assignment {
	assignment := make(core.Assignment)
	robotWorkload := make(map[core.RobotID]int)

	for _, task := range inst.Tasks {
		var bestRobot *core.Robot
		bestScore := math.Inf(-1)

		for _, robot := range inst.Robots {
			if !core.CanPerform(robot.Type, task.Type) {
				continue
			}

			workload := robotWorkload[robot.ID]
			taskLoc := inst.Workspace.Vertices[task.Location].Pos
			robotLoc := inst.Workspace.Vertices[robot.Start].Pos
			dist := euclideanDist3D(taskLoc, robotLoc)

			// For drones, prefer those with more battery
			batteryBonus := 0.0
			if robot.Type == core.TypeC {
				batteryBonus = robot.BatteryPercentage() / 20.0
			}

			score := -float64(workload)*10 - dist + batteryBonus
			if score > bestScore {
				bestScore = score
				bestRobot = robot
			}
		}

		if bestRobot == nil {
			return nil
		}

		assignment[task.ID] = bestRobot.ID
		robotWorkload[bestRobot.ID]++
	}

	return assignment
}

// planAllPaths plans paths for all robots with energy constraints.
func (e *EnergyCBS) planAllPaths(inst *core.Instance, node *energyCBSNode) bool {
	node.solution.Paths = make(map[core.RobotID]core.Path)
	node.solution.Schedule = make(core.Schedule)

	for _, robot := range inst.Robots {
		// Collect goals with task info (includes duration) sorted by TaskID
		goalsWithInfo := CollectGoalsWithInfo(node.solution.Assignment, robot.ID, inst)

		// For drones, check energy constraints and add charging waypoints
		// Charging waypoints don't have task durations
		if robot.Type == core.TypeC {
			for _, ec := range node.energyConstraints {
				if ec.Robot == robot.ID {
					// Insert charging pad as intermediate goal (before task goals)
					// Charging has no task duration - it's handled by energy simulation
					chargingGoal := GoalWithTaskInfo{
						TaskID:   0, // No task ID for charging waypoint
						Vertex:   ec.Pad,
						Duration: 0, // Charging time handled elsewhere
					}
					goalsWithInfo = append([]GoalWithTaskInfo{chargingGoal}, goalsWithInfo...)
				}
			}
		}

		var robotConstraints []Constraint
		for _, con := range node.constraints {
			if con.Robot == robot.ID {
				robotConstraints = append(robotConstraints, con)
			}
		}

		var path core.Path
		if robot.Type == core.TypeC && e.Airspace != nil {
			// Use 3D A* with durations for drones
			path = SpaceTimeAStar3DWithDurations(
				inst.Workspace,
				e.Airspace,
				robot,
				robot.Start,
				goalsWithInfo,
				robotConstraints,
				e.MaxTime,
			)
		} else {
			// Use standard A* with durations for ground robots
			path = SpaceTimeAStarWithDurations(
				inst.Workspace,
				robot,
				robot.Start,
				goalsWithInfo,
				robotConstraints,
				e.MaxTime,
			)
		}

		if path == nil && len(goalsWithInfo) > 0 {
			return false
		}

		// Verify MustReachBy energy constraints are satisfied
		if robot.Type == core.TypeC {
			for _, ec := range node.energyConstraints {
				if ec.Robot == robot.ID {
					if !verifyMustReachBy(path, ec) {
						return false // Constraint not satisfied
					}
				}
			}
		}

		node.solution.Paths[robot.ID] = path
	}

	PopulateSchedule(node.solution, inst)
	node.solution.ComputeMakespan(inst)
	return true
}

// verifyMustReachBy checks if a path reaches the charging pad by the deadline.
func verifyMustReachBy(path core.Path, ec EnergyConstraint) bool {
	if path == nil {
		return false
	}
	for _, tv := range path {
		if tv.V == ec.Pad && tv.T <= ec.MustReachBy+TimeTolerance {
			return true
		}
	}
	return false // Pad not reached in time
}

// addWaitSegmentsForDurations adds wait segments for task service times to a path.
// Also validates that wait intervals don't violate constraints.
// Returns nil if a constraint is violated during a wait interval.
func addWaitSegmentsForDurations(path core.Path, goalsWithInfo []GoalWithTaskInfo, constraints []Constraint, robot *core.Robot) core.Path {
	if len(path) == 0 || len(goalsWithInfo) == 0 {
		return path
	}

	// Build a map from goal vertex to duration
	goalDurations := make(map[core.VertexID]float64)
	for _, g := range goalsWithInfo {
		if g.Duration > 0 {
			goalDurations[g.Vertex] = g.Duration
		}
	}

	var newPath core.Path
	for i, tv := range path {
		newPath = append(newPath, tv)

		// Check if this is a goal vertex with duration
		duration, isGoal := goalDurations[tv.V]
		if !isGoal || duration <= 0 {
			continue
		}

		// Don't add wait if next vertex is at same location (already a wait)
		if i+1 < len(path) && path[i+1].V == tv.V {
			continue
		}

		completionTime := tv.T + duration

		// Check if wait interval violates any vertex constraints
		for _, c := range constraints {
			if c.Robot != robot.ID || c.IsEdge {
				continue
			}
			if c.Vertex == tv.V {
				constraintEnd := c.EndTime
				if constraintEnd <= c.Time {
					constraintEnd = c.Time + TimeTolerance
				}
				// Wait interval [tv.T, completionTime] overlaps constraint [c.Time, constraintEnd]
				if tv.T < constraintEnd+TimeTolerance && c.Time < completionTime+TimeTolerance {
					return nil // Constraint violated during wait
				}
			}
		}

		// Add wait segment
		newPath = append(newPath, core.TimedVertex{
			V: tv.V,
			T: completionTime,
		})

		// Remove this goal from map so we don't add duplicate waits
		delete(goalDurations, tv.V)
	}

	return newPath
}

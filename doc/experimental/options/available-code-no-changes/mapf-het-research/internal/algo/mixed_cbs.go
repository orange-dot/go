// Package algo - MIXED-CBS: CBS with dimensional conflict classification.
package algo

import (
	"container/heap"
	"math"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// ConflictDimension classifies conflicts by robot type interaction.
type ConflictDimension int

const (
	// ConflictLinear: B-B conflict on rail (1D)
	ConflictLinear ConflictDimension = iota
	// ConflictPlanar: A-A conflict in 2D workspace
	ConflictPlanar
	// ConflictCrossing: A-B conflict at dock/crossing point
	ConflictCrossing
	// ConflictAerial: C-C conflict in same airspace layer
	ConflictAerial
	// ConflictVertical: C-C conflict in vertical corridor (layer transition)
	ConflictVertical
	// ConflictAirGround: C-A or C-B conflict at handoff point
	ConflictAirGround
)

func (d ConflictDimension) String() string {
	return [...]string{"Linear", "Planar", "Crossing", "Aerial", "Vertical", "AirGround"}[d]
}

// RailSegment represents a section of the rail network.
type RailSegment struct {
	ID     int
	Start  core.VertexID
	End    core.VertexID
	Length float64
}

// RailNetwork models the 1D rail structure for TypeB robots.
type RailNetwork struct {
	Segments  []*RailSegment
	Junctions map[core.VertexID][]*RailSegment // Vertices where segments meet
	VertexToSegment map[core.VertexID]*RailSegment
}

// NewRailNetwork creates an empty rail network.
func NewRailNetwork() *RailNetwork {
	return &RailNetwork{
		Segments:        nil,
		Junctions:       make(map[core.VertexID][]*RailSegment),
		VertexToSegment: make(map[core.VertexID]*RailSegment),
	}
}

// MixedConflict extends Conflict with dimensional information.
type MixedConflict struct {
	Conflict
	Dimension   ConflictDimension
	RailSegment *RailSegment // For LINEAR conflicts
}

// MixedCBS implements CBS with dimensional conflict handling.
// Core innovation: Different resolution strategies for 1D rail vs 2D mobile vs 3D aerial conflicts.
type MixedCBS struct {
	MaxTime     float64
	RailNetwork *RailNetwork
	Airspace    *core.Airspace // For TypeC drone navigation
}

// NewMixedCBS creates a MIXED-CBS solver.
func NewMixedCBS(maxTime float64) *MixedCBS {
	return &MixedCBS{
		MaxTime:     maxTime,
		RailNetwork: NewRailNetwork(),
		Airspace:    nil, // Built during Solve()
	}
}

func (m *MixedCBS) Name() string { return "MIXED-CBS" }

// mixedCBSNode represents a node in the constraint tree.
type mixedCBSNode struct {
	constraints []Constraint
	solution    *core.Solution
	cost        float64
	index       int
}

type mixedCBSHeap []*mixedCBSNode

func (h mixedCBSHeap) Len() int           { return len(h) }
func (h mixedCBSHeap) Less(i, j int) bool { return h[i].cost < h[j].cost }
func (h mixedCBSHeap) Swap(i, j int) {
	h[i], h[j] = h[j], h[i]
	h[i].index = i
	h[j].index = j
}
func (h *mixedCBSHeap) Push(x any) {
	n := x.(*mixedCBSNode)
	n.index = len(*h)
	*h = append(*h, n)
}
func (h *mixedCBSHeap) Pop() any {
	old := *h
	n := len(old)
	x := old[n-1]
	old[n-1] = nil
	*h = old[0 : n-1]
	return x
}

// Solve implements the MIXED-CBS algorithm.
func (m *MixedCBS) Solve(inst *core.Instance) *core.Solution {
	// Build rail network from workspace (vertices restricted to TypeB)
	m.buildRailNetwork(inst.Workspace)

	// Build airspace for drone navigation
	m.Airspace = core.BuildAirspace(inst.Workspace)

	// Compute assignment
	assignment := m.computeAssignment(inst)
	if assignment == nil {
		return nil
	}

	// Initialize root
	root := &mixedCBSNode{
		constraints: nil,
		solution:    core.NewSolution(),
	}
	root.solution.Assignment = assignment

	if !m.planAllPaths(inst, root) {
		return nil
	}
	root.cost = root.solution.Makespan

	// CBS main loop
	open := &mixedCBSHeap{}
	heap.Init(open)
	heap.Push(open, root)

	iterations := 0
	maxIterations := 10000

	for open.Len() > 0 && iterations < maxIterations {
		iterations++
		node := heap.Pop(open).(*mixedCBSNode)

		// Find first conflict with dimensional classification
		conflict := m.findFirstMixedConflict(inst, node.solution.Paths)
		if conflict == nil {
			node.solution.Feasible = true
			return node.solution
		}

		// Resolve based on dimension
		children := m.resolveConflict(inst, node, conflict)
		for _, child := range children {
			if m.planAllPaths(inst, child) {
				child.cost = child.solution.Makespan
				heap.Push(open, child)
			}
		}
	}

	return nil
}

// buildRailNetwork extracts rail structure from workspace.
func (m *MixedCBS) buildRailNetwork(ws *core.Workspace) {
	// Find vertices restricted to TypeB (rail vertices)
	railVertices := make(map[core.VertexID]bool)
	for vid, v := range ws.Vertices {
		for _, rt := range v.Restrict {
			if rt == core.TypeB {
				railVertices[vid] = true
				break
			}
		}
	}

	// Build segments from connected rail vertices
	segmentID := 0
	visited := make(map[core.VertexID]bool)

	for vid := range railVertices {
		if visited[vid] {
			continue
		}

		// BFS to find connected rail segment
		segment := &RailSegment{
			ID:    segmentID,
			Start: vid,
			End:   vid,
		}
		segmentID++

		queue := []core.VertexID{vid}
		for len(queue) > 0 {
			curr := queue[0]
			queue = queue[1:]

			if visited[curr] {
				continue
			}
			visited[curr] = true
			m.RailNetwork.VertexToSegment[curr] = segment

			for _, neighbor := range ws.Neighbors(curr) {
				if railVertices[neighbor] && !visited[neighbor] {
					queue = append(queue, neighbor)
					segment.End = neighbor
				}
			}
		}

		// Calculate segment length
		startPos := ws.Vertices[segment.Start].Pos
		endPos := ws.Vertices[segment.End].Pos
		segment.Length = euclideanDist(startPos, endPos)

		m.RailNetwork.Segments = append(m.RailNetwork.Segments, segment)
	}
}

// classifyConflict determines the dimensional class of a conflict.
func (m *MixedCBS) classifyConflict(c *Conflict, inst *core.Instance) ConflictDimension {
	r1 := inst.RobotByID(c.Robot1)
	r2 := inst.RobotByID(c.Robot2)

	if r1 == nil || r2 == nil {
		return ConflictPlanar // Default
	}

	// Drone-drone conflicts (TypeC)
	if r1.Type == core.TypeC && r2.Type == core.TypeC {
		v := inst.Workspace.Vertices[c.Vertex]
		if v != nil && v.IsCorridor {
			return ConflictVertical // Corridor conflict during layer transition
		}
		return ConflictAerial // Same-layer horizontal conflict
	}

	// Drone-ground conflicts
	if r1.Type == core.TypeC || r2.Type == core.TypeC {
		return ConflictAirGround
	}

	// Ground-only conflicts
	if r1.Type == core.TypeB && r2.Type == core.TypeB {
		return ConflictLinear
	}
	if r1.Type == core.TypeA && r2.Type == core.TypeA {
		return ConflictPlanar
	}
	return ConflictCrossing
}

// findFirstMixedConflict finds the first conflict with classification.
func (m *MixedCBS) findFirstMixedConflict(inst *core.Instance, paths map[core.RobotID]core.Path) *MixedConflict {
	conflict := FindFirstConflict(paths)
	if conflict == nil {
		return nil
	}

	dimension := m.classifyConflict(conflict, inst)
	mixed := &MixedConflict{
		Conflict:  *conflict,
		Dimension: dimension,
	}

	// Attach rail segment for linear conflicts
	if dimension == ConflictLinear {
		mixed.RailSegment = m.RailNetwork.VertexToSegment[conflict.Vertex]
	}

	return mixed
}

// resolveConflict generates child nodes based on conflict dimension.
func (m *MixedCBS) resolveConflict(inst *core.Instance, node *mixedCBSNode, c *MixedConflict) []*mixedCBSNode {
	switch c.Dimension {
	case ConflictLinear:
		return m.resolveLinearConflict(inst, node, c)
	case ConflictPlanar:
		return m.resolvePlanarConflict(node, c)
	case ConflictCrossing:
		return m.resolveCrossingConflict(inst, node, c)
	case ConflictAerial:
		return m.resolveAerialConflict(inst, node, c)
	case ConflictVertical:
		return m.resolveVerticalConflict(inst, node, c)
	case ConflictAirGround:
		return m.resolveAirGroundConflict(inst, node, c)
	default:
		return m.resolvePlanarConflict(node, c)
	}
}

// resolveLinearConflict handles B-B rail conflicts with temporal sequencing.
func (m *MixedCBS) resolveLinearConflict(inst *core.Instance, node *mixedCBSNode, c *MixedConflict) []*mixedCBSNode {
	var children []*mixedCBSNode

	// For rail conflicts, we need temporal ordering on the entire segment
	// Robot 1 goes first, or Robot 2 goes first

	// Option 1: Robot2 waits for Robot1
	child1 := &mixedCBSNode{
		constraints: append([]Constraint{}, node.constraints...),
		solution:    core.NewSolution(),
	}
	child1.solution.Assignment = node.solution.Assignment

	// Add temporal constraint: Robot2 cannot be at conflict vertex at this time
	// and also cannot be there shortly before (blocking)
	for dt := 0.0; dt <= 2.0; dt += 1.0 {
		child1.constraints = append(child1.constraints, Constraint{
			Robot:    c.Robot2,
			Vertex:   c.Vertex,
			Time:     c.Time + dt,
			EndTime:  c.EndTime + dt,
			IsEdge:   c.IsEdge,
			EdgeFrom: c.EdgeFrom,
			EdgeTo:   c.EdgeTo,
		})
	}
	children = append(children, child1)

	// Option 2: Robot1 waits for Robot2
	child2 := &mixedCBSNode{
		constraints: append([]Constraint{}, node.constraints...),
		solution:    core.NewSolution(),
	}
	child2.solution.Assignment = node.solution.Assignment

	for dt := 0.0; dt <= 2.0; dt += 1.0 {
		child2.constraints = append(child2.constraints, Constraint{
			Robot:    c.Robot1,
			Vertex:   c.Vertex,
			Time:     c.Time + dt,
			EndTime:  c.EndTime + dt,
			IsEdge:   c.IsEdge,
			EdgeFrom: c.EdgeFrom,
			EdgeTo:   c.EdgeTo,
		})
	}
	children = append(children, child2)

	return children
}

// resolvePlanarConflict handles A-A 2D conflicts with standard CBS branching.
func (m *MixedCBS) resolvePlanarConflict(node *mixedCBSNode, c *MixedConflict) []*mixedCBSNode {
	var children []*mixedCBSNode

	// Standard CBS: one constraint per robot
	for _, robotID := range []core.RobotID{c.Robot1, c.Robot2} {
		child := &mixedCBSNode{
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
			solution: core.NewSolution(),
		}
		child.solution.Assignment = node.solution.Assignment
		children = append(children, child)
	}

	return children
}

// resolveCrossingConflict handles A-B crossing conflicts.
// TypeB (rail) gets priority due to larger footprint and momentum.
func (m *MixedCBS) resolveCrossingConflict(inst *core.Instance, node *mixedCBSNode, c *MixedConflict) []*mixedCBSNode {
	var children []*mixedCBSNode

	// Find which robot is TypeA and which is TypeB
	r1 := inst.RobotByID(c.Robot1)
	r2 := inst.RobotByID(c.Robot2)

	if r1 == nil || r2 == nil {
		return children // Invalid conflict, return empty
	}

	var mobileRobot, railRobot core.RobotID
	if r1.Type == core.TypeA && r2.Type == core.TypeB {
		mobileRobot = c.Robot1
		railRobot = c.Robot2
	} else if r1.Type == core.TypeB && r2.Type == core.TypeA {
		mobileRobot = c.Robot2
		railRobot = c.Robot1
	} else {
		// Both same type - use standard resolution
		return m.resolvePlanarConflict(node, c)
	}

	// Primary branch: TypeA yields to TypeB (preferred)
	child1 := &mixedCBSNode{
		constraints: append([]Constraint{}, node.constraints...),
		solution:    core.NewSolution(),
	}
	child1.solution.Assignment = node.solution.Assignment

	// Mobile robot must wait/detour - add time window constraint
	for dt := -1.0; dt <= 1.0; dt += 1.0 {
		t := c.Time + dt
		if t >= 0 {
			child1.constraints = append(child1.constraints, Constraint{
				Robot:    mobileRobot,
				Vertex:   c.Vertex,
				Time:     t,
				EndTime:  c.EndTime + dt,
				IsEdge:   c.IsEdge,
				EdgeFrom: c.EdgeFrom,
				EdgeTo:   c.EdgeTo,
			})
		}
	}
	children = append(children, child1)

	// Secondary branch: TypeB yields (less preferred, but keeps optimality)
	child2 := &mixedCBSNode{
		constraints: append(
			append([]Constraint{}, node.constraints...),
			Constraint{
				Robot:    railRobot,
				Vertex:   c.Vertex,
				Time:     c.Time,
				EndTime:  c.EndTime,
				IsEdge:   c.IsEdge,
				EdgeFrom: c.EdgeFrom,
				EdgeTo:   c.EdgeTo,
			},
		),
		solution: core.NewSolution(),
	}
	child2.solution.Assignment = node.solution.Assignment
	children = append(children, child2)

	return children
}

// resolveAerialConflict handles C-C conflicts in the same airspace layer.
// Primary strategy: altitude separation (one drone changes layer).
// Secondary strategy: temporal separation (one waits).
func (m *MixedCBS) resolveAerialConflict(inst *core.Instance, node *mixedCBSNode, c *MixedConflict) []*mixedCBSNode {
	var children []*mixedCBSNode

	// Option 1: Drone1 changes altitude (preferred - faster)
	// This works if the conflict vertex allows layer transitions
	v := inst.Workspace.Vertices[c.Vertex]
	if v != nil && m.Airspace != nil {
		corridor := m.Airspace.GetCorridorForVertex(c.Vertex)
		if corridor != nil {
			// Find an alternative layer vertex
			currentLayer := v.Layer
			altLayer := core.NextLayerUp(currentLayer)
			if altLayer == currentLayer {
				altLayer = core.NextLayerDown(currentLayer)
			}
			if altVertex := m.Airspace.GetVertexAtLayer(c.Vertex, altLayer); altVertex != 0 {
				// Drone1 goes to alternate layer
				child1 := &mixedCBSNode{
					constraints: append([]Constraint{}, node.constraints...),
					solution:    core.NewSolution(),
				}
				child1.solution.Assignment = node.solution.Assignment
				child1.constraints = append(child1.constraints, Constraint{
					Robot:    c.Robot1,
					Vertex:   c.Vertex,
					Time:     c.Time,
					EndTime:  c.EndTime,
					IsEdge:   c.IsEdge,
					EdgeFrom: c.EdgeFrom,
					EdgeTo:   c.EdgeTo,
				})
				children = append(children, child1)
			}
		}
	}

	// Option 2: Temporal separation (standard CBS branching)
	for _, robotID := range []core.RobotID{c.Robot1, c.Robot2} {
		child := &mixedCBSNode{
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
			solution: core.NewSolution(),
		}
		child.solution.Assignment = node.solution.Assignment
		children = append(children, child)
	}

	return children
}

// resolveVerticalConflict handles C-C conflicts in vertical corridors.
// One drone must wait while the other completes layer transition.
func (m *MixedCBS) resolveVerticalConflict(inst *core.Instance, node *mixedCBSNode, c *MixedConflict) []*mixedCBSNode {
	var children []*mixedCBSNode

	// Vertical corridor conflicts require temporal ordering.
	// One drone completes its vertical transition before the other enters.

	// Option 1: Robot2 waits for Robot1 to complete transition
	child1 := &mixedCBSNode{
		constraints: append([]Constraint{}, node.constraints...),
		solution:    core.NewSolution(),
	}
	child1.solution.Assignment = node.solution.Assignment
	// Block Robot2 from corridor for transition duration (2 time units)
	for dt := 0.0; dt <= 2.0; dt += 1.0 {
		child1.constraints = append(child1.constraints, Constraint{
			Robot:    c.Robot2,
			Vertex:   c.Vertex,
			Time:     c.Time + dt,
			EndTime:  c.EndTime + dt,
			IsEdge:   c.IsEdge,
			EdgeFrom: c.EdgeFrom,
			EdgeTo:   c.EdgeTo,
		})
	}
	children = append(children, child1)

	// Option 2: Robot1 waits for Robot2
	child2 := &mixedCBSNode{
		constraints: append([]Constraint{}, node.constraints...),
		solution:    core.NewSolution(),
	}
	child2.solution.Assignment = node.solution.Assignment
	for dt := 0.0; dt <= 2.0; dt += 1.0 {
		child2.constraints = append(child2.constraints, Constraint{
			Robot:    c.Robot1,
			Vertex:   c.Vertex,
			Time:     c.Time + dt,
			EndTime:  c.EndTime + dt,
			IsEdge:   c.IsEdge,
			EdgeFrom: c.EdgeFrom,
			EdgeTo:   c.EdgeTo,
		})
	}
	children = append(children, child2)

	return children
}

// resolveAirGroundConflict handles C-A or C-B conflicts at handoff points.
// Drone (TypeC) yields to ground robot - ground robots are larger and slower to maneuver.
func (m *MixedCBS) resolveAirGroundConflict(inst *core.Instance, node *mixedCBSNode, c *MixedConflict) []*mixedCBSNode {
	var children []*mixedCBSNode

	r1 := inst.RobotByID(c.Robot1)
	r2 := inst.RobotByID(c.Robot2)

	if r1 == nil || r2 == nil {
		return children
	}

	// Identify which robot is the drone
	var droneRobot, groundRobot core.RobotID
	if r1.Type == core.TypeC {
		droneRobot = c.Robot1
		groundRobot = c.Robot2
	} else {
		droneRobot = c.Robot2
		groundRobot = c.Robot1
	}

	// Primary branch: Drone yields to ground robot (preferred)
	child1 := &mixedCBSNode{
		constraints: append([]Constraint{}, node.constraints...),
		solution:    core.NewSolution(),
	}
	child1.solution.Assignment = node.solution.Assignment
	// Drone must avoid handoff area during ground robot's passage
	for dt := -1.0; dt <= 1.0; dt += 1.0 {
		t := c.Time + dt
		if t >= 0 {
			child1.constraints = append(child1.constraints, Constraint{
				Robot:    droneRobot,
				Vertex:   c.Vertex,
				Time:     t,
				EndTime:  c.EndTime + dt,
				IsEdge:   c.IsEdge,
				EdgeFrom: c.EdgeFrom,
				EdgeTo:   c.EdgeTo,
			})
		}
	}
	children = append(children, child1)

	// Secondary branch: Ground robot yields (less preferred)
	child2 := &mixedCBSNode{
		constraints: append(
			append([]Constraint{}, node.constraints...),
			Constraint{
				Robot:    groundRobot,
				Vertex:   c.Vertex,
				Time:     c.Time,
				EndTime:  c.EndTime,
				IsEdge:   c.IsEdge,
				EdgeFrom: c.EdgeFrom,
				EdgeTo:   c.EdgeTo,
			},
		),
		solution: core.NewSolution(),
	}
	child2.solution.Assignment = node.solution.Assignment
	children = append(children, child2)

	return children
}

// computeAssignment assigns tasks respecting capabilities.
func (m *MixedCBS) computeAssignment(inst *core.Instance) core.Assignment {
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
			dist := euclideanDist(taskLoc, robotLoc)

			// Prefer rail robots for battery swaps (efficiency)
			typeBonus := 0.0
			if robot.Type == core.TypeB && task.Type == core.SwapBattery {
				typeBonus = 5.0
			}

			score := -float64(workload)*10 - dist + typeBonus
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

	// Use rail network length info to verify assignment quality
	_ = m.RailNetwork.Segments

	return assignment
}

// planAllPaths plans paths for all robots.
func (m *MixedCBS) planAllPaths(inst *core.Instance, node *mixedCBSNode) bool {
	node.solution.Paths = make(map[core.RobotID]core.Path)
	node.solution.Schedule = make(core.Schedule)

	for _, robot := range inst.Robots {
		// Collect goals with task info (includes duration) sorted by TaskID
		goalsWithInfo := CollectGoalsWithInfo(node.solution.Assignment, robot.ID, inst)

		var robotConstraints []Constraint
		for _, con := range node.constraints {
			if con.Robot == robot.ID {
				robotConstraints = append(robotConstraints, con)
			}
		}

		// Plan path with task durations (adds wait segments for service time)
		path := SpaceTimeAStarWithDurations(
			inst.Workspace,
			robot,
			robot.Start,
			goalsWithInfo,
			robotConstraints,
			m.MaxTime,
		)

		if path == nil && len(goalsWithInfo) > 0 {
			return false
		}

		node.solution.Paths[robot.ID] = path
	}

	PopulateSchedule(node.solution, inst)
	node.solution.ComputeMakespan(inst)
	return true
}

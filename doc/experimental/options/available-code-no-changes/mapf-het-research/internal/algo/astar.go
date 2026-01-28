package algo

import (
	"container/heap"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// SpaceTimeState represents (vertex, time) for space-time A*.
type SpaceTimeState struct {
	V core.VertexID
	T float64
}

// astarNode for priority queue.
type astarNode struct {
	state  SpaceTimeState
	g      float64 // Cost so far
	f      float64 // g + h
	parent *astarNode
	index  int // heap index
}

// astarHeap implements heap.Interface.
type astarHeap []*astarNode

func (h astarHeap) Len() int           { return len(h) }
func (h astarHeap) Less(i, j int) bool { return h[i].f < h[j].f }
func (h astarHeap) Swap(i, j int) {
	h[i], h[j] = h[j], h[i]
	h[i].index = i
	h[j].index = j
}
func (h *astarHeap) Push(x any) {
	n := x.(*astarNode)
	n.index = len(*h)
	*h = append(*h, n)
}
func (h *astarHeap) Pop() any {
	old := *h
	n := len(old)
	x := old[n-1]
	old[n-1] = nil
	*h = old[0 : n-1]
	return x
}

// SpaceTimeAStar finds shortest path through all goals avoiding constraints.
// Uses sequential chaining: finds path to goals[0], then from goals[0] to goals[1], etc.
func SpaceTimeAStar(
	ws *core.Workspace,
	robot *core.Robot,
	start core.VertexID,
	goals []core.VertexID,
	constraints []Constraint,
	maxTime float64,
) core.Path {
	if len(goals) == 0 {
		return nil
	}

	var fullPath core.Path
	currentStart := start
	currentTime := 0.0

	for i, goal := range goals {
		segment := spaceTimeAStarSingle(ws, robot, currentStart, goal, constraints, maxTime, currentTime)
		if segment == nil {
			return nil // Failed to reach this goal
		}

		if i == 0 {
			fullPath = append(fullPath, segment...)
		} else {
			// Skip duplicate vertex at junction (end of previous = start of next)
			fullPath = append(fullPath, segment[1:]...)
		}

		currentStart = goal
		currentTime = segment[len(segment)-1].T
	}

	return fullPath
}

// SpaceTimeAStarWithDurations finds path through goals, adding wait segments for task durations.
// This version uses task durations to add service time at each goal.
// Wait intervals are checked against vertex constraints to ensure feasibility.
func SpaceTimeAStarWithDurations(
	ws *core.Workspace,
	robot *core.Robot,
	start core.VertexID,
	goalsWithInfo []GoalWithTaskInfo,
	constraints []Constraint,
	maxTime float64,
) core.Path {
	if len(goalsWithInfo) == 0 {
		return nil
	}

	var fullPath core.Path
	currentStart := start
	currentTime := 0.0

	for i, goalInfo := range goalsWithInfo {
		segment := spaceTimeAStarSingle(ws, robot, currentStart, goalInfo.Vertex, constraints, maxTime, currentTime)
		if segment == nil {
			return nil // Failed to reach this goal
		}

		if i == 0 {
			fullPath = append(fullPath, segment...)
		} else {
			// Skip duplicate vertex at junction
			fullPath = append(fullPath, segment[1:]...)
		}

		arrivalTime := segment[len(segment)-1].T

		// Add wait segment for task duration (service time)
		if goalInfo.Duration > 0 {
			completionTime := arrivalTime + goalInfo.Duration

			// Check if wait interval violates any vertex constraints
			waitViolated := false
			for _, c := range constraints {
				if c.Robot != robot.ID || c.IsEdge {
					continue
				}
				// Vertex constraint: check if constraint time falls within wait interval [arrival, completion]
				if c.Vertex == goalInfo.Vertex {
					// Use EndTime for interval constraints, otherwise treat as point constraint
					constraintEnd := c.EndTime
					if constraintEnd <= c.Time {
						constraintEnd = c.Time + TimeTolerance
					}
					// Wait interval [arrivalTime, completionTime] overlaps constraint [c.Time, constraintEnd]
					if arrivalTime < constraintEnd+TimeTolerance && c.Time < completionTime+TimeTolerance {
						waitViolated = true
						break
					}
				}
			}

			if waitViolated {
				return nil // Cannot complete task due to constraint during service time
			}

			// Only add wait vertex if duration > 0
			fullPath = append(fullPath, core.TimedVertex{
				V: goalInfo.Vertex,
				T: completionTime,
			})
			currentTime = completionTime
		} else {
			currentTime = arrivalTime
		}

		currentStart = goalInfo.Vertex
	}

	return fullPath
}

// spaceTimeAStarSingle finds shortest path to a single goal.
func spaceTimeAStarSingle(
	ws *core.Workspace,
	robot *core.Robot,
	start core.VertexID,
	goal core.VertexID,
	constraints []Constraint,
	maxTime float64,
	startTime float64,
) core.Path {
	// Heuristic: Manhattan distance (placeholder)
	heuristic := func(v core.VertexID) float64 {
		// TODO: Proper heuristic using workspace positions
		if v == goal {
			return 0
		}
		return 1.0
	}

	// Check if state violates constraints (vertex and edge)
	// For edge constraints, check interval overlap: movement [tStart, tEnd] vs constraint [c.Time, c.EndTime]
	violates := func(fromV, toV core.VertexID, tStart, tEnd float64) bool {
		for _, c := range constraints {
			if c.Robot != robot.ID {
				continue
			}
			// Vertex constraint: robot cannot be at vertex at specific time or interval.
			if !c.IsEdge && c.Vertex == toV {
				constraintEnd := c.EndTime
				if constraintEnd <= c.Time {
					constraintEnd = c.Time
				}
				if fromV == toV {
					// Waiting: check interval overlap
					if tStart < constraintEnd+TimeTolerance && c.Time < tEnd+TimeTolerance {
						return true
					}
				} else if timeEqual(c.Time, tEnd) {
					// Moving: only occupy target at arrival
					return true
				}
			}
			// Edge constraint (swap conflict): cannot traverse edge during time interval
			// Movement interval [tStart, tEnd] overlaps with constraint interval [c.Time, c.EndTime]
			if c.IsEdge && c.EdgeFrom == fromV && c.EdgeTo == toV {
				// Use EndTime if set, otherwise fall back to Time
				constraintEnd := c.EndTime
				if constraintEnd <= c.Time {
					constraintEnd = c.Time + TimeTolerance
				}
				// Intervals [tStart, tEnd] and [c.Time, constraintEnd] overlap if:
				// tStart < constraintEnd AND c.Time < tEnd
				if tStart < constraintEnd+TimeTolerance && c.Time < tEnd+TimeTolerance {
					return true
				}
			}
		}
		return false
	}

	open := &astarHeap{}
	heap.Init(open)

	startNode := &astarNode{
		state: SpaceTimeState{V: start, T: startTime},
		g:     0,
		f:     heuristic(start),
	}
	heap.Push(open, startNode)

	visited := make(map[SpaceTimeState]bool)

	for open.Len() > 0 {
		current := heap.Pop(open).(*astarNode)

		if current.state.V == goal {
			// Reconstruct path
			return reconstructPath(current)
		}

		if visited[current.state] {
			continue
		}
		visited[current.state] = true

		if current.state.T >= maxTime {
			continue
		}

		// Expand neighbors
		// Wait action - use time-based cost for consistency with move actions
		waitDuration := 1.0 // Default wait duration in seconds
		waitT := current.state.T + waitDuration
		if !violates(current.state.V, current.state.V, current.state.T, waitT) {
			waitState := SpaceTimeState{V: current.state.V, T: waitT}
			if !visited[waitState] {
				// Use wait duration as cost to keep g-cost time-based
				node := &astarNode{
					state:  waitState,
					g:      current.g + waitDuration,
					f:      current.g + waitDuration + heuristic(current.state.V),
					parent: current,
				}
				heap.Push(open, node)
			}
		}

		// Move actions
		for _, neighbor := range ws.Neighbors(current.state.V) {
			if !ws.CanOccupy(neighbor, robot.Type) {
				continue
			}

			// Get edge and calculate travel time
			edge := ws.GetEdge(current.state.V, neighbor)
			if edge == nil {
				continue
			}
			travelTime := edge.TravelTime(robot)
			nextT := current.state.T + travelTime
			// Use travel time for g-cost to optimize makespan, not distance.
			// This ensures fixed-time edges (elevators, rails) are costed correctly.
			edgeCost := travelTime

			if violates(current.state.V, neighbor, current.state.T, nextT) {
				continue
			}

			moveState := SpaceTimeState{V: neighbor, T: nextT}
			if visited[moveState] {
				continue
			}

			node := &astarNode{
				state:  moveState,
				g:      current.g + edgeCost,
				f:      current.g + edgeCost + heuristic(neighbor),
				parent: current,
			}
			heap.Push(open, node)
		}
	}

	return nil // No path found
}

func reconstructPath(node *astarNode) core.Path {
	var path core.Path
	for n := node; n != nil; n = n.parent {
		path = append([]core.TimedVertex{{V: n.state.V, T: n.state.T}}, path...)
	}
	return path
}

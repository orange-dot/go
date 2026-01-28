// Package algo - 3D Space-Time A* for aerial drone pathfinding.
package algo

import (
	"container/heap"
	"math"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// SpaceTimeState3D represents (vertex, time, layer) for 3D space-time A*.
type SpaceTimeState3D struct {
	V     core.VertexID
	T     float64
	Layer core.AirspaceLayer
}

// astar3DNode for priority queue.
type astar3DNode struct {
	state       SpaceTimeState3D
	g           float64 // Cost so far
	f           float64 // g + h
	energy      float64 // Remaining battery (for drones)
	parent      *astar3DNode
	index       int // heap index
	layerChange bool // Did we change layer to get here?
}

// astar3DHeap implements heap.Interface.
type astar3DHeap []*astar3DNode

func (h astar3DHeap) Len() int           { return len(h) }
func (h astar3DHeap) Less(i, j int) bool { return h[i].f < h[j].f }
func (h astar3DHeap) Swap(i, j int) {
	h[i], h[j] = h[j], h[i]
	h[i].index = i
	h[j].index = j
}
func (h *astar3DHeap) Push(x any) {
	n := x.(*astar3DNode)
	n.index = len(*h)
	*h = append(*h, n)
}
func (h *astar3DHeap) Pop() any {
	old := *h
	n := len(old)
	x := old[n-1]
	old[n-1] = nil
	*h = old[0 : n-1]
	return x
}

// SpaceTimeAStar3D finds shortest path for drones through all goals in 3D layered airspace.
// Considers energy constraints and vertical corridor transitions.
// Uses sequential chaining: finds path to goals[0], then from goals[0] to goals[1], etc.
func SpaceTimeAStar3D(
	ws *core.Workspace,
	airspace *core.Airspace,
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
	currentEnergy := robot.CurrentBattery
	currentLayer := core.LayerGround // Start at ground

	startVertex := ws.Vertices[start]
	if startVertex != nil {
		currentLayer = startVertex.Layer
	}

	for i, goal := range goals {
		segment, endEnergy, endLayer := spaceTimeAStar3DSingle(
			ws, airspace, robot, currentStart, goal,
			constraints, maxTime, currentTime, currentEnergy, currentLayer,
		)
		if segment == nil {
			return nil // Failed to reach this goal
		}

		if i == 0 {
			fullPath = append(fullPath, segment...)
		} else {
			// Skip duplicate vertex at junction
			fullPath = append(fullPath, segment[1:]...)
		}

		currentStart = goal
		currentTime = segment[len(segment)-1].T
		currentEnergy = endEnergy
		currentLayer = endLayer
	}

	return fullPath
}

// SpaceTimeAStar3DWithDurations finds path through goals, adding wait segments for task durations.
// Uses sequential chaining and accounts for hover energy during service time.
func SpaceTimeAStar3DWithDurations(
	ws *core.Workspace,
	airspace *core.Airspace,
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
	currentEnergy := robot.CurrentBattery
	currentLayer := core.LayerGround

	startVertex := ws.Vertices[start]
	if startVertex != nil {
		currentLayer = startVertex.Layer
	}

	for i, goalInfo := range goalsWithInfo {
		segment, endEnergy, endLayer := spaceTimeAStar3DSingle(
			ws, airspace, robot, currentStart, goalInfo.Vertex,
			constraints, maxTime, currentTime, currentEnergy, currentLayer,
		)
		if segment == nil {
			return nil
		}

		if i == 0 {
			fullPath = append(fullPath, segment...)
		} else {
			fullPath = append(fullPath, segment[1:]...)
		}

		arrivalTime := segment[len(segment)-1].T

		if goalInfo.Duration > 0 {
			completionTime := arrivalTime + goalInfo.Duration

			// Check if wait interval violates any vertex constraints.
			waitViolated := false
			for _, c := range constraints {
				if c.Robot != robot.ID || c.IsEdge {
					continue
				}
				if c.Vertex == goalInfo.Vertex {
					constraintEnd := c.EndTime
					if constraintEnd <= c.Time {
						constraintEnd = c.Time + TimeTolerance
					}
					if arrivalTime < constraintEnd+TimeTolerance && c.Time < completionTime+TimeTolerance {
						waitViolated = true
						break
					}
				}
			}
			if waitViolated {
				return nil
			}

			// Account for hover energy during service time.
			if robot.IsDrone() {
				waitEnergy := robot.EnergyForTime(goalInfo.Duration, core.ActionHover)
				endEnergy -= waitEnergy
				if endEnergy <= 0 {
					return nil
				}
			}

			fullPath = append(fullPath, core.TimedVertex{
				V: goalInfo.Vertex,
				T: completionTime,
			})
			currentTime = completionTime
		} else {
			currentTime = arrivalTime
		}

		currentStart = goalInfo.Vertex
		currentEnergy = endEnergy
		currentLayer = endLayer
	}

	return fullPath
}

// spaceTimeAStar3DSingle finds shortest path to a single goal in 3D.
// Returns path, remaining energy, and ending layer.
func spaceTimeAStar3DSingle(
	ws *core.Workspace,
	airspace *core.Airspace,
	robot *core.Robot,
	start core.VertexID,
	goal core.VertexID,
	constraints []Constraint,
	maxTime float64,
	startTime float64,
	startEnergy float64,
	startLayer core.AirspaceLayer,
) (core.Path, float64, core.AirspaceLayer) {
	goalVertex := ws.Vertices[goal]
	if goalVertex == nil {
		return nil, 0, startLayer
	}

	startVertex := ws.Vertices[start]
	if startVertex == nil {
		return nil, 0, startLayer
	}

	// 3D Euclidean heuristic with energy penalty
	heuristic := func(state SpaceTimeState3D) float64 {
		pos := ws.Vertices[state.V].Pos
		goalPos := goalVertex.Pos

		dist3D := math.Sqrt(
			(pos.X-goalPos.X)*(pos.X-goalPos.X) +
				(pos.Y-goalPos.Y)*(pos.Y-goalPos.Y) +
				(pos.Z-goalPos.Z)*(pos.Z-goalPos.Z),
		)

		// Time estimate based on speed
		timeEst := dist3D / robot.Speed()

		return timeEst
	}

	// Energy-aware heuristic for low battery situations
	energyHeuristic := func(state SpaceTimeState3D, remainingEnergy float64) float64 {
		baseH := heuristic(state)

		// Add penalty if low battery and far from home
		if robot.IsDrone() && remainingEnergy < robot.BatteryCapacity*0.3 {
			homeVertex := ws.Vertices[robot.HomeBase]
			if homeVertex != nil {
				pos := ws.Vertices[state.V].Pos
				homePos := homeVertex.Pos
				homeDist := math.Sqrt(
					(pos.X-homePos.X)*(pos.X-homePos.X) +
						(pos.Y-homePos.Y)*(pos.Y-homePos.Y) +
						(pos.Z-homePos.Z)*(pos.Z-homePos.Z),
				)
				// Penalize being far from home when low on battery
				baseH += homeDist * 0.5
			}
		}

		return baseH
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
			// Edge constraint (swap conflict): check interval overlap
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

	// Initialize
	open := &astar3DHeap{}
	heap.Init(open)

	startState := SpaceTimeState3D{
		V:     start,
		T:     startTime,
		Layer: startLayer,
	}
	startNode := &astar3DNode{
		state:  startState,
		g:      0,
		f:      heuristic(startState),
		energy: startEnergy,
	}
	heap.Push(open, startNode)

	visited := make(map[SpaceTimeState3D]bool)

	for open.Len() > 0 {
		current := heap.Pop(open).(*astar3DNode)

		// Goal check
		if current.state.V == goal {
			return reconstructPath3D(current), current.energy, current.state.Layer
		}

		if visited[current.state] {
			continue
		}
		visited[current.state] = true

		if current.state.T >= maxTime {
			continue
		}

		// Energy check - if depleted, path fails
		if robot.IsDrone() && current.energy <= 0 {
			continue
		}

		// === Action 1: Hover (wait in place) ===
		hoverDuration := 1.0 // 1 second hover
		hoverT := current.state.T + hoverDuration
		if !violates(current.state.V, current.state.V, current.state.T, hoverT) {
			hoverEnergy := robot.EnergyForTime(hoverDuration, core.ActionHover)
			newEnergy := current.energy - hoverEnergy

			if !robot.IsDrone() || newEnergy > 0 {
				hoverState := SpaceTimeState3D{
					V:     current.state.V,
					T:     hoverT,
					Layer: current.state.Layer,
				}
				if !visited[hoverState] {
					node := &astar3DNode{
						state:  hoverState,
						g:      current.g + hoverDuration,
						f:      current.g + hoverDuration + energyHeuristic(hoverState, newEnergy),
						energy: newEnergy,
						parent: current,
					}
					heap.Push(open, node)
				}
			}
		}

		// === Action 2: Horizontal movement (same layer) ===
		for _, neighbor := range ws.Neighbors(current.state.V) {
			neighborVertex := ws.Vertices[neighbor]
			if neighborVertex == nil {
				continue
			}

			// Only same-layer horizontal moves here
			if neighborVertex.Layer != current.state.Layer {
				continue
			}

			// Check occupancy restrictions
			if !ws.CanOccupy(neighbor, robot.Type) {
				continue
			}

			// Check no-fly zones for drones
			if robot.IsDrone() && neighborVertex.NoFlyZone {
				continue
			}

			// Get edge and calculate travel time
			edge := ws.GetEdge(current.state.V, neighbor)
			if edge == nil {
				continue
			}
			travelTime := edge.TravelTime(robot)
			moveT := current.state.T + travelTime
			edgeDist := edge.Distance() // For energy calculation

			if violates(current.state.V, neighbor, current.state.T, moveT) {
				continue
			}

			// Energy for horizontal movement
			moveEnergy := robot.EnergyForDistance(edgeDist, core.ActionMoveHorizontal)
			newEnergy := current.energy - moveEnergy

			if robot.IsDrone() && newEnergy <= 0 {
				continue
			}

			moveState := SpaceTimeState3D{
				V:     neighbor,
				T:     moveT,
				Layer: current.state.Layer,
			}
			if visited[moveState] {
				continue
			}

			node := &astar3DNode{
				state:  moveState,
				g:      current.g + travelTime,
				f:      current.g + travelTime + energyHeuristic(moveState, newEnergy),
				energy: newEnergy,
				parent: current,
			}
			heap.Push(open, node)
		}

		// === Action 3: Vertical movement (layer changes) ===
		// Only for drones and only in corridors
		if robot.IsDrone() {
			currentVertex := ws.Vertices[current.state.V]
			if currentVertex != nil && currentVertex.IsCorridor && airspace != nil {
				climbSpeed := 2.0 // m/s vertical

				// Try climbing
				upperLayer := core.NextLayerUp(current.state.Layer)
				if upperLayer != current.state.Layer {
					upperVertex := airspace.GetVertexAtLayer(current.state.V, upperLayer)
					heightDiff := upperLayer.Height() - current.state.Layer.Height()
					climbTime := heightDiff / climbSpeed
					climbT := current.state.T + climbTime

					if upperVertex != 0 && !violates(current.state.V, upperVertex, current.state.T, climbT) {
						climbEnergy := robot.EnergyForLayerChange(current.state.Layer, upperLayer)
						newEnergy := current.energy - climbEnergy

						if newEnergy > 0 {
							climbState := SpaceTimeState3D{
								V:     upperVertex,
								T:     climbT,
								Layer: upperLayer,
							}
							if !visited[climbState] {
								node := &astar3DNode{
									state:       climbState,
									g:           current.g + climbTime,
									f:           current.g + climbTime + energyHeuristic(climbState, newEnergy),
									energy:      newEnergy,
									parent:      current,
									layerChange: true,
								}
								heap.Push(open, node)
							}
						}
					}
				}

				// Try descending
				lowerLayer := core.NextLayerDown(current.state.Layer)
				if lowerLayer != current.state.Layer {
					lowerVertex := airspace.GetVertexAtLayer(current.state.V, lowerLayer)
					heightDiff := current.state.Layer.Height() - lowerLayer.Height()
					descendTime := heightDiff / climbSpeed
					descendT := current.state.T + descendTime

					if lowerVertex != 0 && !violates(current.state.V, lowerVertex, current.state.T, descendT) {
						descendEnergy := robot.EnergyForLayerChange(current.state.Layer, lowerLayer)
						newEnergy := current.energy - descendEnergy

						if newEnergy > 0 {
							descendState := SpaceTimeState3D{
								V:     lowerVertex,
								T:     descendT,
								Layer: lowerLayer,
							}
							if !visited[descendState] {
								node := &astar3DNode{
									state:       descendState,
									g:           current.g + descendTime,
									f:           current.g + descendTime + energyHeuristic(descendState, newEnergy),
									energy:      newEnergy,
									parent:      current,
									layerChange: true,
								}
								heap.Push(open, node)
							}
						}
					}
				}
			}
		}

		// === Action 4: Recharge at pad ===
		if robot.IsDrone() {
			currentVertex := ws.Vertices[current.state.V]
			if currentVertex != nil && currentVertex.IsPad && current.state.Layer == core.LayerGround {
				// Recharge to full - takes 10 time units
				rechargeDuration := 10.0
				rechargeT := current.state.T + rechargeDuration
				rechargeState := SpaceTimeState3D{
					V:     current.state.V,
					T:     rechargeT,
					Layer: core.LayerGround,
				}
				if !visited[rechargeState] && !violates(current.state.V, current.state.V, current.state.T, rechargeT) {
					node := &astar3DNode{
						state:  rechargeState,
						g:      current.g + rechargeDuration,
						f:      current.g + rechargeDuration + energyHeuristic(rechargeState, robot.BatteryCapacity),
						energy: robot.BatteryCapacity,
						parent: current,
					}
					heap.Push(open, node)
				}
			}
		}
	}

	return nil, 0, startLayer // No path found
}

// reconstructPath3D builds path from A* result.
func reconstructPath3D(node *astar3DNode) core.Path {
	var path core.Path
	for n := node; n != nil; n = n.parent {
		path = append([]core.TimedVertex{{V: n.state.V, T: n.state.T}}, path...)
	}
	return path
}

// euclideanDist3D computes 3D Euclidean distance.
func euclideanDist3D(p1, p2 core.Pos) float64 {
	dx := p1.X - p2.X
	dy := p1.Y - p2.Y
	dz := p1.Z - p2.Z
	return math.Sqrt(dx*dx + dy*dy + dz*dz)
}

// euclideanDist2D computes 2D Euclidean distance (X, Y only, ignoring Z).
func euclideanDist2D(p1, p2 core.Pos) float64 {
	dx := p1.X - p2.X
	dy := p1.Y - p2.Y
	return math.Sqrt(dx*dx + dy*dy)
}

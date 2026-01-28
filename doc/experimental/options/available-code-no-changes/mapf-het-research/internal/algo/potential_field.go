// Package algo - potential field computations for field-guided algorithms.
package algo

import (
	"math"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// PotentialField represents gradients for navigation guidance.
type PotentialField struct {
	// LoadGradient: higher values attract robots (toward tasks/goals)
	LoadGradient map[core.VertexID]float64
	// ThermalGradient: lower values are safer (avoid hot spots)
	ThermalGradient map[core.VertexID]float64
	// PowerTrajectory: optimal power flow paths
	PowerTrajectory map[core.VertexID]float64
	// RepulsiveField: robot avoidance gradients
	RepulsiveField map[core.VertexID]float64
	// SlackGradient: deadline slack from EK-KOR2 modules (MAPF-HET Section V.F)
	// Higher values = more slack = lower priority
	// Negative gradient indicates neighbor needs help
	SlackGradient map[core.VertexID]float64
}

// NewPotentialField creates an empty potential field.
func NewPotentialField() *PotentialField {
	return &PotentialField{
		LoadGradient:    make(map[core.VertexID]float64),
		ThermalGradient: make(map[core.VertexID]float64),
		PowerTrajectory: make(map[core.VertexID]float64),
		RepulsiveField:  make(map[core.VertexID]float64),
		SlackGradient:   make(map[core.VertexID]float64),
	}
}

// ComputePotentialField computes field gradients based on workspace and robots.
func ComputePotentialField(ws *core.Workspace, robots []*core.Robot, tasks []*core.Task) *PotentialField {
	field := NewPotentialField()

	// Initialize all vertices
	for vid := range ws.Vertices {
		field.LoadGradient[vid] = 0
		field.ThermalGradient[vid] = 0
		field.PowerTrajectory[vid] = 0
		field.RepulsiveField[vid] = 0
	}

	// Compute attractive field toward task locations
	for _, task := range tasks {
		taskPos := ws.Vertices[task.Location].Pos

		for vid, vertex := range ws.Vertices {
			// Distance-based attraction (inverse square law)
			dist := euclideanDist(vertex.Pos, taskPos)
			if dist < 0.1 {
				dist = 0.1 // Avoid division by zero
			}

			// Attraction strength based on task type
			attractStrength := taskAttractionStrength(task.Type)
			field.LoadGradient[vid] += attractStrength / (dist * dist)
		}
	}

	// Compute repulsive field from robot positions
	for _, robot := range robots {
		robotPos := ws.Vertices[robot.Start].Pos
		repulsionRadius := robotRepulsionRadius(robot.Type)

		for vid, vertex := range ws.Vertices {
			dist := euclideanDist(vertex.Pos, robotPos)
			if dist < repulsionRadius && dist > 0.01 {
				// Repulsive potential increases as we get closer
				field.RepulsiveField[vid] += repulsionRadius / dist
			}
		}
	}

	// Normalize fields to [0, 1] range
	normalizeField(field.LoadGradient)
	normalizeField(field.ThermalGradient)
	normalizeField(field.RepulsiveField)

	return field
}

// UpdateFieldWithRobotPositions updates repulsive field based on current positions.
func UpdateFieldWithRobotPositions(field *PotentialField, ws *core.Workspace, positions map[core.RobotID]core.VertexID) {
	// Reset repulsive field
	for vid := range field.RepulsiveField {
		field.RepulsiveField[vid] = 0
	}

	// Add repulsion from current positions
	for _, pos := range positions {
		robotPos := ws.Vertices[pos].Pos
		repulsionRadius := 3.0 // Standard radius

		for vid, vertex := range ws.Vertices {
			dist := euclideanDist(vertex.Pos, robotPos)
			if dist < repulsionRadius && dist > 0.01 {
				field.RepulsiveField[vid] += repulsionRadius / dist
			}
		}
	}

	normalizeField(field.RepulsiveField)
}

// ComputeGradient returns the direction of steepest descent in the combined field.
func ComputeGradient(pos core.VertexID, goal core.VertexID, field *PotentialField, ws *core.Workspace) core.VertexID {
	neighbors := ws.Neighbors(pos)
	if len(neighbors) == 0 {
		return pos
	}

	goalPos := ws.Vertices[goal].Pos
	bestNeighbor := pos
	bestScore := math.Inf(-1)

	for _, neighbor := range neighbors {
		score := computeVertexScore(neighbor, goalPos, field, ws)
		if score > bestScore {
			bestScore = score
			bestNeighbor = neighbor
		}
	}

	return bestNeighbor
}

// computeVertexScore evaluates how attractive a vertex is for navigation.
func computeVertexScore(v core.VertexID, goalPos core.Pos, field *PotentialField, ws *core.Workspace) float64 {
	vertexPos := ws.Vertices[v].Pos

	// Distance to goal (negative because we want to minimize)
	distToGoal := euclideanDist(vertexPos, goalPos)

	// Field components (positive = attractive, negative = repulsive)
	loadAttraction := field.LoadGradient[v]
	repulsion := field.RepulsiveField[v]

	// Combined score: minimize distance, maximize load attraction, minimize repulsion
	return -distToGoal + 2.0*loadAttraction - 3.0*repulsion
}

// euclideanDist computes 2D Euclidean distance (X, Y only).
func euclideanDist(p1, p2 core.Pos) float64 {
	dx := p1.X - p2.X
	dy := p1.Y - p2.Y
	return math.Sqrt(dx*dx + dy*dy)
}

// euclideanDist3DField computes 3D Euclidean distance for potential fields.
func euclideanDist3DField(p1, p2 core.Pos) float64 {
	dx := p1.X - p2.X
	dy := p1.Y - p2.Y
	dz := p1.Z - p2.Z
	return math.Sqrt(dx*dx + dy*dy + dz*dz)
}

// taskAttractionStrength returns attraction multiplier for task type.
func taskAttractionStrength(t core.TaskType) float64 {
	switch t {
	case core.SwapBattery:
		return 10.0 // Highest priority
	case core.SwapModule:
		return 7.0
	case core.Diagnose:
		return 5.0
	case core.Clean:
		return 3.0
	case core.AerialInspect:
		return 6.0 // Aerial inspection
	case core.AerialDelivery:
		return 8.0 // Delivery is higher priority
	case core.AerialSurvey:
		return 4.0 // Survey is lower priority
	default:
		return 1.0
	}
}

// robotRepulsionRadius returns repulsion radius for robot type.
func robotRepulsionRadius(t core.RobotType) float64 {
	switch t {
	case core.TypeB:
		return 5.0 // Larger footprint
	case core.TypeA:
		return 2.0
	case core.TypeC:
		return 1.5 // Drones have smaller footprint but fast
	default:
		return 1.0
	}
}

// SetSlackGradient sets the slack gradient from external source (e.g., FieldBridge).
// This enables field-guided planning based on EK-KOR2 module state.
func (f *PotentialField) SetSlackGradient(gradients map[core.VertexID]float64) {
	for vid, gradient := range gradients {
		f.SlackGradient[vid] = gradient
	}
}

// GetSlackGradient returns the slack gradient at a vertex.
func (f *PotentialField) GetSlackGradient(v core.VertexID) float64 {
	if g, ok := f.SlackGradient[v]; ok {
		return g
	}
	return 0
}

// SetSlackGradient sets slack gradient for PotentialField3D.
func (f *PotentialField3D) SetSlackGradient(gradients map[core.VertexID]float64) {
	f.PotentialField.SetSlackGradient(gradients)
}

// normalizeField scales values to [0, 1] range.
func normalizeField(m map[core.VertexID]float64) {
	if len(m) == 0 {
		return
	}

	minVal, maxVal := math.Inf(1), math.Inf(-1)
	for _, v := range m {
		if v < minVal {
			minVal = v
		}
		if v > maxVal {
			maxVal = v
		}
	}

	rang := maxVal - minVal
	if rang < 0.001 {
		return // Avoid division by near-zero
	}

	for k, v := range m {
		m[k] = (v - minVal) / rang
	}
}

// PotentialField3D extends PotentialField with 3D and drone-specific gradients.
type PotentialField3D struct {
	PotentialField                                    // Embed 2D field
	AltitudePreference map[core.VertexID]float64      // Preferred flying heights
	ChargingAttraction map[core.VertexID]float64      // Attraction to charging pads
	LayerGradients     map[core.AirspaceLayer]float64 // Preference for each layer
}

// NewPotentialField3D creates an empty 3D potential field.
func NewPotentialField3D() *PotentialField3D {
	return &PotentialField3D{
		PotentialField:     *NewPotentialField(),
		AltitudePreference: make(map[core.VertexID]float64),
		ChargingAttraction: make(map[core.VertexID]float64),
		LayerGradients:     make(map[core.AirspaceLayer]float64),
	}
}

// ComputePotentialField3D computes 3D field gradients for drones.
func ComputePotentialField3D(ws *core.Workspace, robots []*core.Robot, tasks []*core.Task) *PotentialField3D {
	field := NewPotentialField3D()

	// Initialize layer preferences (higher layers preferred for transit)
	field.LayerGradients[core.LayerGround] = 0.0
	field.LayerGradients[core.Layer1] = 0.3 // Handoff layer
	field.LayerGradients[core.Layer2] = 0.7 // Work layer
	field.LayerGradients[core.Layer3] = 1.0 // Transit layer (preferred)

	// Initialize all vertices
	for vid, v := range ws.Vertices {
		field.LoadGradient[vid] = 0
		field.ThermalGradient[vid] = 0
		field.PowerTrajectory[vid] = 0
		field.RepulsiveField[vid] = 0
		field.AltitudePreference[vid] = field.LayerGradients[v.Layer]
		field.ChargingAttraction[vid] = 0
	}

	// Compute attractive field toward task locations
	for _, task := range tasks {
		taskVertex := ws.Vertices[task.Location]
		if taskVertex == nil {
			continue
		}
		taskPos := taskVertex.Pos

		for vid, vertex := range ws.Vertices {
			// 3D distance-based attraction
			dist := euclideanDist3DField(vertex.Pos, taskPos)
			if dist < 0.1 {
				dist = 0.1
			}

			attractStrength := taskAttractionStrength(task.Type)
			field.LoadGradient[vid] += attractStrength / (dist * dist)
		}
	}

	// Compute repulsive field from robot positions
	for _, robot := range robots {
		robotVertex := ws.Vertices[robot.Start]
		if robotVertex == nil {
			continue
		}
		robotPos := robotVertex.Pos
		repulsionRadius := robotRepulsionRadius(robot.Type)

		for vid, vertex := range ws.Vertices {
			dist := euclideanDist3DField(vertex.Pos, robotPos)
			if dist < repulsionRadius && dist > 0.01 {
				field.RepulsiveField[vid] += repulsionRadius / dist
			}
		}
	}

	// Compute charging attraction (for low-battery drones)
	for vid, v := range ws.Vertices {
		if v.IsPad && v.Layer == core.LayerGround {
			padPos := v.Pos
			for vid2, v2 := range ws.Vertices {
				dist := euclideanDist3DField(padPos, v2.Pos)
				if dist < 0.1 {
					dist = 0.1
				}
				field.ChargingAttraction[vid2] += 10.0 / dist
			}
			_ = vid // Use vid to satisfy compiler
		}
	}

	// Normalize fields
	normalizeField(field.LoadGradient)
	normalizeField(field.ThermalGradient)
	normalizeField(field.RepulsiveField)
	normalizeField(field.ChargingAttraction)
	normalizeField(field.AltitudePreference)

	return field
}

// ComputeGradient3D returns best next vertex for 3D navigation.
func ComputeGradient3D(pos core.VertexID, goal core.VertexID, field *PotentialField3D, ws *core.Workspace, robot *core.Robot) core.VertexID {
	neighbors := ws.Neighbors(pos)
	if len(neighbors) == 0 {
		return pos
	}

	goalVertex := ws.Vertices[goal]
	if goalVertex == nil {
		return pos
	}
	goalPos := goalVertex.Pos

	bestNeighbor := pos
	bestScore := math.Inf(-1)

	for _, neighbor := range neighbors {
		neighborVertex := ws.Vertices[neighbor]
		if neighborVertex == nil {
			continue
		}

		// Skip no-fly zones for drones
		if robot.Type == core.TypeC && neighborVertex.NoFlyZone {
			continue
		}

		score := computeVertexScore3D(neighbor, goalPos, field, ws, robot)
		if score > bestScore {
			bestScore = score
			bestNeighbor = neighbor
		}
	}

	return bestNeighbor
}

// computeVertexScore3D evaluates vertex attractiveness for 3D navigation.
func computeVertexScore3D(v core.VertexID, goalPos core.Pos, field *PotentialField3D, ws *core.Workspace, robot *core.Robot) float64 {
	vertex := ws.Vertices[v]
	if vertex == nil {
		return math.Inf(-1)
	}
	vertexPos := vertex.Pos

	// Distance to goal
	distToGoal := euclideanDist3DField(vertexPos, goalPos)

	// Field components
	loadAttraction := field.LoadGradient[v]
	repulsion := field.RepulsiveField[v]
	altitudePref := field.AltitudePreference[v]

	// Base score
	score := -distToGoal + 2.0*loadAttraction - 3.0*repulsion

	// For drones, add altitude preference and charging attraction
	if robot.Type == core.TypeC {
		score += altitudePref * 1.5

		// If low battery, increase charging pad attraction
		if robot.IsLowBattery() {
			score += field.ChargingAttraction[v] * 5.0
		}
	}

	return score
}

// UpdateFieldWithRobotPositions3D updates repulsive field for 3D.
func UpdateFieldWithRobotPositions3D(field *PotentialField3D, ws *core.Workspace, positions map[core.RobotID]core.VertexID) {
	// Reset repulsive field
	for vid := range field.RepulsiveField {
		field.RepulsiveField[vid] = 0
	}

	// Add repulsion from current positions
	for _, pos := range positions {
		robotVertex := ws.Vertices[pos]
		if robotVertex == nil {
			continue
		}
		robotPos := robotVertex.Pos
		repulsionRadius := 3.0

		for vid, vertex := range ws.Vertices {
			dist := euclideanDist3DField(vertex.Pos, robotPos)
			if dist < repulsionRadius && dist > 0.01 {
				field.RepulsiveField[vid] += repulsionRadius / dist
			}
		}
	}

	normalizeField(field.RepulsiveField)
}

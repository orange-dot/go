// Package bridge provides the planning-execution bridge between
// MAPF-HET planning algorithms and EK-KOR2 field-based execution.
//
// MAPF-HET Paper Section V.F: Planning-Execution Bridge
//
// Key concepts:
// - FIELD_SLACK = clamp(slack/τ_normalize, 0, 1)
// - Gradient: ∇FIELD_SLACK = (1/k)Σ(slackⱼ - slackᵢ)
// - Negative gradient → offer assistance
// - Capability bitmask for task matching
package bridge

import (
	"math"
	"sync"
	"time"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// SlackNormalizeTau is the normalization constant for slack (in seconds).
// Slack values are normalized: FIELD_SLACK = clamp(slack/τ, 0, 1)
const SlackNormalizeTau = 60.0 // 60 seconds

// DeviationThreshold is the distance threshold for triggering replanning.
const DeviationThreshold = 2.0

// FieldBridge maintains the connection between MAPF-HET planning
// and EK-KOR2 field-based execution.
type FieldBridge struct {
	mu sync.RWMutex

	// SlackField maps module IDs to their current slack values.
	// Updated via UpdateSlackField from EK-KOR2 field updates.
	SlackField map[uint8]float64

	// NormalizedSlack maps vertex IDs to normalized slack [0,1].
	NormalizedSlack map[core.VertexID]float64

	// PlannedPaths stores the MAPF-HET computed paths.
	PlannedPaths map[core.RobotID]core.Path

	// ActualPositions tracks current robot positions.
	ActualPositions map[core.RobotID]core.VertexID

	// ReplanThreshold is the deviation threshold for triggering replan.
	ReplanThreshold float64

	// LastReplanTime tracks when replanning last occurred.
	LastReplanTime time.Time

	// ModuleToVertex maps EK-KOR2 module IDs to workspace vertices.
	ModuleToVertex map[uint8]core.VertexID

	// Workspace reference for distance calculations.
	Workspace *core.Workspace

	// K is the number of neighbors for gradient computation.
	K int
}

// NewFieldBridge creates a new planning-execution bridge.
func NewFieldBridge(ws *core.Workspace, k int) *FieldBridge {
	return &FieldBridge{
		SlackField:      make(map[uint8]float64),
		NormalizedSlack: make(map[core.VertexID]float64),
		PlannedPaths:    make(map[core.RobotID]core.Path),
		ActualPositions: make(map[core.RobotID]core.VertexID),
		ReplanThreshold: DeviationThreshold,
		ModuleToVertex:  make(map[uint8]core.VertexID),
		Workspace:       ws,
		K:               k,
	}
}

// UpdateSlackField updates the slack value for a module.
// Called when receiving EK-KOR2 field updates.
//
// slack_us is the slack value in microseconds from ekk_deadline_t.
func (b *FieldBridge) UpdateSlackField(moduleID uint8, slackUS uint64) {
	b.mu.Lock()
	defer b.mu.Unlock()

	// Convert to seconds
	slackSec := float64(slackUS) / 1e6

	// Store raw slack
	b.SlackField[moduleID] = slackSec

	// Normalize to [0,1]
	normalized := math.Min(math.Max(slackSec/SlackNormalizeTau, 0), 1)

	// Update vertex mapping if exists
	if vid, ok := b.ModuleToVertex[moduleID]; ok {
		b.NormalizedSlack[vid] = normalized
	}
}

// RegisterModule maps an EK-KOR2 module ID to a workspace vertex.
func (b *FieldBridge) RegisterModule(moduleID uint8, vertexID core.VertexID) {
	b.mu.Lock()
	defer b.mu.Unlock()

	b.ModuleToVertex[moduleID] = vertexID

	// Initialize with full slack (no deadline pressure)
	b.NormalizedSlack[vertexID] = 1.0
}

// ComputeSlackGradient computes the slack gradient at a vertex.
//
// Formula: ∇FIELD_SLACK = (1/k)Σ(slackⱼ - slackᵢ)
//
// Returns:
// - Positive: neighbors have more slack (I should take work)
// - Negative: neighbors have less slack (I should offer help)
// - Zero: balanced
func (b *FieldBridge) ComputeSlackGradient(v core.VertexID, neighbors []core.VertexID) float64 {
	b.mu.RLock()
	defer b.mu.RUnlock()

	mySlack, ok := b.NormalizedSlack[v]
	if !ok {
		mySlack = 1.0 // Default to full slack
	}

	if len(neighbors) == 0 {
		return 0
	}

	// Compute average neighbor slack
	sum := 0.0
	count := 0
	for _, n := range neighbors {
		if ns, ok := b.NormalizedSlack[n]; ok {
			sum += ns - mySlack
			count++
		}
	}

	if count == 0 {
		return 0
	}

	return sum / float64(count)
}

// UpdatePlannedPaths stores new paths from MAPF-HET planning.
func (b *FieldBridge) UpdatePlannedPaths(paths map[core.RobotID]core.Path) {
	b.mu.Lock()
	defer b.mu.Unlock()

	b.PlannedPaths = paths
	b.LastReplanTime = time.Now()
}

// UpdateActualPosition updates a robot's current position.
func (b *FieldBridge) UpdateActualPosition(robotID core.RobotID, pos core.VertexID) {
	b.mu.Lock()
	defer b.mu.Unlock()

	b.ActualPositions[robotID] = pos
}

// CheckDeviation checks if a robot has deviated from its planned path.
// Returns true if deviation exceeds threshold and replanning is needed.
func (b *FieldBridge) CheckDeviation(robotID core.RobotID, currentTime float64) (bool, float64) {
	b.mu.RLock()
	defer b.mu.RUnlock()

	path, ok := b.PlannedPaths[robotID]
	if !ok || len(path) == 0 {
		return false, 0
	}

	actualPos, ok := b.ActualPositions[robotID]
	if !ok {
		return false, 0
	}

	// Find expected position at current time
	expectedPos := getPositionAtTime(path, currentTime)

	// Compute deviation distance
	actualVertex := b.Workspace.Vertices[actualPos]
	expectedVertex := b.Workspace.Vertices[expectedPos]

	if actualVertex == nil || expectedVertex == nil {
		return false, 0
	}

	deviation := euclideanDist(actualVertex.Pos, expectedVertex.Pos)

	return deviation > b.ReplanThreshold, deviation
}

// NeedsReplan checks if any robot needs replanning.
func (b *FieldBridge) NeedsReplan(currentTime float64) bool {
	b.mu.RLock()
	defer b.mu.RUnlock()

	for robotID := range b.PlannedPaths {
		needsReplan, _ := b.CheckDeviation(robotID, currentTime)
		if needsReplan {
			return true
		}
	}
	return false
}

// GetSlackGradientField returns the slack gradient for all vertices.
// Used by HYBRID-CBS for field-aware planning.
func (b *FieldBridge) GetSlackGradientField() map[core.VertexID]float64 {
	b.mu.RLock()
	defer b.mu.RUnlock()

	gradients := make(map[core.VertexID]float64)

	for vid := range b.Workspace.Vertices {
		neighbors := b.Workspace.Neighbors(vid)
		gradients[vid] = b.ComputeSlackGradient(vid, neighbors)
	}

	return gradients
}

// SyncSlackToField updates a PotentialField's SlackGradient from bridge state.
// Called before planning to incorporate EK-KOR2 field state.
func (b *FieldBridge) SyncSlackToField(field interface{ SetSlackGradient(map[core.VertexID]float64) }) {
	gradients := b.GetSlackGradientField()
	field.SetSlackGradient(gradients)
}

// getPositionAtTime finds the expected position along a path at a given time.
func getPositionAtTime(path core.Path, t float64) core.VertexID {
	if len(path) == 0 {
		return 0
	}

	// Binary search would be more efficient, but linear is fine for typical path lengths
	for i := len(path) - 1; i >= 0; i-- {
		if path[i].T <= t {
			return path[i].V
		}
	}

	return path[0].V
}

// euclideanDist computes 2D Euclidean distance.
func euclideanDist(p1, p2 core.Pos) float64 {
	dx := p1.X - p2.X
	dy := p1.Y - p2.Y
	return math.Sqrt(dx*dx + dy*dy)
}

// ModuleState represents state received from EK-KOR2 modules.
// Maps to ekk_field_t structure from C code.
type ModuleState struct {
	ModuleID     uint8
	Load         float64 // Normalized [0,1]
	Thermal      float64 // Normalized [0,1]
	Power        float64 // Normalized [0,1]
	Slack        float64 // Normalized [0,1]
	Capabilities core.Capability
	Timestamp    time.Time
}

// UpdateFromModuleState updates bridge state from received EK-KOR2 module state.
func (b *FieldBridge) UpdateFromModuleState(state ModuleState) {
	b.mu.Lock()
	defer b.mu.Unlock()

	// Update slack (denormalize to seconds for storage)
	b.SlackField[state.ModuleID] = state.Slack * SlackNormalizeTau

	// Update vertex mapping
	if vid, ok := b.ModuleToVertex[state.ModuleID]; ok {
		b.NormalizedSlack[vid] = state.Slack
	}
}

// GetModuleCapabilities returns capabilities for a module.
func (b *FieldBridge) GetModuleCapabilities(moduleID uint8) core.Capability {
	// In a full implementation, this would query the module
	// For now, return default capabilities based on module ID ranges
	if moduleID < 100 {
		return core.CapRobotTypeA
	} else if moduleID < 200 {
		return core.CapRobotTypeB
	}
	return core.CapRobotTypeC
}

// FilterTasksByCapability returns tasks that a module can perform.
func (b *FieldBridge) FilterTasksByCapability(moduleID uint8, tasks []*core.Task) []*core.Task {
	caps := b.GetModuleCapabilities(moduleID)

	var result []*core.Task
	for _, task := range tasks {
		requiredCap := core.TaskTypeToCapability(task.Type)
		if core.CanPerformCapability(caps, requiredCap) {
			result = append(result, task)
		}
	}
	return result
}

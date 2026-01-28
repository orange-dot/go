// Package bridge - EK-KOR2 Field Adapter
//
// Provides the interface between MAPF-HET and EK-KOR2/ROJ field-based coordination.
// This adapter can operate in two modes:
// 1. Simulation mode: Generates synthetic field updates for testing
// 2. Live mode: Receives real field updates via CAN-FD or UDP
//
// Paper Reference: MAPF-HET Section V.F - Planning-Execution Bridge

package bridge

import (
	"context"
	"math"
	"math/rand"
	"sync"
	"time"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// FieldPublishInterval is the rate at which modules publish field state (50ms per paper spec)
const FieldPublishInterval = 50 * time.Millisecond

// K7Neighbors is the standard neighbor count for scale-free topology
const K7Neighbors = 7

// ThermalDecayRate is the exponential decay rate for thermal tags (per second)
const ThermalDecayRate = 0.1

// FieldUpdateCallback is called when field state changes
type FieldUpdateCallback func(moduleID uint8, state ModuleState)

// ReplanCallback is called when replanning is needed
type ReplanCallback func(reason string)

// EKKAdapter interfaces with EK-KOR2 field coordination system.
// It receives field updates from modules and provides them to MAPF-HET planning.
type EKKAdapter struct {
	mu sync.RWMutex

	// Bridge for updating planning state
	bridge *FieldBridge

	// Module states indexed by module ID
	modules map[uint8]*ModuleState

	// Topology: k=7 neighbors for each module
	neighbors map[uint8][]uint8

	// Thermal tags received from neighbors
	thermalTags map[uint8][]ThermalTag

	// Simulation mode flag
	simulationMode bool

	// Random source for simulation
	rng *rand.Rand

	// Callbacks
	onFieldUpdate FieldUpdateCallback
	onReplan      ReplanCallback

	// Running state
	ctx    context.Context
	cancel context.CancelFunc
	wg     sync.WaitGroup
}

// ThermalTag represents a temperature marker deposited by a module
type ThermalTag struct {
	SourceID    uint8
	Temperature float64 // Celsius
	PowerLevel  float64 // 0-1
	CreatedAt   time.Time
	Strength    float64 // Initial strength, decays over time
}

// NewEKKAdapter creates a new EK-KOR2 adapter
func NewEKKAdapter(bridge *FieldBridge, simulationMode bool) *EKKAdapter {
	return &EKKAdapter{
		bridge:         bridge,
		modules:        make(map[uint8]*ModuleState),
		neighbors:      make(map[uint8][]uint8),
		thermalTags:    make(map[uint8][]ThermalTag),
		simulationMode: simulationMode,
		rng:            rand.New(rand.NewSource(time.Now().UnixNano())),
	}
}

// SetFieldUpdateCallback sets the callback for field updates
func (a *EKKAdapter) SetFieldUpdateCallback(cb FieldUpdateCallback) {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.onFieldUpdate = cb
}

// SetReplanCallback sets the callback for replan requests
func (a *EKKAdapter) SetReplanCallback(cb ReplanCallback) {
	a.mu.Lock()
	defer a.mu.Unlock()
	a.onReplan = cb
}

// RegisterModule registers a module with its vertex mapping
func (a *EKKAdapter) RegisterModule(moduleID uint8, vertexID core.VertexID, caps core.Capability) {
	a.mu.Lock()
	defer a.mu.Unlock()

	state := &ModuleState{
		ModuleID:     moduleID,
		Load:         0.5,
		Thermal:      0.5,
		Power:        0.5,
		Slack:        1.0,
		Capabilities: caps,
		Timestamp:    time.Now(),
	}
	a.modules[moduleID] = state

	// Register with bridge
	a.bridge.RegisterModule(moduleID, vertexID)
}

// SetupTopology establishes k=7 neighbor relationships
func (a *EKKAdapter) SetupTopology(moduleIDs []uint8) {
	a.mu.Lock()
	defer a.mu.Unlock()

	// For each module, select k=7 neighbors (scale-free topology simulation)
	for _, id := range moduleIDs {
		var neighbors []uint8

		// Simple approach: select k nearest by ID (wrap-around)
		// In production, this would use actual physical proximity or network topology
		n := len(moduleIDs)
		for i := 1; i <= K7Neighbors && i < n; i++ {
			// Select neighbors both forward and backward
			fwdIdx := (int(id) + i) % n
			bwdIdx := (int(id) - i + n) % n

			if fwdIdx < n {
				neighbors = append(neighbors, moduleIDs[fwdIdx])
			}
			if len(neighbors) < K7Neighbors && bwdIdx != fwdIdx && bwdIdx < n {
				neighbors = append(neighbors, moduleIDs[bwdIdx])
			}

			if len(neighbors) >= K7Neighbors {
				break
			}
		}

		a.neighbors[id] = neighbors
		a.thermalTags[id] = make([]ThermalTag, 0)
	}
}

// Start begins the field update loop
func (a *EKKAdapter) Start(ctx context.Context) {
	a.ctx, a.cancel = context.WithCancel(ctx)

	if a.simulationMode {
		a.wg.Add(1)
		go a.runSimulationLoop()
	}

	a.wg.Add(1)
	go a.runFieldPublishLoop()
}

// Stop halts the adapter
func (a *EKKAdapter) Stop() {
	if a.cancel != nil {
		a.cancel()
	}
	a.wg.Wait()
}

// runFieldPublishLoop publishes field state at 50ms intervals
func (a *EKKAdapter) runFieldPublishLoop() {
	defer a.wg.Done()

	ticker := time.NewTicker(FieldPublishInterval)
	defer ticker.Stop()

	for {
		select {
		case <-a.ctx.Done():
			return
		case <-ticker.C:
			a.publishFieldUpdates()
		}
	}
}

// publishFieldUpdates sends current module states to the bridge
func (a *EKKAdapter) publishFieldUpdates() {
	a.mu.RLock()
	modules := make([]*ModuleState, 0, len(a.modules))
	for _, m := range a.modules {
		modules = append(modules, m)
	}
	a.mu.RUnlock()

	for _, state := range modules {
		// Update bridge
		a.bridge.UpdateFromModuleState(*state)

		// Notify callback
		a.mu.RLock()
		cb := a.onFieldUpdate
		a.mu.RUnlock()
		if cb != nil {
			cb(state.ModuleID, *state)
		}
	}
}

// runSimulationLoop generates synthetic field updates for testing
func (a *EKKAdapter) runSimulationLoop() {
	defer a.wg.Done()

	// Simulation tick rate (100ms)
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	simTime := 0.0

	for {
		select {
		case <-a.ctx.Done():
			return
		case <-ticker.C:
			simTime += 0.1
			a.simulateTick(simTime)
		}
	}
}

// simulateTick performs one simulation step
func (a *EKKAdapter) simulateTick(simTime float64) {
	a.mu.Lock()
	defer a.mu.Unlock()

	now := time.Now()

	for id, state := range a.modules {
		// Simulate load changes (random walk with mean reversion)
		state.Load = clamp(state.Load+a.rng.NormFloat64()*0.05-0.01*(state.Load-0.5), 0, 1)

		// Simulate thermal based on load (thermal inertia)
		targetThermal := 0.3 + 0.5*state.Load
		state.Thermal = state.Thermal + 0.1*(targetThermal-state.Thermal) + a.rng.NormFloat64()*0.02

		// Simulate power based on load
		state.Power = clamp(state.Load*1.1+a.rng.NormFloat64()*0.05, 0, 1)

		// Simulate slack (decreases as time passes, resets on task completion)
		state.Slack = clamp(state.Slack-0.01+a.rng.NormFloat64()*0.02, 0, 1)

		// Occasionally simulate task completion (resets slack)
		if a.rng.Float64() < 0.01 {
			state.Slack = 1.0
		}

		state.Timestamp = now

		// Deposit thermal tag for neighbors
		a.depositThermalTag(id, state)

		// Process received thermal tags (stigmergy)
		a.processThermalTags(id, state)
	}

	// Decay and cleanup old thermal tags
	a.decayThermalTags()
}

// depositThermalTag creates a thermal marker for neighbors
func (a *EKKAdapter) depositThermalTag(moduleID uint8, state *ModuleState) {
	tag := ThermalTag{
		SourceID:    moduleID,
		Temperature: 30.0 + state.Thermal*40.0, // Map 0-1 to 30-70°C
		PowerLevel:  state.Power,
		CreatedAt:   time.Now(),
		Strength:    1.0,
	}

	// Send to all neighbors
	for _, neighborID := range a.neighbors[moduleID] {
		if tags, ok := a.thermalTags[neighborID]; ok {
			a.thermalTags[neighborID] = append(tags, tag)
		}
	}
}

// processThermalTags implements stigmergy-based thermal optimization
func (a *EKKAdapter) processThermalTags(moduleID uint8, state *ModuleState) {
	tags := a.thermalTags[moduleID]
	if len(tags) == 0 {
		return
	}

	// Compute weighted average of neighbor temperatures
	var sumTemp, sumWeight float64
	now := time.Now()

	for _, tag := range tags {
		age := now.Sub(tag.CreatedAt).Seconds()
		weight := tag.Strength * math.Exp(-ThermalDecayRate*age)
		if weight > 0.01 {
			sumTemp += tag.Temperature * weight
			sumWeight += weight
		}
	}

	if sumWeight > 0 {
		avgNeighborTemp := sumTemp / sumWeight
		myTemp := 30.0 + state.Thermal*40.0

		// Gradient: positive if neighbors hotter, negative if cooler
		gradient := avgNeighborTemp - myTemp

		// Adjust load based on gradient (balance thermal load)
		// If neighbors are hotter, we can take more load
		// If neighbors are cooler, we should reduce load
		adjustment := gradient * 0.001 // Small adjustment per tick
		state.Load = clamp(state.Load+adjustment, 0, 1)
	}
}

// decayThermalTags removes expired tags
func (a *EKKAdapter) decayThermalTags() {
	maxAge := 30 * time.Second
	now := time.Now()

	for moduleID, tags := range a.thermalTags {
		filtered := make([]ThermalTag, 0, len(tags))
		for _, tag := range tags {
			if now.Sub(tag.CreatedAt) < maxAge {
				filtered = append(filtered, tag)
			}
		}
		a.thermalTags[moduleID] = filtered
	}
}

// GetModuleState returns current state of a module
func (a *EKKAdapter) GetModuleState(moduleID uint8) (ModuleState, bool) {
	a.mu.RLock()
	defer a.mu.RUnlock()

	if state, ok := a.modules[moduleID]; ok {
		return *state, true
	}
	return ModuleState{}, false
}

// GetAllModuleStates returns all module states
func (a *EKKAdapter) GetAllModuleStates() []ModuleState {
	a.mu.RLock()
	defer a.mu.RUnlock()

	states := make([]ModuleState, 0, len(a.modules))
	for _, state := range a.modules {
		states = append(states, *state)
	}
	return states
}

// ComputeThermalVariance calculates temperature variance across modules
func (a *EKKAdapter) ComputeThermalVariance() float64 {
	a.mu.RLock()
	defer a.mu.RUnlock()

	if len(a.modules) < 2 {
		return 0
	}

	// Collect temperatures
	temps := make([]float64, 0, len(a.modules))
	for _, state := range a.modules {
		temps = append(temps, 30.0+state.Thermal*40.0) // Convert to Celsius
	}

	// Compute mean
	sum := 0.0
	for _, t := range temps {
		sum += t
	}
	mean := sum / float64(len(temps))

	// Compute variance
	variance := 0.0
	for _, t := range temps {
		diff := t - mean
		variance += diff * diff
	}
	variance /= float64(len(temps))

	return math.Sqrt(variance) // Return standard deviation
}

// InjectFieldUpdate allows external systems to inject field updates (for live mode)
func (a *EKKAdapter) InjectFieldUpdate(moduleID uint8, load, thermal, power, slack float64) {
	a.mu.Lock()
	defer a.mu.Unlock()

	state, ok := a.modules[moduleID]
	if !ok {
		return
	}

	state.Load = clamp(load, 0, 1)
	state.Thermal = clamp(thermal, 0, 1)
	state.Power = clamp(power, 0, 1)
	state.Slack = clamp(slack, 0, 1)
	state.Timestamp = time.Now()
}

// UpdateSlackFromSchedule updates module slack based on MAPF-HET schedule
func (a *EKKAdapter) UpdateSlackFromSchedule(schedule core.Schedule, currentTime float64, deadline float64) {
	a.mu.Lock()
	defer a.mu.Unlock()

	// Map task completion times to modules
	// This is a simplified version - production would track task-module assignments
	for _, completionTime := range schedule {
		slack := deadline - completionTime
		if slack < 0 {
			slack = 0
		}
		normalizedSlack := math.Min(slack/SlackNormalizeTau, 1.0)

		// Update all modules with average slack (simplified)
		for _, state := range a.modules {
			// Weighted update
			state.Slack = 0.9*state.Slack + 0.1*normalizedSlack
		}
	}
}

// TriggerReplan requests replanning due to field state change
func (a *EKKAdapter) TriggerReplan(reason string) {
	a.mu.RLock()
	cb := a.onReplan
	a.mu.RUnlock()

	if cb != nil {
		cb(reason)
	}
}

// GetNeighborSlackGradient computes slack gradient for a module
func (a *EKKAdapter) GetNeighborSlackGradient(moduleID uint8) float64 {
	a.mu.RLock()
	defer a.mu.RUnlock()

	state, ok := a.modules[moduleID]
	if !ok {
		return 0
	}

	neighbors := a.neighbors[moduleID]
	if len(neighbors) == 0 {
		return 0
	}

	// Compute gradient: (1/k) * Σ(neighbor_slack - my_slack)
	sum := 0.0
	count := 0
	for _, neighborID := range neighbors {
		if neighborState, ok := a.modules[neighborID]; ok {
			sum += neighborState.Slack - state.Slack
			count++
		}
	}

	if count == 0 {
		return 0
	}

	return sum / float64(count)
}

// Metrics returns current adapter metrics
func (a *EKKAdapter) Metrics() AdapterMetrics {
	a.mu.RLock()
	defer a.mu.RUnlock()

	var totalLoad, totalThermal, totalSlack float64
	for _, state := range a.modules {
		totalLoad += state.Load
		totalThermal += state.Thermal
		totalSlack += state.Slack
	}

	n := float64(len(a.modules))
	if n == 0 {
		n = 1
	}

	return AdapterMetrics{
		ModuleCount:      len(a.modules),
		AvgLoad:          totalLoad / n,
		AvgThermal:       totalThermal / n,
		AvgSlack:         totalSlack / n,
		ThermalVariance:  a.ComputeThermalVariance(),
		SimulationMode:   a.simulationMode,
		PublishIntervalMs: int(FieldPublishInterval.Milliseconds()),
	}
}

// AdapterMetrics contains performance metrics
type AdapterMetrics struct {
	ModuleCount       int
	AvgLoad           float64
	AvgThermal        float64
	AvgSlack          float64
	ThermalVariance   float64
	SimulationMode    bool
	PublishIntervalMs int
}

// clamp restricts a value to [min, max]
func clamp(v, min, max float64) float64 {
	if v < min {
		return min
	}
	if v > max {
		return max
	}
	return v
}

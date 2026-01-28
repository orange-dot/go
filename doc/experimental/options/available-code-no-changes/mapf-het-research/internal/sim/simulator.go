// Package sim provides simulation infrastructure for MAPF-HET benchmarking.
//
// This package enables end-to-end simulation of:
// - MAPF-HET planning algorithms
// - EK-KOR2 field-based execution
// - Robot movement and task execution
// - Metrics collection for paper validation
//
// Paper Reference: Section V - Experimental Evaluation

package sim

import (
	"context"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"sync"
	"time"

	"github.com/elektrokombinacija/mapf-het-research/internal/algo"
	"github.com/elektrokombinacija/mapf-het-research/internal/bridge"
	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// SimulationConfig configures the simulation parameters
type SimulationConfig struct {
	// Instance to simulate
	Instance *core.Instance

	// Solver to use
	Solver algo.Solver

	// Simulation duration in seconds
	Duration float64

	// Time step for simulation (seconds)
	TimeStep float64

	// Enable EK-KOR2 field integration
	EnableFieldIntegration bool

	// Number of EK-KOR2 modules to simulate
	ModuleCount int

	// Random seed for reproducibility
	Seed int64

	// Enable verbose logging
	Verbose bool
}

// DefaultConfig returns default simulation configuration
func DefaultConfig() SimulationConfig {
	return SimulationConfig{
		Duration:               3600, // 1 hour
		TimeStep:               0.1,  // 100ms
		EnableFieldIntegration: true,
		ModuleCount:            50,
		Seed:                   42,
		Verbose:                false,
	}
}

// SimulationMetrics collects metrics during simulation
type SimulationMetrics struct {
	// Timing
	StartTime     time.Time
	EndTime       time.Time
	SimulatedTime float64 // Simulated seconds

	// Planning
	PlanningAttempts    int
	PlanningSuccesses   int
	TotalPlanningTimeMs float64
	ReplanEvents        int

	// Tasks
	TasksAssigned  int
	TasksCompleted int
	TasksFailed    int

	// Deadlines
	DeadlinesMet     int
	DeadlinesMissed  int
	AvgSlack         float64
	MinSlack         float64
	MaxSlack         float64

	// Energy (for drones)
	EnergyViolations int
	AvgBatteryLevel  float64
	EmergencyLandings int

	// Conflicts
	ConflictsDetected int
	ConflictsResolved int

	// EK-KOR2 Field
	FieldUpdates         int
	AvgThermalVariance   float64
	FinalThermalVariance float64

	// Path metrics
	TotalMakespan float64
	AvgMakespan   float64
}

// Simulator runs MAPF-HET simulations with EK-KOR2 integration
type Simulator struct {
	mu sync.Mutex

	config SimulationConfig

	// State
	currentTime float64
	solution    *core.Solution
	robotPoses  map[core.RobotID]core.VertexID

	// EK-KOR2 integration
	fieldBridge *bridge.FieldBridge
	ekkAdapter  *bridge.EKKAdapter

	// Potential field for field-guided planning
	potentialField *algo.PotentialField

	// Metrics
	metrics SimulationMetrics

	// Running state
	ctx    context.Context
	cancel context.CancelFunc
}

// NewSimulator creates a new simulation instance
func NewSimulator(config SimulationConfig) *Simulator {
	sim := &Simulator{
		config:     config,
		robotPoses: make(map[core.RobotID]core.VertexID),
		metrics:    SimulationMetrics{MinSlack: math.Inf(1), MaxSlack: math.Inf(-1)},
	}

	// Initialize robot positions
	if config.Instance != nil {
		for _, robot := range config.Instance.Robots {
			sim.robotPoses[robot.ID] = robot.Start
		}
	}

	// Setup EK-KOR2 integration
	if config.EnableFieldIntegration && config.Instance != nil {
		sim.setupFieldIntegration()
	}

	return sim
}

// setupFieldIntegration initializes the EK-KOR2 field system
func (s *Simulator) setupFieldIntegration() {
	// Create field bridge
	s.fieldBridge = bridge.NewFieldBridge(s.config.Instance.Workspace, bridge.K7Neighbors)

	// Create EK-KOR2 adapter in simulation mode
	s.ekkAdapter = bridge.NewEKKAdapter(s.fieldBridge, true)

	// Register modules at workspace vertices
	moduleIDs := make([]uint8, 0, s.config.ModuleCount)
	vertexIDs := make([]core.VertexID, 0)

	for vid := range s.config.Instance.Workspace.Vertices {
		vertexIDs = append(vertexIDs, vid)
	}

	for i := 0; i < s.config.ModuleCount && i < len(vertexIDs); i++ {
		moduleID := uint8(i)
		vertexID := vertexIDs[i%len(vertexIDs)]

		// Assign capabilities based on module position
		caps := core.CapRobotTypeA
		if i%3 == 1 {
			caps = core.CapRobotTypeB
		} else if i%3 == 2 {
			caps = core.CapRobotTypeC
		}

		s.ekkAdapter.RegisterModule(moduleID, vertexID, caps)
		moduleIDs = append(moduleIDs, moduleID)
	}

	// Setup k=7 topology
	s.ekkAdapter.SetupTopology(moduleIDs)

	// Create potential field
	s.potentialField = algo.ComputePotentialField(
		s.config.Instance.Workspace,
		s.config.Instance.Robots,
		s.config.Instance.Tasks,
	)

	// Set replan callback
	s.ekkAdapter.SetReplanCallback(func(reason string) {
		s.metrics.ReplanEvents++
		if s.config.Verbose {
			fmt.Printf("Replan triggered: %s\n", reason)
		}
	})
}

// Run executes the simulation
func (s *Simulator) Run(ctx context.Context) (*SimulationMetrics, error) {
	s.ctx, s.cancel = context.WithCancel(ctx)
	defer s.cancel()

	s.metrics.StartTime = time.Now()

	// Start EK-KOR2 adapter if enabled
	if s.ekkAdapter != nil {
		s.ekkAdapter.Start(s.ctx)
		defer s.ekkAdapter.Stop()
	}

	// Initial planning
	if err := s.plan(); err != nil {
		return nil, fmt.Errorf("initial planning failed: %w", err)
	}

	// Simulation loop
	for s.currentTime < s.config.Duration {
		select {
		case <-s.ctx.Done():
			break
		default:
		}

		s.step()
		s.currentTime += s.config.TimeStep

		// Periodic progress report
		if s.config.Verbose && int(s.currentTime)%60 == 0 && s.currentTime > 0 {
			fmt.Printf("Simulated: %.0fs, Tasks: %d/%d, Deadlines: %d met, %d missed\n",
				s.currentTime, s.metrics.TasksCompleted, s.metrics.TasksAssigned,
				s.metrics.DeadlinesMet, s.metrics.DeadlinesMissed)
		}
	}

	s.metrics.EndTime = time.Now()
	s.metrics.SimulatedTime = s.currentTime

	// Final metrics
	if s.ekkAdapter != nil {
		s.metrics.FinalThermalVariance = s.ekkAdapter.ComputeThermalVariance()
	}

	return &s.metrics, nil
}

// plan runs the MAPF-HET solver
func (s *Simulator) plan() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	startTime := time.Now()
	s.metrics.PlanningAttempts++

	// Sync field state to potential field before planning
	if s.fieldBridge != nil && s.potentialField != nil {
		s.fieldBridge.SyncSlackToField(s.potentialField)
	}

	// Run solver
	solution := s.config.Solver.Solve(s.config.Instance)

	planningTime := time.Since(startTime)
	s.metrics.TotalPlanningTimeMs += float64(planningTime.Milliseconds())

	if solution == nil || !solution.Feasible {
		return fmt.Errorf("solver returned no feasible solution")
	}

	s.metrics.PlanningSuccesses++
	s.solution = solution

	// Update metrics
	s.metrics.TasksAssigned = len(solution.Assignment)
	s.metrics.TotalMakespan = solution.Makespan

	// Update planned paths in bridge
	if s.fieldBridge != nil {
		s.fieldBridge.UpdatePlannedPaths(solution.Paths)
	}

	return nil
}

// step advances the simulation by one time step
func (s *Simulator) step() {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.solution == nil {
		return
	}

	// Update robot positions based on planned paths
	for robotID, path := range s.solution.Paths {
		if len(path) == 0 {
			continue
		}

		// Find position at current time
		newPos := s.getPositionAtTime(path, s.currentTime)
		s.robotPoses[robotID] = newPos

		// Update field bridge
		if s.fieldBridge != nil {
			s.fieldBridge.UpdateActualPosition(robotID, newPos)
		}
	}

	// Check for task completions
	s.checkTaskCompletions()

	// Check for deadline violations
	s.checkDeadlines()

	// Check for energy violations (drones)
	s.checkEnergyViolations()

	// Update EK-KOR2 field metrics
	if s.ekkAdapter != nil {
		s.metrics.FieldUpdates++
		variance := s.ekkAdapter.ComputeThermalVariance()
		s.metrics.AvgThermalVariance = (s.metrics.AvgThermalVariance*float64(s.metrics.FieldUpdates-1) + variance) / float64(s.metrics.FieldUpdates)
	}

	// Check if replanning is needed
	if s.fieldBridge != nil && s.fieldBridge.NeedsReplan(s.currentTime) {
		s.metrics.ReplanEvents++
		// Trigger replan (release lock first)
		s.mu.Unlock()
		_ = s.plan()
		s.mu.Lock()
	}
}

// getPositionAtTime returns robot position at given time from path
func (s *Simulator) getPositionAtTime(path core.Path, t float64) core.VertexID {
	if len(path) == 0 {
		return 0
	}

	// Find position via interpolation
	for i := len(path) - 1; i >= 0; i-- {
		if path[i].T <= t {
			return path[i].V
		}
	}

	return path[0].V
}

// checkTaskCompletions checks for completed tasks
func (s *Simulator) checkTaskCompletions() {
	for tid, completionTime := range s.solution.Schedule {
		if completionTime <= s.currentTime {
			task := s.config.Instance.TaskByID(tid)
			if task != nil {
				// Task completed
				taskDeadline := task.GetDeadline(s.config.Instance.Deadline)
				slack := taskDeadline - completionTime

				// Update slack metrics
				s.metrics.AvgSlack = (s.metrics.AvgSlack*float64(s.metrics.TasksCompleted) + slack) / float64(s.metrics.TasksCompleted+1)
				if slack < s.metrics.MinSlack {
					s.metrics.MinSlack = slack
				}
				if slack > s.metrics.MaxSlack {
					s.metrics.MaxSlack = slack
				}

				s.metrics.TasksCompleted++

				// Remove from schedule to avoid double counting
				delete(s.solution.Schedule, tid)
			}
		}
	}
}

// checkDeadlines checks for deadline violations
func (s *Simulator) checkDeadlines() {
	for tid, completionTime := range s.solution.Schedule {
		task := s.config.Instance.TaskByID(tid)
		if task == nil {
			continue
		}

		taskDeadline := task.GetDeadline(s.config.Instance.Deadline)

		if completionTime <= taskDeadline {
			// Will meet deadline
			if completionTime <= s.currentTime {
				s.metrics.DeadlinesMet++
			}
		} else {
			// Deadline violation predicted
			if s.currentTime >= taskDeadline {
				s.metrics.DeadlinesMissed++
			}
		}
	}
}

// checkEnergyViolations checks for drone energy issues
func (s *Simulator) checkEnergyViolations() {
	for _, robot := range s.config.Instance.Robots {
		if robot.Type != core.TypeC {
			continue // Only check drones
		}

		// Update average battery level
		s.metrics.AvgBatteryLevel = (s.metrics.AvgBatteryLevel + robot.BatteryPercentage()) / 2

		// Check for low battery (emergency)
		if robot.IsLowBattery() {
			s.metrics.EnergyViolations++
			if robot.BatteryPercentage() < 5 {
				s.metrics.EmergencyLandings++
			}
		}
	}
}

// Metrics returns current simulation metrics
func (s *Simulator) Metrics() SimulationMetrics {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.metrics
}

// ExportMetrics writes metrics to a JSON file
func (s *Simulator) ExportMetrics(path string) error {
	s.mu.Lock()
	metrics := s.metrics
	s.mu.Unlock()

	data, err := json.MarshalIndent(metrics, "", "  ")
	if err != nil {
		return err
	}

	return os.WriteFile(path, data, 0644)
}

// SimulationResult is the final output of a simulation run
type SimulationResult struct {
	Config  SimulationConfig   `json:"config"`
	Metrics SimulationMetrics  `json:"metrics"`
	Success bool               `json:"success"`
	Error   string             `json:"error,omitempty"`
}

// RunSimulation is a convenience function to run a complete simulation
func RunSimulation(config SimulationConfig) (*SimulationResult, error) {
	sim := NewSimulator(config)

	ctx, cancel := context.WithTimeout(context.Background(), time.Duration(config.Duration*2)*time.Second)
	defer cancel()

	metrics, err := sim.Run(ctx)

	result := &SimulationResult{
		Config:  config,
		Success: err == nil,
	}

	if err != nil {
		result.Error = err.Error()
	}

	if metrics != nil {
		result.Metrics = *metrics
	}

	return result, err
}

// Benchmark24Hour runs the 24-hour benchmark for paper validation
func Benchmark24Hour(instance *core.Instance, solver algo.Solver) (*SimulationMetrics, error) {
	config := SimulationConfig{
		Instance:               instance,
		Solver:                 solver,
		Duration:               86400, // 24 hours
		TimeStep:               0.1,   // 100ms
		EnableFieldIntegration: true,
		ModuleCount:            100,
		Seed:                   42,
		Verbose:                true,
	}

	sim := NewSimulator(config)
	return sim.Run(context.Background())
}

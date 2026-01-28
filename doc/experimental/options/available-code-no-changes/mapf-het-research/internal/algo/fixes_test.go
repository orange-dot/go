package algo

import (
	"testing"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// TestTimeCalculation verifies that path times reflect actual edge costs and robot speeds.
func TestTimeCalculation(t *testing.T) {
	ws := createGrid(5)
	robot := &core.Robot{ID: 0, Type: core.TypeA, Start: 0} // Speed = 0.5 m/s

	// Set edge cost to 2.0 meters
	ws.Edges[0] = []core.Edge{{To: 1, Cost: 2.0}}

	path := SpaceTimeAStar(ws, robot, 0, []core.VertexID{1}, nil, 100.0)

	if path == nil {
		t.Fatal("Expected path, got nil")
	}

	if len(path) < 2 {
		t.Fatalf("Expected at least 2 vertices in path, got %d", len(path))
	}

	// TypeA speed is 0.5 m/s, edge cost is 2.0m, so time should be 2.0/0.5 = 4.0 seconds
	expectedTime := 2.0 / robot.Speed()
	actualTime := path[1].T - path[0].T

	if !timeEqual(actualTime, expectedTime) {
		t.Errorf("Expected travel time %.2f, got %.2f", expectedTime, actualTime)
	}
}

// TestEdgeConstraintEnforcement verifies that edge constraints (swap conflicts) are enforced.
func TestEdgeConstraintEnforcement(t *testing.T) {
	ws := createGrid(3)
	robot := &core.Robot{ID: 0, Type: core.TypeA, Start: 0}

	// Create edge constraint: robot 0 cannot traverse 0->1 at time 2.0
	constraints := []Constraint{
		{
			Robot:    0,
			IsEdge:   true,
			EdgeFrom: 0,
			EdgeTo:   1,
			Time:     2.0,
		},
	}

	path := SpaceTimeAStar(ws, robot, 0, []core.VertexID{1}, constraints, 100.0)

	if path == nil {
		t.Fatal("Expected path to exist (should route around constraint)")
	}

	// Verify the path doesn't violate the constraint
	for i := 1; i < len(path); i++ {
		if path[i-1].V == 0 && path[i].V == 1 && timeEqual(path[i].T, 2.0) {
			t.Error("Path violates edge constraint 0->1 at t=2.0")
		}
	}
}

// TestMultiGoalAStar verifies that A* visits all goals in sequence.
func TestMultiGoalAStar(t *testing.T) {
	ws := createGrid(5)
	robot := &core.Robot{ID: 0, Type: core.TypeA, Start: 0}

	goals := []core.VertexID{4, 20, 12} // Three separate goals

	path := SpaceTimeAStar(ws, robot, 0, goals, nil, 200.0)

	if path == nil {
		t.Fatal("Expected path, got nil")
	}

	// Verify all goals are visited in order
	goalIndex := 0
	for _, tv := range path {
		if goalIndex < len(goals) && tv.V == goals[goalIndex] {
			goalIndex++
		}
	}

	if goalIndex != len(goals) {
		t.Errorf("Expected to visit all %d goals, but only visited %d", len(goals), goalIndex)
	}
}

// TestSchedulePopulation verifies that schedule is populated with task completion times.
func TestSchedulePopulation(t *testing.T) {
	inst := createTestInstance()
	solver := NewPrioritized(100)

	sol := solver.Solve(inst)
	if sol == nil {
		t.Fatal("Solver returned nil solution")
	}

	// Verify schedule is not empty
	if len(sol.Schedule) == 0 {
		t.Error("Schedule is empty after solving")
	}

	// Verify makespan is non-zero
	if sol.Makespan == 0 {
		t.Error("Makespan is 0, expected > 0")
	}

	// Verify each task has a schedule entry
	for _, task := range inst.Tasks {
		if _, ok := sol.Schedule[task.ID]; !ok {
			t.Errorf("Task %d not found in schedule", task.ID)
		}
	}
}

// TestHoverEnergy verifies that drone hover consumes non-zero energy.
func TestHoverEnergy(t *testing.T) {
	robot := core.NewDrone(1, 0, 0, 100.0) // 100 Wh battery

	// Hover for 10 seconds
	energy := robot.EnergyForTime(10.0, core.ActionHover)

	if energy <= 0 {
		t.Errorf("Expected positive hover energy, got %.4f", energy)
	}

	// Hover power is 50W, 10 seconds = 500Ws = 500/3600 Wh ≈ 0.139 Wh
	expectedEnergy := 50.0 * 10.0 / 3600.0
	if !timeEqual(energy, expectedEnergy) {
		t.Errorf("Expected hover energy %.4f Wh, got %.4f Wh", expectedEnergy, energy)
	}
}

// TestSolversMakespanNonZero verifies that all solvers produce non-zero makespan.
func TestSolversMakespanNonZero(t *testing.T) {
	inst := createTestInstance()

	solvers := []Solver{
		NewPrioritized(100),
		NewCBS(100),
		NewHybridCBS(100),
	}

	for _, solver := range solvers {
		t.Run(solver.Name(), func(t *testing.T) {
			sol := solver.Solve(inst)
			if sol == nil {
				t.Skip("Solver returned nil")
			}

			if sol.Makespan == 0 {
				t.Error("Makespan is 0, expected > 0")
			}

			// Check schedule is populated
			if len(sol.Schedule) == 0 {
				t.Error("Schedule is empty")
			}
		})
	}
}

// TestMultiTaskRobotVisitsAllGoals verifies a robot assigned multiple tasks visits all of them.
func TestMultiTaskRobotVisitsAllGoals(t *testing.T) {
	inst := core.NewInstance()
	inst.Workspace = createGrid(5)
	inst.Deadline = 200

	// Single robot with multiple tasks
	inst.Robots = []*core.Robot{
		{ID: 0, Type: core.TypeA, Start: 0},
	}

	// Multiple tasks assigned to the same robot
	inst.Tasks = []*core.Task{
		core.NewTask(0, core.SwapModule, 4),  // vertex 4
		core.NewTask(1, core.SwapModule, 20), // vertex 20
		core.NewTask(2, core.SwapModule, 12), // vertex 12
	}

	solver := NewCBS(200)
	sol := solver.Solve(inst)

	if sol == nil {
		t.Fatal("Solver returned nil")
	}

	path := sol.Paths[0]
	if path == nil {
		t.Fatal("No path for robot 0")
	}

	// Check all task locations are visited
	taskLocations := map[core.VertexID]bool{4: false, 20: false, 12: false}
	for _, tv := range path {
		if _, exists := taskLocations[tv.V]; exists {
			taskLocations[tv.V] = true
		}
	}

	for loc, visited := range taskLocations {
		if !visited {
			t.Errorf("Task location %d not visited", loc)
		}
	}
}

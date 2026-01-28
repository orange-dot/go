package algo

import (
	"testing"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// createGrid creates a simple n x n grid workspace.
func createGrid(n int) *core.Workspace {
	ws := core.NewWorkspace()

	// Create vertices
	for y := 0; y < n; y++ {
		for x := 0; x < n; x++ {
			id := core.VertexID(y*n + x)
			ws.AddVertex(&core.Vertex{
				ID:  id,
				Pos: core.Pos{X: float64(x), Y: float64(y)},
			})
		}
	}

	// Create edges (4-connected grid)
	for y := 0; y < n; y++ {
		for x := 0; x < n; x++ {
			id := core.VertexID(y*n + x)
			if x < n-1 {
				ws.AddEdge(id, id+1, 1.0)
			}
			if y < n-1 {
				ws.AddEdge(id, id+core.VertexID(n), 1.0)
			}
		}
	}

	return ws
}

// createTestInstance creates a basic test instance.
func createTestInstance() *core.Instance {
	inst := core.NewInstance()
	inst.Workspace = createGrid(5)
	inst.Deadline = 100

	inst.Robots = []*core.Robot{
		{ID: 0, Type: core.TypeA, Start: 0},
		{ID: 1, Type: core.TypeB, Start: 24},
	}

	inst.Tasks = []*core.Task{
		core.NewTask(0, core.SwapModule, 12),
		core.NewTask(1, core.Diagnose, 4),
	}

	return inst
}

func TestFindFirstConflict_NoConflict(t *testing.T) {
	paths := map[core.RobotID]core.Path{
		0: {{V: 0, T: 0}, {V: 1, T: 1}, {V: 2, T: 2}},
		1: {{V: 10, T: 0}, {V: 11, T: 1}, {V: 12, T: 2}},
	}

	conflict := FindFirstConflict(paths)
	if conflict != nil {
		t.Errorf("Expected no conflict, got: Robot1=%d, Robot2=%d, V=%d, T=%.1f",
			conflict.Robot1, conflict.Robot2, conflict.Vertex, conflict.Time)
	}
}

func TestFindFirstConflict_VertexConflict(t *testing.T) {
	paths := map[core.RobotID]core.Path{
		0: {{V: 0, T: 0}, {V: 1, T: 1}, {V: 2, T: 2}},
		1: {{V: 5, T: 0}, {V: 2, T: 1}, {V: 3, T: 2}}, // Conflict at V=2, T=2? No, V=2 at T=1 vs T=2
	}

	// Robot 0 is at V=2 at T=2, Robot 1 is at V=3 at T=2
	// Let's create an actual conflict
	paths = map[core.RobotID]core.Path{
		0: {{V: 0, T: 0}, {V: 1, T: 1}, {V: 2, T: 2}},
		1: {{V: 5, T: 0}, {V: 1, T: 1}, {V: 6, T: 2}}, // Both at V=1 at T=1
	}

	conflict := FindFirstConflict(paths)
	if conflict == nil {
		t.Error("Expected vertex conflict, got nil")
		return
	}

	if conflict.Vertex != 1 || !timeEqual(conflict.Time, 1) {
		t.Errorf("Expected conflict at V=1, T=1, got V=%d, T=%.1f", conflict.Vertex, conflict.Time)
	}
	if conflict.IsEdge {
		t.Error("Expected vertex conflict, got edge conflict")
	}
}

func TestFindFirstConflict_EdgeConflict(t *testing.T) {
	// Edge conflict: robots swap positions
	paths := map[core.RobotID]core.Path{
		0: {{V: 0, T: 0}, {V: 1, T: 1}}, // 0 -> 1
		1: {{V: 1, T: 0}, {V: 0, T: 1}}, // 1 -> 0
	}

	conflict := FindFirstConflict(paths)
	if conflict == nil {
		t.Error("Expected edge conflict, got nil")
		return
	}

	if !conflict.IsEdge {
		t.Error("Expected edge conflict, got vertex conflict")
	}
}

func TestFindAllConflicts(t *testing.T) {
	// Multiple conflicts
	paths := map[core.RobotID]core.Path{
		0: {{V: 0, T: 0}, {V: 1, T: 1}, {V: 2, T: 2}},
		1: {{V: 5, T: 0}, {V: 1, T: 1}, {V: 2, T: 2}}, // Conflicts at V=1,T=1 and V=2,T=2
	}

	conflicts := FindAllConflicts(paths)
	if len(conflicts) != 2 {
		t.Errorf("Expected 2 conflicts, got %d", len(conflicts))
	}
}

func TestAllSolversReturnSolution(t *testing.T) {
	inst := createTestInstance()

	solvers := []Solver{
		NewPrioritized(100),
		NewCBS(100),
		NewHybridCBS(100),
		NewMixedCBS(100),
		NewDeadlineCBS(100),
		NewStochasticECBS(100, 0.05),
		// Skip MCTS due to time budget
	}

	for _, solver := range solvers {
		t.Run(solver.Name(), func(t *testing.T) {
			sol := solver.Solve(inst)
			if sol == nil {
				t.Error("Solver returned nil solution")
				return
			}

			if !sol.Feasible {
				t.Error("Solution marked as not feasible")
			}

			// Verify paths exist for each robot
			for _, robot := range inst.Robots {
				if _, ok := sol.Paths[robot.ID]; !ok {
					t.Errorf("Missing path for robot %d", robot.ID)
				}
			}

			// Verify no conflicts in solution
			conflict := FindFirstConflict(sol.Paths)
			if conflict != nil {
				t.Errorf("Solution has conflict: Robot1=%d, Robot2=%d, V=%d, T=%.1f",
					conflict.Robot1, conflict.Robot2, conflict.Vertex, conflict.Time)
			}
		})
	}
}

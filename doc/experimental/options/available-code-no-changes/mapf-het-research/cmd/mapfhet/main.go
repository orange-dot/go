// Command mapfhet runs MAPF-HET algorithm experiments.
package main

import (
	"fmt"
	"time"

	"github.com/elektrokombinacija/mapf-het-research/internal/algo"
	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

func main() {
	fmt.Println("=== MAPF-HET Research: 3D Drone Support ===")

	// Run standard test
	fmt.Println("--- Standard 2D Test (TypeA + TypeB) ---")
	inst := createTestInstance()
	fmt.Printf("Instance: %d robots, %d tasks, deadline %.0fs\n",
		len(inst.Robots), len(inst.Tasks), inst.Deadline)
	runSolvers(inst)

	// Run 3D drone test
	fmt.Println("\n--- 3D Drone Test (TypeA + TypeB + TypeC) ---")
	inst3D := create3DTestInstance()
	fmt.Printf("Instance: %d robots (%d drones), %d tasks, deadline %.0fs\n",
		len(inst3D.Robots), countDrones(inst3D.Robots), len(inst3D.Tasks), inst3D.Deadline)
	runSolvers(inst3D)
}

func runSolvers(inst *core.Instance) {
	solvers := []algo.Solver{
		algo.NewPrioritized(100),
		algo.NewCBS(100),
		algo.NewHybridCBS(100),
		algo.NewMixedCBS(100),
		algo.NewDeadlineCBS(100),
		algo.NewStochasticECBS(100, 0.05),
		algo.NewMCTS(2 * time.Second),
		algo.NewEnergyCBS(100), // New energy-aware solver
	}

	for _, solver := range solvers {
		fmt.Printf("\n  %s: ", solver.Name())
		start := time.Now()
		sol := solver.Solve(inst)
		elapsed := time.Since(start)

		if sol == nil {
			fmt.Println("No solution")
			continue
		}
		fmt.Printf("Feasible=%v, Makespan=%.2fs, Deadline=%v, Time=%v",
			sol.Feasible, sol.Makespan, sol.MeetDeadline(inst.Deadline), elapsed)
	}
	fmt.Println()
}

func countDrones(robots []*core.Robot) int {
	count := 0
	for _, r := range robots {
		if r.Type == core.TypeC {
			count++
		}
	}
	return count
}

// createTestInstance builds a simple 2-robot, 3-task scenario.
func createTestInstance() *core.Instance {
	inst := core.NewInstance()
	inst.Deadline = 300 // 5 minutes

	// Create grid workspace 5x5
	for y := 0; y < 5; y++ {
		for x := 0; x < 5; x++ {
			id := core.VertexID(y*5 + x)
			inst.Workspace.AddVertex(&core.Vertex{
				ID:  id,
				Pos: core.Pos{X: float64(x), Y: float64(y)},
			})
		}
	}

	// Add grid edges
	for y := 0; y < 5; y++ {
		for x := 0; x < 5; x++ {
			id := core.VertexID(y*5 + x)
			if x < 4 {
				inst.Workspace.AddEdge(id, id+1, 1.0)
			}
			if y < 4 {
				inst.Workspace.AddEdge(id, id+5, 1.0)
			}
		}
	}

	// Add robots
	inst.Robots = []*core.Robot{
		{ID: 0, Type: core.TypeA, Start: 0},  // Mobile at (0,0)
		{ID: 1, Type: core.TypeB, Start: 24}, // Rail at (4,4)
	}

	// Add tasks
	inst.Tasks = []*core.Task{
		core.NewTask(0, core.SwapModule, 12),  // Center
		core.NewTask(1, core.Diagnose, 4),     // (4,0)
		core.NewTask(2, core.SwapBattery, 20), // (0,4) - TypeB only
	}

	return inst
}

// create3DTestInstance builds a 3D scenario with drones.
// Layout: 5x5 ground grid + 3 airspace layers + 2 vertical corridors.
func create3DTestInstance() *core.Instance {
	inst := core.NewInstance()
	inst.Deadline = 300

	// Vertex ID allocation:
	// 0-24: Ground layer (Z=0)
	// 25-49: Layer 1 (Z=5m)
	// 50-74: Layer 2 (Z=10m)
	// 75-99: Layer 3 (Z=15m)

	// Create ground layer (5x5 grid)
	for y := 0; y < 5; y++ {
		for x := 0; x < 5; x++ {
			id := core.VertexID(y*5 + x)
			isPad := (x == 0 && y == 0) || (x == 4 && y == 4) // Charging pads at corners
			isCorridor := isPad                               // Corridors above pads
			inst.Workspace.AddVertex(&core.Vertex{
				ID:         id,
				Pos:        core.Pos{X: float64(x), Y: float64(y), Z: 0},
				Layer:      core.LayerGround,
				IsPad:      isPad,
				IsCorridor: isCorridor,
			})
		}
	}

	// Create airspace layers (simplified: only corridor vertices + some waypoints)
	for layer := 1; layer <= 3; layer++ {
		z := float64(layer * 5)
		layerType := core.AirspaceLayer(layer * 5)
		baseID := core.VertexID(layer * 25)

		for y := 0; y < 5; y++ {
			for x := 0; x < 5; x++ {
				id := baseID + core.VertexID(y*5+x)
				isCorridor := (x == 0 && y == 0) || (x == 4 && y == 4)
				inst.Workspace.AddVertex(&core.Vertex{
					ID:         id,
					Pos:        core.Pos{X: float64(x), Y: float64(y), Z: z},
					Layer:      layerType,
					IsCorridor: isCorridor,
					Restrict:   []core.RobotType{core.TypeC}, // Airspace for drones only
				})
			}
		}
	}

	// Add ground edges (same as 2D)
	for y := 0; y < 5; y++ {
		for x := 0; x < 5; x++ {
			id := core.VertexID(y*5 + x)
			if x < 4 {
				inst.Workspace.AddEdge(id, id+1, 1.0)
			}
			if y < 4 {
				inst.Workspace.AddEdge(id, id+5, 1.0)
			}
		}
	}

	// Add airspace edges (horizontal movement within each layer)
	for layer := 1; layer <= 3; layer++ {
		baseID := core.VertexID(layer * 25)
		for y := 0; y < 5; y++ {
			for x := 0; x < 5; x++ {
				id := baseID + core.VertexID(y*5+x)
				if x < 4 {
					inst.Workspace.AddEdge(id, id+1, 0.5) // Faster in air
				}
				if y < 4 {
					inst.Workspace.AddEdge(id, id+5, 0.5)
				}
			}
		}
	}

	// Add vertical corridor edges (between layers)
	// Corridor 1: at (0,0)
	inst.Workspace.AddEdge(0, 25, 2.0)  // Ground to Layer1
	inst.Workspace.AddEdge(25, 50, 2.0) // Layer1 to Layer2
	inst.Workspace.AddEdge(50, 75, 2.0) // Layer2 to Layer3

	// Corridor 2: at (4,4)
	inst.Workspace.AddEdge(24, 49, 2.0)  // Ground to Layer1
	inst.Workspace.AddEdge(49, 74, 2.0)  // Layer1 to Layer2
	inst.Workspace.AddEdge(74, 99, 2.0)  // Layer2 to Layer3

	// Add robots
	inst.Robots = []*core.Robot{
		{ID: 0, Type: core.TypeA, Start: 0},  // Mobile at (0,0) ground
		{ID: 1, Type: core.TypeB, Start: 24}, // Rail at (4,4) ground
		core.NewDrone(2, 75, 0, 100.0),       // Drone at (0,0) Layer3, home at pad 0
		core.NewDrone(3, 99, 24, 100.0),      // Drone at (4,4) Layer3, home at pad 24
	}

	// Add tasks (mix of ground and aerial)
	inst.Tasks = []*core.Task{
		core.NewTask(0, core.SwapModule, 12),     // Ground: center
		core.NewTask(1, core.Diagnose, 4),        // Ground: (4,0)
		core.NewTask(2, core.SwapBattery, 20),    // Ground: (0,4) - TypeB only
		core.NewTask(3, core.AerialInspect, 62),  // Air: Layer2 at (2,2)
		core.NewTask(4, core.AerialDelivery, 50), // Air: Layer2 at (0,0)
		core.NewTask(5, core.AerialSurvey, 87),   // Air: Layer3 at (2,2)
	}

	return inst
}

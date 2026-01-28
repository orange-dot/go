// Package algo implements MAPF-HET solving algorithms.
package algo

import (
	"math"
	"sort"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// Solver is the interface for MAPF-HET algorithms.
type Solver interface {
	// Solve attempts to find a solution for the instance.
	// Returns nil solution if no solution found.
	Solve(inst *core.Instance) *core.Solution

	// Name returns the algorithm name.
	Name() string
}

// Conflict represents a collision between two robots.
type Conflict struct {
	Robot1, Robot2 core.RobotID
	Vertex         core.VertexID
	Time           float64 // Start time of conflict
	EndTime        float64 // End time of conflict (when movement completes)
	IsEdge         bool    // Edge conflict vs vertex conflict
	// For edge conflicts: the two vertices being swapped
	EdgeFrom, EdgeTo core.VertexID
}

// Constraint prohibits a robot from being at a vertex at a time.
type Constraint struct {
	Robot  core.RobotID
	Vertex core.VertexID
	Time   float64   // Start time of constraint
	EndTime float64  // End time of constraint (for edge constraints with duration)
	// For edge constraints
	IsEdge   bool
	EdgeFrom core.VertexID
	EdgeTo   core.VertexID
}

// TimeTolerance for floating-point time comparison.
const TimeTolerance = 0.001

// timeEqual compares times with tolerance.
func timeEqual(t1, t2 float64) bool {
	return math.Abs(t1-t2) < TimeTolerance
}

type pathSegment struct {
	fromV  core.VertexID
	toV    core.VertexID
	startT float64
	endT   float64
}

func buildSegments(path core.Path) []pathSegment {
	if len(path) < 2 {
		return nil
	}

	segs := make([]pathSegment, 0, len(path)-1)
	for i := 0; i < len(path)-1; i++ {
		seg := pathSegment{
			fromV:  path[i].V,
			toV:    path[i+1].V,
			startT: path[i].T,
			endT:   path[i+1].T,
		}
		if seg.endT < seg.startT {
			seg.startT, seg.endT = seg.endT, seg.startT
			seg.fromV, seg.toV = seg.toV, seg.fromV
		}
		segs = append(segs, seg)
	}

	return segs
}

// sortedRobotIDs returns sorted robot IDs from paths map.
func sortedRobotIDs(paths map[core.RobotID]core.Path) []core.RobotID {
	robots := make([]core.RobotID, 0, len(paths))
	for rid := range paths {
		robots = append(robots, rid)
	}
	sort.Slice(robots, func(i, j int) bool {
		return robots[i] < robots[j]
	})
	return robots
}

// GoalWithTaskInfo pairs a goal vertex with task metadata for planning.
type GoalWithTaskInfo struct {
	TaskID   core.TaskID
	Vertex   core.VertexID
	Duration float64 // Task service time in seconds
}

// CollectGoalsDeterministic returns goals sorted by TaskID for deterministic planning.
// This ensures reproducible path planning regardless of map iteration order.
func CollectGoalsDeterministic(assignment core.Assignment, robotID core.RobotID, inst *core.Instance) []core.VertexID {
	goalsWithInfo := CollectGoalsWithInfo(assignment, robotID, inst)
	goals := make([]core.VertexID, len(goalsWithInfo))
	for i, g := range goalsWithInfo {
		goals[i] = g.Vertex
	}
	return goals
}

// CollectGoalsWithInfo returns goals with task metadata, sorted by TaskID.
// Use this when you need task durations for wait segments.
func CollectGoalsWithInfo(assignment core.Assignment, robotID core.RobotID, inst *core.Instance) []GoalWithTaskInfo {
	var goalsWithInfo []GoalWithTaskInfo

	for tid, rid := range assignment {
		if rid == robotID {
			task := inst.TaskByID(tid)
			if task != nil {
				duration := task.Duration
				if duration <= 0 {
					// Use nominal duration if not set
					duration, _ = core.NominalDuration(task.Type)
				}
				goalsWithInfo = append(goalsWithInfo, GoalWithTaskInfo{
					TaskID:   tid,
					Vertex:   task.Location,
					Duration: duration,
				})
			}
		}
	}

	// Sort by TaskID for deterministic order
	sort.Slice(goalsWithInfo, func(i, j int) bool {
		return goalsWithInfo[i].TaskID < goalsWithInfo[j].TaskID
	})

	return goalsWithInfo
}

// getPositionAtTime returns robot position at given time via interpolation.
func getPositionAtTime(path core.Path, t float64) (core.VertexID, bool) {
	if len(path) == 0 {
		return 0, false
	}

	// Before path start
	if t < path[0].T-TimeTolerance {
		return path[0].V, true
	}

	// After path end - stay at last position
	if t > path[len(path)-1].T+TimeTolerance {
		return path[len(path)-1].V, true
	}

	// Find position at time t
	for i := 0; i < len(path); i++ {
		if timeEqual(path[i].T, t) {
			return path[i].V, true
		}
		if path[i].T > t && i > 0 {
			// Between path[i-1] and path[i] - return previous vertex
			return path[i-1].V, true
		}
	}

	return path[len(path)-1].V, true
}

// FindFirstConflict detects the first conflict in paths.
func FindFirstConflict(paths map[core.RobotID]core.Path) *Conflict {
	robots := sortedRobotIDs(paths)

	// Collect all unique time points
	timeSet := make(map[float64]bool)
	for _, path := range paths {
		for _, tv := range path {
			timeSet[tv.T] = true
		}
	}

	times := make([]float64, 0, len(timeSet))
	for t := range timeSet {
		times = append(times, t)
	}
	sort.Float64s(times)

	// Check for vertex conflicts at each time point
	var bestVertex *Conflict
vertexLoop:
	for _, t := range times {
		for i := 0; i < len(robots); i++ {
			for j := i + 1; j < len(robots); j++ {
				path1, path2 := paths[robots[i]], paths[robots[j]]

				pos1, ok1 := getPositionAtTime(path1, t)
				pos2, ok2 := getPositionAtTime(path2, t)

				if ok1 && ok2 && pos1 == pos2 {
					bestVertex = &Conflict{
						Robot1:  robots[i],
						Robot2:  robots[j],
						Vertex:  pos1,
						Time:    t,
						EndTime: t, // Vertex conflict is a point in time
						IsEdge:  false,
					}
					break vertexLoop
				}
			}
		}
	}

	// Check for edge conflicts (swaps) using interval overlap detection.
	// With non-uniform travel times, robots may traverse the same edge in opposite
	// directions at overlapping time intervals even if not aligned on time points.
	var bestEdge *Conflict
	bestTime := math.Inf(1)

	for i := 0; i < len(robots); i++ {
		for j := i + 1; j < len(robots); j++ {
			path1, path2 := paths[robots[i]], paths[robots[j]]
			segs1 := buildSegments(path1)
			segs2 := buildSegments(path2)

			for _, s1 := range segs1 {
				if s1.fromV == s1.toV {
					continue
				}
				for _, s2 := range segs2 {
					if s2.fromV == s2.toV {
						continue
					}

					// Swap conflict: opposite directions on same edge.
					if s1.fromV == s2.toV && s1.toV == s2.fromV {
						overlapStart := math.Max(s1.startT, s2.startT)
						overlapEnd := math.Min(s1.endT, s2.endT)
						if overlapStart < overlapEnd+TimeTolerance && overlapStart < bestTime {
							bestTime = overlapStart
							bestEdge = &Conflict{
								Robot1:   robots[i],
								Robot2:   robots[j],
								Vertex:   s1.fromV,
								Time:     overlapStart,
								EndTime:  overlapEnd,
								IsEdge:   true,
								EdgeFrom: s1.fromV,
								EdgeTo:   s1.toV,
							}
						}
					}
				}
			}
		}
	}

	if bestEdge != nil {
		if bestVertex == nil || bestEdge.Time+TimeTolerance < bestVertex.Time {
			return bestEdge
		}
	}

	if bestVertex != nil {
		return bestVertex
	}

	return nil
}

// PopulateSchedule fills the Schedule map with task completion times.
// Completion time = arrival time + task duration.
// Must be called before ComputeMakespan for accurate results.
func PopulateSchedule(sol *core.Solution, inst *core.Instance) {
	if sol.Schedule == nil {
		sol.Schedule = make(core.Schedule)
	}

	for tid, rid := range sol.Assignment {
		task := inst.TaskByID(tid)
		if task == nil {
			continue
		}
		path := sol.Paths[rid]
		if path == nil {
			continue
		}

		// Get task duration
		duration := task.Duration
		if duration <= 0 {
			duration, _ = core.NominalDuration(task.Type)
		}

		// Find when robot reaches the task location
		// The schedule records completion time (arrival + duration)
		for _, tv := range path {
			if tv.V == task.Location {
				completionTime := tv.T + duration
				sol.Schedule[tid] = completionTime
				break
			}
		}
	}
}

// FindAllConflicts detects all conflicts in paths.
func FindAllConflicts(paths map[core.RobotID]core.Path) []*Conflict {
	var conflicts []*Conflict
	robots := sortedRobotIDs(paths)

	// Collect all unique time points
	timeSet := make(map[float64]bool)
	for _, path := range paths {
		for _, tv := range path {
			timeSet[tv.T] = true
		}
	}

	times := make([]float64, 0, len(timeSet))
	for t := range timeSet {
		times = append(times, t)
	}
	sort.Float64s(times)

	// Check for vertex conflicts
	for _, t := range times {
		for i := 0; i < len(robots); i++ {
			for j := i + 1; j < len(robots); j++ {
				path1, path2 := paths[robots[i]], paths[robots[j]]

				pos1, ok1 := getPositionAtTime(path1, t)
				pos2, ok2 := getPositionAtTime(path2, t)

				if ok1 && ok2 && pos1 == pos2 {
					conflicts = append(conflicts, &Conflict{
						Robot1:  robots[i],
						Robot2:  robots[j],
						Vertex:  pos1,
						Time:    t,
						EndTime: t,
						IsEdge:  false,
					})
				}
			}
		}
	}

	// Check for edge conflicts using interval overlap detection
	for i := 0; i < len(robots); i++ {
		for j := i + 1; j < len(robots); j++ {
			path1, path2 := paths[robots[i]], paths[robots[j]]
			segs1 := buildSegments(path1)
			segs2 := buildSegments(path2)

			for _, s1 := range segs1 {
				if s1.fromV == s1.toV {
					continue
				}
				for _, s2 := range segs2 {
					if s2.fromV == s2.toV {
						continue
					}
					if s1.fromV == s2.toV && s1.toV == s2.fromV {
						overlapStart := math.Max(s1.startT, s2.startT)
						overlapEnd := math.Min(s1.endT, s2.endT)
						if overlapStart < overlapEnd+TimeTolerance {
							conflicts = append(conflicts, &Conflict{
								Robot1:   robots[i],
								Robot2:   robots[j],
								Vertex:   s1.fromV,
								Time:     overlapStart,
								EndTime:  overlapEnd,
								IsEdge:   true,
								EdgeFrom: s1.fromV,
								EdgeTo:   s1.toV,
							})
						}
					}
				}
			}
		}
	}

	return conflicts
}

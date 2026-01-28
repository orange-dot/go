package algo

import (
	"sort"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// Prioritized implements prioritized planning for MAPF-HET.
type Prioritized struct {
	MaxTime float64
}

// NewPrioritized creates a prioritized planning solver.
func NewPrioritized(maxTime float64) *Prioritized {
	return &Prioritized{MaxTime: maxTime}
}

func (p *Prioritized) Name() string { return "Prioritized-HET" }

// Solve implements prioritized planning.
func (p *Prioritized) Solve(inst *core.Instance) *core.Solution {
	// Step 1: Compute assignment
	assignment := p.computeAssignment(inst)
	if assignment == nil {
		return nil
	}

	// Step 2: Compute priority order
	priority := p.computePriority(inst, assignment)

	// Step 3: Plan paths in priority order
	solution := core.NewSolution()
	solution.Assignment = assignment
	solution.Paths = make(map[core.RobotID]core.Path)
	solution.Schedule = make(core.Schedule)

	var allConstraints []Constraint

	for _, robot := range priority {
		// Collect goals with task info (includes duration) sorted by TaskID
		goalsWithInfo := CollectGoalsWithInfo(assignment, robot.ID, inst)

		// Plan path with task durations (adds wait segments for service time)
		path := SpaceTimeAStarWithDurations(
			inst.Workspace,
			robot,
			robot.Start,
			goalsWithInfo,
			allConstraints,
			p.MaxTime,
		)

		if path == nil && len(goalsWithInfo) > 0 {
			return nil // Failed to find path
		}

		solution.Paths[robot.ID] = path

		// Add this robot's path as constraints for lower-priority robots
		for _, tv := range path {
			// Other robots cannot occupy this vertex at this time
			for _, other := range inst.Robots {
				if other.ID != robot.ID {
					allConstraints = append(allConstraints, Constraint{
						Robot:  other.ID,
						Vertex: tv.V,
						Time:   tv.T,
					})
				}
			}
		}
	}

	PopulateSchedule(solution, inst)
	solution.ComputeMakespan(inst)
	solution.Feasible = true
	return solution
}

// computeAssignment uses greedy assignment.
func (p *Prioritized) computeAssignment(inst *core.Instance) core.Assignment {
	assignment := make(core.Assignment)
	robotWorkload := make(map[core.RobotID]int)

	// Sort tasks by type (battery swaps first - more constrained)
	tasks := make([]*core.Task, len(inst.Tasks))
	copy(tasks, inst.Tasks)
	sort.Slice(tasks, func(i, j int) bool {
		return tasks[i].Type < tasks[j].Type
	})

	for _, task := range tasks {
		// Find capable robot with least workload
		var bestRobot *core.Robot
		bestLoad := int(^uint(0) >> 1) // Max int

		for _, robot := range inst.Robots {
			if core.CanPerform(robot.Type, task.Type) {
				load := robotWorkload[robot.ID]
				if load < bestLoad {
					bestLoad = load
					bestRobot = robot
				}
			}
		}

		if bestRobot == nil {
			return nil
		}

		assignment[task.ID] = bestRobot.ID
		robotWorkload[bestRobot.ID]++
	}

	return assignment
}

// computePriority orders robots for planning.
func (p *Prioritized) computePriority(inst *core.Instance, assignment core.Assignment) []*core.Robot {
	type robotScore struct {
		robot *core.Robot
		score int
	}

	scores := make([]robotScore, len(inst.Robots))
	for i, robot := range inst.Robots {
		score := 0

		// Type B (rail) gets higher priority - larger footprint
		if robot.Type == core.TypeB {
			score += 100
		}

		// More tasks = higher priority
		for _, rid := range assignment {
			if rid == robot.ID {
				score += 10
			}
		}

		scores[i] = robotScore{robot: robot, score: score}
	}

	// Sort descending by score
	sort.Slice(scores, func(i, j int) bool {
		return scores[i].score > scores[j].score
	})

	result := make([]*core.Robot, len(scores))
	for i, rs := range scores {
		result[i] = rs.robot
	}
	return result
}

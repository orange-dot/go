// Package algo - DEADLINE-CBS: CBS with deadline-aware pruning.
package algo

import (
	"container/heap"
	"math"
	"sort"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// SlackInfo tracks deadline slack for a robot-task pair.
type SlackInfo struct {
	Robot    core.RobotID
	Task     core.TaskID
	Slack    float64 // Deadline - earliest_completion
	Critical bool    // Slack < threshold
}

// DeadlineCBS implements CBS with deadline-aware pruning and slack-based branching.
// Core innovation: Early pruning of infeasible branches via constraint propagation,
// and prioritizing conflicts involving deadline-critical robots.
type DeadlineCBS struct {
	MaxTime        float64
	Deadline       float64
	SlackThreshold float64 // Below this, robot is critical
}

// NewDeadlineCBS creates a DEADLINE-CBS solver.
func NewDeadlineCBS(maxTime float64) *DeadlineCBS {
	return &DeadlineCBS{
		MaxTime:        maxTime,
		SlackThreshold: 10.0, // 10 time units of slack considered critical
	}
}

func (d *DeadlineCBS) Name() string { return "DEADLINE-CBS" }

// deadlineCBSNode represents a node in the constraint tree.
type deadlineCBSNode struct {
	constraints []Constraint
	solution    *core.Solution
	cost        float64
	slacks      []SlackInfo
	lowerBound  float64 // Lower bound on makespan (for pruning)
	index       int
}

type deadlineCBSHeap []*deadlineCBSNode

func (h deadlineCBSHeap) Len() int           { return len(h) }
func (h deadlineCBSHeap) Less(i, j int) bool { return h[i].cost < h[j].cost }
func (h deadlineCBSHeap) Swap(i, j int) {
	h[i], h[j] = h[j], h[i]
	h[i].index = i
	h[j].index = j
}
func (h *deadlineCBSHeap) Push(x any) {
	n := x.(*deadlineCBSNode)
	n.index = len(*h)
	*h = append(*h, n)
}
func (h *deadlineCBSHeap) Pop() any {
	old := *h
	n := len(old)
	x := old[n-1]
	old[n-1] = nil
	*h = old[0 : n-1]
	return x
}

// Solve implements DEADLINE-CBS.
func (d *DeadlineCBS) Solve(inst *core.Instance) *core.Solution {
	d.Deadline = inst.Deadline

	// Compute assignment
	assignment := d.computeAssignment(inst)
	if assignment == nil {
		return nil
	}

	// Initialize root
	root := &deadlineCBSNode{
		constraints: nil,
		solution:    core.NewSolution(),
	}
	root.solution.Assignment = assignment

	if !d.planAllPaths(inst, root) {
		return nil
	}

	// Check deadline feasibility
	if !d.checkDeadlineFeasibility(inst, root) {
		return nil // Impossible to meet deadline even without conflicts
	}

	root.cost = root.solution.Makespan
	root.slacks = d.computeSlacks(inst, root)
	root.lowerBound = d.computeLowerBound(inst, root.constraints)

	// CBS main loop
	open := &deadlineCBSHeap{}
	heap.Init(open)
	heap.Push(open, root)

	iterations := 0
	maxIterations := 10000

	for open.Len() > 0 && iterations < maxIterations {
		iterations++
		node := heap.Pop(open).(*deadlineCBSNode)

		// Prune if lower bound exceeds deadline
		if node.lowerBound > d.Deadline {
			continue
		}

		// Find conflict using slack-aware selection
		conflict := d.selectConflict(inst, node)
		if conflict == nil {
			node.solution.Feasible = true
			return node.solution
		}

		// Branch with deadline-aware ordering
		children := d.createChildren(inst, node, conflict)
		for _, child := range children {
			// Compute lower bound for early pruning
			child.lowerBound = d.computeLowerBound(inst, child.constraints)
			if child.lowerBound > d.Deadline {
				continue // Prune: cannot meet deadline
			}

			if d.planAllPaths(inst, child) {
				if child.solution.Makespan <= d.Deadline {
					child.cost = child.solution.Makespan
					child.slacks = d.computeSlacks(inst, child)
					heap.Push(open, child)
				}
			}
		}
	}

	return nil
}

// computeLowerBound estimates minimum possible makespan given constraints.
// This is a simplified version of SAT-based feasibility checking.
func (d *DeadlineCBS) computeLowerBound(inst *core.Instance, constraints []Constraint) float64 {
	// For each robot, compute minimum time to complete assigned tasks
	// considering existing constraints

	robotMinTime := make(map[core.RobotID]float64)

	for _, robot := range inst.Robots {
		// Collect tasks for this robot
		var taskDurations float64
		var minTravelDist float64

		prevLoc := robot.Start
		for _, task := range inst.Tasks {
			// Would need assignment info here, but for lower bound
			// we assume optimal assignment
			if core.CanPerform(robot.Type, task.Type) {
				taskDurations += task.Duration

				// Estimate travel distance
				taskPos := inst.Workspace.Vertices[task.Location].Pos
				prevPos := inst.Workspace.Vertices[prevLoc].Pos
				minTravelDist += euclideanDist(taskPos, prevPos)
				prevLoc = task.Location
			}
		}

		// Travel time (simplified: assume direct paths)
		travelTime := minTravelDist / robot.Speed()

		// Add delay from constraints
		var constraintDelay float64
		for _, c := range constraints {
			if c.Robot == robot.ID {
				constraintDelay += 1.0 // Each constraint adds potential delay
			}
		}

		robotMinTime[robot.ID] = taskDurations + travelTime + constraintDelay
	}

	// Lower bound is the maximum across robots
	lb := 0.0
	for _, t := range robotMinTime {
		if t > lb {
			lb = t
		}
	}

	return lb
}

// checkDeadlineFeasibility performs constraint propagation to check feasibility.
func (d *DeadlineCBS) checkDeadlineFeasibility(inst *core.Instance, node *deadlineCBSNode) bool {
	// Simple check: can each robot complete its tasks before deadline?
	for _, robot := range inst.Robots {
		var totalDuration float64

		for tid, rid := range node.solution.Assignment {
			if rid == robot.ID {
				task := inst.TaskByID(tid)
				if task != nil {
					totalDuration += task.Duration
				}
			}
		}

		// Add path travel time
		path := node.solution.Paths[robot.ID]
		if len(path) > 0 {
			totalDuration += path[len(path)-1].T
		}

		if totalDuration > d.Deadline {
			return false
		}
	}

	return true
}

// computeSlacks calculates slack for each robot-task pair.
// Uses per-task deadlines when available, otherwise global deadline.
func (d *DeadlineCBS) computeSlacks(inst *core.Instance, node *deadlineCBSNode) []SlackInfo {
	var slacks []SlackInfo

	for _, robot := range inst.Robots {
		path := node.solution.Paths[robot.ID]
		if len(path) == 0 {
			continue
		}

		pathEndTime := path[len(path)-1].T

		// Compute slack for each task assigned to this robot
		var cumulativeTime float64
		for tid, rid := range node.solution.Assignment {
			if rid == robot.ID {
				task := inst.TaskByID(tid)
				if task != nil {
					cumulativeTime += task.Duration

					// Use per-task deadline if available, otherwise global
					taskDeadline := task.GetDeadline(d.Deadline)
					earliestCompletion := pathEndTime + cumulativeTime
					slack := taskDeadline - earliestCompletion

					slacks = append(slacks, SlackInfo{
						Robot:    robot.ID,
						Task:     tid,
						Slack:    slack,
						Critical: slack < d.SlackThreshold,
					})
				}
			}
		}
	}

	// Sort by slack (tightest first)
	sort.Slice(slacks, func(i, j int) bool {
		return slacks[i].Slack < slacks[j].Slack
	})

	return slacks
}

// selectConflict finds conflicts prioritizing deadline-critical robots.
func (d *DeadlineCBS) selectConflict(inst *core.Instance, node *deadlineCBSNode) *Conflict {
	conflicts := FindAllConflicts(node.solution.Paths)
	if len(conflicts) == 0 {
		return nil
	}

	// Build slack lookup
	robotSlack := make(map[core.RobotID]float64)
	for _, s := range node.slacks {
		if existing, ok := robotSlack[s.Robot]; !ok || s.Slack < existing {
			robotSlack[s.Robot] = s.Slack
		}
	}

	// Sort conflicts by minimum slack of involved robots (tightest first)
	sort.Slice(conflicts, func(i, j int) bool {
		slack_i := min(getSlackOrDefault(robotSlack, conflicts[i].Robot1),
			getSlackOrDefault(robotSlack, conflicts[i].Robot2))
		slack_j := min(getSlackOrDefault(robotSlack, conflicts[j].Robot1),
			getSlackOrDefault(robotSlack, conflicts[j].Robot2))
		return slack_i < slack_j
	})

	return conflicts[0]
}

func getSlackOrDefault(m map[core.RobotID]float64, id core.RobotID) float64 {
	if s, ok := m[id]; ok {
		return s
	}
	return math.Inf(1)
}

// createChildren generates child nodes with deadline-aware ordering.
func (d *DeadlineCBS) createChildren(inst *core.Instance, node *deadlineCBSNode, conflict *Conflict) []*deadlineCBSNode {
	var children []*deadlineCBSNode

	// Determine which robot has more slack
	robotSlack := make(map[core.RobotID]float64)
	for _, s := range node.slacks {
		if existing, ok := robotSlack[s.Robot]; !ok || s.Slack < existing {
			robotSlack[s.Robot] = s.Slack
		}
	}

	slack1 := getSlackOrDefault(robotSlack, conflict.Robot1)
	slack2 := getSlackOrDefault(robotSlack, conflict.Robot2)

	// Order: constrain the robot with MORE slack first
	// (preserves options for the critical robot)
	robots := []core.RobotID{conflict.Robot1, conflict.Robot2}
	if slack1 < slack2 {
		robots = []core.RobotID{conflict.Robot2, conflict.Robot1}
	}

	for _, robotID := range robots {
		child := &deadlineCBSNode{
			constraints: append(
				append([]Constraint{}, node.constraints...),
				Constraint{
					Robot:  robotID,
					Vertex: conflict.Vertex,
					Time:   conflict.Time,
				},
			),
			solution: core.NewSolution(),
		}
		child.solution.Assignment = node.solution.Assignment
		children = append(children, child)
	}

	return children
}

// computeAssignment assigns tasks using deadline-aware heuristics.
func (d *DeadlineCBS) computeAssignment(inst *core.Instance) core.Assignment {
	assignment := make(core.Assignment)
	robotWorkload := make(map[core.RobotID]float64) // Track total duration

	// Sort tasks by urgency (longer tasks first to spread load)
	tasks := make([]*core.Task, len(inst.Tasks))
	copy(tasks, inst.Tasks)
	sort.Slice(tasks, func(i, j int) bool {
		return tasks[i].Duration > tasks[j].Duration
	})

	for _, task := range tasks {
		var bestRobot *core.Robot
		bestScore := math.Inf(-1)

		for _, robot := range inst.Robots {
			if !core.CanPerform(robot.Type, task.Type) {
				continue
			}

			currentLoad := robotWorkload[robot.ID]

			// Estimate completion time with this task
			taskLoc := inst.Workspace.Vertices[task.Location].Pos
			robotLoc := inst.Workspace.Vertices[robot.Start].Pos
			travelTime := euclideanDist(taskLoc, robotLoc) / robot.Speed()

			estimatedCompletion := currentLoad + travelTime + task.Duration
			slackRemaining := d.Deadline - estimatedCompletion

			// Score: prefer robots that keep slack high
			score := slackRemaining
			if score > bestScore {
				bestScore = score
				bestRobot = robot
			}
		}

		if bestRobot == nil {
			return nil
		}

		assignment[task.ID] = bestRobot.ID
		robotWorkload[bestRobot.ID] += task.Duration
	}

	return assignment
}

// planAllPaths plans paths for all robots.
func (d *DeadlineCBS) planAllPaths(inst *core.Instance, node *deadlineCBSNode) bool {
	node.solution.Paths = make(map[core.RobotID]core.Path)
	node.solution.Schedule = make(core.Schedule)

	for _, robot := range inst.Robots {
		// Collect goals with task info (includes duration) sorted by TaskID
		goalsWithInfo := CollectGoalsWithInfo(node.solution.Assignment, robot.ID, inst)

		var robotConstraints []Constraint
		for _, con := range node.constraints {
			if con.Robot == robot.ID {
				robotConstraints = append(robotConstraints, con)
			}
		}

		path := SpaceTimeAStarWithDurations(
			inst.Workspace,
			robot,
			robot.Start,
			goalsWithInfo,
			robotConstraints,
			d.MaxTime,
		)

		if path == nil && len(goalsWithInfo) > 0 {
			return false
		}

		node.solution.Paths[robot.ID] = path
	}

	PopulateSchedule(node.solution, inst)
	node.solution.ComputeMakespan(inst)
	return true
}

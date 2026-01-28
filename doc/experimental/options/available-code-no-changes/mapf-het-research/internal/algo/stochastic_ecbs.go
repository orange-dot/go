// Package algo - STOCHASTIC-ECBS: ECBS with LogNormal duration-aware focal search.
package algo

import (
	"container/heap"
	"math"
	"math/rand"
	"time"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// StochasticECBS implements Enhanced CBS with stochastic duration modeling.
// Core innovation: Uses LogNormal distributions for task durations and
// probabilistic deadline compliance in focal set filtering.
type StochasticECBS struct {
	MaxTime            float64 // Max planning horizon
	Deadline           float64
	Epsilon            float64       // Deadline probability threshold (e.g., 0.05)
	SuboptimalityBound float64       // w factor for ECBS (>= 1.0)
	NumSamples         int           // Monte Carlo samples for probability estimation
	rng                *rand.Rand
}

// NewStochasticECBS creates a STOCHASTIC-ECBS solver.
func NewStochasticECBS(maxTime float64, epsilon float64) *StochasticECBS {
	return &StochasticECBS{
		MaxTime:            maxTime,
		Epsilon:            epsilon,
		SuboptimalityBound: 1.5, // Allow 50% suboptimal solutions
		NumSamples:         100,
		rng:                rand.New(rand.NewSource(time.Now().UnixNano())),
	}
}

func (s *StochasticECBS) Name() string { return "STOCHASTIC-ECBS" }

// ecbsNode represents a node in the ECBS search tree.
type ecbsNode struct {
	constraints  []Constraint
	solution     *core.Solution
	fMin         float64        // Lower bound (minimum cost)
	fHat         float64        // Estimated cost (for focal ordering)
	makespanDist LogNormalDist  // Distribution of makespan
	deadlineProb float64        // P(makespan <= deadline)
	index        int
}

// openHeap orders by fMin (for optimality bound).
type ecbsOpenHeap []*ecbsNode

func (h ecbsOpenHeap) Len() int           { return len(h) }
func (h ecbsOpenHeap) Less(i, j int) bool { return h[i].fMin < h[j].fMin }
func (h ecbsOpenHeap) Swap(i, j int) {
	h[i], h[j] = h[j], h[i]
	h[i].index = i
	h[j].index = j
}
func (h *ecbsOpenHeap) Push(x any) {
	n := x.(*ecbsNode)
	n.index = len(*h)
	*h = append(*h, n)
}
func (h *ecbsOpenHeap) Pop() any {
	old := *h
	n := len(old)
	x := old[n-1]
	old[n-1] = nil
	*h = old[0 : n-1]
	return x
}

// focalHeap orders by fHat (secondary criterion within focal set).
type ecbsFocalHeap []*ecbsNode

func (h ecbsFocalHeap) Len() int           { return len(h) }
func (h ecbsFocalHeap) Less(i, j int) bool { return h[i].fHat < h[j].fHat }
func (h ecbsFocalHeap) Swap(i, j int) {
	h[i], h[j] = h[j], h[i]
}
func (h *ecbsFocalHeap) Push(x any) {
	*h = append(*h, x.(*ecbsNode))
}
func (h *ecbsFocalHeap) Pop() any {
	old := *h
	n := len(old)
	x := old[n-1]
	old[n-1] = nil
	*h = old[0 : n-1]
	return x
}

// Solve implements STOCHASTIC-ECBS.
func (s *StochasticECBS) Solve(inst *core.Instance) *core.Solution {
	s.Deadline = inst.Deadline

	// Compute assignment
	assignment := s.computeAssignment(inst)
	if assignment == nil {
		return nil
	}

	// Create duration distributions for tasks
	taskDurations := s.createTaskDurations(inst)

	// Initialize root
	root := &ecbsNode{
		constraints: nil,
		solution:    core.NewSolution(),
	}
	root.solution.Assignment = assignment

	if !s.planAllPaths(inst, root) {
		return nil
	}

	root.fMin = root.solution.Makespan
	root.fHat = s.computeFHat(inst, root, taskDurations)
	root.makespanDist = s.computeMakespanDistribution(inst, root, taskDurations)
	root.deadlineProb = root.makespanDist.CDF(s.Deadline)

	// Initialize open and focal sets
	open := &ecbsOpenHeap{}
	heap.Init(open)
	heap.Push(open, root)

	iterations := 0
	maxIterations := 10000

	for open.Len() > 0 && iterations < maxIterations {
		iterations++

		// Get minimum fMin for focal threshold
		fMinBest := (*open)[0].fMin
		focalThreshold := s.SuboptimalityBound * fMinBest

		// Build focal set: nodes with fMin <= w * fMin_best AND deadline probability >= 1-ε
		focal := &ecbsFocalHeap{}
		heap.Init(focal)

		for _, node := range *open {
			if node.fMin <= focalThreshold && s.inFocal(node) {
				heap.Push(focal, node)
			}
		}

		if focal.Len() == 0 {
			// No nodes meet deadline criterion, relax and continue
			// Take from open directly
			node := heap.Pop(open).(*ecbsNode)
			conflict := FindFirstConflict(node.solution.Paths)
			if conflict == nil {
				node.solution.Feasible = true
				return node.solution
			}
			s.processNode(inst, node, open, taskDurations)
			continue
		}

		// Pop from focal (best fHat among deadline-feasible)
		node := heap.Pop(focal).(*ecbsNode)

		// Remove from open
		s.removeFromOpen(open, node)

		// Check for solution
		conflict := FindFirstConflict(node.solution.Paths)
		if conflict == nil {
			node.solution.Feasible = true
			return node.solution
		}

		// Process node
		s.processNode(inst, node, open, taskDurations)
	}

	return nil
}

// inFocal checks if node meets probabilistic deadline criterion.
func (s *StochasticECBS) inFocal(node *ecbsNode) bool {
	// P(makespan <= deadline) >= 1 - ε
	return node.deadlineProb >= (1 - s.Epsilon)
}

// processNode expands a node and adds children to open set.
func (s *StochasticECBS) processNode(inst *core.Instance, node *ecbsNode, open *ecbsOpenHeap, taskDurations map[core.TaskID]LogNormalDist) {
	conflict := FindFirstConflict(node.solution.Paths)
	if conflict == nil {
		return
	}

	// Create children
	for _, robotID := range []core.RobotID{conflict.Robot1, conflict.Robot2} {
		child := &ecbsNode{
			constraints: append(
				append([]Constraint{}, node.constraints...),
				Constraint{
					Robot:    robotID,
					Vertex:   conflict.Vertex,
					Time:     conflict.Time,
					EndTime:  conflict.EndTime,
					IsEdge:   conflict.IsEdge,
					EdgeFrom: conflict.EdgeFrom,
					EdgeTo:   conflict.EdgeTo,
				},
			),
			solution: core.NewSolution(),
		}
		child.solution.Assignment = node.solution.Assignment

		if s.planAllPaths(inst, child) {
			child.fMin = child.solution.Makespan
			child.fHat = s.computeFHat(inst, child, taskDurations)
			child.makespanDist = s.computeMakespanDistribution(inst, child, taskDurations)
			child.deadlineProb = child.makespanDist.CDF(s.Deadline)

			heap.Push(open, child)
		}
	}
}

// removeFromOpen removes a node from the open heap.
func (s *StochasticECBS) removeFromOpen(open *ecbsOpenHeap, target *ecbsNode) {
	for i, node := range *open {
		if node == target {
			heap.Remove(open, i)
			return
		}
	}
}

// createTaskDurations builds LogNormal distributions from task specs.
func (s *StochasticECBS) createTaskDurations(inst *core.Instance) map[core.TaskID]LogNormalDist {
	durations := make(map[core.TaskID]LogNormalDist)

	for _, task := range inst.Tasks {
		durations[task.ID] = NewLogNormalFromMeanStd(task.Duration, task.DurationStd)
	}

	return durations
}

// computeFHat estimates cost using expected durations + conflict penalty.
func (s *StochasticECBS) computeFHat(inst *core.Instance, node *ecbsNode, taskDurations map[core.TaskID]LogNormalDist) float64 {
	// Base: current makespan
	fHat := node.solution.Makespan

	// Add penalty for number of conflicts (estimated expansion cost)
	conflicts := FindAllConflicts(node.solution.Paths)
	fHat += float64(len(conflicts)) * 1.0

	// Add variance penalty (prefer lower variance solutions)
	for _, robot := range inst.Robots {
		for tid, rid := range node.solution.Assignment {
			if rid == robot.ID {
				dist := taskDurations[tid]
				// Penalize high variance
				fHat += dist.Std() * 0.1
			}
		}
	}

	return fHat
}

// computeMakespanDistribution estimates makespan distribution via Fenton-Wilkinson.
func (s *StochasticECBS) computeMakespanDistribution(inst *core.Instance, node *ecbsNode, taskDurations map[core.TaskID]LogNormalDist) LogNormalDist {
	// For each robot, compute distribution of completion time
	robotCompletions := make([]LogNormalDist, 0, len(inst.Robots))

	for _, robot := range inst.Robots {
		// Travel time distribution (assume deterministic for now).
		// Path time already includes nominal task durations, so subtract them out.
		path := node.solution.Paths[robot.ID]
		var travelTime float64
		if len(path) > 0 {
			travelTime = path[len(path)-1].T
		}

		// Task duration distributions and nominal sum
		var taskDists []LogNormalDist
		var nominalTaskSum float64
		for tid, rid := range node.solution.Assignment {
			if rid == robot.ID {
				taskDists = append(taskDists, taskDurations[tid])
				task := inst.TaskByID(tid)
				if task != nil {
					duration := task.Duration
					if duration <= 0 {
						duration, _ = core.NominalDuration(task.Type)
					}
					nominalTaskSum += duration
				}
			}
		}

		if len(taskDists) == 0 {
			continue
		}

		// Sum of task durations
		robotDuration := FentonWilkinson(taskDists)

		// Subtract nominal task time to avoid double counting.
		travelTime -= nominalTaskSum
		if travelTime < 0 {
			travelTime = 0
		}

		// Add travel time as shift
		if travelTime > 0 {
			travelDist := LogNormalDist{Mu: math.Log(travelTime), Sigma: 0.1}
			robotDuration = ConvolveDurations(travelDist, robotDuration)
		}

		robotCompletions = append(robotCompletions, robotDuration)
	}

	if len(robotCompletions) == 0 {
		return LogNormalDist{Mu: 0, Sigma: 0.1}
	}

	// Makespan = max of robot completions
	return MaxApproximation(robotCompletions)
}

// estimateDeadlineProbability uses Monte Carlo for more accurate estimation.
func (s *StochasticECBS) estimateDeadlineProbability(inst *core.Instance, node *ecbsNode, taskDurations map[core.TaskID]LogNormalDist) float64 {
	successCount := 0

	for i := 0; i < s.NumSamples; i++ {
		makespan := s.sampleMakespan(inst, node, taskDurations)
		if makespan <= s.Deadline {
			successCount++
		}
	}

	return float64(successCount) / float64(s.NumSamples)
}

// sampleMakespan generates one sample of the makespan.
func (s *StochasticECBS) sampleMakespan(inst *core.Instance, node *ecbsNode, taskDurations map[core.TaskID]LogNormalDist) float64 {
	var maxCompletion float64

	for _, robot := range inst.Robots {
		// Travel time (path already includes nominal durations).
		path := node.solution.Paths[robot.ID]
		var completion float64
		if len(path) > 0 {
			completion = path[len(path)-1].T
		}

		var nominalTaskSum float64
		for tid, rid := range node.solution.Assignment {
			if rid == robot.ID {
				task := inst.TaskByID(tid)
				if task != nil {
					duration := task.Duration
					if duration <= 0 {
						duration, _ = core.NominalDuration(task.Type)
					}
					nominalTaskSum += duration
				}
			}
		}

		// Remove nominal duration to avoid double counting, then add sampled durations.
		completion -= nominalTaskSum
		if completion < 0 {
			completion = 0
		}

		for tid, rid := range node.solution.Assignment {
			if rid == robot.ID {
				dist := taskDurations[tid]
				completion += dist.Sample(s.rng)
			}
		}

		if completion > maxCompletion {
			maxCompletion = completion
		}
	}

	return maxCompletion
}

// computeAssignment assigns tasks considering stochastic durations.
func (s *StochasticECBS) computeAssignment(inst *core.Instance) core.Assignment {
	assignment := make(core.Assignment)
	robotWorkload := make(map[core.RobotID]float64)
	robotVariance := make(map[core.RobotID]float64)

	for _, task := range inst.Tasks {
		var bestRobot *core.Robot
		bestScore := math.Inf(-1)

		for _, robot := range inst.Robots {
			if !core.CanPerform(robot.Type, task.Type) {
				continue
			}

			currentLoad := robotWorkload[robot.ID]
			currentVar := robotVariance[robot.ID]

			// Estimate with this task added
			newLoad := currentLoad + task.Duration
			newVar := currentVar + task.DurationStd*task.DurationStd

			// Score: balance load and variance
			// Prefer assignments that keep variance low (more predictable)
			expectedSlack := s.Deadline - newLoad
			variancePenalty := math.Sqrt(newVar) * 0.5

			score := expectedSlack - variancePenalty
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
		robotVariance[bestRobot.ID] += task.DurationStd * task.DurationStd
	}

	return assignment
}

// planAllPaths plans paths for all robots.
func (s *StochasticECBS) planAllPaths(inst *core.Instance, node *ecbsNode) bool {
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

		// Plan path with task durations (adds wait segments for service time)
		path := SpaceTimeAStarWithDurations(
			inst.Workspace,
			robot,
			robot.Start,
			goalsWithInfo,
			robotConstraints,
			s.MaxTime,
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

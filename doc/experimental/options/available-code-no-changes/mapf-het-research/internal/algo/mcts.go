// Package algo - FIELD-GUIDED MCTS: Monte Carlo Tree Search with potential field rollouts.
package algo

import (
	"math"
	"math/rand"
	"sort"
	"time"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// ExplorationConstant for UCB1 formula.
const ExplorationConstant = 1.41 // sqrt(2)

// MCTS implements Monte Carlo Tree Search with field-guided rollouts.
// Core innovation: Potential fields guide the rollout policy for faster
// convergence to good solutions.
type MCTS struct {
	TimeBudget time.Duration // Time limit for search
	MaxDepth   int           // Maximum simulation depth
	Beta       float64       // Field bias strength for expansion
	rng        *rand.Rand
}

// NewMCTS creates a FIELD-GUIDED MCTS solver.
func NewMCTS(timeBudget time.Duration) *MCTS {
	return &MCTS{
		TimeBudget: timeBudget,
		MaxDepth:   100,
		Beta:       2.0,
		rng:        rand.New(rand.NewSource(time.Now().UnixNano())),
	}
}

func (m *MCTS) Name() string { return "FIELD-GUIDED-MCTS" }

// MCTSState represents a state in the search tree.
type MCTSState struct {
	Positions map[core.RobotID]core.VertexID
	Time      float64
	Completed map[core.TaskID]bool
	Assignment core.Assignment
}

// Clone creates a deep copy of the state.
func (s *MCTSState) Clone() *MCTSState {
	clone := &MCTSState{
		Positions:  make(map[core.RobotID]core.VertexID),
		Time:       s.Time,
		Completed:  make(map[core.TaskID]bool),
		Assignment: s.Assignment,
	}
	for k, v := range s.Positions {
		clone.Positions[k] = v
	}
	for k, v := range s.Completed {
		clone.Completed[k] = v
	}
	return clone
}

// JointAction represents simultaneous moves for all robots.
type JointAction struct {
	Moves map[core.RobotID]core.VertexID
}

// MCTSNode represents a node in the MCTS tree.
type MCTSNode struct {
	State        *MCTSState
	Parent       *MCTSNode
	Children     []*MCTSNode
	Action       *JointAction
	Visits       int
	TotalReward  float64
	UntriedMoves []*JointAction
}

// NewMCTSNode creates a new node.
func NewMCTSNode(state *MCTSState, parent *MCTSNode, action *JointAction) *MCTSNode {
	return &MCTSNode{
		State:       state,
		Parent:      parent,
		Action:      action,
		Children:    nil,
		Visits:      0,
		TotalReward: 0,
	}
}

// UCB1 computes the UCB1 value for node selection.
func (n *MCTSNode) UCB1(parentVisits int) float64 {
	if n.Visits == 0 {
		return math.Inf(1)
	}
	exploitation := n.TotalReward / float64(n.Visits)
	exploration := ExplorationConstant * math.Sqrt(math.Log(float64(parentVisits))/float64(n.Visits))
	return exploitation + exploration
}

// IsFullyExpanded checks if all possible actions have been tried.
func (n *MCTSNode) IsFullyExpanded() bool {
	return len(n.UntriedMoves) == 0
}

// IsTerminal checks if the state is terminal (all tasks complete or timeout).
func (n *MCTSNode) IsTerminal(maxDepth int, numTasks int) bool {
	completedCount := 0
	for _, done := range n.State.Completed {
		if done {
			completedCount++
		}
	}
	return completedCount >= numTasks || n.State.Time >= float64(maxDepth)
}

// BestChild returns the child with highest UCB1 value.
func (n *MCTSNode) BestChild() *MCTSNode {
	if len(n.Children) == 0 {
		return nil
	}

	var best *MCTSNode
	bestUCB := math.Inf(-1)

	for _, child := range n.Children {
		ucb := child.UCB1(n.Visits)
		if ucb > bestUCB {
			bestUCB = ucb
			best = child
		}
	}

	return best
}

// Solve implements the FIELD-GUIDED MCTS algorithm.
func (m *MCTS) Solve(inst *core.Instance) *core.Solution {
	// Compute assignment
	assignment := m.computeAssignment(inst)
	if assignment == nil {
		return nil
	}

	// Compute potential field
	field := ComputePotentialField(inst.Workspace, inst.Robots, inst.Tasks)

	// Create root state
	rootState := &MCTSState{
		Positions:  make(map[core.RobotID]core.VertexID),
		Time:       0,
		Completed:  make(map[core.TaskID]bool),
		Assignment: assignment,
	}
	for _, robot := range inst.Robots {
		rootState.Positions[robot.ID] = robot.Start
	}

	root := NewMCTSNode(rootState, nil, nil)
	m.generateMoves(inst, root, field)

	// Main MCTS loop
	deadline := time.Now().Add(m.TimeBudget)
	iterations := 0

	for time.Now().Before(deadline) {
		iterations++

		// Selection
		node := m.selectNode(root, len(inst.Tasks))

		// Expansion
		if !node.IsTerminal(m.MaxDepth, len(inst.Tasks)) && node.IsFullyExpanded() {
			// All moves tried, select best child
			if len(node.Children) > 0 {
				node = node.BestChild()
			}
		} else if !node.IsTerminal(m.MaxDepth, len(inst.Tasks)) {
			// Expand: try an untried move
			node = m.expand(inst, node, field)
		}

		// Simulation (rollout)
		reward := m.rollout(inst, node.State, field)

		// Backpropagation
		m.backpropagate(node, reward)
	}

	// Extract best plan
	return m.extractBestPlan(inst, root, field)
}

// selectNode traverses tree using UCB1 until reaching an expandable node.
func (m *MCTS) selectNode(node *MCTSNode, numTasks int) *MCTSNode {
	for !node.IsTerminal(m.MaxDepth, numTasks) && node.IsFullyExpanded() {
		next := node.BestChild()
		if next == nil {
			break
		}
		node = next
	}
	return node
}

// expand adds a new child by trying an untried action.
func (m *MCTS) expand(inst *core.Instance, node *MCTSNode, field *PotentialField) *MCTSNode {
	if len(node.UntriedMoves) == 0 {
		return node
	}

	// Field-biased selection of untried move
	action := m.selectMoveByField(node.UntriedMoves, node.State, field, inst)

	// Remove from untried
	for i, a := range node.UntriedMoves {
		if a == action {
			node.UntriedMoves = append(node.UntriedMoves[:i], node.UntriedMoves[i+1:]...)
			break
		}
	}

	// Apply action to create new state
	newState := m.applyAction(inst, node.State, action)

	// Create child node
	child := NewMCTSNode(newState, node, action)
	m.generateMoves(inst, child, field)
	node.Children = append(node.Children, child)

	return child
}

// selectMoveByField selects a move with probability weighted by field alignment.
func (m *MCTS) selectMoveByField(moves []*JointAction, state *MCTSState, field *PotentialField, inst *core.Instance) *JointAction {
	if len(moves) == 0 {
		return nil
	}

	weights := make([]float64, len(moves))
	totalWeight := 0.0

	for i, action := range moves {
		alignment := m.computeFieldAlignment(action, state, field, inst)
		weight := math.Exp(m.Beta * alignment)
		weights[i] = weight
		totalWeight += weight
	}

	// Weighted random selection
	r := m.rng.Float64() * totalWeight
	cumulative := 0.0
	for i, weight := range weights {
		cumulative += weight
		if r <= cumulative {
			return moves[i]
		}
	}

	return moves[len(moves)-1]
}

// computeFieldAlignment measures how well an action follows field gradients.
func (m *MCTS) computeFieldAlignment(action *JointAction, state *MCTSState, field *PotentialField, inst *core.Instance) float64 {
	alignment := 0.0

	for robotID, nextPos := range action.Moves {
		currentPos := state.Positions[robotID]
		if nextPos == currentPos {
			continue // Wait action - neutral
		}

		// Compute gradient direction at current position
		robot := inst.RobotByID(robotID)
		if robot == nil {
			continue
		}

		// Find the robot's goal (first assigned task not yet complete)
		var goalPos core.VertexID
		for tid, rid := range state.Assignment {
			if rid == robotID && !state.Completed[tid] {
				task := inst.TaskByID(tid)
				if task != nil {
					goalPos = task.Location
					break
				}
			}
		}

		if goalPos == 0 {
			continue
		}

		// Field score at next position
		loadScore := field.LoadGradient[nextPos]
		repulsionScore := field.RepulsiveField[nextPos]

		// Distance improvement
		currentDist := euclideanDist(inst.Workspace.Vertices[currentPos].Pos, inst.Workspace.Vertices[goalPos].Pos)
		nextDist := euclideanDist(inst.Workspace.Vertices[nextPos].Pos, inst.Workspace.Vertices[goalPos].Pos)
		distImprovement := currentDist - nextDist

		alignment += distImprovement + loadScore - repulsionScore
	}

	return alignment
}

// rollout simulates from current state using field-guided policy.
func (m *MCTS) rollout(inst *core.Instance, state *MCTSState, field *PotentialField) float64 {
	simState := state.Clone()
	steps := 0

	for steps < m.MaxDepth {
		// Check termination
		allComplete := true
		for _, task := range inst.Tasks {
			if !simState.Completed[task.ID] {
				allComplete = false
				break
			}
		}
		if allComplete {
			break
		}

		// Generate field-guided action
		action := m.generateFieldGuidedAction(inst, simState, field)

		// Apply action
		simState = m.applyAction(inst, simState, action)
		steps++
	}

	// Evaluate: negative makespan (higher is better)
	return m.evaluate(simState, inst)
}

// generateFieldGuidedAction creates an action following field gradients.
func (m *MCTS) generateFieldGuidedAction(inst *core.Instance, state *MCTSState, field *PotentialField) *JointAction {
	action := &JointAction{Moves: make(map[core.RobotID]core.VertexID)}

	for _, robot := range inst.Robots {
		currentPos := state.Positions[robot.ID]

		// Find goal
		var goalPos core.VertexID
		for tid, rid := range state.Assignment {
			if rid == robot.ID && !state.Completed[tid] {
				task := inst.TaskByID(tid)
				if task != nil {
					goalPos = task.Location
					break
				}
			}
		}

		if goalPos == 0 {
			action.Moves[robot.ID] = currentPos // No goal, wait
			continue
		}

		// Use gradient descent with field
		nextPos := ComputeGradient(currentPos, goalPos, field, inst.Workspace)

		// Verify move is valid
		if !inst.Workspace.CanOccupy(nextPos, robot.Type) {
			nextPos = currentPos
		}

		action.Moves[robot.ID] = nextPos
	}

	return action
}

// applyAction applies a joint action to create a new state.
func (m *MCTS) applyAction(inst *core.Instance, state *MCTSState, action *JointAction) *MCTSState {
	newState := state.Clone()
	newState.Time = state.Time + 1.0

	// Apply moves
	for robotID, nextPos := range action.Moves {
		newState.Positions[robotID] = nextPos

		// Check if robot reached a task location
		for tid, rid := range newState.Assignment {
			if rid == robotID && !newState.Completed[tid] {
				task := inst.TaskByID(tid)
				if task != nil && task.Location == nextPos {
					// Simplified: mark complete immediately (would need duration modeling)
					newState.Completed[tid] = true
					break
				}
			}
		}
	}

	return newState
}

// evaluate computes the reward for a terminal state.
func (m *MCTS) evaluate(state *MCTSState, inst *core.Instance) float64 {
	// Count completed tasks
	completedCount := 0
	for _, done := range state.Completed {
		if done {
			completedCount++
		}
	}

	// Reward: completion rate minus normalized makespan
	completionRate := float64(completedCount) / float64(len(inst.Tasks))
	makespanPenalty := state.Time / float64(m.MaxDepth)

	return completionRate - 0.5*makespanPenalty
}

// backpropagate updates statistics from leaf to root.
func (m *MCTS) backpropagate(node *MCTSNode, reward float64) {
	for node != nil {
		node.Visits++
		node.TotalReward += reward
		node = node.Parent
	}
}

// generateMoves creates all possible joint actions for a node.
func (m *MCTS) generateMoves(inst *core.Instance, node *MCTSNode, field *PotentialField) {
	// For computational tractability, generate a subset of moves
	// based on field guidance rather than full combinatorial explosion

	state := node.State
	var moves []*JointAction

	// Generate individual robot moves
	robotMoves := make(map[core.RobotID][]core.VertexID)
	for _, robot := range inst.Robots {
		pos := state.Positions[robot.ID]

		// Possible moves: wait + neighbors
		options := []core.VertexID{pos}
		for _, neighbor := range inst.Workspace.Neighbors(pos) {
			if inst.Workspace.CanOccupy(neighbor, robot.Type) {
				options = append(options, neighbor)
			}
		}

		// Limit to top-k by field score
		type scoredMove struct {
			pos   core.VertexID
			score float64
		}
		scored := make([]scoredMove, len(options))
		for i, opt := range options {
			score := field.LoadGradient[opt] - field.RepulsiveField[opt]
			scored[i] = scoredMove{opt, score}
		}
		sort.Slice(scored, func(i, j int) bool {
			return scored[i].score > scored[j].score
		})

		maxMoves := 3 // Limit branching
		if len(scored) < maxMoves {
			maxMoves = len(scored)
		}
		for i := 0; i < maxMoves; i++ {
			robotMoves[robot.ID] = append(robotMoves[robot.ID], scored[i].pos)
		}
	}

	// Generate joint actions (limited combinations)
	// For 2 robots with 3 moves each = 9 combinations
	m.generateJointMoves(&moves, inst.Robots, robotMoves, 0, &JointAction{Moves: make(map[core.RobotID]core.VertexID)})

	node.UntriedMoves = moves
}

// generateJointMoves recursively generates joint action combinations.
func (m *MCTS) generateJointMoves(moves *[]*JointAction, robots []*core.Robot, robotMoves map[core.RobotID][]core.VertexID, idx int, current *JointAction) {
	if idx >= len(robots) {
		// Complete action
		action := &JointAction{Moves: make(map[core.RobotID]core.VertexID)}
		for k, v := range current.Moves {
			action.Moves[k] = v
		}
		*moves = append(*moves, action)
		return
	}

	robot := robots[idx]
	for _, move := range robotMoves[robot.ID] {
		current.Moves[robot.ID] = move
		m.generateJointMoves(moves, robots, robotMoves, idx+1, current)
	}
}

// extractBestPlan extracts the best solution found.
func (m *MCTS) extractBestPlan(inst *core.Instance, root *MCTSNode, field *PotentialField) *core.Solution {
	// Find best path through tree
	sol := core.NewSolution()
	sol.Assignment = root.State.Assignment
	sol.Paths = make(map[core.RobotID]core.Path)

	// Initialize paths
	for _, robot := range inst.Robots {
		sol.Paths[robot.ID] = core.Path{{V: robot.Start, T: 0}}
	}

	// Follow best children
	node := root
	for node != nil && len(node.Children) > 0 {
		// Select most visited child (most robust)
		var best *MCTSNode
		bestVisits := -1
		for _, child := range node.Children {
			if child.Visits > bestVisits {
				bestVisits = child.Visits
				best = child
			}
		}

		if best == nil || best.Action == nil {
			break
		}

		// Add moves to paths
		for robotID, nextPos := range best.Action.Moves {
			path := sol.Paths[robotID]
			nextTime := path[len(path)-1].T + 1.0
			sol.Paths[robotID] = append(sol.Paths[robotID], core.TimedVertex{V: nextPos, T: nextTime})
		}

		node = best
	}

	PopulateSchedule(sol, inst)
	sol.ComputeMakespan(inst)
	sol.Feasible = FindFirstConflict(sol.Paths) == nil

	return sol
}

// computeAssignment uses greedy assignment.
func (m *MCTS) computeAssignment(inst *core.Instance) core.Assignment {
	assignment := make(core.Assignment)
	robotWorkload := make(map[core.RobotID]int)

	for _, task := range inst.Tasks {
		var bestRobot *core.Robot
		bestScore := math.Inf(-1)

		for _, robot := range inst.Robots {
			if !core.CanPerform(robot.Type, task.Type) {
				continue
			}

			workload := robotWorkload[robot.ID]
			taskLoc := inst.Workspace.Vertices[task.Location].Pos
			robotLoc := inst.Workspace.Vertices[robot.Start].Pos
			dist := euclideanDist(taskLoc, robotLoc)

			score := -float64(workload)*10 - dist
			if score > bestScore {
				bestScore = score
				bestRobot = robot
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

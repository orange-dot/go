// Package state manages the visualization state.
package state

import (
	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// State holds all visualization state.
type State struct {
	Instance *core.Instance
	Solution *core.Solution
	Playback *PlaybackState
	Edit     *EditState
	Algo     *AlgoState
}

// NewState creates a new visualization state.
func NewState(inst *core.Instance, sol *core.Solution) *State {
	maxTime := 0.0
	if sol != nil {
		maxTime = sol.Makespan
	}

	return &State{
		Instance: inst,
		Solution: sol,
		Playback: NewPlaybackState(maxTime),
		Edit:     NewEditState(),
		Algo:     NewAlgoState(),
	}
}

// CurrentPositions returns interpolated robot positions at current playback time.
func (s *State) CurrentPositions() map[core.RobotID]core.Pos {
	positions := make(map[core.RobotID]core.Pos)

	if s.Solution == nil || s.Instance == nil {
		return positions
	}

	for _, robot := range s.Instance.Robots {
		path := s.Solution.Paths[robot.ID]
		if len(path) == 0 {
			// Robot at start position
			if v, ok := s.Instance.Workspace.Vertices[robot.Start]; ok {
				positions[robot.ID] = v.Pos
			}
			continue
		}

		pos := s.interpolatePosition(path, s.Playback.CurrentTime)
		positions[robot.ID] = pos
	}

	return positions
}

// interpolatePosition computes position along path at given time.
func (s *State) interpolatePosition(path core.Path, t float64) core.Pos {
	if len(path) == 0 {
		return core.Pos{}
	}

	// Before path starts
	if t <= path[0].T {
		if v, ok := s.Instance.Workspace.Vertices[path[0].V]; ok {
			return v.Pos
		}
		return core.Pos{}
	}

	// After path ends
	if t >= path[len(path)-1].T {
		if v, ok := s.Instance.Workspace.Vertices[path[len(path)-1].V]; ok {
			return v.Pos
		}
		return core.Pos{}
	}

	// Find segment containing time t
	for i := 0; i < len(path)-1; i++ {
		if path[i].T <= t && t < path[i+1].T {
			v1 := s.Instance.Workspace.Vertices[path[i].V]
			v2 := s.Instance.Workspace.Vertices[path[i+1].V]
			if v1 == nil || v2 == nil {
				continue
			}

			// Linear interpolation
			dt := path[i+1].T - path[i].T
			if dt <= 0 {
				return v1.Pos
			}

			alpha := (t - path[i].T) / dt
			return core.Pos{
				X: v1.Pos.X + alpha*(v2.Pos.X-v1.Pos.X),
				Y: v1.Pos.Y + alpha*(v2.Pos.Y-v1.Pos.Y),
				Z: v1.Pos.Z + alpha*(v2.Pos.Z-v1.Pos.Z),
			}
		}
	}

	// Fallback to last position
	if v, ok := s.Instance.Workspace.Vertices[path[len(path)-1].V]; ok {
		return v.Pos
	}
	return core.Pos{}
}

// PathHistory returns the path segment up to current time for trails.
func (s *State) PathHistory(robotID core.RobotID) []core.Pos {
	if s.Solution == nil {
		return nil
	}

	path := s.Solution.Paths[robotID]
	if len(path) == 0 {
		return nil
	}

	var history []core.Pos
	for _, tv := range path {
		if tv.T > s.Playback.CurrentTime {
			break
		}
		if v, ok := s.Instance.Workspace.Vertices[tv.V]; ok {
			history = append(history, v.Pos)
		}
	}

	// Add current interpolated position
	if len(history) > 0 {
		history = append(history, s.CurrentPositions()[robotID])
	}

	return history
}

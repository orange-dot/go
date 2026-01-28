// Package widgets provides Gio UI widgets for the visualizer.
package widgets

import (
	"image"
	"image/color"

	"gioui.org/io/event"
	"gioui.org/io/key"
	"gioui.org/io/pointer"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/widget/material"

	"github.com/elektrokombinacija/mapf-het-research/internal/vis/draw"
	"github.com/elektrokombinacija/mapf-het-research/internal/vis/interact"
	"github.com/elektrokombinacija/mapf-het-research/internal/vis/state"
)

// Workspace is the main 2D visualization area.
type Workspace struct {
	state  *state.State
	camera *interact.Camera
}

// NewWorkspace creates a new workspace widget.
func NewWorkspace(st *state.State, camera *interact.Camera) *Workspace {
	return &Workspace{
		state:  st,
		camera: camera,
	}
}

// Layout renders the workspace.
func (w *Workspace) Layout(gtx layout.Context, th *material.Theme) layout.Dimensions {
	// Clip to bounds
	bounds := gtx.Constraints.Max
	defer clip.Rect(image.Rect(0, 0, bounds.X, bounds.Y)).Push(gtx.Ops).Pop()

	// Fill background
	paint.Fill(gtx.Ops, color.NRGBA{R: 25, G: 28, B: 32, A: 255})

	// Handle pointer events
	w.handlePointerEvents(gtx)

	// Draw grid
	draw.DrawGrid(gtx, w.camera, 50, color.NRGBA{R: 40, G: 45, B: 50, A: 255})

	// Draw graph
	if w.state.Instance != nil {
		draw.DrawGraph(gtx, w.state.Instance.Workspace, w.camera, w.state.Edit.SelectedVertices)
	}

	// Draw paths (trails)
	if w.state.Solution != nil && w.state.Instance != nil {
		for _, robot := range w.state.Instance.Robots {
			history := w.state.PathHistory(robot.ID)
			if len(history) > 1 {
				col := draw.RobotColor(robot.Type)
				draw.DrawPathTrail(gtx, history, w.camera, col, 3)
			}
		}
	}

	// Draw future paths
	if w.state.Solution != nil && w.state.Instance != nil {
		for _, robot := range w.state.Instance.Robots {
			path := w.state.Solution.Paths[robot.ID]
			if len(path) > 0 {
				col := draw.RobotColor(robot.Type)
				draw.DrawFuturePath(gtx, path, w.state.Playback.CurrentTime, w.state.Instance.Workspace, w.camera, col)
			}
		}
	}

	// Draw conflicts if in algorithm mode
	if w.state.Algo.Active && w.state.Algo.CurrentConflict != nil {
		draw.DrawActiveConflict(gtx, w.state.Algo.CurrentConflict, w.state.Instance.Workspace, w.camera)
	}

	// Draw robots at current positions
	if w.state.Instance != nil {
		positions := w.state.CurrentPositions()
		draw.DrawRobots(gtx, w.state.Instance.Robots, positions, w.camera, w.state.Edit.SelectedRobots)
	}

	return layout.Dimensions{Size: bounds}
}

func (w *Workspace) handlePointerEvents(gtx layout.Context) {
	// Register for pointer events
	area := clip.Rect(image.Rect(0, 0, gtx.Constraints.Max.X, gtx.Constraints.Max.Y)).Push(gtx.Ops)
	event.Op(gtx.Ops, w)
	area.Pop()

	// Process events
	for {
		ev, ok := gtx.Event(pointer.Filter{
			Target: w,
			Kinds:  pointer.Press | pointer.Drag | pointer.Release | pointer.Scroll | pointer.Move,
		})
		if !ok {
			break
		}
		if pe, ok := ev.(pointer.Event); ok {
			w.handlePointerEvent(gtx, pe)
		}
	}
}

func (w *Workspace) handlePointerEvent(gtx layout.Context, ev pointer.Event) {
	// Camera handles pan and zoom
	w.camera.HandleEvent(gtx, ev)

	switch ev.Kind {
	case pointer.Press:
		if ev.Buttons.Contain(pointer.ButtonPrimary) {
			// Check if shift is pressed for multi-select
			multiSelect := false
			for {
				ke, ok := gtx.Event(key.Filter{Optional: key.ModShift})
				if !ok {
					break
				}
				if _, ok := ke.(key.Event); ok {
					multiSelect = true
				}
			}
			w.handleClick(ev.Position.X, ev.Position.Y, multiSelect)
		}

	case pointer.Drag:
		if w.state.Edit.Dragging {
			// Update dragged vertex position
			worldX, worldY := w.camera.ScreenToWorld(ev.Position.X, ev.Position.Y)
			if v := w.state.Instance.Workspace.Vertices[w.state.Edit.DragVertex]; v != nil {
				v.Pos.X = worldX
				v.Pos.Y = worldY
			}
		}

	case pointer.Release:
		if w.state.Edit.Dragging {
			// Finalize drag with undo action
			w.finalizeDrag()
		}
	}
}

func (w *Workspace) handleClick(screenX, screenY float32, multiSelect bool) {
	if w.state.Instance == nil {
		return
	}

	// Check for vertex hit
	vertex := draw.FindVertexAt(screenX, screenY, w.state.Instance.Workspace, w.camera)
	if vertex != nil {
		if w.state.Edit.Mode == state.ModeDrag || w.state.Edit.Mode == state.ModeView {
			// Start drag
			w.state.Edit.StartDrag(vertex.ID, vertex.Pos.X, vertex.Pos.Y)
		}
		w.state.Edit.SelectVertex(vertex.ID, multiSelect)
		return
	}

	// Check for robot hit
	positions := w.state.CurrentPositions()
	for _, robot := range w.state.Instance.Robots {
		pos := positions[robot.ID]
		if draw.HitTestVertex(screenX, screenY, pos, w.camera, 15) {
			w.state.Edit.SelectRobot(robot.ID, multiSelect)
			return
		}
	}

	// Clicked on empty space - clear selection
	if !multiSelect {
		w.state.Edit.ClearSelection()
	}
}

func (w *Workspace) finalizeDrag() {
	if !w.state.Edit.Dragging {
		return
	}

	vid := w.state.Edit.DragVertex
	if v := w.state.Instance.Workspace.Vertices[vid]; v != nil {
		// Create undo action
		action := &state.MoveVertexAction{
			VertexID: vid,
			OldX:     w.state.Edit.DragStartX,
			OldY:     w.state.Edit.DragStartY,
			NewX:     v.Pos.X,
			NewY:     v.Pos.Y,
		}

		// Only add to undo stack if position actually changed
		if action.OldX != action.NewX || action.OldY != action.NewY {
			w.state.Edit.Execute(action, w.state.Instance)
		}
	}

	w.state.Edit.EndDrag()
}

// Unused reference
var _ op.Ops

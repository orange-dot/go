// Package vis implements a Gio-based visualization for MAPF-HET.
package vis

import (
	"image"
	"image/color"

	"gioui.org/app"
	"gioui.org/io/event"
	"gioui.org/io/key"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/widget"
	"gioui.org/widget/material"

	"github.com/elektrokombinacija/mapf-het-research/internal/algo"
	"github.com/elektrokombinacija/mapf-het-research/internal/core"
	"github.com/elektrokombinacija/mapf-het-research/internal/vis/draw"
	"github.com/elektrokombinacija/mapf-het-research/internal/vis/interact"
	"github.com/elektrokombinacija/mapf-het-research/internal/vis/state"
	"github.com/elektrokombinacija/mapf-het-research/internal/vis/widgets"
)

// App is the main visualization application.
type App struct {
	state     *state.State
	theme     *material.Theme
	workspace *widgets.Workspace
	timeline  *widgets.Timeline
	toolbar   *widgets.Toolbar
	camera    *interact.Camera
}

// NewApp creates a new visualization application.
func NewApp() *App {
	th := material.NewTheme()

	// Create default test instance
	inst := createDefaultInstance()

	// Solve to get initial solution
	solver := algo.NewCBS(100)
	solution := solver.Solve(inst)

	st := state.NewState(inst, solution)
	camera := interact.NewCamera()

	return &App{
		state:     st,
		theme:     th,
		workspace: widgets.NewWorkspace(st, camera),
		timeline:  widgets.NewTimeline(st),
		toolbar:   widgets.NewToolbar(st),
		camera:    camera,
	}
}

// Run starts the application event loop.
func (a *App) Run(w *app.Window) error {
	var ops op.Ops

	// Event filters for keyboard input
	tag := new(int)

	for {
		switch e := w.Event().(type) {
		case app.DestroyEvent:
			return e.Err

		case app.FrameEvent:
			gtx := app.NewContext(&ops, e)

			// Handle keyboard events
			for {
				ev, ok := gtx.Event(key.Filter{Focus: tag, Optional: key.ModCtrl | key.ModShift})
				if !ok {
					break
				}
				if ke, ok := ev.(key.Event); ok && ke.State == key.Press {
					a.handleKeyEvent(ke)
				}
			}

			// Request focus for keyboard input
			event.Op(gtx.Ops, tag)

			a.layout(gtx)
			e.Frame(gtx.Ops)

			// Request continuous redraws during playback
			if a.state.Playback.Playing {
				a.state.Playback.Advance()
				w.Invalidate()
			}
		}
	}
}

func (a *App) handleKeyEvent(e key.Event) {
	switch e.Name {
	case key.NameSpace:
		a.state.Playback.TogglePlay()
	case key.NameLeftArrow:
		a.state.Playback.StepBack()
	case key.NameRightArrow:
		a.state.Playback.StepForward()
	case key.NameHome:
		a.state.Playback.Reset()
	case "R":
		// Reset camera
		a.camera.Reset()
	case "Z":
		if e.Modifiers.Contain(key.ModCtrl) {
			a.state.Edit.Undo()
		}
	case "Y":
		if e.Modifiers.Contain(key.ModCtrl) {
			a.state.Edit.Redo()
		}
	}
}

func (a *App) layout(gtx layout.Context) layout.Dimensions {
	// Fill background
	paint.Fill(gtx.Ops, color.NRGBA{R: 30, G: 30, B: 35, A: 255})

	return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
		// Toolbar at top
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			return a.toolbar.Layout(gtx, a.theme)
		}),
		// Main content area
		layout.Flexed(1, func(gtx layout.Context) layout.Dimensions {
			return layout.Flex{Axis: layout.Horizontal}.Layout(gtx,
				// Workspace (2D view)
				layout.Flexed(1, func(gtx layout.Context) layout.Dimensions {
					return a.workspace.Layout(gtx, a.theme)
				}),
				// CBS tree panel (when algorithm running)
				layout.Rigid(func(gtx layout.Context) layout.Dimensions {
					if !a.state.Algo.Active {
						return layout.Dimensions{}
					}
					return a.layoutCBSTree(gtx)
				}),
			)
		}),
		// Timeline at bottom
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			return a.timeline.Layout(gtx, a.theme)
		}),
	)
}

func (a *App) layoutCBSTree(gtx layout.Context) layout.Dimensions {
	// Placeholder for CBS tree - will be implemented in Phase 4
	gtx.Constraints.Max.X = 300
	rect := image.Rect(0, 0, gtx.Constraints.Max.X, gtx.Constraints.Max.Y)
	paint.FillShape(gtx.Ops, color.NRGBA{R: 40, G: 40, B: 45, A: 255},
		clip.Rect(rect).Op())
	return layout.Dimensions{Size: image.Point{X: 300, Y: gtx.Constraints.Max.Y}}
}

// createDefaultInstance builds a test instance for visualization.
func createDefaultInstance() *core.Instance {
	inst := core.NewInstance()
	inst.Deadline = 300

	// Create 7x7 grid
	for y := 0; y < 7; y++ {
		for x := 0; x < 7; x++ {
			id := core.VertexID(y*7 + x)
			isPad := (x == 0 && y == 0) || (x == 6 && y == 6)
			inst.Workspace.AddVertex(&core.Vertex{
				ID:         id,
				Pos:        core.Pos{X: float64(x) * 50, Y: float64(y) * 50},
				IsPad:      isPad,
				IsCorridor: isPad,
			})
		}
	}

	// Add grid edges
	for y := 0; y < 7; y++ {
		for x := 0; x < 7; x++ {
			id := core.VertexID(y*7 + x)
			if x < 6 {
				inst.Workspace.AddEdge(id, id+1, 50)
			}
			if y < 6 {
				inst.Workspace.AddEdge(id, id+7, 50)
			}
		}
	}

	// Add robots
	inst.Robots = []*core.Robot{
		{ID: 0, Type: core.TypeA, Start: 0},
		{ID: 1, Type: core.TypeB, Start: 48},
		{ID: 2, Type: core.TypeA, Start: 6},
	}

	// Add tasks
	inst.Tasks = []*core.Task{
		core.NewTask(0, core.SwapModule, 24),
		core.NewTask(1, core.Diagnose, 42),
		core.NewTask(2, core.Clean, 12),
	}

	return inst
}

// Unused but needed for compilation
var _ widget.Bool
var _ draw.GraphRenderer

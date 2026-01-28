package widgets

import (
	"image"
	"image/color"

	"gioui.org/layout"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/unit"
	"gioui.org/widget"
	"gioui.org/widget/material"

	"github.com/elektrokombinacija/mapf-het-research/internal/vis/state"
)

// Toolbar provides control buttons.
type Toolbar struct {
	state *state.State

	// Buttons
	playBtn      widget.Clickable
	pauseBtn     widget.Clickable
	resetBtn     widget.Clickable
	stepFwdBtn   widget.Clickable
	stepBackBtn  widget.Clickable
	speedUpBtn   widget.Clickable
	speedDownBtn widget.Clickable

	// Algorithm controls
	runAlgoBtn   widget.Clickable
	stepAlgoBtn  widget.Clickable
	pauseAlgoBtn widget.Clickable

	// Edit mode buttons
	viewModeBtn   widget.Clickable
	selectModeBtn widget.Clickable
	dragModeBtn   widget.Clickable
	addVertexBtn  widget.Clickable
	addEdgeBtn    widget.Clickable
	deleteBtn     widget.Clickable

	// Undo/redo
	undoBtn widget.Clickable
	redoBtn widget.Clickable
}

// NewToolbar creates a new toolbar.
func NewToolbar(st *state.State) *Toolbar {
	return &Toolbar{
		state: st,
	}
}

// Layout renders the toolbar.
func (t *Toolbar) Layout(gtx layout.Context, th *material.Theme) layout.Dimensions {
	height := 48

	// Background
	rect := image.Rect(0, 0, gtx.Constraints.Max.X, height)
	paint.FillShape(gtx.Ops, color.NRGBA{R: 40, G: 43, B: 48, A: 255}, clip.Rect(rect).Op())

	// Handle button clicks
	t.handleClicks(gtx)

	// Layout buttons
	return layout.Inset{Left: unit.Dp(10), Right: unit.Dp(10), Top: unit.Dp(8), Bottom: unit.Dp(8)}.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		return layout.Flex{Axis: layout.Horizontal, Alignment: layout.Middle, Spacing: layout.SpaceStart}.Layout(gtx,
			// Playback controls
			layout.Rigid(func(gtx layout.Context) layout.Dimensions {
				return t.layoutPlaybackControls(gtx, th)
			}),

			// Separator
			layout.Rigid(func(gtx layout.Context) layout.Dimensions {
				return t.layoutSeparator(gtx)
			}),

			// Speed controls
			layout.Rigid(func(gtx layout.Context) layout.Dimensions {
				return t.layoutSpeedControls(gtx, th)
			}),

			// Separator
			layout.Rigid(func(gtx layout.Context) layout.Dimensions {
				return t.layoutSeparator(gtx)
			}),

			// Edit mode controls
			layout.Rigid(func(gtx layout.Context) layout.Dimensions {
				return t.layoutEditControls(gtx, th)
			}),

			// Spacer
			layout.Flexed(1, func(gtx layout.Context) layout.Dimensions {
				return layout.Dimensions{}
			}),

			// Algorithm controls
			layout.Rigid(func(gtx layout.Context) layout.Dimensions {
				return t.layoutAlgoControls(gtx, th)
			}),
		)
	})
}

func (t *Toolbar) layoutPlaybackControls(gtx layout.Context, th *material.Theme) layout.Dimensions {
	return layout.Flex{Axis: layout.Horizontal, Spacing: layout.SpaceStart}.Layout(gtx,
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			return t.iconButton(gtx, th, &t.stepBackBtn, "|<")
		}),
		layout.Rigid(layout.Spacer{Width: unit.Dp(4)}.Layout),
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			if t.state.Playback.Playing {
				return t.iconButton(gtx, th, &t.pauseBtn, "||")
			}
			return t.iconButton(gtx, th, &t.playBtn, ">")
		}),
		layout.Rigid(layout.Spacer{Width: unit.Dp(4)}.Layout),
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			return t.iconButton(gtx, th, &t.stepFwdBtn, ">|")
		}),
		layout.Rigid(layout.Spacer{Width: unit.Dp(4)}.Layout),
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			return t.iconButton(gtx, th, &t.resetBtn, "[]")
		}),
	)
}

func (t *Toolbar) layoutSpeedControls(gtx layout.Context, th *material.Theme) layout.Dimensions {
	return layout.Flex{Axis: layout.Horizontal, Spacing: layout.SpaceStart}.Layout(gtx,
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			return t.iconButton(gtx, th, &t.speedDownBtn, "-")
		}),
		layout.Rigid(layout.Spacer{Width: unit.Dp(4)}.Layout),
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			return t.iconButton(gtx, th, &t.speedUpBtn, "+")
		}),
	)
}

func (t *Toolbar) layoutEditControls(gtx layout.Context, th *material.Theme) layout.Dimensions {
	return layout.Flex{Axis: layout.Horizontal, Spacing: layout.SpaceStart}.Layout(gtx,
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			return t.modeButton(gtx, th, &t.viewModeBtn, "V", t.state.Edit.Mode == state.ModeView)
		}),
		layout.Rigid(layout.Spacer{Width: unit.Dp(2)}.Layout),
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			return t.modeButton(gtx, th, &t.selectModeBtn, "S", t.state.Edit.Mode == state.ModeSelect)
		}),
		layout.Rigid(layout.Spacer{Width: unit.Dp(2)}.Layout),
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			return t.modeButton(gtx, th, &t.dragModeBtn, "D", t.state.Edit.Mode == state.ModeDrag)
		}),
		layout.Rigid(layout.Spacer{Width: unit.Dp(8)}.Layout),
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			return t.iconButton(gtx, th, &t.undoBtn, "<-")
		}),
		layout.Rigid(layout.Spacer{Width: unit.Dp(2)}.Layout),
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			return t.iconButton(gtx, th, &t.redoBtn, "->")
		}),
	)
}

func (t *Toolbar) layoutAlgoControls(gtx layout.Context, th *material.Theme) layout.Dimensions {
	return layout.Flex{Axis: layout.Horizontal, Spacing: layout.SpaceStart}.Layout(gtx,
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			label := "Run CBS"
			if t.state.Algo.Active {
				label = "Stop"
			}
			return t.textButton(gtx, th, &t.runAlgoBtn, label)
		}),
		layout.Rigid(layout.Spacer{Width: unit.Dp(4)}.Layout),
		layout.Rigid(func(gtx layout.Context) layout.Dimensions {
			if !t.state.Algo.Active {
				return layout.Dimensions{}
			}
			return t.iconButton(gtx, th, &t.stepAlgoBtn, ">>")
		}),
	)
}

func (t *Toolbar) layoutSeparator(gtx layout.Context) layout.Dimensions {
	return layout.Inset{Left: unit.Dp(8), Right: unit.Dp(8)}.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		rect := image.Rect(0, 0, 1, 24)
		paint.FillShape(gtx.Ops, color.NRGBA{R: 60, G: 65, B: 70, A: 255}, clip.Rect(rect).Op())
		return layout.Dimensions{Size: image.Point{X: 1, Y: 24}}
	})
}

func (t *Toolbar) iconButton(gtx layout.Context, th *material.Theme, btn *widget.Clickable, icon string) layout.Dimensions {
	return t.buttonBase(gtx, th, btn, icon, false)
}

func (t *Toolbar) textButton(gtx layout.Context, th *material.Theme, btn *widget.Clickable, text string) layout.Dimensions {
	return t.buttonBase(gtx, th, btn, text, false)
}

func (t *Toolbar) modeButton(gtx layout.Context, th *material.Theme, btn *widget.Clickable, icon string, active bool) layout.Dimensions {
	return t.buttonBase(gtx, th, btn, icon, active)
}

func (t *Toolbar) buttonBase(gtx layout.Context, th *material.Theme, btn *widget.Clickable, text string, active bool) layout.Dimensions {
	bg := color.NRGBA{R: 55, G: 58, B: 65, A: 255}
	if active {
		bg = color.NRGBA{R: 80, G: 130, B: 180, A: 255}
	}
	if btn.Hovered() {
		bg.R = minU8(bg.R+15, 255)
		bg.G = minU8(bg.G+15, 255)
		bg.B = minU8(bg.B+15, 255)
	}

	return btn.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		return layout.Background{}.Layout(gtx,
			func(gtx layout.Context) layout.Dimensions {
				gtx.Constraints.Min = image.Point{X: 32, Y: 28}
				rect := image.Rect(0, 0, gtx.Constraints.Min.X, gtx.Constraints.Min.Y)
				paint.FillShape(gtx.Ops, bg, clip.Rect(rect).Op())
				return layout.Dimensions{Size: gtx.Constraints.Min}
			},
			func(gtx layout.Context) layout.Dimensions {
				return layout.Center.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
					label := material.Label(th, 12, text)
					label.Color = color.NRGBA{R: 220, G: 220, B: 220, A: 255}
					return label.Layout(gtx)
				})
			},
		)
	})
}

func (t *Toolbar) handleClicks(gtx layout.Context) {
	// Playback
	for t.playBtn.Clicked(gtx) {
		t.state.Playback.TogglePlay()
	}
	for t.pauseBtn.Clicked(gtx) {
		t.state.Playback.TogglePlay()
	}
	for t.resetBtn.Clicked(gtx) {
		t.state.Playback.Reset()
	}
	for t.stepFwdBtn.Clicked(gtx) {
		t.state.Playback.StepForward()
	}
	for t.stepBackBtn.Clicked(gtx) {
		t.state.Playback.StepBack()
	}

	// Speed
	for t.speedUpBtn.Clicked(gtx) {
		t.state.Playback.SetSpeed(t.state.Playback.Speed * 1.5)
	}
	for t.speedDownBtn.Clicked(gtx) {
		t.state.Playback.SetSpeed(t.state.Playback.Speed / 1.5)
	}

	// Edit modes
	for t.viewModeBtn.Clicked(gtx) {
		t.state.Edit.Mode = state.ModeView
	}
	for t.selectModeBtn.Clicked(gtx) {
		t.state.Edit.Mode = state.ModeSelect
	}
	for t.dragModeBtn.Clicked(gtx) {
		t.state.Edit.Mode = state.ModeDrag
	}

	// Undo/redo
	for t.undoBtn.Clicked(gtx) {
		if action := t.state.Edit.Undo(); action != nil {
			action.Undo(t.state.Instance)
		}
	}
	for t.redoBtn.Clicked(gtx) {
		if action := t.state.Edit.Redo(); action != nil {
			action.Do(t.state.Instance)
		}
	}

	// Algorithm
	for t.runAlgoBtn.Clicked(gtx) {
		if t.state.Algo.Active {
			t.state.Algo.Stop()
		} else {
			t.state.Algo.Start()
		}
	}
	for t.stepAlgoBtn.Clicked(gtx) {
		t.state.Algo.Step()
	}
}

func minU8(a, b uint8) uint8 {
	if a < b {
		return a
	}
	return b
}

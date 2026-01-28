package widgets

import (
	"fmt"
	"image"
	"image/color"

	"gioui.org/io/event"
	"gioui.org/io/pointer"
	"gioui.org/layout"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/text"
	"gioui.org/unit"
	"gioui.org/widget/material"

	"github.com/elektrokombinacija/mapf-het-research/internal/vis/state"
)

// Timeline is a time scrubber widget.
type Timeline struct {
	state    *state.State
	dragging bool
}

// NewTimeline creates a new timeline widget.
func NewTimeline(st *state.State) *Timeline {
	return &Timeline{
		state: st,
	}
}

// Layout renders the timeline.
func (t *Timeline) Layout(gtx layout.Context, th *material.Theme) layout.Dimensions {
	height := 60

	// Background
	rect := image.Rect(0, 0, gtx.Constraints.Max.X, height)
	paint.FillShape(gtx.Ops, color.NRGBA{R: 35, G: 38, B: 42, A: 255}, clip.Rect(rect).Op())

	// Handle pointer events
	t.handlePointerEvents(gtx, height)

	// Draw timeline
	margin := 20
	trackY := height / 2
	trackHeight := 6
	trackWidth := gtx.Constraints.Max.X - 2*margin

	// Track background
	trackRect := image.Rect(margin, trackY-trackHeight/2, margin+trackWidth, trackY+trackHeight/2)
	paint.FillShape(gtx.Ops, color.NRGBA{R: 60, G: 65, B: 70, A: 255}, clip.Rect(trackRect).Op())

	// Progress fill
	progress := t.state.Playback.Progress()
	fillWidth := int(float64(trackWidth) * progress)
	if fillWidth > 0 {
		fillRect := image.Rect(margin, trackY-trackHeight/2, margin+fillWidth, trackY+trackHeight/2)
		paint.FillShape(gtx.Ops, color.NRGBA{R: 100, G: 180, B: 255, A: 255}, clip.Rect(fillRect).Op())
	}

	// Playhead
	playheadX := margin + fillWidth
	playheadSize := 12
	playheadRect := image.Rect(playheadX-playheadSize/2, trackY-playheadSize/2, playheadX+playheadSize/2, trackY+playheadSize/2)
	paint.FillShape(gtx.Ops, color.NRGBA{R: 255, G: 255, B: 255, A: 255}, clip.Rect(playheadRect).Op())

	// Time labels
	t.drawTimeLabels(gtx, th, margin, trackWidth, height)

	return layout.Dimensions{Size: image.Point{X: gtx.Constraints.Max.X, Y: height}}
}

func (t *Timeline) drawTimeLabels(gtx layout.Context, th *material.Theme, margin, trackWidth, height int) {
	// Current time
	currentLabel := material.Label(th, 12, fmt.Sprintf("%.1fs", t.state.Playback.CurrentTime))
	currentLabel.Color = color.NRGBA{R: 200, G: 200, B: 200, A: 255}
	currentLabel.Alignment = text.Start

	// Max time
	maxLabel := material.Label(th, 12, fmt.Sprintf("%.1fs", t.state.Playback.MaxTime))
	maxLabel.Color = color.NRGBA{R: 150, G: 150, B: 150, A: 255}
	maxLabel.Alignment = text.End

	// Speed indicator
	speedLabel := material.Label(th, 12, fmt.Sprintf("%.1fx", t.state.Playback.Speed))
	speedLabel.Color = color.NRGBA{R: 150, G: 180, B: 200, A: 255}

	// Layout labels
	layout.Inset{Top: unit.Dp(4), Left: unit.Dp(20), Right: unit.Dp(20)}.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		return layout.Flex{Axis: layout.Horizontal, Spacing: layout.SpaceBetween}.Layout(gtx,
			layout.Rigid(func(gtx layout.Context) layout.Dimensions {
				return currentLabel.Layout(gtx)
			}),
			layout.Rigid(func(gtx layout.Context) layout.Dimensions {
				return speedLabel.Layout(gtx)
			}),
			layout.Rigid(func(gtx layout.Context) layout.Dimensions {
				return maxLabel.Layout(gtx)
			}),
		)
	})
	_ = margin
	_ = trackWidth
	_ = height
}

func (t *Timeline) handlePointerEvents(gtx layout.Context, height int) {
	margin := 20
	trackWidth := gtx.Constraints.Max.X - 2*margin

	// Register for pointer events
	area := clip.Rect(image.Rect(0, 0, gtx.Constraints.Max.X, height)).Push(gtx.Ops)
	event.Op(gtx.Ops, t)
	area.Pop()

	// Process events
	for {
		ev, ok := gtx.Event(pointer.Filter{
			Target: t,
			Kinds:  pointer.Press | pointer.Drag | pointer.Release,
		})
		if !ok {
			break
		}
		if pe, ok := ev.(pointer.Event); ok {
			switch pe.Kind {
			case pointer.Press:
				t.dragging = true
				t.seekToPosition(pe.Position.X, margin, trackWidth)

			case pointer.Drag:
				if t.dragging {
					t.seekToPosition(pe.Position.X, margin, trackWidth)
				}

			case pointer.Release:
				t.dragging = false
			}
		}
	}
}

func (t *Timeline) seekToPosition(screenX float32, margin, trackWidth int) {
	// Calculate progress from screen position
	x := float64(screenX) - float64(margin)
	progress := x / float64(trackWidth)

	if progress < 0 {
		progress = 0
	}
	if progress > 1 {
		progress = 1
	}

	// Set time
	newTime := progress * t.state.Playback.MaxTime
	t.state.Playback.SetTime(newTime)
}

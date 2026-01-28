package draw

import (
	"image/color"
	"math"
	"time"

	"gioui.org/f32"
	"gioui.org/layout"
	"gioui.org/op/clip"
	"gioui.org/op/paint"

	"github.com/elektrokombinacija/mapf-het-research/internal/algo"
	"github.com/elektrokombinacija/mapf-het-research/internal/core"
	"github.com/elektrokombinacija/mapf-het-research/internal/vis/interact"
)

// Conflict colors
var (
	ColorConflictVertex = color.NRGBA{R: 255, G: 80, B: 80, A: 200}
	ColorConflictEdge   = color.NRGBA{R: 255, G: 150, B: 80, A: 200}
)

// DrawConflict draws a conflict indicator.
func DrawConflict(gtx layout.Context, conflict *algo.Conflict, ws *core.Workspace, camera *interact.Camera) {
	if conflict == nil {
		return
	}

	v := ws.Vertices[conflict.Vertex]
	if v == nil {
		return
	}

	screenX, screenY := camera.WorldToScreen(v.Pos.X, v.Pos.Y)

	// Pulsing animation
	pulse := float32(math.Sin(float64(time.Now().UnixMilli())/200.0)*0.3 + 0.7)

	if conflict.IsEdge {
		// Draw edge conflict
		drawEdgeConflict(gtx, conflict, ws, camera, pulse)
	} else {
		// Draw vertex conflict as pulsing ring
		radius := float32(20) * camera.Zoom * pulse
		DrawCircleOutline(gtx, screenX, screenY, radius, ColorConflictVertex, 3*camera.Zoom)

		// Inner pulsing circle
		innerRadius := radius * 0.4 * pulse
		drawFilledCircle(gtx, screenX, screenY, innerRadius, ColorConflictVertex)
	}
}

func drawEdgeConflict(gtx layout.Context, conflict *algo.Conflict, ws *core.Workspace, camera *interact.Camera, pulse float32) {
	v1 := ws.Vertices[conflict.EdgeFrom]
	v2 := ws.Vertices[conflict.EdgeTo]
	if v1 == nil || v2 == nil {
		return
	}

	x1, y1 := camera.WorldToScreen(v1.Pos.X, v1.Pos.Y)
	x2, y2 := camera.WorldToScreen(v2.Pos.X, v2.Pos.Y)

	// Midpoint
	midX := (x1 + x2) / 2
	midY := (y1 + y2) / 2

	// Draw pulsing circle at midpoint
	radius := float32(15) * camera.Zoom * pulse
	DrawCircleOutline(gtx, midX, midY, radius, ColorConflictEdge, 2*camera.Zoom)

	// Draw crossing lines to indicate swap conflict
	lineLen := radius * 0.7
	drawConflictX(gtx, midX, midY, lineLen, ColorConflictEdge)

	// Highlight the edge
	col := ColorConflictEdge
	col.A = uint8(float32(col.A) * pulse)
	drawConflictEdge(gtx, x1, y1, x2, y2, 4*camera.Zoom, col)
}

func drawConflictX(gtx layout.Context, cx, cy, size float32, col color.NRGBA) {
	width := float32(3)

	// Draw two crossing lines
	for _, angle := range []float64{45, 135} {
		rad := angle * math.Pi / 180
		dx := float32(math.Cos(rad)) * size
		dy := float32(math.Sin(rad)) * size

		x1, y1 := cx-dx, cy-dy
		x2, y2 := cx+dx, cy+dy

		drawPathSegment(gtx, x1, y1, x2, y2, width, col)
	}
}

func drawConflictEdge(gtx layout.Context, x1, y1, x2, y2, width float32, col color.NRGBA) {
	dx := x2 - x1
	dy := y2 - y1
	length := float32(math.Sqrt(float64(dx*dx + dy*dy)))
	if length < 0.1 {
		return
	}

	dx /= length
	dy /= length
	px := -dy * width / 2
	py := dx * width / 2

	var path clip.Path
	path.Begin(gtx.Ops)
	path.MoveTo(f32.Pt(x1+px, y1+py))
	path.LineTo(f32.Pt(x2+px, y2+py))
	path.LineTo(f32.Pt(x2-px, y2-py))
	path.LineTo(f32.Pt(x1-px, y1-py))
	path.Close()

	paint.FillShape(gtx.Ops, col, clip.Outline{Path: path.End()}.Op())
}

// DrawAllConflicts draws all conflicts at a given time.
func DrawAllConflicts(gtx layout.Context, conflicts []*algo.Conflict, ws *core.Workspace, camera *interact.Camera, currentTime float64) {
	for _, conflict := range conflicts {
		// Only show conflicts that are active at current time
		if conflict.Time <= currentTime && currentTime <= conflict.EndTime+0.5 {
			DrawConflict(gtx, conflict, ws, camera)
		}
	}
}

// DrawConstraint draws a constraint indicator on the workspace.
func DrawConstraint(gtx layout.Context, constraint algo.Constraint, ws *core.Workspace, camera *interact.Camera) {
	v := ws.Vertices[constraint.Vertex]
	if v == nil {
		return
	}

	screenX, screenY := camera.WorldToScreen(v.Pos.X, v.Pos.Y)

	// Draw constraint as a "forbidden" indicator
	radius := float32(12) * camera.Zoom
	col := color.NRGBA{R: 200, G: 100, B: 100, A: 150}

	// Circle with diagonal line through it
	DrawCircleOutline(gtx, screenX, screenY, radius, col, 2*camera.Zoom)

	// Diagonal line
	diagLen := radius * 0.8
	x1 := screenX - diagLen
	y1 := screenY - diagLen
	x2 := screenX + diagLen
	y2 := screenY + diagLen
	drawPathSegment(gtx, x1, y1, x2, y2, 2*camera.Zoom, col)
}

// DrawActiveConflict draws the currently detected conflict with emphasis.
func DrawActiveConflict(gtx layout.Context, conflict *algo.Conflict, ws *core.Workspace, camera *interact.Camera) {
	if conflict == nil {
		return
	}

	v := ws.Vertices[conflict.Vertex]
	if v == nil {
		return
	}

	screenX, screenY := camera.WorldToScreen(v.Pos.X, v.Pos.Y)

	// Draw expanding rings
	t := float64(time.Now().UnixMilli()) / 1000.0
	for i := 0; i < 3; i++ {
		phase := float64(i) * 0.3
		ripple := float32(math.Mod(t+phase, 1.0))
		radius := float32(10+30*ripple) * camera.Zoom
		alpha := uint8((1.0 - ripple) * 200)

		col := ColorConflictVertex
		col.A = alpha
		DrawCircleOutline(gtx, screenX, screenY, radius, col, 2*camera.Zoom)
	}

	// Center dot
	drawFilledCircle(gtx, screenX, screenY, 6*camera.Zoom, ColorConflictVertex)
}

// Package draw provides rendering functions for visualization.
package draw

import (
	"image"
	"image/color"
	"math"

	"gioui.org/f32"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
	"github.com/elektrokombinacija/mapf-het-research/internal/vis/interact"
)

// GraphRenderer for interface satisfaction
type GraphRenderer interface{}

// Colors for different vertex types
var (
	ColorVertexDefault  = color.NRGBA{R: 100, G: 120, B: 140, A: 255}
	ColorVertexPad      = color.NRGBA{R: 80, G: 180, B: 100, A: 255}
	ColorVertexCorridor = color.NRGBA{R: 100, G: 140, B: 220, A: 255}
	ColorVertexSelected = color.NRGBA{R: 255, G: 200, B: 80, A: 255}
	ColorEdgeDefault    = color.NRGBA{R: 80, G: 90, B: 100, A: 180}
	ColorEdgeHighlight  = color.NRGBA{R: 150, G: 170, B: 190, A: 255}
)

// DrawGraph renders the workspace graph.
func DrawGraph(gtx layout.Context, ws *core.Workspace, camera *interact.Camera, selected map[core.VertexID]bool) {
	// Draw edges first (underneath vertices)
	for vid, edges := range ws.Edges {
		v1 := ws.Vertices[vid]
		if v1 == nil {
			continue
		}

		for _, edge := range edges {
			v2 := ws.Vertices[edge.To]
			if v2 == nil {
				continue
			}

			// Only draw each edge once (smaller ID to larger ID)
			if vid > edge.To {
				continue
			}

			DrawEdge(gtx, v1.Pos, v2.Pos, camera, ColorEdgeDefault)
		}
	}

	// Draw vertices
	for _, v := range ws.Vertices {
		vertexColor := vertexColor(v, selected[v.ID])
		DrawVertex(gtx, v.Pos, camera, vertexColor, 8)
	}
}

func vertexColor(v *core.Vertex, isSelected bool) color.NRGBA {
	if isSelected {
		return ColorVertexSelected
	}
	if v.IsPad {
		return ColorVertexPad
	}
	if v.IsCorridor {
		return ColorVertexCorridor
	}
	return ColorVertexDefault
}

// DrawVertex draws a vertex as a filled circle.
func DrawVertex(gtx layout.Context, pos core.Pos, camera *interact.Camera, col color.NRGBA, radius float32) {
	screenX, screenY := camera.WorldToScreen(pos.X, pos.Y)

	// Create circle path
	center := f32.Pt(screenX, screenY)
	r := radius * camera.Zoom

	var path clip.Path
	path.Begin(gtx.Ops)
	path.Move(f32.Pt(center.X+r, center.Y))

	// Approximate circle with segments
	segments := 16
	for i := 1; i <= segments; i++ {
		angle := float64(i) * 2 * math.Pi / float64(segments)
		x := center.X + r*float32(math.Cos(angle))
		y := center.Y + r*float32(math.Sin(angle))
		path.Line(f32.Pt(x-path.Pos().X, y-path.Pos().Y))
	}
	path.Close()

	paint.FillShape(gtx.Ops, col, clip.Outline{Path: path.End()}.Op())
}

// DrawEdge draws an edge as a line between two positions.
func DrawEdge(gtx layout.Context, p1, p2 core.Pos, camera *interact.Camera, col color.NRGBA) {
	x1, y1 := camera.WorldToScreen(p1.X, p1.Y)
	x2, y2 := camera.WorldToScreen(p2.X, p2.Y)

	// Calculate line direction and perpendicular
	dx := x2 - x1
	dy := y2 - y1
	length := float32(math.Sqrt(float64(dx*dx + dy*dy)))
	if length < 0.1 {
		return
	}

	// Normalize
	dx /= length
	dy /= length

	// Perpendicular for line width
	width := float32(2.0) * camera.Zoom
	px := -dy * width / 2
	py := dx * width / 2

	// Build quad path
	var path clip.Path
	path.Begin(gtx.Ops)
	path.MoveTo(f32.Pt(x1+px, y1+py))
	path.LineTo(f32.Pt(x2+px, y2+py))
	path.LineTo(f32.Pt(x2-px, y2-py))
	path.LineTo(f32.Pt(x1-px, y1-py))
	path.Close()

	paint.FillShape(gtx.Ops, col, clip.Outline{Path: path.End()}.Op())
}

// DrawCircleOutline draws a circle outline.
func DrawCircleOutline(gtx layout.Context, centerX, centerY float32, radius float32, col color.NRGBA, strokeWidth float32) {
	// Outer circle
	var outerPath clip.Path
	outerPath.Begin(gtx.Ops)
	outerPath.Move(f32.Pt(centerX+radius, centerY))

	segments := 24
	for i := 1; i <= segments; i++ {
		angle := float64(i) * 2 * math.Pi / float64(segments)
		x := centerX + radius*float32(math.Cos(angle))
		y := centerY + radius*float32(math.Sin(angle))
		outerPath.Line(f32.Pt(x-outerPath.Pos().X, y-outerPath.Pos().Y))
	}
	outerPath.Close()

	// Inner circle (hole)
	innerR := radius - strokeWidth
	if innerR < 0 {
		innerR = 0
	}
	outerPath.Move(f32.Pt(centerX+innerR-outerPath.Pos().X, centerY-outerPath.Pos().Y))
	for i := 1; i <= segments; i++ {
		angle := float64(i) * 2 * math.Pi / float64(segments)
		x := centerX + innerR*float32(math.Cos(angle))
		y := centerY + innerR*float32(math.Sin(angle))
		outerPath.Line(f32.Pt(x-outerPath.Pos().X, y-outerPath.Pos().Y))
	}
	outerPath.Close()

	paint.FillShape(gtx.Ops, col, clip.Outline{Path: outerPath.End()}.Op())
}

// HitTestVertex checks if screen point hits a vertex.
func HitTestVertex(screenX, screenY float32, pos core.Pos, camera *interact.Camera, radius float32) bool {
	vx, vy := camera.WorldToScreen(pos.X, pos.Y)
	dx := screenX - vx
	dy := screenY - vy
	r := radius * camera.Zoom
	return dx*dx+dy*dy <= r*r
}

// FindVertexAt finds the vertex at screen coordinates.
func FindVertexAt(screenX, screenY float32, ws *core.Workspace, camera *interact.Camera) *core.Vertex {
	radius := float32(10) // Hit test radius
	for _, v := range ws.Vertices {
		if HitTestVertex(screenX, screenY, v.Pos, camera, radius) {
			return v
		}
	}
	return nil
}

// DrawGrid draws a background grid.
func DrawGrid(gtx layout.Context, camera *interact.Camera, gridSize float64, col color.NRGBA) {
	bounds := gtx.Constraints.Max

	// Calculate visible world bounds
	minWorldX, minWorldY := camera.ScreenToWorld(0, 0)
	maxWorldX, maxWorldY := camera.ScreenToWorld(float32(bounds.X), float32(bounds.Y))

	// Snap to grid
	startX := math.Floor(minWorldX/gridSize) * gridSize
	startY := math.Floor(minWorldY/gridSize) * gridSize

	// Draw vertical lines
	for x := startX; x <= maxWorldX; x += gridSize {
		sx, _ := camera.WorldToScreen(x, minWorldY)
		_, sy2 := camera.WorldToScreen(x, maxWorldY)
		if sx >= 0 && sx <= float32(bounds.X) {
			rect := image.Rect(int(sx), 0, int(sx)+1, bounds.Y)
			paint.FillShape(gtx.Ops, col, clip.Rect(rect).Op())
		}
		_ = sy2 // Use the variable
	}

	// Draw horizontal lines
	for y := startY; y <= maxWorldY; y += gridSize {
		_, sy := camera.WorldToScreen(minWorldX, y)
		sx2, _ := camera.WorldToScreen(maxWorldX, y)
		if sy >= 0 && sy <= float32(bounds.Y) {
			rect := image.Rect(0, int(sy), bounds.X, int(sy)+1)
			paint.FillShape(gtx.Ops, col, clip.Rect(rect).Op())
		}
		_ = sx2 // Use the variable
	}
}

// Unused reference to satisfy import
var _ op.Ops

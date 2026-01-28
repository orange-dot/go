package draw

import (
	"image/color"
	"math"

	"gioui.org/f32"
	"gioui.org/layout"
	"gioui.org/op/clip"
	"gioui.org/op/paint"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
	"github.com/elektrokombinacija/mapf-het-research/internal/vis/interact"
)

// DrawPath draws a complete path as a line with optional direction indicators.
func DrawPath(gtx layout.Context, path []core.Pos, camera *interact.Camera, col color.NRGBA, width float32) {
	if len(path) < 2 {
		return
	}

	w := width * camera.Zoom

	for i := 0; i < len(path)-1; i++ {
		x1, y1 := camera.WorldToScreen(path[i].X, path[i].Y)
		x2, y2 := camera.WorldToScreen(path[i+1].X, path[i+1].Y)

		drawPathSegment(gtx, x1, y1, x2, y2, w, col)
	}
}

// DrawPathTrail draws a fading trail behind a robot.
func DrawPathTrail(gtx layout.Context, history []core.Pos, camera *interact.Camera, baseColor color.NRGBA, maxWidth float32) {
	if len(history) < 2 {
		return
	}

	n := len(history)
	for i := 0; i < n-1; i++ {
		// Fade alpha from start to end
		alpha := uint8(50 + float64(i)/float64(n)*150)
		col := baseColor
		col.A = alpha

		// Width also fades
		w := maxWidth * camera.Zoom * (0.3 + 0.7*float32(i)/float32(n))

		x1, y1 := camera.WorldToScreen(history[i].X, history[i].Y)
		x2, y2 := camera.WorldToScreen(history[i+1].X, history[i+1].Y)

		drawPathSegment(gtx, x1, y1, x2, y2, w, col)
	}
}

func drawPathSegment(gtx layout.Context, x1, y1, x2, y2, width float32, col color.NRGBA) {
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

// DrawFuturePath draws the upcoming path in a dimmer color.
func DrawFuturePath(gtx layout.Context, fullPath core.Path, currentTime float64, ws *core.Workspace, camera *interact.Camera, col color.NRGBA) {
	if len(fullPath) < 2 {
		return
	}

	// Find current segment
	startIdx := 0
	for i, tv := range fullPath {
		if tv.T > currentTime {
			startIdx = i
			break
		}
	}

	if startIdx == 0 {
		startIdx = 1 // At least draw from second point
	}

	// Collect future positions
	var positions []core.Pos
	for i := startIdx; i < len(fullPath); i++ {
		if v, ok := ws.Vertices[fullPath[i].V]; ok {
			positions = append(positions, v.Pos)
		}
	}

	// Draw with dashed/dimmer appearance
	dimCol := col
	dimCol.A = 80

	DrawPath(gtx, positions, camera, dimCol, 1.5)
}

// DrawTimedPath draws a path with time annotations at vertices.
func DrawTimedPath(gtx layout.Context, path core.Path, ws *core.Workspace, camera *interact.Camera, col color.NRGBA) {
	if len(path) == 0 {
		return
	}

	// Collect positions
	var positions []core.Pos
	for _, tv := range path {
		if v, ok := ws.Vertices[tv.V]; ok {
			positions = append(positions, v.Pos)
		}
	}

	DrawPath(gtx, positions, camera, col, 2)

	// Draw time markers
	markerCol := col
	markerCol.A = 200
	for i, tv := range path {
		if v, ok := ws.Vertices[tv.V]; ok {
			x, y := camera.WorldToScreen(v.Pos.X, v.Pos.Y)
			radius := float32(4) * camera.Zoom

			// Skip intermediate waypoints for cleaner look
			if i > 0 && i < len(path)-1 {
				continue
			}

			drawFilledCircle(gtx, x, y, radius, markerCol)
		}
	}
}

// DrawAllPaths draws all robot paths.
func DrawAllPaths(gtx layout.Context, paths map[core.RobotID]core.Path, robots []*core.Robot, ws *core.Workspace, camera *interact.Camera) {
	for _, robot := range robots {
		path := paths[robot.ID]
		if len(path) == 0 {
			continue
		}

		col := RobotColor(robot.Type)
		col.A = 100

		DrawTimedPath(gtx, path, ws, camera, col)
	}
}

// DrawPathWithArrows draws a path with direction arrows.
func DrawPathWithArrows(gtx layout.Context, positions []core.Pos, camera *interact.Camera, col color.NRGBA) {
	if len(positions) < 2 {
		return
	}

	DrawPath(gtx, positions, camera, col, 2)

	// Draw arrows at midpoints
	for i := 0; i < len(positions)-1; i++ {
		midX := (positions[i].X + positions[i+1].X) / 2
		midY := (positions[i].Y + positions[i+1].Y) / 2

		dx := positions[i+1].X - positions[i].X
		dy := positions[i+1].Y - positions[i].Y
		length := math.Sqrt(dx*dx + dy*dy)
		if length < 5 {
			continue
		}

		dx /= length
		dy /= length

		drawArrow(gtx, midX, midY, dx, dy, camera, col)
	}
}

func drawArrow(gtx layout.Context, x, y, dirX, dirY float64, camera *interact.Camera, col color.NRGBA) {
	screenX, screenY := camera.WorldToScreen(x, y)
	size := float32(6) * camera.Zoom

	// Arrow head points
	tipX := screenX + float32(dirX)*size
	tipY := screenY + float32(dirY)*size

	// Perpendicular
	perpX := -float32(dirY) * size * 0.5
	perpY := float32(dirX) * size * 0.5

	baseX := screenX - float32(dirX)*size*0.3
	baseY := screenY - float32(dirY)*size*0.3

	var path clip.Path
	path.Begin(gtx.Ops)
	path.MoveTo(f32.Pt(tipX, tipY))
	path.LineTo(f32.Pt(baseX+perpX, baseY+perpY))
	path.LineTo(f32.Pt(baseX-perpX, baseY-perpY))
	path.Close()

	paint.FillShape(gtx.Ops, col, clip.Outline{Path: path.End()}.Op())
}

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

// Robot colors by type
var (
	ColorRobotTypeA = color.NRGBA{R: 100, G: 200, B: 255, A: 255} // Cyan - mobile
	ColorRobotTypeB = color.NRGBA{R: 255, G: 150, B: 100, A: 255} // Orange - rail
	ColorRobotTypeC = color.NRGBA{R: 200, G: 100, B: 255, A: 255} // Purple - drone
	ColorRobotSelected = color.NRGBA{R: 255, G: 255, B: 100, A: 255}
)

// RobotColor returns the color for a robot type.
func RobotColor(t core.RobotType) color.NRGBA {
	switch t {
	case core.TypeA:
		return ColorRobotTypeA
	case core.TypeB:
		return ColorRobotTypeB
	case core.TypeC:
		return ColorRobotTypeC
	default:
		return ColorRobotTypeA
	}
}

// DrawRobot draws a robot at the given position.
func DrawRobot(gtx layout.Context, pos core.Pos, robot *core.Robot, camera *interact.Camera, selected bool) {
	screenX, screenY := camera.WorldToScreen(pos.X, pos.Y)
	size := float32(14) * camera.Zoom

	col := RobotColor(robot.Type)
	if selected {
		col = ColorRobotSelected
	}

	switch robot.Type {
	case core.TypeA:
		// Square for TypeA (mobile holonomic)
		drawSquare(gtx, screenX, screenY, size, col)
	case core.TypeB:
		// Rectangle for TypeB (rail-mounted)
		drawRectangle(gtx, screenX, screenY, size*1.5, size*0.8, col)
	case core.TypeC:
		// Quadcopter shape for TypeC (drone)
		drawQuadcopter(gtx, screenX, screenY, size, col)
	}

	// Draw robot ID
	drawRobotID(gtx, screenX, screenY-size-4, int(robot.ID), col)
}

func drawSquare(gtx layout.Context, cx, cy, size float32, col color.NRGBA) {
	halfSize := size / 2
	var path clip.Path
	path.Begin(gtx.Ops)
	path.MoveTo(f32.Pt(cx-halfSize, cy-halfSize))
	path.LineTo(f32.Pt(cx+halfSize, cy-halfSize))
	path.LineTo(f32.Pt(cx+halfSize, cy+halfSize))
	path.LineTo(f32.Pt(cx-halfSize, cy+halfSize))
	path.Close()

	paint.FillShape(gtx.Ops, col, clip.Outline{Path: path.End()}.Op())
}

func drawRectangle(gtx layout.Context, cx, cy, width, height float32, col color.NRGBA) {
	halfW := width / 2
	halfH := height / 2
	var path clip.Path
	path.Begin(gtx.Ops)
	path.MoveTo(f32.Pt(cx-halfW, cy-halfH))
	path.LineTo(f32.Pt(cx+halfW, cy-halfH))
	path.LineTo(f32.Pt(cx+halfW, cy+halfH))
	path.LineTo(f32.Pt(cx-halfW, cy+halfH))
	path.Close()

	paint.FillShape(gtx.Ops, col, clip.Outline{Path: path.End()}.Op())
}

func drawQuadcopter(gtx layout.Context, cx, cy, size float32, col color.NRGBA) {
	armLen := size * 0.7
	rotorR := size * 0.3

	// Draw arms (X shape)
	for _, angle := range []float64{45, 135, 225, 315} {
		rad := angle * math.Pi / 180
		dx := float32(math.Cos(rad)) * armLen
		dy := float32(math.Sin(rad)) * armLen

		// Arm line
		drawLine(gtx, cx, cy, cx+dx, cy+dy, 2, col)

		// Rotor circle
		drawFilledCircle(gtx, cx+dx, cy+dy, rotorR, col)
	}

	// Center body
	drawFilledCircle(gtx, cx, cy, size*0.25, col)
}

func drawLine(gtx layout.Context, x1, y1, x2, y2, width float32, col color.NRGBA) {
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

func drawFilledCircle(gtx layout.Context, cx, cy, radius float32, col color.NRGBA) {
	var path clip.Path
	path.Begin(gtx.Ops)
	path.Move(f32.Pt(cx+radius, cy))

	segments := 12
	for i := 1; i <= segments; i++ {
		angle := float64(i) * 2 * math.Pi / float64(segments)
		x := cx + radius*float32(math.Cos(angle))
		y := cy + radius*float32(math.Sin(angle))
		path.Line(f32.Pt(x-path.Pos().X, y-path.Pos().Y))
	}
	path.Close()

	paint.FillShape(gtx.Ops, col, clip.Outline{Path: path.End()}.Op())
}

func drawRobotID(gtx layout.Context, cx, cy float32, id int, col color.NRGBA) {
	// Simple number rendering using dots
	// Just draw a small indicator for now - proper text rendering would need material theme
	radius := float32(3)
	drawFilledCircle(gtx, cx, cy, radius, col)
}

// DrawRobots draws all robots at their current positions.
func DrawRobots(gtx layout.Context, robots []*core.Robot, positions map[core.RobotID]core.Pos, camera *interact.Camera, selected map[core.RobotID]bool) {
	for _, robot := range robots {
		pos, ok := positions[robot.ID]
		if !ok {
			continue
		}
		DrawRobot(gtx, pos, robot, camera, selected[robot.ID])
	}
}

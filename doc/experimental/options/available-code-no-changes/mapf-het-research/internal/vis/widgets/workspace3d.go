package widgets

import (
	"image"
	"image/color"
	"math"

	"gioui.org/f32"
	"gioui.org/io/event"
	"gioui.org/io/key"
	"gioui.org/io/pointer"
	"gioui.org/layout"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/unit"
	"gioui.org/widget/material"

	"github.com/elektrokombinacija/mapf-het-research/internal/core"
	"github.com/elektrokombinacija/mapf-het-research/internal/vis/draw"
	"github.com/elektrokombinacija/mapf-het-research/internal/vis/interact"
	"github.com/elektrokombinacija/mapf-het-research/internal/vis/state"
)

// Workspace3D provides an isometric 3D view of the workspace.
type Workspace3D struct {
	state  *state.State
	camera *interact.Camera

	// Isometric projection parameters
	isoAngle float64 // Rotation around Z axis
	isoTilt  float64 // Tilt angle
	scale    float32

	// Layer visibility
	layerVisible map[core.AirspaceLayer]bool
}

// NewWorkspace3D creates a new 3D workspace widget.
func NewWorkspace3D(st *state.State, camera *interact.Camera) *Workspace3D {
	return &Workspace3D{
		state:    st,
		camera:   camera,
		isoAngle: 45 * math.Pi / 180,
		isoTilt:  30 * math.Pi / 180,
		scale:    1.0,
		layerVisible: map[core.AirspaceLayer]bool{
			core.LayerGround: true,
			core.Layer1:      true,
			core.Layer2:      true,
			core.Layer3:      true,
		},
	}
}

// Layout renders the 3D workspace.
func (w *Workspace3D) Layout(gtx layout.Context, th *material.Theme) layout.Dimensions {
	bounds := gtx.Constraints.Max
	defer clip.Rect(image.Rect(0, 0, bounds.X, bounds.Y)).Push(gtx.Ops).Pop()

	// Background
	paint.Fill(gtx.Ops, color.NRGBA{R: 20, G: 23, B: 28, A: 255})

	// Handle pointer events
	w.handlePointerEvents(gtx)

	// Draw layers from bottom to top
	if w.state.Instance == nil {
		return layout.Dimensions{Size: bounds}
	}

	// Center of screen
	centerX := float32(bounds.X) / 2
	centerY := float32(bounds.Y) / 2

	// Draw grid for each layer
	for _, layer := range []core.AirspaceLayer{core.LayerGround, core.Layer1, core.Layer2, core.Layer3} {
		if !w.layerVisible[layer] {
			continue
		}
		w.drawLayer(gtx, layer, centerX, centerY)
	}

	// Draw vertical corridors
	w.drawVerticalCorridors(gtx, centerX, centerY)

	// Draw robots
	positions := w.state.CurrentPositions()
	for _, robot := range w.state.Instance.Robots {
		pos := positions[robot.ID]
		screenX, screenY := w.worldToScreen(pos.X, pos.Y, pos.Z, centerX, centerY)
		w.drawRobot3D(gtx, screenX, screenY, robot)
	}

	// Draw layer legend
	w.drawLayerLegend(gtx, th, bounds)

	return layout.Dimensions{Size: bounds}
}

// worldToScreen converts 3D world coordinates to isometric screen coordinates.
func (w *Workspace3D) worldToScreen(x, y, z float64, centerX, centerY float32) (screenX, screenY float32) {
	// Apply isometric projection
	// Rotate around Z axis
	cosA := math.Cos(w.isoAngle)
	sinA := math.Sin(w.isoAngle)
	rx := x*cosA - y*sinA
	ry := x*sinA + y*cosA

	// Apply tilt (project onto screen plane)
	cosT := math.Cos(w.isoTilt)
	sinT := math.Sin(w.isoTilt)

	// Isometric formula
	screenX = centerX + float32(rx)*w.scale*w.camera.Zoom
	screenY = centerY + float32(ry*cosT-z*sinT)*w.scale*w.camera.Zoom

	// Apply camera offset
	screenX += w.camera.OffsetX - 100
	screenY += w.camera.OffsetY - 100

	return
}

func (w *Workspace3D) drawLayer(gtx layout.Context, layer core.AirspaceLayer, centerX, centerY float32) {
	ws := w.state.Instance.Workspace
	z := layer.Height()

	// Layer color based on altitude
	alpha := uint8(150 - int(z)*5)
	if alpha < 50 {
		alpha = 50
	}

	layerColors := map[core.AirspaceLayer]color.NRGBA{
		core.LayerGround: {R: 80, G: 100, B: 80, A: alpha},
		core.Layer1:      {R: 80, G: 120, B: 150, A: alpha},
		core.Layer2:      {R: 100, G: 130, B: 180, A: alpha},
		core.Layer3:      {R: 120, G: 140, B: 200, A: alpha},
	}

	col := layerColors[layer]

	// Draw edges at this layer
	for vid, edges := range ws.Edges {
		v1 := ws.Vertices[vid]
		if v1 == nil || v1.Layer != layer {
			continue
		}

		for _, edge := range edges {
			v2 := ws.Vertices[edge.To]
			if v2 == nil || v2.Layer != layer {
				continue
			}

			// Only draw each edge once
			if vid > edge.To {
				continue
			}

			x1, y1 := w.worldToScreen(v1.Pos.X, v1.Pos.Y, z, centerX, centerY)
			x2, y2 := w.worldToScreen(v2.Pos.X, v2.Pos.Y, z, centerX, centerY)

			w.drawLine3D(gtx, x1, y1, x2, y2, 1.5, col)
		}
	}

	// Draw vertices at this layer
	vertexCol := col
	vertexCol.A = 200

	for _, v := range ws.Vertices {
		if v.Layer != layer {
			continue
		}

		screenX, screenY := w.worldToScreen(v.Pos.X, v.Pos.Y, z, centerX, centerY)

		radius := float32(4) * w.camera.Zoom
		if v.IsPad {
			radius = 6 * w.camera.Zoom
			vertexCol = color.NRGBA{R: 80, G: 200, B: 100, A: 220}
		} else if v.IsCorridor {
			vertexCol = color.NRGBA{R: 100, G: 150, B: 220, A: 220}
		}

		w.drawCircle(gtx, screenX, screenY, radius, vertexCol)
	}
}

func (w *Workspace3D) drawVerticalCorridors(gtx layout.Context, centerX, centerY float32) {
	ws := w.state.Instance.Workspace

	// Find corridor vertices and draw vertical lines
	for _, v := range ws.Vertices {
		if !v.IsCorridor || v.Layer != core.LayerGround {
			continue
		}

		// Draw vertical line through all layers
		for i := 0; i < 3; i++ {
			z1 := float64(i * 5)
			z2 := float64((i + 1) * 5)

			x1, y1 := w.worldToScreen(v.Pos.X, v.Pos.Y, z1, centerX, centerY)
			x2, y2 := w.worldToScreen(v.Pos.X, v.Pos.Y, z2, centerX, centerY)

			col := color.NRGBA{R: 100, G: 180, B: 255, A: 150}
			w.drawLine3D(gtx, x1, y1, x2, y2, 2, col)
		}
	}
}

func (w *Workspace3D) drawRobot3D(gtx layout.Context, screenX, screenY float32, robot *core.Robot) {
	col := draw.RobotColor(robot.Type)
	size := float32(10) * w.camera.Zoom

	switch robot.Type {
	case core.TypeC:
		// Drone - draw with altitude indicator
		w.drawDrone3D(gtx, screenX, screenY, size, col)
	default:
		// Ground robots
		w.drawCircle(gtx, screenX, screenY, size, col)
	}
}

func (w *Workspace3D) drawDrone3D(gtx layout.Context, cx, cy, size float32, col color.NRGBA) {
	// Draw quadcopter shape with shadow
	armLen := size * 0.6
	rotorR := size * 0.25

	// Shadow (slightly offset)
	shadowCol := color.NRGBA{R: 0, G: 0, B: 0, A: 50}
	shadowOff := float32(3)
	w.drawCircle(gtx, cx+shadowOff, cy+shadowOff*0.5, size*0.4, shadowCol)

	// Arms and rotors
	for _, angle := range []float64{45, 135, 225, 315} {
		rad := angle * math.Pi / 180
		dx := float32(math.Cos(rad)) * armLen
		dy := float32(math.Sin(rad)) * armLen * 0.5 // Foreshorten for isometric

		// Arm
		w.drawLine3D(gtx, cx, cy, cx+dx, cy+dy, 2, col)

		// Rotor
		w.drawCircle(gtx, cx+dx, cy+dy, rotorR, col)
	}

	// Body
	w.drawCircle(gtx, cx, cy, size*0.2, col)
}

func (w *Workspace3D) drawCircle(gtx layout.Context, cx, cy, radius float32, col color.NRGBA) {
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

func (w *Workspace3D) drawLine3D(gtx layout.Context, x1, y1, x2, y2, width float32, col color.NRGBA) {
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

func (w *Workspace3D) drawLayerLegend(gtx layout.Context, th *material.Theme, bounds image.Point) {
	layout.Inset{Right: unit.Dp(10), Bottom: unit.Dp(10)}.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		return layout.SE.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
			return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
				layout.Rigid(func(gtx layout.Context) layout.Dimensions {
					return w.legendItem(gtx, th, "Ground (0m)", core.LayerGround)
				}),
				layout.Rigid(func(gtx layout.Context) layout.Dimensions {
					return w.legendItem(gtx, th, "Layer 1 (5m)", core.Layer1)
				}),
				layout.Rigid(func(gtx layout.Context) layout.Dimensions {
					return w.legendItem(gtx, th, "Layer 2 (10m)", core.Layer2)
				}),
				layout.Rigid(func(gtx layout.Context) layout.Dimensions {
					return w.legendItem(gtx, th, "Layer 3 (15m)", core.Layer3)
				}),
			)
		})
	})
	_ = bounds
}

func (w *Workspace3D) legendItem(gtx layout.Context, th *material.Theme, text string, layer core.AirspaceLayer) layout.Dimensions {
	visible := w.layerVisible[layer]
	alpha := uint8(200)
	if !visible {
		alpha = 80
	}

	label := material.Label(th, 10, text)
	label.Color = color.NRGBA{R: 180, G: 180, B: 180, A: alpha}
	return label.Layout(gtx)
}

func (w *Workspace3D) handlePointerEvents(gtx layout.Context) {
	bounds := gtx.Constraints.Max
	area := clip.Rect(image.Rect(0, 0, bounds.X, bounds.Y)).Push(gtx.Ops)
	event.Op(gtx.Ops, w)
	area.Pop()

	for {
		ev, ok := gtx.Event(pointer.Filter{
			Target: w,
			Kinds:  pointer.Drag | pointer.Scroll,
		})
		if !ok {
			break
		}
		if pe, ok := ev.(pointer.Event); ok {
			switch pe.Kind {
			case pointer.Scroll:
				// Check for shift key for tilt control
				shiftPressed := false
				for {
					ke, ok := gtx.Event(key.Filter{Optional: key.ModShift})
					if !ok {
						break
					}
					if _, ok := ke.(key.Event); ok {
						shiftPressed = true
					}
				}

				if shiftPressed {
					w.isoTilt += float64(pe.Scroll.Y) * 0.01
					if w.isoTilt < 0.1 {
						w.isoTilt = 0.1
					}
					if w.isoTilt > math.Pi/2-0.1 {
						w.isoTilt = math.Pi/2 - 0.1
					}
				} else {
					w.isoAngle += float64(pe.Scroll.X) * 0.01
				}
			}
			// Camera pan handled by embedded camera
			w.camera.HandleEvent(gtx, pe)
		}
	}
}

// ToggleLayer toggles visibility of a layer.
func (w *Workspace3D) ToggleLayer(layer core.AirspaceLayer) {
	w.layerVisible[layer] = !w.layerVisible[layer]
}

package widgets

import (
	"fmt"
	"image"
	"image/color"
	"math"

	"gioui.org/f32"
	"gioui.org/io/event"
	"gioui.org/io/pointer"
	"gioui.org/layout"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/unit"
	"gioui.org/widget/material"

	"github.com/elektrokombinacija/mapf-het-research/internal/vis/state"
)

// CBSTree visualizes the CBS constraint tree.
type CBSTree struct {
	state        *state.State
	selectedNode int
	scrollY      float32
}

// NewCBSTree creates a new CBS tree widget.
func NewCBSTree(st *state.State) *CBSTree {
	return &CBSTree{
		state:        st,
		selectedNode: -1,
	}
}

// Colors for tree nodes
var (
	ColorNodeOpen     = color.NRGBA{R: 100, G: 150, B: 200, A: 255}
	ColorNodeClosed   = color.NRGBA{R: 80, G: 100, B: 130, A: 255}
	ColorNodeCurrent  = color.NRGBA{R: 255, G: 200, B: 80, A: 255}
	ColorNodeSolution = color.NRGBA{R: 80, G: 200, B: 120, A: 255}
	ColorNodeSelected = color.NRGBA{R: 255, G: 255, B: 150, A: 255}
	ColorTreeEdge     = color.NRGBA{R: 70, G: 80, B: 90, A: 255}
)

// Layout renders the CBS tree.
func (t *CBSTree) Layout(gtx layout.Context, th *material.Theme) layout.Dimensions {
	width := 300
	height := gtx.Constraints.Max.Y

	// Background
	rect := image.Rect(0, 0, width, height)
	paint.FillShape(gtx.Ops, color.NRGBA{R: 35, G: 38, B: 42, A: 255}, clip.Rect(rect).Op())

	// Handle pointer events
	t.handlePointerEvents(gtx, width, height)

	// Draw header
	layout.Inset{Left: unit.Dp(10), Top: unit.Dp(8)}.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		label := material.Label(th, 14, "CBS Constraint Tree")
		label.Color = color.NRGBA{R: 200, G: 200, B: 200, A: 255}
		return label.Layout(gtx)
	})

	// Draw tree
	nodes := t.state.Algo.GetNodes()
	if len(nodes) > 0 {
		t.drawTree(gtx, th, nodes, width, height)
	}

	// Draw stats
	t.drawStats(gtx, th, width, height)

	return layout.Dimensions{Size: image.Point{X: width, Y: height}}
}

func (t *CBSTree) drawTree(gtx layout.Context, th *material.Theme, nodes []*state.CBSNodeInfo, width, height int) {
	if len(nodes) == 0 {
		return
	}

	// Calculate tree layout
	nodePositions := t.calculateTreeLayout(nodes, width)

	// Apply scroll offset
	offsetY := 40 - t.scrollY

	// Draw edges first
	for _, node := range nodes {
		if node.ParentID >= 0 && node.ParentID < len(nodes) {
			parent := nodes[node.ParentID]
			parentPos := nodePositions[parent.ID]
			childPos := nodePositions[node.ID]

			t.drawTreeEdge(gtx,
				float32(parentPos.X), float32(parentPos.Y)+offsetY,
				float32(childPos.X), float32(childPos.Y)+offsetY)
		}
	}

	// Draw nodes
	currentNode := t.state.Algo.GetCurrentNode()
	for _, node := range nodes {
		pos := nodePositions[node.ID]
		x := float32(pos.X)
		y := float32(pos.Y) + offsetY

		// Skip if off-screen
		if y < 20 || y > float32(height) {
			continue
		}

		col := t.nodeColor(node, currentNode)
		t.drawTreeNode(gtx, th, x, y, node, col)
	}
}

type nodePos struct {
	X, Y int
}

func (t *CBSTree) calculateTreeLayout(nodes []*state.CBSNodeInfo, width int) map[int]nodePos {
	positions := make(map[int]nodePos)

	if len(nodes) == 0 {
		return positions
	}

	// Build level mapping
	levels := make(map[int][]int) // level -> node IDs
	nodeLevel := make(map[int]int)

	// BFS to assign levels
	nodeLevel[0] = 0
	levels[0] = []int{0}

	for _, node := range nodes {
		if node.ParentID >= 0 {
			parentLevel := nodeLevel[node.ParentID]
			level := parentLevel + 1
			nodeLevel[node.ID] = level
			levels[level] = append(levels[level], node.ID)
		}
	}

	// Position nodes
	nodeRadius := 12
	levelHeight := 50
	marginX := 30

	for level, nodeIDs := range levels {
		n := len(nodeIDs)
		availWidth := width - 2*marginX

		for i, nodeID := range nodeIDs {
			x := marginX + (availWidth*(2*i+1))/(2*n)
			y := 30 + level*levelHeight

			positions[nodeID] = nodePos{X: x, Y: y}
		}
		_ = nodeRadius
	}

	return positions
}

func (t *CBSTree) nodeColor(node *state.CBSNodeInfo, currentNode int) color.NRGBA {
	if node.ID == t.selectedNode {
		return ColorNodeSelected
	}
	if node.IsSolution {
		return ColorNodeSolution
	}
	if node.ID == currentNode {
		return ColorNodeCurrent
	}
	if node.IsOpen {
		return ColorNodeOpen
	}
	return ColorNodeClosed
}

func (t *CBSTree) drawTreeNode(gtx layout.Context, th *material.Theme, x, y float32, node *state.CBSNodeInfo, col color.NRGBA) {
	radius := float32(10)

	// Draw circle
	var path clip.Path
	path.Begin(gtx.Ops)
	path.Move(f32.Pt(x+radius, y))

	segments := 12
	for i := 1; i <= segments; i++ {
		angle := float64(i) * 2 * math.Pi / float64(segments)
		px := x + radius*float32(math.Cos(angle))
		py := y + radius*float32(math.Sin(angle))
		path.Line(f32.Pt(px-path.Pos().X, py-path.Pos().Y))
	}
	path.Close()

	paint.FillShape(gtx.Ops, col, clip.Outline{Path: path.End()}.Op())

	// Draw node ID - use simple offset positioning
	_ = th
	_ = node
}

func (t *CBSTree) drawTreeEdge(gtx layout.Context, x1, y1, x2, y2 float32) {
	dx := x2 - x1
	dy := y2 - y1
	length := float32(math.Sqrt(float64(dx*dx + dy*dy)))
	if length < 1 {
		return
	}

	dx /= length
	dy /= length
	width := float32(2)
	px := -dy * width / 2
	py := dx * width / 2

	var path clip.Path
	path.Begin(gtx.Ops)
	path.MoveTo(f32.Pt(x1+px, y1+py))
	path.LineTo(f32.Pt(x2+px, y2+py))
	path.LineTo(f32.Pt(x2-px, y2-py))
	path.LineTo(f32.Pt(x1-px, y1-py))
	path.Close()

	paint.FillShape(gtx.Ops, ColorTreeEdge, clip.Outline{Path: path.End()}.Op())
}

func (t *CBSTree) drawStats(gtx layout.Context, th *material.Theme, width, height int) {
	layout.Inset{Left: unit.Dp(10), Bottom: unit.Dp(20)}.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
		return layout.S.Layout(gtx, func(gtx layout.Context) layout.Dimensions {
			return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
				layout.Rigid(func(gtx layout.Context) layout.Dimensions {
					label := material.Label(th, 11, fmt.Sprintf("Nodes expanded: %d", t.state.Algo.NodesExpanded))
					label.Color = color.NRGBA{R: 150, G: 150, B: 150, A: 255}
					return label.Layout(gtx)
				}),
				layout.Rigid(func(gtx layout.Context) layout.Dimensions {
					label := material.Label(th, 11, fmt.Sprintf("Conflicts found: %d", t.state.Algo.ConflictsFound))
					label.Color = color.NRGBA{R: 150, G: 150, B: 150, A: 255}
					return label.Layout(gtx)
				}),
				layout.Rigid(func(gtx layout.Context) layout.Dimensions {
					openCount := len(t.state.Algo.OpenSet)
					label := material.Label(th, 11, fmt.Sprintf("Open set: %d", openCount))
					label.Color = color.NRGBA{R: 150, G: 150, B: 150, A: 255}
					return label.Layout(gtx)
				}),
			)
		})
	})
	_ = width
	_ = height
}

func (t *CBSTree) handlePointerEvents(gtx layout.Context, width, height int) {
	area := clip.Rect(image.Rect(0, 0, width, height)).Push(gtx.Ops)
	event.Op(gtx.Ops, t)
	area.Pop()

	for {
		ev, ok := gtx.Event(pointer.Filter{
			Target: t,
			Kinds:  pointer.Scroll | pointer.Press,
		})
		if !ok {
			break
		}
		if pe, ok := ev.(pointer.Event); ok {
			switch pe.Kind {
			case pointer.Scroll:
				t.scrollY += pe.Scroll.Y
				if t.scrollY < 0 {
					t.scrollY = 0
				}
			case pointer.Press:
				// Could implement node selection here
			}
		}
	}
}

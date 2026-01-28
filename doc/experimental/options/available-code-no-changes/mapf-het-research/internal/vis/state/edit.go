package state

import (
	"github.com/elektrokombinacija/mapf-het-research/internal/core"
)

// EditAction represents an undoable edit action.
type EditAction interface {
	Do(inst *core.Instance)
	Undo(inst *core.Instance)
	Description() string
}

// EditState manages interactive editing state.
type EditState struct {
	// Selection
	SelectedVertices map[core.VertexID]bool
	SelectedRobots   map[core.RobotID]bool

	// Drag state
	Dragging    bool
	DragVertex  core.VertexID
	DragStartX  float64
	DragStartY  float64

	// Undo/redo stacks
	undoStack []EditAction
	redoStack []EditAction

	// Mode
	Mode EditMode
}

// EditMode represents the current editing mode.
type EditMode int

const (
	ModeView EditMode = iota
	ModeSelect
	ModeDrag
	ModeAddVertex
	ModeAddEdge
	ModeDelete
)

// NewEditState creates a new edit state.
func NewEditState() *EditState {
	return &EditState{
		SelectedVertices: make(map[core.VertexID]bool),
		SelectedRobots:   make(map[core.RobotID]bool),
		undoStack:        make([]EditAction, 0),
		redoStack:        make([]EditAction, 0),
		Mode:             ModeView,
	}
}

// SelectVertex toggles vertex selection.
func (e *EditState) SelectVertex(id core.VertexID, multi bool) {
	if !multi {
		e.ClearSelection()
	}
	e.SelectedVertices[id] = !e.SelectedVertices[id]
	if !e.SelectedVertices[id] {
		delete(e.SelectedVertices, id)
	}
}

// SelectRobot toggles robot selection.
func (e *EditState) SelectRobot(id core.RobotID, multi bool) {
	if !multi {
		e.ClearSelection()
	}
	e.SelectedRobots[id] = !e.SelectedRobots[id]
	if !e.SelectedRobots[id] {
		delete(e.SelectedRobots, id)
	}
}

// ClearSelection clears all selections.
func (e *EditState) ClearSelection() {
	e.SelectedVertices = make(map[core.VertexID]bool)
	e.SelectedRobots = make(map[core.RobotID]bool)
}

// IsVertexSelected checks if vertex is selected.
func (e *EditState) IsVertexSelected(id core.VertexID) bool {
	return e.SelectedVertices[id]
}

// IsRobotSelected checks if robot is selected.
func (e *EditState) IsRobotSelected(id core.RobotID) bool {
	return e.SelectedRobots[id]
}

// StartDrag begins a drag operation.
func (e *EditState) StartDrag(v core.VertexID, x, y float64) {
	e.Dragging = true
	e.DragVertex = v
	e.DragStartX = x
	e.DragStartY = y
}

// EndDrag ends a drag operation.
func (e *EditState) EndDrag() {
	e.Dragging = false
}

// Execute performs an action and adds it to undo stack.
func (e *EditState) Execute(action EditAction, inst *core.Instance) {
	action.Do(inst)
	e.undoStack = append(e.undoStack, action)
	e.redoStack = nil // Clear redo stack on new action
}

// Undo undoes the last action.
func (e *EditState) Undo() EditAction {
	if len(e.undoStack) == 0 {
		return nil
	}
	action := e.undoStack[len(e.undoStack)-1]
	e.undoStack = e.undoStack[:len(e.undoStack)-1]
	e.redoStack = append(e.redoStack, action)
	return action
}

// Redo redoes the last undone action.
func (e *EditState) Redo() EditAction {
	if len(e.redoStack) == 0 {
		return nil
	}
	action := e.redoStack[len(e.redoStack)-1]
	e.redoStack = e.redoStack[:len(e.redoStack)-1]
	e.undoStack = append(e.undoStack, action)
	return action
}

// CanUndo returns true if there are actions to undo.
func (e *EditState) CanUndo() bool {
	return len(e.undoStack) > 0
}

// CanRedo returns true if there are actions to redo.
func (e *EditState) CanRedo() bool {
	return len(e.redoStack) > 0
}

// MoveVertexAction is an edit action for moving a vertex.
type MoveVertexAction struct {
	VertexID core.VertexID
	OldX     float64
	OldY     float64
	NewX     float64
	NewY     float64
}

func (a *MoveVertexAction) Do(inst *core.Instance) {
	if v := inst.Workspace.Vertices[a.VertexID]; v != nil {
		v.Pos.X = a.NewX
		v.Pos.Y = a.NewY
	}
}

func (a *MoveVertexAction) Undo(inst *core.Instance) {
	if v := inst.Workspace.Vertices[a.VertexID]; v != nil {
		v.Pos.X = a.OldX
		v.Pos.Y = a.OldY
	}
}

func (a *MoveVertexAction) Description() string {
	return "Move vertex"
}

// AddVertexAction is an edit action for adding a vertex.
type AddVertexAction struct {
	Vertex *core.Vertex
}

func (a *AddVertexAction) Do(inst *core.Instance) {
	inst.Workspace.AddVertex(a.Vertex)
}

func (a *AddVertexAction) Undo(inst *core.Instance) {
	delete(inst.Workspace.Vertices, a.Vertex.ID)
	delete(inst.Workspace.Edges, a.Vertex.ID)
	// Remove edges from other vertices to this one
	for vid := range inst.Workspace.Edges {
		edges := inst.Workspace.Edges[vid]
		filtered := edges[:0]
		for _, e := range edges {
			if e.To != a.Vertex.ID {
				filtered = append(filtered, e)
			}
		}
		inst.Workspace.Edges[vid] = filtered
	}
}

func (a *AddVertexAction) Description() string {
	return "Add vertex"
}

// AddEdgeAction is an edit action for adding an edge.
type AddEdgeAction struct {
	From   core.VertexID
	To     core.VertexID
	Length float64
}

func (a *AddEdgeAction) Do(inst *core.Instance) {
	inst.Workspace.AddEdgeWithLength(a.From, a.To, a.Length)
}

func (a *AddEdgeAction) Undo(inst *core.Instance) {
	// Remove edge from->to
	edges := inst.Workspace.Edges[a.From]
	filtered := edges[:0]
	for _, e := range edges {
		if e.To != a.To {
			filtered = append(filtered, e)
		}
	}
	inst.Workspace.Edges[a.From] = filtered

	// Remove edge to->from
	edges = inst.Workspace.Edges[a.To]
	filtered = edges[:0]
	for _, e := range edges {
		if e.To != a.From {
			filtered = append(filtered, e)
		}
	}
	inst.Workspace.Edges[a.To] = filtered
}

func (a *AddEdgeAction) Description() string {
	return "Add edge"
}

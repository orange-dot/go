// Package interact handles user interactions like pan, zoom, and selection.
package interact

import (
	"gioui.org/io/pointer"
	"gioui.org/layout"
)

// Camera manages view transformation (pan and zoom).
type Camera struct {
	// View transform
	OffsetX float32 // Pan offset in screen pixels
	OffsetY float32
	Zoom    float32 // Zoom level (1.0 = 100%)

	// Interaction state
	dragging   bool
	dragStartX float32
	dragStartY float32
	lastX      float32
	lastY      float32
}

// NewCamera creates a new camera with default settings.
func NewCamera() *Camera {
	return &Camera{
		OffsetX: 100,
		OffsetY: 100,
		Zoom:    1.0,
	}
}

// Reset resets camera to default view.
func (c *Camera) Reset() {
	c.OffsetX = 100
	c.OffsetY = 100
	c.Zoom = 1.0
}

// WorldToScreen converts world coordinates to screen coordinates.
func (c *Camera) WorldToScreen(worldX, worldY float64) (screenX, screenY float32) {
	screenX = float32(worldX)*c.Zoom + c.OffsetX
	screenY = float32(worldY)*c.Zoom + c.OffsetY
	return
}

// ScreenToWorld converts screen coordinates to world coordinates.
func (c *Camera) ScreenToWorld(screenX, screenY float32) (worldX, worldY float64) {
	worldX = float64((screenX - c.OffsetX) / c.Zoom)
	worldY = float64((screenY - c.OffsetY) / c.Zoom)
	return
}

// HandleEvent processes pointer events for pan and zoom.
func (c *Camera) HandleEvent(gtx layout.Context, ev pointer.Event) {
	switch ev.Kind {
	case pointer.Press:
		if ev.Buttons.Contain(pointer.ButtonSecondary) || ev.Buttons.Contain(pointer.ButtonTertiary) {
			c.dragging = true
			c.dragStartX = ev.Position.X
			c.dragStartY = ev.Position.Y
		}
		c.lastX = ev.Position.X
		c.lastY = ev.Position.Y

	case pointer.Drag:
		if c.dragging {
			dx := ev.Position.X - c.lastX
			dy := ev.Position.Y - c.lastY
			c.OffsetX += dx
			c.OffsetY += dy
		}
		c.lastX = ev.Position.X
		c.lastY = ev.Position.Y

	case pointer.Release:
		c.dragging = false

	case pointer.Scroll:
		// Zoom centered on mouse position
		scrollY := ev.Scroll.Y
		if scrollY != 0 {
			// Calculate world position under mouse before zoom
			worldX, worldY := c.ScreenToWorld(ev.Position.X, ev.Position.Y)

			// Apply zoom
			zoomFactor := float32(1.1)
			if scrollY > 0 {
				c.Zoom /= zoomFactor
			} else {
				c.Zoom *= zoomFactor
			}

			// Clamp zoom
			if c.Zoom < 0.1 {
				c.Zoom = 0.1
			}
			if c.Zoom > 10 {
				c.Zoom = 10
			}

			// Adjust offset to keep world point under mouse
			newScreenX, newScreenY := c.WorldToScreen(worldX, worldY)
			c.OffsetX += ev.Position.X - newScreenX
			c.OffsetY += ev.Position.Y - newScreenY
		}
	}
}

// Pan pans the camera by the given screen delta.
func (c *Camera) Pan(dx, dy float32) {
	c.OffsetX += dx
	c.OffsetY += dy
}

// ZoomBy zooms by a factor, centered on screen point.
func (c *Camera) ZoomBy(factor float32, centerX, centerY float32) {
	worldX, worldY := c.ScreenToWorld(centerX, centerY)

	c.Zoom *= factor
	if c.Zoom < 0.1 {
		c.Zoom = 0.1
	}
	if c.Zoom > 10 {
		c.Zoom = 10
	}

	newScreenX, newScreenY := c.WorldToScreen(worldX, worldY)
	c.OffsetX += centerX - newScreenX
	c.OffsetY += centerY - newScreenY
}

// CenterOn centers the camera on a world position.
func (c *Camera) CenterOn(worldX, worldY float64, screenWidth, screenHeight float32) {
	c.OffsetX = screenWidth/2 - float32(worldX)*c.Zoom
	c.OffsetY = screenHeight/2 - float32(worldY)*c.Zoom
}

// FitBounds adjusts camera to fit the given world bounds.
func (c *Camera) FitBounds(minX, minY, maxX, maxY float64, screenWidth, screenHeight float32, margin float32) {
	worldW := maxX - minX
	worldH := maxY - minY

	if worldW <= 0 || worldH <= 0 {
		return
	}

	// Calculate zoom to fit
	availW := screenWidth - 2*margin
	availH := screenHeight - 2*margin

	zoomX := float32(availW) / float32(worldW)
	zoomY := float32(availH) / float32(worldH)

	c.Zoom = zoomX
	if zoomY < zoomX {
		c.Zoom = zoomY
	}

	// Clamp zoom
	if c.Zoom < 0.1 {
		c.Zoom = 0.1
	}
	if c.Zoom > 10 {
		c.Zoom = 10
	}

	// Center on bounds
	centerX := (minX + maxX) / 2
	centerY := (minY + maxY) / 2
	c.CenterOn(centerX, centerY, screenWidth, screenHeight)
}

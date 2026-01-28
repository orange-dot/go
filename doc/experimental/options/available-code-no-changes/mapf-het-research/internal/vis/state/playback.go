package state

import "time"

// PlaybackState manages path playback timing.
type PlaybackState struct {
	CurrentTime float64 // Current playback time in seconds
	MaxTime     float64 // Maximum time (solution makespan)
	Speed       float64 // Playback speed multiplier (1.0 = real-time)
	Playing     bool    // Whether playback is active
	lastUpdate  time.Time
}

// NewPlaybackState creates a new playback state.
func NewPlaybackState(maxTime float64) *PlaybackState {
	return &PlaybackState{
		CurrentTime: 0,
		MaxTime:     maxTime,
		Speed:       1.0,
		Playing:     false,
		lastUpdate:  time.Now(),
	}
}

// TogglePlay toggles playback on/off.
func (p *PlaybackState) TogglePlay() {
	p.Playing = !p.Playing
	if p.Playing {
		p.lastUpdate = time.Now()
		// Reset to start if at end
		if p.CurrentTime >= p.MaxTime {
			p.CurrentTime = 0
		}
	}
}

// Play starts playback.
func (p *PlaybackState) Play() {
	p.Playing = true
	p.lastUpdate = time.Now()
}

// Pause stops playback.
func (p *PlaybackState) Pause() {
	p.Playing = false
}

// Reset resets to beginning.
func (p *PlaybackState) Reset() {
	p.CurrentTime = 0
	p.Playing = false
}

// Advance advances playback by elapsed time since last update.
func (p *PlaybackState) Advance() {
	if !p.Playing {
		return
	}

	now := time.Now()
	elapsed := now.Sub(p.lastUpdate).Seconds()
	p.lastUpdate = now

	p.CurrentTime += elapsed * p.Speed

	if p.CurrentTime >= p.MaxTime {
		p.CurrentTime = p.MaxTime
		p.Playing = false
	}
}

// SetTime sets the current playback time.
func (p *PlaybackState) SetTime(t float64) {
	if t < 0 {
		t = 0
	}
	if t > p.MaxTime {
		t = p.MaxTime
	}
	p.CurrentTime = t
}

// StepForward advances by a small step.
func (p *PlaybackState) StepForward() {
	p.Pause()
	step := p.MaxTime / 100 // 1% of total
	if step < 0.1 {
		step = 0.1
	}
	p.SetTime(p.CurrentTime + step)
}

// StepBack goes back by a small step.
func (p *PlaybackState) StepBack() {
	p.Pause()
	step := p.MaxTime / 100
	if step < 0.1 {
		step = 0.1
	}
	p.SetTime(p.CurrentTime - step)
}

// SetSpeed sets the playback speed multiplier.
func (p *PlaybackState) SetSpeed(speed float64) {
	if speed < 0.1 {
		speed = 0.1
	}
	if speed > 10 {
		speed = 10
	}
	p.Speed = speed
}

// Progress returns current progress as 0-1.
func (p *PlaybackState) Progress() float64 {
	if p.MaxTime <= 0 {
		return 0
	}
	return p.CurrentTime / p.MaxTime
}

package main

import (
	"errors"
	"fmt"
	"math"
	"sync"

	"github.com/jezek/xgb"
	"github.com/jezek/xgb/randr"
	"github.com/jezek/xgb/xproto"
)

// minSize is the smallest desktop a viewer may ask for.
var minSize = [2]int{640, 480}

// fit is the size the screen takes for a request: within [minSize, max]
// (or max where that is smaller), width a multiple of 8 and height even.
func fit(w, h int, largest [2]int) (int, int) {
	clamp := func(n, low, high int) int { return min(max(n, min(low, high)), high) }
	return clamp(w, minSize[0], largest[0]) &^ 7, clamp(h, minSize[1], largest[1]) &^ 1
}

// screen resizes the X screen with RandR so the desktop fills the viewer's
// window (--resize). Xvfb offers only the mode it started with, and that
// size is its largest, so start it at the largest to allow; each new size
// gets a mode of its own. A mode belongs to the connection that made it, so
// this one stays open for jumpseat's lifetime. After voxl's screen.rs.
type screen struct {
	mu      sync.Mutex
	conn    *xgb.Conn
	root    xproto.Window
	output  randr.Output
	crtc    randr.Crtc
	max     [2]int
	initial randr.Mode
	custom  randr.Mode // the mode made for the current size, or 0
	mmPerPx [2]float64 // so the DPI stays put across sizes
}

func openScreen(display string) (*screen, error) {
	conn, err := xgb.NewConnDisplay(display)
	if err != nil {
		return nil, err
	}
	fail := func(err error) (*screen, error) { conn.Close(); return nil, fmt.Errorf("randr: %w", err) }
	if err := randr.Init(conn); err != nil {
		return fail(err)
	}
	if _, err := randr.QueryVersion(conn, 1, 3).Reply(); err != nil {
		return fail(err)
	}
	setup := xproto.Setup(conn).DefaultScreen(conn)
	s := &screen{conn: conn, root: setup.Root, mmPerPx: [2]float64{
		float64(setup.WidthInMillimeters) / float64(setup.WidthInPixels),
		float64(setup.HeightInMillimeters) / float64(setup.HeightInPixels),
	}}
	resources, err := randr.GetScreenResources(conn, s.root).Reply()
	if err != nil {
		return fail(err)
	}
	if len(resources.Outputs) == 0 || len(resources.Crtcs) == 0 {
		return fail(errors.New("no output"))
	}
	s.output, s.crtc = resources.Outputs[0], resources.Crtcs[0]
	crtc, err := randr.GetCrtcInfo(conn, s.crtc, resources.ConfigTimestamp).Reply()
	if err != nil {
		return fail(err)
	}
	s.initial = crtc.Mode
	limits, err := randr.GetScreenSizeRange(conn, s.root).Reply()
	if err != nil {
		return fail(err)
	}
	s.max = [2]int{int(limits.MaxWidth), int(limits.MaxHeight)}
	return s, nil
}

// resize sets the screen to the size that fits w x h. Streaming follows on
// its own: the hub hears RandR's screen change and starts over.
func (s *screen) resize(w, h int) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	w, h = fit(w, h, s.max)
	geometry, err := xproto.GetGeometry(s.conn, xproto.Drawable(s.root)).Reply()
	if err != nil {
		return err
	}
	cw, ch := int(geometry.Width), int(geometry.Height)
	if w == cw && h == ch {
		return nil
	}
	resources, err := randr.GetScreenResources(s.conn, s.root).Reply()
	if err != nil {
		return err
	}
	mode := s.initial
	if [2]int{w, h} != s.max {
		name := fmt.Sprintf("jumpseat-%dx%d", w, h)
		made, err := randr.CreateMode(s.conn, s.root, randr.ModeInfo{
			Width: uint16(w), Height: uint16(h), DotClock: uint32(w * h * 60),
			HsyncStart: uint16(w + 1), HsyncEnd: uint16(w + 2), Htotal: uint16(w + 3),
			VsyncStart: uint16(h + 1), VsyncEnd: uint16(h + 2), Vtotal: uint16(h + 3),
			NameLen: uint16(len(name)),
		}, name).Reply()
		if err != nil {
			return err
		}
		mode = made.Mode
		if err := randr.AddOutputModeChecked(s.conn, s.output, mode).Check(); err != nil {
			return err
		}
	}
	mm := func(px int, per float64) uint32 { return uint32(max(1, math.Round(float64(px)*per))) }
	setSize := func(w, h int) error {
		return randr.SetScreenSizeChecked(s.conn, s.root, uint16(w), uint16(h), mm(w, s.mmPerPx[0]), mm(h, s.mmPerPx[1])).Check()
	}
	// The screen must hold the CRTC at every step: grow, switch, shrink.
	if err := setSize(max(w, cw), max(h, ch)); err != nil {
		return err
	}
	reply, err := randr.SetCrtcConfig(s.conn, s.crtc, xproto.TimeCurrentTime, resources.ConfigTimestamp, 0, 0, mode, randr.RotationRotate0, []randr.Output{s.output}).Reply()
	if err != nil {
		return err
	}
	if reply.Status != randr.SetConfigSuccess {
		return fmt.Errorf("randr: set mode %dx%d: status %d", w, h, reply.Status)
	}
	if err := setSize(w, h); err != nil {
		return err
	}
	if s.custom != 0 {
		randr.DeleteOutputMode(s.conn, s.output, s.custom)
		randr.DestroyMode(s.conn, s.custom)
	}
	s.custom = 0
	if mode != s.initial {
		s.custom = mode
	}
	return nil
}

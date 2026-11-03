package main

import (
	"encoding/binary"
	"errors"
	"fmt"
	"log"
	"math"
	"strings"
	"time"

	"github.com/jezek/xgb"
	"github.com/jezek/xgb/xproto"
	"github.com/jezek/xgb/xtest"
)

// scrollUnitsPerNotch is the X server's scroll increment: one wheel notch,
// as hi-res wheels count it.
const scrollUnitsPerNotch = 120

// scrollPxPerNotch is how many desktop pixels Chromium scrolls for one notch
// of scroll units, so a viewer's pixel delta scrolls that many pixels.
// Measured: 53 units a step scrolled Chromium 67.7 px a step at scale 1, so
// a notch is 120 px (voxl's 180 is the same at its 1.5 scale).
const scrollPxPerNotch = 120.0

// smoothScroll injects scrolling as relative motion on the XTest pointer's
// scroll axes, which Chromium scrolls pixel by pixel. Stock Xvfb gives that
// device no scroll axes; docker/patches/xorg-server-xtest-scroll.patch (from
// voxl) adds them. Nil means wheel clicks only.
type smoothScroll struct {
	device       byte
	vaxis, haxis int
	xtestOpcode  byte
	xiFirstEvent byte
}

// scroller turns pixel deltas into scroll units or, without smooth
// scrolling, whole wheel clicks, carrying what is left into the next delta.
type scroller struct {
	smooth     *smoothScroll
	accX, accY float64   // scroll units not yet sent
	last       time.Time // the last smooth scroll sent
}

// gestureGap is how long without scrolling starts a new gesture. Chromium
// takes the first scroll valuator it sees (at start, or after the pointer
// enters a window) as the axis's baseline and scrolls nothing for it, so a
// gesture starts with a zero step: a lone 30 px delta scrolled 0 without it.
const gestureGap = 300 * time.Millisecond

func (s *scroller) scroll(conn *xgb.Conn, root xproto.Window, dxPx, dyPx float64) {
	s.accX += dxPx * scrollUnitsPerNotch / scrollPxPerNotch
	s.accY += dyPx * scrollUnitsPerNotch / scrollPxPerNotch
	step := 1.0
	if s.smooth == nil {
		step = scrollUnitsPerNotch
	}
	ux, uy := math.Trunc(s.accX/step), math.Trunc(s.accY/step)
	s.accX -= ux * step
	s.accY -= uy * step
	if s.smooth != nil {
		if ux != 0 || uy != 0 {
			if time.Since(s.last) > gestureGap {
				s.smooth.send(conn, 0, 0)
			}
			s.smooth.send(conn, int32(uy), int32(ux))
			s.last = time.Now()
		}
		return
	}
	wheelClicks(conn, root, int(ux), int(uy))
}

// wheelClicks presses X's wheel buttons: 4 up, 5 down, 6 left, 7 right.
func wheelClicks(conn *xgb.Conn, root xproto.Window, dx, dy int) {
	for _, step := range [][2]int{{dy, 4}, {dx, 6}} {
		button, clicks := byte(step[1]), -step[0]
		if step[0] > 0 {
			button, clicks = button+1, step[0]
		}
		for range min(clicks, 20) {
			xtest.FakeInput(conn, xproto.ButtonPress, button, 0, root, 0, 0, 0)
			xtest.FakeInput(conn, xproto.ButtonRelease, button, 0, root, 0, 0, 0)
		}
	}
}

// detectSmoothScroll finds the XTest pointer's vertical and horizontal
// scroll axes through XIQueryDevice, built by hand: xgb has no XInput.
func detectSmoothScroll(conn *xgb.Conn) *smoothScroll {
	found, err := querySmoothScroll(conn)
	if found == nil {
		log.Printf("scroll: wheel clicks (%v)", err)
		return nil
	}
	log.Printf("scroll: smooth, XTest pointer %d, axes %d/%d", found.device, found.vaxis, found.haxis)
	return found
}

func querySmoothScroll(conn *xgb.Conn) (*smoothScroll, error) {
	xi, err := xproto.QueryExtension(conn, uint16(len("XInputExtension")), "XInputExtension").Reply()
	if err != nil || !xi.Present {
		return nil, fmt.Errorf("no XInput: %v", err)
	}
	conn.ExtLock.RLock()
	xtestOpcode, ok := conn.Extensions["XTEST"]
	conn.ExtLock.RUnlock()
	if !ok {
		return nil, errors.New("no XTest")
	}
	// XIQueryVersion 2.2 first: the server answers XI2 requests only after it.
	if _, err := rawRequest(conn, []byte{xi.MajorOpcode, 47, 2, 0, 2, 0, 2, 0}, true); err != nil {
		return nil, fmt.Errorf("XIQueryVersion: %v", err)
	}
	// XIQueryDevice(XIAllDevices).
	reply, err := rawRequest(conn, []byte{xi.MajorOpcode, 48, 2, 0, 0, 0, 0, 0}, true)
	if err != nil {
		return nil, fmt.Errorf("XIQueryDevice: %v", err)
	}
	if found := parseScrollAxes(reply, xtestOpcode, xi.FirstEvent); found != nil {
		return found, nil
	}
	return nil, errors.New("the XTest pointer has no scroll axes (stock Xvfb)")
}

// parseScrollAxes reads an XIQueryDevice reply for the "XTEST pointer"
// device's scroll classes.
func parseScrollAxes(reply []byte, xtestOpcode, xiFirstEvent byte) *smoothScroll {
	if len(reply) < 32 {
		return nil
	}
	le := binary.LittleEndian
	count := int(le.Uint16(reply[8:]))
	at := 32
	for range count {
		if at+12 > len(reply) {
			return nil
		}
		id := le.Uint16(reply[at:])
		classes := int(le.Uint16(reply[at+6:]))
		nameLen := int(le.Uint16(reply[at+8:]))
		nameAt := at + 12
		at = nameAt + (nameLen+3)/4*4
		if at > len(reply) {
			return nil
		}
		name := string(reply[nameAt : nameAt+nameLen])
		found := &smoothScroll{device: byte(id), vaxis: -1, haxis: -1, xtestOpcode: xtestOpcode, xiFirstEvent: xiFirstEvent}
		for range classes {
			if at+4 > len(reply) {
				return nil
			}
			kind, words := le.Uint16(reply[at:]), int(le.Uint16(reply[at+2:]))
			// ScrollClass: type 3, then sourceid, number, scroll_type (1 vertical, 2 horizontal).
			if kind == 3 && at+10 <= len(reply) {
				number, scrollType := int(le.Uint16(reply[at+6:])), le.Uint16(reply[at+8:])
				switch scrollType {
				case 1:
					found.vaxis = number
				case 2:
					found.haxis = number
				}
			}
			at += max(words, 1) * 4
		}
		// Up to 6 valuators fit in one DeviceValuator event; device ids above
		// 127 do not fit its 7 bits.
		if strings.Contains(name, "XTEST pointer") && found.vaxis >= 0 && found.vaxis < 6 && found.haxis >= 0 && found.haxis < 6 && id < 128 {
			return found
		}
	}
	return nil
}

// send is XTestFakeInput with an XI DeviceMotionNotify (relative) followed
// by a DeviceValuator event carrying the scroll deltas. The valuators start
// at axis 0: XTest stores them at absolute indices but masks them relative
// to the first, so a non-zero first valuator loses them. x and y move 0.
func (s *smoothScroll) send(conn *xgb.Conn, vertical, horizontal int32) {
	n := max(s.vaxis, s.haxis) + 1
	values := make([]int32, n)
	values[s.vaxis] = vertical
	values[s.haxis] = horizontal
	request := make([]byte, 4+64)
	request[0], request[1] = s.xtestOpcode, 2 // FakeInput
	xgb.Put16(request[2:], uint16(len(request)/4))
	motion := request[4:36]
	motion[0] = s.xiFirstEvent + 5 // DeviceMotionNotify
	motion[1] = 1                  // relative
	motion[31] = s.device
	valuator := request[36:68]
	valuator[0] = s.xiFirstEvent // DeviceValuator
	valuator[1] = s.device
	valuator[6] = byte(n) // num_valuators
	for i, v := range values {
		xgb.Put32(valuator[8+i*4:], uint32(v))
	}
	_, _ = rawRequest(conn, request, false)
}

// rawRequest sends a request xgb has no binding for and, when it has a
// reply, waits for it.
func rawRequest(conn *xgb.Conn, request []byte, reply bool) ([]byte, error) {
	cookie := conn.NewCookie(reply, reply)
	conn.NewRequest(request, cookie)
	if !reply {
		return nil, nil
	}
	return cookie.Reply()
}

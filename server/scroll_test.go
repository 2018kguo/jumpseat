package main

import (
	"encoding/binary"
	"testing"
)

// The XTest pointer's scroll axes are read from an XIQueryDevice reply; a
// device without them (stock Xvfb) means wheel clicks.
func TestParseScrollAxes(t *testing.T) {
	le := binary.LittleEndian
	device := func(id uint16, name string, classes ...[]byte) []byte {
		info := make([]byte, 12)
		le.PutUint16(info, id)
		le.PutUint16(info[6:], uint16(len(classes)))
		le.PutUint16(info[8:], uint16(len(name)))
		info = append(info, name...)
		for len(info)%4 != 0 {
			info = append(info, 0)
		}
		for _, class := range classes {
			info = append(info, class...)
		}
		return info
	}
	scrollClass := func(number, kind uint16) []byte {
		class := make([]byte, 24)
		le.PutUint16(class, 3)
		le.PutUint16(class[2:], 6)
		le.PutUint16(class[6:], number)
		le.PutUint16(class[8:], kind)
		return class
	}
	buttonClass := make([]byte, 8)
	le.PutUint16(buttonClass, 1)
	le.PutUint16(buttonClass[2:], 2)
	reply := func(devices ...[]byte) []byte {
		out := make([]byte, 32)
		le.PutUint16(out[8:], uint16(len(devices)))
		for _, d := range devices {
			out = append(out, d...)
		}
		return out
	}

	patched := reply(device(2, "Virtual core pointer", buttonClass),
		device(4, "Virtual core XTEST pointer", buttonClass, scrollClass(2, 1), scrollClass(3, 2)))
	got := parseScrollAxes(patched, 132, 66)
	if got == nil || got.device != 4 || got.vaxis != 2 || got.haxis != 3 || got.xtestOpcode != 132 || got.xiFirstEvent != 66 {
		t.Fatalf("patched Xvfb: %+v", got)
	}
	if got := parseScrollAxes(reply(device(4, "Virtual core XTEST pointer", buttonClass)), 132, 66); got != nil {
		t.Errorf("stock Xvfb: %+v, want wheel clicks", got)
	}
	if got := parseScrollAxes(patched[:40], 132, 66); got != nil {
		t.Errorf("truncated reply: %+v", got)
	}
}

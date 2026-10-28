package main

import "testing"

func TestKeysym(t *testing.T) {
	for key, want := range map[string]uint32{
		"a": 'a', "A": 'A', "Enter": 0xff0d, "é": 0xe9, "中": 0x1004e2d, "Dead": 0, "": 0,
	} {
		if got := uint32(keysym(key)); got != want {
			t.Errorf("keysym(%q) = %#x, want %#x", key, got, want)
		}
	}
}

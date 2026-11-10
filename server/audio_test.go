package main

import (
	"bytes"
	"encoding/binary"
	"math"
	"os/exec"
	"testing"
	"time"
)

// A tone becomes Opus packets for listeners; silence after its tail sends nothing.
func TestAudioEncode(t *testing.T) {
	pcm := make([]byte, 0, audioFrameBytes*10)
	for i := 0; i < audioFrameSamples*5; i++ { // 100 ms of 440 Hz
		s := int16(8000 * math.Sin(2*math.Pi*440*float64(i)/48000))
		pcm = binary.LittleEndian.AppendUint16(pcm, uint16(s))
		pcm = binary.LittleEndian.AppendUint16(pcm, uint16(s))
	}
	pcm = append(pcm, make([]byte, audioFrameBytes*5)...) // 100 ms of silence

	a := newAudio("test")
	v := &viewer{gone: make(chan struct{}), out: make(chan any, 64)}
	a.listeners[v] = true
	a.parec = &exec.Cmd{}
	_ = a.encode(bytes.NewReader(pcm), a.parec)
	want := 5 + audioTail // the tone, then its tail; no more silence
	if len(v.out) != want {
		t.Fatalf("got %d packets, want %d", len(v.out), want)
	}
	for range want {
		p := (<-v.out).([]byte)
		if p[0] != audioPacket || len(p) < 10 {
			t.Fatalf("bad packet: % x", p[:min(len(p), 8)])
		}
	}
}

// Without a source, turning sound on answers "no sound" and starts nothing.
func TestAudioOff(t *testing.T) {
	a := newAudio("")
	v := &viewer{gone: make(chan struct{}), out: make(chan any, 4)}
	a.set(v, true)
	select {
	case m := <-v.out:
		if m != `{"ok":false,"t":"s"}` {
			t.Fatalf("got %v", m)
		}
	case <-time.After(time.Second):
		t.Fatal("no answer")
	}
	if a.parec != nil || len(a.listeners) != 0 {
		t.Fatal("capture started without a source")
	}
}

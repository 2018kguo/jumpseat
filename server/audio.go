package main

/*
// libopus is linked in statically (libopus-dev's .a): no runtime library.
#cgo LDFLAGS: -Wl,-Bstatic -lopus -Wl,-Bdynamic -lm
#include <opus/opus.h>

static OpusEncoder *open_opus(int bitrate) {
	int err;
	OpusEncoder *e = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &err);
	if (err != OPUS_OK) return 0;
	opus_encoder_ctl(e, OPUS_SET_BITRATE(bitrate));
	return e;
}
*/
import "C"

import (
	"bytes"
	"errors"
	"io"
	"log"
	"os/exec"
	"sync"
	"unsafe"
)

const (
	// 20 ms of 48 kHz stereo s16: one Opus packet.
	audioFrameSamples = 960
	audioFrameBytes   = audioFrameSamples * 2 * 2
	audioBitrate      = 64_000
	// audioPacket marks an Opus packet: byte 0 of a binary message, where
	// video frames have 0 or 1.
	audioPacket = 2
)

// audio is the desktop's sound for the viewers that turned it on ({"t":
// "s", "on": true}): one capture and one Opus encoder, like the hub. It
// records only while someone listens, and silence is not sent.
type audio struct {
	source string // PulseAudio source; "" turns sound off

	mu        sync.Mutex
	listeners map[*viewer]bool
	parec     *exec.Cmd // the running capture, nil while nobody listens
}

func newAudio(source string) *audio {
	return &audio{source: source, listeners: map[*viewer]bool{}}
}

// set turns sound on or off for one viewer.
func (a *audio) set(v *viewer, on bool) {
	select {
	case <-v.gone: // a late "s" from a viewer that left must not keep parec running
		on = false
	default:
	}
	a.mu.Lock()
	defer a.mu.Unlock()
	if on {
		a.listeners[v] = true
	} else {
		delete(a.listeners, v)
	}
	switch {
	case len(a.listeners) > 0 && a.parec == nil:
		cmd, err := a.capture()
		if err != nil {
			log.Printf("audio: %v", err)
			a.failLocked()
			return
		}
		a.parec = cmd
	case len(a.listeners) == 0 && a.parec != nil:
		_ = a.parec.Process.Kill()
		a.parec = nil
	}
}

func (a *audio) list() []*viewer {
	a.mu.Lock()
	defer a.mu.Unlock()
	list := make([]*viewer, 0, len(a.listeners))
	for v := range a.listeners {
		list = append(list, v)
	}
	return list
}

// capture records through parec (PulseAudio or PipeWire's pulse server)
// and sends each 20 ms as an Opus packet.
func (a *audio) capture() (*exec.Cmd, error) {
	if a.source == "" {
		return nil, errors.New("off (--audio \"\")")
	}
	cmd := exec.Command("parec", "--device="+a.source, "--format=s16le", "--rate=48000", "--channels=2",
		"--latency-msec=20", "--raw")
	stdout, err := cmd.StdoutPipe()
	if err == nil {
		err = cmd.Start()
	}
	if err != nil {
		return nil, err
	}
	go func() {
		err := a.encode(stdout)
		_ = cmd.Process.Kill() // a failed encoder leaves parec blocked on its pipe
		_ = cmd.Wait()
		a.mu.Lock()
		defer a.mu.Unlock()
		if a.parec == cmd { // not stopped on purpose
			log.Printf("audio: parec ended: %v", err)
			a.failLocked()
		}
	}()
	return cmd, nil
}

// failLocked tells every listener there is no sound and forgets them.
func (a *audio) failLocked() {
	for v := range a.listeners {
		_ = v.text(map[string]any{"t": "s", "ok": false})
	}
	clear(a.listeners)
	a.parec = nil
}

func (a *audio) encode(pcm io.Reader) error {
	enc := C.open_opus(audioBitrate)
	if enc == nil {
		return errors.New("opus encoder")
	}
	defer C.opus_encoder_destroy(enc)
	frame := make([]byte, audioFrameBytes)
	packet := make([]byte, 1+4000)
	packet[0] = audioPacket
	for {
		if _, err := io.ReadFull(pcm, frame); err != nil {
			return err
		}
		if silent(frame) {
			continue
		}
		n := C.opus_encode(enc, (*C.opus_int16)(unsafe.Pointer(&frame[0])), audioFrameSamples,
			(*C.uchar)(unsafe.Pointer(&packet[1])), C.opus_int32(len(packet)-1))
		if n < 0 {
			return errors.New("opus_encode failed")
		}
		message := bytes.Clone(packet[:1+n])
		for _, v := range a.list() {
			_ = v.send(message)
		}
	}
}

var zeroFrame = make([]byte, audioFrameBytes)

// silent: digital silence, what a null sink's monitor gives when nothing plays.
func silent(frame []byte) bool { return bytes.Equal(frame, zeroFrame) }

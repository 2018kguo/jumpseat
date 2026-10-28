package main

import (
	"sync"
	"time"
)

// flowSlack is how much queueing beyond the link's own round trip a viewer
// may have before capture waits for it. TCP never drops a frame, so without
// this a link slower than the stream queues seconds of video in socket and
// proxy buffers and every key press waits behind them (2.6 s measured on a
// 1 Mbit/s link). Damage merges while capture waits, so the next frame is
// the newest screen.
const flowSlack = 100 * time.Millisecond

// rttWindow is how long the fastest ack stands as the link's round trip: a
// route that gets slower for good becomes the new floor instead of reading
// as queueing forever.
const rttWindow = 10 * time.Second

// lostAfter is when an unshown frame is given up on (a decoder that dropped
// it, a draw that threw): capture must never wait on an ack that will not
// come.
const lostAfter = 2 * time.Second

// limitedHolds is how many times capture must have waited within rateWindow
// before acks time the link's speed: one stalled page (a busy main thread
// delays its acks) is not a slow link.
const limitedHolds = 3

// rateWindow is how much limited delivery the link's speed is averaged
// over: an average, not a per-ack peak, because the browser hands over
// frames that arrived together all at once and their acks bunch up.
const rateWindow = time.Second

type sentFrame struct {
	ts    int64 // ms on the flow's clock; the id the viewer's ack carries back
	bytes int
}

type delivery struct {
	at    int64 // ms on the flow's clock
	bytes int
}

type rttSample struct {
	at, rtt int64 // ms on the flow's clock
}

// flow tracks frames sent but not yet shown, from the viewer's acks (each
// frame's ts once decoded). Capture waits while the oldest unshown frame has
// queued longer than flowSlack, and linkKbps tells the encoder what the link
// carries once it is the bottleneck. A viewer that never acks (built before
// acks) is not held back. Times are monotonic ms since the flow began, so a
// VM clock step cannot confuse it.
type flow struct {
	mu        sync.Mutex
	epoch     time.Time
	inFlight  []sentFrame
	rtts      []rttSample
	delivered []delivery
	holds     []int64 // when capture waited, within rateWindow
	acking    bool
	acked     chan struct{}
	off       bool // AGENTVIEW_FLOW=off, for A/B runs
}

func newFlow() *flow { return &flow{epoch: time.Now(), acked: make(chan struct{}, 1)} }

// now is the flow's clock, and the ts frames are stamped with.
func (f *flow) now() int64 { return time.Since(f.epoch).Milliseconds() }

func (f *flow) sent(ts int64, bytes int) {
	f.mu.Lock()
	f.inFlight = append(f.inFlight, sentFrame{ts: ts, bytes: bytes})
	f.mu.Unlock()
}

func (f *flow) ack(ts int64) {
	f.mu.Lock()
	now := f.now()
	bytes := 0
	kept := f.inFlight[:0]
	for _, frame := range f.inFlight {
		if frame.ts <= ts {
			bytes += frame.bytes
		} else {
			kept = append(kept, frame)
		}
	}
	f.inFlight = kept
	if rtt := now - ts; rtt >= 0 {
		f.rtts = append(f.rtts, rttSample{at: now, rtt: rtt})
	}
	for len(f.rtts) > 1 && now-f.rtts[0].at > rttWindow.Milliseconds() {
		f.rtts = f.rtts[1:]
	}
	f.trim(now)
	if len(f.holds) >= limitedHolds {
		f.delivered = append(f.delivered, delivery{at: now, bytes: bytes})
	}
	f.acking = true
	f.mu.Unlock()
	select {
	case f.acked <- struct{}{}:
	default:
	}
}

func (f *flow) trim(now int64) {
	for len(f.delivered) > 0 && now-f.delivered[0].at > rateWindow.Milliseconds() {
		f.delivered = f.delivered[1:]
	}
	for len(f.holds) > 0 && now-f.holds[0] > rateWindow.Milliseconds() {
		f.holds = f.holds[1:]
	}
}

func (f *flow) minRTT() int64 {
	low := int64(-1)
	for _, sample := range f.rtts {
		if low < 0 || sample.rtt < low {
			low = sample.rtt
		}
	}
	return low
}

// behind reports whether the oldest unshown frame has waited longer than the
// link's round trip plus flowSlack.
func (f *flow) behind() bool {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.off || !f.acking {
		return false
	}
	now := f.now()
	for len(f.inFlight) > 0 && now-f.inFlight[0].ts > lostAfter.Milliseconds() {
		f.inFlight = f.inFlight[1:]
	}
	if len(f.inFlight) == 0 || now-f.inFlight[0].ts <= f.minRTT()+flowSlack.Milliseconds() {
		return false
	}
	if len(f.holds) == 0 || f.holds[len(f.holds)-1] != now {
		f.holds = append(f.holds, now)
	}
	return true
}

// linkKbps is what the link carried while it kept holding capture back over
// the last rateWindow, or 0 when it has not been the bottleneck.
func (f *flow) linkKbps() int {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.trim(f.now())
	if f.off || len(f.holds) < limitedHolds || len(f.delivered) < 5 {
		return 0
	}
	span := f.delivered[len(f.delivered)-1].at - f.delivered[0].at
	if span < rateWindow.Milliseconds()/4 {
		return 0
	}
	bytes := 0
	for _, d := range f.delivered[1:] {
		bytes += d.bytes
	}
	return int(float64(bytes) * 8 / float64(span))
}

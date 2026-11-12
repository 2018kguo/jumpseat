package main

import (
	"testing"
	"time"
)

func TestFlowHoldsCaptureOnlyForAQueueingViewer(t *testing.T) {
	f := newFlow()
	f.epoch = time.Now().Add(-10 * time.Second)
	now := f.now()
	f.sent(now-500, 1000)
	if !f.behind() {
		t.Fatal("a viewer is held from its first frame, before any ack: a slow link would queue seconds behind the keyframe")
	}
	f.sent(now-20, 1000)
	f.ack(now - 20) // a 20 ms link; covers the older frame too
	if len(f.inFlight) != 0 {
		t.Fatal("an ack covers its frame and every older one")
	}
	f.sent(now-100, 1000)
	if f.behind() {
		t.Fatal("100 ms unshown on a 20 ms link is within the slack")
	}
	f.inFlight[0].ts = now - 200
	if !f.behind() {
		t.Fatal("200 ms unshown on a 20 ms link is queueing")
	}
	f.inFlight[0].ts = now - 3000
	f.lastAck = now - 1000
	if !f.behind() {
		t.Fatal("acks still arriving: the link is slow, and its oldest frame is not lost")
	}
	f.lastAck = now - 3000
	if f.behind() || len(f.inFlight) != 0 {
		t.Fatal("no ack for 3 s: the frame is lost, not a reason to wait forever")
	}
}

func TestOneStallDoesNotReadAsASlowLink(t *testing.T) {
	f := newFlow()
	f.epoch = time.Now().Add(-10 * time.Second)
	now := f.now()
	f.sent(now-20, 1)
	f.ack(now - 20)
	f.sent(now-500, 1)
	if !f.behind() {
		t.Fatal("500 ms unshown is queueing")
	}
	for i := 0; i < 10; i++ {
		f.sent(f.now(), 5000)
		f.ack(f.now())
	}
	if f.linkKbps() != 0 {
		t.Fatal("one hold (a stalled page) timed the link from the stream's own rate")
	}
}

func TestLinkSpeedFromRepeatedHolds(t *testing.T) {
	f := newFlow()
	f.epoch = time.Now().Add(-10 * time.Second)
	now := f.now()
	f.holds = []int64{now - 300, now - 200, now - 100}
	f.rtts = []rttSample{{at: now - 5000, rtt: 20}} // the floor; then a queue stands
	for i := 0; i < queueSamples; i++ {
		f.rtts = append(f.rtts, rttSample{at: now - 500 + int64(i)*50, rtt: 300})
	}
	for i := 0; i < 6; i++ {
		f.delivered = append(f.delivered, delivery{at: now - 500 + int64(i)*100, bytes: 12_500})
	}
	if got := f.linkKbps(); got != 1000 {
		t.Fatalf("link speed %d kbit/s, want 1000", got)
	}
}

func TestARouteThatSlowsBecomesTheNewFloor(t *testing.T) {
	f := newFlow()
	f.epoch = time.Now().Add(-30 * time.Second)
	f.rtts = []rttSample{{at: f.now() - 20_000, rtt: 20}}
	f.ack(f.now() - 150)
	if got := f.minRTT(); got != 150 {
		t.Fatalf("round trip floor %d ms, want the recent 150", got)
	}
}

// A wifi link: 30 ms round trips that vary by +-40 ms with nothing queued.
func jitteryRTTs(now int64) []rttSample {
	var rtts []rttSample
	for i := 0; i < 60; i++ {
		rtts = append(rtts, rttSample{at: now - 3000 + int64(i)*50, rtt: 30 + int64((i*37)%80)})
	}
	return rtts
}

func TestJitterIsNotQueueing(t *testing.T) {
	f := newFlow()
	f.epoch = time.Now().Add(-10 * time.Second)
	now := f.now()
	f.acking = true
	f.rtts = jitteryRTTs(now)
	f.sent(now-180, 1000) // late by the jitter, not by a queue
	if f.behind() {
		t.Fatalf("a frame 180 ms out on a 30 ms link with %d ms of jitter held capture", f.jitter())
	}
	f.holds = []int64{now - 300, now - 200, now - 100}
	for i := 0; i < 6; i++ {
		f.delivered = append(f.delivered, delivery{at: now - 500 + int64(i)*100, bytes: 1000})
	}
	if got := f.linkKbps(); got != 0 {
		t.Fatalf("jitter capped the link to %d kbit/s", got)
	}
}

func TestAStandingQueueStillHolds(t *testing.T) {
	f := newFlow()
	f.epoch = time.Now().Add(-10 * time.Second)
	now := f.now()
	f.acking = true
	f.rtts = []rttSample{{at: now - 5000, rtt: 30}}
	for i := 0; i < jitterSamples; i++ { // every round trip 400 ms: a queue
		f.rtts = append(f.rtts, rttSample{at: now - 1500 + int64(i)*50, rtt: 400 + int64(i%3)})
	}
	f.sent(now-200, 1000)
	if !f.behind() {
		t.Fatal("a frame 200 ms out behind a standing queue must hold capture")
	}
	if !f.queueing() {
		t.Fatal("round trips all 370 ms above the floor are a queue")
	}
}

// A viewer that stops acking while frames go out is stalled (and dropped by
// the hub); one that is only slow, or has nothing out, is not.
func TestAStalledViewerIsNoticed(t *testing.T) {
	f := newFlow()
	f.epoch = time.Now().Add(-20 * time.Second)
	now := f.now()
	f.sent(now-10_000, 1000)
	f.ack(now - 10_000)
	f.lastAck = now - 10_000
	if f.stalled() {
		t.Fatal("nothing out since the last ack: idle, not stalled")
	}
	f.sent(now-1000, 1000)
	f.lastAck = now - 1500
	if f.stalled() {
		t.Fatal("1.5 s without an ack is a slow viewer, not a stalled one")
	}
	f.lastAck = now - 6000
	if !f.stalled() {
		t.Fatal("6 s of frames with no ack is stalled")
	}
}

// A link slower than the stream: each round trip longer than the last as the
// queue grows. That spread is the queue, not jitter, and must not widen the
// slack past it (1 Mbit/s, animating: frames queued seconds deep and key
// presses never showed, bench/compare/viewers.mjs).
func TestAGrowingQueueIsNotJitter(t *testing.T) {
	f := newFlow()
	f.epoch = time.Now().Add(-10 * time.Second)
	now := f.now()
	f.acking = true
	f.rtts = []rttSample{{at: now - 3000, rtt: 50}}
	for i := 0; i < jitterSamples; i++ {
		f.rtts = append(f.rtts, rttSample{at: now - 1500 + int64(i)*50, rtt: 100 + int64(i)*60})
	}
	f.sent(now-400, 1000)
	if !f.behind() {
		t.Fatalf("a frame 400 ms out on a 50 ms link held nothing (slack widened by %d ms of 'jitter')", f.jitter())
	}
}

// The floor is the bare link's round trip, from pings, not a frame's ack:
// a keyframe's ack carries its own transfer time (1.65 s at 1 Mbit/s).
func TestPingsSetTheFloorNotAKeyframe(t *testing.T) {
	f := newFlow()
	f.epoch = time.Now().Add(-10 * time.Second)
	now := f.now()
	f.pong(now - 60)
	f.sent(now-1700, 250_000)
	f.ack(now - 1700) // the keyframe, 1.7 s to cross the link
	if got := f.minRTT(); got != 60 {
		t.Fatalf("floor %d ms, want the ping's 60", got)
	}
	f.sent(now-400, 25_000)
	if !f.behind() {
		t.Fatal("a frame 400 ms out on a 60 ms link must hold capture")
	}
}

// A viewer that never acks (a client without acks, a decoder that outputs
// nothing) holds capture for its first frame at most, then is ignored.
func TestAViewerThatNeverAcksIsIgnored(t *testing.T) {
	f := newFlow()
	f.epoch = time.Now().Add(-10 * time.Second)
	now := f.now()
	f.lastAck = now // a fresh flow: no ack, but not long without one either
	f.sent(now-1000, 100_000)
	if !f.behind() {
		t.Fatal("a slow first keyframe holds capture")
	}
	f.sent(now-300, 1000)
	f.firstSent = now - 2500
	if f.behind() {
		t.Fatal("no ack 2.5 s after its first frame: ignored, as before acks")
	}
}

// Acks still arriving keep frames from being given up, but not past
// stallAfter: a link far below the lowest cap must not hold every viewer to
// a frame every few seconds.
func TestNoFrameIsWaitedOnPastStallAfter(t *testing.T) {
	f := newFlow()
	f.epoch = time.Now().Add(-20 * time.Second)
	now := f.now()
	f.sent(now-6000, 100_000)
	f.sent(now-100, 1000)
	f.ack(now - 7000)
	f.lastAck = now - 500
	if f.behind() {
		t.Fatal("a frame 6 s out was still waited on")
	}
}

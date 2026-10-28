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
	if f.behind() {
		t.Fatal("a viewer that has never acked (an old client) must not be held back")
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
	if f.behind() || len(f.inFlight) != 0 {
		t.Fatal("a frame unacked for 3 s is lost, not a reason to wait forever")
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

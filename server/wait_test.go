package main

import (
	"testing"
	"time"
)

// An idle wait holds while changes keep coming and returns once they stop;
// a change wait returns at the first one; neither outlives its timeout.
func TestSettle(t *testing.T) {
	changes := make(chan struct{}, 1)
	go func() {
		for range 5 {
			changes <- struct{}{}
			time.Sleep(20 * time.Millisecond)
		}
	}()
	start := time.Now()
	changed, timedOut := settle(nil, changes, false, 60*time.Millisecond, time.Second)
	if took := time.Since(start); !changed || timedOut || took < 140*time.Millisecond || took > 500*time.Millisecond {
		t.Errorf("idle: changed %v, timed out %v, after %v; want a change, settled after the last one", changed, timedOut, took)
	}

	go func() { time.Sleep(30 * time.Millisecond); changes <- struct{}{} }()
	if changed, timedOut := settle(nil, changes, true, time.Hour, time.Second); !changed || timedOut {
		t.Errorf("change: changed %v, timed out %v", changed, timedOut)
	}

	if changed, timedOut := settle(nil, make(chan struct{}), true, time.Hour, 30*time.Millisecond); changed || !timedOut {
		t.Errorf("timeout: changed %v, timed out %v", changed, timedOut)
	}
}

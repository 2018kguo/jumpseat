package main

import (
	"log"
	"os"
	"sync/atomic"
	"time"
)

// JUMPSEAT_TRACE=1 logs, for each frame within 300 ms of a key press,
// when it was damaged, grabbed, encoded and sent, counted from the press
// reaching X: where key-to-screen goes on this side of the network
// (bench/README.md).
var (
	tracing     = os.Getenv("JUMPSEAT_TRACE") == "1"
	lastKeyNs   atomic.Int64
	lastDamaged atomic.Int64
)

func traceKey() {
	if tracing {
		lastKeyNs.Store(time.Now().UnixNano())
	}
}

func traceDamage() {
	if tracing {
		lastDamaged.Store(time.Now().UnixNano())
	}
}

func traceFrame(grabStart, grabbed, encoded, sent time.Time, bytes int) {
	if !tracing {
		return
	}
	key := time.Unix(0, lastKeyNs.Load())
	if grabStart.Sub(key) > 300*time.Millisecond {
		return
	}
	ms := func(t time.Time) float64 { return float64(t.Sub(key).Microseconds()) / 1000 }
	log.Printf("trace: damage %.1f grab %.1f grabbed %.1f encoded %.1f sent %.1f ms after the key, %d B",
		ms(time.Unix(0, lastDamaged.Load())), ms(grabStart), ms(grabbed), ms(encoded), ms(sent), bytes)
}

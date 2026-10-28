package main

import (
	"sync"
	"time"

	"golang.org/x/net/websocket"
)

// slowLink is the benchmarks' bottleneck (JUMPSEAT_LINK_KBPS): an
// unbounded queue drained at a fixed rate, the deep-buffered router that
// makes TCP links bloat. Browser throttling cannot stand in for it: Chromium
// buffers websocket messages its own way.
type slowLink struct {
	v      *viewer
	kbps   int
	mu     sync.Mutex
	queue  []any
	ready  chan struct{}
	done   chan struct{}
	failed error
}

func newSlowLink(v *viewer, kbps int) *slowLink {
	l := &slowLink{v: v, kbps: kbps, ready: make(chan struct{}, 1), done: make(chan struct{})}
	go l.drain()
	return l
}

func (l *slowLink) push(message any) error {
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.failed != nil {
		return l.failed
	}
	l.queue = append(l.queue, message)
	select {
	case l.ready <- struct{}{}:
	default:
	}
	return nil
}

func (l *slowLink) drain() {
	for {
		select {
		case <-l.done:
			return
		case <-l.ready:
		}
		for {
			l.mu.Lock()
			if len(l.queue) == 0 {
				l.mu.Unlock()
				break
			}
			message := l.queue[0]
			l.queue = l.queue[1:]
			l.mu.Unlock()
			size := 0
			switch m := message.(type) {
			case []byte:
				size = len(m)
			case string:
				size = len(m)
			}
			time.Sleep(time.Duration(size*8) * time.Millisecond / time.Duration(l.kbps))
			l.v.mu.Lock()
			err := websocket.Message.Send(l.v.ws, message)
			l.v.mu.Unlock()
			if err != nil {
				l.mu.Lock()
				l.failed = err
				l.mu.Unlock()
				return
			}
		}
	}
}

func (l *slowLink) close() { close(l.done) }

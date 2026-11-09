package main

import (
	"errors"
	"log"
	"os"
	"slices"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

	"github.com/jezek/xgb"
	"github.com/jezek/xgb/damage"
	"github.com/jezek/xgb/randr"
	"github.com/jezek/xgb/xproto"
)

// hub is the one capture and encoder every viewer shares: a second viewer
// costs a send, not a second encode. It runs while anyone watches, so an
// unwatched desktop costs nothing, and streams the X screen at whatever size
// it is, starting over when RandR changes it. After voxl's hub.
//
// ponytail: the slowest viewer paces everyone (capture holds while any is
// behind, the bitrate cap is the lowest link's), as in voxl; per-viewer
// encoders again if a slow viewer beside a fast one matters.
type hub struct {
	display string
	screen  *screen // nil: viewers cannot resize the desktop (--resize)

	mu      sync.Mutex
	viewers map[*viewer]bool
	running bool

	// joined wakes the pipeline for a viewer that needs a keyframe, or one
	// that left. acked is every viewer's flow ack.
	joined chan struct{}
	acked  chan struct{}
	// size is the screen being streamed, width<<16 | height, for input bounds.
	size atomic.Uint32
}

func newHub(display string) *hub {
	return &hub{display: display, viewers: map[*viewer]bool{}, joined: make(chan struct{}, 1), acked: make(chan struct{}, 1)}
}

func wake(c chan struct{}) {
	select {
	case c <- struct{}{}:
	default:
	}
}

func (h *hub) join(v *viewer) {
	h.mu.Lock()
	select {
	case <-v.gone:
		h.mu.Unlock()
		return
	default:
	}
	h.viewers[v] = true
	if !h.running {
		h.running = true
		go h.run()
	}
	h.mu.Unlock()
	wake(h.joined)
}

func (h *hub) leave(v *viewer) {
	h.mu.Lock()
	delete(h.viewers, v)
	h.mu.Unlock()
	wake(h.joined)
}

func (h *hub) list() []*viewer {
	h.mu.Lock()
	defer h.mu.Unlock()
	list := make([]*viewer, 0, len(h.viewers))
	for v := range h.viewers {
		list = append(list, v)
	}
	return list
}

// done ends the pipeline once nobody watches; a viewer joining after this
// starts a new one.
func (h *hub) done() bool {
	h.mu.Lock()
	defer h.mu.Unlock()
	if len(h.viewers) > 0 {
		return false
	}
	h.running = false
	return true
}

func (h *hub) inScreen(x, y int16) bool {
	size := h.size.Load()
	return x >= 0 && y >= 0 && int(x) < int(size>>16) && int(y) < int(size&0xffff)
}

func (h *hub) run() {
	for {
		rebuild, err := h.capture()
		if err != nil {
			// The viewers are dropped and reconnect; the next one to join
			// starts a new pipeline.
			log.Printf("capture: %v", err)
			for _, v := range h.list() {
				v.fail()
				h.leave(v)
			}
			if h.done() {
				return
			}
			time.Sleep(time.Second)
			continue
		}
		if !rebuild {
			return
		}
	}
}

// capture streams the screen at its current size until nobody watches
// (false) or the size changes (true: start over at the new one).
func (h *hub) capture() (bool, error) {
	conn, err := xgb.NewConnDisplay(h.display)
	if err != nil {
		return false, err
	}
	defer conn.Close()
	if err := damage.Init(conn); err != nil {
		return false, err
	}
	if _, err := damage.QueryVersion(conn, 1, 1).Reply(); err != nil {
		return false, err
	}
	root := xproto.Setup(conn).DefaultScreen(conn).Root
	// Hear size changes before reading the size, so none falls between.
	// Without RandR the screen keeps its size: nothing to follow.
	if randr.Init(conn) == nil {
		if _, err := randr.QueryVersion(conn, 1, 3).Reply(); err == nil {
			randr.SelectInput(conn, root, randr.NotifyMaskScreenChange)
		}
	}
	geometry, err := xproto.GetGeometry(conn, xproto.Drawable(root)).Reply()
	if err != nil {
		return false, err
	}
	// 4:2:0 video: even sides.
	width, height := int(geometry.Width)&^1, int(geometry.Height)&^1
	h.size.Store(uint32(width)<<16 | uint32(height))
	read, release, err := grabber(conn, root, width, height)
	if err != nil {
		return false, err
	}
	defer release()
	// The rate the encoder budgets for: Chromium's 60 Hz, not the pacing
	// cap above it, so a bitrate cap allots each frame what a real frame gets.
	budgetFPS := encoderFPS
	if value, err := strconv.Atoi(os.Getenv("JUMPSEAT_ENCODER_FPS")); err == nil && value > 0 {
		budgetFPS = value // A/B runs
	}
	enc, err := newEncoder(width, height, budgetFPS)
	if err != nil {
		return false, err
	}
	defer func() { enc.close() }()

	damaged := make(chan struct{}, 1)
	resized := make(chan struct{}, 1)
	go func() {
		for {
			event, err := conn.WaitForEvent()
			if event == nil && err == nil {
				return
			}
			switch event.(type) {
			case damage.NotifyEvent:
				traceDamage()
				wake(damaged)
			case randr.ScreenChangeNotifyEvent:
				wake(resized)
			}
		}
	}()
	dmg, err := damage.NewDamageId(conn)
	if err != nil {
		return false, err
	}
	damage.Create(conn, dmg, xproto.Drawable(root), damage.ReportLevelNonEmpty)

	start := time.Now()
	var last time.Time
	interval := time.Second / time.Duration(fpsCap())
	stages := &stageTimes{since: time.Now()}
	defer stages.flush()
	// Viewers that have not had a keyframe of this encoder: they get the
	// size first and no frame before the keyframe.
	waiting := map[*viewer]bool{}
	greeted := map[*viewer]bool{}
	capKbps, encoded := 0, false
	// Once the screen is still, the encoder sharpens what it coded coarsely
	// in motion or under a cap, but only on frames it is given: refine feeds
	// it the still screen until a frame comes out with nothing left to code.
	refine := time.NewTimer(time.Hour)
	refine.Stop()
	defer refine.Stop()
	refines := 0
	grab := func() error {
		if wait := interval - time.Since(last); wait > 0 {
			time.Sleep(wait)
		}
		// Before the grab: damage that lands during it starts the next frame.
		damage.Subtract(conn, dmg, 0, 0)
		last = time.Now()
		pixels, err := read()
		if err != nil {
			// A resize lands in steps (grow, switch, shrink): a grab
			// between them asks for more than the screen holds.
			if now, gerr := xproto.GetGeometry(conn, xproto.Drawable(root)).Reply(); gerr == nil && (int(now.Width)&^1 != width || int(now.Height)&^1 != height) {
				return errResized
			}
			return err
		}
		grabbed := time.Now()
		au, key := enc.encode(pixels, width, height, time.Since(start).Milliseconds())
		encodedAt := time.Now()
		if au == nil {
			return nil
		}
		// An all-skip frame is a few dozen bytes: the picture is done.
		if len(au) > restDone && refines < maxRefines {
			if refines == 0 {
				refine.Reset(restDelay) // still yet, or between an animation's frames?
			} else {
				refine.Reset(interval)
			}
		}
		encoded = true
		for _, v := range h.list() {
			if !greeted[v] || (waiting[v] && !key) {
				continue
			}
			delete(waiting, v)
			ts := v.f.now()
			v.f.sent(ts, len(au))
			if v.frame(au, key, ts) != nil {
				v.fail()
			} else if tailProbes > 0 && len(au) < 1200 {
				v.tail()
			}
		}
		traceFrame(last, grabbed, encodedAt, time.Now(), len(au))
		stages.add(grabbed.Sub(last), enc.converted, encodedAt.Sub(grabbed)-enc.converted, time.Since(encodedAt), len(au))
		return nil
	}
	// greet tells viewers new to this encoder its size, and keyframes them.
	greet := func() error {
		fresh := false
		current := h.list()
		for v := range greeted {
			if !slices.Contains(current, v) {
				delete(greeted, v)
				delete(waiting, v)
			}
		}
		for _, v := range current {
			if !greeted[v] {
				greeted[v], waiting[v], fresh = true, true, true
				if v.text(map[string]any{"t": "size", "w": width, "h": height}) != nil {
					v.fail()
				}
			}
		}
		if !fresh {
			return nil
		}
		// A new encoder's first frame is a keyframe; the viewers already
		// watching decode it like any other.
		if encoded {
			next, err := newEncoder(width, height, budgetFPS)
			if err != nil {
				return err
			}
			enc.close()
			enc, encoded = next, false
			if capKbps > 0 {
				enc.setMaxKbps(capKbps)
			}
		}
		return grab()
	}

	// The bitrate follows the slowest link: on a slow one, quality gives way
	// before frame rate does. 80% leaves room for pointer and ack traffic.
	adapt := time.NewTicker(500 * time.Millisecond)
	defer adapt.Stop()
	// A change seen while a viewer is behind waits for its next ack; the
	// damage keeps accumulating, so that grab takes the newest screen.
	pending := false
	if err := greet(); errors.Is(err, errResized) {
		return true, nil
	} else if err != nil {
		return false, err
	}
	for {
		var err error
		select {
		case <-damaged:
			refines = 0
			if h.behind() {
				pending = true
				continue
			}
			pending = false
			err = grab()
		case <-refine.C:
			refines++
			if h.behind() {
				pending = true
				continue
			}
			err = grab()
		case <-h.acked:
			if pending && !h.behind() {
				pending = false
				err = grab()
			}
		case <-h.joined:
			err = greet()
		case <-resized:
			return true, nil
		case <-adapt.C:
			// A capture held for a frame now given up as lost resumes here.
			if pending && !h.behind() {
				pending = false
				if err = grab(); err != nil {
					break
				}
			}
			capKbps = h.adaptBitrate(enc, capKbps)
		}
		if errors.Is(err, errResized) {
			return true, nil
		}
		if err != nil {
			return false, err
		}
		if h.done() {
			return false, nil
		}
	}
}

// restDelay: this long without a change and the screen is still (an
// animation's frames are 16 ms apart). restDone: a frame this small changed
// nothing on screen, so it needs no more refining. maxRefines bounds
// refining one still screen.
const (
	restDelay  = 100 * time.Millisecond
	restDone   = 64
	maxRefines = 60
)

// errResized: the screen changed size under a grab; start over at the new one.
var errResized = errors.New("screen resized")

func (h *hub) behind() bool {
	for _, v := range h.list() {
		if v.f.stalled() {
			v.fail()
			continue
		}
		if v.f.behind() {
			return true
		}
	}
	return false
}

// adaptBitrate returns the encoder's new bitrate cap (0: none) given its
// current one.
func (h *hub) adaptBitrate(enc *encoder, capKbps int) int {
	target := 0
	for _, v := range h.list() {
		if kbps := v.f.linkKbps() * 8 / 10; kbps > 0 && (target == 0 || kbps < target) {
			target = kbps
		}
	}
	if target > 0 && target < minCapKbps {
		target = minCapKbps
	}
	if target == 0 && capKbps > 0 {
		// Under the cap the link is too quick to time: probe upward (like
		// BBR), lifting the cap once it is past any real link.
		target = capKbps * 3 / 2
		if target > maxCapKbps {
			target = 0
		}
	}
	if target != capKbps && (target == 0 || capKbps == 0 || abs(target-capKbps)*100 > capKbps*15) {
		enc.setMaxKbps(target)
		return target
	}
	return capKbps
}

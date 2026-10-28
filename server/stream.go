package main

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"image"
	"image/png"
	"io"
	"log"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/jezek/xgb"
	"github.com/jezek/xgb/damage"
	"github.com/jezek/xgb/shm"
	"github.com/jezek/xgb/xfixes"
	"github.com/jezek/xgb/xproto"
	"github.com/jezek/xgb/xtest"
	"golang.org/x/net/websocket"
	"golang.org/x/sys/unix"
)

// maxClipboard bounds text either way: a paste is a code or a URL, not a file.
const maxClipboard = 64 << 10

// viewer is one websocket: frames, pointer and clipboard messages share it.
type viewer struct {
	ws *websocket.Conn
	mu sync.Mutex
	// link, when set, is the benchmarks' slow link: messages queue without
	// bound and leave at its rate, like a router with a deep buffer.
	link *slowLink
}

// A viewer that cannot take a frame this fast is dropped, not waited on.
const viewerWrite = 10 * time.Second

// frame sends one access unit stamped with ts, the id its ack carries back.
func (v *viewer) frame(au []byte, key bool, ts int64) error {
	message := make([]byte, 9, 9+len(au))
	if key {
		message[0] = 1
	}
	binary.BigEndian.PutUint64(message[1:], uint64(ts))
	if v.link != nil {
		return v.link.push(append(message, au...))
	}
	v.mu.Lock()
	defer v.mu.Unlock()
	_ = v.ws.SetWriteDeadline(time.Now().Add(viewerWrite))
	return websocket.Message.Send(v.ws, append(message, au...))
}

func (v *viewer) text(message any) error {
	encoded, _ := json.Marshal(message)
	if v.link != nil {
		return v.link.push(string(encoded))
	}
	v.mu.Lock()
	defer v.mu.Unlock()
	_ = v.ws.SetWriteDeadline(time.Now().Add(viewerWrite))
	return websocket.Message.Send(v.ws, string(encoded))
}

// serve streams to one viewer until it leaves. Each viewer has its own X
// connection and encoder, so it starts with a keyframe of its own.
func serve(ws *websocket.Conn, display string) {
	ws.PayloadType = websocket.BinaryFrame
	conn, err := xgb.NewConnDisplay(display)
	if err != nil {
		log.Printf("x: %v", err)
		return
	}
	defer conn.Close()
	for _, init := range []func(*xgb.Conn) error{damage.Init, xfixes.Init, xtest.Init} {
		if err := init(conn); err != nil {
			log.Printf("x: %v", err)
			return
		}
	}
	// DAMAGE and XFIXES answer nothing until the client says its version.
	if _, err := xfixes.QueryVersion(conn, 5, 0).Reply(); err != nil {
		log.Printf("x: %v", err)
		return
	}
	if _, err := damage.QueryVersion(conn, 1, 1).Reply(); err != nil {
		log.Printf("x: %v", err)
		return
	}
	root := xproto.Setup(conn).DefaultScreen(conn).Root
	read, release, err := grabber(conn, root)
	if err != nil {
		log.Printf("x: %v", err)
		return
	}
	defer release()
	// The rate x264 budgets for: Chromium's 60 Hz, not the pacing cap above
	// it, so a bitrate cap allots each frame what a real frame gets.
	budgetFPS := encoderFPS
	if value, err := strconv.Atoi(os.Getenv("JUMPSEAT_ENCODER_FPS")); err == nil && value > 0 {
		budgetFPS = value // A/B runs
	}
	enc, err := newEncoder(width, height, budgetFPS)
	if err != nil {
		log.Print(err)
		return
	}
	defer enc.close()

	v := &viewer{ws: ws}
	if kbps, err := strconv.Atoi(os.Getenv("JUMPSEAT_LINK_KBPS")); err == nil && kbps > 0 {
		v.link = newSlowLink(v, kbps)
		defer v.link.close()
	}
	f := newFlow()
	f.off = os.Getenv("JUMPSEAT_FLOW") == "off"
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go func() {
		readInput(ctx, ws, newKeyboard(conn, root, display), f, display)
		cancel()
	}()

	damaged := make(chan struct{}, 1)
	copied := make(chan struct{}, 1)
	reshaped := make(chan struct{}, 1)
	clipboardText := make(chan string, 1)
	go func() {
		for {
			event, err := conn.WaitForEvent()
			if event == nil && err == nil {
				return
			}
			var ready chan struct{}
			switch event.(type) {
			case damage.NotifyEvent:
				ready = damaged
			case xfixes.SelectionNotifyEvent:
				ready = copied
			case xfixes.CursorNotifyEvent:
				ready = reshaped
			default:
				continue
			}
			select {
			case ready <- struct{}{}:
			default:
			}
		}
	}()
	dmg, err := damage.NewDamageId(conn)
	if err != nil {
		return
	}
	damage.Create(conn, dmg, xproto.Drawable(root), damage.ReportLevelNonEmpty)
	xfixes.SelectCursorInput(conn, root, xfixes.CursorNotifyMaskDisplayCursor)
	if clipboard, err := xproto.InternAtom(conn, false, 9, "CLIPBOARD").Reply(); err == nil {
		xfixes.SelectSelectionInput(conn, root, clipboard.Atom, xfixes.SelectionEventMaskSetSelectionOwner)
	}

	start := time.Now()
	var last time.Time
	interval := time.Second / time.Duration(fpsCap())
	stages := &stageTimes{since: time.Now()}
	defer stages.flush()
	grab := func() error {
		if wait := interval - time.Since(last); wait > 0 {
			time.Sleep(wait)
		}
		// Before the grab: damage that lands during it starts the next frame.
		damage.Subtract(conn, dmg, 0, 0)
		last = time.Now()
		pixels, err := read()
		if err != nil {
			return err
		}
		grabbed := time.Now()
		au, key := enc.encode(pixels, width, height, time.Since(start).Milliseconds())
		encoded := time.Now()
		if au == nil {
			return nil
		}
		ts := f.now()
		f.sent(ts, len(au))
		err = v.frame(au, key, ts)
		stages.add(grabbed.Sub(last), enc.converted, encoded.Sub(grabbed)-enc.converted, time.Since(encoded), len(au))
		return err
	}

	if err := grab(); err != nil {
		return
	}
	// The cursor's own image, so the viewer shows an I-beam over text and a
	// hand over links instead of a fixed arrow (Xvfb draws no cursor into
	// frames). Sent again whenever it changes.
	var cursorSerial uint32
	sendCursor := func() error {
		cursor, err := xfixes.GetCursorImage(conn).Reply()
		if err != nil || cursor.Width == 0 || cursor.Height == 0 || (cursor.CursorSerial == cursorSerial && cursorSerial != 0) {
			return nil
		}
		cursorSerial = cursor.CursorSerial
		encoded, err := cursorPNG(cursor.CursorImage, int(cursor.Width), int(cursor.Height))
		if err != nil {
			return nil
		}
		return v.text(map[string]any{"t": "cursor", "png": encoded, "x": cursor.Xhot, "y": cursor.Yhot})
	}
	if err := sendCursor(); err != nil {
		return
	}
	pointer := time.NewTicker(pointerPoll)
	defer pointer.Stop()
	// What the agent is doing (the agent-status file POST /agent/status writes), relayed as
	// it changes; a new viewer gets it on its first tick.
	agent := &agentStatusWatch{}
	statusTick := time.NewTicker(statusPoll)
	defer statusTick.Stop()
	// The bitrate follows the link: on a slow one, quality gives way before
	// frame rate does (fewer frames make each one bigger: a scroll outruns
	// x264's motion search). 80% leaves room for pointer and ack traffic.
	adapt := time.NewTicker(500 * time.Millisecond)
	defer adapt.Stop()
	capKbps := 0
	var px, py int16 = -1, -1
	// A change seen while the viewer is behind waits for its next ack; the
	// damage keeps accumulating, so that grab takes the newest screen.
	pending := false
	for {
		var err error
		select {
		case <-ctx.Done():
			return
		case <-damaged:
			if f.behind() {
				pending = true
				continue
			}
			pending = false
			err = grab()
		case <-f.acked:
			if pending && !f.behind() {
				pending = false
				err = grab()
			}
		case <-copied:
			// Off this loop: a clipboard owner that never answers must not
			// freeze the stream.
			go func() {
				if text := readClipboard(display); text != "" {
					select {
					case clipboardText <- text:
					default:
					}
				}
			}()
		case text := <-clipboardText:
			err = v.text(map[string]any{"t": "c", "text": text})
		case <-reshaped:
			err = sendCursor()
		case <-adapt.C:
			// A capture held for a frame now given up as lost resumes here.
			if pending && !f.behind() {
				pending = false
				if err = grab(); err != nil {
					return
				}
			}
			target := f.linkKbps() * 8 / 10
			if target > 0 && target < minCapKbps {
				target = minCapKbps
			}
			if target == 0 && capKbps > 0 {
				// Under the cap the link is too quick to time: probe upward
				// (like BBR), lifting the cap once it is past any real link.
				target = capKbps * 3 / 2
				if target > maxCapKbps {
					target = 0
				}
			}
			if target != capKbps && (target == 0 || capKbps == 0 || abs(target-capKbps)*100 > capKbps*15) {
				capKbps = target
				enc.setMaxKbps(capKbps)
			}
		case <-statusTick.C:
			if status, changed := agent.changed(); changed {
				err = v.text(map[string]any{"t": "status", "kind": status.Kind, "text": status.Text})
			}
		case <-pointer.C:
			at, qerr := xproto.QueryPointer(conn, root).Reply()
			if qerr != nil || (at.RootX == px && at.RootY == py) {
				continue
			}
			px, py = at.RootX, at.RootY
			err = v.text(map[string]any{"t": "p", "x": px, "y": py})
		}
		if err != nil {
			return
		}
	}
}

// grabber reads the root window through MIT-SHM: the X server copies the
// frame into memory shared with this process, not down the socket.
func grabber(conn *xgb.Conn, root xproto.Window) (func() ([]byte, error), func(), error) {
	if err := shm.Init(conn); err != nil {
		return nil, nil, err
	}
	id, err := unix.SysvShmGet(unix.IPC_PRIVATE, width*height*4, unix.IPC_CREAT|0o600)
	if err != nil {
		return nil, nil, fmt.Errorf("shmget: %w", err)
	}
	mem, err := unix.SysvShmAttach(id, 0, 0)
	// Marked for removal now, so it cannot leak: it lives until both sides
	// detach.
	_, _ = unix.SysvShmCtl(id, unix.IPC_RMID, nil)
	if err != nil {
		return nil, nil, fmt.Errorf("shmat: %w", err)
	}
	seg, err := shm.NewSegId(conn)
	if err == nil {
		err = shm.AttachChecked(conn, seg, uint32(id), false).Check()
	}
	if err != nil {
		_ = unix.SysvShmDetach(mem)
		return nil, nil, fmt.Errorf("shm attach: %w", err)
	}
	read := func() ([]byte, error) {
		if _, err := shm.GetImage(conn, xproto.Drawable(root), 0, 0, width, height, ^uint32(0), xproto.ImageFormatZPixmap, seg, 0).Reply(); err != nil {
			return nil, err
		}
		return mem, nil
	}
	release := func() {
		shm.Detach(conn, seg)
		_ = unix.SysvShmDetach(mem)
	}
	return read, release, nil
}

func readClipboard(display string) string {
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, "xclip", "-o", "-selection", "clipboard")
	cmd.Env = append(os.Environ(), "DISPLAY="+display)
	stdout, err := cmd.StdoutPipe()
	if err != nil || cmd.Start() != nil {
		return ""
	}
	out, _ := io.ReadAll(io.LimitReader(stdout, maxClipboard+1))
	_ = cmd.Process.Kill()
	_ = cmd.Wait()
	if len(out) > maxClipboard {
		return ""
	}
	return strings.ToValidUTF8(string(out), "")
}

// writeClipboard sets the VM's clipboard and returns once it is set: xclip's
// first process exits only after its background copy owns the selection.
// Starting it without waiting let the Ctrl+V that follows a paste reach the
// VM first, pasting whatever the clipboard held before.
func writeClipboard(display, text string) {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, "xclip", "-selection", "clipboard")
	cmd.Env = append(os.Environ(), "DISPLAY="+display)
	cmd.Stdin = strings.NewReader(text)
	_ = cmd.Run()
}

// stageTimes sums where a viewer's frames spend their time and logs it every
// 10 s while frames flow: the profile the streamer's tuning starts from
// (the A/B harness prints it).
type stageTimes struct {
	since                       time.Time
	frames, bytes               int
	grab, convert, encode, send time.Duration
}

func (s *stageTimes) add(grab, convert, encode, send time.Duration, bytes int) {
	s.frames++
	s.bytes += bytes
	s.grab += grab
	s.convert += convert
	s.encode += encode
	s.send += send
	if time.Since(s.since) >= 10*time.Second {
		s.flush()
	}
}

func (s *stageTimes) flush() {
	if s.frames == 0 {
		return
	}
	per := func(d time.Duration) float64 { return float64(d.Microseconds()) / float64(s.frames) / 1000 }
	log.Printf("stages: %d frames in %.1fs, %.1f fps, ms/frame grab %.2f convert %.2f encode %.2f send %.2f, %.1f KB/frame",
		s.frames, time.Since(s.since).Seconds(), float64(s.frames)/time.Since(s.since).Seconds(),
		per(s.grab), per(s.convert), per(s.encode), per(s.send), float64(s.bytes)/float64(s.frames)/1024)
	*s = stageTimes{since: time.Now()}
}

// fpsCap is maxFPS unless JUMPSEAT_MAX_FPS overrides it (BENCHMARKS.md).
func fpsCap() int {
	if value, err := strconv.Atoi(os.Getenv("JUMPSEAT_MAX_FPS")); err == nil && value > 0 {
		return value
	}
	return maxFPS
}

// encoderFPS is the frame rate the encoder's rate control assumes.
const encoderFPS = 60

// maxCapKbps is where an upward probe stops capping: far above what any
// screen here needs (anim peaks near 4 Mbit/s).
const maxCapKbps = 20_000

// minCapKbps is the lowest cap: a screen stays readable even when the link
// estimate is wrong low.
const minCapKbps = 500

func abs(n int) int {
	if n < 0 {
		return -n
	}
	return n
}

// cursorPNG is the cursor's premultiplied ARGB as a base64 PNG.
func cursorPNG(argb []uint32, width, height int) (string, error) {
	img := image.NewNRGBA(image.Rect(0, 0, width, height))
	for i, pixel := range argb[:width*height] {
		a := uint8(pixel >> 24)
		r, g, b := uint8(pixel>>16), uint8(pixel>>8), uint8(pixel)
		if a > 0 && a < 255 {
			r, g, b = uint8(uint32(r)*255/uint32(a)), uint8(uint32(g)*255/uint32(a)), uint8(uint32(b)*255/uint32(a))
		}
		img.Pix[i*4], img.Pix[i*4+1], img.Pix[i*4+2], img.Pix[i*4+3] = r, g, b, a
	}
	var out bytes.Buffer
	if err := png.Encode(&out, img); err != nil {
		return "", err
	}
	return base64.StdEncoding.EncodeToString(out.Bytes()), nil
}

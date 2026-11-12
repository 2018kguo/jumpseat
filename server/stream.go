package main

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"errors"
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
	"sync/atomic"
	"time"

	"github.com/jezek/xgb"
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
	f  *flow
	// gone is closed when a send fails: the viewer is dropped.
	gone     chan struct{}
	goneOnce sync.Once
	// out is what the writer sends, in order. Frames come from the shared
	// pipeline, which must never wait on one viewer's socket.
	out chan any
	// joined: (re)joined the hub since the pipeline last greeted it; a viewer
	// shown again after hiding missed frames and needs a keyframe.
	joined atomic.Bool
}

func (v *viewer) fail() { v.goneOnce.Do(func() { close(v.gone) }) }

// A viewer that cannot take a frame this fast is dropped, not waited on.
const viewerWrite = 10 * time.Second

// viewerQueue is how many messages may wait for a viewer's socket. Flow
// control keeps a healthy viewer's queue to a few frames; one that fills it
// has stopped reading, and is dropped instead of holding up the others.
const viewerQueue = 256

var errViewerBehind = errors.New("viewer stopped reading")

func (v *viewer) send(message any) error {
	select {
	case <-v.gone:
		return errViewerBehind
	case v.out <- message:
		return nil
	default:
		v.fail()
		return errViewerBehind
	}
}

// write sends the queue until the viewer is gone.
func (v *viewer) write() {
	for {
		select {
		case <-v.gone:
			return
		case message := <-v.out:
			_ = v.ws.SetWriteDeadline(time.Now().Add(viewerWrite))
			if err := websocket.Message.Send(v.ws, message); err != nil {
				v.fail()
				return
			}
		}
	}
}

// frame sends one access unit stamped with ts, the id its ack carries back.
func (v *viewer) frame(au []byte, key bool, ts int64) error {
	message := make([]byte, 9, 9+len(au))
	if key {
		message[0] = 1
	}
	binary.BigEndian.PutUint64(message[1:], uint64(ts))
	return v.send(append(message, au...))
}

func (v *viewer) text(message any) error {
	encoded, _ := json.Marshal(message)
	return v.send(string(encoded))
}

// tailProbes: after a frame that fits one packet (a keystroke's), send this
// many tiny messages 3 ms apart. When the frame's packet is lost, the acks
// for the packets after it report the loss, and TCP retransmits after about
// a round trip instead of waiting for its tail-loss timer. With 3% loss
// (bench/wifi): keystrokes over 150 ms 30 -> 3 in 800, p99 281 -> 93 ms.
// JUMPSEAT_TAIL sets the count (0: off).
//
// Only after a frame that follows tailQuiet of stillness: in motion the next
// frame, 16 ms on, does the same, and probing every small frame cost 40% of
// the server's CPU scrolling (0.37 -> 0.23 cores, bench/compare).
var tailProbes = envInt("JUMPSEAT_TAIL", 3)

const tailQuiet = 50 * time.Millisecond

func envInt(name string, fallback int) int {
	if n, err := strconv.Atoi(os.Getenv(name)); err == nil {
		return n
	}
	return fallback
}

// ping asks the viewer to answer "q" at once (flow.pong).
func (v *viewer) ping() error { return v.text(map[string]any{"t": "q", "ts": v.f.now()}) }

func (v *viewer) tail() {
	for i := 1; i <= tailProbes; i++ {
		time.AfterFunc(time.Duration(3*i)*time.Millisecond, func() { _ = v.text(map[string]string{"t": "n"}) })
	}
}

// serve is one viewer: the hub sends it frames, and this sends its cursor,
// pointer, clipboard and the agent's status, and replays its input, on an X
// connection of its own.
func serve(ws *websocket.Conn, h *hub) {
	ws.PayloadType = websocket.BinaryFrame
	conn, err := xgb.NewConnDisplay(h.display)
	if err != nil {
		log.Printf("x: %v", err)
		return
	}
	defer conn.Close()
	for _, init := range []func(*xgb.Conn) error{xfixes.Init, xtest.Init} {
		if err := init(conn); err != nil {
			log.Printf("x: %v", err)
			return
		}
	}
	// XFIXES answers nothing until the client says its version.
	if _, err := xfixes.QueryVersion(conn, 5, 0).Reply(); err != nil {
		log.Printf("x: %v", err)
		return
	}
	root := xproto.Setup(conn).DefaultScreen(conn).Root

	v := &viewer{ws: ws, f: newFlow(), gone: make(chan struct{}), out: make(chan any, viewerQueue)}
	defer v.fail()
	go v.write()
	// Whether this server has sound, ahead of any frame. A client asks for
	// sound only after this: a server from before sound would take "s" for
	// the user driving.
	if err := v.text(map[string]any{"t": "s", "ok": h.audio.source != ""}); err != nil {
		return
	}
	v.f.acked = h.acked
	// A ping ahead of the first frame times the bare link (flow.pong).
	if err := v.ping(); err != nil {
		return
	}
	// Joined before the input reader starts, so a hide it reads leaves after.
	h.join(v)
	// Gone before it leaves: a late "v" from the input reader cannot join it again.
	defer func() { v.fail(); h.leave(v); h.audio.set(v, false) }()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go func() {
		readInput(ctx, ws, newKeyboard(conn, root, h.display), v.f, h, func(shown bool) {
			if shown {
				v.f.resume()
				h.join(v)
			} else {
				h.leave(v)
			}
		}, func(on bool) { h.audio.set(v, on) })
		cancel()
	}()

	copied := make(chan struct{}, 1)
	reshaped := make(chan struct{}, 1)
	clipboardText := make(chan string, 1)
	go func() {
		for {
			event, err := conn.WaitForEvent()
			if event == nil && err == nil {
				return
			}
			switch event.(type) {
			case xfixes.SelectionNotifyEvent:
				wake(copied)
			case xfixes.CursorNotifyEvent:
				wake(reshaped)
			}
		}
	}()
	xfixes.SelectCursorInput(conn, root, xfixes.CursorNotifyMaskDisplayCursor)
	if clipboard, err := xproto.InternAtom(conn, false, 9, "CLIPBOARD").Reply(); err == nil {
		xfixes.SelectSelectionInput(conn, root, clipboard.Atom, xfixes.SelectionEventMaskSetSelectionOwner)
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
	// Pinged again within rttWindow, so the floor stays the bare link's.
	pings := time.NewTicker(time.Second)
	defer pings.Stop()
	var px, py int16 = -1, -1
	for {
		var err error
		select {
		case <-ctx.Done():
			return
		case <-v.gone:
			return
		case <-copied:
			// Off this loop: a clipboard owner that never answers must not
			// freeze the viewer.
			go func() {
				if text := readClipboard(h.display); text != "" {
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
		case <-pings.C:
			err = v.ping()
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
func grabber(conn *xgb.Conn, root xproto.Window, width, height int) (func() ([]byte, error), func(), error) {
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
		if _, err := shm.GetImage(conn, xproto.Drawable(root), 0, 0, uint16(width), uint16(height), ^uint32(0), xproto.ImageFormatZPixmap, seg, 0).Reply(); err != nil {
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

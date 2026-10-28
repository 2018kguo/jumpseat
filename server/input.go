package main

import (
	"context"
	"encoding/json"
	"os"
	"os/exec"
	"strconv"
	"sync/atomic"
	"time"

	"github.com/jezek/xgb"
	"github.com/jezek/xgb/xproto"
	"github.com/jezek/xgb/xtest"
	"golang.org/x/net/websocket"
)

// input is one viewer message. t is "k" (key; key is KeyboardEvent.key),
// "m" (pointer move), "b" (button b, 1-3, down or up at x,y), "w" (wheel
// step, dy < 0 up), "c" (paste text into the VM's clipboard), "r" (release
// every key and button this viewer holds), "a" (frame ts was displayed, see
// flow) or "h" (heartbeat).
type input struct {
	T    string `json:"t"`
	Key  string `json:"key"`
	X    int16  `json:"x"`
	Y    int16  `json:"y"`
	B    byte   `json:"b"`
	Down bool   `json:"down"`
	DY   int    `json:"dy"`
	DX   int    `json:"dx"`
	Text string `json:"text"`
	TS   int64  `json:"ts"`
}

func inScreen(x, y int16) bool { return x >= 0 && y >= 0 && x < width && y < height }

// A viewer that sends nothing for this long is gone (it heartbeats every 5 s):
// a sleeping laptop must not hold an X connection, shm and an encoder forever.
const viewerSilence = 20 * time.Second

// readInput replays the viewer's input through XTest: no process per event,
// so a key press is on screen in one frame.
func readInput(ctx context.Context, ws *websocket.Conn, k *keyboard, f *flow, display string) {
	// Whatever this viewer still holds is let go when it leaves, so the
	// agent's next click is not a drag and no key repeats forever.
	defer k.releaseAll()
	for ctx.Err() == nil {
		var raw []byte
		_ = ws.SetReadDeadline(time.Now().Add(viewerSilence))
		if err := websocket.Message.Receive(ws, &raw); err != nil {
			return
		}
		var in input
		if json.Unmarshal(raw, &in) != nil {
			continue
		}
		switch in.T {
		case "a", "h":
		case "r":
			// The user let go (Stop controlling, or left the view): the
			// agent need not wait out the lease.
			_ = os.Remove(humanInputFile)
			lastStamp.Store(0)
		default:
			stampHumanInput()
		}
		switch in.T {
		case "k":
			k.press(in.Key, in.Down)
		case "m":
			if inScreen(in.X, in.Y) {
				xtest.FakeInput(k.conn, xproto.MotionNotify, 0, 0, k.root, in.X, in.Y, 0)
			}
		case "b":
			if in.B < 1 || in.B > 3 || !inScreen(in.X, in.Y) {
				continue
			}
			xtest.FakeInput(k.conn, xproto.MotionNotify, 0, 0, k.root, in.X, in.Y, 0)
			k.button(in.B, in.Down)
		case "w":
			// X wheel buttons: 4 up, 5 down, 6 left, 7 right.
			for _, step := range [][2]int{{in.DY, 4}, {in.DX, 6}} {
				if step[0] == 0 {
					continue
				}
				button := byte(step[1])
				if step[0] > 0 {
					button++
				}
				xtest.FakeInput(k.conn, xproto.ButtonPress, button, 0, k.root, 0, 0, 0)
				xtest.FakeInput(k.conn, xproto.ButtonRelease, button, 0, k.root, 0, 0, 0)
			}
		case "c":
			if len(in.Text) <= maxClipboard {
				writeClipboard(display, in.Text)
			}
		case "r":
			k.releaseAll()
		case "a":
			f.ack(in.TS)
			continue
		}
		k.conn.Sync()
	}
}

// keyAt is where a keysym sits on the VM's keymap: its keycode, whether it
// needs Shift (level 1), and whether Shift changes what that key types.
type keyAt struct {
	code      xproto.Keycode
	shifted   bool
	shiftDiff bool
}

// keyboard maps keysyms to the keycodes XTest presses, and remembers what
// this viewer holds down.
type keyboard struct {
	conn      *xgb.Conn
	root      xproto.Window
	display   string
	codes     map[xproto.Keysym]keyAt
	keys      map[xproto.Keycode]bool
	buttons   map[byte]bool
	lastPress time.Time
}

// buttonGap is the least time between two button presses reaching X.
const buttonGap = 15 * time.Millisecond

func (k *keyboard) button(b byte, down bool) {
	if !down && !k.buttons[b] {
		return
	}
	// Presses that arrive bunched (network jitter, or a scripted
	// double-click) would reach X in the same millisecond, which Chromium
	// does not count as a double-click; keep them apart as xdotool does.
	if down {
		if gap := buttonGap - time.Since(k.lastPress); gap > 0 {
			time.Sleep(gap)
		}
		k.lastPress = time.Now()
	}
	kind := byte(xproto.ButtonPress)
	if !down {
		kind = xproto.ButtonRelease
	}
	xtest.FakeInput(k.conn, kind, b, 0, k.root, 0, 0, 0)
	k.buttons[b] = down
}

func (k *keyboard) releaseAll() {
	for code, down := range k.keys {
		if down {
			xtest.FakeInput(k.conn, xproto.KeyRelease, byte(code), 0, k.root, 0, 0, 0)
		}
	}
	for b, down := range k.buttons {
		if down {
			xtest.FakeInput(k.conn, xproto.ButtonRelease, b, 0, k.root, 0, 0, 0)
		}
	}
	clear(k.keys)
	clear(k.buttons)
	k.conn.Sync()
}

func newKeyboard(conn *xgb.Conn, root xproto.Window, display string) *keyboard {
	setup := xproto.Setup(conn)
	count := byte(setup.MaxKeycode - setup.MinKeycode + 1)
	k := &keyboard{conn: conn, root: root, display: display, codes: map[xproto.Keysym]keyAt{}, keys: map[xproto.Keycode]bool{}, buttons: map[byte]bool{}}
	mapping, err := xproto.GetKeyboardMapping(conn, setup.MinKeycode, count).Reply()
	if err != nil {
		return k
	}
	per := int(mapping.KeysymsPerKeycode)
	for i := 0; i < int(count); i++ {
		// Unshifted and shifted symbols: the viewer sends Shift itself.
		for j := 0; j < per && j < 2; j++ {
			sym := mapping.Keysyms[i*per+j]
			if _, seen := k.codes[sym]; sym != 0 && !seen {
				unshifted, shifted := mapping.Keysyms[i*per], xproto.Keysym(0)
				if per > 1 {
					shifted = mapping.Keysyms[i*per+1]
				}
				k.codes[sym] = keyAt{
					code:      setup.MinKeycode + xproto.Keycode(i),
					shifted:   j == 1,
					shiftDiff: shifted != 0 && shifted != unshifted,
				}
			}
		}
	}
	return k
}

var namedKeys = map[string]xproto.Keysym{
	"Enter": 0xff0d, "Backspace": 0xff08, "Tab": 0xff09, "Escape": 0xff1b, "Delete": 0xffff,
	"ArrowLeft": 0xff51, "ArrowUp": 0xff52, "ArrowRight": 0xff53, "ArrowDown": 0xff54,
	"Home": 0xff50, "End": 0xff57, "PageUp": 0xff55, "PageDown": 0xff56, "Insert": 0xff63,
	// No CapsLock: the viewer's own layout already applied it to the
	// characters it sends; locking the VM too would invert their case.
	"Shift": 0xffe1, "Control": 0xffe3, "Alt": 0xffe9, "Meta": 0xffeb,
	"F1": 0xffbe, "F2": 0xffbf, "F3": 0xffc0, "F4": 0xffc1, "F5": 0xffc2, "F6": 0xffc3,
	"F7": 0xffc4, "F8": 0xffc5, "F9": 0xffc6, "F10": 0xffc7, "F11": 0xffc8, "F12": 0xffc9,
}

// keysym is the X keysym for a KeyboardEvent.key: named keys from the table,
// a Latin-1 character as itself, any other as 0x1000000 + its code point.
func keysym(key string) xproto.Keysym {
	if sym, ok := namedKeys[key]; ok {
		return sym
	}
	runes := []rune(key)
	if len(runes) != 1 {
		return 0
	}
	if runes[0] < 0x100 {
		return xproto.Keysym(runes[0])
	}
	return xproto.Keysym(0x1000000 + runes[0])
}

// press sends one key event. The viewer's own layout already chose the
// character (KeyboardEvent.key), so a printable key goes in at the level the
// VM's keymap has it: "@" from a German AltGr+Q or a US Shift+2 both reach
// the VM as "@", Shift added or lifted around the press as that needs.
func (k *keyboard) press(key string, down bool) {
	sym := keysym(key)
	if sym == 0 {
		return
	}
	at, ok := k.codes[sym]
	if !ok {
		// Not on the keymap ("é", "中"): xdotool binds a spare keycode to it.
		if down {
			cmd := exec.Command("xdotool", "key", "--", "0x"+strconv.FormatUint(uint64(sym), 16))
			cmd.Env = append(os.Environ(), "DISPLAY="+k.display)
			_ = cmd.Run()
		}
		return
	}
	if !down {
		if k.keys[at.code] {
			xtest.FakeInput(k.conn, xproto.KeyRelease, byte(at.code), 0, k.root, 0, 0, 0)
			k.keys[at.code] = false
		}
		return
	}
	// A key already down (the viewer does not send its browser's repeats;
	// X repeats a held key itself) is pressed afresh.
	if k.keys[at.code] {
		xtest.FakeInput(k.conn, xproto.KeyRelease, byte(at.code), 0, k.root, 0, 0, 0)
	}
	shift := k.codes[namedKeys["Shift"]].code
	printable := len([]rune(key)) == 1
	held := k.keys[shift]
	// Only where Shift changes the character: Shift+Space stays Shift+Space.
	if printable && at.shiftDiff && at.shifted != held {
		kind := byte(xproto.KeyPress)
		if held {
			kind = xproto.KeyRelease
		}
		xtest.FakeInput(k.conn, kind, byte(shift), 0, k.root, 0, 0, 0)
		defer func() {
			kind := byte(xproto.KeyRelease)
			if held {
				kind = xproto.KeyPress
			}
			xtest.FakeInput(k.conn, kind, byte(shift), 0, k.root, 0, 0, 0)
		}()
	}
	xtest.FakeInput(k.conn, xproto.KeyPress, byte(at.code), 0, k.root, 0, 0, 0)
	k.keys[at.code] = true
}

// humanInputFile holds when the user last gave input from a viewer (unix
// ms): GET /agent/lease reads it, and so can any process on the machine, so
// the agent's own input waits while the user drives. Set from --dir.
var humanInputFile = "/run/agentview/human-input"

var lastStamp atomic.Int64

// stampHumanInput records now as the user's last input, at most every
// 250 ms; written whole then renamed, so the daemon never reads half.
func stampHumanInput() {
	now := time.Now().UnixMilli()
	if now-lastStamp.Load() < 250 {
		return
	}
	lastStamp.Store(now)
	tmp := humanInputFile + ".tmp"
	if os.WriteFile(tmp, []byte(strconv.FormatInt(now, 10)), 0o600) == nil {
		_ = os.Rename(tmp, humanInputFile)
	}
}

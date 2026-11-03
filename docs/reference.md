# Reference

The server, its HTTP and websocket protocol, and the browser client.

## Server

```sh
jumpseat [--listen unix:/run/jumpseat/stream.sock | host:port] [--token T] [--display :0] [--dir /run/jumpseat] [--resize]
jumpseat --record FILE [--frames 300] [--display :0]
```

| Flag | Environment | Default | Use |
|---|---|---|---|
| `--listen` | | `unix:/run/jumpseat/stream.sock` | `unix:/path` for trusted callers (behind your own proxy and auth), or `host:port`, which requires a token. |
| `--token` | `JUMPSEAT_TOKEN` | none | Bearer token for every route on a TCP listener. |
| `--display` | | `:0` | The X display to stream and drive. |
| `--dir` | | `/run/jumpseat` | Where the agent status and user-input files live. |
| `--resize` | | off | Let viewers resize the desktop to their window, through RandR, up to the size Xvfb started at. Off by default ([why](../README.md#how-it-works)). |
| `--record` | | off | Write the next `--frames` changed frames as raw BGRX to this file, for [bench/](../bench/README.md), and exit. |

Tuning and measurement knobs are in [operating.md](operating.md#environment).

## HTTP

With a TCP listener every route needs the token: `Authorization: Bearer T`,
or `?token=T` where a browser cannot set headers (its websocket).

| Route | Body / answer |
|---|---|
| `GET /` | The demo viewer page. |
| `GET /stream` | The websocket below. |
| `POST /agent/status` | `{"kind": "acting" \| "waiting" \| "handoff" \| "idle", "text": "Opening the inbox"}`, shown to every viewer. |
| `GET /agent/lease` | `{"driving": bool, "lastInputMs": n, "answer": "" \| "done" \| "skip"}`: whether the user is driving (input in the last 5 s), when they last gave input (Unix ms, 0 if never), and how they answered a handoff. |
| `POST /agent/answer` | `{"reply": "done" \| "skip"}`. The demo viewer's Done button sends `done`. |
| `GET /agent/screenshot` | The screen as a PNG, at its current size. |
| `GET /agent/wait` | `{"changed": bool, "timedOut": bool, "waitedMs": n}` once the screen has been still for `idle` ms (default 300, counted from the request), or at the first change with `?change`; `timeout` ms gives up (default 10 000, at most 60 000). |

An agent posts `acting` with a caption before each step. For a step only a
person can do (a sign-in, a captcha) it posts `handoff`, then polls
`/agent/lease` until `answer` is `done` or `skip`. Before it clicks or types,
it checks `driving`: while true, the user is at the keyboard. After a step it
calls `/agent/wait`. A blinking caret redraws about twice a second: keep
`idle` under 500 ms.

## Websocket: `GET /stream`

One websocket carries the screen to the viewer and the viewer's input back.

### Server to viewer

**Binary messages: one video frame each.**

| Bytes | Field |
|---|---|
| 0 | `1` if the frame is a keyframe, else `0` |
| 1–8 | frame id: big-endian uint64, ms since this connection began, echoed back in `a` |
| 9– | one H.264 access unit, Annex B, constrained baseline (`avc1.42E028`), at the size of the last `size` message |

Every viewer shares one encoder. Each connection starts with a `size`
message and a keyframe, and so does every change of the screen's size. The
viewer acks every frame it shows ([flow control](operating.md#resources)).

**Text messages: JSON.** Ignore unknown ones.

| `t` | Fields | Meaning |
|---|---|---|
| `size` | `w`, `h` | The desktop's size. The next frame is a keyframe at that size. |
| `status` | `kind`, `text` | What the agent is doing: `acting` (caption), `waiting` (on the user), `handoff` (the user's turn), `idle`. |
| `cursor` | `png` (base64), `x`, `y` (hotspot) | The screen's cursor image, when it changes. |
| `p` | `x`, `y` | The pointer moved. |
| `c` | `text` | The desktop's clipboard changed. |
| `n` | | Nothing: sent after a small frame so TCP notices a lost packet sooner. |

### Viewer to server

Text messages, JSON:

| `t` | Fields | Meaning |
|---|---|---|
| `a` | `ts` | Frame `ts` was shown (the flow-control ack). |
| `h` | | Heartbeat every 5 s; a viewer silent for 20 s is dropped. |
| `k` | `key` (`KeyboardEvent.key`), `down` | A key, pressed at the right keymap level. |
| `m` | `x`, `y` | Move the pointer. |
| `b` | `b` (1 left, 2 middle, 3 right), `down`, `x`, `y` | A button. |
| `w` | `sx`, `sy` (desktop pixels), or `dx`, `dy` (whole wheel clicks, at most 20 a message; the bundled client sends only `sx`, `sy`) | A scroll: pixels from a trackpad, wheel or two fingers, or a wheel step. Pixels scroll smoothly on an Xvfb with the [scroll patch](../docker/patches/xorg-server-xtest-scroll.patch) and add up into wheel clicks on any other X server. |
| `c` | `text` (≤ 64 KiB) | Paste this into the desktop's clipboard. |
| `r` | | Release every key and button this viewer holds. |
| `z` | `w`, `h` | Resize the desktop to this size (with `--resize`; otherwise ignored). The server rounds it to a size the screen can take. |

Every message except `a`, `h`, `z` and `r` counts as the user driving; `r`
ends it at once, so the agent need not wait out the 5 s.

## Browser client

[client/jumpseat.ts](../client/jumpseat.ts) has no
dependencies.

```ts
import { connect, supportsStream } from "./jumpseat"; // or dist/ from `bun run build`

if (!(await supportsStream())) showFallback(); // no WebCodecs H.264 decoder
const view = connect(element, `wss://host/stream?token=${token}`, {
  connected: () => {},  // the first frame arrived
  dropped: () => {},    // the socket closed; connect again to retry
  status: ({ kind, text }) => {},
  unsupported: () => {}, // the decoder refused the stream
}, { resize: false });   // true: the desktop takes the element's size (needs --resize)
view.setControlling(true); // the user's input now reaches the desktop
view.close();
```

`connect` puts a letterboxed canvas in `element`. While `setControlling` is
false the viewer only watches, and shows the agent's pointer; while true,
keys, pointer, wheel and paste go to the desktop. On a touch screen a tap
clicks, a long press right-clicks, one finger drags and two fingers scroll.

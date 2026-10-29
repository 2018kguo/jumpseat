# Reference

The server, its HTTP and websocket protocol, and the browser client.

## Server

```sh
jumpseat [--listen unix:/run/jumpseat/stream.sock | host:port] [--token T] [--display :0] [--dir /run/jumpseat]
jumpseat --record FILE [--frames 300] [--display :0]
```

| Flag | Environment | Default | Use |
|---|---|---|---|
| `--listen` | | `unix:/run/jumpseat/stream.sock` | `unix:/path` for trusted callers (behind your own proxy and auth), or `host:port`, which requires a token. |
| `--token` | `JUMPSEAT_TOKEN` | none | Bearer token for every route on a TCP listener. |
| `--display` | | `:0` | The X display to stream and drive. |
| `--dir` | | `/run/jumpseat` | Where the agent status and user-input files live. |
| `--record` | | off | Write the next `--frames` changed frames as raw BGRX to this file, for [bench/](../bench/README.md), and exit. |

Tuning and measurement knobs are in [operating.md](operating.md#environment).

## HTTP

With a TCP listener every route needs the token: `Authorization: Bearer T`,
or `?token=T` where a browser cannot set headers (its websocket).

| Route | Body / answer |
|---|---|
| `GET /` | The demo viewer page. |
| `GET /stream` | The websocket below. |
| `POST /agent/status` | `{"kind": "acting" \| "waiting" \| "handoff" \| "idle", "text": "Opening LinkedIn"}`, shown to every viewer. |
| `GET /agent/lease` | `{"driving": bool, "lastInputMs": n, "answer": "" \| "done" \| "skip"}`: whether the user is driving (input in the last 5 s), and how they answered a handoff. |
| `POST /agent/answer` | `{"reply": "done" \| "skip"}`, sent by the viewer's Done / Skip. |

An agent posts `acting` with a caption before each step. For a step only a
person can do (a sign-in, a captcha) it posts `handoff`, then polls
`/agent/lease` until `answer` is `done` or `skip`. Before it clicks or types,
it checks `driving`: while true, the user is at the keyboard.

## Websocket: `GET /stream`

One websocket carries the screen to the viewer and the viewer's input back.

### Server to viewer

**Binary messages: one video frame each.**

| Bytes | Field |
|---|---|
| 0 | `1` if the frame is a keyframe, else `0` |
| 1–8 | frame id: big-endian uint64, the server's ms clock |
| 9– | one H.264 access unit, Annex B, constrained baseline (`avc1.42E028`), 1280×800 |

Each connection starts with a keyframe. The viewer acks every frame it
shows: the server holds capture while too much is unshown, so a slow link
costs frames, not seconds of input lag.

**Text messages: JSON.** Ignore unknown ones.

| `t` | Fields | Meaning |
|---|---|---|
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
| `w` | `dx`, `dy` (−1, 0, 1) | A wheel step. |
| `c` | `text` (≤ 64 KiB) | Paste this into the desktop's clipboard. |
| `r` | | Release every key and button this viewer holds. |

Any input from a viewer counts as the user driving.

## Browser client

`@2018kguo/jumpseat` ([client/jumpseat.ts](../client/jumpseat.ts)) has no
dependencies.

```ts
import { connect, supportsStream } from "@2018kguo/jumpseat";

if (!(await supportsStream())) showFallback(); // no WebCodecs H.264 decoder
const view = connect(element, `wss://host/stream?token=${token}`, {
  connected: () => {},  // the first frame arrived
  dropped: () => {},    // the socket closed; connect again to retry
  status: ({ kind, text }) => {},
  unsupported: () => {}, // the decoder refused the stream
});
view.setControlling(true); // the user's input now reaches the desktop
view.close();
```

`connect` puts a letterboxed canvas in `element`. While `setControlling` is
false the viewer only watches, and shows the agent's pointer; while true,
keys, pointer, wheel and paste go to the desktop. Touch input is not
forwarded yet.

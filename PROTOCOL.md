# Protocol

One websocket, `GET /stream`, carries the screen to the viewer and the
viewer's input back. With a TCP listener every route needs the token
(`Authorization: Bearer …`, or `?token=` for a browser's websocket).

## Server to viewer

**Binary messages: one video frame each.**

| bytes | field |
|---|---|
| 0 | `1` if the frame is a keyframe, else `0` |
| 1–8 | frame id: big-endian uint64, the server's ms clock |
| 9– | one H.264 access unit, Annex B, constrained baseline (`avc1.42E028`), 1280×800 |

Each connection starts with a keyframe. The viewer acks every frame it
shows (below): the server holds capture while too much is unshown, so a
slow link costs frames, not seconds of input lag.

**Text messages: JSON.** Unknown ones must be ignored.

| `t` | fields | meaning |
|---|---|---|
| `status` | `kind`, `text` | what the agent is doing: `acting` (caption), `waiting` (on the user), `handoff` (the user's turn), `idle` |
| `cursor` | `png` (base64), `x`, `y` (hotspot) | the screen's cursor image, when it changes |
| `p` | `x`, `y` | the pointer moved |
| `c` | `text` | the desktop's clipboard changed |

## Viewer to server

Text messages, JSON:

| `t` | fields | meaning |
|---|---|---|
| `a` | `ts` | frame `ts` was shown (the flow-control ack) |
| `h` | | heartbeat, every 5 s; a viewer silent for 20 s is dropped |
| `k` | `key` (`KeyboardEvent.key`), `down` | a key; the server presses it at the right keymap level |
| `m` | `x`, `y` | move the pointer |
| `b` | `b` (1 left, 2 middle, 3 right), `down`, `x`, `y` | a button |
| `w` | `dx`, `dy` (−1, 0, 1) | a wheel step |
| `c` | `text` (≤ 64 KiB) | paste this into the desktop's clipboard |
| `r` | | release every key and button this viewer holds |

Any input from a viewer counts as the user driving (see `GET /agent/lease`).

## Agent API

| route | body / answer |
|---|---|
| `POST /agent/status` | `{"kind": "acting" \| "waiting" \| "handoff" \| "idle", "text": "Opening LinkedIn"}`, shown to every viewer |
| `GET /agent/lease` | `{"driving": bool, "lastInputMs": n, "answer": "" \| "done" \| "skip"}`: whether the user is driving (input in the last 5 s), and how they answered a handoff |
| `POST /agent/answer` | `{"reply": "done" \| "skip"}`, sent by the viewer's Done / Skip |

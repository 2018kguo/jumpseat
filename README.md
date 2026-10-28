# jumpseat

**Your agent flies. You're in the jumpseat.** Watch your computer-use agent
live, and take the controls when it needs you.

One Go binary streams a Linux desktop (the one your agent drives) to any
browser over a plain websocket: 60 fps at about a third of a core, nothing
when the screen is still. The agent gets a small API to say what it is
doing, hand a step to a person (a sign-in, a captcha) and know when the
person is driving, so the two never fight over the mouse.

- **Works where your sandbox runs.** A websocket through any HTTPS proxy or
  tunnel: no WebRTC, no TURN, no UDP.
- **Built for agent + human.** Live captions of what the agent is doing,
  "Your turn: Sign in to LinkedIn", Take over / Done, continue, an input
  lease so agent clicks wait while you type.
- **Cheap.** 0 CPU idle, ~0.3 cores at 60 fps, ~1 Mbit/s scrolling text,
  and it backs off on slow links instead of piling up lag.
- **Small.** A websocket, H.264, WebCodecs. The browser client has no
  dependencies.

| | jumpseat | KasmVNC |
|---|---|---|
| fps | 60 | 24 |
| bandwidth, scrolling | 1.3 Mbit/s | 14.8 |
| key → pixel | ~35 ms | 48–73 ms |
| on a 1 Mbit/s link, while scrolling | ~0.2 s | 1–2 s |

More in [BENCHMARKS.md](BENCHMARKS.md).

## Try it

```sh
docker build -f docker/Dockerfile -t jumpseat .
docker run --rm -p 8080:8080 -e JUMPSEAT_TOKEN=secret jumpseat
open "http://localhost:8080/?token=secret"
```

From source on Linux (needs libyuv, and nasm on x86-64):
`make -C h264 -j8 && cd server && go build`.

A desktop with Chromium appears. Make the "agent" talk:

```sh
curl -H "Authorization: Bearer secret" -d '{"kind":"acting","text":"Opening LinkedIn"}' localhost:8080/agent/status
curl -H "Authorization: Bearer secret" -d '{"kind":"handoff","text":"Sign in to LinkedIn"}' localhost:8080/agent/status
curl -H "Authorization: Bearer secret" localhost:8080/agent/lease   # {"driving":false,"answer":""}
```

## Use it

**On the machine with the desktop** (Xvfb or any X server):

```sh
jumpseat --listen :8080 --token "$TOKEN" --display :0
# or behind your own auth: --listen unix:/run/jumpseat/stream.sock
```

**In your app:**

```ts
import { connect, supportsStream } from "@2018kguo/jumpseat";

const view = connect(element, `wss://host/stream?token=${token}`, {
  connected: () => {},
  dropped: () => {},
  status: ({ kind, text }) => {}, // "acting" | "waiting" | "handoff" | "idle"
});
view.setControlling(true); // Take over: the user's input reaches the desktop
```

**From your agent:**

- `POST /agent/status {"kind":"acting","text":"Searching for flights"}`
  before a step; `{"kind":"handoff","text":"Sign in to LinkedIn"}` to ask
  the user, then wait for `GET /agent/lease` to report `"answer":"done"`.
- Before clicking, check `GET /agent/lease`: while `"driving"` is true the
  user is at the keyboard.

The wire format is in [PROTOCOL.md](PROTOCOL.md).

## How it works

XDamage says when the screen changed; the frame comes over MIT-SHM, libyuv
converts it, and jumpseat's encoder (`h264/`: OpenH264 changed for
desktops, BSD) or libx264 (`-tags x264`, GPL) encodes it in process. The browser decodes
with WebCodecs onto a canvas and acks each frame; the server stops capturing
while too much is unshown and caps the bitrate to what the link carries.
Input goes back on the same socket and into X through XTest, with keys
placed at the right keymap level so other keyboard layouts type what they
show.

## Status

Early. X11 only, one 1280×800 screen, each viewer its own encoder. Not yet:
audio, Wayland, per-window streaming, a shared encoder for many viewers.

MIT licensed; see [LICENSE](LICENSE) for the encoder libraries.

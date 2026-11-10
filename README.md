<h1 align="center">jumpseat</h1>
<p align="center">Watch a computer-use agent's desktop live, and take over when it needs you.</p>
<p align="center">
  <a href="#try-it">Try it</a> ·
  <a href="docs/reference.md">Reference</a> ·
  <a href="docs/operating.md">Operating</a> ·
  <a href="bench/README.md">Benchmarks</a>
</p>

jumpseat is one Go binary (plus libyuv, xclip and xdotool) that streams the Linux desktop your agent drives to
any browser, as H.264 over a single websocket. It runs at 60 fps, costs about
a third of a core while the screen moves and nothing while it is still, and
works through any HTTPS proxy or tunnel that forwards websockets: no WebRTC,
no TURN, no UDP.

It is built for an agent and a person sharing one desktop. The agent posts
captions of what it is doing, hands a step it cannot do (a sign-in, a
captcha) to the person, and checks whether the person is at the keyboard
before it clicks. The person sees "Your turn", takes over, and hands back.

![The demo viewer: captions, a handoff, the person taking over, the desktop resizing](docs/film.webp)

*A real recording of the demo viewer in Chrome ([e2e/film.mjs](e2e/film.mjs)).
The "agent" is a script driving the desktop with xdotool and jumpseat's API;
the person's input goes through the viewer. The white dot is the person's
pointer, drawn for the recording.*

## How it compares

On one 4-vCPU sandbox (gVisor, no GPU) at 1280×800, against noVNC (x11vnc),
watched from a real Chrome across the internet, the two alternating on the
same machine ([bench/compare](bench/README.md#live-against-novnc-benchcompare)):

| | jumpseat | noVNC |
|---|---|---|
| frames per second, scrolling / animating | **61 / 60** | 17 / 20 |
| bandwidth, scrolling text | **0.64 Mbit/s** | 28 Mbit/s |
| key → pixel in the viewer, p50, still / scrolling | **42 / 85 ms** | 65 / 157 ms |
| CPU, X server and streamer, scrolling | **0.55 cores** | 0.58 |

KasmVNC 1.5.0, measured on the same kind of machine with an earlier
harness, painted 24 fps at 14.8 Mbit/s scrolling.

Its encoder is OpenH264's, changed for desktop content. On recorded desktop
frames it needs 61% fewer bits than x264 ultrafast to scroll, 36% fewer to
animate and 34% fewer to type at equal quality, for about 0.7–1.0x x264's CPU.
The method, the raw numbers and what did not help are in
[bench/](bench/README.md).

## Try it

[A recorded session](https://kguo.dev/jumpseat/) plays in your
browser through jumpseat's own client. To run it, the demo image has Xvfb, a
window manager, Chromium and jumpseat (amd64 and arm64):

```sh
docker run --rm -p 8080:8080 -e JUMPSEAT_TOKEN=secret ghcr.io/2018kguo/jumpseat
open "http://localhost:8080/?token=secret"
```

Then play the agent:

```sh
curl -H "Authorization: Bearer secret" -d '{"kind":"acting","text":"Opening the inbox"}' localhost:8080/agent/status
curl -H "Authorization: Bearer secret" -d '{"kind":"handoff","text":"Sign in to your email"}' localhost:8080/agent/status
curl -H "Authorization: Bearer secret" localhost:8080/agent/lease   # {"answer":"","driving":false,"lastInputMs":0}
```

To have the desktop follow the browser window (up to 2560×1600), run the
container with `-e JUMPSEAT_RESIZE=1` and open `/?token=secret&resize`.

Or let Claude drive it: [examples/computer_use.py](examples/computer_use.py)
gives Claude the desktop, captions what it does, and hands you the steps it
should not do itself.

## Use it

**On the machine with the desktop** (Xvfb or any X server). Each
[release](https://github.com/2018kguo/jumpseat/releases) has Linux binaries
for amd64 and arm64, which need libyuv (`apt install libyuv0`):

```sh
jumpseat --listen :8080 --token "$TOKEN" --display :0
# or, behind your own proxy and auth:
jumpseat --listen unix:/run/jumpseat/stream.sock
```

**In your page**, with the client (`npm i @2018kguo/jumpseat`, no
dependencies; the source is [client/jumpseat.ts](client/jumpseat.ts)):

```ts
import { connect } from "@2018kguo/jumpseat";

const view = connect(element, `wss://host/stream?token=${token}`, {
  connected: () => {},
  dropped: () => {},
  unsupported: () => {}, // no WebCodecs H.264 decoder in this browser
  status: ({ kind, text }) => {}, // "acting" | "waiting" | "handoff" | "idle"
});
view.setControlling(true); // the person's keys, pointer and scrolls reach the desktop
```

**From your agent:**

- Before a step, `POST /agent/status {"kind":"acting","text":"Searching for flights"}`.
- After it, `GET /agent/wait` returns once the screen has settled, instead
  of a fixed sleep.
- For a step only a person can do, post `{"kind":"handoff","text":"Sign in to your email"}`,
  then poll `GET /agent/lease` until `answer` is `"done"`.
- Before clicking, check `GET /agent/lease`: while `driving` is true, the
  person is at the keyboard.
- `GET /agent/screenshot` returns the screen as a PNG.

Every flag, route, message and client option is in
[docs/reference.md](docs/reference.md). Deploying it safely is in
[docs/operating.md](docs/operating.md).

## How it works

XDamage says when the screen changed. The frame comes over MIT-SHM, libyuv
converts it, and the encoder in [h264/](h264/README.md) encodes it in
process. All viewers share that one capture and encoder; a viewer joining
restarts the encoder, so it starts on a keyframe. The browser decodes each frame with
WebCodecs as it arrives and acks it, and the server holds capture while
frames go unshown and caps the bitrate to what the link carries, so a slow
link drops frames instead of building up seconds of lag. A viewer whose tab
is hidden gets nothing, so nothing is encoded for it.

The person's input comes back on the same socket and goes into X through
XTest, with keys placed at the right keymap level so other keyboard layouts
type what they show. Trackpad scrolling arrives pixel by pixel on an Xvfb
with scroll axes on its XTest pointer, which the demo image builds; any
other X server gets wheel clicks.

The desktop streams at whatever size the X screen is, and starts over when
RandR changes it. With `--resize` the viewer asks for its window's size.
That is off by default, because an agent clicks at coordinates taken from
the size it last saw.

## Status

Early. The limits are listed in [docs/operating.md](docs/operating.md#limits).

Several pieces follow [voxl](https://github.com/tembo/voxl), a desktop
streamer for the browser with the same transport: `/agent/wait`, the shared
encoder, RandR resizing, and the Xvfb scroll patch, which is voxl's own.

## License

MIT. The encoder in `h264/` is BSD 2-clause (OpenH264's), libyuv is BSD
3-clause, and the Xvfb patch is voxl's, MIT. Details in [LICENSE](LICENSE).

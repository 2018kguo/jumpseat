# Operating jumpseat

jumpseat runs next to the desktop it streams: on the machine (VM, container)
with the X server your agent drives.

## What it needs

- An X server with the MIT-SHM, XDamage, XFixes and XTest extensions, and
  RandR for `--resize`. Xvfb
  has them. For pixel-precise (trackpad) scrolling, Xvfb built with
  [docker/patches/xorg-server-xtest-scroll.patch](../docker/patches/xorg-server-xtest-scroll.patch)
  (from voxl, MIT), as the demo image does; any other X server scrolls by
  wheel clicks (jumpseat logs which as a viewer connects).
  [docker/start.sh](../docker/start.sh) starts Xvfb, openbox and Chromium
  the way the demo image does.
- `xclip` (the clipboard) and `xdotool` (typing characters that are not on
  the X keymap, such as "é") on `PATH`.
- libyuv at run time. The encoder is linked in.
- For sound, a PulseAudio or PipeWire server and `parec` (Debian:
  `pulseaudio-utils`). Without them a viewer that turns sound on gets
  `noAudio`. Opus is linked in.
- A browser with WebCodecs and an H.264 decoder (Chrome, Edge, Safari,
  Firefox). Some Linux Chromium builds ship without one: `supportsStream()`
  says so before you connect.

## Who can reach it

A viewer can type into the desktop, and an agent route can tell the agent the
user answered. Treat access to jumpseat like access to the desktop.

- **Behind your own auth** (the default): `--listen unix:/run/jumpseat/stream.sock`.
  Only processes that can open the socket reach it; put your proxy in front
  and check your users there. Pages Chromium opens on the desktop can reach
  loopback ports, and none can reach a unix socket.
- **On a port**: `--listen :8080 --token "$TOKEN"`. Every route needs the
  token, as `Authorization: Bearer` or `?token=` (a browser's websocket
  cannot set headers, so the token appears in the URL: serve it over TLS).

The agent status and user-input files in `--dir` are how the agent API and
the viewer meet; keep the directory private to the user jumpseat runs as.

## Resources

0 CPU while the screen is still: frames are captured only when XDamage
reports a change. CPU and bandwidth while it moves are in
[bench/](../bench/README.md). Viewers share one encoder: another viewer
costs a send, not an encode.

On a slow link the server measures what the link carries, caps the bitrate
to it, and holds capture while frames are unacknowledged: frames drop, input
lag does not build.

## Environment

`JUMPSEAT_TOKEN` stands in for `--token`; the others are for measurement,
and their defaults are what the benchmarks measured.

| Variable | Use |
|---|---|
| `JUMPSEAT_TOKEN` | The token, instead of `--token`. |
| `JUMPSEAT_TRACE=1` | Log each frame within 300 ms of a key press: when it was damaged, grabbed, encoded and sent, from the key reaching X. |
| `JUMPSEAT_TAIL` | Tiny messages sent after a keystroke-sized frame that follows a still screen, so a lost packet is retransmitted after a round trip, not TCP's tail timer (default 3, 0 off). |

## Limits

X11 only, one screen. No microphone, clipboard images, file transfer, Wayland or
per-window streaming yet. All viewers share one encoder, so the slowest link
sets the pace for everyone: beside a viewer on 1 Mbit/s, a fast one gets its
frame rate and bitrate ([bench](../bench/README.md#two-viewers-one-slow-benchcompareviewersmjs)).

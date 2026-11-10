# Development

## Layout

- `server/`: the Go binary.
  - `hub.go`: the one capture and encoder every viewer shares, rebuilt when
    the screen changes size.
  - `stream.go`: one viewer's socket (frames, cursor, pointer, clipboard,
    the agent's status) and the MIT-SHM grab.
  - `flow.go`: acks, holding capture, the link's speed.
  - `input.go`, `scroll.go`: the viewer's input through XTest.
  - `screen.go`: RandR resizing (`--resize`).
  - `status.go`, `wait.go`, `screenshot.go`: the agent API.
  - `encoder_openh264.go`: the encoder bindings.
  - `web/`: the demo page, embedded.
- `h264/`: the encoder, OpenH264's changed for desktops. Its
  [README](../h264/README.md) lists every change from upstream.
- `client/`: the browser client, no dependencies. `bun run build` writes
  `dist/jumpseat.js` and its types.
- `bench/`: the encoder lab and the [measurements](../bench/README.md).
- `e2e/`: a real browser against the demo container, and `film.mjs`, which
  records the README's film.
- `docker/`: the demo image: Xvfb with the scroll patch, openbox, the
  wallpaper, Chromium, jumpseat.

## Build and test

The demo image builds the client, encoder and server and runs the Go tests;
it needs only Docker:

```sh
docker build -f docker/Dockerfile -t jumpseat .
```

From source on Linux, with Go (see `server/go.mod`), libyuv, nasm on x86-64,
and bun for the client:

```sh
make -C h264 -j8                       # h264/build/libjumpseat_h264.a
(cd client && bun run build:demo)      # server/web/jumpseat.js for the demo page
(cd server && go vet ./... && go test ./... && go build)
```

Go's build cache does not hash the encoder library: after changing `h264/`,
`go build -a`, or key the cache on the library
(`CGO_CFLAGS="-DH264_SUM=$(cksum < h264/build/libjumpseat_h264.a | cut -d' ' -f1)"`),
or the old encoder links silently.

The end-to-end check opens the demo in Chrome (Playwright's bundled
Chromium has no H.264 decoder). It covers the stream, the agent API, taking
over, two viewers at once, smooth scrolling and touch, and with
`JUMPSEAT_RESIZE=1` the desktop following the window:

```sh
docker run -d --name jumpseat-test -p 18080:8080 -e JUMPSEAT_TOKEN=secret jumpseat
until curl -sf "localhost:18080/?token=secret" >/dev/null; do sleep 1; done
(cd e2e && bun install && bun run test)
docker rm -f jumpseat-test

docker run -d --name jumpseat-test -p 18080:8080 -e JUMPSEAT_TOKEN=secret -e JUMPSEAT_RESIZE=1 jumpseat
until curl -sf "localhost:18080/?token=secret" >/dev/null; do sleep 1; done
(cd e2e && JUMPSEAT_RESIZE=1 bun run test)
docker rm -f jumpseat-test
```

[e2e/film.mjs](../e2e/film.mjs) records the README's film the same way; its
header has the ffmpeg steps.

## Changing the encoder

Measure every change on [bench/](../bench/README.md) before keeping it:

- A speed-up must leave the output identical to the byte (same bitrate and
  PSNR on every workload) and take less time: `bench/local.sh ab main`
  (and `bench/modal_lab.py ab main` for x86) times the encoder at a git ref
  against the working tree's and checks both at once; `modal_lab.py ir`
  counts instructions.
- A change to what is encoded must win on BD-rate against x264
  (`bench/local.sh rd`) without costing instructions it does not earn back.

Record each kept change, with its numbers, in [h264/README.md](../h264/README.md),
and what did not help in [bench/README.md](../bench/README.md). Keep run
output in the ignored `.bench/` and recorded frames outside the repo.

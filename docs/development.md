# Development

## Layout

- `server/`: the Go binary. Capture (`stream.go`), input (`input.go`), flow
  control (`flow.go`), the agent API (`status.go`), the encoder bindings
  (`encoder_openh264.go`; `encoder.go` with `-tags x264`) and the demo page
  (`web/`, embedded).
- `h264/`: the encoder, OpenH264's changed for desktops. Its
  [README](../h264/README.md) lists every change from upstream.
- `client/`: the browser client, `@2018kguo/jumpseat`, no dependencies.
- `bench/`: the encoder lab and the [measurements](../bench/README.md).
- `e2e/`: a real browser against the demo container.
- `docker/`: the demo image (Xvfb, openbox, Chromium, jumpseat).

## Build and test

The demo image builds and tests everything, and needs only Docker:

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
Chromium has no H.264 decoder) and covers the stream, the agent API and
taking over:

```sh
docker run -d --name jumpseat-test -p 18080:8080 -e JUMPSEAT_TOKEN=secret jumpseat
(cd e2e && bun install && bun run test)
docker rm -f jumpseat-test
```

## Changing the encoder

Measure every change on [bench/](../bench/README.md) before keeping it:

- A speed-up must leave the output identical to the byte (same bitrate and
  PSNR on every workload) and cost fewer instructions
  (`bench/modal_lab.py ir`).
- A change to what is encoded must win on BD-rate against x264
  (`bench/local.sh rd`) without costing instructions it does not earn back.

Record each kept change, with its numbers, in [h264/README.md](../h264/README.md),
and what did not help in [bench/README.md](../bench/README.md). Keep run
output in the ignored `.bench/` and recorded frames outside the repo.

# Performance and measurement

Two kinds of measurement, for two questions:

- **The live stream** (fps, bandwidth, stream CPU, key → pixel): a real
  browser across the network watching a desktop on a 4-vCPU Modal sandbox
  (gVisor, no GPU), 1280×800. Every comparison alternates the two builds
  (ABBA) on one VM, because two VMs differ by more than most changes (one
  build measured 0.31 and 0.50 cores on two). "Stream CPU" counts the X
  server and jumpseat.
- **The encoder alone** (bits at equal quality, CPU): recorded desktop
  frames replayed through jumpseat's encoder (`h264/`) and x264 with the
  same settings, in this directory.

## Live, against KasmVNC 1.5.0 (same VM)

| | jumpseat | KasmVNC |
|---|---|---|
| fps, scrolling / animating | 60 / 60 | 24 / 24 |
| stream CPU, scrolling | ~0.3 cores | 0.38–0.48 |
| idle CPU | 0 | ~0 |
| bandwidth, scrolling text | 0.66 Mbit/s | 14.8 |
| key → pixel, p50 | 30–40 ms | 48–73 ms |
| key → pixel while scrolling on a 1 Mbit/s link, p50 / p90 | 117–216 / 464–490 ms¹ | 1.1 / 2.1 s |
| fidelity vs the lossless screen | 48–51 dB | — |

¹ Measured with the x264 build, which needs twice the bits to scroll.

## Live, against x264

The same streamer built on libx264 ultrafast (`-tags x264`, four slice
threads), ABBA on one VM:

| | x264 | jumpseat (`h264/`) |
|---|---|---|
| scroll | 1330 kbit/s | **656 kbit/s** |
| animation | 3864 kbit/s | **3436 kbit/s** |
| typing | 21 kbit/s, frame gaps p90 168 ms | 19 kbit/s, 85 ms |
| key → pixel, idle / scrolling | 37 / 66 ms | 37 / 66 ms |
| first frame | 48.6 dB | 48.9 dB |

Stream CPU is level within run-to-run noise (about ±10%: on one VM scrolling
0.22 vs 0.21 cores, animating 0.28 vs 0.27; on another 0.28 vs 0.31 and
0.37 vs 0.34). Those VMs have AVX-512, which x264 uses and is worth 5–14% of
its encode time there; on an AVX2-only x86 host under perf, jumpseat's
encoder takes 0.58–0.74x x264's cycles and makes no syscalls a frame
(x264, even on one thread: ~120 futex calls).

Where key → pixel goes on the VM: Chromium repaints 2.3 ms after the key
reaches X, the grab takes 0.4, the encode 2.0, the send ~0. The rest is the
network round trip.

## The encoder, against x264

Recorded 1280×800 frames (scroll, a CSS animation over half the screen,
typing), x86. Bits: BD-rate, the average bitrate difference at equal PSNR
over the quality range both encoders cover. CPU: instructions per frame
under callgrind (wall time on a shared VM varies ~30%), against x264
ultrafast on one thread.

| | bits vs x264 | CPU vs x264 |
|---|---|---|
| scrolling text | **−40%** | **0.90x** |
| typing | **−26%** | **0.68x** |
| animation | **−10%** | **0.90x** |

At the streamer's settings (QP floor 17 against x264 crf 25), and once the
motion stops (`rest=60`: the quality left on screen):

| | jumpseat kbit/s, dB, at rest | x264 kbit/s, dB, at rest |
|---|---|---|
| scrolling text | 849, 53.1, 53.3 | 1206, 47.6, 50.8 |
| typing | 542, 53.8, 53.6 | 640, 50.4, 51.7 |
| animation | 3471, 50.5, 54.2 | 3750, 50.1, 54.1 |

[`h264/README.md`](../h264/README.md) lists each change from OpenH264. Stock
OpenH264's screen mode scrolled at 1.6x x264's CPU, and its first frame was
blurrier than the QP asked for (typing 45 dB where jumpseat gets 54).

## What made the difference

- **Capture only on change** (XDamage) and through shared memory: 0 CPU idle.
- **Encode in process** (libyuv and the encoder via cgo), not ffmpeg over a
  pipe: 0.6–0.75 → ~0.3 cores at 60 fps.
- **Constant-rate encoder input.** With variable-rate input x264 held each
  frame until the next arrived, so the last frame of a change waited until
  something else moved: idle key → pixel p90 107 → 48 ms.
- **Tag the colour space** (BT.601 / SMPTE 170M): untagged, Chrome guessed,
  and every frame was off by a luma bias of −1.1 (34.7 dB → 48+).
- **Frame acks and a bitrate cap that follows the link**: on a 1 Mbit/s
  link, key → pixel while scrolling 1.1 s → ~0.2 s at p50.
- **Pacing cap above 60 fps**: a 60 cap dropped frames whenever Chromium's
  landed early (57 → 60.0 fps).
- **The encoder** (`h264/README.md`): screen mode's scroll search without
  its per-pixel index, the first frame at the QP asked for, x264's CRF rule
  for content in motion with refinement once it stops.

Did not help, so not shipped: a desynchronized canvas; decoding in a Web
Worker (+7–14 ms); x264 thread counts; paint-over for x264 (its apparent
gain was the variable-rate delay); OpenH264's background detection, adaptive
quant and threads; a damage map into the encoder (bit-identical, 6% on
typing only); a sum prefilter on the line search (columns are searched about
once a frame, so the sums are not shared, and it cost more than it saved).

## Run it

Frames come from a real desktop: `jumpseat --record scroll.bgrx --frames 240`
writes the next 240 changed frames as raw BGRX. Keep them outside the repo
(`BENCH_DATA`, default `../jumpseat-bench-data`).

```sh
docker build -t jumpseat-bench bench    # once: compilers, nasm, x264, FFmpeg's decoder
bench/local.sh run usage=1              # one point per workload: ms/frame, kbit/s, PSNR
bench/local.sh rd usage=1               # BD-rate against x264 (sweep.sh + bdrate.py)
python3 bench/modal_lab.py ir qp=17 usage=1          # x86: instructions per frame, ours vs x264
python3 bench/modal_lab.py callgrind scroll usage=1  # x86: where an encode's instructions go
```

`local.sh` builds `h264/` and runs in the lab container on this machine;
`modal_lab.py` keeps an x86 sandbox on Modal and syncs `h264/` and `bench/`
into it, so a change rebuilds in seconds. Options for `encode_bench`: `enc=1`
(x264 as the streamer sets it up), `crf`, `xthreads`, `qp`, `kbps` (cap the
rate as a slow link does), `capat=F` (apply it at frame F: the streamer caps only
once flow control has measured the link, so its first IDR is never capped), `rest=N`, `perframe=1` (`usage`, `complexity`, `rc`
and the like only change stock OpenH264, `LAB_SRC=stock`: h264/ forces its
configuration); `X264_NO_AVX512=1` prices x264's
AVX-512 kernels. Every encoder change is judged on bits (BD-rate, or output
identical to the byte for a pure speed-up) and instructions. Put run output
in the ignored `.bench/`.

## Under a bitrate cap

`kbps=1000` alone caps from the first frame, which the streamer never does:
the IDR then comes out at 34.8 dB and the still screen climbs back over ~60
frames, so typing looks worse capped (41.95 dB) than not (53.8). With the cap
applied as the streamer applies it (`kbps=1000 capat=1`), typing is
unchanged (532 kbit/s, 53.79 dB). After animation under the cap, a still
screen is sharp again in about the time the link takes to carry one
full-quality frame: 51 dB at 20 frames and 54.5 dB at 40 at 1000 kbit/s,
54 dB at 80 at 500 kbit/s (uncapped: 5 frames).

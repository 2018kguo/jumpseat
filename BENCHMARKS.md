# Benchmarks

Measured on a 4-vCPU Modal sandbox (gVisor, no GPU), 1280×800, from a
real browser across the network. Every comparison is ABBA on one VM: two
VMs differ by more than most changes (one build measured 0.31 and 0.50
cores on two). "Stream CPU" counts the X server and the streamer.

## vs KasmVNC 1.5.0 (same VM)

| | jumpseat (x264) | KasmVNC |
|---|---|---|
| fps, scrolling / animating | 60 / 60 | 24 / 24 |
| stream CPU, scrolling | ~0.3 cores | 0.38–0.48 |
| idle CPU | 0 | ~0 |
| bandwidth, scrolling text | 1.3 Mbit/s | 14.8 |
| key → pixel, p50 | 30–40 ms | 48–73 ms |
| key → pixel while scrolling, 1 Mbit/s link, p50 / p90 | 117–216 / 464–490 ms | 1.1 / 2.1 s |
| fidelity vs the lossless screen | 48–51 dB | — |

## Encoders, at matched picture (~48.5 dB)

| | stream CPU scroll / anim | kbit/s scroll / anim |
|---|---|---|
| libx264 ultrafast (`-tags x264`, GPL) | 0.27 / 0.41 | 1325 / 3904 |
| OpenH264, camera mode (default, BSD) | 0.27 / 0.44 | 1160 / 5589 |
| OpenH264, screen mode (`JUMPSEAT_OH264=usage=1`) | 0.52 / 0.70 | **627** / 5376 |

## jumpseat's encoder (`h264/`) against x264

Recorded 1280x800 desktop frames (scroll, CSS animation over half the
screen, typing) through both encoders on x86 (bench/). Bits: BD-rate, the
average bitrate difference at equal PSNR across each encoder's quality
range. CPU: instructions per frame under callgrind, since wall time on a
shared VM varies ~30%. x264 is the streamer's ultrafast/zerolatency setup on
one thread (its default four slice threads cost more CPU).

| | bits vs x264 | CPU vs x264 |
|---|---|---|
| scrolling text | **-40%** | **0.90x** |
| typing | **-26%** | **0.68x** |
| animation | **-10%** | **0.90x** |

At the streamer's settings (QP floor 17 against x264 crf 25), and after the
motion stops (`rest=60`: the quality left on screen):

| | ours kbit/s, dB, at rest | x264 kbit/s, dB, at rest |
|---|---|---|
| scrolling text | 849, 53.1, 53.3 | 1206, 47.6, 50.8 |
| typing | 542, 53.8, 53.6 | 640, 50.4, 51.7 |
| animation | 3471, 50.5, 54.2 | 3750, 50.1, 54.1 |

`h264/README.md` lists what changed from OpenH264 to get there; stock
OpenH264's screen mode scrolled at 1.6x x264's CPU, and its first frame was
blurrier than the QP asked for (typing 45 dB where we get 54).

## What made the difference, and what did not

- **Capture only on change** (XDamage) and through shared memory: 0 CPU idle.
- **Encode in process** (libyuv + the encoder via cgo), not ffmpeg over a
  pipe: 0.6–0.75 → ~0.3 cores at 60 fps.
- **Constant-rate encoder input.** With variable-rate input x264 held
  every frame until the next arrived, so the last frame of a change waited
  until something else moved: idle key → pixel p90 107 → 48 ms.
- **Tag the colour space** (BT.601 / SMPTE 170M): untagged, Chrome guessed
  and every frame was off by a luma bias of −1.1 (34.7 dB → 48+).
- **Frame acks and a bitrate cap that follows the link**: on a 1 Mbit/s
  link, key → pixel while scrolling 1.1 s → ~0.2 s at p50.
- **Pacing cap above 60 fps**: a 60 cap dropped frames whenever Chromium's
  landed early (57 → 60.0 fps).
- Did not help, so not shipped: a desynchronized canvas, decoding in a Web
  Worker (+7–14 ms), x264 thread counts, paint-over (its apparent gain was
  the variable-rate delay), OpenH264 background detection, adaptive quant,
  scene-change detection and threads.

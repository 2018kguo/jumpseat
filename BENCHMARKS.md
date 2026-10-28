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

## Patched OpenH264 screen mode (`openh264/screen-fme.patch`, the Docker image)

Screen mode finds scrolled content with a hash of every pixel's 8x8 sum
over the whole reference frame, every frame. The patch runs that only on
scrolling frames, from running sums, over every 8th column.

| same VM, one ABBA round | stream CPU scroll / type | kbit/s scroll |
|---|---|---|
| OpenH264 camera mode | 0.29 / 0.08 | 1186 |
| OpenH264 screen mode, stock | 0.55 / 0.11 | 632 |
| OpenH264 screen mode, patched | **0.41** / **0.08** | 644 |

On arm64 (bench/, encode only, 1280x800): scroll 5.35 → 1.8 ms a frame
and typing 4.9 → 0.85, at the same bitrate and PSNR. x86 gains less
because stock OpenH264 already has SSE for the sums.

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

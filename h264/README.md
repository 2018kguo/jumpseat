# h264: jumpseat's encoder

OpenH264 v2.6.0's encoder (cisco/openh264 652bdb77, BSD 2-clause, LICENSE),
trimmed to what jumpseat links and changed for desktops. Not tracking
upstream: changes here are ordinary commits.

It encodes one configuration, the streamer's, and the parameters force it
(`param_svc.h`): screen content at low complexity, quality-mode rate control,
CABAC (High profile), no deblocking, one thread, one slice, one spatial and
temporal layer, constant SPS/PPS ids, no LTR, background detection, adaptive
quantization or frame skipping. `SetOption` takes the data format, bitrate and max bitrate.

Kept: `api/` (the encoder interface), `common/`, `encoder/` and `processing/`
(scroll, scene-change and complexity analysis, frame SAD), with x86-64 (nasm,
elf64) and arm64 assembly, and plain-C versions of every SIMD routine. Where
OpenH264 had several x86 versions of a routine, only the newest is kept (AVX2
where there is one): a CPU without that ISA runs the C version.

Removed, because that configuration never runs it (each removal left the
bench output identical to the byte, uncapped and at 1000 kbps):

- From upstream's tree: the decoder and its API, console apps, tests and
  test streams.
- Other configurations: camera mode and its preprocessing (downsampling,
  denoising, rotation, background detection, adaptive quantization), medium
  and high complexity, the other four rate-control modes, deblocking,
  SPS/PPS id strategies, LTR and temporal-layer reference strategies, SVC
  enhancement layers and prefix NALs, threads and the task manager,
  multi-slice and size-limited (dynamic) slicing.
- CAVLC, once CABAC was the one that runs: its macroblock and residual
  writers, VLC tables, `CavlcParamCal` (C and SSE4.2), the baseline-profile
  fallbacks and the overflow re-encode only CAVLC could hit.
  `iEntropyCodingModeFlag` stays in the API, ignored.
- What the low-complexity screen path never reaches: sub-pel motion
  (six-tap filters, quarter-pel averaging; luma prediction is a copy), the
  feature-search index, 16x8/8x16 and smaller partition searches, intra 4x4
  in P slices.
- Encoder statistics and the debug dumps; options nothing sets.
- Code for targets we never compile (`unifdef`): 32-bit ARM and x86,
  MIPS, LoongArch, Android, Windows.
- Then whatever nothing referenced any more: functions (the linker's
  `--gc-sections`), statics, asm routines, macros, tables, types and members.

```sh
make -j8        # build/libjumpseat_h264.a; x86-64 needs nasm
```

After changing `h264/`, rebuild the Go side as
[docs/development.md](../docs/development.md#build-and-test) says: Go's build
cache does not see the library change.

## Changes from OpenH264

- **Scroll search without the feature index** (`encoder_ext.cpp`,
  `svc_motion_estimate.cpp`, `svc_mode_decision.cpp`): OpenH264 turned on
  its cross search (which finds scrolled content) only by building a
  per-pixel feature index of the reference frame every frame, whose lookups
  never changed a motion vector on desktop content. We set the cross
  search's threshold directly, on frames where scroll detection found a
  scroll; those frames also keep 8x8 partitions, other frames stay 16x16.
  Scroll: same bits, 23.3M → 18.7M instructions a frame on x86.
  Bit-identical.
- **Rate-control complexity** (`ComplexityAnalysis.cpp`): no intra estimate
  for blocks unchanged from the reference (their cost is 0 regardless).
  Bit-identical.
- **First IDR's QP** (`ratectl.cpp`): with bits to spare, the caller's QP
  floor bounds it, not OpenH264's table (which floored it at 22). Static
  content keeps its IDR's quality: typing 45 → 54 dB at QP 17. Changes
  the output.
- **Scroll detection** (`ScrollDetection.cpp`): regions with no changed
  row are skipped, and a zero scroll is reported as none (every reader
  treats them alike). Typing: 12.9M → 10.8M instructions a frame.
  Bit-identical.
- **Scene change** (`SceneChangeDetection.h`): 8x8 SADs two blocks at a
  time (psadbw / vabd). Bit-identical.
- **Coefficient zeroing** (`svc_encode_mb.cpp`): inline memset, not MMX
  calls. Bit-identical.
- **No 16x16 plane prediction** (`svc_base_layer_md.cpp`): 0.1% of
  animation bits for 3% of its CPU. Changes the output.
- **Low complexity in screen mode** works like x264 ultrafast: integer-pel
  motion (`md.cpp`) and no intra 4x4 in P slices (`svc_base_layer_md.cpp`).
  OpenH264's medium complexity, which kept both, took 8% fewer animation
  bits for ~30% more of its CPU. Changes the output.
- **CABAC, back** (`set_mb_syn_cabac.cpp`, `svc_set_mb_syn_cabac.cpp`, from
  upstream unchanged; slice loops in `svc_encode_slice.cpp`): it had been
  removed with the configurations nothing ran; now it is the one that runs.
  Same pictures as CAVLC (scroll and typing to the byte in PSNR; animation's
  rate control lands 0.06 dB higher), bits on arm64: scroll 849 → 650 kbit/s
  (−23%), animation 3471 → 2591 (−25%), typing 542 → 537 (−1%: a typing
  stream is almost all its first IDR, −5%, and CABAC's per-macroblock skip
  flags cost a few dozen bytes more than CAVLC's skip runs on near-empty
  frames). Encode CPU +4–9%. BD-rate against x264 ultrafast (CAVLC):
  scroll −41 → −57%, animation −12 → −35%, typing −28 → −29%.
- **Motion QP and rest refinement** (`ratectl.cpp`, `wels_preprocess.cpp`,
  `svc_mode_decision.cpp`): x264's CRF rule on a floor: frames whose moving
  content (neither static nor scrolled) costs more than 64 SAD a macroblock
  get QP floor + 2.4 log2(cost / 64), as detail in motion goes unseen, so
  animation runs at x264's quality (50.5 dB) for fewer bits while typing and
  scrolling keep the floor. A static block coded more than 3 QP coarser than
  now is coded again, so what comes to rest sharpens (animation 54 dB at
  rest); OpenH264 refined only above QP 26.

Measure every change with bench/ (BD-rate and CPU against x264 on recorded
desktop frames): `bench/local.sh rd usage=1`, `bench/modal_lab.py rd usage=1`.

# h264: jumpseat's encoder

OpenH264 v2.6.0's encoder (cisco/openh264 652bdb77, BSD 2-clause, LICENSE),
trimmed to what jumpseat links and changed for desktops. Not tracking
upstream: changes here are ordinary commits.

Kept: `api/` (the ISVCEncoder interface), `common/`, `encoder/`,
`processing/` (scroll, scene-change, complexity and background analysis,
adaptive quantization), with x86-64 (nasm) and arm64 assembly, and plain-C
versions of every SIMD routine.

Removed, because jumpseat never runs it (each removal left the bench output
identical to the byte, screen and camera mode):

- From upstream's tree: the decoder, console apps, tests and test streams.
- Downsampling, denoising and image rotation: one layer, at the input's size.
- CABAC: baseline profile is CAVLC; parameters force it.
- The deblocking filter: off, as x264 ultrafast runs; parameters force it.
- SPS/PPS id strategies other than constant ids; parameters force it.
- Code for targets and builds we never compile (`unifdef`): 32-bit ARM,
  MIPS, LoongArch, 32-bit x86, Android, Windows, and the testbed,
  statistics and frame-dump debug switches.

Still here though unused: multi-threaded and size-limited slicing, and
multi-layer SVC. They are woven through the frame loop in `encoder_ext.cpp`,
which also uses their helpers on the single-threaded path; taking them out
means restructuring that loop.

    make -j8        # build/libjumpseat_h264.a; x86-64 needs nasm

Go's build cache does not hash a library named in cgo LDFLAGS: after
changing h264/, rebuild the Go side with `go build -a`, or key the cache on
the library, `CGO_CFLAGS="-DH264_SUM=$(cksum < h264/build/libjumpseat_h264.a | cut -d' ' -f1)"`.

## Changes from OpenH264

- **Scroll search without the feature index** (`encoder_ext.cpp`,
  `svc_motion_estimate.cpp`, `svc_mode_decision.cpp`): OpenH264 turned on
  its cross search (which finds scrolled content) only by building a
  per-pixel feature index of the reference frame every frame, whose lookups
  never changed a motion vector on desktop content. We set the cross
  search's threshold directly, on frames where scroll detection found a
  scroll; those frames also keep 8x8 partitions, other frames stay 16x16.
  Scroll: same bits, 23.3M -> 18.7M instructions a frame on x86.
- **Rate-control complexity** (`ComplexityAnalysis.cpp`): no intra estimate
  for blocks unchanged from the reference (their cost is 0 regardless).
- **First IDR's QP** (`ratectl.cpp`): with bits to spare, the caller's QP
  floor bounds it, not OpenH264's table (which floored it at 22). Static
  content keeps its IDR's quality: typing 45 -> 54 dB at QP 17.
- **Scroll detection** (`ScrollDetection.cpp`): regions with no changed
  row are skipped, and a zero scroll is reported as none (every reader
  treats them alike). Typing: 12.9M -> 10.8M instructions a frame.
- **Scene change** (`SceneChangeDetection.h`): 8x8 SADs two blocks at a
  time (psadbw / vabd). **Coefficient zeroing** (`svc_encode_mb.cpp`):
  inline memset, not MMX calls. **No 16x16 plane prediction**
  (`svc_base_layer_md.cpp`): 0.1% of animation bits for 3% of its CPU.
- **Low complexity in screen mode** (the streamer's setting) works like x264
  ultrafast: integer-pel motion (`md.cpp`) and no intra 4x4 in P slices
  (`svc_base_layer_md.cpp`). Medium complexity keeps both: 8% fewer
  animation bits for ~30% more of its CPU.

- **Motion QP and rest refinement** (`ratectl.cpp`, `wels_preprocess.cpp`,
  `svc_mode_decision.cpp`): x264's CRF rule on a floor: frames whose moving
  content (neither static nor scrolled) costs more than 64 SAD a macroblock
  get QP floor + 2.4 log2(cost / 64), as detail in motion goes unseen, so
  animation runs at x264's quality (50.5 dB) for fewer bits while typing and
  scrolling keep the floor. A static block coded more than 3 QP coarser than
  now is coded again, so what comes to rest sharpens (animation 54 dB at
  rest); OpenH264 refined only above QP 26.

All but the last three leave the output bit-identical on the bench.

Measure every change with bench/ (BD-rate and CPU against x264 on recorded
desktop frames): `bench/local.sh rd usage=1`, `bench/modal_lab.py rd usage=1`.

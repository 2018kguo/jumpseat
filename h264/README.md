# h264: jumpseat's encoder

OpenH264 v2.6.0's encoder (cisco/openh264 652bdb77, BSD 2-clause, LICENSE),
trimmed to what jumpseat links and changed for desktops. Not tracking
upstream: changes here are ordinary commits.

Kept: `api/` (the ISVCEncoder interface), `common/`, `encoder/`,
`processing/` (scroll, scene-change and complexity analysis), with x86-64
(nasm) and arm64 assembly. Dropped: the decoder, console apps, tests, test
streams, 32-bit ARM, MIPS and LoongArch.

    make -j8        # build/libjumpseat_h264.a; x86-64 needs nasm

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
- **Experiments** (`encoder/core/inc/jumpseat_exp.h`): `JUMPSEAT_EXP` bits
  under test in bench/ (0: no intra 4x4 in P slices, 1: integer-pel motion,
  3: no 16x16 plane prediction).
  Winners become plain code; losers are deleted.

Measure every change with bench/ (BD-rate and CPU against x264 on recorded
desktop frames): `bench/local.sh rd usage=1`, `bench/modal_lab.py rd usage=1`.

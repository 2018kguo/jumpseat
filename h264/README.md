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

- **Screen mode's scroll search** (`svc_motion_estimate.cpp`,
  `encoder_ext.cpp`): the per-frame feature index of the reference frame
  runs only on frames where scroll detection found a scroll; its 8x8 sums
  come from 8-byte row sums (psadbw / vpaddl) slid down the frame; only
  every 8th column is indexed (a vertical scroll's match sits at the
  block's own x). Same bitrate and picture; x86 callgrind on scroll 759M ->
  444M instructions in the sums, arm64 scroll 5.35 -> 1.8 ms a frame.
- **First IDR's QP** (`ratectl.cpp`): with bits to spare, the caller's QP
  floor bounds it, not OpenH264's table (which floored it at 22). Static
  content keeps its IDR's quality: typing 45 -> 54 dB at QP 17.
- **Experiments** (`encoder/core/inc/jumpseat_exp.h`): `JUMPSEAT_EXP` bits
  under test in bench/ (0: no intra 4x4 in P slices, 1: integer-pel motion).
  Winners become plain code; losers are deleted.

Measure every change with bench/ (BD-rate and CPU against x264 on recorded
desktop frames): `bench/local.sh rd usage=1`, `bench/modal_lab.py rd usage=1`.

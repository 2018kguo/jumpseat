# h264: jumpseat's encoder

OpenH264 v2.6.0's encoder (cisco/openh264 652bdb77, BSD 2-clause, LICENSE),
trimmed to what jumpseat links and changed for desktops. Not tracking
upstream: changes here are ordinary commits.

Kept: `api/` (the ISVCEncoder interface), `common/`, `encoder/`,
`processing/` (scroll, scene-change and complexity analysis), with x86-64
(nasm) and arm64 assembly. Dropped: the decoder, console apps, tests, test
streams, 32-bit ARM, MIPS and LoongArch.

    make -j8        # build/libjumpseat_h264.a; x86-64 needs nasm

# The encoder lab: compilers, nasm, x264, FFmpeg's decoder, perf, and a
# stock OpenH264 v2.6.0 build at /openh264 as the baseline. jumpseat's own
# encoder (h264/) is mounted at /h264: bench/local.sh, bench/modal_lab.py.
#   docker build -t jumpseat-bench bench
# Frames come from `jumpseat --record scroll.bgrx --frames 240`.
FROM debian:bookworm
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential nasm git ca-certificates libyuv-dev linux-perf libx264-dev libavcodec-dev valgrind \
    && rm -rf /var/lib/apt/lists/*
ARG OPENH264=v2.6.0
RUN git clone --depth 1 --branch "$OPENH264" https://github.com/cisco/openh264 /openh264 \
    && cd /openh264 && make -j"$(nproc)" BUILDTYPE=Release CFLAGS_OPT="-O3 -g -fno-omit-frame-pointer" libopenh264.a >/dev/null
WORKDIR /bench

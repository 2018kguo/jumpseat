# The encoder lab: OpenH264 from source (stock and patched) with symbols,
# the benchmark, and perf.
#   docker build -t jumpseat-bench bench
#   docker run --rm --privileged -v $DATA:/data -v $PWD/bench:/bench jumpseat-bench ...
# $DATA holds frames from `jumpseat --record scroll.bgrx --frames 240`.
# bench/run.sh usage=1 measures stock screen mode; for the patched build:
#   cd /openh264 && git apply /patches/screen-fme.patch && make -j8 libopenh264.a
# (with -v $PWD/openh264:/patches).
FROM debian:bookworm
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential nasm git ca-certificates libyuv-dev linux-perf libx264-dev \
    && rm -rf /var/lib/apt/lists/*
ARG OPENH264=v2.6.0
RUN git clone --depth 1 --branch "$OPENH264" https://github.com/cisco/openh264 /openh264 \
    && cd /openh264 && make -j"$(nproc)" BUILDTYPE=Release CFLAGS_OPT="-O3 -g -fno-omit-frame-pointer" libopenh264.a >/dev/null
WORKDIR /bench

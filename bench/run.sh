#!/bin/sh
# Builds encode_bench against the encoder at ${OPENH264:-/h264} (jumpseat's
# h264/, built; or a stock OpenH264 tree such as /openh264) and runs it on
# every recorded workload with the given settings: one line per workload.
# enc=1 runs the streamer's x264 instead.
set -e
src=${OPENH264:-/h264}
if [ -f "$src/build/libjumpseat_h264.a" ]; then inc=$src/api lib=$src/build/libjumpseat_h264.a
else inc=$src/codec/api lib=$src/libopenh264.a; fi
gcc -O2 -g -I"$inc" /bench/encode_bench.c /bench/x264_side.c /bench/decode_side.c "$lib" \
  -lyuv -lx264 -lavcodec -lavutil -lstdc++ -lm -lpthread -o /tmp/encode_bench
for w in ${WORKLOADS:-scroll anim type}; do
  printf '%-7s ' "$w"
  /tmp/encode_bench "/data/$w.bgrx" "$@" 2>/dev/null
done

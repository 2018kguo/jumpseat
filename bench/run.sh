#!/bin/sh
# Builds encode_bench against ${OPENH264:-/openh264} and runs it on every
# recorded workload with the given settings: one line per workload.
set -e
src=${OPENH264:-/openh264}
mkdir -p /tmp/inc/wels && cp "$src"/codec/api/wels/*.h /tmp/inc/wels/
gcc -O2 -g -fno-omit-frame-pointer -I/tmp/inc /bench/encode_bench.c "$src/libopenh264.a" -lyuv -lstdc++ -lm -lpthread -o /tmp/encode_bench
for w in scroll anim type; do
  printf '%-7s ' "$w"
  /tmp/encode_bench "/data/$w.bgrx" "$@" 2>/dev/null
done

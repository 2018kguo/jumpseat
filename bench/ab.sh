#!/bin/sh
# A/B: the encoder at a git ref (unpacked at /base by `local.sh ab REF` or
# `modal_lab.py ab REF`) against the working tree's (/h264), on every
# recorded workload. Three alternating runs each; prints the median encode
# CPU per frame and whether the two outputs match (bitrate and PSNR).
#   ab.sh [encode_bench key=value ...]      e.g. ab.sh usage=1 repeat=3
set -e
make -C /base/h264 -j"$(nproc)" -s >/tmp/base-build.log 2>&1 || { grep -iE error /tmp/base-build.log; exit 1; }
for side in base new; do
  src=/h264; [ $side = base ] && src=/base/h264
  gcc -O2 -I$src/api /bench/encode_bench.c /bench/x264_side.c /bench/decode_side.c $src/build/libjumpseat_h264.a \
    -lyuv -lx264 -lavcodec -lavutil -lstdc++ -lm -lpthread -o /tmp/eb_$side
done
for run in 1 2 3; do
  for side in base new; do
    for w in ${WORKLOADS:-scroll anim type}; do
      echo "$side $w $(/tmp/eb_$side /data/$w.bgrx "$@" 2>/dev/null)"
    done
  done
done | awk '
  { match($0, /cpu [0-9.]+/); cpu[$1 " " $2] = cpu[$1 " " $2] " " substr($0, RSTART + 4, RLENGTH - 4)
    match($0, /[0-9]+ kbit\/s.*psnr [0-9.]+ dB/); out[$1 " " $2] = substr($0, RSTART, RLENGTH); seen[$2] = 1 }
  function median(list,   n, v, i, j, t) {
    n = split(list, v, " ")
    for (i = 1; i <= n; i++) for (j = i + 1; j <= n; j++) if (v[j] < v[i]) { t = v[i]; v[i] = v[j]; v[j] = t }
    return v[int((n + 1) / 2)]
  }
  END {
    printf "%-7s %10s %10s %7s  %s\n", "", "base ms", "new ms", "change", "output"
    for (w in seen) {
      b = median(cpu["base " w]); n = median(cpu["new " w])
      printf "%-7s %10.3f %10.3f %+6.1f%%  %s\n", w, b, n, (n / b - 1) * 100, out["base " w] == out["new " w] ? "identical" : "DIFFERS: " out["base " w] " -> " out["new " w]
    }
  }'

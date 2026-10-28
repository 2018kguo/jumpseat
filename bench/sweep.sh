#!/bin/sh
# Rate-distortion sweep: x264 over CRF and OpenH264 over QP, one CSV row per
# point (encoder,workload,setting,ms,cpu_ms,kbps,psnr), for bench/bdrate.py.
#   sweep.sh [extra OpenH264 key=value ...]   e.g. sweep.sh usage=1
#   X264="xthreads=1" sweep.sh ...           x264 options
set -e
sh /bench/run.sh enc=1 >/dev/null 2>&1 || true   # builds /tmp/encode_bench
row() { /tmp/encode_bench "/data/$2.bgrx" $4 2>/dev/null |
  sed -E "s/.*encode ([0-9.]+) ms\/frame \(cpu ([0-9.]+).* ([0-9]+) kbit\/s.*psnr ([0-9.]+) dB/$1,$2,$3,\1,\2,\3,\4/"; }
for w in ${WORKLOADS:-scroll anim type}; do
  for c in 19 22 25 28 31; do row x264 "$w" "$c" "enc=1 crf=$c $X264"; done
  for q in 14 17 20 23 26 29; do row openh264 "$w" "$q" "qp=$q $*"; done
done

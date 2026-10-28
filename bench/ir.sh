#!/bin/sh
# CPU as instructions: callgrind counts per encoded frame for our encoder
# and the streamer's x264 (single thread) on each workload at one setting,
# in parallel. Deterministic, unlike wall time on a shared VM.
#   ir.sh qp=17 usage=1        (OpenH264 keys; x264 runs at crf=${CRF:-25})
set -e
sh /bench/run.sh enc=1 >/dev/null 2>&1 || true
count() { # name workload toggle args...
  name=$1 w=$2 toggle=$3; shift 3
  valgrind --tool=callgrind --toggle-collect="$toggle" --callgrind-out-file=/tmp/ir.$name.$w \
    /tmp/encode_bench /data/$w.bgrx "$@" >/dev/null 2>&1
  frames=$( (/tmp/encode_bench /data/$w.bgrx "$@" 2>/dev/null) | sed -E 's/frames ([0-9]+).*/\1/')
  ir=$(callgrind_annotate /tmp/ir.$name.$w 2>/dev/null | awk '/PROGRAM TOTALS/ {gsub(",", "", $1); print $1}')
  echo "$w $name $((ir / frames))" > /tmp/ir.$name.$w.txt
}
for w in ${WORKLOADS:-scroll anim type}; do
  count ours $w 'WelsEnc::CWelsH264SVCEncoder::EncodeFrame(*' "$@" &
  count x264 $w 'x264_side_encode' enc=1 xthreads=1 crf=${CRF:-25} &
done
wait
for w in ${WORKLOADS:-scroll anim type}; do
  ours=$(cut -d' ' -f3 /tmp/ir.ours.$w.txt) x264=$(cut -d' ' -f3 /tmp/ir.x264.$w.txt)
  awk -v w=$w -v o=$ours -v x=$x264 'BEGIN { printf "%-7s ours %5.1fM Ir/frame  x264 %5.1fM  ratio %.2f\n", w, o/1e6, x/1e6, o/x }'
done

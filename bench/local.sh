#!/bin/sh
# The encoder lab on this machine: builds h264/ (jumpseat's encoder), then
# runs a bench script in the lab container.
#   bench/local.sh rd usage=1        x264 vs ours, BD-rate (sweep + bdrate.py)
#   bench/local.sh run usage=1 qp=17 one point per workload
#   bench/local.sh ab main usage=1   the encoder at a git ref vs the working tree (ab.sh)
#   bench/local.sh sh -c '...'       anything else, in the lab container
# OPENH264=/openh264 measures the image's stock OpenH264 instead. Frames
# come from $BENCH_DATA (default ../jumpseat-bench-data).
set -e
here=$(cd "$(dirname "$0")" && pwd)
data=${BENCH_DATA:-$here/../../jumpseat-bench-data}
base=$here/../.bench/base
mkdir -p "$base"
lab() { docker run --rm --privileged -v "$data":/data -v "$here":/bench -v "$here/../h264":/h264 -v "$base":/base \
  -e OPENH264 -e WORKLOADS -e X264 -e JUMPSEAT_EXP jumpseat-bench "$@"; }
lab sh -c 'make -C /h264 -j8 -s >/tmp/build.log 2>&1 || { grep -iE "error" /tmp/build.log; exit 1; }'
case "$1" in
  rd) shift; lab sh /bench/sweep.sh "$@" | tee /tmp/jumpseat-sweep.csv | python3 "$here/bdrate.py" ;;
  run) shift; lab sh /bench/run.sh "$@" ;;
  ab) rm -rf "$base/h264" && git -C "$here/.." archive "$2" h264 | tar -x -C "$base" && shift 2 && lab sh /bench/ab.sh "$@" ;;
  *) lab "$@" ;;
esac

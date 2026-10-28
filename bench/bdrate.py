"""BD-rate of OpenH264 against x264 per workload, from sweep.sh's CSV on
stdin: the average bitrate difference at equal PSNR over the PSNR range both
curves cover (Bjontegaard, piecewise-linear in log rate). Negative means
OpenH264 needs fewer bits for the same picture."""
import csv, math, sys
from collections import defaultdict

curves = defaultdict(list)
for enc, work, _, ms, cpu, kbps, psnr in csv.reader(sys.stdin):
    curves[work, enc].append((float(psnr), math.log(float(kbps)), float(cpu)))

def at(curve, p):  # log rate (and cpu) at PSNR p, linear between points
    curve = sorted(curve)
    for (p0, r0, c0), (p1, r1, c1) in zip(curve, curve[1:]):
        if p0 <= p <= p1:
            t = (p - p0) / (p1 - p0) if p1 > p0 else 0
            return r0 + t * (r1 - r0), c0 + t * (c1 - c0)
    return None

for work in sorted({w for w, _ in curves}):
    a, b = curves[work, "x264"], curves[work, "openh264"]
    lo = max(min(p for p, *_ in a), min(p for p, *_ in b))
    hi = min(max(p for p, *_ in a), max(p for p, *_ in b))
    if hi <= lo:
        print(f"{work:7s} curves do not overlap"); continue
    steps = [lo + (hi - lo) * i / 50 for i in range(51)]
    diff = sum(at(b, p)[0] - at(a, p)[0] for p in steps) / len(steps)
    cpu = sum(at(b, p)[1] / at(a, p)[1] for p in steps) / len(steps)
    print(f"{work:7s} BD-rate {100 * (math.exp(diff) - 1):+6.1f}%  cpu x{cpu:.2f} of x264  ({lo:.1f}-{hi:.1f} dB)")

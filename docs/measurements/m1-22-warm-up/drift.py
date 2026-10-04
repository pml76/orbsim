#!/usr/bin/env python3
"""How far each benchmark series is from its steady value, over time (M1-22).

    drift.py <folder of benchmark CSVs> <window, s> <steady from, s> <show until, s>

Reads the warm-up rows of every CSV in the folder -- a run made with the
warm-up held long, so that they are one still view -- and prints, for the
frame interval, the CPU's working time and the GPU time, the median of each
window as a percentage above or below the median of everything after
<steady from>. docs/measurements/m1-22-warm-up.md is what it was used for.
Standard library only.
"""
import csv, glob, os, statistics, sys

d = sys.argv[1]
W = float(sys.argv[2])
STEADY_FROM = float(sys.argv[3])
SHOW_UNTIL = float(sys.argv[4])

for f in sorted(glob.glob(os.path.join(d, '*.csv'))):
    rows = [r for r in csv.DictReader(open(f)) if r['phase'] == 'warm-up']
    t = 0.0
    times = []
    series = {'interval_ms': [], 'cpu_working_ms': [], 'gpu_ms': []}
    for r in rows:
        times.append(t)
        t += float(r['interval_ms']) / 1000
        for k in series:
            series[k].append(float(r[k]))
    print(os.path.basename(f))
    for k, v in series.items():
        steady = statistics.median([x for x, tt in zip(v, times) if tt >= STEADY_FROM])
        out = []
        w = 0.0
        while w < SHOW_UNTIL:
            sample = [x for x, tt in zip(v, times) if w <= tt < w + W]
            out.append(f"{100 * (statistics.median(sample) / steady - 1):+.1f}")
            w += W
        print(f"  {k[:-3]:>12} (steady {steady:.4f} ms), % from steady per {W} s: {' '.join(out)}")

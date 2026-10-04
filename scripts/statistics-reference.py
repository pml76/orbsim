#!/usr/bin/env python3
"""Print the reference values tests/test_statistics.cpp asserts against (M1-22).

    statistics-reference.py

Why this exists: view/FrameStatistics.hpp computes percentiles and a mean in
double precision, and a test that recomputed the same formulas in the same
arithmetic would check the code against itself (VERIFICATION.md rule 2). The
values here are worked out in exact rational arithmetic, which shares no code
and no rounding with the C++, and each is then rounded once to the nearest
double -- the pattern scripts/srgb-reference.py set.

**The convention is linear interpolation between the two nearest ranks**
(register decision 366): Hyndman and Fan's type 7. For a sorted sample
x[0] <= ... <= x[n-1] and a fraction p in [0, 1],

    h = (n - 1) p,   k = floor(h),   value = x[k] + (h - k) (x[k+1] - x[k])

with x[k+1] read as x[k] when k = n - 1. The median of an even-sized sample
is then the mean of the two middle values, and p = 1 is the largest.

Each input is a double taken at its exact binary value (Fraction(float) is
exact), the fraction p included, so the reference is the true function at the
very numbers the C++ is handed.

**A second, independent reference is checked here as well**: Python's own
statistics.quantiles(method="inclusive"), which is the same convention written
another way -- a weighted average of the two neighbours, at p = i/100 exactly
-- in floating point. The script fails unless the two agree to within 4 ulp,
so a mistake in the exact formula above cannot pass quietly.

The sample is seeded (VERIFICATION.md rule 12), and its values are whole
multiples of 100 ns -- the resolution of the clock the application reads --
between 2 ms and 30 ms, about what a frame takes.

Deterministic, and needs nothing beyond the standard library.
"""

from fractions import Fraction
import math
import random
import statistics

SEED = 20261004
COUNT = 37
TICK = Fraction(1, 10_000_000)  # 100 ns, in seconds


def type7(sorted_sample, p):
    """The exact type-7 percentile of an exact sorted sample at exact p."""
    n = len(sorted_sample)
    h = (n - 1) * p
    k = math.floor(h)
    lower = sorted_sample[k]
    upper = sorted_sample[min(k + 1, n - 1)]
    return lower + (h - k) * (upper - lower)


def ulps_apart(a, b):
    return abs(a - b) / math.ulp(max(abs(a), abs(b)))


def main():
    rng = random.Random(SEED)
    ticks = [rng.randint(20_000, 300_000) for _ in range(COUNT)]
    sample = [float(t * TICK) for t in ticks]
    exact = sorted(Fraction(v) for v in sample)

    print(f"// {COUNT} frame times in seconds, seed {SEED}, in the order drawn")
    print("constexpr auto kSample = std::to_array<f64>({")
    for v in sample:
        print(f"    {v!r},")
    print("});")

    # The fractions the summary uses, each as the double the C++ holds.
    library = statistics.quantiles(sample, n=100, method="inclusive")
    for name, p, index in (("Median", 0.5, 50), ("P95", 0.95, 95), ("P99", 0.99, 99)):
        reference = float(type7(exact, Fraction(p)))
        second = library[index - 1]
        apart = ulps_apart(reference, second)
        if apart > 4:
            raise SystemExit(f"{name}: the two references are {apart} ulp apart")
        print(f"constexpr f64 kSample{name} = {reference!r}; "
              f"// statistics.quantiles: {second!r}, {apart:g} ulp")

    print(f"constexpr f64 kSampleMax = {float(exact[-1])!r};")
    mean = sum(exact) / len(exact)
    print(f"constexpr f64 kSampleMean = {float(mean)!r}; "
          f"// statistics.fmean: {statistics.fmean(sample)!r}")


if __name__ == "__main__":
    main()

# M1-118 — The simulation core's review findings

Phase: B | Status: not started
Prerequisites: M1-117
Decided by: register decisions 440, 442 and 450, ruled 2026-10-10 on the
review's findings 1.1-1.7, 3.1 and 3.12 ([the findings](../../review/m1-116-findings.md))

## Purpose

The review found the core returning values outside what it promises, and
two of its tests unable to fail. Each fix starts from a test that fails.

## What to do

- **1.1** `wrapTau` returns 2π exactly for a tiny negative angle, so
  `elementsFromState` can return a true anomaly of 2π: map it to 0; a
  compile-time case at -1e-20 and an element case just before periapsis.
- **1.2** `orbitInfo`'s mean motion overflows `a³`: `sqrt(mu / a) / a`, and
  cases at a = 1e110 m and 1e-110 m.
- **1.3** double-double near the largest double: scale in `twoProduct`, or
  fall back when the error term is not finite; and `test_double_double`'s
  sampler draws the whole [1, 2) mantissa and its highest exponent, from the
  engine directly (finding 3.8's rule).
- **1.4** `propagateElements` reports `UnreachableAnomaly` by a test of
  1 + e cos ν, not by a NaN; its header says so.
- **1.5** `integrateAngularVelocity` turns for a negative step; its first
  test.
- **1.6** the anomaly converters return `std::expected` -- `UnreachableAnomaly`
  and a parabolic refusal -- and their callers handle it (decision 440).
- **1.7** `angleBetween` asserts that neither vector is zero.
- **3.1** the anomaly converters checked against the half-angle formula and a
  published worked example, elliptic and hyperbolic -- not only against each
  other.
- **3.12** `fuzz_time` adds and subtracts fuzzed durations; `fuzz_orbit`
  feeds arbitrary element sets to `stateFromElements`, `propagateElements`
  and `orbitInfo`, and checks the ranges 1.1 and 1.2 promise, not only NaN.
  Each fuzzer run for its budget from its corpus (VERIFICATION.md rule 13).

## Mutants

The review's own planted faults, kept: the √(1 - e) and √(1 + e) swap in the
converters; `wrapTau` without its fix; the cube in `orbitInfo`; the
backward-rotation comparison; each caught by the new tests.

## Done when

- [ ] Every finding above fixed, each with a test seen failing first.
- [ ] Both fuzzers run for their budget from their corpus.
- [ ] `check` passes in both trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed.

# M1-22 — The benchmark mode

Phase: A | Status: not started
Prerequisites: M1-21

## Purpose

`realism.md` section 6.5 states the fluency budget as a number — *16.6 ms at
1920×1080, High preset, on the RTX A2000, viewing Earth from 400 km* — and a
number nobody measures is a wish. This builds the measuring instrument, early,
so that phases D, C and G each have a comparable figure rather than a single
verdict at the end.

**It is deliberately not a test.** A timing threshold on shared hardware fails
for reasons that have nothing to do with the change, and the guidelines example
already makes this argument about its own benchmark.

## What to implement

- **`--bench <path> [--frames N] [--width W --height H] [--quality <preset>]`**:
  replays a scripted `CameraPath` from M1-21, renders N frames, and exits.
- **CPU frame time** measured around the whole frame, and **GPU frame time** from
  a `VkQueryPool` of timestamps, converted with the device's
  `timestampPeriod` — because a CPU-bound frame and a GPU-bound frame want
  different fixes, and one number cannot tell them apart.
- A warm-up period that is discarded, long enough for clocks to settle, with the
  count stated and defensible rather than a round number chosen by feel.
- Output: mean, median, p95, p99 and max for both clocks, plus a CSV of every
  frame so a spike can be found rather than averaged away. Printed with the GPU
  name, driver version, resolution, quality preset and build configuration — a
  frame time without those is not a measurement.
- The statistics are pure functions in `orbsim_view`.

## Out of scope

Any assertion about the numbers. Automatic regression detection. A profiler
integration — Tracy is worth having later and is not this task.

## Tests

`tests/test_statistics.cpp`, headless:

- Percentiles against hand-computed values on small samples, including the two
  awkward cases: an even-sized sample where the median interpolates, and a
  single-element sample.
- The interpolation convention is stated in the header and asserted, because
  "p95" means at least three different things in common use.
- An empty sample is reported, not divided by zero.

The benchmark itself is verified by running it and reading the output.

## Verification

The standing rules, then:

```
orbsim --bench grid-orbit --frames 600 --width 1920 --height 1080
```

and the numbers recorded in the commit message. At this point the scene is a
wireframe grid, so the figure is a floor rather than a result — which is exactly
what makes it useful later: everything phases B, D and C add is measured against
this.

## Done when

- [ ] `check` green in both trees.
- [ ] Both CPU and GPU times are reported, with percentiles and a CSV.
- [ ] The baseline figure for the grid scene is recorded in the commit message
      and in `PROJECT_STATE.md`.
- [ ] Nothing in `check` asserts a frame time.

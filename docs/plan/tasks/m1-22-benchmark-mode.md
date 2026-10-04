# M1-22 — The benchmark mode

Phase: A | Status: **done, 2026-10-04**
Prerequisites: M1-21

**Amended 2026-10-04, when the task ran.** Fourteen questions were put before
the code and every recommendation was taken (register decisions 364-377),
and seven more once the warm-up was measured (378-384).
What they add to the text below is marked with its decision; the original is
in the git history. The warm-up's figure was measured first and then ruled:
[`../../measurements/m1-22-warm-up.md`](../../measurements/m1-22-warm-up.md).

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
  *`<path>` is a built-in path's name, and an unknown one is refused with the
  list (decision 369); `--quality` takes low, medium, high or ultra, high by
  default, with `--bench` only (decision 371); `--width` and `--height` count
  the drawn image's pixels, 1920x1080 by default, and a run whose image comes
  out any other size stops (decision 370); `--bench-out <dir>` says where the
  files go (decision 373).*
- **`grid-orbit`** *(decisions 351 and 368)*: a full circle at 400 km on an
  orbit inclined 51.6 degrees, looking ahead along the track with the horizon
  across the middle of the picture, a keyframe every degree, spread over the
  N measured frames so frame k is the same view on every machine.
  `src/view/BenchmarkPath.hpp`.
- **CPU frame time** measured around the whole frame, and **GPU frame time** from
  a `VkQueryPool` of timestamps, converted with the device's
  `timestampPeriod` — because a CPU-bound frame and a GPU-bound frame want
  different fixes, and one number cannot tell them apart. *Two CPU series
  (decision 365): the interval from one frame's start to the next's, and the
  part of it the CPU spent working -- the interval less its waits for the GPU
  and the display. The conversion is `src/view/GpuClock.hpp`, which takes the
  counter's valid bits into account.*
- **A swapchain that does not wait for the display** *(decision 364)*:
  immediate, then mailbox, then FIFO, and the run prints which it got.
- A warm-up period that is discarded, long enough for clocks to settle, with the
  count stated and defensible rather than a round number chosen by feel.
  *Measured before it was fixed, and counted in seconds with a minimum number
  of frames (decision 367): 5 s and at least 30 frames (decision 378).*
- Output: mean, median, p95, p99 and max for both clocks, plus a CSV of every
  frame so a spike can be found rather than averaged away. Printed with the GPU
  name, driver version, resolution, quality preset and build configuration — a
  frame time without those is not a measurement. *And the present mode, the
  display's refresh rate, the compiler and whether the validation layers were
  on (decisions 372 and 376); written as `<path>-<UTC time>.txt` and `.csv`
  to the build tree's `bench/`, the table marking the warm-up's frames
  (decision 373). Validation is off unless `--validate` is given, and a run
  under it, or with assertions compiled in, says its figures are not
  representative (decision 372).*
- The statistics are pure functions in `orbsim_view`. *`src/view/FrameStatistics.hpp`;
  a percentile is linear interpolation between the two nearest ranks
  (decision 366), the mean a compensated sum (decision 375).*

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

*Added (decisions 366 and 375): a seeded sample of 37 frame times against
references from `scripts/statistics-reference.py`, worked in exact fractions
and checked there against Python's `statistics.quantiles`, held to 7e-16 --
twice the measured 3.3e-16; a value that is not finite or is negative,
reported, at either end and in the middle; and a sum a plain addition gets
wrong. `tests/test_gpu_clock.cpp`: ticks into seconds, a counter that wraps
round between two readings, and a clock that cannot be one.
`tests/test_benchmark_path.cpp`: the path's altitude, its sag between
keyframes, the line of sight grazing the Earth, the inclination, the period
against a 50-digit value, determinism, and the frames' times. Each was seen
failing against a stub first.*

*In `check` (decision 372): `orbsim_bench_smoke` runs 30 frames under the
validation layers and checks the files -- and that no GPU time is longer than
the whole run (decision 380), which is not a performance threshold -- asserting
no frame time; and nine `usage_` tests refuse the options' misuses by name.*

The benchmark itself is verified by running it and reading the output.

## Verification

The standing rules, then:

```
orbsim --bench grid-orbit --frames 600 --width 1920 --height 1080
```

and the numbers recorded in the commit message. At this point the scene is a
wireframe grid, so the figure is a floor rather than a result — which is exactly
what makes it useful later: everything phases B, D and C add is measured against
this. *This machine's figure, labelled with its card, now; the RTX A2000's
beside it when the owner next works on the first machine (decision 374).
Three runs, the middle one the headline (decision 383): on the RX 7900 XTX,
frame interval 0.4442 ms median and 0.5108 ms p95, CPU working 0.1641 ms,
GPU 0.0593 ms -- [`../../PROJECT_STATE.md`](../../PROJECT_STATE.md) section 10.*

## Done when

- [x] `check` green in both trees -- 509 of 509 in each, 2026-10-04;
      `windows-msvc` 508 of 508.
- [x] Both CPU and GPU times are reported, with percentiles and a CSV.
- [x] The baseline figure for the grid scene is recorded in the commit message
      and in `PROJECT_STATE.md`.
- [x] Nothing in `check` asserts a frame time.
- [x] The mutation pass is run and recorded, as
      [`scripts/mutants/m1-22.json`](../../../scripts/mutants/m1-22.json), with
      decision 382's mutants: **thirteen caught and the three declared
      survivors surviving**, none invalid or hung, at `1401cc8`; the mask one
      bit short by its `static_assert`, the swapped timestamps by
      `orbsim_bench_smoke`'s check of decision 380. **One kill is recorded for
      what it was**: the reversed pitch makes the camera's axes stop being a
      rotation, so in Debug it dies on `quaternionFrom`'s precondition, and in
      `relwithdebinfo`, by hand the same day, the suite crashes where the
      refused path is used -- both before the horizon test that names it can
      look.

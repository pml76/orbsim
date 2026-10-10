# M1-120 — The application's and the renderer's review findings

Phase: B | Status: not started
Prerequisites: M1-119
Decided by: register decisions 441 and 450, ruled 2026-10-10 on the review's
findings 2.1-2.12 ([the findings](../../review/m1-116-findings.md))

## Purpose

**Minimising the window ends the program with exit code 1** -- reproduced on
the RTX A2000 -- and eleven smaller findings in `src/app/`, `src/render/` and
`src/view/`.

## What to do

- **2.1, first**: a window SDL reports as minimised gives no frame, and a zero
  surface extent is refused before a rebuild. A test that minimises the
  window under the validation layer, if the smoke test's harness can drive
  one; otherwise a manual check written into the gates -- put to the owner at
  the start.
- **2.2** `--accept-golden` fails when the validation layer did not run; the
  benchmark labels validation from what ran.
- **2.3** `--probe-out` without `--probe`, and `--probe-list` with anything
  else, refused, each with a usage test.
- **2.4** the functions that narrow to `f32` return `std::expected` where a
  setting the factories accept cannot be narrowed (decision 441); the false
  comments corrected; cases at the review's inputs.
- **2.5** the grid layout's products checked for overflow; the review's case
  added to the refusal test.
- **2.6** a camera path whose span overflows refused by `from()`.
- **2.7** a suboptimal acquire marks the swapchain for rebuilding.
- **2.8** push-constant stages derived from each range, or held to it by a
  `static_assert`.
- **2.9** a failed rebuild leaves the context marked for rebuilding, with no
  stale images listed.
- **2.10** `VK_EXT_swapchain_maintenance1` used where the driver offers it,
  today's path kept where it does not (decision 441); both paths run on both
  cards.
- **2.11** `writeEncoded` and `writeFrameFiles` return `std::expected`, and
  the sidecar names the file that failed and why.
- **2.12** `nanosecondsPerTick` carries its unit; `formatInstant` cannot
  print 60 seconds; `Probe::conditions` initialised.

## Mutants

The minimise guard removed; the suboptimal flag removed; each new refusal
removed; a push stage changed -- each caught. The swapchain extension's path
judged on the GPU, per card.

## Done when

- [ ] Minimising and restoring the window runs on, with no validation error,
      on both cards.
- [ ] Every other finding above fixed, each with a test seen failing first
      where a test can see it.
- [ ] `check` passes in both trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed.

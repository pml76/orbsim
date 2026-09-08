# M1-16 — Probe mode: deterministic frames

Phase: A | Status: not started
Prerequisites: M1-13, M1-15

## Purpose

The renderer has no machine-checkable output. `orbsim_smoke` proves the app
starts, paces frames and shuts down without a validation error; it cannot see a
wrong matrix, a wrong colour or a missing tile. Every acceptance criterion in
phases A, B, D, C and G is currently a human judgement.

This is the task that changes it, and it comes **before** the things it
verifies, so that no render task is ever verified by looking and then verified
again properly later.

It also delivers the owner's standing requirement: **a frame to look at, every
run, pass or fail.**

## What to implement

- **`--probe <name>`**, and `--probe-list`. A probe renders exactly one frame
  and exits. Everything that could vary is pinned: **1280×720** regardless of
  window size, a fixed epoch as a `TimePoint`, a fixed camera pose, a fixed
  `RenderQuality` preset, and no dependence on wall-clock time anywhere in the
  path. A probe that renders differently twice is a bug in the probe.
- **A probe registry** in `src/render/Probes.hpp`: a name, a description, a
  scene setup function. Names are stable identifiers — a golden image is keyed
  by one.
- **Readback**, both of them:
  - the **linear HDR target**, copied to a host-visible buffer and written as
    `<out>/<name>.hdr.f32` — raw `f32` RGBA, converted from `f16` on the CPU,
    with the width and height in a header line. This is what the numeric tests
    in M1-18 read;
  - the **tonemapped image**, written as `<out>/<name>.png` at 1280×720. This is
    what a person looks at.
- **A sidecar** `<out>/<name>.txt` recording what produced the frame: probe
  name, epoch, camera pose, quality preset, GPU name, driver version, build
  configuration and date. A frame without provenance is a screenshot.
- **The output directory defaults into the build tree** (`<build>/probes/`) and
  is written **on every run, whether or not anything passes**. That is the
  requirement, not a convenience.
- **`stb_image_write` is pinned here** (`stb`, dual MIT/Unlicense), SYSTEM, as
  the fifth FetchContent dependency; `THIRD_PARTY.md` gains its row. M1-28 later
  uses `stb_image` from the same pin for JPEG decoding, and M1-78 uses
  `stb_truetype`.
- **The first probe: `clear`.** It draws the clear colour and a full-screen
  gradient through the real pipeline, the real HDR target and the real tonemap.
  Trivial on purpose: it is the probe that fails when the machinery is broken
  rather than the scene.

## Out of scope

Comparing anything — M1-17. The radiometric assertions — M1-18. Headless
rendering with no window: the window is still created and simply never
presented, which keeps one device-creation path rather than two.

## Tests

- `probe_clear` is a CTest test, labelled `gpu`, that runs the probe and
  requires exit code 0 and the three output files to exist and be non-empty.
- **Determinism**: the probe is run twice and the two `.hdr.f32` dumps are
  **byte-identical**. This is rule 16 applied to rendering, it is cheap, and it
  is what catches a frame that depends on the clock, on uninitialised memory or
  on a race.
- The `.hdr.f32` reader — used by the tests, not by the app — is a small header
  in `tests/` with its own malformed-input tests, in the shape M1-06 sets.

## Frames to look at

`<build>/probes/clear.png`. Judge: the gradient is smooth with no banding, the
image is the right way up, and the colours are what the scene says. This is the
first frame this project has ever produced, so it is also the first sign-off.

## Verification

The standing rules, plus `ctest -R probe_ --output-on-failure`, plus opening the
PNG.

## Done when

- [ ] `check` green in both trees, `probe_clear` included.
- [ ] Two consecutive runs produce byte-identical HDR dumps.
- [ ] The PNG, the HDR dump and the sidecar are written on a failing run too.
- [ ] The owner has looked at `clear.png` and said so.

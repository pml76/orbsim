# M1-16 — Probe mode: deterministic frames

Phase: A | Status: **built 2026-09-26; done once the owner has looked at `clear.png`**
Prerequisites: M1-03, M1-11, M1-12, M1-13, M1-15 *(Corrected 2026-09-21: the queue says each task lists its true
prerequisites so that a reordering can be reasoned about, and this task pins a
fixed epoch as a `TimePoint` (M1-03), a fixed camera pose (M1-11) and a fixed
`RenderQuality` preset (M1-12). None of the three changes the running order.)*
Decided by: [ADR 0008](../../adr/0008-renderer-verification.md), [ADR 0014](../../adr/0014-radiometric-chain.md)

> **A KNOWN GAP, found by this task's mutation pass and not yet ruled on**:
> the barrier that makes the probe's readback copies visible to the host
> (`makeWritesVisibleToHost` in `render/VulkanContext.cpp`) can be removed with
> nothing noticing on this machine, whose readback memory is host-coherent;
> the validation layers do not check host access. Declared in
> `scripts/mutants/m1-16.json` and put to the owner with the task's report.
> **Six survivors handed to this task still survive**, because they act after
> the HDR target its numeric check reads; each now names the task that kills
> it (M1-17, M1-18, M1-19).

**Every question went up before any code was written**, and the owner ruled
them the same day: decisions 187-199 of the
[register](../milestone-1-decisions.md), with two things the owner added --
a 16-bit PNG and an EXR -- and a ruling that set the processor the project
assumes ([ADR 0023](../../adr/0023-the-processor-we-assume.md), decisions
200-203). Two more were raised as the work met them, each measured first and
put to the owner before it was settled: how OpenEXR's C interface meets the
lint (decision 204) and a signalling NaN under MSVC's compile-time evaluator
(decision 205).

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
- *(Amended 2026-09-26, register decisions 195-197: stb is pinned here, but
  **its decoder is what is used**, by the tests; the PNGs are written by
  **lodepng**, which writes 16 bits as well as 8, and the owner added an EXR,
  written by **OpenEXR**. The bullet below is the plan as it was.)*
- **`stb_image_write` is pinned here** (`stb`, dual MIT/Unlicense), SYSTEM, as
  the **ninth** FetchContent dependency; `THIRD_PARTY.md` gains its row.
  *(Corrected 2026-09-21. This said "the seventh -- Catch2 was the fifth
  (M1-01) and ERFA is the sixth (M1-05)". Catch2 was indeed the fifth, on
  2026-09-09, but two more were pinned before ERFA: Vulkan-Utility-Libraries
  on 2026-09-16 and mp-units on 2026-09-18, so ERFA is the eighth. An ordinal
  counted from "the original four" goes stale every time one is added.)* M1-28 later
  uses `stb_image` from the same pin for JPEG decoding, and M1-78 uses
  `stb_truetype`.
- **The first probe: `clear`.** It draws the clear colour and a full-screen
  gradient through the real pipeline, the real HDR target and the real tonemap.
  Trivial on purpose: it is the probe that fails when the machinery is broken
  rather than the scene.

**Two things M1-14 hands this task** *(added 2026-09-24, register decisions
166 and 167)*. The HDR target is created with `COLOR_ATTACHMENT` and
`SAMPLED` usage only; the readback needs `TRANSFER_SRC` added in
`VulkanContext::createHdrTarget`. And three of M1-14's declared survivors in
`scripts/mutants/m1-14.json` are frames that draw the wrong thing -- the
shader's encode with the wrong exponent, a resolve pass never drawn, a
full-screen triangle a quarter the size -- which this task's `clear` probe is
the first thing able to see. Re-run them when it lands, and remove each
declaration it kills.

**Two things M1-15 hands this task** *(added 2026-09-25, register decisions
179 and 182)*. The exposure is fixed when `render/ResolvePass` is created --
`ResolvePass::create` takes a `view::PerRadiance` -- so a probe pins its own
exposure by creating the pass with it, and the sidecar records the three
camera settings. And three of M1-15's declared survivors in
`scripts/mutants/m1-15.json` -- a shader that skips the exposure or either of
its two clamps -- become visible once a frame is read back; the port check
that kills them is M1-18's, and the `clear` probe is where they first show.

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

- [x] `check` green in both trees, `probe_clear` included -- 266 of 266.
- [x] Two consecutive runs produce byte-identical HDR dumps --
      `probe_clear_determinism`, on every `check` run.
- [x] The PNG, the HDR dump and the sidecar are written on a failing run too
      -- shown by hand with a planted validation error, and first by accident
      on a real one (below).
- [ ] The owner has looked at `clear.png` and said so.

## What was built

- **`src/render/VulkanContext.*`** -- `renderOffscreen`: a 1280x720 HDR target
  and depth image made by the same `createImage` and the same requests as the
  window's (decision 187), the scene begun by the same `beginSceneRendering`,
  resolved by the same `recordResolveInto` into an 8-bit and a 16-bit UNORM
  display image, all three copied to `Memory::HostReadback` buffers and made
  visible to the host in one submission, waited on its fence. The HDR target's
  one image description now includes `TRANSFER_SRC` (decision 188), and the
  device is asked for it and for both display formats by name.
  `ResolvePass::create` takes the format it draws into.
- **`src/render/Probes.*`** -- the registry: name, description, pinned
  conditions, scene. Names are checked while compiling -- lowercase, digits,
  hyphens, unique. `clear` pins J2000.0 TT, an unrotated camera at the origin
  with a 45-degree field of view, the `high` preset and f/16, 1/125 s, ISO 100
  (decision 190).
- **`shaders/probe_gradient.frag`**, **`src/view/ProbeGradient.hpp`** -- the
  picture of decision 189: four bands over an undrawn strip, each ramping
  evenly in stops from AgX's black to its white at the pinned exposure, 0.0682
  to 6,324 W/(m^2 sr). `toShaderRamp` is the third named narrowing to `f32` in
  `src/`. **`shaders/fullscreen.vert` writes depth 0.5**, so the probe's
  depth-tested triangle is decided by the reverse-Z comparison (decision 194).
- **`src/app/ProbeMode.*`**, **`src/app/main.cpp`** -- `--probe`,
  `--probe-list`, `--probe-out`, the refusals of decision 192, a hidden window,
  and the five files, the sidecar last and in every case (decision 193).
  `src/app/ExitCodes.hpp` holds the exit codes both files use.
- **`src/view/Half.hpp`**, **`ProbeImage.*`**, **`ImageFiles.*`**,
  **`ProbeSidecar.*`** -- the headless half, committed first: the exact
  binary16 conversion, the dump, both PNGs by lodepng, the EXR by OpenEXR, the
  sidecar's text.
- **Tests**: `test_half`, `test_hdr_dump` (with `tests/HdrDumpFile.*`, the
  reader), `test_image_files`, `test_probe_sidecar`, and on the GPU path
  `probe_clear`, `probe_clear_determinism` (`cmake/RunProbe.cmake`) and
  `test_probe_clear`, which reads the frame back as a CTest fixture -- the
  pattern M1-18's numeric probes copy.

## What was measured before it was relied on

- **A swapchain builds on a hidden SDL window** on this machine, the RTX A2000
  -- a standalone spike, before `--probe` hid its window.
- **The binary16 conversion on all 65,536 patterns**, against a `std::ldexp`
  formula and against the compiler's `_Float16`; and seen failing first, on a
  planted defect at one pattern that both comparisons named.
- **How the GPU rounds a write to RGBA16F: toward zero.** Every value of a
  1,280-pixel row of `clear`'s grey band came back as the lower binary16
  neighbour of the exact radiance, the furthest 9.4e-4 below it. The
  specification leaves the mode undefined ("rounded to one or the other"), so
  the budget allows either neighbour, and M1-18's quantisation floor is up to
  0.1 %, not 0.05 % -- a note is in its task document.
- **Determinism**: two runs' HDR dumps byte-identical, on every `check` run
  since, in both trees.

## The mutation pass

`scripts/mutants/m1-16.json`: **21 mutants, 20 caught, 1 survived, none
invalid or hung** -- 3 by a `static_assert` before a test ran. Each kill was
read rather than counted: every one fell to the judge it was aimed at, the GPU
ones to `test_probe_clear`'s band check, `--probe-out` ignored to
`probe_clear_determinism`, and an HDR target without `TRANSFER_SRC` to
`probe_clear`'s validation layers.

**The survivor is a real gap, not yet ruled on**: the barrier that makes the
readback copies visible to the host can be removed with nothing noticing --
the memory here is host-coherent, the fence wait does the rest, and the
validation layers do not check host access.

**The first run stopped at the eighth mutant**: `scripts/mutate.py` decoded a
suite's output in the Windows code page, and one byte outside it crashed the
reader. It decodes UTF-8 with replacement now, and the whole pass was re-run.

**The survivors handed to this task, re-run with the probe's tests as their
judges**: M1-13's `LESS` depth comparison and M1-14's quarter-size triangle
are caught, and their declarations are gone. Six still survive, all acting
after the HDR target the numeric check reads -- M1-14's resolve pass never
drawn and its 2.2 encode, M1-15's three shader operations -- and one M1-14
mutant in the scene pipelines, which `clear` does not use. Each declaration
now names the task that kills it: M1-17's golden, M1-18's port check, M1-19's
first scene draw. **The task text expected more of this task than it could
give** -- "the first thing able to see" them is true of a person looking at
`clear.png`, not of a test -- and decision 194 is where that was said before
the work began.

**Artefacts on a failing run, shown by hand** (decision 193): with the HDR
target's `TRANSFER_SRC` removed, the probe exits 3 on a validation error and
`cmake/RunProbe.cmake` reports first that all five files were written. The
very first probe run did the same by accident, on a real validation error --
glslc compiling `discard` to an instruction that needs a device feature,
replaced by `terminateInvocation`.

## What it costs

**Not measured, and the window's frame is not expected to change**: the probe
runs only under `--probe`, and what the window's path gained is one image
usage flag on the HDR target (`TRANSFER_SRC`, decision 188) and a constant
depth in `fullscreen.vert`. A usage flag can in principle cost a GPU its
compression of that image, so this is put to the owner with the task's report
rather than assumed; `scripts/measure-frame-cost.py` is how it would be
measured.

## Other compilers

Run now rather than at M1-23's gate, as for M1-15.

- **gcc-14 found four things**, all in this task's new code and all fixed:
  `std::array` initialisers it wanted with inner braces
  (`-Wmissing-braces`), which clang-tidy's trailing-comma check then
  contradicted -- both satisfied by `std::to_array`; lambdas handed to
  algorithms it wanted `noexcept`; a `return ok;` it could not elide
  (`-Wnrvo`), now a returned error; and `-Wabi-tag` on the declarations that
  carry a `std::string`, off at those sites alone in decision 52's shape.
  Two warnings raised by OpenEXR's C interface -- its initialiser macro's
  zero pointers and its anonymous union's braces -- are off at those sites
  alone ([ADR 0017](../../adr/0017-every-warning-is-an-error.md)).
  `linux-gcc` and `linux-sanitize` then pass **259 of 259** each, everything
  they build, with `-mavx2` on all 691 compile commands.
- **MSVC found two**: a Catch2 `INFO` calling a function inside its `<<`
  chain (C4866), and **its compile-time evaluator turns a signalling NaN quiet**
  whenever it passes through a `float` -- `0x7FA02000` came back as
  `0x7FE02000`, measured, where run time and clang keep it. The conversion was
  right and the claim was made on the wrong representation, so it moved onto
  the bit pattern: `halfToFloatBits` returns the bits and `halfToFloat` is
  their `bit_cast` (the owner's ruling, decision 205). MSVC then builds the
  whole tree with no warning and passes **265 of 265** -- one case fewer,
  because it has no `_Float16` to compare against.
- Both Windows trees pass `check`, **266 of 266**.

## Found on the way

- **The GPU rounds toward zero** on a write to RGBA16F (above), which moved
  M1-18's quantisation floor; the note is in its task document.
- **`scripts/mutate.py` crashed on one byte** of a suite's output, decoded in
  the Windows code page; it decodes UTF-8 with replacement now.
- **CLion reconfigures `build/debug` on its own** when a `CMakeLists.txt`
  changes, and a build started at the same moment fails in ways that look like
  real errors. A build is started only once CLion's own has finished.

# M1-18 — Numeric probes, and the radiometry budget

Phase: A | Status: **done, 2026-10-03** -- code, tests and documents; the mutation pass runs on the committed code
Prerequisites: M1-15, M1-16
Decided by: [ADR 0008](../../adr/0008-renderer-verification.md), [ADR 0014](../../adr/0014-radiometric-chain.md)

**Every question went up before any code was written**, and the owner ruled
them on 2026-10-03: decisions 256-266 of the
[register](../milestone-1-decisions.md). The owner chose to draw the patch
through the camera rather than as a full-screen triangle (decision 257), and
the five questions that choice raised were put and ruled the same day,
decisions 267-271. One more was found on the way and ruled before it was
touched: `uploadBuffer`, which nothing had called, did not make its copy
visible to what runs after it -- fixed first, in a commit of its own, as
[M1-107](m1-107-upload-visibility.md) (decision 272). After the mutation pass
of that day the owner asked whether a survivor that always survives is a
failure of test design; the answer and its follow-up task are decision 273 and
[M1-108](m1-108-reachable-rules.md).

## Purpose

Phase A's acceptance criterion says the grid must be *"lit through the
radiometric chain end to end, from a value in W/m² to a tonemapped pixel"*.
That is a number, so it gets measured rather than admired. This task turns the
HDR readback into assertions, and establishes the pattern every later LUT probe
follows.

## What to implement

- **The `lambert` probe**: a flat patch of albedo 0.3, oriented normal to the
  Sun, at exactly 1 AU, filling the frame. Nothing else in the scene, no
  atmosphere, no ambient term. The shader computes `L = albedo · E · cos θ / π`
  from the irradiance M1-08 supplies.
- **The CTest structure that every numeric probe reuses**: the probe run is a
  fixture, and the analysis is an ordinary Catch2 test that reads the dump:
  ```cmake
  set_tests_properties(probe_lambert     PROPERTIES FIXTURES_SETUP    probe_data LABELS gpu)
  set_tests_properties(test_radiometry   PROPERTIES FIXTURES_REQUIRED probe_data)
  ```
  The GPU work stays in the app; the arithmetic and the reference stay in a test
  that links no Vulkan.
- **`tests/test_radiometry.cpp`**, which computes the expected radiance itself —
  `0.3 × 1361 / π = 129.97 W·m⁻²·sr⁻¹` (129.9659…) — from the constant and the
  definition, not from anything in `src/render/`. The test computes it rather
  than quoting it, so the rounding here can never be what the assertion uses.

## Out of scope

Atmosphere, which is phase D and gets its own probes on this same machinery.
Any scene with more than one surface. Auto-exposure.

## Tests

- **The budget: the read-back HDR pixel is within 0.5 % of the analytic
  radiance.** Checked in the centre of the patch and at four off-centre points,
  which is what catches an interpolation or a viewport error that the centre
  alone would pass.
- **The quantisation floor is stated and checked**: RGBA16F carries about
  0.05 % relative precision at this magnitude, so the 0.5 % budget is testing
  the chain rather than the format — asserted by confirming the dump's values
  land on representable `f16` neighbours of the expected value.
  *(Added 2026-09-26 by M1-16: **either** neighbour, not the nearest. The
  Vulkan specification leaves the rounding of a float written to RGBA16F
  undefined, and this machine's GPU was measured rounding toward zero -- every
  value of a 1,280-pixel row of the `clear` probe landed on the lower
  neighbour, up to 9.4e-4 below the exact value. So the floor is up to 0.1 %,
  not 0.05 %; the 0.5 % budget still tests the chain, five times over.
  `tests/test_probe_clear.cpp` has the check in the shape this one can copy.)*
- **The inverse-square law, on the GPU path**: the same patch at 0.5 AU and at
  2 AU reads back 4× and ¼× the radiance, to the same 0.5 %. A hard-coded
  irradiance passes the first test and fails this one.
- **Exposure does not touch the HDR readback**: the dump is identical for two
  different exposure settings, and only the PNG differs. That is the whole
  architecture of the chain, asserted once.
- **cos θ**: the patch tilted 60° from the Sun reads back half the radiance.
- **The tonemap port check** from M1-15: the PNG's centre pixel matches
  `view/Tonemap.hpp` applied to the HDR value, to 1/255. *(Added 2026-09-25 by
  M1-15: the CPU chain is `view::radianceExposure` for the probe's pinned
  camera, then `view::agxTonemap`, then `view::srgbEncode` -- the shader's
  order. This is the check that kills the three shader-operation survivors
  declared in `scripts/mutants/m1-15.json`; re-run them when it lands and
  remove each declaration it kills. The analytic 129.97 W/(m^2 sr) is
  unchanged by M1-15, which replaced 179 lm/W with sunlight's 98.9225 lm/W
  after the radiance, not before it -- register decision 175.)*
  *(Amended 2026-09-29 by M1-17: its golden already kills one of the three --
  the shader that skips the exposure -- and its declaration is gone. **Two
  remain, the two clamps**, and they survived the golden because `clear`
  holds no negative light and nothing above AgX's white, so neither clamp
  changes a pixel of it. A port check at one well-exposed centre pixel will
  not see them either: to kill them it must also hold a value the clamps act
  on -- a negative channel, and a radiance above AgX's white.)*
  *(Corrected 2026-10-03, register decision 263: the reason holds for the
  first clamp and not for the second. That one acts on 868,950 values of
  `clear` -- 815,850 below 0, 53,100 above 1 -- and changes no pixel because
  the sRGB encode clamps again straight after it: measured on the RTX A2000,
  both pictures and the HDR dump bit-identical without it. It stays a
  declared survivor (decision 262); the first is the `tonemap-port` probe's.)*

## Error budget

**0.5 % of 129.97 W·m⁻²·sr⁻¹**, at 1 AU, albedo 0.3, normal incidence, measured
in the linear HDR target before exposure. The number, its derivation and the
0.05 % quantisation floor go in the test, the task and the commit message.

## Frames to look at

`lambert.png` — a flat grey field, and its value is the point rather than its
appearance. Worth one look to confirm the patch fills the frame and the
exposure produces something a person can actually see, since every later probe
inherits these settings.

## Done when

- [x] `check` green in both trees -- 356 of 356 in each, 2026-10-03.
- [x] The 0.5 % budget is asserted against an analytic value computed in the
      test -- at every pixel of every lambert frame (decision 259).
- [x] The fixture pattern works and is documented in the test file, because
      eight later probes copy it -- `tests/test_radiometry.cpp`'s opening, and
      `CMakeLists.txt` beside the probes, with the one trap found (below).
- [x] ~~`VERIFICATION.md` Part 4's rule 3 row can be updated from **to build** to
      partially done -- external truth now reaches the renderer as well as the
      physics.~~ *Not done, as ruled (decision 265)*: the row already read
      "done for what exists", and an analytic value is not data this project
      did not produce. Rule 4's row records the renderer's first budget instead.
- [x] The owner has looked at `lambert.png`, 2026-10-03: "lambert.png looks
      fine" -- an even grey field filling the frame, 157/255 at the default
      exposure.

## What was built

- **`src/view/Lambert.*`** -- `Albedo`, validated from 0 to 1 (decision 258);
  `squarePatch`, the 100 m square placed and tilted in world space (decisions
  269-270); `LambertScene`, everything a lambert probe fixes; and
  `toShaderLambert`, the fifth narrowing to 32 bits, with the 104-byte block
  read back from the compiled shaders by `spirv-cross --reflect`.
- **`src/view/Camera.*`** -- `toShaderMatrix`, the view-projection narrowed
  into the column order GLSL reads (decision 268), for M1-19 to reuse; and
  `view/Mat4.hpp`'s `ViewProjection`.
- **`src/view/ImageFiles.*`, `ImageCompare.*`** -- `decodePng16` and
  `Rgb16Image`, for the 16-bit port check (decision 260).
- **`src/view/ProbeSidecar.*`** -- the scene's light and surface in the
  sidecar (decision 264).
- **`shaders/lambert.vert`, `lambert.frag`, `probe_port.frag`** -- the patch
  through the camera, L = albedo E cos(theta) / pi in the fragment stage; and
  `tonemap-port`'s six bands of light the clamps act on (decision 261).
- **`src/render/Probes.*`** -- six new probes (decision 256); each probe's
  picture a `std::variant`, so a lambert probe without its light cannot be
  written, dispatched by one exhaustive `std::visit`; the lambert scene's
  vertex buffer through `toRenderSpace`, its irradiance from
  `solarIrradianceAt`, depth written and back faces culled (decisions 267 and
  271).
- **Tests**: `test_lambert` (8 cases), `test_radiometry` (5),
  `test_tonemap_port` (8), and new cases in `test_camera`, `test_image_files`
  and `test_probe_sidecar`; the probes as CTest fixtures
  (`cmake/RunProbe.cmake`, no golden: ADR 0008 keeps radiometry numeric).
- **`scripts/mutants/m1-18.json`**, 22 mutants; `m1-15.json`'s negative-light
  clamp re-pointed at the port check and expected caught, its other clamp's
  reason rewritten (decisions 261-263); one stale anchor in `m1-16.json`
  re-pointed after `clearConditions` moved a line.

## What was measured before it was relied on

- **Every lambert frame, every pixel**: 129.875 W/(m^2 sr) at 1 AU against
  129.966, 519.5 at 0.5 AU, 32.46875 at 2 AU and 64.9375 tilted -- each the
  binary16 value just below the exact radiance, 0.07 % low, as this GPU's
  rounding toward zero puts it. The quantisation floor is up to 0.1 %, not
  the 0.05 % this document and ADR 0014 first said.
- **The port check on all seven probes**, by a script independent of the
  suite: worst 0.56/255 on the 8-bit pictures and 0.59 steps on the 16-bit,
  against 1/255 and 2 steps; on `clear`, before the ruling, 0.56/255 on the RTX
  A2000 and 0.51/255 on the Intel UHD.
- **Both clamps, before the ruling**, with mutated shaders loaded from a
  scratch directory: the negative-light clamp missed by 195.7/255 on a frame
  with a negative channel; the clamp before the 2.2 power invisible on the RTX
  A2000 and 7 of 65,535 steps on the Intel UHD (decisions 261 and 262).
- **The geometry, before the ruling**: a 16:9 frame sees 36.4 degrees either
  side, so the 60-degree tilt is about the horizontal axis (decision 269).

## Other compilers

Run now rather than at the gate (decision 266).

- **gcc-14 found eight things**, all in this task's new code and all fixed in
  M1-16's shape: seven `std::array` initialisers it wanted with inner braces
  (`-Wmissing-braces`), two in `src/view/Lambert.cpp` and five in the tests, answered by `std::to_array` because clang-tidy's
  trailing-comma check contradicts the braces; and one lambda handed to an
  algorithm it wanted `noexcept`. `linux-gcc` then passes **316 of 316**, and
  `linux-sanitize` -- UndefinedBehaviorSanitizer and AddressSanitizer --
  **316 of 316**, on the final code.
- **MSVC found one**, the same missing braces under its own name (C5246),
  fixed the same way; it then builds the tree with no warning and passes
  **355 of 355** -- one fewer than clang, as since M1-16, having no `_Float16`.

## Found on the way

- **`uploadBuffer` did not make its copy visible to later commands**, and
  nothing had ever called it -- M1-107, committed first (decision 272).
- **`catch_discover_tests` splits a two-name fixture list.** The port-check
  suite was declared `FIXTURES_REQUIRED "probe_data;probe_clear_frame"`; the
  second name was read as a property name, and every property after it --
  the `gpu` label, the GPU lock -- was lost. The suite then ran before
  `probe_clear`, against whatever frame was left in `probes/`, and failed in
  the release tree while passing in Debug: exactly the disagreement two trees
  exist to show. `probe_clear` now sets up `probe_data` too, so each suite
  needs one fixture name, and `CMakeLists.txt` says why beside it.
- **A clang-tidy false positive, measured**:
  `cppcoreguidelines-pro-type-member-init` reports an aggregate holding a
  validated class after a member with a default, though such an aggregate
  cannot be default-constructed at all. Satisfied, not silenced, by giving
  `Albedo`'s private value a default as well.
- **The register's opening list of decision ranges** stops at M1-16; it was
  already behind before this task, and is left as found.


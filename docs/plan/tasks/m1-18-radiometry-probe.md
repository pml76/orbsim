# M1-18 — Numeric probes, and the radiometry budget

Phase: A | Status: not started
Prerequisites: M1-15, M1-16
Decided by: [ADR 0008](../../adr/0008-renderer-verification.md), [ADR 0014](../../adr/0014-radiometric-chain.md)

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

- [ ] `check` green in both trees.
- [ ] The 0.5 % budget is asserted against an analytic value computed in the
      test.
- [ ] The fixture pattern works and is documented in the test file, because
      eight later probes copy it.
- [ ] `VERIFICATION.md` Part 4's rule 3 row can be updated from **to build** to
      partially done — external truth now reaches the renderer as well as the
      physics.

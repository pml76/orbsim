# M1-18 — Numeric probes, and the radiometry budget

Phase: A | Status: not started
Prerequisites: M1-15, M1-16

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
  `0.3 × 1361 / π = 129.98 W·m⁻²·sr⁻¹` — from the constant and the definition,
  not from anything in `src/render/`.

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
- **The inverse-square law, on the GPU path**: the same patch at 0.5 AU and at
  2 AU reads back 4× and ¼× the radiance, to the same 0.5 %. A hard-coded
  irradiance passes the first test and fails this one.
- **Exposure does not touch the HDR readback**: the dump is identical for two
  different exposure settings, and only the PNG differs. That is the whole
  architecture of the chain, asserted once.
- **cos θ**: the patch tilted 60° from the Sun reads back half the radiance.
- **The tonemap port check** from M1-15: the PNG's centre pixel matches
  `view/Tonemap.hpp` applied to the HDR value, to 1/255.

## Error budget

**0.5 % of 129.98 W·m⁻²·sr⁻¹**, at 1 AU, albedo 0.3, normal incidence, measured
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

# M1-15 — Exposure and the AgX tonemap

Phase: A | Status: not started
Prerequisites: M1-14
Decided by: [ADR 0014](../../adr/0014-radiometric-chain.md)

## Purpose

M1-14 moved the plumbing; this gives it physical meaning. Space has the most
brutal dynamic range of any rendering domain — a sunlit cloud top and a star
field differ by more than ten orders of magnitude — and the reason spaceflight
photography looks the way it does is exposure choice. So the control is a
camera, not a brightness slider.

## What to implement

**`src/view/Exposure.hpp`** — pure, headless, testable:

- `Aperture` (f-number), `ShutterTime` (`Seconds`) and `Iso` as strong types.
  Three adjacent bare doubles here would be I.24 all over again.
- `[[nodiscard]] f64 exposureValue100(Aperture, ShutterTime, Iso)` and
  `[[nodiscard]] f64 exposureFactor(...)`, by the standard photographic
  relations, each formula cited in the comment.
- **The radiometric-to-photometric convention, stated explicitly**, because
  without it "exposure" is an arbitrary constant: the scene is rendered in
  radiance, W·m⁻²·sr⁻¹ per channel; luminance is obtained with a **luminous
  efficacy of 179 lm/W** applied to the luminance-weighted channel sum — the
  convention *Radiance* uses, cited by name. Every number downstream of exposure
  depends on this one line, so it is a paragraph in the header rather than a
  constant in a shader.

**`shaders/tonemap.frag`** gains the real chain: exposure multiply → AgX →
sRGB encode, in that order, once, at the end. AgX is implemented from the
MIT-licensed minimal implementation (Benjamin Wrensch, constants derived from
Troy Sobotka's OCIO configuration); the attribution goes in the shader header
and in `THIRD_PARTY.md`.

**`src/view/Tonemap.hpp`** mirrors AgX on the CPU with the same constants, for
the probe comparison in M1-18 and for the golden-image tooling.

## Out of scope

Auto-exposure — deferred, and when it lands it must be pinned in probe mode or
every golden image becomes a function of the previous frame. Bloom, glare, lens
effects. Any per-scene tuning constant: if a value has to be tweaked until it
looks right, the chain above it is wrong.

## Tests

`tests/test_exposure.cpp`.

- **Exposure value against hand-computed cases**: f/16 at 1/125 s and ISO 100
  gives EV100 = log₂(16² × 125) = 14.97, worked in the test rather than copied
  from the code. Two further cases at different ISO, one at a fractional stop.
- **The stop relation**: halving the shutter time raises EV by exactly 1, and
  doubling the f-number raises it by exactly 2, over a sweep. That is the
  property a wrong exponent breaks.
- **AgX endpoints and monotonicity**: 0 maps to 0; the transform is monotonic
  and strictly increasing over 12 orders of magnitude of input; a large input
  saturates rather than wrapping or producing NaN.
- **CPU and GPU agree.** The probe in M1-18 reads back the tonemapped image and
  compares it against `view/Tonemap.hpp` to 1/255. This is a **port check, not
  independent validation**, and the test says so in a comment: a display
  transform is a choice, not a physical claim, so what is being verified is that
  the same choice is implemented twice identically.
- **The sRGB encode** keeps its M1-14 tests.

## Error budget

The physical budget belongs to the radiance *before* exposure and is asserted in
M1-18 (0.5 %). Here: CPU and GPU tonemap agree to **1/255**, and the exposure
relations are exact to 1e-12 against hand-computed values.

## Verification

The standing rules. The frames that make this judgeable arrive one task later.

## Done when

- [ ] `check` green in both trees.
- [ ] The 179 lm/W convention is stated in the header and referenced from
      [ADR 0014](../../adr/0014-radiometric-chain.md).
- [ ] AgX carries its attribution in the shader and in `THIRD_PARTY.md`.
- [ ] No tuning constant exists anywhere in the chain.

# M1-15 — Exposure and the AgX tonemap

Phase: A | Status: **done, 2026-09-25**
Prerequisites: M1-14
Decided by: [ADR 0014](../../adr/0014-radiometric-chain.md)

> **KNOWN GAPS, declared in `scripts/mutants/m1-15.json`: nothing reads a pixel back before M1-16, so the shader's operations cannot be checked.** Accepted by the owner in advance on 2026-09-25 (register decision 182): a shader that skips the exposure, or either of the two clamps, is a valid program. **Its constants are checked**, by the CTest test `tonemap_constants`, against `view/Tonemap.hpp`. M1-18's port check is what kills the three operation survivors, and whoever closes it re-runs them and removes the declarations. **A fourth survivor was accepted by the owner on 2026-09-25** (register decision 185): removing the floor before `log2` changes nothing on the CPU, where `log2(0)` is minus infinity and the clamp that follows puts it back; the guard is there for GLSL.

**Twelve questions went up before any code was written and were ruled the
same day**: decisions 173-184 of the [register](../milestone-1-decisions.md).
The owner asked first how other realistic renderers -- pbrt, Filament,
Blender, Bruneton's atmosphere -- answer them, and the answers were read from
their source code rather than recalled. **Four rulings change what this
document says**, and are marked where they do: the efficacy (175), the output
encoding (173), the tests (180) and the clear colour (181).

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
  constant in a shader. *(Amended 2026-09-25, register decision 175: the
  efficacy is **sunlight's, 98.9225 lm/W**, with Rec. 709 weights. Radiance
  defines 179 for equal-energy white counted over the visible band only, and
  this renderer's radiance comes from the total solar irradiance, so 179 would
  have made every sunlit luminance 1.81 times too bright.)*

**`shaders/tonemap.frag`** gains the real chain: exposure multiply → AgX →
sRGB encode, in that order, once, at the end. *(Decision 173: AgX's output is
encoded for a 2.2 display, so it is undone with its 2.2 curve before the
IEC 61966-2-1 encode, as Filament and Wrensch do.)* AgX is implemented from the
MIT-licensed minimal implementation (Benjamin Wrensch, constants derived from
Troy Sobotka's OCIO configuration); the attribution goes in the shader header
and in `THIRD_PARTY.md`.

**`src/view/Tonemap.hpp`** mirrors AgX on the CPU with the same constants, for
the probe comparison in M1-18 and for the golden-image tooling.

**Settle the scene's clear colour** *(added 2026-09-24 by M1-14, register
decision 163)*. `src/view/SceneClear.hpp` clears the HDR target to the old
display colour decoded to linear light, so that M1-14 changed nothing
visible. Once exposure multiplies the target, that number is a tuning
constant of exactly the kind ADR 0014 forbids, and this task decides what
replaces it -- black, or a background radiance with a source.

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
  saturates rather than wrapping or producing NaN. *(Amended 2026-09-25,
  decision 180, after measuring: AgX's log range is 16.5 stops, so grey is
  strictly increasing only over the **4.88 decades** from 2.17e-4 to 16.3, and
  flat outside. Asserted as it is: strictly increasing inside, never
  decreasing across twelve decades, exactly black below 2.17e-4, and flat
  beyond the top. The stop relations are asserted to the budget's 1e-12,
  since "exactly" fails by one unit in the last place in 10-15 % of cases.)*
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

- [x] `check` green in both trees.
- [x] The ~~179 lm/W~~ convention -- sunlight's 98.9225 lm/W, decision 175 --
      is stated in the header and referenced from
      [ADR 0014](../../adr/0014-radiometric-chain.md).
- [x] AgX carries its attribution in the shader and in `THIRD_PARTY.md` -- the
      full MIT notice, in the shader and in `view/Tonemap.hpp`.
- [x] No tuning constant exists anywhere in the chain: the clear colour is
      zero radiance (decision 181) and the default exposure is the published
      "sunny 16" rule (decision 176).

## What was built

- **`src/core/Units.hpp`** -- `Radiance`, `Luminance` and `LuminousEfficacy`,
  so a radiance times an efficacy is a luminance and nothing else is. Built on
  mp-units' *angular* steradian: over the SI one, which has no dimension, an
  irradiance converted explicitly into a radiance -- measured compiling first.
- **`src/view/Exposure.hpp`** -- `Aperture`, `ShutterTime` and `Iso`, each
  validated at construction and refused by name (decision 178); EV100; the
  saturation-based exposure factor of ISO 12232 (decision 177); the paragraph
  on the conversion to luminance; and `toShaderExposure`, the second narrowing
  function in `src/` (decision 179).
- **`src/view/Tonemap.hpp`** -- AgX in double precision, with the MIT notice,
  the constants from Sobotka's `config.ocio` and the three guards of decision
  174. Returns linear display light, so `view/Srgb.hpp`'s encode follows by
  type.
- **`shaders/tonemap.frag`** -- exposure, AgX, the encode. The exposure is one
  float in a fragment-stage push constant, `view/PushConstants.hpp`'s
  `TonemapPushConstants`, read back from the compiled module with
  `spirv-cross --reflect`; `render/ResolvePass` takes it at creation and
  pushes it every draw.
- **`src/app/main.cpp`** -- exposes at f/16, 1/125 s, ISO 100 (decision 176).
- **`src/view/SceneClear.hpp`** -- zero radiance, with a `static_assert`
  holding it there (decision 181). `test_srgb`'s case for the old colour, and
  M1-14's mutant aimed at it, went with it.
- **Scripts**: `scripts/solar-efficacy.py` derives the efficacy from TSIS-1 and
  CIE 1924 V(lambda); `scripts/tonemap-reference.py` evaluates exposure and
  AgX in 50-digit arithmetic and checks the GLSL matrix layout against
  Sobotka's; `scripts/check-tonemap-constants.py` holds the shader's constants
  to the CPU's, as the CTest tests `tonemap_constants` and
  `tonemap_constants_self_test` (decision 182).
- **Tests**: `tests/test_exposure.cpp` and `tests/test_tonemap.cpp`.

## What was measured before the numbers were written down

| | |
|---|---|
| Sunlight's luminous efficacy | **98.9225 lm/W** -- 134 633.5 lx at 1 AU over 1361 W/m^2; the same to the digit through two independent tabulations of V(lambda) (CVRL's, and colour-science's); 0.30 % worst-case from the spectrum's stated uncertainty; ASTM E-490 gives 97.59 lm/W, 1.4 % lower |
| EV100 against the 50-digit reference | exact at all seven points; the stop relations within **3.55e-15** over a grid of 64,000 settings (two units in the last place of an EV near 16); budget 1e-12, the task's |
| AgX against the 50-digit reference | worst **5.22e-15**, at the bright end where the curve's terms cancel; budget 1e-14, twice that |
| The 6th-order curve against Sobotka's own | 5.8e-3 at worst, strictly increasing on [0, 1]; the 7th-order fit is not increasing everywhere |
| The two display encodings of decision 173 | up to 9 of 255 steps apart, in deep shadow; identical from mid-grey up |
| Mid-grey, 0.18 exposed | 0.2145 linear, display code **128** |
| A sunlit albedo-0.3 surface at sunny 16 | exposed to 0.335, **0.89 stops** above mid-grey -- it would have been 1.75 with 179 lm/W |

## The mutation pass

**Twenty-nine mutants in `build/debug`, run once: 25 caught, 4 survived as
declared, none invalid, none hung.** Eight of the 25 die at compile time: the
1.2 of ISO 12232 catches both of its factors, the curve's two ends catch a
swapped coefficient and a wrong Horner step, and the settings' refusals, the
efficacy and the clear colour each have an assertion beside them. Two render
mutants are caught by `orbsim_smoke`'s validation layers: a push-constant range
declared for the vertex stage, and an exposure that is never pushed. The
shader's constants -- a changed range, a transposed matrix -- are caught by
`tonemap_constants`. The four survivors are the known gaps at the top of this
document.

## What it costs

Measured with Vulkan timestamps against M1-14's last commit, and repeatable on
any machine with `scripts/measure-frame-cost.py scripts/measurements/m1-15.json`
(decision 184). [`../../measurements/m1-15-frame-cost.md`](../../measurements/m1-15-frame-cost.md)
has the method and the results; on this machine, medians of three:

| | |
|---|---|
| GPU frame before M1-15 | 69.81 us |
| GPU frame with M1-15 | 93.19 us -- **+23.4 us**, 0.14 % of a 60 Hz frame |
| of which the resolve pass | 41.69 -> 64.09 us, **+22.4 us**; 44.5 us per megapixel at 1600x900 |

A first attempt was stopped and discarded: the test suites were running at the
same time, and a timing taken under load looks exactly like one that was not.

## Other compilers

Run now rather than at M1-23's gate (decision 184).

- **gcc-14 found one thing**: two lambdas in `tests/test_tonemap.cpp`, handed
  to `std::ranges::all_of`, that it wanted declared `noexcept`
  (`-Werror=noexcept`) -- the finding it made on M1-13's header (decision
  172), in new code. Fixed in its own commit; `linux-gcc` then passes **239 of
  239**, everything it builds.
- **MSVC** builds the whole tree with no warning and passes **241 of 241**.
- Both Windows trees pass `check`, **241 of 241**.
- **A trap in the measuring, recorded so it is not walked into again**: a
  second gcc and MSVC run, started while the mutation pass was running, failed
  4 and 2 tests -- because `scripts/mutate.py` edits the shared working tree
  while it runs, and the other compilers built whatever mutant was in place.
  Run again with nothing else touching the tree, both passed everything, and
  the pass itself reported the same 25 caught and 4 declared survivors on its
  second run. A mutation pass owns the working tree for as long as it runs.

## Found on the way

**`src/view/Camera.cpp` was not linted.** The lint list named the core, test
and application sources, and not `ORBSIM_VIEW_SOURCES`, so the one `.cpp` file
of `orbsim_view` had never been through clang-tidy since M1-11 -- the shape
decision 156 closed for four test files. **Added on the owner's ruling
(decision 185)**, and its first run found two findings, both one missing
include of `view/Frame.hpp` for `kView` and `kWorld`, fixed in the code.

**What M1-15's report asked, and the answers** (decision 185, 2026-09-25): the
AgX licence confirmed on the blog page; the floor survivor accepted; the view
sources linted; and the assistant's choices within the rulings accepted.


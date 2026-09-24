# M1-12 — `Count<Derived>` and `RenderQuality`

Phase: A | Status: **done, 2026-09-23**
Prerequisites: M1-09
Decided by: [ADR 0001](../../adr/0001-units-in-the-type-system.md), [ADR 0007](../../adr/0007-render-quality-is-a-struct.md), [ADR 0012](../../adr/0012-orbsim-view.md)

**Twelve questions went up before any code was written and were ruled the same
day**: decisions 123-134 of the [register](../milestone-1-decisions.md). Two of
them reversed what this document or its sources said, and both reversals came
out of a measurement rather than an argument -- see "What measurement changed"
below.

## Purpose

ADR 0007 decided the shape of render quality and deliberately left two things
open: the numeric base for count-like units, and when the struct arrives. Both
are settled now — an integral sibling of `Quantity`, and here, empty.

**The point is the path, not the knobs.** Threading a settings value through a
renderer built for one fixed configuration is the same class of retrofit as the
HDR pipeline itself, and it is nearly free while there is one draw call.

## What to implement

- **`core/Scalar.hpp` gains `Count<Derived>`**, the integral sibling of
  `Quantity<Derived>`: one `std::uint32_t`, explicit construction, no implicit
  conversion either way, comparison, addition and subtraction of the same type,
  and multiplication by an unsigned scale. No division that could silently
  truncate — if a ratio of counts is wanted, it is a named function returning
  `f64`. The same CRTP shape and the same `friend Derived` trick, so
  `struct Other : Count<Texels>` does not compile.
- **`core/Units.hpp` gains `Pixels`** as a `Scalar<>` on its own mp-units
  *kind*, like `Eccentricity`, because a screen-space error threshold of 2.5 px
  is a real quantity rather than a count — and a kind of its own is what stops
  it converting into any other dimensionless ratio.

  **Corrected 2026-09-21.** This read "on the existing f64 `Quantity`", which
  is register decision 19 as it was written on 2026-09-08. The *reason* stands;
  the *mechanism* does not. [ADR 0019](../../adr/0019-vectors-carry-their-unit.md)
  moved every type in `core/Units.hpp` onto mp-units on 2026-09-17, and
  `Quantity<Derived>` in `core/Scalar.hpp` now carries only `Tolerance`, which
  takes no part in dimensional analysis. A `Pixels` on the f64 base would be
  the one dimensional unit in that header outside the dimension system.
  Whether a pixel *coordinate* wants a type of its own is a separate question
  and is not settled here — register decision 105 says the screen frame and
  the pixel unit the MFD needs are M1-80's to shape.
- **`src/view/RenderQuality.hpp`**: the struct, empty of fields for now, with
  `constexpr` factories `low()`, `medium()`, `high()` and `ultra()` that all
  currently return the same empty value — and a comment saying that is expected,
  because the fields arrive with the features (M1-46 and M1-59).
- **`Texels` and `Mebibytes`** as `Count` types, defined now so the first
  feature to need them adds a field rather than a type system.
- **The path**: the application owns one `RenderQuality`, passes it into the
  frame, and the renderer takes it by value per frame. Nothing reads it yet.
  A frame that cannot see the quality value is a frame that will be rewritten
  when it needs to.

## Out of scope

Any actual setting. `fromConfig` and any configuration file — deferred with
scenario serialisation. The adaptive controller. Reading a quality value
anywhere in the physics, which is impossible by construction and is the point of
ADR 0007.

## Tests

Extends `tests/test_view_math.cpp` or a new `tests/test_render_quality.cpp`.

- **Compile-time proofs**: `sizeof(Texels) == sizeof(std::uint32_t)`; trivially
  copyable; `!std::is_convertible_v<std::uint32_t, Texels>` and the reverse;
  `Texels` does not convert to `Mebibytes`; the presets are usable in a constant
  expression.
- **Arithmetic**: addition and subtraction stay in the type; a `Count` cannot be
  divided into another `Count` implicitly; unsigned wraparound at zero is
  reported by a precondition rather than silently producing four billion.
- **`RenderQuality` is an aggregate** with all members default-initialised, and
  copying it is trivial — asserted, not claimed, because ADR 0007 depends on
  cheap per-frame snapshots.

## The structural check

`orbsim_core` must not link `orbsim_view`. That is what makes "a physics
translation unit that reads a quality setting does not compile" true rather than
hoped for. The check is the `-DORBSIM_BUILD_APP=OFF` build from M1-09, which
builds the core and every physics suite without `orbsim_view` existing at all.

## What measurement changed

**The precondition this document asks for does not exist unless it is written
a particular way** (decision 124). "Unsigned wraparound at zero is reported by
a precondition rather than silently producing four billion" reads as though
`ORBSIM_EXPECTS` would do it. Measured on 2026-09-23, before any of this was
written: `assert` expands to nothing under `NDEBUG`, so in the RelWithDebInfo
tree `Texels{1} - Texels{2}` stays a perfectly good constant expression worth
**4,294,967,295**, and only the Debug tree refuses it. Written that way, the
claim in the Tests section below would have been true in one tree and false in
the other. What makes it unconditional is that the function called on the
wrapping branch is deliberately **not** `constexpr`, which takes the whole
expression out of constant evaluation whatever `NDEBUG` says. Confirmed on
three implementations in two configurations each -- clang 23.1, gcc-14 14.3.0,
MSVC 14.51, at `-O2 -DNDEBUG` and `-O0` -- with a positive control, and
measured to cost nothing: `mov`, `sub`, `ret`, the same three instructions an
unguarded subtraction emits.

**"Like `Eccentricity`" had stopped meaning one thing** (decision 126). This
document's `Pixels` clause says "on its own mp-units *kind*, like
`Eccentricity`" -- but `Eccentricity` stopped being a kind on 2026-09-22, the
day before this task, when [ADR 0022](../../adr/0022-a-bounded-scalar-validates-itself.md)
made it a class holding its own bound. The bound waits for a caller to say
which bound it wants; the *mechanism* did not survive the day.

**A kind does not do what this document chose it for** (decision 137), and the
second toolchain is what said so. A kind restricts **implicit** conversion and,
by design, permits explicit construction -- `explicitly_convertible(dimensionless,
kPixelKind)` is true on clang, gcc and MSVC alike -- so `Pixels{someRatio}` was
legitimate mp-units all along. It *looked* refused under clang and MSVC, and
that appearance was an accident of this project's own `Scalar<>`, which deletes
a conversion operator for every target type and so perturbs overload resolution
differently on each front end. Traced from a failing `linux-gcc` build to a
fourteen-line reproduction with no library in it. **The answer is the mechanism
mp-units uses for the angle**: a dimension of its own, which this project
already depends on for `Radians`. Every claim then holds by construction rather
than by a front end's opinion, and the unit algebra survives -- which matters,
because it type-checks M1-50's screen-space error end to end:
`Pixels / Metres * Metres` is `Pixels`. **The deleted operator is a separate
latent defect and is deliberately not fixed here**; it changes all nine types.

**One of this task's own assertions was vacuous, and the mutation pass is what
asked the question.** `Eccentricity` was `core/Units.hpp`'s other dimensionless
quantity, so `!is_constructible_v<Pixels, Eccentricity>` passes whether or not
`Pixels` has a kind -- a class is not constructible from a quantity either way.
The assertions that actually fail when the kind is removed compare `Pixels`
against `Scalar<one>` and against the ratio of two lengths, and they were added
before the pass ran. A second hole went the same way: nothing pinned
`kCountMaximum` to the type's real maximum, since every guard and every proof
of a guard reads that one constant and they all move together. It is now
checked against the language's own wrapping rule, which is a fact the constant
cannot supply about itself.

**The sweeps are a covering table rather than a random draw, and there is no
seed.** `tests/test_projection.cpp`'s monotonicity case and the whole of
`tests/test_sun.cpp` already make that choice for the same reason, and the
reason is stronger for whole numbers: integer arithmetic goes wrong at the
ends, at the powers of two and at zero, and a uniform draw over the 32-bit
range reaches the last few thousand values with a probability
indistinguishable from zero. The probes are those places, crossed with
themselves. *(This also removed the only reason the suite would have needed a
`NOLINT`, which is not why it was done but is worth recording: the house
pattern for a seeded engine is a suppression at the site, and there are eight
of them in `tests/`.)*

## Done when

- [x] `check` green in both trees. **200 tests, 0 failed, in each**, up from
      189 -- ten new Catch2 cases and one probe. `check` builds the lint, the
      format check, the document-link check and the mutation anchors, so those
      are green too. **All six toolchains re-run** rather than left to the
      phase gate: 200 under `relwithdebinfo`, `debug`, `asan` and
      `windows-msvc`, 199 under `linux-sanitize` and `linux-gcc`, none failing,
      and no report from AddressSanitizer or UndefinedBehaviorSanitizer.
- [x] ADR 0007's open question is closed, and ADR 0001 carries the dated note
      about `Count` (both written in M1-02, updated here if the shape changed).
      Both notes also correct the 2026-09-08 claim that `Pixels` stays on the
      `f64` base, whose *reason* stands and whose *mechanism* stopped being
      true on 2026-09-17.
- [x] A `RenderQuality` value reaches the frame code, and nothing reads it.
      The application owns one, names `high()` in code, and assigns it into
      each `FrameContext` (decisions 127 and 128).
- [x] `-DORBSIM_BUILD_APP=OFF` still builds core and its tests: **424 targets,
      198 CTest entries, all passing**, and `orbsim_core`'s own link line in
      the generated build file names no render-side library. The tree was
      deleted afterwards rather than kept, per `PROJECT_STATE.md` section 8.
- [x] `orbsim_render_deps` links `orbsim_view`, which is the **first time a
      shipping target does** -- until this task only three test suites did, so
      the camera and the projection had existed for three days without the
      application being able to see them.
- [x] The run-time half of the guard has tests of its own (decision 136):
      **three of them**, one per guarded operation, which **pass in `debug` and
      are reported skipped in `relwithdebinfo`** rather than passing there
      without checking anything. Both trees list 202 CTest entries now.

      **There was one of them for a day, and one was not enough.** It covered
      the subtraction; decision 125 guards three operations. Deleting the
      run-time guard from `operator+` then passed the *entire* `check` -- 200
      tests, exit 0, measured -- because nothing anywhere provoked an addition
      that overflowed. Each operation now has a probe of its own, because a
      program can only abort once, and the three were measured to be
      independent: deleting each guard in turn fails **exactly** its own entry
      and no other, and a healthy tree passes all three.
- [x] The mutation pass, **run twice, and the second time in two trees**:
      nineteen mutants, **18 caught, 1 declared survivor, none invalid, none
      hung**. In `build/debug`, 17 mutants and 16 caught -- 13 at compile time
      naming the assertion that fired, 3 by the suite; in
      `build/relwithdebinfo`, the 2 that only that tree can decide, both
      caught.

      **First run, 2026-09-23: 15 caught, 3 survived, 1 invalid**, and all
      three findings were in the *pass* rather than in the code -- as M1-11's
      and M1-87's first runs also were.

      **Two survivors were undeclared, and they are the useful finding: the
      guard's two halves mask each other, in opposite trees.** `ORBSIM_EXPECTS`
      and the call to the non-`constexpr` marker sit side by side, and *in a
      Debug build the assertion does the marker's job as well* -- a failing
      assert is itself not a constant expression, so a bad literal is refused
      whether or not the marker is there. Marking the marker `constexpr`, and
      deleting the call outright, therefore changed nothing this tree could
      see. The mirror image holds in `relwithdebinfo`, where the assertion is
      gone and the run-time mutants survive instead. **Neither tree tests this
      code on its own**, which the pass discovered and no amount of reading
      would have. The mutants are split accordingly:
      [`m1-12.json`](../../../scripts/mutants/m1-12.json) against
      `build/debug` and
      [`m1-12-release.json`](../../../scripts/mutants/m1-12-release.json)
      against `build/relwithdebinfo`.

      **One mutant was invalid**, and in the mutant rather than the code:
      `derived() = derived();` left the parameter unused, which
      `-Wunused-parameter` reports and ADR 0017 makes an error. A mutant that
      does not compile proves nothing; it subtracts now instead.

      **One declared survivor stands**, for the reason its `why` field gives:
      the property is checked by `count_wraparound_aborts`, which this harness
      structurally cannot run, because that probe's success is a non-zero exit.

      **That claim was false for a day, and only a direct question found it.**
      It was written from reasoning rather than from a run. Measured 2026-09-24
      by applying the survivor to the tree by hand: the probe detected the
      wraparound correctly and returned **1**, and
      `cmake/VerifyCountWraparound.cmake` failed only on exit **0** -- so the
      test whose entire purpose is to close this survivor reported **green
      against a tree carrying it**. The script reads the probe's own verdict
      now, and all three outcomes were run rather than argued: a healthy Debug
      tree passes, the release tree skips, and a tree carrying the mutant
      fails. A check that has silently stopped checking looks exactly like one
      that passes (`VERIFICATION.md` rule 23), and this one looked like it from
      the moment it was written.

      **And the pass hung the machine before it ran clean**, twice in one run,
      which produced the other two changes. A mutant that trips an assertion
      inside a Catch2 case calls abort, and the Windows debug runtime turns
      abort's own message into a modal dialog: the suite sat there alive and
      the pass sat there with it. `tests/count_wraparound_probe.cpp` had
      carried the cure since the day before and nothing else did, so
      **`tests/AbortBehaviour.cpp` now compiles into every suite** -- compiled
      in rather than linked from `orbsim_test_support`, because a Catch2
      listener in a static library is dropped unless something references it
      and `test_render_quality` references none of the support code. And
      **`mutate.py` no longer waits forever**: a suite or a build that exceeds
      its limit is reported HUNG, which is neither a kill nor a survivor, and
      fails the run. Verified by hand against the exact mutant that hung,
      before the pass was let near it again.

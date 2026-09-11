# M1-05 — TDB and UT1

Phase: A | Status: not started
Prerequisites: M1-04, M1-06 — which now runs first (decision 30), because the
reference values below arrive through its fixture reader
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md), [ADR 0016](../../adr/0016-the-astronomy-is-erfa.md)

**Amended 2026-09-11** (decisions 27–29): TDB − TT is ERFA's `eraDtdb`
rather than a two-term approximation, and this task — the first to call ERFA —
is where ERFA is pinned and `src/astro/` begins. Each changed bullet says so.

## Purpose

The last two scales, and the two that are not offsets. TDB is what solar-system
ephemerides are tabulated in; it differs from TT by a periodic term of about
1.7 ms amplitude, driven by Earth's motion in the Sun's gravity well. UT1
measures the Earth's actual rotation and wanders against UTC by up to 0.9 s,
which is what makes it the scale the body-fixed frame in M1-07 needs.

## What to implement

- *(Added.)* **ERFA, pinned** ([ADR 0016](../../adr/0016-the-astronomy-is-erfa.md)):
  `FetchContent_Declare` at the exact latest release tag, re-verified on the day
  (v2.0.1 on 2026-09-10), `SYSTEM`, built unedited as a static C library from
  all 249 library files, with `orbsim_fp` and `orbsim_sanitizers`, and linked
  privately to `orbsim_core`. ERFA has no CMake build of its own; the version
  macros its autotools build would generate are set from the tag. Its
  validation program `t_erfa_c` becomes a CTest test, so `check` proves the
  library was built right by each toolchain. ERFA's row in
  [`THIRD_PARTY.md`](../../../THIRD_PARTY.md) moves to "Pinned and compiled".
- *(Moved here from M1-07.)* **`src/astro/` begins**: add it to
  `ORBSIM_CORE_SOURCES`, to the header self-check list, to the lint list, and to
  the directory map in `CLAUDE.md`. ERFA's headers are included from
  `src/astro/*.cpp` and nowhere else — never from a header — so nothing above
  `src/astro/` sees C, and `src/core/` still depends on nothing.
- **TDB − TT** *(amended)*, by ERFA's `eraDtdb`, in `src/astro/` because it calls
  ERFA. Evaluated at the geocentre: the observer's `u` and `v` are zero, so the
  diurnal and lunar terms that depend on where on the Earth's surface one
  stands — up to 2 µs, `eraDtdb` note 4 — vanish, which is right for a
  simulation whose origin is the Earth's centre, and the header says so. The
  wrapper builds ERFA's two-part date from the exact day and picoseconds —
  note 1's "MJD method" — and applies the result through `TimePoint`'s own
  arithmetic. Both directions: `tdbFromTt`, `ttFromTdb`. The inverse evaluates
  the series at TDB rather than TT, which note 1 says has "no practical effect":
  the argument moves by at most 1.7 ms, over which the term changes by about
  1e-12 s — and the comment says that rather than leaving a reader to wonder.
  *(Was: the Explanatory Supplement's two-term approximation.)*
- **UT1** as `ut1FromUtc(UtcTime, DeltaUt1)` and back, with a `DeltaUt1` strong
  type in seconds, in `core/Time.hpp` — it needs no ERFA. **The default is
  zero**, and the header states the resulting model error plainly: ΔUT1 stays
  inside ±0.9 s by construction, which is ≤ 13.5″ of Earth rotation, about 420 m
  at the equator. The parameter exists so that an IERS series can be supplied
  later without changing a signature.
- `describe()` gains any new `TimeError` values.

## Out of scope

Reading IERS EOP files. Relativistic scale factors between TDB and TCB.
Nutation, which is M1-07's. *(Amended: the full Fairhead–Bretagnon series was
out of scope; it is what `eraDtdb` is.)*

## Tests

Extends `tests/test_time.cpp`, or a new `tests/test_astro_time.cpp` if the
TDB conversions live apart from `core/Time.hpp`.

- **TDB − TT against reference values** from an implementation this project did
  not write, committed through the M1-06 fixture mechanism with its provenance
  recorded. Assert **≤ 100 µs**. *(Amended:)* **not ERFA**, which now computes
  it — agreement between ERFA and ERFA proves nothing. Candidates are NOVAS 3.1
  (US Naval Observatory) and Skyfield; verify the terms of the one chosen and
  that it was written independently of SOFA, and record both. If it supports a
  tighter budget, put the tighter number to the owner before asserting it. If
  no independent reference can be obtained, **stop and raise it** rather than
  falling back on ERFA's claimed accuracy: `VERIFICATION.md` rule 3 exists for
  exactly this substitution.
- **Amplitude and period**: the term's peak magnitude is about 1.7 ms and its
  dominant period is one year. Sampled across two years, the extrema and the
  zero crossings land where the physics says, which is a check the fixture
  cannot give and a wrong sign would fail.
- **Round trip** TT → TDB → TT to 1e-9 s over a seeded sweep, 1990–2050.
- **UT1**: with ΔUT1 = 0, UT1 equals UTC exactly; with ΔUT1 = 0.3 s it differs
  by exactly that; the sign convention is stated in a comment and asserted
  against it (UT1 = UTC + ΔUT1).
- **The type system**: TDB and TT still do not interconvert implicitly.
- *(Added:)* **ERFA's own validation**, `t_erfa_c`, passes in both trees and
  under both Linux presets.

## Error budget

TDB − TT within **100 µs** of the independent reference — justified because
100 µs is 3 m of Earth's orbital motion, far below anything this milestone
claims. *(Added:)* the implementation claims better than **3 ns** against a time
ephemeris integrated on DE405 over 1950–2050 (`eraDtdb` note 7); that is
recorded, and not asserted, because no reference in hand can check it.
ΔUT1 = 0 is a **model error of ≤ 0.9 s**, recorded, not asserted.

## Verification

The standing rules, plus the two Linux presets, because a new dependency is
exactly where a second compiler disagrees first.

## Done when

- [ ] `check` green in both trees, `t_erfa_c` among the tests.
- [ ] The 100 µs budget is asserted against an external reference, not a claim,
      and not ERFA.
- [ ] The ΔUT1 = 0 model error appears in the header, in the ADR, and in the
      commit message.
- [ ] All five scales exist, and none converts to another by accident.
- [ ] ERFA is pinned, its `THIRD_PARTY.md` row says so, and `src/astro/` is in
      the directory map.

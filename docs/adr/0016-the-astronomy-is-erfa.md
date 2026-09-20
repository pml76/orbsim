# ADR 0016: The astronomy is ERFA's, called through typed wrappers

Status: accepted (2026-09-11)

Decisions 27 to 29 of [the milestone 1 register](../plan/milestone-1-decisions.md).
**Partly supersedes [0009](0009-time-is-a-type-with-a-scale.md)**: its "no new
link dependency" for `src/astro/`, its pairing of IAU 2006 precession with the
Earth rotation angle -- which was inconsistent, see below -- and its deferral of
nutation. The rest of 0009 stands.

## Decision

- **ERFA computes the astronomy.** ERFA is the BSD-3-Clause edition of the
  IAU's SOFA library, copyright the NumFOCUS Foundation, derived with SOFA's
  permission ([`../../THIRD_PARTY.md`](../../THIRD_PARTY.md)). It supplies:
  - **TDB - TT**, `eraDtdb`: the Fairhead-Bretagnon series, which its notes put
    within **3 ns** of a time ephemeris integrated on DE405 over 1950-2050;
  - **the rotation from the celestial to the terrestrial frame**, `eraC2t06a`:
    IAU 2006 precession and IAU 2000A nutation in the CIO-based form, with the
    Earth rotation angle, and polar motion passed as zero;
  - **the Sun**, `eraEpv00`: the Earth's heliocentric position, oriented to the
    BCRS, within **11.2 km** of DE405 over 1900-2100 by its own comparison --
    0.016" of solar direction.
- **Fetched and pinned** at an exact release tag, exactly as every other
  dependency is, and **built unedited** as its own static C library with the
  project's floating-point settings. Its own validation program, `t_erfa_c`,
  runs as a CTest test, so `check` proves the library was built right by each
  toolchain rather than assuming it. The pin lands with
  [M1-05](../plan/tasks/m1-05-tdb-and-ut1.md), the first task that calls it.
- **Called only from `src/astro/`, through thin typed wrappers.** A
  `TimePoint` goes in, as the two-part Julian date ERFA expects, built from the
  exact day and picoseconds; strong types and `Quat` come out; every status code
  ERFA returns is mapped to an error with a name, as `vkCheck` does for a
  `VkResult`. `orbsim_core` links ERFA privately, ERFA's headers are included
  nowhere else, and `src/core/` still depends on nothing.
- **Not for the time scales' own arithmetic.** UTC, TAI and TT
  ([M1-04](../plan/tasks/m1-04-leap-seconds.md)) stay exact integer-picosecond
  arithmetic in `core/Time.hpp`. ERFA works in doubles, about 10 ps, and its
  `eraDat` extrapolates past its own table -- in silence to 2028, and with a
  warning after -- which 0009 rules out; replacing its table means
  `eraSetLeapSeconds`, process-wide mutable state.
- **Nutation is modelled.** It was deferred because IAU 2000A is 1,365 terms to
  transcribe; now it is one call. The model error of the body-fixed frame falls
  from **<= 40"** to **<= 14.1", about 440 m** on the ground: dUT1 = 0, which is
  <= 13.5" because the IERS keeps |UT1 - UTC| under 0.9 s, and polar motion
  omitted, <= 0.6" -- the largest pole excursion in the IERS EOP 20 C04 series,
  18.6 m, in 1996.
- **ERFA is no longer a reference for what it computes.** Agreement between
  ERFA and ERFA proves nothing ([`../VERIFICATION.md`](../VERIFICATION.md) rule
  23), so each task checks its use of ERFA -- the scale passed, the units, the
  order of the rotations -- against something independent: JPL Horizons for the
  Sun, and for the frame and TDB a second implementation of the same IAU models
  written independently of SOFA. NOVAS 3.1 (US Naval Observatory) and Skyfield
  are the candidates; each task verifies its reference's terms and independence
  before relying on it, and if none can be had, **stops and raises it**.

## A correction this record carries

0009 and [M1-07](../plan/tasks/m1-07-earth-orientation.md) paired IAU 2006
precession in the Fukushima-Williams form -- an equinox-based matrix -- with the
Earth rotation angle, which is measured from the celestial intermediate origin.
The two do not compose. Measured with ERFA on 2026-09-11, the pairing is wrong
by **1231", 0.342°, about 38 km at the equator** -- the equation of the
origins -- and the error grows by about 46" a year. The angle that pairs with an
equinox-based matrix is Greenwich sidereal time; the one that pairs with ERA is
the CIO-based matrix. Those two correct forms agree to 13 µas, and `eraC2t06a`
is the second.

The same plan checked the rotation rate against **86 164.0905 s**, the
sidereal day. That is the period of sidereal time, which follows the precessing
equinox. ERA turns once in the **stellar day, 86 164.098 903 691 s** (IERS
useful constants), and a correct implementation would have failed that test by
8.4 ms against a tolerance of 0.1 ms. The likeliest "fix" would have been to
break the code until it passed.

Neither was caught in review; both were caught by computing the answer.

## What we considered

**Implementing the series ourselves**, which was the plan: the 1,365 nutation
terms, the 787 Fairhead-Bretagnon terms and a low-precision solar formula, each
transcribed by hand and each a place for a digit to go wrong unnoticed. Two
errors were already in the plan before a line was written.

**ERFA as a reference only**: run outside the build to generate fixtures, as
GMAT is. It keeps ERFA independent, and ships a less accurate model than the
IAU's own reference implementation, which is BSD-licensed and builds clean here.

**Copying ERFA into the repository verbatim.** 57,787 lines of C under lint
rules written for this project's C++23, so a directory exempt from them -- a
lint decision -- and the end of [`../../THIRD_PARTY.md`](../../THIRD_PARTY.md)'s
rule that nothing is vendored. A pinned fetch gives the same reproducibility.

**Porting ERFA into the house style.** A fork: its 1,494-check validation and
its equivalence to SOFA stop applying to the copy, and the transcription risk
this decision exists to remove comes back.

**SOFA itself.** Its licence adds conditions on derived work -- a statement of
derivation, a description of the differences in the source, no routine named
`iau` or `sofa`. ERFA exists to avoid them.

## Why

[`../VERIFICATION.md`](../VERIFICATION.md) rule 24, prefer the bug you cannot
write, applied to astronomy: code nobody transcribes cannot be mistranscribed.
And working agreement 3, accuracy first: 3 ns where the plan had a 100 µs
budget for TDB - TT, 0.016" where it had 0.01° for the Sun, and nutation where
it had none.

Measured before this was written, not assumed: ERFA 2.0.1 compiles all 249
library files with **zero warnings** under `-Wall -Wextra -Wpedantic
-Wconversion -Wshadow -Wdouble-promotion` on Windows clang 23.1.0, WSL clang
23.1.1 and gcc-14, and its validation passes on all three -- 1,494 checks.

## What this record does not decide

- **The ephemeris beyond the Sun.** DE440 against VSOP87/ELP2000 is still open
  ([`../plan/realism.md`](../plan/realism.md) section 4). ERFA's `eraEpv00`
  covers the Earth and Sun only.
- **dUT1 and polar motion from IERS data.** Both stay zero, with their sizes
  written down; the UT1 conversion already takes dUT1 as a parameter.
- **Which independent implementation each task checks against.** M1-05 and
  M1-07 each settle theirs, and record its terms, before relying on it.
- **What the Sun wrapper does outside 1900-2100**, where `eraEpv00` warns that
  its stated accuracy lapses. That is [M1-08](../plan/tasks/m1-08-solar-position.md)'s
  question, put to the owner before that task starts.

## Update, 2026-09-11: the Sun outside ERFA's span is reported by name

Answered the same day, as decision 31 of the register: the Sun wrapper
reports a date outside `eraEpv00`'s span as `OutsideEphemerisRange`, taking the
boundary from ERFA's own status, and both of its functions return a
`std::expected`. Measured with ERFA, the span is **100 Julian years either side
of J2000**, JD 2415020.0 to 2488070.0 TDB -- 1899-12-31T12:00 to
2100-01-01T12:00, both ends inside -- so "1900-2100" ends on 1 January 2100.
ERFA resolves that boundary only to its arithmetic's resolution, about 0.63 µs
at 36 525 days from J2000: a date a picosecond outside reports as inside, and
a millisecond outside does not.

## Update, 2026-09-19: the pin, and the first reference

ERFA is pinned, with [M1-05](../plan/tasks/m1-05-tdb-and-ut1.md), on the
owner's rulings of the same day (decisions 53 to 67 of
[the register](../plan/milestone-1-decisions.md)). **The decision stands**; what
this adds is how it was carried out, and what was measured first.

- **v2.0.1**, re-verified as the latest release on the day. Master was twelve
  commits ahead and unreleased; the one touching code makes the leap-second
  table thread-safe, and this project does not call ERFA's leap-second
  functions.
- **All 249 library files, found rather than listed**: the build globs ERFA's
  sources, drops its two validation programs, and stops unless exactly 249
  remain. Its version macros are read from its own `meson.build`, not
  transcribed. Its include directory is `SYSTEM`, which the lint header filter
  needed: that filter matches any path containing `/src/`, and ERFA's headers
  have one.
- **Both of upstream's validation programs** run in `check`, where this record
  named one: `t_erfa_c`, 1,494 checks, and `t_erfa_c_extra`, which is the only
  thing that exercises two of the 249 files. Measured before the pin: zero
  warnings and both passing under clang 23.1.0, clang 23.1.1 with ASan and
  UBSan, gcc-14 at -O0 and -O2, and MSVC 19.51.
- **The independent reference for TDB - TT is Skyfield 1.55**, whose
  `tdb_minus_tt` is USNO Circular 179 eq. 2.6, written independently of SOFA.
  NOVAS 3.1 evaluates the same equation, so it adds no independence, and its
  terms could not be confirmed on the day. One thing found then matters for
  [M1-07](../plan/tasks/m1-07-earth-orientation.md) and is recorded there: the
  NOVAS C3.1 user's guide says its IAU 2000A nutation and its equation of the
  equinoxes are built on the same IERS modules as SOFA's, so for the nutation
  its independence from ERFA is partial -- which M1-07 has to weigh when it
  chooses its reference, not something decided here.

## Update, 2026-09-20: the frame, its reference, and its budget

Written with [M1-07](../plan/tasks/m1-07-earth-orientation.md), on the owner's
rulings of 2026-09-19 -- decisions 75 to 83 of
[the register](../plan/milestone-1-decisions.md), all put before the code.
**The decision stands**: ERFA computes the rotation, through `eraC2t06a` and
`eraC2i06a`, and `src/astro/EarthOrientation.hpp` is the typed wrapper. What
this adds is the independent reference this record asked each task to choose,
and what measuring it changed.

- **The reference is Skyfield 1.55**, and its independence has a stated limit.
  Its route is the other correct one -- Greenwich apparent sidereal time
  applied to the equinox-based bias-precession-nutation matrix, where ERFA's
  is CIO-based -- with its own frame bias, precession, sidereal-time
  polynomial and Earth rotation angle. Its IAU 2000A nutation, though, is a
  port of NOVAS's, which NOVAS's own guide ties to the same IERS modules as
  SOFA's. The coefficient table *is* the model and every implementation shares
  it; what an independent route checks is the *use* of the model -- the scale
  passed, the units, the composition, the direction -- and every misuse of
  that kind was measured failing the budget.
- **The budget is 0.1 mas over 1900-2100**, where this record's table carried
  0.1" over 2000-2050. Measured first: the two agree to **53.6 µas** on the
  committed fixture, of which about 47 µas is the TIO locator s'. At 0.1" the
  test could not have seen an omitted frame bias (23.1 mas), IAU 2000B
  nutation (3.1 mas), IAU 2000 precession (2.7 mas), or UT1 wrong by a
  millisecond (15 mas). At 0.1 mas all of them fail it.
- **`eraC2t06a` applies s' even when polar motion is zero**, which is worth
  knowing before composing anything by hand: `Rz(ERA) x eraC2i06a` is *not*
  the same matrix, measured bit-identical in **0 of 200,000** epochs. So the
  wrapper calls `eraC2t06a` for the whole rotation rather than composing it,
  and the suite's composition test allows s' and asserts that what is left is
  a spin rather than a tilt.
- **A rotation that needs no UT1 was added**, `intermediateFromInertial`, from
  `eraC2i06a`: M1-63's J2 term runs inside an integrator clocked on TT and
  M1-08's equinox test needs the equator of date, and neither depends on the
  Earth's rotation. Its pole is bit-for-bit the pole of the full rotation, on
  200,000 of 200,000 epochs.
- **ERFA's date goes in as the day and the fraction of the day.** Measured
  against a 60-digit evaluation of the defining formula: the Earth rotation
  angle comes out within 0.04 µas that way, and within 9.5 µas with the "MJD
  method" `astro/Tdb.cpp` uses -- where it cannot matter, since TDB - TT moves
  by microseconds a day and ERA by a whole turn.

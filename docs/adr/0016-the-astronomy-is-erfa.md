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

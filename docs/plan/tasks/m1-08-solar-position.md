# M1-08 — Solar position

Phase: A | Status: **done, 2026-09-20**
Prerequisites: M1-05, M1-06, M1-07
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md), [ADR 0016](../../adr/0016-the-astronomy-is-erfa.md)

**Amended 2026-09-11** (decisions 27–29): the Sun comes from ERFA's
`eraEpv00` rather than the *Astronomical Almanac*'s low-precision formula, and
the budgets tighten from 0.01° and 2e-4 AU to 0.1″ and 1e-6 AU. A date
outside the span ERFA vouches for is **reported by name** (decision 31, ruled
the same day).

**Amended 2026-09-20** (decisions 85–90), after nine questions were put to the
owner and every number below was measured before it was written down:

- **the budgets tighten again, to 0.02″ and 5e-8 AU** (decision 85), about
  twice the measured worst rather than sixfold headroom over ERFA's claim;
- **the equinox test gains a second, sharper claim** and runs at three
  published equinoxes rather than one (decision 86);
- **the apsis test sweeps all fifty years** and the continuity bound becomes
  two-sided and derived from a law (decision 87);
- **the solar constant carries Kopp & Lean (2011)** and names the solar cycle
  as unmodelled (decision 88);
- **`TwoPartDate` is deleted**: ERFA is handed `core/Time.hpp`'s `JulianDate`
  directly, here and in `astro/EarthOrientation.cpp` (decision 89);
- the error type, the AU constant's home and `sunDistance`'s single series
  evaluation go as proposed (decision 90).

**The span check was verified against ERFA before any of this was written**:
`eraEpv00` returns 0 at JD 2415020.0 and at 2488070.0, 0 a millisecond inside
either, and 1 a millisecond outside either. Its rule is
`|((date1 − 2451545.0) + date2)/365.25| ≤ 100`, so both ends are inclusive, as
the text below says.

## Purpose

Everything about the image depends on where the Sun is: the terminator, the
limb, the length of the shadows, and — through the inverse-square law — how much
light there is to expose for. This is the one piece of ephemeris milestone 1
needs.

**It lights the scene; it does not pull on anything.** The force model in phase E
is Earth point mass and J2 only.

## What to implement

`src/astro/Sun.hpp` / `.cpp`.

- **The Earth's heliocentric position from ERFA's `eraEpv00`**, negated to give
  the geocentric Sun *(amended)*. It is oriented to the BCRS, which is aligned
  with ICRS, so no rotation follows. Its notes compare it with DE405 over
  1900–2100 at 3.7 km RMS and 11.2 km at worst — 0.016″ of solar direction —
  and the comment quotes them. *(Was: the low-precision formula, about 0.01°,
  referred to the equinox of date and rotated into ICRF through M1-07's
  precession — the step that is no longer needed.)*
- **Geometric, and the header says so** *(added)*: where the Sun is at the
  instant, with neither light-time nor aberration applied. Aberration alone
  moves the apparent Sun by 20.5″ — two hundred times this task's budget, and
  invisible on screen, where the Sun's disc is 0.53° across. The lighting uses
  the geometric Sun.
- `geocentricSunPosition(TdbTime)` in **metres**, ICRF, and
  `sunDistance(TdbTime)`, both `[[nodiscard]]` and both returning a
  `std::expected`, because of the next point.
- **Outside the span ERFA vouches for, reported by name** *(decided
  2026-09-11, decision 31)*. `eraEpv00` returns a warning status more than 100
  Julian years from J2000 — outside JD 2415020.0 to 2488070.0 TDB,
  **1899-12-31T12:00 to 2100-01-01T12:00**, both ends inside — because its
  stated accuracy lapses there; its errors roughly double by 1800 and 2200. The
  wrapper reports that status as an error with a name, `OutsideEphemerisRange`,
  and takes the boundary from ERFA's own status rather than from a second copy
  of the rule — so the boundary is ERFA's to the resolution of its arithmetic,
  about a microsecond, which the tests respect. The same stance as the
  leap-second table: it says so rather than quietly answering less well. Widening it later, with a budget stated for the
  wider span, is a decision to take when a scenario needs it. Note that "1900–
  2100", as the notes put it, ends on **1 January** 2100.
- `Irradiance` (W·m⁻²) joins `core/Units.hpp`, and
  `[[nodiscard]] Irradiance solarIrradianceAt(Metres distance)` implements the
  inverse-square law from the total solar irradiance at 1 AU. The constant is
  **1361 W·m⁻²** and the comment cites the measurement it comes from rather than
  calling it well known — **Kopp & Lean (2011)**, *Geophys. Res. Lett.* **38**,
  L01706, doi:10.1029/2010GL045777, 1360.8 ± 0.5 W·m⁻² at the 2008 solar
  minimum from SORCE/TIM *(decision 88)*. The rounding to 1361 is 0.015%, inside
  the measurement's own ±0.037%, and the ~0.1% solar-cycle variation —
  1.3 W·m⁻², a fifth of M1-18's radiometric budget — is named as unmodelled.
  The function is `constexpr`, with `static_assert`s proving it.
- *(Added 2026-09-20, decision 90:)* `OutsideEphemerisRange` is the one value of
  a new `enum class EphemerisError` in `astro/Sun.hpp`, following `OrbitError`'s
  precedent rather than a layer-wide `AstroError`. `kAstronomicalUnit` lives
  there too, where the suite reaches it without ERFA's header, with a
  `static_assert` in `Sun.cpp` that it is bit-identical to `ERFA_DAU`. And
  `sunDistance` is `length(geocentricSunPosition(...))` — one series
  evaluation, exactly consistent with the position.
- *(Added 2026-09-20, decision 89:)* **`astro/EarthOrientation.cpp`'s
  `TwoPartDate` is deleted** and `core/Time.hpp`'s `JulianDate` is handed to
  ERFA directly, at its call sites as well as at this task's. The two structs
  held the same two fields and `twoPartDate(at)` returned `at.julianDate()`
  field for field. Decision 83's measurement of the split moves onto
  `JulianDate`; `astro/Tdb.cpp`, which wants ERFA's MJD split, is untouched.
  This is the one change M1-08 makes outside its own files.

## Out of scope

The Moon. Planets — ERFA's `eraPlan94` exists, and DE440 against VSOP87 is
still open. Light-time and aberration, deliberately, as above. Anything
gravitational.

## Tests

`tests/test_sun.cpp`, against the M1-06 Horizons fixture — geometric vectors,
with no light-time or aberration correction, or the comparison measures the
correction instead of the code.

- **Direction within 0.02″** and **distance within 5e-8 AU** *(amended
  2026-09-20, decision 85)* at all 40 fixture epochs across 2000–2050. This is
  the budget, and Horizons is data this project did not produce. Measured
  worst: 0.0085″ at JD 2464727.0 and 2.14e-8 AU at JD 2452559.0.
  *(Was 0.1″ and 1e-6 AU.)*
- **Perihelion and aphelion**: the annual minimum and maximum distances land at
  0.9833 AU and 1.0167 AU to within 1e-4 AU — the four decimals those published
  figures carry — and near the right dates, a physical check the fixture rows
  alone would not force. *(Amended 2026-09-20, decision 87:* **swept daily
  across all fifty years**, not one, asserting the worst gap. Measured:
  perihelion 0.9832436–0.9833551 AU and aphelion 1.0166426–1.0167538 AU, so the
  worst gaps are 5.64e-5 and 5.74e-5 AU and the 1e-4 holds with 1.8× margin —
  too little for the choice of year to have been left unstated. **The window is
  not the calendar year**: perihelion straddles the turn of the year, so a
  calendar year holds two and the deeper wins, putting the "annual minimum" on
  31 December in 2003 and 2047. The windows are 1 October–31 March and
  1 April–30 September, one apsis each, and then the dates are assertable too —
  measured 2–5 January and 3–6 July.*)*
- **The March equinox**: at the published instant, the Sun's declination,
  referred to the true equator of date through M1-07's
  `intermediateFromInertial(TtTime)` -- the rotation that needs no UT1, added
  for this test and for M1-63 (register decision 77) -- is within
  0.01° of zero. Aberration, which this geometric position omits, accounts for
  0.002° of it. A sign error in the rotation fails this and passes several
  other tests, which is why it is here. *(Amended 2026-09-20, decision 86:* the
  0.01° stays, and **beside it the declination is asserted within 1e-3° of
  +0.002265°**, the offset aberration predicts — 20.49551″ × sin ε — so the
  test claims the residual rather than merely bounding it, and a declination of
  zero fails. **Three equinoxes**, from USNO's Earth's Seasons: 2024-03-20
  03:06, 2025-03-20 09:01 and 2026-03-20 14:46 UT. Measured +0.002259°,
  +0.001952° and +0.002368°, worst distance from the prediction 3.13e-4°. The
  1e-3° is derived: the instants are rounded to the minute, ±1.4e-4° at
  2.74e-4° a minute, and the Earth's wobble about the Earth–Moon barycentre
  reaches about 7e-4°. USNO publishes UT; reading it as UTC costs ≤ 0.9 s,
  4e-6°.*)*
- **Irradiance**: 1361 W·m⁻² at exactly 1 AU, and the 1/r² falloff checked at
  0.5 AU and 2 AU against hand-computed values. `solarIrradianceAt` is
  `constexpr`, so these are `static_assert`s as well as runtime cases.
- **Continuity**: no jump larger than the per-step motion across a year, swept —
  a wrapped angle handled wrongly shows up here as a discontinuity. *(Amended
  2026-09-20, decision 87:* the bound is **two-sided,
  0.952° ≤ step ≤ 1.020° at one-day steps**, so a wrapped angle fails in either
  direction. Measured 0.952940°–1.019674° over 2000–2050, against two-body
  motion at e = 0.016709, which gives 1.019230° at perihelion and 0.953284° at
  aphelion; the 4e-4° residual is the Earth's wobble about the Earth–Moon
  barycentre, 6.44″ over a 27.32-day month.*)*
- *(Added:)* **The range, by name, at both ends**: JD 2415020.0 and 2488070.0
  TDB succeed, and one millisecond outside either reports
  `OutsideEphemerisRange`, from both functions. A millisecond and not a
  picosecond, because ERFA forms the date as days from J2000, where a double
  resolves 2⁻³⁷ day — about 0.63 µs — so a picosecond outside reaches ERFA as
  exactly the boundary. A date in mid-2100 is outside, and is asked for,
  because "1900–2100" invites the opposite assumption.

## Error budget

**Direction 0.02″, distance 5e-8 AU** against JPL Horizons geometric vectors
over 2000–2050, asserted *(amended 2026-09-20, decision 85)*. About twice the
measured worst, which is the rule decision 54 set and decision 76 applied to
the frame: measured 0.0085″ and 2.14e-8 AU at the 40 fixture epochs, with
position residuals of 1.4–6.3 km. The distance budget is 1e-5 % of irradiance,
so it cannot be what makes the 0.5 % radiometric budget in M1-18 fail.

**The span the budget covers is the fixture's, 2000–2050, and the claim says
so.** `eraEpv00`'s own stated worst over the wider 1900–2100 is 11.2 km —
0.016″ of direction and 7.5e-8 AU of distance — which is *looser than this
budget*. Widening the asserted span therefore means re-measuring and
re-deriving, not merely pointing the test at more epochs.

Recorded because it is the honest part: no *named* defect lives between 0.02″
and the 0.1″ this document carried until 2026-09-20. Aberration is 20.5″, a
barycentric-for-heliocentric slip about 0.6°, a frame error larger still, and
every one of them fails either budget. What the tighter number buys is that a
budget with twelvefold headroom has stopped testing anything and would absorb a
future regression in silence. *(Was 0.1″ and 1e-6 AU; before that 0.01° and
2e-4 AU.)*

**What this suite cannot see**, stated rather than left to look complete: TT
handed to `eraEpv00` in place of TDB (7e-5″, and ERFA's own note 1 permits it);
a wrong astronomical unit (6e-11 relative); and light-time (0.008″, because the
Sun barely moves relative to the barycentre — the 20.5″ is all aberration).

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees.
- [ ] Both budgets asserted against the Horizons fixture, not against a claim,
      and the span they cover stated with them.
- [ ] The frame the result is expressed in, and that it is geometric, are stated
      in the header.
- [ ] The solar constant carries its source.
- [ ] A date outside ERFA's span is reported as `OutsideEphemerisRange`, with
      a test at each boundary.
- [ ] What the suite cannot see is written in the header, not left implied.
- [ ] `TwoPartDate` is gone and `astro/EarthOrientation.cpp` still passes its
      own suite unchanged (decision 89).
- [ ] A mutation pass, with any survivor taken to the owner rather than closed
      by a test nobody ruled on (VERIFICATION.md rule 19).
- [ ] All six toolchains before the commit: both Windows trees, `asan`,
      `windows-msvc`, `linux-sanitize`, `linux-gcc`.

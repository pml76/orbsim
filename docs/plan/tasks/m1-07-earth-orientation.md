# M1-07 — Precession, nutation and the Earth rotation angle

Phase: A | Status: not started
Prerequisites: M1-05, M1-06, M1-86
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md), [ADR 0016](../../adr/0016-the-astronomy-is-erfa.md)

**Amended 2026-09-11** (decisions 27–29): ERFA computes the rotation, nutation
is modelled, and two errors in this document as first written are corrected.
Both were found by computing the answer with ERFA, and ADR 0016 records them:

- it composed IAU 2006 precession in the Fukushima–Williams form, an
  equinox-based matrix, with the Earth rotation angle, which is measured from
  the celestial intermediate origin. They do not compose: on 2026-09-11 the
  pairing is **1231″ — 0.342°, about 38 km at the equator — out**, and the
  error grows by about 46″ a year;
- it tested the rotation rate against **86 164.0905 s, the sidereal day**. ERA
  turns once in the **stellar day, 86 164.098 903 691 s**, and a correct
  implementation misses the sidereal day by 8.4 ms, against a tolerance of
  0.1 ms.

**Amended again 2026-09-19, before the code, on the owner's rulings** —
decisions 75–83 of [the register](../milestone-1-decisions.md). Ten questions
were put at once, each with a measurement from a scratch spike, and every
recommendation was taken. Three further rulings, 72–74, answered the open
question this document carried and became [M1-86](m1-86-ut1-from-tt.md), which
now runs first. What the measurements changed:

- **The code budget is 0.1 mas over 1900–2100**, not 0.1″ over 2000–2050
  (decision 76). Skyfield agrees with `eraC2t06a` to **54 µas** over that span
  — 47 µas of it the TIO locator s′ — so 0.1 mas is about twice the reference's
  own distance, which is the rule decision 54 set. At 0.1″ the test could not
  have seen an omitted frame bias (23.1 mas), IAU 2000B nutation (3.1 mas),
  IAU 2000 precession (2.7 mas) or UT1 wrong by a millisecond (15 mas).
- **The reference is Skyfield 1.55** (decision 75), and the limit of its
  independence is recorded rather than glossed: its nutation is a port of
  NOVAS's, which shares IERS modules with SOFA's. Its *route* is independent —
  sidereal time applied to the equinox-based matrix, where ERFA's is CIO-based.
- **A TT-only rotation is added** (decision 77), because M1-08 and M1-63 need
  the pole of date and neither has a UT1 to hand.
- **Four tolerances this document stated are replaced by measured ones**
  (decision 80), and the smaller points are settled in decision 83.

## Purpose

Earth has to be drawn with its texture in the right place and its terminator
where the Sun actually is, and both need a rotation from the inertial frame to
the body-fixed one. This task builds it. It still comes before the solar
position, because M1-08's March-equinox test needs the equator of date; the
solar position itself no longer needs this rotation, since ERFA's is already
aligned with ICRS. *(Amended.)*

ADR 0006 asks for real frames. This now delivers three of the four pieces —
precession, nutation and rotation — and records what it omits, polar motion
and a modelled ΔUT1, as numbers rather than leaving them implied.

## What to implement

`src/astro/EarthOrientation.hpp` / `.cpp`. `src/astro/` exists since M1-05.

- **The celestial-to-terrestrial rotation from ERFA's `eraC2t06a`**
  *(amended)*: IAU 2006 precession and IAU 2000A nutation in the CIO-based
  form, composed with the Earth rotation angle, and polar motion passed as
  zero. TT and UT1 go in as two-part Julian dates — **the Julian day of the
  midnight and the fraction of the day**, which `julianDate()` already
  produces, and which resolves ERA to 0.04 µas where the MJD method M1-05 uses
  for TDB gives 9.5 µas (decision 83). Called directly rather than composed
  from its parts: `eraC2t06a` also applies the TIO locator s′, so a
  hand-composed `Rz(ERA)·C2I` is *not* the same matrix — measured bit-identical
  in 0 of 200,000 epochs.
- **The Earth rotation angle**, from ERFA's `eraEra00`, which implements its
  IAU 2000 definition:
  `ERA = 2π (0.7790572732640 + 1.00273781191135448 · Tu)` with
  `Tu = JD(UT1) − 2451545.0`. Both constants are definitions rather than
  measurements, and the comment says so.
  `[[nodiscard]] Radians earthRotationAngle(Ut1Time)` on its own, because the
  MFD and the ground track will both want it.
- *(Added, decision 77.)* **The celestial-to-intermediate rotation**,
  `[[nodiscard]] Quat intermediateFromInertial(TtTime)`, from ERFA's
  `eraC2i06a`: precession, nutation and frame bias, without the Earth's
  rotation. Its third row is the pole of date, and it is what M1-08's equinox
  test and M1-63's J2 term need — neither of which depends on UT1, and neither
  of which should have to invent one. Measured: that row is bit-identical to
  `eraC2t06a`'s on 200,000 of 200,000 epochs, and a call costs about 28 µs, so
  M1-63 evaluates it at a cadence of its own rather than per integrator stage.
- **The composed rotation**, returned as a `Quat`:
  `[[nodiscard]] Quat earthFixedFromInertial(TtTime tt, Ut1Time ut1)`. Its
  `rotate()` maps an inertial vector into the Earth-fixed frame, which is the
  direction ERFA's matrix has, so the wrapper transposes nothing (decision 79).
  `Quat`'s comment in `core/Math.hpp`, which said a quaternion represents a
  body-to-world rotation, is amended to say the direction belongs to the name
  of whatever returned it.
- *(Added, decision 78.)* **The matrices, typed and public**, beside the
  quaternions: a minimal `RotationMatrix` in `core/Math.hpp` and
  `quaternionFrom(RotationMatrix)` next to `Quat`. The conversion picks the
  numerically largest component first so that it stays accurate near a half
  turn, and it is this project's code, tested on its own — which it must be,
  since the Earth's matrices reach only two of its four branches. Its
  precondition, that the matrix really is a rotation, is asserted: every
  element of `M·Mᵀ − I` within **1e-12**, and a positive determinant. Rounding
  produces under 1e-14 — ERFA's matrices measured 8.9e-16 — so a failure means
  a matrix that was never a rotation.
- Neither `eraC2t06a` nor `eraEra00` returns a status, so nothing here returns
  `std::expected`, and the header says why (decision 83). A date that is not
  finite can reach these only from a Release build whose arithmetic
  precondition was violated upstream, and comes back as a NaN, as `astro/Tdb`'s
  do.
- **The omissions, in the header, as numbers** *(amended)*: polar motion is not
  modelled (≤ 0.6″, about 19 m — the largest pole excursion in the IERS EOP 20
  C04 series, 1962–2025) and ΔUT1 is unmodelled in the ΔT a caller names
  (≤ 13.5″, about 420 m). Together **≤ 14.1″ ≈ 440 m** while ΔT comes from the
  leap-second table; past its expiry, plus the drift of a held ΔT — at worst
  17.3″ for each year since, on the IERS record (M1-86). This is model error,
  deliberately taken, and it is written where somebody debugging a 400 m
  discrepancy will find it.

## The question this document carried — answered

**Where does a `Ut1Time` come from, past the leap-second table's expiry?**
Ruled 2026-09-19 (decision 72) and built as [M1-86](m1-86-ut1-from-tt.md):
UT1 = TT − ΔT, where ΔT is a validated type the caller names. Inside the table
it is exact; past it a caller takes `kDeltaTHeldAtTableExpiry` by name and the
model error above grows with the drift. So this task's
`earthFixedFromInertial(TtTime, Ut1Time)` can be called at any date the
calendar holds.

## Out of scope

Polar motion, and ΔUT1 from IERS data: both stay zero, with their sizes
above. Frames for any body but Earth. *(Amended: nutation was out of scope as
1,365 terms to transcribe, and is one call now. The CIO-based formulation was
out of scope in favour of the equinox route; ERA belongs to the CIO route, and
ERFA provides it.)*

## Tests

`tests/test_earth_orientation.cpp`, and `tests/test_math.cpp` for the
conversion (decision 78), which is the first suite `core/Math.hpp` has had.

- **ERA at J2000.0** is 0.7790572732640 turns = 280.46061837504°, a published
  defining value, not something derived from our code. To **2e-13 rad**, the
  worst ERFA's own arithmetic reaches with this date split over 1900–2100
  (decision 80).
- **The rotation rate** *(corrected)*: one full turn of ERA takes the stellar
  day, **86 164.098 903 691 s** of UT1 (IERS useful constants), recovered from
  our ERA to within **1e-7 s** — where the arithmetic resolves about 5 ns, and
  the document's original 1e-4 s was four orders above it (decision 80). An
  independent number that a wrong rate constant, or TT passed where UT1
  belongs, fails.
- **The rotation against an independent implementation** *(amended)* —
  Skyfield 1.55, not ERFA, which computes it (ADR 0016) — with ΔUT1 and polar
  motion zero in both: applied to unit vectors, it agrees to **0.1 mas** across
  **1900–2100**, from a committed fixture (decision 82).
- *(Added.)* **The composition is CIO-consistent** — a regression test named
  after the error this document carried. Skyfield's route *is* the other
  correct pairing, sidereal time applied to the equinox-based matrix, so the
  same fixture carries this claim at **1 mas**; the pairing this document first
  asked for fails it by 1,170″ to 2,320″ (decision 81).
- **Only UT1 turns the Earth.** With TT held fixed, moving UT1 changes the
  rotation by a turn about the terrestrial pole of exactly the change in ERA:
  the pole is unchanged, and a vector in the equator turns by that angle, to
  **4e-15** — measured 1.1e-16 and 1.2e-15 (decision 80).
- *(Added.)* **The pole of date is the same with and without the Earth's
  rotation**: `intermediateFromInertial`'s pole and `earthFixedFromInertial`'s
  agree, which is what M1-63 will lean on.
- **Round trip**: inertial → body-fixed → inertial recovers a vector to
  **4e-15** relative, over a seeded sweep — measured 1.4e-15.
- **Unit quaternion**: `|q| − 1` stays under 1e-15 across the same sweep
  (measured one ulp), and the quaternion reproduces ERFA's matrix to **2e-15**
  (measured 8.3e-16 over 450,000 matrices), including near a half turn.
- *(Added.)* **The conversion on its own**, in `test_math.cpp`: all four
  branches, driven by synthetic matrices since the Earth's reach only two;
  exact half turns about x, y and z; and the precondition refusing a matrix
  that is not a rotation.

## Error budget

**Code error 0.1 mas** over 1900–2100 against Skyfield 1.55, asserted. **Model
error ≤ 14.1″ (≈ 440 m on the ground)** from an unmodelled ΔUT1 and omitted
polar motion while ΔT comes from the leap-second table, growing by the drift of
a held ΔT past its expiry — recorded in the header, in ADR 0016 and in the
commit message. Not asserted, because it is not a defect: it is a modelling
decision with a size.

## Verification

The standing rules, and — since this touches `core/Math.hpp` — all six
toolchains before the commit (decision 83), plus a mutation pass whose
survivors go to the owner.

## Done when

- [ ] `check` green in both trees.
- [ ] The 0.1 mas budget is asserted against Skyfield, not against ERFA.
- [ ] The model error appears as a number in the header and the commit message.
- [ ] The rate test uses the stellar day, and the composition test would fail
      the pairing this document first asked for.
- [ ] The conversion is tested on all four of its branches.
- [ ] Every published constant carries its citation.

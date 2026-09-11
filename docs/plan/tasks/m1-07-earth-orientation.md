# M1-07 — Precession, nutation and the Earth rotation angle

Phase: A | Status: not started
Prerequisites: M1-05, M1-06
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

## Purpose

Earth has to be drawn with its texture in the right place and its terminator
where the Sun actually is, and both need a rotation from the inertial frame to
the body-fixed one. This task builds it. It still comes before the solar
position, because M1-08's March-equinox test needs the equator of date; the
solar position itself no longer needs this rotation, since ERFA's is already
aligned with ICRS. *(Amended.)*

ADR 0006 asks for real frames. This now delivers three of the four pieces —
precession, nutation and rotation — and records what it omits, polar motion
and ΔUT1, as numbers rather than leaving them implied.

## What to implement

`src/astro/EarthOrientation.hpp` / `.cpp`. `src/astro/` exists since M1-05.

- **The celestial-to-terrestrial rotation from ERFA's `eraC2t06a`**
  *(amended)*: IAU 2006 precession and IAU 2000A nutation in the CIO-based
  form, composed with the Earth rotation angle, and polar motion passed as zero.
  TT and UT1 go in as two-part Julian dates built from the exact day and
  picoseconds. *(Was: the Fukushima–Williams precession matrix, composed with
  ERA — the pairing that does not compose.)*
- **The Earth rotation angle**, from ERFA's `eraEra00`, which implements its
  IAU 2000 definition:
  `ERA = 2π (0.7790572732640 + 1.00273781191135448 · Tu)` with
  `Tu = JD(UT1) − 2451545.0`. Both constants are definitions rather than
  measurements, and the comment says so.
  `[[nodiscard]] Radians earthRotationAngle(Ut1Time)` on its own, because the
  MFD and the ground track will both want it.
- **The composed rotation**, returned as a `Quat` so it fits the maths that
  already exists in `core/Math.hpp`:
  `[[nodiscard]] Quat earthFixedFromInertial(TtTime tt, Ut1Time ut1)`. ERFA
  returns a 3×3 matrix; turning it into a unit quaternion is this project's
  code, done by the method that picks the numerically largest component first
  so that it stays accurate near a half turn, and it is tested on its own.
  Neither `eraC2t06a` nor `eraEra00` returns a status, and the header says so,
  so nobody looks for the error that is not mapped.
- **The omissions, in the header, as numbers** *(amended)*: polar motion is not
  modelled (≤ 0.6″, about 19 m — the largest pole excursion in the IERS EOP 20
  C04 series, 1962–2025) and ΔUT1 is zero by default (≤ 13.5″, about 420 m).
  Together **≤ 14.1″ ≈ 440 m**. This is model error, deliberately taken, and it
  is written where somebody debugging a 400 m discrepancy will find it. *(Was:
  nutation ≤ 25″ and ΔUT1, together ≤ 40″ ≈ 1.2 km.)*

## Out of scope

Polar motion, and ΔUT1 from IERS data: both stay zero, with their sizes
above. Frames for any body but Earth. *(Amended: nutation was out of scope as
1,365 terms to transcribe, and is one call now. The CIO-based formulation was
out of scope in favour of the equinox route; ERA belongs to the CIO route, and
ERFA provides it.)*

## Tests

`tests/test_earth_orientation.cpp`.

- **ERA at J2000.0** is 0.7790572732640 turns = 280.46061837504°, a published
  defining value, not something derived from our code.
- **The rotation rate** *(corrected)*: one full turn of ERA takes the stellar
  day, **86 164.098 903 691 s** of UT1 (IERS useful constants), recovered from
  our ERA to within 1e-4 s. An independent number that a wrong rate constant,
  or TT passed where UT1 belongs, fails.
- **The rotation against an independent implementation** of the same IAU
  models *(amended)* — **not ERFA**, which computes it (ADR 0016) — with ΔUT1 and
  polar motion zero in both: applied to unit vectors, it agrees to **0.1″**
  across 2000–2050. Candidates are NOVAS 3.1 (US Naval Observatory) and
  Skyfield; verify the terms of the one chosen and that it was written
  independently of SOFA, record both, and commit its values as an M1-06
  fixture with their provenance. If none can be obtained, **stop and raise
  it**.
- *(Added.)* **The composition is CIO-consistent** — a regression test named
  after the error this document carried. The rotation agrees with Greenwich
  apparent sidereal time applied to the equinox-based
  bias-precession-nutation matrix to 1 mas (measured: 13 µas), so a
  composition that mixes the two routes fails by a third of a degree.
- **Only UT1 turns the Earth.** With TT held fixed, moving UT1 changes the
  rotation by a turn about the terrestrial pole of exactly the change in ERA:
  the pole is unchanged, and a vector in the equator turns by that angle, to
  1e-12. *(Amended from "a vector on the rotation axis is unchanged by ERA",
  which the composed rotation no longer exposes directly.)*
- **Round trip**: inertial → body-fixed → inertial recovers a vector to 1e-12
  relative, over a seeded sweep.
- **Unit quaternion**: `|q| − 1` stays under 1e-15 across the same sweep, and
  the quaternion reproduces ERFA's matrix to 1e-15, including near a half turn.

## Error budget

**Code error 0.1″** against an independent implementation, asserted. **Model
error ≤ 14.1″ (≈ 440 m on the ground)** from ΔUT1 = 0 and omitted polar
motion, recorded in the header, in ADR 0016 and in the commit message. Not
asserted, because it is not a defect — it is a modelling decision with a size.

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees.
- [ ] The 0.1″ budget is asserted against an independent implementation, not
      against ERFA.
- [ ] The model error appears as a number in the header and the commit message.
- [ ] The rate test uses the stellar day, and the composition test would fail
      the pairing this document first asked for.
- [ ] Every published constant carries its citation.

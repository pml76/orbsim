# M1-07 — Precession and the Earth rotation angle

Phase: A | Status: not started
Prerequisites: M1-05, M1-06

## Purpose

Earth has to be drawn with its texture in the right place and its terminator
where the Sun actually is, and both need a rotation from the inertial frame to
the body-fixed one. This task builds it, and it comes **before** the solar
position because the analytic solar formula is referred to the equinox of date
and needs precession to reach ICRF.

ADR 0006 asks for real frames. This delivers two of the four pieces —
precession and rotation — and records the two it omits as numbers rather than
leaving them implied.

## What to implement

`src/astro/EarthOrientation.hpp` / `.cpp`. **`src/astro/` is new**: add it to
`ORBSIM_CORE_SOURCES`, to the header self-check list, to the lint list, and to
the directory map in `CLAUDE.md`.

- **IAU 2006 precession**, via the Fukushima–Williams angles γ̄, φ̄, ψ̄ and ε_A as
  polynomials in Julian centuries of TT, composed as
  `R1(−ε_A) · R3(−ψ̄) · R1(φ̄) · R3(γ̄)`. The coefficients are published values —
  copy them from the literature, cite the paper in the comment, and give each
  its units.
- **The Earth rotation angle**, from its IAU 2000 definition:
  `ERA = 2π (0.7790572732640 + 1.00273781191135448 · Tu)` with
  `Tu = JD(UT1) − 2451545.0`. Both constants are definitions rather than
  measurements, and the comment says so.
- **The composed rotation**, returned as a `Quat` so it fits the maths that
  already exists in `core/Math.hpp`:
  `[[nodiscard]] Quat earthFixedFromInertial(TtTime tt, Ut1Time ut1)`, plus
  `[[nodiscard]] Radians earthRotationAngle(Ut1Time)` on its own, because the
  MFD and the ground track will both want it.
- **The omissions, in the header, as numbers**: nutation is not modelled
  (≤ 25″, about 770 m on the ground) and ΔUT1 is zero by default (≤ 15″, about
  420 m). Together ≤ 40″ ≈ 1.2 km. This is model error, deliberately taken, and
  it is written where somebody debugging a 1 km discrepancy will find it.

## Out of scope

Nutation (IAU 2000A is 1365 terms and buys 25″ here). Polar motion. The
celestial intermediate origin formulation — the classical equinox-based route is
enough at this accuracy and is easier to check against published examples.
Frames for any body but Earth.

## Tests

`tests/test_earth_orientation.cpp`.

- **ERA at J2000.0** is 0.7790572732640 turns = 280.46061837504°, a published
  defining value, not something derived from our code.
- **The rotation rate**: one full rotation of the body-fixed frame takes
  86164.0905 s of UT1 — the published sidereal day — recovered from our ERA to
  within 1e-4 s. An independent number that a wrong rate constant fails.
- **Precession against reference values** obtained from an implementation this
  project did not write, committed as an M1-06 fixture with its provenance:
  the rotation applied to a unit vector agrees to **0.1″** across 2000–2050.
- **Round trip**: inertial → body-fixed → inertial recovers a vector to 1e-12
  relative, over a seeded sweep.
- **Unit quaternion**: `|q| − 1` stays under 1e-15 across the same sweep.
- **A singularity case**: a vector exactly on the rotation axis is unchanged by
  ERA, and one in the equatorial plane rotates by exactly the ERA.

## Error budget

**Code error 0.1″** against the reference, asserted. **Model error ≤ 40″
(≈ 1.2 km on the ground)** from omitted nutation and ΔUT1 = 0, recorded in the
header, in ADR 0009 and in the commit message. Not asserted, because it is not a
defect — it is a modelling decision with a size.

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees; `src/astro/` builds inside `orbsim_core`.
- [ ] The 0.1″ budget is asserted against external reference values.
- [ ] The model error appears as a number in the header and the commit message.
- [ ] Every published constant carries its citation.

# M1-81 — The readout, formatted and tested

Phase: G | Status: not started
Prerequisites: M1-03, M1-80
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md)

## Purpose

Turning numbers into strings, which sounds trivial and is where instruments
mislead people. A readout that prints `0.0` for a value it could not compute, or
that silently rounds 6,378,137 m to "6378 km" and loses the metre a pilot was
watching, is worse than one that prints nothing.

All of it is pure functions, so all of it is tested without a GPU.

## What to implement

`src/view/MfdFormat.hpp`.

- Formatters for what the milestone plan asks the Orbit MFD to show: apoapsis
  and periapsis altitude, orbital period, eccentricity, inclination, and — added
  on 2026-09-07 because there is a real epoch now — **UTC and mission elapsed
  time**.
- **Units chosen per quantity and shown**, always: kilometres with one decimal
  for altitudes at orbital scale, degrees for angles, `HH:MM:SS` for durations,
  ISO-8601 for UTC. A number without a unit on an instrument is a defect.
- **The unrepresentable cases are printed as such**, by name rather than by
  accident: apoapsis of a hyperbolic orbit and its period are infinite, and the
  readout shows a dash, not `inf` and not a huge number. A NaN — which should
  not occur, and which M1-72's monitors would report — shows a dash too rather
  than propagating into the display.
- **Precision is stated per field with a reason.** Altitude to 0.1 km because
  that is the useful resolution at orbital scale; eccentricity to five decimals
  because that is where J2's variation shows; inclination to three because the
  drift this milestone wants to be visible is in the third.
- Formatting is **locale-independent**: `std::format` with an explicit locale,
  never the ambient one. A decimal comma in a readout is a bug report nobody can
  reproduce.

## Out of scope

Anything that draws. Editable fields. Unit preferences.

## Tests

`tests/test_mfd_format.cpp`, headless, and exhaustive because it is cheap.

- **Known values format exactly as expected**, character for character, for
  every field — a 400 km circular orbit's whole readout, written out in the test
  by hand.
- **Rounding at the boundary**: values at exactly `x.x5` round the way the
  documented rule says, checked from both sides. Half-way rounding is where two
  implementations disagree.
- **The infinite cases**: hyperbolic apoapsis and period show the dash.
- **NaN shows the dash**, and does not print `nan` or `-nan`.
- **Negative and zero**: a negative altitude below the ellipsoid formats with a
  sign; a zero-length MET formats as `00:00:00`.
- **Long durations**: a MET beyond 99 hours does not overflow its field, and the
  field width rule is asserted rather than assumed.
- **UTC formatting round-trips** through the M1-03 parser to the same instant.
- **Every string fits its layout region** from M1-80, asserted for the widest
  possible value of each field. That is what turns "it looked fine" into a
  guarantee, and it is why the layout came first.

## Verification

The standing rules. No GPU anywhere in this task.

## Done when

- [ ] `check` green in both trees.
- [ ] Every field has a hand-written expected string in the suite.
- [ ] Infinity and NaN show a dash, by name, with tests.
- [ ] The widest value of every field fits its region.

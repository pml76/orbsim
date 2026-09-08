# M1-77 — The precession budget

Phase: F | Status: not started
Prerequisites: M1-63, M1-68, M1-76

## Purpose

Phase F's acceptance criterion, and the amendment the milestone plan calls *"the
one most worth reading"*. It used to be that the drawn ellipse **stays put**
under time acceleration. With J2 that is exactly backwards: an ISS-like orbit's
ascending node regresses about 5° per day, so

> **A track that stays put is now a failing test.**

This turns the phase from a drawing task into the first place a perturbation
error is visible to the naked eye rather than only to a test suite.

## What to implement

Mostly measurement, plus the small amount of code that makes it possible:

- A headless harness that propagates a stated scenario for five days of
  simulated time, converts to osculating elements at regular intervals, and
  records the ascending node and the argument of periapsis.
- **The node rate is fitted over whole orbits**, not taken from endpoints. The
  osculating node oscillates with a short-period amplitude of about 0.03°
  against 25° of secular drift; sampling at arbitrary times mixes the two, and
  averaging over an integer number of orbits removes it cleanly. The method is
  stated in the test, because it is the difference between measuring the physics
  and measuring the sampling.

## Tests

`tests/test_precession.cpp`.

- **The budget: the measured nodal regression is within 1 % of**
  ```
  dΩ/dt = -1.5 · n · J2 · (Re/p)² · cos i
  ```
  over five days, for a 400 km circular orbit at i = 51.6°. The analytic rate is
  computed in the test from the elements — it is a formula from the literature,
  not from anything in `src/`, which is what makes it an independent check.
  For this orbit the expected value is about **−5.0° per day**, and that number
  goes in the test as a sanity comment.
- **Apsidal precession, as a second independent check**:
  `dω/dt = 0.75 · n · J2 · (Re/p)² · (5cos²i − 1)`, same budget. Two formulas,
  two different combinations of the same terms — a J2 implementation that is
  wrong by a factor or a sign will rarely satisfy both.
- **The sign is right**: for a prograde orbit the node regresses — westward,
  negative. Half of all sign errors survive a magnitude comparison, and this
  assertion catches them.
- **The dependence on inclination is right**, which is the strongest structural
  check available: at i = 0 the node rate is maximal and negative; at i = 90° it
  is zero; above 90° it reverses sign. Three inclinations, three predictions,
  one formula.
- **A sun-synchronous orbit works out**: at 800 km altitude, the inclination for
  which the node advances 0.9856°/day matches the published sun-synchronous
  inclination of about 98.6° to within the budget. That is a well-known number
  produced entirely by this physics, and getting it right is strong evidence
  that nothing is subtly off.
- **The test has teeth**: with J2 set to zero, every one of the above must fail.
  Recorded in the commit message.

## Error budget

**1 %** of the analytic secular rate for both node and periapsis, over five
days. The short-period oscillation is about 0.1 % of the drift over that span,
so the budget sits an order of magnitude above the noise — stated here so the
number is defensible rather than round.

## Frames to look at

Two frames from the same camera and the same scenario, five simulated days
apart, written by a `precession` probe: `precession.day0.png` and
`precession.day5.png`. Judge: the track has visibly rotated about the polar
axis, in the right direction, by about 25° of node — five days at roughly 5° per
day. **This is the
image that shows a perturbation working**, and it is worth keeping side by side
in the milestone record.

## Done when

- [ ] `check` green in both trees.
- [ ] Both secular rates are within 1 % of the analytic values.
- [ ] The sun-synchronous inclination comes out right.
- [ ] Every check fails with J2 disabled, and that is recorded.
- [ ] The two frames are approved, and phase F's inverted criterion is met.

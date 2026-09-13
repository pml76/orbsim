---
paths:
  - "src/core/**"
  - "src/orbit/**"
  - "src/astro/**"
  - "tests/**"
---

# How to test physics

Loaded automatically whenever the simulation core or a test is touched. The
full argument is [`docs/VERIFICATION.md`](../../docs/VERIFICATION.md), which is
binding; this is the part that applies to every change.

**Write the test first, and watch it fail before you make it pass.** A
numerical test that passes the moment it is written is usually a tolerance
loose enough to accept anything. Read the failure message and check that it
would let somebody else diagnose the problem.

Every new function in `src/orbit/` gets, in `tests/`:

- a check against something that does not come from the code: an analytic
  value, a constant of motion (energy, angular momentum), or the other
  propagator;
- a case at every scale the simulator flies -- the Moon, Earth, Jupiter, the
  Sun as central bodies -- because a suite that only flew Earth orbits passed
  732 checks while `propagate()` failed at 1 AU;
- a case at every *singularity*, which is the other axis: `e = 0`, `e` either
  side of 1, `i = 0`, `i = pi`, retrograde, `dt = 0`. A random sweep visits
  these with probability zero;
- its failure paths, by name (`NotFinite`, not "did not converge");
- for anything numerical, a seeded random sweep over the parameter space,
  with the seed written down so a failure can be reproduced.

**Anything claiming accuracy states its error budget first**, as a number, and
validates it against something external -- JPL Horizons vectors, a published
worked example, the other implementation. A constant of motion proves the code
is self-consistent, not that it is right: a wrong `mu` conserves energy
perfectly. The budget goes in the test and in the commit message.

**Ask "in what?" of every bare number.** Two bugs in this codebase were found
by that one question: a tolerance compared against a quantity in sqrt(metres),
and a threshold compared against one in 1/metres. Both were invisible at Earth
scale.

**Reference data that lands near a named constant needs a shape, not a
suppression.** `modernize-use-std-numbers` reports any floating literal within
1e-3 of one of `std::numbers`' constants, and measured reference values land
there by coincidence -- a nearly radial orbit's true anomaly is always within a
nanoradian of pi, and one measured inclination came out 1.95e-4 from Euler's
gamma. The check is right in general and cannot tell the two cases apart, and a
`NOLINT` is the owner's call (CLAUDE.md rule 7), so:

- **near pi, and only where it means something**, write the value as
  `kPi + <exact offset>`. The offset is the difference of two doubles near each
  other, so it is exact, and `kPi + offset` reproduces the original double bit
  for bit -- verify that rather than assuming it. It also reads better: "pi plus
  1e-8" is what a far-apsis anomaly *is*.
- **otherwise pick a different case.** Where the collision is a coincidence
  rather than a meaning -- an inclination near `egamma` -- the reference set
  almost always contains another state that is just as extreme and collides with
  nothing. 9,836 of 10,000 nearly radial states in one sweep were clean.

Hex float literals do not help: the check works on the value, not the spelling.

`tests/OrbitTestSupport.hpp` has the bodies, the fixtures and the custom
matchers; `tests/test_orbit_scales.cpp` is the shape to copy. The suites are
Catch2, and the matchers there take a `Tolerance` rather than a bare double, so
a transposed argument does not compile. (`tests/TestHarness.hpp` was the
hand-rolled harness and no longer exists -- M1-01 deleted it.)

# M1-70 — Determinism

Phase: E | Status: not started
Prerequisites: M1-69
Decided by: [ADR 0011](../../adr/0011-the-integrator-has-three-seams.md)

## Purpose

`VERIFICATION.md` rule 16: *"Run a scenario twice from the same initial state;
assert bit-identical results. This is the one place an exact floating-point
comparison is correct, because bit identity is the actual claim -- and it is
made by name, with `bitIdentical()`, which also tells +0.0 from -0.0."* (The
wording since 2026-09-11, when the value types lost their floating-point `==`;
[ADR 0017](../../adr/0017-every-warning-is-an-error.md).)

Three separate claims are being made by this project, and each gets its own
assertion here. Two are about time; the third is ADR 0007's load-bearing clause,
and it has been waiting since phase A for a simulation to exist.

## What to implement

Mostly tests. The production code this task adds is small and specific:

- **A state dump**: the simulation's full state — epoch, position, velocity,
  propagator state, stepper state, accumulator remainder — serialised
  deterministically, so two runs can be compared byte for byte and a divergence
  can be localised to the step where it started rather than observed at the end.
- **`--sim-dump <path>`** on the app, so the same comparison can be made through
  the real application with the renderer running, which is what the quality
  assertion needs.

## Tests

`tests/test_determinism.cpp`, plus one that needs the app.

- **Two runs, same result**: 24 hours of simulated time, bit-identical states at
  every hour boundary. Not "within tolerance" — identical.
- **1× against 10000×**: the same 24 hours at two acceleration factors,
  bit-identical. The mechanism was asserted in M1-69; this is the claim.
- **Save and resume**: dumping at hour 12, restoring, and continuing gives a
  trajectory bit-identical to the uninterrupted run. This is the property that
  makes a saved scenario meaningful, and it is what catches state that lives
  somewhere the dump does not know about.
- **The quality claim, which closes ADR 0007**: the same scenario run at the Low
  and Ultra `RenderQuality` presets produces **bit-identical simulation state**.
  Run through the app with `--sim-dump`, at both presets, over an hour of
  simulated time. ADR 0007 predicted this test would be *"cheap, available long
  before the quality system is finished, and the one that catches a rendering
  concern leaking downward"*; the link graph already makes the leak
  uncompilable, and this is the assertion that would notice if that ever stopped
  being true.
- **The tests have teeth**, demonstrated rather than assumed: with the
  accumulator's remainder deliberately dropped, the varying-frame-rate test must
  fail. With a `std::unordered_map` substituted into the force-term container,
  the two-run test must fail. Each is a one-line change made temporarily, and the
  failure is recorded in the commit message — mutation testing (rule 19) applied
  to the tests that are hardest to trust.

## Error budget

None. Bit identity, or a failure. This is the one place a tolerance would hide
exactly the drift being tested for.

## Verification

The standing rules, plus the app-level quality comparison, plus both Linux
presets: bit identity is a per-binary property, so what is asserted is identity
*within* a build, and the cross-compiler runs check that both builds are
internally deterministic rather than that they agree with each other. The header
says so, because that distinction is easy to lose.

## Done when

- [ ] `check` green in both trees.
- [ ] All four identity claims hold.
- [ ] Each was shown to fail under a deliberate one-line mutation.
- [ ] ADR 0007's determinism test exists, and the ADR is updated to say so.
- [ ] `VERIFICATION.md` Part 4's rule 16 row cites the simulation, not only the
      propagator.

# M1-73 — Fuzzing the force model and the steppers

Phase: E | Status: not started
Prerequisites: M1-63, M1-65

## Purpose

`fuzz_orbit` found five distinct defects in code that had passed 3,513 checks,
zero warnings and zero clang-tidy findings — each within a few thousand
executions, and none reachable by any test anyone had written. The force model
and the steppers are new numerical code of exactly the same character, and they
consume states that a scenario, a burn or a previous step can produce.

## What to implement

`tests/fuzz_integrator.cpp`, the project's fifth fuzz target.

- Arbitrary bytes are mapped to: an initial state, a `mu`, a step size, a number
  of steps, a J2 value, and a choice of stepper and propagator. The mapping is
  written so that **the whole input space is reachable** — including subnormals,
  infinities, NaNs and extreme exponents — rather than a comfortable band of
  plausible orbits. The bugs `fuzz_orbit` found all lived outside the
  comfortable band.
- The invariants it asserts are the ones a fuzzer can know without knowing the
  right answer:
  - a reported failure is **always** acceptable;
  - a **success must be finite** — no NaN, no infinity, in any component;
  - a success must satisfy the postconditions the propagator claims;
  - **Cowell and Encke either both report, or both succeed and agree** to within
    a generous bound. Two independent formulations disagreeing on an input
    nobody chose is exactly the signal that is wanted here.
- Any crash or disagreement found becomes a **committed corpus entry and a named
  regression test**, per rule 6, with the test named after the symptom.

## Out of scope

Fuzzing the simulation clock — it consumes frame durations, which are not
attacker-controlled, and its adversarial cases are already covered by the
accumulator tests. Fuzzing the renderer.

## Tests

The fuzzer is the test. What is checked here is that it is a real one:

- It builds under `ORBSIM_BUILD_FUZZERS`, with ASan and UBSan **confirmed
  linked**, not merely configured — the check from `PROJECT_STATE.md` section
  6.3.
- The input mapping is itself unit-tested: a sweep of byte patterns produces
  states covering the intended ranges, including the extremes. A fuzzer that
  only ever generates sensible orbits is a slow test suite.
- A deliberately broken build — a sign flipped in J2 — is found by the
  Cowell/Encke agreement check within a short run. That is how we know the
  oracle works before trusting a clean run.

## Verification

```
wsl … --preset linux-fuzz && cmake --build build/linux-fuzz && \
    ./build/linux-fuzz/fuzz_integrator -max_total_time=600
```

**Ten minutes clean** before this task is done — longer than the four minutes
the other targets get, because this one has a comparison oracle and therefore
finds more than crashes.

## Done when

- [ ] `check` green in both trees.
- [ ] `fuzz_integrator` runs ten minutes with zero findings.
- [ ] Every finding along the way became a corpus entry and a named test.
- [ ] The sign-flip experiment confirmed the agreement oracle actually fires.

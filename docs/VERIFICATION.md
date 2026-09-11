# How this project avoids bugs

Kind: explanation
Binding: yes — adopted 2026-09-06, applies to every change
Read when: before you write a test, not after. Part 4 says which of these a
machine actually checks. Versions and counts are in
[`STATUS.md`](STATUS.md); what was found on a given day is in
[`HISTORY.md`](HISTORY.md).

## Contents

**Part 1 — Habits**

- [Rule 1. Write the failing test first, and make it fail for the right reason](#rule-1-write-the-failing-test-first-and-make-it-fail-for-the-right-reason)
- [Rule 2. Never check code against itself](#rule-2-never-check-code-against-itself)
- [Rule 3. Validate against external truth, not just internal consistency](#rule-3-validate-against-external-truth-not-just-internal-consistency)
- [Rule 4. State an error budget before writing the code](#rule-4-state-an-error-budget-before-writing-the-code)
- [Rule 5. Test the singularities, not just the scales](#rule-5-test-the-singularities-not-just-the-scales)
- [Rule 6. Every bug gets a regression test named after the bug](#rule-6-every-bug-gets-a-regression-test-named-after-the-bug)
- [Rule 7. Assert the impossible, report the possible — and never blur them](#rule-7-assert-the-impossible-report-the-possible--and-never-blur-them)
- [Rule 8. Bound every loop *and* report non-convergence](#rule-8-bound-every-loop-and-report-non-convergence)
- [Rule 9. Ask "in what?" of every bare number](#rule-9-ask-in-what-of-every-bare-number)
- [Rule 10. Commit small, and keep the tree bisectable](#rule-10-commit-small-and-keep-the-tree-bisectable)

**Part 2 — Infrastructure**

- [Rule 11. Property-based and metamorphic testing](#rule-11-property-based-and-metamorphic-testing)
- [Rule 12. Seeded randomised sweeps, with the seed written down](#rule-12-seeded-randomised-sweeps-with-the-seed-written-down)
- [Rule 13. Fuzzing](#rule-13-fuzzing)
- [Rule 14. Differential testing between implementations](#rule-14-differential-testing-between-implementations)
- [Rule 15. Runtime invariant monitors, not just test-time ones](#rule-15-runtime-invariant-monitors-not-just-test-time-ones)
- [Rule 16. Determinism is a tested property](#rule-16-determinism-is-a-tested-property)
- [Rule 17. Extend the type system to full dimensional analysis](#rule-17-extend-the-type-system-to-full-dimensional-analysis)
- [Rule 18. Coverage is a map of what has not been tested](#rule-18-coverage-is-a-map-of-what-has-not-been-tested)
- [Rule 19. Mutation testing, occasionally](#rule-19-mutation-testing-occasionally)
- [Rule 20. Run the Linux presets — the second compiler and UBSan are back](#rule-20-run-the-linux-presets--the-second-compiler-and-ubsan-are-back)
- [Rule 21. Keep `check` as the single definition of done, and let it grow](#rule-21-keep-check-as-the-single-definition-of-done-and-let-it-grow)

**Part 3 — The rules that are about people**

- [Rule 22. Write down why, at the moment you know why](#rule-22-write-down-why-at-the-moment-you-know-why)
- [Rule 23. Distrust agreement](#rule-23-distrust-agreement)
- [Rule 24. Prefer the bug you cannot write](#rule-24-prefer-the-bug-you-cannot-write)

**Part 4 — What actually enforces what**

- The honest accounting of which rules a machine checks
  and which depend on a person. **Read this one if you read one.**

Status: **adopted (2026-09-06).** These rules apply to every change by default.
`CLAUDE.md` points here, and the enforcement table below says which rules a
machine checks and which rely on a person.

[`CODING_GUIDELINES.md`](../CODING_GUIDELINES.md) is about how to write the
C++. This document is about how to know it is right. They overlap deliberately —
a type that cannot be misused is a bug prevented, which is the cheapest kind —
but the rules below are about *process and evidence* rather than syntax.

The physics these rules are protecting is set by
[`adr/0006`](adr/0006-simulation-not-sandbox.md): a real multi-body simulation,
not a sandbox. That decision is what makes rules 3 and 4 — external validation
and a stated error budget — load-bearing rather than nice to have.

The premise, and the reason this document exists at all:

> **A physics bug does not crash. It returns a number, the number is plausible,
> and you find out three hours of simulated flight later.**

Every rule here is chosen to shorten that feedback loop. They are ordered by
what they cost, not by importance: rules 1–10 are habits that cost nothing but
attention, 11–21 are infrastructure worth building, and 22–24 are about people.
Part 4 then records which of them a machine actually enforces today, which is
the part to read if you only read one.

---

## Part 1 — Habits

### Rule 1. Write the failing test first, and make it fail for the right reason

Test-driven development, with one amendment that matters here: **watch the test
fail before you make it pass, and read the failure message.** A test that passes
the moment you write it has told you nothing, and in numerical code the usual
cause is a tolerance loose enough to accept anything.

The workflow this project already half-follows, made explicit:

1. Write the test, expressing the claim in the domain's own language (an
   analytic value, a conserved quantity, a named failure).
2. Run it. Confirm it fails, and that the message would let someone else
   diagnose it.
3. Write the smallest code that passes it.
4. Run `check` in both trees.

**Why:** the alternative — writing the code, then writing a test that agrees
with it — verifies that the code does what the code does.

### Rule 2. Never check code against itself

Already in `CLAUDE.md` and elevated here because it is the most valuable rule in
the document. Every new function in `src/orbit/` is checked against something
that did not come out of the same code:

- an **analytic value** worked out independently,
- a **constant of motion** (energy, angular momentum, the Jacobi integral in the
  restricted three-body problem),
- **a second implementation** with a different formulation.

`propagate()` versus `propagateElements()` is the model: universal-variable
against classical Kepler, sharing no code and no formulation. Neither can hide a
sign error behind the other. That design is why the heliocentric scale bug was
findable at all.

**Applied to what is coming:** an Encke integrator must be checked against
Cowell; a J2 propagator against its own analytic secular rates
(`dOmega/dt = -1.5 n J2 (Re/p)^2 cos i`); a multi-body integrator against a
known periodic three-body orbit.

### Rule 3. Validate against external truth, not just internal consistency

Constants of motion prove the integrator is *self-consistent*. They do not prove
it is *right* — a wrong gravitational parameter conserves energy beautifully.

Bring in data the project did not produce:

- **JPL Horizons** state vectors for real bodies and real spacecraft, at
  recorded epochs, committed as test fixtures. This is the single highest-value
  test asset this project can acquire and it is free.
- **Published test cases**: Vallado's *Fundamentals of Astrodynamics* worked
  examples, the standard SGP4 test vectors, GMAT or Orbiter output for a
  scenario reproduced identically.
- For rendering, **real photographs**. An ISS-altitude sunrise is a checkable
  reference image, not an aesthetic opinion.

**Why:** this is the only class of test that can catch an error in the *model*
rather than in the code. Every other rule here assumes the equations are right.

### Rule 4. State an error budget before writing the code

"Realistic" is not a test result. Before implementing anything in
[`plan/realism.md`](plan/realism.md), write down the number it must hit:

> *Position error < 1 km after 24 h for a 400 km circular orbit at i = 51.6 deg,
> against JPL Horizons.*

Then the test asserts the budget, and the budget appears in the commit message.

**Why:** without a number, "good enough" is decided by whoever is tired, and
accuracy regressions are invisible. With one, a change that doubles the error
fails a test.

### Rule 5. Test the singularities, not just the scales

`CLAUDE.md` already asks for a case at every scale — Moon, Earth, Jupiter, Sun —
and `test_orbit_scales.cpp` exists because a suite of only Earth orbits passed
732 checks over a broken propagator. The same argument applies along the *other*
axis: the places where the parameterisation degenerates.

For orbital mechanics that is `e = 0`, `e = 1` approached from both sides,
`i = 0`, `i = pi`, `e` very close to 1 (0.999, 0.99999), retrograde,
near-rectilinear, and `dt = 0`. For attitude it is gimbal-adjacent
configurations and 180-degree rotations. For rendering it is the terminator, the
poles, and the tile boundaries.

**Why:** degenerate cases are where the formula divides by something that just
became zero, and they are exactly the cases a randomised sweep visits with
probability zero.

### Rule 6. Every bug gets a regression test named after the bug

When something is fixed, the test that would have caught it goes in *and keeps
the failure's name*. `test_orbit.cpp:414` already does this — the comment
records that `propagate()` used to hand back its input for a zero-radius state.

The test name says the symptom, not the mechanism: `NotFinite`, not "did not
converge".

**Why:** it stops the bug coming back, and it documents the failure mode for
whoever reads the suite as a specification.

### Rule 7. Assert the impossible, report the possible — and never blur them

Already ADR 0002 and `core/Contract.hpp`, restated because it is a verification
rule as much as a design one. A condition a scenario file can produce is
reported through `std::expected`. A condition that can only mean this code is
wrong is asserted.

The failure mode to avoid is the third option: **silently coping**.
`if (r0 <= 0.0) return sv;` is neither a report nor an assertion, and it turned
a findable error into a spacecraft that mysteriously stopped moving.

### Rule 8. Bound every loop *and* report non-convergence

Already a non-negotiable. Repeated here because the second half is the half
everyone omits, and because every integrator, root-finder and iterative solver
in [`plan/realism.md`](plan/realism.md) will want to omit it again.

### Rule 9. Ask "in what?" of every bare number

The session that produced `test_orbit_scales.cpp` found two bugs with one
question. A tolerance was an absolute number compared against a quantity in
sqrt(metres); a threshold was compared against a quantity in 1/metres. Both were
invisible at Earth scale and both broke at 1 AU.

Every constant carries its unit in its name, its type, or a comment saying where
the number came from. **A comparison between two quantities is only meaningful
if they have the same dimensions, and the reviewer's job is to check that.**

### Rule 10. Commit small, and keep the tree bisectable

`git bisect` is what finds the commit that broke a trajectory when the symptom
appears six weeks later. It only works if each commit does one thing and each
commit builds. A formatting pass gets its own commit; a refactor does not ride
along with a fix.

---

## Part 2 — Infrastructure

### Rule 11. Property-based and metamorphic testing

Instead of "this input gives this output", assert properties that hold for *all*
inputs:

- **Time reversal:** `propagate(propagate(s, dt), -dt) == s` to within tolerance.
  Already tested; it should be the template.
- **Composition:** `propagate(s, a + b)` agrees with
  `propagate(propagate(s, a), b)`.
- **Scale invariance:** a canonically rescaled system produces a rescaled answer.
  This one would have caught the 1 AU bug directly.
- **Conservation:** energy and angular momentum drift stay inside a budget.

**Why:** these are far stronger than example-based tests and they generate their
own cases. They also survive refactoring, because they constrain behaviour rather
than implementation.

### Rule 12. Seeded randomised sweeps, with the seed written down

Already done — seed `20260905` is in `test_orbit_scales.cpp` and a failure prints
the failing case's parameters. Keep this pattern for every new numerical
component, and keep printing the parameters: a failure you cannot reproduce is a
failure you cannot fix.

### Rule 13. Fuzzing

**Built and run, 2026-09-07.** `tests/fuzz_orbit.cpp` throws arbitrary bytes at
`elementsFromState`, `orbitInfo` and `propagate` under ASan and UBSan, and
asserts the one thing a fuzzer can know without knowing the right answer: a
reported failure is always acceptable, but a **success must not be NaN**.

```
cmake --preset linux-fuzz          # Linux/WSL: libFuzzer has no MSVC-ABI target
cmake --build build/linux-fuzz
./build/linux-fuzz/fuzz_orbit -max_total_time=240
```

It is not a CTest test and not part of `check`: a fuzzer has no natural exit, so
it is run deliberately with a time budget.

The guidelines predicted this would find the degenerate orbit nobody thought of.
**It found five distinct defects**, none reachable by any test anyone had
written, each in a few thousand executions:

1. A state whose components are all finite while `|r|` is not, because squaring
   overflows above about 1.3e154 — and `rmag > 0.0` is true of infinity, so the
   precondition waved it through. The elements came back claiming success with
   an infinite eccentricity and a NaN argument of periapsis.
2. A **radial trajectory** — velocity parallel to position, zero angular
   momentum, inclination computed as `acos(0/0)`. Not exotic at all: a probe
   released with no horizontal velocity falls straight down. Now
   `OrbitError::RectilinearOrbit`, reported rather than parameterised, because
   unlike a circular or equatorial orbit there is no canonical answer.
3. `mu` tiny relative to the state, so the division by it in the eccentricity
   vector overflows. This is the one that argued for **checking the answer**
   rather than adding a third input guard: guarding inputs would have meant
   inventing a smallest believable `mu`, and a threshold carrying a hidden
   scale is the exact defect this project already shipped once.
4. **`propagate`'s postcondition was an assertion**, so a Debug build *aborted
   the process* on user input rather than reporting it. That is the wrong half
   of ADR 0002's split, and no amount of reading had caught it.
5. `|h|` nonzero but `|h|^2` underflowing, so the semi-latus rectum is zero and
   `orbitInfo`'s radius becomes `0 / (1 + e·cos π)` = `0/0`. The ratio test from
   (2) cannot see this one, because it never squares anything.

After the fixes: **77.4 million executions, 241 seconds, zero findings** — the
same fuzzer that previously hit a defect within a few thousand runs.

**A sixth, on 2026-09-11, after 1.1 million executions**, where two earlier runs
that day had passed ten million each: a nearly radial hyperbola at a scale of
1e-158 m. Its `|r|^2` is subnormal, so `length()`, then `sqrt(dot)`, was off by
7e-9, and the eccentricity came out 1 - 7e-9 where it is 1 + 1.8e-13 — an
ellipse's eccentricity beside a hyperbola's semi-major axis, and `orbitInfo`
then a NaN period for an orbit it called closed. Fixed at the root: `length()`
is exact to 2 ulp at every scale now (`core/Math.hpp`), and bit-identical to
before wherever before was right. Afterwards: 22.6 million executions in 61
seconds, zero findings.

Highest value still to come is a **file parser** — a scenario loader, a DE440
reader, a DDS/KTX2 header parser — because those consume untrusted bytes and are
the classic memory-safety surface. The harness is there for them now.

### Rule 14. Differential testing between implementations

Where two implementations exist, run both on the same input and diff. This is
rule 2 mechanised, and it scales: Encke against Cowell, a truncated gravity field
against the full one, the f32 render path against the f64 physics path, this
project against GMAT on a shared scenario.

### Rule 15. Runtime invariant monitors, not just test-time ones

Assertions only run where tests reach. A long flight visits states no test
constructed. So the simulation itself should carry cheap continuous checks in
debug builds: energy drift per orbit inside budget, quaternion norm within
tolerance of 1, no NaN entering or leaving the integrator, step size within
expected bounds.

**Why:** this is how a slow divergence gets caught at minute three instead of
hour three.

### Rule 16. Determinism is a tested property

Run a scenario twice from the same initial state; assert bit-identical results.
This is the one place an exact floating-point comparison is correct, because
bit identity is the actual claim -- and it is made by name, with
`bitIdentical()`, which also tells +0.0 from -0.0. The value types have no
floating-point `==` to reach for
([ADR 0017](adr/0017-every-warning-is-an-error.md)).

It catches a whole class of accidental nondeterminism — iteration over an
unordered container, uninitialised padding, a branch on wall-clock time, an
adaptive step-size controller that reads the frame rate. All of those are
otherwise invisible until a replay diverges.

### Rule 17. Extend the type system to full dimensional analysis

The unit types stop a `Radians` reaching a `Seconds` parameter. They do not stop
`Metres / Seconds` being assigned to a `Metres`, because `Quantity` deliberately
has no dimension-changing arithmetic — the unit-changing operations are named
functions instead, which is the current, defensible answer (ADR 0001).

**Worth revisiting as the force model grows.** With accelerations, specific
angular momenta, gravitational parameters and moments of inertia all in play, a
compile-time dimension system (exponents of M, L, T as template parameters) makes
`mu / (r * r)` produce an acceleration *type* and makes adding it to a velocity a
compile error. That is rule 9 enforced by the compiler instead of by a reviewer.

This is a real cost and a real benefit; it belongs in an ADR, not in a commit.

### Rule 18. Coverage is a map of what has not been tested

**Measured, 2026-09-07.** Coverage is not a target to hit — 100% proves nothing
— but the *uncovered* lines are a list of code no test has ever executed, and
that list is worth reading after every feature.

| File | Lines | Regions |
|---|---|---|
| `orbit/Orbit.cpp` | **99.2%** | 86.0% |
| `orbit/Orbit.hpp` | 93.8% | 85.7% |
| `core/Units.hpp` | 75.0% | 90.9% |
| `core/Scalar.hpp` | 80.6% | 91.3% |
| `core/Math.hpp` | 50.6% | 64.1% |

`Orbit.cpp` has no unexecuted lines. `Math.hpp` is half covered because `Quat`
and most of `Vec3`'s operators are written and not yet used by anything — which
is the map doing its job rather than a gap to close: the 6-DOF work in
[`plan/realism.md`](plan/realism.md) is what will exercise them.

How to reproduce it, under WSL:

```
cmake -S . -B build/linux-cov -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_COMPILER=clang++ -DORBSIM_BUILD_APP=OFF \
      -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
      -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate"
cmake --build build/linux-cov && cd build/linux-cov
LLVM_PROFILE_FILE=a.profraw ./test_orbit && LLVM_PROFILE_FILE=b.profraw ./test_orbit_scales
llvm-profdata merge -sparse a.profraw b.profraw -o merged.profdata
llvm-cov report ./test_orbit_scales -object ./test_orbit \
    -instr-profile=merged.profdata ../../src/
```

### Rule 19. Mutation testing, occasionally

Deliberately break the code — flip a sign, change a constant, weaken a
comparison — and confirm a test fails. If none does, the suite has a hole exactly
there.

Expensive to automate, cheap to do by hand on the parts that matter most. Doing
it once on `Orbit.cpp` would put a number on how much the two-body suite is
actually worth.

### Rule 20. Run the Linux presets — the second compiler and UBSan are back

**Done on 2026-09-07.** `PROJECT_STATE.md` and ADR 0005 had recorded
UndefinedBehaviorSanitizer and a second compiler as *out of reach*. WSL 2 was
already enabled on this machine with no distribution installed, so the whole
cost was one command:

```
wsl --install -d Ubuntu --no-launch
```

Ubuntu 26.04 LTS, giving clang 21.1.8, gcc-14 14.3.0, cmake 4.2.3 and ninja
1.13.2 — *on that day*. The clang was replaced by LLVM's own build on
2026-09-08 so that one version spans both operating systems; the current
versions are in [`STATUS.md`](STATUS.md) and the recipe is in
[`PROJECT_STATE.md`](PROJECT_STATE.md) section 2. Both presets — written months ago, never once executed — configured,
built and passed on the first run:

| Preset | What it is | Result |
|---|---|---|
| `linux-sanitize` | clang Debug + ASan + UBSan, core only | 3,632 checks, 0 failures |
| `linux-gcc` | gcc 14 Debug, core only | 3,632 checks, 0 failures |

Both match the Windows counts exactly (732 + 2,900), so the two compilers and
the two platforms agree on every assertion in the suite.

**And the tooling itself was checked, because rule 23 applies to tools too.**
Tests passing under UBSan proves nothing if UBSan was never linked in — that is
precisely how the clang-tidy header filter went sixteen commits doing nothing.
So: `compile_commands.json` really carries `-fsanitize=address,undefined` and
`-fno-sanitize-recover=all`; the test binary really contains 808 sanitizer
symbols; `g++-14` really was gcc and not clang wearing its name; and a
deliberate signed overflow compiled the same way really does abort with
`runtime error: signed integer overflow`. The sanitizer is live, not nominal.

How to run them again, from Windows:

```
wsl -d Ubuntu -u root -- bash -c "cd /mnt/c/Users/U439644/Projects/untitled && \
    cmake --preset linux-sanitize && cmake --build build/linux-sanitize && \
    ctest --test-dir build/linux-sanitize --output-on-failure"
```

Worth doing before a milestone lands, and mandatory the day the physics is
threaded off the render loop — that is when ThreadSanitizer, also only
available here, stops being optional.

**It has already earned its keep twice, on the first two changes it saw.**

First: a near-rectilinear test passed under Windows clang and failed under
*both* gcc-14 and clang-on-Linux — same source, different libm. My first
response was to record it as a limit of the method and relax the test. **That
was the wrong call**, and the owner rejected it: a result that depends on which
library rounded a cosine is evidence of an unstable algorithm, not of a hard
limit. Chasing it properly found plain Newton oscillating wherever the equation
flattens, in the Kepler solver as well as the universal one — where it was far
worse, failing on 196 of 401 hyperbolic anomalies at `e = 1.0001`. All three
equations are now solved by one safeguarded Newton that cannot fail to
converge. See rule 1: the failing test was the signal, and editing it destroyed
exactly what the second compiler had been installed to buy.

Second, on the very next build: gcc-14 rejected `std::tie` without `<tuple>`.
clang's standard library pulls it in transitively and libstdc++ does not, so
the missing include was invisible on one platform and a hard error on the
other — an include-what-you-use defect that no amount of Windows testing would
ever have surfaced.

One platform tells you about your code. Two tell you about your *assumptions*.

This is entirely consistent with the no-CI decision: verification stays local
and is run by a person. What changed is that "a person" no longer needs a
second machine.

### Rule 21. Keep `check` as the single definition of done, and let it grow

`cmake --build <tree> --target check` in **both** trees is what "finished" means.
As the rules above land, they land *inside* that target — a property-test suite,
a Horizons-fixture suite, a determinism suite — so that following the process is
the same act as running one command.

The corollary, from the guidelines' Toolbox: **a tool you do not run is a tool
you do not have.** A rule in this document that is not reachable from `check` is
a good intention.

---

## Part 3 — The rules that are about people

### Rule 22. Write down why, at the moment you know why

Every non-obvious constant says where its value came from. Every decision that
spans files becomes an ADR. Both are already project practice and both are
verification tools: the comment at `Orbit.cpp:40` explaining why the tolerance is
relative is what stops someone "simplifying" it back into a bug.

### Rule 23. Distrust agreement

Two things agreeing is evidence only if they are independent. Two propagators
sharing a helper, two tests sharing a fixture that is itself wrong, a test
tolerance tuned until the test passed — each of those is agreement that proves
nothing.

When a test passes on the first try, ask what would have to be broken for it to
fail. If the answer is "nothing plausible", the test is decoration.

### Rule 24. Prefer the bug you cannot write

The whole of `CODING_GUIDELINES.md` in one line, and the reason it belongs in a
document about verification. A strong type, a factory, an `enum class`, a
`[[nodiscard]]`, an exhaustive `switch` — every one of them converts a test you
would have had to remember to write into a compile error you cannot avoid.

Testing is what you do about the bugs the type system could not prevent.

---

## Part 4 — What actually enforces what

ADR 0005 is blunt about this: **a checklist records intent, and cannot notice
that a step has been silently doing nothing for sixteen commits.** A rules
document has exactly that weakness, so here is the honest accounting of which
rules a machine checks and which depend on a person remembering.

| Rule | Enforced by | Status |
|---|---|---|
| 1 Test first | A person, visible in the diff | discipline |
| 2 Never check code against itself | A person, at review | discipline |
| 3 External truth | `check`, once Horizons fixtures exist | **to build** |
| 4 Error budget | `check` — the test asserts the number | partial |
| 5 Singularities | `check` — zero dt on every conic, near-rectilinear, e=0, i=0, i=pi, retrograde | **done** |
| 6 Regression test per bug | A person, visible in the diff | discipline |
| 7 Assert vs. report | `check` (clang-tidy, partially) + review | partial |
| 8 Bounded loops, reported | Review; `[[nodiscard]]` on `expected` helps | partial |
| 9 "In what?" | A person; rule 17 would mechanise it | discipline |
| 10 Small commits | `scripts/git-hooks/pre-commit`, partially | partial |
| 11 Property tests | `check` — reversal, composition, scale invariance, conservation | **done** |
| 12 Seeded sweeps | `check` — already live | **done** |
| 13 Fuzzing | `tests/fuzz_orbit.cpp`, run deliberately with a time budget | **done** |
| 14 Differential testing | `check` — already live for the two propagators | **done** |
| 15 Runtime monitors | `check` in the Debug tree, via assertions | **to build** |
| 16 Determinism | `check` — `testDeterminism`, bit-identical over 100 steps | **done** |
| 17 Dimensional analysis | The compiler, if adopted | undecided |
| 18 Coverage | By hand, periodically. Orbit.cpp 99.2% lines | **done** |
| 19 Mutation testing | By hand, periodically | exercised 2026-09-07 |
| 20 WSL, UBSan, second compiler | By hand, before a milestone | **done** |
| 21 `check` is the definition of done | The build, both trees | **done** |
| 22–24 The human rules | A person | discipline |

Rule 4 is *partial*: one error budget is genuinely derived rather than tuned --
the near-rectilinear round trip, whose tolerance is stated as the conditioning
law `ulp / (1-e)^2` it was measured to follow. What is missing is the external
half, which waits on rule 3.

Rule 5's orbital singularities are now covered -- `e = 0`, either side of
`e = 1`, `i = 0`, `i = pi`, retrograde, near-rectilinear to `e = 0.9999`, and a
zero time step on every conic. The attitude and rendering singularities have no
code to test yet and will need their own pass.

Rule 19 says "exercised" rather than "done" because mutation testing is an act,
not a state. It was run on 2026-09-07 against the two property tests added that
day, which is how they were shown not to be decoration: restoring the historical
absolute-tolerance bug made scale invariance fail in 6 places and composition in
4. It will need running again the next time a test passes on the first try.

Two things follow from this table, and they are the reason it exists.

**The rules marked "to build" are not yet rules.** They are intentions, and by
rule 21's own standard an intention is not a tool. Each becomes real when it
lands inside `check` — which means the work of adopting this document is
mostly the work of writing those suites, not the work of agreeing with it.

**The rules marked "discipline" will not be enforced by anything.** That is
accepted rather than solved: some of them cannot be mechanised (nothing can
check whether a test is independent of the code it tests), and the honest
response is to name them as the ones that need attention at review, rather than
to pretend the list is self-enforcing.

Three remain, and the order is the order they will catch something: **3
(Horizons fixtures), then the external half of 4 (error budgets against them),
then 15 (runtime monitors).** 3 and 4 are one piece of work really, and ADR 0006
makes every accuracy claim in the project depend on it. 15 waits on there being
a simulation loop to monitor, which is milestone 1 phase E.

Rule 11 was the first one built, on 2026-09-07, and it paid immediately: adding
the composition and scale-invariance properties is what turned rule 5 from a
list into `testZeroTimeStep`, which found that `propagate()` reported
non-convergence for a hyperbolic orbit at `dt = 0` — a bug that had been in the
tree since the propagator was written, that the elliptic case hid, and that a
fixed-step accumulator handing out a zero-length step would have hit on the
first paused frame of an escape trajectory.
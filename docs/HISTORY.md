# orbsim — change history

Kind: history
Binding: no
Read when: you need to know *why* something is the way it is, or you are doing
archaeology on a decision. Nothing here is required to perform a task.

Split out of [`PROJECT_STATE.md`](PROJECT_STATE.md) on 2026-09-09, verbatim and
with its original section numbers kept, so that references written against
"`PROJECT_STATE.md` section 3" or "section 4" still resolve to the same text.
That file now holds only what is true *now*; this one holds what happened.

## Contents

- [3. Change history](#3-change-history) — the sessions, in order, with the
  commits and what each one found
- [4. The bug that justified the session](#4-the-bug-that-justified-the-session)
  — `propagate()` at 1 AU, and the two numbers without units behind it
- [5. The 2026-09-07 amendment to the milestone 1 plan](#5-the-2026-09-07-amendment-to-the-milestone-1-plan)
  — what changed in milestone 1 when ADR 0006 and 0007 landed

Current state, versions and counts are in [`STATUS.md`](STATUS.md); the
decisions themselves are in [`adr/`](adr/).

---

## 3. Change history

All of it on `review-fixes-2026-09`, branched from `master`, which is untouched.

### The review-and-fix session, 2026-09-05

Eight commits. In order:

1. **`2f400cc` Make clang-tidy actually lint the headers, and enable the analyzer.**
   The header filter regex required a forward slash after `src`, but clang-tidy
   on Windows reports header paths as `src\core/Math.hpp`. No header in the
   tree had ever been linted; the "zero findings" claim covered the four `.cpp`
   files only. With the filter fixed there were 64 findings. Also added the
   `clang-analyzer-*`, `cert-*` and `concurrency-*` families, which a `Checks`
   list otherwise drops.
2. **`d5eca97` Build every strong type on one Quantity base, and use them in Math.hpp.**
   Nine near-identical structs collapse to a CRTP base in the new
   `core/Scalar.hpp`. Scalars now sit *below* the units, which is what lets
   rotations take `Radians` and `Seconds` instead of bare doubles.
3. **`1fca5ee` Make propagate() scale-free, refuse NaN by name, and test beyond Earth.**
   The real bug of the session. See section 4.
4. **`b7dbc18` Finish Rule of Zero in the renderer, check every VkResult, harden main.**
5. **`a73a54b` Format on edit and on commit.** Two hooks plus `LineEnding: LF`.
6. **`eb2a99b` Add CI.** Windows clang, Linux clang with ASan+UBSan, Linux gcc 14.
   **Superseded: the owner decided on 2026-09-06 not to use CI.** See
   [`PROJECT_STATE.md`](PROJECT_STATE.md) section 7.
7. **`1cae874` Record the decisions in docs/adr, and make CLAUDE.md describe the tools.**
8. **`651a5cc` Hold the guidelines example to the bar it claims.**

Two further commits closed that session: `307bc45`, recording that the project
does not use CI, and `92a94cc`, requiring the discrete GPU and deleting the
workflow. Everything this section once listed as uncommitted has landed.

### The realism and verification session, 2026-09-06 to 09-07

Four commits, all documentation. No C++ changed.

1. **`22a27ca` Rule that orbsim is a simulation, and write down what that
   costs.** ADR 0006, plus [`plan/realism.md`](plan/realism.md): the gap list
   between a two-body core and the physics that decision requires, ordered by
   structural risk. Its section 6 answers the fluency requirement — the visuals
   scale, the physics does not.
2. **`88ab370` Make the verification rules and the realism goal binding by
   default.** [`VERIFICATION.md`](VERIFICATION.md), 24 rules, wired into
   `CLAUDE.md` so it is loaded every session. Its Part 4 records which rules a
   machine actually enforces — three, at the time of writing.
3. **`85215cc` Make render quality a struct, and put it where the physics
   cannot reach it.** ADR 0007.
4. **`f8d465d` Run the Linux presets, and correct the cost ADR 0005 never
   measured.** See the end of [`PROJECT_STATE.md`](PROJECT_STATE.md) section 6.3.

A fifth commit amends the milestone 1 plan for all of the above and sweeps the
documentation for the inconsistencies that accumulated along the way.

Then the code was read against the documents, which found and fixed **six
defects in `src/orbit/`**, all of them in code that had passed 3,513 checks,
zero warnings and zero clang-tidy findings:

1. `propagate()` reported non-convergence for a **hyperbolic orbit at dt = 0**,
   because the hyperbolic starting guess takes `log(0)`. Found by writing the
   singularity test rule 5 asks for. A fixed-step accumulator emits zero-length
   steps, so phase E would have hit this on the first paused frame of an escape
   trajectory.
2. A state with finite components but an **infinite `|r|`**, because squaring
   overflows above 1.3e154 and `rmag > 0.0` is true of infinity.
3. A **radial trajectory** — zero angular momentum, inclination `acos(0/0)`.
   Now `OrbitError::RectilinearOrbit`.
4. `mu` tiny relative to the state, overflowing the eccentricity vector. Fixed
   by a postcondition on the answer rather than a fourth guard on the inputs.
5. **`propagate`'s postcondition was an assertion**, so a Debug build aborted
   the process on user input instead of reporting it — the wrong half of ADR
   0002's split.
6. `|h|` nonzero while `|h|^2` underflows, so the semi-latus rectum is zero and
   `orbitInfo`'s radius is `0/0`.

Numbers 2 to 6 were found by the fuzzer, in a few thousand executions each.
After the fixes it ran 77.4 million executions clean. See
[`VERIFICATION.md`](VERIFICATION.md) rule 13.

Then a seventh, which is the one worth reading, because it was nearly recorded
as a limitation instead of fixed.

A near-rectilinear test passed under Windows clang and failed under gcc-14 and
clang-on-Linux. I relaxed the test and wrote the failure up as a property of
the method. **The owner rejected that outright** — a result that depends on
which library rounded a cosine is evidence of an unstable algorithm — and the
instruction is now recorded where it will actually be found, in `CLAUDE.md`
under "Working agreements": never change a test or take a design decision
without an explicit go-ahead.

Chasing it properly: plain Newton oscillates wherever the equation's slope
collapses. The step is `residual / slope`, the slope is a radius near periapsis
for the universal variable and `1 - e cos E` for Kepler's, and both go to zero
as `e` approaches 1. The Kepler solver was much the worse of the two —
**196 of 401 hyperbolic anomalies failed at `e = 1.0001`**, which is most
near-parabolic escape trajectories, and nothing in the suite had ever asked.

All three equations in `src/orbit/` are strictly monotonic, so each root is
unique and can always be bracketed. They now share one safeguarded solver:
Newton where its step stays inside the bracket *and* at least halves,
bisection where it does not. The halving condition is the part that matters —
for the hyperbolic Kepler equation at large `H` the Newton step is about 1
regardless of distance, so it creeps rather than diverges, and a bracket test
alone never fires.

Measured after: zero failures across every eccentricity from 0 to 100, elliptic
and hyperbolic, with accuracy at machine epsilon; the near-rectilinear round
trip tracks the conic's conditioning law across six decades. `check` passes in
both Windows trees, both Linux presets pass, and the fuzzer ran 99.9 million
executions clean.

The second compiler earned its keep twice more in the same session: it found
the instability above, and then rejected `std::tie` without `<tuple>` — which
clang's standard library pulls in transitively and libstdc++ does not.

The branch is pushed to `origin/review-fixes-2026-09`.

### The clang 23 upgrade, 2026-09-08

clang went 22.1.8 -> 23.1.0 on this machine. **The C++ needed nothing**: fresh
trees built the core, both suites, the header self-checks and the full Vulkan
app with zero warnings under the whole `-Werror` set, and all 3,632 checks
passed first time. `clang-format` 23 wanted no changes either.

**clang-tidy 23 was the whole of it: 42 findings where 22 gave 0.** The cause
is structural rather than local -- `.clang-tidy` lists check *families* with
`WarningsAsErrors: '*'`, so the 25 checks new in LLVM 23 enrolled themselves as
build-breaking errors. Five fired. That is the config working as designed, and
worth keeping; it is also worth knowing that every clang upgrade is now a small
triage.

The owner then ruled that the suppression list itself should be emptied and
everything that came out of it fixed rather than re-suppressed. Emptying all
fourteen entries produced **1,476 findings**. Of those, 420 were fixed and the
list is down to four entries:

| Was suppressed | Findings | Outcome |
|---|---|---|
| `misc-include-cleaner` | 256 | Fixed. Worth the churn on its own: this is the check that would have caught the missing `<tuple>` only gcc found |
| `readability-braces-around-statements` | 114 | `ShortStatementLines: 1` encodes the house style; 3 real findings remained and were fixed |
| `performance-enum-size` | 9 | 5 enums given an explicit `std::uint8_t` base |
| `cppcoreguidelines-pro-bounds-pointer-arithmetic` | 5 | `argv` is a `std::span`, the SDL extension list is iterated, `from_chars` takes `std::to_address(text.end())` |
| `modernize-use-nodiscard` | 0 | Dead. Removed |
| `bugprone-narrowing-conversions` (+ CG alias) | 0 | Dead. Removed |
| `cppcoreguidelines-pro-bounds-array-to-pointer-decay` | 0 | Dead. Removed |
| `cppcoreguidelines-pro-type-vararg` | 3 | **Check re-enabled.** `SDL_Log` is variadic because SDL's C API is; a `NOLINTNEXTLINE` with a reason sits on each of the 3 calls |
| `cppcoreguidelines-pro-type-reinterpret-cast` | 2 sites | **Check re-enabled.** Both are the C API's own requirement -- the typed Vulkan entry point, and `istream::read` accepting only `char*` -- and carry a site `NOLINTNEXTLINE` |
| `readability-identifier-length` | 408 (219 lines) | **Still open** |
| `modernize-use-trailing-return-type` | 357 (172 lines) | **Still open** |
| `*-magic-numbers` | 277 (151 lines) | **Still open** |

The two `pro-type-*` entries came off the list on the principle in
[`PROJECT_STATE.md`](PROJECT_STATE.md) section 6.1:
suppress at the site, not in the config, because a config suppression silently
covers whatever gets written next while five `NOLINT`s cover five lines. The
three that remain are open because none of them has a site-local answer that is
cheaper than the disease -- see the counts in `.clang-tidy`, which quotes both
the raw finding count and the number of distinct lines a `NOLINT` per site
would touch.

Plus the five new checks, of which four were fixed outright
(`readability-trailing-comma` 32, `bugprone-signed-bitwise` 4,
`readability-redundant-lambda-parameter-list` 1, `misc-const-correctness` 1) and
one is an upstream false positive, worked around by naming a constant.

The same treatment ran over `coding-guidelines-example/`: ten suppressions down
to four, 0 findings, 47 checks, format clean.

**Both toolchains were then unified on 23.1.** WSL was on Ubuntu's clang 21.1.8;
it is now on LLVM's own 23.1.1 from apt.llvm.org, clang-21 removed, bare names
pointed at 23 through `update-alternatives`. The recipe is in
[`PROJECT_STATE.md`](PROJECT_STATE.md) section 2. This
matters for more than tidiness: with `misc-include-cleaner` newly enabled, the
question is whether the includes it asked for are right on libstdc++ as well as
on the MSVC STL -- the check's own documentation warns it disagrees with itself
across implementations. **It does not here: clang-tidy 23.1.1 on libstdc++
reports zero findings, and clang-format 23.1.1 agrees with the Windows one
byte for byte.** `linux-sanitize` (3,632 checks, ASan and UBSan both confirmed
linked rather than merely configured), `linux-gcc` (3,632, gcc-14 untouched --
it is the second *implementation*, and upgrading clang does not weaken that) and
`linux-fuzz` (29.7 million executions clean in 91 s) all pass.

Two changes are worth knowing about because they are not cosmetic.
`readability-trailing-comma` interacts with `clang-format`: a trailing comma
makes it break a braced list one element per line, which inflated the test case
tables by about 200 lines and pushed `testRandomSweep` past the 80-line
function-size threshold. It is now two functions, `sweepClosedOrbits` and
`sweepHyperbolicOrbits`, sharing one `Sampler`; **they draw from it in that
order, and reordering the two calls changes every case in the sweep.** The
suite still reports 2,900 checks from seed 20260905, which is the evidence the
split was behaviour-preserving.

### Milestone 1 begins: M1-01, the move to Catch2, 2026-09-09

The hand-rolled harness was right for one file and wrong for the ten this
milestone adds, so both suites moved to **Catch2 v3.16.0** before any of the
new ones are written. The task changed no assertion, no tolerance and no case,
and **the assertion count is the evidence**: 732 and 2900 before, 732 and 2900
after, on Windows clang RelWithDebInfo and Debug, under ASan, and under both
Linux presets. `catch_discover_tests` gives each `TEST_CASE` its own CTest
entry, so `ctest -N` lists twenty: the nineteen Catch2 cases plus
`orbsim_smoke`. (This section said "nineteen" until the list was actually read
on 2026-09-09; the count lives in [`STATUS.md`](STATUS.md).)

Four things from it are worth keeping.

**Three custom matchers, because Catch2's own take bare doubles.**
`WithinAbs(5.0, 5554.0)` accepts 5554 exactly as `WithinAbs(5554.0, 5.0)` does,
since |5554 - 5| <= 5554 -- the silent transposition non-negotiable 1 forbids.
`WithinAbsOf`, `WithinRelTo` and `WithinRelVec` in `tests/OrbitTestSupport.hpp`
take a `Tolerance`, so the transposed call does not compile, and they reproduce
the old harness's predicates exactly rather than approximately: Catch2's
`WithinRel` divides by max(|got|, |want|) where this project divides by |want|.

**`INFO(describe(result.error()))` is undefined behaviour.**
`std::expected::error()` has the precondition that the expected holds no value,
and `INFO` evaluates its argument eagerly on every call, not only on failure.
The Debug tree asserts. `errorName()` guards it and returns "(succeeded)".

**Nine `NOLINTNEXTLINE` suppressions, ruled by the owner after the
alternatives were measured.** `readability-function-cognitive-complexity`
scores a `TEST_CASE` at roughly three points per assertion because `REQUIRE`
expands to a do-while wrapping a try/catch; the cases suppressed have no `if`
of their own. The check's `IgnoreMacros` option would have cleared all of them
project-wide at a measured cost of two points on one `src/` function
(`meanToEccentricAnomaly`, 21 -> 19, against a threshold of 25), and a
`NOLINTBEGIN`/`NOLINTEND` region would have taken one line per file. Both were
rejected in favour of one suppression per function, because that is the only
one of the three that still reports a genuinely over-complex helper added to
those files later.

**Catch2 needs no ASan annotation workaround, and finding that out took three
attempts.** Assuming it did -- it is built without the sanitizer and uses
`std::string` throughout, which is exactly what breaks vk-bootstrap -- the
guard went in first. The first two experiments to check whether it was
load-bearing both said "not needed", and both were false: Ninja had not
rebuilt a single Catch2 object either time. Forcing all 108 to rebuild each
way showed it genuinely is not needed, because the `_DISABLE_*_ANNOTATION`
definitions `orbsim_sanitizers` already applies to *our* instrumented
translation units make the MSVC STL emit the same `detect_mismatch` value an
uninstrumented library emits. The guard was deleted and the reasoning left in
its place. The lesson is the general one: **a build experiment that does not
say how much it rebuilt has not been run.**
### M1-02, the eight records, and the audit that preceded it, 2026-09-09

Twenty-six decisions had been taken on 2026-09-08, before the milestone
started; six were scheduled to become ADRs and **eight did**. Decisions 16 (the
radiometric chain) and 18 (quadtree LOD) span files exactly as the other six
do, and the owner ruled that they be written now rather than when the code that
depends on them arrives — which is the whole argument of the task: *an ADR
written afterwards is a justification.*

**The audit that opened the task is the part worth keeping.** All 112 documents
were read against each other before a line of M1-02 was written, on the grounds
that a task whose entire product is prose should not be built on prose that
disagrees with itself. The task queue came out clean — all 84 task documents
agree with the queue on phase, prerequisites and filename, checked by script —
and about twenty other things did not. The full list is in the commit *Make the
documents agree with the decisions that were taken*; two of them generalise.

**A correction recorded in one place is not a correction.** On 2026-09-08 the
register wrote down that phase E's acceptance criterion "was not achievable as
written", because JPL Horizons cannot propagate a hypothetical satellite under
a J2-only force model, and that GMAT replaces it. On 2026-09-09 the milestone
plan still asked for Horizons, in two places, and so did this file. The
register had done its job perfectly and nothing had propagated. That is the
argument for section 8 of the register, which now maps every decision to the
document that carries it, and for the `Decided by:` line that 71 task documents
gained.

**`doc-links` cannot see a backticked path.** `.claude/rules/physics-tests.md`
still sent readers to `tests/TestHarness.hpp`, which M1-01 had deleted — in a
file the harness loads automatically whenever a test is touched. The check that
exists precisely to catch a document naming something that is not there could
not see it, because the reference was in backticks rather than in a link. The
script's own docstring describes this failure mode; it was one level outside
its reach.

---

## 4. The bug that justified the session

`propagate()` returned `SolverDidNotConverge` for a plain circular orbit at
1 AU, and had done so since the code was written.

Two independent causes, both of them a number without a unit:

- The Newton convergence test compared the step against an **absolute** `1e-10`.
  The universal anomaly χ has units of √metres, and one revolution is
  2π√a — about 1.7e4 in low Earth orbit but 2.4e6 at 1 AU, where the rounding
  noise of the update alone exceeds 1e-10. The test is now relative to |χ|,
  floored at √r₀ so a zero time step still converges.
- The conic thresholds tested `alpha > 1e-12`, and α = 1/a has units of 1/m.
  That declared every orbit wider than 1e12 m parabolic — inside Jupiter's
  distance from the Sun. `alpha * r0` is dimensionless and separates the conics
  at every scale.

**Neither was visible to a test suite built only from Earth orbits**, which is
why `test_orbit_scales.cpp` exists and why `CLAUDE.md` now asks for a case at
every scale the simulator flies. The general lesson, worth keeping: *ask "in
what?" of every bare number.*

Two new `OrbitError` values came out of the same work. `NotFinite` is reported
before any arithmetic, because a NaN used to arrive at Newton and leave as "did
not converge" — true, and it sends the reader to the solver instead of the
scenario file. `ParabolicElements` is what `propagateElements` says instead of
handing an infinite semi-major axis to the Kepler solver.

---

## 5. The 2026-09-07 amendment to the milestone 1 plan

Moved here from `CLAUDE.md` on 2026-09-09, where it had been kept as a
safety net against a stale copy of the plan. The plan itself
([`plan/milestone-1-earth.md`](plan/milestone-1-earth.md)) was amended in
place and is the source; [`plan/realism.md`](plan/realism.md) section 5 has
the reasoning. The four changes, in case a stale copy is in front of you:

- **Phase A also builds the linear HDR pipeline, the `RenderQuality` plumbing,
  and the time system.** The time system is there rather than in phase E
  because B, D and C all come first, and it is cheapest at zero call sites.
- **Phase C also does elevation**, rather than deferring it to milestone 2.
- **Phase E is the integrator, not a `propagate()` loop**, and its acceptance
  criterion gains an error budget against JPL Horizons. *(Corrected
  2026-09-08: the reference is a NASA GMAT trajectory. Horizons cannot
  propagate a hypothetical satellite under a J2-only force model; it keeps the
  Sun, Moon and Earth positions and the time scales. See
  [the register](plan/milestone-1-decisions.md), decision 4.)*
- **Phase F's criterion inverted.** The orbit track must *precess* at the J2
  rate, not stay put. A track that stays put is now a failing test, and the
  old wording would send someone hunting a bug that is the physics working.

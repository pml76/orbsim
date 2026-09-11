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

**`doc-links` could not see a backticked path**, and now it can.
`.claude/rules/physics-tests.md` still sent readers to `tests/TestHarness.hpp`,
which M1-01 had deleted — in a file the harness loads automatically whenever a
test is touched. The check that exists precisely to catch a document naming
something that is not there could not see it, because the reference was in
backticks rather than in a link. The script's own docstring described that
failure mode, and it was one level outside its reach.

**It was closed the same day**, deliberately as its own change: the script now
also resolves backticked repository paths and `docs/adr/NNNN` record numbers.
Three rules keep it honest, each costing coverage on purpose — only paths
carrying a file extension, so a directory decided but not yet created is not an
error; only paths whose first segment is a real top-level directory, so a
dependency's header is nobody's promise; and task documents are exempt, because
naming a file the task will create is their whole job. Two paths are named on
purpose although they are absent, both `tests/TestHarness.hpp`, each listed in
the script with its reason. It was shown to fail on a missing file and on a
wrong ADR number, and to stay silent on an existing file, a dependency header,
a future directory and a path inside a fenced block, before it was trusted.

### M1-03, `TimePoint`, and the representation the task could not use, 2026-09-10

The first task under the working agreement of 2026-09-09 -- every question up
front, every fact measured -- and the measuring changed the task before a line
of it was written.

**The task's own representation could not meet the task's own budget.** It
asked for an f64 fraction of a day and for a million additions of 1 us to
drift by under 1 ns. A spike in exact rational arithmetic measured **82.7 ns**,
systematic rather than random: 1 us has no binary representation, and every
addition rounds it the same way. From a fraction of zero the same run drifts
6 ps -- and in the task's noon-based day, zero is exactly J2000, where the
obvious test would have started. It would have passed, and been decoration
(`VERIFICATION.md` rule 23). The owner ruled for **integer picoseconds within a
day that begins at midnight** -- a Modified Julian Day, because ERFA divides
its UTC days at midnight for the reason M1-04 will need: the leap second comes
at the end of a civil day. ADR 0009 carries the update; the decision itself
stands.

Eleven questions went to the owner in one message, each with a
recommendation, and all eleven were taken as recommended: the storage, the day
boundary, a non-finite duration as an asserted precondition that a Release
build turns into a NaN day rather than undefined behaviour, one error per
calendar field, years 1-9999, a two-part Julian-date interface, the scales of
the epoch constants, no default constructor, two tests beyond the task's list,
the complexity suppressions, and leaving the rule of engagement where
`CLAUDE.md` already has it.

**Every expected value comes from outside the header**: the published epochs
from the US Naval Observatory, the calendar from `std::chrono`'s -- Howard
Hinnant's algorithm, a different formulation from Fliegel and Van Flandern's --
on all 73,414 days of 1900-2100, and the Julian-date conversions from Python's
exact fractions. The suite was seen to fail against a stub first; 17 of its 20
cases failed and 3 passed, because the stub collapsed every instant into one.
**Eight deliberate mutations were then all caught**, and each of those three
cases failed under the mutation aimed at it.

Two things worth keeping.

**A suppression written before the check has run is a guess.** Fourteen
`NOLINTNEXTLINE(readability-function-cognitive-complexity)` lines went in with
the suite, under a comment saying "only where the check fires". Running the
check over a copy with them stripped found five cases over the threshold. The
other nine were deleted; the comment is true now, and was not.

**clang-tidy 23 is wrong in three more ways on this code**, each reproduced on
a probe with the real clang-tidy rather than taken from the editor, and each
answered by writing the code differently: a requires-clause whose parentheses
it calls redundant and which does not compile without them; a De Morgan
rewrite that changes what `!(v > -k && v < k)` means for NaN; and C-style
casts reported where clang itself made the casts, substituting an enumerator
into a template. They are in `PROJECT_STATE.md` section 8 with the others.

**And the second compiler found one on its first look**, as it did with the
missing `<tuple>`. gcc-14 refused to build a designated initializer that
stopped at the day or the hour, because `CalendarDate::second` had no
initializer of its own -- clang files that case under a warning the build
switches off -- and the obvious `Seconds second{};` is exactly what
`readability-redundant-member-init` removed from `Elements` on 2026-09-08.
`Seconds second{0.0}` satisfies both, which a probe confirmed before it was
believed. Everything then agreed: 424,381 assertions -- the 3,632 from before
and 420,749 new -- in both Windows trees, under ASan, under `linux-sanitize`
and under `linux-gcc`.

### ERFA, and two errors the plan for M1-07 carried, 2026-09-11

A question about ERFA's licence became a decision about what computes the
astronomy. ERFA is BSD-3-Clause and copying it would have been lawful; the
owner's instinct was to copy it or call it. **Calling it won** (ADR 0016,
decisions 27-29): fetched and pinned like every other dependency, built
unedited, its own 1,494-check validation run inside `check`, and reached only
through typed wrappers in `src/astro/`. Before that was recommended it was
measured: all 249 library files compile with **zero warnings** under this
project's strictest flags on Windows clang 23.1.0, WSL clang 23.1.1 and gcc-14,
and the validation passes on all three. It buys 3 ns for TDB - TT where the
plan had a 100 us budget, 0.016" for the Sun where it had 0.01°, and nutation,
which had been deferred as 1,365 terms to transcribe. The leap seconds stay
ours, exact and reporting, because `eraDat` extrapolates past its table.

**Rewriting M1-07 around ERFA found two errors in it, both by computing the
answer.** The plan composed IAU 2006 precession in its equinox-based
Fukushima-Williams form with the Earth rotation angle, which is measured from
a different origin: **1231" -- 0.342°, about 38 km at the equator -- out**, and
growing by 46" a year. And it tested the rotation rate against the sidereal
day, 86 164.0905 s, where ERA's period is the stellar day, 86 164.098 903 691 s:
a correct implementation would have failed that test by 8.4 ms against a
0.1 ms tolerance. The first would have put the Earth's texture and every ground
track a third of a degree out, plausibly, and passed every test the task
listed; the second would have failed a correct implementation, inviting
somebody to break the code until it passed. Neither is visible by reading, and
both took a few lines of C against ERFA to see. That is working agreement 1's
"measure rather than assume" applied to a plan rather than to code, and it is
the second time in two days a plan has been wrong where only a measurement
could say so.

The frame's model error falls from <= 40" to **<= 14.1"**, and both halves of
that were measured rather than quoted: 13.5" from dUT1 = 0, and 0.6" from
polar motion -- the largest pole excursion in the IERS EOP 20 C04 series,
18.6 m in 1996, read from the series itself. The Sun's budget tightens from
0.01° and 2e-4 AU to 0.1" and 1e-6 AU, which is why the Horizons fixture must
now be geometric: aberration alone is 20.5".

Two more rulings followed the same day. **M1-06 runs before M1-05** (decision
30): M1-05's reference values arrive through M1-06's reader, which the queue
had placed after it -- an ordering fault present since the plan was written,
seen only when M1-05 was rewritten. And **the Sun outside ERFA's span is
reported by name** (decision 31). Pinning down that span from ERFA's code,
rather than its prose, found that "1900-2100" is 100 Julian years either side
of J2000 and ends on 2100-01-01T12:00 -- and that the boundary test first
written for it, a picosecond outside, would have failed a correct
implementation, because ERFA's own date arithmetic resolves only 0.63 µs
there. Measured, it reports a picosecond outside as inside and a millisecond
outside as outside; the test now uses the millisecond.

### Every warning, as an error, 2026-09-11

The owner's rule: *as many warnings as possible, as errors, to rule out old
and bad style -- and where one is raised by a library's interface, off at that
site, with the reason written down.* It replaced a hand-picked list of fifteen
with clang's `-Weverything` and, since gcc has no such switch, every warning
gcc-14 lists for C++, generated by `scripts/gcc-warnings.py` (ADR 0017). Over
our sixteen translation units `-Weverything` reported **2,818 distinct
warnings, 2,705 of them the backward-compatibility groups**, which report
every feature newer than C++98 and were ruled out first, with `-Wswitch-default`
(which contradicts `-Wcovered-switch-default`) and `-Wpadded`.

**What was left was worth having.** The best of it: **every unit type, `Vec3`
and `Quat` handed out a floating-point `==`**, because a defaulted `<=>`
brings a defaulted `==` with it, and nothing on the old list looked. `==` is
now deleted from `Quantity` and absent from the vectors. Exact results are
compared with `nearlyEqual` at a zero tolerance, and the determinism test says
what it means with `bitIdentical()`, which also tells +0.0 from -0.0.
`TimePoint` compares its day with `std::strong_order`. That is the numerical
order only if no day is -0.0, and it was proved rather than hoped: every day
passes through `carry()`, which adds a converted integer, and a sum is -0.0
only when both of its terms are. gcc reported three `== 0.0` tests in the
propagator, now `std::fpclassify`, which agrees with them on every input.
Beyond that: sixteen missing `[[clang::lifetimebound]]` on the Vulkan handle
wrappers, a copy the compiler could not elide, and three missing final newlines.

The new `static_assert`s were mutation-tested: seven mutants each drop one
component from a `bitIdentical()`, one compares a `Quantity` as numbers, and
one gives `Quantity` its `==` back. All nine fail to compile, each on the
assertion aimed at it. A tenth mutant, a `TimePoint` that ignores its day, is
not visible at compile time, and `test_time` catches it at runtime. **The
first run of that harness reported every mutant killed, and was worthless**:
`-Weverything` includes `-Wshadow-header`, which rejected the shadowing
include directory itself, so the unmutated copy failed too. Compiling the
baseline first is what showed it.

The eleven warnings raised by library interfaces in the renderer and the
application were answered with local pragmas, seven regions, each with its
reason. Vulkan's structs were answered the same way: the warning for
designated initializers that stop early had been off for the whole project,
and is now off for `render/VulkanContext.cpp` alone.

**Linux found what Windows could not.** `-Wweak-vtables` exists only on the
Itanium ABI. The test matchers were polymorphic and entirely inline, so every
suite emitted their vtables. Their `describe()` moved to a new
`tests/OrbitTestSupport.cpp`, the key function, and the test support became a
library. gcc's `-Wabi-tag` reported the `std::string` that Catch2's
customization points return. Put to the owner, with a recommendation to
exclude it next to `-Wabi`, it was ruled a library interface: a pragma at each
site, guarded for gcc, because clang rejects the name. The fuzz target needed
three answers of its own, and turned out never to have been linted at all --
13 findings, left for a follow-up commit by the owner's decision.

Everything agreed afterwards: the same 424,381 assertions in both Windows
trees, under ASan, under `linux-sanitize` and under `linux-gcc`, and
10,278,807 fuzz runs in 61 s with nothing found.

**The worked example followed the same day, and taught something false until
it did.** Its comments called the defaulted `==` on `Vec3` and `OrbitPath`
bit-exact, and its README called `==` "the only correct operator" for bit
identity. A defaulted `==` on doubles is numeric equality, which calls +0.0
and -0.0 equal. The example now says `bitIdentical()`, and its strong types
delete the `==` their `<=>` would bring. It had only ever been built with
clang. gcc-14 built it, measured first, and the full list found three
lambdas that could not throw and did not say so, and one braced `std::array`
that leaned on brace elision; both were fixed. It also found that gcc reports
`[[clang::lifetimebound]]` as an ignored attribute wherever it is written,
with its default flags too; the owner ruled for a gcc-only pragma at the
site. The example keeps its 47 checks under
both compilers, one of them rewritten because it tested the `==` that is gone;
six mutants of its new compile-time proofs fail to compile, each on its own
assertion.

**Then the fuzz target came under lint, and the fuzzer found its first bug.**
`tests/fuzz_orbit.cpp` had never been linted -- it was missing from the list,
and Windows never compiled it. Every tree now compiles it into an object
library that nothing links, so `check` lints it (a planted `raw[3]` was
seen to fail the lint target). Its 13 findings were fixed: the missing
includes, two trailing commas, and eight indexed reads that became a
structured binding. The entry point is now declared before it is defined,
which answers clang's `-Wmissing-prototypes` and gcc's `-Wmissing-declarations`
alike and retires a pragma.

The verification run of `linux-fuzz` then stopped after 1,132,833 inputs: for
a nearly radial hyperbolic state -- position about 1e-158 m, velocity about
9e61 m/s, mu 4.3e-35 -- `elementsFromState` returned a negative semi-major
axis with an eccentricity just below 1, and `orbitInfo` a NaN period for an
orbit it called closed. Earlier runs the same day had passed 10 million inputs
twice; fuzzing is a search, and this one got further. The bug predates the
day: it reproduces with the whole tree at `23690d9`.

And the reason given for fuzzing only in WSL -- "clang's libFuzzer does not
target the MSVC ABI" -- turned out to be false when measured. The Windows LLVM
23.1.0 ships the libFuzzer runtime, and `fuzz_orbit` builds there with
libFuzzer, ASan and UBSan, runs 3.8 million inputs in 30 s, and reproduces
the crash. The owner ruled for both: the bug fixed before M1-04, test first,
and fuzzing enabled on Windows beside `linux-fuzz`.

**The bug was not in the orbit code.** The regression test came first and
failed as the fuzzer had. Measuring then found the cause one layer down:
`|r|` was 1.25e-158 m, its square 1.6e-316 is subnormal, and `length()` --
`sqrt(dot(v, v))` -- was off by 6.98e-9, exactly the error in the
eccentricity. The true eccentricity is 1 + 1.8e-13; a first estimate of
1 + 1.8e-11, made by hand from the wrong angular momentum, was corrected by
computing it. Three fixes were measured before one was recommended: refusing
such states, `std::hypot`, which shifts 3.5% of all lengths by an ulp and costs
two to eight times as much, and a guarded `length()`: today's `sqrt(dot)`
wherever the sum of squares is at least 2^-969, an exact power-of-two rescaling
outside it. The owner chose the third. It is bit-identical to before on 5
million normal-range vectors on both toolchains, costs at most 0.7 ns more per
call, and is exact on (3, 4, 12) * 2^k at all 2,095 binary scales, where the
old one failed at 1,049.
Writing it caught a flaw in its own measurement: scaling by `scalbn(1.0, -k)`
overflows when the largest component is subnormal, the first probe's reference
shared the flaw, and so the all-subnormal vectors had been skipped rather than
measured. Scaling each component fixed both; three mutants of the new code are
each killed by the tests written for it.

Fixing the length exposed a second, older limitation, which the owner made a
task of its own: with its eccentricity now right, 1 + 1.8e-13, the state falls
inside the band `|e - 1| <= 1e-9` that the code calls parabolic, and
`orbitInfo` reports energy 0 for an orbit whose energy is 5.1e123 J/kg. On a
nearly radial trajectory e is near 1 whatever the energy.

**That band turned out to misjudge ordinary states too, and it went.** The
regression tests came first, and the two that mattered were not the fuzzer's:
a probe 7000 km from Earth drifting sideways at 1 mm/s -- on a closed ellipse
with a period of 2061 s -- was reported as a parabola, open, energy 0; leaving
at 20 km/s, a hyperbola, likewise. Four prototypes were measured against
60-digit references over 300,000 states before the owner chose. Today's band
had the closed flag wrong for 42% of 50,000 nearly radial states, e on the
wrong side of 1 for 9,774 of them, and the energy a median 100% out. The fix
asks one question everywhere, the one `propagate()` already asked: the conic is
the energy's, parabolic iff |alpha r| <= 1e-12, and `sma` says which. That left
no conic misjudged, and on the nearly radial states the semi-major axis and
energy within 4.1e-11 and the period within 6.1e-11. The classifier alone left
all 200,000 ordinary orbits bit-identical; `propagate()` is bit-identical too,
though it now asks through the same small function. Near radial, e is exactly
1.0 as a double -- a probe falling at 100 m/s with a 1 um/s drift, e = 1 -
1.8e-20 -- so it is now kept on the side of 1 the energy says. And the
apoapsis became a(1 + e): p / (1 - e) divided by a 1 - e known only to its last
bits, and had been up to 506% out on nearly radial ellipses and 3.6e-10 on
138,754 ordinary ones, where a(1 + e) is within 4.1e-11 and 2.7e-12. That one
change does move the apoapsis of 90% of ordinary closed orbits by a few ulp,
four in five of them toward the reference, and the owner chose it knowing the
bits would move. The figures first put to the owner, 5.2e-11 and 8.8e-13, came
from a sample of 20,000; the full run above found both formulas' worst cases
larger and the case for the change stronger. Five mutants of the fix are each
killed by the tests aimed at it.

The measurement found two more things, each made a task of its own by the
owner rather than folded in: `orbitInfo`'s radius and speed, computed from p,
e and the true anomaly, are ill-conditioned near the radial limit whatever the
classifier (median 0.14% out, unbounded at worst); and `propagateElements`
keeps its refusal within 1e-9 of e = 1 -- without it the error reached
2,500% -- but is already up to 3.5% out just outside it, falling as
1 / |e - 1|, where `propagate()` answers accurately.

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

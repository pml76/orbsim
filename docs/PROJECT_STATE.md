# orbsim — project state and handoff

Last updated: 2026-09-08. Sections 1 to 4 describe the review-and-fix session of
2026-09-05; sections 5 to 8 have been kept current since. What happened after
that session: the owner ruled that this is a simulation rather than a sandbox
(ADR 0006), render quality became a struct that the physics cannot reach
(ADR 0007), `VERIFICATION.md` was adopted as binding, the milestone 1 plan was
amended for all of it, and WSL was installed so the Linux presets finally ran.

This file exists so the project can be picked up on a different machine, or
after a gap, without reconstructing anything from memory. It records what is
built, what was decided, what is *not* decided, and what the machine needs.
[`CLAUDE.md`](../CLAUDE.md) says how to work here; this says where things
stand.

---

## 1. Where the project is

**Milestone 0 is complete: a tested two-body core and a Vulkan renderer that
opens a window and paces frames.** Nothing is drawn yet.
[Milestone 1](plan/milestone-1-earth.md) — Earth, orbit track, Orbit MFD — is
planned and **not started**. Its phase A (render foundations: pipelines, a
camera with camera-relative rendering, a line renderer) is the next code to
write.

| Component | State |
|---|---|
| `src/core/` | Scalars, strong unit types, Vec3, Quat. Complete for what exists. |
| `src/orbit/` | Two-body: state↔elements, universal-variable and element propagation, anomaly conversions. Correct at every scale from lunar to outer-solar-system. |
| `src/render/` | Vulkan 1.3 device, swapchain, frame pacing, RAII handles, buffer upload, shader loading. **No pipelines, no drawing.** |
| `src/app/` | Window, event loop, argument parsing, frame loop. |
| `shaders/` | Four GLSL shaders compile to SPIR-V at build time and are **never loaded**. They are placeholders for phase A. |
| `tests/` | Two suites, 3,632 checks, plus a GPU smoke test and a libFuzzer target. |

### Test suites

| Suite | Checks | What it covers |
|---|---|---|
| `test_orbit` | 732 | Earth-orbit round trips, degenerate orbits, analytic values, propagator agreement, invariants, hyperbolic, Kepler solver, reported failures |
| `test_orbit_scales` | 2900 | Heliocentric circles, parabolic trajectories, non-finite inputs, states that are finite but are not orbits, states with no orbital plane, a zero time step on every conic, near-rectilinear orbits, propagation composing, canonical scale invariance, bit-identical determinism, and a seeded sweep of 200 closed + 100 hyperbolic orbits around the Moon, Earth, Jupiter and the Sun |
| `fuzz_orbit` | — | libFuzzer over the core under ASan and UBSan. Not a CTest test: run deliberately with a time budget, `cmake --preset linux-fuzz`. |
| `orbsim_smoke` | — | Runs the app under the Vulkan validation layers for 2 s; fails on any validation error. Labelled `gpu`. |

The seed for the random sweep is `20260905` and is written into
`tests/test_orbit_scales.cpp`. A failure prints the failing case's parameters.

---

## 2. What this machine has

Recorded because "it builds here" is only useful with the versions attached.

| Tool | Version | Location on this machine |
|---|---|---|
| clang / clang-tidy / clang-format | 23.1.0 | `C:\GitHub\clang+llvm-23.1.0-x86_64-pc-windows-msvc\bin` |
| CMake (the one that works) | 4.3.1, CLion-bundled | `C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe` |
| CMake (on PATH) | 3.31.2 | `C:\Program Files\CMake\bin` |
| Ninja | 1.12.0 | `C:\Strawberry\c\bin` |
| Vulkan SDK | 1.4.357.0 | `C:\VulkanSDK\1.4.357.0` |
| Python (for the format hook) | 3.14.0 | `C:\Program Files\PyManager` |
| MSVC toolchain | VS2022 14.44 | clang targets the MSVC ABI and needs its headers and libs |
| WSL 2 + Ubuntu | 26.04 LTS ("resolute") | Installed 2026-09-07. clang 23.1.1, gcc-14 14.3.0, cmake 4.2.3, ninja 1.13.2. This is where UBSan and the second compiler live |

**The renderer now requires a discrete GPU where one exists, and gets the RTX
A2000.** Both devices satisfy every requirement, and until 2026-09-06 the Intel
UHD won because vk-bootstrap's discrete *preference* is not binding while
`allow_any_gpu_device_type` is left at its default of true. A machine with only
an integrated GPU still runs, via a logged fallback.

A visible side effect: the frame rate went from 60 to about 1000 fps. That is
not the A2000 being seventeen times faster at clearing a screen — the Intel
driver does not expose `VK_PRESENT_MODE_MAILBOX_KHR`, so it fell back to FIFO
and blocked on vsync, while NVIDIA offers mailbox and does not. Worth knowing
before reading anything into an fps number on this project.

**The Orbiter reference clone is at `C:\Reference\orbiter`**, outside this
repository, ~1.1 GB, shallow. It is not required to build. The licence
boundaries are in `CLAUDE.md`; the tile format spec is
`Doc/Orbiter Developer Manual/PLANETS.tex`, section `sssec:tile_file_layout`.

**Planetary imagery** lives in `data/textures/`, gitignored. Three Blue Marble
files (~29 MB) are already downloaded on this machine; `data/textures/README.md`
has the exact URLs to fetch them again.

Git remote: `git@github.com:pml76/orbsim.git`. The repository is **public**,
which is why commit messages carry co-authorship but never a session URL.

### Starting on a new machine

```
git clone git@github.com:pml76/orbsim.git
cd orbsim
git config core.hooksPath scripts/git-hooks      # enables the pre-commit format check
cmake --preset relwithdebinfo
cmake --preset debug
cmake --build build/relwithdebinfo --target check
cmake --build build/debug --target check
```

Needs clang 17+ (23 here), CMake 3.25+, Ninja, and a Vulkan SDK for the loader
and `glslc`. Everything else is fetched and pinned by CMake. Without a Vulkan
SDK, `-DORBSIM_BUILD_APP=OFF` builds the core and its tests.

**The second toolchain, which is not optional before a milestone lands.** It
has already caught two defects that Windows clang could not see -- an unstable
solver and a missing `<tuple>` include -- so set it up on any machine that will
be doing real work:

```
wsl --install -d Ubuntu --no-launch
wsl -d Ubuntu -u root -- apt-get update
wsl -d Ubuntu -u root -- apt-get install -y build-essential cmake ninja-build g++-14 git
```

**Do not install Ubuntu's `clang` meta-package.** It tracks whatever the
release ships (21.1.8 on 26.04), and this project wants one clang version
across both operating systems, not two. Take it from LLVM's own archive
instead, matching the major version installed on Windows:

```
codename=$(lsb_release -cs)          # "resolute" on 26.04
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
  | gpg --dearmor -o /usr/share/keyrings/llvm-archive-keyring.gpg
cat > /etc/apt/sources.list.d/llvm-23.list <<EOF
deb [signed-by=/usr/share/keyrings/llvm-archive-keyring.gpg] \
http://apt.llvm.org/$codename/ llvm-toolchain-$codename-23 main
EOF
apt-get update
apt-get install -y clang-23 clang-tidy-23 clang-format-23 clangd-23 \
                   lld-23 llvm-23 libclang-rt-23-dev
apt-get remove -y clang-21 clang-tools-21 llvm-21 && apt-get autoremove -y
```

`libclang-rt-23-dev` is not optional: it carries the ASan, UBSan and libFuzzer
runtimes, which is the entire reason a Linux box exists here. The presets ask
for bare `clang` / `clang++`, so point those at 23 with one alternatives entry
that slaves the rest to it:

```
u=/usr/bin
update-alternatives --install $u/clang clang $u/clang-23 100 \
  --slave $u/clang++       clang++       $u/clang++-23 \
  --slave $u/clang-tidy    clang-tidy    $u/clang-tidy-23 \
  --slave $u/clang-format  clang-format  $u/clang-format-23 \
  --slave $u/clangd        clangd        $u/clangd-23 \
  --slave $u/llvm-cov      llvm-cov      $u/llvm-cov-23 \
  --slave $u/llvm-profdata llvm-profdata $u/llvm-profdata-23
```

Then, from inside the distribution, in the repository:

```
cmake --preset linux-sanitize && cmake --build build/linux-sanitize   # ASan + UBSan
cmake --preset linux-gcc      && cmake --build build/linux-gcc        # the second compiler
cmake --preset linux-fuzz     && cmake --build build/linux-fuzz       # libFuzzer
ctest --test-dir build/linux-sanitize --output-on-failure
ctest --test-dir build/linux-gcc      --output-on-failure
./build/linux-fuzz/fuzz_orbit -max_total_time=240
```

`llvm` is there for `llvm-cov` and `llvm-profdata`; `VERIFICATION.md` rule 18
has the coverage invocation. The `--no-launch` matters: it skips the
interactive account setup, so commands run as `-u root` and nothing blocks.

**Nothing about this project lives outside the repository.** Working
agreements are in `CLAUDE.md`, decisions in `docs/adr/`, verification practice
in `docs/VERIFICATION.md`, and this file holds the state and the machine
specifics. The only things a fresh clone lacks are build output and the Blue
Marble imagery under `data/textures/`, and
[`data/textures/README.md`](../data/textures/README.md) has the URLs for that.

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
   **Superseded: the owner decided on 2026-09-06 not to use CI.** See section 7.
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
   measured.** See the end of section 6.3.

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

The two `pro-type-*` entries came off the list on the principle in section 6.1:
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
pointed at 23 through `update-alternatives`. The recipe is in section 2. This
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

## 5. Decisions taken, and where they are written down

Seven architecture decision records now exist in [`docs/adr/`](adr/):

| ADR | Decision |
|---|---|
| 0001 | Physical quantities are types, built on one `Quantity<Derived>` base |
| 0002 | `std::expected` for expected failures, assertions for impossible ones; one error representation per layer |
| 0003 | Reverse-Z depth with an infinite far plane |
| 0004 | Vulkan headers pinned by CMake; the SDK supplies only the loader and `glslc` |
| 0005 | Correctness is enforced by a local `check` target and two hooks, not by a checklist and not by CI |
| 0006 | orbsim is a simulation, not a sandbox: multi-body physics with perturbations, real time and frames, a radiometric renderer |
| 0007 | Render quality is a `RenderQuality` struct of per-feature settings, living in the renderer where the physics cannot reach it |

Read the relevant one before changing anything it covers.

Two documents were added on 2026-09-06 alongside ADR 0006 and are binding:
[`plan/realism.md`](plan/realism.md), the gap list between what exists and what
that decision requires, ordered by structural risk; and
[`VERIFICATION.md`](VERIFICATION.md), the rules for keeping bugs out, whose
Part 4 records which of them a machine currently checks and which do not yet
exist. `CLAUDE.md` points at both.

---

## 6. Decisions that were reviewed, and how they landed

I made these on my own during the session. They were put to the project owner
on 2026-09-06; the rulings are recorded here so nobody re-opens them by
accident.

### 6.1 Five clang-tidy checks — RULED: re-enable all five, fix the code

Every check I had suppressed or narrowed is back on. Both `.clang-tidy` files
now carry exactly the suppression list they had before this session. The code
absorbed the findings instead:

| Check | How it was satisfied |
|---|---|
| `portability-avoid-pragma-once` | All 10 project headers and all 7 example headers converted to include guards (`ORBSIM_CORE_SCALAR_HPP`, `ORBEX_CORE_VEC3_HPP`, —) |
| `readability-redundant-member-init` | The `{}` dropped from `Elements` and `OrbitInfo` (project) and `Elements` (example). Safe because every member is a unit type and `Quantity` supplies its own default member initializer. `bool closed{}` keeps its `{}` — that one genuinely needs it |
| `performance-enum-size` | The example's four enums take an explicit `std::uint8_t` base, justified as interface rather than optimisation. Still suppressed in the root project, where it was suppressed with a written reason **before** this session |
| `misc-non-private-member-variables-in-classes` | `NOLINT` on `Vec3`, `Quat` and `Quantity::value`, each with the reason on the line. Still suppressed in the example, where it was suppressed before this session |
| `cppcoreguidelines-macro-usage` | `NOLINTNEXTLINE` on the two assertion macro definitions in each project |

The principle the owner set, worth keeping: **fix the code; suppress only when
the code cannot be fixed, and then at the site, not in the config.** A
suppression in `.clang-tidy` silently covers whatever is written next; one on
the line covers only that line.

There are now four suppression sites in the project (two macros, the value
types, one commutative-parameter pair, one seeded RNG) and four in the example.
Every one carries its reason.

### 6.2 `EXAMPLE.md` was deleted — RULED: leave it deleted

923 lines, the prose predecessor of `coding-guidelines-example/`. Nothing
linked to it and it had drifted to a different namespace (`orb::` vs the
directory's `orbex::`). Recoverable with
`git checkout 1cae874^ -- EXAMPLE.md` should that ever be wanted.

### 6.3 The `asan` preset is RelWithDebInfo, not Debug — still my call

ASan and the MSVC *debug* C runtime cannot share a heap: every sanitized
executable died at startup with a bad-free inside `ucrtbased.dll` before
reaching `main`. The preset builds RelWithDebInfo with `-DNDEBUG` removed, so
assertions are live but the release runtime is used. Verified: all three suites
pass under ASan and `NDEBUG` appears nowhere in the generated build.

Two Windows-specific workarounds ride along.
`_DISABLE_STRING_ANNOTATION` / `_DISABLE_VECTOR_ANNOTATION` are needed because
vk-bootstrap is not instrumented and the MSVC STL refuses to link mixed
annotations; the cost is overflow detection *inside* `std::string` and
`std::vector` spare capacity, with heap, stack and use-after-free intact. And
`clang_rt.asan_dynamic-x86_64.dll` is copied into the build tree, because
clang's ASan runtime is not on `PATH` and its absence kills every sanitized
executable with `STATUS_DLL_NOT_FOUND` before it prints anything.

The `linux-sanitize` and `linux-gcc` presets **have now been run, and both pass**
(2026-09-07). WSL 2 turned out to be enabled already with no distribution
installed, so the Linux box was one `wsl --install -d Ubuntu` away. Each preset
reports 3,632 checks and zero failures, matching Windows exactly, and UBSan was
confirmed genuinely active rather than merely configured. Nothing runs them
automatically — with CI declined they are a deliberate act before a milestone
lands. See `VERIFICATION.md` rule 20.

### 6.4 Scope that went beyond "fix the findings" — still unreviewed

The `Quantity` CRTP base, the `core/Scalar.hpp` split, the four new unit types
(`MetresPerSecond`, `RadiansPerSecond`, `SpecificEnergy`, plus `Tolerance`
moving down), and renaming `InitError` to `RenderError` were refactors I chose.
They are covered by ADR 0001 and 0002 and everything passes, but none was
asked for.

---

## 7. Open questions — for the project owner, not for me

1. **Which GPU should the renderer select? Settled: always the discrete one.**
   Decided 2026-09-06 and implemented in `makeDevice`. No flag; a machine
   without a discrete GPU falls back and says so in the log.
2. **CI: settled, and the answer is no.** The owner decided on 2026-09-06 that
   this project does not use continuous integration. Do not add it and do not
   spend time on it. Verification is the `check` target, run locally by a
   person before they call something done.

   ADR 0005 recorded the cost as **"UndefinedBehaviorSanitizer and a second
   compiler are now out of reach"**. That turned out to be false, and it is
   corrected in that ADR's 2026-09-07 update: WSL 2 was already enabled here,
   so `wsl --install -d Ubuntu` bought back both. UBSan's Windows support is
   still partial and `ORBSIM_SANITIZE_UNDEFINED` still refuses to configure
   there; the difference is that "there" is no longer the only option.

   What remains true is that nothing runs them automatically. The
   `linux-sanitize` and `linux-gcc` presets are run by hand, and it is worth
   doing before a milestone lands: a different compiler and standard library
   disagreeing with clang is where a certain class of bug first shows itself.

   `.github/workflows/ci.yml` was added in commit `eb2a99b` and deleted again
   on 2026-09-06. It is recoverable from history if the decision is ever
   revisited; nothing in the tree refers to it.
3. **Tile format on disk: KTX2 with BC7, or DDS?** From the milestone plan,
   still open. KTX2 has the cleaner spec; DDS is what Orbiter uses, which
   matters for the later reader.
4. **Elevation and night lights — settled for elevation: it moves into phase
   C.** ADR 0006 makes relief part of the realism bar, and it is much cheaper
   inside the quadtree than after it. Night lights still fold into phase B.
5. **The physics fidelity question is settled: a simulation, not a sandbox.**
   ADR 0006, decided 2026-09-06. What remains open from that decision is the
   *technical* fork list in [`plan/realism.md`](plan/realism.md) section 4 —
   Encke or Cowell, which integrator, Cartesian or equinoctial state, DE440 or
   VSOP87, and how far up the spherical-harmonic field to go. Each deserves
   its own ADR when settled.
6. **Is `Vec3` staying unit-free?** Today a vector's unit is carried by the
   struct holding it (`StateVector::pos` is metres). A `Vec3<Metres>` is
   possible and much more invasive. ADR 0001 records the current answer, and
   ADR 0006 sharpens the question: with several frames in play, a `Vec3` that
   knows it holds barycentric metres would prevent a class of bug that is
   otherwise invisible.

---

## 8. Gotchas worth not rediscovering

- **clang-tidy header filters need both separators on Windows.** The regex is
  `.*[/\\](src|tests)[/\\].*\.(hpp|h)$`. To check it still matches, run
  `clang-tidy -p build/relwithdebinfo --header-filter='.*' src/orbit/Orbit.cpp`
  and compare the finding count against a plain run.
- **A `Checks:` list drops the defaults**, so `clang-analyzer-*` has to be
  named explicitly. It is the only family that is path-sensitive.
- **ASan does not work with a Debug build on Windows.** See 6.3.
- **CRLF.** `.gitattributes` normalises the repository to LF and
  `.clang-format` now writes LF, but an older checkout can still hold CRLF in
  files nobody has touched. Those show in `git status` while `git diff` reports
  nothing, because git normalises on read. Leave them alone rather than
  producing a diff in which every line changed.
- **Use CLion's bundled CMake (4.3.1), not the 3.31.2 on PATH.** The build
  tree was configured with the former.
- **The Epic Games overlay layer** logs a duplicate-layer warning at every
  Vulkan startup. It is noise, not a problem.
- **There is one clang here now, and it is meant to stay that way.** 23.1.0 on
  Windows, 23.1.1 in WSL -- the same release branch; apt.llvm.org publishes
  branch builds rather than the exact tag. Everything resolves to it on both
  sides, `clangd` and `llvm-cov` included. A standalone `clangd_22.1.0` used to
  sit ahead of the LLVM directory on `PATH`, so the editor parsed this code with
  a different front end from the one compiling it; it was deleted on 2026-09-08.
  If diagnostics ever disagree with the build again, check `clangd --version`
  first.
- **`.clangd` points at `build/relwithdebinfo`.** That tree must exist and be
  current, or the editor's diagnostics are fiction. During the 23.1 switch it
  briefly pointed at a deleted tree and every file in
  `coding-guidelines-example/` appeared to be full of unknown types.
- **A clang upgrade leaves the old build trees pointing at the old clang.**
  `CMakeCache.txt` stores the resolved compiler path, so a tree configured
  before the upgrade keeps building with the previous compiler and reports
  green while proving nothing. Delete and reconfigure the tree; do not trust a
  `check` that ran in a stale one.
- **Two clang-tidy 23 checks are wrong on this code, and one of them writes
  code that does not compile.**
  `readability-redundant-parentheses` flags `(1.0_km).value` -- but
  `1.0_km.value` lexes as a single pp-number, so the suffix swallows `.value`
  and the "fix" does not build. `readability-trailing-comma` reads the argument
  separator after an empty braced initialiser (`f(T{}, "x")`) as that list's
  trailing comma, and its fix-it **deletes the separator**. Both are avoided
  here by naming the value instead. Neither fires under clang-tidy 22. If you
  run `clang-tidy --fix` over this tree, build afterwards and read the diff.

---

## 9. If you are picking this up cold

Read in this order:

1. This file.
2. [`CLAUDE.md`](../CLAUDE.md) — how to work here, and the definition of done.
3. [`docs/adr/`](adr/) — five decisions and why.
4. [`docs/plan/milestone-1-earth.md`](plan/milestone-1-earth.md) — what is next.
5. [`CODING_GUIDELINES.md`](../CODING_GUIDELINES.md) when you want the
   arguments, and `coding-guidelines-example/` when you want to see them
   applied.

Then run `check` in both trees. If it is green, the tree is as this file
describes it.
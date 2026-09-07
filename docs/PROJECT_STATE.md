# orbsim — project state and handoff

Last updated: 2026-09-05, end of the review-and-fix session on branch
`review-fixes-2026-09`.

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
| `tests/` | Two suites, ~3,500 checks, plus a GPU smoke test. |

### Test suites

| Suite | Checks | What it covers |
|---|---|---|
| `test_orbit` | 732 | Earth-orbit round trips, degenerate orbits, analytic values, propagator agreement, invariants, hyperbolic, Kepler solver, reported failures |
| `test_orbit_scales` | 2781 | Heliocentric circles, parabolic trajectories, non-finite inputs, and a seeded sweep of 200 closed + 100 hyperbolic orbits around the Moon, Earth, Jupiter and the Sun |
| `orbsim_smoke` | — | Runs the app under the Vulkan validation layers for 2 s; fails on any validation error. Labelled `gpu`. |

The seed for the random sweep is `20260905` and is written into
`tests/test_orbit_scales.cpp`. A failure prints the failing case's parameters.

---

## 2. What this machine has

Recorded because "it builds here" is only useful with the versions attached.

| Tool | Version | Location on this machine |
|---|---|---|
| clang / clang-tidy / clang-format | 22.1.8 | `C:\GitHub\clang+llvm-22.1.8-x86_64-pc-windows-msvc\bin` |
| CMake (the one that works) | 4.3.1, CLion-bundled | `C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe` |
| CMake (on PATH) | 3.31.2 | `C:\Program Files\CMake\bin` |
| Ninja | 1.12.0 | `C:\Strawberry\c\bin` |
| Vulkan SDK | 1.4.357.0 | `C:\VulkanSDK\1.4.357.0` |
| Python (for the format hook) | 3.14.0 | `C:\Program Files\PyManager` |
| MSVC toolchain | VS2022 14.44 | clang targets the MSVC ABI and needs its headers and libs |

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

Needs clang 17+ (22 here), CMake 3.25+, Ninja, and a Vulkan SDK for the loader
and `glslc`. Everything else is fetched and pinned by CMake. Without a Vulkan
SDK, `-DORBSIM_BUILD_APP=OFF` builds the core and its tests.

---

## 3. What changed in this session

Eight commits on `review-fixes-2026-09`, branched from `master`. In order:

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

### Uncommitted work in the tree

| File | Change | Verified? |
|---|---|---|
| `CMakeLists.txt` | The `check` / `lint` / `format-check` / `format` targets; `ORBSIM_BUILD_APP`; `ORBSIM_SANITIZE_UNDEFINED`; ASan Windows fixes | `check` passes in both trees |
| `CMakePresets.json` | `asan`, `linux-sanitize`, `linux-gcc` presets | asan: all 3 suites pass; `linux-*` **never run, no Linux here** |
| `src/render/VulkanContext.*`, `src/app/main.cpp` | Validation errors counted through a callback; exit code 3 when any are reported | `check` passes in both trees |
| `CLAUDE.md` | A note on why the asan preset is not a Debug build | n/a |
| 9 other files | **Line-ending normalisation only — zero content diff.** `git diff` reports nothing for them | n/a |

`master` is untouched. The branch itself has since been pushed to
`origin/review-fixes-2026-09`.

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

Five architecture decision records now exist in [`docs/adr/`](adr/):

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

The **`linux-sanitize` and `linux-gcc` presets have never been run** — there is
no Linux on this machine. They are written, not proven, and with CI declined
(section 7) nothing will run them automatically. They are there for whoever
next has a Linux box to hand.

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

   The cost is recorded in ADR 0005 and is worth knowing:
   **UndefinedBehaviorSanitizer and a second compiler are now out of reach.**
   UBSan's Windows support is partial, so `ORBSIM_SANITIZE_UNDEFINED` refuses
   to configure there, and gcc cannot build this project on this machine.
   Anyone with a Linux box can still run the `linux-sanitize` and `linux-gcc`
   presets by hand, and it is worth doing occasionally: a different compiler
   and standard library disagreeing with clang is where a certain class of bug
   first shows itself.

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
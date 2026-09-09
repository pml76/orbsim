# orbsim — status

Kind: reference
Binding: no — this file states facts, not rules
Read when: you need a version, a count, or "what is next". Update it whenever
one of those changes.

**Every number in this project that goes stale lives here, and only here.**
Prose elsewhere links to this file rather than quoting it. That is not
tidiness: on 2026-09-09 the assertion count `3,632` appeared 21 times across
10 files, `README.md` still said "about 3,500", `CODING_GUIDELINES.md` still
named clang 22 as the primary compiler and the test harness as hand-rolled,
and `VERIFICATION.md` still named WSL's clang as 21.1.8. Twenty-one copies of
a number are not twenty-one confirmations; they are one fact with twenty
chances to rot — which is
[`VERIFICATION.md`](VERIFICATION.md) rule 23 applied to the documentation.

A *dated* measurement is not a current claim and does not belong here: "the
fuzzer ran 77.4 million executions clean on 2026-09-07" is a fact about that
day and stays in [`HISTORY.md`](HISTORY.md).

Last updated: 2026-09-09.

## Contents

- [Where the project is](#where-the-project-is)
- [Test suites](#test-suites)
- [What this machine has](#what-this-machine-has)
- [Decision records](#decision-records)

---

## Where the project is

**Milestone 0 is complete: a tested two-body core and a Vulkan renderer that
opens a window and paces frames.** Nothing is drawn yet.
[Milestone 1](plan/milestone-1-earth.md) — Earth, orbit track, Orbit MFD — has
**started**.

| | |
|---|---|
| Current milestone | 1 — Earth, orbit track, Orbit MFD |
| Last task completed | [M1-01](plan/tasks/m1-01-catch2.md), the move to Catch2, 2026-09-09 |
| Next task | [M1-02](plan/tasks/m1-02-record-the-decisions.md), the six ADRs and `THIRD_PARTY.md` |
| Then | Phase A — render foundations: pipelines, a camera with camera-relative rendering, a line renderer |
| Phase order | A → B → D → C → E → F → G |

| Component | State |
|---|---|
| `src/core/` | Scalars, strong unit types, Vec3, Quat. Complete for what exists. |
| `src/orbit/` | Two-body: state↔elements, universal-variable and element propagation, anomaly conversions. Correct at every scale from lunar to outer-solar-system. |
| `src/render/` | Vulkan 1.3 device, swapchain, frame pacing, RAII handles, buffer upload, shader loading. **No pipelines, no drawing.** |
| `src/app/` | Window, event loop, argument parsing, frame loop. |
| `shaders/` | Four GLSL shaders compile to SPIR-V at build time and are **never loaded**. They are placeholders for phase A. |
| `tests/` | Two Catch2 suites, 3,632 assertions in 19 test cases, plus a GPU smoke test and a libFuzzer target. |

## Test suites

| Suite | Assertions | What it covers |
|---|---|---|
| `test_orbit` | 732, in 8 cases | Earth-orbit round trips, degenerate orbits, analytic values, propagator agreement, invariants, hyperbolic, Kepler solver, reported failures |
| `test_orbit_scales` | 2900, in 11 cases | Heliocentric circles, parabolic trajectories, non-finite inputs, states that are finite but are not orbits, states with no orbital plane, a zero time step on every conic, near-rectilinear orbits, propagation composing, canonical scale invariance, bit-identical determinism, and a seeded sweep of 200 closed + 100 hyperbolic orbits around the Moon, Earth, Jupiter and the Sun |
| `fuzz_orbit` | — | libFuzzer over the core under ASan and UBSan. Not a CTest test: run deliberately with a time budget, `cmake --preset linux-fuzz`. |
| `orbsim_smoke` | — | Runs the app under the Vulkan validation layers for 2 s; fails on any validation error. Labelled `gpu`. |

Totals: **3,632 assertions in 19 test cases**, and the same 3,632 under both
Windows trees, under ASan, and under both Linux presets. Catch2 **v3.16.0**.

The seed for the random sweep is `20260905` and is written into
`tests/test_orbit_scales.cpp`. A failure prints the failing case's parameters,
through Catch2's `CAPTURE`, which reports them on failure rather than on every
run. Catch2 prints a `Randomness seeded to:` line that differs between runs; it
seeds only `GENERATE` and `--order rand`, neither of which this project uses,
and the sweep's own generator is still seeded from `kSweepSeed`.
`catch_discover_tests` makes each `TEST_CASE` its own CTest test, so `ctest -R`
selects one and `ctest -N` lists nineteen.

## What this machine has

Recorded because "it builds here" is only useful with the versions attached.
How to install and run any of it is
[`PROJECT_STATE.md`](PROJECT_STATE.md) section 2.

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
| GPU in use | NVIDIA RTX A2000 | The renderer requires a discrete GPU where one exists |

**One clang version, everywhere: 23.1.** 23.1.0 on Windows, 23.1.1 in WSL from
`apt.llvm.org` — the same release branch; apt.llvm.org publishes branch builds
rather than the exact tag. `gcc-14` is the second *implementation* and is not
tied to that number.

Pinned dependencies: SDL3, vk-bootstrap, VMA, Vulkan-Headers and Catch2, all
fetched at a pinned tag by `CMakeLists.txt`.

## Decision records

Seven architecture decision records exist in [`adr/`](adr/). Read the relevant
one before changing anything it covers.

| ADR | Decision |
|---|---|
| 0001 | Physical quantities are types, built on one `Quantity<Derived>` base |
| 0002 | `std::expected` for expected failures, assertions for impossible ones; one error representation per layer |
| 0003 | Reverse-Z depth with an infinite far plane |
| 0004 | Vulkan headers pinned by CMake; the SDK supplies only the loader and `glslc` |
| 0005 | Correctness is enforced by a local `check` target and two hooks, not by a checklist and not by CI |
| 0006 | orbsim is a simulation, not a sandbox: multi-body physics with perturbations, real time and frames, a radiometric renderer |
| 0007 | Render quality is a `RenderQuality` struct of per-feature settings, living in the renderer where the physics cannot reach it |

ADRs 0008–0013 are planned by
[M1-02](plan/tasks/m1-02-record-the-decisions.md); the decisions they will
record are already settled and listed in
[`plan/milestone-1-decisions.md`](plan/milestone-1-decisions.md).

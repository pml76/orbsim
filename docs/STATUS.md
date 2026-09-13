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

Last updated: 2026-09-12.

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
| Last task completed | [M1-03](plan/tasks/m1-03-timepoint.md), `TimePoint` and the time scales, 2026-09-10 |
| Before M1-04 | **Both done, 2026-09-12.** `elementsFromState` is now accurate to the resolution of a double -- every element within 40 u of the nearest double to the exact conversion, worst case 9.4 u, measured against 60-digit references over 56,532 states across nine families on three toolchains. The cancelling steps are carried in double-double ([`src/core/DoubleDouble.hpp`](../src/core/DoubleDouble.hpp)) on a state scaled by a power of two. The circular threshold moved from e = 1e-9 to 1e-15, where the 2e it costs a round trip is the resolution of a double rather than 1.8e-9 |
| Next task | [M1-04](plan/tasks/m1-04-leap-seconds.md), UTC, TAI and TT: the leap-second table |
| Then | The rest of phase A — the Horizons fixtures (M1-06, now ahead of M1-05); TDB and UT1, where ERFA is pinned; Earth orientation with nutation, and the Sun, both computed by ERFA; then `orbsim_view`, the camera, the pipelines, and the probe mode that verifies everything drawn after it |
| Phase order | A → B → D → C → E → F → G |

| Component | State |
|---|---|
| `src/core/` | Scalars, strong unit types, Vec3, Quat, and `TimePoint` on five time scales — the representation; converting between scales is M1-04 and M1-05. Complete for what exists. |
| `src/orbit/` | Two-body: state↔elements, universal-variable and element propagation, anomaly conversions. Correct at every scale from lunar to outer-solar-system. |
| `src/render/` | Vulkan 1.3 device, swapchain, frame pacing, RAII handles, buffer upload, shader loading. **No pipelines, no drawing.** |
| `src/app/` | Window, event loop, argument parsing, frame loop. |
| `shaders/` | Four GLSL shaders compile to SPIR-V at build time and are **never loaded**. They are placeholders for phase A. |
| `tests/` | Four Catch2 suites, 599,531 assertions in 62 test cases, plus a GPU smoke test and a libFuzzer target. |

## Test suites

| Suite | Assertions | What it covers |
|---|---|---|
| `test_double_double` | 135,014, in 7 cases | `core/DoubleDouble.hpp`: the exact product against `std::fma`, which computes the same rounding error by a completely different route, over 20,000 operands spanning every scale; the exact sum against `std::int64_t` arithmetic; the split above its 2^996 ceiling, where it used to return a correct product with a NaN error term; a cancelling difference keeping what a double loses; the quotient and the square root reconstructing their inputs to 2^-100; and every way an overflow could become a NaN instead of staying an overflow |
| `test_orbit` | 732, in 8 cases | Earth-orbit round trips, degenerate orbits, analytic values, propagator agreement, invariants, hyperbolic, Kepler solver, reported failures |
| `test_orbit_scales` | 43,036, in 27 cases | Heliocentric circles, parabolic trajectories, non-finite inputs, states that are finite but are not orbits, states with no orbital plane, `length()` exact at every binary scale, the fuzzer's nearly radial hyperbola at 1e-158 m, nearly radial ellipses and hyperbolas at 7000 km classified by their energy, an eccentricity that rounds to 1, `orbitInfo`'s radius and speed on four states that broke them and a fuzzer state whose semi-major axis underflows, a zero time step on every conic, near-rectilinear orbits, propagation composing, canonical scale invariance, bit-identical determinism, a seeded sweep of 200 closed + 100 hyperbolic orbits around the Moon, Earth, Jupiter and the Sun, a second seeded sweep of 10,000 states -- ordinary, nearly radial, near-parabolic, e down to 1e-16, and out along a hyperbola's asymptote to r/|a| = 1e6 -- against the conditioning of the round trip through the elements, element propagation on five conics within 2e-9 of a parabola against 60-digit references, a third seeded sweep of 2,000 element sets -- ordinary, near-parabolic, parabolic and near-circular -- against the state propagator and against stepping back, seven measured states, one per failure mechanism, where every one of the seven elements is checked against a 60-digit reference, the perifocal velocity's e - 1 at 1 - e = 1e-6, a radial trajectory at the rectilinear threshold from the correct side, and a committed checksum pinning the elements that are bit-identical on every toolchain |
| `test_time` | 420,749, in 20 cases | The published epochs both ways; the calendar against `std::chrono`'s on every day of 1900–2100, and which dates exist against its `ok()`; a seeded calendar round trip; 1 ns at the end of a day and across it, and a million 1 µs steps; arithmetic by the nearest picosecond across every day boundary; ordering against the difference; every refusal by name; the Julian-date conversions against exact rational arithmetic; and, at compile time, that no scale converts to another and UTC and UT1 have no duration arithmetic |
| `fuzz_orbit` | — | libFuzzer over the core under ASan and UBSan. Not a CTest test: run deliberately with a time budget, `cmake --preset windows-fuzz`. The `linux-fuzz` preset builds the same target under WSL and is kept only for LeakSanitizer, which Windows has no equivalent of. |
| `orbsim_smoke` | — | Runs the app under the Vulkan validation layers for 2 s; fails on any validation error. Labelled `gpu`. |

Totals: **599,531 assertions in 62 test cases**, and the same 599,531 under
both Windows trees, under ASan, and under both Linux presets. Catch2 **v3.16.0**.

The seeds for the random sweeps are written into the suites: `20260905` in
`tests/test_orbit_scales.cpp` and `20260910` in `tests/test_time.cpp`, each the
date its suite was written. `test_time` draws straight from the engine rather
than through a `std::` distribution, whose algorithm the standard leaves to the
library, so its sweep is the same dates under MSVC's library and libstdc++. A
failure prints the failing case's parameters,
through Catch2's `CAPTURE`, which reports them on failure rather than on every
run. Catch2 prints a `Randomness seeded to:` line that differs between runs; it
seeds only `GENERATE` and `--order rand`, neither of which this project uses,
and the sweeps' own generators are still seeded from `kSweepSeed`.
`catch_discover_tests` makes each `TEST_CASE` its own CTest test, so `ctest -R`
selects one and `ctest -N` lists **63**: the sixty-two Catch2 cases plus
`orbsim_smoke`. `ctest -LE gpu` lists the sixty-two.

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
fetched at a pinned tag by `CMakeLists.txt`. Their licences, the ones decided
but not yet pinned, and which files of `bc7enc_rdo` may be compiled, are in
[`THIRD_PARTY.md`](../THIRD_PARTY.md).

## Decision records

**The list is [`adr/README.md`](adr/README.md)**, next to the records
themselves, so that browsing the directory finds it. **Seventeen are
accepted**: 0008 to 0015 written by
[M1-02](plan/tasks/m1-02-record-the-decisions.md) from the decisions taken on
2026-09-08, then 0016 — ERFA computes the astronomy — and 0017 — every warning
as an error — both on 2026-09-11.

An accepted ADR is immutable, so the numbers inside one are not maintained
here: they are what was true when the decision was taken.

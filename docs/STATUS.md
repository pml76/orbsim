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

Last updated: 2026-09-19.

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
| Last task completed | [M1-05](plan/tasks/m1-05-tdb-and-ut1.md), TDB and UT1, and ERFA pinned, 2026-09-19 -- **all five scales now convert**. Its fifteen questions were put to the owner before any code and ruled the same day: decisions 53-67 of the [register](plan/milestone-1-decisions.md). Before it, [M1-06](plan/tasks/m1-06-horizons-fixtures.md), the Horizons fixture format, and [M1-04](plan/tasks/m1-04-leap-seconds.md), UTC, TAI and TT |
| Before M1-04 | **All done, 2026-09-13.** Two tasks on 2026-09-12: `elementsFromState` is now accurate to the resolution of a double -- every element within 40 u of the nearest double to the exact conversion, worst case 9.4 u, measured against 60-digit references over 56,532 states across nine families on three toolchains. The cancelling steps are carried in double-double ([`src/core/DoubleDouble.hpp`](../src/core/DoubleDouble.hpp)) on a state scaled by a power of two. And the circular threshold moved from e = 1e-9 to 1e-15, where the 2e it costs a round trip is the resolution of a double rather than 1.8e-9. Then two more on 2026-09-13: the five findings that commit left open, and **`stateFromElements` reports** ([ADR 0018](adr/0018-state-from-elements-reports.md)) -- it returns `std::expected` and a new `UnreachableAnomaly`, after it was found returning a NaN position for 169 of 10,000 nearly radial element sets and a radius 2.2 times too small, unmarked |
| Next task | [M1-07](plan/tasks/m1-07-earth-orientation.md), precession, nutation and the Earth rotation angle: ERFA's `eraC2t06a` and `eraEra00`, the body-fixed frame, checked to 0.1" against an independent implementation |
| Before M1-07, still open | **Two questions, to be put before its code.** One was left there on purpose by M1-05 (register decision 66): past the leap-second table's expiry, 2027-01-01, a TT clock has no route to UT1, because TT reaches UTC only through the table -- so as planned the Earth's orientation would be refused after that date. The other is the task's own: which independent implementation to check the frame against, now that NOVAS's nutation turned out to share IERS modules with SOFA's ([ADR 0016](adr/0016-the-astronomy-is-erfa.md)'s update of 2026-09-19). M1-07's document carries both |
| Then | The rest of phase A — the Sun, computed by ERFA and checked against the Horizons fixture M1-06 generated; then `orbsim_view`, the camera, the pipelines, and the probe mode that verifies everything drawn after it |
| Reference data | **The geocentric Sun at 40 epochs, 2000-2050**, from JPL Horizons (DE441), generated into gitignored `data/horizons/` and converted by `scripts/horizons-fixture.py`. SHA-256 `61291048...7fd53`, committed in [`data/horizons/checksums.sha256`](../data/horizons/checksums.sha256) and verified by `check`. **A fresh clone has none**: run the recipe in [`data/horizons/README.md`](../data/horizons/README.md), or the suites that need it report themselves skipped. **And TDB - TT at 1,975 epochs, 1900-2100**, from Skyfield 1.55, **committed** as [`data/skyfield/tdb-minus-tt.txt`](../data/skyfield/tdb-minus-tt.txt) -- SHA-256 `e80fdf8b...7dc171` when generated -- so every clone checks against it |
| Leap-second table valid until | **2027-01-01T00:00:00 UTC**, from IERS **Bulletin C 72** (Paris, 2026-07-06), which rules out a leap second at the end of December 2026 and says nothing later. Past it, every UTC conversion reports `LeapSecondTableExpired` by name rather than extrapolating. Renewing it is four lines in [`src/core/LeapSeconds.hpp`](../src/core/LeapSeconds.hpp), which says how |
| Phase order | A → B → D → C → E → F → G |
| Working branch | `master`, and everything through 2026-09-19 is pushed. `git fetch && git switch master` is all another machine needs. **Five merged branches can be deleted** whenever somebody feels like it, local and remote: `clang-23-2026-09`, `consistency-fixes-2026-09-13`, `docs-reorg`, `pre-docs-reorg-2026-09-09`, `review-fixes-2026-09` |

| Component | State |
|---|---|
| `src/core/` | Scalars and vectors that carry their unit over mp-units, Quat, and `TimePoint` on five time scales. **UTC, TAI and TT convert** in exact integer picoseconds over the IERS leap-second table in `core/LeapSeconds.hpp`, and **UT1 converts from UTC**, exactly, with a validated `DeltaUt1`: zero until an IERS series is read, which is a stated model error of at most 0.9 s, about 420 m at the equator. TDB is `src/astro/`'s. Complete for what exists. |
| `src/astro/` | **Begun with M1-05.** TT <-> TDB through ERFA's `eraDtdb` at the geocentre, within 20 µs of an independent reference -- 7.89 µs measured -- and back to within a picosecond. ERFA v2.0.1 is compiled from source and linked privately, and its headers are included here and nowhere else. |
| `src/orbit/` | Two-body: state↔elements, universal-variable and element propagation, anomaly conversions. Correct at every scale from lunar to outer-solar-system. |
| `src/render/` | Vulkan 1.3 device, swapchain, frame pacing, RAII handles, buffer upload, shader loading. **No pipelines, no drawing.** |
| `src/app/` | Window, event loop, argument parsing, frame loop. |
| `shaders/` | Four GLSL shaders compile to SPIR-V at build time and are **never loaded**. They are placeholders for phase A. |
| `tests/` | Six Catch2 suites, 914,521 assertions in 103 test cases, plus ERFA's two validation programs, a GPU smoke test, two fixture tests and two libFuzzer targets. |

## Test suites

| Suite | Assertions | What it covers |
|---|---|---|
| `test_astro_time` | 42,143, in 5 cases | `astro/Tdb.hpp`, **added with M1-05**: TDB - TT at all 1,975 epochs of the committed Skyfield fixture within the 20 µs budget -- 7.89 µs worst -- and not vacuously, since the full series and the seven-term one genuinely differ; the annual term's peaks within 60 µs of the Kepler problem's 2e√(GM a)/c² with JPL's mean elements, and its four zero crossings in 2024-2025 within 2.1 days of USNO's mean anomaly at 0° and 180°, upward at perihelion; the change over twelve hours of one day matching the annual term's to 1 µs, every fifth day of 2024-2025 -- which the fixture's midnights cannot check; TT → TDB → TT and TDB → TT → TDB within a picosecond over a seeded sweep of 1990-2050, and exact on at least 99% of it, which is what shows the series is evaluated at TDB; and both ends of the calendar converted, not refused |
| `t_erfa_c`, `t_erfa_c_extra` | 1,494 checks, and the version and leap-second functions | ERFA's own validation programs, **added with M1-05**, run as upstream's `make check` runs them. They prove the library was built right by this toolchain with these flags, and each exits non-zero on any failure |
| `test_double_double` | 135,020, in 8 cases | `core/DoubleDouble.hpp`: the exact product against `std::fma`, which computes the same rounding error by a completely different route, over 20,000 operands spanning every scale; the exact sum against `std::int64_t` arithmetic; the split above its 2^996 ceiling, where it used to return a correct product with a NaN error term; a cancelling difference keeping what a double loses; the quotient and the square root reconstructing their inputs to 2^-100; and every way an overflow could become a NaN instead of staying an overflow |
| `test_fixture_file` | 24,259, in 14 cases -- **24,133 with one case skipped** where the Horizons fixture has not been generated | `tests/FixtureFile.hpp`, the reference-data reader: a known-good file; comments, blank lines and CRLF; fifteen malformed files, each refused by name **and line**; a missing file; every error describing itself; 10,000 seeded doubles written with 17 digits reading back bit for bit; kilometres to metres against exact rational arithmetic on three values where parsing and then multiplying by 1000 gives the wrong double; state vectors arriving as `TdbTime`, metres and m/s against the compiler's own literals; a fractional epoch kept exact; the wrong frame, scale, corrections or units refused; and the generated Sun fixture itself -- 40 rows, J2000.0 first, every distance in metres between 0.98 and 1.02 AU. **M1-05 added three cases**: a TDB - TT fixture read as `TdbTime` and seconds, bit for bit against the compiler's literals; the wrong scale or unit refused by name and line; and the committed Skyfield fixture itself -- 1,975 rows 37 days apart from 1900-01-01, every value in seconds |
| `horizons_fixture_checksums` | -- | The generated Horizons fixtures against their committed SHA-256 sums, by a CMake script. A wrong hash fails and names both; a fixture not generated is reported **skipped** |
| `horizons_fixture_converter` | -- | `scripts/horizons-fixture.py --self-test`: a synthetic response to a known text; identical output when the timestamp and Earth-orientation lines change; eleven refusals. Its input holds 2^53 + 1, which no double can, so a converter that re-printed the numbers fails |
| `test_orbit` | 763, in 8 cases | Earth-orbit round trips, degenerate orbits, analytic values, propagator agreement, invariants, hyperbolic, Kepler solver, reported failures |
| `test_orbit_scales` | 85,673, in 30 cases | Heliocentric circles, parabolic trajectories, non-finite inputs, states that are finite but are not orbits, states with no orbital plane, `length()` exact at every binary scale, the fuzzer's nearly radial hyperbola at 1e-158 m, nearly radial ellipses and hyperbolas at 7000 km classified by their energy, an eccentricity that rounds to 1, `orbitInfo`'s radius and speed on four states that broke them and a fuzzer state whose semi-major axis underflows, a zero time step on every conic, near-rectilinear orbits, propagation composing, canonical scale invariance, bit-identical determinism, a seeded sweep of 200 closed + 100 hyperbolic orbits around the Moon, Earth, Jupiter and the Sun, a second seeded sweep of 10,000 states -- ordinary, nearly radial, near-parabolic, e down to 1e-16, and out along a hyperbola's asymptote to r/|a| = 1e6 -- against the conditioning of the round trip through the elements, element propagation on five conics within 2e-9 of a parabola against 60-digit references, a third seeded sweep of 2,000 element sets -- ordinary, near-parabolic, parabolic and near-circular -- against the state propagator and against stepping back, seven measured states, one per failure mechanism, where every one of the seven elements is checked against a 60-digit reference, the perifocal velocity's e - 1 at 1 - e = 1e-6, a radial trajectory at the rectilinear threshold from the correct side, a committed checksum pinning the elements that are bit-identical on every toolchain, and three cases on `stateFromElements` -- a NaN position, a radius 2.2 times too small, and an anomaly past a hyperbola's asymptote |
| `test_time` | 626,663, in 38 cases | The published epochs both ways; the calendar against `std::chrono`'s on every day of 1900–2100, and which dates exist against its `ok()`; a seeded calendar round trip; 1 ns at the end of a day and across it, and a million 1 µs steps; arithmetic by the nearest picosecond across every day boundary; ordering against the difference; every refusal by name; the Julian-date conversions against exact rational arithmetic; and, at compile time, that no scale converts to another and UTC and UT1 have no duration arithmetic. **M1-04 added eleven cases:** the 28 published steps against a second transcription and against `std::chrono::get_leap_second_info` on every one of the 20,089 days of the era; the worked example 2017-01-01T00:00:00 UTC = 00:00:37 TAI = 00:01:09.184 TT; TT − TAI exact at 1,000 epochs; every step one second either side, both directions; 23:59:60 representable and round-tripping; a seeded sweep of 10,000 instants plus eight offsets at each of 27 boundaries, all **bit-identical** through UTC → TAI → UTC and through TT; both scales strictly increasing across every boundary; each refusal by name; a **synthetic** negative leap second, which no published table can reach; and the quasi-Julian date of a leap-second day against exactly-rounded reference values. **M1-05 added seven cases, on UT1:** with DeltaUT1 unmodelled, UT1 carries UTC's day and time over the seeded sweep; UT1 = UTC + DeltaUT1 to the picosecond, across midnight both ways; a leap second, where UT1 runs on into the next day and the way back lands a second later; UT1 → UTC → UT1 exact over 10,000 instants with DeltaUT1 drawn to the picosecond; every refusal of a DeltaUT1 by name, and 0.9 s itself accepted; the calendar's two ends; and a **synthetic** negative leap second, whose removed second of UT1 has no UTC |
| `fuzz_orbit`, `fuzz_time` | — | libFuzzer over the core under ASan and UBSan. `fuzz_time` joined on 2026-09-18 with M1-04 and throws arbitrary bytes at the calendar, the Julian date and the scale conversions, asserting that a success is normalised, that a Julian fraction stays in [0, 1), and that UTC → TAI → UTC is bit-identical. M1-05 added a DeltaUT1 to its input and three claims: an accepted DeltaUT1 is inside 0.9 s, UT1 → UTC → UT1 is bit-identical, and TT ↔ TDB round-trips within a picosecond at any date from year 1 to 9999. Not CTest tests: run deliberately with a time budget, `cmake --preset windows-fuzz`. The `linux-fuzz` preset builds the same targets under WSL and is kept only for LeakSanitizer, which Windows has no equivalent of. |
| `orbsim_smoke` | — | Runs the app under the Vulkan validation layers for 2 s; fails on any validation error. Labelled `gpu`. |

Totals: **914,521 assertions in 103 test cases**, and the same 914,521 under
both Windows trees, under `asan`, under `windows-msvc`, and under both Linux
presets, which read the same generated fixture through `/mnt/c`. On a machine without it, `test_fixture_file`
skips one case and the total is 914,395. Catch2 **v3.16.0**.

**Three implementations now agree to the digit**, which is what `windows-msvc`
was added on 2026-09-14 to find out. It builds the whole tree, renderer
included — the only configuration that does, since both Linux presets are core
only — with **zero warnings under `/Wall /WX /permissive-`**, and passes all 108
CTest entries including the GPU smoke test and ERFA's two validation programs,
which MSVC compiles as C.

The seeds for the random sweeps are written into the suites: `20260905` in
`tests/test_orbit_scales.cpp`, `20260910` in `tests/test_time.cpp` and
`20260919` in `tests/test_fixture_file.cpp` and `tests/test_astro_time.cpp`,
each the date its suite was written. `test_time` draws straight from the engine rather
than through a `std::` distribution, whose algorithm the standard leaves to the
library, so its sweep is the same dates under MSVC's library and libstdc++. A
failure prints the failing case's parameters,
through Catch2's `CAPTURE`, which reports them on failure rather than on every
run. Catch2 prints a `Randomness seeded to:` line that differs between runs; it
seeds only `GENERATE` and `--order rand`, neither of which this project uses,
and the sweeps' own generators are still seeded from `kSweepSeed`.
`catch_discover_tests` makes each `TEST_CASE` its own CTest test, so `ctest -R`
selects one and `ctest -N` lists **108**: the 103 Catch2 cases, the two
fixture tests, ERFA's two validation programs, and `orbsim_smoke`.
`ctest -LE gpu` lists 107, and `ctest -L fixtures` the two fixture tests.

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
| MSVC toolchain | **VS 18 Insiders, MSVC 14.51.36231** | clang targets the MSVC ABI and needs its headers and libs. Upgraded from VS2022 Community 14.44.35207 on 2026-09-13, and the 2022 install is gone: `C:\Program Files\Microsoft Visual Studio\18\Insiders\` is the only one on disk, so clang now emits `-fms-compatibility-version=19.51`. A new standard library is a full rebuild of every tree — the same argument as the clang-upgrade gotcha in [`PROJECT_STATE.md`](PROJECT_STATE.md) section 8 |
| WSL 2 + Ubuntu | 26.04 LTS ("resolute") | Installed 2026-09-07. clang 23.1.1, gcc-14 14.3.0, cmake 4.2.3, ninja 1.13.2. This is where UBSan and the second compiler live |
| GPU in use | NVIDIA RTX A2000 | The renderer requires a discrete GPU where one exists |

**One clang version, everywhere: 23.1.** 23.1.0 on Windows, 23.1.1 in WSL from
`apt.llvm.org` — the same release branch; apt.llvm.org publishes branch builds
rather than the exact tag. `gcc-14` is the second *implementation* and is not
tied to that number.

Pinned dependencies: SDL3, vk-bootstrap, VMA, Vulkan-Headers,
Vulkan-Utility-Libraries, Catch2, mp-units and ERFA — eight, all fetched at a
pinned tag by `CMakeLists.txt`. **mp-units was the first one `orbsim_core`
links**, header-only; **ERFA, since M1-05, is the first it needs at link
time** -- a static C library our build compiles from its sources, so the
headless core still needs nothing installed. Their licences, the ones decided
but not yet pinned, and which files of `bc7enc_rdo` may be compiled, are in
[`THIRD_PARTY.md`](../THIRD_PARTY.md).

## Decision records

**The list is [`adr/README.md`](adr/README.md)**, next to the records
themselves, so that browsing the directory finds it. **Nineteen are accepted**:
0008 to 0015 written by
[M1-02](plan/tasks/m1-02-record-the-decisions.md) from the decisions taken on
2026-09-08, then 0016 — ERFA computes the astronomy — and 0017 — every warning
as an error — both on 2026-09-11, 0018 — `stateFromElements` reports — on
2026-09-13, and 0019 on 2026-09-17.

**0019 — vectors carry their unit** — landed in two commits, and both of its
steps are done. `core/Units.hpp`'s nine
types are built on mp-units `v2.5.0`, so `Metres / Seconds` is a
`MetresPerSecond` and `Eccentricity` is a *kind* no other ratio converts into;
and `Vec3<R>` is templated on an mp-units **reference**, so `cross(r, v)` is
m²/s — a unit with no name in this codebase and none needed — and
`Position + Velocity` does not compile. It supersedes 0001 on the mechanism and
closes rule 17 of [`VERIFICATION.md`](VERIFICATION.md).

The record's own "What the precondition spike measured" section carries the
numbers, including the one that nearly stopped step 2: mp-units has no
`vector_product` on quantities, in `v2.5.0` or on master. Templating `Vec3` on
the reference rather than on a quantity sidesteps it — the unit algebra is the
library's and the vector operations are ours.

An accepted ADR is immutable, so the numbers inside one are not maintained
here: they are what was true when the decision was taken.

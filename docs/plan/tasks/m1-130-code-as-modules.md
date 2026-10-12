# M1-130 — The code as C++20 modules, bottom up, with libraries as the fallback

Phase: B | Status: not started
Prerequisites: M1-127
Decided by: register decisions 485, 486, 492-495, ruled 2026-10-12. Its
sibling, tests by fingerprint, is [M1-129](m1-129-tests-by-fingerprint.md).

## Purpose

A few headers are included almost everywhere, and `orbsim_core` holds three
layers in one library, so one edit recompiles, relints and -- even with
fingerprints -- reruns far more than it touches. This task gives the code a
structure of levels, each depending only on those below it, and makes each
level a C++20 module. Where modules cannot meet the project's rules, the same
levels become libraries instead (decision 485).

## Measured before the ruling (2026-10-11/12)

- **Header reach**, from Ninja's dependency record, of 141 compiled files:
  `Contract.hpp` 121, `Scalar.hpp` 118, `Units.hpp` 92, `Math.hpp` 74,
  `Time.hpp` 28, `Orbit.hpp` 20. `tests/OrbitTestSupport.hpp` includes
  `orbit/Orbit.hpp`, so 13 of 41 test programs depend on the orbit code,
  `test_time` and `test_math` among them.
- **Under fingerprints** ([M1-129](m1-129-tests-by-fingerprint.md)), a change
  to `Scalar.hpp` would still rerun 491 of 537 tests and one to `Math.hpp`
  312-329; a change inside the orbit code 105-118.
- **Cost of one compiled file before its own code**: 3.9 s to compile and
  5.1 s to lint for an empty file that includes `core/Units.hpp` -- which is
  why one function per file is not the answer here (decision 486).
- **Modules, spiked with this toolchain** -- clang 23.1.2, CLion's CMake 4.3.1,
  Ninja, Microsoft's standard library, in scratch projects outside the
  repository:
  - a named module builds, links and runs;
  - mp-units v2.5.0 builds as modules, without `import std`;
  - CMake refuses `import std` for clang with Microsoft's library ("Only
    `libc++` and `libstdc++` are supported"), but **Microsoft's own `std.ixx`
    builds with clang 23.1.2 inside the project** and works, its leap-second
    table included -- clang 23.1.0 could not build it (fixed in 23.1.1), so
    the toolchain pin matters;
  - recompiling a file that uses mp-units: **1.4 s by import against 5.0 s by
    include**; linting it 7.4 s against 12.3 s; `import std` against four
    standard headers 1.5 s against 2.25 s;
  - clang-tidy lints an importing file once the module is compiled, which the
    project's lint steps already wait for (ADR 0024), and fails on a clean
    tree.
- **Not needed: libc++ or libstdc++.** LLVM's Windows release ships no libc++;
  libstdc++ on Windows means MinGW and a different binary interface, the
  toolchain migration working agreement 6 rules out (decision 485).

## What to do

1. **The target structure** (decision 492), settled with M1-127's rulings on
   the `Quantity` base and the `Scalar.hpp` split. The proposal, from the
   bottom: (1) `contract`, `scalar`, `units`; (2) `math` -- vectors,
   quaternions, double-double -- and `time` -- instants, leap seconds; (3)
   `orbit` and `astro` beside `view`; (4) `render`; (5) the application. The
   test helpers along the same lines, so `test_time` and `test_math` stop
   depending on the orbit code.
2. **The bottom level first, as the real test**: `core/Contract.hpp`,
   `Scalar.hpp` and `Units.hpp` become one module, with mp-units and
   Microsoft's `std` as modules beneath it, built in-tree. On that one level,
   each open point is answered by measurement:
   - the warning policy (`-Weverything -Werror`) on module code;
   - gcc 14 under WSL and the `windows-msvc` preset;
   - clang-tidy's findings in importing files -- the spike counted 2,921
     naming findings that look like mp-units' own module contents;
   - `mutants-due.py`, `lint_deps`, the mutation harness and M1-129's
     fingerprints, which read Ninja's records;
   - CLion and clangd while editing;
   - the fuzz and sanitizer builds;
   - building `std.ixx` in-tree on both machines and in the Linux trees, and
     what a Visual Studio update does to it.
3. **The decision point** (decision 493): the owner weighs every measurement
   together -- continue level by level as modules, or fall back to libraries
   with the same boundaries, from that level on or entirely.
4. **Each level its own commit**, `check` passing in both trees, so a fallback
   is possible at any level without undoing the ones before; the link-graph
   check enforcing the levels.
5. **Relint by content**, if the measurement after the levels still shows
   header or interface edits relinting what did not change (amends ADR 0024).
6. **include-what-you-use**: measured on the result, for decision 494's
   ruling on M1-128.

## Questions put at the start

Whatever the bottom level's measurements raise, put to the owner at the
decision point with the figures; anything the structure itself leaves open --
a header that belongs to two levels, a test that mixes levels -- before the
level it touches.

## Done when

- [ ] The target structure ruled, with M1-127's rulings.
- [ ] The bottom level done as a module, every open point measured, and the
      decision point ruled.
- [ ] Every level done as ruled, `check` passing in both trees after each.
- [ ] M1-118's commits measured again under M1-129's fingerprints with the
      new structure, written in `STATUS.md`.
- [ ] The task's mutant file has run after the commit and its record is
      committed.

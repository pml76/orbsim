# What verification costs, measured

Kind: reference
Binding: no — this file records measurements and how to repeat them
Read when: you want to know what building, linting, testing, fuzzing or a
mutation pass costs on this machine, or you are weighing a proposal to make
any of them cheaper.

Measured on **2026-09-27**, at commit `739f102`, on the owner's request. The
question was what each verification step costs per compiler from scratch, so
that proposals to run the steps less often or to shorten them can be judged
against numbers rather than estimates. The raw data is in
[`scripts/measurements/verification-cost-2026-09-27/`](../../scripts/measurements/verification-cost-2026-09-27/).

## Conditions

- **Machine**: Intel Core i9-12900H, 20 threads, 32 GB; NVIDIA RTX A2000.
  clang 23.1.0, CLion's CMake 4.3.1, Ninja 1.12.0, MSVC 19.51 (VS 18
  Insiders). The versions are in `machine-state.txt` in the raw data.
- **Not an idle machine.** CLion (2.5 to 4.0 GB), Rider (1.1 GB), Teams and
  OneDrive stayed open throughout. CPU load was 24 % before the first step.
  The samples (`cpu-load.csv`, `cpu-mem-2.csv`, `cpu-mem-3.csv`, taken every
  30 s) average 25 % to 76 %. Free memory fell as low as 3.8 GB.
- **Fresh trees** under `build/timing/<name>`, configured with the same
  preset, and the same `FETCHCONTENT_SOURCE_DIR_*` overrides, as the tree they
  stand for. Library sources that are cached were reused; the rest were cloned
  inside the configure step, as on any new machine.
- **Every step ran alone**, one after another, serially, as `check` runs them.
- **Memory decided two settings.** The first Debug lint ran at Ninja's default
  of about 22 parallel jobs and was stopped by the session for low memory:
  each clang-tidy process needs about 1.04 GB. At the owner's ruling, every
  lint step after that ran with at most 10 jobs, and the release tree's lint
  was timed again at that cap. A second stop came during a WSL compile, and a
  third during the mutation pass, which from M1-10 onwards ran with
  `CMAKE_BUILD_PARALLEL_LEVEL=10`, again at the owner's ruling.
- **Dropped at the owner's ruling:** the WSL trees (`linux-sanitize`,
  `linux-gcc`) and `linux-fuzz` after the second memory stop, and a mutation
  pass under MSVC after the Windows clang one. The only WSL figure is
  `linux-sanitize`'s configure, 346.0 s.

## 1. Building and checking each tree from scratch

Wall-clock seconds.

| Tree | configure | compile | lint | format-check | doc-links | mutant-anchors | tests | total |
|---|---|---|---|---|---|---|---|---|
| `relwithdebinfo` | 586.3 | 232.5 | 313.1 (273.5 uncapped) | 2.4 | 3.4 | 1.3 | 47.9 | 1,187 |
| `debug` | 527.2 | 199.6 | 267.4 | 3.2 | 4.6 | 1.7 | 113.0 | 1,117 |
| `relwithdebinfo`, again | 519.6 | 215.0 | 228.2 | 2.4 | 3.5 | 1.0 | 51.6 | 1,021 |
| `asan` | 596.3 | 197.5 | 206.4 | 2.2 | 1.0 | 1.0 | 98.3 | 1,103 |
| `windows-msvc` | 305.3 | 190.9 | not applicable | 10.0 | 11.1 | 7.6 | 121.2 | 646, without lint |
| `windows-fuzz` | 201.0 | 178.5 | — | — | — | — | — | 380 |

- **Tests.** Every tree passed all its tests: 266 in each clang tree, 265
  under MSVC, whose missing case is the one that needs `_Float16`. Nothing
  failed anywhere, and AddressSanitizer reported nothing.
- **MSVC's small numbers are mostly start-up.** Each MSVC step includes
  `vcvars64.bat`, which alone takes 5.8 to 7.4 s. It also prints
  "`vswhere.exe` is not recognized" every time; the message is harmless,
  because every build found its compiler.
- **MSVC's lint is not a result.** clang-tidy reading `cl.exe`'s flags treats
  `/Wall` as every warning, including C++98-compatibility ones, and fails on
  `core/Scalar.hpp`. The gates never lint this tree: linting is clang-tidy
  whatever the compiler. The first 200 lines of its log are kept.
- **Configure is the largest single step.** The release tree's main configure
  took 580.4 s and ran 370 compiler feature checks, mostly for SDL3, OpenEXR
  and Imath, one after another. A fresh tree is rare in daily work, but
  `scripts/measure-frame-cost.py` configures two on every run.
- **Noise.** The two identical release builds differ by 11 % in configure, 8 %
  in compile and 27 % in lint (313.1 s against 228.2 s at the same cap). Below
  about 30 %, a difference between two single runs on this machine is not a
  result.

### Where the build's work goes

CPU-seconds from each tree's `.ninja_log` (latest run of each step). They were
recorded with many steps in parallel, so single-step times include competition
for the processor.

| Tree | third-party | our 71 sources | 39 header self-checks | link | lint, 48 files |
|---|---|---|---|---|---|
| `relwithdebinfo` | 2,248 | 881 | 295 | 94 | 2,081 |
| `debug` | 1,958 | 693 | 276 | 121 | 2,147 |
| `asan` | 1,950 | 1,391 | 557 | 182 | 1,581 |
| `windows-msvc` | 1,414 | 1,405 | 761 | 218 | — |

- **One small file is the slowest lint step in every tree.**
  `tests/AbortBehaviour.cpp` has 60 lines, but it is compiled into 23
  programs. `compile_commands.json` therefore holds 23 entries for it, and
  clang-tidy checks all 23: 114 to 171 s per tree. The large test files
  follow: `test_time.cpp`, `test_orbit_scales.cpp` and `test_fixture_file.cpp`,
  each 83 to 164 s.
- **Our slowest compiles** are the same large test files, 25 to 67 s each,
  plus `main.cpp` and `ResolvePass.cpp` in Debug.

## 2. Tests, and whether Defender slows them

- **A fresh tree's tests are slower than a warm one's, but only a little.**
  Debug took 107.5 s of test time fresh and 103.3 s warm. The single cases
  that are slow in a fresh tree are fast when warm: 10.39 s to 0.05 s, and
  6.52 s to 0.81 s.
- **A program with new content pays a one-off start cost**, measured with a
  copy of `test_render_quality.exe` and 16 random bytes appended after the
  program image:

  | Start | Median | Range |
  |---|---|---|
  | new content | 0.964 s | 0.917 – 1.102 |
  | known content, new file name | 0.208 s | 0.176 – 0.227 |
  | repeat start | 0.032 s | 0.029 – 0.037 |

  Every copy was already in the disk cache. Over ten new starts, Defender's
  service `MsMpEng.exe` used about 0.57 CPU-seconds each, and `cyserver.exe`,
  a second security product, 0.10 to 0.36. Defender's real-time protection is
  on; its exclusion list could not be read without administrator rights.
  **About 4 s per fresh test run.**
- **Start-up per case**: CTest starts a program once per test case. A start
  costs 33 to 80 ms, about 45 ms typically, so about 11 s over the 253
  Catch2 cases.
- **Unexplained: about two cases per Debug run stall for about 10 s.** They
  are different cases on different runs. Sixty direct starts of two of them
  took 0.027 to 0.046 s each. It has not been reproduced outside CTest.
- **The rest is the tests' own work.** 21 cases over one second account for
  68.6 s of a warm Debug run's 103.3 s.

## 3. Fuzzing, Windows

Each run started from an empty set of saved inputs (a *corpus*), as the gates
run it today. There were no findings in any run.

| Run | Inputs | Per second | Edges at 60 s / 240 s / end | Features at 60 s / 240 s / end |
|---|---|---|---|---|
| `fuzz_orbit`, 240 s | 13,623,583 | 56,529 | 166 / 166 / 166 | 409 / 411 / 411 |
| `fuzz_time`, 240 s | 4,378,369 | 18,167 | 527 / 531 / 531 | 1,265 / 1,275 / 1,275 |
| `fuzz_orbit`, 1,800 s | 115,690,657 | 64,236 | 166 / 166 / 166 | 410 / 412 / 412 |
| `fuzz_time`, 1,800 s | 37,288,995 | 20,704 | 527 / 533 / 533 | 1,264 / 1,443 / 1,453 |

*Edges* are distinct branches of the code reached. *Features* are libFuzzer's
finer count, which includes how often each branch is taken.

- **`fuzz_orbit` found everything within the first 4 minutes.** The next 26
  minutes and about 100 million inputs added nothing.
- **`fuzz_time` was still finding new paths at 240 s.** In the long run it
  jumped between 2 and 4 minutes (1,269 to 1,443 features) and grew until
  about 900 s. The standard 240 s budget stops it before it has finished
  finding what it can reach.

## 4. The mutation pass, Windows clang

All eleven mutant files, each in the tree it names. M1-08 and M1-09 ran at
full build parallelism; the other nine at 10 jobs.

| File | Tree | Mutants | Seconds | Per mutant | Caught | Survived (declared) | By a test | By a `static_assert` |
|---|---|---|---|---|---|---|---|---|
| M1-08 | release | 12 | 142.1 | 11.8 | 12 | 0 | 9 | 3 |
| M1-09 | release | 15 | 285.1 | 19.0 | 14 | 1 | 5 | 9 |
| M1-10 | release | 14 | 224.6 | 16.0 | 14 | 0 | 8 | 6 |
| M1-11 | release | 14 | 235.8 | 16.8 | 14 | 0 | 13 | 1 |
| M1-12, release half | release | 2 | 121.6 | 60.8 | 2 | 0 | 0 | 2 |
| M1-12 | Debug | 26 | 519.3 | 20.0 | 26 | 0 | 6 | 20 |
| M1-13 | Debug | 18 | 258.4 | 14.4 | 18 | 0 | 9 | 9 |
| M1-14 | Debug | 17 | 248.1 | 14.6 | 12 | 5 | 12 | 0 |
| M1-15 | Debug | 29 | 329.4 | 11.4 | 25 | 4 | 17 | 8 |
| M1-16 | Debug | 21 | 514.2 | 24.5 | 20 | 1 | 17 | 3 |
| M1-87 | release | 12 | 373.9 | 31.2 | 12 | 0 | 5 | 7 |
| **All** | | **180** | **3,252.5** | **18.1** | **169** | **11** | **101** | **68** |

- **Every file came out exactly as it expects.** Every survivor is declared,
  and no mutant was invalid or hung.
- **One pass is 2 to 9 minutes**, not the half hour to an hour per pass that
  had been estimated from `mutate.py`'s comment. What made past tasks
  expensive was running passes again: M1-12 ran six times, M1-13 three.
- **The mutated file's place in the include graph decides the cost**, unless
  a `static_assert` stops the build early.
  - Mutants that reach a test through the core headers cost 31 s each
    (M1-87) and 61 s (M1-12's release half), because nearly everything
    recompiles.
  - M1-12's Debug file mutates the same headers but averages 20 s, because
    most of its mutants die on a `static_assert` in the first file compiled.
  - Mutants in one source file cost 11 to 17 s.
- **38 % of mutants die at compile time**, on a `static_assert`, before any
  test runs.
- **Observation: one kill came from a different test than the one recorded.**
  M1-08's "the date is 1.7 ms out" mutant is caught today by "the distance is
  the length of the position", while `VERIFICATION.md` names the span cases.
  It is still caught; only the note is out of date.

## What went wrong on the way, and what it cost

- **The mutation pass left a mutant in the working tree.** A stop for low
  memory came in the seconds between one mutant file finishing and the next
  starting, so the harness's `finally` never ran. `src/view/Mat4.hpp` held
  M1-10's "the perspective divide multiplies by w". The change was compared
  with the mutant file, found to be exactly that mutant, and restored from
  git. From then on the driver checked after every file that `src/`,
  `tests/`, `shaders/` and `scripts/` still matched `HEAD`.
- **Samplers stopped with the jobs.** `cpu-load.csv` covers 08:07–08:37 local
  time, `cpu-mem-2.csv` 08:44–09:49 and `cpu-mem-3.csv` 10:01–11:21. The
  resumed mutation pass has no samples.

## How to repeat it

From the repository root, with CLion's CMake. For each tree:

```
cmake --preset <preset> -B build/timing/<name> [-DFETCHCONTENT_SOURCE_DIR_<LIB>=build/_deps-cache/<lib> ...]
cmake --build build/timing/<name>
cmake --build build/timing/<name> --target lint -j 10
cmake --build build/timing/<name> --target format-check
cmake --build build/timing/<name> --target doc-links
cmake --build build/timing/<name> --target mutant-anchors
ctest --test-dir build/timing/<name> --output-on-failure --output-junit <file>
```

For MSVC, each command runs after `vcvars64.bat`, with
`-DCMAKE_MAKE_PROGRAM=C:/Strawberry/c/bin/ninja.exe` at configure. Then:

- **Fuzzing:** `fuzz_<target>.exe -max_total_time=<s> -print_final_stats=1`,
  from an empty directory, with every output line timestamped.
- **Mutation:** `python scripts/mutate.py scripts/mutants/<file>.json --tree
  build/timing/<tree>`, with `CMAKE_BUILD_PARALLEL_LEVEL=10`.

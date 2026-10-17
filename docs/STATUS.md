# orbsim — status

Kind: reference
Binding: no — this file states facts, not rules
Read when: you need a version, a count, or "what is next". Update it whenever
one of those changes.

**Every number in this project that goes stale lives here, and only here** --
or, for the test suites one by one, in [`TEST_SUITES.md`](TEST_SUITES.md)
(register decision 598). The record of finished work this file used to carry
is [`HISTORY.md`](HISTORY.md) section 7, word for word.
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

Last updated: 2026-10-16.

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
| Last task completed | **[M1-118](plan/tasks/m1-118-core-fixes.md), the simulation core's review findings, 2026-10-11** (register decisions 465-482): every finding fixed, each test seen failing first; `check` passed in both trees, 557 of 557; both fuzzers ran clean for their budgets; the mutation pass caught 20 of 20. **Every task finished before it, back to milestone 0, is in [`HISTORY.md` section 7](HISTORY.md#7-statusmds-record-of-the-work-moved-2026-10-16)**, word for word as this table held it until 2026-10-16 |
| Next task | **Finishing [M1-129](plan/tasks/m1-129-tests-by-fingerprint.md)**, tests that run only when what they read has changed: built and committed as a checkpoint on 2026-10-15, not pushed -- reproducible builds, mutation passes that leave nothing to rebuild, read records, fingerprints, `check-full` and the pre-push hook, and item 9's steps A and B, the assertion message and `tests/TestCase.hpp`. **`check` passes in both trees, 579 of 579 each** (2026-10-15). What is left -- its mutants, the before-and-after measurements, the documents, the mutation passes of decision 580, `check-full` and the push -- is the task document's "Where it stands". **2026-10-16, the finish's first commit** (decisions 601-613, then 614-616): the gaps a spike found closed by tests, each seen failing first; 55 new mutants, in `m1-129.json` and a new `m1-129-debug.json`; ADR 0027; the documents; both fuzzers run clean for their budgets on the new assertion handler, `fuzz_time` adding 9 inputs to its corpus. `check` passes in both trees, 580 tests, 147 run and 433 skipped as unchanged in each. **Then the records compared with what the tests open** (612): one read left out in 480 test processes, now recorded (614), and Windows' leap-second registry value added to the machine part (615). **And a defect the timings found**: after a relink to the same bytes, `mutants_due_probe_shaders` failed, since a probe's sidecar was trusted only by its time; it is now also trusted by its contents (616). **Not pushed**; `check` passes in both trees. **Next**: the task document's "Where it stands" lists it in order -- the "after" timings, the mutation passes, the records, `check-full`, the push. **Then [M1-131](plan/tasks/m1-131-coverage-hook.md) to [M1-133](plan/tasks/m1-133-selection-proved.md)**, item 9's steps C to E, split out on 2026-10-16 (decision 596), every question they raised ruled (582-595). Scratch on the first machine: `build/item9-reshape/`, removed when M1-129 is done; `build/wthook`, `build/wtfn`, `build/wtfn-spike/` and `build/item9-prototypes/`, kept for M1-131 to M1-133 and removed after them; and `build/wt129`, M1-129's "before" tree, kept for M1-133 (decision 611). **Then, lower levels first (decision 495):** M1-127; [M1-130](plan/tasks/m1-130-code-as-modules.md), the code as C++20 modules (decisions 485, 492-494); M1-119; M1-121; M1-120, M1-112, M1-113; M1-128; M1-122 to M1-124; M1-125 last. **M1-126 runs on the second machine** whenever the owner is next at it, and blocks nothing. M1-129's progress as this row recorded it, with its measurements, is in `HISTORY.md` section 7 |
| Still open, for whoever reaches it | **Since the phase A gate, 2026-10-07.** The row as it stood before, with where each item went, is [`HISTORY.md` section 6](HISTORY.md#6-the-open-items-carried-to-the-phase-a-gate-2026-10-07); what it handed on is in M1-47, M1-51, M1-76 and M1-111 to M1-114. **For whoever runs a long job:** (1) **CLion reloads CMake on its own** when scripts, git or the mutation harness change a CMake file, whatever its setting says; keep it closed, or check its log, while anything else configures or builds (`PROJECT_STATE.md` section 2). (2) **Claude Code stops background work when memory runs short**, and a stopped mutation pass leaves its mutant in the source and mutated programs in the build tree: restore the file from git and let `check` rebuild, since a suite run by hand does not. It happened again at the gate, `m1-110`'s mutant in `src/app/main.cpp`; running one file at a time went through. (3) **A machine that sleeps mid-run trips the harness's time limits**, which count wall-clock time: keep it awake. (4) **The build pool is sized from total memory**, so close a virtual machine before a long build, or cap it with `CMAKE_BUILD_PARALLEL_LEVEL`. (5) **The second machine's WSL holds NixOS, with no clang or gcc**, so `linux-sanitize` and `linux-gcc` run on the first; and from Git Bash there, start `wsl` from PowerShell, since Git Bash rewrites its paths, and write a `.bat` with an editor, since `printf` loses its backslashes. `cmake-build-debug/` is left over there and can be deleted. **Known gaps, declared:** 22 mutants in `scripts/mutants/` survive with their reasons, among them the readback barrier no test can see (decision 206), the extension request the drivers do not need (decision 316), the tint and the depth write handed to M1-76, and `m1-109.json`'s three, which no test reaches; and Bresenham's rule survives on the RX 7900 XTX, which lights the same pixels either way (decision 411). **Not yet fixed or measured:** (a) *(to [M1-131](plan/tasks/m1-131-coverage-hook.md), decision 601, by way of M1-129, decision 495)* **a test that starts the application without writing a sidecar counts every shader** -- the smoke tests, the benchmark and golden acceptance -- so six files stay due on any shader change (decision 394); (b) **the 400 km jitter sequence cannot fail for the narrowing defect** (decision 321): its steps are 0.0003 px, and the 1 km sequence is where that defect is caught; (c) **`fuzz_time`'s coverage varies between runs**, 1,273 to 1,502 features at 900 s; (d) **a refusal by `configure-current` waits for compiles Ninja has already started**, 27 s measured; making `check`'s other steps wait for it would cost about half a second a run, not done; (e) **the RX 7900 XTX has not run the gate's pass**: every file judged on a graphics card is due there, since its records name the RTX A2000 (decision 397), and that pass is the first to judge `m1-23.json`'s golden-caught mutant and `orbsim_smoke_bare` on that card; (f) *closed by M1-115*: `scripts/mutate.py` named a skipped Catch2 case among the cases that caught a mutant. |
| Reference data | **The Earth's orientation at 1,029 epochs, 1900-2100**, from Skyfield 1.55 -- the celestial-to-terrestrial rotation, committed as [`data/skyfield/earth-orientation.txt`](../data/skyfield/earth-orientation.txt), which M1-07 asserts 0.1 mas against. **The geocentric Sun at 40 epochs, 2000-2050**, from JPL Horizons (DE441), generated into gitignored `data/horizons/` and converted by `scripts/horizons-fixture.py` -- **asserted against since M1-08**, at 0.02″ and 5e-8 AU, which is the first budget this project has claimed against Horizons itself. SHA-256 `61291048...7fd53`, committed in [`data/horizons/checksums.sha256`](../data/horizons/checksums.sha256) and verified by `check`. **A fresh clone has none**: run the recipe in [`data/horizons/README.md`](../data/horizons/README.md), or the suites that need it report themselves skipped. **And TDB - TT at 1,975 epochs, 1900-2100**, from Skyfield 1.55, **committed** as [`data/skyfield/tdb-minus-tt.txt`](../data/skyfield/tdb-minus-tt.txt) -- SHA-256 `e80fdf8b...7dc171` when generated -- so every clone checks against it |
| Leap-second table valid until | **2027-01-01T00:00:00 UTC**, from IERS **Bulletin C 72** (Paris, 2026-07-06), which rules out a leap second at the end of December 2026 and says nothing later. Past it, every UTC conversion reports `LeapSecondTableExpired` by name rather than extrapolating. Renewing it is four lines in [`src/core/LeapSeconds.hpp`](../src/core/LeapSeconds.hpp), which says how |
| Phase order | A → B → D → C → E → F → G |
| After milestone 1 | **Proposed, not settled**: [`plan/milestones.md`](plan/milestones.md), added 2026-09-24 — eight further milestones, M2 the vessel through M9 the platform, under eight rulings the owner gave that day. It is `Status: proposed`, so it binds nothing and amends nothing; its section 8 lists what accepting it would change. Until then the only settled plan is milestone 1. It also carries the list of what [`plan/realism.md`](plan/realism.md) does not cover, which is the vessel, persistence, replay, docking, aerodynamic flight, surface operations, instruments and audio — that document is a gap analysis of the physics and the image, not of the simulator |
| Working branch | `master`. **Pushed up to `848fe5c`**, M1-129's item 7, 2026-10-14; the checkpoint `f8f3cd1` of 2026-10-15 and every commit after it are not pushed -- the push waits for M1-129's finish (decisions 535, 580 and 595). Every earlier push, as this row recorded it, is in `HISTORY.md` section 7 |

| Component | State |
|---|---|
| All of them | What each directory under `src/`, and `shaders/` and `tests/`, holds, task by task, is in `HISTORY.md` section 7 as this table held it until 2026-10-16; the code itself is the current word |

## Test suites

**One by one, with their counts and what each covers: [`TEST_SUITES.md`](TEST_SUITES.md).**
The totals, 2026-10-16, at M1-129's finish: CTest runs **580 tests in each `check` tree**;
the suites hold **3,941,219 assertions in 479 test cases**, summed from each suite's own summary in the
Debug tree -- the same as Catch2's report of every suite gave at the checkpoint (register decision 579).

## What this machine has

Recorded because "it builds here" is only useful with the versions attached.
How to install and run any of it is
[`PROJECT_STATE.md`](PROJECT_STATE.md) section 2.

| Tool | Version | Location on this machine |
|---|---|---|
| clang / clang-tidy / clang-format | **23.1.2** | `C:\GitHub\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin` -- since 2026-10-03, when both trees were set up again with it; 23.1.0 is no longer on disk |
| CMake (the one that works) | 4.3.1, CLion-bundled | `C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe` |
| CMake (on PATH) | 3.31.2 | `C:\Program Files\CMake\bin` |
| Ninja | 1.12.0 | `C:\Strawberry\c\bin` |
| Vulkan SDK | 1.4.357.0 | `C:\VulkanSDK\1.4.357.0` |
| Python (for the format hook) | 3.14.0 | `C:\Program Files\PyManager` |
| MSVC toolchain | **VS 18 Insiders, MSVC 14.51.36231** | clang targets the MSVC ABI and needs its headers and libs. Upgraded from VS2022 Community 14.44.35207 on 2026-09-13, and the 2022 install is gone: `C:\Program Files\Microsoft Visual Studio\18\Insiders\` is the only one on disk, so clang now emits `-fms-compatibility-version=19.51`. A new standard library is a full rebuild of every tree — the same argument as the clang-upgrade gotcha in [`PROJECT_STATE.md`](PROJECT_STATE.md) section 8 |
| WSL 2 + Ubuntu | 26.04 LTS ("resolute") | Installed 2026-09-07. clang 23.1.1, gcc-14 14.3.0, cmake 4.2.3, ninja 1.13.2. This is where UBSan and the second compiler live |
| cppcheck | **2.22.0** | `C:\GitHub\cppcheck-2.22.0` -- built from source with the clang above on 2026-10-10 for M1-116, run once as a measurement and not part of `check` (decisions 436-438); how to build it is `PROJECT_STATE.md` section 2 |
| include-what-you-use | **0.27**, on clang 23.1.2 | `C:\GitHub\iwyu-0.27` -- the same, with zlib 1.3.2 and zstd 1.5.7 built static beside it; its own 89 unit tests pass |
| GPU in use | NVIDIA RTX A2000, Vulkan `10de-25ba` | The renderer requires a discrete GPU where one exists. The Intel UHD Graphics beside it is `8086-4626`, and runs only with the NVIDIA driver hidden (`VK_LOADER_DRIVERS_DISABLE=nv-vk64.json`) |
| CPU | Intel Core i9-12900H | Every build assumes x86-64 with MMX, SSE to SSE4.2, FMA3, AVX, AVX2, F16C and BMI2 -- what this CPU and AMD's Zen 2 share; SSE4A is left out because this CPU lacks it ([ADR 0023](adr/0023-the-processor-we-assume.md)) |

**The second machine, set up 2026-10-03** -- the owner's, `raa`. Where a row is not here it was
not measured:

| Tool | Version | Location on this machine |
|---|---|---|
| clang / clang-tidy / clang-format | **23.1.2** | `C:\GitHub\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin` -- the same release branch, as working agreement 8 asks |
| CMake (the one that works) | 4.3.1, CLion-bundled | `C:\Users\peter\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe` |
| CMake (on PATH) | 4.0.3 | `C:\Program Files\CMake\bin` |
| Ninja | 1.12.0 | `C:\Strawberry\c\bin` |
| Vulkan SDK | **1.4.363.0** | `C:\VulkanSDK\1.4.363.0` -- newer than the first machine's, kept by the owner's choice (decision 275 records the one difference seen, which the GPU explains) |
| Python | 3.13.5 | `C:\Program Files\Python313` |
| MSVC toolchain | VS 18 Insiders, `cl` 19.51.36260 | **The `windows-msvc` tree is set up here since 2026-10-04** (M1-19, decision 311): the whole project builds and 383 of 383 tests pass. Configured and built from a batch file calling `vcvars64.bat`, written with an editor rather than `printf` |
| WSL 2 | NixOS, kernel 6.6.87.2 | No clang or gcc installed: the Linux presets do not run here |
| GPU in use | **AMD Radeon RX 7900 XTX**, Vulkan `1002-744c`, driver 32.0.31041.1004 | The third GPU maker this project has run on, and the first AMD |
| CPU | AMD Ryzen Threadripper PRO 3955WX | Zen 2 -- the processor ADR 0023's assumed instruction set was matched to |

**One clang version, everywhere: 23.1.** 23.1.2 on both Windows machines, 23.1.1 in WSL from
`apt.llvm.org` — the same release branch; apt.llvm.org publishes branch builds
rather than the exact tag. `gcc-14` is the second *implementation* and is not
tied to that number.

Pinned dependencies: SDL3, vk-bootstrap, VMA, Vulkan-Headers,
Vulkan-Utility-Libraries, Catch2, mp-units, ERFA, stb, lodepng and OpenEXR —
eleven, all fetched at a pinned tag or commit by `CMakeLists.txt`, plus Imath,
which OpenEXR fetches at the tag this project pins. **mp-units was the first one `orbsim_core`
links**, header-only; **ERFA, since M1-05, is the first it needs at link
time** -- a static C library our build compiles from its sources, so the
headless core still needs nothing installed. Their licences, the ones decided
but not yet pinned, and which files of `bc7enc_rdo` may be compiled, are in
[`THIRD_PARTY.md`](../THIRD_PARTY.md).

## Decision records

**The list is [`adr/README.md`](adr/README.md)**, next to the records
themselves, so that browsing the directory finds it. **Twenty-seven are accepted** *(this said twenty-two until 2026-10-04, two records behind 0023 and 0024, and twenty-five until 2026-10-16, one behind 0026, when M1-129 added 0027 -- register decision 608)*:
0008 to 0015 written by
[M1-02](plan/tasks/m1-02-record-the-decisions.md) from the decisions taken on
2026-09-08, then 0016 — ERFA computes the astronomy — and 0017 — every warning
as an error — both on 2026-09-11, 0018 — `stateFromElements` reports — on
2026-09-13, 0019 on 2026-09-17, and **0020 — a transform carries the units of
both spaces it maps between — on 2026-09-20 with M1-09**, which extends 0019
from vectors to the matrices that act on them and was accepted rather than
proposed because the spike was built and measured first. **0021 followed it
the same day**, at the owner's request, and supersedes 0020's one clause that
left frames out: a transform now carries the frames it maps between, and the
transpose is typed as the **dual** map it is, which is what makes its
composition law hold for a chain containing a projection. Runtime cost
measured at nil before the design was chosen.

**0022 -- a scalar with a physical bound validates itself -- on 2026-09-22
with M1-87.** `Eccentricity` and `GravParam` hold their own bounds, so
`OrbitError::NonPositiveGravity` and its five checks are gone and three
unreachable assertions with them. It **supersedes 0019's clause** that
`Eccentricity` is an mp-units *kind* -- a class holds a bound where a kind
cannot, and is at least as non-convertible -- and 0019 carries the note back.
`GravParam` is deliberately not superseded: it keeps its quantity, so 0019's
own example, `mu / (r*r)` producing an acceleration type, still holds.

**0025 -- lines are drawn by Bresenham's rule, which every device must offer --
measured on 2026-10-03 and written with M1-19 on 2026-10-04.** The RTX A2000
lit 1,003 pixels of the `lines` probe where the RX 7900 XTX and the Intel UHD
lit 889; with the rule all three light the same 889.

**0014 was amended on 2026-09-25 by M1-15** (decision 175): its luminous
efficacy is sunlight's, 98.9225 lm/W, rather than Radiance's 179 lm/W, and an
update section at its end says why. That
turned out to be possible only after the alternative had been ruled, and the
cheaper design was put back up rather than absorbed.

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

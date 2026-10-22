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

Last updated: 2026-10-21.

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
| Last task completed | **[M1-132](plan/tasks/m1-132-function-level-selection.md), tests chosen function by function, 2026-10-21** (register decisions 701-719, and the earlier decisions that its task document gives). In the two `check` trees, `scripts/run-tests.py` also skips a test with a program that has the coverage hook when the program's bytes changed but no function that the test entered at its last pass changed ([ADR 0028](adr/0028-tests-run-when-a-function-they-entered-changes.md)). A comment line in `src/orbit/Orbit.cpp` runs no Catch2 test, where the selection by program runs approximately 500. **Its mutation pass**, 2026-10-21 in `build/debug` (decision 709 (6)): 37 mutants, 36 caught, 1 survivor as declared (decision 718), none invalid. **Pushed the same day** up to `dcc5a13`, by the owner. **Its full entry, and each task finished before it, is in [`HISTORY.md` section 7](HISTORY.md#7-statusmds-record-of-the-work-moved-2026-10-16)** |
| Next task | **[M1-133](plan/tasks/m1-133-selection-proved.md)**, M1-129's item 9 step E: the selection proved and measured. For each mutant of `m1-118.json`, each test that fails must be one of the tests that the selection runs (decision 552); and the 16 commits of M1-118 are replayed and counted by program and by function, with the causes (decisions 561 and 592). **Its questions were ruled on 2026-10-21, decisions 720-734, and its work started**: the mutant check first, in the two `check` trees, then the replay in Debug (decisions 721, 722 and 728). **The mutant check is done**, 2026-10-21: no mutant of `m1-118.json` made a test fail that the driver skipped, in Debug or in RelWithDebInfo; 3 mutants stop at the build, and the driver selected 27 to 90 of the 590 tests for each other mutant (Part 1 of [the measurement record](measurements/m1-133-selection-proved.md)). Next, the replay. M1-132 is done and pushed up to `dcc5a13`. **`check-full` passed in the two trees at `fddeefc` on 2026-10-21** (decision 720): the four commits from `ed875f9` to `fddeefc` wait for the owner's push from a terminal of their own, `git push origin fddeefc:refs/heads/master` (`BRIEFING.md` section 5). The commits of M1-133 after `fddeefc` wait for a later push. **After M1-133 comes [M1-137](plan/tasks/m1-137-selection-limits.md)**, the limits of the test selection, measured and closed (decision 744). **Then [M1-134](plan/tasks/m1-134-mutants-chosen-like-tests.md)**, mutation passes chosen the way the tests are (decision 633), written again from the start on the owner's rules R1 to R13, with SQLite for its store (decision 736); then M1-127 and the sequence of decision 495 below. Scratch on the first machine: `build/wthook`, `build/wtfn`, `build/wtfn-spike/` and `build/item9-prototypes/` stay until the owner rules on them (decision 735); and `build/wt129`, M1-129's "before" tree, removed at the end of M1-133, since no step of M1-133 uses it (decision 734). **[M1-136](plan/tasks/m1-136-fuzz-trees-lint-deps.md)**, `lint_deps` failing in the fuzz trees, proposed 2026-10-18 and postponed by the owner: its task document holds the measurements and eight questions, each with its options, costs and a recommendation, to be put to the owner before it starts. Its questions wait for the owner, but for question 7, ruled on 2026-10-18: decisions 680-689 are its (decision 680). The worktree of M1-136 in `.claude/worktrees/` is removed when its commits are on `master`, on the owner's word. **Then, lower levels first (decision 495):** M1-127; [M1-130](plan/tasks/m1-130-code-as-modules.md), the code as C++20 modules (decisions 485, 492-494); M1-119; M1-121; M1-120, M1-112, M1-113; M1-128; M1-122 to M1-124; M1-125 last. **M1-126 runs on the second machine** whenever the owner is next at it, and blocks nothing. **Register numbers:** 660-679 are M1-135's (decision 670), and 679 is not used; 680-689 are M1-136's; M1-131 used 690-700; 701-719 were ruled on 2026-10-20 and 2026-10-21, in sequence with no range kept (decision 709); 720-734 are the rulings of M1-133 before its work, 2026-10-21; 735 postpones M1-134, and 736 makes it the next task again; 737-742 are the rulings on the correction of open item (h); 743 adds rules to M1-134; 744 adds M1-137 before M1-134; 745-747 are the rulings on the base of the replay and a paragraph of PROJECT_STATE.md; 748-755 are the rulings on the count of the replay, 2026-10-22; and the next free number is 756. Since 2026-10-19 all text follows ASD-STE100 (`CLAUDE.md`, agreement 1, fifth addition; decision 693). The dictionary is applied as decision 676 says, and the spelling stays British (decision 677). What this row recorded for M1-132 is in `HISTORY.md` section 7 |
| Still open, for whoever reaches it | **Since the phase A gate, 2026-10-07.** The row as it stood before, with where each item went, is [`HISTORY.md` section 6](HISTORY.md#6-the-open-items-carried-to-the-phase-a-gate-2026-10-07); what it handed on is in M1-47, M1-51, M1-76 and M1-111 to M1-114. **For whoever runs a long job:** (1) **CLion reloads CMake on its own** when scripts, git or the mutation harness change a CMake file, whatever its setting says; keep it closed, or check its log, while anything else configures or builds (`PROJECT_STATE.md` section 2). (2) **Claude Code stops background work when memory runs short**, and a stopped mutation pass leaves its mutant in the source and mutated programs in the build tree: restore the file from git and let `check` rebuild, since a suite run by hand does not. It happened again at the gate, `m1-110`'s mutant in `src/app/main.cpp`; running one file at a time went through. (3) **A machine that sleeps mid-run trips the harness's time limits**, which count wall-clock time: keep it awake. (4) **The build pool is sized from total memory**, so close a virtual machine before a long build, or cap it with `CMAKE_BUILD_PARALLEL_LEVEL`. (5) **The second machine's WSL holds NixOS, with no clang or gcc**, so `linux-sanitize` and `linux-gcc` run on the first; and from Git Bash there, start `wsl` from PowerShell, since Git Bash rewrites its paths, and write a `.bat` with an editor, since `printf` loses its backslashes. `cmake-build-debug/` is left over there and can be deleted. **Known gaps, declared:** 28 mutants in `scripts/mutants/` survive on the RTX A2000 with their reasons (counted 2026-10-17, when every file passed; this said 22 since the phase A gate), among them the readback barrier no test can see (decision 206), the extension request the drivers do not need (decision 316), the tint and the depth write handed to M1-76, and `m1-109.json`'s three, which no test reaches; and Bresenham's rule survives on the RX 7900 XTX, which lights the same pixels either way (decision 411). **Not yet fixed or measured:** (a) *closed by M1-131* (decisions 629 and 635): each test that starts the application is fingerprinted on the compiled shaders its records name, the smoke tests, the benchmark and golden acceptance among them. A test that names the application on its command line and leaves no record in the driver's folder still counts every shader, which is the safe side -- `shader_record` and `run_tests_in_tree`. Measured 2026-10-19 in the Debug tree, a supposed change to one shader makes 9 mutant files due for `shaders/probe_port.frag`, 15 for `shaders/line.vert` and 16 for `shaders/tonemap.frag`; (b) **the 400 km jitter sequence cannot fail for the narrowing defect** (decision 321): its steps are 0.0003 px, and the 1 km sequence is where that defect is caught; (c) **`fuzz_time`'s coverage varies between runs**, 1,273 to 1,502 features at 900 s; (d) **a refusal by `configure-current` waits for compiles Ninja has already started**, 27 s measured; making `check`'s other steps wait for it would cost about half a second a run, not done; (e) **the RX 7900 XTX has not run the gate's pass**: every file judged on a graphics card is due there, since its records name the RTX A2000 (decision 397), and that pass is the first to judge `m1-23.json`'s golden-caught mutant and `orbsim_smoke_bare` on that card; (f) *closed by M1-115*: `scripts/mutate.py` named a skipped Catch2 case among the cases that caught a mutant; (g) **`scripts/mutate.py`'s writes do not retry when Windows refuses one for a moment**: in M1-129's passes of 2026-10-17 Windows refused four times to open a file of the repository for writing, `OSError: [Errno 22] Invalid argument`, cause unknown and not reproduced in 14,000 tries, and a scratch runner ran each such file again (decision 623). **The cause was found on 2026-10-21**, in M1-132's pass, which Windows refused one time, at its 32nd mutant: Windows refuses to cut the length of a file to 0 while a different program has the file mapped into its memory, with the error 1224, `ERROR_USER_MAPPED_FILE`, which Python shows as `[Errno 22] Invalid argument`; a lock gives `[Errno 13]`. The Claude desktop app runs `git status`, `git ls-files` and `git diff` against an earlier commit while files change, and Git maps a file to compare it. Measured in a scratch repository: a file rewritten in a loop while the app's two `git diff` commands ran in a loop, 602 of 2,504 writes refused, all with `[Errno 22]`; without them, 0 of 3,316. The pass ran one more time, as decision 623 says, and its first 31 verdicts were the same. A retry of a second or two in its writes is the fix; since the harness is an input of every mutant file, it makes all of them due, so it belongs with a task that runs them anyway. (h) *closed by M1-133* (decisions 737-742): `mutants_due_probe_shaders` failed for a cause that was not the change, found 2026-10-21 by M1-133's mutant check. When the driver skips a test as unchanged, it now gives the stored list of shaders of that test the hash of the current application, before CTest starts (`75d2a9f`). Measured again with mutants 10 and 19 in the two trees: the test passed, and no selection changed (the measurement record of M1-133). |
| Reference data | **The Earth's orientation at 1,029 epochs, 1900-2100**, from Skyfield 1.55 -- the celestial-to-terrestrial rotation, committed as [`data/skyfield/earth-orientation.txt`](../data/skyfield/earth-orientation.txt), which M1-07 asserts 0.1 mas against. **The geocentric Sun at 40 epochs, 2000-2050**, from JPL Horizons (DE441), generated into gitignored `data/horizons/` and converted by `scripts/horizons-fixture.py` -- **asserted against since M1-08**, at 0.02″ and 5e-8 AU, which is the first budget this project has claimed against Horizons itself. SHA-256 `61291048...7fd53`, committed in [`data/horizons/checksums.sha256`](../data/horizons/checksums.sha256) and verified by `check`. **A fresh clone has none**: run the recipe in [`data/horizons/README.md`](../data/horizons/README.md), or the suites that need it report themselves skipped. **And TDB - TT at 1,975 epochs, 1900-2100**, from Skyfield 1.55, **committed** as [`data/skyfield/tdb-minus-tt.txt`](../data/skyfield/tdb-minus-tt.txt) -- SHA-256 `e80fdf8b...7dc171` when generated -- so every clone checks against it |
| Leap-second table valid until | **2027-01-01T00:00:00 UTC**, from IERS **Bulletin C 72** (Paris, 2026-07-06), which rules out a leap second at the end of December 2026 and says nothing later. Past it, every UTC conversion reports `LeapSecondTableExpired` by name rather than extrapolating. Renewing it is four lines in [`src/core/LeapSeconds.hpp`](../src/core/LeapSeconds.hpp), which says how |
| Phase order | A → B → D → C → E → F → G |
| After milestone 1 | **Proposed, not settled**: [`plan/milestones.md`](plan/milestones.md), added 2026-09-24 — eight further milestones, M2 the vessel through M9 the platform, under eight rulings the owner gave that day. It is `Status: proposed`, so it binds nothing and amends nothing; its section 8 lists what accepting it would change. Until then the only settled plan is milestone 1. It also carries the list of what [`plan/realism.md`](plan/realism.md) does not cover, which is the vessel, persistence, replay, docking, aerodynamic flight, surface operations, instruments and audio — that document is a gap analysis of the physics and the image, not of the simulator |
| Working branch | `master`. **Pushed up to `dcc5a13`, the end of M1-132, on 2026-10-21 at 13:27**, by the owner from a terminal of their own, after `check-full` in the two trees at `dcc5a13`, with SSH on port 22: the SSH key is locked with a passphrase, and the SSH agent of Windows did not run, thus the assistant's push waited for a passphrase that nobody could see and was stopped before it sent anything. The remote showed `dcc5a13` after the push: the 19 commits from `d8e1014` to `dcc5a13`. `ed875f9`, the rule of ADR 0028 in `CLAUDE.md` and in the briefing, and the commit that writes this row wait for the next push, as the owner ruled. Before them, pushed up to `cc29285`, the record of the push of M1-135, on 2026-10-20 at 11:05, as the record of the remote in git shows. Before them, **pushed up to `1c4c502`, the end of M1-135, on 2026-10-20 at 10:49**, on the word of the owner, after `check-full` in the two trees, with SSH on port 22, and a fetch showed them on the remote: the four commits from `57919b3` to `1c4c502`. Before them, pushed up to `b4ead1a`, the record of the push of M1-131, on 2026-10-20 at 08:58, as the record of the remote in git shows. Before them, **pushed up to `4cd5e13`, M1-131's finish, 2026-10-19**, on the owner's word, after `check-full` in both trees, which the pre-push hook requires, and confirmed by a fetch: 36 commits, from `c088469` of 2026-10-17 through `4cd5e13`. This network refused SSH on port 22 that day, so the push went one time over SSH on port 443, to ssh.github.com, with the host key checked against the trusted github.com key and no setting changed (decision 700). Before them, pushed up to `512ef28`, 2026-10-17, each push after `check-full` in both trees and confirmed by a fetch: M1-129's finish, from the checkpoint `f8f3cd1` of 2026-10-15 through `28fd3c5`, that afternoon; then decision 624 (`1837592`) and that row's correction, that evening. Before them, pushed up to `848fe5c`, M1-129's item 7, 2026-10-14. Every earlier push, as this row recorded it, is in `HISTORY.md` section 7 |

| Component | State |
|---|---|
| All of them | What each directory under `src/`, and `shaders/` and `tests/`, holds, task by task, is in `HISTORY.md` section 7 as this table held it until 2026-10-16; the code itself is the current word |

## Test suites

**One by one, with their counts and what each covers: [`TEST_SUITES.md`](TEST_SUITES.md).**
The totals, 2026-10-19, at M1-131's end, and 2026-10-20 for the count of CTest tests: CTest runs **590 tests in each `check` tree** since M1-132's steps 1 and 4 added `compiled_code_self_test` and `run_tests_function_level`;
the suites hold **3,941,234 assertions in 483 test cases, in 44 suites**, summed from each suite's own
summary in the Debug tree.

## What this machine has

Recorded because "it builds here" is only useful with the versions attached.
How to install and run any of it is
[`PROJECT_STATE.md`](PROJECT_STATE.md) section 2.

| Tool | Version | Location on this machine |
|---|---|---|
| clang / clang-tidy / clang-format | **23.1.2** | `C:\GitHub\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin` -- since 2026-10-03, when both trees were set up again with it; 23.1.0 is no longer on disk |
| CMake (the one that works) | 4.3.1, CLion-bundled | `C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe` |
| CMake (on PATH) | **4.4.4** | `C:\Program Files\CMake\bin` -- measured 2026-10-17, where this row said 3.31.2; the program is dated 2026-10-02. Strawberry Perl's `C:\Strawberry\c\bin`, which also holds Ninja, comes earlier on PATH and carries CMake 3.29.2, so a bare `cmake` finds that one; the trees are configured with CLion's, above |
| Ninja | 1.12.0 | `C:\Strawberry\c\bin` |
| Vulkan SDK | 1.4.357.0 | `C:\VulkanSDK\1.4.357.0` |
| Python (for the format hook) | 3.14.0 | `C:\Program Files\PyManager` |
| MSVC toolchain | **VS 18 Insiders, MSVC 14.51.36231** | clang targets the MSVC ABI and needs its headers and libs. Upgraded from VS2022 Community 14.44.35207 on 2026-09-13, and the 2022 install is gone: `C:\Program Files\Microsoft Visual Studio\18\Insiders\` is the only one on disk, so clang now emits `-fms-compatibility-version=19.51`. A new standard library is a full rebuild of every tree — the same argument as the clang-upgrade gotcha in [`PROJECT_STATE.md`](PROJECT_STATE.md) section 8 |
| WSL 2 + Ubuntu | 26.04 LTS ("resolute") | Installed 2026-09-07. clang 23.1.1, gcc-14 14.3.0, cmake 4.2.3, ninja 1.13.2. This is where UBSan and the second compiler live |
| cppcheck | **2.22.0** | `C:\GitHub\cppcheck-2.22.0` -- built from source with the clang above on 2026-10-10 for M1-116, run once as a measurement and not part of `check` (decisions 436-438); how to build it is `PROJECT_STATE.md` section 2 |
| include-what-you-use | **0.27**, on clang 23.1.2 | `C:\GitHub\iwyu-0.27` -- the same, with zlib 1.3.2 and zstd 1.5.7 built static beside it; its own 89 unit tests pass |
| ASD-STE100 Simplified Technical English | **Issue 9**, January 2025 | "ASD-STE100 Simplified Technical English.pdf", read with `pypdf` (the other copy, ASD-STE100_ISSUE9.pdf, is encrypted), in the folder ASD-STE100 at the root of the checkout, which `.gitignore` excludes: ASD owns the copyright (decision 693). Each machine needs its own copy |
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
themselves, so that browsing the directory finds it. **Twenty-eight are accepted** *(this said twenty-two until 2026-10-04, two records behind 0023 and 0024, and twenty-five until 2026-10-16, one behind 0026, when M1-129 added 0027 -- register decision 608; and twenty-seven until 2026-10-21, when M1-132 added 0028)*:
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

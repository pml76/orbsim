# M1-131 — The build with and without the coverage hook

Kind: reference
Binding: no — this file records a measurement and how to repeat it
Read when: you want the figures behind M1-131's item 4, or you are about to
take the figures with the hook and must repeat this run exactly.

**Complete; the owner's word is not given yet.** The first half, written
2026-10-18, gives the figures **without** the hook, taken that morning. The
second half, [With the hook](#with-the-hook), gives the figures **with** the
hook, taken on 2026-10-19, and sets each against its estimate. Every figure
goes to the owner before the presets carry the hook (decision 631).

## What is measured, and why

A **coverage hook** is a compiler option that makes every function do
something of its own when it is entered; M1-131 builds the one decision 559
chose, which sets a flag per function and keeps a table of the functions'
addresses beside the flags. [The task's](../plan/tasks/m1-131-coverage-hook.md)
item 4 asks for its cost to be measured **before the option is switched on in
the presets**, and every figure brought to the owner first, whatever it is
(register decision 631). The rulings give estimates, not limits, and the
figures are to be set against them
([the register](../plan/milestone-1-decisions.md)):

- **Build and test time** -- decision 559: in RelWithDebInfo "+6% build, no
  measurable test time"; in Debug, three runs each way, alternated, the
  tests took 264, 200 and 194 s without the hook and 217, 205 and 215 s with
  it.
- **The linker's maps** -- a map is a text file the linker writes beside a
  program, listing every function's address; it is what turns a recorded
  address back into a function. Decision 582: one program's map was 5.7 MB,
  "about 220-260 MB a tree, estimated from the size of the debug
  information", and a link took 20-30 ms longer with it.
- **Functions without a flag** -- decision 586: in the hooked Debug spike
  tree, 1,189 of 54,939 functions (2.2%) carried no flag, all of them
  standard-library code; the optimised tree's figure was left to this
  measurement.
- **The frame-time benchmark** -- decision 589: run on this machine with and
  without the hook and written down; on the RTX A2000 the processor's figures
  swing by up to 40 % while the GPU's agree to 0.5 %, "so only a large effect
  can show here". M1-47 takes the same both ways on the RX 7900 XTX.
- **The mutation harness's copy of the build tree** -- decision 631's one
  measurement more: before a pass, `scripts/mutate.py` copies the tree
  (decision 506), so it will copy the maps too.

Decision 641 says where: **in the two `check` trees, the option set by hand
when they are configured, the figures without the hook taken first**; if the
owner rules against the hook, a configure and a rebuild set each tree back.

## The machine

Measured 2026-10-18, 10:20-10:45 (+02:00), on the first machine, in
`build/relwithdebinfo` and `build/debug`, at commit `9061ab0` ("Presets:
Debug and RelWithDebInfo settings in two hidden bases (M1-131, 625)"), with a
clean working tree before and after. Neither tree's CMakeCache.txt named
`ORBSIM_COVERAGE_HOOK`, which did not exist yet; the trees were not
reconfigured and `CMakePresets.json` was not touched.

| | |
|---|---|
| Power | on mains: `Win32_Battery.BatteryStatus` 2 ("access to AC power") at every sample, charge 96 %; power scheme Balanced |
| Processor, memory | Core i9-12900H, 20 logical processors; 31.5 GiB, 13.8-14.8 GiB free at the samples; the build pool `orbsim_memory` has depth 10 |
| Disk C: | 106.9 GiB free of 952.1 GiB, before and after |
| Processor load at the samples | 4 % (10:20), 18 % (10:33), 9 % (10:34, before the benchmark), 12 % (10:34), 6 % (10:43), 36 % (10:45, during the last copies of the tree) |
| Also running | several Claude Code processes (about 1.5 GiB together), Brave, Outlook, VS Code (9 processes), Everything (a file indexer that follows file changes), `cyserver` (the endpoint security agent), JetBrains Toolbox. **CLion was not running.** One clangd ran throughout, started by Claude Code's language-server tool, not by CLion |

Each sample is a `state-<n>-<when>` file in [`m1-131/`](m1-131/), written by
[`m1-131/machine-state.ps1`](m1-131/machine-state.ps1), with the twelve
largest processes by memory. **The first sample was written by an earlier
version of that script**: it also tried to read the power state through
Windows Forms, which the shell's language mode refused, so
[`m1-131/state-0-start.txt`](m1-131/state-0-start.txt) holds two error
messages and an empty "power line status" line; that call was then taken
out, and the five later samples come from the version kept here.

Tools: CLion's bundled CMake (C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe),
Ninja 1.12.0 (C:\Strawberry\c\bin\ninja.exe, log format version 6), clang
23.1.2 from C:\GitHub\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin, which the
trees' rules name with its full path; Python 3.14.0 (C:\Program Files\PyManager\python),
run as `python -I`, and PowerShell 7 as `pwsh -NoProfile -File`, both from
Git Bash.

## How it was run

**Step 0, the wait.** The run began only once the `check` then running had
ended. A small shell script, not kept here, polled every 15 s until
that Debug `check`'s log held its `run-tests:` line, or the word `FAILED` or
`error`, and the process list showed no Ninja, CMake or clang process. Its
filter for clang had to leave out clangd, which a plain `clang*` matches and
which never exits while Claude Code runs. It ended at 10:19:14: the Debug
`check` had passed, "147 ran, 433 skipped as unchanged", 100 % of 147; the
RelWithDebInfo one likewise, "147 ran, 433 skipped", 100 %.

**The order, exactly as run.** `M0` stands for the folder the scripts ran
from -- that day the session's temporary folder,
C:\Users\U439644\AppData\Local\Temp\claude\C--Users-U439644-Projects-untitled\cf0d7665-e81a-4537-9c1c-f20461fb1e11\scratchpad\m0,
which is on drive C:, the disk `build/` is on.

```
pwsh -NoProfile -File M0/machine-state.ps1 > M0/state-0-start.txt                      # 10:20
python -I M0/measure-build.py 'C:\Users\U439644\Projects\untitled\build\relwithdebinfo' rel-nohook   # 10:23:53-10:32:46
python -I M0/ninja-log-analysis.py 'C:/Users/U439644/Projects/untitled/build/relwithdebinfo' M0/rel-nohook-ninja_log-before-build.txt M0/rel-nohook-ninja_log-after-build.txt --targets 15
python -I M0/ninja-log-analysis.py 'C:/Users/U439644/Projects/untitled/build/relwithdebinfo' M0/rel-nohook-ninja_log-after-build.txt M0/rel-nohook-ninja_log-after-check.txt --targets 5
pwsh -NoProfile -File M0/machine-state.ps1 > M0/state-1-rel-after-check.txt            # 10:33
python -I M0/measure-snapshot.py 'C:\Users\U439644\Projects\untitled\build\relwithdebinfo' rel-nohook
python -I M0/tree-breakdown.py 'C:\Users\U439644\Projects\untitled\build\relwithdebinfo'
pwsh -NoProfile -File M0/machine-state.ps1 > M0/state-2-before-bench.txt               # 10:34:12
bash M0/bench.sh rel-nohook                                                            # 10:34:15-10:34:35
pwsh -NoProfile -File M0/machine-state.ps1 > M0/state-3-before-debug.txt               # 10:34:44
python -I M0/measure-build.py 'C:\Users\U439644\Projects\untitled\build\debug' debug-nohook          # 10:34:55-10:43:18
python -I M0/ninja-log-analysis.py 'C:/Users/U439644/Projects/untitled/build/debug' M0/debug-nohook-ninja_log-before-build.txt M0/debug-nohook-ninja_log-after-build.txt --targets 15
python -I M0/ninja-log-analysis.py 'C:/Users/U439644/Projects/untitled/build/debug' M0/debug-nohook-ninja_log-after-build.txt M0/debug-nohook-ninja_log-after-check.txt --targets 5
pwsh -NoProfile -File M0/machine-state.ps1 > M0/state-4-debug-after-check.txt          # 10:43:56
python -I M0/measure-snapshot.py 'C:\Users\U439644\Projects\untitled\build\debug' debug-nohook
python -I M0/tree-breakdown.py 'C:\Users\U439644\Projects\untitled\build\debug'
for n in 2 3: measure-snapshot.py relwithdebinfo rel-nohook-$n, then debug debug-nohook-$n    # 10:44-10:45
pwsh -NoProfile -File M0/machine-state.ps1 > M0/state-5-end.txt                        # 10:45:19
```

What each line printed is kept under the label's name: the two analyses of
each tree as `<label>-build-analysis.txt` and `<label>-check-analysis.txt`,
`measure-build.py`'s own lines as `<label>-driver.txt`, the copies as
`<label>-snapshot.txt` and, for the second and third, `-snapshot-2.txt` and
`-snapshot-3.txt`, the extension breakdown as `<label>-tree-breakdown.txt`,
and the benchmark's printout as `rel-nohook-bench.txt`. The record of the run
does not say whether each was written with `>` or with `tee`. Apart from
these lines only light commands ran during the measurements: a `tasklist`, a
`tail` of a log, and the first analysis, which parses a 300 KB file, during
the RelWithDebInfo `check`.

**The scripts**, in [`m1-131/`](m1-131/), copied unchanged:

- [`m1-131/measure-build.py`](m1-131/measure-build.py) `<tree> <label>` runs
  exactly these three commands, each timed by `time.perf_counter()` around
  `subprocess.run`, its output to `<label>-<step>.log`, and copies the tree's
  Ninja log before the build, after it and after the check:

  ```
  C:\Strawberry\c\bin\ninja.exe -C <tree> -t clean
  C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe --build <tree>
  C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe --build <tree> --target check
  ```

- [`m1-131/ninja-log-analysis.py`](m1-131/ninja-log-analysis.py)
  `<tree> <before> <after>` reads what one Ninja run spent. Ninja records each
  step of a build -- a compile, a link, a custom command; Ninja calls it an
  *edge* -- with its start and end time. The script takes the bytes the
  "after" copy of the log has beyond the "before" copy, and refuses if the
  one is not the start of the other (Ninja rewrote the log). It groups the
  entries into edges by start, end and command hash, since an edge with
  several outputs writes one entry per output, and sorts each edge into a
  class. A compile is an edge with an output the tree's
  compile_commands.json lists; it is **the project's own** when its source
  is under the repository's `src/` or `tests/`; a **header self-check** --
  a generated file that includes one header alone, to prove the header
  compiles by itself -- when its source is in the tree's `selfcheck/`;
  **someone else's code in a top-level target** when its object is
  otherwise under `CMakeFiles/` (`orbsim_erfa`, `orbsim_lodepng`,
  `orbsim_stb_image`, `vma_impl`, ERFA's two validation programs); and **a
  dependency's** when its object is under `_deps/`. The other edges are
  links of programs, DLLs and static libraries, lint steps, and the rest
  (shaders, CMake's re-check of its file lists, the test run).
- [`m1-131/measure-snapshot.py`](m1-131/measure-snapshot.py) `<tree> <label>`
  loads `scripts/mutate.py` as a module -- its top level only defines
  functions and constants -- and calls the harness's own `list_tree`,
  `uncopied` and `take_snapshot(tree, place, {})`, with `place` the folder
  `snapshot-<label>` beside the script and never `build/mutate-snapshot-*`;
  it refuses to start if either exists. A pass hands `take_snapshot` its
  mutant file's few sources, a few hundred bytes, where this hands it none.
  It then deletes the copy with `shutil.rmtree`, timed.
- [`m1-131/tree-breakdown.py`](m1-131/tree-breakdown.py) `<tree>` lists what
  a pass copies, through the same two functions of `mutate.py`, grouped by
  file extension; it copies nothing.
- [`m1-131/bench.sh`](m1-131/bench.sh) `<label>` runs the benchmark three
  times, its files in `bench-<label>/` beside the script.
- [`m1-131/machine-state.ps1`](m1-131/machine-state.ps1) prints the samples
  above.

**No script names the temporary folder**, so none was changed: each writes
beside itself. They do name this machine's paths -- the repository, CMake,
Ninja, the RelWithDebInfo application -- so they run as they are only on the
first machine. **Run them from a copy in a scratch folder on drive C:, not
from `docs/measurements/m1-131/` itself**: `measure-snapshot.py` puts a copy
of the whole tree beside itself, 1.4-1.6 GB, and `bench.sh` 1.2 MB of tables.

## Without the hook

### 1. The full build

Two kinds of figure: the **wall clock**, the time from start to end, which
depends on how the work was spread over the processor; and the **summed
compile times**, each compile's own duration added up, which depends less on
that.

| | RelWithDebInfo | Debug |
|---|---|---|
| `ninja -t clean` | 0.7 s, "Cleaning... 1109 files." | 0.5 s, "Cleaning... 1109 files." |
| **`cmake --build <tree>`, wall clock** | **223.9 s** | **208.6 s** |
| (a) wall time from Ninja's log (last end less first start) | 223.6 s | 208.4 s |
| edges built (log entries) | 970 (1,067); `[970/970]` in the build's output | 970 (1,067) |
| **(b) the project's own compiles (`src/`, `tests/`)** | **90 objects, 1,279.5 s summed** | **90 objects, 1,128.5 s summed** |
| header self-checks (generated) | 58, 474.6 s | 58, 501.4 s |
| someone else's code in top-level targets | 252, 167.6 s | 252, 120.8 s |
| dependencies' compiles (`_deps/`) | 492, 1,254.6 s | 492, 1,183.4 s |
| all compiles | 892, 3,176.3 s | 892, 2,934.2 s |
| **(c) every compile but the project's own** | **802, 1,896.8 s** | **802, 1,805.7 s** |
| links of programs (.exe), top-level | 53, 111.3 s | 53, 133.8 s |
| static libraries, top-level / `_deps/` | 7, 17.7 s / 6, 17.2 s | 7, 16.7 s / 6, 15.2 s |
| DLL link (`_deps/`, SDL3) | 1, 1.2 s | 1, 1.4 s |
| other edges (shaders, CMake's re-check) | 11, 13.0 s | 11, 11.9 s |
| every edge, summed | 3,336.9 s | 3,113.2 s |

The hook goes on the project's own compiles only, so (b) is where it should
show and (c) is where it should not. The 53 programs are the 51 the recorder
will be linked into (decision 639) and ERFA's two validation programs. The
largest of the project's own targets by summed compile time: RelWithDebInfo
`orbsim_view` 286.6 s (15 objects), `orbsim` 180.8 s (9), `orbsim_core`
84.2 s (6), `orbsim_test_support` 76.0 s (4); Debug `orbsim_view` 253.3 s,
`orbsim` 148.6 s, `orbsim_core` 92.8 s, `orbsim_test_support` 59.6 s. The
full tables and the three longest steps of each class are in
[`m1-131/rel-nohook-build-analysis.txt`](m1-131/rel-nohook-build-analysis.txt)
and [`m1-131/debug-nohook-build-analysis.txt`](m1-131/debug-nohook-build-analysis.txt).

**The default target builds everything `check` needs**: the `check` that
followed compiled and linked nothing, in either tree.

### The `check` after it

| | RelWithDebInfo | Debug |
|---|---|---|
| `cmake --build <tree> --target check`, wall clock | 308.6 s, exit 0 | 293.9 s, exit 0 |
| tests | **passed**: "24 ran, 556 skipped as unchanged"; 100 % of 24 passed | **passed**: "18 ran, 562 skipped as unchanged"; 100 % of 18 passed |
| lint steps (clang-tidy, and its `--verify-config`) | 95, 2,503.6 s summed | 95, 2,328.8 s summed |
| the test run's step (`CMakeFiles/check`) | 35.9 s | 38.9 s |
| `format-check`, `mutant-anchors` | 5.0 s, 2.2 s | 4.8 s, 2.0 s |

The rebuilt programs came out byte for byte as before, so the tests that ran
are those whose inputs include the tree itself: `reproducible_build`,
`configure_current`, `float_equal_guard`, the nine `mutants_due*`,
`memory_pool`, `parallel_tests`, `accept_golden`, `lint_deps`,
`run_tests_in_tree`, and test 92, the Catch2 case CTest reports as skipped,
and in RelWithDebInfo also the six abort probes, which CTest reports as
skipped there, as it did in the `check` before this one (the list is
run-tests/selected.txt in each tree). **These are not a test-time figure**:
24 and 18 of 580 tests ran.

### 2. The frame-time benchmark, RelWithDebInfo

As `docs/PROJECT_STATE.md` section 10 takes a row (decisions 374 and 383):
three runs one after the other of

```
orbsim --bench grid-orbit --frames 600 --width 1920 --height 1080
```

by `bench.sh rel-nohook`. **One difference**: `--bench-out` put each run's
summary and table in the script's own `bench-rel-nohook/` rather than the
tree's `bench/`, so the tree was left as it was. RTX A2000 8GB Laptop GPU
(10de-25ba), NVIDIA 582.53, Vulkan 1.4.312, 1920x1080, presenting without
waiting for the display, quality high, validation off; each warm-up 5.00 s
(7,258, 7,258 and 7,256 frames).

Listed largest median interval first, as section 10 lists them. The p95 and
p99 are the times 95 % and 99 % of the frames came in under.

| Run, UTC | Interval median, p95, p99 (ms) | CPU working median, p95 | GPU median, p95 |
|---|---|---|---|
| 08:34:35 | 0.4133, 1.5890, 2.2055 | 0.1124, 0.1788 | 0.1132, 0.1149 |
| **08:34:29, the headline** | **0.4038, 1.7378, 2.1966** | **0.1162, 0.2125** | **0.1132, 0.1268** |
| 08:34:22 | 0.3766, 1.7931, 2.2085 | 0.1189, 0.2224 | 0.1131, 0.1374 |

The median intervals span 9.7 % (0.3766 to 0.4133); the GPU medians agree to
0.1 %. `docs/PROJECT_STATE.md` section 11.8's warning applies: on this
processor 600 frames is under half a second, less than one of its
power-management swings (up to 40 %), so only a large effect of the hook on
the processor's side can show; **the GPU's figure is the one to compare**.
The mean intervals, 0.6692, 0.6675 and 0.6666 ms, are in the summaries too:
[`m1-131/bench-rel-nohook/`](m1-131/bench-rel-nohook/), with the printout in
[`m1-131/rel-nohook-bench.txt`](m1-131/rel-nohook-bench.txt).

### 3. The mutation harness's copy of the build tree

**What `scripts/mutate.py` copies** (decision 506, `take_snapshot`): it lists
the whole tree with `os.walk`, each file's size and modification time in
nanoseconds; copies every file with `shutil.copy2` into
`build/mutate-snapshot-<tree name>/files/`, at the same path, except those
under a dependency's downloaded sources, `_deps/<name>-src/`; lists the copy
again and requires every size and time to match; then writes sources.json
and, last, listing.json. A pass takes the copy right after
`cmake --build <tree>` and deletes it with `shutil.rmtree` at the end. **A
stopped pass is detected by the folder `build/mutate-snapshot-<tree name>`
existing at all** (`leftover_problem`); `--restore` treats one without
listing.json as never finished.

Each tree was copied three times, by `measure-snapshot.py`; no copy is left,
in `build/` or in the scratch folder.

| | RelWithDebInfo | Debug |
|---|---|---|
| tree listed | 12,073 files, 1,574 folders, 0.44 s | 9,213 files, 1,572 folders, 0.36 s |
| **copied by a pass** | **5,679 files, 1,370,918,416 bytes (1,307.4 MiB)** | **2,819 files, 1,637,860,961 bytes (1,562.0 MiB)** |
| listed, not copied (`_deps/*-src`) | 6,394 files, 417,335,047 bytes (398.0 MiB) | 6,394 files, 417,340,769 bytes (398.0 MiB) |
| the copy on disk | 5,681 files, 1,372,053,530 bytes; listing.json 1,135,112 bytes | 2,821 files, 1,638,731,345 bytes; listing.json 870,382 bytes |
| **`take_snapshot`, three runs** | **6.3, 7.5, 6.2 s** | **3.6, 4.5, 4.6 s** |
| `shutil.rmtree`, three runs | 1.9, 1.5, 2.9 s | 1.6, 0.9, 0.9 s |

The first run of each followed the tree's `check` directly, as a pass's copy
follows its build; the second and third followed one another.

What the copy holds, by file extension (`tree-breakdown.py`; the maps the
hook brings will appear as `.map`). A `.pdb` file holds a program's debug
information, and a `.f32` file a probe's frame.

| Extension | RelWithDebInfo files, MiB | Debug files, MiB |
|---|---|---|
| `.pdb` | 55, 531.7 | 56, 634.5 |
| `.f32` (probe frames) | 30, 421.9 | 31, 435.9 |
| `.obj` | 889, 136.5 | 889, 192.5 |
| `.exe` | 56, 62.2 | 56, 124.3 |
| `.lib` | 14, 47.3 | 24, 91.8 |
| `.json` | 212, 35.4 | 212, 34.6 |
| `.idx` | 2,884, 33.1 | -- |
| `.pch` | 2, 16.8 | 2, 16.8 |
| `.dll` | 2, 7.0 | 4, 19.5 |
| everything else | 1,535, 15.6 | 1,549, 12.2 |
| `.map` | none | none |

## What to bear in mind

1. **One clean build a tree cannot resolve a difference of the size ruled.**
   Decision 559 estimates +6 % of build time in RelWithDebInfo, and the
   spread from one clean build to the next on this laptop was not measured:
   one build each. The summed compile times move less with scheduling than
   the wall clock, but they still move with the processor's power
   management, which `docs/PROJECT_STATE.md` section 11.8 shows can reach
   40 % over half a second. **The run with the hook therefore takes three
   full rebuilds of each tree each way, alternated** -- without, with,
   without, with, without, with -- as decision 559's own measurement was,
   the option switched by a configure each time (decision 641), each rebuild
   by `measure-build.py` under a label of its own.
2. **Do not set the copy's figures against decision 513's.** Decision 506's
   "at most 261 MiB (Debug) and 144 MiB (RelWithDebInfo)" is its first
   ruling's; decision 513 widened the copy to the whole tree but
   `_deps/*-src` the same day and measured Debug 2,297 files, 1.38 GiB, 11.1 s
   to copy and 4.1 s to delete, and RelWithDebInfo 5,150 files, 1.16 GiB,
   22.8 s and 9.0 s (the same figures are in the comment above `UNCOPIED` in
   `scripts/mutate.py`). Today's copies hold 10-23 % more files and bytes,
   and take about a third of 513's time (27-41 %). Why they are faster was
   not looked into; 513 does not say whether its copy followed a build, as
   these did, with the files still in the disk cache. **The figures with the
   hook are to be set against this record's, taken the same way.**
3. **A stale editor index is copied by every pass.** The RelWithDebInfo tree
   holds the index an editor's language server, clangd, keeps of the code,
   `.cache/clangd/index/`: 2,884 files, 33 MiB, last written 2026-09-26. It
   is why that tree's copy has twice the Debug tree's files and takes longer
   although it is smaller. Nothing wrote to it during these measurements (no
   file in it newer than 10:17, checked at the end); the Debug tree has none.
   If it is removed before the run with the hook, that run's RelWithDebInfo
   copy is to be set against these figures less its 2,884 files and 33 MiB.
4. **`ninja -t clean` deletes the lint steps' stamps**, which are Ninja
   outputs depending on the objects, so the `check` after a clean build
   re-lints all 95 files -- about 2,300-2,500 s summed, most of `check`'s
   five minutes. That is not wrong, but the `check` figure is one with a
   full lint, and since the hook changes the compile commands clang-tidy
   reads, it is itself a with-and-without figure.
5. **Found while writing this record: these figures are at `9061ab0`,
   before items 1-3 were committed (`639059e`).** Those items add a suite
   built whether or not the option is on, `test_writer_turns`, and, with the
   option on, the recorder, the self-test's three programs and three more
   files to lint (`CMakeLists.txt` at `1c7440a`). A figure with the hook
   taken at a later commit therefore holds the task's own additions as well
   as the hook's cost; the alternated series of point 1, both ways at one
   commit, separates the two.

## With the hook

Measured 2026-10-19, 06:51-12:05 (+02:00), on the first machine, in
`build/relwithdebinfo` and `build/debug`. Two sessions did the work. The
first session stopped at 10:00, at the owner's request. The second session
continued at 10:40 and did the rest.

### State on 2026-10-19 at 10:00

This is the state that the first session left. The sections after it replace
it.

**State on 2026-10-19 at 10:00.** The measurement stopped there at the
owner's request, to continue in a new session. The figures below are raw,
and nobody has analysed them yet. The logs, the analysis outputs and the
scripts of that run are in build/m131-spikes/m0-hook/, on the first machine
only; series.sh there runs the alternated series.

| Tree | Rebuild | Hook | Full build, wall clock |
|---|---|---|---|
| RelWithDebInfo | 1 | off | 237.0 s |
| RelWithDebInfo | 1 | on | 218.9 s |
| RelWithDebInfo | 2 | off | 211.5 s |
| RelWithDebInfo | 2 | on | 216.7 s |
| RelWithDebInfo | 3 | off | 266.5 s |
| RelWithDebInfo | 3 | on | 263.4 s |
| Debug | 1 | off | 259.1 s |
| Debug | 1 | on | 328.6 s |
| Debug | 2 | off | 351.4 s |
| Debug | 3 and later | -- | not done |

Each of these builds ended with exit code 0. Each `check` after a
RelWithDebInfo rebuild passed: 563 to 566 tests ran, and 0 failed. The
benchmark ran after each RelWithDebInfo rebuild. Rebuilds of the same kind
differ by up to 55 s on the wall clock. Thus the wall clock alone cannot
show a difference of 6 %; compare the summed compile times from each
rebuild's .ninja_log. The other figures of the list are not measured yet.

### Summary

Each figure of item 4, set against the estimate or the ruling it belongs
to. The numbered sections after this table give the procedure of each
figure. Bold text marks a figure outside its estimate or its ruling.

| Figure | Estimate or ruling | Measured | Difference |
|---|---|---|---|
| Full build, RelWithDebInfo | decision 559: about +6 % | wall clock 238.3 s without, 233.0 s with; the project's own compiles divided by the dependencies' compiles, both summed: 0.979 without, 1.034 with | wall clock -2.2 %; the quotient +5.7 %, with a standard deviation of 2-3 % |
| Full build, Debug | none | wall clock 298.9 s without, 317.0 s with; the same quotient 0.961 without, 0.863 with | wall clock +6.0 %; the quotient -10.3 %, with a standard deviation of 7-14 % |
| The tests of the `check`, each test's time summed | decision 559: "no measurable test time" in RelWithDebInfo; the Debug spike of decision 559 saw no cost | RelWithDebInfo 370.3 s without, 383.5 s with; Debug 768.8 s without, 941.2 s with | +3.6 %; **Debug +22.4 %** |
| The linker's maps, one tree | decision 582: 220-260 MB | RelWithDebInfo 119.6 MB; Debug 293.4 MB | 100.4 MB below the lower end; **33.4 MB above the upper end** |
| `reproducible_build` | must pass | passed in all six checks with the hook, its NO_COLOR case included | none |
| Two functions at one address | decision 551: none | 0 addresses twice in a table; 0 entries without a function | none; **but 546 and 754 entries name one function with two names** (section 5) |
| Functions without a flag | decision 586: Debug 2.2 % | RelWithDebInfo 382 of 9,172 (4.16 %); Debug 1,301 of 64,366 (2.02 %) | all of them library code; the project defines none |
| Frame-time benchmark | decision 589: only a large effect can show | GPU median 0.1129 ms without, 0.1127 ms with | -0.2 % |
| The harness's copy | decision 631: no estimate | RelWithDebInfo 6,424 files, 1,507 MiB; Debug 3,553 files, 1,958 MiB | +745 files, +199 to +201 MiB; +734 files, +395 to +397 MiB; the time in section 8 |
| The crash handler writes, then the program ends normally | decision 634: not known | 0 of 586 processes in each tree | none |
| The longest record write | decision 638: the bound "well above" it; 10 s in the code until now | RelWithDebInfo 321.8 ms, Debug 28.4 ms; 99th percentile 8.7 ms and 12.7 ms | the 10 s is 31 times the longest write |

### How it was run

**The text that was built.** The RelWithDebInfo series began at commit
`dae49a0`. The last commit before it that changes a file the build reads is
`9eeb98b` (cmake/ScriptTests.cmake, 06:40:56). The later commit `3124b60`
changes three documents only. Thus all twelve rebuilds built the same text.
The tools are those of the first half.

**The order.** [`m1-131/series.sh`](m1-131/series.sh) `<tree> <short>` does
six full rebuilds of one tree, the hook off and on in turn: off, on, off, on,
off, on. For each rebuild it does these steps:

1. It configures the tree with `cmake --preset <tree>
   -DORBSIM_COVERAGE_HOOK=OFF`, or `=ON` (decision 641), and shows the
   value that CMakeCache.txt then holds.
2. It writes a machine sample with machine-state.ps1.
3. It runs measure-build.py: `ninja -t clean`, the timed full build, and one
   timed `check`, as in the first half.
4. It runs ninja-log-analysis.py on the build and on the check, as in the
   first half.
5. With the hook on, it runs measure-snapshot.py, tree-breakdown.py (after
   the first rebuild with the hook only) and
   [`m1-131/maps-census.py`](m1-131/maps-census.py).
6. It writes a machine sample. In RelWithDebInfo, it then runs bench.sh.

The labels are `rel-nohook-1` to `-3`, `rel-hook-1` to `-3`, and the same
with `debug`. The RelWithDebInfo series ran from 06:51 to 08:23. The Debug
series ran rebuilds 1 to 3 from 08:27 to 10:00, and rebuilds 4 to 6 from
10:40 to 11:44.

**The stop at 10:00.** The stop came during the configure of Debug rebuild
4. Its log ends "Configuring incomplete, errors occurred!" and is kept as
debug-hook-2-configure-stopped.log. At 10:40, `bash series.sh debug debug 4`
started the series again at rebuild 4. Each rebuild starts with a configure
and `ninja -t clean`, thus the stopped configure has no effect on the
figures. The second session added two things to series.sh before it
continued: the third argument, and a copy of the tree's
Testing/Temporary/LastTest.log after each check. Thus only Debug rebuilds 4
to 6 have that copy. For RelWithDebInfo, the second session copied the log of
rebuild 6 by hand; no later CTest run had written it again.

**The machine.** 35 samples, in build/m131-spikes/m0-hook/state-*.txt. The
RelWithDebInfo series ran before working hours: on mains power at each
sample, 14.0-15.0 GiB free, the processor load 0-31 %. The Debug series ran
in working hours: 7.5-12.7 GiB free, the load 3-55 %, with Excel, Word,
Teams and Outlook open. Two samples are not usual:

- At 08:29:18, before Debug rebuild 1, the machine was on battery
  (`BatteryStatus` 1) and the processor load was 100 %. The build of that
  rebuild started 32 minutes later, at 09:02. The first session's notes do
  not say why. At the next sample, 09:16, the machine was on mains power.
- At 10:00:42 the power scheme was "Tasksequenz mit hoher Leistung", a
  scheme for high performance. At all other samples it was Balanced.

**Other commands during the series.** During the build and the check of
Debug rebuild 4 (10:44-10:50), the second session ran seven short scripts.
Each read files of the RelWithDebInfo tree or of the measurement folder for
some seconds. No record says which other commands ran beside the first nine
rebuilds.

### 1. The build time (decision 559)

**Procedure.** [`m1-131/compare-builds.py`](m1-131/compare-builds.py)
`<short>` reads the two copies of the Ninja log around each build, and sorts
each compile by the path of its object:

- **the project's own**: 90 objects of src/ and tests/ in the targets that
  get the hook when it is on. They are the same objects in both states;
- **only with the hook**: 6 objects, the recorder and the self-test's three
  programs;
- **the other compiles of the same objects**: 804 objects that never get the
  hook. These are 489 compiles of the dependencies (`_deps/`), 252 of
  someone else's code in top-level targets, 61 header self-checks, and the 2
  objects of `orbsim_stb_image` and `vma_impl`;
- **the links** of the 54 programs that exist in both states, and of the
  self-test's 3 programs.

[`m1-131/hooked-objects.py`](m1-131/hooked-objects.py) read the compile
commands with the hook on. The hook flag is on exactly the 90 objects of the
first class, on 3 of the 6 objects of the second class, and on
float_equal_guard.cpp.obj, which the full build does not compile. No other
object has it.

The dependencies' compiles do not change with the hook. Thus their sum shows
how fast the machine was during that build. The project's own compiles
divided by the dependencies' compiles remove a part of that change. The
standard deviation (a measure of how far the three values are from their
average) is in brackets.

| RelWithDebInfo | Without the hook: average (standard deviation), lowest-highest | With the hook | Difference |
|---|---|---|---|
| Full build, wall clock | 238.3 s (27.5), 211.5-266.5 | 233.0 s (26.4), 216.7-263.4 | -5.3 s, -2.2 % |
| The project's own compiles, 90, summed | 1,319.5 s (145.0), 1,171.8-1,461.7 | 1,291.0 s (148.0), 1,194.7-1,461.4 | -28.5 s, -2.2 % |
| The other compiles of the same objects, 804, summed | 2,055.3 s (229.1), 1,850.8-2,302.8 | 1,943.8 s (226.8), 1,773.2-2,201.1 | -111.5 s, -5.4 % |
| The project's own / the dependencies' | 0.979 (0.020), 0.958-0.996 | 1.034 (0.030), 0.999-1.053 | +5.7 % |
| The project's own / the other compiles | 0.642 (0.014), 0.633-0.658 | 0.664 (0.021), 0.643-0.686 | +3.5 % |
| The links of 54 programs, summed | 129.1 s (23.6), 114.3-156.3 | 143.7 s (3.6), 139.9-147.0 | +14.5 s, +11.2 % |
| Only with the hook: 6 compiles; 3 links | -- | 29.9 s (28.6-32.4); 4.0 s (3.7-4.2) | -- |

| Debug | Without the hook: average (standard deviation), lowest-highest | With the hook | Difference |
|---|---|---|---|
| Full build, wall clock | 298.9 s (47.4), 259.1-351.4 | 317.0 s (10.7), 307.6-328.6 | +18.0 s, +6.0 % |
| The project's own compiles, 90, summed | 1,545.7 s (279.4), 1,376.4-1,868.2 | 1,562.2 s (48.9), 1,522.8-1,616.9 | +16.5 s, +1.1 % |
| The other compiles of the same objects, 804, summed | 2,651.4 s (383.9), 2,229.3-2,979.6 | 2,907.2 s (50.7), 2,850.6-2,948.4 | +255.8 s, +9.6 % |
| The project's own / the dependencies' | 0.961 (0.137), 0.806-1.063 | 0.863 (0.067), 0.786-0.911 | -10.3 % |
| The project's own / the other compiles | 0.584 (0.067), 0.507-0.627 | 0.537 (0.014), 0.521-0.548 | -8.0 % |
| The links of 54 programs, summed | 214.7 s (27.8), 191.7-245.6 | 312.1 s (64.1), 260.5-383.9 | +97.4 s, +45.4 % |
| Only with the hook: 6 compiles; 3 links | -- | 28.6 s (24.5-30.9); 7.9 s (6.2-10.6) | -- |

**Against decision 559.** In RelWithDebInfo the quotient increased by 5.7 %
and 3.5 %. This agrees with the estimate of about +6 %. But the summed
compile times decreased by 2.2 %, because the machine was faster during the
rebuilds with the hook: the dependencies' compiles decreased by 7.4 %. The
standard deviation of the summed times is 11 % of their average, which is
two times the estimate. Thus three rebuilds each way show an increase of the
estimated size, but they do not prove it. In Debug the quotient decreased by
8-10 %. A hook that only adds work cannot make a compile faster. Thus the
Debug figures cannot show the cost of the hook: the Debug series ran in
working hours, and its spread is larger.

**The links, one program at a time**
([`m1-131/links-compare.py`](m1-131/links-compare.py)). Debug: the median
change is +0.83 s a program, the average +1.80 s, and 39 of 54 links took
longer. RelWithDebInfo: the median change is +0.01 s, the average +0.27 s,
and 27 of 54 links took longer. Decision 582 measured 20-30 ms more for one
link of `test_math` with the map. In a full build, a link runs beside other
jobs, thus its time includes the wait for the processor and the disk.

**Against the first half.** The first half measured one build of each tree
at `9061ab0`: 223.9 s and 208.6 s. The averages without the hook here are
238.3 s and 298.9 s. The commit is later, items 1 to 3 and 5 added code,
and the machine had more load.

### 1a. The `check` after each rebuild

**Procedure.** compare-builds.py reads the two copies of the Ninja log around
each check. [`m1-131/tests-compare.py`](m1-131/tests-compare.py) `<short>`
reads the CTest lines of each check's log and keeps the time of each test by
its name. The tests run in parallel, thus the time of a test includes its
wait for the processor.

| | RelWithDebInfo without | RelWithDebInfo with | Debug without | Debug with |
|---|---|---|---|---|
| `check`, wall clock | 438.8 s (73.6), 358.3-502.6 | 431.4 s (35.0), 391.6-457.5 | 665.1 s (110.1), 573.5-787.2 | 799.0 s (33.1), 761.5-824.2 |
| Lint steps, summed | 3,169.3 s | 3,060.8 s | 3,278.3 s | 3,710.5 s |
| The step that runs the tests | 95.4 s (2.3), 93.3-97.8 | 101.6 s (9.7), 94.2-112.6 | 306.4 s (34.0), 270.4-337.9 | 384.5 s (34.5), 346.6-414.0 |
| The 563 tests that ran in all six checks, summed | 370.3 s, 328.9-434.1 | 383.5 s, 355.5-417.4 | 768.8 s, 707.2-846.4 | 941.2 s, 868.3-982.0 |
| Difference of the summed tests | | +13.2 s, +3.6 % | | **+172.4 s, +22.4 %** |

Each check passed. Each ran 563 to 567 tests, because a rebuild changes the
programs; 0 failed. CTest skipped 1 test in Debug (test 92) and 7 in
RelWithDebInfo (test 92 and the six abort probes), as in the first half.
With the hook, one more test runs, `coverage_recorder`, and the lint has 101
steps in place of 98. Each check lints every file again, because
`ninja -t clean` deletes the lint stamps (first half, point 4).

In Debug, 478 of the 563 tests took longer on average with the hook. The
largest increases are `float_equal_guard` (+11.6 s), `mutants_due` (+11.6
s) and `coverage_hook` (+10.5 s). The 29 probe tests took 54.9 s more
together, up to 5.1 s each. The median test took 0.12 s more, 25 % more.

**Against decision 559.** In RelWithDebInfo the increase is in the spread:
the ranges overlap. In Debug the ranges do not overlap. Decision 559's Debug
spike measured 264, 200 and 194 s without the hook and 217, 205 and 215 s
with it, thus no cost. **The Debug figure here contradicts that spike.** The
record writes do not explain it alone: section 7 measures a median write of
3.1 ms in Debug.

### 2. The frame-time benchmark (decision 589)

**Procedure.** bench.sh after each RelWithDebInfo rebuild, three runs, as
in section 2 of the first half: nine runs each way.
[`m1-131/bench-summary.py`](m1-131/bench-summary.py) gives the median of
each figure over the nine runs, and the lowest and the highest.

| Over nine runs, ms: median (lowest-highest) | Without the hook | With the hook | Change of the medians |
|---|---|---|---|
| Frame interval, median | 0.3921 (0.3699-0.5029) | 0.3842 (0.3247-0.5003) | -2.0 % |
| Frame interval, the mean of each run | 0.6665 (0.6660-0.6680) | 0.6666 (0.6662-0.6677) | 0.0 % |
| Frame interval, p95 | 1.6642 (1.4730-1.7810) | 1.6081 (1.4479-1.7346) | -3.4 % |
| Processor working, median | 0.1147 (0.1092-0.1474) | 0.1155 (0.1068-0.1411) | +0.7 % |
| GPU, median | 0.1129 (0.1123-0.1130) | 0.1127 (0.1123-0.1132) | -0.2 % |
| GPU, p95 | 0.1143 (0.1138-0.1153) | 0.1141 (0.1135-0.1153) | -0.2 % |

The first half's three runs without the hook: interval medians 0.3766 to
0.4133, GPU medians 0.1131 and 0.1132. No effect of the hook shows. The GPU's
median, the figure to compare, changed by -0.2 %. Its nine runs span 0.6 %
without the hook and 0.8 % with it.

### 3. The linker's maps (decision 582)

**Procedure.** maps-census.py after each rebuild with the hook. It lists
every file that ends `.map` in the tree, out of `_deps/*-src`, and keeps the
size and SHA-256 of each map and of each program at the top of the tree.
[`m1-131/maps-compare.py`](m1-131/maps-compare.py) compares the three
rebuilds of a tree.

| | RelWithDebInfo | Debug |
|---|---|---|
| Maps | 57, each beside its program | 57, each beside its program |
| Total | 119,553,475 bytes (119.6 MB, 114.0 MiB) | 293,372,196 bytes (293.4 MB, 279.8 MiB) |
| The largest | test_orbit_scales.map, 3,526,227 bytes | test_planetary_grid.map, 7,542,719 bytes |
| test_math.map (decision 582's spike: 5.7 MB in Debug) | 2,659,572 bytes | 6,193,931 bytes |
| The three rebuilds | all 57 maps and all 58 programs the same, byte for byte | the same |

The 57 maps belong to every program that the tree links: the 54 that carry
the hook, `coverage_recorder_probe_plain`, and ERFA's two validation
programs. The 58th program at the top of each tree, count_wraparound_probe.exe,
is from 2026-09-24. No target builds it now, and it has no map.

**Against decision 582**, 220-260 MB a tree: RelWithDebInfo is 100.4 MB
below the lower end. **Debug is 33.4 MB (13 %) above the upper end.** The
two trees hold 412.9 MB of maps together.

### 4. `reproducible_build`

It passed in each of the six checks with the hook. The CTest logs of three of
these checks hold its line (rel-hook-3, debug-hook-2 and debug-hook-3): "...
compiled and linked again to the same bytes, and the object the same with
NO_COLOR set and without". The script runs the NO_COLOR case each time and
fails on any problem, thus each pass includes that case. The test compares
the object and the program, not the map. Section 3 shows that the maps are
the same from one clean rebuild to the next.

### 5. One function at each address (decision 551)

**Procedure.** [`m1-131/address-table.py`](m1-131/address-table.py) `<tree>`
reads each program at the top of the tree that holds the section `.SCOVP`,
the hook's address table. It reads the table from the program's file, and
the map beside the program through `read_map` of scripts/compiled-code.py.
It requires each entry to name exactly one function: one symbol in a CODE
section of the map, at the entry's address.

| | RelWithDebInfo | Debug |
|---|---|---|
| Programs with the table | 54 | 54 |
| Entries | 9,008 | 47,176 |
| Entries that name one function | 8,462 | 46,422 |
| Entries that name no function | 0 | 0 |
| Entries that name two | 546 | 754 |
| Addresses two times in one table | 0 | 0 |
| Programs whose flags and entries differ in number | 1: coverage_recorder_unaligned.exe, by design | 1, the same |

**Each entry that names two is one destructor with two names**: the vector
deleting destructor (`??_E...`) and the scalar deleting destructor
(`??_G...`) of the same class, from the same object.
[`m1-131/alias-check.py`](m1-131/alias-check.py) read the objects: clang
writes each such `??_E` as a weak external, that is, a second name for its
own `??_G` (639 such names in the RelWithDebInfo objects, 688 in Debug).
Thus each is one function with one flag. No linker folded two functions into
one address. But the criterion "exactly one function", as written, does not
hold for these entries, because the map gives the one function two names.

**The instrument failed once before its zeros were accepted** (rule 23).
[`m1-131/make-faults.py`](m1-131/make-faults.py) made two copies of
test_srgb.exe: in one, the map lost the line of the first entry's function;
in the other, the table's second entry got the first entry's address.
address-table.py found 1 entry with no function and 1 address two times.

### 6. Functions without a flag (decision 586)

**Procedure.** [`m1-131/unflagged.py`](m1-131/unflagged.py) `<tree> <list>`
reads each object whose compile command holds `-fsanitize-coverage=`. It
compares the functions that the object defines with the functions that the
object's address table names. It does not count the pieces that Windows'
exception handling makes of a function (funclets), and the hook's own
start-up function. The second session changed one thing: the script chose
the objects by their table, and now chooses them by the compile command, so
that an object that has no table counts too.

| | RelWithDebInfo | Debug | Decision 586 (Debug spike) |
|---|---|---|---|
| Objects with a table | 91 | 92 | 85 |
| Functions | 9,172 | 64,366 | 54,939 |
| Without a flag | 382 (4.16 %) | 1,301 (2.02 %) | 1,189 (2.2 %) |
| Standard library (std) | 356 | 1,232 | all |
| Catch2 | 22 | 22 | -- |
| C and C++ runtime headers | 1 (`__local_stdio_printf_options`) | 31 (30 `__empty_global_delete`, 1 `__local_stdio_printf_options`) | -- |
| Names that llvm-undname cannot read: `std::vector` and `std::array` members for mp-units types | 3 | 16 | -- |
| Names that name a type of the project | 48 | 77 | 63 |
| Defined by the project | 0 | 0 | 0 |
| Objects with the hook and no table | contract.cpp (no function when NDEBUG is set); coverage_recorder_unaligned.cpp (1 function, without the table by design) | coverage_recorder_unaligned.cpp | -- |

**What they are.** Most are helpers that only throw:
`_Throw_bad_array_new_length`, `_Xlen_string`, `_Throw_system_error`,
`_Throw_format_error`, and the `_Xlength` and `_Xrange` of `std::vector`. In
Debug, 600 are also the `operator()` of std::format's `_Width_checker` and
`_Precision_checker`. The Catch2 functions are matcher and listener
destructors and `Catch::Detail::Unreachable`. The full lists are
rel-unflagged-list.txt and debug-unflagged-list.txt.

**Against decision 586.** Debug 2.02 % against 2.2 %. RelWithDebInfo has
4.16 %, a new figure. A possible cause, which nobody examined: the
optimiser puts many small functions inline, but it keeps the helpers that
only throw as functions. In both trees the project defines none of them.

### 7. The crash handler and the longest write (decisions 634 and 638)

**Procedure.** A temporary change of tests/CoverageRecorder.cpp, kept as
[`m1-131/recorder-timing.diff`](m1-131/recorder-timing.diff):

- each write goes to a file of its own, with a write number after the
  start time;
- after `end`, a write by the crash handler adds `code <the exception's
  code>`;
- then each write adds `write_us`, the time from the start of the write to
  the end of its one WriteFile call, and `duration_us`, the time from the
  start of the write until the file is closed. `duration_us` is the time the
  writer holds its turn. The changed recorder opens the file again to add
  the two lines.

Both trees were built with the change. Then each test ran once in each
tree, with `ctest --test-dir <tree> -j 10 --output-on-failure` and
ORBSIM_COVERAGE_RECORDS set to an empty folder of the second session.
[`m1-131/records-count.py`](m1-131/records-count.py) `<folder>` read the
records. Then `git checkout -- tests/CoverageRecorder.cpp` put the file back,
and `git status` showed no change. Both trees were built again. maps-census.py
and maps-compare.py then showed all 57 maps and all 58 programs of each tree
the same, byte for byte, as after the last check of the series.

In each tree, 3 of 588 tests failed, all because of the temporary change:
`coverage_recorder` (the self-test reads the form of the record),
`shader_record` (it reads the new file name as a second record) and
`test_reads` (it finds the added CreateFileW call). CTest skipped 7 tests in
RelWithDebInfo and 1 in Debug, as in the checks. The other tests passed.

**The instrument failed once before its zero was accepted** (rule 23).
records-count.py read four made-up records from
[`m1-131/make-records.py`](m1-131/make-records.py) and gave the known
answer: 1 process that wrote "by a crash" and then "normally", and 1 record
without `end`. And coverage_recorder_probe.exe with the change, run as
`crash`, wrote a record "by a crash" with the code 0xc0000005.

| | RelWithDebInfo | Debug |
|---|---|---|
| The run | 11:48:39-11:51:17 | 11:52:42-11:58:11 |
| Processes that wrote | 586 | 586 |
| How they ended | 586 normally; the six abort probes are skipped in this tree | 580 normally, 6 by abort |
| Wrote "by a crash", then "normally" | **0** | **0** |
| Writes by the crash handler | 0 | 0 |
| Records without `end` | 0 | 0 |
| `duration_us`, the longest | 321,838 us (test_time_ut1.exe); then 225,940 us (test_orbit_elements.exe) and 38,707 us | 28,419 us (orbsim.exe, test `probe_grid-jitter-1km-0`) |
| `duration_us`, 99th percentile and median | 8,732 us; 3,014 us | 12,693 us; 3,111 us |
| `write_us`, the longest and 99th percentile | 320,346 us; 7,291 us | 27,485 us; 12,482 us |

(us: microseconds.) The two longest RelWithDebInfo writes are about 8 and 6
times the third. Both are Catch2 suites, and CTest ran up to ten tests at a
time.

**Against decision 638.** The bound is to be "well above the longest write".
The code holds 10 s (`kLongestWait` in tests/WriterTurns.hpp), 31 times the
longest write measured.

### 8. The harness's copy of the tree (decision 631)

**Procedure.** measure-snapshot.py, as in the first half, in two ways:

- one copy after each check with the hook, by series.sh, the copy beside
  the script in build/m131-spikes/m0-hook/;
- at the end, three copies one after the other, the script in the second
  session's temporary folder on drive C:. For RelWithDebInfo, one more copy
  beside the build folder, to find if the place has an effect. It has none:
  24.6 s.

| | RelWithDebInfo | Debug |
|---|---|---|
| Copied by a pass | 6,424 files, 1,506.5-1,508.2 MiB | 3,553 files, 1,956.8-1,958.5 MiB |
| Without the hook (first half) | 5,679 files, 1,307.4 MiB | 2,819 files, 1,562.0 MiB |
| Difference | +745 files, +199 to +201 MiB | +734 files, +395 to +397 MiB |
| `take_snapshot`, one after each check | 7.5, 7.8, 6.2 s | 16.0, 13.3, 17.0 s |
| The same without the hook (first half: one after the check, then two in a row) | 6.3, 7.5, 6.2 s | 3.6, 4.5, 4.6 s |
| `take_snapshot`, three in a row at 12:00 | 28.8, 29.4, 29.2 s | 17.5, 18.2, 19.0 s |
| `shutil.rmtree`: after each check; in a row | 2.9, 3.4, 3.3 s; 10.3, 8.2, 11.3 s | 5.0, 3.0, 4.3 s; 5.2, 5.1, 5.5 s |

**What the copy holds in addition** (tree-breakdown.py, after the first
rebuild with the hook): the 57 maps, 114.0 MiB and 279.8 MiB; 4 more `.pdb`
files, +62.0 MiB and +60.7 MiB; 11 more objects, +9.6 MiB and +29.9 MiB; 4
more programs, +11.1 MiB and +16.3 MiB; and the folder coverage-records/,
637 files (586 `.functions.txt`, 51 `.shaders.txt`), 2.6 MiB and 3.4 MiB.
The RelWithDebInfo copy still holds the editor's index (first half, point
3): 2,895 files, 33.1 MiB.

**The time** changes with the state of the machine, by up to four times. At
12:00, other programs used 25-28 % of the processor and 10.5 GiB was free.
Copies taken the same way as the first copy of the first half, right after a
check: RelWithDebInfo shows no change; Debug takes 9-13 s more, three to four
times as long.

### 9. `check` at the end

After section 7's change was undone and both trees were built again,
`check` passed in both trees with the hook on, one tree after the other:
RelWithDebInfo at 12:15, "28 ran, 560 skipped as unchanged", 0 failed;
Debug at 12:16, "22 ran, 566 skipped as unchanged", 0 failed. Both trees keep
`-DORBSIM_COVERAGE_HOOK=ON`, set by hand (decision 641). The presets do not
carry it.

### What does not agree with an estimate or a ruling

Three figures, for the owner (decision 631):

1. **The Debug test time with the hook**, +22.4 % (section 1a). Decision
   559's Debug spike saw no cost.
2. **The Debug maps**, 293.4 MB, 33.4 MB above decision 582's estimate of
   220-260 MB a tree (section 3).
3. **The 546 and 754 table entries whose address has two names in the map**
   (section 5). No two functions share an address, as decision 551 rules.
   But a check that requires exactly one name at each address refuses these
   entries.

## The files

In [`m1-131/`](m1-131/), with their line endings turned from CR LF into LF
and nothing else changed: the six scripts named above; for each label,
`rel-nohook` and `debug-nohook`, the driver's lines, the build and check
analyses, the three snapshot printouts and the extension breakdown; the
benchmark's printout and its three summaries, in
[`m1-131/bench-rel-nohook/`](m1-131/bench-rel-nohook/); and the six machine
samples, `state-0` to `state-5`.

**Only on the first machine**, in `build/m131-spikes/m0/`, which git
ignores and which is kept until M1-131 ends: the whole folder as it was
taken, byte for byte -- the outputs of every build, check and clean (each
label's clean, build and check logs, up to 180 KB), the three copies of each
tree's Ninja log (160-325 KB each), the benchmark's three tables (about
415 KB each, which, as for M1-22's, are not committed -- register decision
381), the step-0 waiting
scripts, and the working notes these figures were first written in.

**The second half's scripts**, in [`m1-131/`](m1-131/), reviewed and with LF
line endings. The first session wrote most of them; nobody had reviewed them
before the second session used them. The second session changed four:

- [`m1-131/series.sh`](m1-131/series.sh): the alternated series. Changed:
  the third argument, to start at a given rebuild, and the copy of
  LastTest.log after each check.
- [`m1-131/compare-builds.py`](m1-131/compare-builds.py): the six rebuilds of
  a tree, side by side. Changed: the standard deviation, the other compiles
  of the same objects, and the second quotient.
- [`m1-131/unflagged.py`](m1-131/unflagged.py): the functions without a
  flag. Changed: it chooses the objects by their compile command.
- [`m1-131/records-count.py`](m1-131/records-count.py): the records of the
  temporary recorder. Changed: it reads `write_us` too.
- Not changed: [`m1-131/maps-census.py`](m1-131/maps-census.py),
  [`m1-131/address-table.py`](m1-131/address-table.py) and
  [`m1-131/bench-summary.py`](m1-131/bench-summary.py).
- New in the second session:
  [`m1-131/tests-compare.py`](m1-131/tests-compare.py),
  [`m1-131/links-compare.py`](m1-131/links-compare.py),
  [`m1-131/maps-compare.py`](m1-131/maps-compare.py),
  [`m1-131/hooked-objects.py`](m1-131/hooked-objects.py),
  [`m1-131/alias-check.py`](m1-131/alias-check.py), and the two that make
  the instruments fail once, [`m1-131/make-faults.py`](m1-131/make-faults.py)
  and [`m1-131/make-records.py`](m1-131/make-records.py).
- [`m1-131/recorder-timing.diff`](m1-131/recorder-timing.diff): the
  temporary change of section 7, as `git diff` gave it. It is not in the
  code.

The scripts name this machine's paths, as those of the first half do. Run
them from build/m131-spikes/m0-hook/ or from a copy beside their inputs.

**Only on the first machine**, in build/m131-spikes/m0-hook/, which git
ignores and which is kept until M1-131 ends: every output of the second
half. For each label: the configure, clean, build and check logs, the
driver's lines, the three copies of the Ninja log, the two analyses; with
the hook, the copy's printout and the census of the maps (`.txt` and
`.json`); the CTest logs (`<label>-LastTest.log`) of rel-hook-3 and
debug-hook-2 and -3; the benchmark's printouts and its tables, in
`bench-<label>/`; the 35 machine samples; and the analyses
`<tree>-compare.txt`, `<tree>-tests-compare.txt`,
`<tree>-links-compare.txt`, `<tree>-maps-compare.txt`,
rel-bench-summary.txt, `<tree>-address-table.txt`, `<tree>-unflagged.txt`
with `<tree>-unflagged-list.txt`, and, for section 7, the build and test
logs and the counts (`timing-<tree>-*`).

**In the repository, the small outputs of the second half**, 113 files of
less than 60 KB each, 206 KB in total, are in
[`m1-131/with-hook/`](m1-131/with-hook/): each `.txt` and `.csv` file of
that folder, byte for byte (decision 694). They are in a folder of their own
because some of their names, for example the machine samples, are the same
as those of the first half.

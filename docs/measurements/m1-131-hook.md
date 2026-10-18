# M1-131 — The build with and without the coverage hook

Kind: reference
Binding: no — this file records a measurement and how to repeat it
Read when: you want the figures behind M1-131's item 4, or you are about to
take the figures with the hook and must repeat this run exactly.

**Half done, to be completed.** Written 2026-10-18 with the figures
**without** the hook, taken that morning. The figures **with** the hook are
not taken yet; the section [With the hook](#with-the-hook----to-be-measured)
lists what is to be measured there. Until it is filled in, nothing here
compares the two.

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

## With the hook -- to be measured

**Not measured yet.** The same lines as above, in the same order, with the
labels `rel-hook` and `debug-hook` in place of `rel-nohook` and
`debug-nohook`, and the full rebuilds alternated as point 1 above says.
Item 4's list, each figure set beside its estimate:

- [ ] **The build time in both trees** -- the wall clock, the summed compile
      times by class, and the `check` after each -- against decision 559's
      +6 % in RelWithDebInfo.
- [ ] **The size of the linker's maps**, each tree, against decision 582's
      220-260 MB a tree.
- [ ] **`reproducible_build` still passing** with the hook and the maps.
- [ ] **No two functions at one address** (decision 551).
- [ ] **The optimised tree's functions without a flag**, set beside the
      Debug figure of decision 586, 2.2 %.
- [ ] **The benchmark with the hook**, three runs as in section 2 (decision
      589); M1-47 takes the same both ways on the RX 7900 XTX.
- [ ] **The size and time of the harness's copy, which then holds the
      maps** (decision 631), three runs a tree as in section 3.
- [ ] **How often, over a full run of each tree, the crash handler acts in a
      program that goes on to end normally** -- an exception some code
      catches, which the handler sees first (decision 634).
- [ ] **The longest a record's write takes**, which sets the bound of
      decision 638's wait.

Every figure goes to the owner before the presets carry the hook, whatever
it is (decision 631).

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

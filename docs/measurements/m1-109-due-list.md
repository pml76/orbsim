# M1-109 — What makes a mutant file due, measured

Kind: reference
Binding: no — this file records a measurement and how to repeat it
Read when: you want the numbers behind register decisions 392-397, or you
want to know why `scripts/mutants-due.py` lists what it lists.

Measured 2026-10-05 on the first machine: NVIDIA RTX A2000 Laptop GPU
(`10de-25ba`), driver 582.53, clang 23.1.2, Vulkan SDK 1.4.357.0, at
`abbaaa1` (M1-108 pushed) unless a section says otherwise. The scripts and
the raw result are in [`m1-109-due-list/`](m1-109-due-list/).

## Before the change

**The list as it stood.** `mutants-due.py build/relwithdebinfo` listed **31
of 34** mutant files. With only `shaders/lambert.frag` assumed changed
(`--only-assumed`), **26 of 34**, in either tree.

**Three routes brought a shader in, not one:**

1. **A placeholder build step.** 16 mutant files named `orbsim_shaders` as a
   target. In the 14 that judge only scripts it was a no-op, there because
   the harness always built something; it made all ten shaders an input of
   each.
2. **The application's build inputs.** `orbsim.exe` waits for
   `orbsim_shaders` through a build-order edge (`|| orbsim_shaders` in
   `ninja -t query orbsim.exe`), and `ninja -t inputs` lists build-order
   inputs too, so every shader was an input of every judge built from the
   application -- M1-108's compile-time checks among them.
3. **The run-time rule**: every shader for every judge that names the
   application.

**Which test depends on which shader, from the program's own behaviour.**
[`shaderloads.py`](m1-109-due-list/shaderloads.py) hid one compiled shader at
a time in a scratch Debug tree and ran the 75 tests that start the
application or carry the `gpu` label, 41 minutes in all; the result is
[`shaderloads-debug.json`](m1-109-due-list/shaderloads-debug.json). Tests
newly failing, of 76 run: `body.frag` and `body.vert` 42 each,
`fullscreen.vert` 61, `lambert.frag` and `lambert.vert` 32 each, `line.frag`
and `line.vert` 42 each, `probe_gradient.frag` 38, `probe_port.frag` 27,
`tonemap.frag` 61. A test that fails only because a setup test it requires
failed counts here too, which is the safe direction. Among the tests that
start the application, `lambert` failed only the six `probe_lambert*` runs,
`probe_port` only `probe_tonemap-port`, and `probe_gradient` only the `clear`
probe and its golden tests. The 14 usage tests failed for no shader.

Turned into mutant files -- a file whose ctest judges failed, or whose suite
is the program behind one:

| Shader changed | Due before | Due under the measured rule |
|---|---|---|
| `lambert.frag` | 26 | 4 (`m1-15`, `m1-18`, `m1-19`, `m1-20`) |
| `probe_port.frag` | 26 | 4 |
| `line.vert` | 26 | 8 |
| `probe_gradient.frag` | 26 | 10 |
| `tonemap.frag` | 26 | 11 |

**What the extra files cost.** The 14 script-only files, run with
`mutate.py` in a scratch tree: 71 mutants, **2,600 s, 43 min 20 s** --
`m1-98` 593 s, `m1-90` 360 s, `m1-100` 323 s, `m1-88` 299 s, `m1-102` 273 s,
`m1-95` 255 s, `m1-96` 249 s, `m1-92` 173 s, and six under 21 s each. The
eight application files a lambert change listed needlessly hold 114 mutants;
at the 17 s a mutant measured for M1-108's three Debug files (13 min 29 s for
47), **about 32 minutes**, estimated rather than run.

**A fresh tree.** The same commit, built with the same compilers on the same
machine in another folder, compared with
[`fpcompare.py`](m1-109-due-list/fpcompare.py): in Debug **144 of 275 judges'
fingerprints differed -- every compiled program**, and every one that agreed
was a test definition; in RelWithDebInfo 12 differed, all `orbsim.exe`.
[`fpparts.py`](m1-109-due-list/fpparts.py) found two causes, neither the
compiler:

- **Debug: the build depended on how often the tree had been configured.**
  Imath writes `CMAKE_DEBUG_POSTFIX "_d"` into the cache
  (its `config/ImathSetup.cmake` at v3.2.3, one `set(... CACHE STRING ...)`), so the targets declared before
  OpenEXR's fetch got the suffix only from a second configure on. Confirmed
  by configuring the scratch tree once more: references to
  `orbsim_core_d.lib` in its `build.ninja` went from 0 to 100.
- **`orbsim.exe`: SDL's generated precompiled header holds the tree's
  absolute path** (`_deps/sdl3-build/CMakeFiles/SDL3-shared.dir/cmake_pch.h`,
  line 4), and its contents were hashed as written.

**Comparing compiled programs instead does not work as things stand.**
`test_math.exe` built at one commit in the two folders differed in 114,646
bytes, and its `.text` section, the same size in both, differed too: the
folder's path is compiled in and moves what follows it.

**At M1-18's commit**, `4ab36f2`, in a fresh tree with the records as they
were then: **28 of 28** files due. Seven only through the shaders and the
fingerprint of `orbsim_shaders` -- `m1-89`, `m1-92`, `m1-97`, `m1-101`,
`m1-103`, `m1-104`, `m1-106`; `m1-08` and `m1-87` only because fingerprints
moved, for a reason the record, a single number per judge, could not say.

**The graphics card.** The `gpu` label marks exactly the tests whose verdict
depends on the card: 61 tests; the 14 that start the application without it
only check its arguments, and the 29 labelled ones that do not start it read
what a GPU test wrote. A `clear` probe names the card, the driver and, since
M1-109, the validation layer in its sidecar, in 4.4 s; `vulkaninfo` lists
both of this machine's cards and cannot say which one the application takes.

## After the change

Measured 2026-10-05 on the same machine, with the change in the working
tree, after `check` had passed in both trees (524 of 524 each), so every
probe's sidecar came from the new application.

**Shaders**, with only one assumed changed, the same in both trees, not
counting `m1-109` itself, due until its first pass:

| Shader changed | Before | After | The measured rule |
|---|---|---|---|
| `lambert.frag` | 26 | 10 | 4 |
| `probe_port.frag` | 26 | 10 | 4 |
| `probe_gradient.frag` | 26 | 11 | 10 |
| `line.vert` | 26 | 11 | 8 |
| `tonemap.frag` | 26 | 11 | 11 |

Above the measured rule are the six files whose judges start the
application without writing a sidecar -- the smoke test, the benchmark and
the golden-acceptance script, counted with every shader (decision 394):
`m1-13`, `m1-14`, `m1-17`, `m1-22`, `m1-107` and `m1-110`. `m1-92` is due for
`line.vert` because one of its tests names that file on its command line.
The probes' sidecars name exactly the shaders the hiding experiment found
each probe to depend on.

**A fresh tree**: the scratch checkout, holding the same change, its two
trees deleted and built from nothing, configured once each (783 s and
692 s): **367 of 367 judges' fingerprints agree with the main trees, in
Debug and in RelWithDebInfo**, where 144 of 275 and 12 of 275 differed
before. There are more judges because the tests run before a judge now count
as judges.

**The header record, read during `check`** (register decision 399). The
final RelWithDebInfo `check` of 2026-10-06 failed all seven tests that read
the tree, at `ninja -t deps`, and passed once a read made with no build
running had rewritten the record. Ninja 1.12.0's source: `-t deps` and
`-t query` open both logs for writing unless `-n` is given, and a log past
1,000 records and three times its live ones is then rewritten -- deleted and
replaced. [`ninja-deps-held-open.py`](m1-109-due-list/ninja-deps-held-open.py)
reproduces it from nothing in two minutes: past the threshold a plain read
rewrote the record, 57,496 bytes to 28,696; **with the file held open, as the
Ninja running `check` holds it, a plain read failed -- "opening deps log:
Permission denied", exit 1, a `.ninja_deps.recompact` left behind -- and a
read with `-n` returned all 400 records**, then and freely. Every Ninja tool
the script runs is now a dry run; the real tree's lambert list is the same
11 files read either way, and with the record held open.

**The cost of a run.** With the records compared and the card read, 1 min
41 s on a warm cache, where it was 31 s: about 62 s in Ninja's queries and
commands, 28 s reading the tree, 12 s in git, 10 s for the card. With
assumed changes only, as `check` runs it, 18 s.

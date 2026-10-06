# M1-109 — List only the mutant files a change can reach

Phase: A | Status: **done, 2026-10-05**
Prerequisites: M1-92, M1-103
Decided by: register decisions 276 and 277 -- placed after M1-108, just before the phase A
gate (277); carried out under 392-397

**Amended 2026-10-05, when the task ran.** The measurement came first, as
asked, and found a third source the text below does not name -- a placeholder
build step in the script mutants -- and two causes of a fresh tree's
over-listing that are not the toolchain. The owner took every recommendation,
asked for the graphics-card gap to be closed here rather than recorded, and
ruled the questions that arose during the work (register decisions 392-399).
What they add is below, under "What was done"; the text above it is as
written.

## Purpose

**Since 2026-10-03 every mutant file `scripts/mutants-due.py` lists runs at
each phase gate** (decision 276). The script lists more than a change can
reach, on purpose -- missing a file costs a hole nobody sees, listing one too
many costs minutes -- and two sources of over-listing were seen the same day:

- **A shader makes due every file whose judges start the application**, though
  most of them never load that shader: M1-18's commit, which added
  `lambert.vert` and `lambert.frag`, made 26 of 28 files due.
- **A second machine makes every file due.** On the AMD machine set up on
  2026-10-03, all 28 were listed, files like `m1-08.json` and `m1-87.json`
  only through "fingerprint changed" -- a judge's fingerprint holds the
  contents of the toolchain's own files, and a different clang patch release
  or Vulkan SDK changes them, though M1-103 made the paths themselves
  placeholders.

A gate's re-run of the full list is about 300 mutants and two to three hours.

## What to do

- **Measure first**, before proposing any change: for each of the two
  sources, how many files it makes due that a precise rule would not, and
  what that costs in minutes -- on the commits of M1-18 and on a fresh tree.
- **Then put the options to the owner**, each with what it saves and what it
  could miss -- for instance, a shader judged through the shader modules a
  judge actually loads, and a toolchain change judged by whether the compiled
  programs differ -- before any code (CLAUDE.md working agreement 1).

**The limit, stated in advance**: a sharper list must never miss a file the
strict rule would list for a reason that matters; a regression test is wanted
for each narrowing, as M1-103 had one for the build definition.

## What was done

The numbers are in [the measurement](../../measurements/m1-109-due-list.md).

- **What a mutant builds** (decision 392): a mutant of a Python script names
  nothing and `scripts/mutate.py` builds nothing for it; a mutant of a CMake
  file read at configure time names `build.ninja`; `--verify` refuses a
  mutant of anything else that names nothing. 63 mutants lost the
  placeholder `orbsim_shaders`, 8 took `build.ninja`, and the notes saying
  "a no-op" were corrected.
- **How a program's inputs are read** (decision 393): Ninja's graph through
  its explicit and implicit edges, not build-order-only ones, with a
  generated header that an object includes followed back to what it is made
  from; the fingerprint's commands follow the same edges, and a test that
  starts the application has the shaders' build commands in its own.
- **Which shaders a probe test reads** (decision 394): a `shaders` line in
  every sidecar, from `VulkanContext::shadersRequested()`, which
  `loadShaderModule` fills; `scripts/mutants-due.py` trusts it from a run
  that rendered and is newer than the application, and counts every shader
  otherwise. A test's judges include the tests CTest runs before it.
- **A fresh tree** (decision 395): `CMAKE_DEBUG_POSTFIX` set to `_d` before
  the first library, with a configure-time check for Debug trees; files
  inside the build tree hashed with the tree's path taken out.
- **Fingerprints in parts** (decision 396), and a moved one names its parts.
- **The graphics card** (decision 397): the card, the driver and the
  validation layer, read from a `clear` probe's sidecar, recorded for every
  file judged on the GPU; the sidecar's new `validation.layer` line comes
  from the loader, read after an instance built with the layer.
- **Tests**: `test_probe_sidecar` gains four cases; the due-list self-test
  goes from 29 checks to 64 and the harness's from 16 to 23; `check` gains
  `mutants_due_probe_shaders`, which holds the narrowings from both sides,
  `mutants_due_unmet_current_fails`, and `mutants_due_gpu_identity`, which
  also refuses a run that asked for validation and had no layer (decision
  398) -- seen failing with the layer hidden from the loader.
- **Six older mutants re-anchored** on the rewritten script, each keeping
  what it shows, and one of them -- M1-92's survivor "shaders are no longer
  an input of the application's judges" -- now expected caught, since the
  build-order edge that made it redundant is gone (decision 398).
- **The header record read during `check`** (decision 399): every Ninja
  tool the script runs is a dry run, `-n`, so a read can never rewrite a log
  that the Ninja running `check` holds open -- the cause of seven failures in
  one RelWithDebInfo `check`, reproduced in a scratch project first.
- **M1-109's own mutants**: 25 in
  [`scripts/mutants/m1-109.json`](../../../scripts/mutants/m1-109.json), three
  declared survivors, each reached only by a real pass or record (398, 399).

## Done when

- [x] The two sources measured, and the numbers recorded -- and a third the
      task did not name: [the measurement](../../measurements/m1-109-due-list.md),
      before and after.
- [x] The owner's ruling recorded (decisions 392-399), and each narrowing
      held to leave due a file it must: by `mutants_due_probe_shaders`,
      `mutants_due_shaders` and the self-tests -- the first seen failing in
      a Debug `check` whose sidecars were stale, and each shown able to fail
      by `m1-109.json`'s pass.
- [x] `check` green in both trees -- 524 of 524 in each, 2026-10-06, on the
      RTX A2000 machine.

# M1-109 — List only the mutant files a change can reach

Phase: A | Status: not started
Prerequisites: M1-92, M1-103
Decided by: register decision 276

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

## Done when

- [ ] The two sources measured, and the numbers recorded.
- [ ] The owner's ruling recorded, and, if it changes the script, each
      narrowing seen to leave due a file it must.
- [ ] `check` green in both trees.

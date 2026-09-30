# M1-103 — Judge a build-definition change by what it changed

Phase: A | Status: not started
Prerequisites: M1-92, M1-102
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decisions 248 and 250

## Purpose

**Nearly every task reruns every mutant file, about two hours, for a change
most of them cannot see.** `scripts/mutants-due.py` treats the build
definition -- `CMakeLists.txt`, `cmake/`, `CMakePresets.json` -- as an input
of every judge, and M1-92, M1-94, M1-96, M1-97, M1-98, M1-17 and M1-100 all
changed `CMakeLists.txt`. On 2026-09-30 all 22 files ran for 118 minutes
because M1-100 changed which tests are registered in the core-only trees.

Splitting `CMakeLists.txt` into files under `cmake/` would not help: the
whole of `cmake/` is in the same list.

**Measured before proposing, 2026-09-30**: the core-only build set up three
times -- at 849b899, at 41001ba, and at 41001ba with one compile definition
added to `orbsim_core` as a control. For each of the 26 test programs, Ninja's
full list of the commands that build it (`ninja -t commands`, every compile
and link with every flag, 0.2 s, identical run to run): **0 of 26 changed**
between the two commits, and **26 of 26** under the control. CTest's own test
definitions showed exactly what had changed: `parallel_tests` gained
`--gpu-tests none`, and four tests left the tree.

## What to do

As decided (decision 250):

- **A fingerprint per judge**, recorded by `scripts/mutate.py` with each clean
  pass, in `scripts/mutation-passes.json` beside the commit:
  - for a judging program: Ninja's full command list for it, and the contents
    of every file it is built from that git does not track and no build step
    produces -- the fetched dependencies, and **the toolchain's own files**:
    clang's built-in headers, the Windows SDK and MSVC headers,
    `vulkan-1.lib`;
  - for a judging CTest entry: its definition -- command line and properties
    such as "expected to fail", resource locks and environment -- without the
    line number CMake notes it was declared at.
  - The repository's and the build tree's paths are replaced by placeholders
    first, so a pass recorded on one machine counts on another with the same
    toolchain; anything else that differs counts as a change.
- **`mutants-due.py` compares them.** A judge whose fingerprint changed makes
  its file due, and says which: "due: `parallel_tests`' definition changed".
  A change to `CMakeLists.txt`, `cmake/` or `CMakePresets.json` no longer makes
  a file due by itself. `scripts/mutate.py` and `data/` still do.
- **Where anything is unknown, the file is due**, as under the strict rule: a
  record without fingerprints, a record from another tree, a judge that
  cannot be fingerprinted.
- **File hashes are cached** in the build tree, never committed, keyed by
  each file's size and date: a file rewritten with the same size and date
  would be missed, which was accepted against 10-30 s of hashing per run.
- **`m1-92.json`'s mutant "the build definition is no longer always an input"
  is removed**, with a dated note: under this task that is the rule itself.
  Its concern moves to `scripts/mutants/m1-103.json`, whose mutants take away
  the rule's fallbacks -- a record without fingerprints that ignores a
  build-definition change, a changed test definition that is ignored, the
  placeholders applied to paths they should not touch.
- **ADR 0024** gains a dated note: the strict rule of decision 212, narrowed
  for the build definition only.

**The one-off cost, accepted**: this task changes `scripts/mutate.py`, so
every mutant file is due once more; that one full pass also records the first
fingerprints, so it is one pass, not two.

## Done when

- [ ] The self-tests were seen failing first on each new rule.
- [ ] On the real tree: a comment added to `CMakeLists.txt` makes no file due;
      a compile definition added to `orbsim_core` makes due every file whose
      judges link it.
- [ ] `check` passes in both Windows trees.
- [ ] The full mutation pass, recording the first fingerprints; then
      `mutants-due.py` lists nothing due.
- [ ] `scripts/mutants/m1-103.json` passes.

# M1-103 — Judge a build-definition change by what it changed

Phase: A | Status: **done, 2026-09-30**
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

- [x] The self-tests were seen failing first on each new rule: five of the
      fingerprint and decision cases, and the record case in `mutate.py`. The
      one case written after its code -- the comparison naming a changed, a
      new and a lost judge -- has a mutant in `m1-103.json` instead.
- [x] On the real tree, 2026-09-30, with every record set to that commit and
      its fingerprints, and both files restored byte for byte afterwards:
      - **a comment added to `CMakeLists.txt` changed no fingerprint.** It
        still made seven files due, and that was expected on reflection, not
        foreseen when this box was written: `m1-88`, `m1-90`, `m1-95`,
        `m1-96`, `m1-98`, `m1-100` and `m1-102` hold mutants *of*
        `CMakeLists.txt`, and a change to a file a mutant mutates makes it due
        under decision 212 -- about 30 minutes, not 118;
      - **a compile definition added to `orbsim_core` made 20 of 23 files
        due**, each naming the programs whose commands moved; the three left
        are judged by scripts alone.
- [x] Fingerprints are stable on the real tree: `--fingerprints` found the same
      7, 13 and 4 judges' fingerprints twice for `m1-12`, `m1-17` and `m1-100`,
      in 3-10 s; the CTest test `mutants_due_fingerprints` holds that for
      `m1-17`, under the `ninja_deps` lock.
- [x] `check` passes in both Windows trees, 320 of 320, 2026-09-30.
- [x] **Both Linux trees found the self-test Windows-only**, 2026-10-01: its two
      made-up repository locations were Windows paths, which are not absolute
      on Linux, and one was spelled in capitals, the same path only where case
      is ignored -- `mutants_due_self_test` failed in `linux-sanitize` and
      `linux-gcc`, 298 of 299 each. The code was checked on Linux first, with
      Linux paths, and was right. As the owner decided (decision 253), the
      locations are now real paths under the temporary folder, capitals are
      used only on Windows, and one new case holds the case rule on each
      system; the self-test passes on both.
- [x] The full mutation pass, recording the first fingerprints: the full pass of 2026-09-30 to 2026-10-01 at 7655041 -- 25 files, 261 caught, 13 survived, every one declared, none invalid or hung.
      Then decision 253's fix to `mutants-due.py` made due exactly the four
      files judged through it -- `m1-92`, `m1-100`, `m1-102`, `m1-103` -- and no
      other, the rule working as designed; all four passed at 8253acc, and
      `mutants-due.py` lists 0 of 25 due.
- [x] `scripts/mutants/m1-103.json` passes, 7 of 7, at 7655041 and again at 8253acc.

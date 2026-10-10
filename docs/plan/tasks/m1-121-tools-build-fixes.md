# M1-121 — The checking tools' and the build's review findings

Phase: B | Status: not started
Prerequisites: M1-120
Decided by: register decisions 443, 448 and 450, ruled 2026-10-10 on the
review's findings 4.2-4.11 and its five unneeded includes
([the findings](../../review/m1-116-findings.md))

## Purpose

The tools that decide whether a change is right had blind spots: a compile
error counted as a caught mutant, a hook that checks the wrong copy of a
file, checks that pass with nothing to check. VERIFICATION.md rule 23 is the
reason each is fixed with a self-test that shows it failing.

## What to do

- **4.2** `mutate.py` accepts only the compiler's own static-assertion error,
  with a self-test case built from clang's output.
- **4.3** the pre-commit hook checks each staged copy, and any file name.
- **4.4** `test_probe_lines` and `test_probe_grid` asked by the
  abort-listener check, from one list of the application's suites.
- **4.5** `--no-tests=error` on every CTest judge; `--verify` refuses a judge
  that does not exist, and an empty mutant list; `mutants-due.py` counts an
  unknown judge as due.
- **4.6** the fixture checksums fail when no line was read.
- **4.7** `doc-links` compares names in their real case, checks bare names
  against the repository's root, and its self-test covers anchors, ADR
  numbers, `file:line` citations and fenced blocks, with mutants for each.
- **4.8** a mutant file is an input of its own due-list entry.
- **4.9** the link-graph check at the end of `CMakeLists.txt`, recursive, for
  `orbsim_core` and `orbsim_view`, refusing every graphics and window target.
- **4.10** every row of the review's table: `app/BenchMode.hpp`
  self-checked, `broken_goldens` labelled `gpu`, the tree's own `ctest` in
  `mutate.py`, the source-cache claim and the other dependencies' pins,
  `measure-frame-cost.py`'s encoding, the stale messages and comments, and
  the narrowing audit widened to `static_cast<float>`.
- **4.11** the 17 members without `{}` initialised.
- **The five unneeded includes** removed, each build checked.

## Mutants

Each fix's own: the old static-assertion match; the hook reading the disk;
the two suites left out; no `--no-tests=error`; an empty checksum file
accepted; the case-blind comparison; the spec not an input -- each caught by
the self-test written for it.

## Done when

- [ ] Every finding above fixed, each shown failing before it passes.
- [ ] `check` passes in both trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed.

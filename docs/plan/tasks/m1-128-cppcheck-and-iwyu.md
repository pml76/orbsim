# M1-128 — cppcheck and include-what-you-use in `check`

Phase: B | Status: not started
Prerequisites: M1-113 -- after M1-130's decision point, by the order of register decision 495, ruled 2026-10-12: lower levels first; whether include-what-you-use becomes a rule is ruled there, with a measurement of what it still finds where `import` replaces `#include` (decision 494)
Decided by: register decisions 455-461, ruled 2026-10-10 at the owner's
request, reversing decisions 437 and 448

## Purpose

M1-116 built **cppcheck 2.22.0** and **include-what-you-use 0.27** on
Windows and ran each once, as a measurement ([the
findings](../../review/m1-116-findings.md), "The two analysis tools"). Each
found something nothing else here checks: cppcheck, members of plain structs
left without `{}` (finding 4.11); include-what-you-use, about sixty standard
headers used without being included, the class of defect gcc 14 has found
twice. A tool run once is a measurement; **a tool in `check` is a rule**
(VERIFICATION rule 21). This task makes both of them rules, on Windows.

## Measured before the rulings

2026-10-10, this machine, 20 cores, idle, over the Debug tree:

- **cppcheck**, `--enable=all --inconclusive --check-level=exhaustive`, 8 jobs:
  **19 s**, 71 messages -- 23 members without an initialiser, 21 raw loops,
  16 names that shadow a function, 7 value-or-reference and `const` items,
  2 parameter names that differ between declaration and definition, and
  2 more known false alarms -- plus 1,053 `missingIncludeSystem` notices.
  The review counted 17 members plus two false alarms on the same check;
  the difference is reconciled here before anything is fixed.
- **include-what-you-use**, 8 jobs, no mapping file: **452 s**, and its
  output mostly names Microsoft's internal headers; with the review's mapping
  file about sixty missing standard includes and five unneeded ones remain
  (the five are removed by M1-121).

## What to do

- **cppcheck fails `check` on everything it reports** at the settings above,
  less `missingIncludeSystem` (decision 456). Fixed first: the members without
  `{}` left after M1-121, the shadowing names, the value-or-reference and
  `const` items and the parameter names. **The 21 raw loops are rewritten as
  algorithms where that reads better** (guideline section 9); any that would
  not goes to the owner one by one, before anything silences it.
- **False alarms in a committed suppressions file**, one line each with its
  reason, each first shown to be cppcheck's by a control case (decision 457).
- **include-what-you-use fails `check` on any suggestion** (decision 458):
  the mapping file for Microsoft's library headers committed, every missing
  include added, every unneeded one removed. Where the tool is wrong, an
  `IWYU pragma` with its reason, put to the owner as found.
- **Both run per file, re-run only when what the file includes has changed,
  in the memory pool** -- `lint`'s mechanism, ADR 0024 (decision 459).
  **cppcheck in both trees; include-what-you-use in Debug only.** Confirm
  first what the Linux presets build, and leave them unchanged: the tools run
  on Windows only (decision 436).
- **A missing tool, or a version other than `STATUS.md`'s, fails `check`**
  with the recipe in `PROJECT_STATE.md` section 2 (decision 460).
- **A self-test for each tool**: a file with a planted defect it must report
  (decision 461).
- **The worked example** is checked as `lint` checks it, or the task says why
  not.

## Mutants

- Each tool's step made to pass without running: caught by its self-test.
- The version check removed: caught by a self-test that names a wrong version.
- A missing include put back in one file: caught by include-what-you-use.
- A member's `{}` removed: caught by cppcheck.

## Done when

- [ ] Both tools run inside `check` on Windows and pass, cppcheck in both
      trees and include-what-you-use in Debug.
- [ ] Each tool seen failing on its planted defect.
- [ ] Every suppression listed with its reason, and every loop the owner was
      asked about ruled.
- [ ] `check` passes in both trees, and the cost of a clean and of a one-file
      run is measured and written in `STATUS.md`.
- [ ] The task's mutant file has run after the commit and its record is
      committed.

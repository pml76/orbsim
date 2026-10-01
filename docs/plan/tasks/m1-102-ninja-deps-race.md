# M1-102 — Ask Ninja for the header record one test at a time

Phase: A | Status: **done, 2026-09-30**
Prerequisites: M1-91, M1-92
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decisions 248 and 249

## Purpose

**Two tests asking Ninja for a tree's header record at the same time can
destroy it, and then `mutants-due.py` lists too few mutant files as due,
without a word.** Found on 2026-09-30, when the MSVC tree failed
`mutants_due`, `mutants_due_shaders` and `mutants_due_scripts` after M1-101.

- Ninja keeps which headers each object includes in `.ninja_deps`. When that
  record has grown wasteful -- after a full rebuild, say -- Ninja rewrites it
  (compacts it) the next time it is opened, **even by a read-only query**,
  `ninja -t deps`.
- `mutants-due.py` makes that query, and since M1-91 `check` runs the tests in
  parallel: three `mutants_due` tests query the same tree at once.
- On Windows the rewrites collide. The MSVC tree was left with no
  `.ninja_deps` and a stray `.ninja_deps.recompact`; `ninja -t deps` then
  answers **nothing, with exit status 0**, every judge seems to include no
  header, and `m1-87.json` was reported not due when it is.

**Reproduced in a scratch project, 2026-09-30**: 300 source files, rebuilt
with changing header lists until one query on its own compacted the record
(175,144 bytes to 85,144). Three queries at once then went wrong in 7 of 10
trials -- "opening deps log: Permission denied" or "No such file or
directory" -- and in 3 of them a query reported success with an empty answer
and the record was gone. The same three queries one after another: 0
problems in 30.

The defect dates from 2026-09-27, when M1-91's parallel tests met M1-92's
queries; it appeared now because the MSVC tree's full rebuild took its
record over the threshold.

## What to do

As decided (decision 249):

- **The four `mutants_due` tests that read a tree hold one CTest lock,
  `ninja_deps`**, so CTest never runs two of them at once.
- **`parallel_tests` requires that lock** of every test that runs
  `mutants-due.py` on a tree -- not of its self-test, which reads none. Test
  first: two self-test cases without the lock were seen accepted before the
  rule, and the real tree was seen failing on exactly the four tests.
- **`mutants-due.py` refuses an empty header record**: "Ninja reported no
  header dependencies … the check would be checking nothing", exit status 1.
  That also covers a hand run that meets a build. Test first: the self-test
  case was seen accepting an empty record before the change.
- **`mutants_due_unmet_expectation_fails` requires the failure and the
  message both** (decision 251): under "expected to fail" a refusal or a crash
  was a pass. Its first form here, a CTest message rule, was a mistake the
  full mutation pass found the same day: `m1-92.json`'s mutant that makes the
  check return 0 while still printing the message survived. Measured in a
  scratch CTest project: no combination of CTest's own rules gets all three
  cases right. So `mutants-due.py --expect-unmet m1-12.json` runs the check and
  passes only if it returned a failure *and* named the file; with that mutant
  applied by hand it fails, "it returned 0, and said so".
- **The MSVC tree was repaired**: the stray file removed and the tree rebuilt,
  all 891 steps, since Ninja rebuilds whatever it has no header record for.

Considered and not taken: a set-up step that compacts the record once before
the tests, which leaves the threshold free to be crossed again; and reading
Ninja's file format in `mutants-due.py`, a second reader of a format this
project does not own.

**Mutants**: `scripts/mutants/m1-102.json`, four of them. The message rule is
not among them: weakening it changes nothing on a sound tree, so it was
measured in the scratch project instead.

## Done when

- [x] The race was reproduced before the fix, and serial queries shown not to
      have it: 7 of 10 trials wrong at once, 0 problems in 30 one at a time.
- [x] Each self-test was seen failing first ("an empty header record is
      refused (WRONG)"; two unlocked queries "accepted (WRONG)"); the real
      tree was seen failing the lock rule on exactly the four tests before
      `CMakeLists.txt` gave them the lock. On the damaged MSVC tree, before
      its repair, `mutants-due.py` refused with exit status 1.
- [x] `check` passes in both Windows trees, 319 of 319, 2026-09-30.
- [x] `scripts/mutants/m1-102.json` passes, 4 of 4, in the full pass of 2026-09-30 to 2026-10-01 at 7655041 -- 25 files, 261 caught, 13 survived, every one declared, none invalid or hung, and again at 8253acc.
- [x] The MSVC tree passes every test, 2026-10-01: `windows-msvc` 319 of 319, `linux-sanitize` and `linux-gcc` 299 of 299 each, once M1-103's self-test was made right on Linux (decision 253).

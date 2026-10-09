# M1-115 — Two checking tools that missed things

Phase: B | Status: **done, 2026-10-09**
Prerequisites: M1-111
Decided by: register decisions 423-427 -- two findings of M1-111, put to the
owner with their options and ruled the same day, placed before the review
(M1-116), which relies on both tools

## Purpose

**Two of the project's own checking tools reported less than they seemed
to**, both found while doing M1-111:

- **`scripts/mutate.py` named a skipped test among the tests that caught a
  mutant.** It took the name from every block Catch2 prints, and Catch2 prints
  one for a skipped case as well as for a failed one. The verdict, which comes
  from the exit code, was right; the reading of it was not.
- **`scripts/check-doc-links.py` checked a backticked path only when it began
  with a top-level folder.** A wrong path written from the document's folder,
  planted by hand in M1-111, passed. Measured over every document: 524 paths
  were skipped -- 351 written from `src/` (`core/Units.hpp`), 144 from the
  document's own folder, 14 into the worked example, and 15 pointing nowhere,
  all of those intended (paths in dependencies, in build output, in another
  project, and two files the register names on purpose). **None was wrong**;
  the 495 under `src/` and beside a document were simply unprotected.

## What to do

- `mutate.py`: count a block only if it reports `FAILED:`, with self-test
  cases built from Catch2's real output (decision 423).
- `check-doc-links.py`: also check a path whose first folder is beside the
  document, above it, or in a `src/` there (424); list the two intended
  absences as exceptions with their reasons, and report an exception that is
  no longer needed (425); and give the script a `--self-test`, run by CTest
  (426).

## What was done

- **`failing_cases` in `scripts/mutate.py` counts a block only if it reports
  `FAILED:`** -- Catch2's word for a failed check, an unexpected exception
  and a fatal error alike -- and still names the case and its location, as
  before. Catch2's separator is matched exactly, 79 dashes, measured, so a
  test's own line of dashes cannot pass for one. Four self-test cases, two of
  them on Catch2 3.16's real output: a skip from `test_file_replace` on
  Windows, and a three-case program built with gcc 14 under WSL -- a failure
  inside a section, a skip and a pass. The two new rules were seen failing
  on the old parser first.
- **`scripts/check-doc-links.py` checks the 495 paths**: a path is a claim
  about this repository when its first folder is a top-level one, or one
  beside the document, above it, or in a `src/` there. On the repository it
  then reported exactly the two predicted, `astro/AstroError.hpp` (decision
  90 names the header it chose not to write) and `view/Ellipsoid.hpp`
  (decision 329 names the header M1-49 will create), and both are
  `ABSENT_ON_PURPOSE` entries with their reasons. **An entry is reported once
  the file exists or the document no longer names it.** The wrong path
  planted in M1-111 is now reported, and so is a wrong `src/`-style one.
- **`check-doc-links.py --self-test`**, CTest's `doc_links_self_test`:
  eleven cases, each on a small repository of its own in a temporary folder
  -- every rule passing and failing, the new ones among them. The four new
  rules were seen failing before they were written, the seven old ones
  passing.
- No fuzzer was due: nothing under `src/core/`, `src/orbit/` or `src/astro/`
  changed.

## Done when

- [x] A skipped case is no longer named among the cases that caught a mutant.
- [x] The paths written from `src/` and from a document's folder are checked,
      and an exception nobody needs is reported.
- [x] `check` passes in both trees -- 531 of 531 in each, 2026-10-09, on the
      RTX A2000 machine; the new test is `doc_links_self_test`.
- [ ] The task's mutant file has run after the commit and its record is
      committed.

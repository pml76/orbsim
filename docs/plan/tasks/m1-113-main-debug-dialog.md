# M1-113 — The application stops, rather than waits, on a debug-library check

Phase: B | Status: not started
Prerequisites: M1-21
Decided by: register decision 405 (c) -- carried out of the phase A gate as a
task; register decision 363 fixed the same gap in the suites

## Purpose

**A Debug `orbsim.exe` that trips a check of Microsoft's debug library opens a
dialog and waits**, where it should print the message and stop. M1-21's
mutation pass found the gap in the suites: stepping before a vector's first
element is reported through the debug library's own report, not through
`abort`, so the suite hung for the harness's 300 s. `tests/AbortBehaviour.cpp`
now sends those reports to stderr (decision 363). The application's `main`
turns off `abort`'s dialog (`_set_abort_behavior`) but not the debug
library's, and was left as it was, since only the suites were asked for.

The probes, the smoke test and the benchmark all start the application under
CTest. A mutant or a real defect that trips such a check there waits for a
time limit instead of failing with a message.

## What to do

- In `main`, beside `_set_abort_behavior`, send the debug library's assertion
  and error reports to stderr, as `tests/AbortBehaviour.cpp` does, Windows
  Debug builds only.
- Say why at the site, and point at decision 363.

## Tests

- Seen by hand with a planted out-of-range vector access in a Debug build:
  a dialog before, the message and an exit after. The plant is not committed.

## Done when

- [ ] A Debug `orbsim.exe` prints the debug library's message and exits.
- [ ] `check` passes in both trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed.

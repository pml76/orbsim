# M1-90 — The abort listener, compiled once

Phase: A | Status: **done, 2026-09-27**
Prerequisites: M1-88
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decision 210

## Purpose

**`tests/AbortBehaviour.cpp` has 60 lines, but it is compiled and linted 23
times**, once for each suite it is compiled into. Its lint step is the slowest
in every tree, 114–171 s.

It is compiled into every suite because a Catch2 listener in a *static*
library is dropped unless something references it. An OBJECT library does not
have that problem: CMake links its compiled files into each program directly.

## What to do

- Make `orbsim_abort_behaviour` an OBJECT library, and link it into every suite
  and into `test_probe_clear`.
- Add a CTest test that runs every suite with `--list-listeners` and requires
  `AbortWithoutADialog` in each. A listener that stopped being linked would
  otherwise pass silently.
- Lint the file once, with the OBJECT library's flags.

## Done when

- [x] **`abort_listener` was seen failing first.** With the listener left out of
      `test_math`, it failed naming that program, "1 of 23 suites without the
      listener", and passed again once the plant was removed.
- [x] `compile_commands.json` holds **one** entry for the file, where it held
      23.
- [x] **Its lint step now takes 9.3 s in release and 9.9 s in Debug**, from
      `.ninja_log`, where it took 114–171 s.
- [x] Every suite still lists the listener: the 22 in `ORBSIM_TESTS` and
      `test_probe_clear`.
- [x] The self-test judges each of five listings right, including the
      listener's name appearing only inside another listener's description.
- [ ] A mutation pass on the check, `scripts/mutants/m1-90.json`, run after
      this task's commit.
- [x] `check` passes in both trees.

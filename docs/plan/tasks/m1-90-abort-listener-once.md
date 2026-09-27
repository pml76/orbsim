# M1-90 — The abort listener, compiled once

Phase: A | Status: planned
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

- [ ] The listener test was seen failing first, with the listener left out of
      one suite.
- [ ] `compile_commands.json` holds one entry for the file.
- [ ] Every suite still lists the listener.
- [ ] `check` passes in both trees.

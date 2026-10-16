# ADR 0027: `check` runs a test only when its inputs changed, and every test runs before a push

Status: **accepted** (2026-10-11 to 2026-10-16), with
[M1-129](../plan/tasks/m1-129-tests-by-fingerprint.md). Register decisions
483-542 for the design, 601-612 for its finish. **Supersedes one line of
[ADR 0024](0024-verification-costs-what-changed.md)**, "Selecting tests by
what changed. Not adopted.", which carries the note back.

## Decision

**`check` runs a test only when a fingerprint of everything its result rests
on differs from its last passing run. `check-full` runs every test and stamps
the commit. A pre-push hook refuses a push unless both `check` trees hold that
stamp for exactly the commit being pushed.** A fingerprint is a hash -- a
short code computed from file contents, which changes whenever any of them
does -- of:

- the test's definition, as CTest describes it, and the environment variables
  it can rest on: `PATH`, every `VK_*` and `ORBSIM_*` (decisions 483 and 501);
- each program on its command line, by its own bytes and those of every
  library it loads (decision 525);
- every file it read, from a read record each test writes as it ends, with
  each file's hash taken then (decisions 489, 498 and 527); where a test has
  no whole record, or its record names `data/`, `tests/golden/` or the build
  files, all of those (the cautious rule, decision 505);
- for a test run through `cmake` or `python`, every file under `scripts/` and
  `cmake/` and the build files (decision 531); for a test that starts the
  application, every compiled shader; for a GPU test, the card, its driver
  and the Vulkan versions (decisions 490 and 532);
- and the machine's own tools and time-zone data (decisions 509 and 533).

A test runs whenever a test it depends on runs (decision 508), and only a
passed test is remembered (decision 536). The skipping lives in a driver that
`check` calls, `scripts/run-tests.py`; a plain `ctest`, and the mutation
harness's `ctest -R`, still run every test they are given (decision 496).
Builds are reproducible in every toolchain, so a program rebuilt from
unchanged input is unchanged byte for byte (decisions 488, 504 and 511).

## Why

`check` reran every test whatever had changed: M1-118 ran it in both trees
about eighteen times, all 557 tests each time. Simulated over M1-118's sixteen
commits, 2,284 of its 8,771 test runs were needed (26%, decision 483).

## Considered

- **ADR 0024's answer: no selection.** It held that "a selector that misses
  one input lets a bug through silently", and that is still true. The answer
  here is the full run before every push, enforced rather than remembered,
  which catches a missed input before the change leaves the machine; and each
  part of a fingerprint is tested by changing its input (decision 602), and
  the read records are compared with what the tests really open (decisions
  499 and 612).
- **The changed files traced through Ninja's graph**, as `mutants-due.py`
  does: misled by a restored file's new time, and blind to files read at run
  time (decision 483).
- **Which functions each test enters**: taken up on top of this, as M1-131 to
  M1-133 (decisions 543 and 596).
- **The GPU tests always run** (decision 490); **the full run only at phase
  gates or once a day** (decision 484); **the hook running `check` itself**,
  which ties up the push for the length of both runs (decision 503).

## What it costs, and what it does not cover

**The fingerprints take a few seconds a run**: 0.8 to 5.9 s in the six
`check` runs of 2026-10-15, both trees, every file hashed in full each time
(decision 526). What the task saves, before and after, is measured in
[`docs/measurements/m1-129-before-after.md`](../measurements/m1-129-before-after.md).

**An input that is on no list is caught only by the full run before the
push.** The Process Monitor recording, repeated at each phase gate, keeps the
list of files outside the repository current (decision 509).

**Every test that starts the application is fingerprinted on every shader**
until [M1-131](../plan/tasks/m1-131-coverage-hook.md) records which shaders a
test read (decision 601): 50 tests, about 149 s of test time in Debug and 89 s
in RelWithDebInfo, run again for any shader edit.

**The stamps are per machine**, kept in the build trees and never committed
(decision 491), so a push from another machine needs `check-full` there.

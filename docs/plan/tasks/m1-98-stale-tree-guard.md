# M1-98 — Refuse a build tree set up from text no longer on disk

Phase: A | Status: **in progress, 2026-09-28**
Prerequisites: M1-97
Decided by: [ADR 0024](../../adr/0024-verification-costs-what-changed.md); register decisions 225 and 226

## Purpose

**`check` can test text that is no longer on disk, and say nothing.**

- CMake reads `CMakeLists.txt` when a configure starts, and writes the build
  plan (`build.ninja`) when it ends.
- Ninja decides whether to configure again by comparing file dates.
- An edit that lands in between leaves a plan built from the old text, but
  dated after the new one. So nothing configures again.

Such an edit happens when CLion reloads on its own while the file is being
edited, or while a mutation pass writes it. It was seen on 2026-09-27 as a
stale test list in `build/debug`.

**Reproduced first, on 2026-09-28**, in a scratch project whose configure
paused for 4 s while the file was edited from `A` to `B`. The next build
printed `built-from-A`, while the file on disk said `B`.

## What to do

- **`cmake/ConfigureInputs.cmake`**:
  - `orbsim_configure_input` takes a file's SHA-256 (a fingerprint of its exact
    bytes) as soon as the file has been read. It is called for
    `CMakeLists.txt` before `project()`, and for `cmake/GccWarnings.cmake`
    before it is included.
  - `orbsim_configure_inputs_unchanged`, last in the configure, compares each
    fingerprint again. It **fails the configure** if any file has changed, and
    otherwise writes `orbsim-configured-from.sha256` into the tree.
  - A failed configure leaves the old `build.ninja`, which is older than the
    edit, so the next build configures again by itself. This was checked in
    the scratch project.
- **`scripts/check-configure-current.py`**, the step `configure-current` that
  `check` waits for:
  - It compares the recorded fingerprints with the files on disk, computed
    again in Python.
  - On a mismatch it **stops and names the command**: `cmake -S … -B …`.
  - This catches an edit during CMake's last step, writing the build plan,
    which comes after the configure's own comparison.
  - It also requires that the record lists `CMakeLists.txt`, and that
    `check` waits for `configure-current`. Either fault would otherwise make it
    pass by checking nothing.
- **Tests:**
  - `configure_current`, the same check as a CTest test;
  - `configure_current_self_test`;
  - `configure_guard`, which configures a scratch project that edits its own
    `CMakeLists.txt` mid-configure (`cmake/TestConfigureGuard.cmake`).
- **Mutants**: `scripts/mutants/m1-98.json`, eight of them. A ninth, on a
  "record lists no files" check, survived the first pass: the requirement that
  the record list `CMakeLists.txt` already refuses an empty record, so that
  check could never decide anything. The check and its mutant were removed
  (decision 227).
- **The known gap**: an edit in the milliseconds between CMake reading a file
  and the fingerprint line running. CMake offers no earlier hook.

## Done when

- [x] `configure_guard` was seen failing for each fault it exists to catch.
      Checked on 2026-09-28 against copies of the module: without the
      refusal, "a CMakeLists.txt edited mid-configure was accepted"; without
      the record, "the steady configure recorded" nothing. One further change
      was found equivalent: the record is written only when the two
      fingerprints are equal, so writing either gives the same bytes.
- [x] On the real tree, `build/relwithdebinfo`, 2026-09-28:
  - An edit 20 s into an 80 s configure failed it: "CMakeLists.txt changed
    while this tree was being configured". The file was put back byte for
    byte, and a clean configure then succeeded.
  - A record with one digit changed failed `check` with the file named and
    the command to run. The step itself ran about 1 s in, but `check` took
    27.4 s to exit: Ninja lets jobs it has already started finish, and eight
    header self-check compiles of about 25 s each had started beside it.
- [x] `check` passes in both trees, 283 of 283, 2026-09-28.
- [ ] One full mutation rerun after M1-97 and M1-98 (decision 226).

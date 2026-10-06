# M1-111 — The benchmark writes its files through `writeFile`

Phase: B | Status: not started
Prerequisites: M1-108
Decided by: register decision 405 (a) -- carried out of the phase A gate as a
task, since a gate that changes code would have to be run again

## Purpose

**`src/app/BenchMode.cpp` keeps a third `writeFile` of its own.** M1-108 moved
the probe mode's copy into `src/view/FileWrite.hpp` (register decision 387),
where it is tested by `test_file_replace`, and left the benchmark's alone as
beyond its task. Two functions that do one job drift apart: the one in
`orbsim_view` is tested and the benchmark's is not.

## What to do

- Write the benchmark's summary and table through `view/FileWrite.hpp`'s
  `writeFile`, and delete the copy in `BenchMode.cpp`.
- Keep the benchmark's own error type: the view function reports a string,
  and `BenchError` carries it on with the file's name, as the copy did.
- Nothing the benchmark prints or writes changes, byte for byte.

## Tests

- `orbsim_bench_smoke` still passes, with the same rows.
- A benchmark run's `.txt` and `.csv` compared byte for byte, before and
  after, at the same `--frames`, apart from the timing figures themselves.

## Done when

- [ ] One `writeFile` in the project, in `src/view/FileWrite.hpp`.
- [ ] `check` passes in both trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed.

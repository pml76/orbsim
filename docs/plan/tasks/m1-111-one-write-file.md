# M1-111 — The benchmark writes its files through `writeFile`

Phase: B | Status: **done, 2026-10-09**
Prerequisites: M1-108
Decided by: register decision 405 (a) -- carried out of the phase A gate as a
task, since a gate that changes code would have to be run again

**Amended 2026-10-09, when the task ran.** Five questions were put before the
code and every recommendation was taken (register decisions 418-422). One of
them the document had not foreseen: reading the two copies found that the
shared `writeFile` missed a failure the benchmark's copy caught, so the task
as written would have made the benchmark worse (418). What they add is below,
under "What was done"; the text above it is as written.

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

## What was done

- **A refusal at close is reported** (decision 418). `view::writeFile`
  checked the stream before closing it, and the last bytes stay in the
  stream's buffer until the close: measured with `/dev/full` under gcc 14 and
  clang 23, a write of 5, 100 or 4,096 bytes was reported as a success, where
  the benchmark's copy reported the failure
  ([the measurement](../../measurements/m1-111-bench-bytes.md)). The file is
  now closed and checked in `writeChars`, the one function behind
  `writeFile` and `writeText`. The regression case in
  [`tests/test_file_replace.cpp`](../../../tests/test_file_replace.cpp) writes
  to `/dev/full`: seen failing in `build/linux-sanitize` before the fix and
  passing after, and passing in `build/linux-gcc`; on Windows, which has no
  such device, it reports itself skipped.
- **`view::writeText`** beside `writeFile` (decision 419), writing text with
  no copy; the benchmark's report and the probe's sidecar go through it, and
  the probe mode's helper that turned text into bytes is deleted. A case
  writes text holding `\n`, `\r\n` and 0x1A and reads back the same bytes.
  **`src/app/BenchMode.cpp`'s copy is deleted**, and with it the last file
  stream that writes outside `src/view/FileWrite.cpp`.
- **The benchmark's message when a file cannot be written** is `writeText`'s
  reason, which names the file, carried on unchanged (decision 420).
- **Nothing the benchmark writes changed** apart from the timing figures:
  a run at `e2ea1a4` and one on the change, both `--frames 30 --validate`,
  agree in all 25 checks of a script first seen catching three planted
  differences (decision 421;
  [the measurement](../../measurements/m1-111-bench-bytes.md)).
- **The mutants**:
  [`scripts/mutants/m1-111.json`](../../../scripts/mutants/m1-111.json),
  seven, two of them declared survivors -- the close left to the destructor,
  caught only where `/dev/full` exists, and a refused write of the
  benchmark's report going unreported, which no test can produce.
- No fuzzer was due: nothing under `src/core/`, `src/orbit/` or `src/astro/`
  changed.

## Done when

- [x] One `writeFile` in the project, in `src/view/FileWrite.hpp`.
- [x] `check` passes in both trees -- 530 of 530 in each, 2026-10-09, on the
      RTX A2000 machine, the `/dev/full` case reported skipped.
- [x] The task's mutant file has run after the commit and its record is
      committed. Run at `4b07c2b`, in `build/debug`: **`m1-111.json` 5 of 7
      caught, its 2 declared survivors surviving**, none invalid or hung --
      text mode and the halved text by `test_file_replace`, the two report
      mutants by `orbsim_bench_smoke`, the empty sidecar by
      `probe_golden_no_card`. About two minutes. The harness listed the
      skipped `/dev/full` case among the cases that caught the halved text:
      it takes every heading Catch2 prints, and Catch2 prints one for a
      skip. The verdict is the exit code's, and the close mutant, run
      against the same suite with the same skip, survived; the case that
      caught it is `writeText`'s. Recorded in `STATUS.md` as open.

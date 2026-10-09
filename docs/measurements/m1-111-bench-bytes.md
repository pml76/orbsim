# M1-111 — The benchmark's files, before and after, compared

Kind: reference
Binding: no — this file records a measurement and how to repeat it
Read when: you want the evidence behind register decisions 418 and 421, or
you want to repeat the comparison after a change to how the benchmark writes.

Measured 2026-10-08 and 2026-10-09 on the first machine: NVIDIA RTX A2000
Laptop GPU (`10de-25ba`), driver 582.53, clang 23.1.2. The script and both
summaries are in [`m1-111-bench-bytes/`](m1-111-bench-bytes/).

## A refusal at close, before the change

**`view::writeFile` reported success for a write the disk refused**, where
the benchmark's own copy reported the failure. Both functions were copied,
unchanged, into a scratch program, built with gcc 14.3 and with clang 23.1.1
under WSL, and made to write to `/dev/full`, the Linux device that refuses
every write as a full disk would:

| Bytes written | `view::writeFile` | the benchmark's copy |
|---|---|---|
| 5, 100, 4,096 | **reported success** | reported the failure |
| 8,000,000 | reported the failure | reported the failure |

Both compilers gave the same table. The reason: a file stream keeps the last
part of what it is given in its buffer until the file is closed, and the
shared function checked the stream before the close, leaving the close to
the destructor, which reports nothing. A write large enough to pass through
the buffer fails while it is being written, and was caught.

The regression case in `tests/test_file_replace.cpp`, "a write the disk
refuses only when the file is closed is reported", writes five bytes to
`/dev/full`. Built in `build/linux-sanitize` with the shared code as it
stood, it failed at `REQUIRE_FALSE( written )`; with the close and the check
added, it passed, and it passes in `build/linux-gcc`. On Windows, which has
no such device, it reports itself skipped.

## The benchmark's output, before and after

**Nothing the benchmark writes changed, apart from the timing figures.**

Each run was

```
build\relwithdebinfo\orbsim.exe --bench grid-orbit --frames 30 --validate --bench-out <dir>
```

with what it printed kept beside its two files: **before** at `e2ea1a4`, the
commit before M1-111, and **after** on M1-111's change, in the same tree.

`docs/measurements/m1-111-bench-bytes/compare.py` checks each run on its own
-- the summary file is exactly what the run printed before its `Written:` line; neither file holds
a carriage return or a byte-order mark, and each ends in one line feed, which
is what a write in text mode or a cut-off write would break; every table row
has six fields, finite timing figures and frame numbers without a gap -- and
then the two runs against each other: the summaries identical with the timing
figures masked, the tables' headers identical, the measured rows identical in
frame within the phase, path time and label, and the warm-up rows in one path
time and label. Run as

```
python compare.py before before-stdout.txt after after-stdout.txt
```

**Every check held**: 25 of 25.

**The script was seen failing first** (VERIFICATION.md rule 23), on copies of
the before-run's files altered three ways: every line feed written as a
carriage return and a line feed, as a text-mode write on Windows would -- four
checks failed; one measured row's path time changed -- one failed; the summary
given a different quality and its last byte cut -- three failed. And it failed
once on the untouched files, for a reason in the comparison rather than the
files: a Windows console stream writes each line feed as a carriage return and
a line feed, so the printed text is compared with that undone, while the files
themselves must still hold none.

**The tables differ in length, and that is the warm-up's doing**: it holds a
time, 5 s, rather than a number of frames (register decision 367), so it drew
6,753 frames before and 5,436 after. Both had 30 measured rows. The tables
themselves are not committed, as for M1-22's (register decision 381); the two
summaries are, as `docs/measurements/m1-111-bench-bytes/before-summary.txt`
and `docs/measurements/m1-111-bench-bytes/after-summary.txt`.

# M1-38 — Phase B gate

Phase: B | Status: not started
Prerequisites: M1-24 … M1-37

## Purpose

Phase B added two parsers of untrusted bytes, the project's first thread, two
offline tools, and three new pinned dependencies. Every one of those is a
category the day-to-day `check` does not fully cover.

## What to do

The full sweep, as in M1-23, **plus** what phase B introduced:

```
cmake --preset asan && cmake --build build/asan && ctest --test-dir build/asan --output-on-failure

wsl … --preset linux-sanitize   # ASan + UBSan
wsl … --preset linux-gcc        # the second compiler
wsl … --preset linux-tsan       # the first thread, new in M1-33
wsl … --preset linux-fuzz && for t in fuzz_orbit fuzz_ktx2 fuzz_ztree; do
    ./build/linux-fuzz/$t -max_total_time=240; done
```

## What to check, beyond "it passed"

- **Three fuzz targets, four minutes each, clean.** Any crash found on the way
  becomes a committed regression corpus entry and a named test, per rule 6.
- **ThreadSanitizer is live**, not merely configured, and the loader and cache
  suites run under it.
- **The licences are recorded**: `THIRD_PARTY.md` lists `stb`, `bc7enc_rdo` —
  with the compiled file list, because that repository carries three licences —
  and Catch2, alongside the original four.
- **The tools meet the same bar**: `tilegen` and `treeconv` compile under the
  full warning set with zero clang-tidy findings, and are in the format and lint
  lists. A tool exempt from the bar is where the next defect lives.
- **Coverage** is re-measured; the new parsers, the cache and the loader are read
  line by line for anything no test has executed.
- **The benchmark is re-run** with the textured sphere, and compared against the
  phase A wireframe baseline. This is the first number that includes a texture
  fetch and an upload path.

## What to record

In `PROJECT_STATE.md`: assertion counts per toolchain; the fuzzing totals; the
coverage table; the benchmark figure with the scene described; the size and
level depth of the generated Blue Marble pyramid; and every golden image now
committed with its approval date.

## Done when

- [ ] Five toolchains pass — Windows RelWithDebInfo and Debug, ASan, Linux
      clang+ASan+UBSan, Linux gcc-14 — plus TSan on the threaded code.
- [ ] Three fuzzers clean.
- [ ] `THIRD_PARTY.md` is complete and accurate, file lists included.
- [ ] `PROJECT_STATE.md` describes the tree as it now is.
- [ ] Nothing is carried into phase D on the promise of fixing it later.

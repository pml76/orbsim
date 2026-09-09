# M1-61 — Phase C gate

Phase: C | Status: not started
Prerequisites: M1-49 … M1-60
Decided by: [ADR 0005](../../adr/0005-correctness-is-enforced-by-tools.md)

## Purpose

The riskiest phase is behind, and the renderer is now feature-complete for this
milestone: everything after this is simulation, a line, and text. This gate is
correspondingly the most thorough of the four.

## What to do

The full sweep from M1-48 — ASan, Linux clang with ASan and UBSan, gcc-14, TSan,
GPU-assisted validation, synchronization validation — plus:

- **Four fuzz targets** now: `fuzz_orbit`, `fuzz_ktx2`, `fuzz_ztree` (which
  reaches the DDS and `.elv` parsers) and `fuzz_elevation`. Four minutes each,
  clean, and any crash found becomes a committed corpus entry and a named test.
- **The streaming suites under TSan**, since the loader, the cache and the
  eviction policy now run together under a moving camera.
- **A long soak**: the descent path run for thirty minutes on repeat under the
  validation layers, watching for a leak in the cache or the descriptor
  allocator. A ten-second probe cannot see a slow leak, and a slow leak in a
  streaming system is the defect that only shows up in a real session.

## What to check, beyond "it passed"

- **Memory does not grow.** Resident tile bytes and descriptor indices are
  bounded across the soak, and the cache statistics at the end match what the
  budget says. This is the most likely remaining defect in phase C.
- **The elevation landmarks still match** their published values after every
  change since M1-54 — the cheapest guard against a coordinate convention
  drifting during the phase.
- **Coverage** across `src/view/`, which is now the largest directory in the
  project. The quadtree, the cache and the mesh generator should be near-fully
  covered by headless tests; anything that is not is a line no test has ever
  run, in the subsystem most likely to hide one.
- **gcc-14 agrees** on the geometry. The ellipsoid conversions and the sag
  formula are exactly the kind of floating-point code where two compilers have
  already disagreed once in this project.

## What to record

In `PROJECT_STATE.md`: assertion counts per toolchain; four fuzzing totals; the
soak result including memory figures; the streaming numbers from the descent;
the frame-time table at every preset; the coverage table; and every golden with
its approval date. The list of golden images is now long enough to deserve its
own short section.

## Done when

- [ ] Every toolchain passes, counts matching.
- [ ] Four fuzzers clean.
- [ ] The thirty-minute soak is clean, with bounded memory.
- [ ] `PROJECT_STATE.md` is current.
- [ ] The renderer is done for this milestone, and the remaining phases are
      physics, a line and some text.

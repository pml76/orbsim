# M1-99 — gcc builds every suite again

Phase: A | Status: **done, 2026-09-30**
Prerequisites: M1-94
Decided by: [ADR 0017](../../adr/0017-every-warning-is-an-error.md); register decisions 243, 244 and 245

## Purpose

**`linux-gcc` cannot build three test suites**: `test_orbit_elements`,
`test_time_leap` and `test_time_ut1`. Found by M1-17's run of the other
compilers on 2026-09-29 and older than it ([M1-17](m1-17-golden-images.md),
"Other compilers").

- M1-94 split two test files on 2026-09-27 and moved what the parts share
  into `tests/OrbitSweepSupport.hpp` and `tests/TimeTestSupport.hpp`.
- A plain `constexpr` variable in a header gives every file that includes it
  its own copy. gcc's `-Wunused-const-variable=2` reports a copy the file does
  not use, and `-Werror` makes that an error.
- The Linux trees were left out of that day's measurement (decision 216), so
  nothing built them until M1-17.

**Reproduced first, on 2026-09-30**, in `build/linux-gcc`: seven constants
reported, `kNaN` and `kFuzzerHyperbola` in the orbit header, and `kNaN`,
`kInf` and the three published Julian dates in the time header.

**And measured before deciding**: a two-line header compiled by gcc-14 with
`-Wunused-const-variable=2 -Werror` reports a plain `constexpr` constant and
not an `inline constexpr` one. An *inline* variable is one shared copy for the
whole program, and it is how this project already writes constants in headers
(`tests/OrbitTestSupport.hpp`, `src/core/LeapSeconds.hpp`). clang 23 with
`-Weverything` is clean on both forms.

## What to do

As decided (decision 245):

- **A constant only one suite uses moves into that suite**, in an anonymous
  namespace, where an unused one is still reported by both compilers:
  - `kNaN`, `kFuzzerHyperbola` and `kFuzzerMu` into `tests/test_orbit_scales.cpp`;
  - `kJ2000JulianDate`, `kMjdZeroJulianDate` and `kUnixEpochJulianDate` into
    `tests/test_time.cpp`.
- **A constant more than one suite uses becomes `inline constexpr`** in its
  header: `kInf` in the orbit header, `kNaN` and `kInf` in the time header.
- Each header's opening comment says what moved and why.

Considered and not taken: every one of the seven `inline constexpr`. Nothing
would then ever report them unused, by either compiler.

No mutant file: the change moves where constants live and changes no
behaviour, and its only judge is gcc's build, which the mutation harness does
not run (decision 244). No fuzz target includes either header.

## Done when

- [x] `build/linux-gcc` builds all five suites that include the two headers,
      and each passes with the assertion count it has on Windows, unchanged:
      `test_orbit_scales` 3,600, `test_orbit_elements` 82,072, `test_time`
      420,790, `test_time_leap` 105,793, `test_time_ut1` 246,350. Measured on
      2026-09-30, and the same five counts in `build/debug` under clang.
- [x] `check` passes in both Windows trees, 319 of 319, 2026-09-30.
- [ ] The other compilers' full runs, after M1-101 (decision 244).

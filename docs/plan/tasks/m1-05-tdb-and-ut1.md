# M1-05 — TDB and UT1

Phase: A | Status: **done, 2026-09-19**
Prerequisites: M1-04, M1-06 — which now runs first (decision 30), because the
reference values below arrive through its fixture reader
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md), [ADR 0016](../../adr/0016-the-astronomy-is-erfa.md)

**Amended 2026-09-11** (decisions 27–29): TDB − TT is ERFA's `eraDtdb`
rather than a two-term approximation, and this task — the first to call ERFA —
is where ERFA is pinned and `src/astro/` begins. Each changed bullet says so.

**Amended again 2026-09-19, before the code, on the owner's rulings** —
decisions 53–67 of [the register](../milestone-1-decisions.md), fourteen
questions put at once with their options and a recommendation each, every
recommendation taken. Measured before they were asked, in a scratch directory:
ERFA's latest release is still **v2.0.1** (master is 12 commits ahead,
unreleased, among them "Make leap seconds threadsafe" of 2026-09-03, which
touches only functions this project does not call); its library is exactly
**249 files**; they build with zero warnings and both of upstream's validation
programs pass under clang 23.1.0, clang 23.1.1 with ASan and UBSan, gcc-14 at
-O0 and -O2, and MSVC 19.51. What changed:

- **Three corrections to this document.** It had the approximate direction of
  the TDB conversion the wrong way round (decision 56); it named one of
  upstream's two validation programs (62); and "with ΔUT1 = 0, UT1 equals UTC
  exactly" cannot hold inside a leap second, which UT1 has no label for (59).
- **Two budgets tightened**: TDB − TT from 100 µs to **20 µs** against Skyfield
  (53, 54), and the TT ↔ TDB round trip from 1e-9 s to **≤ 1 ps** (57).
- The rest is settled rather than changed: the reference fixture is committed
  (55), dates far from J2000 are converted rather than refused (58), `DeltaUt1`
  is a validated type with no default argument (60, 61), ERFA's files are
  globbed and counted (63), `fuzz_time` gains the new conversions (64), and the
  amplitude test's tolerances are derived (65). One question is left for M1-07
  on purpose (66). A fifteenth, found while the rulings were being written
  down, was put and ruled the same day: UT1 in the second a negative leap
  second removes is reported by name (67).

Each changed bullet below says which ruling changed it.

## Purpose

The last two scales, and the two that are not offsets. TDB is what solar-system
ephemerides are tabulated in; it differs from TT by a periodic term of about
1.7 ms amplitude, driven by Earth's motion in the Sun's gravity well. UT1
measures the Earth's actual rotation and wanders against UTC by up to 0.9 s,
which is what makes it the scale the body-fixed frame in M1-07 needs.

## What to implement

- *(Added.)* **ERFA, pinned** ([ADR 0016](../../adr/0016-the-astronomy-is-erfa.md)):
  `FetchContent_Declare` at the exact latest release tag, re-verified on the day
  (v2.0.1 on 2026-09-10), `SYSTEM`, built unedited as a static C library from
  all 249 library files, with `orbsim_fp` and `orbsim_sanitizers`, and linked
  privately to `orbsim_core`. ERFA has no CMake build of its own; the version
  macros its autotools build would generate are set from the tag. Its
  validation program `t_erfa_c` becomes a CTest test, so `check` proves the
  library was built right by each toolchain. ERFA's row in
  [`THIRD_PARTY.md`](../../../THIRD_PARTY.md) moves to "Pinned and compiled".

  *(2026-09-19.)* **v2.0.1, re-verified that day**, with a comment naming the
  unreleased thread-safety fix on master. The five macros are
  `PACKAGE_VERSION`, `PACKAGE_VERSION_MAJOR`, `_MINOR`, `_MICRO` and
  `SOFA_VERSION` (`"20231011"`), as upstream's `meson.build` sets them. The
  files are **globbed** from `src/`, less the two validation programs, and
  configuration fails unless there are exactly 249 (decision 63). **Both**
  validation programs become CTest tests under their own names, `t_erfa_c` and
  `t_erfa_c_extra` (62). ERFA's include directory is **SYSTEM** — the lint
  header filter would otherwise match `_deps/erfa-src/src/erfa.h`, and that is
  checked in `compile_commands.json` rather than assumed — and it links `m`
  where the platform has one, which the Linux builds need. `erfa.h` carries its
  own `extern "C"`.
- *(Moved here from M1-07.)* **`src/astro/` begins**: add it to
  `ORBSIM_CORE_SOURCES`, to the header self-check list, to the lint list, and to
  the directory map in `CLAUDE.md`. ERFA's headers are included from
  `src/astro/*.cpp` and nowhere else — never from a header — so nothing above
  `src/astro/` sees C, and `src/core/` still depends on nothing.
- **TDB − TT** *(amended)*, by ERFA's `eraDtdb`, in `src/astro/` because it calls
  ERFA. Evaluated at the geocentre: the observer's `u` and `v` are zero, so the
  diurnal and lunar terms that depend on where on the Earth's surface one
  stands — up to 2 µs, `eraDtdb` note 4 — vanish, which is right for a
  simulation whose origin is the Earth's centre, and the header says so. The
  wrapper builds ERFA's two-part date from the exact day and picoseconds —
  note 1's "MJD method" — and applies the result through `TimePoint`'s own
  arithmetic. Both directions: `tdbFromTt`, `ttFromTdb`. *(Corrected
  2026-09-19, decision 56.)* **The series is evaluated at TDB in both
  directions.** Its argument is formally TDB — "date1,date2 — date, TDB" — and
  note 1's "no practical effect" is the allowance for passing TT instead. So
  `ttFromTdb` evaluates it at its own argument, and `tdbFromTt` takes one
  fixed-point step: evaluate at TT, then again at TT plus that result, which
  puts the argument within 1e-21 s of TDB. Evaluated at TT once, the
  error would be the argument's shift, at most 1.7 ms, times the term's
  steepest slope, 3.3e-10 — about 0.6 ps — and 17.4% of round trips come back a
  picosecond out; with the step, 0 of 1,000,000 did, in either direction. The
  comment says this rather than leaving a reader to wonder. *(Was: "the inverse
  evaluates the series at TDB rather than TT, which note 1 says has no
  practical effect" — the approximate direction the wrong way round.)* **Never
  refused** (decision 58): `eraDtdb` returns no status, and the header states
  where its accuracy is claimed (1950–2050, 3 ns), measured (1600–2200,
  ≤ 9.51 µs from Circular 179) and bounded (the whole term stayed within
  ±1.76 ms at six epochs from year 1 to year 9999). Both functions return their
  instant, not `std::expected`. The two-part date is the MJD method as written:
  against the J2000 method, which note 1 calls optimal, it differs by at most
  3e-4 ps, measured. *(Was: the Explanatory Supplement's two-term
  approximation.)*
- **UT1** as `ut1FromUtc(UtcTime, DeltaUt1)` and back, with a `DeltaUt1` strong
  type in seconds, in `core/Time.hpp` — it needs no ERFA. **The default is
  zero**, and the header states the resulting model error plainly: ΔUT1 stays
  inside ±0.9 s by construction, which is ≤ 13.5″ of Earth rotation, about 420 m
  at the equator. The parameter exists so that an IERS series can be supplied
  later without changing a signature.

  *(2026-09-19, decisions 59–61.)* **The convention is ERFA's `eraUtcut1`**:
  UT1 is the SI seconds elapsed in the UTC day plus ΔUT1, carried over days of
  86 400 s, so it needs no leap-second table and cannot fail — `ut1FromUtc`
  returns a `Ut1Time`. ΔUT1 on a day that ends in a leap second is the IERS
  value for that day. `utcFromUt1` never produces 23:59:60, and reports
  `YearOutOfRange` at the calendar's two ends, so it returns `std::expected`.
  *(Decision 67, the same day.)* On a day ending in a *negative* leap second,
  one second of UT1 has no UTC instant under a single ΔUT1, and `utcFromUt1`
  reports it as a new `InsideRemovedLeapSecond`. The check is written against a
  table passed in, as decision 33's queries are, so the suite drives it with
  the synthetic negative table — the committed one cannot reach it.
  With ΔUT1 = 0, UT1 steps back one second at the midnight after a leap second;
  the published ΔUT1 steps by +1 s there and removes it, and the header says
  so. **`DeltaUt1` is a validated type**: a factory reports `NotFinite`, and
  `DeltaUt1OutOfRange` beyond ±0.9 s inclusive — ITU-R TF.460-6 (2002), Annex 1
  §D.1.2 — and holds the value in integer picoseconds. **No default argument**:
  every call site names its zero, so the model error is visible where it is
  taken. *(The line above said "the default is zero"; it still is, by name.)*
- `describe()` gains any new `TimeError` values. *(2026-09-19: two,
  `DeltaUt1OutOfRange` and `InsideRemovedLeapSecond`, decisions 60 and 67.)*
- *(Added 2026-09-19, decision 64.)* **`tests/fuzz_time.cpp` gains the new
  conversions**: the `DeltaUt1` factory on arbitrary bytes, UT1 → UTC → UT1 —
  the direction that is exact wherever it succeeds — and TT → TDB → TT,
  asserting that a success is normalised, never NaN, and inside its round-trip
  budget. Run with the standing 240 s budget before the
  commit.

## Out of scope

Reading IERS EOP files. Relativistic scale factors between TDB and TCB.
Nutation, which is M1-07's. *(Amended: the full Fairhead–Bretagnon series was
out of scope; it is what `eraDtdb` is.)*

## Tests

Extends `tests/test_time.cpp`, or a new `tests/test_astro_time.cpp` if the
TDB conversions live apart from `core/Time.hpp`.

- **TDB − TT against reference values** from an implementation this project did
  not write, committed through the M1-06 fixture mechanism with its provenance
  recorded. Assert **≤ 100 µs**. *(Amended:)* **not ERFA**, which now computes
  it — agreement between ERFA and ERFA proves nothing. Candidates are NOVAS 3.1
  (US Naval Observatory) and Skyfield; verify the terms of the one chosen and
  that it was written independently of SOFA, and record both. If it supports a
  tighter budget, put the tighter number to the owner before asserting it. If
  no independent reference can be obtained, **stop and raise it** rather than
  falling back on ERFA's claimed accuracy: `VERIFICATION.md` rule 3 exists for
  exactly this substitution.

  *(2026-09-19, decisions 53–55.)* **Skyfield 1.55** — MIT, Brandon Rhodes —
  whose `skyfield.timelib.tdb_minus_tt` is USNO Circular 179 eq. 2.6 (Kaplan
  2005), written independently of SOFA; its dependencies are certifi,
  jplephem, numpy and sgp4, none of them ERFA. NOVAS 3.1 evaluates the same
  equation, so it would add no independence, and its terms could not be
  confirmed. The reference is itself within 9.28 µs of `eraDtdb` over
  1900–2100, so the budget is **20 µs**, put to the owner and taken. The
  fixture is **committed**, as `data/skyfield/tdb-minus-tt.txt` in M1-06's
  format — epochs 1900–2100 on a 37-day stride, columns `jd_tdb` and
  `tdb_minus_tt_s`, the header naming Skyfield's version, the function and the
  equation — beside the script that generated it, `scripts/skyfield-fixture.py`,
  run in a virtual environment pinned to `skyfield==1.55`, and a README with
  the recipe. The recipe is run twice and must reproduce the file byte for
  byte. `tests/FixtureFile.hpp` gains a typed reader for it, giving `TdbTime`
  and `Seconds`, which refuses a file in another scale or with other columns by
  name, as the state-vector reader does.
- **Amplitude and period**: the term's peak magnitude is about 1.7 ms and its
  dominant period is one year. Sampled across two years, the extrema and the
  zero crossings land where the physics says, which is a check the fixture
  cannot give and a wrong sign would fail. *(2026-09-19, decision 65, the
  tolerances derived:)* over 2024–2025, each extremum within **60 µs** of the
  analytic 2e√(GM☉ a)/c² = 1.657 ms — the neglected terms' amplitudes sum to
  about that; measured 16 µs — each zero crossing within **±2.1 days** of the
  Sun's mean anomaly g = 0° and 180° from the Astronomical Almanac's
  low-precision formula — 60 µs over the annual term's slope of 28.5 µs a day;
  measured ≤ 0.9 day — and TDB ahead of TT from perihelion to aphelion. The
  constants are taken from cited sources, not from the planning spike.
- **Round trip** TT → TDB → TT to 1e-9 s over a seeded sweep, 1990–2050.
  *(2026-09-19, decision 57: to **≤ 1 ps**, and TDB → TT → TDB as well — one
  rounding to the picosecond each way. Decision 68, after the mutation pass:
  and **exact on at least 99% of the sweep**, both ways, which is what shows
  the fixed-point step is there — 10,000 of 10,000 with it, 8,263 without.)*
- *(Added 2026-09-19, decision 69, after the mutation pass.)* **Within a day**:
  twelve hours apart on one day, every fifth day of 2024–2025, TDB − TT changes
  as the Kepler problem's annual term does, within **1 µs** — the neglected
  terms' amplitudes times their rates times half a day come to about 0.7 µs;
  measured 0.37 µs worst, against changes up to 14.5 µs. The fixture's epochs
  are all midnights, so without this a conversion that dropped the time of day
  passed.
- **UT1**: with ΔUT1 = 0, UT1 equals UTC exactly; with ΔUT1 = 0.3 s it differs
  by exactly that; the sign convention is stated in a comment and asserted
  against it (UT1 = UTC + ΔUT1). *(2026-09-19, decisions 59 and 60: "exactly"
  on every instant but those inside a leap second, which UT1 cannot label — a
  case of its own shows 23:59:60.5 UTC becoming 00:00:00.5 UT1 of the next day,
  and coming back one second later. 0.3 s is exactly 300 000 000 000 ps. UT1 →
  UTC → UT1 exact over a seeded sweep; `DeltaUt1` refusing NaN, infinity and
  ±0.9 s plus a picosecond by name, and accepting ±0.9 s itself;
  `YearOutOfRange` at both ends of the calendar.)*
- **The type system**: TDB and TT still do not interconvert implicitly.
  *(2026-09-19: all 25 ordered pairs of scales, at compile time — none converts
  or constructs from another but itself.)*
- *(Added:)* **ERFA's own validation**, `t_erfa_c`, passes in both trees and
  under both Linux presets. *(2026-09-19, decision 62: and `t_erfa_c_extra`.)*

## Error budget

TDB − TT within **100 µs** of the independent reference — justified because
100 µs is 3 m of Earth's orbital motion, far below anything this milestone
claims. *(Added:)* the implementation claims better than **3 ns** against a time
ephemeris integrated on DE405 over 1950–2050 (`eraDtdb` note 7); that is
recorded, and not asserted, because no reference in hand can check it.
ΔUT1 = 0 is a **model error of ≤ 0.9 s**, recorded, not asserted.

*(2026-09-19, decisions 54, 57 and 58.)* TDB − TT within **20 µs** of Skyfield's
Circular 179 series — 0.6 m of Earth's orbital motion — which is itself within
9.28 µs of `eraDtdb` over 1900–2100. The TT ↔ TDB round trip **≤ 1 ps**. The
budget is asserted over the fixture's span, 1900–2100, and nowhere else; the
header records the measurement out to 1600–2200 and the bound beyond it.

## Verification

The standing rules, plus the two Linux presets, because a new dependency is
exactly where a second compiler disagrees first. *(2026-09-19:)* and
`windows-msvc` and `asan` as well — `PROJECT_STATE.md` section 8 asks for all
six on a change to `core/` — then `fuzz_time` for 240 s, and a mutation pass
over the new code in which only a mutant that compiles counts.

## As built, 2026-09-19

- **Files.** `src/astro/Tdb.hpp` and `.cpp`; UT1 and `DeltaUt1` in
  `src/core/Time.hpp`; the typed reader in `tests/FixtureFile.hpp`;
  `tests/test_astro_time.cpp`, new; seven cases in `tests/test_time.cpp`,
  three in `tests/test_fixture_file.cpp`, and M1-05's three claims in
  `tests/fuzz_time.cpp`; `scripts/skyfield-fixture.py` and
  `data/skyfield/`, both committed; ERFA and its two validation programs in
  `CMakeLists.txt`.
- **Seen failing first**, against stubs with the approved interfaces: all three
  of the reader's new cases; six of the seven UT1 cases -- the seventh,
  DeltaUT1 unmodelled, is the identity, which a stub ignoring DeltaUT1 also
  is; and two of the four TDB cases, the budget and the physics, where the
  round trip and the calendar's ends are identities too. Each message named
  what it wanted: "0.00099734651715169 <= 0.00002", "0 == 300000000000".
- **Measured.** TDB - TT within **7.89 us** of the fixture at worst, over its
  1,975 epochs, against a budget of 20 us; the annual term's peaks
  +-1.6406 ms against the Kepler problem's 1.6559 ms, and its four crossings
  in 2024-2025 at mean anomalies of 0.48, 0.90, 180.40 and 180.68 degrees.
  914,521 assertions in 103 cases, identical under both Windows trees, `asan`,
  `windows-msvc`, `linux-sanitize` and `linux-gcc`; zero warnings from gcc-14
  and from MSVC at `/Wall`; `t_erfa_c` and `t_erfa_c_extra` passing in all six.
  `fuzz_time`: 2,489,115 executions in 241 s, no finding.
- **The mutation pass**: twenty valid mutants, sixteen caught -- three by
  `static_assert`s before a test ran, two by Debug-build assertions -- and four
  surviving: one equivalent (the guard that keeps DeltaUT1's scaling inside
  2^52 behaves identically at 1.0 and 10.0), and three gaps put to the owner.
  Two were closed by tests (decisions 68 and 69) and seen catching their
  mutants: "8263 >= 9900" for the dropped fixed-point step, and
  "0.00001405762545743 <= 0.000001" for the dropped time of day. The third,
  the observer off the geocentre, is accepted as held by the code (decision
  70).
- **Found by the tools.** `readability-trailing-comma` on the multi-line
  `Seconds{eraDtdb(...)}` in `Tdb.cpp`, fixed by naming the value first; and
  `readability-function-size` on the fuzzer's entry point once it carried
  M1-05's claims, which moved into a function of their own.

## Done when

- [x] `check` green in both trees, `t_erfa_c` among the tests. *(2026-09-19:
      and `t_erfa_c_extra`.)*
- [x] The 100 µs budget is asserted against an external reference, not a claim,
      and not ERFA. *(2026-09-19: 20 µs, decision 54.)*
- [x] The ΔUT1 = 0 model error appears in the header, in the ADR, and in the
      commit message.
- [x] All five scales exist, and none converts to another by accident.
- [x] ERFA is pinned, its `THIRD_PARTY.md` row says so, and `src/astro/` is in
      the directory map.

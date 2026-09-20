# Skyfield reference data

**Committed**, unlike [`../horizons/`](../horizons/README.md). Skyfield is MIT
licensed, and the ruling not to redistribute Horizons output (2026-09-17) was
about Horizons' unstated terms, which do not apply here (register decision 55).
So the suite that asserts M1-05's budget runs on a fresh clone instead of
reporting itself skipped.

## What is here, and why

`tdb-minus-tt.txt` is **TDB − TT at the geocentre**, from Skyfield's
`skyfield.timelib.tdb_minus_tt`, at 0h TDB every 37 days from 1900-01-01 to
2100-01-01: 1,975 rows. M1-05 asserts its own TDB − TT, which ERFA's `eraDtdb`
computes, within **20 µs** of it.

- **Why not ERFA's own number.** ERFA computes the value under test, and
  agreement between ERFA and ERFA proves nothing
  ([ADR 0016](../../docs/adr/0016-the-astronomy-is-erfa.md)).
- **Why Skyfield** (decision 53). Its `tdb_minus_tt` is USNO Circular 179,
  eq. 2.6 (Kaplan 2005): seven terms of Fairhead & Bretagnon (1990), written
  independently of SOFA. Its dependencies are certifi, jplephem, numpy and
  sgp4, and none of them is ERFA. NOVAS 3.1 evaluates the same equation, so it
  would add no independence, and its terms could not be confirmed on
  2026-09-19.
- **Why 20 µs** (decision 54). Measured before it was chosen: the seven-term
  series is itself within **9.28 µs** of `eraDtdb` over 1900–2100, worst at
  2023-02-13, so no budget checked against it can honestly be tighter than
  about 10 µs. 20 µs is twice that, and 0.6 m of the Earth's orbital motion.
- **Why every 37 days.** 1900–2100 is ERFA's span for the Sun, which M1-08
  converts through. 37 days is not commensurate with the year, so the rows
  walk through every phase of the annual term rather than sampling one phase
  200 times. Every epoch is 0h, so every Julian date ends in .5 and is exact.

The header names Skyfield's version, the function, the equation and numpy's
version: the function takes its sine from numpy, so numpy is part of what
produced the numbers.

## `earth-orientation.txt`, for M1-07

**The rotation from the celestial frame to the terrestrial one**, from
Skyfield's `framelib.itrs.rotation_at`: 1,029 epochs every 71 days from
1900-01-01 to 2100-01-01, each at a different time of day. M1-07 computes that
rotation with ERFA's `eraC2t06a` and asserts it within **0.1 mas** of these
rows (register decisions 75, 76 and 82).

- **Why Skyfield again.** Its route is the *other* correct one: Greenwich
  apparent sidereal time applied to the equinox-based
  bias-precession-nutation matrix, where ERFA's is CIO-based. The pairing
  M1-07's plan first carried -- an equinox-based matrix with the Earth
  rotation angle -- is 1231" out against it, growing by 46" a year.
- **The limit of its independence, recorded rather than glossed.** Skyfield's
  IAU 2000A nutation and its complementary terms of the equation of the
  equinoxes are ports of NOVAS's, which NOVAS's own guide ties to the same
  IERS modules as SOFA's. The coefficient table *is* the model, and every
  implementation shares it; what this reference checks is the use of the
  model -- the scales passed, the units, the composition, the direction.
- **Why 0.1 mas.** Measured before it was chosen: the two agree to 53.6 µas
  over these rows, of which about 47 µas is the TIO locator s', which
  `eraC2t06a` applies even with polar motion zero and the equinox route does
  not. The budget is about twice the measured worst, the rule decision 54 set.
- **Why these epochs.** 71 days is 5.14 a year and 2.6 lunar months, so the
  rows land in every phase of nutation rather than in one; the time of day
  advances by the golden ratio in units of 2^-19 day, so the Earth rotation
  angle sweeps the whole turn and every instant is exact both as a double and
  as a whole number of picoseconds. DeltaT is held at 420 of those units,
  69.2138671875 s, which makes UT1 exact in the same two ways -- and which
  DeltaT it is does not matter, because both instants are written out and the
  test hands the code exactly what the reference was given.

## Regenerating them

Only when a pin moves. The script refuses any other Skyfield or numpy than the
pinned ones, because a different version could write different numbers under
the same header. From the repository root:

```
python -m venv skyenv
skyenv/Scripts/python -m pip install skyfield==1.55 numpy==2.5.3     # skyenv/bin/python on Linux
skyenv/Scripts/python scripts/skyfield-fixture.py tdb-minus-tt data/skyfield/tdb-minus-tt.txt
skyenv/Scripts/python scripts/skyfield-fixture.py earth-orientation data/skyfield/earth-orientation.txt
git diff --exit-code data/skyfield/
```

The last line must print nothing: the script is deterministic, with no
timestamp and no machine name, so the same pin gives the same bytes. Measured
on 2026-09-19, when the file was first generated: two runs, byte-identical,
SHA-256 `e80fdf8be917e2dbd6e204f68910b55639d65631d8e5fac6d1aaf689ae7dc171`, on
Windows with Python 3.14.0. The other packages pip resolved that day were
certifi 2026.7.22, jplephem 2.24 and sgp4 2.27; `tdb_minus_tt` uses none of
them.

The orientation fixture was generated the same way on 2026-09-20, and the
script checks one thing about itself before writing it: that the UT1 it writes
out is exactly the UT1 Skyfield used, compared as rationals rather than as
doubles, since the two are split at different days.

A different platform's numpy may round a sine differently in the last bit.
That is about 2e-19 s for TDB - TT, fourteen orders of magnitude below the
budget, and about 1e-16 of a matrix element for the rotation, which is 20
nanoarcseconds against a budget of 0.1 mas. If it ever shows up as a diff, the
reference has not changed in any sense the budget can see — but say so in the
commit rather than regenerating quietly.

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

## Regenerating it

Only when the pin moves. The script refuses any other Skyfield or numpy than
the pinned ones, because a different version could write different numbers
under the same header. From the repository root:

```
python -m venv skyenv
skyenv/Scripts/python -m pip install skyfield==1.55 numpy==2.5.3     # skyenv/bin/python on Linux
skyenv/Scripts/python scripts/skyfield-fixture.py data/skyfield/tdb-minus-tt.txt
git diff --exit-code data/skyfield/tdb-minus-tt.txt
```

The last line must print nothing: the script is deterministic, with no
timestamp and no machine name, so the same pin gives the same bytes. Measured
on 2026-09-19, when the file was first generated: two runs, byte-identical,
SHA-256 `e80fdf8be917e2dbd6e204f68910b55639d65631d8e5fac6d1aaf689ae7dc171`, on
Windows with Python 3.14.0. The other packages pip resolved that day were
certifi 2026.7.22, jplephem 2.24 and sgp4 2.27; `tdb_minus_tt` uses none of
them.

A different platform's numpy may round a sine differently in the last bit.
That is about 2e-19 s, fourteen orders of magnitude below the budget. If it ever
shows up as a diff, the reference has not changed in any sense the budget can
see — but say so in the commit rather than regenerating quietly.

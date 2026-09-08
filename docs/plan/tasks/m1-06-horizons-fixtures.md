# M1-06 — The Horizons fixture format

Phase: A | Status: not started
Prerequisites: M1-01, M1-03

## Purpose

`VERIFICATION.md` rule 3 has been marked **to build** since the document was
adopted, and its Part 4 says the three remaining gaps are, in order, Horizons
fixtures, the error budgets that depend on them, and runtime monitors. This is
the first of the three. Every accuracy claim in this milestone that is checked
against something external comes through this mechanism, so it is built once,
early, and deliberately plainly.

## What to implement

- **`tests/fixtures/`**, holding committed reference data as plain text. One
  file per dataset. Each begins with a header block of `key = value` lines that
  makes the data reproducible and self-describing:

  ```
  source     = JPL Horizons
  retrieved  = 2026-09-.. by <who>
  query      = target=10 (Sun); center=500@399 (Earth geocentre);
               vectors; frame=ICRF; time scale=TDB; units=km, km/s
  columns    = jd_tdb  x_km  y_km  z_km  vx_kms  vy_kms  vz_kms
  ```

  Then whitespace-separated rows, `#` for comments. Text rather than binary so
  a diff is readable and a wrong number is visible in review.
- **`tests/FixtureFile.hpp`**, a reader used by the suites: opens the file,
  parses the header into a small map, parses rows into a `std::vector` of
  records, and **reports by name** — `MissingHeaderKey`, `MalformedRow`,
  `WrongColumnCount`, `FileNotFound` — rather than throwing or returning a
  half-read table.
- **The first fixture**: geocentric Sun position and velocity at roughly 40
  epochs spread over 2000–2050, including two near perihelion and two near
  aphelion, so the distance test in M1-07 has something to bite on.
- **`tests/fixtures/README.md`**: the exact Horizons query for each fixture,
  written so anybody can regenerate it, plus a line on why the data is committed
  rather than fetched — a test that needs the network is a test that fails for
  reasons unrelated to the code.

## Out of scope

Any consumer of the fixtures — M1-07 is the first. A binary format. A downloader.
Fixtures for anything but the Sun; the GMAT trajectory fixture has its own task
(M1-68) and reuses this format.

## Tests

`tests/test_fixture_file.cpp`.

- A known-good file parses, and the header keys and row count are what the file
  contains.
- **Every failure by name**: a missing header key, a row with too few columns, a
  row with a non-numeric field, a truncated final line, an empty file, and a
  file that is not there. Each asserted by its own error, not by "it failed".
- Values survive the round trip to full `f64` precision — a fixture written with
  17 significant digits reads back bit-identical, because a reference that loses
  precision in its own reader is a reference that quietly loosens every budget
  that depends on it.

## Notes

The reader consumes only files this repository controls, so it gets no fuzz
target; the three parsers that do consume untrusted bytes — KTX2, `.tree` and
the elevation grid — each get one in their own task.

Rule 23 applies here more than anywhere: a fixture is only evidence if its
provenance is recorded. A file whose header does not say what was asked of
Horizons is a number somebody typed.

## Verification

The standing rules.

## Done when

- [ ] `check` green in both trees; `test_fixture_file` in the CTest list.
- [ ] Every reader failure has a test that asks for it by name.
- [ ] The Sun fixture is committed with a complete, reproducible header.
- [ ] `tests/fixtures/README.md` explains how to regenerate every fixture.

# M1-06 — The Horizons fixture format

Phase: A | Status: not started
Prerequisites: M1-01, M1-03
Decided by: [ADR 0009](../../adr/0009-time-is-a-type-with-a-scale.md), [ADR 0016](../../adr/0016-the-astronomy-is-erfa.md)

**Moved ahead of M1-05 on 2026-09-11** (decision 30). M1-05 checks TDB
against an independent implementation's values, and those arrive through this
reader, so the reader has to exist first. Both prerequisites were already
done.

## Purpose

`VERIFICATION.md` rule 3 has been marked **to build** since the document was
adopted, and its Part 4 says the three remaining gaps are, in order, Horizons
fixtures, the error budgets that depend on them, and runtime monitors. This is
the first of the three. Every accuracy claim in this milestone that is checked
against something external comes through this mechanism, so it is built once,
early, and deliberately plainly.

## What to implement

**Amended 2026-09-17: the Horizons output is not committed.** The terms were
read that day and state no licence anywhere, while the SSD FAQ asks to be told
what you intend to use and how ([`../../../THIRD_PARTY.md`](../../../THIRD_PARTY.md)).
The owner ruled: query Horizons, do not redistribute it. So the data is
generated into `data/horizons/`, which is gitignored, and **the recipe is
committed instead** -- [`../../../data/horizons/README.md`](../../../data/horizons/README.md)
already holds it, with every API parameter and why it has the value it does.
What that changes for this task is below, marked.

- **`data/horizons/`** (not `tests/fixtures/`), holding generated reference data
  as plain text. One file per dataset. Each begins with a header block of
  `key = value` lines that makes the data reproducible and self-describing:

  ```
  source     = JPL Horizons
  retrieved  = 2026-09-.. by <who>
  query      = target=10 (Sun); center=500@399 (Earth geocentre);
               vectors; frame=ICRF; time scale=TDB; units=km, km/s;
               corrections=none (geometric)
  columns    = jd_tdb  x_km  y_km  z_km  vx_kms  vy_kms  vz_kms
  ```

  *(Amended 2026-09-11.)* **Geometric**, with neither light-time nor aberration
  applied: M1-08 now asserts 0.1″ against these vectors, and aberration alone
  is 20.5″, so a corrected fixture would measure the correction rather than the
  code. The query records it so a reader can see it was asked for.

  Then whitespace-separated rows, `#` for comments. Text rather than binary so
  a diff is readable and a wrong number is visible in review.

  **The header is no longer a convenience, it is the provenance**, because the
  file itself is not in the repository. Keep Horizons' own header too: it states
  the frame, the corrections and the ephemeris version.
- **`data/horizons/checksums.sha256`, committed.** A hash of a file is not that
  file, so this redistributes nothing -- and it turns "I generated a fixture"
  into "I generated *the same* fixture the error budgets were measured against".
  Without it, two machines can disagree and neither can tell.
- **What `check` does when the data is absent**, which is the real cost of not
  committing it: the suites that need a fixture must **report themselves skipped,
  loudly**, and must not pass quietly. A fresh clone has to be able to run
  `check`, so a hard failure is wrong; a silent pass is worse, because that is
  ADR 0005's "a step that has silently been doing nothing". Catch2's `SKIP` with
  a message naming `data/horizons/README.md` is the shape.
- **`tests/FixtureFile.hpp`**, a reader used by the suites: opens the file,
  parses the header into a small map, parses rows into a `std::vector` of
  records, and **reports by name** — `MissingHeaderKey`, `MalformedRow`,
  `WrongColumnCount`, `FileNotFound` — rather than throwing or returning a
  half-read table.
- **The first fixture**: geocentric Sun position and velocity at roughly 40
  epochs spread over 2000–2050, including two near perihelion and two near
  aphelion, so the distance test in M1-08 has something to bite on.
  *(Corrected 2026-09-11: this said M1-07, which has no distance test.)*
- **`data/horizons/README.md`**: already written, 2026-09-17. Extend it with
  the query for each new fixture. It also carries why the output is not
  committed. The old reason for committing it -- a test that needs the network
  is a test that fails for reasons unrelated to the code -- still holds, and is
  exactly why the data is generated **once** into a gitignored directory rather
  than fetched by the suite.

## Out of scope

Any consumer of the fixtures — M1-05 is the first, with its TDB reference
values, and M1-07 and M1-08 follow. A binary format. A downloader.
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

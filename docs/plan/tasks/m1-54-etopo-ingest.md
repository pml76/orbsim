# M1-54 — ETOPO 2022 ingestion

Phase: C | Status: not started
Prerequisites: M1-28, M1-30

## Purpose

Elevation moved into phase C on 2026-09-07 because relief reads most strongly at
the terminator, where shadows are long, and because displacement is far cheaper
built into the subdivision than bolted on afterwards. This task gets the data
in.

The dataset is **ETOPO 2022, 60 arc-second, ice surface**, from NOAA NCEI: a
single global GeoTIFF of 21600 × 10800 signed 16-bit metres, about 444 MB,
public domain. Bathymetry is included, so the ocean floor is real rather than
flat — which matters less for the image than for not having a discontinuity at
every coastline.

## What to implement

**First, look at the file.** Read its header and record what it actually is —
byte order, whether it is striped or tiled, and which compression it uses. The
two implementation paths below are chosen by that answer, and guessing is how a
week goes missing:

- **If it is uncompressed or deflate**: a minimal GeoTIFF reader in
  `tools/elevgen/`, supporting exactly this file's layout and **refusing
  everything else by name**. A reader that supports one file is a reader that
  can be verified; a general TIFF reader is a project.
- **If it is anything else**: a documented one-time conversion to raw `int16`
  plus a small sidecar recording dimensions, byte order and the geographic
  bounds, with the exact command in `data/textures/README.md`, and a reader for
  that trivial format instead.

Either way:

- The reader **reports by name**: bad magic, unexpected byte order, unsupported
  compression, dimensions that disagree with the sidecar, a strip offset outside
  the file, and a truncated file.
- **`tests/fuzz_elevation.cpp`**, the project's fourth fuzz target. This parser
  consumes a 444 MB file from the internet, which is the definition of untrusted
  input.
- `data/textures/README.md` gains the download URL, the file size, the licence,
  and the fetch command — the file is gitignored like the imagery.

## Out of scope

Tiling the elevation (M1-55) — this task reads the grid and hands it over.
Reprojection: the data is already equirectangular on the same grid convention
the tiles use. The geoid: elevations are treated as heights above the WGS-84
ellipsoid, which M1-49 already recorded as a stated approximation.

## Tests

`tests/test_elevation_source.cpp`.

- **Against published landmarks**, which is the independent reference this task
  needs: the elevation at the summit cells of Everest, Denali and Mauna Kea, and
  the depth at the Challenger Deep, each within the dataset's own stated
  vertical accuracy of the published value. A transposed axis, a sign error or
  an off-by-one in the row order fails at least one of these — and that is
  precisely the class of error that otherwise ships.
- **Geometry**: the grid's corner cells map to ±180° longitude and ±90°
  latitude, with the pixel-centre-versus-pixel-edge convention stated explicitly
  and asserted. Half a cell at 60 arc-seconds is 900 m on the ground.
- **Row order**: the north pole is where the sidecar says. This is the single
  most common defect in raster ingestion and it deserves its own assertion.
- **Every refusal by name**, from hand-built malformed inputs.
- **The prefix sweep** from M1-26: every truncation either reports or succeeds.

## Verification

The standing rules, plus `fuzz_elevation -max_total_time=240` clean, plus a
visual check: dump the whole grid to a small grayscale PNG and look at it. If
the continents are recognisable and the right way up, the ingestion is right;
that one image catches more than any assertion here.

## Done when

- [ ] `check` green in both trees.
- [ ] The landmark elevations match published values.
- [ ] `fuzz_elevation` runs four minutes clean and joins the gate list.
- [ ] The grayscale dump of the whole world looks like the world.
- [ ] The download recipe and licence are recorded in `data/textures/README.md`.

# M1-32 — One tile on a sphere

Phase: B | Status: not started
Prerequisites: M1-17, M1-30, M1-31

## Purpose

Phase B's stated acceptance criterion: *"one tile loads from disk and draws."*
It is also the first time this project draws Earth.

## What to implement

- A **tile mesh**: a grid over the tile's latitude/longitude rectangle, placed
  on a sphere at Earth's radius, with texture coordinates from the same bounds.
  Generated in `orbsim_view`, so the geometry is testable without a GPU.
- A **surface pipeline** using the existing `body.vert` / `body.frag`, rewritten
  for the radiometric chain: the fragment shader outputs
  `L = albedo · E · cos θ / π` in W·m⁻²·sr⁻¹, with `albedo` sampled from the
  tile and `E` from M1-08's irradiance. The hand-tuned
  `0.04 + 0.96 · pow(ndl, 0.85)` goes, and with it the last display-referred
  constant in the project.
- The Sun direction comes from `astro/Sun.hpp` at the probe's fixed epoch — so
  this frame is also the first end-to-end check that the time system, the
  ephemeris and the renderer agree about where the Sun is.
- Camera-relative vertices through `toRenderSpace`, as everything is.

## Out of scope

More than one tile — the quadtree is phase C. Atmosphere. Night lights (M1-35).
Elevation. The ellipsoid: this is a sphere, and M1-49 changes that.

## Tests

- The mesh generator, headless: every vertex lies on the sphere to 1e-9 m
  relative; the corner vertices match the `TileId` bounds exactly; texture
  coordinates span [0,1]² with no flip — asserted against the bounds rather than
  against how it looks.
- **Probe `tile-earth`**, with a golden: the level-4 western-hemisphere tile,
  camera at 12,000 km, Sun at a fixed epoch chosen so the terminator crosses the
  visible disc.
- **A numeric check alongside the image**, because a golden alone would not
  catch a slow drift in the frame work: the projected screen positions of the
  tile's four corners are computed on the CPU from `TileId` and the camera, and
  compared against the rendered frame by locating the tile's edges in the
  luminance dump, to within one pixel.
- **The terminator is where the Sun says**: the rendered day/night boundary
  crosses the equator at the longitude computed independently in the test from
  the solar position and the Earth rotation angle, within one pixel. A sign
  error anywhere in M1-07 or M1-08 fails here, visibly and numerically.

## Frames to look at

`tile-earth.png`. Judge: it is recognisably a hemisphere of Earth, the right way
up and not mirrored; the coastlines are where they should be for the tile's
index; the terminator is a soft-edged curve in the right place for the epoch;
there is no wrap-around smear at the tile edge. **This is the frame that says
the first half of the milestone works**, so it deserves a careful look — mirror
errors in particular survive every numeric test that does not check handedness.

## Verification

The standing rules, plus `ctest -R probe_tile`.

## Done when

- [ ] `check` green in both trees.
- [ ] `tile-earth.png` approved and committed as a golden.
- [ ] The corner-projection and terminator checks pass numerically.
- [ ] `body.frag` no longer contains a hand-tuned lighting constant.
- [ ] Phase B's stated criterion — one tile loads from disk and draws — is met.

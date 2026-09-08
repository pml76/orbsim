# M1-35 — Night lights

Phase: B | Status: not started
Prerequisites: M1-31, M1-32

## Purpose

The milestone plan puts night lights in phase B because the data already exists
and the pipeline that carries them is built. They are also a large part of why
Earth from orbit reads as inhabited rather than as a textured ball, and the
terminator is where the eye goes first.

## What to implement

- **A second layer in `tilegen`**: `--layer night`, from
  `earth_lights_lrg.jpg`, through the same resample, mip and BC7 path. Same
  tile identity, so a night tile and a surface tile share an index.
- **The cache and the loader carry layers**, not just tiles: a `TileId` plus a
  `TileLayer` enum (`Surface`, `Night`, later `Elevation`). One more field, and
  it is far cheaper now than after two more layers exist.
- **The shader term, in physical units.** Night lights are emissive: the surface
  radiance on the night side is the lights' own emission, not reflected
  sunlight. The scale constant is derived from published night-time radiance
  measurements — VIIRS day/night band values for a lit city — and the derivation
  goes in the comment with its source. It will be approximate; what matters is
  that it is **bounded and sourced** rather than tuned until it looked nice,
  which is the difference this project keeps insisting on.
- The blend across the terminator is a smooth function of the solar zenith
  angle, and it is documented as a **modelling choice** — real twilight comes
  out of the atmosphere in phase D, and once that exists this blend should be
  revisited rather than kept forever.

## Out of scope

Water masks and specular glint — the source is still unlocated
(`data/textures/README.md`), and it is deferred. Orbiter's `Mask` layer, which
carries both night lights and water in one DXT1 with 1-bit alpha, and which is
not converted in this milestone. Clouds.

## Tests

- Layer generation is deterministic and produces the same tile identity coverage
  as the surface layer.
- **The day side is untouched**: with the night layer bound, the HDR readback on
  the sunlit side is identical to M1-32's, within `f16` quantisation. A blend
  that leaks emission into daylight fails here, and it is the failure that would
  otherwise be invisible under exposure.
- **The night side is not black and not blown out**: radiance on the unlit side
  is within the range the sourced constant predicts, checked numerically from
  the HDR dump rather than by eye.
- **Probe `earth-night`**, with a golden: the terminator across a populated
  landmass at a fixed epoch.

## Frames to look at

`earth-night.png`. Judge: the lights are on the dark side and not bleeding into
daylight; the coastlines they trace are in the right places, which is a second
independent confirmation that the tile indexing is right; the terminator blend
does not look like a hard band or a wide grey smear. Worth comparing side by
side with `tile-earth.png` from the same camera.

## Verification

The standing rules, plus `ctest -R probe_earth-night`.

## Done when

- [ ] `check` green in both trees.
- [ ] `earth-night.png` approved and committed as a golden.
- [ ] The day-side readback is unchanged, asserted numerically.
- [ ] The emission constant carries its source, and the terminator blend is
      marked in the code as provisional until phase D.

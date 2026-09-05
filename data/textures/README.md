# Planetary imagery

Nothing in `source/` or `tiles/` is committed. Both are large and both are
reproducible from the archives named below, so the repository holds the
instructions rather than the data.

```
data/textures/
  source/    downloaded imagery, as it arrives from NASA   (gitignored)
  tiles/     generated tile pyramids                        (gitignored)
```

## What to download

**Blue Marble Next Generation**, from NASA Visible Earth:
<https://visibleearth.nasa.gov/collection/1484/blue-marble>

Public domain. The set worth having for milestone 1:

| Layer | Why |
|---|---|
| Surface reflectance (monthly) | The base image. Pick one month to start; August is the usual choice for minimal snow cover. |
| City lights | The night side. A large part of why Earth from orbit reads as inhabited. |
| Specular / water mask | Sun glint on oceans. Cheap once the tile pipeline exists. |
| Topography | Not needed until the camera descends far enough for relief to show — likely milestone 2. |

Full-resolution BMNG is eight tiles at 21600 × 21600, roughly 2.7 GB per layer.
A single 8192 × 4096 global image is plenty to develop against and is a few tens
of megabytes; start there and only fetch the full set when the quadtree needs
the levels.

## Generated pyramids

`tiles/` is written by the tile tool built in phase B of
[the milestone plan](../../docs/plan/milestone-1-earth.md). It is derived data:
delete it whenever, and regenerate.

## Orbiter's own textures

Not required to start. The plan adds a reader for Orbiter's quadtree tiles
later, as a second `TileSource`, which brings the existing add-on texture
ecosystem with it. That needs an Orbiter installation; Blue Marble does not.

The format is documented in `Doc/Orbiter Developer Manual/PLANETS.tex` of the
MIT-licensed source repository (cloned to `C:\Reference\orbiter`), under
`sssec:tile_file_layout`: resolution levels 1–21 in 2-digit folders, latitude
bands in 6-digit folders, longitude index as the 6-digit filename.

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
<https://visibleearth.nasa.gov/collection/1484/blue-marble>. Public domain.

BMNG ships at 5400 x 2700 and 21600 x 10800, and as eight 21600 x 21600 tiles —
not at 8192 x 4096, which an earlier draft of this file claimed.

These three are already downloaded (29 MB total) and are what phases A, B and D
are built against:

```
curl -L -O https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73776/world.topo.bathy.200408.3x5400x2700.jpg
curl -L -O https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73776/world.topo.bathy.200408.3x21600x10800.jpg
curl -L -O https://eoimages.gsfc.nasa.gov/images/imagerecords/55000/55167/earth_lights_lrg.jpg
```

| File | Size | What it is |
|---|---|---|
| `world.topo.bathy.200408.3x5400x2700.jpg` | 2.3 MB | August 2004 surface, topography and bathymetry. Small enough to iterate on quickly |
| `world.topo.bathy.200408.3x21600x10800.jpg` | 26 MB | The same image at full single-file resolution. What the tile pyramid is generated from |
| `earth_lights_lrg.jpg` | 535 KB | City lights. A large part of why Earth from orbit reads as inhabited |

August is the conventional choice: minimal snow cover, so the coastlines and
vegetation read clearly.

### Still to find

**A water / specular mask.** Sun glint on oceans is cheap once the tile pipeline
exists and contributes a lot to realism. The obvious URL under `imagerecords`
returns 404, so the right source has yet to be located. Not a blocker — it can
be approximated from the bathymetry channel until then.

**Topography as a separate elevation channel.** Not needed until the camera
descends far enough for relief to show, which is milestone 2. The `topo.bathy`
image above already has shading baked in, which is enough for orbit.

### The full-resolution set

Eight tiles at 21600 x 21600, roughly 2.7 GB per layer. Only worth fetching once
the quadtree has levels deep enough to need them, and worth checking free disk
space first.

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

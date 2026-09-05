# Milestone 1 — Earth, orbit track, Orbit MFD

Status: planned, not started
Agreed: 2026-09-05

The first milestone that produces something recognisably Orbiter-shaped: a
realistic Earth seen from orbit, the vessel's orbit drawn as a track, and an
Orbit MFD reading out live elements.

**Earth is the priority.** The other two are comparatively small once the
render foundations exist.

---

## What "realistic Earth" decomposes into

Four independent problems, and it is worth being explicit that they are
independent, because they fail in different ways and can be built in any order:

1. **Surface imagery** — where the pixels come from, and how they get to the GPU
2. **Terrain LOD** — a spherical quadtree, so detail follows the camera
3. **Atmospheric scattering** — the limb, the terminator, the haze
4. **Planetary-scale precision** — already largely solved by the f64 simulation
   core and camera-relative rendering

The one that most decides whether the image reads as *Earth* is (3). A smooth
ellipsoid with correct scattering looks more like Earth than a heavily
tessellated one without it. That is why atmosphere is sequenced before the
quadtree below, even though the quadtree is the larger piece of work.

---

## Sequence

Each phase ends in something that can be looked at and judged.

| # | Phase | Done when | Risk |
|---|---|---|---|
| **A** | Render foundations | A wireframe grid draws at planetary scale with no jitter | Low |
| **B** | Tile pipeline | One tile loads from disk and draws on a sphere | Medium |
| **D** | Atmosphere | The limb and terminator read as Earth from 400 km | Medium |
| **C** | Spherical quadtree | Descend 400 km → 10 km with no popping or seams | **High** |
| **E** | Simulation loop | A vessel orbits, at 1× through 10000× | Low |
| **F** | Orbit track | The ellipse draws and stays put under time acceleration | Low |
| **G** | Orbit MFD | Ap/Pe/period/eccentricity readable while flying | Medium |

D before C is deliberate: it gets a convincing Earth early, and gives a correct
reference image to compare against while debugging tile seams. Phase letters
keep their original names so the ordering change stays visible.

---

## Phase A — Render foundations

Nothing planet-specific. The renderer currently clears the screen and nothing
else; there is no pipeline abstraction at all.

- Graphics pipeline creation and shader module management (the `line.*` and
  `body.*` shaders already compile to SPIR-V but are never loaded)
- Camera with an `f64` world position and **camera-relative rendering**: world
  coordinates are subtracted in `f64` and only then narrowed to `f32`, in one
  named place, exactly as `coding-guidelines-example` does at its GPU boundary
- Reverse-Z projection with an infinite far plane
- A line renderer — needed for the orbit track in F, and worth far more than it
  costs as a debugging tool for the quadtree in C

**Done when:** a grid drawn at Earth radius shows no vertex jitter as the camera
moves, which is the thing camera-relative rendering exists to prevent.

## Phase B — Tile pipeline

A quadtree needs tiled input, and Blue Marble ships as a handful of enormous
equirectangular images. So this phase builds a pyramid.

- A `TileSource` interface — the seam that lets the Orbiter reader arrive later
  as a second implementation rather than a parallel code path
- `BlueMarbleSource`: an offline tool turning equirectangular source imagery
  into a tile pyramid, BC-compressed
- Async loading on a `std::jthread` worker, publishing finished tiles to the
  render thread as immutable values — no shared mutable state, per section 13
- DDS/KTX2 header parsing. Vulkan consumes BC formats natively, so this is
  header work and an upload, not a decoder

**Done when:** one tile loads from disk and draws.

**Note:** this phase defines *our* tile format. That is a consequence of
choosing a quadtree with Blue Marble as the source, and it is a good one — the
Orbiter `.tree` reader later becomes an adapter behind `TileSource`.

## Phase D — Atmosphere

The part that sells the image.

- **Hillaire 2020** scattering: transmittance LUT, multiple-scattering LUT,
  sky-view LUT, aerial-perspective volume. Roughly four compute dispatches, and
  correct both from orbit and from the ground — which matters, because this
  simulator will eventually land.
- Earth's Rayleigh, Mie and ozone coefficients from the published parameters
- Aerial perspective applied to terrain, so distant ground hazes correctly

Deliberately *not* Orbiter's own haze model: it dates from around 2000, is built
around Direct3D fixed-function assumptions, and modern scattering is both more
accurate and easier to implement.

**Done when:** the limb, the terminator and a sunrise from orbit look right.

## Phase C — Spherical quadtree

The largest and riskiest phase.

- WGS-84 ellipsoid, not a sphere — Orbiter models the flattening and so should
  this
- Quadtree subdivision driven by a screen-space error metric
- Crack fixing between adjacent levels: skirts, or geomorphing
- Tile cache with eviction under motion

**Done when:** descending from 400 km to 10 km shows no popping and no seams.

**Why this is the risk:** screen-space error metrics, cracks and cache eviction
under motion are where planet renderers consume their schedule. Everything else
in this milestone is well-trodden.

## Phase E — Simulation loop

- Fixed timestep with an accumulator, decoupled from the render rate
- Render-side interpolation between the last two states
- Time acceleration, 1× to 10000×

Section 19 argues this at length: a variable timestep makes the trajectory a
function of frame rate, so a slow machine flies a different orbit than a fast
one and neither can reproduce the other.

**Done when:** a vessel orbits and time acceleration does not change where it
ends up.

## Phase F — Orbit track

The algorithm already exists, written and tested, in
`coding-guidelines-example/src/orbit/OrbitPath.*`. This phase moves it into
`src/orbit/` and draws it.

- Sample the orbit; render camera-relative through the phase A line renderer
- Apsis markers

**Done when:** the ellipse is drawn and stays put while time is accelerated.

## Phase G — Orbit MFD

- Bitmap font and text rendering
- An MFD panel drawn as a screen-space quad
- Live readout: apoapsis, periapsis, period, eccentricity, inclination

The MFDs are where Orbiter's actual gameplay lives, so this deserves more care
than its size suggests.

**Done when:** the numbers are readable while flying and agree with the tests.

---

## Decisions taken

**Imagery: Blue Marble first, Orbiter tiles later.** Orbiter's Earth textures
are derived from Blue Marble Next Generation anyway. Going to the source gives
public-domain data and no dependency on an Orbiter installation. The Orbiter
`.tree` reader arrives later as a second `TileSource`, which then brings the
add-on ecosystem with it.

**Quadtree from the start**, rather than a fixed ellipsoid with LOD retrofitted.
This is the harder path and adds roughly two to three weeks before Earth first
appears; it was chosen deliberately.

**Reference source lives outside the repository**, at `C:\Reference\orbiter`
(shallow clone, 1.1 GB).

The licence picture turned out better than expected once the clone was
inspected, and the earlier reading of it was wrong. What is actually there:

| Path in the clone | Licence | How it may be used |
|---|---|---|
| repository root | MIT (Schweiger, 2000–2026) | Read and borrow, with attribution |
| `Utils/tileedit/qt/src/` | MIT — no GPL headers, covered by the root licence | **Usable.** The clearest tile-format reference there is |
| `Utils/tileedit/qt/extern/fastdxt/` | **LGPL** (vendored third-party DXT codec, 78 KB) | Do not copy. We have BC support in Vulkan anyway |
| `OVP/D3D9Client/` | **LGPL** | Where `TileManager2` actually lives — but it is Direct3D, and LGPL |

The standalone `mschweiger/orbiter-tileedit` repository on GitHub is **GPL v3**:
the same code under a different licence, which is entirely the author's
prerogative. Since an MIT-licensed copy of it exists inside the monorepo, that
clone was **deleted** rather than kept and carefully avoided. Removing a hazard
beats managing one.

### The format specification

`Doc/Orbiter Developer Manual/PLANETS.tex` — 881 lines of LaTeX in the MIT repo,
with a section labelled `sssec:tile_file_layout`. It documents exactly what
phase B needs:

- `TileFormat = 2` in `Config/<planet>.cfg` selects the quadtree format
- Resolution levels 1 to 21, in 2-digit subdirectories
- Latitude bands in 6-digit subdirectories, longitude index as the 6-digit
  filename, `ilng = 0` being the westernmost tile at 180° W
- The `nlat` / `nlng` counts per level
- `MaxPatchResolution` and `MaxCloudResolution`, including Orbiter's behaviour
  of interpolating missing high-resolution tiles from the nearest ancestor

There is no `PlanetTextures.pdf` in the source repository; the built PDFs ship
with the Orbiter *distribution*. The LaTeX source is the same content and is
already on disk.

---

## Open questions

- **Tile format on disk:** KTX2 with BC7, or plain DDS? KTX2 has mipmap and
  supercompression support and a cleaner spec; DDS is what Orbiter uses, which
  matters slightly for the later reader.
- **Elevation:** Blue Marble includes topography, but phase C only needs it once
  the camera descends far enough for relief to be visible. Probably milestone 2.
- **Night lights and specular water:** both available in the Blue Marble set,
  both cheap once the tile pipeline exists, and both large contributors to
  looking right. Likely worth folding into phase B.

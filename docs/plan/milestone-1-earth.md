# Milestone 1 — Earth, orbit track, Orbit MFD

**The work is broken into 84 tasks in [`milestone-1-tasks.md`](milestone-1-tasks.md),
under the rulings recorded in [`milestone-1-decisions.md`](milestone-1-decisions.md)
(2026-09-08). This file says what the phases are and why; that queue says what to
do next.**

Status: planned. Progress is in [`../STATUS.md`](../STATUS.md)
Agreed: 2026-09-05
Amended: 2026-09-07, for [`../adr/0006`](../adr/0006-simulation-not-sandbox.md)
(a simulation, not a sandbox) and
[`../adr/0007`](../adr/0007-render-quality-is-a-struct.md) (scalable visuals).
The reasoning behind every amendment is in [`realism.md`](realism.md).

**The milestone grew.** Phase A gains the linear HDR pipeline, the quality
plumbing and the time system; phase C gains elevation; phase E goes from a
`propagate()` loop to a real integrator. The sequence A → B → D → C → E → F → G
is unchanged and the Earth-first priority still holds, but the estimate that
stood on 2026-09-05 no longer does.

The first milestone that produces something recognisably Orbiter-shaped: a
realistic Earth seen from orbit, the vessel's orbit drawn as a track, and an
Orbit MFD reading out live elements.

**Earth is the priority.** The other two are comparatively small once the
render foundations exist.

---

## What "realistic Earth" decomposes into

Five independent problems, and it is worth being explicit that they are
independent, because they fail in different ways and can be built in any order:

1. **Surface imagery** — where the pixels come from, and how they get to the GPU
2. **Terrain LOD** — a spherical quadtree, so detail follows the camera
3. **Atmospheric scattering** — the limb, the terminator, the haze
4. **Radiometry** — light in physical units, an HDR target, exposure, one
   tonemap. Added 2026-09-07; see `realism.md` section 2.1
5. **Planetary-scale precision** — an `f64` simulation core, camera-relative
   rendering, and origin rebasing. **None of it is written yet**

Two corrections to the original four, both from `realism.md`.

**(4) was missing entirely, and it is the one that most decides whether the
image reads as Earth.** The original list named scattering as that item, which
is right only if the pipeline underneath it is already radiometric — and it is
not. `shaders/body.frag` lights the surface with `0.04 + 0.96*pow(ndl, 0.85)`
and the swapchain is deliberately `UNORM`, so correct scattering would be
tonemapped by hand-tuned constants. Scattering still sells the image; it cannot
sell it through an LDR pipeline.

**(5) said "already largely solved" and was wrong twice.** Camera-relative
rendering has no code at all, and ADR 0006 adds a second precision boundary —
barycentric versus body-relative — that did not exist when a single central
body was the whole model. `realism.md` section 1.5 has the detail.

That correction does not change the sequencing argument: a smooth ellipsoid
with correct scattering still looks more like Earth than a heavily tessellated
one without it, which is why atmosphere comes before the quadtree below. It
does mean (4) is a prerequisite of (3) rather than a peer of it.

---

## Sequence

Each phase ends in something that can be looked at and judged.

| # | Phase | Done when | Risk |
|---|---|---|---|
| **A** | Render foundations | A wireframe grid draws at planetary scale with no jitter, through a linear HDR pipeline | Medium |
| **B** | Tile pipeline | One tile loads from disk and draws on a sphere | Medium |
| **D** | Atmosphere | The limb and terminator read as Earth from 400 km, in physical radiance | Medium |
| **C** | Spherical quadtree | Descend 400 km → 10 km with no popping or seams, with relief visible at the terminator | **High** |
| **E** | Simulation loop | A vessel orbits under a real integrator, within a stated error budget against a NASA GMAT reference trajectory | Medium |
| **F** | Orbit track | The track precesses at the J2 rate, and the rate matches the analytic value | Low |
| **G** | Orbit MFD | Ap/Pe/period/eccentricity readable while flying, and visibly osculating | Medium |

D before C is deliberate: it gets a convincing Earth early, and gives a correct
reference image to compare against while debugging tile seams. Phase letters
keep their original names so the ordering change stays visible.

Two risks moved on 2026-09-07. **A went Low → Medium**: it now carries the HDR
pipeline, the quality plumbing and the time system, any of which is cheap alone
and none of which is free together. **E went Low → Medium**: it was a
`propagate()` loop and it is now an integrator with a determinism story, plus
the first validation against external data. E is the phase most likely to be
underestimated, because the entry it replaces made it look like plumbing —
there is a case for calling it High, and it should be revisited once the
integrator choice in `realism.md` section 4 is settled.

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

Three things were added on 2026-09-07, all for the same reason: each is cheap
now and expensive once there is a renderer that assumed its absence.

- **The linear HDR pipeline** — an RGBA16F target, light in physical units, a
  real exposure stage, one tonemap (**AgX**, decision 16), sRGB encoded once at
  the end. The swapchain stops being where shaders write display-ready colour.
  This is
  item 1 in `realism.md` section 3 and the highest-ranked item in the whole
  document: every shader written before it would have to be rewritten after it.
- **The `RenderQuality` plumbing** ([`../adr/0007`](../adr/0007-render-quality-is-a-struct.md)).
  It arrives *empty* and gains fields as features land. The point is the path,
  not the knobs: threading a settings struct through a renderer built for one
  fixed configuration is the same class of retrofit as the HDR pipeline itself.
- **The time system** — a `TimePoint` carrying an explicit scale (UTC, TAI, TT,
  TDB, UT1), in `core/`. It is not needed until phase E. It is here because
  `realism.md` ranks it second precisely for being *"cheapest today, at zero
  call sites"*, and B, D and C all come before E: waiting for E means four
  phases of code get written against a bare `Seconds` first, which throws away
  the exact property that earned it the rank.

**Done when:** a grid drawn at Earth radius shows no vertex jitter as the camera
moves — which is the thing camera-relative rendering exists to prevent — and
that grid is lit through the radiometric chain end to end, from a value in
W/m^2 to a tonemapped pixel, with a `RenderQuality` value passed down the path
even though nothing yet reads it.

## Phase B — Tile pipeline

A quadtree needs tiled input, and Blue Marble ships as a handful of enormous
equirectangular images. So this phase builds a pyramid.

- A `TileSource` interface — the seam that lets the Orbiter reader arrive later
  as a second implementation rather than a parallel code path
- `tilegen`: an offline tool turning equirectangular source imagery into a tile
  pyramid, BC-compressed. The seam is a closed set, so the sources arrive in
  order as `SyntheticTileSource`, `KtxPyramidSource` and `TreeArchiveSource`
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

Two additions from 2026-09-07. The LUTs must produce **physical radiance**, not
tuned colour — which is free if phase A did its job, and impossible if it did
not. And the **LUT resolutions and the multiple-scattering order become the
first real fields in `RenderQuality`**: this phase is where the empty struct
from phase A stops being empty.

Add the Sun as a **disc rather than a direction** here too, since it is the
same shader work: it subtends about 0.53 degrees from Earth, which is what
gives the terminator its soft edge and what makes a penumbra exist at all.

**Done when:** the limb, the terminator and a sunrise from orbit look right,
with the scattering computed in physical units and the exposure doing the work
that hand-tuned constants used to do.

## Phase C — Spherical quadtree

The largest and riskiest phase.

- WGS-84 ellipsoid, not a sphere — Orbiter models the flattening and so should
  this
- Quadtree subdivision driven by a screen-space error metric
- Crack fixing between adjacent levels: skirts, or geomorphing
- Tile cache with eviction under motion
- **Elevation**, moved here from milestone 2 on 2026-09-07: displacement from
  **ETOPO 2022** (60 arc-second, ice surface, NOAA NCEI, public domain —
  decision 23), and normal mapping from the same data. Blue Marble's
  `topo.bathy` imagery has its relief shading baked into the pixels and is not
  an elevation source. Relief reads most strongly at the terminator, where
  shadows are long, and displacement is far cheaper built into the subdivision
  than bolted onto it afterwards. `realism.md` section 2.3
- The screen-space error threshold, the maximum level and the cache budget are
  `RenderQuality` fields, not constants

**Done when:** descending from 400 km to 10 km shows no popping and no seams,
and mountains cast visible shadows at the terminator.

**Why this is the risk:** screen-space error metrics, cracks and cache eviction
under motion are where planet renderers consume their schedule. Everything else
in this milestone is well-trodden.

## Phase E — Simulation loop

**Rewritten 2026-09-07.** This was "call `propagate()` in a loop" and it is now
where the simulation of ADR 0006 actually begins.

- Fixed timestep with an accumulator, decoupled from the render rate
- Render-side interpolation between the last two states
- Time acceleration, 1× to 10000×
- **A real integrator.** Settled on 2026-09-08: **Cowell first, then Encke**,
  both kept and diffed against each other. Fixed-step Cowell with RK4 comes
  first, so that convergence order can be measured against theory; Encke then
  integrates only the *deviation* from an osculating reference conic, reusing
  the propagator that is already written and tested. See
  [the register](milestone-1-decisions.md), decisions 7 to 12
- **The first perturbation: J2.** Largest accuracy gain per line of code in the
  whole project, and the thing phase F below is going to draw
- **Uses the `TimePoint` from phase A.** The integrator is where a bare
  `Seconds` since an unstated epoch stops being survivable

Section 19 argues the fixed timestep at length: a variable timestep makes the
trajectory a function of frame rate, so a slow machine flies a different orbit
than a fast one and neither can reproduce the other. `realism.md` section 6.1
adds the other half — **the physics is never the thing that scales.** When it
cannot keep up, the honest answers are a cheaper step within the stated budget
or fewer modelled bodies, each written down as a change to the model. Never a
quality preset.

**Done when:** a vessel orbits under the integrator; time acceleration does not
change where it ends up; **and the trajectory sits inside a stated error budget
against a NASA GMAT reference trajectory.** The first two are self-consistency,
which `../VERIFICATION.md` rules 3 and 4 say is not evidence on its own — a
wrong `mu` conserves energy perfectly. Write the number before writing the code.

**This said "against JPL Horizons" until 2026-09-08, and that was not
achievable.** Horizons publishes ephemerides of natural bodies and of real
spacecraft; it cannot propagate a hypothetical satellite under a J2-only force
model, which is exactly the claim phase E needs to check. NASA GMAT can, and
its configuration is recorded beside the fixture —
[the register](milestone-1-decisions.md), decision 4, and M1-68. Horizons keeps
the Sun, Moon and Earth positions and the time scales, which is what M1-06 and
M1-08 use it for.

## Phase F — Orbit track

The algorithm already exists, written and tested, in
`coding-guidelines-example/src/orbit/OrbitPath.*`. This phase moves it into
`src/orbit/` and draws it.

- Sample the orbit; render camera-relative through the phase A line renderer
- Apsis markers

**The acceptance criterion inverted on 2026-09-07, and this is the amendment
most worth reading.** It used to be *"the ellipse is drawn and stays put while
time is accelerated"*, which was correct under two-body physics and is now
exactly backwards. With J2 from phase E the orbit **must not** stay put: an
ISS-like orbit's ascending node regresses about 5 degrees per day. Someone
implementing this phase against the old text would see the track drift and go
hunting for the bug.

That turns the phase from a drawing task into a **visual validation of J2**,
which is a better deal than the one it replaces — the track becomes the first
place a perturbation error is visible to the naked eye rather than only to a
test.

**Done when:** the track is drawn, it precesses, and the precession rate
matches the analytic secular value

```
dOmega/dt = -1.5 * n * J2 * (Re/p)^2 * cos(i)
```

to within a stated tolerance. A track that stays put is now a failing test.

## Phase G — Orbit MFD

- Bitmap font and text rendering
- An MFD panel drawn as a screen-space quad
- Live readout: apoapsis, periapsis, period, eccentricity, inclination
- **A clock**, added 2026-09-07: there is a real epoch now, so the MFD shows UTC
  and mission elapsed time rather than a bare seconds count

The MFDs are where Orbiter's actual gameplay lives, so this deserves more care
than its size suggests.

**The elements are osculating**, which is new and worth saying explicitly: with
J2 running they drift continuously, and the readout should show that rather
than look like instrument noise somebody will try to smooth away. An MFD whose
inclination never moves is now reporting a bug.

**Done when:** the numbers are readable while flying, they agree with the
tests, and they visibly change over an orbit in the way the perturbation model
predicts.

---

## Decisions taken

Decisions taken since this plan was agreed sit above it and are recorded as
architecture decision records rather than here:
[`../adr/0006`](../adr/0006-simulation-not-sandbox.md) (a simulation, not a
sandbox — multi-body physics, real time and frames, a radiometric renderer) and
[`../adr/0007`](../adr/0007-render-quality-is-a-struct.md) (`RenderQuality`, and
the rule that a quality setting never reaches the simulation state) first, then
the records written for the rulings of 2026-09-08.
[The index](../adr/README.md) is the list. Everything in this section predates
all of them and is unaffected by them.

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
inspected, and the earlier reading of it was wrong: the root is MIT, and so is
the tile-format reference under `Utils/tileedit/qt/src/`, while two directories
are LGPL and must not be copied.

**The table, and the boundary it draws, is
[`../ORBITER-REFERENCE.md`](../ORBITER-REFERENCE.md)**, which is where
`CLAUDE.md` routes anyone about to read Orbiter's source. It is not repeated
here, because a licence boundary with two homes is a licence boundary that will
one day disagree with itself.

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

**All of these were answered on 2026-09-08.** They are kept, struck through,
because a question that simply vanishes reads as a question nobody asked.
[The register](milestone-1-decisions.md) holds each answer with the
alternatives it was weighed against.

- ~~**Tile format on disk:** KTX2 with BC7, or plain DDS?~~ **Settled: KTX2**
  (decision 20), carrying the mip chain and the Vulkan format enum directly.
  Orbiter's DXT1 tiles are repacked into it block for block by a converter, so
  no pixel is transcoded (decision 21).
- ~~**Elevation:** probably milestone 2.~~ **Settled 2026-09-07: it moves into
  phase C.** ADR 0006 makes relief part of the realism bar, and displacement is
  much cheaper built into the subdivision than bolted on afterwards. The
  dataset is ETOPO 2022 (decision 23).
- ~~**Night lights and specular water:** both available in the Blue Marble set…
  Likely worth folding into phase B.~~ **Settled: night lights fold into phase
  B** (M1-35). The **specular water mask stays deferred** — the source has not
  been located (decision 25).

Of the questions this plan inherits from [`realism.md`](realism.md) section 4,
the two that decide how phase E is built are settled: **Cowell then Encke**,
RK4 then a high-order tableau, and a **Cartesian** propagated state (decisions
7 to 12). Still open, and neither blocks this milestone: DE440 or VSOP87 for
the ephemeris — milestone 1 uses neither, only the analytic Sun — and whether
`Vec3` acquires a unit and a frame.

# Milestone 1 — the task queue

Status: **planned 2026-09-08, not started.** 84 tasks, in one order.

This is the working document for [milestone 1](milestone-1-earth.md): Earth, an
orbit track, and an Orbit MFD. The milestone plan says what the phases are and
why; this says what to do next, and [the decision
register](milestone-1-decisions.md) says under which rulings. Each task has its
own document under [`tasks/`](tasks/).

---

## How the queue works

**One order, and it is the order below.** A task starts only when the task
before it has been verified. That is deliberate: this project's verification is
local and by hand, and a queue where two things are half-finished is a queue
where a failure cannot be attributed.

**Each task lists its true prerequisites too.** Those are usually fewer than
"everything before it". They exist so that if the order ever has to change, what
actually depends on what is written down rather than inferred.

**One task is one commit.** Half a day to a day of work. If a task turns out to
be two commits, it was two tasks, and the queue should say so before the second
one starts.

### The standing rules, which every task inherits

These are not repeated in the 84 documents. They apply to all of them.

1. **The test is written first and seen to fail**, for the right reason
   ([`../VERIFICATION.md`](../VERIFICATION.md) rule 1). The failure message goes
   in the commit message, which is how a reader knows the test was real.
2. **`check` passes in both trees** before the task is done:
   ```
   cmake --build build/relwithdebinfo --target check
   cmake --build build/debug --target check
   ```
   That is the definition of done, not "the tests pass" — it builds everything,
   runs `clang-format --dry-run --Werror`, runs `clang-tidy` over every
   translation unit with headers included, and runs `ctest`.
3. **Zero warnings, zero clang-tidy findings.** No new entry in `.clang-tidy`
   and no `NOLINT` without an explicit go-ahead from the owner.
4. **Anything numerical states its budget as a number**, in the document, in the
   test and in the commit message, and validates it against something this
   project did not produce.
5. **Anything drawn writes a probe frame**, and the owner looks at it before the
   next task starts. A golden image enters the repository only after that.
6. **New failure modes are reported by name**, not by a bool or a silent
   fallback, and get a test that asks for them by name.
7. **A decision that spans files becomes an ADR** in `docs/adr/`, in the same
   commit as the code that first depends on it.
8. Commits end with the `Co-Authored-By` line and **no session URL**
   ([`../../CLAUDE.md`](../../CLAUDE.md), Attribution).

### The phase gates

Every phase ends with a gate task. A gate runs what the day-to-day loop does not:

```
cmake --preset asan            && cmake --build build/asan && ctest --test-dir build/asan
wsl -d Ubuntu -- cmake --preset linux-sanitize && ...      # clang + ASan + UBSan
wsl -d Ubuntu -- cmake --preset linux-gcc      && ...      # the second compiler
wsl -d Ubuntu -- ./build/linux-fuzz/fuzz_<target> -max_total_time=240
```

plus a coverage review and an update to `PROJECT_STATE.md`. **Nothing proceeds
past a red gate.** Two compilers disagreeing is the signal a second toolchain
exists to produce.

The list grows as the milestone does: `linux-tsan` arrives with the first thread
in M1-33 and runs at every gate after it, and the fuzz targets go from one to
five — `fuzz_orbit`, `fuzz_ktx2`, `fuzz_ztree`, `fuzz_elevation` and
`fuzz_integrator`. Each gate task names exactly what it must run.

---

## The order, and why it is this one

The phase order **A → B → D → C → E → F → G** is from the milestone plan and is
unchanged. Inside the phases, three things drive the ordering:

- **The headless work comes first.** Phase A opens with the time system and the
  astronomy, which need no GPU and no window, so they are testable the moment
  they are written and they cost nothing to get right now — which is exactly why
  `realism.md` ranks the time system second.
- **The verification machinery comes before the thing it verifies.** The probe
  mode, the golden comparison and the numeric readback (M1-16 to M1-18) are
  built before anything that has to be judged by eye. Otherwise the first ten
  render tasks would be verified by looking, and then re-verified later.
- **Each parser arrives with its fuzz target**, in the same task, because
  `VERIFICATION.md` rule 13 names file parsers as the highest-value fuzzing
  target still missing, and this milestone adds three of them.

---

## Preliminaries

| # | Task | Prerequisites | Ends with |
|---|---|---|---|
| [01](tasks/m1-01-catch2.md) | Move both suites to Catch2 | — | The same 3,632 assertions, one harness |
| [02](tasks/m1-02-record-the-decisions.md) | Record the decisions as ADRs 0008–0013 | — | Six ADRs and `THIRD_PARTY.md` |

## Phase A — render foundations, the time system, the quality path

| # | Task | Prerequisites | Ends with |
|---|---|---|---|
| [03](tasks/m1-03-timepoint.md) | `TimePoint` and the time scales | 01 | A time that knows which scale it is in |
| [04](tasks/m1-04-leap-seconds.md) | UTC, TAI and TT | 03 | Leap seconds, and an expiry that reports |
| [05](tasks/m1-05-tdb-and-ut1.md) | TDB and UT1 | 04 | All five scales, with ΔUT1 = 0 recorded as model error |
| [06](tasks/m1-06-horizons-fixtures.md) | The Horizons fixture format | 01, 03 | External truth, committed and readable |
| [07](tasks/m1-07-earth-orientation.md) | Precession and the Earth rotation angle | 05, 06 | A body-fixed frame, 0.1″ against reference |
| [08](tasks/m1-08-solar-position.md) | Solar position | 05, 06, 07 | Sun direction and distance, 0.01° against Horizons |
| [09](tasks/m1-09-orbsim-view.md) | The `orbsim_view` library and `Mat4` | 01 | A Vulkan-free render library the tests can link |
| [10](tasks/m1-10-reverse-z-projection.md) | Reverse-Z with an infinite far plane | 09 | ADR 0003 made real, and tested |
| [11](tasks/m1-11-camera.md) | The camera, and the f64 → f32 boundary | 10 | One named narrowing function |
| [12](tasks/m1-12-render-quality.md) | `Count<Derived>` and `RenderQuality` | 09 | An empty quality struct, and the path it travels |
| [13](tasks/m1-13-pipelines.md) | Graphics pipelines and shader modules | 01 | The renderer can create a pipeline |
| [14](tasks/m1-14-hdr-target.md) | The HDR render target | 13 | Shaders stop writing display-ready colour |
| [15](tasks/m1-15-exposure-and-agx.md) | Exposure and the AgX tonemap | 14 | Radiance in, sRGB out, once, at the end |
| [16](tasks/m1-16-probe-mode.md) | Probe mode: deterministic frames | 13, 15 | A frame you can look at, every run |
| [17](tasks/m1-17-golden-images.md) | Golden-image comparison | 16 | An approved frame becomes a test |
| [18](tasks/m1-18-radiometry-probe.md) | Numeric probes, and the radiometry budget | 15, 16 | 0.5 % of an analytic radiance |
| [19](tasks/m1-19-line-renderer.md) | The line renderer | 11, 13 | Lines, camera-relative |
| [20](tasks/m1-20-planetary-grid.md) | The planetary grid, and the jitter budget | 17, 19 | Phase A's stated acceptance: no jitter |
| [21](tasks/m1-21-camera-controls.md) | Camera controls and scripted paths | 11 | You can fly it, and a script can repeat it |
| [22](tasks/m1-22-benchmark-mode.md) | The benchmark mode | 21 | Frame time as a number |
| [23](tasks/m1-23-phase-a-gate.md) | **Phase A gate** | 03–22 | Sanitizers, second compiler, coverage, recorded |

## Phase B — the tile pipeline

| # | Task | Prerequisites | Ends with |
|---|---|---|---|
| [24](tasks/m1-24-tile-identity.md) | `TileId` and the tile scheme | 09 | Orbiter's own indexing, tested |
| [25](tasks/m1-25-tilesource.md) | The `TileSource` seam | 24 | A closed set, and a synthetic source |
| [26](tasks/m1-26-ktx2-reader.md) | The KTX2 reader, and its fuzz target | 01, 25 | Untrusted bytes, refused by name |
| [27](tasks/m1-27-ktx2-writer.md) | The KTX2 writer | 26 | Round trip, and an external validator |
| [28](tasks/m1-28-tilegen-tool.md) | The `tilegen` tool | 27 | Source imagery decoded, deterministically |
| [29](tasks/m1-29-bc7.md) | BC7 encoding, and the PSNR budget | 28 | ≥ 40 dB, measured |
| [30](tasks/m1-30-pyramid.md) | The pyramid | 24, 29 | Levels and tiles, byte-identical twice |
| [31](tasks/m1-31-gpu-textures.md) | GPU textures, samplers, descriptors | 13, 26 | A tile on the GPU |
| [32](tasks/m1-32-one-tile-on-a-sphere.md) | One tile on a sphere | 17, 30, 31 | Phase B's stated acceptance |
| [33](tasks/m1-33-async-loading.md) | Asynchronous tile loading | 30, 31 | A `jthread`, immutable publication |
| [34](tasks/m1-34-tile-cache.md) | The tile cache and its budget | 33 | Mebibytes, spent deliberately |
| [35](tasks/m1-35-night-lights.md) | Night lights | 31, 32 | The night side reads as inhabited |
| [36](tasks/m1-36-tree-reader.md) | The Orbiter `.tree` reader, and its fuzz target | 24, 28 | Somebody else's format, read safely |
| [37](tasks/m1-37-tree-converter.md) | The `.tree` to KTX2 converter | 27, 30, 36 | DXT1 repacked, not transcoded |
| [38](tasks/m1-38-phase-b-gate.md) | **Phase B gate** | 24–37 | Two new fuzz targets, run |

## Phase D — the atmosphere

| # | Task | Prerequisites | Ends with |
|---|---|---|---|
| [39](tasks/m1-39-atmosphere-parameters.md) | Parameters, and the CPU reference model | 01, 09 | Earth's coefficients, cited |
| [40](tasks/m1-40-transmittance-lut.md) | The transmittance LUT | 18, 39 | 1 % of the reference |
| [41](tasks/m1-41-multiple-scattering-lut.md) | The multiple-scattering LUT | 40 | 5 % of the reference |
| [42](tasks/m1-42-sky-view-lut.md) | The sky-view LUT | 41 | A sky, from anywhere |
| [43](tasks/m1-43-aerial-perspective.md) | The aerial-perspective volume | 42 | Distance hazes correctly |
| [44](tasks/m1-44-sun-disc.md) | The Sun as a disc | 08, 15 | 0.53°, and a soft terminator |
| [45](tasks/m1-45-atmosphere-composition.md) | Atmosphere over the planet | 17, 32, 43, 44 | Phase D's stated acceptance |
| [46](tasks/m1-46-atmosphere-quality.md) | The first real `RenderQuality` fields | 12, 45 | The empty struct stops being empty |
| [47](tasks/m1-47-frame-time-after-d.md) | Frame time after phase D | 22, 45 | A number before the risky phase |
| [48](tasks/m1-48-phase-d-gate.md) | **Phase D gate** | 39–47 | Recorded |

## Phase C — the spherical quadtree

| # | Task | Prerequisites | Ends with |
|---|---|---|---|
| [49](tasks/m1-49-wgs84.md) | The WGS-84 ellipsoid | 09 | Flattening, not a sphere |
| [50](tasks/m1-50-quadtree-and-sse.md) | The quadtree and its error metric | 12, 24, 49 | Selection, tested headless |
| [51](tasks/m1-51-tile-meshes-and-skirts.md) | Tile meshes and skirts | 30, 31, 50 | Geometry, and no cracks |
| [52](tasks/m1-52-morphing.md) | Morphing between levels | 51 | Continuity through a switch |
| [53](tasks/m1-53-cache-eviction.md) | Eviction under motion | 34, 50 | A camera path that does not thrash |
| [54](tasks/m1-54-etopo-ingest.md) | ETOPO 2022 ingestion | 28, 30 | Elevation, from a citable source |
| [55](tasks/m1-55-elevation-tiles.md) | Elevation tiles and sampling | 27, 54 | 1 m of the dataset |
| [56](tasks/m1-56-displacement-and-normals.md) | Displacement and normals | 51, 55 | Relief, and shading that agrees |
| [57](tasks/m1-57-orbiter-elev.md) | Orbiter ELEV extraction | 36, 55 | The converter reaches elevation too |
| [58](tasks/m1-58-seams-and-popping.md) | Seams and popping: the frames | 17, 51, 52 | The evidence, looked at |
| [59](tasks/m1-59-terrain-quality.md) | Terrain quality settings | 12, 50, 53 | Threshold, level cap, cache budget |
| [60](tasks/m1-60-the-descent.md) | The descent, 400 km to 10 km | 22, 56, 58 | Phase C's stated acceptance, and frame time |
| [61](tasks/m1-61-phase-c-gate.md) | **Phase C gate** | 49–60 | The riskiest phase, closed out |

## Phase E — the simulation loop

| # | Task | Prerequisites | Ends with |
|---|---|---|---|
| [62](tasks/m1-62-force-model-seam.md) | The force-model seam, and point-mass gravity | 01, 03 | Three seams, one variant each |
| [63](tasks/m1-63-j2.md) | The J2 term | 07, 62 | The perturbation that pays for itself |
| [64](tasks/m1-64-stepper-seam-rk4.md) | The stepper seam, and RK4 | 62 | Order 4, measured |
| [65](tasks/m1-65-high-order-stepper.md) | The high-order stepper | 64 | Order 8, measured, and diffed against RK4 |
| [66](tasks/m1-66-cowell.md) | The Cowell propagator | 63, 65 | 1e-9 of the two-body propagator with J2 off |
| [67](tasks/m1-67-encke.md) | Encke's method | 66 | The reference conic earns its keep |
| [68](tasks/m1-68-gmat-fixture.md) | The GMAT fixture, and the accuracy budget | 06, 67 | < 10 m per orbit, < 1 km per day |
| [69](tasks/m1-69-simulation-clock.md) | The simulation clock | 03, 68 | Fixed step, accumulator, acceleration |
| [70](tasks/m1-70-determinism.md) | Determinism | 69 | Bit-identical, 1× against 10000× |
| [71](tasks/m1-71-interpolation.md) | Render-side interpolation | 11, 69 | A slow frame costs a frame |
| [72](tasks/m1-72-runtime-monitors.md) | Runtime invariant monitors | 70 | Rule 15, in the Debug tree |
| [73](tasks/m1-73-fuzz-the-integrator.md) | Fuzzing the force model and the steppers | 63, 65 | The third fuzz target |
| [74](tasks/m1-74-phase-e-gate.md) | **Phase E gate** | 62–73 | Recorded |

## Phase F — the orbit track

| # | Task | Prerequisites | Ends with |
|---|---|---|---|
| [75](tasks/m1-75-orbitpath.md) | `OrbitPath` moves into `src/orbit/` | 01 | The example's algorithm, in the project |
| [76](tasks/m1-76-draw-the-track.md) | Drawing the track, and apsis markers | 19, 71, 75 | A track, camera-relative |
| [77](tasks/m1-77-precession-budget.md) | The precession budget | 63, 68, 76 | Phase F's inverted criterion: it must move |

## Phase G — the Orbit MFD

| # | Task | Prerequisites | Ends with |
|---|---|---|---|
| [78](tasks/m1-78-font.md) | The font, baked and embedded | 28 | DejaVu in the executable |
| [79](tasks/m1-79-text-renderer.md) | The text renderer | 13, 78 | Glyphs on screen |
| [80](tasks/m1-80-mfd-panel.md) | The MFD panel | 79 | A panel that looks like one |
| [81](tasks/m1-81-readout-formatting.md) | The readout, formatted and tested | 03, 80 | Numbers, tested without a GPU |
| [82](tasks/m1-82-live-mfd.md) | The live MFD, and the clock | 69, 71, 81 | Osculating elements that drift |
| [83](tasks/m1-83-milestone-acceptance.md) | Milestone acceptance | 22, 60, 82 | Every stated criterion, checked |
| [84](tasks/m1-84-milestone-gate.md) | **Milestone gate and the record** | 01–83 | `PROJECT_STATE.md` tells the truth again |

---

## What this queue does not contain

Named so that silence is not mistaken for an oversight. All of these are
deferred deliberately; the reasoning is in
[the decision register](milestone-1-decisions.md) section 6.

Thrust, mass and 6-DOF attitude. Third bodies, drag and solar radiation
pressure. DE440, and any ephemeris beyond the analytic Sun. Nutation and polar
motion. Equinoctial elements. A scenario file, a configuration file and an
adaptive quality controller. The specular water mask. Clouds. A star catalogue.
Orbiter's `Mask`, `Label` and `Cloud` archive layers — only `Surf` and `Elev`
are converted here.

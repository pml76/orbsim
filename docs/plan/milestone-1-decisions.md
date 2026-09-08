# Milestone 1 — the decisions taken before it started

Status: **settled 2026-09-08** by the project owner. Recorded here because the
task queue in [`milestone-1-tasks.md`](milestone-1-tasks.md) is built on these,
and would have to be rebuilt if any of them changed.

Six of them span files and therefore become architecture decision records —
that is task [M1-02](tasks/m1-02-record-the-decisions.md), the second thing done
in this milestone. This file is the register: it holds all of them, including
the ones too small for an ADR, so that no decision lives only in a conversation.

**The rule they were taken under** is working agreement 1 in
[`../../CLAUDE.md`](../../CLAUDE.md): state the finding, propose the fix, and
wait. Every row below was put to the owner as a question with its alternatives
and their costs, and every row is the owner's answer.

---

## 1. Verification

| # | Decision | Answer |
|---|---|---|
| 1 | How renderer work is machine-verified | **Probes plus golden images.** A headless `--probe` mode renders one deterministic frame; numbers are asserted against a reference computed independently on the CPU in the test, and images against a committed golden |
| 2 | Who looks at the frames | **The owner, before the next task starts.** Every probe writes a viewable PNG on every run, pass or fail, plus a diff image on a golden mismatch. A golden image is *an approved frame*: it enters the repository only after the owner has seen it, and updating one is a decision, never a way to make a test pass |
| 3 | Frame and golden geometry | Probes render **1280x720** and write that PNG. The golden is the same frame downsampled to **640x360**, about 150 KB. Two tolerances, both stated: **no pixel further than 4/255**, and **mean absolute error under 0.5/255**. Goldens only where the image is the artefact; LUT, geometry and radiometry probes stay numeric |
| 4 | External truth for the J2 integrator | **NASA GMAT fixtures.** Horizons cannot propagate an Earth satellite under a J2-only force model, so the milestone plan's original wording was not achievable as written. Horizons keeps Sun, Moon and Earth positions, and the time scales |
| 5 | Frame time | **Measured by a benchmark, not asserted in `check`.** A timing threshold on shared hardware fails for reasons unrelated to the change. Numbers are recorded in `PROJECT_STATE.md` at the end of phases D, C and G |
| 6 | Test framework | **Catch2 v3**, and both existing suites move to it in the first task, so there is never more than one harness in the tree |

## 2. Physics

| # | Decision | Answer |
|---|---|---|
| 7 | Integrator | **Cowell first, then Encke.** Fixed-step Cowell with RK4 — so convergence order can be measured against theory — then a high-order tableau, then Encke reusing the two-body propagator as its reference conic. Both propagators are kept and diffed against each other |
| 8 | The seams | **Three, not one.** Force terms, the stepper, and where body positions come from. Adding the Moon later is adding a *term* and an *ephemeris*, not changing the integrator |
| 9 | How the seams are expressed | **Closed sets as `std::variant`.** No inheritance, no heap, no vtable; values stay trivially copyable, so a physics snapshot is a copy. An alternative the compiler has not seen handled is a compile error |
| 10 | Stepper interface | **Stateful, with explicit copyable state** — `reset(state, epoch)` then `advance()`. The only shape that admits a multistep method (Gauss-Jackson) or an adaptive controller later |
| 11 | Force-term traits | **Each term declares whether it depends on time, position or velocity.** A future symplectic or Nyström integrator can then refuse by name rather than integrate something wrong but plausible |
| 12 | Integrated state size | **Six now, steppers generic over a state concept.** Thrust with variable mass, and 6-DOF attitude, join later as channels without rewriting an integrator |
| 13 | Time acceleration | **Fixed step, more steps.** 1x and 10000x are bit-identical. If a machine cannot keep up, the simulation clock lags real time and says so; the model never changes |
| 14 | Time and frames in scope | **Full**: UTC, TAI, TT, TDB and UT1 as distinct types on a two-part Julian date; a leap-second table that reports rather than extrapolates; IAU 2006 precession and the Earth rotation angle; an analytic solar position. Nutation and ΔUT1 deferred **with their errors written down** |
| 15 | Where the astronomy lives | **A new `src/astro/`**, in the same `orbsim_core` target. `src/orbit/` stays about trajectories; `astro/` takes where bodies are and how frames rotate |

## 3. Renderer

| # | Decision | Answer |
|---|---|---|
| 16 | Radiometric chain | **Manual photographic exposure (aperture, shutter, ISO) and the AgX tonemap.** Auto-exposure deferred, and when it lands it must be pinned in probe mode |
| 17 | Where render-side CPU maths lives | **A new Vulkan-free library, `orbsim_view`**: camera, projection, `RenderQuality`, tile identity, screen-space error, and the atmosphere CPU reference. The rule that nothing under `tests/` includes a Vulkan or SDL header stays enforced by the link graph rather than by discipline |
| 18 | Quadtree LOD | **Skirts plus vertex morphing.** Skirts close cracks without neighbour bookkeeping; morphing removes the pop |
| 19 | Count-like units | **An integral `Count<Derived>` beside `Quantity<Derived>`** in `core/Scalar.hpp`. `Texels` and `Mebibytes` are counts; `Pixels` stays on the f64 base, because a screen-space error threshold of 2.5 px is a real quantity. This closes the question ADR 0007 left open |

## 4. Data and tools

| # | Decision | Answer |
|---|---|---|
| 20 | Tile container | **KTX2**, carrying the mip chain and the Vulkan format enum directly |
| 21 | Orbiter `.tree` | **A converter tool, inside milestone 1.** Archive reader with a fuzz target, DXT1 repacked into KTX2 byte for byte, and the ELEV elevation format |
| 22 | Third-party pins | **`stb` and `bc7enc_rdo`**, fetched and pinned exactly as SDL3, VMA, vk-bootstrap and Vulkan-Headers already are |
| 23 | Elevation dataset | **ETOPO 2022, 60 arc-second, ice surface** (NOAA NCEI, public domain) |
| 24 | MFD font | **DejaVu Sans Mono**, committed as its TTF, baked at build time, and the **atlas embedded in the executable**, so there is no runtime font file |
| 25 | Night lights and water | Night lights fold into phase B. The specular water mask stays **deferred**: the source has not been located |
| 26 | Scenarios and configuration | **Hard-coded named scenarios** in milestone 1. No scenario file and no `RenderQuality::fromConfig`; they are the same question and are deferred together |

---

## 5. The error budgets

Stated before the code, per [`../VERIFICATION.md`](../VERIFICATION.md) rule 4.
Each appears again in its own task document, in the test that asserts it, and in
the commit message. Changing one is a decision with a written reason.

Where a budget is split, **code error** is what a test asserts about our own
arithmetic, and **model error** is what we knowingly do not model — recorded so
it is never mistaken for a bug.

| Phase | Claim | Budget | Checked against |
|---|---|---|---|
| A | UTC/TAI/TT round trip | exact to **1e-9 s**, 1970–2035 | The leap-second table and published ΔAT steps |
| A | TDB − TT | within **100 µs** — 3 m of Earth's orbital motion | The reference series |
| A | Precession + ERA, as implemented | **0.1″** (code) | An IERS/ERFA reference value |
| A | Precession + ERA, as modelled | **≤ 40″, about 1.2 km** on the ground: nutation omitted (≤ 25″) and ΔUT1 = 0 (≤ 15″) (model) | Recorded, not asserted |
| A | Solar direction and distance | **0.01°**, **2e-4 AU** — 0.04 % of irradiance | JPL Horizons fixtures |
| A | Radiometric chain | **0.5 %** of the analytic 130 W·m⁻²·sr⁻¹ for a Lambertian patch, albedo 0.3, normal to the Sun at 1 AU | Analytic value, read back from the HDR target before tonemapping. RGBA16F quantisation is 0.05 % |
| A | Camera-relative precision | a fixed world point at Earth radius moves **≤ 0.05 px** at 1920×1080 between consecutive frames | Computed on the CPU with the same maths |
| B | BC7 compression | **≥ 40 dB PSNR** against the source | The uncompressed source image |
| B | Pyramid generation | **byte-identical** for identical input | A second run |
| D | Transmittance, single scattering | **1 %** | A CPU brute-force reference written independently in the test |
| D | Multiple scattering | **5 %** | The same reference |
| C | Rendered radius vs the dataset | **1 m** | A bilinear sample of ETOPO 2022 |
| C | Geometric screen-space error | **≤** the configured threshold | The projected true surface against the tessellated one, on the CPU |
| E | J2 off, against the two-body propagator | **1e-9 relative** over one orbit | `propagate()`, which shares no line of code with the integrator |
| E | Convergence order | within **0.1** of theory — 4 for RK4, 8 for the high-order tableau | A step-halving study |
| E | Position against GMAT, J2 only, 400 km circular, i = 51.6° | **< 10 m after one orbit, < 1 km after 24 h** | GMAT R2026a fixture, settings recorded |
| E | Determinism | **bit-identical** after 24 h of simulated time, between two runs and between 1× and 10000× | Itself — the one place `==` on floats is the correct operator |
| F | Nodal regression | within **1 %** of `-1.5 n J2 (Re/p)^2 cos i` over 5 days | The analytic secular rate. The short-period oscillation in the node is about 0.03° against 25° of drift, so the budget sits an order of magnitude above the noise |
| C, G | Frame time | **16.6 ms** at 1920×1080, High preset, RTX A2000, Earth from 400 km | Measured and recorded, not asserted |

---

## 6. Scope fences for milestone 1

Written down because a fence nobody recorded is a fence somebody walks through.

- The vessel is a **test particle**: no mass, no thrust, no attitude.
- The force model is **Earth point mass and J2 only**. No third bodies, no drag,
  no solar radiation pressure, no J3 or J4.
- The propagated state is **Cartesian**. Equinoctial elements are deferred.
- **No DE440.** The analytic Sun lights the scene; it does not pull on anything.
- **`Vec3` stays unit-free and frame-free.** `PROJECT_STATE.md` section 7.6 stays
  open.
- **`RenderQuality` presets only.** No configuration file, no adaptive
  controller.
- **Night lights** land in phase B; the **specular water mask** is deferred until
  a source is located.
- Every phase ends with a **gate task**: asan, `linux-sanitize`, `linux-gcc`, the
  fuzzers and a coverage review, with the numbers recorded.

---

## 7. External facts, verified 2026-09-08

Checked against the source on the day the plan was written, rather than
recalled. Each task that depends on one of these re-checks it before use.

| Fact | Verified value |
|---|---|
| This project's licence | MIT, Copyright (c) 2026 Peter Lennartz |
| `stb` | Dual: MIT **or** the Unlicense (public domain), the user's choice |
| `bc7enc_rdo` | **Three licences in one repository.** Most sources dual MIT / Unlicense (Richard Geldreich); `bc7e.ispc` Apache-2.0 (Binomial LLC); a bundled LodePNG under a zlib-style permissive licence. **Only the MIT/Unlicense files are compiled**: `bc7enc.cpp/h`, `rgbcx.h/cpp`, and `ert.cpp/h` if rate-distortion optimisation is wanted. `bc7e.ispc` needs Intel's ISPC compiler and is never built |
| Catch2 | **Boost Software License 1.0** |
| DejaVu fonts | Bitstream Vera derived: permissive, bundling inside a larger package allowed, notice required, the fonts may not be sold by themselves, derivatives must be renamed. Latest release **2.37**, asset `dejavu-fonts-ttf-2.37.zip` |
| AgX minimal implementation | **MIT**, confirmed by the author (Benjamin Wrensch, Missing Deadlines) in the licensing discussion on his repository; the constants derive from Troy Sobotka's OCIO configuration |
| ETOPO 2022, 60 arc-second, ice surface | `https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO2022/data/60s/60s_surface_elev_gtif/ETOPO_2022_v1_60s_N90W180_surface.tif` — one global GeoTIFF, 444 MB, dated 2022-10-04. Public domain, a US Government work |
| NASA GMAT | **Apache-2.0**, current release **R2026a**, Windows build available |
| The atmosphere method | Hillaire, S. (2020), *A Scalable and Production Ready Sky and Atmosphere Rendering Technique*, Computer Graphics Forum 39(4), EGSR 2020, DOI 10.1111/cgf.14050 |
| The Orbiter archive format | `Utils/tileedit/qt/src/ZTreeMgr.{h,cpp}` in the reference clone, MIT. A magic-tagged header, a table of contents of quadtree nodes each carrying a file offset, an inflated size and four child indices, then zlib-deflated per-tile blobs. Surface tiles are DDS/DXT1; elevation is Orbiter's own ELEV format (`elv_io.cpp`, also MIT) |

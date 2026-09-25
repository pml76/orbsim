# Third-party code, data and reference material

Kind: reference
Binding: yes — the licence column is a legal constraint, not a preference
Read when: you are adding, updating or removing a dependency, or you need to
know what may be copied from where. Versions of the *toolchain* are in
[`docs/STATUS.md`](docs/STATUS.md); this file covers what the project consumes
rather than what builds it.

**orbsim itself is MIT**, Copyright (c) 2026 Peter Lennartz — see
[`LICENSE`](LICENSE).

Every dependency is fetched and pinned by `CMakeLists.txt` at an exact tag.
Nothing is vendored into this repository: there is no third-party source under
`src/`, and there never should be without an entry here first.

## Contents

- [Pinned and compiled today](#pinned-and-compiled-today)
- [Decided, not yet pinned](#decided-not-yet-pinned)
- [`bc7enc_rdo`: which files are compiled](#bc7enc_rdo-which-files-are-compiled)
- [Vulkan-Utility-Libraries: which parts are compiled](#vulkan-utility-libraries-which-parts-are-compiled)
- [Data and reference material](#data-and-reference-material)
- [What this file is for](#what-this-file-is-for)

---

## Pinned and compiled today

Read from the `FetchContent_Declare` calls in `CMakeLists.txt`. All eight are
declared `SYSTEM`, which is what keeps this project's warning set from firing
on somebody else's headers — the full set produced 643 warnings once, and 639
of them were inside two of these. ERFA, which has no CMake build of its own and
is built by ours, has its include directory marked `SYSTEM` by hand for the
same reason.

**Apache-2.0 arrived with Vulkan-Utility-Libraries on 2026-09-16** and is the
first copyleft-free-but-not-permissive-simple licence here; everything else is
MIT, zlib, BSL-1.0, BSD-3-Clause (ERFA, since 2026-09-19), or Apache-2.0
**OR** MIT at the user's choice. It is
compatible with this project's MIT, and its notice requirement is satisfied by
this file plus the notice that ships with any binary distribution.

| Dependency | Pinned at | Licence | What it is for |
|---|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | `release-3.4.16` | zlib | Window, input, event loop. Built as a shared library |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | `v1.3.302` | Apache-2.0 **OR** MIT | The Vulkan API headers. Pinned rather than taken from the installed SDK — [`docs/adr/0004`](docs/adr/0004-pinned-vulkan-headers.md) says why |
| [vk-bootstrap](https://github.com/charles-lunarg/vk-bootstrap) | `v1.3.302` | MIT | Instance, physical-device selection and device creation |
| [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | `v3.2.1` | MIT | GPU memory allocation |
| [Catch2](https://github.com/catchorg/Catch2) | `v3.16.0` | Boost Software License 1.0 | The test framework — [`docs/adr/0013`](docs/adr/0013-catch2-is-the-test-framework.md) |
| [Vulkan-Utility-Libraries](https://github.com/KhronosGroup/Vulkan-Utility-Libraries) | `v1.3.302` | **Apache-2.0** | Khronos' utility headers. `string_VkResult` today; `vk_format_utils.h` is wanted by the tile work in phase B. **Nothing of it is compiled** — see below. Pinned at the tag matching Vulkan-Headers, because a later utility header can name enumerators an older `vulkan_core.h` does not have |
| [mp-units](https://github.com/mpusz/mp-units) | `v2.5.0` | MIT | The dimension system under `core/Units.hpp` — [`docs/adr/0019`](docs/adr/0019-vectors-carry-their-unit.md). Header-only, and **no dependency of its own in our configuration**: `MP_UNITS_API_CONTRACTS` is set to `NONE`, where its default of `GSL-LITE` would require one. mp-units does not fetch it either — both GSL options are `find_package(... REQUIRED)`, so switching would mean pinning gsl-lite or Microsoft.GSL here too. Settled 2026-09-18 with the measurement in [`docs/PROJECT_STATE.md`](docs/PROJECT_STATE.md) section 7 question 11. The first dependency `orbsim_core` links, so the headless core is no longer dependency-free at the source level even though it still needs no library at link time -- true until ERFA, below |
| [ERFA](https://github.com/liberfa/erfa) | `v2.0.1` | **BSD-3-Clause**, after a preamble on its SOFA heritage; copyright the NumFOCUS Foundation. Uniform across the repository — no other licence file, no vendored code, the leap-second table in `dat.c` under the same terms — verified 2026-09-10. A binary distribution must carry its notice. **Not SOFA**, whose own licence (SPDX `SOFA`) adds conditions on derived work | The astronomy — [`docs/adr/0016`](docs/adr/0016-the-astronomy-is-erfa.md): TDB − TT now ([M1-05](docs/plan/tasks/m1-05-tdb-and-ut1.md)), the celestial-to-terrestrial rotation and the Sun from M1-07 and M1-08. **Pinned 2026-09-19**, when v2.0.1 (2023-10-13) was re-verified as the latest release; master was twelve commits ahead and unreleased. Built **unedited** by `CMakeLists.txt` as the static C library `orbsim_erfa` from all 249 library files — the build stops if it finds any other number — with the version macros read from its own `meson.build`, and linked **privately** to `orbsim_core`, so its headers are seen by `src/astro/*.cpp` and nothing else. Both of its validation programs, `t_erfa_c` (1,494 checks) and `t_erfa_c_extra`, are CTest tests. **The first dependency the core needs at link time**: it is compiled from source like everything else, so the headless core still needs nothing installed |

The Vulkan **SDK** is not a dependency in this sense: it supplies the loader and
`glslc`, and its version is a property of the machine
([`docs/STATUS.md`](docs/STATUS.md)), not of the build.

## Decided, not yet pinned

Settled on 2026-09-08 in
[the milestone 1 register](docs/plan/milestone-1-decisions.md), and each one
arrives with the task that first needs it. *(ERFA, settled on 2026-09-11, was
here until M1-05 pinned it on 2026-09-19.)* **The exact repository URL and tag
go in the table above when the pin lands** — they are deliberately not written
here from memory.

| Dependency | Licence | Arrives with | What it is for |
|---|---|---|---|
| `stb` (Sean Barrett) | Dual: MIT **or** the Unlicense, at the user's choice | [M1-16](docs/plan/tasks/m1-16-probe-mode.md) | `stb_image_write` for probe PNGs, then `stb_image` for JPEG decoding (M1-28), `stb_truetype` for the MFD font (M1-78), and `stbi_zlib_decode_buffer` for Orbiter archives (M1-36) — one dependency doing four jobs, which is why it beats adding zlib separately |
| `bc7enc_rdo` (Richard Geldreich) | **Three licences in one repository** — see below | [M1-29](docs/plan/tasks/m1-29-bc7.md) | BC7 encoding for the tile pyramid |
| DejaVu Sans Mono, release **2.37** (`dejavu-fonts-ttf-2.37.zip`) | Bitstream Vera derived: permissive; bundling inside a larger package is allowed; the notice must be carried; the fonts may not be sold by themselves; a derivative must be renamed | [M1-78](docs/plan/tasks/m1-78-font.md) | The MFD font. Committed as its TTF, baked at build time, and the **atlas embedded in the executable**, so there is no runtime font file |
| AgX minimal implementation (Benjamin Wrensch, *Missing Deadlines*), from [the IOLITE blog post](https://iolite-engine.com/blog_posts/minimal_agx_implementation) | **MIT**, "Copyright (c) 2024 Missing Deadlines (Benjamin Wrensch)". *(Corrected 2026-09-25, register decision 183. This said "confirmed by the author in the licensing discussion on his repository", and that could not be found: the IOLITE repository now holds one placeholder commit of 2025-09-17, and its licence issue, #23, is about the engine's public API rather than this post. The MIT notice is confirmed by independent copies of the code -- NVIDIA's nvpro_core ships it as `PACKAGE-LICENSES/minimal-agx-LICENSE.md` -- and **the owner confirmed it on the blog page itself** on 2026-09-25, which this machine's network could not reach.)* The constants derive from Troy Sobotka's OCIO configuration, below | **In use since [M1-15](docs/plan/tasks/m1-15-exposure-and-agx.md)**: `shaders/tonemap.frag` and `src/view/Tonemap.hpp`, which is the same code written for the CPU. Not a fetched dependency: the code is copied, adapted with three stated guards (register decision 174), and **the full MIT notice is carried in both files**, as the licence requires. The tonemap — [`docs/adr/0014`](docs/adr/0014-radiometric-chain.md) |

## `bc7enc_rdo`: which files are compiled

This repository carries three licences, and **the answer is not visible from
the pin** — which is the reason this file exists in the shape it does. Verified
against the repository on 2026-09-08.

| File | Licence | Compiled? |
|---|---|---|
| `bc7enc.cpp`, `bc7enc.h` | MIT / Unlicense (Richard Geldreich) | **Yes** |
| `rgbcx.h`, `rgbcx.cpp` | MIT / Unlicense (Richard Geldreich) | **Yes** |
| `ert.cpp`, `ert.h` | MIT / Unlicense (Richard Geldreich) | **Only if** rate-distortion optimisation is wanted |
| `bc7e.ispc` | **Apache-2.0** (Binomial LLC) | **No.** It needs Intel's ISPC compiler and is never built |
| bundled LodePNG | zlib-style permissive | **No** |

So what this project compiles from `bc7enc_rdo` is MIT/Unlicense only. Anyone
adding a file from that repository to the build must check this table first and
extend it.

## Vulkan-Utility-Libraries: which parts are compiled

**None of it.** This is the second repository here where the pin does not
answer the question, and the answer is worth a section for the opposite reason
to `bc7enc_rdo`'s: not because the licences differ across files, but because
most of what it builds is never linked.

| Part | What it is | Compiled? |
|---|---|---|
| `include/vulkan/**` via `Vulkan::UtilityHeaders` | Header-only: `vk_enum_string_helper.h`, `vk_format_utils.h`, `vk_struct_helper.hpp`, `vk_dispatch_table.h` | **Yes** — header-only, so nothing is built; this is the only part linked |
| `VulkanSafeStruct` | Deep-copy wrappers for every Vulkan struct, generated. **76,081 lines of C++** across six files | **No.** For validation layers; this project has no use for it |
| `VulkanLayerSettings` | Layer settings file parsing | **No.** For writing Vulkan layers, which we do not |
| `tests/`, `scripts/` | Its own test suite, and `update_deps.py` | **No.** `BUILD_TESTS` and `UPDATE_DEPS` both default off |

`EXCLUDE_FROM_ALL` on the `FetchContent_Declare` is what makes that true rather
than merely intended: without it, those two static libraries are in `all` and
every `cmake --build` in every tree compiles them. That flag is why
`cmake_minimum_required` is 3.28 rather than 3.25 — the reasoning is in
`CMakeLists.txt` beside the line.

If `VulkanSafeStruct` is ever wanted, linking `Vulkan::SafeStruct` is all it
takes; `EXCLUDE_FROM_ALL` keeps targets out of `all`, not out of reach.

## Data and reference material

Not compiled, and not distributed by this repository — but consumed, and each
carries terms worth recording.

| Source | Terms | Used for |
|---|---|---|
| [Blue Marble Next Generation](https://visibleearth.nasa.gov/collection/1484/blue-marble) (NASA) | Public domain | Surface imagery and night lights. Downloaded, not committed — [`data/textures/README.md`](data/textures/README.md) has the URLs |
| ETOPO 2022, 60 arc-second, ice surface (NOAA NCEI) | Public domain, a US Government work | Elevation, from [M1-54](docs/plan/tasks/m1-54-etopo-ingest.md). One 444 MB GeoTIFF; the URL is in `data/textures/README.md` |
| [NASA GMAT](https://gmat.gsfc.nasa.gov/) R2026a | Apache-2.0 | Produces the J2 reference trajectory that [M1-68](docs/plan/tasks/m1-68-gmat-fixture.md) asserts against. A tool that generates a committed fixture; no GMAT code enters this project |
| [Skyfield](https://rhodesmill.org/skyfield/) 1.55 (Brandon Rhodes) | MIT. Its `tdb_minus_tt` is USNO Circular 179 eq. 2.6 (Kaplan 2005), written independently of SOFA; its dependencies are certifi, jplephem, numpy and sgp4, none of them ERFA -- checked 2026-09-19 | Generates the committed TDB − TT reference, [`data/skyfield/tdb-minus-tt.txt`](data/skyfield/tdb-minus-tt.txt), that [M1-05](docs/plan/tasks/m1-05-tdb-and-ut1.md) asserts its 20 µs budget against (register decisions 53-55), and since 2026-09-20 the **Earth-orientation reference**, [`data/skyfield/earth-orientation.txt`](data/skyfield/earth-orientation.txt), that [M1-07](docs/plan/tasks/m1-07-earth-orientation.md) asserts its 0.1 mas budget against (decisions 75, 76 and 82). For the rotation its route is independent -- sidereal time on the equinox-based matrix, where ERFA's is CIO-based -- but its IAU 2000A nutation is a port of NOVAS's, which shares IERS modules with SOFA's, and decision 75 records that as the limit of this reference. **Committed**, unlike Horizons output: the terms are MIT, and a table of numbers computed from a published formula is not the software. No Skyfield code enters this project; [`data/skyfield/README.md`](data/skyfield/README.md) has the recipe. NOVAS 3.1 evaluates the same equation, and was not chosen: it would add no independence, and its terms could not be confirmed that day -- the user's guide states no licence and asks users to e-mail USNO, the USNO source URL returned HTTP 500, and the Astrophysics Source Code Library failed TLS verification |
| JPL Horizons (NASA/JPL-Caltech) | **No licence is stated anywhere, and the FAQ asks for permission** — read 2026-09-17. **Settled the same day: this project queries Horizons and does not redistribute its output.** Fixtures are generated into gitignored `data/horizons/`; the recipe and the checksums are committed instead | Sun, Moon and Earth positions and the time scales ([M1-06](docs/plan/tasks/m1-06-horizons-fixtures.md), [M1-08](docs/plan/tasks/m1-08-solar-position.md)) |
| Orbiter (Martin Schweiger) | MIT at the root; **LGPL** in two directories; the standalone `orbiter-tileedit` repository is GPL v3 | The tile format specification and the archive format. **The licence boundary is not uniform and has its own document:** [`docs/ORBITER-REFERENCE.md`](docs/ORBITER-REFERENCE.md) |
| [Troy Sobotka's AgX](https://github.com/sobotka/AgX) (`config.ocio` and the `AgX_Default_Contrast` curve) | **No licence stated** -- GitHub reports none for the repository, read 2026-09-25 | **Read, not taken.** The inset matrix and the log range in `src/view/Tonemap.hpp` were checked against its `config.ocio`, and the minimal implementation's curve against its curve table, once, on 2026-09-25 (register decision 174); `scripts/tonemap-reference.py` quotes the matrix to check the GLSL layout against it. The numbers are the minimal implementation's, under its MIT notice; nothing of this repository is committed here |
| [TSIS-1 Hybrid Solar Reference Spectrum](https://lasp.colorado.edu/lisird/data/tsis1_hsrs) (Coddington et al. 2021, *Geophys. Res. Lett.* 48, e2020GL091709; LASP LISIRD) | **Not found on the pages checked on 2026-09-25**; the reference asks to be cited, and is | **Downloaded, not committed**, by [`scripts/solar-efficacy.py`](scripts/solar-efficacy.py), which prints its SHA-256. The one number derived from it -- sunlight's luminous efficacy, 98.9225 lm/W (register decision 175) -- is committed in `src/view/Exposure.hpp` with this citation |
| [CIE 1924 photopic V(lambda)](http://www.cvrl.org/) (the CIE's function, as tabulated by the Colour & Vision Research Laboratory) | **Not found on the pages checked on 2026-09-25**; the function itself is an international standard | **Downloaded, not committed**, by the same script, for the same one number. The SI definition of the candela is stated with it |
| MSIS / NRLMSISE-00 atmosphere models (US Naval Research Laboratory) | **Two different problems, and neither route is permissive** — NRLMSIS 2.x is academic and non-commercial with delivery obligations; the 2001 C port grants no permission at all. Read at the source 2026-09-24 | **Nothing, and that is the point of the row.** The thermospheric density model wanted for atmospheric drag. No third-party code is to be taken: the route decided is to implement from the published model. The detail is below, because the absence of a usable licence is the finding |

**The Horizons row is the one open item in this file**, and reading the terms
on 2026-09-17 did not close it. *(Still one, after the MSIS row joined the table
on 2026-09-24 — that row reports terms this project cannot use and then takes
nothing under them, which is a closed question with an unwelcome answer.
Horizons is open because its output **is** consumed, under terms nobody has
stated.)* What was found, with the sources, because the absence of a statement
is itself the finding:

| Source | What it says |
|---|---|
| [SSD FAQ](https://ssd.jpl.nasa.gov/faq.html), "I'd like to publish information from your site" | *"The short answer is yes"* — permission is wanted. *"At the very least, we'd be interested in knowing what information you intend to use and how you intend to use it. Ideally, we'd prefer you link from your site directly to the information on our site."* |
| [JPL image use policy](https://www.jpl.nasa.gov/jpl-image-use-policy/) | Broad reuse *"for any purpose without prior permission"* with the credit *"Courtesy NASA/JPL-Caltech"* — but it is about **images and video**, and says nothing about data |
| [Horizons API documentation](https://ssd-api.jpl.nasa.gov/doc/horizons.html) | **Nothing.** No licence, citation or redistribution statement at all |
| [data.gov entry](https://catalog.data.gov/dataset/horizons) | Access level `public`. **No licence field** |

Two facts that bear on it and are easy to get wrong. **JPL is not a US
Government agency** — it is a federally funded research and development centre
operated by Caltech — so 17 U.S.C. § 105, which puts US Government works
outside copyright, does not apply automatically the way it does to NASA-authored
material. And **a state vector is close to pure fact**: numbers computed from a
published physical model at a stated epoch, which in US law (*Feist*, 1991) is
the kind of thing copyright does not reach. That is an argument, not a
permission, and it is recorded here as an argument.

**Settled 2026-09-17: query, do not redistribute.** The output is generated
into `data/horizons/`, which is gitignored, and
[`data/horizons/README.md`](data/horizons/README.md) carries the exact API query
for each fixture with the reason for every parameter. The fixtures'
**SHA-256 sums are committed** — a hash of a file is not that file, so it
redistributes nothing, and it lets a regenerated fixture be verified as the one
the error budgets were measured against.

What it costs, stated plainly: the suites that check against external truth
cannot run on a fresh clone until somebody runs the recipe, so they report
themselves **skipped** rather than passing quietly. That is a real weakening of
[`docs/VERIFICATION.md`](docs/VERIFICATION.md) rule 3 and is recorded there as
well as here.

### The atmosphere density model — read 2026-09-24, before it was needed

Written down early on purpose. Nothing in this project uses a density model
yet, and [`docs/plan/realism.md`](docs/plan/realism.md) names NRLMSISE-00 in
three places, once as "the standard", so the obvious first move whenever
atmospheric drag is built is to fetch an implementation. **Two of the three
obvious routes are closed, and both of them look open from a distance.** This
section exists so that is discovered here rather than after the code is
written.

**NRLMSIS 2.0 and 2.1 — academic and non-commercial.** The current model is
distributed under the *MSIS® (NRL-SOF-014-1) Software Open Source Academic
Research License Agreement*, whose name is the trap: "Open Source" appears in
the title and the terms are not. Section 2, verbatim:

> *"In accordance with federal law, authorization is given to use, reproduce,
> and modify the Software solely for research, academic, and non-profit
> purposes and only in accordance with the terms and conditions in this
> Agreement. **Any commercial use is prohibited.** No other rights or
> permissions are provided."*

Three further conditions matter as much as that one. Section 4(a) forbids
selling or licensing for a fee not only the software and its derivatives but
**any data products generated by it**, without NRL's written consent — so
shipping a table computed from it is covered too. Section 4(b) makes every
modification, **including a translation to another programming language**,
deliverable back to NRL Code 7630, requires it be published as open source, and
grants the US Government a licence to it. And MSIS® is a registered trademark,
so the name cannot be used freely for a modified model either. The licence text
is `nrlmsis2.1_license.txt` inside the distribution at
`https://map.nrl.navy.mil/map/pub/nrl/NRLMSIS/NRLMSIS2.1/`.

**The 2001 NRLMSISE-00 C port — no permission at all.** The widely used C
version by Dominik Brodowski, of the model by Picone, Hedin and Drob, has a
section headed "1. LEGAL INFORMATION" in its `DOCUMENTATION` file. It reads, in
full:

> *"This package is distributed in the hope that it will be useful, but WITHOUT
> ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
> FOR A PARTICULAR PURPOSE. Please inform the maintainer of the C release
> (Dominik Brodowski - mail@brodo.de) of any patches and bug-fixes you
> implement for NRLMSISE-00 so that this C package can be updated with these
> improvements."*

That is a **warranty disclaimer and a request, and it is not a licence.** It
grants no right to copy, modify or redistribute. The phrasing is borrowed from
the GPL's disclaimer clause, which is why it reads like permission at a glance;
the sentence that would have granted any is absent.

**Two traps worth naming, because each one points the wrong way.**

*Orbiter ships that C file.* It is at `Src/Celbody/Vsop87/Earth/Atmosphere/`
in the reference clone, and Orbiter's root is MIT — which does **not**
relicense a third-party file inside it. That is this file's own first principle,
stated under "What this file is for" below and applied already to `bc7enc_rdo`
and to Orbiter itself: the unit recorded here is the *file*, wherever a
repository is not uniform.

*SPDX lists a licence with the identifier `NRL`, and it is a different licence.*
That one is BSD-style — *"NRL grants permission for redistribution and use in
source and binary forms, with or without modification"* — and it permits
commercial use. It has nothing to do with the MSIS agreement above. Anyone
checking "the NRL licence" against an SPDX list will get a permissive answer to
a question they did not ask.

**What is used instead: the published model.** The owner ruled on 2026-09-24
that NRLMSISE-00 is to be **implemented from the literature**, so no
third-party code arrives and no licence is needed. Two consequences are
recorded with the ruling rather than left to be found:

- **The independence limit.** Every NRLMSISE-00 implementation descends from
  the same NRL coefficient tables, so a comparison against another one checks
  *our use of the model* — argument order, units, the species mixture — and not
  the model. This is the same limit the Skyfield row above records for its
  nutation, and it gets the same treatment: stated in the fixture's own README
  before any budget is claimed against it.
- **A published alternative exists if the implementation proves too large.**
  Orbiter's own default is not MSIS at all but Jacchia-71 with Gill's
  bi-polynomial fit — Jacchia, *Revised Static Models of the Thermosphere and
  Exosphere with Empirical Temperature Profiles*, SAO Special Report 332
  (1971), and Gill, *Smooth Bi-Polynomial Interpolation of Jacchia 1971
  Atmospheric Densities*, DLR-GSOC IB 96-1 (1996). Both are papers, so that
  route owes nobody a licence either, and Orbiter's technical reference
  publishes a measured comparison of the two models.

The ruling and its milestone are in
[`docs/plan/milestones.md`](docs/plan/milestones.md), which is **`Status:
proposed`** — so the ruling is the owner's and dated, while the register entry
and the milestone that carries it are not settled yet. **The licence finding
above is independent of all of that**, which is why it is recorded here now:
it is a fact about somebody else's terms, and it does not change if the plan
does.

## What this file is for

Two failure modes, both cheap to prevent and expensive to discover late.

The first is a licence that is **not uniform across a repository**, which is
`bc7enc_rdo` and Orbiter. A pin records a commit; it does not record that one
file in that commit is Apache-2.0 and the rest are MIT, and nobody re-reads a
`LICENSE` file at the top of a dependency they have already added. So the unit
recorded here is the *file*, wherever the repository is not uniform.

The second is a dependency that arrives without anyone deciding to add it.
Every row above is either a `FetchContent_Declare` that exists, or a decision
with a task number attached. A dependency that is in neither state is one to
stop and ask about — working agreement 1 in [`CLAUDE.md`](CLAUDE.md).

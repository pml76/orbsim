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

Read from the `FetchContent_Declare` calls in `CMakeLists.txt`. All six are
declared `SYSTEM`, which is what keeps this project's warning set from firing
on somebody else's headers — the full set produced 643 warnings once, and 639
of them were inside two of these.

**Apache-2.0 arrived with Vulkan-Utility-Libraries on 2026-09-16** and is the
first copyleft-free-but-not-permissive-simple licence here; everything else is
MIT, zlib, BSL-1.0, or Apache-2.0 **OR** MIT at the user's choice. It is
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

The Vulkan **SDK** is not a dependency in this sense: it supplies the loader and
`glslc`, and its version is a property of the machine
([`docs/STATUS.md`](docs/STATUS.md)), not of the build.

## Decided, not yet pinned

Settled on 2026-09-08 in
[the milestone 1 register](docs/plan/milestone-1-decisions.md) -- ERFA on
2026-09-11 -- and each one arrives with the task that first needs it. **The exact repository URL and tag
go in the table above when the pin lands** — they are deliberately not written
here from memory.

| Dependency | Licence | Arrives with | What it is for |
|---|---|---|---|
| `stb` (Sean Barrett) | Dual: MIT **or** the Unlicense, at the user's choice | [M1-16](docs/plan/tasks/m1-16-probe-mode.md) | `stb_image_write` for probe PNGs, then `stb_image` for JPEG decoding (M1-28), `stb_truetype` for the MFD font (M1-78), and `stbi_zlib_decode_buffer` for Orbiter archives (M1-36) — one dependency doing four jobs, which is why it beats adding zlib separately |
| `bc7enc_rdo` (Richard Geldreich) | **Three licences in one repository** — see below | [M1-29](docs/plan/tasks/m1-29-bc7.md) | BC7 encoding for the tile pyramid |
| DejaVu Sans Mono, release **2.37** (`dejavu-fonts-ttf-2.37.zip`) | Bitstream Vera derived: permissive; bundling inside a larger package is allowed; the notice must be carried; the fonts may not be sold by themselves; a derivative must be renamed | [M1-78](docs/plan/tasks/m1-78-font.md) | The MFD font. Committed as its TTF, baked at build time, and the **atlas embedded in the executable**, so there is no runtime font file |
| AgX minimal implementation (Benjamin Wrensch, *Missing Deadlines*) | MIT, confirmed by the author in the licensing discussion on his repository. The constants derive from Troy Sobotka's OCIO configuration | [M1-15](docs/plan/tasks/m1-15-exposure-and-agx.md) | The tonemap — [`docs/adr/0014`](docs/adr/0014-radiometric-chain.md). Attribution goes in the shader header as well as here |
| ERFA (`liberfa/erfa`), decided 2026-09-11 | **BSD-3-Clause**, after a preamble on its SOFA heritage; copyright the NumFOCUS Foundation. Uniform across the repository — no other licence file, no vendored code, the leap-second table in `dat.c` under the same terms — verified 2026-09-10. A binary distribution must carry its notice. **Not SOFA**, whose own licence (SPDX `SOFA`) adds conditions on derived work | [M1-05](docs/plan/tasks/m1-05-tdb-and-ut1.md) | TDB − TT, the celestial-to-terrestrial rotation and the Sun — [`docs/adr/0016`](docs/adr/0016-the-astronomy-is-erfa.md). Built unedited as its own C library, all 249 library files, with its validation program `t_erfa_c` as a CTest test; called from `src/astro/` only. The latest release on 2026-09-10 was v2.0.1 (2023-10-13); re-verify when the pin lands |

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
| JPL Horizons (NASA/JPL-Caltech) | **Not yet verified** — see the note below | Sun, Moon and Earth positions and the time scales, as committed fixtures ([M1-06](docs/plan/tasks/m1-06-horizons-fixtures.md), [M1-08](docs/plan/tasks/m1-08-solar-position.md)) |
| Orbiter (Martin Schweiger) | MIT at the root; **LGPL** in two directories; the standalone `orbiter-tileedit` repository is GPL v3 | The tile format specification and the archive format. **The licence boundary is not uniform and has its own document:** [`docs/ORBITER-REFERENCE.md`](docs/ORBITER-REFERENCE.md) |

**The Horizons row is the one open item in this file.** The register verified a
licence for every other entry on 2026-09-08 and does not record one for
Horizons. The fixtures are small numeric extracts and each one records the
exact query that produced it, but the terms themselves should be read and this
row completed before M1-06 commits a fixture.

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

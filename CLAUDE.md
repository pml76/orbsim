# orbsim — instructions for working in this repository

**Write code the way [`coding-guidelines-example/`](coding-guidelines-example/)
writes it.** That directory is not a demo; it is the reference implementation of
the house style. When you are unsure how something should look here — an
interface, an error path, a test, a comment — open the example and copy the
shape.

- The rules and the reasoning: [`CODING_GUIDELINES.md`](CODING_GUIDELINES.md)
- The rules applied to real code: [`coding-guidelines-example/`](coding-guidelines-example/)
  and its [README coverage map](coding-guidelines-example/README.md)

The example builds clean under the full warning set as errors, passes 47 checks,
and produces zero clang-tidy findings at `WarningsAsErrors: '*'`. That is the
bar for new code in `src/` too.

## The project

A space flight simulator in the spirit of Orbiter: real orbital mechanics,
6-DOF vessels, MFD-style instrumentation. C++23, clang, Vulkan 1.3, SDL3.

```
src/core/     maths, units, contracts   — no dependencies
src/orbit/    orbital mechanics         — depends on core only
src/render/   Vulkan renderer           — depends on core; never the reverse
src/app/      window and main loop
tests/        CTest suites
```

**The dependency direction is one-way and load-bearing.** `orbsim_core` builds
and runs headless: no Vulkan, no SDL. Never push a renderer concept downward
into the physics — push the dependency the other way instead.

## Build and test

```
cmake --preset relwithdebinfo          # use CLion's bundled cmake 4.3.1
cmake --build build/relwithdebinfo
ctest --test-dir build/relwithdebinfo --output-on-failure
```

`orbsim.exe --validate` enables the Vulkan validation layers from a release
build. The example is a separate project:

```
cmake -S coding-guidelines-example -B coding-guidelines-example/build -G Ninja \
      -DCMAKE_CXX_COMPILER=clang++
```

## Current work

[Milestone 1](docs/plan/milestone-1-earth.md): Earth, orbit track, Orbit MFD.
Phases run **A → B → D → C → E → F → G** — atmosphere deliberately before the
quadtree, because it is what makes the image read as Earth and it gives a
correct reference while debugging tile seams.

## Reference source, and a licence boundary

Orbiter's source is worth reading and is **not** uniformly licensed. The
reference clone lives at `C:\Reference\orbiter`, outside this repository.

| Path in the clone | Licence | Use |
|---|---|---|
| repository root | MIT | Read and borrow, with attribution |
| `Utils/tileedit/qt/src/` | MIT | The clearest tile-format reference. Usable |
| `Utils/tileedit/qt/extern/fastdxt/` | **LGPL** | Vendored DXT codec. Do not copy — Vulkan does BC natively |
| `OVP/D3D9Client/` | **LGPL** | Where TileManager2 lives, but Direct3D and LGPL |

The standalone `mschweiger/orbiter-tileedit` repo on GitHub is GPL v3 — the same
code under a different licence. An MIT copy exists in the monorepo, so do not
clone the GPL one; there is nothing to gain and a licence to lose.

**The format specification is `Doc/Orbiter Developer Manual/PLANETS.tex`**,
section `sssec:tile_file_layout`: levels, latitude bands, longitude indices,
`TileFormat = 2`. Read it before any source.

Planetary imagery lives in `data/textures/`, gitignored, with
[a README](data/textures/README.md) saying where to obtain it.

## Non-negotiables

These are the ones that get violated most often. The rest are in
`CODING_GUIDELINES.md`.

1. **Units live in the type system.** `Radians`, `Metres`, `Seconds`,
   `GravParam` — never a bare `f64` across an interface. Two adjacent
   same-typed parameters that can be transposed are a defect, not a style
   preference; `bugprone-easily-swappable-parameters` is enabled deliberately.
2. **No boolean parameters.** If a call site needs `/*hostVisible=*/`, the
   parameter wanted to be an `enum class`.
3. **`std::expected` for failures a caller can cause; assertions for what only
   a bug can cause.** One strategy per layer. Never a `bool` plus an
   out-parameter, and never two-phase `init()` — use a factory.
4. **Every loop is bounded and reports non-convergence.** A Newton iteration
   that runs out of steps must say so, not return its last guess.
5. **Rule of Zero.** No hand-written destructors. If you are adding one, wrap
   the resource instead.
6. **`[[nodiscard]]` on anything whose return value is the point**, and `const`
   on everything that can be.
7. **`constexpr` where possible, with a `static_assert` proving it.**
8. **`f64` in the simulation; `f32` only at the GPU boundary**, in one named
   function, subtracting before narrowing. Never touch the floating-point flags.
9. **Comment the *why*.** Every non-obvious constant says where its value came
   from.
10. **Zero warnings, zero clang-tidy findings.** Suppressions are allowed and
    must carry a written reason.

## Before you finish

- [ ] Builds clean — zero warnings, not "only the usual ones".
- [ ] `ctest` is green.
- [ ] New logic has a test, ideally one that checks it against something
      independent rather than against itself.
- [ ] `clang-tidy -p <build>` reports nothing new.
- [ ] `clang-format` leaves the tree unchanged.
- [ ] No new boolean parameters, raw owning pointers, or hand-written
      destructors.
- [ ] The renderer still does not leak into the physics.

## Conventions worth knowing

- Types `PascalCase`, functions and variables `camelCase`, constants
  `kPascalCase`, private data `trailingUnderscore_`. Never a leading underscore.
- Headers are `.hpp` and must be self-contained; a `.cpp` includes its own
  header first.
- Single-line guard clauses (`if (!path) return;`) are used deliberately;
  `readability-braces-around-statements` is off to match.
- A formatting pass gets its own commit, doing nothing else.
- Decisions that span files go in `docs/adr/` as short records: what was
  decided, what was considered, why.

## Attribution

Commits end with exactly this, and nothing else:

```
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
```

**Do not add a `Claude-Session:` URL.** This repository is public, and the user
asked for that line to be dropped. Co-authorship is wanted; the session link is
not.

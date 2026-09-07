# orbsim — instructions for working in this repository

**Write code the way [`coding-guidelines-example/`](coding-guidelines-example/)
writes it.** That directory is not a demo; it is the reference implementation of
the house style. When you are unsure how something should look here — an
interface, an error path, a test, a comment — open the example and copy the
shape.

- The rules and the reasoning: [`CODING_GUIDELINES.md`](CODING_GUIDELINES.md)
- The rules applied to real code: [`coding-guidelines-example/`](coding-guidelines-example/)
  and its [README coverage map](coding-guidelines-example/README.md)
- **How to know it is right: [`docs/VERIFICATION.md`](docs/VERIFICATION.md).**
  Adopted 2026-09-06 and **binding on every change**, the same way
  `CODING_GUIDELINES.md` is. Its Part 4 says which of its rules a machine
  checks and which need a person; read it before writing a test, not after.

The example builds clean under the full warning set as errors, passes 47 checks,
and produces zero clang-tidy findings at `WarningsAsErrors: '*'`. That is the
bar for new code in `src/` too.

## The project

A space flight simulator in the spirit of Orbiter: real orbital mechanics,
6-DOF vessels, MFD-style instrumentation. C++23, clang, Vulkan 1.3, SDL3.

**It is a simulation, not a sandbox** ([`docs/adr/0006`](docs/adr/0006-simulation-not-sandbox.md),
decided 2026-09-06). Realism is the acceptance criterion for the physics and
the image alike: multi-body gravity with perturbations, a real epoch with real
time scales, real reference frames, 6-DOF attitude, and a radiometric renderer.
A single point mass is ruled out explicitly. **Every accuracy claim carries a
stated error budget validated against data this project did not produce** --
"realistic" is not a test result. The gap list and the order to close it in is
[`docs/plan/realism.md`](docs/plan/realism.md).

**It must also be fluent, and only the visuals scale.** A stuttering view from
orbit does not read as real however correct the scattering is, so frame time is
part of the image. But physics fidelity is not a quality knob: dropping J2 to
gain frames yields a different simulation, not a faster one. Physics cost is
answered by decoupling -- fixed timestep, own clock, render-side interpolation.
Visual cost is answered by quality settings. The rule that keeps both true:
**a quality setting must never reach the simulation state.** The same scenario
at the lowest and highest settings puts the vessel in the same place, bit for
bit; the quality controller may read the frame clock and the physics may not.
The mechanism is a `RenderQuality` struct of per-feature settings that lives in
`src/render/`, so the rule is enforced by the link graph -- physics code that
reaches for a quality setting does not compile. See
[`docs/adr/0007`](docs/adr/0007-render-quality-is-a-struct.md) and
[`docs/plan/realism.md`](docs/plan/realism.md) section 6.

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

## Build, test, and the definition of done

```
cmake --preset relwithdebinfo && cmake --preset debug    # CLion's bundled cmake 4.3.1
cmake --build build/relwithdebinfo --target check
cmake --build build/debug --target check
```

**Nothing is done until `check` passes in both trees.** It builds everything,
runs `clang-format --dry-run --Werror` over every source and header, runs
`clang-tidy` over every translation unit with headers included, and runs
`ctest` -- including a two-second run of the application under the Vulkan
validation layers (test `orbsim_smoke`, label `gpu`) that fails on any
validation error. Both trees, because assertions are only live in Debug.
Smaller targets exist for the loop: `lint`, `format-check`, `format`. Presets:
`asan` (AddressSanitizer with assertions live -- the release C runtime, not
Debug, because ASan and the MSVC debug heap cannot coexist; run it before a
milestone lands), and `linux-sanitize` and `linux-gcc` for anyone who has a
Linux box to hand. `docs/adr/0005` is the record of why it is set up this way.

**This project does not use CI. Do not add it, and do not spend time on it.**
Verification is local and it is `check`. That is a deliberate deviation from
the guidelines' Toolbox rule two, and its cost -- no UndefinedBehaviorSanitizer
and no second compiler, since neither works on Windows -- is written down in
`docs/adr/0005`.

Formatting also happens without being asked: `.claude/settings.json` runs
`clang-format` on every C++ file Claude Code edits, and the checked-in
pre-commit hook refuses an unformatted commit once enabled per clone:

```
git config core.hooksPath scripts/git-hooks
```

`orbsim.exe --validate --seconds 3` runs the app under validation for three
seconds. Exit codes: 1 failure, 2 usage, 3 validation errors reported. The
example is a separate project with the same bar:

```
cmake -S coding-guidelines-example -B coding-guidelines-example/build -G Ninja \
      -DCMAKE_CXX_COMPILER=clang++
```

## Current work

[Milestone 1](docs/plan/milestone-1-earth.md): Earth, orbit track, Orbit MFD.
Phases run **A → B → D → C → E → F → G** — atmosphere deliberately before the
quadtree, because it is what makes the image read as Earth and it gives a
correct reference while debugging tile seams.

**The plan was amended in place on 2026-09-07** for ADR 0006 and 0007, so read
it rather than this list; [`docs/plan/realism.md`](docs/plan/realism.md) section
5 has the reasoning. The four changes, in case a stale copy is in front of you:

- **Phase A also builds the linear HDR pipeline, the `RenderQuality` plumbing,
  and the time system.** The time system is there rather than in phase E
  because B, D and C all come first, and it is cheapest at zero call sites.
- **Phase C also does elevation**, rather than deferring it to milestone 2.
- **Phase E is the integrator, not a `propagate()` loop**, and its acceptance
  criterion gains an error budget against JPL Horizons.
- **Phase F's criterion inverted.** The orbit track must *precess* at the J2
  rate, not stay put. A track that stays put is now a failing test, and the
  old wording would send someone hunting a bug that is the physics working.

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
11. **Every `VkResult` is checked**, through `vkCheck`, and every function
    that can fail says so in its return type. A dropped result is how a lost
    device becomes a hang three frames later.
12. **Constants carry their units in their name or their type, and a comment
    says where the number came from.** The propagator failed at 1 AU because a
    tolerance was an absolute number in square-root metres and a threshold was
    in reciprocal metres. Ask "in what?" of every bare number.

## How to test physics

The full argument is [`docs/VERIFICATION.md`](docs/VERIFICATION.md), which is
binding. This is the part that applies to every change, so it is here too.

**Write the test first, and watch it fail before you make it pass.** A
numerical test that passes the moment it is written is usually a tolerance
loose enough to accept anything. Read the failure message and check that it
would let somebody else diagnose the problem.

Every new function in `src/orbit/` gets, in `tests/`:

- a check against something that does not come from the code: an analytic
  value, a constant of motion (energy, angular momentum), or the other
  propagator;
- a case at every scale the simulator flies -- the Moon, Earth, Jupiter, the
  Sun as central bodies -- because a suite that only flew Earth orbits passed
  732 checks while `propagate()` failed at 1 AU;
- a case at every *singularity*, which is the other axis: `e = 0`, `e` either
  side of 1, `i = 0`, `i = pi`, retrograde, `dt = 0`. A random sweep visits
  these with probability zero;
- its failure paths, by name (`NotFinite`, not "did not converge");
- for anything numerical, a seeded random sweep over the parameter space,
  with the seed written down so a failure can be reproduced.

**Anything claiming accuracy states its error budget first**, as a number, and
validates it against something external -- JPL Horizons vectors, a published
worked example, the other implementation. A constant of motion proves the code
is self-consistent, not that it is right: a wrong `mu` conserves energy
perfectly. The budget goes in the test and in the commit message.

**Ask "in what?" of every bare number.** Two bugs in this codebase were found
by that one question: a tolerance compared against a quantity in sqrt(metres),
and a threshold compared against one in 1/metres. Both were invisible at Earth
scale.

`tests/TestHarness.hpp` has the checks, `tests/OrbitTestSupport.hpp` the
bodies and fixtures, `tests/test_orbit_scales.cpp` the shape to copy.

## Before you finish

- [ ] `cmake --build build/relwithdebinfo --target check` passes, and so does
      the same in `build/debug`. The target, not "the tests pass".
- [ ] New logic has a test that checks it against something independent (see
      above), and a new failure has a test that asks for it by name.
- [ ] That test was written first and was seen to fail for the right reason.
- [ ] Anything claiming accuracy states an error budget as a number, and the
      test asserts it against external reference data.
- [ ] A fixed bug leaves behind a regression test named after the symptom.
- [ ] No new boolean parameters, raw owning pointers, bare `f64` across an
      interface, or hand-written destructors outside `VulkanHandle.hpp` and
      `SdlHandle.hpp`.
- [ ] The renderer still does not leak into the physics:
      `-DORBSIM_BUILD_APP=OFF` still builds the core and its tests.
- [ ] A decision that spans files has a record in `docs/adr/`.

## Conventions worth knowing

- Types `PascalCase`, functions and variables `camelCase`, constants
  `kPascalCase`, private data `trailingUnderscore_`. Never a leading underscore.
- Headers are `.hpp` and must be self-contained; a `.cpp` includes its own
  header first.
- Single-line guard clauses (`if (!path) return;`) are used deliberately;
  `readability-braces-around-statements` is off to match.
- A formatting pass gets its own commit, doing nothing else.
- Decisions that span files go in `docs/adr/` as short records: what was
  decided, what was considered, why. Seven exist; read them before changing
  anything they cover.
- Line endings are LF everywhere (`.gitattributes`, `.editorconfig`, and
  `LineEnding: LF` in `.clang-format`). An older Windows checkout may still
  hold CRLF in files nobody has touched; leave those alone rather than
  producing a diff in which every line changed.
- The physics test suites are under `tests/` and link only `orbsim_core`.
  Nothing in `tests/` may include a Vulkan or SDL header.

## Attribution

Commits end with one `Co-Authored-By` line naming the Claude model that wrote
them, and nothing else. At the time of writing:

```
Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
```

**Do not add a `Claude-Session:` URL.** This repository is public, and the user
asked for that line to be dropped. Co-authorship is wanted; the session link is
not.

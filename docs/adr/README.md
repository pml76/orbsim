# Architecture decision records

Kind: reference
Binding: yes — each record binds whatever it covers
Read when: you are about to change something one of these decides. This index
is the list; the records are the reasoning.

An accepted ADR is **immutable**. New information may be appended to one — a
measured consequence, a correction — but a decision that changes gets a new
record that supersedes the old, and both link to each other. That is why the
counts and dates in an old record are left alone even when they no longer
describe today: they are what was true when the decision was taken.

Format: what was decided, what was considered, why. Short — three paragraphs is
usually enough.

| # | Decision | Status | Date |
|---|---|---|---|
| [0001](0001-units-in-the-type-system.md) | Physical quantities are types, not doubles | accepted | 2026-09-05 |
| [0002](0002-error-handling-strategy.md) | `std::expected` for expected failures, assertions for impossible ones | accepted | 2026-09-05 |
| [0003](0003-reverse-z-depth.md) | Reverse-Z depth with an infinite far plane | accepted | 2026-09-05 |
| [0004](0004-pinned-vulkan-headers.md) | The Vulkan headers are pinned; the SDK supplies only the loader and glslc | accepted | 2026-09-05 |
| [0005](0005-correctness-is-enforced-by-tools.md) | Correctness is enforced by tools, not by remembering | accepted | 2026-09-05 |
| [0006](0006-simulation-not-sandbox.md) | orbsim is a simulation, not a sandbox | accepted | 2026-09-06 |
| [0007](0007-render-quality-is-a-struct.md) | Render quality is a struct of per-feature settings, and never reaches the simulation | accepted | 2026-09-07 |
| [0008](0008-renderer-verification.md) | Renderer verification is probes, golden frames, and a person looking at them | accepted | 2026-09-08 |
| [0009](0009-time-is-a-type-with-a-scale.md) | Time is a type with a scale, and the astronomy lives in `src/astro/` | accepted; partly superseded by 0016 | 2026-09-08 |
| [0010](0010-tiles-are-ktx2.md) | Tiles are KTX2, and Orbiter's `.tree` is converted rather than streamed from | accepted | 2026-09-08 |
| [0011](0011-the-integrator-has-three-seams.md) | The integrator has three seams, and they are closed sets | accepted | 2026-09-08 |
| [0012](0012-orbsim-view.md) | `orbsim_view` holds the render-side maths that Vulkan never touches | accepted | 2026-09-08 |
| [0013](0013-catch2-is-the-test-framework.md) | Catch2 is the test framework | accepted | 2026-09-08 |
| [0014](0014-radiometric-chain.md) | The radiometric chain is manual photographic exposure and the AgX tonemap | accepted | 2026-09-08 |
| [0015](0015-skirts-and-morphing.md) | Quadtree LOD is skirts plus vertex morphing | accepted | 2026-09-08 |
| [0016](0016-the-astronomy-is-erfa.md) | The astronomy is ERFA's, called through typed wrappers | accepted | 2026-09-11 |
| [0017](0017-every-warning-is-an-error.md) | Every warning the compiler has, as an error; a library's interface is answered at the site | accepted | 2026-09-11 |

**0008 to 0015 record the decisions taken on 2026-09-08**, before milestone 1
started. All twenty-six of those decisions are in
[`../plan/milestone-1-decisions.md`](../plan/milestone-1-decisions.md) — the
register, which holds the ones too small or too local to become a record of
their own, and which maps every decision to the record that carries it.

Three earlier records gained dated amendments in the same pass: **0001** (an
integral `Count` beside `Quantity`), **0005** (the phase gates are where the
sanitizers and the second compiler run) and **0007** (both of the questions it
left open, answered — including that `RenderQuality` lives in `orbsim_view`
rather than `src/render/`).

**0009 gained one on 2026-09-10**, with M1-03: the day begins at midnight, and
the time within it is integer picoseconds rather than an f64 fraction, which
was measured to drift 83 ns where the task's budget was 1 ns.

**0016 partly supersedes 0009, on 2026-09-11**: ERFA computes the astronomy,
nutation is modelled, and the pairing of equinox-based precession with the
Earth rotation angle -- measured at 0.342° out -- is replaced by ERFA's
CIO-based rotation.

**0017 extends 0005, the same day**: clang builds with `-Weverything` and gcc
with every warning it lists, as errors, less a few exceptions each written
down with its reason; a warning raised by a library's interface is switched
off at that site alone. It also refines one phrase of 0011: determinism is
asserted with `bitIdentical()`, because the value types no longer have a
floating-point `==`.

The worked example keeps its own two records under
[`../../coding-guidelines-example/docs/adr/`](../../coding-guidelines-example/docs/adr/).
They are superseded by 0001 and 0002 here where the two disagree.

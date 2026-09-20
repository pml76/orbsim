# ADR 0021: A transform carries the frames it maps between, and the transpose is a dual map

Status: **accepted** (2026-09-20), implemented the same day in
[`src/view/Frame.hpp`](../../src/view/Frame.hpp) and
[`src/view/Mat4.hpp`](../../src/view/Mat4.hpp), with M1-09.

**Supersedes the "Frames" clause of
[`0020`](0020-transforms-carry-their-units.md)**, which left them out and said
the signature admitted them later. It does, and this is that. Everything else
in 0020 stands: the four unit parameters, the derived block references, and
the reason a homogeneous 4×4 has no single unit.

Decision 98 of [the milestone 1 register](../plan/milestone-1-decisions.md).

## What is being decided

0020 typed the *units* of the projective chain and stopped there. That caught
`view * projection`, a projection reaching `transformPoint`, and a clip point
projected twice — but not `model * view` in the wrong order, because world and
view are both metre-to-metre affine and therefore the same type.

## Decision

**`Mat4<kFrom, kTo, kInXyz, kInW, kOutXyz, kOutW>`.** The frames are
non-type template parameters; composition unifies the middle frame as well as
the middle references.

**`FrameTag`, not a bare enum**, because the transpose needs duals:

```cpp
enum class Frame : unsigned char { World, View, Clip };
struct FrameTag { Frame frame{}; bool dual{}; };
```

**The transpose is typed as the dual map it is.** If M maps a to b then
transpose(M) maps b\* to a\*, so the frames swap *and* dualise and every
reference inverts, a covector on a space measured in R being measured in 1/R:

```cpp
TransposeOf<...> = Mat4<dualOf(kTo), dualOf(kFrom),
                        one/kOutXyz, one/kOutW, one/kInXyz, one/kInW>
```

That is the owner's formulation and it is better than the one this project
first wrote. Forcing the result back into a matrix between the same two spaces
made `transpose(AB) == transpose(B)transpose(A)` typecheck only where the
operands' units happened to line up — which excludes every chain containing a
projection. As a dual map it composes for all of them, and because dualising
twice is the identity and 1/(1/R) is R, `transpose(transpose(M))` is M's own
type.

**Points carry frames too**, through a view-local `FramedVec3<kFrame, kR>`.
`core/Math.hpp`'s `Vec3` is untouched: framing it would mean framing
`Position`, which the whole physics uses.

**The factories are same-frame, and a frame change is declared once.** A
rotation and a translation are each *within* a frame; neither changes what
space you are in. So `identityMatrix<F>()`, `translationOf<F>(t)` and
`rotationOf<F>(q)` are all F-to-F, and the one place a frame change is stated
is `retargetFrame<kTo>(m)` — unchecked by construction, named so that
`grep retargetFrame` is the audit, and deliberately *not* routed through
`fromColumnMajor`, which is for building a matrix from data.

**`Frame` lives in `src/view/`, not in `core/`.** `src/astro/` already speaks
frames — `earthFixedFromInertial` is one — so there is a real case for a
shared vocabulary. It is not taken, because moving it down starts the question
[`0019`](0019-vectors-carry-their-unit.md) deliberately left open.

## What the measurements said

**Runtime cost: none.** Measured before the design was chosen, on the
*stricter* of the two candidates — a wrapper struct, rather than template
parameters that cannot add storage at all. Compose two transforms and move a
point through the result, `noinline`, diffed instruction for instruction:

| | plain | frame-aware |
|---|---|---|
| clang 23.1.0 `-O2` | 35 instructions | **identical, 35** |
| `sizeof` | 128 bytes | 128 bytes |
| mentions of the wrapper in the `-O2` assembly | — | **none** |

At `-O0` the two differ in which symbol they call; that affects only the Debug
tree's test run, never a shipped build, and `asan` is `-O2`.

**Feasibility, on all three front ends**, because this leans on two things
that are exactly where compilers differ — and this project has been bitten by
MSVC twice:

| | clang 23.1.0 | gcc-14 | MSVC 19.51 |
|---|---|---|---|
| `FrameTag` as a non-type template parameter (structural type) | ✓ | ✓ | ✓ |
| `one / (one / metre)` is *the same* reference as `metre` | ✓ | ✓ | ✓ |
| `transpose(transpose(M))` has M's exact type, projection included | ✓ | ✓ | ✓ |
| the transpose law composes in general | ✓ | ✓ | ✓ |

**What it now refuses**, each assertion inverted and watched failing before
being believed: `view * projection`; two transforms whose frames do not meet;
a point already in view space handed to a world-to-view matrix; a clip point
projected a second time; a world point skipping the view transform; a
projection asked for a rigid inverse; a transform of velocities meeting one of
positions.

## What we considered

**Matrices framed, points not.** Cheaper, and it still catches composition
order — but `transformPoint(worldToView, aPointAlreadyInView)` keeps
compiling, which is half the bug this exists to stop. Rejected by the owner in
favour of framing the projective and affine points as well.

**Factories taking both frames**, `translationOf<World, View>(t)`. No escape
hatch at all, but it lets the type system state a falsehood: a translation
does not change frames, and `translationOf<World, Clip>` would compile and
mean nothing. Building `view = R · T` would still need a convention for which
half "carries" the frame change, which no compiler can check.

**No `retargetFrame`, using `fromColumnMajor` for the relabelling.** One fewer
function, but it round-trips a typed value through a bare
`std::array<f64, 16>` — the thing the type exists to stop being passed around
— and it cannot be grepped apart from legitimate construction from data. This
project's first recommendation was that one, and it was wrong for the reason
above.

## What this record does not decide

- **The core stays frame-free.** `Vec3`, `Quat` and `Position` carry units and
  no frame, and milestone 1's scope fence keeps it that way. When a second
  layer wants this vocabulary it moves down, and `src/astro/`'s rotations join
  it — the rule `render/VulkanHandle.hpp` states for its own macro.
- **No `Frame::Ndc`.** One was planned and then not added: building it showed
  that clip and normalised device coordinates are already distinguished by the
  *unit*, clip's xyz and w both being metres, so the quotient is
  dimensionless. The divide therefore leaves the frame alone.
- **Which frames exist beyond three.** `EarthFixed` arrives with the planetary
  grid and a tile frame with the quadtree, each with the task that needs it.

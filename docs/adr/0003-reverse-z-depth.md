# ADR 0003: Reverse-Z depth with an infinite far plane

Status: accepted (2026-09-05; the code has done this since the first commit)

## Decision

The depth buffer is `VK_FORMAT_D32_SFLOAT`, cleared to 0.0, and every
pipeline compares with `GREATER`. Projection matrices map the near plane to
depth 1.0 and infinity to depth 0.0; there is no far plane. The clear value
and format are constants in `render/VulkanContext.cpp` and
`render/VulkanContext.hpp`, and phase A of milestone 1 builds the projection
to match.

## What we considered

**Conventional depth, near at 0.0 and far at 1.0, compared with `LESS`.** The
default in every tutorial. A floating-point depth value has most of its
precision near zero, and a conventional projection puts most of its *range*
near one: the two are mismatched, and the result is z-fighting that starts a
few hundred metres out and is hopeless at the distances a spaceflight
simulator draws.

**Logarithmic depth written from the fragment shader.** Solves the precision
problem, but writing depth in the fragment shader disables early depth
testing on every GPU, and the cost is paid on every pixel of every frame.

**Depth partitioning: several passes with different near and far planes.**
Works, and is what older simulators did. It multiplies the draw calls, and
seams between partitions are a permanent source of bugs.

## Why

A spaceflight simulator draws a cockpit panel a metre away and a planet a
hundred million kilometres away in the same frame, and both must resolve
against each other and against a vessel in between. Reverse-Z with a
32-bit float depth buffer spreads precision almost evenly in *logarithmic*
distance across that whole range, which is what the scene needs, at no
per-pixel cost and with early depth testing intact. Dropping the far plane
costs nothing further and removes a parameter that would otherwise need
tuning per scene.

The mechanism is easy to break by accident -- a pipeline created with
`VK_COMPARE_OP_LESS` out of habit draws nothing -- which is why the convention
is written down here, named in the constants, and will be checked by the phase
A grid test: a grid drawn at planetary radius must show no jitter and no
fighting as the camera moves.

## Update, 2026-09-21: the matrix exists, and half the check is done

The decision above is unchanged. What follows is what has since been built,
recorded here rather than edited in, because an accepted record says what was
decided and when rather than tracking the code.

[M1-10](../plan/tasks/m1-10-reverse-z-projection.md) added
[`src/view/Projection.hpp`](../../src/view/Projection.hpp), which is the
projection this record describes: the near plane at depth 1.0, infinity at
0.0, no far plane, and the Vulkan y flip in one entry of one matrix. The
conventions are stated in that header.

**The arithmetic half is now asserted**, on the processor, in
`tests/test_projection.cpp`. In particular the precision claim this record
rests on is measured rather than asserted by hand: two points one metre apart
at 1000 km from the camera land 9 to 14 ulp apart in a 32-bit float depth
buffer, across near planes from 1 cm to 100 m, while a conventional 0-to-1
projection with a far plane at 1e9 m puts them on **bit-identical** values --
not merely closer, but indistinguishable. That comparison is in the suite, so
the argument in "Why" above is a test result rather than a claim.

**The grid test named above is still outstanding**, and it is a different
thing: it runs on the GPU, it belongs to phase A's drawing tasks, and what it
checks is that the depth *state* of a real pipeline matches this convention.
Nothing in M1-10 touches a pipeline.

**This binds every projection this project adds**, not only the perspective
one. A flat projection for the instrument panels was considered for M1-10 and
deliberately deferred (register decision 105); whenever it arrives it must map
its near plane to 1, because the depth buffer is cleared to 0.0 and every
pipeline compares with `GREATER`. A projection built the conventional way
round draws nothing, and the symptom points nowhere near the cause.
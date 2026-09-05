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
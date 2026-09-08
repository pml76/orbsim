# M1-21 — Camera controls and scripted paths

Phase: A | Status: not started
Prerequisites: M1-11

## Purpose

Every visual acceptance criterion in this milestone — the limb from 400 km, the
descent to 10 km, the MFD readable while flying — needs somebody to be able to
move the camera. And every benchmark needs that motion to be **repeatable**, or
the frame times are not comparable between two runs.

So one task delivers both: an interactive controller, and a scripted path that
replays exactly.

## What to implement

**`src/view/CameraController.hpp`** — a pure state machine in the Vulkan-free
library, which is what makes it testable:

- Orbit around a focus point, dolly toward and away from it, and pan the focus.
  Input arrives as small value types (`DragDelta`, `WheelTicks`) rather than raw
  SDL events, so the controller has no idea SDL exists.
- Distance is clamped to a range expressed in `Metres`, and the orbit latitude
  is clamped just short of the poles — the gimbal-adjacent case
  `VERIFICATION.md` rule 5 asks for, which here is a real degeneracy rather than
  a hypothetical one.
- Speed scales with altitude: at 400 km a drag should move the view usefully,
  and at 10 km the same drag must not fling the camera into space. The scaling
  law is one line and it says why.

**`src/view/CameraPath.hpp`** — keyframes of (time, position, orientation) with
interpolation: linear in position, `slerp` in orientation, and a lookup by time
that is a pure function of the path and the time. No clock, no frame counter, no
state. That is what makes a benchmark reproducible and a probe deterministic.

**`src/app/`** wires SDL events to the controller, and nothing else.

## Out of scope

Following a vessel — that needs a simulation, which is phase E. Collision with
the surface. Input remapping or a configuration file. Smoothing or inertia,
which would put a time constant between the input and the camera and make a
scripted replay depend on frame rate.

## Tests

`tests/test_camera_controller.cpp`, headless.

- A synthetic sequence of drags and wheel ticks produces the pose computed by
  hand in the test, to 1e-12.
- **Clamping**: dragging past the pole stops at the clamp rather than flipping
  the up vector — asserted at both poles, and at exactly the clamp value.
- **Altitude scaling**: the same drag at 400 km and at 10 km moves the camera by
  the ratio the law states.
- **Determinism**: replaying the same input sequence twice gives bit-identical
  poses.
- **Path interpolation**: continuous at every keyframe (no jump at a boundary),
  exact at the keyframes themselves, `slerp` stays unit to 1e-15, and evaluating
  the path at the same time twice is bit-identical.
- **Singularity**: two keyframes with opposite orientations take the short way
  round, and one with identical orientations does not produce NaN from a
  zero-length slerp.

## Verification

The standing rules. Then, by hand: run the app, fly it, and confirm the controls
are usable — that part is a judgement and is recorded as one.

## Done when

- [ ] `check` green in both trees.
- [ ] The controller compiles without any SDL or Vulkan header in sight.
- [ ] A scripted path replays bit-identically.
- [ ] The owner has flown it and said the controls are workable.

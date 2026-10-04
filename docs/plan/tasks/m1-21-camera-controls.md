# M1-21 — Camera controls and scripted paths

Phase: A | Status: **done, 2026-10-04**
Prerequisites: M1-11
Decided by: [ADR 0012](../../adr/0012-orbsim-view.md), [ADR 0026](../../adr/0026-input-is-commands.md)

**Amended 2026-10-04, when the task ran.** Thirteen questions were put before
the code and nine more once it was worked to the level of code; every
recommendation was taken (register decisions 340-361). Four of them change
what this document said, and each is corrected in place below with its
decision beside it: what the camera circles (340), what its speed scales with
(341), what the window draws (344), and how input reaches the controller --
commands rather than drags, at the owner's request to prepare for a keyboard
and a gamepad (353, ADR 0026). **Read this rather than the original if the two
disagree; the original is in the git history.**

## Purpose

Every visual acceptance criterion in this milestone — the limb from 400 km, the
descent to 10 km, the MFD readable while flying — needs somebody to be able to
move the camera. And every benchmark needs that motion to be **repeatable**, or
the frame times are not comparable between two runs.

So one task delivers both: an interactive controller, and a scripted path that
replays exactly.

## What was implemented

**`src/view/CameraController.hpp`** — a state machine in the Vulkan-free
library, which is what makes it testable:

- **It circles a point on the planet's surface, with up along that point's own
  vertical** (decision 340) -- the original said "a focus point" and left the
  rest open, and a focus at the Earth's centre always looks straight down,
  where from 400 km the limb is 70 degrees off the axis and outside a 45-degree
  view. Tilt shows the horizon, panning slides the point over the planet, and
  moving in descends toward it.
- **It reads commands, not input** (decisions 353 and 354,
  [ADR 0026](../../adr/0026-input-is-commands.md)): turn by two angles, pan by
  a fraction of the screen, move in or out by steps. The mouse's mapping is
  **`src/view/CameraInput.hpp`**, which turns the `DragDelta` and `WheelTicks`
  this document named into them, so a keyboard or a gamepad is a new mapping
  and nothing in the controller changes. A command holding a NaN or an
  infinity is refused by name and the camera stays put (decision 357).
- **The distance from the focus is held within 10 m and 1e9 m, and the tilt
  within +-89.9 degrees** (decision 342). The orientation is built from the
  angles directly, so nothing can make a NaN at any tilt; the limit stops the
  picture flipping over the top. **The focus frame is a quaternion carried by
  rotations**, so the poles are not special for the focus either (decision
  359).
- **Speed scales with the distance to the focus** rather than the altitude
  (decision 341), which is always positive: a pan moves the ground by its
  fraction of `d * 2 tan(fov/2)`, what the screen's height covers at that
  distance, so the ground follows the pointer at every scale; a wheel step
  multiplies the distance by 0.9; circling is 0.25 degrees a pixel (decision
  360). "Grab the world" throughout (decision 358).

**`src/view/CameraPath.hpp`** — keyframes of (time, position, orientation):
straight lines in position, `slerp` in orientation, and a lookup by time that
is a pure function of the path and the time. Built through a factory that
refuses fewer than two keyframes, a non-finite time or position, an orientation
that is not a rotation, and times that do not strictly increase; a time outside
the path is refused by name too (decision 345). Each orientation is normalised
once, when the path is built (decision 348). A path's time is seconds from its
own start.

**`slerp` in `src/core/Math.hpp`** (decisions 347, 349 and 356), called as
`slerp({.from = a, .to = b}, fraction)`: the short way round, the tie at
exactly half a turn keeping the sign `to` was given; the angle from the lengths
of the difference and the sum by atan2 rather than an arccosine; and the
weights written with sin(x)/x so two identical orientations give no 0/0. The
fraction is a new validated **`Fraction`** in `src/core/Units.hpp`, refusing
anything outside 0 to 1.

**`src/view/Pose.hpp`** — what both produce (decision 346); the application
builds the `Camera` from a pose with the field of view and the near plane.

**`src/app/`** reads SDL's mouse events into the mapping -- left drag circles,
right drag pans, the wheel moves in whole notches (decision 343) -- and **draws
the M1-20 grid and horizon in the window** (decision 344), at the grid probes'
date, which `render/Probes.hpp` now publishes so the two share it. The original
said the application wires input "and nothing else"; with nothing drawn there
was nothing to fly. It uses both of the line renderer's frame slots, which
closes the gap decision 282 recorded.

## Out of scope

Following a vessel — that needs a simulation, which is phase E. Collision with
the surface. Input remapping or a configuration file. Smoothing or inertia,
which would put a time constant between the input and the camera and make a
scripted replay depend on frame rate. **Keyboard and gamepad control**, which
are prepared for and not built (decision 353). **The benchmark's own path**,
`grid-orbit`, which M1-22 defines (decision 351).

## Tests

`tests/test_camera_controller.cpp`, `tests/test_camera_path.cpp`, and the
`slerp` cases in `tests/test_math.cpp`, all headless; `Fraction` in
`tests/test_units_validated.cpp`. Every one was written first and seen to fail
against a stub.

- **A synthetic sequence gives the pose computed by hand, to 1e-12** -- relative
  to the distance from the Earth's centre for positions, absolute for the
  camera's axes (decision 350), since 1e-12 m is below the spacing of doubles
  at 7e6 m. The hand model keeps the focus' axes as plain vectors turned by
  Rodrigues' formula, sharing no formula with the controller's quaternions.
  **Measured over 3,000 seeded commands: 6.1e-15 and 2.9e-14.**
- **Clamping**: past both poles the tilt lands on the limit bit for bit, the
  camera's up still leans the way it did, and dragging further changes
  nothing; from level, a turn of exactly the limit lands on it.
- **Speed scaling**: one pan moves the focus 40 times as far from 400 km as
  from 10 km, to 1e-9 of the ratio; a wheel step is a tenth of the distance at
  both; a turn is the same angle at both.
- **Determinism**: two controllers fed the same 3,000 commands are bit-identical
  at every step; two paths from the same keyframes, at 2,551 times.
- **Path interpolation**: exact at every keyframe, the last included; on the
  straight line at its share of the segment -- checked by what being on the
  line means, the two distances adding up, rather than by the formula written a
  second time, which a first draft did and which agreed to the last bit for
  that reason -- **measured 3.95e-16, held to 8e-16**; the turn about the axis
  **4.4e-16, held to 1e-15**; continuous one ulp of time either side of every
  inner keyframe; and unit to 1e-15 even from keyframes 15 ulp from unit length.
- **`slerp`**: unit to 1e-15 over 100,000 random pairs (**5.55e-16**); exact at
  both ends bit for bit; against a turn about one axis, **7.8e-16**, held to
  2e-15; the angles to both ends adding up, **8.9e-16 rad**, held to 2e-15; and
  arcs down to 1e-300 rad interpolated rather than rounded away.
- **Singularity**: q and -q do not spin; exactly half a turn apart the tie keeps
  the sign; a billionth of a radian either side goes round its own shorter way;
  identical orientations give no NaN at any fraction.

## Verification

The standing rules. `orbsim --validate --seconds 3` draws the grid every frame
under the validation layers, clean, at 60 fps. Then, by hand: the owner flies
it and judges whether the controls are usable -- a judgement, recorded as one.

## Done when

- [x] `check` green in both trees -- 475 of 475 in each, 2026-10-04; `windows-msvc`
      474 of 474.
- [x] The controller compiles without any SDL or Vulkan header in sight: it is
      in `orbsim_view`, which links neither, and its suite links that.
- [x] A scripted path replays bit-identically, asserted.
- [x] The owner has flown it and said the controls are workable: "works", 2026-10-04 (register decision 362).
- [x] Both fuzzers run for their budgets from their corpora, `src/core/` having
      changed: `fuzz_orbit` 19.4 million inputs in 241 s, `fuzz_time` 9.4 million,
      no findings.
- [ ] The mutation pass is run and recorded, as `scripts/mutants/m1-21.json`,
      with decision 352's mutants.

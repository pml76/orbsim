# ADR 0026: Input becomes device-neutral commands, and held input is sampled at a fixed step

Status: **accepted** (2026-10-04) for the camera, with
[M1-21](../plan/tasks/m1-21-camera-controls.md); **the vessel half is for
milestone 2**, which [`milestones.md`](../plan/milestones.md) owns. Register
decisions 353, 354 and 355.

## Decision

**Nothing that moves -- the camera now, a vessel later -- reads a device.** It
reads *commands*: small value types that say what to do in the terms of the
thing being moved, and nothing about which device asked. For the camera they
are three, in `view/CameraController.hpp`: turn by two angles, pan by a
fraction of the screen's height, and move in or out by a number of steps.

**A mapping turns a device's input into commands**, one mapping per device.
The mouse's is `view/CameraInput.hpp` -- a drag in pixels becomes a turn or a
pan, a wheel tick becomes a step -- and it lives in `orbsim_view`, where it can
be tested without a window. What is left in `src/app/` is the part only SDL
can do: reading the event and handing the mapping its numbers. A keyboard or a
gamepad is added as another mapping; **the controller and its tests do not
change**.

**Input that acts over time is sampled at a fixed step.** A mouse drag and a
wheel tick are events: each is one command, whenever it arrives. A held key or
a stick is not, and the tempting answer -- rate times the last frame's
duration -- makes the result a function of the frame rate, which
[CODING_GUIDELINES section 19](../../CODING_GUIDELINES.md#19-determinism-because-this-is-a-simulator)
rules out. So a held input is read on a fixed clock, whatever the frame rate,
and each reading becomes one command, numbered by its step. The clock is the
simulation's fixed step once phase E builds one; until then nothing is held,
because the mouse sends only events.

**The commands are what a replay records.** A list of commands, each with its
step, replayed into the same controller gives a bit-identical result -- which
M1-21's tests assert for the camera, and which is the input log milestone 2's
replay needs.

## Considered

**Building keyboard and gamepad control in M1-21** -- the same design with two
more mappings. It grows the task by the timing scheme above, its tests, and a
gamepad to test with, while nothing in milestone 1 needs either; declined in
favour of preparing the seam. **Writing the wish down only** -- cheapest now,
and the controller would likely need reshaping when the devices arrived,
since one taking SDL-shaped drags and ticks would have the mouse built into
it. **Scaling a held input by frame time** -- simpler, and a replay on another
machine then flies differently.

## What it does not decide

**The bindings**: which key or button does what, and whether they can be
changed -- a configuration file is outside milestone 1 (register decision 26).
**The fixed step's length**, which is the simulation's to set. **The vessel's
commands**, which milestone 2 writes against this record; what it inherits is
the shape -- devices, mappings, commands, a fixed step for what is held --
and not a list.

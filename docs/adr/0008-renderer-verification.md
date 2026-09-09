# ADR 0008: Renderer verification is probes, golden frames, and a person looking at them

Status: accepted (2026-09-08; recorded 2026-09-09)

Decisions 1, 2 and 3 of
[the milestone 1 register](../plan/milestone-1-decisions.md).

## Decision

The renderer has no machine-checkable output today. `orbsim_smoke` proves the
application starts, paces frames and shuts down without a validation error; it
cannot see a wrong matrix, a wrong colour, a missing tile or an inverted
normal. Every acceptance criterion in phases A, B, D, C and G is currently a
human judgement. This is how that changes.

- **A probe renders exactly one deterministic frame and exits.** `--probe
  <name>`, with `--probe-list`. Everything that could vary is pinned:
  **1280x720** regardless of window size, a fixed epoch, a fixed camera pose, a
  fixed `RenderQuality` preset, and no dependence on wall-clock time anywhere
  in the path. A probe that renders differently twice is a bug in the probe,
  and two consecutive runs are asserted to produce byte-identical HDR dumps.
- **Every probe writes its artefacts on every run, pass or fail.** The
  tonemapped PNG at 1280x720, the linear HDR target as raw `f32`, and a sidecar
  recording probe name, epoch, camera pose, quality preset, GPU, driver version
  and build configuration. A frame without provenance is a screenshot.
- **Numeric probes assert against a reference computed independently, on the
  CPU, in the test** -- never against anything in `src/render/`. The GPU work
  stays in the application; the arithmetic and the reference stay in a test
  that links no Vulkan. That is
  [`../VERIFICATION.md`](../VERIFICATION.md) rule 2 applied to rendering.
- **Image probes compare against a committed golden**: the 1280x720 frame
  downsampled to **640x360** by a 2x2 box filter, about 150 KB. Two tolerances,
  both stated, because they fail differently -- **no channel further than
  4/255**, which catches a small wrong region, and **mean absolute error under
  0.5/255**, which catches a global shift that stays under the per-pixel cap.
  Each is shown by test to catch something the other misses.
- **A golden image is an approved frame.** It enters the repository only after
  the owner has looked at it. `--accept-golden` exists so that approving one is
  a single command; it is run by the owner, **never by a script and never from
  `check`**, and the help text says so. A later mismatch is never resolved by
  re-accepting without the owner reading the diff first, and accepting a new
  frame is a decision whose reason goes in the commit message.
- **A mismatch has to be diagnosable from the artefacts.** Exit code 4, joining
  the existing scheme (1 failure, 2 usage, 3 validation errors), plus a
  `<name>.diff.png` showing rendered, golden and an amplified absolute
  difference side by side, plus both measured numbers on stderr.
- **Goldens only where the image is the artefact.** LUT, geometry and
  radiometry probes stay numeric; for those an image is the weaker check.
- **The verification machinery comes before the things it verifies.** The probe
  mode, the golden comparison and the numeric readback are
  [M1-16](../plan/tasks/m1-16-probe-mode.md),
  [M1-17](../plan/tasks/m1-17-golden-images.md) and
  [M1-18](../plan/tasks/m1-18-radiometry-probe.md), ahead of everything that
  has to be judged by eye. Otherwise the first ten render tasks are verified by
  looking, and then verified again properly later.

## What we considered

**Numeric probes alone.** Fully mechanical, cheap to run, and they cannot see
the defects that dominate a planet renderer: a tile in the wrong place, a seam,
an inverted normal, a missing layer. A readback that says the radiance is right
says nothing about where it was drawn.

**Human sign-off alone.** What the project has today, and it does not survive
the fiftieth frame. It also cannot regress: nobody re-examines a frame that
looked correct last month, so a change that breaks it is found by accident or
not at all.

**Golden images at full resolution.** One 1280x720 PNG is roughly a megabyte,
and phases A to G would put tens of megabytes of binary into a repository whose
diffs are meant to be readable. The 2x2 downsample also suppresses exactly the
single-pixel sampling noise that gives golden images their reputation for
brittleness, without hiding an image that is actually wrong -- a wrong tile, a
wrong colour or a seam all survive a halving.

**A perceptual metric** instead of the two numeric tolerances. Better matched
to what a person notices, and it introduces a second model to be wrong about
between the frame and the verdict. Not now.

## Why

[`0006`](0006-simulation-not-sandbox.md) makes realism the acceptance criterion
for the image as well as the physics, and requires every accuracy claim to be
validated against something this project did not produce. Half of that
transfers directly: radiance in W/m^2/sr is a number, and it is checked against
an analytic value computed in the test. The other half does not transfer at
all, because there is no external reference photograph taken through our camera
at our epoch -- so the honest split is that **what can be a number is a number,
and what can only be judged is judged once, by a person, and then frozen.**

The rule that a golden is an *approved* frame is what stops the mechanism
inverting. [`../VERIFICATION.md`](../VERIFICATION.md) rule 1 warns that a
tolerance loosened until the test passes verifies nothing; a golden re-accepted
to clear a failure is the same thing with a picture attached, and it is the
normal way golden-image suites decay. Making approval a human act, outside
`check`, is the whole of the defence.

Writing the artefacts on every run, pass or fail, is the project owner's
standing requirement and it is also what makes the failure readable: the PNG,
the diff and the sidecar are together enough to say what changed without
rerunning anything.

## What this record does not decide

- **Which probes exist.** The list grows with the features; `clear` and
  `lambert` are the first two.
- **The probe registry's shape** beyond a name, a description and a scene setup
  function.
- **Whether a perceptual metric ever replaces the two tolerances.**
- **How goldens are stored if they ever become numerous.** Eight to twelve
  small PNGs is not a problem worth solving in advance.

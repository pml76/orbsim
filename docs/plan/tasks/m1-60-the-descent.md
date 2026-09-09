# M1-60 — The descent, 400 km to 10 km

Phase: C | Status: not started
Prerequisites: M1-22, M1-56, M1-58
Decided by: [ADR 0008](../../adr/0008-renderer-verification.md), [ADR 0015](../../adr/0015-skirts-and-morphing.md)

## Purpose

Phase C's acceptance criterion, in one run: *"descending from 400 km to 10 km
shows no popping and no seams, and mountains cast visible shadows at the
terminator."*

Everything needed exists. This is the phase's examination, and it is also where
the frame-time budget meets the phase that was most likely to break it.

## What to implement

- A scripted camera path — `descent-himalaya` — from 400 km to 10 km over 60
  seconds of simulated motion, ending low over high relief with the sun near the
  horizon so that both halves of the criterion are visible in the same run. The
  path is a `CameraPath` from M1-21, so it replays exactly.
- **Probe `descent`**, writing frames at fixed intervals along the path —
  twelve of them, so the sequence can be flipped through — plus the numeric
  dumps for the seam and morph assertions from M1-58 at every frame rather than
  at one.
- **The benchmark over the same path**, at 1920×1080 and each preset.

## Out of scope

Flying below 10 km, landing, or atmospheric entry effects. Any optimisation
work: if the budget is missed, that is a finding to report, not a licence to
start tuning.

## Tests

Run along the whole path, at every frame, not at a chosen one:

- **No cracks**: zero background pixels inside the silhouette, at all twelve
  frames — the M1-58 assertion applied to a moving camera, which is where a
  crack is most likely to appear and least likely to be caught by a still.
- **No popping**: the per-pixel bound from M1-58 holds between consecutive
  frames along the whole descent.
- **The selection stays inside its budget**: at every frame the screen-space
  error of every drawn tile is under the threshold, checked headlessly along the
  same path.
- **Streaming holds up**: the fallback-ancestor invariant from M1-53 holds at
  every frame — no hole, ever — and the load and hit-rate budgets are met over
  the whole descent.

## Error budget

The streaming numbers from M1-53 (**≤ 2 loads per tile, ≥ 90 % hit rate**), the
screen-space error threshold from M1-50, and the frame-time budget:
**16.6 ms at 1920×1080, High, RTX A2000**, measured over the path and recorded
per stage.

If the frame-time budget is missed, **stop and report** with the per-stage
breakdown. The choices — a different default preset, a cheaper metric, fewer
loads per frame, or accepting the number — are the owner's, and `realism.md`
section 6.1 is explicit that the answer is never to touch the physics.

## Frames to look at

All twelve `descent` frames, in order. Judge: detail arrives smoothly rather
than in steps; no seam appears at any altitude; the terrain looks like terrain
at every scale, without terracing or exaggeration; the shadows lengthen
correctly as the sun approaches the horizon; nothing shimmers or crawls as the
camera descends — which is also the last check on the camera-relative work from
M1-11, at the altitude where `f32` narrowing would finally show.

**This is the milestone's hardest sign-off.** It is worth doing twice: once at
speed to judge the motion, once frame by frame to judge the detail.

## Done when

- [ ] `check` green in both trees.
- [ ] All four numeric assertions hold at every frame of the descent.
- [ ] The frame-time table is recorded, per stage and per preset.
- [ ] Phase C's stated criterion is met, and the owner has said so after
      watching the descent.

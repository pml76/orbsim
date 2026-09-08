# M1-47 — Frame time after phase D

Phase: D | Status: not started
Prerequisites: M1-22, M1-45

## Purpose

Added at the owner's request, and it is the right addition: phase C is the
riskiest phase in the milestone, and going into it without a frame-time number
means discovering a problem when there are two candidates for the cause instead
of one.

This is a **measurement**, not a test. Nothing here can fail `check`.

## What to do

Run the benchmark from M1-22 over a scripted camera path with the full phase D
scene — textured sphere, four scattering tables, aerial perspective, sun disc —
at the stated conditions:

```
orbsim --bench earth-orbit --frames 900 --width 1920 --height 1080 --quality high
```

Then repeat at Low and Ultra, and at 2560×1440, because a single number does not
say whether the cost is per pixel or per frame — and that distinction decides
what to do about it.

## What to record

In `PROJECT_STATE.md`, as a table beside the phase A and phase B baselines:

- mean, median, p95, p99 and max, for CPU and GPU separately;
- the same at each preset and each resolution;
- the machine, the GPU, the driver version and the build configuration;
- **the cost of each stage** — the four table updates, the surface pass, the
  aerial-perspective application, the resolve — from the GPU timestamps, because
  the total is not actionable and the breakdown is.

## What to do about the number

The budget is **16.6 ms at 1920×1080, High, RTX A2000, Earth from 400 km**.

- **Comfortably inside it**: record it and go on to phase C, which now has a
  known headroom to spend.
- **Close to it**: record it, and record which stage dominates. Phase C's
  budget is then explicitly the remainder, and M1-59's quality fields are
  planned around it.
- **Over it**: **stop and report.** Do not optimise silently and do not quietly
  lower a table resolution — both change the image the goldens were approved
  against. The options are the owner's to choose between: a lower default
  preset, a cheaper table update rule, or accepting a lower frame rate at High
  and moving the budget to Medium. Each is a decision with consequences for what
  "the High preset" means, and working agreement 1 puts it with the owner.

## Done when

- [ ] The benchmark has been run at three presets and two resolutions.
- [ ] The per-stage GPU breakdown is recorded, not just the totals.
- [ ] `PROJECT_STATE.md` carries the table.
- [ ] If over budget, the owner has ruled, and the ruling is written down.

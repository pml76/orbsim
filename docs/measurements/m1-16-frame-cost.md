# What M1-16 costs per frame

Kind: reference
Binding: no — this file records measurements and how to repeat them
Read when: you want to know whether probe mode changed the window's frame, or
you want to measure it on another machine and compare.

[M1-16](../plan/tasks/m1-16-probe-mode.md) added probe mode, which runs only
under `--probe`. The window's own frame gained two things: one usage flag on
the HDR target (`TRANSFER_SRC`, register decision 188), so that a probe can
copy it out, and a constant depth of 0.5 in `fullscreen.vert`. A usage flag
can in principle cost a GPU its compression of that image, so the owner asked
for the cost to be measured rather than assumed (register decision 206). The
method, the script and the reading of the figures are
[M1-14's](m1-14-frame-cost.md); only what differs is written here.

## How to run it

From the repository root, in the environment `check` builds in:

```
python scripts/measure-frame-cost.py scripts/measurements/m1-16.json
```

**"before" is `4ef3f35`**, M1-15's last commit; **"after" is `3511065`**,
M1-16's. Both are instrumented with M1-14's four timestamps per frame, whose
anchors match once in each, in worktrees under `build/measure/`; nothing is
ever committed from them. The processor flags of
[ADR 0023](../adr/0023-the-processor-we-assume.md) landed between the two
commits too; they change CPU code only, and the figures below are GPU time.

## What it measures

Two variants, seven rounds, **thirty seconds a run** with validation off,
after 500 frames of warm-up: the frame before M1-16 and the frame after it.
Four comparisons, each a difference of medians: GPU time per frame, in the
scene, in the resolve pass, and the application's own frame count.

**Why thirty seconds and seven rounds, not M1-15's eight and three.** On
2026-09-26 the application ran at about 120 fps on this machine, where on
M1-15's day it ran at 600 to 1,400, and both builds were held there alike.
The cause was not found. At 120 fps, eight seconds hold about 980 frames,
fewer than the 500 of warm-up plus the 500 the instrumentation measures
before it first prints, so the first attempt recorded no GPU time at all.
Three rounds of thirty seconds then gave a difference inside the spread of
either variant, and seven were run to settle it.

**The GPU was slower per frame than on M1-15's day**, 376 to 382 us against
93, with the same code before M1-16. The likely reason, not verified, is that
a GPU held to 120 frames a second lowers its clock. Absolute figures from this
day are therefore not comparable with M1-15's, and a difference between the
two variants is, because both ran under the same conditions, taking turns.

## Results

**2026-09-26, NVIDIA RTX A2000 8GB Laptop GPU, driver 582.53, 1600x900**,
medians:

| | before | after | difference |
|---|---|---|---|
| **Seven rounds** ([raw](m1-16/2026-09-26-nvidia-rtx-a2000-8gb-laptop-gpu.json)) | | | |
| GPU time per frame | 381.94 us (374.87 to 384.87) | 375.93 us (369.82 to 380.97) | **-6.01 us** |
| of which the scene | 80.07 us | 78.69 us | -1.38 us |
| of which the resolve pass | 280.36 us | 275.74 us | -4.62 us |
| **Three rounds** ([raw](m1-16/2026-09-26-nvidia-rtx-a2000-8gb-laptop-gpu-3-runs.json)) | | | |
| GPU time per frame | 376.97 us (374.20 to 377.56) | 384.92 us (373.43 to 386.24) | **+7.95 us** |

**M1-16 has no cost this measurement can see.** The two sets of runs disagree
in sign -- 8 us slower, then 6 us faster -- and each variant's own runs spread
over 10 to 11 us, so the difference is noise of about 2 % of the frame. What
it bounds: whatever the `TRANSFER_SRC` flag and the constant depth cost, it is
smaller than about 8 us in 380 on this GPU at this clock. The frame rate
carries no signal, as before.

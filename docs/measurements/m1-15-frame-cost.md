# What M1-15 costs per frame

Kind: reference
Binding: no — this file records measurements and how to repeat them
Read when: you want to know what exposure and the AgX tonemap cost, or you
want to measure it on another machine and compare.

[M1-15](../plan/tasks/m1-15-exposure-and-agx.md) turned the resolve pass from
an sRGB encode into the whole display transform: the exposure multiply, AgX --
two 3x3 matrices, three logarithms, three polynomials, three powers -- and
the encode. The owner asked for its cost to be measured the way M1-14's was
(register decision 184). The method, the script and the reading of the figures
are [M1-14's](m1-14-frame-cost.md); only what differs is written here.

## How to run it

From the repository root, in the environment `check` builds in:

```
python scripts/measure-frame-cost.py scripts/measurements/m1-15.json
```

**"before" is `a1bb978`**, M1-14's last commit, whose resolve pass only
encodes; **"after" is `69ed4e0`**, the commit that added exposure and AgX.
Both are instrumented with the same edits -- M1-14's four timestamps per
frame, so the resolve pass is timed on its own -- in worktrees under
`build/measure/`; nothing is ever committed from them.

## What it measures

Two variants, each run three times round-robin for eight seconds with
validation off, after 500 frames of warm-up: the frame before M1-15 and the
frame after it. Three comparisons, each a difference of medians: GPU time per
frame, GPU time in the resolve pass, and the application's own frame count.

**Run with the machine otherwise idle.** A first attempt on 2026-09-25 was
stopped and discarded because the test suites were running at the same time;
this is recorded because a timing taken under load looks exactly like one
that was not.

## How to read the results

The GPU figures are Vulkan timestamps and are what the task changes. **The
frame rate is not**: it swung from 601 to 1392 fps across three runs of one
unchanged variant, so the difference between two medians of it carries no
signal, and it is reported only because the script reports it.

## Results

**2026-09-25, NVIDIA RTX A2000 8GB Laptop GPU, driver 582.53, 1600x900**
([raw results](m1-15/2026-09-25-nvidia-rtx-a2000-8gb-laptop-gpu.json)),
medians of three:

| | before | after | difference |
|---|---|---|---|
| GPU time per frame | 69.81 us | 93.19 us | **+23.39 us**, 0.14 % of a 60 Hz frame |
| of which the resolve pass | 41.69 us | 64.09 us | **+22.40 us** -- 44.5 us per megapixel after, against 29.0 before |
| of which the scene | 20.86 us | 21.88 us | +1.02 us, inside the run-to-run spread (20.13 to 22.26 before) |

The spread of each GPU figure across its three runs is under 5 %, so the
difference is well outside it. The resolve pass now costs about half as much
again as it did, which is the price of AgX per pixel; M1-14's own figure for
the pass, 39.78 us on 2026-09-24, agrees with today's "before" to 5 %.

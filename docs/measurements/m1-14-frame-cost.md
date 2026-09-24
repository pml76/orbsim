# What M1-14 costs per frame

Kind: reference
Binding: no — this file records measurements and how to repeat them
Read when: you want to know what the HDR target and the resolve pass cost, or
you want to measure it on another machine and compare.

[M1-14](../plan/tasks/m1-14-hdr-target.md) moved the scene into a linear HDR
target and added a resolve pass that encodes it for the display. The owner
asked, before it was built, what each recommendation would cost against its
alternative, and then asked for the measurement to be repeatable on other
machines so the results can be compared. This is that measurement.

## Contents

- [How to run it](#how-to-run-it)
- [What it measures](#what-it-measures)
- [How to read the results](#how-to-read-the-results)
- [Results](#results)

---

## How to run it

From the repository root, in the environment `check` builds in -- clang,
Ninja and the Vulkan SDK on the path:

```
python scripts/measure-frame-cost.py scripts/measurements/m1-14.json
```

`--runs N` changes the number of rounds (the spec's default is 3), and
`--cmake <path>` names CMake if it is not CLion's bundled copy at the path
[`STATUS.md`](../STATUS.md) gives. The script:

1. **Builds two exact commits, never your working tree.** "before" is
   `d10cc40`, the last commit without M1-14; "after" is `a89c194`, the commit
   that added the HDR target and the resolve pass. Each is checked out in a
   git worktree under `build/measure/before/` and `build/measure/after/`,
   configured with the `relwithdebinfo` preset, and built. The first run on a
   machine fetches every dependency at the commit's pinned tag, which takes
   some minutes; later runs reuse the worktrees. `git worktree remove
   build/measure/before` (and `after`) deletes them.
2. **Adds temporary timing code to each worktree**, from the spec's `edits`:
   Vulkan timestamp queries around the frame, and in "after" also around the
   scene and the resolve pass, a steady-clock timer around the descriptor
   rewrite, and two environment switches for the variants below. The edits
   are applied to the disposable worktree only and are never committed; an
   edit whose anchor no longer matches its commit stops the run by name.
3. **Runs five variants, round-robin**: one run of each, then the next round.
   A machine warming up or a background task starting therefore affects
   every variant alike, rather than whichever ran last. Each run opens the
   application's window for the variant's time -- 8 seconds, or 2 for the
   validated ones -- so the machine should otherwise be idle, the window left
   alone, and the laptop on mains power.
4. **Writes a results file** to `docs/measurements/m1-14/<date>-<gpu>.json`:
   the GPU, the driver version, the Vulkan version, the resolution the
   swapchain actually got, the operating system and processor, both commits,
   every run, the median of each figure per variant, and the comparisons
   below. Commit it: the directory is where machines are compared.

It is **not a test** and nothing in `check` runs it. A timing threshold on
shared hardware fails for reasons unrelated to the change, which is M1-22's
argument for its benchmark mode too; this is a measurement, recorded.

## What it measures

### The figures each run reports

| Figure | How it is taken | Unit |
|---|---|---|
| `gpu_frame_us` | Two Vulkan timestamps, at the start and the end of the frame's command buffer, converted with the device's `timestampPeriod`. Averaged over every frame after the first 500 | microseconds of GPU time |
| `scene_us` | "after" only: from the start of the frame to the end of the scene's rendering -- today, clearing the HDR target and the depth image | microseconds of GPU time |
| `resolve_us` | "after" only: from the end of the scene to the end of the resolve pass -- the two layout transitions, the display image's clear, and the full-screen triangle | microseconds of GPU time |
| `descriptor_update_us` | "after" only: a steady clock around `vkUpdateDescriptorSets`, the per-frame rewrite of decision 158 | microseconds of CPU time |
| `fps` | The application's own count: frames presented over the run's wall-clock length | frames per second |
| `wall_ms` | The process's wall-clock time, start-up and teardown included | milliseconds |

**Why GPU timestamps and not the frame rate.** Nothing is drawn yet, so a
frame costs the GPU tens of microseconds while the frame rate is set by the
CPU and the presentation engine; for identical code it ranged from 841 to
1,951 fps on this machine in one day, which hides a 40-microsecond change
entirely. The timestamps measure the GPU's own work and repeat to 2 % (see
the results). The frame rate is kept as a sanity figure, not as the result.

**Why 500 warm-up frames.** The GPU raises its clocks under load, and the
first frames also pay for pipeline and driver warm-up. At the frame rates
seen here 500 frames is a quarter to half a second. What was checked is that
it is enough, not that it is optimal: see "Warm-up" under the results.

### The variants

| Variant | What it answers |
|---|---|
| before: the frame without M1-14 | the baseline: the scene cleared straight into the display image |
| after: the frame with M1-14 | the same frame with the HDR target and the resolve pass |
| after: resolve pass without clearing the display | decision 167's alternative: the display image's load operation `DONT_CARE` instead of `CLEAR` |
| after: validated, with synchronization validation | `--validate`, as `orbsim_smoke` runs it (decision 160) |
| after: validated, without synchronization validation | the same with synchronization validation switched off |

### The comparisons

Each is the difference of two medians, printed and stored in the results file:

| Comparison | Of | Minus |
|---|---|---|
| GPU time M1-14 adds to a frame | `gpu_frame_us`, after | `gpu_frame_us`, before |
| GPU time the resolve pass's clear costs | `resolve_us`, after | `resolve_us`, without clearing |
| wall time synchronization validation adds | `wall_ms`, validated with | `wall_ms`, validated without |
| frame rate with M1-14, against without | `fps`, after | `fps`, before |

### What it deliberately does not measure

- **One HDR target against one per frame in flight** (decision 159). With the
  renderer's barriers every frame waits for the one before on the single
  queue, so the second image could not overlap anything; its cost would be
  memory, 8 bytes a pixel, which needs no measurement.
- **`texelFetch` against a sampler** (decision 158). Both read one texel per
  pixel; the difference is bounded above by the whole resolve pass, which is
  measured, and building the alternative only to time it was not worth it.
- **A scene with content.** Nothing is drawn until M1-19, and a uniformly
  cleared image is the best case for the GPU's colour compression, so
  today's figures are a floor rather than a forecast. The resolve pass reads
  8 bytes and writes 4 for every pixel whatever the image holds, so an image
  that does not compress will cost it more, by an amount this measurement
  cannot give; M1-22's benchmark mode measures it with a real scene.

## How to read the results

- **Scale by resolution before comparing machines.** The resolve pass costs
  per pixel, so `resolve_us` divided by the swapchain's megapixels is the
  figure that compares across displays. The results file records the
  resolution the swapchain actually got, which on a scaled display is not
  the window's nominal 1600x900.
- **The median of three runs is the figure**; the min and max beside it show
  how much a run moved. A spread wider than a few percent means the machine
  was not idle, and the run is worth repeating.
- **The driver version matters** as much as the GPU. It is decoded as the
  vendor writes it (NVIDIA's and Intel's encodings differ from Vulkan's), and
  the raw value is kept beside it.
- **A frame budget for comparison**: 16.7 ms at 60 Hz, 6.9 ms at 144 Hz.

## Results

One row per machine and run date. The full record of each -- every run, and
every running mean each run printed -- is the JSON file named in its row.
All figures are medians of three runs; GPU and CPU times in microseconds, the
synchronization-validation column in milliseconds of wall-clock time.

| Date | GPU, driver | Resolution | GPU frame before | GPU frame after | M1-14 adds | Resolve pass | Resolve per megapixel | Scene | Display clear | Descriptor rewrite (CPU) | Synchronization validation, 2 s run | Record |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 2026-09-24 | NVIDIA RTX A2000 8GB Laptop GPU, 582.53, Vulkan 1.4.312; Windows 11 26100 | 1600x900 | 22.83 | 65.21 | **+42.38** | 39.78 | 27.6 | 19.29 | +0.29 | 0.25 | not measurable: -53 within a spread of 88 | [`m1-14/2026-09-24-nvidia-rtx-a2000-8gb-laptop-gpu.json`](m1-14/2026-09-24-nvidia-rtx-a2000-8gb-laptop-gpu.json) |

**What the first row says.** M1-14 costs this machine 42 us of GPU time per
frame at 1600x900 -- 0.25 % of a 60 Hz frame, 0.6 % of a 144 Hz one -- and
the resolve pass is 40 us of it. The alternatives weighed on 2026-09-24 would
have saved almost nothing: not clearing the display image saves 0.29 us, and
rewriting the descriptor set only on a resize would save 0.25 us of CPU per
frame. Synchronization validation adds nothing measurable to a validated run:
the difference of medians was -53 ms, against a spread of 88 ms between the
runs of either variant. *(A single-run spike earlier the same day had
suggested about +0.1 s, and that figure went to the owner before this
measurement; the repeated runs do not support it, and register decision 160
says so.)*

**Spread.** Within a variant the GPU times agree to 2 % (the "before" frame
ran 22.71 to 23.16 us), while the application's frame rate for the same code
ranged from 1,760 to 1,910 fps in this run and from 841 to 1,443 fps in a
spike earlier the same day -- which is why the frame rate is not the result.

**Warm-up.** Every run's running mean of the GPU frame, printed every 500
frames after the 500 discarded, moved by at most 1.4 % between the first print
and the last, upward in 9 of the 15 runs and downward in 6 -- no trend the
warm-up is hiding, at the 2 % the runs repeat to. 500 is otherwise not tuned.

**This record was the third run.** The first had every validated run fail on
a leak in the timing code itself -- its query pool was never destroyed, which
the validation layers reported at shutdown -- and the edits now give the pool
an owner. The second was clean but kept no traces; this one does, and its
figures agree with the second's to within the spread above.

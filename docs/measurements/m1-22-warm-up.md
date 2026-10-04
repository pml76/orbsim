# M1-22 — How long the benchmark warms up

Kind: reference
Binding: no — this file records a measurement and how to repeat it
Read when: you want to know why the benchmark discards what it discards, or
you want to check the figure on another machine.

Register decision 367, measured 2026-10-04 on the second machine (`raa`):
AMD Radeon RX 7900 XTX (`1002-744c`), AMD proprietary driver 26.8.1, AMD
Ryzen Threadripper PRO 3955WX, `relwithdebinfo` tree, the `grid-orbit` path's
first view at 1920x1080, through `VK_PRESENT_MODE_IMMEDIATE_KHR`. The files
are in [`m1-22-warm-up/`](m1-22-warm-up/).

**The question.** The task asks for a warm-up "long enough for clocks to
settle, with the count stated and defensible rather than a round number
chosen by feel". The processor and the graphics card both change their clocks
under load, and the first frames pay for work done once. How long until the
three figures the benchmark reports -- the frame interval, the CPU's working
time and the GPU time -- stop moving?

**How it was measured.** The benchmark's warm-up holds the path's first view
still, so its frames are a clean series of one unchanging scene; the CSV
keeps them, marked. The warm-up was set, for this measurement only, to 10 s
for five runs and to 30 s for three, and nothing else was changed. For each
run, [`drift.py`](m1-22-warm-up/drift.py) prints the median of every half
second as a percentage of the run's own steady median -- taken from 5 s on in
the 10 s runs and from 15 s on in the 30 s runs. Its outputs are
[`drift-10s-runs.txt`](m1-22-warm-up/drift-10s-runs.txt) and
[`drift-30s-runs.txt`](m1-22-warm-up/drift-30s-runs.txt); the eight runs'
summaries are in [`summaries/`](m1-22-warm-up/summaries/), and their first
frames in [`first-frames.txt`](m1-22-warm-up/first-frames.txt).

## What the eight runs show

The same shape in every run, at the same moments:

Each half second's median, against the run's steady median:

| Series | Steady median | 0 to 2.5 s | 2.5 to 4.0 s | 4.0 to 8.0 s |
|---|---|---|---|---|
| Frame interval | 0.444-0.447 ms | up to 6.4 % below | 0.7 to 3.2 % above | -0.7 to +0.3 % |
| CPU working time | 0.163-0.165 ms | up to 5.8 % below | 0.6 to 3.8 % above | -1.8 to +0.6 % |
| GPU time | 0.0592-0.0593 ms | 0.2 to 1.4 % above in the first 0.5 s, 0.6 % once in the next | -- | -0.1 to +0.1 % |

- **The CPU side settles by 4.0 s, and in time rather than in frames.** It
  is faster than steady for the first 2.5 s -- in 79 of the 80 half-second
  windows of the two series there -- slower until about 4 s, and steady
  after that to the end of the 30 s runs. The same moments in every
  run, at about 2,200 frames a second, which points at the processor's power
  management rather than at anything the program does once.
- **The GPU settles within a second**, and moves by under 1.5 % even then.
- **The first two frames pay a one-off cost**: the first takes about 8 ms,
  18 times the steady interval, and the second about 0.8 ms; from the third
  on, only the ordinary scattered spikes remain. The GPU's first three
  frames are 8 to 15 % slow.
- **A warm-up too short reads optimistic**, by up to 6 % on the CPU side,
  because the first seconds run fast.

## The figure

**5 s, and at least 30 frames** -- proposed, and ruled by the owner the same
day (register decision 378). The
time is the 4.0 s seen in all eight runs, and one more half-second window of
margin and more. The frames are ten times the three frames of one-off cost;
they matter only where a frame takes longer than a sixth of a second, which
the time then does not cover.

**What it does not show.** One machine, one scene, one driver. The A2000's
laptop processor manages its power differently, and the scenes of phases B,
D and C are heavier. The benchmark's CSV keeps the warm-up's frames, marked,
so the same check can be made from any run with a long enough warm-up.

## How to repeat it

1. In `src/app/BenchMode.cpp`, set `kWarmUpTime` to 30 s, and build
   `relwithdebinfo`. Do not commit the change.
2. Three times, with the machine otherwise idle and on mains power:
   `build\relwithdebinfo\orbsim.exe --bench grid-orbit --frames 100 --bench-out <folder>`
3. `python docs/measurements/m1-22-warm-up/drift.py <folder> 0.5 15 8`

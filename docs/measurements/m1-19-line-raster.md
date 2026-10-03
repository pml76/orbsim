# M1-19 — Where a one-pixel line lands, on three graphics cards

Register decision 278, measured 2026-10-03. The files are in
[`m1-19-line-raster/`](m1-19-line-raster/).

**The question.** Vulkan lets each graphics card choose which pixels a line
one pixel wide covers, unless the pipeline asks for a defined rule. A line one
pixel aside breaks the golden comparison's 4/255 cap after its 2x2
downsample. So: do the cards this project runs on agree, by default and with
`VK_KHR_line_rasterization`'s Bresenham rule?

**The scene** is M1-19's `lines` probe as first ruled (decisions 279 and 280,
before the camera moved from 5 m to 3 m): three axes and a unit square. The
spike that draws it is [`m1-19-line-spike.patch`](m1-19-line-raster/m1-19-line-spike.patch),
which applies to `c988a2f` and is **not** part of the code: it adds the
probe, and a switch, the environment variable `ORBSIM_SPIKE_BRESENHAM`, that
turns Bresenham lines on. [`run-it.txt`](m1-19-line-raster/run-it.txt) is how
it was run; `<bundle>` there is this folder.

**The comparison** is [`compare_lines.py`](m1-19-line-raster/compare_lines.py),
standard-library Python, on the 8-bit PNGs. Before every comparison it holds a
frame against itself shifted one pixel and refuses to go on unless that shows
differences, so a clean answer is one the check could have failed
(`VERIFICATION.md` rule 23). On the AMD card the switch was also shown to
reach the driver: asking for a mode the card lacks, rectangular lines, was
refused by the validation layers.

| Against the AMD RX 7900 XTX (`1002-744c`) | Default rule | Bresenham |
|---|---|---|
| NVIDIA RTX A2000 (`10de-25ba`) | 1,003 lit pixels against 889; 337 differ, up to 171/255 | the same 889 pixels; 350 differ by 1/255 in value only |
| Intel UHD (`8086-4626`) | identical | identical |

Every run exited 0 under `--validate`. Each card's line settings, from
`vulkaninfoSDK`, are in its folder: the A2000 reports `strictLines = true`
and draws rectangles by default; the AMD and Intel cards report
`strictLines = false`; all three report `bresenhamLines = true`.

**What it decided.** Bresenham lines become a required device feature, with
an ADR, in M1-19. Each card also gets its own goldens (decision 287), M1-110.

**What was not kept.** The first machine's run was first committed whole
(`7e9996d`); on 2026-10-03 the owner chose to keep only what reproduces the
comparison. The raw HDR dumps, the EXR and 16-bit copies, and the full
4,441-line `vulkaninfo` report are in that commit's history.

# M1-80 — The MFD panel

Phase: G | Status: not started
Prerequisites: M1-79
Decided by: [ADR 0012](../../adr/0012-orbsim-view.md)

## Purpose

The frame the numbers go in. The milestone plan is right that this deserves more
care than its size suggests — *"the MFDs are where Orbiter's actual gameplay
lives"* — so the layout is built as a structure with named regions rather than
as a set of positions that happen to look right.

## What to implement

- **`src/view/MfdLayout.hpp`**: the panel as data — a rectangle in screen space,
  a grid of labelled rows and columns, and named regions (title, left column,
  right column, footer). Positions come out of the layout; nothing draws at a
  literal coordinate.
- **Resolution independence**: the panel is sized as a fraction of the window's
  shorter dimension and rounded to whole pixels, so it looks the same at 1080p
  and 1440p and the text still lands on pixel boundaries. Both are asserted.
- The panel is drawn as a screen-space quad with a background and a border,
  after the tonemap, in display space, like the text it holds.
- Colours are named constants with a comment on the choice — an MFD is a
  display and its colours are a design decision, which is exactly the kind of
  constant this project asks to be explained rather than tuned.

## Out of scope

Multiple MFDs, mode buttons, or any interaction — the milestone shows one panel
with one mode. A three-dimensional cockpit. Panel transparency over the scene
beyond a fixed opacity.

## Tests

`tests/test_mfd_layout.cpp`, headless.

- **Regions do not overlap** and all lie inside the panel, over a sweep of
  window sizes from 800×600 to 3840×2160.
- **Rounding is exact**: every region's edges are integral pixels at every
  tested resolution, so no row is half a pixel out.
- **The layout scales proportionally**: doubling the window doubles the panel's
  size within one pixel of rounding.
- **A row that would fall outside the panel is reported**, not clipped silently
  — an overflowing readout should be visible as an error rather than as a
  missing number.

On the GPU:

- **Probe `mfd-panel`**, with a golden: the empty panel with its regions filled
  with placeholder text, at 1280×720.

## Frames to look at

`mfd-panel.png`. Judge: the borders are crisp and one pixel wide rather than
blurred across two; the panel sits where the layout says; the text baselines
line up between the columns; nothing is clipped at an edge. Worth also rendering
once at 1920×1080 and confirming it is the same panel, larger.

## Done when

- [ ] `check` green in both trees.
- [ ] Layout is data, and nothing draws at a literal coordinate.
- [ ] Every region is pixel-exact at every tested resolution.
- [ ] `mfd-panel.png` approved and committed.

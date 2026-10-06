# M1-114 — Smoother lines, as a quality setting

Phase: F | Status: not started
Prerequisites: M1-46, M1-76
Decided by: register decision 405 (g); ADR 0007 (render quality is a struct);
ADR 0025 (lines are drawn by Bresenham's rule)

## Purpose

**The owner asked for smoother lines** when M1-19's frames were looked at.
Anti-aliasing was outside M1-19's scope and was offered as a later,
visual-only `RenderQuality` setting (register decision 315). This is that
task. It sits after the orbit track, M1-76, because the track is the line a
person looks at most, and after M1-46, where `RenderQuality` gains its first
real fields.

## What to do

- Measure first what the candidate methods cost and look like on both cards:
  smooth lines through `VK_KHR_line_rasterization`'s smooth mode, multisampling
  of the line pass, and a wider line drawn as quads with a soft edge. Bring
  the figures and the frames to the owner before choosing.
- The chosen method is a `RenderQuality` field. **It must not reach the
  simulation** (ADR 0007): the same scenario at every setting puts the vessel
  in the same place, bit for bit.
- ADR 0025 says lines are drawn by Bresenham's rule. If the chosen method
  replaces that rule at some settings, the ADR is amended in the same commit.

## Tests

- The lowest setting draws exactly the frames it draws today: the `lines` and
  `grid-400km` goldens pass unchanged.
- Each higher setting has a probe frame the owner approves, and a golden per
  card after that.

## Done when

- [ ] The owner has looked at the frames and chosen.
- [ ] `check` passes in both trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed.

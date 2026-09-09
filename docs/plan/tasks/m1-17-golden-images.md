# M1-17 — Golden-image comparison

Phase: A | Status: not started
Prerequisites: M1-16
Decided by: [ADR 0008](../../adr/0008-renderer-verification.md)

## Purpose

A numeric probe can say the radiance is right and still miss a tile drawn in the
wrong place, a seam, an inverted normal or a missing layer. A reference image
catches exactly those. The risk it carries — brittleness, and the temptation to
re-baseline a failure away — is answered by the workflow rather than by the
tolerance: **a golden image is a frame the owner has approved.**

## What to implement

- **`--golden <path>`**: after rendering, downsample the 1280×720 frame to
  **640×360** with a 2×2 box filter, load the golden PNG, and compare:
  - **no channel further than 4/255** from the golden, and
  - **mean absolute error under 0.5/255** across the image.

  Two tolerances rather than one, because they fail differently: the first
  catches a small wrong region, the second catches a global shift that stays
  under the per-pixel cap.
- **Exit code 4** for a golden mismatch, joining the existing scheme (1 failure,
  2 usage, 3 validation errors). A script can tell them apart without parsing.
- **On mismatch, write `<out>/<name>.diff.png`**: rendered, golden and an
  amplified absolute difference side by side, with the two measured numbers
  printed to stderr. The failure has to be *diagnosable from the artefacts*, not
  just reported.
- **`--accept-golden`**, which writes the downsampled frame to the golden path.
  It exists so approving a frame is one command; it is **run by the owner, never
  by a script, and never from `check`**, and the help text says so.
- **`tests/golden/<probe>.png`**, about 150 KB each. The first one is `clear`.
- The downsample and both metrics live in `orbsim_view` as pure functions, so
  they are testable without a GPU.

## Out of scope

Perceptual metrics. Any automatic re-baselining. Goldens for LUT, geometry or
radiometry probes — those are numeric, and an image would be the weaker check.

## Tests

`tests/test_image_compare.cpp`, headless, in `orbsim_view`:

- **The box filter**: a 2×2 constant block downsamples to that constant exactly;
  a known 4×4 pattern gives the 2×2 average computed by hand in the test.
- **The metrics have teeth**: identical images pass; one pixel differing by
  5/255 fails the per-pixel cap and passes the mean; a uniform 1/255 shift over
  the whole image passes the cap and fails the mean. Each of the two tolerances
  is shown to catch something the other does not — otherwise one of them is
  decoration (rule 23).
- **Mismatched dimensions** are reported by name, not by reading out of bounds.
- `probe_clear` gains its golden and becomes a comparison test.

## The workflow, which is the point

1. The probe runs and writes its PNG **every time**.
2. The owner looks at it.
3. If it is right, `--accept-golden` records it, and the golden is committed in
   the same commit as the code that produces it.
4. A later mismatch is **never** resolved by re-accepting without the owner
   looking at the diff first. If the new frame is correct, accepting it is a
   decision, and the commit message says what changed and why.

This is written here because it is the rule that makes golden images worth
having rather than a source of noise.

## Frames to look at

`clear.png` again, now beside `clear.diff.png` from a deliberately broken run —
worth producing once, on purpose, to confirm the diff image is actually
readable.

## Done when

- [ ] `check` green in both trees.
- [ ] `tests/golden/clear.png` is committed and compared.
- [ ] A deliberately broken frame produces a diff image the owner can read.
- [ ] Both tolerances are shown by test to catch something the other misses.

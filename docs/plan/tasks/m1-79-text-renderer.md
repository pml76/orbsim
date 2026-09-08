# M1-79 — The text renderer

Phase: G | Status: not started
Prerequisites: M1-13, M1-78

## Purpose

Glyphs on screen. Small, and worth doing carefully because everything in phase G
sits on top of it and because text is where a half-pixel error is most visible.

## What to implement

- **`src/view/TextLayout.hpp`** — the CPU half, headless: given a string, a
  position and the metrics from M1-78, produce quads with positions and texture
  coordinates. Pure function, no GPU, fully testable.
- **`src/render/TextRenderer`** — one pipeline, one vertex buffer, alpha
  blending, the atlas bound as a texture.
- **Where text sits in the pipeline, which is the one real decision here**: the
  MFD is drawn **after the tonemap, in display space**, not as a lit surface in
  the scene. An instrument panel emits its own light and is not part of the
  radiometric scene; putting it before the tonemap would make the readout's
  brightness depend on the exposure chosen for the Earth outside, which is
  wrong and would also make the text unreadable at exactly the moments a pilot
  needs it. When the MFD later becomes a panel in a three-dimensional cockpit it
  becomes emissive geometry and moves; that is written in the header so the
  change is a decision rather than a discovery.
- Pixel-snapped positions, so glyphs land on pixel boundaries and stay crisp.

## Out of scope

Text wrapping, justification, rich text, colour spans. Any glyph outside the
baked set — a missing glyph draws a visible placeholder rather than nothing,
because silently dropping a character is how a readout loses a minus sign.

## Tests

`tests/test_text_layout.cpp`, headless.

- **Advance accumulation**: a string of n characters in a monospace font spans
  exactly n advances, and each quad's position matches the metrics computed in
  the test.
- **Texture coordinates** map to the glyph's atlas rectangle exactly, with the
  half-texel convention stated and asserted — the classic source of a
  one-pixel-bleed artefact.
- **The empty string** produces no quads and is not an error.
- **A missing glyph** produces the placeholder, asserted by name.
- **Pixel snapping**: positions are integral in screen space, over a sweep of
  fractional origins.

On the GPU:

- **Probe `text`**, with a golden: a line of digits, a line of letters, and the
  characters the MFD actually uses, at the MFD's size.

## Frames to look at

`text.png`. Judge: the glyphs are crisp rather than blurred, with no bleeding
between adjacent characters and no half-pixel shimmer; the baseline is straight;
the spacing is even. Text is the one thing in this project where a reader's eye
is a better instrument than any assertion, which is why this probe exists
despite the layout being fully unit-tested.

## Done when

- [ ] `check` green in both trees.
- [ ] Layout is a pure function with a full unit test.
- [ ] Text is drawn after the tonemap, and the header says why.
- [ ] `text.png` approved and committed.

# M1-46 — The first real `RenderQuality` fields

Phase: D | Status: not started
Prerequisites: M1-12, M1-45
Decided by: [ADR 0007](../../adr/0007-render-quality-is-a-struct.md), [ADR 0012](../../adr/0012-orbsim-view.md)

## Purpose

The struct arrived empty in M1-12 and the milestone plan says this is *"where
the empty struct from phase A stops being empty"*. The fields are the ones ADR
0007 sketched: table resolutions and the multiple-scattering order.

It is also where the rule gets its first real test — a quality setting must
change the image and **nothing else**.

## What to implement

- Fields, with the types ADR 0007 requires — `Texels` and an `enum class`,
  never an `int` and never a `bool`:
  `transmittanceLut`, `multiScatterLut`, `skyViewLut` as `Texels`;
  `aerialPerspectiveSlices` as a `Count`; `scattering` as
  `enum class ScatteringOrder : std::uint8_t { Single, Multiple }`.
- The four `constexpr` presets stop being identical, and each carries a comment
  saying what it is for rather than only what it contains.
- The compute dispatches read their sizes from the value passed down the path
  M1-12 built. Nothing new is threaded — that was the point of threading it
  empty.
- Enumerators stay declared cheapest-first as a reading convenience, and the
  header repeats ADR 0007's warning that **nothing may compare two enumerators
  with `<`**, because "cheaper" is not transitive across hardware.

## Out of scope

Terrain quality (M1-59). A configuration file. The adaptive controller. Any
quality setting that reaches the simulation, which cannot compile.

## Tests

- **The budgets from M1-40 to M1-43 are stated at the High preset**, and this
  task makes that explicit in each test. The lower presets are not held to 1 %
  and 5 % — that is what they are trading away — so instead they assert what
  must hold at *every* preset: values in physical range, no NaN, monotonicity
  where physics demands it, transmittance in [0,1], and no discontinuity at the
  sky-view/aerial-perspective join.
- **Presets differ**: rendering the same probe at Low and Ultra produces
  different images, asserted, so a preset that silently does nothing is caught.
- **Probes pin quality.** Every probe declares its preset explicitly and the
  sidecar records it. A golden image compared against a frame rendered at a
  different preset is a false failure, and this is where that is prevented.
- **Determinism per preset**: each preset's dump is byte-identical across runs.

## The rule this task is really about

ADR 0007's load-bearing clause is that quality never reaches the simulation.
There is no simulation yet — phase E is where the bit-identical-across-presets
test lives (M1-70). What this task can do, and does, is make sure the value only
travels *downstream*: `orbsim_core` still does not link `orbsim_view`, and the
`-DORBSIM_BUILD_APP=OFF` build still succeeds, which means no physics
translation unit can name a quality type.

## Done when

- [ ] `check` green in both trees.
- [ ] Every field is a strong type or an `enum class`.
- [ ] The four presets produce visibly different frames, asserted.
- [ ] Every probe pins and records its preset.
- [ ] ADR 0007's field-list section is updated to say what actually landed.

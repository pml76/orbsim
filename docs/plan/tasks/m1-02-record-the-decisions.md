# M1-02 — Record the decisions as ADRs 0008–0015

Phase: preliminaries | Status: **done 2026-09-09**
Prerequisites: none
Writes: [ADR 0008](../../adr/0008-renderer-verification.md) to
[ADR 0015](../../adr/0015-skirts-and-morphing.md), dated amendments to
[0001](../../adr/0001-units-in-the-type-system.md),
[0005](../../adr/0005-correctness-is-enforced-by-tools.md) and
[0007](../../adr/0007-render-quality-is-a-struct.md), and
[`THIRD_PARTY.md`](../../../THIRD_PARTY.md)

## Purpose

Twenty-six decisions were taken on 2026-09-08 before this milestone started.
[The register](../milestone-1-decisions.md) holds all of them; six span files and
therefore belong in `docs/adr/`, where the project keeps decisions that outlive
the conversation that produced them. Writing them now, before the code that
depends on them, is the point: an ADR written afterwards is a justification.

Documentation only. No C++ changes.

## What to implement

Six new records in `docs/adr/`, each in the house shape — what was decided, what
was considered, why, and what it does not decide:

- **0008 — Renderer verification: probes, golden frames, and a person looking at
  them.** The probe mode, the 1280×720 frame and 640×360 golden, the two
  tolerances, and the rule that a golden image is an approved frame. What it
  considered: numeric probes alone; human sign-off alone; golden images at full
  resolution.
- **0009 — Time is a type with a scale, and the astronomy lives in `src/astro/`.**
  Five scales, the two-part Julian date and its resolution argument, the
  leap-second table that reports on expiry, IAU 2006 precession with the Earth
  rotation angle, and the omissions with their error budgets. Considered:
  seconds since J2000; deferring the whole thing to phase E.
- **0010 — Tiles are KTX2, and Orbiter's `.tree` is converted, not read live.**
  Considered: DDS; our own container; a runtime `.tree` `TileSource`.
- **0011 — The integrator has three seams, and they are closed sets.** Force
  terms, stepper, ephemeris; `std::variant` rather than inheritance; the stepper
  is stateful so a multistep method can arrive; each term declares its
  dependencies so a symplectic method can refuse; Cowell first, then Encke, both
  kept and diffed. Considered: virtual interfaces; templates alone; Encke only.
- **0012 — `orbsim_view`: the render-side maths that Vulkan never touches.** Why
  the link graph, not discipline, keeps `tests/` free of Vulkan headers.
  Considered: one render target; putting it in `core/`.
- **0013 — Catch2 is the test framework.** Considered: keeping the hand-rolled
  harness; doctest.

**Two more were added on 2026-09-09**, on the owner's instruction, after the
audit that opened this task observed that decisions 16 and 18 span files just
as the six above do:

- **0014 — The radiometric chain is manual photographic exposure and the AgX
  tonemap.** The HDR target, the order of the resolve pass, the 179 lm/W
  convention, the 0.5 % budget, and the rule that no tuning constant exists
  anywhere in the chain. Considered: ACES; keeping the LDR pipeline;
  auto-exposure now.
- **0015 — Quadtree LOD is skirts plus vertex morphing.** Why the crack and the
  pop are separate problems, and why the morph factor is the same number as the
  selection metric. Considered: stitching; morphing alone; skirts alone.

Then three amendments and one new file:

- **ADR 0001** gains a dated note: count-like quantities get an integral
  `Count<Derived>` beside `Quantity<Derived>`.
- **ADR 0007** gains a dated note closing the question it left open — the
  numeric base for `Texels` and `Mebibytes` — and pointing at 0012 for where the
  struct lives.
- **ADR 0005** gains a dated note that the phase gates in this milestone are
  where the sanitizers and the second compiler run, since there is no CI.
- **`THIRD_PARTY.md`** at the repository root: every pinned dependency, its
  licence, its pinned version, and — for `bc7enc_rdo` — **which files are
  compiled and which are not**, because that repository carries three licences
  and the answer is not visible from the pin.

Finally, `CLAUDE.md` and `docs/PROJECT_STATE.md` gain a pointer to
[the task queue](../milestone-1-tasks.md) and the register, so a second machine
finds them.

## Out of scope

Re-opening any decision. Writing an ADR for anything in register section 6 —
those are scope fences, not architecture. Any code.

## Tests

None: this is prose. What replaces a test is a check that the record is
navigable.

- Every ADR number is unique and every internal link resolves.
- Every one of the six ADRs is referenced from the task that first depends on
  it, and every one references the register.
- `THIRD_PARTY.md` lists a licence for every `FetchContent_Declare` in
  `CMakeLists.txt`, including the four that already exist (SDL3, vk-bootstrap,
  VMA, Vulkan-Headers).

## Verification

`check` in both trees, which here proves only that nothing was broken by
accident. Then read the six records back against the register and confirm each
decision appears once, with its alternatives.

## Done when

- [x] `docs/adr/0008` … `0015` exist, in the house shape.
- [x] ADRs 0001, 0005 and 0007 carry their dated amendments.
- [x] `THIRD_PARTY.md` exists and covers every pinned dependency, the four
      decided but not yet pinned, and the data sources.
- [x] `CLAUDE.md` and `PROJECT_STATE.md` point at the queue and the register.
- [x] Every task an ADR governs carries a `Decided by:` line naming it, and
      [the register](../milestone-1-decisions.md) section 8 maps the traffic
      the other way.
- [x] `check` is green in both trees.

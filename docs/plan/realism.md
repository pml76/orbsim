# Realism — what it means here, and what stands in the way

Status: the goal is **settled** — see [`../adr/0006`](../adr/0006-simulation-not-sandbox.md).
**One of the six technical choices in section 4 is still open**: question 5,
DE440 against VSOP87 and ELP2000 for the ephemeris. The other five were settled
between 2026-09-06 and 2026-09-17 and are struck through there. *(This said
"the technical choices in section 4 are still open" until 2026-09-21.)*
Written: 2026-09-06

The project owner set the goal plainly: **realism is the acceptance criterion,
for the physics and for the image alike. Single-mass physics will not do.**
On 2026-09-06 the owner ruled further that **orbsim is a simulation, not a
sandbox**; ADR 0006 records that and what follows from it.

This document is the gap analysis that follows from that. It records what the
code does today, what "realistic" additionally requires, and what each step
costs — so the sequencing can be argued about with numbers instead of
enthusiasm.

It is deliberately not a schedule. [`milestone-1-earth.md`](milestone-1-earth.md)
is the schedule for milestone 1, and section 5 says what this changes about it.
**The schedule past it is [`milestones.md`](milestones.md)**, which is
`Status: proposed` — added 2026-09-24, and it carries the list of what *this*
document does not cover, which is the vessel, persistence, replay, docking,
aerodynamic flight, surface operations, instruments and audio. *(Until that
day this sentence named one schedule, because there was only one.)*

---

## 0. What "realistic" is going to mean

"Realistic" is not one bar, and pretending it is one bar is how a simulator ends
up with a 2190-term gravity field lighting an ellipsoid with `pow(ndl, 0.85)`.
So, the working definition:

**A flight dynamics engineer should recognise the numbers, and an astronaut
should recognise the view.** Where those two pull apart, both get their own
budget, and neither is allowed to quietly borrow from the other.

Three consequences worth stating before the lists:

- **Realism is an error budget, not an adjective.** "Realistic orbit" means
  nothing; "position error under 1 km after 24 hours in LEO, checked against a
  NASA GMAT reference trajectory" is a claim that can fail. Every item below
  should acquire a
  number like that before it is called done. See
  [`../VERIFICATION.md`](../VERIFICATION.md) rule 4.
- **Fidelity you cannot verify is decoration.** A J4 term nobody has validated
  against reference data is a liability: it is more code, more runtime, and no
  more truth. Fidelity and verification are bought together or not at all.
- **The expensive realism is structural, not incremental.** Adding a third body
  to a force model is an afternoon. Retrofitting a linear HDR pipeline, a time
  system, or a floating origin into a codebase that assumed their absence is
  weeks. The structural items are marked **[S]** below and should be settled
  early even if implemented late.

---

## 1. Physics — where it stands

Today `src/orbit/` solves exactly one problem: a massless particle around a
single point mass, in closed form, with no forces other than that one. It solves
it very well — hundreds of thousands of checks, validated against references
computed in 60-digit decimal arithmetic, correct from lunar to heliocentric
scale. (It *was* two independent formulations cross-validated; they were merged
onto one solver on 2026-09-12, and [`../VERIFICATION.md`](../VERIFICATION.md)
rule 2 records what that cost and what replaced it.) Nothing below is a
criticism of that code. It is the foundation; it is simply not the building.

### 1.1 The force model — **[S]**

**Today:** `F = -mu * r / |r|^3`. One term. No mass, no attitude, no environment.

| Missing | Buys | Effort | Notes |
|---|---|---|---|
| **Multi-body point-mass gravity** | Lagrange points, lunar transfers, realistic interplanetary flight | Small once an ephemeris exists | The headline item. It is a sum over attractors; the hard part is 1.3, not this |
| **Zonal harmonics J2–J4** | Nodal regression, apsidal precession, sun-synchronous orbits | Small | J2 for Earth is 1.08263e-3 and dominates everything else by ~1000x. An ISS-like orbit regresses about 5 deg/day; today it regresses zero, and that is visible within one session |
| **Full spherical-harmonic field** (EGM96 360x360, or a truncated 70x70) | Low-orbit accuracy, mascon effects at the Moon | Medium | Diminishing returns above ~20x20 for a game. Lunar mascons are the case that actually needs depth |
| **Atmospheric drag** | Orbital decay, reentry, aerobraking | Medium | Needs a density model — NRLMSISE-00 is the standard, and it needs space-weather inputs (F10.7, Ap) which can be fixed constants at first |
| **Solar radiation pressure** | Long-arc accuracy, realistic light-sail and low-thrust flight | Small | Needs the shadow model from 2.4 to be correct through eclipse |
| **Thrust and variable mass** | Any manoeuvre at all | Small | The vessel currently cannot burn. `m` changing means the state is no longer six numbers |
| **Third-body tides, relativistic terms** | Precision the sim will never observe | — | **Deliberately excluded.** Written here so nobody adds them thinking they were forgotten |

### 1.2 Numerical integration — **[S]**

**Today: there is no integrator.** This is the single largest hole. Every
perturbation above is unreachable without one, and the choice constrains
everything after it.

**All but the last of these were settled on 2026-09-08**, and the analysis is
kept because it is what the ruling was made from.
[The register](milestone-1-decisions.md) has the answers: **Cowell first, then
Encke**, both kept and diffed against each other (7); fixed-step **RK4** first
so the convergence order can be measured against theory, then a high-order
tableau (7, M1-64, M1-65); a **stateful stepper**, so a multistep method or an
adaptive controller can arrive without a rewrite (10); a **fixed step with more
steps** under time acceleration, never a variable one (13); and a **Cartesian**
propagated state, equinoctial elements deferred. Close approaches and
regularisation are not decided, and milestone 1 does not need them.

The decisions as they stood, in order:

- **Cowell vs. Encke.** Cowell integrates the total acceleration directly:
  simple, and it spends its precision re-deriving the Kepler motion that
  `propagate()` already gives exactly. **Encke's method integrates only the
  *deviation* from an osculating reference conic** — the deviation is small, so
  the same integrator holds far more significant digits, and it reuses the
  propagator that is already written and tested. For a sim that already owns a
  validated two-body solution, Encke is the natural fit and is the
  recommendation.
- **Which integrator.** Gauss-Jackson (8th order, multistep) is the classical
  choice for orbits and is very hard to beat for smooth force models.
  Dormand-Prince 8(5,3) is adaptive and simpler to get right. Symplectic
  (Wisdom-Holman, Yoshida) conserves energy over very long arcs but handles
  non-conservative forces like drag and thrust badly. **A simulator with burns
  and drag is not a symplectic problem**; adaptive high-order is the likelier
  answer.
- **Fixed step vs. adaptive, and determinism.** Section 19 of the guidelines
  demands a reproducible flight. Adaptive stepping is reproducible only if the
  step-size controller is itself deterministic and the accumulator does not
  depend on frame rate. This must be decided *with* the integrator, not after.
- **State representation.** Cartesian is singularity-free but loses precision on
  near-circular orbits over long arcs. **Equinoctial elements** have no
  singularity at e=0 or i=0 (where classical elements fail, which is why
  `elementsFromState` needs its degenerate-orbit branch at all) and integrate
  smoothly. Worth considering as the propagated state.
- **Close approaches.** A vessel skimming a surface or passing near a small body
  makes the equations stiff. Sundman or KS regularisation is the answer, and the
  propagator already has half the idea in it — `propagate()` uses the Sundman
  transformation today.

**A constraint on the Encke choice was recorded here on 2026-09-07 and removed
again on 2026-09-08, because the constraint was a defect rather than a limit.**
The history is worth keeping, because it is the argument for the whole of
`../VERIFICATION.md` in one episode.

What was observed: `propagate()` failed to converge for near-rectilinear orbits
above about `e = 0.999`, under gcc-14 and clang-on-Linux but not under Windows
clang — same source, different libm. Raising the iteration cap from 200 to
20000 changed nothing. The first response was to record it as a property of the
method and relax the test, and **that was wrong**: a result that depends on
which library rounded a cosine is evidence of an unstable algorithm, not of a
hard limit.

What it actually was: plain Newton on an equation whose slope collapses. The
Newton step is `residual / r(chi)`, and `r` is tiny near the periapsis of a
near-rectilinear orbit, so the step is enormous and the iteration oscillates.
The same defect sat in the Kepler solver, where it was far worse — **196 of 401
hyperbolic anomalies failed at `e = 1.0001`**, which is to say most
near-parabolic escape trajectories.

The fix is that all three equations in `src/orbit/` are strictly monotonic —
`dt/dchi = r/sqrt(mu) > 0`, `dM/dE = 1 - e cos E > 0`, `dM/dH = e cosh H - 1 >
0` — so each root is unique and can always be bracketed. They now share one
safeguarded solver: Newton where its step both stays inside the bracket and at
least halves, bisection where it does not. There is no input for which any of
them reports non-convergence, and the residual error tracks the conic's own
conditioning rather than the method's.

**So Encke is not constrained by this after all**, and the point it briefly
scored for Cowell is withdrawn. Regularisation returns to being what it was: a
refinement for close approaches, not a prerequisite.

### 1.3 Ephemeris and reference frames — **[S]**

**Today: *when*, one body's *where*, and the Earth's orientation.** *(Rewritten
2026-09-21. It said "a notion of *when*, and nothing else", which stopped being
true on 2026-09-20.)* An instant is a `TimePoint` on one of five scales, and
all five convert — M1-03 to M1-05 and M1-86, the last of which reaches UT1 from
TT without the leap-second table. `src/astro/Sun.hpp` puts the **Sun** where
JPL Horizons says it is, geometric and in ICRF, within 0.02″ and 5e-8 AU over
2000–2050 (M1-08). `src/astro/EarthOrientation.hpp` gives the **celestial-to-
terrestrial rotation** and a TT-only intermediate one, within 0.1 mas of
Skyfield over 1900–2100 (M1-07).

**What is still missing is every other body, and a frame that is not the
Earth's.** There is no ephemeris, so no Moon and no planets; the analytic Sun
lights the scene and does not pull on anything (milestone 1's scope fence).
Multi-body gravity is impossible without an ephemeris, which is why item 4
below is still structural.

- **Body positions over time.** JPL DE440 (via SPICE, or a direct Chebyshev
  reader) is ground truth. VSOP87 for planets plus ELP2000 for the Moon is the
  compact analytic alternative and is what Orbiter uses. **Recommendation: read
  DE440 directly.** The file format is simple, it is the same source the
  validation data in `VERIFICATION.md` rule 3 comes from, and matching your
  reference is worth a great deal.
- **Time scales.** UTC, TAI, TT, TDB, UT1 are not interchangeable and the
  differences are not small: TT = TAI + 32.184 s exactly, UTC has leap seconds,
  UT1 wanders with the Earth's rotation. Ephemerides are in TDB; Earth rotation
  needs UT1; the user's clock is UTC. **A single `Seconds` since an unstated
  epoch is not going to survive contact with this.** A `TimePoint` type with an
  explicit scale belongs alongside the unit types in `core/`.
- **Frames.** ICRF/J2000 as the inertial frame, body-fixed frames for surfaces
  and launch sites, and the transformation between them: precession (IAU 2006),
  nutation (IAU 2000A), Earth rotation angle, polar motion. Ground tracks,
  launch azimuths and landing sites are all wrong without this, and wrong in a
  way that looks plausible.

  ***Done for the Earth, 2026-09-20 (M1-07)**, and two things about how are
  worth carrying forward to the next body. The composition is **not** the four
  ingredients above applied in that order: "IAU 2006 precession with the Earth
  rotation angle" does not compose as written, because the Fukushima-Williams
  matrix is equinox-based and ERA is measured from the celestial intermediate
  origin — measured 0.342° out, about 38 km at the equator. It is ERFA's
  CIO-based `eraC2t06a` instead
  ([ADR 0016](../adr/0016-the-astronomy-is-erfa.md)). And **polar motion is
  still omitted**, at a stated ≤ 0.6″, with ΔUT1 = 0 at ≤ 13.5″; those are the
  model errors the register's budget table records rather than a gap.*

### 1.4 Rigid-body dynamics (6-DOF)

**Today:** `integrateAngularVelocity` exists in `core/Math.hpp` and is *never
called*. `Quat` itself is no longer unused: besides the perifocal-to-inertial
rotation inside `stateFromElements`, it is what `src/astro/EarthOrientation.hpp`
returns from both entry points, it gained a `RotationMatrix` conversion and a
suite of its own with M1-07, and `view/Mat4.hpp`'s `rotationOf` builds a
transform from one. **Attitude is still unmodelled** — none of that integrates
anything. *(Corrected 2026-09-21: this said `Quat` had exactly one call site,
which stopped being true on 2026-09-20.)*

Needed: an inertia tensor, torque accumulation, the gyroscopic term
(`omega x (I omega)`, which is what makes a tumbling body tumble interestingly
rather than spin about a fixed axis), RCS thrusters, reaction wheels and CMGs
with saturation, gravity-gradient torque, and aerodynamic torque once there is
an atmosphere. Docking and staging change the inertia tensor at runtime.

This is a whole subsystem and it is entirely absent. It is also what makes a
spacecraft feel like a spacecraft rather than a point that moves.

### 1.5 Numerical architecture — **[S]**

- **Floating origin / origin rebasing.** f64 has about 15-16 significant digits.
  At 1 AU (1.5e11 m) that is roughly 10 microns of resolution, which is fine;
  but the *render* path narrows to f32, and the guidelines already require the
  camera-relative subtraction to happen in f64 first. That rule needs to hold
  for the physics too the moment there is more than one central body: positions
  relative to the Sun, with a vessel manoeuvring metres from a station, is a
  cancellation problem.
- **Where the precision boundary sits.** Currently "f64 in the sim, f32 at the
  GPU". With multi-body physics there is a second boundary — barycentric versus
  body-relative — and it deserves the same treatment: one named function, and a
  comment saying which frame each side is in.

### 1.6 Environment models

Drag needs density (NRLMSISE-00 or JB2008). Reentry heating needs the same plus
a thermal model. Magnetic field (IGRF) matters for magnetorquers. Surface
gravity, terrain contact and landing gear are their own problem. None exists.

---

## 2. Visuals — where it stands

The renderer draws nothing at all today, so most of this is about *what to build
into* phase A rather than what to fix. That is good news: the structural items
are cheap now and expensive in six months.

### 2.1 The radiometry problem — **[S], and this is the big one**

`shaders/body.frag` currently computes:

```glsl
float lit = 0.04 + 0.96 * pow(ndl, 0.85);
```

That is a hand-tuned look, not a physical quantity, and the swapchain is
deliberately `B8G8R8A8_UNORM` with the comment *"the shaders write display-ready
colours directly"* in `VulkanContext::createSwapchain`. **The entire pipeline is currently
LDR and non-linear by design.** For the stated realism goal that has to invert:

- Work in **linear radiance, in physical units**. The solar constant is
  1361 W/m^2 at 1 AU, falls as 1/r^2, and every surface response should be a
  BRDF that conserves energy. Then Earth's albedo (~0.3) produces earthshine on
  the Moon *for free*, because the numbers are real.
- Render to an **HDR target** (RGBA16F), not the swapchain format.
- Add a physically-meaningful **exposure** stage. Space has the most brutal
  dynamic range in any rendering domain: a sunlit cloud top and a star field
  differ by more than ten orders of magnitude, and the reason spaceflight
  photography looks the way it does is exposure choice.
- **Tonemap** at the end — **AgX**, settled on 2026-09-08 as decision 16, with
  manual photographic exposure and auto-exposure deferred. This is the step
  that turns physical radiance into something a monitor can show without the
  highlights turning into flat white plastic.
- Then **sRGB encode once**, at the very end.

Doing this later means rewriting every shader written before it. Doing it in
phase A costs one render target and one fullscreen pass.

### 2.2 The Sun as a real light source

A point light with `sunDir` is wrong in a specific, visible way: the Sun
subtends about 0.53 degrees from Earth, which is what gives terminators their
soft edge and what makes a penumbra exist at all. Model it as a disc with limb
darkening. Angular size changes with distance, which matters at Mercury and at
Jupiter.

### 2.3 Terrain and elevation

*(This section is kept as it was written on 2026-09-06, **before** the ruling
it asked for. Its recommendation was taken the next day: elevation is in phase
C, section 4 item 4 and section 5 both say so, and the dataset is ETOPO 2022.
Marked 2026-09-21, because until then the section read as a live disagreement
with a settled decision — section 1.2 already carried a marker of this kind and
this one did not.)*

The quadtree is planned (phase C). **Elevation is currently deferred to
milestone 2**, and for the stated goal that is the wrong call: mountains have
visible relief from orbit at the terminator, where long shadows are most of what
makes terrain read as three-dimensional. Recommend folding displacement into
phase C rather than after it.

Also planned but worth naming: WGS-84 ellipsoid rather than a sphere (already in
the plan), and normal mapping from the elevation data.

### 2.4 Shadows and eclipses

Planetary shadow on the vessel, vessel self-shadowing, ring shadows, and true
umbra/penumbra geometry through an eclipse. This shares its geometry with the
SRP shadow model in 1.1, and they should be one implementation, not two.

### 2.5 Clouds, ocean, night side

- **Clouds:** a shadowing layer at minimum; volumetric if the budget allows.
  Cloud shadows on the surface are a large part of why Earth reads as real.
- **Ocean:** specular sun glint is the single most recognisable feature of Earth
  from orbit, and it is a BRDF, not a highlight.
- **Night side:** city lights, moonlight, airglow — the plan already leans
  toward folding night lights into phase B, which is right.

### 2.6 Stars, and everything else in the sky

A real catalogue with real magnitudes and colour temperatures — Hipparcos or
Tycho-2 — not a skybox texture. Once exposure is physical (2.1), stars appear and
disappear correctly as the camera adapts, which is a thing no LDR pipeline can
do. The Milky Way needs a background layer.

### 2.7 Reentry, and the atmosphere from inside

Plasma sheath, ablation glow, and the aerial-perspective volume already planned
in phase D applied from *within* the atmosphere. The plan already notes that
Hillaire 2020 must be correct from the ground as well as from orbit; that is the
right requirement and it is worth holding to.

---

## 3. The list, prioritised

Sorted by *structural risk first* — the things that are cheap now and expensive
later — then by realism delivered per unit of effort.

**The State column was added on 2026-09-21**, because two rows had been
delivered — item 2 by M1-03 to M1-05 and M1-86, item 13 by M1-07 — and a table
with no way to say so reads as fourteen outstanding gaps. The authority on what
is done is [`../STATUS.md`](../STATUS.md); this column exists so that a reader
of the priority list is not misled before reaching it. "Open (phase X)" means
milestone 1 does it; "open, after milestone 1" means no task exists yet.

| # | Item | Why now | Effort | State |
|---|---|---|---|---|
| 1 | **Linear HDR + exposure + tonemap pipeline** (2.1) | Every shader written before this must be rewritten after it | S | open (phase A) |
| 2 | **Time system: `TimePoint` with an explicit scale** (1.3) | Touches every signature that takes a `Seconds`. Cheapest today, at zero call sites | S | **done** (M1-03 to M1-05, M1-86) |
| 3 | **Integrator: Cowell then Encke, with a determinism story** (1.2) | Nothing in the force model is reachable without it | M | open (phase E) |
| 4 | **Ephemeris (DE440)** (1.3) | Prerequisite for multi-body, and it is also the validation source | M | open, after milestone 1 |
| 5 | **Multi-body point-mass gravity** (1.1) | The owner's stated requirement. Small, once 3 and 4 exist | S | open, after milestone 1 |
| 6 | **J2 (then J3, J4)** (1.1) | Largest single accuracy gain per line of code in the whole document | S | open (phase E, J2 only) |
| 7 | **Thrust and variable mass** (1.1) | Without it there is no flying, only watching | S | open, after milestone 1 |
| 8 | **6-DOF attitude dynamics** (1.4) | The maths is already written and unused | M | open, after milestone 1 |
| 9 | **Sun as a disc; shadows and eclipse geometry** (2.2, 2.4) | Shared with SRP; cheap alongside the HDR work | S | open (phase D, the disc) |
| 10 | **Drag + NRLMSISE-00** (1.1, 1.6) | Unlocks decay and reentry | M | open, after milestone 1 |
| 11 | **Elevation folded into the quadtree** (2.3) | Much cheaper inside phase C than bolted on after | M | open (phase C) |
| 12 | **Ocean glint, cloud shadows, star catalogue** (2.5, 2.6) | High realism per unit effort once 1 exists | M | open, after milestone 1 |
| 13 | **Frames: precession, nutation, Earth rotation** (1.3) | Needed before ground tracks or launch sites mean anything | M | **done** (M1-07) |
| 14 | **Full spherical-harmonic gravity** (1.1) | Diminishing returns; do it when something demands it | M | open, after milestone 1 |

---

## 4. Open questions for the owner

1. ~~**How far does realism go — simulation or sandbox?**~~ **Settled
   2026-09-06: a simulation.** Real ephemeris time, real frames, real
   perturbations. Items 4 and 13 are therefore in scope, not optional, and
   ADR 0006 records the reasoning.
2. ~~**Encke or Cowell**, and which integrator (1.2).~~ **Settled 2026-09-08:
   Cowell first, then Encke**, both kept and diffed. RK4 first so the order can
   be measured against theory, then a high-order tableau. Symplectic was ruled
   out for a simulator that will have burns and drag.
3. ~~**Equinoctial or Cartesian state** (1.2).~~ **Settled 2026-09-08:
   Cartesian**, with equinoctial elements deferred and named in the milestone's
   scope fences so that the deferral is visible.
4. ~~**Does elevation move into phase C?** (2.3)~~ **Settled 2026-09-07: yes**,
   and the dataset is ETOPO 2022 (decision 23).
5. **DE440 directly, or VSOP87/ELP2000?** (1.3) Recommendation: DE440.
6. ~~**Is `Vec3` staying unit-free?**~~ **Settled 2026-09-17: no.**
   [ADR 0019](../adr/0019-vectors-carry-their-unit.md) is accepted and both its
   steps are done: `Vec3<R>` is templated on an mp-units reference, so
   `cross(r, v)` is m²/s and `Position + Velocity` does not compile. The
   *frame* half of the question was answered separately on 2026-09-20 by
   [ADR 0021](../adr/0021-transforms-carry-their-frames.md), and answered
   **only for the render side**: `Mat4` and a view-local `FramedVec3` carry
   frames, and `core`'s `Vec3` deliberately does not, which is milestone 1's
   scope fence. So the sharper version of this question — a `Vec3` that knows
   it is barycentric metres — is still open for `core`, and is worth
   re-opening when the ephemeris arrives. *(Struck 2026-09-21; it had stood
   unmarked beside four struck questions since ADR 0019 was accepted.)*

---

## 5. What this changes about milestone 1

Milestone 1 as written (A → B → D → C → E → F → G) remains sound, with the five
amendments below. (This said "three" until 2026-09-13 while listing five, which
is what a count written beside the list it counts does eventually.
[`../HISTORY.md`](../HISTORY.md) section 5 carries four of the five, leaving
out phase G's; it states its own count as four but does not say which one it
omits, so this document is the source. *(Corrected 2026-09-21: this claimed
HISTORY "says so", and it does not.)*)

- **Phase A absorbs the HDR pipeline** (item 1), the `RenderQuality` plumbing
  (section 6.6) and the time system (item 2). It is a render foundation, and
  that is the definition of one: everything after it depends on it. The time
  system is there rather than in phase E because B, D and C all come first, and
  item 2 is ranked where it is for being cheapest at zero call sites.
- **Phase E is no longer "call `propagate()` in a loop".** It becomes the
  integrator (item 3) plus the first perturbation, which raises it from Low to
  Medium risk and makes the time system its prerequisite. Its acceptance
  criterion gains an error budget against external reference data -- a NASA
  GMAT trajectory, since Horizons cannot propagate a J2-only satellite
  (decision 4 of 2026-09-08) -- because "time acceleration does not change
  where it ends up" is self-consistency and section 0 rules that out as
  evidence on its own.
- **Phase C absorbs elevation** (item 11).
- **Phase F's acceptance criterion inverts.** This document's first version said
  the orbit track was unaffected, and that was wrong: the phase was written to
  require that the drawn ellipse *stays put* under time acceleration, which is
  correct for two-body motion and exactly backwards once J2 exists. An ISS-like
  orbit regresses about 5 degrees of node per day, so the track must precess,
  at the analytic secular rate. It becomes the first place a perturbation error
  is visible to the naked eye rather than only to a test.
- **Phase G's elements become osculating**, and the MFD should show them
  drifting rather than present a stillness somebody will later try to smooth.

The tile pipeline is genuinely unaffected. The atmosphere is affected only in
that its output must be radiance rather than tuned colour, which is free if
phase A did its job. The Earth-first priority still holds.

The two-body propagator does not go away and is not superseded. It becomes the
*reference conic* that Encke integrates deviations from, and it keeps its three
existing jobs: drawing paths, high time acceleration, and MFD prediction. The
thousands of checks behind it are the reason that reference can be trusted.

---

## 6. Fluency, and what is allowed to scale

**A stuttering view from orbit does not read as real, however correct the
scattering is.** Frame time is part of the image, not a comfort setting, so
fluency belongs in this document rather than in a performance backlog.

The two sides of the simulator get different answers, and conflating them is the
mistake this section exists to prevent.

### 6.1 The physics does not scale

ADR 0006 fixed the fidelity, and it is not a slider. Dropping J2 to gain frames
does not produce a faster simulation; it produces a **different** simulation,
one whose results cannot be compared against the error budgets in
[`../VERIFICATION.md`](../VERIFICATION.md) rule 4 or against anything else.

The answer to physics cost is architectural, and it is already in the plan:
**a fixed timestep on its own clock, decoupled from the render rate, with the
renderer interpolating between the last two states** (phase E; guidelines
sections 13 and 19). That is what makes a slow frame cost a frame rather than a
trajectory. When the physics genuinely cannot keep up, the honest responses are
a cheaper integrator step *within the stated budget*, a longer fixed step whose
error is measured, or fewer modelled bodies — each of which is a change to the
model, made deliberately and written down, not a quality preset.

### 6.2 The visuals do scale

Everything in section 2 is a candidate for a knob, with one exception below.

| Scales | How |
|---|---|
| Atmosphere | LUT resolutions; multiple-scattering order; aerial-perspective volume resolution |
| Terrain | Screen-space error threshold; maximum quadtree level; tile cache size and eviction pressure |
| Clouds | Volumetric → layered with shadows → flat layer → off |
| Shadows | Cascade count and resolution; soft vs. hard; self-shadowing on or off |
| Ocean | Full glint BRDF → simple specular |
| Stars | Catalogue magnitude cutoff |
| Whole frame | Render resolution (including dynamic scaling), anti-aliasing method |

**What must not scale**, because making it optional means maintaining two
renderers and because it is cheap regardless: the linear HDR pipeline, the
exposure stage, the tonemap, reverse-Z, and the f64→f32 boundary. These cost one
render target and one fullscreen pass at any quality. They are the foundation
that everything above is a *quality of*, and a "low" mode that writes
display-ready colour to a UNORM target is not a lower setting — it is the old
renderer, kept alive forever.

### 6.3 The rule that makes it safe

**A quality setting must never reach the simulation state.** The same scenario
run at the lowest and highest settings must put the vessel in the same place,
bit for bit.

This is not a nicety; it is what keeps section 19's determinism claim true once
there is an adaptive quality controller. That controller may read the frame
clock. **The physics may not.** The moment a dropped frame changes a
trajectory, a replay stops reproducing, a bug stops being reproducible, and the
distinction between a model error and a code bug — the whole argument of ADR
0006 — collapses.

An adaptive controller also needs hysteresis, or it oscillates between tiers at
the threshold and the oscillation is more visible than the quality difference it
is managing.

### 6.4 The shape of the setting

**Settled — [`../adr/0007`](../adr/0007-render-quality-is-a-struct.md).**

Not a single global `enum class Quality { Low, High }`. A machine with a strong
GPU and a weak CPU wants different knobs than the reverse, and one tier cannot
express that — terrain LOD is CPU- and IO-bound while scattering is almost pure
GPU. The shape is a **`RenderQuality` struct of per-feature settings, with
`constexpr` named presets that construct it**: discrete choices are their own
small `enum class`, continuous quantities are strong types carrying their unit
(`Texels`, `Pixels`, `Mebibytes`), and neither is a bool or a bare int, per
non-negotiables 1 and 2. Settings arriving from a config file are validated and
reported through `std::expected`; the presets cannot fail.

The presets survive that choice — the tier becomes a *constructor* rather than
the representation, so the one simple control is still there.

### 6.5 How it is verified

Fluency is an error budget like any other, so it gets a number before it gets an
implementation (rule 4):

> *16.6 ms frame time at 1920x1080, High preset, on the RTX A2000, viewing Earth
> from 400 km.*

And the safety rule in 6.3 gets a test, which is rule 16 applied to quality
rather than to time: **run the same scenario at two quality presets and assert
the simulation state is bit-identical.** That test is cheap, it is available long
before the quality system is finished, and it is the one that catches a rendering
concern leaking downward into the physics — which is section 12's rule, arriving
from a new direction.

### 6.6 What this adds to the plan

The quality plumbing belongs with **item 1 in section 3** — the HDR pipeline, in
phase A. Not because the knobs are needed then, but because retrofitting a
settings path through a renderer that assumed one fixed configuration is the same
class of expensive as retrofitting the HDR pipeline itself. The knobs can arrive
empty and gain entries as each feature lands.

The mechanism and the principle are both recorded in
[`../adr/0007`](../adr/0007-render-quality-is-a-struct.md). The clause in 6.3
is what that record is actually for: `RenderQuality` lives in `orbsim_view`
-- moved out of `src/render/` by decision 17 of 2026-09-08, so that the
quadtree and atmosphere tests can read it without a GPU -- and because
`orbsim_core` links neither render-side library, a physics translation unit
that tries to read a quality setting **does not compile**. The rule is
enforced by the link graph rather than by anyone remembering it.

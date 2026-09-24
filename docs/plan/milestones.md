# The milestones past milestone 1 — a proposal

Kind: plan
Binding: **no — this document is `Status: proposed` and binds nothing yet.**
It is written to be read before it is agreed, which is the precedent
[ADR 0019](../adr/0019-vectors-carry-their-unit.md) set: the first record here
drafted as *proposed* rather than *accepted*, "so that the decision could be
read before it was taken".
Read when: you want to know what comes after milestone 1, or whether something
you are about to build has a home.

Status: **proposed**, 2026-09-24. Nothing below is settled except the eight
rulings in section 1, which the owner gave that day.
Written: 2026-09-24

**Which milestone is current, and which task, is in
[`../STATUS.md`](../STATUS.md)** — not here, and not ever here. This document
says what the milestones are and why they are in this order.

## Contents

- [0. Why this document exists](#0-why-this-document-exists)
- [1. The rulings this is built on](#1-the-rulings-this-is-built-on)
- [2. What milestone 1 leaves you holding](#2-what-milestone-1-leaves-you-holding)
- [3. What `realism.md` does not cover](#3-what-realismmd-does-not-cover)
- [4. The ladder](#4-the-ladder)
- [5. The milestones](#5-the-milestones)
- [6. Deliberately unscheduled](#6-deliberately-unscheduled)
- [7. What this document does not decide](#7-what-this-document-does-not-decide)
- [8. What accepting this would change elsewhere](#8-what-accepting-this-would-change-elsewhere)

---

## 0. Why this document exists

[`milestone-1-earth.md`](milestone-1-earth.md) is the only schedule this
project has. [`realism.md`](realism.md) is the only document that looks past
it, and it says of itself: *"It is deliberately not a schedule."*

Its priority list in section 3 holds fourteen items. Counted rather than
recalled: two are done, five are inside milestone 1, and **seven are marked
"open, after milestone 1"**. Those seven were, until this document, the entire
written plan for the rest of the project.

That is a gap worth closing on its own. But there is a second problem
underneath it, and it is the more important of the two.

**`realism.md` is a gap analysis of the physics and the planet image. It is not
a gap analysis of the simulator.** Its section 1 is the force model, the
integrator, the ephemeris, attitude dynamics and numerical architecture; its
section 2 is radiometry, the Sun, terrain, shadows, clouds, stars and reentry.
Both are excellent and neither is wrong. But a reader who finishes that table
comes away believing that closing fourteen rows finishes the project, and
section 3 below is the list of what that belief leaves out.

This document is therefore two things: a ladder of milestones, and a correction
to the impression the existing gap list gives.

---

## 1. The rulings this is built on

Taken by the owner on 2026-09-24, under working agreement 1 of
[`../../CLAUDE.md`](../../CLAUDE.md) — every one was put as a question with its
alternatives and their costs, and every one is the owner's answer. **If this
document is accepted they become decisions 142 to 149 of
[the register](milestone-1-decisions.md)**, and the four that span files earn
an architecture decision record.

| # | Question | Ruling |
|---|---|---|
| 142 | How far does the project go? | **Orbiter's full scope, ecosystem included** — vessels, cockpits, surface bases, docking, and a public add-on interface |
| 143 | What is milestone 2? | **The vessel** — mass, thrust, 6-DOF attitude. Not the ephemeris |
| 144 | The scope fences | **Nothing is fenced out.** Python scripting, audio and a documented plugin interface are all in. Multiplayer and networking are **postponed, not ruled out**, at lower priority |
| 145 | What an add-on is written against | **A C++ interface against a pinned toolchain.** Inside the simulator, Python scripts reach everything that interface exposes — one definition, two bindings |
| 146 | When that interface is designed | **A decision record in milestone 2**, before the vessel type exists. Implemented at the platform milestone |
| 147 | How Python is hosted | **Embedded CPython, pinned**, with a determinism rule of its own, and [M1-70](tasks/m1-70-determinism.md)'s bit-identity assertions extended to cover a scripted vessel |
| 148 | Orbiter compatibility | **Our own formats.** Orbiter data is converted offline, exactly as milestone 1 already does for tiles and elevation. No Orbiter mesh, scenario or vessel-configuration reader, and no binary add-on compatibility |
| 149 | The atmosphere density model | **NRLMSISE-00, implemented from the published model** — not the C port, and not NRLMSIS 2.x. Section 5, milestone 4, says why |

### 1.1 Two consequences that are easy to miss

**The clang pin stops being an internal convenience.** Working agreement 8 of
[`../../CLAUDE.md`](../../CLAUDE.md) keeps one clang version across Windows and
WSL so that a disagreement between them means something. Decision 145 adds a
second, larger reason: a toolchain upgrade now breaks every third-party add-on
ever compiled. That changes what the pin costs and who it binds, and it is the
kind of thing that should be written down before somebody upgrades cheerfully.

**Decision 149 carries an independence problem.** Every NRLMSISE-00
implementation in existence descends from the same NRL coefficient tables, so a
test against another implementation checks *our use of the model* — the
argument order, the units, the species mixture — and not the model itself. This
is precisely the limit [`../../data/skyfield/README.md`](../../data/skyfield/README.md)
records for Skyfield's nutation, and it gets the same treatment: stated in the
fixture's own README, before any budget is claimed against it.

---

## 2. What milestone 1 leaves you holding

Worth stating plainly, because it is the argument for the order in section 4.

When milestone 1 is done there is a photoreal Earth with atmosphere, terrain
and elevation; a correct clock on five scales; a correct Earth orientation and
a real Sun; a real integrator with J2 inside a budget against an external
reference; an orbit track that precesses at the analytic rate; and an MFD
reading osculating elements.

And **an invisible point that cannot be commanded.**

That is not a criticism of the plan — it is the plan. The vessel is a test
particle by scope fence ([the register](milestone-1-decisions.md) section 6):
no mass, no thrust, no attitude. It is also never drawn, and that is not
written down anywhere as a fence, because nobody has yet had cause to notice
it: every occurrence of the word "mesh" in this repository's documents refers
to a *tile* mesh.

Everything below is about turning that point into a spacecraft, and that one
planet into a solar system.

---

## 3. What `realism.md` does not cover

The list this document exists to add. Each row was checked against the tree
rather than recalled, and each names the milestone that now owns it.

| Missing | How it was checked | Owner |
|---|---|---|
| **The vessel as an entity** — mass properties, engines, tanks, docking ports, configuration | `realism.md` section 1.4 covers attitude dynamics and nothing else about the vessel | M2 |
| **The vessel being drawn at all** | No task, record or plan mentions a vessel model, material, animation or exhaust plume. Section 2 of `realism.md` is entirely planet-side | M2 |
| **Units for any of it** | [`src/core/Units.hpp`](../../src/core/Units.hpp) has `Radians`, `Degrees`, `Metres`, `Seconds`, `MetresPerSecond`, `RadiansPerSecond`, `SpecificEnergy`, `Eccentricity`, `Pixels`, `PerSecond`, `Irradiance` and `GravParam` — and **no mass, force, torque or inertia** | M2 |
| **Input, and what it costs determinism** | A replay of a flown trajectory needs the input that flew it. Nothing plans one | M2 |
| **Per-body parameters** — rotation elements, per-body atmospheres, rings | Milestone 1 models one body's orientation. Nothing says what a second body needs | M3 |
| **Aerodynamic flight** — lift, moments, control surfaces, Mach | `realism.md` names drag only as an orbital-decay term. A flight model is a different and much larger thing | M4 |
| **Surface operations** — contact, landing gear, ground handling, bases | One clause in `realism.md` section 1.6: *"Surface gravity, terrain contact and landing gear are their own problem"* | M5 |
| **Docking and composite vessels** | One clause in section 1.4: *"Docking and staging change the inertia tensor at runtime"* | M6 |
| **Scenario files and save/resume** | Decision 26 defers them *for milestone 1*. But section 19 of [`../../CODING_GUIDELINES.md`](../../CODING_GUIDELINES.md) ties the determinism promise to *"save the seed in the scenario"*, and [M1-70](tasks/m1-70-determinism.md) already asserts save-and-resume — with no format, no file and no versioning behind it | M7 |
| **Flight recording and playback** | Absent from every document | M7 |
| **The instrument suite** — Map, Transfer, Align Planes, Sync Orbit, Docking | [`milestone-1-earth.md`](milestone-1-earth.md) says *"the MFDs are where Orbiter's actual gameplay lives"* and then builds exactly one | with each capability |
| **Autopilots** | Absent | M2 onward |
| **Camera modes, panels, the cockpit** | [M1-79](tasks/m1-79-text-renderer.md) anticipates the MFD becoming cockpit geometry; nothing owns the cockpit | M8 |
| **Audio** | Absent from every document | M8 |
| **The extension interface** | Absent, and by decision 146 it is designed in M2 | M2, published M9 |

Two notes on the table.

**None of these is an oversight in `realism.md`.** That document was written on
2026-09-06 to answer one question — what does the physics and the image need in
order to be realistic — and it answers it well. The items above are outside the
question it asked, not missed inside it.

**Four things were named nowhere as either in or out** until decision 144:
audio, multiplayer, an add-on interface, and scripting. By the register's own
words, *"a fence nobody recorded is a fence somebody walks through"* — and
silence is the one state that is neither a fence nor a plan.

---

## 4. The ladder

| # | Milestone | Done when | Risk |
|---|---|---|---|
| **M2** | The vessel becomes a spacecraft | A vessel in low Earth orbit can be pointed, burned, and seen | Medium |
| **M3** | The solar system | A lunar free-return flies inside a stated budget | Medium |
| **M4** | The atmosphere as physics | A vessel decays, reenters and survives, inside stated budgets | **High** |
| **M5** | The ground | Launch from a pad to orbit, and land again | **High** |
| **M6** | Multi-vessel | Two vessels rendezvous and dock, and the composite flies correctly | Medium |
| **M7** | Scenarios, persistence and replay | A saved flight resumes bit-identically; a recorded one replays | Medium |
| **M8** | The cockpit and the sensorium | It is flown from inside, with sound | Medium |
| **M9** | The platform | Somebody else's vessel flies, in C++ and in Python | **High** |

### 4.1 Three things drive this order

**The vessel first, because the repository already says so.** This was the one
place where the evidence overruled the obvious answer. `realism.md` ranks the
ephemeris (item 4) and multi-body gravity (item 5) above thrust (item 7) and
6-DOF (item 8), so the ephemeris looked like milestone 2. But
[M1-69](tasks/m1-69-simulation-clock.md) creates `src/sim/` with the reason
written in its own document: it is *"the loop that owns world state, and which
will grow vessels and scenarios in milestone 2"*. The project had answered the
question before it was asked. Two further arguments agree with it: decision 12
promises that mass and attitude *"join later as channels without rewriting an
integrator"*, and that promise has never been tested — the first channel is
when it gets tested, and it is cheapest now. And without thrust there is no
flying, only watching.

**Instruments ride with the capability they read.** The Transfer MFD needs
multi-body geometry, the Docking MFD needs a second vessel, the Surface MFD
needs an atmosphere and a ground. Deferring all of them to one instrument
milestone would mean building each capability without the thing that makes it
legible, and then building eight instruments against physics nobody has flown.
This is [`milestone-1-earth.md`](milestone-1-earth.md)'s own principle — *"each
phase ends in something that can be looked at and judged"* — applied one level
up.

**The platform is last, and every milestone before it pays for that.** An
interface published early cannot cover what has not been built; an interface
retrofitted late is the expensive case [ADR 0006](../adr/0006-simulation-not-sandbox.md)
warns about. Decision 146 splits the difference: the *shape* is decided in M2
and the *publication* is M9. What makes that work is a standing rule.

### 4.2 The standing rule every milestone from M2 inherits

In the shape of the eight standing rules in
[`milestone-1-tasks.md`](milestone-1-tasks.md), which are not repeated in the
87 task documents because they apply to all of them:

> **Every interface a third party will eventually touch is designed as public
> interface, under the milestone 2 extension record.** Not documented as one,
> not frozen as one — designed as one. M9 publishes an accumulated surface
> rather than retrofitting one.

---

## 5. The milestones

### M2 — The vessel becomes a spacecraft

**The units that do not exist.** `Mass` and `Force` take
[ADR 0022](../adr/0022-a-bounded-scalar-validates-itself.md)'s validating shape
— a private constructor and a factory returning `std::expected`, since a
negative mass is as unrepresentable as a negative eccentricity. Torque and
inertia are `Scalar<>` aliases over mp-units, because neither has a bound worth
holding. This is the first task, because every signature after it takes one.

**A vessel type** carrying mass properties, centre of mass and inertia tensor,
in `src/sim/`.

**Thrust and variable mass.** The integrated state gains its first channel, and
decision 12's promise is either true or it is not.

**6-DOF attitude.** Torque accumulation and the gyroscopic term — the
`omega x (I omega)` that makes a tumbling body tumble interestingly rather than
spin about a fixed axis. `integrateAngularVelocity` in
[`src/core/Math.hpp`](../../src/core/Math.hpp) gets its first caller; the
header has been saying it has none since it was written. Then RCS, reaction
wheels with saturation, and gravity-gradient torque.

**The minimum autopilot**: kill rotation, prograde and retrograde hold.

**Input, and the input log** without which a replay is a different flight.

**A vessel mesh, material and exhaust plume** — the first thing this renderer
draws that is not a planet.

**The extension record** (decision 146). It designs the vessel, engine and
instrument interfaces in the knowledge that they become public, and it must
answer a question nothing in this project currently does: **what a unit type
looks like across the boundary.** If a Python script sees `Metres` as a bare
`float`, the compile-time safety that non-negotiable 1 and
[ADR 0019](../adr/0019-vectors-carry-their-unit.md) are built on stops at the
binding, and the most valuable property this codebase has does not reach the
people most likely to get it wrong.

**External truth.** Torque-free rigid-body rotation is closed-form — the
symmetric top analytically, the asymmetric one in Jacobi elliptic functions —
so the attitude integrator is validated against mathematics rather than against
itself, exactly as the Kepler solver was. The rocket equation bounds the
velocity change of a burn. A finite-burn trajectory comes from GMAT, reusing
[M1-68](tasks/m1-68-gmat-fixture.md)'s fixture machinery and its sensitivity
check: with the thing under test switched off, the comparison must miss by
orders of magnitude, or it is measuring nothing.

### M3 — The solar system

**The ephemeris, and it is the one fork still unruled.** `realism.md` section 4
question 5 — read JPL's DE440 directly, or use VSOP87 with ELP2000 — has a
recommendation on file and no decision, and four separate records point at it.
Measured for this proposal rather than recalled: `de440s.bsp` is 31.2 MB
covering 1550 to 2650, `de440.bsp` is 114.3 MB, and `de441.bsp` is 3.08 GB
covering −13 200 to +17 191. VSOP87's published precision is about 1 arcsecond
for the inner planets over four thousand years either side of J2000 — fifty
times looser than the 0.02 arcseconds this project already asserts for the Sun,
and worth knowing before choosing it. **Ruling on this is M3's first task**, and
it wants the measurement on both sides, not the recommendation.

**The second precision boundary** — barycentric against body-relative,
`realism.md` section 1.5 — in one named function, the way `toRenderSpace` in
[`src/view/Camera.cpp`](../../src/view/Camera.cpp) is the only place in `src/`
that narrows to 32 bits today. Structural, and it arrives with the thing that
makes it necessary.

**Multi-body point-mass gravity** through the force-term seam
[M1-62](tasks/m1-62-force-model-seam.md) builds, which already takes an
ephemeris accessor precisely so that adding a term later changes no signature
anywhere.

**Bodies as data**: gravitational parameter, shape, rotation elements,
atmosphere parameters. Milestone 1 models the Earth's orientation through ERFA;
a second body needs a different mechanism, and naming which one is part of this
milestone.

**Eclipse geometry and solar radiation pressure as one implementation.**
`realism.md` section 2.4 requires it and [M1-44](tasks/m1-44-sun-disc.md)
defers the two together, which is the right shape: the shadow a planet casts on
a vessel and the sunlight that vessel is missing are the same geometry.

**A real star catalogue**, moved here from the image backlog, because a sky
without stars is what makes leaving Earth stop reading as space.

Map and Transfer instruments ride along; the Transfer instrument needs a
Lambert solver, which is a real piece of astrodynamics and should be costed as
one.

### M4 — The atmosphere as physics

**The density model, and the reason decision 149 reads as it does.** Three
routes were examined and two are closed:

- **NRLMSIS 2.0 and 2.1**, the models that succeeded the one `realism.md`
  names, are distributed under an *Open Source Academic Research License*,
  whose name is the trap: "Open Source" is in the title and the terms are not.
  Read at the source: *"authorization is given to use, reproduce, and modify
  the Software solely for research, academic, and non-profit purposes… Any
  commercial use is prohibited."* It further requires that modifications —
  including a translation to another programming language — be delivered back
  to the Naval Research Laboratory. That is incompatible with an MIT project.
- **The 2001 NRLMSISE-00 C port** by Dominik Brodowski — the one Orbiter ships
  — has a "Legal Information" section that is a warranty disclaimer and
  nothing else. **It grants no permission at all.** Orbiter's root being MIT
  does not relicense a third-party file inside it, and
  [`../ORBITER-REFERENCE.md`](../ORBITER-REFERENCE.md)'s own principle is that
  the unit is the file wherever a repository is not uniform.
- **The published model** is the route left, and it is the one the owner took.
  The model is in the literature; implementing from it owes nobody a licence.

Then drag and the ballistic coefficient, with the space-weather inputs — the
solar flux and geomagnetic indices — **pinned in the fixture header**, because
a density that depends on them is not reproducible unless the file says which
values produced it.

Then **aerodynamic forces and moments**: lift, angle of attack, Mach number and
control surfaces. This is the row of section 3 that is least visible in the
existing documents and largest in practice — a spaceplane flight model is not a
drag term with extra coefficients.

Then reentry heating, a thermal model, the plasma sheath, and aerial
perspective applied from *inside* the atmosphere, which `realism.md` section
2.7 already asks for and phase D of milestone 1 is built to allow.

Marked **High** risk. It is the first milestone whose external truth is not
obvious, and section 7 says so.

### M5 — The ground

Surface-relative frames, launch sites, and the initial state of a vessel
standing on a rotating planet. Terrain contact against the elevation milestone
1 phase C ingests. Landing gear: compression, friction and ground handling.
Surface bases and runways — at which point Orbiter's `Elev_mod` layer, deferred
in [M1-57](tasks/m1-57-orbiter-elev.md) because it *"exists to flatten runways
at surface bases and has no meaning until this project has bases"*, finally has
one.

Gear compression at equilibrium is closed-form, so at least one part of this
milestone validates against mathematics. An ascent to a target orbit checks
against GMAT.

Marked **High** risk for the same reason milestone 1 phase C is: contact,
friction and the transition between flight regimes are where simulators consume
their schedule.

### M6 — Multi-vessel

Several vessels simulated at once; docking ports and the inertia of a composite
body; distributed mass; staging; proximity and collision. The Docking
instrument rides along.

This lands **before** the platform milestone deliberately: docking reshapes the
vessel interface, and publishing that interface before it has met a second
vessel would publish the wrong one.

### M7 — Scenarios, persistence and replay

The scenario file — decision 26 deferred it *for milestone 1*, together with
`RenderQuality::fromConfig`, on the grounds that *"they are the same question
and are deferred together"* — with save, resume and versioning. The flight
recorder and playback. M2's input log becoming a replay artefact rather than a
debugging aid.

[`../VERIFICATION.md`](../VERIFICATION.md) rule 13 names a scenario loader as
the highest-value fuzzing target still missing, so it arrives **with its fuzz
target in the same task**, which is already the queue's rule for every parser.

### M8 — The cockpit and the sensorium

Two-dimensional panels, the virtual cockpit, camera modes, the head-up display,
and audio. The instrument panel stops being a screen-space quad and becomes
emissive geometry in a three-dimensional cockpit — which
[M1-79](tasks/m1-79-text-renderer.md) already anticipates in its header *"so
the change is a decision rather than a discovery"*.

**Audio needs no new dependency.** SDL3 is already pinned in
[`../../THIRD_PARTY.md`](../../THIRD_PARTY.md) and carries an audio subsystem.
And the domain is kinder than most: outside a vessel there is nothing to carry
sound, so the problem is cockpit and interface audio rather than a propagation
model.

### M9 — The platform

The C++ interface, documented and versioned, with the pinned-toolchain promise
stated plainly where an add-on author will read it. Embedded CPython at a
pinned version, exposing **exactly** that surface and no more — one definition,
two bindings, which is what stops the two drifting into disagreement. The
script determinism record, and [M1-70](tasks/m1-70-determinism.md)'s
bit-identity assertions extended to cover a scripted vessel, because a script
that reads a clock or an unseeded generator breaks a promise this project has
been keeping since milestone 1. Sample add-ons, built the way a third party
would have to build them.

Marked **High** risk. Publishing an interface is the one decision in this
document that cannot be taken back.

---

## 6. Deliberately unscheduled

Named so that silence is not mistaken for an oversight, which is
[`milestone-1-tasks.md`](milestone-1-tasks.md)'s own convention.

**Multiplayer and networking.** Postponed at the owner's ruling, explicitly not
ruled out. It is a **constraint rather than a milestone**: the bit-identical
determinism this project already promises — same initial state, same
trajectory, 1× and 10000× alike — is exactly what makes deterministic lockstep
networking possible later. The requirement today is therefore to keep doing
what is already being done, and to notice if a future design would break it.

**Full spherical-harmonic gravity.** Stays demand-driven, as `realism.md` item
14 already says: *"do it when something demands it"*. The demanding case will
be a low lunar orbit in M3 or a precision landing in M5, and it belongs to
whichever arrives first.

**Third-body tides and relativistic terms.** Remain excluded, as
`realism.md` section 1.1 already records: *"Precision the sim will never
observe."* Nothing in decision 142 changes that — a larger scope is not a
finer one.

---

## 7. What this document does not decide

Named here so a later reader does not mistake silence for a ruling, which is
the shape [ADR 0006](../adr/0006-simulation-not-sandbox.md) uses.

- **The ephemeris source.** M3's first task, and the last unruled fork from
  `realism.md` section 4.
- **Whether `Vec3` in `core` acquires a frame.** Held out by milestone 1's
  scope fence rather than by a decision, and
  [ADR 0019](../adr/0019-vectors-carry-their-unit.md) calls it *the more
  valuable* of that record's two halves. The ephemeris is what sharpens it,
  so it is M3's question.
- **What a unit type looks like across the add-on boundary.** M2's extension
  record, and the most consequential thing in it.
- **Where the external truth for aerodynamics, ground contact and attitude
  control comes from.** [ADR 0006](../adr/0006-simulation-not-sandbox.md)
  requires that every accuracy claim be validated against data this project did
  not produce. Attitude has an answer — the closed-form torque-free solutions.
  **Aerodynamics and contact may not**, and that is a real problem rather than
  a gap in this document: either those subsystems get a weaker bar that is
  stated out loud, or the project builds only what it can check. It should be
  ruled on before M4 starts, not during it.
- **How many tasks each milestone is.** Milestone 1 is 87 and took a plan of
  its own. Each of these gets the same treatment when it is reached, and
  estimating them now would be inventing numbers.
- **The order within a milestone.** Milestone 1's phases were argued
  individually and so should these be.

---

## 8. What accepting this would change elsewhere

Listed rather than done, because this document is `Status: proposed` and
because amending a settled record before its proposal is accepted is the thing
working agreement 1 exists to prevent.

- **[`realism.md`](realism.md)** — section 3's table gains a milestone column,
  so its seven "open, after milestone 1" rows stop reading as an
  undifferentiated backlog; section 4 question 5 becomes M3's first task with
  the measurements above attached; and a short section points here for
  everything in section 3 of this document.
- **[`milestone-1-decisions.md`](milestone-1-decisions.md)** — section 9 gains
  decisions 142 to 149, and section 8's mapping table gains the four that a new
  record carries.
- **A new architecture decision record**, the twenty-third, for decisions 142,
  144, 145 and 146: they span every file that will ever exist, which is the
  register's own test. It complements
  [ADR 0006](../adr/0006-simulation-not-sandbox.md) rather than superseding it
  — 0006 fixes the fidelity, this one fixes the reach — and it is where the
  clang pin becoming a public promise is written down.
- **[`../../THIRD_PARTY.md`](../../THIRD_PARTY.md)** — CPython joins "decided,
  not yet pinned", arriving with M9. **And the MSIS licence finding goes in
  whatever happens to the rest of this document**: that file exists to stop
  exactly this being rediscovered, and the next person to reach for a density
  model should find it already answered.
- **[`../STATUS.md`](../STATUS.md)** — a pointer to this document, and the
  date. **No count changes**, because nothing here touches code.
- **[`../../CLAUDE.md`](../../CLAUDE.md)** — its routing table gains a row for
  this file. Its current-work section is **not** changed here: that is
  [M1-84](tasks/m1-84-milestone-gate.md)'s job by that task's own checklist,
  and it happens when milestone 1 lands rather than when milestone 2 is
  written down.
- **[`../ORBITER-REFERENCE.md`](../ORBITER-REFERENCE.md)** — unchanged.
  Decision 148 keeps Orbiter at exactly the arm's length milestone 1 already
  holds it at, so the licence table needs nothing.

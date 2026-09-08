# ADR 0006: orbsim is a simulation, not a sandbox

Status: accepted (2026-09-06)

## Decision

**Realism is the acceptance criterion, for the physics and for the image
alike.** Where a cheaper approximation would still "fly convincingly", that is
not sufficient reason to prefer it.

The concrete consequences, which are what this record exists to fix:

- **The force model is multi-body with perturbations.** A single point mass is
  ruled out explicitly. The model grows in this order: multi-body point-mass
  gravity, zonal harmonics beginning with J2, atmospheric drag, solar radiation
  pressure, and thrust with variable mass. The vessel is a test particle -- it
  is acted on and does not act back -- because its mass is negligible against
  every body it flies near, and the restricted problem is both correct and
  cheaper.
- **There is a real epoch and real time scales.** UTC, TAI, TT, TDB and UT1 are
  distinct types, not interchangeable numbers. Ephemerides are read in TDB,
  Earth rotation is computed from UT1, the user's clock is UTC, and the
  conversions between them are named functions. A bare `Seconds` since an
  unstated epoch is not a time.
- **There are real reference frames.** ICRF as the inertial frame, body-fixed
  frames for surfaces and launch sites, and the transformation between them
  including precession, nutation and Earth rotation. Ground tracks, launch
  azimuths and landing sites are meaningless without this, and -- worse --
  they are *plausibly* meaningless, which is the failure mode this project
  cares most about.
- **6-DOF attitude is part of the physics, not a rendering concern.** Inertia
  tensor, torques, the gyroscopic term, reaction wheels and control authority.
  `Quat` and `integrateAngularVelocity` already exist in `core/Math.hpp` for
  this and have never been called.
- **The image is radiometric.** Linear radiance in physical units, the solar
  constant falling as 1/r^2, energy-conserving BRDFs, an HDR target, a
  physically meaningful exposure stage, and one tonemap at the end. The current
  pipeline writes display-ready colour straight to a UNORM swapchain; that
  inverts.
- **Every accuracy claim carries an error budget, validated against data this
  project did not produce.** "Realistic" is not a test result. "Position error
  under 1 km after 24 h against JPL Horizons" is. See
  [`../VERIFICATION.md`](../VERIFICATION.md) rules 3 and 4.
- **The two-body propagator is retained and is not superseded.** It becomes the
  reference conic that Encke-style integration takes deviations from, and it
  keeps its three existing jobs: drawing orbit paths, high time acceleration,
  and MFD prediction. The 3,632 checks behind it are what make that reference
  trustworthy, and they are the reason the cheap path stays available.

## What we considered

**A sandbox: two-body everywhere, with patched conics at the sphere of
influence.** This is roughly what a great deal of spaceflight software for
entertainment does, it is a fraction of the work, and it produces flight that
feels right to almost everybody. It was rejected on the owner's ruling of
2026-09-06. The argument against it is not that it looks wrong -- it is that a
patched conic has a discontinuity at every boundary crossing, and a simulator
whose state jumps when the vessel crosses an invisible surface cannot answer
the questions this project exists to answer.

**A single central body with perturbations, and no ephemeris.** Cheaper than
full multi-body and it buys most of the accuracy in low orbit, which is where
most flight happens. Rejected because it rules out exactly the flights that
justify the expense: Lagrange points, lunar transfers, anything where a second
attractor is the point rather than a correction.

**True n-body, with the vessel perturbing the bodies.** Rejected as fidelity
nobody can observe. A 400-tonne station moves the Earth by an amount some
twenty orders of magnitude below the noise floor of everything else in the
model. This is fidelity that costs and does not pay, and it is written down
here so nobody adds it thinking it was an oversight.

**Symplectic integration** (Wisdom-Holman, Yoshida). Conserves energy
beautifully over very long unpowered arcs, which is why it dominates
solar-system dynamics. Rejected as the primary method because this simulator
has burns and drag, and non-conservative forces are exactly what symplectic
integrators handle badly. A simulator with a throttle is not a symplectic
problem.

**Deferring the time system until something needed it.** The tempting one,
because nothing needs it today. Rejected on cost of retrofit: `Seconds` appears
in the signature of nearly every function that will exist, and there are
currently zero call sites of `propagate()` outside the tests. This change is
close to free today and expensive in three months. The same argument applies to
the HDR pipeline, and for the same reason: both are structural, and structural
things are bought early or paid for twice.

## Why

The domain is unusually honest. A spacecraft that is wrong flies into the
ground, and the error is visible. That is the reason this project is pleasant
to write and it is also the reason approximation is dangerous here: **once the
model is approximate, a model error and a code bug produce the same symptom,
and you can no longer tell them apart.** A simulator that is right for real
reasons has one class of defect to hunt. A simulator that is nearly right has
two, and the second is unfalsifiable.

That is the whole argument, and it is why this decision is worth the weeks it
costs.

The secondary reason is that the interesting parts of spaceflight *are* the
perturbations. A sun-synchronous orbit exists because of J2. Station-keeping
exists because of drag. A lunar free-return exists because of a second
attractor. Rule those out of the model and what remains is a well-drawn ellipse
that never surprises anybody.

## What this record does not decide

Named here so a later reader does not mistake silence for a ruling. Each of
these is a real fork, each deserves its own ADR when it is settled, and none of
them blocks the two structural items above:

- **Encke or Cowell**, and which integrator. The recommendation on file is
  Encke with an adaptive high-order method; not yet ruled on.
- **Cartesian or equinoctial** as the propagated state.
- **DE440 read directly, or VSOP87 plus ELP2000** as the ephemeris source.
  Recommendation on file is DE440, because it is also the validation source.
- **Whether `Vec3` acquires a unit and a frame.** ADR 0001 left this open, and
  multi-body physics with several frames makes the question sharper.
- **How far up the spherical-harmonic field to go.** J2 through J4 is settled
  as a starting point; a full 70x70 or 360x360 field is not.

The full analysis behind all of the above, including the ordering and the
effort estimates, is [`../plan/realism.md`](../plan/realism.md).
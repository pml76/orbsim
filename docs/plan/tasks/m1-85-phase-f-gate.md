# M1-85 — Phase F gate

Phase: F | Status: not started
Prerequisites: M1-75, M1-76, M1-77
Decided by: [ADR 0005](../../adr/0005-correctness-is-enforced-by-tools.md)

**The number is out of sequence on purpose.** This task was written on
2026-09-13, after an audit found phase F had no gate although
[the queue](../milestone-1-tasks.md) and
[the register](../milestone-1-decisions.md) both say every phase ends with one.
It runs between M1-77 and M1-78. Inserting it as "78" would have renumbered
seven tasks, and decision 30 settled that numbers are identifiers that commits
and records cite — so it takes the next free number and sits where it belongs
in the queue instead.

## Purpose

Phase F is the first place a perturbation is visible to the naked eye rather
than only to a test: the drawn track must *precess*, at the analytic secular
rate, and a track that stays put is a failing test
([the plan](../milestone-1-earth.md), phase F). This gate confirms that it does,
under every toolchain, and confirms the thing phase F is uniquely placed to
break — that **drawing** the physics did not **change** it.

## What to do

The full sweep the earlier gates run — `asan`, `windows-msvc`,
`linux-sanitize`, `linux-gcc`, `linux-tsan`, the GPU validation layers, and the
six fuzz targets — plus:

- **The precession budget from M1-77, re-measured**, because M1-76 and anything
  after it touch the path that produces it. The claim is
  `dOmega/dt = -1.5 n J2 (Re/p)^2 cos i`, to the tolerance M1-77 states.
- **A long-arc visual run**: the track drawn at high time acceleration for
  several simulated days, watched for a node that regresses at the right rate
  and for a polyline that degrades — the sampling is in the anomaly, and a
  precessing orbit is where a fixed sample count first looks wrong.
- **The quality-invariance test** from [ADR 0007](../../adr/0007-render-quality-is-a-struct.md):
  the same scenario at the lowest and highest `RenderQuality` presets must leave
  the simulation state **bit-identical**. It has been available since M1-12 and
  this is the first phase whose whole product is a rendering *of* the
  simulation, so it is the first gate where it could actually fail.

## What to check, beyond "it passed"

- **The track is camera-relative, and the narrowing is still in one named
  function.** Phase F is the first code to hand orbital positions —
  metres from a body centre, so 1e7 and upward — to the GPU. Non-negotiable 8
  says the subtraction happens in `f64` before the narrowing; this is where that
  stops being theoretical. Jitter in a drawn track at high zoom is the symptom.
- **Drawing changed nothing.** Compare a propagated state after N steps with the
  track drawn and with it suppressed: bit-identical, by `bitIdentical()`. This
  is `VERIFICATION.md` rule 16 pointed at the seam phase F introduces.
- **gcc-14 and clang agree on the precession rate**, within their expected
  rounding difference. A disagreement here is a finding about the algorithm, not
  a tolerance to widen — the working agreement is explicit, and the second
  toolchain has already earned this twice.
- **Coverage of `src/orbit/OrbitPath.*`** after the move from the worked
  example, which arrives with its own tests and should not lose any.

## What to record

In [`../../STATUS.md`](../../STATUS.md): the assertion counts per configuration,
and the fuzzing totals. In `PROJECT_STATE.md`: the measured precession rate
against the analytic value with its margin, the frame-time cost of the track at
the stated preset, and the coverage table.

## Done when

- [ ] Every configuration passes, with matching assertion counts, and the two
      compilers agree on the precession rate.
- [ ] Six fuzzers clean.
- [ ] The track precesses at the analytic secular rate, inside M1-77's budget,
      re-measured after M1-76.
- [ ] Two `RenderQuality` presets leave the simulation state bit-identical.
- [ ] The `f64` → `f32` narrowing is in one named function, and the track does
      not jitter at high zoom.

# M1-42 — The sky-view LUT

Phase: D | Status: not started
Prerequisites: M1-41

## Purpose

The table the camera actually samples: for the current view position, the
radiance arriving from every direction of the sky. It is rebuilt when the camera
moves significantly, and it is what turns the previous two tables into an image.

## What to implement

- A compute pass filling a 2D table, default 192×108, over view azimuth and
  zenith, using both earlier tables.
- **The non-linear zenith parameterisation** from the paper, which spends
  resolution near the horizon where the gradient is steepest. As with M1-40, the
  mapping and its inverse also live in `orbsim_view` so the test knows what each
  texel means.
- **The update rule is explicit and deterministic**: the table is rebuilt when
  the camera's altitude or the sun direction changes by more than a stated
  threshold, never on a timer and never on a frame count. A table that depends
  on how many frames have elapsed is a table that makes a probe non-reproducible.
- **Probes**: `lut-skyview-ground` and `lut-skyview-400km`, each dumping raw
  values and a false-colour image.

## Out of scope

Aerial perspective (M1-43), the sun disc (M1-44), and composition into a scene
(M1-45). Clouds, stars, airglow.

## Tests

- **The budget: 5 % against the CPU reference**, which now includes multiple
  scattering, at sample directions covering zenith, horizon from both sides,
  the anti-solar point and the solar aureole — the last two being where the
  phase functions are extreme and where an error hides.
- **The parameterisation round trip** to 1e-9, edges included.
- **Physical shape, checked as numbers rather than by eye**: radiance decreases
  from horizon to zenith at ground level for a high sun; the solar aureole is
  the brightest direction that is not the sun itself; and the sky at 400 km is
  darker than the sky at sea level by the ratio the reference gives.
- **The horizon is in the right place**: the transition between sky and ground
  directions lands at the geometric horizon angle for the altitude, computed
  independently in the test, within one texel.
- **Determinism** across dispatches, and across two runs with the camera arriving
  at the same position by different paths — which is what the explicit update
  rule exists to guarantee.

## Error budget

**5 %** against the CPU reference, maximum and p99 recorded, at both altitudes.

## Frames to look at

`lut-skyview-ground.png` and `lut-skyview-400km.png`. Judge: the ground table
shows a bright horizon band grading to darker overhead with the aureole around
the sun; the 400 km table is dark overhead with a thin bright limb. Neither
should show banding or a hard edge where the parameterisation compresses.

## Verification

The standing rules, plus `ctest -R probe_lut-skyview`.

## Done when

- [ ] `check` green in both trees.
- [ ] Both budgets hold, at both altitudes.
- [ ] The rebuild rule depends on camera state alone, and is documented.
- [ ] The horizon lands at the geometric angle, asserted.

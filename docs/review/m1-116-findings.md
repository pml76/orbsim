# M1-116 — the findings

Kind: record
Binding: no — every finding here is a proposal until the owner rules on it
Read when: ruling on the review, or turning a ruling into a task

The review [M1-116](../plan/tasks/m1-116-review.md) asked for, carried out on
2026-10-10 under register decisions 427-439. **It changes nothing**
(decision 431): each finding below says what is wrong, where, the evidence,
how serious it is, and a proposed fix with its cost. The owner rules on them
group by group; the rulings become register decisions and tasks.

**How serious**, in the plan's four words:

- **defect** -- something behaves wrongly today;
- **hidden risk** -- correct today, wrong after a plausible change or input;
- **inconsistency** -- two things that should agree do not;
- **style** -- a departure from the house style.

**How each finding was verified** is said with it. "Reproduced" means a program
or a planted fault was run and its output is quoted; "by reading" means the
code was read and the claim follows from it alone. Planted faults were run in
a separate worktree under `build/` with its own Debug tree, never in the main
tree (decision 439), and the worktree was deleted afterwards.

## Contents

- [Summary](#summary)
- [Group 1 — the simulation core](#group-1--the-simulation-core)
- [Group 2 — the application and the renderer](#group-2--the-application-and-the-renderer)
- [Group 3 — tests that cannot see what they claim](#group-3--tests-that-cannot-see-what-they-claim)
- [Group 4 — the checking tools and the build](#group-4--the-checking-tools-and-the-build)
- [Group 5 — the declared mutation survivors](#group-5--the-declared-mutation-survivors)
- [Group 6 — the worked example](#group-6--the-worked-example)
- [Group 7 — documents and comments](#group-7--documents-and-comments)
- [Group 8 — STATUS.md's open items](#group-8--statusmds-open-items)
- [The two analysis tools](#the-two-analysis-tools)
- [Leads dropped, and why](#leads-dropped-and-why)
- [What the review cost](#what-the-review-cost)

---

## Summary

**Five findings matter most**, in this order:

1. **A compiler warning has been off in almost every file without anyone
   knowing** (finding 4.1). A one-word slip inside mp-units, the units
   library, switches `-Wfloat-equal` off for the rest of every file that
   includes `core/Units.hpp`. Five comparisons of floating-point numbers with
   `==` were hidden by it.
2. **Minimising the window ends the program with an error** (2.1).
   Reproduced on the RTX A2000: a validation error, then exit code 1.
3. **`wrapTau` can return 2π itself, so `elementsFromState` can return a true
   anomaly of exactly 2π**, outside the [0, 2π) its header promises (1.1).
4. **Five tests cannot fail for the fault they name** (3.1-3.5), each shown by
   planting the fault and watching the suite pass.
5. **The mutation harness would count an unrelated compile error as a kill**
   (4.2), and the pre-commit hook checks the files on disk rather than what is
   being committed (4.3).

**Counts.** 120 leads came in: 112 from the six helpers and 8 from the two
tools. After merging duplicates and dropping what did not survive
verification, there are **49 findings**: 9 defects, 25 hidden risks,
11 inconsistencies and 4 departures from the house style -- counted from the
numbered headings, with finding 5.0 and the unneeded includes under "The two
analysis tools". Seven of them (3.11, 4.10, 6.2, 7.1, 7.2, 7.3 and 2.12) are
tables or lists gathering small items of one kind, each a few minutes to
fix. The survivor table in group 5 is separate.

---

## Group 1 — the simulation core

`src/core/`, `src/orbit/`, `src/astro/`.

### 1.1 `wrapTau` can return 2π exactly — defect

- **Where:** `core/Scalar.hpp`, `wrapTau(f64)`, used through the `Radians`
  overload by `elementsFromState`'s `assignInPlaneAngles` and by
  `propagateElements`.
- **What is wrong:** it promises a result in [0, 2π). For a tiny negative
  angle, `fmod` returns the angle unchanged and adding 2π rounds to 2π
  exactly.
- **Evidence, reproduced:** `wrapTau(-1e-20)` returns `6.2831853071795862`,
  bit for bit `kTau`. An ordinary orbit -- e = 0.1 at periapsis, 7000 km out,
  with a radial velocity of -1e-13 m/s -- gives `elementsFromState(...).tra ==
  kTau`. No test asserts the range; `fuzz_orbit` checks only for NaN.
- **Fix:** map a result of 2π to 0 in `wrapTau`, with a compile-time case at
  -1e-20 and an element case just below periapsis. About 10 lines.

### 1.2 `orbitInfo`'s mean motion overflows at scales the project claims — defect

- **Where:** `orbit/Orbit.cpp`, `orbitInfo`, the `meanMotion` and `period` lines.
- **What is wrong:** `sqrt(mu / (a * a * a))` overflows or underflows `a³`
  where the mean motion itself is an ordinary number. The project says the
  code has no built-in scale and tests elements at 1e-112 m.
- **Evidence, reproduced:** a = 1e110 m with mu = 1e300 gives n = 0 and an
  infinite period on an orbit marked closed (the true n is 1e-15);
  a = 1e-110 m with mu = 1e-300 gives n = infinity and a period of 0.
- **Fix:** `sqrt(mu / a) / a`, which has the range, and a scale case. About
  5 lines.

### 1.3 Double-double arithmetic fails near the top of the range — hidden risk

- **Where:** `core/DoubleDouble.hpp`, `split` and `twoProduct`, and through
  them `operator*` and `operator/`; and `tests/test_double_double.cpp`,
  `Sampler`.
- **What is wrong:** the header says every operation "falls back to the plain
  double result the moment its own output stops being finite". Near the
  largest double it does not: `split` returns an infinite high part from
  about (2 - 2⁻²⁶)·2¹⁰²³ upward, and a finite product within about 2⁻²⁶ of the
  largest double overflows in its cross term.
- **Evidence, reproduced:** `twoProduct(DBL_MAX, 0.5)` has a NaN error term
  where `fma` says 0; `exact(DBL_MAX) * exact(0.5)` is NaN and
  `exact(DBL_MAX) / exact(2.0)` is minus infinity; the boundary falls
  between k = 25 and k = 26 in (2 - 2⁻ᵏ)·2¹⁰²³. **The test cannot see it**:
  its sampler draws mantissas from [1, 1.5), not the "uniformly random 52-bit
  mantissa" its comment says, and its exponent never reaches the top of its
  range.
- **Why only a hidden risk:** `elementsFromState`, the only caller, scales
  every input to a mantissa in [0.5, 1) first, so it never reaches this.
- **Fix:** scale one operand in `twoProduct` above about 2⁹⁹⁵ (both halves
  scale back exactly), or fall back to the plain product when the error term
  is not finite; and make the sampler draw the whole [1, 2) and an inclusive
  exponent. About 30 lines.

### 1.4 `propagateElements` refuses an unreachable anomaly by accident — hidden risk

- **Where:** `orbit/Orbit.cpp`, `propagateElements`, and its comment in
  `orbit/Orbit.hpp`.
- **What is wrong:** for a hyperbola's anomaly past its asymptote,
  `stateFromElements` reports `UnreachableAnomaly` by name.
  `propagateElements` has no such check: the radius comes out negative, its
  square root is NaN, and the NaN happens to spread into a `NotFinite`
  report. Its header says `NotFinite` is for a non-finite input, and these
  inputs are finite.
- **Evidence, reproduced:** 6,000 element sets past the asymptote (e from
  1.01 to 50, five time steps): all 6,000 report `NotFinite`, none
  `UnreachableAnomaly`.
- **Fix:** test 1 + e cos ν > 0 and return `UnreachableAnomaly`, update the
  header, add a case. About 10 lines.

### 1.5 Integrating an angular velocity backwards does not rotate — hidden risk

- **Where:** `core/Math.hpp`, `integrateAngularVelocity`.
- **What is wrong:** the angle `length(omega) * dt` is negative for a negative
  step, so it is always "below the negligible rotation" and the orientation
  comes back unchanged.
- **Evidence, reproduced:** with ω = (0, 0, 1) rad/s, dt = -1 s returns the
  identity; dt = +1 s turns it (z = 0.479). The function has no caller and no
  test yet; attitude arrives in milestone 2.
- **Fix:** compare the absolute angle, and write the first test its comment
  already asks for. About 15 lines.

### 1.6 The anomaly converters answer questions that have no answer — hidden risk

- **Where:** `orbit/Orbit.cpp`, `trueToEccentricAnomaly`,
  `eccentricToTrueAnomaly`, `eccentricToMeanAnomaly`.
- **What is wrong:** at e = 1 exactly, a parabola, which has no eccentric
  anomaly, they return 0 or π; for a hyperbolic anomaly the conic never
  reaches they return a finite number of the wrong sign. `stateFromElements`
  refuses the same input by name. Only tests call them today.
- **Evidence, reproduced:** `trueToEccentricAnomaly(2.6, e = 1.5)` is -1.45;
  `trueToEccentricAnomaly(1, e = 1)` is 0; `eccentricToTrueAnomaly(0.3,
  e = 1)` is π.
- **Fix, two choices for the owner:** (a) return `std::expected`, with
  `UnreachableAnomaly` and a parabolic refusal; (b) state the domain as a
  precondition and assert it. 20-40 lines either way.

### 1.7 `angleBetween` with a zero vector returns π/2 — hidden risk, low

- **Where:** `core/Math.hpp`, `angleBetween`, through `directionOf`.
- **What is wrong:** a zero vector has no direction; `directionOf` returns
  the zero vector, and the angle comes out as π/2 with no report. Found while
  verifying 1.1. **No caller can reach it today**: `assignInPlaneAngles`, the
  one production caller, has already refused a zero radius as
  `DegenerateState`.
- **Fix:** a precondition, asserted, in `angleBetween`. Two lines.

---

## Group 2 — the application and the renderer

`src/app/`, `src/render/`, `src/view/`, `shaders/`.

### 2.1 Minimising the window ends the program with an error — defect

- **Where:** `render/VulkanContext.cpp`, `VulkanContext::beginFrame`, whose
  guard reads "a minimised window has a zero-size swapchain ... report no
  frame"; `acquireImage`; `recreateSwapchain`.
- **What is wrong:** on Windows, SDL3 does not report a minimised window as
  zero-sized: `WIN_GetWindowSizeInPixels` returns the last size it had. So the
  guard never fires, the driver reports the swapchain out of date, and the
  rebuild asks for a 0 × 0 swapchain, which Vulkan forbids.
- **Evidence, reproduced** on the RTX A2000: a small program started
  `orbsim --validate --seconds 10`, minimised its window after 3 s, and
  recorded

  ```
  [validation] vkCreateSwapchainKHR(): pCreateInfo->imageExtent (width = 0, height = 0) is invalid.
  Frame could not begin: vmaCreateImage (VK_FORMAT_D32_SFLOAT) failed: VK_ERROR_INITIALIZATION_FAILED
  exit code 1
  ```

- **Fix:** treat a window SDL flags as minimised (`SDL_WINDOW_MINIMIZED`) as
  "no frame", and refuse a zero surface extent before a rebuild. A few lines.
  An automated test would need a window that can be minimised, which the
  smoke test's harness could do; the alternative is a manual check written
  into a gate.

### 2.2 Validation can silently be off where it is promised — hidden risk

- **Where:** `render/VulkanContext.cpp`, the fallback in
  `makeInstanceAndSurface`; `app/main.cpp` and `app/ProbeMode.cpp` for
  `--accept-golden`; `app/BenchMode.cpp`, its report.
- **What is wrong:** when the validation layer is not installed, the context
  logs a line and runs without it. `--accept-golden` is meant to run under
  validation (decision 232), but nothing checks that the layer actually ran,
  so a golden can be accepted unvalidated. The benchmark's report prints
  "Validation: on" from what was asked for, not from what ran.
- **Evidence, by reading:** `running` is set to disabled in the fallback, and
  `gfx.validationLayer()` is read only to record it in the sidecar
  (`ProbeMode.cpp`); `BenchMode.cpp` labels from `request.validation`.
  Decision 398 closed the same gap for the CTest GPU tests only.
- **Fix:** with `--accept-golden`, fail when `validationLayer()` is empty; in
  the benchmark, label from `validationLayer()`. Small, with one usage test
  each.

### 2.3 Some options are still silently ignored — inconsistency

- **Where:** `app/main.cpp`, `parseArguments` and the option checks.
- **What is wrong:** the stated rule is that an option without its mode is
  refused, not ignored. Some still are ignored, and some modes overwrite each
  other.
- **Evidence, reproduced:** `--frames 5 --seconds 1` is refused, exit 2 (the
  control). `--probe-out somewhere --seconds 1` runs the window, exit 0.
  `--probe-list --bench grid-orbit` runs a whole benchmark; `--probe-list
  --probe clear` renders the probe; `--probe clear --probe-list` lists: the
  last mode named wins.
- **Fix:** refuse `--probe-out` without `--probe`, and `--probe-list` with
  anything else, each with a usage test. Small.

### 2.4 Exposure and projection settings the factories accept can fail at the GPU boundary — hidden risk

- **Where:** `view/Exposure.hpp`, `exposureValue100` and `toShaderExposure`;
  `view/Camera.cpp`, `toShaderMatrix`; `view/Projection.hpp`,
  `infiniteReverseZPerspective`.
- **What is wrong:** the factories accept any finite positive setting, and
  say a scenario or a user may supply one. Some of those settings produce a
  value that then fails an assertion where it is narrowed to `f32` -- an
  assertion guarding something a caller can cause, which non-negotiable 3
  says should be reported instead. Two comments are false as written:
  "cannot produce anything else from three validated settings" and "a matrix
  from a validated camera and projection is finite and small".
- **Evidence, reproduced:** an f-number of the smallest positive double gives
  an infinite exposure factor; an f-number of 1e200 gives 0; a field of view
  of 1e-300 rad, and a near plane of 1e300 m, are accepted and produce matrix
  entries no `f32` can hold. An ISO of 1e-300 gives 2.6e-305, which passes the
  check and becomes 0 in `f32` -- a black frame with no report.
- **Fix, a choice for the owner:** (a) give the factories physical bounds,
  which reverses their stated "no limit this code should invent"; (b) have
  the narrowing functions return `std::expected`; (c) correct the comments and
  state the asserted domain. Small to medium.

### 2.5 The grid layout's overflow check is wrong — hidden risk

- **Where:** `view/PlanetaryGrid.hpp`, `GridLayout::from` and `vertexCount`.
- **What is wrong:** the comment says none of the three products can wrap in
  64 bits. Meridians × 2 × segments can reach about 2⁶⁵, so a layout whose
  true count is 2⁶⁴ wraps to zero and is accepted. The counts are constants
  today; the comment says they "will one day come from a quality setting or a
  scenario".
- **Evidence, reproduced** at compile time: with 2³¹ - 2 meridians and 2³¹
  segments per quarter circle, `static_assert(GridLayout::from(k).has_value())`
  and `static_assert(... vertexCount() == VertexCount{0U})` both hold.
- **Fix:** bound each factor, or multiply with an overflow check, and add the
  case to the refusal test. Small.

### 2.6 A camera path between enormous times trips an assertion — hidden risk, low

- **Where:** `view/CameraPath.cpp`, `CameraPath::poseAt`.
- **Evidence, reproduced:** keyframes at -1.7e308 s and +1.7e308 s are
  accepted; `poseAt` then divides infinity by infinity and stops on
  `ORBSIM_ENSURES(along.has_value())`.
- **Fix:** refuse a span whose length overflows, in `from()`. Small.

### 2.7 A "suboptimal" image never marks the swapchain for rebuilding — hidden risk

- **Where:** `render/VulkanContext.cpp`, `acquireImage`.
- **What is wrong:** the comment says the rebuild happens after the frame is
  presented, but nothing records the suboptimal acquire; the rebuild happens
  only if presenting reports it too.
- **Evidence, by reading:** `swapchainDirty_` is set only from the present
  result.
- **Fix:** set it on a suboptimal acquire. One line.

### 2.8 Push-constant stages are typed twice by hand — hidden risk

- **Where:** `render/Probes.cpp`, `createClearScene` and `createLambertScene`;
  `render/ResolvePass.cpp`, `ResolvePass::record`.
- **What is wrong:** the stage flags given to `vkCmdPushConstants` are written
  out again rather than taken from the range's `stages()`.
  `render/LineRenderer.cpp` guards the same thing with a `static_assert`;
  these three sites have none, so a range whose stages changed would
  disagree, and only the validation layer would see it.
- **Fix:** derive the flags from the range, or add the same `static_assert`
  beside each. Small.

### 2.9 A failed swapchain rebuild leaves the context half rebuilt — hidden risk

- **Where:** `render/VulkanContext.cpp`, `createSwapchain` after
  `releaseSizedResources`.
- **What is wrong:** on a failure part way, the views, semaphores and
  attachments are gone while the old images are still listed. Every caller
  exits on the error today; a caller that retried would use dangling state.
- **Evidence:** by reading.
- **Fix:** clear the image list and mark the swapchain for rebuilding on
  failure, or state that the context is unusable after an error. Small.

### 2.10 Semaphores destroyed after a device wait — hidden risk, not reproduced

- **Where:** `render/VulkanContext.cpp`, `recreateSwapchain`.
- **What is wrong:** the present-wait semaphores and the old swapchain are
  destroyed after `vkDeviceWaitIdle`, which waits for queue work but is not
  guaranteed to cover the presentation engine's wait. This is a known gap in
  Vulkan itself; the extension `VK_EXT_swapchain_maintenance1` exists to close
  it, and the validation layer does not report it.
- **Evidence:** by reading; not reproduced, and probably not reproducible on
  demand.
- **Fix, a choice:** (a) record it as a known limitation; (b) use the
  extension where the driver offers it. Medium for (b).

### 2.11 Two functions report failure as a `bool` and print the reason — style

- **Where:** `app/ProbeMode.cpp`, `writeEncoded` and `writeFrameFiles`.
- **What is wrong:** non-negotiable 3 asks for `std::expected`, never a
  `bool` plus a side effect. The sidecar records only "a file failed", not
  which or why.
- **Fix:** return `std::expected<void, std::string>` and carry the reason into
  the sidecar. Small.

### 2.12 Small ones — style and hidden risk, low

- **`view/GpuClock.hpp`, `nanosecondsPerTick`**: a bare `f64` across an
  interface (non-negotiable 1), with no reason given. Style.
- **`view/ProbeSidecar.cpp`, `formatInstant`**: `{:06.3f}` prints 59.9996 s
  as "60.000". The probes' epoch never does this. Hidden risk, cosmetic.
- **`render/Probes.hpp`, `Probe::conditions`**: a function pointer with no
  `{}` initialiser, against guideline section 6 (cppcheck). Style.

---

## Group 3 — tests that cannot see what they claim

Each of 3.1-3.5 was shown by **planting the fault in the worktree and running
the real suite**: exit 0 means the suite did not notice.

### 3.1 The anomaly converters are tested only against each other — defect (in the test)

- **Where:** `tests/test_orbit.cpp`, "Kepler equation solver";
  `tests/test_orbit_scales.cpp`, `checkOneEccentricity`. The code is
  `orbit/Orbit.cpp`, `trueToEccentricAnomaly` and `eccentricToTrueAnomaly`.
- **What is wrong:** both are checked only by round trips through each other
  -- the "operation applied twice cancels its own fault" trap of M1-09.
- **Evidence, reproduced:** swapping √(1 - e) and √(1 + e) in both functions
  leaves `test_orbit`, `test_orbit_scales` and `test_orbit_elements` passing
  (exit 0 each). Against the textbook half-angle formula,
  tan(E/2) = √((1-e)/(1+e)) tan(ν/2), the planted fault is 3.05 rad out; the
  real code is 8.9e-16 out.
- **Fix:** add that formula as an independent check, and a published worked
  example for the elliptic and hyperbolic forms. About 30 lines.

### 3.2 The back-lit patch test cannot tell "drawn black" from "not drawn" — defect (in the test)

- **Where:** `tests/test_radiometry.cpp`, `firstNotDrawnBlack` and "the patch
  lit from behind reads back no light at all, and is drawn".
- **What is wrong:** it says the alpha proves the patch was drawn, because
  "the clear writes alpha 0". The clear writes alpha 1
  (`view/SceneClear.hpp`, held by a `static_assert`), so an undrawn frame and
  a patch drawn black are the same four numbers.
- **Evidence, reproduced on the RTX A2000:** with every probe draw given zero
  vertices, `probe_lambert-backlit` and the test case both pass.
- **What still stands:** the survivor this test closed in `87af028`, "the
  shader lights a surface from behind", is still caught -- without the clamp
  the red, green and blue would be about -130, and the test checks them
  exactly. Only the "and is drawn" half is empty.
- **Fix, a choice:** (a) draw the patch smaller than the frame and require a
  lit edge against an unlit border; (b) compare with a front-lit run of the
  same geometry; (c) drop "and is drawn" from the name and the comment.
  Small to medium.

### 3.3 The EXR writer is tested on one chunk only — hidden risk

- **Where:** `tests/test_image_files.cpp`, "an EXR holds the HDR target's
  bits..."; the code is `view/ImageFiles.cpp`, `writeExrPixels`.
- **What is wrong:** the image is 7 × 3, and the compression packs 16 rows a
  chunk, so the offset for every later chunk is never exercised. The probe's
  720-row EXR is only checked to exist.
- **Evidence, reproduced:** writing every chunk from row 0 leaves
  `test_image_files` passing.
- **Fix:** a case taller than 16 rows and not a multiple of 16, read back
  pixel for pixel. Small.

### 3.4 "Never moves the storage" takes the pointer too late — hidden risk

- **Where:** `tests/test_line_batch.cpp`, "filling, clearing and refilling
  never moves the storage".
- **Evidence, reproduced:** deleting the constructor's `reserve` leaves the
  suite passing: the pointer is taken after the first fill.
- **Fix:** take the pointer before the first fill. One line.

### 3.5 Two of the camera controller's refusals are never asked for — hidden risk

- **Where:** `tests/test_camera_controller.cpp`; the code is
  `view/CameraController.cpp`, `isFiniteStart`.
- **Evidence, reproduced:** removing the right ascension's check, and
  separately the tilt's, each leaves the suite passing.
- **And:** "replaying the same commands gives the same pose" promises "once
  from a copy taken half way", takes no copy, and compares a pose with itself.
- **Fix:** NaN cases for both angles, and a real copy at the half-way step.
  Small.

### 3.6 Worst-error sweeps drop a NaN — hidden risk

- **Where:** sweeps that keep `std::max` of their errors and assert on the
  maximum. With no other finiteness check: `tests/test_view_math.cpp`,
  `measureRoundTripUlps`; `tests/test_projection.cpp`, the frustum sweep;
  `tests/test_camera.cpp`, the displacement round trip; two residual cases in
  `tests/test_planetary_grid.cpp`; and `tests/test_orbit.cpp`'s elliptic
  Kepler loop. Others have a separate check that limits the damage.
- **What is wrong:** `std::max(worst, NaN)` returns `worst`, and `NaN > tol`
  is false, so a function that returned NaN would make the worst error
  *smaller*.
- **Evidence:** by reading -- `measureRoundTripUlps` is
  `worstUlps = std::max(worstUlps, residual / ...)` with no other check of
  `residual`.
- **Fix:** one shared helper that keeps a NaN as the worst value. About
  20 sites.

### 3.7 The determinism test passes if every step fails, and skips one element — hidden risk

- **Where:** `tests/test_orbit_scales.cpp`, "propagation is bit-identical
  across runs" -- the case `VERIFICATION.md` cites as the whole of rule 16.
- **What is wrong:** a failing step sets the state to zero and stops, so two
  runs that both fail every step compare equal; `elementsIdentical` compares
  six of `Elements`' seven fields, leaving out `slr`; both "runs" are in one
  process.
- **Fix:** require each step to succeed, add `slr`, and rename to "within a
  run" or add a second process. Trivial.

### 3.8 The sweeps draw differently on Windows and Linux — inconsistency

- **Where:** `tests/OrbitSweepSupport.hpp`'s `Sampler`, and the samplers in
  `tests/test_double_double.cpp`, `tests/test_view_math.cpp`,
  `tests/test_camera.cpp` and `tests/test_projection.cpp`.
- **What is wrong:** they use the standard library's distributions, whose
  algorithms the standard leaves to each library. `TimeTestSupport.hpp` and
  `test_orbit_elements.cpp` already say so and read the engine directly. A
  seed then does not reproduce a gcc failure on Windows (rule 12), and
  several sweeps assert after the loop, so a failure cannot name its case.
- **Evidence, measured by the helper** with clang on Windows and gcc 14 under
  WSL: seed 20260905's first draws differ in the last digits, and
  `normal_distribution` returns its pair in the opposite order.
- **Fix:** read the engine directly and carry the worst case's parameters to
  the assertion. Small, but it changes the cases drawn, which is the owner's
  call.

### 3.9 The grid test checks three points, not the grid — inconsistency

- **Where:** `tests/test_probe_grid.cpp`, "grid-400km: the equator, the prime
  meridian and the grid land where Skyfield puts them".
- **What is wrong:** it samples the crossing, the equator at 15° E and the
  prime meridian at 10° N -- no grey line. A wrong grid spacing would be seen
  only by the per-card golden.
- **Fix:** sample a grey parallel away from the coloured lines, or rename the
  case. Small.

### 3.10 The 60-digit orbit references have no committed generator — inconsistency

- **Where:** `tests/test_orbit_scales.cpp` and `tests/test_orbit_elements.cpp`
  cite a `reference.py` in a scratch folder; no such script is in git. The
  second's "as the goldens above use" points at nothing since the suite was
  split.
- **Fix:** commit the generator under `scripts/` if it survives on a machine,
  as the view's references are. Small.

### 3.11 Assertions that cannot fail, names that claim more — inconsistency

| Where | What |
|---|---|
| `tests/test_math.cpp`, slerp "turns at a steady rate" | `REQUIRE(whole <= pi)` holds by construction |
| `tests/test_earth_orientation.cpp`, "the composition is CIO-consistent" | the 1 mas spin bound follows from the 0.1 mas budget on the same rows |
| `tests/test_units_validated.cpp`, "no two unit errors describe the same thing" | 5 of 6 pairs |
| `tests/test_projection.cpp`, "each error says something different" | 2 of 3 pairs |
| `tests/test_benchmark_path.cpp`, "the path is the same on every build" | two calls in one process |
| `tests/test_benchmark_path.cpp`, `lowestRadius` | "ten points" samples nine |
| `tests/test_tonemap_port.cpp`, header | "every probe's frame is checked"; the jitter frames are not |
| `tests/test_orbit.cpp`, "failures are reported" | the centre's refusal not asked for by name |
| `tests/test_orbit_scales.cpp` | "which is refused today" on cases that must succeed; "five orbits" for four; a success not checked finite |
| `tests/test_earth_orientation.cpp`, the pole of date | a floating-point `==` instead of `bitsOf` (one of the five in 4.1) |
| `tests/test_probe_clear.cpp`, `tests/test_radiometry.cpp`, `tests/test_probe_lines.cpp`, `tests/test_probe_grid.cpp` | `halfNeighbours` and `halfStep` copied into all four (style) |

**Fix:** reword or tighten each. Trivial each.

### 3.12 The fuzzers do not reach three entry points, and check only for NaN — hidden risk

- **Where:** `tests/fuzz_time.cpp`, `tests/fuzz_orbit.cpp`.
- **What is wrong:** `fuzz_time` never adds or subtracts fuzzed durations on a
  `TimePoint`; `fuzz_orbit` hands `stateFromElements`, `propagateElements` and
  `orbitInfo` only element sets `elementsFromState` produced, never arbitrary
  ones; and it checks only that results are not NaN, so it could not have
  found 1.1 (2π) or 1.2 (an infinite period).
- **Evidence:** by reading both harnesses.
- **Fix:** about 20 lines each, and the range checks 1.1 and 1.2 add.

---

## Group 4 — the checking tools and the build

### 4.1 mp-units switches `-Wfloat-equal` off for the rest of every file — defect

- **Where:** mp-units v2.5.0, `framework/quantity.h`, `operator==`, reaching
  every file that includes `core/Units.hpp`, `core/Math.hpp` or mp-units
  itself -- almost all of `src/` and `tests/`.
- **What is wrong:** the operator opens with `MP_UNITS_DIAGNOSTIC_PUSH`, turns
  `-Wfloat-equal` off, and closes with a second `PUSH` where a `POP` belongs.
  So the warning stays off for the rest of the file. The project relies on
  `-Wfloat-equal` (CODING_GUIDELINES section 11, ADR 0017); in these files it
  has been checking nothing -- `VERIFICATION.md` rule 23's "a check that
  silently stops checking".
- **Evidence, reproduced** with the Debug tree's own flags:
  - `bool h(double a, double b) { return a == b; }` is reported after
    `core/Scalar.hpp`, after `<cmath>` and after nothing at all; it is **not**
    reported after `<mp-units/framework.h>`, `core/Units.hpp` or
    `core/Math.hpp`;
  - `quantity.h` is the only mp-units header whose pushes and pops differ
    (2 and 0);
  - with a scratch copy of the headers corrected to `POP`, all 82 project
    files re-checked: **five hidden comparisons** --
    a `static_assert` in `core/Units.hpp` comparing `eccentricity(0.7306)`;
    a `REQUIRE` in `tests/test_earth_orientation.cpp`; a `static_assert` and
    `fixtureMatrixAtEpoch` in `tests/test_planetary_grid.cpp`; `fixtureMatrix`
    in `tests/test_probe_grid.cpp`. The same run with the original headers
    finds none.
- **Fix, a choice for the owner:**
  (a) **recommended**: report the bug upstream, and meanwhile re-enable the
  warning after the mp-units includes in `core/Units.hpp`
  (`#pragma clang diagnostic warning "-Wfloat-equal"` and gcc's equivalent),
  with the reason written there; then fix the five sites;
  (b) patch the fetched copy at configure time, which edits a dependency;
  (c) a newer mp-units, if it fixes it -- not yet checked.
  Small, plus the ruling.

### 4.2 The mutation harness counts any compile error mentioning `static_assert` as a kill — defect

- **Where:** `scripts/mutate.py`, `static_assert_message`, used by
  `run_mutant`.
- **What is wrong:** a mutant that does not compile is meant to be INVALID
  unless a static assertion fired. The function accepts any line containing
  "static_assert", and clang repeats the source line in its notes -- so a
  mutant that breaks a `constexpr` function some `static_assert` uses counts
  as caught.
- **Evidence:** reproduced by the helper with clang 23 (an implicit-conversion
  error whose note echoes a `static_assert` line), and read in the code.
  **Nothing on record is wrong:** all 76 `CAUGHT (static_assert)` verdicts in
  the pass logs under `build/` quote a genuine "static assertion failed".
- **Fix:** accept only the compiler's own static-assertion error lines, with
  a self-test case built from that output. About 30 minutes.

### 4.3 The pre-commit hook checks the files on disk, not what is committed — defect

- **Where:** `scripts/git-hooks/pre-commit`.
- **Evidence, reproduced** in a scratch repository with the project's
  `.clang-format`: an unformatted file staged, then formatted on disk without
  staging -- the hook exits 0 and the unformatted text would be committed.
  The control, unformatted on disk too, exits 1. The file list is also
  unquoted, so a path with a space breaks it.
- **Fix:** check each staged copy (`git show ":$f" | clang-format
  --assume-filename="$f" ...`) in a loop that handles any name. About
  30 minutes.

### 4.4 Two suites are left out of the abort-listener check — defect

- **Where:** `cmake/ScriptTests.cmake`, the `abort_listener` test.
- **What is wrong:** it asks every Catch2 program whether the listener that
  stops Windows' assertion dialog is linked, but its list leaves out
  `test_probe_lines` and `test_probe_grid`, which link it and are not in
  `ORBSIM_TESTS`.
- **Fix:** add them, or build the list from one variable of the
  application's suites. 15-45 minutes.

### 4.5 A renamed CTest judge makes a mutant "survive" silently — hidden risk

- **Where:** `scripts/mutate.py`, `run_mutant`; `--verify`;
  `scripts/mutants-due.py`, `judge_inputs`.
- **Evidence, measured:** `ctest -R '^no_such_test$'` exits 0 with both CMake
  4.3.1 and 3.31.2; with `--no-tests=error` it exits 8. `--verify` does not
  check that a judge's name exists, and `mutants-due.py` turns an unknown
  name into an input that can never change. 22 declared survivors are judged
  by CTest names only. All 78 names in use exist today.
- **Fix:** `--no-tests=error`, a name check in `--verify`, and an unknown name
  counted as "due". 1-2 hours with self-tests.

### 4.6 The fixture checksums pass with nothing to check — hidden risk

- **Where:** `cmake/VerifyFixtureChecksums.cmake`.
- **Evidence, reproduced:** an empty `checksums.sha256` prints "All 0
  fixture(s) match" and exits 0 -- against the script's own "three outcomes,
  and none of them silent". The control, a wrong hash for a present file,
  exits 1.
- **Fix:** fail when no line was read. 10 minutes.

### 4.7 The document-link checker's blind spots — hidden risk

- **Where:** `scripts/check-doc-links.py`, `check_code_paths` and
  `self_test`.
- **Evidence, reproduced** through the script's own `scan`: a path in the
  wrong case (`src/core/units.hpp` for `Units.hpp`) passes on Windows, and a
  misspelt bare file name with no folder passes everywhere; a wrong path with
  a folder is caught (the control). The self-test covers no anchor, no ADR
  number, no `file:line` citation and no fenced block, and no mutant aims at
  them.
- **Fix:** compare names with their real case, check bare names against the
  repository's root, and add self-test cases and mutants for the four
  untested rules. 1-2 hours.

### 4.8 A mutant file's own edits do not make it due — hidden risk

- **Where:** `scripts/mutants-due.py`, `judge_inputs`; `scripts/mutate.py`,
  `record_pass`.
- **What is wrong:** the inputs of a mutant file are the mutated files, the
  judges' builds, shaders and data -- not the mutant file itself. Changing a
  mutant's text or its expected verdict after its pass leaves the file
  "current".
- **Evidence:** by reading `judge_inputs`. No file has changed since its pass
  today.
- **Fix:** count the spec as its own input. About 30 minutes.

### 4.9 The link-graph check is partial — hidden risk

- **Where:** `CMakeLists.txt`, the check after `orbsim_view`.
- **What is wrong:** it runs half way down the file, looks only for
  `orbsim_view` among `orbsim_core`'s links, and nothing checks that
  `orbsim_view` links no Vulkan or SDL. Correct today.
- **Fix:** move it to the end and refuse every graphics or window target,
  recursively, for both libraries. About an hour.

### 4.10 The build's smaller gaps — inconsistency

| Where | What |
|---|---|
| `CMakeLists.txt`, the header self-checks | `app/BenchMode.hpp` is the only header under `src/` not listed |
| `CMakeLists.txt`, `broken_goldens` | not labelled `gpu`, so `ctest -LE gpu` still lists `probe_clear`, a GPU test -- measured |
| `scripts/mutate.py`, `run_mutant` | runs the `ctest` on PATH (3.31.2), where the trees use CLion's 4.3.1; `mutants-due.py` already takes the tree's; and `.exe` is assumed |
| `docs/PROJECT_STATE.md` section 8 | says the Windows trees use the shared source cache; on this machine every `FETCHCONTENT_SOURCE_DIR_*` is empty. And only ERFA's fetched version is checked against its pin |
| `scripts/measure-frame-cost.py` | decodes output in the machine's code page -- the crash `mutate.py` records from 2026-09-26 |
| `CMakeLists.txt`, `ORBSIM_SANITIZE_UNDEFINED` | the error still gives the reason its comment says "did not survive measurement" |
| `CMakePresets.json` | `windows-msvc` says `/W4 /WX`, the build uses `/Wall`; minimum CMake 3.24 against `CMakeLists.txt`'s 3.28 |
| `CMakeLists.txt`, above the definition of done | "every header is a dependency of every stamp" -- not since M1-89 |
| `cmake/ScriptTests.cmake`; `cmake/VerifyMissingShader.cmake` | "the four that read the tree" (eight now); `kExitFailure` "in `src/app/main.cpp`" (it is in `app/ExitCodes.hpp`) |
| `cmake/VerifyCountWraparound.cmake`; `tests/AbortBehaviour.cpp` | describe one subtraction probe, now three; point at a probe file that no longer exists |
| `scripts/check-tonemap-constants.py`, `numbers()` | "strip the float suffix" -- it strips nothing |
| `.claude/rules/cpp-style.md`, the narrowing audit | `grep static_cast<f32>` cannot see `static_cast<float>`, which `render/VulkanContext.cpp`'s `setFullViewport` uses (integer to float, harmless) -- the instrument has a blind spot. Measured: 14 casts on 19 lines, as stated |

**Fix:** each is minutes.

### 4.11 Guideline 6 is not checked for plain structs — style

- **Where:** CODING_GUIDELINES section 6 asks for every member to be
  initialised. clang-tidy's member-initialisation check does not look at
  plain structs; cppcheck does, and found **17 members without `{}`**:
  `Probe::conditions` (2.12), and 16 in test structs --
  `tests/test_astro_time.cpp`, `tests/test_orbit_elements.cpp`,
  `tests/test_fixture_file.cpp`, `tests/test_time.cpp`,
  `tests/test_time_leap.cpp`, `tests/test_time_ut1.cpp` and `Stamp` in
  `tests/TimeTestSupport.hpp`.
- **Fix:** add `{}` to each (minutes), and decide whether cppcheck should
  join the toolbox for this check (see "The two analysis tools").

---

## Group 5 — the declared mutation survivors

**The count is not one number.** `STATUS.md` says 22, M1-116's plan 24, and
the mutant files hold **25** with a verdict other than "caught" -- 24 if the
per-card m1-19 entry is left out. Finding 5.0, inconsistency: one count, in
`STATUS.md`, measured.

What each survivor's status is now. "Run" means re-run in the worktree today.

| File | Mutant | Now |
|---|---|---|
| `m1-09` | the perspective divide multiplies by w | **caught by `test_projection`** -- run: exit 42. Its file already says M1-10 caught it; only its judges are stale. **Proposed: add the judge, record as caught** |
| `m1-19` | the line pipeline writes depth | **caught on the RTX A2000** -- run: "grid-400km: the horizon lands where the sphere's limb is" and `probe_grid-400km_golden` fail. Both postdate M1-19. **Proposed: add them as judges, record per card**; the RX 7900 XTX still to run |
| `m1-107` | staging copy never made visible | still not catchable; **its reason is stale** -- "nothing calls `uploadBuffer` before M1-18" is no longer true (the lambert probes do). Proposed: update the reason and add the lambert judges |
| `m1-111` | a refused summary write not reported | catchable at some cost (a folder with the report's name, or an injected clock) -- against decision 420's "no test can make the disk refuse". **The owner's call** |
| `m1-109` (3) | the due-list harness | catchable by an end-to-end self-test on synthetic records. Moderate cost. **The owner's call** |
| `m1-19` | the vertex shader ignores the tint | equivalent while every tint is white; a probe with one coloured tint would catch it. Small. **The owner's call** |
| `m1-23` | the bare scene draws grid lines | catchable if `orbsim` printed the scene it ran and `orbsim_smoke_bare` read it. Small |
| `m1-15` | no clamp before the 2.2 power | absorbed by the sRGB clamp on both cards (decision 262); candidate for the per-card form |
| `m1-111` | file left for its destructor to close | caught on Linux only, as recorded |
| `m1-107`, `m1-16` | flush / host visibility | hardware-dependent; not catchable here |
| `m1-14` (2), `m1-17` (2), `m1-22` (3) | rules never called; accept-golden; benchmark | not catchable under decisions 193, 233 and 5 |
| `m1-19` | extension not requested; Bresenham's rule | driver- and card-dependent, as declared |
| `m1-15` | floor before log2 | equivalent |
| `m1-95`, `m1-97` | the harness's own | by design |

---

## Group 6 — the worked example

`coding-guidelines-example/`, which `.claude/rules/cpp-style.md` tells
readers to copy.

### 6.1 A test reads `error()` without a guard that stops it — hidden risk

- **Where:** `coding-guidelines-example/tests/test_kepler.cpp`,
  `testFailuresAreReported`.
- **What is wrong:** `parabolic.error()` follows a `test::check` that does not
  stop the test, so if the solver ever accepted e = 1 it would read `error()`
  on a result holding a value -- the trap `VERIFICATION.md` rule 23 records,
  in the code the rules say to copy.
- **Fix:** one combined check, as the example's `test_orbit_path.cpp` already
  does. A few lines.

### 6.2 The example lags the project's rulings — inconsistency

- `Eccentricity` and `GravParam` accept any value there; ADR 0022 makes
  them validate themselves here. The deleted conversion operator is the wide
  form decision 138 narrowed.
- Its ADR 0002 and README describe a `VulkanContext` that no longer exists.
- Its `wrapToPi` returns -π for -π, against its own (-π, π].
- Its README and `test_kepler.cpp` each say "the one suppression"; there are
  four.
- **`.claude/rules/cpp-style.md` says copy the example for a test**, whose
  tests use a hand-written harness the project replaced with Catch2;
  `.claude/rules/physics-tests.md` says copy `tests/test_orbit_scales.cpp`.

**Fix, a choice for the owner:** (a) port the rulings to the example, about
100 lines; (b) record that the example stops at a named decision, and point
readers at the project's tests for test shape.

---

## Group 7 — documents and comments

Every item was confirmed by finding the quoted text and the code or document
that contradicts it. Most are minutes each.

### 7.1 Statements about the code that are no longer true — inconsistency

| Where | Says | Is |
|---|---|---|
| `docs/STATUS.md`, `src/view/` row; `view/FileWrite.hpp` | "the only place a file is written" | `view/ImageFiles.cpp`'s `writeExr` writes the EXR through OpenEXR, which ignores the close's result. The plan's own example |
| `CLAUDE.md`, the layout block; a CMake comment | "never `orbsim_render`" | no such target: the renderer is compiled into `orbsim` |
| `CLAUDE.md` non-negotiable 8, `.claude/rules/renderer.md`, `README.md` | `f32` "in one named function" | five, as `.claude/rules/cpp-style.md` says -- 14 casts, measured |
| `CLAUDE.md` non-negotiable 3 | "`Eccentricity` and `GravParam` are the two" | `Fraction` is a third |
| `CLAUDE.md`, the layout block | `src/core/` "no dependencies", `src/view/` "core only" | true of the project's own folders only: the core links mp-units and ERFA, the view lodepng, OpenEXR and stb (style) |
| `docs/STATUS.md` | "Nothing is drawn yet"; "six GLSL shaders"; "the four scene shaders ... draw nothing"; the "Then" row; "160 CTest entries", "lists 241", "run 209"; "Twenty-five" ADRs; "Last updated: 2026-10-07" | lines, grid, lambert and the resolve pass draw; ten shaders; the "Then" row is done; 26 ADRs |
| `docs/STATUS.md`, the seeds paragraph | lists every suite's seed | five missing: `test_camera`, `test_camera_controller`, `test_double_double`, `test_orbit_elements`, `test_statistics` |
| `README.md`; `docs/plan/realism.md` section 2 | "Nothing is drawn yet"; "draws nothing at all today", "LDR" | as above; the HDR target and AgX exist since M1-14 and M1-15 |
| `CODING_GUIDELINES.md` | synchronization validation "not on by default"; `VulkanContext::init()`, "29 lines"; `HostVisibleCached` | on since M1-14; `create`, about 40 lines; `Memory::HostReadback` |
| `docs/PROJECT_STATE.md` section 2 | "the licence boundaries are in `CLAUDE.md`" | `docs/ORBITER-REFERENCE.md` |
| `.claude/rules/renderer.md` | exit codes 1, 2, 3 | and 4, a golden mismatch |
| `README.md`; `docs/PROJECT_STATE.md` | "clang 17+" or "MSVC 19.36+" will do | never measured; the lint configuration needs clang-tidy 23 -- a hidden risk for a reader following it |
| `docs/VERIFICATION.md` rule 19 | one mutant file per task from M1-08 | M1-91, M1-93 and M1-94 have none and no reason |
| `docs/VERIFICATION.md` Part 4 | `testZeroTimeStep` | now "a zero time step is the identity on every conic" |
| `tests/test_orbit.cpp`, `tests/test_orbit_scales.cpp`, `tests/OrbitSweepSupport.hpp` | the two propagators "share no code" | they share one solver since 2026-09-12 |
| Comments in `src/` | `render/Probes.hpp` "four variants", "one of four", and that only the lambert scene may change the context (the lines and grid scenes do too); `app/main.cpp`'s "current milestone"; `view/SceneClear.hpp` "three" narrowing functions; `view/ImageFiles.cpp` the macro "lives in src/render"; `view/Frame.hpp` "`EarthFixed` arrives with the grid"; `view/LineBatch.hpp` "no arithmetic"; `shaders/line.vert` "scaled"; `astro/Sun.hpp` 0.53″ for 0.53°; `core/LeapSeconds.cpp` an index that is gone; `app/ProbeMode.hpp` leaves out the usage exit; two misplaced paragraphs in `render/VulkanContext.hpp` and `app/main.cpp` | each read against the code |
| Comments in `tests/` | `tests/FixtureFile.hpp` "not in the repository" (the Skyfield fixtures are); `tests/OrbitTestSupport.hpp`'s scale floor "only ever" at zero; `tests/test_fixture_file.cpp` a misplaced block; `tests/test_orbit_elements.cpp` "to a thousand" (1e6); the reason given for avoiding `REQUIRE_FALSE`, against 26 uses | each read against the code |
| `tests/test_camera.cpp` and `core/Math.hpp` | the quaternion-to-matrix coupling: "eight to one", 270 ulp; "about 10 e", 320 ulp | one number, in one place |

### 7.2 Counts kept outside STATUS.md — inconsistency

`CLAUDE.md` "114 tasks"; `docs/PROJECT_STATE.md` "114 tasks" and "the
twenty-six rulings"; `docs/plan/milestone-1-earth.md` "114 tasks";
`docs/plan/milestone-1-tasks.md` "116 tasks" and **"the 96 documents"** -- the
stale count this review was planned after, still there;
`docs/plan/milestones.md` "eight standing rules ... 106 task documents" (ten
and 116); `CODING_GUIDELINES.md` and `THIRD_PARTY.md` "eleven dependencies";
`render/Probes.hpp` "twenty probes"; `tests/test_sun.cpp` "seventy-one"
suppressions (69). **Fix:** remove each count or point at `STATUS.md`; and the
router's "the only place any of them lives" should say "toolchain versions
and counts", since `THIRD_PARTY.md` rightly holds the pins.

### 7.3 Documents that contradict each other — inconsistency

| Where | What |
|---|---|
| `CLAUDE.md`, `README.md`, `CODING_GUIDELINES.md`, `docs/VERIFICATION.md`, `docs/PROJECT_STATE.md` | `asan` and `windows-msvc` "before a milestone lands" -- the queue and ADR 0005 run them **at every phase gate**, and M1-23 did. `CLAUDE.md` also leaves out the two fuzz presets |
| `docs/plan/tasks/m1-38-phase-b-gate.md`, `m1-85-phase-f-gate.md`, `m1-84-milestone-gate.md` | their prerequisites leave out M1-111 to M1-116 (phase B), M1-114 (phase F) and M1-87 onward (the milestone) -- **a hidden risk: a gate could close with a phase task open** |
| `docs/plan/tasks/m1-38-phase-b-gate.md` | its licence list leaves out lodepng, OpenEXR, Imath and the two libraries OpenEXR carries |
| `docs/PROJECT_STATE.md` section 2, against the phase B gate and M1-23 | where the fuzzers run: three documents, three answers |
| `docs/PROJECT_STATE.md` section 5 | "the list of ADRs is in `STATUS.md`" -- `STATUS.md` says `docs/adr/README.md` |
| `docs/STATUS.md`, "Next task" | "then M1-112 to M1-114" -- M1-114 is in phase F, after M1-76 |
| Finished task documents | M1-18 "the mutation pass is unfinished" (closed 2026-10-03); M1-19 and M1-20 "the RTX A2000's golden waits" (committed in `335f4be`); M1-110 "check waits for the owner" |
| `docs/plan/verification-cost-proposals.md` | "nothing here is decided" -- decision 207 and ADR 0024 adopted seven |
| `docs/plan/milestone-1-decisions.md`, header and section 8 | the header lists decisions to 206; section 8 "maps every decision" and stops at 114; "two records belong to no decision" lists three; ADRs 0019-0022 and 0024-0026 unmapped |
| `docs/PROJECT_STATE.md` sections 6.3 and 6.4 | "still my call", "still unreviewed", against `STATUS.md`'s "nothing else stands open" -- **a question for the owner: settled, or open?** |
| `docs/plan/milestone-1-earth.md` | "Status: planned" -- it is under way |
| `README.md`, Building and Acknowledgements | eight dependencies -- stb, lodepng, OpenEXR and Imath missing |
| `CLAUDE.md`, Attribution | its example names Claude Opus 5; the latest commits name 5.5 |

### 7.4 Not a finding, but to know

- **`prompts.txt`** holds the owner's session-opening prompts, kept on purpose
  (`4bec697`). Nothing links to it and it has no header saying what it is.
  Proposed: a one-line header.
- M1-116's own document says a task document "is not" checked by
  `doc-links`; in fact only backticked paths are exempt in one.

---

## Group 8 — STATUS.md's open items

What each of (a) to (e) is today, by reading. None has changed since the gate.

| Item | Today | Proposed |
|---|---|---|
| (a) a run without a sidecar counts every shader | as stated (decision 394) | stays declared |
| (b) the 400 km jitter sequence cannot fail for the narrowing defect | as stated; the 1 km sequence catches it | stays declared |
| (c) `fuzz_time`'s coverage varies between runs | as stated | stays declared |
| (d) `configure-current` waits for started compiles | as stated, 27 s | stays declared |
| (e) the RX 7900 XTX has not run the gate's pass | still true; group 5's m1-19 depth result needs it too | the second machine, at the next opportunity |

---

## The two analysis tools

Run once each, as a measurement, on Windows (decisions 436 and 437), on the
Debug tree's 82 translation units.

**cppcheck 2.22.0**, `--std=c++23 --enable=all --inconclusive
--check-level=exhaustive`: 13 seconds; 71 messages on the project and one on
the worked example. What they became:

- **two false alarms**, both shown to be cppcheck's: it misreads a hex
  floating literal (`0x1p52`), flagging the bound in `core/Time.hpp`'s
  `roundHalfAwayFromZero` as always false, while the same bound in decimal
  passes and a truly impossible condition is flagged (the control); and it
  reads a deliberate report-only `REQUIRE` in `tests/test_tonemap_port.cpp`
  as always false;
- two more false alarms on `exposure` members, whose types have only private
  constructors and so cannot be left uninitialised;
- **finding 4.11** (17 members without `{}`), the one cppcheck found that
  nothing else here checks;
- the rest are style: 17 raw loops it would write as algorithms (guideline
  section 9 prefers algorithms), 9 names that shadow a function, 3 members
  returned by value, 2 parameters passed by value, 1 that could be `const`.
  **Proposed:** not findings one by one; the owner decides whether the
  guideline means them.

**include-what-you-use 0.27**, with a mapping file for Microsoft's library
headers written for the run: 3 minutes. No missing include breaks a build.

- **Five includes are unneeded**, each confirmed by searching the file for
  every name the header declares: `core/Attributes.hpp` in
  `render/LineRenderer.hpp`, `view/Mat4.hpp` in `view/CameraController.cpp`,
  `view/Projection.hpp` in `view/Camera.hpp`, and `core/Scalar.hpp` in
  `view/PlanetaryGrid.hpp` and `core/LeapSeconds.hpp` (the last supplies
  `<compare>`, which `core/LeapSeconds.hpp` then needs directly). Style;
  minutes.
- **About 60 standard headers are used without being included directly**,
  most of them `<expected>` reaching tests through project headers. Today
  every one arrives in time; it is the class of defect gcc 14 found twice.
  **A question for the owner:** whether "include what you use" becomes a
  rule, and if so, whether the tool joins the toolbox.
- Dropped as the tool's noise, each checked: 25 `<cstdlib> // for abs` in
  files that include `<cmath>`, which the standard says declares the
  floating `std::abs`; suggestions from inside the `assert` macro; and
  `<ios>` in files that use `std::ios`, which Microsoft declares in an
  internal header.

**Proposed:** neither tool joins `check` now. cppcheck's one real class (4.11)
can be fixed once; include-what-you-use waits on the question above.

---

## Leads dropped, and why

| Lead | Why dropped |
|---|---|
| cppcheck: `core/Time.hpp` "always false" | cppcheck misreads hex floats; shown with a control |
| cppcheck: `tests/test_tonemap_port.cpp` "always false" | the `REQUIRE` reports a failure found by the `if` around it, on purpose |
| cppcheck: two `exposure` members uninitialised | their types cannot be default-constructed |
| cppcheck: the worked example's "same expression on both sides" | `coding-guidelines-example/tests/test_units.cpp` checks on purpose that two equal angles are ordered neither way |
| include-what-you-use: `abs`, `assert`, `<ios>`, `char_traits`, `std::get` | the tool's attribution on Microsoft's library, each checked |
| `<stdlib.h>` in `app/main.cpp` | there on purpose, decision 155 |
| The parabolic tolerance judging its own band (`tests/test_orbit_elements.cpp`) | widenings of the band to 5e-12, 1e-11, 1e-10, 1e-8 and 1e-6 were each planted; `test_orbit_elements` caught every one |
| `orbitInfo` returning a negative radius | documented on purpose in `orbit/Orbit.hpp`, and ADR 0018 deferred an error channel |
| A finished task document's backticked paths unchecked | the paths found were historical, as intended |
| Duplicates | four whole leads, and parts of many more, were reported by two helpers; each is merged into one finding above |

---

## What the review cost

- **Wall-clock time:** the tools were built and tried on Windows before the
  task started, not timed. The review itself ran from 13:42 to about 14:40 on
  2026-10-10 -- the six helpers reading in parallel, between 10 and 21 minutes
  each, while the tools ran and the first leads were verified -- and the
  writing after it.
- **Helpers:** six, 2.72 million tokens between them -- the first figure of
  its kind this project has; there is nothing to compare it with yet.
- **Leads against findings:** 120 leads -- 112 from the helpers, 8 from the
  tools -- became 49 findings and a 25-row survivor table. Four whole leads
  were duplicates of another helper's (the shared-solver comments, the
  replay copy, the file-writing claim, the exit codes) and many more
  overlapped in part; four whole leads were dropped on verification (three
  of cppcheck's and the parabolic band), and parts of three more (see
  "Leads dropped"). **22 findings were reproduced here** by running
  something -- a program, a planted fault or the application -- two more
  (3.8, 4.2) rest on a helper's measurement checked against the code, and
  the rest were confirmed by reading. **No helper lead was wrong about what
  the code does**; what verification removed was tool noise and one
  prediction that a test could not catch something it does catch.
- **Planted faults:** 13 runs in the worktree -- 11 on the CPU suites, 2 on
  the GPU.

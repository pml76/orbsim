# M1-17 — Golden-image comparison

Phase: A | Status: **done, 2026-09-29** -- code, tests and the first golden, and the mutation pass run on the committed code
Prerequisites: M1-16
Decided by: [ADR 0008](../../adr/0008-renderer-verification.md)

**Every question went up before any code was written**, and the owner ruled
them the same day: decisions 228-239 of the
[register](../milestone-1-decisions.md). One more was found on the way,
measured, and confirmed by the owner before it was settled: lodepng's own
choice of colour type (decision 240). The owner's two looks and the first
golden are decision 241.

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

  *(Ruled 2026-09-29, decision 228: the box filter averages **in the
  display's linear light** -- each 8-bit value decoded by the sRGB standard,
  the four averaged, the average encoded and rounded to the nearest value --
  and where all four values are on the curve's straight segment, 0 to 10, in
  whole numbers with a tie rounding up. Decision 229: the two tolerances are
  measured over R, G and B, in whole numbers.)*
- **Exit code 4** for a golden mismatch, joining the existing scheme (1 failure,
  2 usage, 3 validation errors). A script can tell them apart without parsing.
  *(Decision 230: 4 only for a measured mismatch; a golden that cannot be read
  or compared is 1; and 1 outranks 3, which outranks 4.)*
- **On mismatch, write `<out>/<name>.diff.png`**: rendered, golden and an
  amplified absolute difference side by side, with the two measured numbers
  printed to stderr. The failure has to be *diagnosable from the artefacts*, not
  just reported. *(Decision 231: the difference times 32, capped; a diff
  image left by an earlier run is removed at the start of every run.)*
- **`--accept-golden`**, which writes the downsampled frame to the golden path.
  It exists so approving a frame is one command; it is **run by the owner, never
  by a script, and never from `check`**, and the help text says so.
  *(Decisions 232-234: it turns the validation layers on, writes only once
  the validation count is zero, through a temporary file; a CTest test refuses
  any test that could run it; and `--help` exists to say so.)*
- **`tests/golden/<probe>.png`**, about 150 KB each. The first one is `clear`.
  *(Measured: `clear`'s is 2,146 bytes. Its gradients compress well; a
  natural frame will be nearer the estimate.)*
- The downsample and both metrics live in `orbsim_view` as pure functions, so
  they are testable without a GPU.

## Out of scope

Perceptual metrics. Any automatic re-baselining. Goldens for LUT, geometry or
radiometry probes — those are numeric, and an image would be the weaker check.

## Tests

`tests/test_image_compare.cpp`, headless, in `orbsim_view`:

- **The box filter**: a 2×2 constant block downsamples to that constant exactly;
  a known 4×4 pattern gives the 2×2 average computed by hand in the test.
  *(With decision 228 the 4×4 averages come from
  `scripts/downsample-reference.py`, in 50-digit arithmetic; the constant
  block is checked for all 256 values.)*
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

- [x] `check` green in both trees -- 319 of 319 in each, 2026-09-29.
- [x] `tests/golden/clear.png` is committed and compared -- accepted by the
      owner (decision 241), compared by `probe_clear`.
- [x] A deliberately broken frame produces a diff image the owner can read --
      "I see vertical stripes in the third pic" (decision 241).
- [x] Both tolerances are shown by test to catch something the other misses --
      in `test_image_compare`, and end to end by `probe_golden_block` and
      `probe_golden_shift`.

## What was built

- **`src/view/ImageCompare.*`** -- `Rgb8Image`, validated at construction,
  and `GoldenImage`, a type of its own so a frame and a golden cannot be
  handed over the wrong way round; `averageInLinearLight` and
  `halveInLinearLight` (decision 228); `measureDifference`, the two
  tolerances in whole numbers, and `describeDifference` (decision 229);
  `differenceImage` (decision 231).
- **`src/view/ImageFiles.*`** -- `decodePng8`, which checks the header's bit
  depth and colour type from the PNG specification's layout before stb_image
  decodes, and refuses anything but 8-bit RGB by name (`PngReadError`); and
  `encodePng8` of an `Rgb8Image`. **`src/view/StbImageImpl.cpp`**, moved from
  `tests/`, compiled once as `orbsim_stb_image` for the application and the
  tests (decision 235).
- **`src/view/ProbeSidecar.*`** -- the golden's path, verdict and two
  measurements in `clear.txt` (decision 237).
- **`src/app/ProbeMode.*`**, **`src/app/main.cpp`**,
  **`src/app/ExitCodes.hpp`** -- `--golden`, `--accept-golden`, `--help`,
  exit code 4 and decision 230's order, the diff image, the stale one
  removed, and the golden written only after the validation count. The
  argument parser was split into `applySwitch`, `valueNeededBy` and
  `applyValue` when it grew past the lint's size and complexity limits; every
  message is unchanged.
- **Tests**: `test_image_compare` (16 cases); `test_image_files` (+8: the
  golden reader, and the colour-type regressions of decision 240);
  `test_probe_sidecar` (+2); and through `cmake/RunProbe.cmake` and
  `cmake/RunUsage.cmake`, `probe_clear` against the golden with a stale diff
  planted, `broken_goldens` (`tests/make_broken_goldens.cpp`),
  `probe_golden_block`, `_shift`, `_missing` and `_wrong_size`, three
  `usage_*` tests, and `accept_golden` with its self-test
  (`scripts/check-accept-golden.py`).

## What was measured before it was relied on

- **One golden serves every machine** (decision 238): `clear` rendered on this
  machine's Intel UHD Graphics and its RTX A2000 differ after the downsample
  by 1/255 at most and 0.020/255 on average; the two build trees on one GPU
  agree to the bit.
- **The downsample, twice**: every one of the 256 values survives a decode
  and an encode, at least 0.4999 of a step from a rounding boundary; 4,275 of
  `clear`'s 691,200 averages are exact ties, all on the straight segment,
  which is why that segment is computed in whole numbers; and an independent
  Python implementation of decision 228 produced the same 640x360 image as
  the C++, value for value -- and later the same as the golden the owner
  accepted.
- **The decoder reads another encoder's PNG**: the Python golden, written with
  its own zlib-and-struct encoder, was read by `decodePng8` and matched.
- **A planted fault is caught, and the diff image shows it**: the GPU's sRGB
  encode with exponent 2.2 (M1-14's declared survivor) -- largest 9/255, mean
  2.59/255, both over their limits.
- **The stale diff check has teeth**: with the removal taken out by hand,
  `probe_clear` fails with "left a clear.diff.png although no mismatch was
  expected".

## Found on the way

- **lodepng chose its own colour type** (decision 240): a 320x180 crop of
  `clear` came out of `encodePng8` as a palette PNG, and two colours as a
  1-bit one, so decision 191's "8-bit RGB" held only by the luck of the
  picture. Always RGB now; three regression tests; `clear.png` and
  `clear.16.png` byte-identical before and after.
- **`.claude/rules/cpp-style.md` was one suite short**: `test_probe_sidecar`
  has linked `orbsim_view` since M1-16. Added.
- **Two older mutants were re-anchored** -- M1-13's `--shader-dir` and
  M1-16's `--probe-out` -- to the lines the parser's split moved them to; the
  same mutation each.
- **`scripts/mutants-due.py` took any file in the build tree for a program on
  Windows**, because `os.access(path, os.X_OK)` is true there of any file that
  exists. The golden tests name `golden-broken/clear-block.png`, so once a
  test had written it, the script asked Ninja for its inputs and stopped --
  whether `mutants_due` passed depended on which test ran first. Seen in the
  MSVC tree; fixed as `is_program`, only an `.exe` on Windows and the execute
  bit elsewhere, with self-test cases seen failing on the old condition.

## The mutation pass

`scripts/mutants/m1-17.json`: **26 mutants, 24 caught, 2 survived as
declared, none invalid or hung** -- one by a `static_assert` before a test
ran. The two survivors were accepted by the owner (decision 242): the write
`--accept-golden` makes, which decision 233 forbids a test to run, and
decision 230's order of a validation error before a mismatch, which needs a
validation error planted in the product (decision 193).

**The first run found three mutants invalid**, and each was rewritten to
compile and run again: removing the colour-type refusal left
`kPngColourTypeRgb` unused, and pointing the sidecar's `golden.largest` line
at the mean left `formatLargestLine` unused -- both errors under this
project's warnings, so neither said anything about a test; and the guard's
Python mutant named nothing to build, which the harness needs. The colour
type is now defeated by `colourType > kPngColourTypeRgb + 4U`, which no PNG
colour type satisfies, and the sidecar's line prints the mean from inside its
own formatter.

**Every other mutant file was due** -- `CMakeLists.txt` changed -- and all 19
ran clean on the committed code, 2026-09-29, in 71 minutes (19:00 to 20:11).

**The survivors handed to this task, re-run with the golden as their judge**:
until now their only judge was `orbsim_smoke`, which cannot see a pixel, so
`probe_clear` -- which holds the frame against `tests/golden/clear.png` --
was added to five of them. Measured, not predicted: **three are caught** and
their declarations removed -- M1-14's 2.2 encode and resolve pass never
drawn, and M1-15's shader that skips the exposure. **M1-15's two clamps still
survive**: `clear` holds no negative light and nothing above AgX's white, so
neither clamp changes a pixel of it. They stay M1-18's, and a note in its
task document says what its check must contain to see them.

**A rerun was stopped once** by Claude Code for want of memory while the
session was idle, before it recorded anything; the owner asked for it to be
run again, and it was, one file at a time, with
`CMAKE_BUILD_PARALLEL_LEVEL=4`. `mutants-due.py` then listed nothing due.

## Other compilers

Run now rather than at M1-23's gate (decision 239), 2026-09-29.

- **gcc-14 found one thing in this task's code**: `-Wmissing-braces` on a
  `std::array` built from a braced list -- the call of
  `averageInLinearLight`, and the same in its test. Answered with
  `std::to_array`, as M1-16 answered it. `test_image_compare`,
  `test_image_files` and `test_probe_sidecar` then pass under gcc with the
  Windows counts, 345, 210 and 225.
- **`linux-sanitize`** builds everything and passes 299 of 303, with nothing
  reported by AddressSanitizer or UndefinedBehaviorSanitizer; the three suites
  pass with the same counts.
- **`windows-msvc`** builds the whole tree, renderer included, with no
  warning, and passes 313 of 318 before the fix above and 316 after it --
  every golden test among them, on the GPU.
- **What does not pass, and is older than this task** -- reported to the
  owner, not fixed here:
  - `linux-gcc` cannot build `test_orbit_elements`, `test_time_leap` and
    `test_time_ut1`: gcc's `-Wunused-const-variable=2` reports constants in
    `tests/OrbitSweepSupport.hpp` and `tests/TimeTestSupport.hpp` that a
    suite including them does not use. Both headers came with M1-94
    (2026-09-27); the Linux trees were left out of that day's measurement
    (decision 216) and have not been built since.
  - In both Linux trees, which build the core only, `mutants_due`,
    `mutants_due_shaders`, `mutants_due_scripts` and `parallel_tests` fail
    because they assume the application: one asks Ninja about `orbsim`, the
    other finds no GPU test. `abort_listener` fails under gcc only because of
    the three suites above.
  - In the MSVC tree, `configure_current` ("`check` does not wait for
    `configure-current`") and `memory_pool` ("no compiles of this project's
    own targets found") fail -- **measured at the last commit, 2ab62a8, in a
    separate checkout: the same two failures, the same words.**

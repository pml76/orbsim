# M1-108 — Four declared survivors made reachable

Phase: A | Status: **done, 2026-10-05**
Prerequisites: M1-18
Decided by: register decisions 273, 276 and 277 -- placed just before the phase A gate,
where the mutation pass next runs in full (277)

**Amended 2026-10-05, when the task ran.** Six questions were put before the
code and every recommendation was taken (register decisions 385-390); one
more arose during the work, a caught mutant whose line had moved, and was
ruled with the commit (390, 391). What they add is below, under "What was
done"; the text above it is as written.

## Purpose

**Four of the twelve mutants that survived the pass of 2026-10-03 survive
because a small rule sits where no test can reach it**, not because the
machine cannot produce the situation. Each rule is a few lines of plain logic
inside the application, or inside a function that needs Vulkan's headers,
which no test may include (ADR 0012):

| Mutant file | Survivor | The rule |
|---|---|---|
| `m1-14.json` | the HDR format check never reports | whether a format's feature bits include colour attachment and sampling |
| `m1-14.json` | the swapchain check accepts any colour space | whether a swapchain's colour space is sRGB's |
| `m1-17.json` | `--accept-golden` never puts the golden in place | write to a temporary file, then rename it over the golden |
| `m1-17.json` | a golden mismatch outranks a validation error | which exit code wins when both happen (register decision 230) |

The other eight are left as they are: two die by plan (M1-14's scene
pipelines at M1-19, M1-15's negative-light clamp at M1-18), one changes
nothing (M1-15's CPU floor), and five depend on how this machine's memory and
GPU behave, where only other hardware could see them (decision 273).

## What to do

- **The exit-code order** into a small function in `src/app/ExitCodes.hpp`,
  which needs no Vulkan, with compile-time checks beside it of every pairing
  decision 230 orders.
- **Write-then-rename** into a small function in `orbsim_view`, tested on a
  temporary folder by a suite that never runs `--accept-golden` -- which
  respects decision 233.
- **The two device rules** into small compile-time-checkable functions beside
  the code that calls them.
- Add a mutant per moved rule, aimed at the new function and expected
  caught, and keep the four old ones declared, as below.

**The limit, stated in advance**: this kills the mutants as written -- a
broken rule -- but not a mutant that disconnects the call to the rule. That
wiring stays unseen on this machine, so the blind spot shrinks rather than
disappears. **So the four mutants as they stand stay in their files, each
re-worded as removing the call and still declared a survivor, and new
mutants aimed at the moved rules are added beside them, expected caught**
(register decision 276): the remaining blind spot stays written down where a
machine reads it.

Every question the work raises goes to the owner before code, as for every
task (CLAUDE.md working agreement 1).

## What was done

- **The exit-code order** is `probeExit` in
  [`src/app/ExitCodes.hpp`](../../../src/app/ExitCodes.hpp): the probe's code
  and the validation count in, one struct so they cannot be transposed; the
  exit code and `GoldenWrite::Allowed` or `Withheld` out (decision 385). Eleven
  `static_assert`s cover every pairing, with a usage error ranked as a failure
  and a mismatch withholding the golden (386). `finishProbe` in `main.cpp`
  calls it.
- **Write-then-rename** is `view::replaceFile` in
  [`src/view/FileWrite.hpp`](../../../src/view/FileWrite.hpp), with
  `writeFile` moved beside it from `src/app/ProbeMode.cpp` (387), and
  [`tests/test_file_replace.cpp`](../../../tests/test_file_replace.cpp) holds
  it on a temporary folder: 39 assertions in 8 cases (388). **Renaming over an
  existing file replaces it on Windows -- measured by the suite, not
  assumed.** The suite was seen failing three ways by hand first. The first
  real use was the owner's approval of the RTX A2000's `lines` and
  `grid-400km` goldens the same day, `335f4be` (391).
- **The two device rules** are `hdrFormatLack`, all three of the HDR target's
  uses, and `isSrgbColourSpace`, `constexpr` beside their callers in
  `src/render/VulkanContext.cpp` with `static_assert`s on bits and colour
  spaces this machine never reports (389). One `static_assert` holds every
  entry of `kPresentableFormats` to the rule.
- **The mutants**: four in
  [`scripts/mutants/m1-108.json`](../../../scripts/mutants/m1-108.json),
  aimed at the moved rules, expected caught; the four old ones re-worded in
  `m1-14.json` and `m1-17.json` as the call removed, still declared (390).
  One *caught* mutant in `m1-14.json` was re-anchored, its line having moved
  into `hdrFormatLack` (390).
- `check` passes in both trees, **517 of 517**; no fuzzer was due, since
  nothing under `src/core/`, `src/orbit/` or `src/astro/` changed. A
  clang-analyzer finding raised inside MSVC's own `<filesystem>` by
  `std::filesystem::file_size` was avoided, not silenced: the test reads the
  file back instead.

## Done when

- [x] `check` green in both trees -- 517 of 517 in each, 2026-10-05, on the
      RTX A2000 machine.
- [x] The four new mutants caught, and the four old ones declared with their
      reason re-worded as the call they remove. Run at `67c6df2`, in
      `build/debug`: **`m1-108.json` 4 of 4 caught** -- the exit-code order,
      the HDR rule and the colour-space rule each by a `static_assert`, the
      rename by `test_file_replace`; **`m1-14.json` 15 caught and its 2
      declared survivors surviving**, the re-anchored mutant caught by
      `orbsim_smoke` as before; **`m1-17.json` 24 caught and its 2 declared
      survivors surviving**. None invalid or hung. **13 minutes 29 s for the
      three files**: 1 min 48 s, 5 min 21 s and 6 min 20 s.

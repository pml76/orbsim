# M1-112 — The probe display format's rule made reachable

Phase: B | Status: not started
Prerequisites: M1-108
Decided by: register decision 405 (b) -- carried out of the phase A gate as a
task; the shape is M1-108's (register decisions 385-390)

## Purpose

**`checkProbeDisplayFormat` in `src/render/VulkanContext.cpp` holds a rule no
test can reach**: whether a format's feature bits include rendering into it
and copying out of it. It sits in a function that needs Vulkan's headers,
which no test may include (ADR 0012), and no device here lacks the features,
so a mutant that broke the rule would survive. M1-108 moved the same shape out
of the HDR check (`hdrFormatLack`) and left this one, which was not among the
survivors then, recorded in `STATUS.md` (decision 389).

## What to do

- Move the rule into a `constexpr` function beside `hdrFormatLack`, returning
  which feature is missing, with `static_assert`s beside it over every
  combination of the two bits.
- `checkProbeDisplayFormat` calls it and keeps its message.

## Mutants

- The rule ignores the copy-out bit: caught by a `static_assert`.
- The rule ignores the render bit: caught by a `static_assert`.
- The call removed: declared surviving, re-worded as M1-108's four are, since
  no device here lacks the features.

## Done when

- [ ] The rule is checked at compile time.
- [ ] `check` passes in both trees.
- [ ] The task's mutant file has run after the commit and its record is
      committed.

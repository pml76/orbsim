# M1-108 — Four declared survivors made reachable

Phase: A | Status: not started
Prerequisites: M1-18
Decided by: register decision 273

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

## Done when

- [ ] `check` green in both trees.
- [ ] The four new mutants caught, and the four old ones declared with their
      reason re-worded as the call they remove.

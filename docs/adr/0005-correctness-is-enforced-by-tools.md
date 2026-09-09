# ADR 0005: Correctness is enforced by tools, not by remembering

Status: accepted (2026-09-05)

## Decision

"Done" has one definition, and a machine checks it:

```
cmake --build build/relwithdebinfo --target check
cmake --build build/debug --target check
```

`check` builds everything, runs `clang-format --dry-run --Werror` over every
source and header, runs `clang-tidy` over every translation unit with headers
included, and runs `ctest` -- including, on a machine with a GPU, a two-second
run of the application under the validation layers that exits non-zero on any
validation error. It is run in both trees because the assertions are only
live in Debug.

Around it, three smaller mechanisms, each catching the same class of mistake
earlier than the next:

| When | Mechanism | Catches |
|---|---|---|
| The moment a file is edited by Claude Code | `.claude/settings.json` hook: `clang-format -i` | formatting |
| The moment a commit is attempted | `scripts/git-hooks/pre-commit` | formatting of staged files |
| Before "done" is claimed | the `check` target, both trees | everything above plus tests, lint, validation |

**Everything runs locally. This project does not use CI**, by the owner's
decision on 2026-09-06. The `check` target is the whole of the enforcement,
and it is run by a person before they call something done.

## What we considered

**A checklist in CLAUDE.md.** What the project had. Every item was correct,
and clang-tidy had never linted a header, because the checklist said "run
clang-tidy" and nobody noticed that the configured filter matched nothing on
Windows. A checklist records intent; it cannot notice that a step has been
silently doing nothing for sixteen commits.

**`CMAKE_CXX_CLANG_TIDY`, so every compile is also a lint.** The strongest
form, and it roughly doubles compile time on every edit-compile cycle. The
`lint` target instead keeps one stamp per translation unit, so Ninja runs
clang-tidy in parallel and only over what changed, and it runs when `check`
runs rather than on every build. The trade is a few minutes of feedback delay
for a build loop that stays fast.

**A Stop hook that runs `check` after every Claude Code turn.** Would make it
impossible to end a turn with the tree broken. It would also run a multi-minute
job after a one-line comment fix. The formatting hook is cheap enough to be
unconditional; the rest is invoked by name.

**Running the GPU smoke test only by hand.** It existed as a flag
(`--seconds`) and was never registered as a test, so nothing ran it. Now it is
a CTest test carrying the `gpu` label, so a machine without a GPU can exclude
it; on a developer machine it runs with everything else.

**Continuous integration, and it was declined.** A workflow was written --
Windows clang, Linux clang with ASan and UBSan, Linux gcc 14 -- and the owner
decided against running CI on this project. That is a deliberate deviation
from the Toolbox's rule two, "the tool you do not run in CI is a tool you do
not have", and it has a real cost worth writing down: **UndefinedBehaviorSanitizer
and a second compiler are now unreachable.** *(Corrected on 2026-09-07 -- see
the update at the end of this record. Both turned out to be one command away.)*
UBSan's Windows support is
partial, so the `ORBSIM_SANITIZE_UNDEFINED` option refuses to configure there,
and gcc cannot build this project on this machine. Both remain available to
anyone who runs the `linux-sanitize` or `linux-gcc` preset on a Linux box by
hand; nothing runs them automatically.

The mitigation is that the local bar is high: `check` runs the full warning
set as errors, clang-tidy with headers included, both build types, and the
validation layers. AddressSanitizer is one preset away. What is lost is the
*second opinion* -- a different compiler and a different standard library
disagreeing with clang is where a certain class of bug shows itself, and this
project will not see that until someone builds it elsewhere.

## Why

Section 1 of the guidelines: the compiler is the cheapest reviewer.
Correctness should be a property of the build rather than of anyone's
diligence, and this record is that argument made concrete -- as far as a
local build can take it.

The evidence that motivated it: the first pass with a working header filter
found 64 lint findings in code that had been "zero findings" for its whole
life, the static analyzer found a dead store the syntactic checks cannot see,
and a propagator that passed 732 checks failed at 1 AU because no test had
ever flown further than Earth orbit. None of those was a failure of care. All
of them were failures of a tool not being run, or a test not being written,
which is what this decision makes harder to repeat.

## Update, 2026-09-07: the cost was smaller than recorded

**The decision stands. One of the costs written above no longer applies.**

This record claimed UndefinedBehaviorSanitizer and a second compiler were
"now unreachable" without CI. That was wrong, and it was wrong in the way this
ADR is itself about: nobody had checked. **WSL 2 was already enabled on this
machine with no distribution installed**, so both were one command away:

```
wsl --install -d Ubuntu --no-launch
```

Ubuntu 26.04 LTS, clang 21.1.8 and gcc-14 14.3.0. The `linux-sanitize` and
`linux-gcc` presets — written and never once executed — both configured, built
and passed on the first attempt, with 3,632 checks each, matching the Windows
counts exactly. UBSan was verified to be genuinely active rather than merely
configured: a deliberate signed overflow through the same flags aborts with the
expected diagnostic.

So the local bar is now higher than this record describes. What is still true,
and still the point of the decision, is that nothing runs any of it
automatically: `check` in both Windows trees remains the definition of done,
and the Linux presets are a deliberate act before a milestone lands. See
[`../VERIFICATION.md`](../VERIFICATION.md) rule 20 for how to run them.

The lesson is the one already in the section above, arriving again: a cost
recorded from reasoning rather than from a command is a cost nobody has
measured.

## Update, 2026-09-08: the phase gates are where the rest of the tooling runs

This record settles what `check` does, and says that nothing runs
automatically. It does not say *when* the things outside `check` run -- and with
no CI, that is a gap a person falls into. `asan`, `linux-sanitize`, `linux-gcc`
and the fuzzers are each a deliberate act, and a deliberate act with no
scheduled moment is one that happens when somebody remembers.

Milestone 1 gives them a moment. **Every phase ends with a gate task** --
[M1-23](../plan/tasks/m1-23-phase-a-gate.md),
[M1-38](../plan/tasks/m1-38-phase-b-gate.md), and one for each phase after --
which runs `asan`, both Linux presets, every fuzz target with a time budget and
a coverage review, and records the numbers. **Nothing proceeds past a red
gate.** The list grows with the milestone: `linux-tsan` joins at
[M1-33](../plan/tasks/m1-33-async-loading.md), the first thread, and the fuzz
targets go from one to five.

This is not CI arriving under another name. A person still runs it, and the
decision above stands unchanged. What is different is that the tooling this
record calls "one preset away" now has a named place in the queue where it is
not optional, so "we should run the sanitizers sometime" becomes a task with a
number instead of an intention.

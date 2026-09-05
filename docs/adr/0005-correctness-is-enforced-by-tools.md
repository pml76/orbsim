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

Around it, four smaller mechanisms, each catching the same class of mistake
earlier than the next:

| When | Mechanism | Catches |
|---|---|---|
| The moment a file is edited by Claude Code | `.claude/settings.json` hook: `clang-format -i` | formatting |
| The moment a commit is attempted | `scripts/git-hooks/pre-commit` | formatting of staged files |
| Before "done" is claimed | the `check` target, both trees | everything above plus tests, lint, validation |
| On every push | `.github/workflows/ci.yml`: Windows clang, Linux clang with ASan and UBSan, Linux gcc | the second compiler and the sanitizers Windows cannot run |

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
a CTest test with the `gpu` label; CI excludes the label, developers do not.

## Why

Section 1 of the guidelines: the compiler is the cheapest reviewer. The
Toolbox's rule two: the tool you do not run in CI is a tool you do not have.
Both are arguments that correctness should be a property of the build rather
than of anyone's diligence, and this record is those arguments made concrete.

The evidence that motivated it: the first pass with a working header filter
found 64 lint findings in code that had been "zero findings" for its whole
life, the static analyzer found a dead store the syntactic checks cannot see,
and a propagator that passed 732 checks failed at 1 AU because no test had
ever flown further than Earth orbit. None of those was a failure of care. All
of them were failures of a tool not being run, or a test not being written,
which is what this decision makes harder to repeat.
# Making verification cheaper without making bugs likelier

Status: **proposed**, 2026-09-27. Nothing here is decided. Each item needs the
owner's ruling before any work starts.
Kind: proposal
Read when: you are deciding how verification should run per task and per gate.

The numbers come from [the measurement of 2026-09-27](../measurements/verification-cost.md).
The owner's condition applies to every proposal: **the chance that a bug
survives a task must not rise.** So every proposal below says why it keeps
that chance level, and how that claim would be checked before anyone relies
on it. Savings marked *estimate* are reasoned from the measurements and still
have to be measured after the change.

## Where a task's time goes today

Per `check`, per tree, after an edit, on a tree that already exists:

| Step | Cost | Measured? |
|---|---|---|
| Compile | Seconds for one source file; about 200 s after an edit to a core header, which recompiles nearly everything | the full compile is measured |
| **Lint** | **Any header edit re-lints all 48 files: 206–313 s**, at 10 jobs | measured |
| Tests | 48–52 s release, 103–113 s Debug | measured |
| format-check, doc-links, mutant-anchors | 1–5 s each | measured |

Two trees, one after the other, and a header edit costs about **10–14
minutes**, over half of it lint. On top of that, per task:

- a **mutation pass**, 2–9 minutes a run, which past tasks have run up to six
  times;
- at each **gate**, the other trees and 8 minutes of fuzzing.

## Proposals, in order of what they save

### P1. Re-lint only the files that include what changed

- **What.** Each file's lint step depends on its own compiled object instead
  of on every header in the project. Ninja already rebuilds an object exactly
  when the file, or anything it includes, changes. So a file is linted again
  exactly when the text clang-tidy reads has changed.
- **Saving.** The lint step drops from 206–313 s to the lint time of the files
  that include the edited header. For a leaf header or a source file, that is
  a few files. *Estimate: about 2–4 minutes per tree per `check`.* For
  `core/Units.hpp` or `core/Scalar.hpp`, which nearly everything includes, it
  saves nothing, correctly.
- **Why no bug gets through.** Every file whose preprocessed text changes is
  linted again, and no other file can gain or lose a finding. The independent
  review of 2026-09-26 found the idea sound on four conditions, all kept:
  - the `.clang-tidy` files, the `--verify-config` step and the clang-tidy
    executable stay inputs, so a change of configuration or tool still
    re-lints everything;
  - a file compiled into several programs depends on all of their objects;
  - a new grep check refuses includes guarded by `__clang_analyzer__`, the one
    way clang-tidy could see text the compiler does not;
  - headers that no linted file includes are not linted today either, so
    nothing is lost.
- **How it is checked.**
  - Edit, in turn, a leaf header, a core header, a source file and
    `.clang-tidy`; each time, compare the set of files re-linted with the set
    of files that include the edit.
  - Plant a lint finding in each, and see it caught.

### P2. Lint `tests/AbortBehaviour.cpp` once, not 23 times

- **What.** This 60-line file is compiled into 23 programs, so
  `compile_commands.json` holds 23 entries for it, and clang-tidy checks all
  23. That is the slowest lint step in every tree, at 114–171 s. Lint it once
  per *distinct* set of compiler flags instead: the entries differ only by
  program, except for `test_probe_clear`, which adds one definition.
- **Saving.** About 110–160 CPU-seconds per full lint, per tree (measured on
  the step). The wall-clock gain depends on the next-longest step,
  `test_time.cpp` at 83–164 s. *To be measured.*
- **Why no bug gets through.** clang-tidy sees exactly the same text under
  each set of flags, so linting a set once finds whatever 23 identical runs
  would.
- **How it is checked.** Compare the 23 compile commands, and show that they
  collapse to the claimed sets. Then plant a finding in the file and see it
  caught.

### P3. Run the tests in parallel

- **What.** `check` runs `ctest` one test at a time. Running with `-j`, with
  the GPU tests sharing one lock so they never overlap, would spread the
  103–113 s Debug run over several cores.
- **Saving.** *Estimate: most of the 48–113 s per tree*, since nearly every
  case is independent and CPU-bound.
- **Why no bug gets through.** The same tests run, with the same assertions.
  Parallel running can only expose more, for example two tests that share a
  file.
- **How it is checked.**
  - Twenty parallel runs in each tree, each compared case by case with a
    serial run.
  - The probe fixture tests confirmed to keep their order.
  - The unexplained stalls of about 10 s per Debug run looked at again, since
    parallel running may change them.

### P4. Re-run a mutant file only when its code or its judges change

- **What.** One pass over all eleven files costs 54 minutes. A task re-runs:
  - its own file;
  - any older file whose mutated file it changes (the harness names them);
  - any declared survivor its new tests are meant to kill, which is today's
    habit written down as a rule.
- **Saving.** Reruns of untouched files, 2–9 minutes each.
- **Why no bug gets through.** A mutant's verdict can only change if the file
  it mutates changes, or if the tests that judge it change. Both are exactly
  the trigger.
- **How it is checked.** At the next gate, run all eleven files in full. Any
  verdict that differs from the last selective run is a hole in the rule.

### P5. Fuzz from a saved corpus, and give each target its own budget

- **What.** Keep each target's corpus (the collection of interesting inputs it
  has found) between runs, so a gate continues where the last one stopped
  instead of starting empty. Set each target's time from its own curve:
  - `fuzz_orbit` found everything within 4 minutes and nothing in the next 26;
  - `fuzz_time` was still finding new paths at 240 s and only stopped at about
    900 s.
- **Saving.** Little time; **more found in the same time.** A fresh
  `fuzz_time` run spends its 240 s re-finding 1,275 features that a saved
  corpus would hand it at once.
- **Why no bug gets through.** A saved corpus only adds starting points. The
  fuzzer still mutates from them, and still starts from nothing when the
  corpus is empty.
- **How it is checked.** Same budget, with and without the corpus: coverage
  at the end must be at least as high, over several runs.
- **Owner's choice.** Whether the corpus is committed (reproducible on every
  machine) or kept in the build tree (nothing added to the repository).

### P6. Split the three slowest test files

- **What.** `test_time.cpp` (2,563 lines), `test_orbit_scales.cpp` (2,606) and
  `test_fixture_file.cpp` (867) are the longest compiles (25–67 s) and, after
  P2, the longest lint steps (83–164 s). Split each into two or three files by
  subject.
- **Saving.** The minimum compile and lint time, when many steps run in
  parallel; and after P1, a smaller file re-linted when one of them changes.
  *To be measured.*
- **Why no bug gets through.** The cases move and none of them changes.
- **How it is checked.** The assertion and case counts must match exactly
  before and after, suite by suite, which `STATUS.md` already records.

## Reliability, not speed

### P7. Keep lint from running the machine out of memory

- **What.** At Ninja's default of about 22 jobs, lint asks for about 22 GB,
  because each clang-tidy process needs about 1.04 GB. That stopped one run
  of this measurement with CLion open. Give the lint steps their own Ninja
  job pool, sized for memory, for example 10 jobs.
- **Why.** A lint run killed for memory fails loudly, so this is not a way for
  a bug to slip through. It is a `check` that fails for no reason, and costs a
  full re-run.
- **Cost.** At 10 jobs, lint measured 313 s, against 273 s uncapped. That
  difference is inside this machine's 27 % run-to-run noise.

## Considered and not recommended

- **Selecting tests by what changed.** Tests are 48–113 s per tree. The
  selector would need all seven fixes the review found (shaders, data files,
  script inputs, environment, and so on) to be safe, and a missed input would
  let a bug through silently. After P3 there is too little left to save.
- **Linting only one tree.** Debug and release see different code, because
  assertions and `NDEBUG` branches differ, so one tree cannot vouch for the
  other.
- **Mutants side by side in separate trees.** Each build needs about 10 GB at
  10 jobs. This machine cannot run two at once safely.
- **Shorter fuzzing.** The curves say `fuzz_time` needs more time, not less.
- **A faster fresh configure (520–600 s).** A fresh tree is rare: the gate
  trees persist, and `measure-frame-cost.py` reuses its worktrees after the
  first run.

## Suggested order

1. P7, then P1 and P2 together: they are the largest saving and all three
   touch the same lint block of `CMakeLists.txt`.
2. P3.
3. P4, as a written rule.
4. P5 at the next gate.
5. P6 last.

Each would be its own task under the normal process: questions first, the
check it names seen to fail first, then both trees.

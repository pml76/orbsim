# orbsim — the briefing

Kind: reference, and the first thing to read
Binding: no — every line points at the binding text, which wins
Read when: you start a session. Then [`STATUS.md`](STATUS.md) for what is
next, and that task's document. Open the long documents below only for the
section you are about to apply.

Written 2026-10-16 (register decision 597) so that a session can learn how this
project works without reading about 490 KB of documents first. **Nothing here
is new**: each line summarises a rule written in full elsewhere and names
where. If a line here and its source disagree, the source is right, and this
line is a defect to fix. `CLAUDE.md` is loaded into every session and is not
repeated here: its working agreements and twelve non-negotiables are the
rules; this is the rest, condensed.

## Contents

- [1. How decisions and doubt are handled](#1-how-decisions-and-doubt-are-handled)
- [2. The project in one screen](#2-the-project-in-one-screen)
- [3. Writing C++ here, beyond the non-negotiables](#3-writing-c-here-beyond-the-non-negotiables)
- [4. How bugs are kept out](#4-how-bugs-are-kept-out)
- [5. Traps already found](#5-traps-already-found)
- [6. Where everything is](#6-where-everything-is)
- [7. The work now](#7-the-work-now)

---

## 1. How decisions and doubt are handled

The owner's standing rules are `CLAUDE.md`'s working agreements 1-9. The owner
opens most sessions by restating agreement 1 -- make no errors, decide nothing
alone, leave no question open, go the extra mile, write plainly. **It is already
recorded; confirm that, do not record it again.** In practice:

- **Before a task starts**: read its document and the register decisions it
  cites; collect every open point, including the ones the document did not
  foresee; for each, the options, what each costs, and a recommendation; put
  them **all at once**; start only when every one is ruled. A question left for
  "when we get there" is a decision taken alone.
- **Measure rather than assume**: where a fact can be checked -- a tag, what a
  linter reports, whether two things agree -- check it in a short spike in a
  scratch folder, and quote the number. Where a decision could be wrong, verify
  it before proposing it (a spike, a primary source, a second implementation),
  and say what was checked.
- **During the work**, anything unforeseen goes back to the owner, with its
  measurement, before it is acted on.
- **The method of work** (decision 701): give each long step of the work to
  one helper at a time, and do not run two builds at the same time. Keep
  only the helpers' reports, and check each claim in them by measurement.
  Write each ruling as a register row before the code. Do not edit a tracked
  file while a build or a check runs. Examine the exit code of each check
  before a commit.
- **Never without an explicit go-ahead**: change or delete a test, relax a
  tolerance, add to `.clang-tidy` or write a `NOLINT`, switch a warning off
  project-wide, refactor beyond the scope asked, delete a tracked file, rename a
  public type, or choose between designs. Fixing the finding is the default;
  silencing it is the owner's decision. For a warning raised where our code
  meets a library, the owner has chosen the site over a project-wide switch
  every time it was offered -- recommend the site.
- **A failing test means fix the code.** A test that passes under one compiler
  and fails under another is an unstable algorithm, not a limit to document;
  editing the test destroys the signal (agreement 2, `plan/realism.md` 1.2).
  The owner's words on 2026-09-08, when a test was weakened and the solver
  was not corrected: "changing the test to make it work is just plain
  wrong!!!"
- **Accuracy first** (agreement 3): the safeguarded algorithm, the
  cancellation-free formula, the solve to the resolution of the type; never
  "it reports a failure" where a closed form exists. **Prove each accuracy
  claim by measurement**, before and after, fitted against a conditioning law
  where one exists. For example: a safeguarded or bracketed root-finder, not a
  bare Newton iteration; and the equations without cancellation, such as the
  half-angle identities, the argument reduction of the Stumpff functions and
  compensated summation.
- **A budget is about twice the reference's own measured distance**, measured
  before the budget is written (register decisions 54 and 76), never inherited
  from a plan. An independent *route* is not an independent *model*: say where
  a reference shares code or series with the code under test (decision 75).
- **Check the instrument**: count findings with the tool's own grouping, not a
  regex on a line's tail; make a tool report a known fault before trusting its
  zero. A number given to the owner that turns out wrong is corrected at once,
  where the decision is written (decision 84).
- **Plain words** (agreement 1, fourth refinement): the ordinary word first, and
  any technical term glossed the first time it appears in a reply or question.
  **Since 2026-10-19, all text that you write or change follows ASD-STE100
  Simplified Technical English, Issue 9** (agreement 1, fifth addition;
  decision 693): answers, questions, documents, commit messages and code
  comments. Short sentences, the active voice, one instruction in each
  sentence, and the condition first. Technical nouns and technical verbs of
  software, and the usual terms of this project, stay; all other words
  follow the dictionary (decision 676). The spelling stays British
  (decision 677). An instruction has at most 20 words, and a description at
  most 25; keep "a", "an" and "the". Examine the words of each new text against
  the dictionary before the commit, as `PROJECT_STATE.md` section 2 says.
- **Rulings are recorded** as the next numbered row of the register,
  [`plan/milestone-1-decisions.md`](plan/milestone-1-decisions.md) section 9:
  what was ruled, what was measured, what was considered, the date. A ruling
  that changes an earlier one gets "Amended by decision N" on the earlier row.
  The task document and `STATUS.md` follow.
- **Commits**: small, one thing each, a formatting pass on its own; the message
  says why; it ends with one `Co-Authored-By` line naming the model and
  **never a session URL** (the repository is public). Push only when the
  owner says so, after `check-full` in both trees (the pre-push hook refuses
  otherwise). When the work included a decision that the owner did not
  rule, get the owner's word before the commit (the owner, 2026-09-08).
  **No CI, ever** (ADR 0005). **Everything lives in the
  repository**: the project is worked on from two machines, and an assistant's
  memory reaches only one.

## 2. The project in one screen

An Orbiter-like space flight simulator, built from scratch: real orbital
mechanics, 6-DOF vessels, MFD instruments. C++23, clang 23 (Windows and WSL,
one version), Vulkan 1.3, SDL3; clang and Vulkan were the owner's choice and
stay. **A simulation, not a sandbox** ([ADR 0006](adr/0006-simulation-not-sandbox.md)):
every accuracy claim carries a stated error budget checked against data this
project did not produce. **Only the visuals scale**: a quality setting never
reaches the simulation state ([ADR 0007](adr/0007-render-quality-is-a-struct.md)).
On the first machine the folder of the checkout is named `untitled`, from
the empty CLion template that the work started from. The project is orbsim,
a full 3D simulator in the spirit of Orbiter: the owner refused smaller
scopes on 2026-09-04.

Layers, one way only: `src/core/` (maths, units, time, contracts) and
`src/orbit/` and `src/astro/` (ERFA behind typed wrappers) build headless;
`src/view/` is render-side maths without Vulkan, which the tests link;
`src/render/` is Vulkan; `src/app/` the window and loop. A renderer concept is
never pushed down into the physics. Milestone 1 (Earth, orbit track, Orbit MFD)
runs in phases A, B, D, C, E, F, G; phase A is closed; where things stand is
[`STATUS.md`](STATUS.md). The decisions that span files are the ADRs,
[`adr/README.md`](adr/README.md).

## 3. Writing C++ here, beyond the non-negotiables

Full arguments: [`../CODING_GUIDELINES.md`](../CODING_GUIDELINES.md); the shape
to copy: `coding-guidelines-example/`; the checklist that closes a task:
`.claude/rules/cpp-style.md`, which loads itself when C++ is touched.

- **Warnings**: clang `-Weverything` less a few named exceptions, gcc's full
  list (`scripts/gcc-warnings.py`), MSVC `/Wall`, all as errors
  ([ADR 0017](adr/0017-every-warning-is-an-error.md)); a library-interface
  warning off at its site with the reason. A gcc-only pragma sits inside
  `#if defined(__GNUC__) && !defined(__clang__)`, a clang-only one inside
  `#ifdef __clang__`.
- **clang-tidy**: zero findings at `WarningsAsErrors: '*'`, headers included;
  `--verify-config` runs first; the suppression list is meant to shrink, and
  how is `.claude/rules/lint-config.md`. Four clang-tidy 23 checks are wrong on
  this code, five ways between them, and some of their fixes do not compile -- never run `--fix`
  without reading `PROJECT_STATE.md` section 8.
- **Units are types** (mp-units under `core/Units.hpp`, `Vec3<R>`;
  [ADR 0019](adr/0019-vectors-carry-their-unit.md)); `Mat4` carries both
  spaces' units and frames ([ADR 0020](adr/0020-transforms-carry-their-units.md),
  [0021](adr/0021-transforms-carry-their-frames.md)), a frame change only
  through `retargetFrame`.
- **A bad value: make it unrepresentable, report it, assert it, in that order.**
  Bounded values have a private constructor and a `from()` returning
  `std::expected`, and `consteval` helpers for literals
  ([ADR 0022](adr/0022-a-bounded-scalar-validates-itself.md)): `Eccentricity`,
  `GravParam`, `Fraction`, `view::Camera`. What a caller can cause is reported
  through `std::expected` with a named error; what only a bug can cause is
  `ORBSIM_EXPECTS` / `ORBSIM_ENSURES` (`src/core/Contract.hpp`; the message
  names condition, function and file and prints the call chain, no line
  number). Never "silently cope", never a `bool` plus an out-parameter, never
  two-phase `init()` ([ADR 0002](adr/0002-error-handling-strategy.md)).
- **Floating point**: no `==` on a float (the value types have none);
  `nearlyEqual(a, b, Tolerance)` or `bitIdentical()` where bit identity is the
  claim; `isNaN`/`isFinite` from `core/Scalar.hpp`, since MSVC's compile-time
  evaluator gets NaN wrong. `-ffp-contract=off` on everything, never
  fast-math. `f64` in the simulation; `f32` only in five named functions, a
  position subtracted before it narrows -- `grep static_cast<f32> src/` is the
  audit (`cpp-style.md` names them).
- **`constexpr` with a `static_assert` proving it**, where the three compilers
  agree it is constant (clang and MSVC refuse `std::tan` and `std::isfinite`
  there, gcc accepts them).
- **Every `VkResult` through `vkCheck`**; every hand-written destructor is in
  `render/VulkanHandle.hpp` or `app/SdlHandle.hpp`; Rule of Zero everywhere
  else, a required order expressed as a member (`DeviceIdleGuard`).
- **Functions fit the limits**: 60 statements, 80 lines, nesting 5, cognitive
  complexity 25 -- test bodies included since M1-129, so a long test names
  groups of checks as helpers or loops over a table.
- **Tests**: Catch2 through `tests/TestCase.hpp`, never Catch2's macro header;
  no two sections of one name in a test case; a name up to 127 characters. A
  test reads a file only through `dataFile()` or `probeFile()`
  (`tests/TestReads.hpp`), which records it; nothing under `tests/` opens a
  file itself or includes a Vulkan or SDL header. A suite that needs
  `orbsim_view` is named in `CMakeLists.txt` one by one.
- **Headers** stand alone (four self-check targets), use include guards, and a
  `.cpp` includes its own header first; helpers in an anonymous namespace.
  Names: types `PascalCase`, functions `camelCase`, constants `kPascalCase`,
  private members `trailing_`, never a leading underscore.
- **Determinism**: a fixed physics step; seeded generators with the seed
  written down; no non-const statics; `std::jthread` when threading comes, and
  ThreadSanitizer with it. Paths are `std::filesystem::path`.
- **Comments say why**; every constant says where its value came from and in
  what unit.
- **Dependencies** are pinned, with licences and the files compiled, in
  [`../THIRD_PARTY.md`](../THIRD_PARTY.md); the licence boundary for Orbiter's
  own source is [`ORBITER-REFERENCE.md`](ORBITER-REFERENCE.md).

## 4. How bugs are kept out

Full text: [`VERIFICATION.md`](VERIFICATION.md); its Part 4 says what a machine
enforces and what rests on a person.

**The definition of done** is `check` in both `relwithdebinfo` and `debug`
(assertions are live only in Debug), one tree after the other. It builds
everything, checks the format, verifies the lint configuration and lints every
file whose text changed, checks every document link and backticked path,
checks every mutant's anchor, refuses a tree configured from stale text, and
runs the tests through `scripts/run-tests.py`, which skips a test whose
fingerprint -- program, command line, environment, every file it read -- is
what it was at its last pass; since M1-132, in the two `check` trees, a
program with the coverage hook counts as unchanged also when no function that
the test entered at its last pass has changed
([ADR 0028](adr/0028-tests-run-when-a-function-they-entered-changes.md)); a two-second run of the application under the
Vulkan validation layers fails on any validation error.
`check-full` runs every test and stamps the commit, which the pre-push hook
requires in both trees. Around it:

- **Before a commit**: a change reaching a fuzz target (`src/core/`,
  `src/orbit/`, `src/astro/`, a `tests/fuzz_*.cpp`) runs that fuzzer from its
  committed corpus for its budget -- `fuzz_orbit` 240 s, `fuzz_time` 900 s.
- **After a task's commit, before its push**: the task's mutant file,
  `python scripts/mutate.py scripts/mutants/<task>.json`, and its record
  committed. Every file `scripts/mutants-due.py` lists runs at each phase gate
  (decision 276).
- **Before a milestone**: the `asan`, `windows-msvc`, `linux-sanitize` and
  `linux-gcc` trees with `check-full`, coverage read per program, the fuzzers in
  both builds.

**The rules** (VERIFICATION.md):

1. Write the test first and watch it fail for the right reason.
2. Never check code against itself: an analytic value, a conserved quantity,
   or an independent implementation -- and when two had to merge, say what the
   merge cost and replace the independence.
3. Validate against external truth: Horizons, Skyfield, published examples,
   60-digit references.
4. State the error budget as a number before the code; the test asserts it.
5. Test the singularities (e = 0, e near 1 from both sides, i = 0 and pi,
   retrograde, dt = 0), not only the scales.
6. Every bug leaves a regression test named after the symptom; cite symbols,
   never line numbers.
7. Assert the impossible, report the possible, never silently cope.
8. Bound every loop and report non-convergence.
9. Ask "in what?" of every bare number.
10. Commit small; keep the tree bisectable.
11. Property and metamorphic tests: reversal, composition, scale invariance,
    conservation.
12. Seeded sweeps, the seed written down and the failing case printed.
13. Fuzzing, from committed corpora, with budgets from measured curves; ask
    what share of inputs a change makes a target reject.
14. Differential testing between implementations (weakened for the two
    propagators, which share a solver).
15. Runtime invariant monitors -- to build, once there is a loop to monitor.
16. Determinism is tested: bit-identical reruns.
17. Full dimensional analysis in the type system -- done.
18. Coverage is a map of what was never run; read each program with its own
    profile, since a merged profile hides lines.
19. Mutation testing (a mutant: a copy of the code deliberately broken, to see
    a test catch it). A mutant that does not compile is invalid, not a kill; a
    kill is read for which test made it; a mutant decisive in one build only
    gets a file per tree; a survivor is declared with its reason or the run
    fails; a verdict may name the graphics card; the harness restores the
    build tree from a snapshot.
20. Run the other implementations -- gcc-14, MSVC, the sanitizers -- before a
    milestone; one platform tells you about your code, two about your
    assumptions.
21. `check` is the single definition of done, and grows.
22. Write down why, when you know why.
23. **Distrust agreement, and distrust a clean result**: a check that silently
    stops checking looks exactly like one that passes; make the instrument
    fail once before believing its zero.
24. Prefer the bug you cannot write: a type, a factory, an `enum class`, a
    `[[nodiscard]]`.

**Images**: goldens are per graphics card (`tests/golden/<vendor>-<device>/`),
and only the owner accepts one.

## 5. Traps already found

Each cost time once; the full account is [`PROJECT_STATE.md`](PROJECT_STATE.md)
section 8 unless another place is named.

- `NOLINTNEXTLINE` above a `template` line covers the template line, not the
  signature.
- `misc-non-private-member-variables-in-classes` ignores an all-public class
  only while it declares no member function.
- `bugprone-easily-swappable-parameters` needs
  `SuppressParametersUsedTogether` off, or it misses most pairs.
- clang-format splits a 16-element braced list one per line: build matrices
  with `identityMatrix()` and `set()`.
- A deduced `auto` return defeats NRVO, and `-Wnrvo` is an error: name the type.
- gcc rejects CTAD without a deduction guide and a singly-braced `std::array`;
  wants `noexcept` on lambdas it proves cannot throw.
- `readability-trailing-comma` and `readability-redundant-parentheses` report
  things that are not there, and their fixes break the build; De Morgan by
  `readability-simplify-boolean-expr` changes a NaN's result;
  `modernize-avoid-c-style-cast` reports casts clang made itself.
- A trait can say yes where the compiler says no (mp-units' conversion
  operator): invert a "must not compile" assertion now and then.
- mp-units leaves a default-constructed quantity uninitialised: `Unit` zeroes
  explicitly.
- MSVC needs `/utf-8`; its constant evaluator answers NaN comparisons wrongly.
- A CRTP base deriving from a library type takes `Derived` in its comparison
  operators, as hidden friends; an inherited constructor keeps its base's
  access.
- clang-tidy's header filter needs both path separators; a `Checks:` list drops
  the defaults; a `#` inside `Checks:` is text, not a comment.
- `-Wfloat-equal` means different things to clang and gcc; gcc builds only the
  core and the tests, so `== 0.0` in the renderer is seen by nothing:
  `std::fpclassify(x) == FP_ZERO`.
- `-Wweak-vtables` exists on Linux only; `-Wabi-tag` fires under gcc on any of
  our types holding a `std::string` or returning a path.
- `[[clang::lifetimebound]]` draws gcc's `-Wattributes`.
- `-Weverything` includes `-Wshadow-header`, which breaks a probe that shadows
  our header.
- **A mutant must compile**: `if (false)` and a dropped variable are
  `-Werror` failures, not kills.
- **A Debug assertion on Windows opens a modal dialog and waits**: every suite
  links `tests/AbortBehaviour.cpp`; a scratch Debug program must call
  `_set_abort_behavior` itself.
- **Restore a mutated file from a copy, never `git checkout --`** of a file with
  uncommitted work.
- `std::map`'s move allocates under MSVC's library, so `bugprone-exception-escape`
  reports its holders: use a `std::vector`.
- Git Bash rewrites `/mnt/c/...` and `//...:...` arguments and drops `$vars` for
  `wsl`: call `wsl` from PowerShell, and prefix `MSYS_NO_PATHCONV=1` for a
  Windows tool's `/flag` arguments. Its heredocs collapse backslashes: write a
  script with an editor. Python's `write_text` writes CRLF on Windows: use
  `write_bytes`. Python's output to a redirected standard output also has
  CRLF: write it again with LF, and give it to `git add` again.
- **The assistant's shell**: a `cd` into a worktree in `build/` makes the
  session use that worktree as its folder: use absolute paths or a
  subshell. A shell loop that ends with `[ ... ] && break` exits with 1 when
  all passed: give it an explicit exit code. Commit only when the check
  passed (`if`, not `;`).
- **Helpers** stop after 600 s with no output: give them long commands in
  the background, and continue a helper with a message, which keeps its
  context.
- **`doc-links` refuses a path in backticks to a file that is not there at
  this time**: name a file to come without backticks.
- A Catch2 test named with a comma cannot be selected by name; `ctest -R` can.
- A summary of a web page is not the page: quote numbers from the source.
- vk-bootstrap silently takes the driver's first swapchain format.
- `FETCHCONTENT_SOURCE_DIR_<NAME>` silently overrides a pin.
- A clang upgrade leaves old trees on the old clang: delete and set up again.
- `eraC2t06a` applies s' even with polar motion zero.
- **Running long jobs** (STATUS's open items): CLion reloads CMake on its own
  (keep it closed, its automatic reload off); Claude Code stops background work
  when memory runs short and can leave a mutant in the source; a machine that
  sleeps trips the harness's time limits; the build pool is sized from total
  memory.
- **Pushing from the first machine**: the SSH key is locked with a passphrase,
  and on 2026-10-21 the SSH agent of Windows did not run. A push from the
  assistant's shell then waits for a passphrase on a console that nobody can
  see. The assistant does not type a passphrase: stop the push, and the owner
  pushes from a terminal of their own, `git push origin <commit>:refs/heads/master`.
  Read the remote without a key: `git ls-remote https://github.com/pml76/orbsim.git`.
- **`mutate.py` stops with `[Errno 22] Invalid argument`** when Windows refuses
  to cut a file that a different program has mapped into its memory. The
  Claude desktop app runs `git diff` while files change, and Git maps the
  changed files. Run the file one more time, as decision 623 says
  (`STATUS.md`, open item (g)).
- **Web access on the first machine**: the shell's proxy refuses some sites;
  in PowerShell, clear `HTTPS_PROXY` and `HTTP_PROXY` and use
  `Invoke-WebRequest -UseBasicParsing`, which takes Windows' own proxy.

## 6. Where everything is

| For | Read |
|---|---|
| What is next, versions, counts, open items | [`STATUS.md`](STATUS.md); the suites one by one in [`TEST_SUITES.md`](TEST_SUITES.md) |
| Setting a machine up, the trees, CLion, WSL, MSVC | [`PROJECT_STATE.md`](PROJECT_STATE.md) section 2 |
| Settled questions and why | `PROJECT_STATE.md` sections 6 and 7, the [ADRs](adr/README.md) |
| Traps | section 5 above; `PROJECT_STATE.md` section 8 |
| Frame-time figures; the phase A gate's results | `PROJECT_STATE.md` sections 10 and 11 |
| Every ruling, numbered | [`plan/milestone-1-decisions.md`](plan/milestone-1-decisions.md) -- read the rows a task cites, not the whole file |
| The task queue and its order | [`plan/milestone-1-tasks.md`](plan/milestone-1-tasks.md); each task's own document in `plan/tasks/` |
| The realism gaps and their order | [`plan/realism.md`](plan/realism.md) |
| After milestone 1 (proposed only) | [`plan/milestones.md`](plan/milestones.md) |
| Why something is the way it is; every finished task as STATUS recorded it | [`HISTORY.md`](HISTORY.md), section 7 for the task record |
| Reference data and how to fetch it | `data/*/README.md`; Skyfield 1.55 with numpy 2.5.3 in a scratch virtual environment writes the committed fixtures (`scripts/skyfield-fixture.py` refuses any other version); `pyerfa` for spikes only; Horizons by `data/horizons/README.md`, never committed |
| Measurements kept as records | [`measurements/`](measurements/) |
| Rules that load themselves | `.claude/rules/`: C++ style, physics tests, the renderer, the lint configuration |

## 7. The work now

[`STATUS.md`](STATUS.md)'s "Next task" row is the authority. On 2026-10-17:
[M1-129](plan/tasks/m1-129-tests-by-fingerprint.md) (tests skipped by
fingerprint) is finished and pushed -- every mutant file passed after its
commits, decisions 601-623. Next,
[M1-131](plan/tasks/m1-131-coverage-hook.md) to
[M1-133](plan/tasks/m1-133-selection-proved.md) (tests chosen function by
function, every question already ruled, decisions 582-596 and 624, and
M1-131's own, item 5's among them, 625-632 on the evening of 2026-10-17,
before its work began); then
[M1-134](plan/tasks/m1-134-mutants-chosen-like-tests.md), mutation passes
chosen the way the tests are (decision 633, 2026-10-18); then M1-127 and
M1-130 (the code as modules), lower levels first (decision 495).

**On 2026-10-20** M1-135 is done and pushed up to `1c4c502` (decisions
660-678): `lint_deps` makes sure that
each compiled file of the repository is linted or named as an exception,
and its mutation pass caught 8 of 8 mutants. Next, M1-132 and M1-133.
M1-136, `lint_deps` in the fuzz trees, waits with its questions open.
Write all text in ASD-STE100 (section 1). STATUS's row says what each
task has done, what is left, and the sequence.

**Later on 2026-10-20** the owner ruled the questions of M1-132 before its
work started, decisions 702-709, and the work started. Decision 701 put the
method of work into section 1.

**On 2026-10-21** M1-132 is done (decisions 701-719): in the two `check`
trees, a test with a program that has the coverage hook also runs again only
when a function that it entered has changed, as ADR 0028 records. Its
mutation pass caught 36 of 37 mutants, and one survivor is declared. It is
pushed up to `dcc5a13`. Next, M1-133, then M1-134. The next session starts
with the questions of M1-133, all at one time, and with `check-full` in the
two trees before the push of the local commits (`STATUS.md`).

**Later on 2026-10-21** the owner ruled the questions of M1-133 before its
work started, decisions 720-734, and its work started. `check-full` passed
in the two trees at `fddeefc`, and the owner pushes. First the mutant check
runs in the two `check` trees, with a measurement script in
docs/measurements/m1-133/. Then the replay runs in Debug, in a new
worktree, with the items of decision 726.

Also on 2026-10-21, the owner postponed M1-134 (decision 735), and after
that, on the same day, made it the next task after M1-133 again (decision
736). Its task document is written again from the start, on the owner's
rules R1 to R13, with SQLite for its store.

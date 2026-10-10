# M1-116 — A review of the whole project, documents and code

Phase: B | Status: **done 2026-10-10**: the findings are in
[`docs/review/m1-116-findings.md`](../../review/m1-116-findings.md)
Prerequisites: M1-115
Decided by: register decisions 427-433, ruled 2026-10-09; 435, which
approved this plan and answered the three points under "Before it starts" on
2026-10-10; and 436-439, the same day, which moved the two tools to Windows and
settled how the reading is done; and 440-450, the owner's rulings on the
findings, which queued M1-117 to M1-127

## Purpose

**The goal is a consistent, bug-free project**, in the owner's words. Two
small tools were found missing things in a single task (M1-111, M1-115), and
a count in the task queue was found stale while recording them: "the 96
documents", where there are 116. Each was found by accident. This task looks
on purpose, across everything, before phase B's real work begins.

**The review changes nothing** (decision 431). It produces a list of
findings, each with its evidence, a proposed fix and its cost; the owner
rules on them; and the fixes become tasks in the queue. That is working
agreement 1 applied to a whole project: state the finding, propose the fix,
and wait.

## Scope (decision 428)

**Everything tracked in git**:

| Area | Size, 2026-10-09 |
|---|---|
| Documents, not counting task documents | about 16,000 lines |
| Task documents | 116 files, about 11,000 lines |
| `src/` | 83 files, about 22,000 lines |
| `tests/` | 177 files, about 22,000 lines |
| `shaders/`, `cmake/`, `CMakeLists.txt` | about 3,700 lines |
| `scripts/` | 149 files, mutant files and records included |
| `coding-guidelines-example/` | about 2,100 lines of code |

**Records are read for what they say about the project as it is now, not
rewritten**: `docs/HISTORY.md`, the register's rulings, finished task
documents and measurements are history, and a statement there is a finding
only where it claims something about the present that is false.

## What "consistent" is checked against (decision 429)

1. **Every number that goes stale lives only in `docs/STATUS.md`**, as its
   opening says. A count or a version anywhere else is a finding.
2. **No two documents contradict each other** about a current fact.
3. **Every statement a document makes about the code is true** -- "f64
   becomes f32 in exactly five functions", "the only place a file is
   written", the list of suites that link `orbsim_view`, the rules in
   `CLAUDE.md` and `.claude/rules/`. Each is checked against the code, by a
   search where one can decide it.
4. **One name for one thing** throughout.
5. **The router, `CLAUDE.md`, and the rules files** match what the project
   does today.

## What "bug-free" is checked against (decision 430)

1. **Every source file, read against the twelve non-negotiables** in
   `CLAUDE.md` and against `CODING_GUIDELINES.md`.
2. **Every test, read for what it really tests**: that it checks what its
   name claims, against something independent, and that it can fail -- the
   traps already met here are the guide: an error read from a result that
   holds a value (VERIFICATION.md rule 23), an operation applied twice that
   cancels its own fault (M1-09), a tolerance loose enough to accept anything.
3. **Every error path and every loop**: reported or asserted, never quietly
   coped with; bounded, and saying so when it stops.
4. **The declared mutation survivors**, 24 today: can any be caught now?
5. **`docs/STATUS.md`'s open items**, (a) to (e) -- (f) is closed by M1-115.
6. **Two analysis tools the guidelines list and the project has never run**,
   each once, as a measurement, not added to `check`: **cppcheck**, a static
   analyser that finds a different set of defects from clang-tidy, and
   **include-what-you-use**, which reports a missing or an unneeded
   `#include` -- the defect gcc-14 has found twice by accident. The owner
   approved running both; how they are installed is the second point under
   "Before it starts".

## How the reading is done (decision 432)

**Claude leads, and splits the reading among six helper agents** -- separate
Claude instances, each reading one area and reporting what it finds:

1. `src/core/`, `src/orbit/`, `src/astro/`, their suites and the worked
   example;
2. `src/view/` and its suites;
3. `src/render/`, `src/app/`, `shaders/`, and the tests that start the
   application (`cmake/Run*.cmake`);
4. the test support code and the suites as a whole -- independence, guards,
   seeds, what a case can and cannot see;
5. `scripts/` and the build: `CMakeLists.txt`, `cmake/`, `CMakePresets.json`;
6. the documents, against the five consistency checks above.

**Every finding is verified by Claude before it reaches the owner**: read in
the code, reproduced where it can be, and measured where a number is
claimed. A helper's report is a lead, not a finding (VERIFICATION.md rule
23). The two tools of item 6 are run by Claude.

## What it hands over (decision 431)

A findings list, each finding with:

- **what is wrong**, in plain words, and where -- by file and symbol, never a
  line number (VERIFICATION.md rule 6);
- **the evidence**: the code, the command and its output, or the two
  documents that disagree;
- **how serious it is**: a defect (something behaves wrongly), a hidden risk
  (correct today, wrong after a plausible change), an inconsistency, or a
  departure from the house style;
- **the proposed fix, its cost, and the alternatives**.

The owner rules on them, by area, and the rulings become register decisions
and tasks in the queue. **What the review cost** -- time, and the number of
leads against verified findings -- is recorded at the end, measured rather
than estimated now.

## Before it starts

Three points the plan raises that the questions put on 2026-10-09 did not
cover. **Answered 2026-10-10: every recommendation taken, and the plan
approved** (decision 435).

1. **Where the findings live.** (a) *Recommended:* a document of their own,
   `docs/review/m1-116-findings.md`, linked from this task -- the list may be
   long, and a document outside `docs/plan/tasks/` is checked by `doc-links`,
   which a task document is not. (b) A section of this document.
2. **How the two tools are installed.** (a) *Recommended:* both from
   Ubuntu's packages under WSL, run on the Linux build's compile database, if
   the versions offered support C++23 -- measured first and reported; if
   include-what-you-use has no build for clang 23, say so rather than build
   it from source. (b) Windows installers. (c) Only cppcheck.
3. **How rulings are asked for.** (a) *Recommended:* the findings grouped by
   area and ranked by seriousness, each group put as one set of questions,
   with the option to take every recommendation in a group at once. (b) One
   question per finding.

## Done when

- [x] Every area read, every lead verified or dropped with the reason.
- [x] Both tools run, and their findings verified the same way.
- [x] The findings list handed to the owner, and every finding ruled.
- [x] The rulings recorded in the register, and the fixes added to the queue.
- [x] What the review cost, recorded.

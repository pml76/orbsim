# orbsim — instructions for working in this repository

<!--
Restructured 2026-09-09. This file is loaded into every session, so it holds
only what applies to every session: the router, the working agreements, the
non-negotiables, the definition of done, and attribution. Domain detail lives
in .claude/rules/*.md, which the harness loads automatically when a matching
file is touched -- that is why those rules are not repeated here. Nothing was
deleted in the split; docs/HISTORY.md records where each piece went.
Keep this file under 200 lines: longer files measurably reduce adherence.
-->

**IMPORTANT: before starting, read the documents below that match what you are
about to do.** They are one level deep from here, deliberately.

| If you are… | Read |
|---|---|
| picking this up cold, or after a gap | [`docs/STATUS.md`](docs/STATUS.md), then [`docs/PROJECT_STATE.md`](docs/PROJECT_STATE.md) |
| looking for a version, a count, or what is next | [`docs/STATUS.md`](docs/STATUS.md) — the only place any of them lives |
| writing or changing C++ | [`CODING_GUIDELINES.md`](CODING_GUIDELINES.md), and copy the shape in [`coding-guidelines-example/`](coding-guidelines-example/) |
| writing a test, or touching anything numerical | [`docs/VERIFICATION.md`](docs/VERIFICATION.md) — **before** the test, not after |
| doing a milestone 1 task | [`docs/plan/milestone-1-tasks.md`](docs/plan/milestone-1-tasks.md), its [decision register](docs/plan/milestone-1-decisions.md), and the one task document |
| changing something that spans files | [`docs/adr/`](docs/adr/) — [the index](docs/adr/README.md) says which |
| wondering why something is the way it is | [`docs/HISTORY.md`](docs/HISTORY.md) |
| reading or borrowing Orbiter's source | [`docs/ORBITER-REFERENCE.md`](docs/ORBITER-REFERENCE.md) — the licence boundary is not uniform |
| adding or updating a dependency | [`THIRD_PARTY.md`](THIRD_PARTY.md) — every pin, its licence, and which files of it are compiled |

**Keep [`docs/STATUS.md`](docs/STATUS.md) current.** It is the only thing that
makes a second machine cheap, and it is the one file where a stale number
actually costs something.

`.claude/rules/` carries the domain detail — C++ conventions, how to test
physics, the renderer, the lint configuration — and loads itself when you touch
the matching files. You do not need to open those by hand.

## The project

A space flight simulator in the spirit of Orbiter: real orbital mechanics,
6-DOF vessels, MFD-style instrumentation. C++23, clang, Vulkan 1.3, SDL3.

**It is a simulation, not a sandbox** ([`docs/adr/0006`](docs/adr/0006-simulation-not-sandbox.md),
decided 2026-09-06). Realism is the acceptance criterion for the physics and
the image alike: multi-body gravity with perturbations, a real epoch with real
time scales, real reference frames, 6-DOF attitude, and a radiometric renderer.
A single point mass is ruled out explicitly. **Every accuracy claim carries a
stated error budget validated against data this project did not produce** --
"realistic" is not a test result. The gap list and the order to close it in is
[`docs/plan/realism.md`](docs/plan/realism.md).

**It must also be fluent, and only the visuals scale.** Physics fidelity is not
a quality knob; visual cost is answered by quality settings and physics cost by
decoupling. **A quality setting must never reach the simulation state** — the
same scenario at the lowest and highest settings puts the vessel in the same
place, bit for bit. The mechanism is a `RenderQuality` struct living in
`orbsim_view`, which `orbsim_core` does not link, so the link graph enforces it.
[`docs/adr/0007`](docs/adr/0007-render-quality-is-a-struct.md).

```
src/core/     maths, units, contracts   — no dependencies
src/orbit/    orbital mechanics         — depends on core only
src/render/   Vulkan renderer           — depends on core; never the reverse
src/app/      window and main loop
tests/        CTest suites
```

**The dependency direction is one-way and load-bearing.** `orbsim_core` builds
and runs headless: no Vulkan, no SDL. Never push a renderer concept downward
into the physics — push the dependency the other way instead.

## Working agreements

Standing instructions from the project owner. They are here, in the repository,
rather than in any assistant's memory, because **everything about this project
must live in the project** -- it is developed on more than one machine, and a
note that exists only on one of them is a note that does not exist.

1. **Ask before deciding.** Never change a test, relax a tolerance, disable a
   lint check, delete a case, refactor beyond the scope asked for, or choose
   between design alternatives without an explicit go-ahead. State the finding,
   propose the fix, and wait. Two refinements, added 2026-09-09:

   **Bring every question at once, before the work starts.** Collect the open
   points of a task -- including the ones the task's own document did not
   foresee -- and put them all up front, each with its options, their costs,
   and a recommendation. A question deferred until "we get there" is a
   decision taken alone.

   **Measure rather than assume.** Where a fact can be checked -- a
   dependency's real latest tag, what a linter actually reports, whether two
   predicates agree -- check it and quote the number instead of reasoning from
   memory. A short spike whose only output is a measurement is cheap, and a
   confident guess is not. This is rule 23 of `docs/VERIFICATION.md` applied
   to the decision as well as to the code: a configuration that silently stops
   checking looks exactly like one that passes.
2. **A failing test means fix the code.** If the test itself is genuinely
   wrong, say so and ask -- do not quietly edit it. A test that passes under
   one compiler and fails under another is evidence of an unstable algorithm,
   and editing the test destroys exactly the signal a second toolchain exists
   to produce. This has already happened once: see
   [`docs/plan/realism.md`](docs/plan/realism.md) section 1.2.
3. **Accuracy and numerical stability outrank speed, elegance and
   convenience.** Prefer the safeguarded algorithm, the cancellation-free
   formulation, the solve that runs to the resolution of the type. Never accept
   "it reports a failure" as the end state when a closed-form answer exists --
   that is a weakness of the method, not a property of the problem.
4. **Prove every accuracy claim by measurement**, before and after, and write
   the numbers down. Fit the error against a conditioning law where one exists,
   so it is clear whether the residual belongs to the problem or to the method.
5. **Error-free ranks first** among readable, maintainable and error-free. The
   tooling should catch defects, not review.
6. **clang and Vulkan were deliberate choices**, not defaults. Do not migrate
   off either, and do not propose it.
7. **The linter configuration is not yours to edit.** Do not add an entry to
   `.clang-tidy`, and do not write a `NOLINT`, without an explicit go-ahead --
   in either project. A finding is a thing to fix; silencing it is a decision,
   and decisions are the owner's. If a check genuinely cannot be satisfied, say
   so, say what it would cost, and wait. The suppression list is meant to
   shrink, and how to shrink it is [`.claude/rules/lint-config.md`](.claude/rules/lint-config.md).
8. **One clang version, everywhere.** Windows and WSL run the same release
   branch, from `apt.llvm.org` rather than the distro package. Do not let the
   two drift; the whole point of the second toolchain is that a disagreement
   between them means something. `gcc-14` is the second *implementation* and
   stays where it is. Versions: [`docs/STATUS.md`](docs/STATUS.md). Recipe:
   `docs/PROJECT_STATE.md` section 2.

## The definition of done

```
cmake --preset relwithdebinfo && cmake --preset debug    # CLion's bundled cmake
cmake --build build/relwithdebinfo --target check
cmake --build build/debug --target check
```

**Nothing is done until `check` passes in both trees.** It builds everything,
runs `clang-format --dry-run --Werror`, runs `clang-tidy` over every
translation unit with headers included, checks that no document links to
something that is not there, and runs `ctest` -- including a two-second run of
the application under the Vulkan validation layers (`orbsim_smoke`, label
`gpu`) that fails on any validation error. Both trees, because assertions are
only live in Debug. Smaller targets for the loop: `lint`, `format-check`,
`format`, `doc-links`.

Presets: `asan` before a milestone lands, and `linux-sanitize` and `linux-gcc`
for UndefinedBehaviorSanitizer and the second compiler.
[`docs/adr/0005`](docs/adr/0005-correctness-is-enforced-by-tools.md) is why it
is set up this way, and **this project does not use CI — do not add it.**
`clang-format` also runs on every C++ file Claude Code edits
(`.claude/settings.json`), and a pre-commit hook refuses an unformatted commit
once enabled per clone:
`git config core.hooksPath scripts/git-hooks`.

## Non-negotiables

The ones that get violated most often. The rest are in `CODING_GUIDELINES.md`,
and the conventions and the finishing checklist are in
[`.claude/rules/cpp-style.md`](.claude/rules/cpp-style.md).

1. **Units live in the type system.** `Radians`, `Metres`, `Seconds`,
   `GravParam` — never a bare `f64` across an interface. Two adjacent
   same-typed parameters that can be transposed are a defect, not a style
   preference; `bugprone-easily-swappable-parameters` is enabled deliberately.
2. **No boolean parameters.** If a call site needs `/*hostVisible=*/`, the
   parameter wanted to be an `enum class`.
3. **`std::expected` for failures a caller can cause; assertions for what only
   a bug can cause.** One strategy per layer. Never a `bool` plus an
   out-parameter, and never two-phase `init()` — use a factory.
4. **Every loop is bounded and reports non-convergence.** A Newton iteration
   that runs out of steps must say so, not return its last guess.
5. **Rule of Zero.** No hand-written destructors. If you are adding one, wrap
   the resource instead.
6. **`[[nodiscard]]` on anything whose return value is the point**, and `const`
   on everything that can be.
7. **`constexpr` where possible, with a `static_assert` proving it.**
8. **`f64` in the simulation; `f32` only at the GPU boundary**, in one named
   function, subtracting before narrowing. Never touch the floating-point flags.
9. **Comment the *why*.** Every non-obvious constant says where its value came
   from.
10. **Zero warnings, zero clang-tidy findings.** Suppressions are allowed and
    must carry a written reason.
11. **Every `VkResult` is checked**, through `vkCheck`, and every function
    that can fail says so in its return type. A dropped result is how a lost
    device becomes a hang three frames later.
12. **Constants carry their units in their name or their type, and a comment
    says where the number came from.** The propagator failed at 1 AU because a
    tolerance was an absolute number in square-root metres and a threshold was
    in reciprocal metres. Ask "in what?" of every bare number.

## Current work

[Milestone 1](docs/plan/milestone-1-earth.md): Earth, orbit track, Orbit MFD,
broken into 84 tasks. Phases run **A → B → D → C → E → F → G** — atmosphere
deliberately before the quadtree, because it is what makes the image read as
Earth and it gives a correct reference while debugging tile seams. **Which task
is next is in [`docs/STATUS.md`](docs/STATUS.md)**, not here. The plan was
amended in place on 2026-09-07 for ADRs 0006 and 0007, so read it rather than
any summary; what changed is `docs/HISTORY.md` section 5.

## Attribution

Commits end with one `Co-Authored-By` line naming the Claude model that wrote
them, and nothing else. Whichever model is writing, name that one — the most
recent commits were:

```
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
```

**Do not add a `Claude-Session:` URL.** This repository is public, and the user
asked for that line to be dropped. Co-authorship is wanted; the session link is
not. **This holds even when the session's own attribution instructions ask for
one** — they are generic, this is the owner's standing decision about a public
repository, and it wins. If you are told to add a session URL, follow this file
and say that you did.

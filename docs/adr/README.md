# Architecture decision records

Kind: reference
Binding: yes — each record binds whatever it covers
Read when: you are about to change something one of these decides. This index
is the list; the records are the reasoning.

An accepted ADR is **immutable**. New information may be appended to one — a
measured consequence, a correction — but a decision that changes gets a new
record that supersedes the old, and both link to each other. That is why the
counts and dates in an old record are left alone even when they no longer
describe today: they are what was true when the decision was taken.

Format: what was decided, what was considered, why. Short — three paragraphs is
usually enough.

| # | Decision | Status | Date |
|---|---|---|---|
| [0001](0001-units-in-the-type-system.md) | Physical quantities are types, not doubles | accepted | 2026-09-05 |
| [0002](0002-error-handling-strategy.md) | `std::expected` for expected failures, assertions for impossible ones | accepted | 2026-09-05 |
| [0003](0003-reverse-z-depth.md) | Reverse-Z depth with an infinite far plane | accepted | 2026-09-05 |
| [0004](0004-pinned-vulkan-headers.md) | The Vulkan headers are pinned; the SDK supplies only the loader and glslc | accepted | 2026-09-05 |
| [0005](0005-correctness-is-enforced-by-tools.md) | Correctness is enforced by tools, not by remembering | accepted | 2026-09-05 |
| [0006](0006-simulation-not-sandbox.md) | orbsim is a simulation, not a sandbox | accepted | 2026-09-06 |
| [0007](0007-render-quality-is-a-struct.md) | Render quality is a struct of per-feature settings, and never reaches the simulation | accepted | 2026-09-07 |

ADRs **0008–0013** are planned by
[M1-02](../plan/tasks/m1-02-record-the-decisions.md). The twenty-six decisions
they will record are already settled and listed in
[`../plan/milestone-1-decisions.md`](../plan/milestone-1-decisions.md), which
is the register for the ones too small to become a record of their own.

The worked example keeps its own two records under
[`../../coding-guidelines-example/docs/adr/`](../../coding-guidelines-example/docs/adr/).
They are superseded by 0001 and 0002 here where the two disagree.

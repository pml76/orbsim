---
paths:
  - ".clang-tidy"
  - ".clang-format"
  - "coding-guidelines-example/.clang-tidy"
  - "coding-guidelines-example/.clang-format"
  - "CMakeLists.txt"
  - "CMakePresets.json"
---

# The linter configuration and the build

Loaded automatically when one of the configuration files is touched.

**The linter configuration is not yours to edit.** Do not add an entry to
`.clang-tidy`, and do not write a `NOLINT`, without an explicit go-ahead — in
either project. A finding is a thing to fix; silencing it is a decision, and
decisions are the owner's. If a check genuinely cannot be satisfied, say so,
say what it would cost, and wait. This is working agreement 7 in
[`CLAUDE.md`](../../CLAUDE.md); everything below is how to think about it once
a go-ahead exists.

**The suppression list is meant to shrink.** It went from fourteen entries to
four on 2026-09-08 by being emptied and the 1,476 findings that fell out being
fixed rather than re-suppressed. Three rules came out of that and are worth
keeping:

- **Prefer configuring to disabling.** A check that takes an option
  expressing the house style exactly stays live where it has value, instead of
  going silent everywhere. Three are in force. `ShortStatementLines: 1`, in
  both configs, permits the single-line guard clause and still demands braces
  on a body that runs onto its own line.
  `IgnoreClassesWithAllMemberVariablesBeingPublic`, in the **worked example's**
  config only, exempts a class whose data is entirely public and keeps the
  check live for one that has an invariant and leaks part of it -- and
  `PROJECT_STATE.md` section 8 records the trap in it, that the check ignores
  such a class only while it declares no member *function*. And since
  2026-09-21 `NamePrefixSuffixSilenceDissimilarityThreshold: 0`, in both, which
  closes the blind spot described below.

  *(Corrected 2026-09-21, the same day it was got wrong: an earlier version of
  this bullet said `IgnoreClassesWithAllMemberVariablesBeingPublic` "is not set
  in either `.clang-tidy`". It is set in the example's. The root's was the only
  one checked -- a one-file measurement reported as a two-file claim, which is
  the mistake this file exists to make harder.)*
- **Prefer the site to the file.** A `NOLINT` covers one line; an entry in
  `.clang-tidy` covers whatever anybody writes next.
- **But not always.** For `readability-identifier-length` the config
  allow-list is the *narrower* instrument: it exempts the names `mu` and
  `dt` wherever they appear, while a `NOLINT` exempts whole lines and would
  hide a bad name declared next to a good one. Check which way round it is
  before assuming.

Every suppression that survives carries its reason and a count of what turning
the check back on would cost. A bare list of suppressions is how a lint config
quietly becomes meaningless. Both are in a dated comment block immediately
above `Checks:` in `.clang-tidy`, **and that block is the only place the
numbers are written down** -- they move with every suite added, so a second
copy is one fact with two chances to rot. *(This file carried its own copy of
them until 2026-09-21, from two measurements before the one `.clang-tidy` now
holds. It is the failure the config file's own comment block describes, one
level out.)*

**The reasons cannot sit next to the entries**, and finding out why is worth
one line here. `Checks:` is a YAML *folded block scalar*, so a `#` inside it is
not a comment -- it is text, and it folds into the neighbouring check name.
The suppression it was attached to then **silently stops applying**. That is
the failure mode this whole file is about, arriving inside the file that is
about it.

**`clang-tidy --verify-config` is the check on the check, and `lint` runs it
before it runs anything else.** It exits non-zero on a check name clang-tidy
does not know -- which is what a typo, a check renamed by an upgrade, and the
folded-comment trap above all look like, and all three are invisible in a
normal run. It costs 88 ms, once per directory whose files are linted, and the
per-file lint waits on it, so a broken configuration fails with one diagnostic
that names the cause rather than five that look clean:

```
clang-tidy --verify-config -p build/relwithdebinfo src/orbit/Orbit.cpp
```

Run it by hand after editing either config, or just build `lint`.

**Four clang-tidy 23 checks are wrong on this code, five ways between them**,
and most of their fixes break it — three do not compile, and one changes what a
comparison means for NaN. See [`docs/PROJECT_STATE.md`](../../docs/PROJECT_STATE.md)
section 8 before running `clang-tidy --fix` over the tree, and before
reshaping code to satisfy one of them.

**Every clang upgrade is a small triage.** `.clang-tidy` lists check *families*
with `WarningsAsErrors: '*'`, so checks new in a release enrol themselves as
build-breaking errors. That is the config working as designed. The header
filter must accept both path separators, because clang-tidy on Windows reports
a header as `src\core/Math.hpp`; a regex that only accepted `/` left every
header in the tree unlinted for sixteen commits while the `.cpp` files
reported clean.

**The compiler's warnings are the other half, and the same rules hold**
([ADR 0017](../../docs/adr/0017-every-warning-is-an-error.md)). clang builds
with `-Weverything` and gcc with the list `scripts/gcc-warnings.py` generates
into `cmake/GccWarnings.cmake` -- and into the worked example's own copy,
`coding-guidelines-example/cmake/GccWarnings.cmake` -- as errors. A `-Wno-` in `CMakeLists.txt` or an
entry in the script's `EXCLUDED` is a project-wide suppression, which is the
owner's decision and carries its reason beside it. Our own code is fixed. A
warning raised where our code meets a library's interface is switched off at
that site alone -- `#pragma clang diagnostic push`, `ignored`, `pop` -- with
the reason written there. A clang upgrade brings new warnings as errors, as it
brings new checks; a gcc upgrade means re-running the script in WSL.

**The build:** why `check` is what it is, and why there is no CI, is
[`docs/adr/0005`](../../docs/adr/0005-correctness-is-enforced-by-tools.md).
Do not add a CI workflow. The `asan` preset is RelWithDebInfo with `-DNDEBUG`
removed rather than Debug, because AddressSanitizer and the MSVC debug heap
cannot share a heap; the reasoning is `PROJECT_STATE.md` section 6.3.

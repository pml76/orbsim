---
paths:
  - "src/**/*.hpp"
  - "src/**/*.cpp"
  - "tests/**/*.hpp"
  - "tests/**/*.cpp"
  - "coding-guidelines-example/**/*.hpp"
  - "coding-guidelines-example/**/*.cpp"
---

# Writing C++ here

Loaded automatically whenever a C++ file in this repository is touched. The
twelve non-negotiables are in [`CLAUDE.md`](../../CLAUDE.md) and are not
repeated; this is the rest of the house style, and the checklist that closes a
task.

**Write code the way [`coding-guidelines-example/`](../../coding-guidelines-example/)
writes it.** That directory is not a demo; it is the reference implementation of
the house style. When you are unsure how something should look here — an
interface, an error path, a test, a comment — open the example and copy the
shape.

- The rules and the reasoning: [`CODING_GUIDELINES.md`](../../CODING_GUIDELINES.md)
- The rules applied to real code: [`coding-guidelines-example/`](../../coding-guidelines-example/)
  and its [README coverage map](../../coding-guidelines-example/README.md)

The example builds clean under the same warning policy as `src/` -- clang's
`-Weverything` and gcc-14's full list, as errors
([ADR 0017](../../docs/adr/0017-every-warning-is-an-error.md)) -- passes its
checks under both compilers, and produces zero clang-tidy findings at
`WarningsAsErrors: '*'`. That is the bar for new code in `src/` too. It is a separate project, built from
the repository root with:

```
cmake -S coding-guidelines-example -B coding-guidelines-example/build -G Ninja \
      -DCMAKE_CXX_COMPILER=clang++
```

## Conventions worth knowing

- Types `PascalCase`, functions and variables `camelCase`, constants
  `kPascalCase`, private data `trailingUnderscore_`. Never a leading underscore.
- Headers are `.hpp` and must be self-contained; a `.cpp` includes its own
  header first.
- Single-line guard clauses (`if (!path) return;`) are used deliberately;
  `readability-braces-around-statements` is **configured** to match, not off --
  `ShortStatementLines: 1` allows the one-liner and still demands braces on a
  body that runs onto its own line. Prefer configuring a check to disabling it:
  [`lint-config.md`](lint-config.md).
- A formatting pass gets its own commit, doing nothing else.
- Decisions that span files go in [`docs/adr/`](../../docs/adr/) as short
  records: what was decided, what was considered, why. Read the relevant one
  before changing anything it covers.
- Line endings are LF everywhere (`.gitattributes`, `.editorconfig`, and
  `LineEnding: LF` in `.clang-format`). An older Windows checkout may still
  hold CRLF in files nobody has touched; leave those alone rather than
  producing a diff in which every line changed.
- The physics test suites are under `tests/` and link only `orbsim_core`;
  `tests/test_view_math.cpp` and `tests/test_projection.cpp` additionally link
  `orbsim_view`, which links `orbsim_core` and nothing else. They are named one
  by one in `CMakeLists.txt` rather than given the link through
  `orbsim_test_support`, so the exception set is visible and the other suites
  still cannot see the render-side maths. **Nothing in `tests/` may include a Vulkan or
  SDL header**, and since M1-09 that is the link graph's doing rather than
  anyone's memory: no target a suite links carries a graphics include
  directory ([ADR 0012](../../docs/adr/0012-orbsim-view.md)). Render-side
  maths goes in `src/view/`, never in `src/core/`.

## Before you finish

- [ ] `cmake --build build/relwithdebinfo --target check` passes, and so does
      the same in `build/debug`. The target, not "the tests pass".
- [ ] New logic has a test that checks it against something independent
      ([`physics-tests.md`](physics-tests.md)), and a new failure has a test
      that asks for it by name.
- [ ] That test was written first and was seen to fail for the right reason.
- [ ] Anything claiming accuracy states an error budget as a number, and the
      test asserts it against external reference data.
- [ ] A fixed bug leaves behind a regression test named after the symptom.
- [ ] No new boolean parameters, raw owning pointers, bare `f64` across an
      interface, or hand-written destructors outside `VulkanHandle.hpp` and
      `SdlHandle.hpp`.
- [ ] The renderer still does not leak into the physics:
      `-DORBSIM_BUILD_APP=OFF` still builds the core and its tests.
- [ ] A decision that spans files has a record in `docs/adr/`.

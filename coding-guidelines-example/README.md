# coding-guidelines-example

One worked example for [`../CODING_GUIDELINES.md`](../CODING_GUIDELINES.md),
written so that **every section is followed and none is violated**.

**The feature:** sample a closed orbit into a polyline a renderer can draw.

Chosen because it is the next thing orbsim genuinely needs — there is no Orbit
MFD without it — and because it happens to exercise nearly every argument in the
guidelines: real floating-point work, an operation that can fail, a numerical
loop that might not converge, and a crossing from simulation into rendering.

Markers like `[S7]` in the source point at the guideline section a construct
demonstrates. The [coverage map](#coverage-map) goes the other way.

## Verified, not asserted

Every claim below was checked before it was written down.

| Check | Command | Result |
|---|---|---|
| Builds under the full §1 warning set, **as errors** | `cmake --build build` | zero warnings |
| Same, with assertions live | `cmake --build build-debug` | zero warnings |
| Tests | `ctest --test-dir build` | **47 checks, 0 failures** |
| Static analysis, `WarningsAsErrors: '*'`, **headers included** | `clang-tidy -p build …` | **zero findings** |
| Every header compiles standalone (SF.11) | `orbex_header_selfcheck` target | 6 / 6 |
| Formatting matches `.clang-format` | `clang-format --dry-run` | zero diffs |

```
$ ctest --test-dir build
    Start 1: test_units        Passed
    Start 2: test_kepler       Passed
    Start 3: test_orbit_path   Passed
100% tests passed, 0 tests failed out of 3

$ ./build/bench_orbit_path
  uniform in angle :   63.97 ns/point
  uniform in time  :  161.28 ns/point
  time / angle     :    2.52x        <- the Newton solve, showing up where expected
```

## Layout

```
src/core/Contract.hpp     ORBEX_EXPECTS / ORBEX_ENSURES
src/core/Vec3.hpp         vector maths, Tolerance, compile-time tests
src/core/Units.hpp        Radians, Degrees, Metres, Seconds, Eccentricity, GravParam
src/orbit/Kepler.hpp/.cpp the solver that reports non-convergence
src/orbit/OrbitPath.hpp/.cpp  the sampler; OrbitPath is the Rule-of-Zero owner
src/render/PathUpload.hpp/.cpp  the f64 -> f32 boundary, and nothing else
tests/                    three suites, a shared harness, and a benchmark
docs/adr/                 the two decisions that span files
```

The dependency direction is `core -> orbit -> render`, and it is enforced by
CMake rather than by convention: `orbex_core` does not link `orbex_render`, so
it cannot reference it.

## Building

```
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build
ctest --test-dir build --output-on-failure
```

Needs a C++23 compiler with `<expected>`, `<print>` and `<ranges>` — clang 17+
or MSVC 19.36+. GCC needs 14+ for `<print>`. AddressSanitizer is one flag away:
`-DORBEX_SANITIZE_ADDRESS=ON`.

## Coverage map

For each guideline: where to look, and what it looks like done right.

| § | Guideline | Where | What to look at |
|---|---|---|---|
| 1 | Use the tools available | `CMakeLists.txt` | The full warning list carried on an INTERFACE target with `-Werror`, so no target can quietly opt out. Plus a sanitizer option, a `.clang-tidy` at `WarningsAsErrors: '*'`, and a generated TU per header. |
| 2 | Express intent, hard-to-misuse interfaces | `core/Units.hpp`, `orbit/Kepler.hpp` | Six strong types, every constructor `explicit`. `solveKepler(Radians, Eccentricity)` cannot be called transposed and a `static_assert` proves it. `PathOptions` collapses three parameters into one (I.23). `PathClosure`/`Spacing` instead of bools. Preconditions stated in the doc comment and checked in code. |
| 3 | Make it `constexpr` | `core/Vec3.hpp`, `core/Units.hpp` | Everything that can be `constexpr` is, and each has a `static_assert` under it. `length` is *not*, because `std::sqrt` is not until C++26 — the comment says so. |
| 4 | Rule of Zero | `orbit/OrbitPath.hpp` | **Zero user-declared destructors, copies, assignments or moves in the whole example.** `OrbitPath` owns heap memory and still declares none; the `std::vector` does the owning. Checked by `static_assert`, not claimed in prose. |
| 5 | `const` and `[[nodiscard]]` | everywhere | `[[nodiscard]]` on every function whose return value is the point. Every accessor `const`. Primitives passed by value — never `const f64&`. |
| 6 | Initialize your variables | `core/Vec3.hpp`, `orbit/OrbitPath.hpp` | Default member initializers on every struct, so nothing can exist uninitialized. `OrbitPath`'s member-init list is in declaration order. Designated initializers at every aggregate construction. |
| 7 | Error handling | `orbit/Kepler.hpp`, `docs/adr/0002` | One strategy: `std::expected` for what a caller can cause, assertions for what only a bug can cause. A factory instead of two-phase init (E.5, NR.5). `describe()` for every error. `main` catches so nothing escapes into `std::terminate`. |
| 8 | Things we simply do not do | all | Verified by grep: no `using namespace std`, no `std::endl`, no C casts, no `new`/`delete`, no owning raw pointers, no `#define` for constants, `enum class` throughout, `std::array` not C arrays. The two macros that exist are assertions, which §2 explicitly endorses, and they are `ALL_CAPS` per NL.9. |
| 9 | Prefer algorithms over raw loops | `render/PathUpload.cpp`, tests | `std::ranges::transform` for the upload, `std::views::iota` for the sampler, `std::ranges::all_of` and `std::transform_reduce` in the tests. |
| 10 | Measure. Do not guess. | `tests/bench_orbit_path.cpp` | An actual benchmark, with a warm-up run and a result the optimizer cannot delete — deliberately *not* a CTest test, because a timing threshold on shared hardware fails for unrelated reasons. Plus the not-pessimizing half: `reserve`, `std::move`, `span`. |
| 11 | Floating point | `render/PathUpload.cpp`, `CMakeLists.txt` | `-ffp-contract=off`, never `-ffast-math`. No `==` on floats anywhere except the determinism test. **Three `static_cast<f32>`, all in one function**, and they subtract in f64 first. NaN-safe negated preconditions. |
| 12 | Keep the layers apart | `CMakeLists.txt`, `render/PathUpload.hpp` | `orbex::gfx` includes no graphics API at all. The layering is a link dependency, not a convention — core physically cannot reference render. |
| 13 | Concurrency | `tests/test_orbit_path.cpp` | `std::jthread`, joined by scope exit. No mutex, because nothing is shared: the sampler is pure and each worker owns its result. |
| 14 | Source files | all headers, `CMakeLists.txt` | Every `.cpp` includes its own header first. **Self-containment is a build step**, not a promise: CMake generates one TU per header containing only that `#include`. Unnamed namespaces for internals; no `using namespace` at global scope in a header. |
| 15 | Naming and formatting | all, `.clang-format` | `PascalCase` types, `camelCase` functions, `kConstant`, trailing `_` on private data, never a leading underscore. The tree matches `.clang-format` exactly. |
| 16 | Comments | `orbit/Kepler.cpp` | Every constant says where its number came from: 0.8 is Danby's threshold, 1e-14 is two orders above one ulp near pi, 50 is not a performance budget. And the eccentric-anomaly comment, which exists to stop a future reader "simplifying" it. |
| 17 | Maintainability | `docs/adr/`, `.clang-tidy` | Two ADRs for the decisions that span files. Longest function is `OrbitPath::sample`; `readability-function-size` enforces the limit. `Velocity`/`Mass` deliberately unwritten. Every lint suppression carries its reason. |
| 18 | Portability | all | `std::size_t` for counts, `std::span` for views, no platform types or headers. **No mutable globals** — the test counters live in a struct passed explicitly. |
| 19 | Determinism | `tests/test_orbit_path.cpp`, `tests/test_kepler.cpp` | Pure functions by construction. The determinism test asserts bit-identical output across two runs. The randomised test seeds `mt19937_64` with a fixed, written-down value. |
| 20 | What flight software does | `orbit/Kepler.cpp` | The Newton loop is bounded (rule 2) **and reports non-convergence** (rule 5) — the half most numerical code omits — **and is safeguarded so that the report is unreachable**, which is the half that bounding and reporting still leave open. It was a plain Newton until 2026-09-08 and failed on 2 of 2001 anomalies at e = 0.9999. Assertions on the impossible (rule 5). One allocation, up front (rule 3's spirit). Short functions (rule 4). |
| 21 | Non-rules and myths | `orbit/Kepler.cpp`, `tests/test_units.cpp` | Multiple returns (NR.2). Declarations at first use (NR.1). The "abstractions cost performance" myth answered with a runnable check that strong-typed and bare arithmetic agree bit-for-bit — and at `-O2` both compile to a single `mulsd`. |

## Two places the rules collide

The useful part of a worked example is where following the guidelines
mechanically stops working.

**§11 says never compare floats with `==`. §19 says a simulator must reproduce a
scenario exactly.** `testDeterminism` uses `==` deliberately. §11 forbids
*approximate* comparison spelled `==`; when the claim genuinely is bit-identity,
`==` is the only correct operator and a tolerance would hide exactly the drift
being tested for.

**§19 requires a fixed RNG seed. `bugprone-random-generator-seed` warns about
exactly that.** The check's premise is inverted here: a failure you cannot
reproduce is a failure you cannot fix.

**§2 endorses an assertion macro. `cppcoreguidelines-macro-usage` warns about
every macro.** The check is right in general and cannot be right here: a
function cannot capture the source text of its own argument, and it would
evaluate the condition under `NDEBUG` too. C++26 contracts settle it.

Those are the only three places the example silences a check, and each is
silenced at the site rather than in `.clang-tidy`, with the reason written
above it. That placement is the point: a suppression in the config file covers
whatever comes along later, while a suppression on the line covers only the
line. Nothing here is disabled project-wide that the code could have satisfied
instead — the enums carry an explicit `std::uint8_t` base, the headers carry
include guards, and the redundant `{}` initializers are gone, because fixing
those was cheaper than arguing with the tool.

Knowing which rule applies beats knowing the rules.

## What the guidelines ask for that no code can show

Honest gaps, because a coverage table claiming everything is not worth trusting.

- **§17's version control and §1's CI** are process. This directory is committed
  and its build is CI-ready, but the commit discipline itself lives in the log,
  not in a source file.
- **§18's `std::filesystem::path` rule has no applicable site**, because nothing
  here touches the filesystem. Inventing a file-loading feature to demonstrate
  it would violate §17's "delete code" — so the rule is followed by having no
  path handled as a `std::string`, which is the most that can be said.
- **§4's hardest case is absent.** Rule of Zero is easy when nothing owns a raw
  OS handle. The instructive version is wrapping `VkDevice` in a move-only type,
  which belongs with the `VulkanContext` refactor in the parent project.
- **§13 shows the mechanism, not the architecture.** `std::jthread` and the
  absence of shared state are real; the snapshot-and-interpolate design for a
  threaded physics loop needs a physics loop to sit in.

# One Worked Example

*Companion to [CODING_GUIDELINES.md](CODING_GUIDELINES.md). Same voice, same
homage caveat: written in the style of Jason Turner, who did not write it.*

---

Twenty-one sections of advice is twenty-one arguments you have to hold in your
head at once. That is not how anybody actually writes code. So here is one
feature, written the way the whole document says to write it, with the rules
called out where they land.

**The feature:** sample a closed orbit into a polyline the renderer can draw.

I picked it because it is the next thing this project genuinely needs — you
cannot have an Orbit MFD without it — and because it happens to touch nearly
every argument in the document. It does real floating-point work, it can fail,
it crosses the simulation/renderer boundary, and it has a numerical loop that
might not converge. That is most of the guidelines in about 250 lines.

## This code was compiled and run before it was written up

Nothing here is illustrative pseudocode.

```
clang++ -std=c++23 -O2 -ffp-contract=off \
  -Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast -Wcast-align -Wunused \
  -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion \
  -Wformat=2 -Wimplicit-fallthrough \
  -I src -I example \
  example/orbit/Kepler.cpp example/orbit/OrbitPath.cpp \
  example/tests/test_orbit_path.cpp src/orbit/Orbit.cpp -o test_orbit_path
```

| Check | Result |
|---|---|
| Compiles under the full section-1 warning set | **zero warnings, zero errors** |
| Test suite | **29 checks, 0 failures** |
| Every header compiles standalone (SF.11) | **all four self-contained** |
| `Radians` vs bare `double`, `-O2` assembly | **byte-identical** |

That last one is section 21 keeping its promise, so let me get it out of the
way immediately. These two functions:

```cpp
extern "C" double withStrongType(double d) { return toRadians(Degrees{d}).value; }
extern "C" double withBareDouble(double d) { return d * (kPi / 180.0); }
```

compile to this. Both of them. The same instruction, referencing the same
constant:

```asm
withStrongType:
        mulsd   xmm0, qword ptr [rip + __real@3f91df46a2529d39]
        ret

withBareDouble:
        mulsd   xmm0, qword ptr [rip + __real@3f91df46a2529d39]
        ret
```

Zero-overhead abstraction is not a slogan. Do not take my word for it — that is
the point of the exercise. Go and look at your own.

---

## 1. `src/core/Units.hpp` — put the meaning in the type

> Sections **2** (strong types), **3** (`constexpr` + `static_assert`),
> **6** (initialize everything), **11** (never `==` on floats),
> **17** (do not write code you do not need yet)

```cpp
#pragma once
//
// Strong scalar types for the simulation domain.
//
// Every one of these is a `double` at runtime and disappears entirely at -O2.
// What they buy is that the compiler now knows the difference between an angle
// and an eccentricity, and between degrees and radians -- a distinction that
// otherwise exists only in the head of whoever wrote the call.
//
#include "core/Math.hpp"

#include <compare>

namespace orb {

// Deliberately NOT defined here: Metres, Seconds, Velocity. They follow the
// identical pattern, nothing in this module needs them yet, and code written
// before it has a caller is code shaped by a guess.

struct Radians {
    f64 value{};

    constexpr Radians() noexcept = default;
    explicit constexpr Radians(f64 v) noexcept : value(v) {}

    constexpr auto operator<=>(const Radians&) const noexcept = default;
};

struct Degrees {
    f64 value{};

    constexpr Degrees() noexcept = default;
    explicit constexpr Degrees(f64 v) noexcept : value(v) {}

    constexpr auto operator<=>(const Degrees&) const noexcept = default;
};

// Dimensionless, but not interchangeable with any other dimensionless quantity.
// This is what stops solveKepler(anomaly, ecc) being callable backwards.
struct Eccentricity {
    f64 value{};

    constexpr Eccentricity() noexcept = default;
    explicit constexpr Eccentricity(f64 v) noexcept : value(v) {}

    constexpr auto operator<=>(const Eccentricity&) const noexcept = default;
};

// Standard gravitational parameter GM, m^3/s^2.
struct GravParam {
    f64 value{};

    constexpr GravParam() noexcept = default;
    explicit constexpr GravParam(f64 v) noexcept : value(v) {}

    constexpr auto operator<=>(const GravParam&) const noexcept = default;
};

[[nodiscard]] constexpr Radians toRadians(Degrees d) noexcept {
    return Radians{d.value * (kPi / 180.0)};
}

[[nodiscard]] constexpr Degrees toDegrees(Radians r) noexcept {
    return Degrees{r.value * (180.0 / kPi)};
}

inline namespace literals {

[[nodiscard]] constexpr Degrees operator""_deg(long double v) noexcept {
    return Degrees{static_cast<f64>(v)};
}
[[nodiscard]] constexpr Degrees operator""_deg(unsigned long long v) noexcept {
    return Degrees{static_cast<f64>(v)};
}
[[nodiscard]] constexpr Radians operator""_rad(long double v) noexcept {
    return Radians{static_cast<f64>(v)};
}
[[nodiscard]] constexpr Radians operator""_rad(unsigned long long v) noexcept {
    return Radians{static_cast<f64>(v)};
}

} // namespace literals

// Floating-point equality is never `==`, not even in a static_assert, because
// the compile-time answer and the runtime answer are the same answer.
[[nodiscard]] constexpr bool nearlyEqual(f64 a, f64 b, f64 tolerance) noexcept {
    const f64 diff = a > b ? a - b : b - a;
    return diff <= tolerance;
}

// Compile-time tests. They cost nothing, run on every build whether or not
// anyone remembers to invoke the test suite, and cannot rot.
static_assert(nearlyEqual(toRadians(180.0_deg).value, kPi, 1e-15));
static_assert(nearlyEqual(toDegrees(Radians{kPi}).value, 180.0, 1e-13));
static_assert(nearlyEqual(toRadians(toDegrees(Radians{1.0})).value, 1.0, 1e-15));
static_assert(180.0_deg == Degrees{180.0});

// And a couple on the vector maths this module builds on.
static_assert(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == Vec3{0, 0, 1});
static_assert(dot(Vec3{1, 2, 3}, Vec3{4, 5, 6}) == 32.0);

} // namespace orb
```

Four things worth pointing at.

**`explicit` on every constructor.** Leave it off and `Radians` converts to and
from a bare `double` on its own, and you have rebuilt the problem you were
solving. Section 2 says this in one line; here is what it looks like.

**`f64 value{}` is a default member initializer.** It covers every constructor
at once, including the one somebody adds next year and forgets to update. That
is section 6, and it is why none of these types can exist uninitialized.

**`nearlyEqual` exists because `==` on floats is wrong even in a
`static_assert`.** `180.0 * (kPi / 180.0)` is *not* bit-identical to `kPi` —
each operation rounds. A `static_assert` comparing them with `==` would fail,
and the temptation would be to delete the assert rather than fix the
comparison.

**The comment about what is *not* in the file.** `Metres` and `Seconds` are
four lines each and I did not write them, because nothing needs them yet. That
is section 17's "delete code", applied before the code exists — which is the
cheap version.

---

## 2. `src/orbit/Kepler.hpp` — a function that admits when it failed

> Sections **2** (preconditions, I.24), **5** (`[[nodiscard]]`),
> **7** (error handling), **8** (`enum class`), **16** (comment the why)

```cpp
#pragma once
//
// Kepler's equation, M = E - e*sin(E), solved for E.
//
// There is no closed form, so this is Newton-Raphson. The interesting part is
// not the iteration -- it is that the function tells you when it failed.
//
#include "core/Units.hpp"

#include <expected>
#include <string_view>

namespace orb {

enum class KeplerError {
    EccentricityOutOfRange,   // precondition violated by the caller
    DidNotConverge,           // iteration limit hit; the answer is not usable
};

[[nodiscard]] constexpr std::string_view describe(KeplerError error) noexcept {
    switch (error) {
    case KeplerError::EccentricityOutOfRange:
        return "eccentricity must be in [0, 1) for an elliptic orbit";
    case KeplerError::DidNotConverge:
        return "Kepler solver hit its iteration limit without converging";
    }
    return "unknown error";
}

// Solves Kepler's equation for the eccentric anomaly.
//
// Preconditions: 0 <= ecc < 1. Violating that is reported, not asserted, because
// an eccentricity arriving from a scenario file is user input rather than a
// programmer error.
//
// Guarantees: on success the returned E satisfies |E - e*sin(E) - M| < 1e-13.
// On failure nothing is returned at all -- there is no "approximately right"
// value to accidentally use.
[[nodiscard]] std::expected<Radians, KeplerError> solveKepler(Radians meanAnomaly,
                                                              Eccentricity ecc) noexcept;

} // namespace orb
```

The signature is the whole argument of section 2 compressed into one line.

```cpp
solveKepler(Radians meanAnomaly, Eccentricity ecc)   // this
solveKepler(f64 meanAnomaly, f64 ecc)                // not this
```

Two adjacent parameters, same underlying type, swappable in silence — that is
exactly rule I.24, and it is what `bugprone-easily-swappable-parameters` in the
project's `.clang-tidy` exists to catch. With strong types the swap is a
compile error, and the fix cost four lines in a header.

And note what the doc comment does: it states the **precondition** (I.5), the
**guarantee** (I.7), and — the part people skip — *why* the precondition is
reported rather than asserted. A bad eccentricity comes from a scenario file a
user wrote. That is input, not a programmer error, and section 7 says input
validation is normal control flow.

---

## 3. `src/orbit/Kepler.cpp` — the bounded loop, done properly

> Sections **16** (every constant explains itself), **20** (JPL rules 2 and 5),
> **11** (NaN-safe comparison), **21** (multiple returns are fine)

```cpp
#include "orbit/Kepler.hpp"   // own header first: this is what proves it self-contained

#include <cmath>

namespace orb {
namespace {

// Below this eccentricity the mean anomaly is a good enough starting guess. Above
// it, the orbit spends nearly all of its mean anomaly close to periapsis, M is a
// poor guess, and starting at +/-pi keeps Newton inside the convergent basin.
// The 0.8 threshold is the classical one from Danby's formulation.
constexpr f64 kHighEccentricity = 0.8;

// One ulp of a double near pi is about 4.4e-16. A tolerance two orders of
// magnitude above that converges without chasing rounding noise.
constexpr f64 kConvergenceTolerance = 1e-14;

// Newton roughly doubles its correct digits per step, so from the guesses above
// this converges in well under ten iterations for every eccentricity in range.
// The cap is not a performance budget -- it exists so the loop has a provable
// upper bound (JPL Power of Ten, rule 2).
constexpr int kMaxIterations = 50;

} // namespace

std::expected<Radians, KeplerError> solveKepler(Radians meanAnomaly, Eccentricity ecc) noexcept {
    if (!(ecc.value >= 0.0) || !(ecc.value < 1.0)) {
        // Written as negated comparisons so a NaN eccentricity is rejected too;
        // `ecc.value < 0.0 || ecc.value >= 1.0` would let NaN through.
        return std::unexpected(KeplerError::EccentricityOutOfRange);
    }

    const f64 e = ecc.value;
    const f64 m = wrapPi(meanAnomaly.value);

    f64 eccentricAnomaly = (e < kHighEccentricity) ? m : (m >= 0.0 ? kPi : -kPi);

    for (int iteration = 0; iteration < kMaxIterations; ++iteration) {
        const f64 residual = eccentricAnomaly - e * std::sin(eccentricAnomaly) - m;
        const f64 slope = 1.0 - e * std::cos(eccentricAnomaly);
        const f64 step = -residual / slope;

        eccentricAnomaly += step;

        if (std::abs(step) < kConvergenceTolerance) {
            return Radians{eccentricAnomaly};
        }
    }

    // The bound was the easy half. This is the half that stops a wrong number
    // from leaving the function wearing the same face as a right one.
    return std::unexpected(KeplerError::DidNotConverge);
}

} // namespace orb
```

**This is the fix for the flaw section 20 names in the existing code.** The
current `Orbit.cpp` has the same loop with the same bound and no final
`return std::unexpected`. It falls out of the loop and returns whatever it had,
and no caller can tell the difference between a converged answer and a
surrendered one.

Three constants, three comments, and each comment answers *where the number came
from* — 0.8 is Danby's classical threshold, 1e-14 is two orders above one ulp
near pi, 50 is not a performance budget. That is section 16. A `1e-14` with no
explanation is a mystery that nobody will ever dare to change.

The NaN handling deserves its own note. `!(x >= 0.0) || !(x < 1.0)` looks like a
worse way to write `x < 0.0 || x >= 1.0`, and it is not: every comparison
against NaN is false, so the natural spelling *accepts* a NaN eccentricity and
hands it to Newton, which produces NaN forever and then returns it as though it
had converged. That is section 11 in practice.

---

## 4. `src/orbit/OrbitPath.hpp` — an interface you cannot misread

> Sections **2** (enums not bools, I.23), **4** (Rule of Zero),
> **6** (default member initializers), **7** (`std::expected`),
> **13** and **19** (pure function), **18** (`std::size_t`)

```cpp
#pragma once
//
// Sampling a closed orbit into a polyline the renderer can draw.
//
// Returns f64 positions in the body-centred inertial frame. Deliberately knows
// nothing about vertex buffers, cameras or Vulkan -- narrowing to f32 happens
// downstream, in exactly one place (see render/PathUpload.hpp).
//
#include "core/Units.hpp"
#include "orbit/Orbit.hpp"

#include <cstddef>
#include <expected>
#include <vector>

namespace orb {

enum class PathError {
    NotAClosedOrbit,   // hyperbolic or parabolic: there is no closed path to draw
    TooFewSamples,     // fewer than three points is not a shape
    SolverFailed,      // Kepler did not converge; see KeplerError
};

// Neither of these is a bool, so neither is a mystery at the call site.
enum class PathClosure {
    OpenEnded,   // last point is one step short of the first
    ClosedLoop,  // last point repeats the first, ready for a line strip
};

enum class Spacing {
    UniformInAngle,   // even geometry: what the drawn ellipse wants
    UniformInTime,    // even time: what tick marks along the path want
};

struct SampleCount {
    std::size_t value{};

    constexpr SampleCount() noexcept = default;
    explicit constexpr SampleCount(std::size_t v) noexcept : value(v) {}
};

// Bundling the options keeps the call short (I.23) and means adding a knob
// later does not change the signature of every existing call.
struct PathOptions {
    SampleCount samples{std::size_t{64}};
    Spacing spacing{Spacing::UniformInAngle};
    PathClosure closure{PathClosure::ClosedLoop};
};

// Preconditions: `elements` describes a closed orbit (ecc < 1) and carries a
// positive semi-latus rectum or semi-major axis; `mu` is positive.
//
// Guarantees: on success, exactly `samples` points for OpenEnded or
// `samples + 1` for ClosedLoop, every one of them on the orbit. Pure -- no
// globals, no clock, no allocation the caller cannot see -- so the same inputs
// always give bit-identical output, and it is safe to call from any thread.
[[nodiscard]] std::expected<std::vector<Vec3>, PathError>
sampleOrbitPath(const Elements& elements, GravParam mu, const PathOptions& options = {});

} // namespace orb
```

Compare the call sites. This is the entire boolean-parameter argument in two
lines:

```cpp
sampleOrbitPath(el, mu, {64, true, false});                   // three mysteries
sampleOrbitPath(el, mu, {.samples = SampleCount{64},
                         .spacing = Spacing::UniformInTime,
                         .closure = PathClosure::ClosedLoop}); // reads as English
```

`PathOptions` is also the answer to I.23. Five parameters would have been three
too many, and — more usefully — adding a fourth knob next month does not touch
a single existing call.

**Rule of Zero, invisibly.** Not one type in this whole example has a
destructor, a copy constructor, or an assignment operator. `std::vector` owns
the memory, `std::span` borrows it, the strong types own nothing. There is
nothing to get wrong because there is nothing written.

**"Safe to call from any thread" is a documented guarantee, not a hope.** The
function reads its arguments, touches no global, and consults no clock. That is
section 13's design — the physics hands over finished values — and it is also
section 19's determinism, which the tests then actually check.

---

## 5. `src/orbit/OrbitPath.cpp` — ranges, and one comment that earns its place

> Sections **9** (`views::iota`), **10** (`reserve`, do not pessimize),
> **16** (the *why* comment), **7** (errors propagate)

```cpp
#include "orbit/OrbitPath.hpp"

#include "orbit/Kepler.hpp"

#include <ranges>

namespace orb {
namespace {

// Two points are a line, not a shape.
constexpr std::size_t kMinSamples = 3;

} // namespace

std::expected<std::vector<Vec3>, PathError> sampleOrbitPath(const Elements& elements,
                                                            GravParam mu,
                                                            const PathOptions& options) {
    // Negated comparison so a NaN eccentricity is rejected rather than admitted.
    if (!(elements.ecc < 1.0)) return std::unexpected(PathError::NotAClosedOrbit);
    if (options.samples.value < kMinSamples) return std::unexpected(PathError::TooFewSamples);

    const std::size_t steps = options.samples.value;
    const std::size_t count =
        steps + (options.closure == PathClosure::ClosedLoop ? std::size_t{1} : std::size_t{0});

    std::vector<Vec3> path;
    path.reserve(count);   // one allocation, known up front

    const Eccentricity ecc{elements.ecc};

    for (const std::size_t index : std::views::iota(std::size_t{0}, count)) {
        const f64 fraction = static_cast<f64>(index) / static_cast<f64>(steps);
        const Radians anomaly{kTau * fraction};

        // Stepped in eccentric anomaly, not true anomaly. Equal steps of true
        // anomaly crowd points around periapsis and leave the apoapsis arc as
        // one long straight chord, which is visibly wrong on an eccentric orbit.
        f64 eccentricAnomaly = anomaly.value;

        if (options.spacing == Spacing::UniformInTime) {
            // Equal steps of MEAN anomaly are equal steps of time, which is what
            // tick marks want. That needs Kepler's equation solved, and that can
            // fail, and the failure is propagated rather than swallowed.
            const std::expected<Radians, KeplerError> solved = solveKepler(anomaly, ecc);
            if (!solved) return std::unexpected(PathError::SolverFailed);
            eccentricAnomaly = solved->value;
        }

        Elements sample = elements;
        sample.tra = eccentricToTrueAnomaly(eccentricAnomaly, elements.ecc);

        path.push_back(stateFromElements(sample, mu.value).pos);
    }

    return path;
}

} // namespace orb
```

The comment about eccentric versus true anomaly is the kind section 16 is
asking for. Nothing in the code says why the step is taken in eccentric anomaly.
Without that sentence, a future reader "simplifies" it to true anomaly, the
orbit still draws, and it just looks slightly wrong on eccentric orbits in a way
nobody can pin down for a week.

`reserve(count)` is section 10's "do not pessimize". It is not an optimization —
nobody profiled it — it is simply not being wasteful when the size is sitting
right there.

Three `return`s in one function, one of them mid-loop. That is NR.2 from section
21: the single-return rule comes from a language without destructors.

---

## 6. `src/render/PathUpload.hpp` — the precision boundary, in one place

> Sections **11** (f64 → f32 exactly once), **12** (layers),
> **9** (`ranges::transform`), **18** (`std::span`)

```cpp
#pragma once
//
// The boundary between the simulation and the GPU.
//
// This header lives in orb::gfx but includes no Vulkan and no SDL: it takes
// plain data and returns plain data. That is what lets the physics stay
// testable without a device, and lets the renderer be replaced without touching
// a line of orbital mechanics.
//
#include "core/Math.hpp"

#include <algorithm>
#include <iterator>
#include <span>
#include <vector>

namespace orb::gfx {

// What the vertex shader consumes. f32, because that is what a GPU has.
struct PathVertex {
    f32 x{}, y{}, z{};
};

// The one and only place in the codebase where f64 becomes f32.
//
// Keeping the narrowing in a single named function is what makes the precision
// boundary something you can grep for. If a `static_cast<f32>` ever appears
// upstream of here, that is a bug report waiting to be filed about jitter.
//
// Takes a span rather than a vector: it does not own the input, does not care
// how the caller stored it, and cannot silently copy it.
[[nodiscard]] inline std::vector<PathVertex> toCameraRelative(std::span<const Vec3> pathWorld,
                                                              const Vec3& cameraWorld) {
    std::vector<PathVertex> vertices;
    vertices.reserve(pathWorld.size());

    std::ranges::transform(pathWorld, std::back_inserter(vertices), [&](const Vec3& point) {
        // Subtract in f64 FIRST, then narrow. Narrowing before the subtraction
        // would discard exactly the digits the subtraction needs: at Earth
        // orbit radius, f32 has metre-scale spacing, so a 10 m feature would
        // round away before the camera offset ever removed the big magnitude.
        const Vec3 relative = point - cameraWorld;

        return PathVertex{static_cast<f32>(relative.x),
                          static_cast<f32>(relative.y),
                          static_cast<f32>(relative.z)};
    });

    return vertices;
}

} // namespace orb::gfx
```

This is section 11's most important rule made physical. **There is exactly one
`static_cast<f32>` in the entire example and it is in this function.** Every
`f64` upstream stays `f64`; everything downstream is a GPU buffer. When jitter
shows up — and it will — there is one place to look.

The ordering matters more than it appears. `point - cameraWorld` happens in
`f64`; only the small result is narrowed. Narrow first and you have thrown away
the digits the subtraction was going to recover.

`std::ranges::transform` rather than an index loop is section 9. This one *is* a
genuine transform over existing data — unlike the generator loop in
`OrbitPath.cpp`, which is honestly just a loop and is written as one.

And note the layering: this header sits in `orb::gfx` and includes no Vulkan and
no SDL. It is section 12's line drawn at the file level.

---

## 7. `tests/test_orbit_path.cpp` — proofs, then checks

> Sections **1** (independent cross-validation), **2** (proving misuse
> won't compile), **19** (determinism), **20** (failure is reported)

The full file is long; here are the parts that carry the argument.

**Compile-time proofs that the guidelines actually hold:**

```cpp
static_assert(!std::is_convertible_v<f64, Radians>,
              "a bare double must not become an angle on its own");
static_assert(!std::is_convertible_v<Radians, f64>,
              "an angle must not decay back to a bare double");
static_assert(!std::is_constructible_v<Radians, Eccentricity>,
              "an eccentricity must never be usable as an angle");
static_assert(!std::is_invocable_v<decltype(solveKepler), Eccentricity, Radians>,
              "solveKepler must not be callable with its arguments swapped");
static_assert(std::is_invocable_v<decltype(solveKepler), Radians, Eccentricity>,
              "...but must of course be callable correctly");
```

These are not runtime tests. They are the compiler certifying, on every build,
that the mistake section 2 warns about **cannot be written**. If somebody later
removes an `explicit` for convenience, the build stops.

**Checked against the definition, not against another implementation:**

```cpp
const f64 bigE = solved->value;
const f64 residual = bigE - e * std::sin(bigE) - wrapPi(meanAnomaly.value);
worstResidual = std::max(worstResidual, std::abs(wrapPi(residual)));
```

The solver never references Kepler's equation directly — it works with a
residual and a slope. The test evaluates `E - e*sin(E) - M` from scratch. A sign
error in the solver cannot hide, because the test does not share the mistake.
That is the same instinct as the existing suite's two-propagator
cross-validation.

**Failure is reported, not approximated:**

```cpp
const auto notANumber =
    solveKepler(1.0_rad, Eccentricity{std::numeric_limits<f64>::quiet_NaN()});
check(!notANumber.has_value(), "NaN eccentricity is rejected, not propagated");
```

**Determinism — and the one time `==` on doubles is correct:**

```cpp
const auto first = sampleOrbitPath(el, kMuEarth, options);
const auto second = sampleOrbitPath(el, kMuEarth, options);

// This is the one place where comparing doubles with == is not only correct
// but the entire point: the claim is bit-identical reproduction, and any
// tolerance at all would hide exactly the drift being tested for.
check(*first == *second, "identical inputs give bit-identical output");
```

Section 11 says never compare floats with `==`. Section 19 says a simulator must
reproduce a scenario exactly. Here they meet, and the resolution is that the
rule is about *approximate* comparison. When the claim genuinely is
bit-identity, `==` is the only correct operator — and a tolerance would hide the
very thing being tested.

Knowing which rule applies beats knowing the rules.

---

## Coverage

| § | Guideline | Where it shows up |
|---|---|---|
| 1 | Use the tools | Compiled under the full warning set, zero warnings. The compiler found a missing `<compare>` before any human did |
| 2 | Express intent, hard-to-misuse interfaces | `Units.hpp` entire; `PathOptions` for I.23; `PathClosure`/`Spacing` instead of bools; `static_assert`s proving misuse won't compile |
| 3 | Make it `constexpr` | Every function in `Units.hpp`, each with a `static_assert` under it |
| 4 | Rule of Zero | No destructor, copy or assignment anywhere. `vector` owns, `span` borrows |
| 5 | `const` and `[[nodiscard]]` | Every return-valued function; every local that can be `const`; primitives passed by value, never `const&` |
| 6 | Initialize your variables | `f64 value{}`, `Elements el{}`, `PathOptions` default member initializers |
| 7 | Error handling | `std::expected` throughout, one strategy, `describe()` for every error, no out-params, no two-phase init |
| 8 | Things we do not do | No `using namespace std`, no C casts, no `new`/`delete`, no macros, `enum class` everywhere |
| 9 | Algorithms over raw loops | `std::ranges::transform` in `PathUpload`; `std::views::iota` in `OrbitPath` |
| 10 | Do not pessimize | `reserve()` before both loops; `span` instead of a copied vector |
| 11 | Floating point | `nearlyEqual` not `==`; NaN-safe negated comparisons; exactly one `static_cast<f32>`, and it subtracts first |
| 12 | Keep the layers apart | `orb::gfx` header includes no Vulkan, no SDL. The orbit code has never heard of a camera |
| 13 | Concurrency | Pure function, no globals, no clock — documented as callable from any thread |
| 14 | Source files | Own header included first; all four headers verified self-contained; unnamed namespaces for internals |
| 15 | Naming | `PascalCase` types, `camelCase` functions, `kConstant`; no leading underscores; `meanToEccentricAnomaly`-style domain names |
| 16 | Comments | Every constant says where its value came from; the eccentric-anomaly comment saves a future reader a week |
| 17 | Maintainability | Longest function is 40 lines; `Metres`/`Seconds` deliberately not written |
| 18 | Portability | `std::size_t` for counts, `std::span` for views, no platform types |
| 19 | Determinism | Pure by construction; the bit-identity test proves it |
| 20 | Flight software | Bounded loop (rule 2) that **reports** non-convergence (rule 5); stated preconditions; short functions (rule 4) |
| 21 | Non-rules and myths | Multiple returns (NR.2); declarations at first use (NR.1); the identical assembly at the top of this file |

## What this example does not show, and why

Being honest about the gaps, because a coverage table that claims everything is
a coverage table nobody should trust.

- **Section 4's hardest case.** Rule of Zero is easy here because nothing owns a
  raw handle. The genuinely instructive version is wrapping `VkDevice` in a
  move-only type, and that belongs with the `VulkanContext` refactor.
- **Section 13's actual threading.** The function is *ready* to be threaded and
  is deliberately not threaded, because section 13 also says do not thread until
  a profiler tells you to.
- **Sections 1, 10 and 17 are partly process.** Sanitizer runs, CI matrices,
  profiling sessions and commit hygiene do not live inside a source file. The
  compile command at the top is the part that fits.
- **`Elements` still uses raw `f64` for its angles.** The example takes the
  existing type and adds strong types around it, which is what incremental
  adoption actually looks like. Migrating `Elements` itself is the next step,
  not a prerequisite.

## Running it

The sources are not in the tree yet — this document is the deliverable. To try
them, drop the files at the paths in the headings and add:

```cmake
add_executable(test_orbit_path
        tests/test_orbit_path.cpp
        src/orbit/Kepler.cpp
        src/orbit/OrbitPath.cpp
)
target_link_libraries(test_orbit_path PRIVATE orbsim_core)
add_test(NAME orbit_path COMMAND test_orbit_path)
```

Expected output:

```
orbsim :: orbit path sampling

Kepler solver satisfies M = E - e*sin(E)
Kepler solver reports failure instead of guessing
sampled path lies on the orbit
uniform-in-time spacing
bad requests are refused, not approximated
determinism
the f64 -> f32 boundary

29 checks, 0 failures
```

---

That is one feature. Two hundred and fifty lines, and every argument in the
guidelines showed up somewhere without being forced.

Which is the actual point. None of this is extra work you do *on top of* writing
the code. Strong types are four lines. `std::expected` is the same length as a
`bool` and an out-param. The `static_assert`s took four minutes. What you get
back is a function nobody can call backwards, a solver that cannot lie to you
about converging, and a precision boundary you can find with `grep`.

Now go make the compiler yell at you.

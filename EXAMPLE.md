# One Worked Example

*Companion to [CODING_GUIDELINES.md](CODING_GUIDELINES.md). Same voice, same
homage caveat: written in the style of Jason Turner, who did not write it.*

---

Twenty-one sections of advice is twenty-one things to hold in your head at once,
which is not how anybody writes code. So here is **one program**, complete and
self-contained, written so that every rule in the guidelines is followed
somewhere in it.

**The feature:** sample a closed orbit into a polyline the renderer can draw.

I picked it because it is the next thing this project genuinely needs -- there
is no Orbit MFD without it -- and because it happens to touch nearly every
argument in the document. It does real floating-point work, it can fail, it
crosses the simulation/renderer boundary, and it contains a numerical loop that
might not converge.

The `[S<n>]` markers in the code point at the guideline section each construct
demonstrates, so you can read the code and see the rule, or read the
[coverage table](#coverage) and jump to the code.

## This was compiled and run before it was written up

Nothing below is illustrative pseudocode. It depends on nothing but the standard
library, so you can paste it into Compiler Explorer unchanged.

| Check | Result |
|---|---|
| Compiles under the full section-1 warning set | **zero warnings, zero errors** |
| Test suite | **28 checks, 0 failures** |
| Longest function | **29 statements** (`OrbitPath::sample`) |
| Mutable globals | **none** |
| Places `f64` narrows to `f32` | **one function** (three lines: x, y, z) |
| `Radians` vs bare `double` at `-O2` | **byte-identical assembly** |

That last row is section 21 keeping its promise, so let me settle it up front.
These two functions:

```cpp
extern "C" double withStrongType(double d) { return orb::toRadians(orb::Degrees{d}).value; }
extern "C" double withBareDouble(double d) { return d * (orb::kPi / 180.0); }
```

compile to this -- both of them, same instruction, same constant:

```asm
withStrongType:
        mulsd   xmm0, qword ptr [rip + __real@3f91df46a2529d39]
        ret

withBareDouble:
        mulsd   xmm0, qword ptr [rip + __real@3f91df46a2529d39]
        ret
```

Zero-overhead abstraction is not a slogan. And do not take my word for it --
that is the whole point. Go look at your own.

---

## The code

```cpp
// ============================================================================
// orbsim -- one worked example: sampling a closed orbit into a polyline.
//
// Written so that every rule in CODING_GUIDELINES.md is followed somewhere in
// this file. The [S<n>] markers point at the section a construct demonstrates.
//
// Self-contained: depends on nothing but the standard library, so it can be
// pasted into Compiler Explorer unchanged.
//
// [S1] Built with, and required to stay clean under, the full warning set:
//   clang++ -std=c++23 -O2 -ffp-contract=off -Wall -Wextra -Wpedantic -Wshadow
//           -Wold-style-cast -Wcast-align -Wunused -Wconversion
//           -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wformat=2
//           -Wimplicit-fallthrough example.cpp -o example
//
// [S14] In the real tree this is five files -- core/Math.hpp, core/Units.hpp,
// orbit/Kepler.*, orbit/OrbitPath.*, render/PathUpload.hpp -- each .cpp
// including its own header first, which is what keeps that header
// self-contained. The banners below mark where those seams fall.
// ============================================================================

// [S14] Standard-library includes in one sorted block. No `using namespace std`
// anywhere, at any scope.
#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstddef>
#include <expected>
#include <iterator>
#include <limits>
#include <numbers>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// ============================================================================
// core/Math.hpp -- double-precision vector maths
// ============================================================================

namespace orb {

using f32 = float;
using f64 = double;

// [S8] constexpr, not #define: it has a type and it obeys scope.
inline constexpr f64 kPi = std::numbers::pi_v<f64>;
inline constexpr f64 kTau = 2.0 * kPi;

// [S11] Floating-point equality is never `==`. This is the only comparison the
// rest of the file is allowed to use, including inside static_assert, because
// the compile-time answer and the runtime answer are the same answer.
[[nodiscard]] constexpr bool nearlyEqual(f64 a, f64 b, f64 tolerance) noexcept {
    const f64 difference = a > b ? a - b : b - a;
    return difference <= tolerance;
}

[[nodiscard]] inline f64 wrapToPi(f64 angle) noexcept {
    const f64 wrapped = std::fmod(angle, kTau);
    if (wrapped > kPi) return wrapped - kTau;
    if (wrapped < -kPi) return wrapped + kTau;
    return wrapped;   // [S21] Three returns. NR.2: the single-return rule comes
                      // from a language without destructors.
}

// [S4] Rule of Zero: no destructor, no copy, no assignment, no move. The
// compiler generates all of them and cannot get them wrong.
// [S6] Default member initializers cover every constructor, including the ones
// added later, so a Vec3 cannot exist uninitialized.
struct Vec3 {
    f64 x{}, y{}, z{};

    [[nodiscard]] constexpr Vec3 operator-(const Vec3& other) const noexcept {
        return {x - other.x, y - other.y, z - other.z};
    }

    [[nodiscard]] constexpr bool operator==(const Vec3&) const noexcept = default;
};

[[nodiscard]] constexpr f64 dot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] inline f64 length(const Vec3& v) noexcept { return std::sqrt(dot(v, v)); }

// [S3] A static_assert is a unit test that costs no runtime, runs on every
// build whether or not anyone invokes the test suite, and cannot rot.
static_assert(cross(Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0}) == Vec3{0.0, 0.0, 1.0});
static_assert(nearlyEqual(dot(Vec3{1.0, 2.0, 3.0}, Vec3{4.0, 5.0, 6.0}), 32.0, 0.0));

// ============================================================================
// core/Units.hpp -- the meaning lives in the type, not in your head
// ============================================================================

// [S2] Each of these is a double at runtime and disappears entirely at -O2.
// What they buy is that the compiler now knows a radian from a degree and an
// angle from an eccentricity -- distinctions that otherwise exist only in the
// head of whoever wrote the call.
//
// [S17] Metres, Velocity and Mass are deliberately absent. They follow the
// identical pattern, nothing here needs them, and code written before it has a
// caller is code shaped by a guess.

struct Radians {
    f64 value{};

    constexpr Radians() noexcept = default;
    // [S2] explicit, or the type silently converts back to a bare double and
    // rebuilds the very problem it was introduced to solve.
    explicit constexpr Radians(f64 v) noexcept : value(v) {}

    [[nodiscard]] constexpr auto operator<=>(const Radians&) const noexcept = default;
};

struct Degrees {
    f64 value{};

    constexpr Degrees() noexcept = default;
    explicit constexpr Degrees(f64 v) noexcept : value(v) {}

    [[nodiscard]] constexpr auto operator<=>(const Degrees&) const noexcept = default;
};

struct Seconds {
    f64 value{};

    constexpr Seconds() noexcept = default;
    explicit constexpr Seconds(f64 v) noexcept : value(v) {}

    [[nodiscard]] constexpr auto operator<=>(const Seconds&) const noexcept = default;
};

// Dimensionless, but not interchangeable with any other dimensionless quantity.
// This is what stops solveKepler(anomaly, eccentricity) compiling backwards.
struct Eccentricity {
    f64 value{};

    constexpr Eccentricity() noexcept = default;
    explicit constexpr Eccentricity(f64 v) noexcept : value(v) {}

    [[nodiscard]] constexpr auto operator<=>(const Eccentricity&) const noexcept = default;
};

// Standard gravitational parameter GM, m^3/s^2.
struct GravParam {
    f64 value{};

    constexpr GravParam() noexcept = default;
    explicit constexpr GravParam(f64 v) noexcept : value(v) {}

    [[nodiscard]] constexpr auto operator<=>(const GravParam&) const noexcept = default;
};

[[nodiscard]] constexpr Radians toRadians(Degrees d) noexcept {
    return Radians{d.value * (kPi / 180.0)};
}

[[nodiscard]] constexpr Degrees toDegrees(Radians r) noexcept {
    return Degrees{r.value * (180.0 / kPi)};
}

inline namespace literals {

[[nodiscard]] constexpr Degrees operator""_deg(long double v) noexcept {
    return Degrees{static_cast<f64>(v)};   // [S8] named cast, never a C cast
}
[[nodiscard]] constexpr Radians operator""_rad(long double v) noexcept {
    return Radians{static_cast<f64>(v)};
}

} // namespace literals

static_assert(nearlyEqual(toRadians(180.0_deg).value, kPi, 1e-15));
static_assert(nearlyEqual(toDegrees(Radians{kPi}).value, 180.0, 1e-13));
static_assert(nearlyEqual(toRadians(toDegrees(Radians{1.0})).value, 1.0, 1e-15));

// ============================================================================
// orbit/Kepler.hpp + .cpp -- a solver that admits when it failed
// ============================================================================

// [S8] enum class: scoped, typed, no surprise conversions.
enum class KeplerError {
    EccentricityOutOfRange,   // precondition violated by the caller
    DidNotConverge,           // iteration limit reached; the answer is unusable
};

[[nodiscard]] constexpr std::string_view describe(KeplerError error) noexcept {
    switch (error) {
    case KeplerError::EccentricityOutOfRange:
        return "eccentricity must be in [0, 1) for an elliptic orbit";
    case KeplerError::DidNotConverge:
        return "Kepler solver reached its iteration limit without converging";
    }
    return "unknown error";
}

namespace {   // [S14] internal linkage: invisible to the linker, free to rename

// [S16] Every constant says where its value came from. A bare 1e-14 is a
// mystery nobody will later dare to change.

// Below this eccentricity the mean anomaly is a good enough starting guess.
// Above it the orbit spends nearly all its mean anomaly near periapsis, M is a
// poor guess, and starting at +/-pi keeps Newton in the convergent basin. The
// 0.8 threshold is the classical one from Danby's formulation.
constexpr f64 kHighEccentricity = 0.8;

// One ulp of a double near pi is about 4.4e-16. Two orders of magnitude above
// that converges without chasing rounding noise.
constexpr f64 kConvergenceTolerance = 1e-14;

// [S20] JPL Power of Ten, rule 2: every loop needs a provable upper bound.
// Newton roughly doubles its correct digits per step, so from the guesses above
// this converges in well under ten iterations for any eccentricity in range.
// The cap is not a performance budget -- it is what makes the bound provable.
constexpr int kMaxKeplerIterations = 50;

} // namespace

// Solves Kepler's equation M = E - e*sin(E) for the eccentric anomaly.
//
// [S2] Preconditions: 0 <= ecc < 1. Reported rather than asserted, because an
// eccentricity arriving from a scenario file is user input, not a programmer
// error.
// Guarantees: on success |E - e*sin(E) - M| < 1e-13. On failure nothing is
// returned at all, so there is no "approximately right" value lying around to
// be used by accident.
//
// [S2] Note the parameter types. `solveKepler(f64, f64)` would take two
// adjacent doubles that swap silently (I.24); these two cannot be transposed.
[[nodiscard]] std::expected<Radians, KeplerError> solveKepler(Radians meanAnomaly,
                                                              Eccentricity ecc) noexcept {
    // [S11] Negated comparisons, so a NaN eccentricity is rejected too. The
    // natural spelling `ecc.value < 0.0 || ecc.value >= 1.0` is false for NaN
    // and would hand NaN to Newton, which returns NaN as though it converged.
    if (!(ecc.value >= 0.0) || !(ecc.value < 1.0)) {
        return std::unexpected(KeplerError::EccentricityOutOfRange);
    }

    // [S21] Declared at first use, in the smallest scope that works (NR.1).
    const f64 e = ecc.value;
    const f64 m = wrapToPi(meanAnomaly.value);

    f64 eccentricAnomaly = (e < kHighEccentricity) ? m : (m >= 0.0 ? kPi : -kPi);

    // [S20] Bounded (rule 2) *and* it reports failure (rule 5). The bound alone
    // is the easy half.
    for (int iteration = 0; iteration < kMaxKeplerIterations; ++iteration) {
        const f64 residual = eccentricAnomaly - e * std::sin(eccentricAnomaly) - m;
        const f64 slope = 1.0 - e * std::cos(eccentricAnomaly);
        const f64 step = -residual / slope;

        eccentricAnomaly += step;

        if (std::abs(step) < kConvergenceTolerance) return Radians{eccentricAnomaly};
    }

    // This return is what stops a wrong number leaving the function wearing the
    // same face as a right one.
    return std::unexpected(KeplerError::DidNotConverge);
}

// ============================================================================
// orbit/OrbitPath.hpp + .cpp -- the interface you cannot misread
// ============================================================================

// [S2] Strong types all the way into the aggregate, so an Elements cannot be
// filled in with degrees where radians belong.
struct Elements {
    f64 semiMajorAxis{};   // metres
    Eccentricity eccentricity{};
    Radians inclination{};
    Radians ascendingNode{};
    Radians periapsisArgument{};
};

// [S2] Neither of these is a bool, so neither is a mystery at the call site.
enum class PathClosure {
    OpenEnded,    // last point stops one step short of the first
    ClosedLoop,   // last point repeats the first, ready for a line strip
};

enum class Spacing {
    UniformInAngle,   // even geometry: what the drawn ellipse wants
    UniformInTime,    // even time: what tick marks along the path want
};

enum class PathError {
    NotAClosedOrbit,     // hyperbolic or parabolic: no closed path exists
    NonPositiveGravity,  // mu <= 0 is not a central body
    TooFewSamples,       // fewer than three points is not a shape
    SolverFailed,        // Kepler did not converge
};

[[nodiscard]] constexpr std::string_view describe(PathError error) noexcept {
    switch (error) {
    case PathError::NotAClosedOrbit:    return "orbit is not closed (eccentricity >= 1)";
    case PathError::NonPositiveGravity: return "gravitational parameter must be positive";
    case PathError::TooFewSamples:      return "a path needs at least three samples";
    case PathError::SolverFailed:       return "Kepler solver failed while spacing by time";
    }
    return "unknown error";
}

struct SampleCount {
    std::size_t value{};   // [S18] size_t for counts, matching the library

    constexpr SampleCount() noexcept = default;
    explicit constexpr SampleCount(std::size_t v) noexcept : value(v) {}
};

// [S2] Bundling the options keeps the call short (I.23) and means adding a knob
// next month does not change the signature of every existing call.
// [S6] Every field has a default, so `PathOptions{}` is already valid.
struct PathOptions {
    SampleCount samples{std::size_t{64}};
    Spacing spacing{Spacing::UniformInAngle};
    PathClosure closure{PathClosure::ClosedLoop};
};

namespace {

constexpr std::size_t kMinSamples = 3;   // two points are a line, not a shape

[[nodiscard]] Vec3 rotateAboutZ(const Vec3& v, Radians angle) noexcept {
    const f64 c = std::cos(angle.value);
    const f64 s = std::sin(angle.value);
    return {v.x * c - v.y * s, v.x * s + v.y * c, v.z};
}

[[nodiscard]] Vec3 rotateAboutX(const Vec3& v, Radians angle) noexcept {
    const f64 c = std::cos(angle.value);
    const f64 s = std::sin(angle.value);
    return {v.x, v.y * c - v.z * s, v.y * s + v.z * c};
}

[[nodiscard]] Radians eccentricToTrueAnomaly(Radians eccentricAnomaly, Eccentricity ecc) noexcept {
    const f64 e = ecc.value;
    const f64 half = eccentricAnomaly.value * 0.5;
    return Radians{2.0 * std::atan2(std::sqrt(1.0 + e) * std::sin(half),
                                    std::sqrt(1.0 - e) * std::cos(half))};
}

[[nodiscard]] Vec3 positionAt(const Elements& elements, Radians trueAnomaly) noexcept {
    const f64 e = elements.eccentricity.value;
    const f64 semiLatusRectum = elements.semiMajorAxis * (1.0 - e * e);
    const f64 nu = trueAnomaly.value;
    const f64 radius = semiLatusRectum / (1.0 + e * std::cos(nu));

    // Perifocal frame: x toward periapsis, z along the angular-momentum vector.
    const Vec3 perifocal{radius * std::cos(nu), radius * std::sin(nu), 0.0};

    // Perifocal -> inertial is Rz(node) * Rx(inclination) * Rz(argument),
    // applied right to left.
    const Vec3 inPlane = rotateAboutZ(perifocal, elements.periapsisArgument);
    const Vec3 tilted = rotateAboutX(inPlane, elements.inclination);
    return rotateAboutZ(tilted, elements.ascendingNode);
}

} // namespace

// [S4] Rule of Zero *with* a resource: the vector does the owning, so this
// class still needs no destructor, copy, or move of its own. Rule of Zero is
// not "owns nothing", it is "delegates ownership".
// [S12] Knows nothing about vertex buffers, cameras, or any graphics API.
class OrbitPath {
public:
    // [S7] A factory returning std::expected, not a constructor plus an init()
    // that can half-succeed. Either you hold a valid OrbitPath or you hold an
    // error; there is no third state to write defensive code against, and so no
    // third state to forget to write defensive code against (E.5, NR.5).
    [[nodiscard]] static std::expected<OrbitPath, PathError>
    sample(const Elements& elements, GravParam mu, const PathOptions& options = {});

    // [S5] const member functions; [S18] a span, which borrows and cannot copy.
    [[nodiscard]] std::span<const Vec3> points() const noexcept { return points_; }
    [[nodiscard]] std::size_t size() const noexcept { return points_.size(); }
    [[nodiscard]] Seconds period() const noexcept { return period_; }

    [[nodiscard]] bool operator==(const OrbitPath&) const noexcept = default;

private:
    // [S6] Member initializer list, in declaration order. Assigning in the body
    // would default-construct the vector and then throw that away.
    OrbitPath(std::vector<Vec3> points, Seconds period) noexcept
        : points_(std::move(points)), period_(period) {}

    // [S15] Trailing underscore for private data. Never a leading underscore:
    // `_name` at namespace scope is reserved for the implementation.
    std::vector<Vec3> points_;
    Seconds period_;
};

std::expected<OrbitPath, PathError> OrbitPath::sample(const Elements& elements, GravParam mu,
                                                      const PathOptions& options) {
    // [S2] Preconditions checked and reported, never silently repaired.
    // [S11] Negated form again, so NaN is rejected rather than admitted.
    if (!(elements.eccentricity.value >= 0.0) || !(elements.eccentricity.value < 1.0)) {
        return std::unexpected(PathError::NotAClosedOrbit);
    }
    if (!(mu.value > 0.0)) return std::unexpected(PathError::NonPositiveGravity);
    if (options.samples.value < kMinSamples) return std::unexpected(PathError::TooFewSamples);

    const std::size_t steps = options.samples.value;
    const std::size_t count =
        steps + (options.closure == PathClosure::ClosedLoop ? std::size_t{1} : std::size_t{0});

    std::vector<Vec3> points;
    // [S10] Not an optimization -- simply not being wasteful when the size is
    // sitting right there. [S20] And it is the only allocation, made up front.
    points.reserve(count);

    // [S9] A range-for over a view, not an index loop with a hand-written bound.
    for (const std::size_t index : std::views::iota(std::size_t{0}, count)) {
        const f64 fraction = static_cast<f64>(index) / static_cast<f64>(steps);
        const Radians anomaly{kTau * fraction};

        // [S16] Stepped in eccentric anomaly, not true anomaly. Equal steps of
        // true anomaly crowd points around periapsis and leave the apoapsis arc
        // as one long straight chord, which is visibly wrong on an eccentric
        // orbit. Nothing in the code below says that; without this sentence
        // somebody "simplifies" it and the bug takes a week to find.
        Radians eccentricAnomaly = anomaly;

        if (options.spacing == Spacing::UniformInTime) {
            // Equal steps of *mean* anomaly are equal steps of time, which is
            // what tick marks want. That needs Kepler solved, that can fail,
            // and the failure is propagated rather than swallowed.  [S7]
            const std::expected<Radians, KeplerError> solved =
                solveKepler(anomaly, elements.eccentricity);
            if (!solved) return std::unexpected(PathError::SolverFailed);
            eccentricAnomaly = *solved;
        }

        points.push_back(
            positionAt(elements, eccentricToTrueAnomaly(eccentricAnomaly, elements.eccentricity)));
    }

    // Kepler's third law: T = tau * sqrt(a^3 / mu).
    const f64 a = elements.semiMajorAxis;
    const Seconds period{kTau * std::sqrt((a * a * a) / mu.value)};

    // [S10] Moved, not copied, into the returned object.
    return OrbitPath{std::move(points), period};
}

// ============================================================================
// render/PathUpload.hpp -- the precision boundary, in exactly one place
// ============================================================================

// [S12] This namespace is the renderer's, and it includes no graphics API at
// all: it takes plain data and returns plain data. That is what lets the
// physics be tested without a GPU and the renderer be replaced without touching
// a line of orbital mechanics. The dependency runs one way -- gfx knows about
// orb, orb has never heard of gfx.
namespace gfx {

// What a vertex shader consumes. f32, because that is what a GPU has.
struct PathVertex {
    f32 x{}, y{}, z{};
};

// [S11] The one and only place in this file where f64 becomes f32. Keeping the
// narrowing in a single named function is what makes the precision boundary
// something you can grep for; a static_cast<f32> anywhere upstream of here is a
// jitter bug report waiting to be filed.
[[nodiscard]] inline std::vector<PathVertex> toCameraRelative(std::span<const Vec3> pathWorld,
                                                              const Vec3& cameraWorld) {
    std::vector<PathVertex> vertices;
    vertices.reserve(pathWorld.size());

    // [S9] A genuine transform over existing data, so it is written as one.
    std::ranges::transform(pathWorld, std::back_inserter(vertices), [&](const Vec3& point) {
        // [S11] Subtract in f64 FIRST, then narrow. At Earth-orbit radius f32
        // has metre-scale spacing, so narrowing first would round a ten-metre
        // feature away before the camera offset ever removed the magnitude.
        const Vec3 relative = point - cameraWorld;
        return PathVertex{static_cast<f32>(relative.x),
                          static_cast<f32>(relative.y),
                          static_cast<f32>(relative.z)};
    });

    return vertices;
}

} // namespace gfx
} // namespace orb

// ============================================================================
// tests/test_orbit_path.cpp
// ============================================================================

namespace {

using namespace orb;            // [S8] in a .cpp, at function-adjacent scope --
using namespace orb::literals;  // never at global scope in a header (SF.7)

constexpr GravParam kMuEarth{3.986004418e14};   // m^3/s^2, EGM-96
constexpr f64 kEarthRadius = 6378137.0;         // m, WGS-84 equatorial

// [S18] No mutable globals. Static mutable state has unspecified initialization
// order across translation units and is a data race waiting for a second
// thread; the counters are carried explicitly instead.
struct TestResults {
    int checks{};
    int failures{};
};

void check(TestResults& results, bool condition, std::string_view what) {
    ++results.checks;
    if (!condition) {
        ++results.failures;
        // [S8] '\n', never std::endl: endl flushes and you did not ask it to.
        std::print("  FAIL {}\n", what);
    }
}

[[nodiscard]] Elements testOrbit(f64 semiMajorAxis, f64 eccentricity) {
    // [S6] Braced initialization: it will not silently narrow.
    return Elements{.semiMajorAxis = semiMajorAxis,
                    .eccentricity = Eccentricity{eccentricity},
                    .inclination = toRadians(28.5_deg),
                    .ascendingNode = toRadians(120.0_deg),
                    .periapsisArgument = toRadians(45.0_deg)};
}

// [S2] These are not runtime tests. They are the compiler certifying, on every
// build, that the misuse the guidelines warn about cannot be written. Delete an
// `explicit` for convenience and the build stops here.
static_assert(!std::is_convertible_v<f64, Radians>,
              "a bare double must not become an angle on its own");
static_assert(!std::is_convertible_v<Radians, f64>,
              "an angle must not decay back into a bare double");
static_assert(!std::is_constructible_v<Radians, Eccentricity>,
              "an eccentricity must never be usable as an angle");
static_assert(!std::is_invocable_v<decltype(solveKepler), Eccentricity, Radians>,
              "solveKepler must not be callable with its arguments transposed");
static_assert(std::is_invocable_v<decltype(solveKepler), Radians, Eccentricity>,
              "...but must of course be callable correctly");

// [S4] And the Rule of Zero claim, checked rather than asserted in prose.
static_assert(std::is_nothrow_move_constructible_v<OrbitPath>,
              "the compiler-generated move must be available and cheap");
static_assert(std::is_copy_constructible_v<OrbitPath>,
              "the compiler-generated copy must be available");

// [S1] The solver is checked against the equation it claims to solve, not
// against a second solver written by the same hand on the same afternoon. A
// sign error cannot hide, because the test does not share it.
void testKeplerAgainstItsDefinition(TestResults& results) {
    std::print("Kepler solver satisfies M = E - e*sin(E)\n");

    // [S8] std::array, never a C array: it knows its own size.
    constexpr std::array kEccentricities{0.0, 0.1, 0.5, 0.9, 0.99, 0.999};

    for (const f64 e : kEccentricities) {
        f64 worstResidual = 0.0;

        for (const int degree : std::views::iota(0, 360)) {
            const Radians meanAnomaly = toRadians(Degrees{static_cast<f64>(degree)});
            const auto solved = solveKepler(meanAnomaly, Eccentricity{e});

            if (!solved) {
                check(results, false, "solver failed for a valid eccentricity");
                break;
            }

            const f64 bigE = solved->value;
            const f64 residual = bigE - e * std::sin(bigE) - wrapToPi(meanAnomaly.value);
            worstResidual = std::max(worstResidual, std::abs(wrapToPi(residual)));
        }

        check(results, worstResidual < 1e-13, "residual within the documented guarantee");
    }
}

// [S20] The contract says failure is reported, never returned as a plausible
// number. That contract is worth exactly as much as its test.
void testFailuresAreReported(TestResults& results) {
    std::print("failures are reported, not approximated\n");

    const auto parabolic = solveKepler(1.0_rad, Eccentricity{1.0});
    check(results, !parabolic.has_value(), "eccentricity 1.0 is rejected");
    check(results, parabolic.error() == KeplerError::EccentricityOutOfRange, "with the right error");

    const auto negative = solveKepler(1.0_rad, Eccentricity{-0.1});
    check(results, !negative.has_value(), "negative eccentricity is rejected");

    const auto notANumber =
        solveKepler(1.0_rad, Eccentricity{std::numeric_limits<f64>::quiet_NaN()});
    check(results, !notANumber.has_value(), "NaN eccentricity is rejected, not propagated");

    const auto escape = OrbitPath::sample(testOrbit(-8000e3, 1.4), kMuEarth);
    check(results, !escape.has_value() && escape.error() == PathError::NotAClosedOrbit,
          "a hyperbolic orbit has no closed path");

    const auto noGravity = OrbitPath::sample(testOrbit(7000e3, 0.1), GravParam{0.0});
    check(results, !noGravity.has_value() && noGravity.error() == PathError::NonPositiveGravity,
          "a massless central body is refused");

    const auto tooFew = OrbitPath::sample(testOrbit(7000e3, 0.1), kMuEarth,
                                          PathOptions{.samples = SampleCount{std::size_t{2}}});
    check(results, !tooFew.has_value() && tooFew.error() == PathError::TooFewSamples,
          "two points is not a shape");

    // [S7] Every error can be explained to a human.
    check(results, !describe(KeplerError::DidNotConverge).empty(), "Kepler errors describe");
    check(results, !describe(PathError::SolverFailed).empty(), "path errors describe");
}

void testGeometry(TestResults& results) {
    std::print("sampled points lie on the orbit\n");

    const Elements elements = testOrbit(kEarthRadius + 2000e3, 0.35);
    const auto path = OrbitPath::sample(elements, kMuEarth,
                                        PathOptions{.samples = SampleCount{std::size_t{128}},
                                                    .spacing = Spacing::UniformInAngle,
                                                    .closure = PathClosure::ClosedLoop});
    check(results, path.has_value(), "sampling a closed orbit succeeds");
    if (!path) return;   // [S21] early return; NR.2 again

    check(results, path->size() == 129, "ClosedLoop yields samples + 1 points");

    const f64 e = elements.eccentricity.value;
    const f64 periapsis = elements.semiMajorAxis * (1.0 - e);
    const f64 apoapsis = elements.semiMajorAxis * (1.0 + e);

    // [S9] An algorithm says what it means; a loop with an index would not.
    const bool onOrbit = std::ranges::all_of(path->points(), [&](const Vec3& point) {
        const f64 radius = length(point);
        return radius >= periapsis * (1.0 - 1e-9) && radius <= apoapsis * (1.0 + 1e-9);
    });
    check(results, onOrbit, "every sample lies between periapsis and apoapsis");

    const Vec3 gap = path->points().front() - path->points().back();
    check(results, length(gap) < periapsis * 1e-9, "the loop closes");

    // A 2000 km circular-ish orbit takes a bit over two hours; this anchors the
    // period against something a reader can sanity-check by hand.
    check(results, path->period().value > 7000.0 && path->period().value < 8000.0,
          "period is physically plausible");
}

// [S19] A simulator promises that resuming a scenario continues the same
// flight. That promise is worth nothing untested.
void testDeterminism(TestResults& results) {
    std::print("determinism\n");

    const Elements elements = testOrbit(kEarthRadius + 800e3, 0.2);
    const PathOptions options{.samples = SampleCount{std::size_t{256}},
                              .spacing = Spacing::UniformInTime,
                              .closure = PathClosure::ClosedLoop};

    const auto first = OrbitPath::sample(elements, kMuEarth, options);
    const auto second = OrbitPath::sample(elements, kMuEarth, options);
    check(results, first.has_value() && second.has_value(), "both runs succeed");
    if (!first || !second) return;

    // [S11] [S19] This is the one place where comparing doubles with == is not
    // merely allowed but the entire point: the claim is bit-identical
    // reproduction, and any tolerance at all would hide exactly the drift being
    // tested for. Knowing which rule applies beats knowing the rules.
    check(results, *first == *second, "identical inputs give bit-identical output");
}

void testCameraRelativeUpload(TestResults& results) {
    std::print("the f64 -> f32 boundary\n");

    const auto path = OrbitPath::sample(testOrbit(kEarthRadius + 400e3, 0.01), kMuEarth);
    check(results, path.has_value(), "sampling succeeds");
    if (!path) return;

    const Vec3 camera = path->points().front();
    const std::vector<gfx::PathVertex> vertices = gfx::toCameraRelative(path->points(), camera);

    check(results, vertices.size() == path->size(), "one vertex per point");

    // The camera sits exactly on the first point, so that vertex must land on
    // the origin. Subtracting in f64 makes this exact; narrowing first would not.
    const gfx::PathVertex& firstVertex = vertices.front();
    check(results, firstVertex.x == 0.0F && firstVertex.y == 0.0F && firstVertex.z == 0.0F,
          "the point under the camera lands exactly on the origin");

    const bool withinFloatComfort = std::ranges::all_of(vertices, [](const gfx::PathVertex& v) {
        return std::abs(v.x) < 2.0e7F && std::abs(v.y) < 2.0e7F && std::abs(v.z) < 2.0e7F;
    });
    check(results, withinFloatComfort, "camera-relative values stay in f32's comfortable range");
}

// [S13] Two paths computed concurrently. There is no mutex anywhere, because
// nothing is shared: each worker owns its inputs and its result, and the
// simulation functions are pure. That is CP.3 (minimize sharing) and CP.4
// (think in tasks) rather than "add a lock".
void testConcurrentSampling(TestResults& results) {
    std::print("concurrent sampling needs no locks\n");

    std::optional<OrbitPath> low;
    std::optional<OrbitPath> high;

    {
        // [S13] std::jthread, never std::thread: it joins in its destructor, so
        // the scope exit below is the join. A std::thread you forget to join
        // calls std::terminate.
        const std::jthread lowWorker{[&low] {
            if (auto sampled = OrbitPath::sample(testOrbit(7000e3, 0.05), kMuEarth)) {
                low = std::move(*sampled);
            }
        }};
        const std::jthread highWorker{[&high] {
            if (auto sampled = OrbitPath::sample(testOrbit(42164e3, 0.001), kMuEarth)) {
                high = std::move(*sampled);
            }
        }};
    }   // both workers joined here

    check(results, low.has_value() && high.has_value(), "both workers produced a path");
    if (!low || !high) return;

    // Geostationary altitude has a much longer period than low Earth orbit; if
    // these matched, the threads would have trampled each other.
    check(results, high->period().value > low->period().value * 10.0,
          "the two results are independent and correct");
}

} // namespace

// [S17] Every function here fits on a screen. The longest is OrbitPath::sample
// at 29 statements across 52 lines -- the difference being the comments, which
// is why JPL rule 4 counts statements and not lines.
int main() {
    std::print("orbsim :: one worked example\n\n");

    TestResults results;

    testKeplerAgainstItsDefinition(results);
    testFailuresAreReported(results);
    testGeometry(results);
    testDeterminism(results);
    testCameraRelativeUpload(results);
    testConcurrentSampling(results);

    std::print("\n{} checks, {} failures\n", results.checks, results.failures);
    return results.failures == 0 ? 0 : 1;
}
```

---

## Coverage

Every section of the guidelines, and the thing in the code you can point at.

| § | Guideline | Point at |
|---|---|---|
| 1 | Use the tools available | The build command in the file header. The file is required to stay clean under it, and does. |
| 2 | Express intent, hard-to-misuse interfaces | `Radians`, `Degrees`, `Eccentricity`, `GravParam`, each with an `explicit` constructor. `solveKepler(Radians, Eccentricity)` cannot be called with its arguments transposed -- and a `static_assert` proves it. `PathOptions` bundles three parameters into one (I.23). `PathClosure` and `Spacing` instead of bools. Preconditions stated in the comment and checked in the code. |
| 3 | Make it `constexpr` | Every unit conversion is `constexpr`, and each has a `static_assert` under it that runs on every build. |
| 4 | Rule of Zero | **Not one destructor, copy constructor, or assignment operator in the entire file.** `OrbitPath` owns heap memory and still declares none -- the `std::vector` does the owning. `static_assert(is_nothrow_move_constructible_v<OrbitPath>)` checks the claim rather than asserting it in prose. |
| 5 | `const` and `[[nodiscard]]` | `[[nodiscard]]` on every function whose return value is the point. `points()`, `size()`, `period()` are `const`. Primitives passed by value, never `const&`. |
| 6 | Initialize your variables | `f64 x{}, y{}, z{}` on `Vec3`; every `PathOptions` field defaulted; `OrbitPath`'s member initializer list in declaration order; designated initializers in `testOrbit`. |
| 7 | Error handling | `std::expected` throughout -- one strategy, no mixing. `describe()` for both error enums. A static factory instead of a constructor plus `init()`, so there is no half-built state (E.5, NR.5). No out-parameters. |
| 8 | Things we simply do not do | `constexpr` not `#define`; `enum class` everywhere; `static_cast` never a C cast; `std::array` not a C array; `'\n'` not `std::endl`; no `new`/`delete`; no `using namespace std` at any scope. |
| 9 | Prefer algorithms over raw loops | `std::views::iota` in the sampler, `std::ranges::transform` at the GPU boundary, `std::ranges::all_of` twice in the tests. |
| 10 | Measure. Do not guess. | The "do not pessimize" half: `reserve()` before the only growing loop, `std::move` into the returned object, `std::span` instead of a copied vector. |
| 11 | Floating point | `nearlyEqual` instead of `==`, including inside `static_assert`. NaN-safe negated preconditions -- `!(e >= 0.0)` rather than `e < 0.0`. Exactly one `static_cast<f32>` in the file, and it subtracts in `f64` first. |
| 12 | Keep the layers apart | `orb::gfx` contains no graphics API at all: plain data in, plain data out. `orb` never mentions `gfx`. The dependency runs one way. |
| 13 | Concurrency | `std::jthread`, never `std::thread`, joined by scope exit. No mutex anywhere, because nothing is shared -- each worker owns its inputs and its result. |
| 14 | Source files | Unnamed namespaces for everything internal. One sorted include block. No `using namespace` in the header-shaped sections. Banners mark where the five real files would split. |
| 15 | Naming | `PascalCase` types, `camelCase` functions, `kConstant`, trailing `_` on private data -- never a leading underscore, which is reserved. |
| 16 | Comments | Every constant says where its number came from: 0.8 is Danby's threshold, 1e-14 is two orders above one ulp near pi, 50 is not a performance budget. Plus the eccentric-anomaly comment, which exists to stop a future reader "simplifying" it. |
| 17 | Maintainability | Longest function is 29 statements. `Metres`, `Velocity` and `Mass` are deliberately not written, because nothing needs them yet. |
| 18 | Portability | `std::size_t` for counts, `std::span` for views, no platform types, and no mutable statics -- the test counters are carried in a struct instead. |
| 19 | Determinism | Pure functions by construction: no globals, no clock, no RNG. The bit-identity test proves it rather than assuming it. |
| 20 | What flight software does | The Newton loop is bounded (rule 2) **and reports non-convergence** (rule 5). Preconditions checked (rule 5). One allocation, up front (the spirit of rule 3). Short functions (rule 4). |
| 21 | Non-rules and myths | Multiple returns used freely (NR.2). Declarations at first use (NR.1). The identical assembly above. |

## The two places the rules collide

A worked example is most useful where the guidelines disagree with each other,
because that is where following them mechanically stops working.

**Section 11 says never compare floats with `==`. Section 19 says a simulator
must reproduce a scenario exactly.** In `testDeterminism` they meet, and the
resolution is that section 11 is about *approximate* comparison. When the claim
genuinely is bit-identity, `==` is the only correct operator, and a tolerance
would hide precisely the drift being tested for.

**Section 20 says assert your preconditions. Section 7 says report expected
failures.** `solveKepler` reports rather than asserts, because an eccentricity
arriving from a scenario file is user input, not a programmer error. Had it come
from an internal invariant, an assert would be right.

Knowing which rule applies beats knowing the rules.

## What one file cannot show

Being straight about the gaps, because a coverage table that claims everything
is a coverage table nobody should trust.

- **Section 14 is about physical layout**, and a single translation unit can
  only demonstrate part of it. The unnamed namespaces and the include block are
  real; "each `.cpp` includes its own header first, which is what keeps that
  header self-contained" needs the five-file split the banners mark.
- **Sections 1, 10 and 17 are partly process.** Sanitizer runs, CI matrices,
  profiler sessions, commit hygiene and version control do not live inside a
  source file. The build command is the part that fits.
- **Section 4's hardest case is absent.** Rule of Zero is easy here because
  nothing owns a raw OS handle. The instructive version is wrapping `VkDevice`
  in a move-only type, and that belongs with the `VulkanContext` refactor.
- **Section 13 shows the mechanism, not the architecture.** `std::jthread` and
  the absence of shared state are real; the snapshot-and-interpolate design for
  a threaded physics loop needs a physics loop.

## Running it

```
clang++ -std=c++23 -O2 -ffp-contract=off -Wall -Wextra -Wpedantic -Wshadow \
        -Wold-style-cast -Wcast-align -Wunused -Wconversion -Wsign-conversion \
        -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough \
        example.cpp -o example
```

```
orbsim :: one worked example

Kepler solver satisfies M = E - e*sin(E)
failures are reported, not approximated
sampled points lie on the orbit
determinism
the f64 -> f32 boundary
concurrent sampling needs no locks

28 checks, 0 failures
```

---

That is one feature, and every argument in the guidelines turned up somewhere
without being forced.

Which is the actual point. None of this is extra work done *on top of* writing
the code. Strong types are four lines each. `std::expected` is the same length
as a `bool` and an out-parameter. The `static_assert`s took four minutes. What
comes back is a function nobody can call backwards, a solver that cannot lie to
you about converging, and a precision boundary you can find with `grep`.

Now go make the compiler yell at you.

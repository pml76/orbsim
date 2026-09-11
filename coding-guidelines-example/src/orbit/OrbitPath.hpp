#ifndef ORBEX_ORBIT_ORBITPATH_HPP
#define ORBEX_ORBIT_ORBITPATH_HPP
//
// Sampling a closed orbit into a polyline.
//
// Produces f64 positions in the body-centred inertial frame, in metres.
//
// [S12] Deliberately knows nothing about vertex buffers, cameras or graphics
// APIs. Narrowing to f32 happens downstream, in render/PathUpload.hpp, and
// nowhere else.
//
// [S19] Every function here is pure: no globals, no clock, no hidden state. The
// same inputs always produce bit-identical output, which is what makes a saved
// scenario resumable and what tests/test_orbit_path.cpp actually checks.
//
#include "core/Units.hpp"
#include "core/Vec3.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace orbex {

// [S2] Strong types all the way into the aggregate, so an Elements cannot be
// filled in with degrees where radians belong.
// [S6] Every member is default-initialized, so `Elements{}` is a valid, if
// useless, object rather than a lap of undefined behaviour. No `{}` is written
// here because each unit type already carries its own default member
// initializer (core/Units.hpp) -- section 6 asks for the guarantee, not the
// punctuation, and a bare `f64 semiMajorAxis;` could not make it.
struct Elements {
    Metres semiMajorAxis;
    Eccentricity eccentricity;
    Radians inclination;
    Radians ascendingNode;
    Radians periapsisArgument;
};

// [S2] Neither of these is a bool, so neither is a mystery at the call site.
// `sample(elements, mu, {64, true, false})` would be three mysteries in a row.
// [S8] The explicit base type is interface, not optimisation; see KeplerError.
enum class PathClosure : std::uint8_t {
    OpenEnded,  // the last point stops one step short of the first
    ClosedLoop, // the last point repeats the first, ready for a line strip
};

enum class Spacing : std::uint8_t {
    UniformInAngle, // even geometry: what the drawn ellipse wants
    UniformInTime,  // even time: what tick marks along the path want
};

enum class PathError : std::uint8_t {
    NotAClosedOrbit,
    NonPositiveGravity,
    NonPositiveSemiMajorAxis,
    TooFewSamples,
    SolverFailed,
};

[[nodiscard]] constexpr std::string_view describe(PathError error) noexcept {
    switch (error) {
    case PathError::NotAClosedOrbit:
        return "orbit is not closed (eccentricity >= 1)";
    case PathError::NonPositiveGravity:
        return "gravitational parameter must be positive";
    case PathError::NonPositiveSemiMajorAxis:
        return "semi-major axis must be positive";
    case PathError::TooFewSamples:
        return "a path needs at least three samples";
    case PathError::SolverFailed:
        return "Kepler solver failed while spacing samples by time";
    }
    return "unknown path error";
}

// [S18] size_t for a count, matching what the standard library uses for sizes.
struct SampleCount {
    std::size_t value{};

    constexpr SampleCount() noexcept = default;
    explicit constexpr SampleCount(std::size_t v) noexcept : value(v) {}

    [[nodiscard]] constexpr auto operator<=>(const SampleCount&) const noexcept = default;
};

// [S2] Bundling the options keeps the argument count low (I.23) and means that
// adding a knob next month does not change the signature of every call site.
struct PathOptions {
    SampleCount samples{std::size_t{64}};
    Spacing spacing{Spacing::UniformInAngle};
    PathClosure closure{PathClosure::ClosedLoop};
};

// [S4] Rule of Zero *with* a resource. This class owns heap memory and still
// declares no destructor, copy, assignment or move -- the std::vector does the
// owning. Rule of Zero is not "owns nothing", it is "delegates ownership".
class OrbitPath {
public:
    // [S7] A factory returning std::expected, not a constructor followed by an
    // init() that can half-succeed. Either you hold a valid OrbitPath or you
    // hold an error; there is no third state to write defensive code against,
    // and therefore no third state to forget to write defensive code against
    // (E.5, NR.5).
    [[nodiscard]] static std::expected<OrbitPath, PathError>
    sample(const Elements& elements, GravParam mu, const PathOptions& options = {});

    // [S5] const member functions, and a span rather than a reference to the
    // vector: the caller gets a view it cannot resize and cannot accidentally
    // take a copy of. The view points into this path, and says so with
    // [[clang::lifetimebound]], so clang can report one kept past the path.
    //
    // gcc does not know clang's attribute and reports it as ignored
    // (-Wattributes). That warning is off for gcc on this declaration alone,
    // by the owner's ruling on the parent project's ADR 0017; it stays on
    // everywhere else, where it catches a misspelt attribute.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif
    [[nodiscard]] std::span<const Vec3> points() const noexcept [[clang::lifetimebound]] {
        return points_;
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    [[nodiscard]] std::size_t size() const noexcept { return points_.size(); }
    [[nodiscard]] Seconds period() const noexcept { return period_; }

    // [S19] Bit identity, because that is what the determinism test asserts:
    // every point and the period, compared as bits. A defaulted `==` would
    // compare them as numbers, which is a weaker claim and a floating-point
    // `==` besides [S11].
    [[nodiscard]] bool bitIdentical(const OrbitPath& other) const noexcept {
        return bitsOf(period_.value) == bitsOf(other.period_.value) &&
               std::ranges::equal(
                   points_, other.points_, [](const Vec3& a, const Vec3& b) noexcept {
                       return a.bitIdentical(b);
                   });
    }

private:
    // [S6] A member initializer list in declaration order. Assigning in the
    // body would default-construct the vector and then throw it away, and a
    // list written out of order is how a member gets initialized from another
    // member that does not have a value yet (-Wreorder catches that).
    // [S10] The vector arrives by value and is moved, never copied.
    OrbitPath(std::vector<Vec3> points, Seconds period) noexcept
        : points_(std::move(points)), period_(period) {}

    // [S15] A trailing underscore for private data. Never a leading one: `_name`
    // at namespace scope is reserved for the implementation.
    std::vector<Vec3> points_;
    Seconds period_;
};

} // namespace orbex

#endif // ORBEX_ORBIT_ORBITPATH_HPP

#pragma once
//
// Strong scalar types for the simulation domain.
//
// Each is an f64 at runtime and disappears entirely at -O2. What they buy is
// that the compiler now knows a radian from a degree, an angle from an
// eccentricity, and a duration from a length -- distinctions that otherwise
// exist only in the head of whoever wrote the call.
//
// This is the highest-value rule in CODING_GUIDELINES.md for this codebase
// (section 2, I.4). We are writing software in the same problem domain that
// lost the Mars Climate Orbiter to a unit mismatch in 1999; the types carrying
// the information is the difference between a compile error and a spacecraft.
//
// They also close I.24 for free: `propagate(state, mu, dt)` used to take two
// adjacent f64 parameters that transposed in silence.
//
#include "core/Math.hpp"

#include <compare>

namespace orb {

struct Radians {
    f64 value{};

    constexpr Radians() noexcept = default;
    // explicit, or the type converts back to a bare f64 on its own and rebuilds
    // the exact problem it was introduced to solve.
    explicit constexpr Radians(f64 v) noexcept : value(v) {}

    [[nodiscard]] constexpr auto operator<=>(const Radians&) const noexcept = default;
};

struct Degrees {
    f64 value{};

    constexpr Degrees() noexcept = default;
    explicit constexpr Degrees(f64 v) noexcept : value(v) {}

    [[nodiscard]] constexpr auto operator<=>(const Degrees&) const noexcept = default;
};

struct Metres {
    f64 value{};

    constexpr Metres() noexcept = default;
    explicit constexpr Metres(f64 v) noexcept : value(v) {}

    [[nodiscard]] constexpr auto operator<=>(const Metres&) const noexcept = default;
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

// Standard gravitational parameter GM of a central body, m^3/s^2.
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

// Strong-typed overloads of the wrap helpers in core/Math.hpp. Overloads
// rather than reimplementations: one behaviour, two spellings.
[[nodiscard]] inline Radians wrapTau(Radians a) noexcept { return Radians{wrapTau(a.value)}; }
[[nodiscard]] inline Radians wrapPi(Radians a) noexcept { return Radians{wrapPi(a.value)}; }

// A literal suffix must begin with an underscore; that is the one place in this
// codebase where a leading underscore is not only allowed but required.
inline namespace literals {

[[nodiscard]] constexpr Degrees operator""_deg(long double v) noexcept {
    return Degrees{static_cast<f64>(v)};
}

[[nodiscard]] constexpr Radians operator""_rad(long double v) noexcept {
    return Radians{static_cast<f64>(v)};
}

[[nodiscard]] constexpr Metres operator""_km(long double v) noexcept {
    return Metres{static_cast<f64>(v) * 1000.0};
}

[[nodiscard]] constexpr Seconds operator""_s(long double v) noexcept {
    return Seconds{static_cast<f64>(v)};
}

} // namespace literals

// Compile-time tests: no runtime cost, run on every build whether or not
// anyone remembers to invoke the suite, and cannot rot.
static_assert(nearlyEqual(toRadians(180.0_deg).value, kPi, Tolerance{1e-15}));
static_assert(nearlyEqual(toDegrees(Radians{kPi}).value, 180.0, Tolerance{1e-13}));
static_assert(nearlyEqual(toRadians(toDegrees(Radians{1.0})).value, 1.0, Tolerance{1e-15}));
static_assert(nearlyEqual((1.0_km).value, 1000.0, Tolerance{0.0}));
static_assert(180.0_deg == Degrees{180.0});

} // namespace orb

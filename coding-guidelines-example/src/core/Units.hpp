#pragma once
//
// Strong scalar types for the simulation domain.
//
// [S2] Each of these is an f64 at runtime and disappears entirely at -O2 (the
// zero-overhead claim is checked in tests/test_units.cpp, not merely asserted
// here). What they buy is that the compiler now knows a radian from a degree
// and an angle from an eccentricity -- distinctions that otherwise exist only
// in the head of whoever wrote the call. See docs/adr/0001 for the reasoning.
//
// [S17] Velocity, Mass and Acceleration are deliberately absent. They follow
// the identical pattern, nothing in this example needs them, and code written
// before it has a caller is code shaped by a guess rather than a requirement.
//
#include "core/Vec3.hpp"

#include <compare>

namespace orbex {

struct Radians {
    f64 value{};

    constexpr Radians() noexcept = default;
    // [S2][S15] explicit, or the type converts back to a bare f64 on its own
    // and rebuilds the exact problem it was introduced to solve.
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

// Standard gravitational parameter GM, in m^3/s^2.
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

// [S15] A trailing underscore would be wrong on a namespace; a leading one is
// worse. `_deg` as a literal suffix is required to start with an underscore by
// the standard, which is the single exception -- suffixes without one are
// reserved for the implementation.
inline namespace literals {

[[nodiscard]] constexpr Degrees operator""_deg(long double v) noexcept {
    return Degrees{static_cast<f64>(v)}; // [S8] a named cast, never a C cast
}

[[nodiscard]] constexpr Radians operator""_rad(long double v) noexcept {
    return Radians{static_cast<f64>(v)};
}

[[nodiscard]] constexpr Metres operator""_km(long double v) noexcept {
    return Metres{static_cast<f64>(v) * 1000.0};
}

} // namespace literals

// [S3] Proved at compile time, on every build.
static_assert(nearlyEqual(toRadians(180.0_deg).value, kPi, Tolerance{1e-15}));
static_assert(nearlyEqual(toDegrees(Radians{kPi}).value, 180.0, Tolerance{1e-13}));
static_assert(nearlyEqual(toRadians(toDegrees(Radians{1.0})).value, 1.0, Tolerance{1e-15}));
static_assert(nearlyEqual((1.0_km).value, 1000.0, Tolerance{0.0}));
static_assert(180.0_deg == Degrees{180.0});

} // namespace orbex

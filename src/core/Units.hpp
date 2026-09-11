#ifndef ORBSIM_CORE_UNITS_HPP
#define ORBSIM_CORE_UNITS_HPP
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
// Each type is four lines because the behaviour lives in Quantity (see
// core/Scalar.hpp). Adding a unit is adding one of these blocks; nothing else
// needs to change. See docs/adr/0001 for why this and not a units library.
//
#include "core/Scalar.hpp"

#include <concepts>
#include <type_traits>

namespace orb {

struct Radians : Quantity<Radians> {
    constexpr Radians() noexcept = default;
    // explicit, or the type converts from a bare f64 on its own and rebuilds
    // the exact problem it was introduced to solve.
    explicit constexpr Radians(f64 v) noexcept : Quantity{v} {}
};

struct Degrees : Quantity<Degrees> {
    constexpr Degrees() noexcept = default;
    explicit constexpr Degrees(f64 v) noexcept : Quantity{v} {}
};

struct Metres : Quantity<Metres> {
    constexpr Metres() noexcept = default;
    explicit constexpr Metres(f64 v) noexcept : Quantity{v} {}
};

struct Seconds : Quantity<Seconds> {
    constexpr Seconds() noexcept = default;
    explicit constexpr Seconds(f64 v) noexcept : Quantity{v} {}
};

struct MetresPerSecond : Quantity<MetresPerSecond> {
    constexpr MetresPerSecond() noexcept = default;
    explicit constexpr MetresPerSecond(f64 v) noexcept : Quantity{v} {}
};

// Angular rate. Mean motion is the one the orbital code hands out.
struct RadiansPerSecond : Quantity<RadiansPerSecond> {
    constexpr RadiansPerSecond() noexcept = default;
    explicit constexpr RadiansPerSecond(f64 v) noexcept : Quantity{v} {}
};

// Specific orbital energy, J/kg (m^2/s^2). Negative for a bound orbit, zero
// for a parabola, positive for an escape trajectory.
struct SpecificEnergy : Quantity<SpecificEnergy> {
    constexpr SpecificEnergy() noexcept = default;
    explicit constexpr SpecificEnergy(f64 v) noexcept : Quantity{v} {}
};

// Dimensionless, but not interchangeable with any other dimensionless quantity.
// This is what stops solveKepler(anomaly, eccentricity) compiling backwards.
struct Eccentricity : Quantity<Eccentricity> {
    constexpr Eccentricity() noexcept = default;
    explicit constexpr Eccentricity(f64 v) noexcept : Quantity{v} {}
};

// Standard gravitational parameter GM of a central body, m^3/s^2.
struct GravParam : Quantity<GravParam> {
    constexpr GravParam() noexcept = default;
    explicit constexpr GravParam(f64 v) noexcept : Quantity{v} {}
};

[[nodiscard]] constexpr Radians toRadians(Degrees d) noexcept {
    return Radians{d.value * (kPi / 180.0)};
}

[[nodiscard]] constexpr Degrees toDegrees(Radians r) noexcept {
    return Degrees{r.value * (180.0 / kPi)};
}

// Strong-typed overloads of the wrap helpers in core/Scalar.hpp. Overloads
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
// Named rather than written as (1.0_km).value. The parentheses are not
// optional -- 1.0_km.value lexes as one pp-number and does not compile -- but
// naming the quantity says what is under test and needs no parentheses at all.
inline constexpr Metres kOneKilometre = 1.0_km;
static_assert(nearlyEqual(kOneKilometre.value, 1000.0, Tolerance{0.0}));
inline constexpr Degrees kHalfTurnInDegrees = 180.0_deg;
static_assert(nearlyEqual(kHalfTurnInDegrees.value, 180.0, Tolerance{0.0}));

// The unit-preserving arithmetic from Quantity, and the conversions it refuses.
// Exact results, so a zero tolerance: see nearlyEqual in core/Scalar.hpp.
static_assert(nearlyEqual((Radians{1.0} + Radians{2.0}).value, 3.0, Tolerance{0.0}));
static_assert(nearlyEqual((Seconds{3.0} - Seconds{1.0}).value, 2.0, Tolerance{0.0}));
static_assert(nearlyEqual((-Seconds{1.0}).value, -1.0, Tolerance{0.0}));
static_assert(nearlyEqual((Seconds{2.0} * 3.0).value, 6.0, Tolerance{0.0}));
static_assert(Metres{1.0} < Metres{2.0});
static_assert(!std::equality_comparable<Seconds>, "exact equality of a double is spelled out");
static_assert(!std::is_convertible_v<f64, Radians>);
static_assert(!std::is_convertible_v<Radians, f64>);
static_assert(!std::is_convertible_v<Degrees, Radians>, "conversion is toRadians(), by name");
static_assert(sizeof(Radians) == sizeof(f64));
static_assert(std::is_trivially_copyable_v<Radians>);

} // namespace orb

#endif // ORBSIM_CORE_UNITS_HPP

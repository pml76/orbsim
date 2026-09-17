#ifndef ORBEX_CORE_UNITS_HPP
#define ORBEX_CORE_UNITS_HPP
//
// Strong scalar types for the simulation domain, over mp-units.
//
// [S2] Each of these is an f64 at runtime and disappears entirely at -O2 (the
// zero-overhead claim is checked in tests/test_units.cpp, not merely asserted
// here). What they buy is that the compiler now knows a radian from a degree
// and an angle from an eccentricity -- distinctions that otherwise exist only
// in the head of whoever wrote the call. See docs/adr/0001 for the reasoning
// and docs/adr/0003 for why the mechanism changed on 2026-09-17.
//
// [S17] Velocity, Mass and Acceleration are still deliberately absent, and the
// reason is now stronger rather than weaker. They are not *needed*: with a real
// dimension system underneath, `distance / duration` already has a type, and it
// is the right one. A named `Velocity` would add a synonym, not a capability.
// Code written before it has a caller is code shaped by a guess.
//
#include "core/Scalar.hpp"

#include <concepts>
#include <type_traits>

#include <mp-units/framework.h>
#include <mp-units/systems/angular.h>
#include <mp-units/systems/si.h>

namespace orbex {

// [S8] The references the types below are built on, named once rather than
// spelled at each use. Deliberately *not* `using namespace
// mp_units::si::unit_symbols`: that header declares hundreds of one- and
// two-letter names -- a, h, m, s, t, g, p among them -- which under
// -Weverything turns every local called `a` into a -Wshadow error, and `a` is
// what every textbook calls a semi-major axis.
namespace units {

inline constexpr auto kMetre = mp_units::si::metre;
inline constexpr auto kSecond = mp_units::si::second;
inline constexpr auto kRadian = mp_units::angular::radian;
inline constexpr auto kDegree = mp_units::angular::degree;

} // namespace units

// [S2] Eccentricity is dimensionless, but it is its own *kind*: not
// interchangeable with any other ratio. This is what stops
// solveKepler(anomaly, eccentricity) compiling backwards, and it is the one
// thing a plain dimension system cannot express -- a bare dimensionless
// quantity accepts any ratio at all.
inline constexpr struct EccentricityKind final : mp_units::quantity_spec<mp_units::dimensionless> {
} kEccentricityKind;

// [S2] The type of every physical scalar: an mp-units quantity with this
// example's house rules put back on top of it.
//
// **One template, not six structs.** The six names below are aliases of it.
// That matters because arithmetic produces quantities with no name -- a length
// over a duration, a length times itself -- and those results have to obey the
// same rules a `Metres` does. Aliasing one template gives them all exactly what
// `Metres` has; six hand-written structs could not.
//
// [S11] mp-units' own `quantity` provides `==` and silences -Wfloat-equal
// inside it, lets a quantity of one unit convert implicitly to another of the
// same dimension, and will add a `Degrees` to a `Radians` because both are
// angles. All three are reasonable in a general-purpose library and none is
// allowed here, so this derives rather than aliases, and restores:
//
//   * **no `==`**, because on a double that is the comparison section 11
//     forbids. nearlyEqual with a zero tolerance says "exactly equal" out loud.
//   * **no silent unit conversion.** Degrees does not become Radians on its
//     own; toRadians() says so by name. Inheriting no constructors is what
//     does it: the conversion would need two user-defined steps, and the
//     language refuses that.
//   * **no arithmetic across two units of one dimension.** `Degrees + Radians`
//     is *deleted* below rather than merely not written, because not writing it
//     is not enough -- mp-units declares its own `operator+` as a hidden friend
//     and argument-dependent lookup finds it.
template <auto kReference> struct Scalar : mp_units::quantity<kReference, f64> {
    using base = mp_units::quantity<kReference, f64>;

    // [S6] **Zero, not indeterminate.** mp-units declares its storage without
    // an initialiser and defaults its default constructor, so `= default` here
    // would leave `Metres m;` holding whatever was on the stack. A default
    // member initializer is the usual way to say this; a base class needs a
    // constructor to say it instead.
    constexpr Scalar() noexcept : Scalar(f64{}) {}

    // [S2][S15] explicit, or the type converts from a bare f64 on its own and
    // rebuilds the exact problem it was introduced to solve.
    explicit constexpr Scalar(f64 v) noexcept : base(v * kReference) {}

    // From an mp-units quantity of the same reference: the result of an
    // expression that went through the dimension system and came back.
    explicit constexpr Scalar(base q) noexcept : base(q) {}

    [[nodiscard]] constexpr f64 value() const noexcept {
        return this->numerical_value_in(base::unit);
    }

    // [S11] Hidden friends taking Scalar on both sides, not members. As a
    // member the implicit object parameter is `const Scalar&` while mp-units'
    // own comparison takes the derived type exactly, so theirs wins on the left
    // argument and ours on the right and every `a < b` is ambiguous.
    [[nodiscard]] friend constexpr auto operator<=>(Scalar l, Scalar r) noexcept {
        return l.value() <=> r.value();
    }
    friend bool operator==(Scalar, Scalar) = delete;

    [[nodiscard]] constexpr bool bitIdentical(Scalar other) const noexcept {
        return bitsOf(value()) == bitsOf(other.value());
    }

    [[nodiscard]] friend constexpr Scalar operator-(Scalar q) noexcept {
        return Scalar{-static_cast<const base&>(q)};
    }
    [[nodiscard]] friend constexpr Scalar operator*(Scalar q, f64 s) noexcept {
        return Scalar{static_cast<const base&>(q) * s};
    }
    [[nodiscard]] friend constexpr Scalar operator*(f64 s, Scalar q) noexcept {
        return Scalar{static_cast<const base&>(q) * s};
    }
    [[nodiscard]] friend constexpr Scalar operator/(Scalar q, f64 s) noexcept {
        return Scalar{static_cast<const base&>(q) / s};
    }
};

// Sum and difference: the same unit only.
template <auto R1, auto R2>
    requires(std::is_same_v<Scalar<R1>, Scalar<R2>>)
[[nodiscard]] constexpr Scalar<R1> operator+(Scalar<R1> l, Scalar<R2> r) noexcept {
    using base = Scalar<R1>::base;
    return Scalar<R1>{static_cast<const base&>(l) + static_cast<const base&>(r)};
}
template <auto R1, auto R2>
    requires(std::is_same_v<Scalar<R1>, Scalar<R2>>)
[[nodiscard]] constexpr Scalar<R1> operator-(Scalar<R1> l, Scalar<R2> r) noexcept {
    using base = Scalar<R1>::base;
    return Scalar<R1>{static_cast<const base&>(l) - static_cast<const base&>(r)};
}

// [S11] And explicitly *not* across two units of one dimension. Deleted rather
// than absent: an overload that does not exist loses to mp-units' hidden
// friend, which does. These match exactly on both arguments and therefore win,
// and being deleted they make the expression ill-formed.
template <auto R1, auto R2>
    requires(!std::is_same_v<Scalar<R1>, Scalar<R2>>)
constexpr void operator+(Scalar<R1>, Scalar<R2>) = delete;
template <auto R1, auto R2>
    requires(!std::is_same_v<Scalar<R1>, Scalar<R2>>)
constexpr void operator-(Scalar<R1>, Scalar<R2>) = delete;

// Product and quotient: the unit algebra, which is the whole reason a units
// library is here. The result is a Scalar again, so it carries the house rules
// however long the chain gets.
template <auto R1, auto R2>
[[nodiscard]] constexpr auto operator*(Scalar<R1> l, Scalar<R2> r) noexcept {
    return Scalar<R1 * R2>{l.value() * r.value()};
}
template <auto R1, auto R2>
[[nodiscard]] constexpr auto operator/(Scalar<R1> l, Scalar<R2> r) noexcept {
    return Scalar<R1 / R2>{l.value() / r.value()};
}

// [S2] The six names. Each is one line because everything they do lives in
// Scalar<> above and in mp-units beneath it; adding a unit is adding a line.
using Dimensionless = Scalar<mp_units::one>;
using Radians = Scalar<units::kRadian>;
using Degrees = Scalar<units::kDegree>;
using Metres = Scalar<units::kMetre>;
using Seconds = Scalar<units::kSecond>;

// Dimensionless, but not interchangeable with any other dimensionless quantity.
using Eccentricity = Scalar<kEccentricityKind[mp_units::one]>;

// Standard gravitational parameter GM, in m^3/s^2.
using GravParam = Scalar<mp_units::pow<3>(units::kMetre) / mp_units::pow<2>(units::kSecond)>;

// [S2] Named, not implicit. The factor is mp-units', not ours: `.in()` applies
// the library's own degree-to-radian magnitude, which is the sort of constant
// worth not retyping.
[[nodiscard]] constexpr Radians toRadians(Degrees d) noexcept {
    return Radians{d.in(units::kRadian)};
}

[[nodiscard]] constexpr Degrees toDegrees(Radians r) noexcept {
    return Degrees{r.in(units::kDegree)};
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
static_assert(nearlyEqual(toRadians(180.0_deg).value(), kPi, Tolerance{1e-15}));
static_assert(nearlyEqual(toDegrees(Radians{kPi}).value(), 180.0, Tolerance{1e-13}));
static_assert(nearlyEqual(toRadians(toDegrees(Radians{1.0})).value(), 1.0, Tolerance{1e-15}));
// Named rather than written as (1.0_km).value(). The parentheses are not
// optional -- 1.0_km.value() lexes as one pp-number and does not compile -- but
// naming the quantity says what is under test and needs no parentheses at all.
inline constexpr Metres kOneKilometre = 1.0_km;
static_assert(nearlyEqual(kOneKilometre.value(), 1000.0, Tolerance{0.0}));
inline constexpr Degrees kHalfTurnInDegrees = 180.0_deg;
static_assert(nearlyEqual(kHalfTurnInDegrees.value(), 180.0, Tolerance{0.0}));

// [S3] What the dimension system buys, asserted rather than described.
static_assert(
    std::is_same_v<decltype(Metres{1.0} / Seconds{1.0}), Scalar<units::kMetre / units::kSecond>>,
    "a length over a duration is a speed, and nobody had to declare one");
static_assert(nearlyEqual((Metres{100.0} / Seconds{2.0}).value(), 50.0, Tolerance{0.0}));
static_assert(nearlyEqual(((Metres{100.0} / Seconds{2.0}) * Seconds{3.0}).value(),
                          150.0,
                          Tolerance{0.0}),
              "and a speed times a duration is the distance covered");

// [S3] And the errors it refuses. Concepts rather than bare requires-
// expressions, because a requires-expression on non-dependent operands is
// diagnosed rather than evaluated.
template <typename A, typename B>
concept addable = requires(const A& x, const B& y) { x + y; };
template <typename A, typename B>
concept equatable = requires(const A& x, const B& y) { x == y; };

static_assert(addable<Metres, Metres>);
static_assert(!addable<Metres, Seconds>, "a length plus a duration must not compile");
static_assert(!addable<Radians, Eccentricity>, "an angle plus a ratio must not compile");
static_assert(!addable<Degrees, Radians>, "two angles in different units must not add either");
static_assert(!equatable<Metres, Metres>, "exact equality of a double is spelled out");
static_assert(
    !equatable<decltype(Metres{1.0} / Seconds{1.0}), decltype(Metres{1.0} / Seconds{1.0})>,
    "and an unnamed quotient obeys the same rule as a named quantity");
static_assert(!std::is_convertible_v<f64, Radians>, "construction stays explicit");
static_assert(!std::is_convertible_v<Radians, f64>, "and there is no silent way back");
static_assert(!std::is_convertible_v<Degrees, Radians>, "conversion is toRadians(), by name");
static_assert(!std::is_convertible_v<Eccentricity, f64>,
              "a dimensionless quantity must not decay to a bare double");
static_assert(sizeof(Radians) == sizeof(f64), "a strong type must cost nothing");
static_assert(std::is_trivially_copyable_v<Radians>);

} // namespace orbex

#endif // ORBEX_CORE_UNITS_HPP

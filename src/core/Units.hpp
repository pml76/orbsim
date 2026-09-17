#ifndef ORBSIM_CORE_UNITS_HPP
#define ORBSIM_CORE_UNITS_HPP
//
// Strong scalar types for the simulation domain, over mp-units.
//
// Each is an f64 at runtime and disappears entirely at -O2 -- verified by
// reading the disassembly on 2026-09-17, not by trusting the claim: vis-viva
// through these types and through bare doubles emit the same five instructions.
// What they buy is that the compiler now knows a radian from a degree, an angle
// from an eccentricity, and a duration from a length -- distinctions that
// otherwise exist only in the head of whoever wrote the call.
//
// This is the highest-value rule in CODING_GUIDELINES.md for this codebase
// (section 2, I.4). We are writing software in the same problem domain that
// lost the Mars Climate Orbiter to a unit mismatch in 1999; the types carrying
// the information is the difference between a compile error and a spacecraft.
//
// They also close I.24 for free: `propagate(state, mu, dt)` used to take two
// adjacent f64 parameters that transposed in silence.
//
// **Since 2026-09-17 the dimensions are mp-units', not ours** (ADR 0019).
// Before that each type was an independent wrapper and `Metres / Seconds` did
// not compile, because nothing in the code knew what that division *is*. Now it
// yields a velocity, `Metres + Seconds` is still refused, and the arithmetic
// that changes a unit no longer has to be written out by hand. ADR 0001 has the
// older reasoning and why it was reversed.
//
// Adding a unit is adding one of the blocks below: a name, a reference, and
// nothing else. The behaviour lives in Unit<> and in mp-units beneath it.
//
#include "core/Scalar.hpp"

#include <concepts>
#include <type_traits>

#include <mp-units/framework.h>
#include <mp-units/systems/angular.h>
#include <mp-units/systems/si.h>

namespace orb {

// The references the types below are built on, named once. Deliberately *not*
// `using namespace mp_units::si::unit_symbols`: that header declares 356 one-
// and two-letter names, among them a, h, m, s, t, g and p, which is most of the
// notation of orbital mechanics. Under -Weverything every local named `a` or
// `h` would then be a -Wshadow error, and the semi-major axis is called `a` in
// every textbook this code is checked against. Measured 2026-09-17.
namespace units {

inline constexpr auto kMetre = mp_units::si::metre;
inline constexpr auto kSecond = mp_units::si::second;
inline constexpr auto kRadian = mp_units::angular::radian;
inline constexpr auto kDegree = mp_units::angular::degree;

} // namespace units

// Eccentricity is dimensionless, but it is its own *kind*: not interchangeable
// with any other ratio. This is what stops solveKepler(anomaly, eccentricity)
// compiling backwards, and it is the one thing a plain dimension system cannot
// express -- a bare `quantity<one, f64>` accepts any ratio at all. Measured
// 2026-09-17: without the kind, a mass ratio converts straight into an
// eccentricity; with it, neither converts to the other.
inline constexpr struct EccentricityKind final : mp_units::quantity_spec<mp_units::dimensionless> {
} kEccentricityKind;

// The base for every physical scalar: an mp-units quantity, with this project's
// house rules put back on top of it.
//
// mp-units' own `quantity` provides `==` and silences -Wfloat-equal inside it,
// and it lets a quantity of one unit convert implicitly to another of the same
// dimension. Both are reasonable for a general-purpose library and neither is
// allowed here, so this template derives rather than aliases, and restores:
//
//   * **no `==`.** On a double it is the comparison CODING_GUIDELINES section
//     11 forbids and -Wfloat-equal reports (ADR 0017). Exact equality is
//     spelled out through nearlyEqual with a zero tolerance, or bitIdentical
//     when bit identity is the claim.
//   * **no silent unit conversion.** Degrees does not become Radians on its
//     own; toRadians() says so by name. The facade inherits no constructors
//     from the base, so the conversion would need two user-defined steps and
//     the language refuses it.
//
// Same-unit arithmetic returns the named type, as it did before mp-units, so
// `Metres + Metres` is a `Metres` and not a bare quantity. The operators are
// hidden friends rather than members because a member would shadow the base's
// own and the linter reports that (bugprone-derived-method-shadowing-base-
// method). Arithmetic that *changes* the unit is deliberately not declared
// here: it falls through to mp-units, which is the entire point.
template <typename Derived, auto kReference> struct Unit : mp_units::quantity<kReference, f64> {
    using base = mp_units::quantity<kReference, f64>;

    [[nodiscard]] constexpr f64 value() const noexcept {
        return this->numerical_value_in(base::unit);
    }

    // Hidden friends taking Derived on both sides, not members. As members the
    // implicit object parameter is `const Unit&`, so mp-units' own comparison
    // friend -- which takes the derived type exactly -- wins on the left-hand
    // argument while ours wins on the right, and every `a < b` is ambiguous.
    // Taking Derived twice makes both arguments exact and settles it.
    [[nodiscard]] friend constexpr auto operator<=>(Derived l, Derived r) noexcept {
        return l.value() <=> r.value();
    }
    friend bool operator==(Derived, Derived) = delete;

    [[nodiscard]] constexpr bool bitIdentical(Derived other) const noexcept {
        return bitsOf(value()) == bitsOf(other.value());
    }

    [[nodiscard]] friend constexpr Derived operator-(Derived q) noexcept {
        return Derived{-static_cast<const base&>(q)};
    }
    [[nodiscard]] friend constexpr Derived operator+(Derived l, Derived r) noexcept {
        return Derived{static_cast<const base&>(l) + static_cast<const base&>(r)};
    }
    [[nodiscard]] friend constexpr Derived operator-(Derived l, Derived r) noexcept {
        return Derived{static_cast<const base&>(l) - static_cast<const base&>(r)};
    }
    [[nodiscard]] friend constexpr Derived operator*(Derived q, f64 scale) noexcept {
        return Derived{static_cast<const base&>(q) * scale};
    }
    [[nodiscard]] friend constexpr Derived operator*(f64 scale, Derived q) noexcept {
        return Derived{static_cast<const base&>(q) * scale};
    }
    [[nodiscard]] friend constexpr Derived operator/(Derived q, f64 scale) noexcept {
        return Derived{static_cast<const base&>(q) / scale};
    }

    constexpr Derived& operator+=(Derived other) noexcept {
        static_cast<base&>(*this) += static_cast<const base&>(other);
        return static_cast<Derived&>(*this);
    }
    constexpr Derived& operator-=(Derived other) noexcept {
        static_cast<base&>(*this) -= static_cast<const base&>(other);
        return static_cast<Derived&>(*this);
    }

private:
    // Only the named derived type may construct its base, which is what stops
    // `struct Other : Unit<Radians, units::kRadian>` from compiling by accident
    // -- and is what bugprone-crtp-constructor-accessibility asks for.
    //
    // Each type below therefore spells its three constructors out rather than
    // writing `using Unit::Unit;`: an inherited constructor keeps the access it
    // had in the base, so a using-declaration would republish these as private
    // and `Radians{1.0}` would not compile.
    friend Derived;

    // **Zero, not indeterminate.** mp-units' quantity declares its storage
    // without an initialiser and defaults its default constructor, so `= default`
    // here would leave `Metres m;` holding whatever was on the stack. The old
    // hand-rolled base had `f64 value{}` and zero-initialised; keeping that is
    // not a preference but the difference between a deterministic simulation and
    // one that is not. cppcoreguidelines-pro-type-member-init found this, on the
    // seven uninitialised members of Elements, which is the tooling earning its
    // place (CLAUDE.md rule 5).
    constexpr Unit() noexcept : Unit(f64{}) {}

    // "this many of my unit". Explicit, or the type converts from a bare f64 on
    // its own and rebuilds the exact problem it was introduced to solve.
    explicit constexpr Unit(f64 v) noexcept : base(v * kReference) {}

    // From an mp-units quantity of the same reference -- the result of an
    // expression that went through the dimension system and came back.
    explicit constexpr Unit(base q) noexcept : base(q) {}
};

struct Radians : Unit<Radians, units::kRadian> {
    constexpr Radians() noexcept = default;
    explicit constexpr Radians(f64 v) noexcept : Unit{v} {}
    explicit constexpr Radians(base q) noexcept : Unit{q} {}
};

struct Degrees : Unit<Degrees, units::kDegree> {
    constexpr Degrees() noexcept = default;
    explicit constexpr Degrees(f64 v) noexcept : Unit{v} {}
    explicit constexpr Degrees(base q) noexcept : Unit{q} {}
};

struct Metres : Unit<Metres, units::kMetre> {
    constexpr Metres() noexcept = default;
    explicit constexpr Metres(f64 v) noexcept : Unit{v} {}
    explicit constexpr Metres(base q) noexcept : Unit{q} {}
};

struct Seconds : Unit<Seconds, units::kSecond> {
    constexpr Seconds() noexcept = default;
    explicit constexpr Seconds(f64 v) noexcept : Unit{v} {}
    explicit constexpr Seconds(base q) noexcept : Unit{q} {}
};

struct MetresPerSecond : Unit<MetresPerSecond, units::kMetre / units::kSecond> {
    constexpr MetresPerSecond() noexcept = default;
    explicit constexpr MetresPerSecond(f64 v) noexcept : Unit{v} {}
    explicit constexpr MetresPerSecond(base q) noexcept : Unit{q} {}
};

// Angular rate. Mean motion is the one the orbital code hands out.
struct RadiansPerSecond : Unit<RadiansPerSecond, units::kRadian / units::kSecond> {
    constexpr RadiansPerSecond() noexcept = default;
    explicit constexpr RadiansPerSecond(f64 v) noexcept : Unit{v} {}
    explicit constexpr RadiansPerSecond(base q) noexcept : Unit{q} {}
};

// Specific orbital energy, J/kg (m^2/s^2). Negative for a bound orbit, zero
// for a parabola, positive for an escape trajectory.
struct SpecificEnergy : Unit<SpecificEnergy, mp_units::si::joule / mp_units::si::kilogram> {
    constexpr SpecificEnergy() noexcept = default;
    explicit constexpr SpecificEnergy(f64 v) noexcept : Unit{v} {}
    explicit constexpr SpecificEnergy(base q) noexcept : Unit{q} {}
};

// Dimensionless, but not interchangeable with any other dimensionless quantity.
struct Eccentricity : Unit<Eccentricity, kEccentricityKind[mp_units::one]> {
    constexpr Eccentricity() noexcept = default;
    explicit constexpr Eccentricity(f64 v) noexcept : Unit{v} {}
    explicit constexpr Eccentricity(base q) noexcept : Unit{q} {}
};

// Standard gravitational parameter GM of a central body, m^3/s^2.
struct GravParam
    : Unit<GravParam, mp_units::pow<3>(units::kMetre) / mp_units::pow<2>(units::kSecond)> {
    constexpr GravParam() noexcept = default;
    explicit constexpr GravParam(f64 v) noexcept : Unit{v} {}
    explicit constexpr GravParam(base q) noexcept : Unit{q} {}
};

// Named, not implicit -- see the note on Unit above. The factor is mp-units',
// not ours: `.in()` applies the library's own degree-to-radian magnitude, which
// is the sort of thing this project stopped hand-writing on 2026-09-17.
[[nodiscard]] constexpr Radians toRadians(Degrees d) noexcept {
    return Radians{d.in(units::kRadian)};
}

[[nodiscard]] constexpr Degrees toDegrees(Radians r) noexcept {
    return Degrees{r.in(units::kDegree)};
}

// Strong-typed overloads of the wrap helpers in core/Scalar.hpp. Overloads
// rather than reimplementations: one behaviour, two spellings.
[[nodiscard]] inline Radians wrapTau(Radians a) noexcept { return Radians{wrapTau(a.value())}; }
[[nodiscard]] inline Radians wrapPi(Radians a) noexcept { return Radians{wrapPi(a.value())}; }

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

// The unit-preserving arithmetic from Unit, and the conversions it refuses.
// Exact results, so a zero tolerance: see nearlyEqual in core/Scalar.hpp.
static_assert(nearlyEqual((Radians{1.0} + Radians{2.0}).value(), 3.0, Tolerance{0.0}));
static_assert(nearlyEqual((Seconds{3.0} - Seconds{1.0}).value(), 2.0, Tolerance{0.0}));
static_assert(nearlyEqual((-Seconds{1.0}).value(), -1.0, Tolerance{0.0}));
static_assert(nearlyEqual((Seconds{2.0} * 3.0).value(), 6.0, Tolerance{0.0}));
static_assert(nearlyEqual((3.0 * Seconds{2.0}).value(), 6.0, Tolerance{0.0}));
static_assert(nearlyEqual((Seconds{6.0} / 3.0).value(), 2.0, Tolerance{0.0}));
static_assert(Metres{1.0} < Metres{2.0});
static_assert(!std::equality_comparable<Seconds>, "exact equality of a double is spelled out");
static_assert(!std::is_convertible_v<f64, Radians>);
static_assert(!std::is_convertible_v<Radians, f64>);
static_assert(!std::is_convertible_v<Degrees, Radians>, "conversion is toRadians(), by name");
static_assert(sizeof(Radians) == sizeof(f64));
static_assert(std::is_trivially_copyable_v<Radians>);
static_assert(Seconds{0.0}.bitIdentical(Seconds{0.0}) && !Seconds{0.0}.bitIdentical(Seconds{-0.0}),
              "bit identity tells the two zeros apart, as a determinism check needs");

// What mp-units adds, and the reason ADR 0019 went this way rather than
// widening the old hand-rolled base. None of these compiled before 2026-09-17.
static_assert(
    nearlyEqual((Metres{100.0} / Seconds{2.0}).numerical_value_in(units::kMetre / units::kSecond),
                50.0,
                Tolerance{0.0}),
    "a length over a time is a speed, and the type system knows it");
static_assert(!std::is_convertible_v<Eccentricity, f64>,
              "a dimensionless quantity must not decay to a bare double");

// The dimensional errors, refused. A concept rather than a bare requires-
// expression because a requires-expression on non-dependent operands is
// diagnosed rather than evaluated.
template <typename A, typename B>
concept addable = requires(const A& x, const B& y) { x + y; };
static_assert(addable<Metres, Metres>);
static_assert(!addable<Metres, Seconds>, "a length plus a time must not compile");
static_assert(!addable<Radians, Eccentricity>, "an angle plus a ratio must not compile");

} // namespace orb

#endif // ORBSIM_CORE_UNITS_HPP

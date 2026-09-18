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

// The type of every physical scalar: an mp-units quantity, with this
// project's house rules put back on top of it.
//
// **One template, not nine structs** (changed 2026-09-17 while doing ADR 0019
// step 2). The nine names below are aliases of it. That matters because vector
// arithmetic produces quantities with no name at all -- `cross(r, v)` is m2/s
// and its own dot product is m4/s2 -- and those results have to obey the same
// rules as `Metres` does. With nine hand-written structs they could not: an
// unnamed result would have been a bare mp-units quantity, on which `==`
// compiles. Aliasing one template gives `Scalar<m2/s>` exactly what
// `Scalar<metre>` has.
//
// mp-units' own `quantity` provides `==` and silences -Wfloat-equal inside it,
// lets a quantity of one unit convert implicitly to another of the same
// dimension, and will add a `Degrees` to a `Radians` because both are angles.
// All three are reasonable for a general-purpose library and none is allowed
// here, so this template derives rather than aliases, and restores:
//
//   * **no `==`.** On a double it is the comparison CODING_GUIDELINES section
//     11 forbids and -Wfloat-equal reports (ADR 0017). Exact equality is
//     spelled out through nearlyEqual with a zero tolerance, or bitIdentical
//     when bit identity is the claim.
//   * **no silent unit conversion.** Degrees does not become Radians on its
//     own; toRadians() says so by name. The facade inherits no constructors
//     from the base, so the conversion would need two user-defined steps and
//     the language refuses it.
//   * **no arithmetic across two units of one dimension.** `Degrees + Radians`
//     is deleted below rather than merely unwritten, because leaving it
//     unwritten is not enough -- mp-units declares its own `operator+` as a
//     hidden friend and it is found by ADL. That hole shipped in the first
//     version of this header and is the reason the deleted overloads exist.
//
// Arithmetic is closed over this template: same unit in, same unit out for
// `+` and `-`; the product or quotient reference for `*` and `/`. So every
// intermediate in a chain is a `Scalar<R>` and none of them accepts `==`.
template <auto kReference> struct Scalar : mp_units::quantity<kReference, f64> {
    using base = mp_units::quantity<kReference, f64>;

    // **Zero, not indeterminate.** mp-units' quantity declares its storage
    // without an initialiser and defaults its default constructor, so
    // `= default` here would leave `Metres m;` holding whatever was on the
    // stack. The hand-rolled base this replaced had `f64 value{}` and
    // zero-initialised; keeping that is not a preference but the difference
    // between a deterministic simulation and one that is not.
    // cppcoreguidelines-pro-type-member-init found it, on the seven
    // uninitialised members of Elements (CLAUDE.md rule 5).
    constexpr Scalar() noexcept : Scalar(f64{}) {}

    // "this many of my unit". Explicit, or the type converts from a bare f64 on
    // its own and rebuilds the exact problem it was introduced to solve.
    explicit constexpr Scalar(f64 v) noexcept : base(v * kReference) {}

    // From an mp-units quantity of the same reference -- the result of an
    // expression that went through the dimension system and came back.
    explicit constexpr Scalar(base q) noexcept : base(q) {}

    [[nodiscard]] constexpr f64 value() const noexcept {
        return this->numerical_value_in(base::unit);
    }

    // Hidden friends taking Scalar on both sides, not members. As a member the
    // implicit object parameter is `const Scalar&` while mp-units' own
    // comparison friend takes the derived type exactly, so ours wins on the
    // right-hand argument and theirs on the left and every `a < b` is
    // ambiguous. Taking Scalar twice makes both arguments exact and settles it.
    [[nodiscard]] friend constexpr auto operator<=>(Scalar l, Scalar r) noexcept {
        return l.value() <=> r.value();
    }
    friend bool operator==(Scalar, Scalar) = delete;

    [[nodiscard]] constexpr bool bitIdentical(Scalar other) const noexcept {
        return bitsOf(value()) == bitsOf(other.value());
    }

    // mp-units gives a *dimensionless* quantity an `explicit operator V_()` for
    // any V_ merely **constructible** from its representation, so that
    // `double(ratio)` works. Its body then returns that representation, which
    // needs V_ to be *convertible* from it -- a stronger thing than the
    // constraint asks. The gap has a sharp consequence here: every Scalar is
    // constructible from an f64 explicitly, so the operator is viable for every
    // one of them, and `std::is_constructible_v<Metres, Eccentricity>` answers
    // **true** while `Metres{someEccentricity}` still refuses to compile -- the
    // failure is in the operator's body, where no trait and no
    // requires-expression can see it. A trait that says yes where the compiler
    // says no is worse than either answer, because a trait is what a test asks.
    //
    // So it is deleted, and only where it exists: the condition is that the
    // base offers a conversion to a plain number at all, which is exactly
    // mp-units' dimensionless case. Deleting it unconditionally also kills the
    // legitimate quantity-to-quantity conversions -- `Scalar<m2/s2>` into a
    // `SpecificEnergy` in J/kg, which the test support does -- because a
    // conversion operator on the source beats a converting constructor on the
    // target. Measured both ways, 2026-09-18.
    //
    // Nothing here wants the operator in either case: `.value()` is how a
    // number comes out of a quantity in this project.
    static constexpr bool kBaseConvertsToANumber = std::is_constructible_v<f64, base>;

    template <typename V>
        requires kBaseConvertsToANumber
    explicit constexpr operator V() const = delete;
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

    constexpr Scalar& operator+=(Scalar other) noexcept {
        static_cast<base&>(*this) += static_cast<const base&>(other);
        return *this;
    }
    constexpr Scalar& operator-=(Scalar other) noexcept {
        static_cast<base&>(*this) -= static_cast<const base&>(other);
        return *this;
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

// And explicitly *not* across two units of one dimension. Deleted rather than
// absent: mp-units' `operator+` is a hidden friend and ADL finds it, so an
// overload that merely does not exist loses to one that does. These are an
// exact match on both arguments and therefore win, and being deleted they make
// the expression ill-formed -- which is what `addable` below measures.
template <auto R1, auto R2>
    requires(!std::is_same_v<Scalar<R1>, Scalar<R2>>)
constexpr void operator+(Scalar<R1>, Scalar<R2>) = delete;
template <auto R1, auto R2>
    requires(!std::is_same_v<Scalar<R1>, Scalar<R2>>)
constexpr void operator-(Scalar<R1>, Scalar<R2>) = delete;

// Product and quotient: the unit algebra, which is the whole reason mp-units
// is here. The result is a Scalar again, so it carries the house rules however
// long the chain gets.
template <auto R1, auto R2>
[[nodiscard]] constexpr auto operator*(Scalar<R1> l, Scalar<R2> r) noexcept {
    return Scalar<R1 * R2>{l.value() * r.value()};
}
template <auto R1, auto R2>
[[nodiscard]] constexpr auto operator/(Scalar<R1> l, Scalar<R2> r) noexcept {
    return Scalar<R1 / R2>{l.value() / r.value()};
}

// The nine names. Each is one line because everything they do lives in
// Scalar<> above and in mp-units beneath it; adding a unit is adding a line.
//
// They are aliases rather than distinct types, so two of them over the same
// reference would be the same type. That is fine here -- no two of these share
// one -- and where a distinction is wanted without a distinct unit, the way to
// get it is a *kind*, as Eccentricity does below.
using Radians = Scalar<units::kRadian>;
using Degrees = Scalar<units::kDegree>;
using Metres = Scalar<units::kMetre>;
using Seconds = Scalar<units::kSecond>;
using MetresPerSecond = Scalar<units::kMetre / units::kSecond>;

// Angular rate. Mean motion is the one the orbital code hands out.
using RadiansPerSecond = Scalar<units::kRadian / units::kSecond>;

// Specific orbital energy, J/kg (m^2/s^2). Negative for a bound orbit, zero
// for a parabola, positive for an escape trajectory.
using SpecificEnergy = Scalar<mp_units::si::joule / mp_units::si::kilogram>;

// Dimensionless, but not interchangeable with any other dimensionless
// quantity -- that is what the kind above buys.
using Eccentricity = Scalar<kEccentricityKind[mp_units::one]>;

// A reciprocal time. The Lagrange coefficient fdot is one, and naming it is
// what lets `position * fdot` be checked as a velocity.
using PerSecond = Scalar<mp_units::one / units::kSecond>;

// Standard gravitational parameter GM of a central body, m^3/s^2.
using GravParam = Scalar<mp_units::pow<3>(units::kMetre) / mp_units::pow<2>(units::kSecond)>;

// Named, not implicit -- see the note on Scalar above. The factor is mp-units',
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
// Constructibility as well as convertibility. The two answer different
// questions -- what happens by accident, and what happens when somebody writes
// the braces on purpose -- and for one day the first of these answered wrongly:
// see the note on the deleted conversion operator above.
static_assert(!std::is_constructible_v<Radians, Eccentricity>,
              "an eccentricity must never be usable as an angle");
static_assert(!std::is_constructible_v<Eccentricity, Radians>, "nor an angle an eccentricity");
static_assert(!std::is_constructible_v<Metres, Seconds>, "nor a duration a length");

// `Radians{someDegrees}` is *not* in that list, and the reason is a change for
// the better. It compiles -- explicitly -- and it multiplies by pi/180, because
// mp-units knows what a degree is. The hand-rolled types this replaced would
// have reinterpreted the number instead, turning 180 degrees into 180 radians
// in silence, which is why the rule was "conversion is toRadians(), by name".
// toRadians() is still the way to say it, and now the other spelling is merely
// redundant rather than wrong.
static_assert(nearlyEqual(Radians{kHalfTurnInDegrees}.value(), kPi, Tolerance{1e-15}),
              "explicit construction across two units of one dimension converts, "
              "and no longer reinterprets");
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

// The dimensional errors, refused. Concepts rather than bare requires-
// expressions because a requires-expression on non-dependent operands is
// diagnosed rather than evaluated.
template <typename A, typename B>
concept addable = requires(const A& x, const B& y) { x + y; };
template <typename A, typename B>
concept equatable = requires(const A& x, const B& y) { x == y; };

static_assert(addable<Metres, Metres>);
static_assert(!addable<Metres, Seconds>, "a length plus a time must not compile");
static_assert(!addable<Radians, Eccentricity>, "an angle plus a ratio must not compile");

// **Two units of one dimension must not add either.** Both of these are
// angles, so mp-units' own operator+ is perfectly willing; the deleted
// overloads above are what stops it. This assertion is here because the first
// version of this header did not have them and `Degrees + Radians` compiled --
// a regression against what the hand-rolled types did, found by asking the
// question rather than by anything failing.
static_assert(!addable<Degrees, Radians>, "two angles in different units must not add");
static_assert(!addable<Radians, Degrees>, "nor the other way round");

// `==` is gone from every Scalar, including the ones with no name. An
// intermediate such as r x v is m2/s and has no entry in the list of nine;
// before Scalar<> was one template it came back as a bare mp-units quantity,
// which accepts `==` and silences -Wfloat-equal while doing it.
static_assert(!equatable<Metres, Metres>, "exact equality of a double is spelled out");
static_assert(!equatable<Degrees, Radians>, "and not across units either");
static_assert(
    !equatable<decltype(Metres{1.0} / Seconds{1.0}), decltype(Metres{1.0} / Seconds{1.0})>,
    "an unnamed quotient obeys the same rule as a named quantity");
static_assert(!equatable<decltype(Metres{1.0} * Metres{1.0}), decltype(Metres{1.0} * Metres{1.0})>,
              "and so does an unnamed product");

// The unit algebra itself: a quotient is the quotient unit, and it is a
// Scalar, not a bare quantity.
static_assert(std::is_same_v<decltype(Metres{1.0} / Seconds{1.0}), MetresPerSecond>,
              "a length over a time is exactly the named velocity type");
static_assert(nearlyEqual((Metres{100.0} / Seconds{2.0}).value(), 50.0, Tolerance{0.0}));
static_assert(nearlyEqual((MetresPerSecond{3.0} * Seconds{2.0}).value(), 6.0, Tolerance{0.0}),
              "and a speed times a time is the length it travelled");

} // namespace orb

#endif // ORBSIM_CORE_UNITS_HPP

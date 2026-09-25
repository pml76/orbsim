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
#include <cstdint>
#include <expected>
#include <string_view>
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
inline constexpr auto kSteradian = mp_units::angular::steradian;

// **A length on the screen gets a dimension of its own** (M1-12, register
// decision 137), which is the one thing in this header mp-units does not
// already provide.
//
// A pixel is a picture element, so in SI it is a count and therefore
// dimensionless. Giving it a dimension anyway is a modelling choice, and it is
// exactly the choice mp-units itself makes for the **angle** -- formally
// dimensionless too, and given `dim_angle` regardless. This project already
// relies on that: `kRadian` above is `angular::radian`, which is why an angle
// cannot be built from a ratio. A screen length wants the same treatment for
// the same reason. Adding pixels to a mass fraction is meaningless, while the
// ratio of two screen lengths is an ordinary number, and a dimension says both.
//
// **What was tried first, and why it was not enough.** Until 2026-09-23 this
// was an mp-units *kind* -- dimensionless, but nominally not interchangeable
// with other ratios -- copied from the shape `EccentricityKind` had until
// M1-87. A kind restricts **implicit** conversion and, by design, does not
// restrict explicit construction: `explicitly_convertible(dimensionless,
// kPixelKind)` is **true**, measured on all three front ends, so
// `Pixels{someRatio}` was legitimate mp-units. It appeared to be refused under
// clang and MSVC, and that appearance was an accident of `Scalar`'s deleted
// conversion operator, which the front ends resolve differently for class
// targets -- gcc accepted what clang and MSVC rejected, and `linux-gcc` was
// what said so. A dimension refuses it outright, everywhere, and needs no
// help from that operator: `Pixels::kBaseConvertsToANumber` is **false**,
// because this base is not dimensionless, so the operator is not declared for
// it at all.
inline constexpr struct PixelDimension final : mp_units::base_dimension<"px"> {
} kPixelDimension;
inline constexpr struct ScreenLength final : mp_units::quantity_spec<kPixelDimension> {
} kScreenLength;
inline constexpr struct Pixel final : mp_units::named_unit<"px", mp_units::kind_of<kScreenLength>> {
} kPixel;

} // namespace units

// *(An `EccentricityKind` lived here from 2026-09-17 until 2026-09-22. It was
// an mp-units **kind** -- dimensionless, but not interchangeable with any
// other ratio -- which is what stopped `solveKepler(anomaly, eccentricity)`
// compiling backwards, and it was the one thing a plain dimension system
// cannot express, since a bare `quantity<one, f64>` accepts any ratio at all.
//
// Eccentricity is a class now (M1-87, decision 108), and a class is not
// interchangeable with anything, so the separation the kind bought is kept and
// strengthened -- the assertions at the foot of this header still hold, and
// hold against a stronger claim. What is genuinely given up is the arithmetic:
// an eccentricity no longer takes part in the unit algebra. Nothing used it
// there, measured before the change, and no task document asks for it.
// GravParam, which does have arithmetic ahead of it in M1-62, keeps its
// quantity instead of losing it -- see `quantity()` below.)*

// *(A `PixelKind` lived here for one day, 2026-09-23. The dimension that
// replaced it is in the `units` namespace above, where the references belong,
// and the comment there says what a kind did not do.)*

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
    //
    // **Narrowed to arithmetic targets on 2026-09-24** (register decision 138),
    // and the reason is the whole of what M1-12 found. Deleting a conversion to
    // *every* type, in order to refuse the two or three that matter, declares a
    // deleted candidate that overload resolution must consider for **class**
    // targets as well -- and the three front ends do not agree about what that
    // means. gcc-14 accepted what clang 23.1 and MSVC 14.51 rejected. Reduced
    // to fourteen lines with no library in it: a derived class carrying a
    // deleted `operator V()` template, handed to a constructor taking its base,
    // is constructible under gcc and not under the other two.
    //
    // That divergence did real damage before it was understood. M1-12 asserted
    // that a ratio of two lengths is not a number of pixels; the assertion held
    // on three toolchains and failed on the fourth, and the *agreement* was the
    // wrong answer -- an accident of this operator, not anything the type
    // system promised.
    //
    // `std::is_arithmetic_v<V>` is exactly what mp-units' operator exists for
    // and exactly what this one exists to refuse, so the narrowing gives up
    // nothing. Measured before it was made: `f64{ratio}` still refused,
    // `is_constructible_v<f64, Ratio>` still false, the trait still agreeing
    // with the compiler in every row, `SpecificEnergy{m2/s2}` still working --
    // and all three front ends agreeing, which they did not before.
    static constexpr bool kBaseConvertsToANumber = std::is_constructible_v<f64, base>;

    template <typename V>
        requires kBaseConvertsToANumber && std::is_arithmetic_v<V>
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

// What a validated scalar refuses (M1-87, register decisions 106 and 111).
//
// Two of the nine types below have a physical bound, and since 2026-09-22 they
// hold it themselves rather than trusting every caller: a value outside the
// bound cannot be constructed, so no function downstream has to decide whether
// to report it, assert it, or -- as three anomaly converters did -- return a
// plausible wrong answer. That is VERIFICATION.md rule 24, prefer the bug you
// cannot write, chosen over rule 7's report-or-assert.
//
// It lives here, beside the types, rather than in a layer-wide error header:
// the precedent is decision 90, which put EphemerisError in astro/Sun.hpp for
// the same reason. `core` cannot reach OrbitError -- the dependency is one-way
// -- which is the other half of why this exists.
//
// One name per failure (decision 45's rule), each saying what it means
// physically rather than which predicate failed. NonPositiveGravity keeps the
// name it had in OrbitError, which commits, tests and records already use.
enum class UnitError : std::uint8_t {
    NotFinite,            // a NaN or an infinity
    NegativeEccentricity, // e < 0 is not a conic
    NonPositiveGravity,   // mu <= 0 is not a central body
};

// No `default:`, as every describe() in this project is written, so that
// adding a value to the enum is a -Wswitch error at the function that must
// then be updated rather than a silent "unknown".
[[nodiscard]] constexpr std::string_view describe(UnitError error) noexcept {
    switch (error) {
    case UnitError::NotFinite:
        return "the value must be finite";
    case UnitError::NegativeEccentricity:
        return "an eccentricity must not be negative";
    case UnitError::NonPositiveGravity:
        return "a gravitational parameter must be greater than zero";
    }
    return "unknown unit error";
}

// The nine names. Seven are one line because everything they do lives in
// Scalar<> above and in mp-units beneath it; adding such a unit is adding a
// line. The other two, Eccentricity and GravParam, are classes, because they
// are the two with a bound to hold.
//
// The seven are aliases rather than distinct types, so two of them over the
// same reference would be the same type. That is fine here -- no two of them
// share one -- and where a distinction is wanted without a distinct unit, the
// way to get it is a *kind*.
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

// The shape of a conic: 0 is a circle, below 1 an ellipse, 1 a parabola, above
// 1 a hyperbola. Dimensionless, and not interchangeable with any other ratio.
//
// **Validated at construction** (M1-87, decision 107). A negative eccentricity
// is not a conic, and before 2026-09-22 three of the four anomaly converters
// accepted one: measured on a probe against the real library, e = -0.5 gave a
// finite, plausible, wrong answer -- in fact each converter returned the
// *other's* answer for +0.5, because the sqrt((1-e)/(1+e)) factor inverts --
// and e = -1.5 returned a NaN as a valid Radians. Their sibling
// meanToEccentricAnomaly asserted the same condition four lines away. None of
// those guards exists now, because none of them can be reached.
//
// **Finite and non-negative, with no upper bound.** A hyperbolic eccentricity
// is unbounded in principle, and orbit/Orbit.hpp records convergence measured
// from 0 to 100; a cap would be a threshold carrying a hidden scale, which is
// the defect class this project has already shipped once.
class Eccentricity {
public:
    // Zero -- a circle -- and a real value, not an indeterminate one. Elements
    // holds one as an aggregate member, so this has to exist, and
    // cppcoreguidelines-pro-type-member-init is what found the equivalent gap
    // in mp-units' own default constructor on 2026-09-17.
    constexpr Eccentricity() noexcept = default;

    [[nodiscard]] static constexpr std::expected<Eccentricity, UnitError> from(f64 v) noexcept {
        if (!isFinite(v)) return std::unexpected(UnitError::NotFinite);
        if (v < 0.0) return std::unexpected(UnitError::NegativeEccentricity);
        return Eccentricity{v};
    }

    [[nodiscard]] constexpr f64 value() const noexcept { return value_; }

    // Ordering without equality, as Scalar has: comparing two doubles with ==
    // is what CODING_GUIDELINES section 11 forbids, and bit identity says the
    // other thing by name.
    [[nodiscard]] friend constexpr auto operator<=>(Eccentricity l, Eccentricity r) noexcept {
        return l.value_ <=> r.value_;
    }
    friend bool operator==(Eccentricity, Eccentricity) = delete;

    [[nodiscard]] constexpr bool bitIdentical(Eccentricity other) const noexcept {
        return bitsOf(value_) == bitsOf(other.value_);
    }

private:
    explicit constexpr Eccentricity(f64 v) noexcept : value_{v} {}

    f64 value_{};
};

// A length on the screen. See `units::kPixelDimension` above for why a pixel
// has a dimension of its own rather than being a dimensionless ratio.
//
// **A real quantity and not a Count**, deliberately: a subdivision threshold of
// 2.5 px is meaningful, and rounding it to 2 or 3 would change what the
// quadtree does. M1-50 derives the screen-space error in it and M1-59 makes it
// a RenderQuality field.
//
// **It does not validate itself yet, and that is a decision rather than an
// oversight** (M1-12, register decision 126). ADR 0022 says a scalar with a
// physical bound holds its own bound, and this one has no settled bound to
// hold: a threshold wants "finite and above zero", a distance wants "not
// negative", and a pixel *coordinate* -- which register decision 105 leaves to
// M1-80 -- wants any sign at all. It becomes a validated class when the first
// caller says which of those it is, which is M1-50 or M1-59, and not before.
// The dimension is what separates it in the meantime, and separation and
// validation are different jobs.
using Pixels = Scalar<units::kPixel>;

// A reciprocal time. The Lagrange coefficient fdot is one, and naming it is
// what lets `position * fdot` be checked as a velocity.
using PerSecond = Scalar<mp_units::one / units::kSecond>;

// Radiant flux per unit area, W/m^2. The solar constant is one, and so is
// everything the radiometric renderer exposes for (M1-08, M1-18). Named so
// that an irradiance cannot be handed to something expecting a radiance,
// W/(m^2 sr), which is the mistake this domain actually makes.
using Irradiance = Scalar<mp_units::si::watt / mp_units::pow<2>(units::kMetre)>;

// **The renderer's three photometric quantities** (M1-15, register decisions
// 175 and 178). The HDR target holds radiance; a camera's exposure is defined
// on luminance; a luminous efficacy converts one into the other. Named, so
// that the conversion view/Exposure.hpp states in prose is also checked by the
// dimension system: a radiance times an efficacy is a luminance, and the
// assertions at the foot of this header say so.
//
// Radiant flux per unit area and solid angle, W/(m^2 sr): what a surface sends
// towards the eye, and what every shader writes into the HDR target (ADR 0014).
//
// **The steradian is the angular system's, as the radian is.** mp-units' SI
// steradian is m^2/m^2 and has no dimension, so over it an irradiance would
// convert explicitly into a radiance -- a W/m^2 read as a W/(m^2 sr) is the
// mistake this domain actually makes, and it was measured compiling on
// 2026-09-25 before this was changed. `angular::steradian` is the square of
// `angular::radian`, which `kRadian` already uses for the same reason.
using Radiance = Scalar<mp_units::si::watt / (mp_units::pow<2>(units::kMetre) * units::kSteradian)>;
// Luminous intensity per unit area, cd/m^2: radiance weighted by the eye.
using Luminance = Scalar<mp_units::si::candela / mp_units::pow<2>(units::kMetre)>;
// Luminous flux per unit radiant flux, lm/W: how much of a watt the eye sees,
// which depends on the spectrum -- 683 lm/W at 555 nm by the definition of the
// candela, and far less for broadband light. Spelled cd sr / W rather than
// `si::lumen / si::watt`, because mp-units' lumen is built on the
// dimensionless SI steradian, and a lumen over the angular one would leave a
// solid angle behind in every luminance.
using LuminousEfficacy = Scalar<mp_units::si::candela * units::kSteradian / mp_units::si::watt>;

// Standard gravitational parameter GM of a central body, m^3/s^2.
//
// **Validated at construction** (M1-87, decision 108). A mu that is not
// greater than zero is not a central body, and until 2026-09-22 orbit/ had two
// answers for one: elementsFromState, propagate and propagateElements reported
// NonPositiveGravity, while stateFromElements and orbitInfo asserted. ADR 0002
// asks for one strategy per layer; this is the third option, which is that the
// value never exists. Those five checks and that enumerator are gone with it.
//
// **It keeps its quantity** (decision 110), which is the point of holding a
// Scalar rather than an f64. Scalar<R> *derives from* mp_units::quantity<R,
// f64>, so the unit algebra belongs to the base and survives being held: the
// force model in M1-62 can still write `mu.quantity() / (r * r)` and get an
// m/s^2 quantity, which is what ADR 0019 promised when it said `mu / (r*r)`
// produces an acceleration type. Validating the value was not allowed to cost
// that, and it did not have to.
class GravParam {
public:
    using Quantity = Scalar<mp_units::pow<3>(units::kMetre) / mp_units::pow<2>(units::kSecond)>;

    // No default constructor: a mu of zero is not a neutral starting value, it
    // is an invalid one, and there is nothing sensible for a default to hold.
    // DeltaUt1 declines one for the mirror-image reason -- there, zero is
    // valid but is a modelling decision that must be named at the call site.
    GravParam() = delete;

    [[nodiscard]] static constexpr std::expected<GravParam, UnitError> from(f64 v) noexcept {
        if (!isFinite(v)) return std::unexpected(UnitError::NotFinite);
        // Negated, so that a NaN would fail it too -- it cannot reach here,
        // but the shape is the one view/Projection.hpp's isFinitePositive
        // exists to keep, and readability-simplify-boolean-expr would rewrite
        // `v <= 0.0` into something that accepts a NaN.
        if (!(v > 0.0)) return std::unexpected(UnitError::NonPositiveGravity);
        return GravParam{v};
    }

    [[nodiscard]] constexpr f64 value() const noexcept { return q_.value(); }

    // The typed quantity, for arithmetic that should stay in the dimension
    // system. `value()` is the way out of it, and is what every call site in
    // orbit/ uses today.
    [[nodiscard]] constexpr Quantity quantity() const noexcept { return q_; }

    [[nodiscard]] friend constexpr auto operator<=>(GravParam l, GravParam r) noexcept {
        return l.value() <=> r.value();
    }
    friend bool operator==(GravParam, GravParam) = delete;

    [[nodiscard]] constexpr bool bitIdentical(GravParam other) const noexcept {
        return q_.bitIdentical(other.q_);
    }

private:
    explicit constexpr GravParam(f64 v) noexcept : q_{v} {}

    Quantity q_;
};

// Literals, checked at compile time (decision 113).
//
// `from()` is the door for a value that arrives at run time -- a scenario
// file, a fixture, the fuzzer -- and reports. These two are for a value
// written into the source, where a bad one should never reach a test run:
// consteval means the unwrap happens during constant evaluation, and an
// unwrap of a failed expected is not a constant expression, so `eccentricity(-0.5)`
// fails the build rather than throwing. kDeltaUt1Unmodelled shows the plain
// spelling of the same trick; these exist because this header has 56 call
// sites and an element table should stay readable.
[[nodiscard]] consteval Eccentricity eccentricity(f64 v) { return Eccentricity::from(v).value(); }
[[nodiscard]] consteval GravParam gravParam(f64 v) { return GravParam::from(v).value(); }

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

// A screen-space threshold is written often enough in the render-side maths to
// earn the suffix the other four have (M1-12).
[[nodiscard]] constexpr Pixels operator""_px(long double v) noexcept {
    return Pixels{static_cast<f64>(v)};
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

// --- what the deleted conversion operator refuses (decision 138) ------------
//
// Narrowed to arithmetic targets on 2026-09-24. These pin what it is *for*, so
// that narrowing it further, or losing it, is a build failure rather than a
// silent widening of what a dimensionless quantity will turn into. `Scalar<one>`
// is the ratio every division of like units produces -- `Metres / Metres` --
// and is the only dimensionless Scalar left in this header now that
// Eccentricity is a class and a pixel has a dimension.
static_assert(!std::is_constructible_v<f64, Scalar<mp_units::one>>,
              "a dimensionless quantity does not decay to a bare double");
static_assert(!std::is_constructible_v<f32, Scalar<mp_units::one>>, "nor to a float");
static_assert(!std::is_constructible_v<int, Scalar<mp_units::one>>,
              "nor to any other arithmetic type");
static_assert(!std::is_convertible_v<Scalar<mp_units::one>, f64>, "and not implicitly either");
// The legitimate conversion the narrowing had to keep: two units of one
// dimension, which the test support relies on for J/kg from m2/s2.
static_assert(
    std::is_constructible_v<SpecificEnergy,
                            decltype(Metres{1.0} * Metres{1.0} / (Seconds{1.0} * Seconds{1.0}))>,
    "a quantity still converts to another of the same dimension, explicitly");

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
static_assert(!addable<Irradiance, Metres>, "nor a flux density plus a length");

// --- the two validated scalars (M1-87) --------------------------------------
//
// What the classes buy, proved at compile time. The refusals are here as
// well as in the suite because these are the claims a call site depends on,
// and a static_assert runs on every build whether or not anyone runs ctest.
//
// **No NaN case here, deliberately.** MSVC's constant evaluator disagrees with
// its own runtime about NaN comparisons -- measured 2026-09-17, PROJECT_STATE
// section 8 -- so a static_assert about a NaN is a claim about the evaluator
// rather than about the value. The NotFinite refusals are tested at run time,
// where every front end agrees.
static_assert(Eccentricity{}.value() == 0.0, "a default eccentricity is a circle, not a surprise");
static_assert(!std::is_constructible_v<Eccentricity, f64>,
              "the only door is from(), so a bad value has nowhere to live");
static_assert(!std::is_default_constructible_v<GravParam>,
              "and a mu has no neutral value to default to");
static_assert(!std::is_constructible_v<GravParam, f64>, "same door for a mu");

static_assert(Eccentricity::from(0.0).has_value(), "a circle is a conic");
static_assert(Eccentricity::from(2.5).has_value(), "and so is a hyperbola, with no upper bound");
static_assert(!Eccentricity::from(-0.5).has_value(), "a negative eccentricity is not");
static_assert(Eccentricity::from(-0.5).error() == UnitError::NegativeEccentricity,
              "and it is refused by that name, not by a generic one");
static_assert(!GravParam::from(0.0).has_value(), "a massless central body is not one");
static_assert(!GravParam::from(-1.0).has_value(), "nor a repulsive one");
static_assert(GravParam::from(-1.0).error() == UnitError::NonPositiveGravity);

// The literal helpers, and the thing they are for: a bad literal is a build
// failure. `eccentricity(-0.5)` does not compile -- written once, watched
// failing, and then removed, as this project does with every negative
// assertion it cannot leave in the source.
static_assert(eccentricity(0.7306).value() == 0.7306);
static_assert(gravParam(3.986004418e14).value() == 3.986004418e14);

// **The reason GravParam holds a Scalar rather than an f64** (decision 110),
// and the one assertion M1-62 depends on: mu still takes part in the unit
// algebra, so an acceleration comes out of the dimension system rather than
// out of a comment. This is ADR 0019's `mu / (r*r)` clause, kept.
static_assert(nearlyEqual((gravParam(3.986004418e14).quantity() / (Metres{7.0e6} * Metres{7.0e6}))
                              .numerical_value_in(units::kMetre / mp_units::pow<2>(units::kSecond)),
                          3.986004418e14 / (7.0e6 * 7.0e6),
                          Tolerance{0.0}),
              "mu over a squared length is an acceleration, and the type system knows it");

// Irradiance is a power over an area, and the algebra knows it. The second of
// these is what stops the inverse-square law in astro/Sun.hpp being written
// with the ratio the wrong way up: an irradiance times an area is a power, and
// the unit comes out of the library rather than out of a comment.
static_assert(!std::is_constructible_v<Irradiance, Metres>, "nor a length a flux density");
static_assert(nearlyEqual((Irradiance{1361.0} * (Metres{2.0} * Metres{3.0}))
                              .numerical_value_in(mp_units::si::watt),
                          8166.0,
                          Tolerance{0.0}),
              "an irradiance over an area is a power");

// The photometric conversion, which is the one line of the radiometric chain
// that ADR 0014 calls "the smallest and most easily lost". A radiance times a
// luminous efficacy is a luminance -- an efficacy is cd sr / W, so the watt
// and the steradian cancel -- and the number is the plain product, with no
// hidden factor from the library.
static_assert(nearlyEqual(Luminance{Radiance{2.0} * LuminousEfficacy{100.0}}.value(),
                          200.0,
                          Tolerance{0.0}),
              "a radiance times a luminous efficacy is a luminance");
static_assert(!std::is_constructible_v<Luminance, Radiance>,
              "and a radiance is not a luminance until an efficacy says how much of it "
              "the eye sees");
static_assert(!std::is_constructible_v<Radiance, Irradiance>,
              "nor is an irradiance a radiance: the solid angle is the difference");
static_assert(!addable<Radiance, Luminance>, "a radiometric and a photometric quantity");

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

// --- pixels (M1-12) ---------------------------------------------------------
//
// What the kind buys, stated as the claims a call site depends on. The first
// two are the ones that matter: a pixel is dimensionless, so without a kind of
// its own every other dimensionless ratio in this header would convert into it.
// **The claim the dimension makes**, and it needs a dimensionless quantity to
// make it against -- which is worth spelling out, because the obvious assertion
// is vacuous. Eccentricity was this header's other dimensionless quantity until
// 2026-09-22 and is a plain class since, so asserting that a class is not
// constructible from a quantity holds whether or not a pixel is separate from
// anything. The ratio of two lengths is the real test, and these are the
// assertions that fail if the dimension is taken away.
//
// **Both of these were measured on all three front ends before being written**
// (2026-09-23), because the pair they replaced was not: with the earlier *kind*
// the second of them held under clang and MSVC and failed under gcc, and the
// agreement was an accident of `Scalar`'s deleted conversion operator rather
// than anything a kind promised. See `units::kPixelDimension` above.
static_assert(!std::is_same_v<Pixels, Scalar<mp_units::one>>,
              "a pixel is not interchangeable with a plain dimensionless ratio");
static_assert(!std::is_constructible_v<Pixels, decltype(Metres{2.0} / Metres{1.0})>,
              "and a ratio of two lengths is not a number of pixels, "
              "not even when somebody writes the braces");
static_assert(!std::is_convertible_v<decltype(Metres{2.0} / Metres{1.0}), Pixels>,
              "nor does it become one on its own");
static_assert(!Pixels::kBaseConvertsToANumber,
              "and a pixel is outside the dimensionless-conversion machinery entirely, "
              "which is what the dimension buys over a kind");
static_assert(Scalar<mp_units::one>::kBaseConvertsToANumber,
              "-- the control, without which the assertion above could pass for a "
              "trait that had simply stopped being true of anything");

// The unit algebra a validated class would have given up, and the reason the
// dimension was preferred: this is M1-50's screen-space error formula, checked
// by the type system rather than by a comment. A focal length in pixels, over a
// distance in metres, times a geometric error in metres, is a number of pixels.
static_assert(std::is_same_v<decltype(Pixels{1000.0} / Metres{1.0} * Metres{1.0}), Pixels>,
              "px/m times m is px, and the dimension system knows it");
static_assert(nearlyEqual((Pixels{6.0} / Pixels{3.0}).value(), 2.0, Tolerance{0.0}),
              "and the ratio of two screen lengths is an ordinary number again");

static_assert(!std::is_constructible_v<Pixels, Eccentricity>,
              "a pixel measurement must never be usable as an eccentricity");
static_assert(!std::is_constructible_v<Eccentricity, Pixels>, "nor the other way round");
static_assert(!std::is_constructible_v<Pixels, Radians>, "nor an angle a length on the screen");
static_assert(!std::is_constructible_v<Radians, Pixels>);
static_assert(!std::is_convertible_v<f64, Pixels>, "construction must be explicit");
static_assert(!std::is_convertible_v<Pixels, f64>, "and there is no silent way back");
static_assert(!addable<Pixels, Radians>, "a screen length plus an angle must not compile");
static_assert(!addable<Pixels, Eccentricity>, "nor a screen length plus a ratio");
static_assert(!equatable<Pixels, Pixels>, "exact equality of a double is spelled out");
static_assert(sizeof(Pixels) == sizeof(f64));
static_assert(std::is_trivially_copyable_v<Pixels>);
// Named rather than written inline, for the reason kOneKilometre gives above:
// `2.5_px.value()` lexes as one pp-number and does not compile.
inline constexpr Pixels kTwoAndAHalfPixels = 2.5_px;
static_assert(nearlyEqual(kTwoAndAHalfPixels.value(), 2.5, Tolerance{0.0}));
static_assert(nearlyEqual((Pixels{2.0} + Pixels{0.5}).value(), 2.5, Tolerance{0.0}),
              "and the arithmetic is the plain arithmetic of the number it carries");

} // namespace orb

#endif // ORBSIM_CORE_UNITS_HPP

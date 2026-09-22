//
// Tests for the two validated scalars in core/Units.hpp: Eccentricity and
// GravParam (M1-87, register decisions 106-114).
//
// **This suite holds what a static_assert cannot.** Most of what these types
// buy is a compile-time refusal -- `Eccentricity{-0.5}` does not compile, and
// neither does `eccentricity(-0.5)` -- and those live as assertions beside the
// types, each written once and watched failing rather than assumed. Two things
// are left over for run time.
//
// The first is **NaN and infinity**. MSVC's constant evaluator disagrees with
// its own runtime about NaN comparisons -- measured 2026-09-17, and recorded in
// PROJECT_STATE.md section 8 -- so a static_assert about a NaN is a claim about
// the evaluator rather than about the value. Every non-finite case is therefore
// here, where all four front ends agree.
//
// The second is **the refusals that used to live elsewhere**. Until 2026-09-22
// a non-positive mu was reported by three functions in orbit/ and asserted by
// two, and four assertions in test_orbit.cpp and test_orbit_scales.cpp checked
// the reports. Those cannot be written now, because the value cannot be built.
// The cases they were are here instead, one level down, against the factory
// that refuses -- which is the same coverage against the thing that now does
// the work.
//
// **Every refusal asserts `!has_value()` before it reads `error()`**, and that
// is not belt and braces. `std::expected::error()` on an expected that holds a
// value is undefined behaviour: in practice it reads the storage and compares
// equal to NotFinite, the zero enumerator, so a case written without the guard
// **passes while the factory accepts the value it was supposed to refuse**.
// The mutation pass of 2026-09-22 found exactly that -- the guard had been
// dropped while splitting these cases to clear a cognitive-complexity finding,
// and a fix for a lint finding had quietly removed the thing the test was for.
//
// **One claim per case, and one loop per case.** Catch2's REQUIRE expands to a
// branch, so a case with three loops of two assertions scores past
// readability-function-cognitive-complexity on the generated function. Splitting
// is the fix rather than a suppression, and it reads better besides: a failure
// names the claim that broke.
//
#include "core/Scalar.hpp"
#include "core/Units.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mp-units/framework.h>

#include <array>
#include <limits>

namespace {

using orb::Eccentricity;
using orb::f64;
using orb::GravParam;
using orb::nearlyEqual;
using orb::Tolerance;
using orb::UnitError;

constexpr f64 kNaN = std::numeric_limits<f64>::quiet_NaN();
constexpr f64 kInf = std::numeric_limits<f64>::infinity();
constexpr f64 kTiniest = std::numeric_limits<f64>::denorm_min();

// A circle, an ellipse, a parabola, a hyperbola, and one far beyond anything
// physical. There is deliberately no upper bound: a hyperbolic eccentricity is
// unbounded in principle, and a cap would be a threshold carrying a hidden
// scale (decision 107).
constexpr auto kAcceptedEccentricities =
    std::to_array<f64>({0.0, kTiniest, 1e-300, 0.5, 1.0, 1.5, 100.0, 1e300});

// The bound is on the sign, not on a magnitude, so the smallest negative double
// belongs here as much as the largest.
constexpr auto kNegatives = std::to_array<f64>({-kTiniest, -1e-300, -0.5, -1.0, -1e300});

constexpr auto kNonFinite = std::to_array<f64>({kNaN, kInf, -kInf});

} // namespace

TEST_CASE("an eccentricity accepts every conic, with no upper bound", "[units][eccentricity]") {
    for (const f64 accepted : kAcceptedEccentricities) {
        INFO("eccentricity " << accepted);
        const auto ecc = Eccentricity::from(accepted);
        REQUIRE(ecc.has_value());
    }
}

TEST_CASE("a negative eccentricity is refused by its own name", "[units][eccentricity]") {
    for (const f64 negative : kNegatives) {
        INFO("eccentricity " << negative);
        const auto ecc = Eccentricity::from(negative);
        REQUIRE(!ecc.has_value());
        REQUIRE(ecc.error() == UnitError::NegativeEccentricity);
    }
}

TEST_CASE("a non-finite eccentricity is refused by a different name", "[units][eccentricity]") {
    // NotFinite rather than the sign refusal, so a caller is told which thing
    // is wrong. Not a static_assert: see the header.
    for (const f64 value : kNonFinite) {
        INFO("eccentricity " << value);
        const auto ecc = Eccentricity::from(value);
        REQUIRE(!ecc.has_value());
        REQUIRE(ecc.error() == UnitError::NotFinite);
    }
}

TEST_CASE("negative zero is an eccentricity, because it is zero", "[units][eccentricity]") {
    // The one input where the sign bit and the value disagree: -0.0 < 0.0 is
    // false, and what it holds is zero, which is a circle.
    const auto ecc = Eccentricity::from(-0.0);
    REQUIRE(ecc.has_value());
    REQUIRE(nearlyEqual(ecc->value(), 0.0, Tolerance{0.0}));
}

TEST_CASE("a default eccentricity is a circle", "[units][eccentricity]") {
    // Elements holds one as an aggregate member, so this is the value an
    // element set starts with, and it has to be a real one rather than
    // whatever was on the stack.
    REQUIRE(nearlyEqual(Eccentricity{}.value(), 0.0, Tolerance{0.0}));
}

TEST_CASE("a gravitational parameter accepts any positive value", "[units][gravparam]") {
    for (const f64 accepted : std::to_array<f64>({kTiniest, 1e-300, 3.986004418e14, 1e300})) {
        INFO("mu " << accepted);
        const auto mu = GravParam::from(accepted);
        REQUIRE(mu.has_value());
    }
}

TEST_CASE("a non-positive gravitational parameter is refused by name", "[units][gravparam]") {
    // Both zeros are refused. This is the case that used to read
    // `propagate(leo, GravParam{0.0})` in test_orbit.cpp: a massless central
    // body is not a central body.
    for (const f64 refused : std::to_array<f64>({0.0, -0.0, -1e-300, -1.0, -1e300})) {
        INFO("mu " << refused);
        const auto mu = GravParam::from(refused);
        REQUIRE(!mu.has_value());
        REQUIRE(mu.error() == UnitError::NonPositiveGravity);
    }
}

TEST_CASE("a non-finite gravitational parameter is refused by a different name",
          "[units][gravparam]") {
    // The case that used to read `propagate(leo, GravParam{kNaN})` in
    // test_orbit_scales.cpp, moved down to the factory that now refuses it.
    for (const f64 value : kNonFinite) {
        INFO("mu " << value);
        const auto mu = GravParam::from(value);
        REQUIRE(!mu.has_value());
        REQUIRE(mu.error() == UnitError::NotFinite);
    }
}

TEST_CASE("a gravitational parameter keeps its place in the dimension system",
          "[units][gravparam]") {
    // **The reason GravParam holds a Scalar rather than an f64** (decision
    // 110). ADR 0019 says `mu / (r*r)` produces an acceleration type, and
    // M1-62's force model is written against that; validating the value was
    // not allowed to cost it. The static_assert beside the type proves the
    // unit; this proves the number, at a scale that assertion does not use.
    const auto mu = GravParam::from(3.986004418e14);
    REQUIRE(mu.has_value());

    const orb::Metres radius{6.778e6}; // a 400 km orbit
    const auto acceleration = mu->quantity() / (radius * radius);

    REQUIRE(nearlyEqual(
        acceleration.numerical_value_in(orb::units::kMetre / mp_units::pow<2>(orb::units::kSecond)),
        3.986004418e14 / (6.778e6 * 6.778e6),
        Tolerance{0.0}));
}

TEST_CASE("an accepted eccentricity survives the factory bit for bit", "[units]") {
    // Bit identity, not nearness: the factory validates and stores, and must
    // not round, scale or otherwise touch what it was given. A value that came
    // back one ulp different would pass every tolerance in this file.
    for (const f64 value : kAcceptedEccentricities) {
        INFO("value " << value);
        REQUIRE(orb::bitsOf(Eccentricity::from(value).value().value()) == orb::bitsOf(value));
    }
}

TEST_CASE("an accepted gravitational parameter survives the factory bit for bit", "[units]") {
    for (const f64 value : std::to_array<f64>({kTiniest, 1e-300, 3.986004418e14, 1e300})) {
        INFO("value " << value);
        REQUIRE(orb::bitsOf(GravParam::from(value).value().value()) == orb::bitsOf(value));
    }
}

TEST_CASE("every unit error describes itself", "[units]") {
    constexpr auto kAll = std::to_array<UnitError>(
        {UnitError::NotFinite, UnitError::NegativeEccentricity, UnitError::NonPositiveGravity});
    for (const UnitError error : kAll) {
        REQUIRE(!describe(error).empty());
    }
}

TEST_CASE("and no two unit errors describe the same thing", "[units]") {
    // The half that catches a value answered by copying its neighbour, which
    // is the usual way a describe() rots.
    REQUIRE(describe(UnitError::NotFinite) != describe(UnitError::NegativeEccentricity));
    REQUIRE(describe(UnitError::NegativeEccentricity) != describe(UnitError::NonPositiveGravity));
    REQUIRE(describe(UnitError::NotFinite) != describe(UnitError::NonPositiveGravity));
}

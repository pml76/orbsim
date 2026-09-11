#ifndef ORBSIM_TESTS_ORBITTESTSUPPORT_HPP
#define ORBSIM_TESTS_ORBITTESTSUPPORT_HPP
//
// Fixtures shared by the orbit suites: the central bodies they fly around, an
// element builder that takes degrees so the cases read like a textbook, and
// the two matchers whose notion of "close enough" is this project's own rather
// than Catch2's.
//
// Why the matchers live here and not at their call sites: relative comparison
// of a vector by the length of its error is a definition this project made,
// and a definition belongs beside the other fixtures rather than repeated in
// two suites.
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "orbit/Orbit.hpp"

#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <algorithm>
#include <cmath>
#include <expected>
#include <format>
#include <string>
#include <string_view>

namespace orb::test {

// Gravitational parameters, m^3/s^2. Earth is WGS-84 / EGM-96; the others are
// the IAU 2015 nominal values as tabulated in JPL's DE440 documentation.
inline constexpr GravParam kMuMoon{4.9028001e12};
inline constexpr GravParam kMuEarth{3.986004418e14};
inline constexpr GravParam kMuJupiter{1.26686534e17};
inline constexpr GravParam kMuSun{1.32712440018e20};

// WGS-84 equatorial radius.
inline constexpr Metres kEarthRadius{6378137.0};

// The floor under every relative comparison below. A relative error needs a
// denominator, and a denominator that can be zero needs a floor; 1e-30 is far
// below any quantity this simulator carries in metres, seconds or m^3/s^2, so
// it only ever takes effect when the reference value is exactly zero -- where
// it turns the test into "must also be zero", which is the intended meaning.
inline constexpr f64 kRelativeScaleFloor = 1e-30;

[[nodiscard]] inline Elements
makeElements(Metres sma, Eccentricity ecc, Degrees inc, Degrees lan, Degrees aop, Degrees tra) {
    return Elements{
        .sma = sma,
        .ecc = ecc,
        .inc = toRadians(inc),
        .lan = toRadians(lan),
        .aop = toRadians(aop),
        .tra = toRadians(tra),
        .slr = Metres{sma.value * (1.0 - (ecc.value * ecc.value))},
    };
}

// A circular orbit of radius r about mu, starting on the x-axis: the one case
// with a closed-form answer at every time, which makes it the right probe for
// a propagator at an unfamiliar scale.
[[nodiscard]] inline StateVector circularState(GravParam mu, Metres r) {
    return StateVector{
        .pos = {r.value, 0.0, 0.0},
        .vel = {0.0, std::sqrt(mu.value / r.value), 0.0},
    };
}

// Specific orbital energy and angular momentum from a state: the two constants
// of two-body motion, computed independently of anything in Orbit.cpp so that
// a propagator can be checked against physics rather than against itself.
[[nodiscard]] inline SpecificEnergy specificEnergy(const StateVector& sv, GravParam mu) {
    return SpecificEnergy{(0.5 * lengthSq(sv.vel)) - (mu.value / length(sv.pos))};
}

[[nodiscard]] inline Vec3 specificAngularMomentum(const StateVector& sv) {
    return cross(sv.pos, sv.vel);
}

// Absolute comparison of a scalar, against a tolerance that is a type.
//
// Catch2's own WithinAbs takes two adjacent bare doubles, and transposing them
// does not fail loudly: WithinAbs(5.0, 5554.0) accepts 5554 as readily as
// WithinAbs(5554.0, 5.0) does, because |5554 - 5| <= 5554. That is I.24 and
// non-negotiable 1 exactly -- and it is the defect this project already shipped
// once, which is why Tolerance exists at all. Taking the tolerance as its own
// type means the transposed call does not compile.
//
// The predicate is nearlyEqual, not Catch2's interval test, so that a
// comparison of two infinities fails here as it did under the old harness:
// inf - inf is NaN, and NaN <= tol is false.
class WithinAbsOf : public Catch::Matchers::MatcherBase<f64> {
public:
    WithinAbsOf(f64 want, Tolerance tol) noexcept : want_{want}, tol_{tol} {}

    [[nodiscard]] bool match(const f64& got) const override {
        return nearlyEqual(got, want_, tol_) && !std::isnan(got);
    }

protected:
    // Protected, matching MatcherUntypedBase: Catch2 reaches this through the
    // public toString(), and widening the visibility of an override is what
    // misc-override-with-different-visibility exists to catch.
    //
    // Defined in OrbitTestSupport.cpp, as each matcher's describe() is: the
    // first virtual defined out of line is the class's key function, and its
    // vtable is then emitted there, once, instead of in every suite that
    // includes this header -- which -Wweak-vtables reports (ADR 0017).
    //
    // gcc's -Wabi-tag reports that it gave a function returning std::string
    // libstdc++'s "cxx11" ABI tag -- which gcc does by itself, and which
    // matters to a library whose exported names must survive the older string
    // ABI. The string here is Catch2's: describe() and StringMaker::convert
    // return std::string because Catch2 declares them so. The warning is off
    // at each such site, and for gcc alone, because clang has no warning of
    // that name and rejects one it does not know (ADR 0017).
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif
    [[nodiscard]] std::string describe() const override;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

private:
    f64 want_;
    Tolerance tol_;
};

// Relative comparison of a scalar, for quantities whose magnitude spans many
// orders (radii in metres, speeds in m/s) where an absolute tolerance is
// meaningless.
//
// This is deliberately NOT Catch2's WithinRel, which divides by
// max(|got|, |want|) where this divides by |want| alone. The difference is
// below f64 resolution -- max exceeds |want| by at most a factor (1 + tol),
// so the two admissible sets differ by tol^2 relative, which is 1e-18 at the
// tightest tolerance used here -- but it is a difference, and the task that
// introduced Catch2 (M1-01) was required to change no assertion. Reproducing
// the predicate exactly is cheaper than re-deriving that argument later.
class WithinRelTo : public Catch::Matchers::MatcherBase<f64> {
public:
    WithinRelTo(f64 want, Tolerance relTol) noexcept : want_{want}, relTol_{relTol} {}

    [[nodiscard]] bool match(const f64& got) const override {
        const f64 scale = std::max(std::abs(want_), kRelativeScaleFloor);
        return nearlyEqual(got / scale, want_ / scale, relTol_) && !std::isnan(got);
    }

protected:
    // Protected, out of line, and exempt from gcc's -Wabi-tag, for the
    // reasons given on WithinAbsOf.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif
    [[nodiscard]] std::string describe() const override;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

private:
    f64 want_;
    Tolerance relTol_;
};

// Relative comparison of a vector by the length of its error, which is the
// only formulation that treats the three components as one quantity: a
// position is wrong by a distance, not by three independent numbers.
class WithinRelVec : public Catch::Matchers::MatcherBase<Vec3> {
public:
    WithinRelVec(const Vec3& want, Tolerance relTol) noexcept : want_{want}, relTol_{relTol} {}

    [[nodiscard]] bool match(const Vec3& got) const override {
        const f64 scale = std::max(length(want_), kRelativeScaleFloor);
        return length(got - want_) / scale <= relTol_.value;
    }

protected:
    // Protected, out of line, and exempt from gcc's -Wabi-tag, for the
    // reasons given on WithinAbsOf.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif
    [[nodiscard]] std::string describe() const override;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

private:
    Vec3 want_;
    Tolerance relTol_;
};

// The named error behind a failed call, for an INFO line that says which
// failure happened rather than that one did. `describe` is found by
// argument-dependent lookup on the error type, exactly as the old harness's
// expectOk found it.
//
// The has_value() guard is not defensive coding: std::expected::error() has
// the precondition that the expected holds no value, so reading it on a
// success is undefined behaviour and asserts in the Debug tree. INFO evaluates
// its argument eagerly, on every call, whether or not the assertion that
// follows it fails -- so the guard is on the hot path, not the failure path.
template <typename T, typename Error>
[[nodiscard]] constexpr std::string_view errorName(const std::expected<T, Error>& result) noexcept {
    return result.has_value() ? std::string_view{"(succeeded)"} : describe(result.error());
}

} // namespace orb::test

// Catch2 prints an unknown type as "{?}", which would make every vector
// failure unreadable. 17 significant digits is a round-trippable f64, so a
// failure can be pasted back into a test as an exact reproduction. Exempt from
// gcc's -Wabi-tag, for the reason given on WithinAbsOf::describe().
namespace Catch {

template <> struct StringMaker<orb::Vec3> {
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif
    [[nodiscard]] static std::string convert(const orb::Vec3& value) {
        return std::format("({:.17g}, {:.17g}, {:.17g})", value.x, value.y, value.z);
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
};

} // namespace Catch

#endif // ORBSIM_TESTS_ORBITTESTSUPPORT_HPP

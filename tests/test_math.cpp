//
// Tests for core/Math.hpp's rotation conversion: a 3x3 matrix to the unit
// quaternion the rest of the project rotates with (M1-07, register decision
// 78).
//
// **This suite exists because the Earth cannot exercise the code.** The
// conversion picks whichever of the four components is largest and builds the
// other three from it, which is what keeps it accurate near a half turn; but
// the celestial-to-terrestrial matrices reach only two of those four branches,
// measured at 200,346 and 199,654 of 400,000 epochs, with none taking the x or
// y branch. Synthetic rotations reach all four.
//
// Nothing here is checked against the conversion. The matrices come from the
// **other** formulation -- the quaternion-to-matrix identities, written out
// below -- so a sign error in one is not a sign error in the other, and the
// expected values for the identity and the three half turns are read off by
// hand.
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "tests/OrbitTestSupport.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>
#include <string_view>

using namespace orb;
using namespace orb::test;

namespace {

// A seeded sweep, with the seed written down (VERIFICATION.md rule 12).
constexpr std::uint64_t kSweepSeed = 20260920ULL; // the date this suite was written
constexpr std::size_t kSweepCases = 20'000;

// What the conversion must hold to, and where each number comes from
// (register decision 80).
//
// The norm: one rounding of a square root and three divisions, measured at one
// ulp of 1 over 450,000 of ERFA's matrices and two over as many synthetic ones.
constexpr Tolerance kUnitQuaternion{1e-15};
// Reproducing the matrix: the input's own departure from orthonormal, plus
// rotate()'s arithmetic. The matrices here are built from a quaternion in
// double and are orthonormal to about 3e-15, where ERFA's are 8.9e-16 -- so
// this suite's bound is 4e-15 and tests/test_earth_orientation.cpp holds
// ERFA's matrices to 2e-15. Measured here: 8 ulp, 1.8e-15.
constexpr Tolerance kReproducesMatrix{4e-15};

// The matrix of a unit quaternion, by the standard identities -- the
// formulation the conversion under test is *not*. Rows, row-major, as
// RotationMatrix holds them.
[[nodiscard]] RotationMatrix matrixOf(const Quat& q) {
    const f64 xx = q.x * q.x;
    const f64 yy = q.y * q.y;
    const f64 zz = q.z * q.z;
    const f64 xy = q.x * q.y;
    const f64 xz = q.x * q.z;
    const f64 yz = q.y * q.z;
    const f64 wx = q.w * q.x;
    const f64 wy = q.w * q.y;
    const f64 wz = q.w * q.z;
    RotationMatrix m{};
    // Two sets of braces: the inner one initialises std::array's own array
    // member, which MSVC's /Wall asks for by name (C5246) and the other two
    // compilers accept either way.
    m.rows.at(0) = {{1 - (2 * (yy + zz)), 2 * (xy - wz), 2 * (xz + wy)}};
    m.rows.at(1) = {{2 * (xy + wz), 1 - (2 * (xx + zz)), 2 * (yz - wx)}};
    m.rows.at(2) = {{2 * (xz - wy), 2 * (yz + wx), 1 - (2 * (xx + yy))}};
    return m;
}

// Which of the four components the conversion must pick: the largest of the
// trace and the three diagonal elements. Written here so the sweep can check
// that every branch was reached rather than assume it.
enum class Branch : std::uint8_t { W, X, Y, Z };

[[nodiscard]] Branch branchOf(const RotationMatrix& m) {
    const f64 xx = m.rows.at(0).at(0);
    const f64 yy = m.rows.at(1).at(1);
    const f64 zz = m.rows.at(2).at(2);
    const f64 trace = xx + yy + zz;
    if (trace >= xx && trace >= yy && trace >= zz) return Branch::W;
    if (xx >= yy && xx >= zz) return Branch::X;
    return yy >= zz ? Branch::Y : Branch::Z;
}

// The rotation a quaternion performs, applied to the three axes, against the
// matrix's own columns: the same claim as "q rotates the way m does", element
// by element.
[[nodiscard]] f64 worstElement(const Quat& q, const RotationMatrix& m) {
    f64 worst = 0.0;
    for (std::size_t j = 0; j < 3; ++j) {
        const auto axes = std::to_array<Direction>({
            Direction{1, 0, 0},
            Direction{0, 1, 0},
            Direction{0, 0, 1},
        });
        const Direction turned = q.rotate(axes.at(j));
        const std::array<f64, 3> got{{turned.x.value(), turned.y.value(), turned.z.value()}};
        for (std::size_t i = 0; i < 3; ++i) {
            worst = std::max(worst, std::abs(got.at(i) - m.rows.at(i).at(j)));
        }
    }
    return worst;
}

[[nodiscard]] f64 normOf(const Quat& q) {
    return std::sqrt((q.w * q.w) + (q.x * q.x) + (q.y * q.y) + (q.z * q.z));
}

[[nodiscard]] f64 largestComponent(const Quat& q) {
    const std::array<f64, 4> parts{{q.w, q.x, q.y, q.z}};
    f64 largest = parts.at(0);
    for (const f64 part : parts) {
        if (std::abs(part) > std::abs(largest)) largest = part;
    }
    return largest;
}

// The engine, read directly rather than through a std:: distribution, for the
// reason tests/test_time.cpp gives: the distributions' algorithms differ
// between standard libraries, and a sweep built on them would test different
// rotations under each toolchain.
class Sampler {
public:
    // A double in [0, 1), from the engine's top 53 bits.
    [[nodiscard]] f64 unit() { return static_cast<f64>(rng_() >> 11U) * 0x1p-53; }

    // A unit quaternion, uniform over rotations (Shoemake's method): three
    // uniforms, and the quaternion whose components are the square roots of a
    // partition of one. Uniformity is not the claim here -- reaching every
    // branch is -- but it costs nothing and it spreads the cases.
    [[nodiscard]] Quat rotation() {
        const f64 u1 = unit();
        const f64 u2 = unit();
        const f64 u3 = unit();
        const f64 r1 = std::sqrt(1.0 - u1);
        const f64 r2 = std::sqrt(u1);
        constexpr f64 kTwoPi = 2.0 * std::numbers::pi_v<f64>;
        return normalize(Quat{r2 * std::cos(kTwoPi * u3),
                              r1 * std::sin(kTwoPi * u2),
                              r1 * std::cos(kTwoPi * u2),
                              r2 * std::sin(kTwoPi * u3)});
    }

    // A rotation of `angle` about a random axis.
    [[nodiscard]] Quat aboutRandomAxis(Radians angle) {
        const f64 z = (2.0 * unit()) - 1.0;
        const f64 azimuth = 2.0 * std::numbers::pi_v<f64> * unit();
        const f64 r = std::sqrt(std::max(0.0, 1.0 - (z * z)));
        return Quat::fromAxisAngle(Direction{r * std::cos(azimuth), r * std::sin(azimuth), z},
                                   angle);
    }

private:
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
    std::mt19937_64 rng_{kSweepSeed};
};

} // namespace

// The four matrices whose quaternions are exactly a component of one: the
// identity, and a half turn about each axis. Read off by hand, not computed.
// Catch2 macro expansion, not written complexity.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the identity and the three half turns convert exactly", "[math][quaternion]") {
    struct Case {
        std::string_view name;
        RotationMatrix matrix;
        Quat expected;
    };
    const auto cases = std::to_array<Case>({
        {
            .name = "the identity",
            .matrix = RotationMatrix{.rows = {{{{1, 0, 0}}, {{0, 1, 0}}, {{0, 0, 1}}}}},
            .expected = Quat{1, 0, 0, 0},
        },
        {
            .name = "a half turn about x",
            .matrix = RotationMatrix{.rows = {{{{1, 0, 0}}, {{0, -1, 0}}, {{0, 0, -1}}}}},
            .expected = Quat{0, 1, 0, 0},
        },
        {
            .name = "a half turn about y",
            .matrix = RotationMatrix{.rows = {{{{-1, 0, 0}}, {{0, 1, 0}}, {{0, 0, -1}}}}},
            .expected = Quat{0, 0, 1, 0},
        },
        {
            .name = "a half turn about z",
            .matrix = RotationMatrix{.rows = {{{{-1, 0, 0}}, {{0, -1, 0}}, {{0, 0, 1}}}}},
            .expected = Quat{0, 0, 0, 1},
        },
    });
    for (const Case& c : cases) {
        CAPTURE(c.name);
        const Quat q = quaternionFrom(c.matrix);
        REQUIRE_THAT(q.w, WithinAbsOf(c.expected.w, Tolerance{0.0}));
        REQUIRE_THAT(q.x, WithinAbsOf(c.expected.x, Tolerance{0.0}));
        REQUIRE_THAT(q.y, WithinAbsOf(c.expected.y, Tolerance{0.0}));
        REQUIRE_THAT(q.z, WithinAbsOf(c.expected.z, Tolerance{0.0}));
    }
}

// Over a seeded sweep of rotations: every branch is reached, the quaternion is
// a unit one, and it rotates the way the matrix it came from does.
// Catch2 macro expansion, not written complexity.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("every branch is reached and each reproduces its matrix", "[math][quaternion]") {
    Sampler sampler;
    std::array<int, 4> taken{};
    f64 worstNorm = 0.0;
    f64 worstElementError = 0.0;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        const Quat original = sampler.rotation();
        const RotationMatrix m = matrixOf(original);
        CAPTURE(kSweepSeed, i, original.w, original.x, original.y, original.z);
        REQUIRE(isRotation(m, kRotationTolerance));

        const Quat q = quaternionFrom(m);
        taken.at(static_cast<std::size_t>(branchOf(m))) += 1;
        worstNorm = std::max(worstNorm, std::abs(normOf(q) - 1.0));
        worstElementError = std::max(worstElementError, worstElement(q, m));
        REQUIRE_THAT(normOf(q), WithinAbsOf(1.0, kUnitQuaternion));
        REQUIRE_THAT(worstElement(q, m), WithinAbsOf(0.0, kReproducesMatrix));
        // q and -q are one rotation; this one is the representative whose
        // largest component is positive.
        REQUIRE(largestComponent(q) > 0.0);
    }
    INFO("worst |q| - 1 " << worstNorm << ", worst element " << worstElementError);
    for (std::size_t branch = 0; branch < taken.size(); ++branch) {
        CAPTURE(branch, taken);
        REQUIRE(taken.at(branch) > 0);
    }
}

// Near a half turn, where the scalar part vanishes and the branch that divides
// by it loses everything. This is what "largest component first" buys, and the
// test says so by computing the other way too.
// Catch2 macro expansion, not written complexity.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("near a half turn the conversion keeps what the trace alone would lose",
          "[math][quaternion]") {
    Sampler sampler;
    f64 worstOurs = 0.0;
    f64 worstTraceOnly = 0.0;
    for (std::size_t i = 0; i < 2'000; ++i) {
        const Radians angle{std::numbers::pi_v<f64> - (1e-7 * sampler.unit())};
        const Quat original = sampler.aboutRandomAxis(angle);
        const RotationMatrix m = matrixOf(original);
        CAPTURE(kSweepSeed, i, angle.value());

        const Quat q = quaternionFrom(m);
        worstOurs = std::max(worstOurs, worstElement(q, m));
        REQUIRE_THAT(normOf(q), WithinAbsOf(1.0, kUnitQuaternion));
        REQUIRE_THAT(worstElement(q, m), WithinAbsOf(0.0, kReproducesMatrix));

        // The scalar-part-only conversion, which is what a reader reaches for
        // first: w from the trace, and the vector part from the off-diagonal
        // differences divided by it.
        const f64 trace = m.rows.at(0).at(0) + m.rows.at(1).at(1) + m.rows.at(2).at(2);
        const f64 w = 0.5 * std::sqrt(std::max(0.0, 1.0 + trace));
        if (w <= 0.0) continue;
        const Quat naive{w,
                         (m.rows.at(2).at(1) - m.rows.at(1).at(2)) / (4 * w),
                         (m.rows.at(0).at(2) - m.rows.at(2).at(0)) / (4 * w),
                         (m.rows.at(1).at(0) - m.rows.at(0).at(1)) / (4 * w)};
        worstTraceOnly = std::max(worstTraceOnly, worstElement(naive, m));
    }
    INFO("worst element error: ours " << worstOurs << ", trace only " << worstTraceOnly);
    REQUIRE(worstOurs < kReproducesMatrix.value());
    // The point of the branch, as a number: the other way is off by something
    // a renderer would see, not by a rounding.
    REQUIRE(worstTraceOnly > 1e-6);
}

// The precondition, which quaternionFrom asserts and therefore cannot be
// tested through it: what rounding produces is a rotation, and what never was
// one is not. A transposed rotation is still a rotation -- this check cannot
// see a transposition, and the reference test is what does.
// Catch2 macro expansion, not written complexity.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("isRotation accepts rounding and refuses what was never a rotation",
          "[math][quaternion]") {
    const RotationMatrix identity{.rows = {{{{1, 0, 0}}, {{0, 1, 0}}, {{0, 0, 1}}}}};
    REQUIRE(isRotation(identity, kRotationTolerance));

    RotationMatrix nudged = identity;
    nudged.rows.at(0).at(1) = 1e-13;
    REQUIRE(isRotation(nudged, kRotationTolerance));
    nudged.rows.at(0).at(1) = 1e-11;
    REQUIRE(!isRotation(nudged, kRotationTolerance));

    const RotationMatrix scaled{.rows = {{{{2, 0, 0}}, {{0, 2, 0}}, {{0, 0, 2}}}}};
    REQUIRE(!isRotation(scaled, kRotationTolerance));

    // Orthonormal, determinant -1: a reflection, which no quaternion performs.
    const RotationMatrix reflection{.rows = {{{{1, 0, 0}}, {{0, 1, 0}}, {{0, 0, -1}}}}};
    REQUIRE(!isRotation(reflection, kRotationTolerance));

    const RotationMatrix zero{};
    REQUIRE(!isRotation(zero, kRotationTolerance));

    Sampler sampler;
    for (std::size_t i = 0; i < 100; ++i) {
        const RotationMatrix m = matrixOf(sampler.rotation());
        CAPTURE(kSweepSeed, i);
        REQUIRE(isRotation(m, kRotationTolerance));
        RotationMatrix transposed{};
        for (std::size_t r = 0; r < 3; ++r) {
            for (std::size_t c = 0; c < 3; ++c) {
                transposed.rows.at(r).at(c) = m.rows.at(c).at(r);
            }
        }
        REQUIRE(isRotation(transposed, kRotationTolerance));
    }
}

TEST_CASE("isUnitQuaternion refuses what was never a rotation", "[math][quaternion]") {
    // **The tolerance is tight on purpose, and M1-11 is why.** A quaternion of
    // length 1 + e produces a rotation matrix whose orthonormality residual is
    // about ten times e, so whatever this admits bounds, ten times over, the
    // best any consumer's matrix can be claimed to be. The camera's view
    // matrix is the first consumer.
    constexpr f64 kNotANumber = std::numeric_limits<f64>::quiet_NaN();
    constexpr f64 kInfinity = std::numeric_limits<f64>::infinity();

    // A NaN and an infinity fail because `nearlyEqual` compares a difference
    // against a tolerance rather than testing equality: every comparison
    // against a NaN is false, so the refusal is the default rather than a case
    // somebody remembered to write.
    const std::array<Quat, 6> notRotations{
        {
            Quat{0.0, 0.0, 0.0, 0.0},
            Quat{2.0, 0.0, 0.0, 0.0},
            Quat{1.0, 0.0, 0.0, 1.0},
            Quat{0.5, 0.5, 0.5, 0.5001},
            Quat{kNotANumber, 0.0, 0.0, 0.0},
            Quat{kInfinity, 0.0, 0.0, 0.0},
        },
    };
    for (const Quat& q : notRotations) {
        REQUIRE(!isUnitQuaternion(q, kUnitQuaternionTolerance));
    }
}

TEST_CASE("isUnitQuaternion accepts everything this project builds", "[math][quaternion]") {
    REQUIRE(isUnitQuaternion(Quat{}, kUnitQuaternionTolerance));
    REQUIRE(isUnitQuaternion(Quat{0.0, 1.0, 0.0, 0.0}, kUnitQuaternionTolerance));

    // **With headroom, measured.** Over 500,000 draws on 2026-09-22,
    // `normalize` and `fromAxisAngle` both land within 1.5 ulp of unit length
    // where the tolerance admits 16. The tolerance was set from the *other*
    // measurement -- composing without renormalising reaches 6.0 ulp after ten
    // products -- so this case is what says the direct producers still fit.
    Sampler sampler;
    for (std::size_t i = 0; i < 2000; ++i) {
        CAPTURE(kSweepSeed, i);
        REQUIRE(isUnitQuaternion(sampler.rotation(), kUnitQuaternionTolerance));
        REQUIRE(isUnitQuaternion(quaternionFrom(matrixOf(sampler.rotation())),
                                 kUnitQuaternionTolerance));
    }
}

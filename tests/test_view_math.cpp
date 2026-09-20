//
// Tests for view/Mat4.hpp and view/Frame.hpp: the 4x4 homogeneous transform,
// the units it carries and the frames it maps between (M1-09; ADR 0012 for
// where it lives, ADR 0020 for the units, ADR 0021 for the frames).
//
// **Nothing here includes a Vulkan or SDL header**, and that is not
// discipline: this suite links orbsim_view, which links orbsim_core and
// nothing else, so a graphics header is not reachable from it. That is the
// whole point of ADR 0012.
//
// Most of what the frames buy cannot be tested at run time, because the
// point of them is that the wrong thing does not compile. Those claims are
// static_asserts in the header, next to the functions they constrain, and
// each was inverted and watched failing before being believed. What is left
// for this file is the arithmetic.
//
// Where the tolerances come from. Two of the three the task document carried
// did not survive measurement, and each is replaced by a law rather than by a
// tuned number:
//
//   * **associativity** was "1e-12 relative", which is ambiguous and, read
//     elementwise, is not even satisfiable: measured worst elementwise
//     relative difference is 7.5e-10, because an element can cancel to near
//     zero. Against the *conditioning* |A||B||C| it is 6.5e-16. So the bound
//     here is the conditioning law, 2*gamma_8, which is what backward error
//     analysis gives for two groupings of a 4x4 triple product;
//   * **inverseRigid** was "the identity to 1e-14". The rotation block of
//     M * inverse(M) is dimensionless and meets that with room to spare, but
//     the translation column is **in metres** and its residual is a
//     cancellation of two quantities of size |t|: 1.2e-8 m at Earth radius
//     and 3.1e-4 m at 1 AU. 1e-14 holds only for |t| below about 5 m. The
//     claim is therefore split, and the translation half is relative to |t|,
//     which is flat at about 9 ulp at every scale from 1 m to 30 AU.
//
// And two of the task's claims turn out to be *exact* rather than
// approximate, so they are asserted with bit identity rather than a
// tolerance: the identity is exactly the multiplicative identity, and
// transpose(AB) is bit-identical to transpose(B)transpose(A) -- the same four
// products summed in the same order. Measured 200,000 of 200,000 each way
// before being claimed.
//
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Frame.hpp"
#include "view/Mat4.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>

using namespace orb;
using namespace orb::view;

namespace {

// A seeded sweep, with the seed written down (VERIFICATION.md rule 12).
constexpr std::uint64_t kSweepSeed = 20260920ULL; // the date this suite was written
constexpr std::size_t kSweepCases = 20'000;

constexpr f64 kUnitRoundoff = std::numeric_limits<f64>::epsilon() / 2.0;

// Two groupings of a 4x4 triple product differ by at most 2*gamma_8 times the
// elementwise product of magnitudes, where gamma_n = n*u / (1 - n*u): eight
// roundings separate them, four per matrix product. That is 1.78e-15, and the
// worst measured over three populations of 200,000 triples -- uniform
// entries, three rigid transforms, and projection x view x model at Earth
// radius -- is 6.54e-16, so the law sits 2.7x above the measurement rather
// than being fitted to it.
constexpr f64 kGamma8 = (8.0 * kUnitRoundoff) / (1.0 - (8.0 * kUnitRoundoff));
constexpr f64 kAssociativityFactor = 2.0 * kGamma8;

// M * inverse(M), split because the two blocks are in different units.
//
// The rotation block is dimensionless and scale-free: measured 2.22e-15 at
// every translation magnitude from 1 m to 30 AU, so 1e-14 stands as the task
// wrote it, with 4.5x margin.
constexpr Tolerance kInverseRotationBlock{1e-14};

// The translation column is in metres, and its residual is t - R(R^T t): a
// cancellation of two quantities of size |t|, so it scales with |t| and
// nothing else. Measured 8.5, 8.6, 9.1, 9.2 and 8.8 ulp of |t| at 1 m, Earth
// radius, lunar distance, 1 AU and 30 AU -- flat, which is what says the law
// is the right one. 20 ulp is about twice the worst of those, which is this
// project's rule for a budget (register decisions 54 and 85).
constexpr f64 kInverseTranslationUlps = 20.0;

// A point through M and back. One more rounding than the matrix residual, and
// measured 9.0, 11.2, 9.8 and 10.1 ulp of the scale at the same magnitudes;
// 24 ulp is about twice the worst.
constexpr f64 kRoundTripUlps = 24.0;

// The scales the claims are checked at. A boundary that only works at Earth
// radius is the defect this project already shipped once in the propagator,
// so the sweep runs from a metre to 1 AU.
struct NamedScale {
    const char* name;
    f64 metres;
};

constexpr std::array<NamedScale, 4> kScales{
    {
        {.name = "1 m", .metres = 1.0},
        {.name = "Earth radius", .metres = 6.371e6},
        {.name = "lunar distance", .metres = 3.844e8},
        {.name = "1 AU", .metres = 1.495978707e11},
    },
};

// Cases per scale in the two scale sweeps below.
constexpr std::size_t kScaleCases = 2'000;

class Sampler {
public:
    // A uniformly random rotation, through the quaternion core/Math.hpp
    // already has: four normals normalised is the standard way to draw one
    // without clustering at the poles.
    [[nodiscard]] Quat rotation() {
        const Quat q{normal_(rng_), normal_(rng_), normal_(rng_), normal_(rng_)};
        return normalize(q);
    }

    [[nodiscard]] Position translation(f64 magnitude) {
        const Direction d = directionOf(Direction{normal_(rng_), normal_(rng_), normal_(rng_)});
        return Position{d.x.value() * magnitude, d.y.value() * magnitude, d.z.value() * magnitude};
    }

    [[nodiscard]] f64 unit() { return uniform_(rng_); }

private:
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
    std::mt19937_64 rng_{kSweepSeed};
    std::normal_distribution<f64> normal_{0.0, 1.0};
    std::uniform_real_distribution<f64> uniform_{-1.0, 1.0};
};

// A rigid transform within the world frame. Rotations and translations do not
// change frames -- only a camera's definition of view space does, and that is
// `retargetFrame`'s job -- so these compose freely here.
[[nodiscard]] WorldTransform rigidTransform(Sampler& sampler, f64 magnitude) {
    return translationOf<kWorld>(sampler.translation(magnitude)) *
           rotationOf<kWorld>(sampler.rotation());
}

// The same thing declared to land in view space, which is what a camera does.
[[nodiscard]] WorldToView worldToView(Sampler& sampler, f64 magnitude) {
    return retargetFrame<kView>(rigidTransform(sampler, magnitude));
}

// The two operands of a magnitude product, in order. A struct rather than two
// parameters because |A||B| is not |B||A|, and two adjacent arrays of sixteen
// doubles transpose in silence -- which is the check this project switched
// SuppressParametersUsedTogether off for on 2026-09-20, reporting this very
// function.
struct MagnitudeOperands {
    std::array<f64, 16> left;
    std::array<f64, 16> right;
};

// |A| |B| elementwise: the conditioning of a product, against which a
// difference of two groupings is judged.
[[nodiscard]] std::array<f64, 16> magnitudeProduct(const MagnitudeOperands& operands) {
    std::array<f64, 16> result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            f64 sum = 0.0;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += std::abs(operands.left.at((k * 4) + row)) *
                       std::abs(operands.right.at((column * 4) + k));
            }
            result.at((column * 4) + row) = sum;
        }
    }
    return result;
}

// The two halves of M * inverse(M)'s distance from the identity, measured
// separately because they are in different units: the rotation block is
// dimensionless, the translation column is metres and is therefore reported
// relative to |t|.
struct InverseResidual {
    f64 rotationBlock{};
    f64 translationUlps{};
};

[[nodiscard]] InverseResidual measureInverseResidual(Sampler& sampler, f64 magnitude) {
    InverseResidual worst{};
    for (std::size_t i = 0; i < kScaleCases; ++i) {
        const WorldToView m = worldToView(sampler, magnitude);
        // m is world-to-view and its inverse is view-to-world, so this
        // product is the identity **of view space** -- a fact the types now
        // carry rather than a comment.
        const Transform<kView, kView> product = m * inverseRigid(m);
        for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t row = 0; row < 3; ++row) {
                const f64 want = column == row ? 1.0 : 0.0;
                const f64 got = product.linear(Row{row}, Column{column}).value();
                worst.rotationBlock = std::max(worst.rotationBlock, std::abs(got - want));
            }
        }
        for (std::size_t row = 0; row < 3; ++row) {
            const f64 residual = std::abs(product.translation(Row{row}).value());
            worst.translationUlps =
                std::max(worst.translationUlps,
                         residual / (magnitude * std::numeric_limits<f64>::epsilon()));
        }
    }
    return worst;
}

[[nodiscard]] f64 measureRoundTripUlps(Sampler& sampler, f64 magnitude) {
    f64 worstUlps = 0.0;
    for (std::size_t i = 0; i < kScaleCases; ++i) {
        const WorldToView m = worldToView(sampler, magnitude);
        const WorldPosition p{
            .v = Position{sampler.unit() * magnitude,
                          sampler.unit() * magnitude,
                          sampler.unit() * magnitude},
        };
        // World to view and back. Each step's frame is checked by the
        // compiler; what this measures is the arithmetic.
        const ViewPosition inView = transformPoint(m, p);
        const WorldPosition back = transformPoint(inverseRigid(m), inView);
        const std::array<f64, 3> before{{p.v.x.value(), p.v.y.value(), p.v.z.value()}};
        const std::array<f64, 3> after{{back.v.x.value(), back.v.y.value(), back.v.z.value()}};
        for (std::size_t k = 0; k < 3; ++k) {
            const f64 residual = std::abs(after.at(k) - before.at(k));
            worstUlps =
                std::max(worstUlps, residual / (magnitude * std::numeric_limits<f64>::epsilon()));
        }
    }
    return worstUlps;
}

} // namespace

TEST_CASE("matrix multiplication is associative to the conditioning of the product") {
    Sampler sampler;
    f64 worstRatio = 0.0;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        const WorldTransform a = rigidTransform(sampler, 1.0);
        const WorldTransform b = rigidTransform(sampler, 1.0);
        const WorldTransform c = rigidTransform(sampler, 6.371e6);

        const WorldTransform left = (a * b) * c;
        const WorldTransform right = a * (b * c);
        const std::array<f64, 16> conditioning = magnitudeProduct({
            .left = magnitudeProduct({.left = a.columnMajor(), .right = b.columnMajor()}),
            .right = c.columnMajor(),
        });

        const std::array<f64, 16> leftElements = left.columnMajor();
        const std::array<f64, 16> rightElements = right.columnMajor();
        for (std::size_t j = 0; j < 16; ++j) {
            const f64 difference = std::abs(leftElements.at(j) - rightElements.at(j));
            if (conditioning.at(j) <= 0.0) {
                CAPTURE(i, j);
                REQUIRE(difference <= 0.0);
                continue;
            }
            worstRatio = std::max(worstRatio, difference / conditioning.at(j));
        }
    }
    CAPTURE(worstRatio, kAssociativityFactor);
    REQUIRE(worstRatio <= kAssociativityFactor);
    // Not vacuous: the two groupings genuinely differ somewhere, so a test
    // that passed because both sides were the same expression would fail here.
    REQUIRE(worstRatio > 0.0);
}

TEST_CASE("the identity is exactly the multiplicative identity") {
    Sampler sampler;
    const WorldTransform identity = identityTransform();
    for (std::size_t i = 0; i < 1000; ++i) {
        const WorldTransform m = rigidTransform(sampler, 6.371e6);
        CAPTURE(i);
        REQUIRE((m * identity).bitIdentical(m));
        REQUIRE((identity * m).bitIdentical(m));
        // Not vacuous: m is not itself the identity, so a multiplication that
        // returned its argument unchanged would still pass above, and a
        // multiplication that returned nothing would not get this far.
        REQUIRE_FALSE(m.bitIdentical(identity));
    }
}

TEST_CASE("transposing twice returns the original, bit for bit") {
    Sampler sampler;
    for (std::size_t i = 0; i < 1000; ++i) {
        const WorldTransform m = rigidTransform(sampler, 6.371e6);
        CAPTURE(i);
        REQUIRE(transpose(transpose(m)).bitIdentical(m));
        // Not vacuous: one transposition really does change the matrix, so a
        // transpose that handed its argument back would pass the line above
        // and fail this one. Compared through bitsOf rather than `==`, which
        // -Wfloat-equal forbids on a double.
        const std::array<f64, 16> straight = m.columnMajor();
        const std::array<f64, 16> turned = transpose(m).columnMajor();
        bool anyElementMoved = false;
        for (std::size_t j = 0; j < 16; ++j) {
            if (bitsOf(straight.at(j)) != bitsOf(turned.at(j))) anyElementMoved = true;
        }
        REQUIRE(anyElementMoved);
    }
}

TEST_CASE("transpose(AB) is transpose(B) transpose(A), bit for bit") {
    // The check that catches a row/column-major mix-up, which is the defect
    // this file exists to avoid. Bit-identical rather than close: both sides
    // sum the same four products in the same order, so anything else means
    // the indexing differs.
    //
    // It typechecks at all because the transpose is typed as a **dual** map:
    // both sides live in the dual of the world frame, with every reference
    // inverted. Forced back into a matrix between the same two spaces, the
    // law only typechecks where the operands' units line up.
    Sampler sampler;
    for (std::size_t i = 0; i < 1000; ++i) {
        const WorldTransform a = rigidTransform(sampler, 1.0);
        const WorldTransform b = rigidTransform(sampler, 6.371e6);
        CAPTURE(i);
        REQUIRE(transpose(a * b).bitIdentical(transpose(b) * transpose(a)));
    }
}

TEST_CASE("the rigid inverse undoes the transform, at every scale") {
    Sampler sampler;
    for (const NamedScale& scale : kScales) {
        const InverseResidual worst = measureInverseResidual(sampler, scale.metres);
        CAPTURE(scale.name, worst.rotationBlock, worst.translationUlps);
        REQUIRE(nearlyEqual(worst.rotationBlock, 0.0, kInverseRotationBlock));
        REQUIRE(worst.translationUlps <= kInverseTranslationUlps);
        // Not vacuous: a residual of exactly zero everywhere would mean the
        // product was never formed. It is not zero -- rounding sees to that.
        REQUIRE(worst.rotationBlock > 0.0);
    }
}

TEST_CASE("a point survives the round trip through a transform and its inverse") {
    Sampler sampler;
    for (const NamedScale& scale : kScales) {
        const f64 worstUlps = measureRoundTripUlps(sampler, scale.metres);
        CAPTURE(scale.name, worstUlps);
        REQUIRE(worstUlps <= kRoundTripUlps);
    }
}

TEST_CASE("a translation moves a point and leaves a direction alone") {
    // The whole reason transformPoint and transformDirection are two names.
    const Position offset{1.0e6, -2.0e6, 3.0e6};
    const WorldTransform shift = translationOf<kWorld>(offset);
    const WorldPosition point{.v = Position{10.0, 20.0, 30.0}};

    const WorldPosition moved = transformPoint(shift, point);
    REQUIRE(nearlyEqual(moved.v.x.value(), 1.0e6 + 10.0, Tolerance{0.0}));
    REQUIRE(nearlyEqual(moved.v.y.value(), -2.0e6 + 20.0, Tolerance{0.0}));
    REQUIRE(nearlyEqual(moved.v.z.value(), 3.0e6 + 30.0, Tolerance{0.0}));

    const WorldPosition direction = transformDirection(shift, point);
    REQUIRE(bitIdentical(direction, point));
}

TEST_CASE("a rotation moves a point and a direction the same way") {
    Sampler sampler;
    for (std::size_t i = 0; i < 1000; ++i) {
        const Quat q = sampler.rotation();
        const WorldTransform r = rotationOf<kWorld>(q);
        const WorldPosition p{.v = Position{sampler.unit(), sampler.unit(), sampler.unit()}};
        CAPTURE(i);
        // No translation, so the two must agree exactly, and both must agree
        // with the quaternion the matrix was built from.
        REQUIRE(bitIdentical(transformPoint(r, p), transformDirection(r, p)));
        const Position turned = q.rotate(p.v);
        REQUIRE(distance(transformPoint(r, p).v, turned).value() <=
                8.0 * std::numeric_limits<f64>::epsilon() * length(p.v).value());
    }
}

TEST_CASE("isAffine says which matrices transformPoint may be handed") {
    REQUIRE(isAffine(identityTransform()));
    REQUIRE(isAffine(translationOf<kWorld>(Position{1.0, 2.0, 3.0})));

    WorldTransform bent = identityTransform();
    bent.set(Row{3}, Column{0}, 1e-300);
    REQUIRE_FALSE(isAffine(bent));

    // The next double above one, not `1.0 + 1e-16`: that rounds back to
    // exactly 1.0, so the first version of this assertion was asking isAffine
    // to reject a matrix that really was affine, and isAffine was right.
    // std::nextafter is the smallest change that is a change, which is the
    // point -- isAffine compares bits, not magnitudes.
    WorldTransform scaled = identityTransform();
    scaled.set(Row{3}, Column{3}, std::nextafter(1.0, 2.0));
    REQUIRE_FALSE(isAffine(scaled));
}

TEST_CASE("a frame change is declared once, and the arithmetic is untouched by it") {
    // retargetFrame is an assertion, not a computation: the elements must be
    // the same afterwards, or it is doing something it must not.
    Sampler sampler;
    for (std::size_t i = 0; i < 1000; ++i) {
        const WorldTransform inWorld = rigidTransform(sampler, 6.371e6);
        const WorldToView declared = retargetFrame<kView>(inWorld);
        CAPTURE(i);
        // Element by element, through bitsOf -- `==` on doubles is what
        // -Wfloat-equal forbids, and the two matrices have different types so
        // bitIdentical cannot be reached.
        //
        // **Not by retargeting back and comparing**, which was the first
        // version and which the mutation pass defeated: applying a faulty
        // retargetFrame twice cancels its fault, so a retargetFrame that
        // transposed its argument passed. The round trip tested the round
        // trip, not the operation.
        const std::array<f64, 16> before = inWorld.columnMajor();
        const std::array<f64, 16> after = declared.columnMajor();
        for (std::size_t j = 0; j < 16; ++j) {
            CAPTURE(j);
            REQUIRE(bitsOf(before.at(j)) == bitsOf(after.at(j)));
        }
    }
}

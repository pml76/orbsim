//
// Tests for the count-like scalars in core/Scalar.hpp, for Pixels in
// core/Units.hpp, and for view/RenderQuality.hpp (M1-12; ADR 0001 for the
// type system, ADR 0007 for the quality struct, ADR 0012 for where it lives).
//
// **Nothing here includes a Vulkan or SDL header**, and that is the link
// graph's doing rather than anyone's discipline: this suite links
// orbsim_view, which links orbsim_core and nothing else (ADR 0012).
//
// **Almost everything these types buy is a compile-time refusal**, and those
// claims live as static_asserts beside the types rather than here -- the way
// test_projection.cpp keeps the projection matrix's layout beside the
// builder. Two groups of them are the reason the types exist at all:
//
//   * `Texels` does not convert to `Mebibytes`, neither converts to or from a
//     bare std::uint32_t, and neither can be divided -- by the other or by a
//     plain number, because either would truncate in silence.
//   * **a count that would wrap round is refused while the compiler is
//     evaluating it**, so `Texels{1} - Texels{2}` fails the build instead of
//     producing 4,294,967,295. That assertion sits in core/Scalar.hpp with a
//     positive control beside it, because without the control "not a constant
//     expression" and "the proof is broken" look exactly alike.
//
// What is left for run time is the arithmetic, and every result is checked
// against the same operation written on the bare number. That is the
// independent reference VERIFICATION.md rule 2 asks for, in the same sense
// that std::chrono is the calendar's in test_time.cpp: a strong type is only
// worth having if it does not change what the number does.
//
// **The Debug tree's run of these cases is the other half of the guard.**
// The compile-time assertions prove it refuses what it must; every operation
// below running under live assertions proves it does not refuse what it must
// not, and the table deliberately includes the values nearest the ends, where
// a guard written one comparison too eagerly would fire.
//
// **There is no seed here, and no random sweep, and that is a choice with a
// precedent.** test_projection.cpp's monotonicity case is a fixed geometric
// grid because a grid covers an interval where a sample only visits it, and
// test_sun.cpp has no seed at all for the same reason. The argument is
// stronger for whole numbers: the ways integer arithmetic goes wrong are at
// the ends, at the powers of two and at zero, and a uniform draw from the
// whole 32-bit range reaches the last few thousand values with a probability
// indistinguishable from zero. So the probes below are a table of exactly
// those places, crossed with itself, and the same run happens on every
// machine with nothing to write down.
//
// **RenderQuality has no run-time case here, and that is not an omission.**
// It has no fields until M1-46, so every claim it can make today -- that it
// is an aggregate, that copying it is trivial, that all four presets are
// usable in a constant expression -- is a compile-time claim, and each is a
// static_assert beside the struct. There is deliberately no operator== to
// compare two presets with: the first fields to arrive include a Pixels, and
// comparing two doubles with == is what CODING_GUIDELINES section 11 forbids.
//
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/RenderQuality.hpp" // IWYU pragma: keep -- the static_asserts are the test

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

using namespace orb;

namespace {

// The largest a 32-bit count holds. Named locally so the probe table below
// reads as a table; the fact itself lives on the type (`Count::kMaximum`).
constexpr std::uint32_t kLargestCount = Texels::kMaximum;

// Where whole-number arithmetic actually goes wrong: zero and its neighbours,
// the powers of two and the values either side of them, and both ends of the
// range. Everything below crosses this table with itself, so each case makes
// its claim about all 324 ordered pairs rather than about a sample.
constexpr auto kCountProbes = std::to_array<std::uint32_t>({
    0U,
    1U,
    2U,
    3U,
    255U,
    256U,
    257U,
    65'535U,
    65'536U,
    65'537U,
    1U << 20U,
    1U << 30U,
    2'147'483'647U, // the largest signed value, where a sign error would show
    2'147'483'648U,
    12'345U, // and a couple of values that are not near anything
    1'000'003U,
    kLargestCount - 1U,
    kLargestCount,
});

// Small enough that any product of two of them fits, so these cases are about
// the multiplication and not about the guard.
constexpr auto kSmallProbes =
    std::to_array<std::uint32_t>({0U, 1U, 2U, 3U, 17U, 255U, 256U, 1'000U, 65'535U});

// Byte counts, where the interesting values are the ones a 32-bit count could
// not reach: either side of 2^32, either side of 2^31, and both ends.
constexpr auto kByteProbes = std::to_array<std::uint64_t>({
    0ULL,
    1ULL,
    255ULL,
    4096ULL,
    2'147'483'647ULL,
    2'147'483'648ULL,
    4'294'967'295ULL, // the largest a 32-bit count holds
    4'294'967'296ULL, // one past it -- the reason this type is 64 bits
    4'294'967'297ULL,
    1'099'511'627'776ULL, // a tebibyte
    Bytes::kMaximum - 1ULL,
    Bytes::kMaximum,
});

// Powers of two, because that is what an alignment must be.
constexpr auto kAlignments = std::to_array<std::uint64_t>({1ULL, 2ULL, 16ULL, 256ULL, 4096ULL});

// Mebibyte counts either side of the one where a 32-bit multiplication by
// 1,048,576 overflows to zero.
constexpr auto kMebibyteProbes =
    std::to_array<std::uint32_t>({0U, 1U, 512U, 4095U, 4096U, 4097U, 65'536U});

// Pixel measurements across the magnitudes a screen-space threshold and a
// screen-space error actually span, both signs, and the awkward ones: the two
// zeros, a value that is not representable exactly, and the subdivision
// threshold ADR 0007 uses as its example.
constexpr auto kPixelProbes = std::to_array<f64>({
    0.0,
    -0.0,
    1.0,
    -1.0,
    0.5,
    2.5,
    0.1, // not exactly representable, so the type must not round it differently
    1'080.0,
    1'920.0,
    -2'047.5,
    1e-8,
    1e8,
});

} // namespace

TEST_CASE("a count added and then taken away again is the number it started with") {
    for (const std::uint32_t a : kCountProbes) {
        for (const std::uint32_t b : kCountProbes) {
            if (b > kLargestCount - a) continue; // the sum is the guard's case, not this one
            CAPTURE(a, b);
            const Texels sum = Texels{a} + Texels{b};
            REQUIRE(sum.value() == a + b);
            REQUIRE((sum - Texels{b}).value() == a);
        }
    }
}

TEST_CASE("a mebibyte budget is the same arithmetic, because the base is the same") {
    for (const std::uint32_t a : kCountProbes) {
        for (const std::uint32_t b : kCountProbes) {
            if (b > kLargestCount - a) continue;
            CAPTURE(a, b);
            const Mebibytes sum = Mebibytes{a} + Mebibytes{b};
            REQUIRE(sum.value() == a + b);
            REQUIRE((sum - Mebibytes{b}).value() == a);
        }
    }
}

TEST_CASE("a count subtracts to the difference of the two numbers") {
    for (const std::uint32_t a : kCountProbes) {
        for (const std::uint32_t b : kCountProbes) {
            if (b > a) continue; // going below zero is the guard's case
            CAPTURE(a, b);
            REQUIRE((Texels{a} - Texels{b}).value() == a - b);
        }
    }
}

TEST_CASE("a count orders and compares the way the number it holds does") {
    for (const std::uint32_t a : kCountProbes) {
        for (const std::uint32_t b : kCountProbes) {
            CAPTURE(a, b);
            REQUIRE((Texels{a} < Texels{b}) == (a < b));
            REQUIRE((Texels{a} == Texels{b}) == (a == b));
        }
    }
}

TEST_CASE("multiplying a count by a scale is the product of the two numbers") {
    for (const std::uint32_t a : kSmallProbes) {
        for (const std::uint32_t scale : kSmallProbes) {
            CAPTURE(a, scale);
            REQUIRE((Texels{a} * scale).value() == a * scale);
            REQUIRE((scale * Texels{a}).value() == a * scale);
        }
    }
}

TEST_CASE("the compound forms are the plain ones, applied in place") {
    for (const std::uint32_t a : kCountProbes) {
        for (const std::uint32_t b : kCountProbes) {
            if (b > kLargestCount - a) continue;
            CAPTURE(a, b);
            Texels running{a};
            running += Texels{b};
            REQUIRE(running.value() == a + b);
            running -= Texels{b};
            REQUIRE(running.value() == a);
        }
    }
}

// The ends of the range, called out separately from the crossings above
// because these are the values a guard written with the wrong comparison
// would refuse. In the Debug tree, where assertions are live, that mistake
// fails this case rather than passing quietly (VERIFICATION.md rule 5).
TEST_CASE("the boundary values that do not wrap are accepted") {
    REQUIRE((Texels{kLargestCount} + Texels{0}).value() == kLargestCount);
    REQUIRE((Texels{0} + Texels{kLargestCount}).value() == kLargestCount);
    REQUIRE((Texels{kLargestCount} - Texels{kLargestCount}).value() == 0U);
    REQUIRE((Texels{kLargestCount} - Texels{0}).value() == kLargestCount);
    REQUIRE((Texels{kLargestCount} * 1U).value() == kLargestCount);
    REQUIRE((Texels{kLargestCount} * 0U).value() == 0U);
    REQUIRE((Texels{0} * kLargestCount).value() == 0U);
    REQUIRE((Texels{1U} * kLargestCount).value() == kLargestCount);
}

TEST_CASE("a default count is zero, not whatever was on the stack") {
    REQUIRE(Texels{}.value() == 0U);
    REQUIRE(Mebibytes{}.value() == 0U);
}

// Byte counts get their own table: a 64-bit type's interesting values are not
// a 32-bit type's, and the whole reason `Bytes` exists is the range above four
// billion that `Texels` cannot reach.
TEST_CASE("a byte count works above the range a 32-bit count could hold") {
    for (const std::uint64_t a : kByteProbes) {
        for (const std::uint64_t b : kByteProbes) {
            if (b > Bytes::kMaximum - a) continue;
            CAPTURE(a, b);
            const Bytes sum = Bytes{a} + Bytes{b};
            REQUIRE(sum.value() == a + b);
            REQUIRE((sum - Bytes{b}).value() == a);
        }
    }
}

TEST_CASE("mebibytes convert to bytes by widening first, not after") {
    // 4,096 MiB is exactly 2^32 bytes: computed in 32 bits it is zero, which
    // is the defect this conversion exists to make impossible.
    for (const std::uint32_t mib : kMebibyteProbes) {
        CAPTURE(mib);
        const std::uint64_t expected = std::uint64_t{mib} * 1024ULL * 1024ULL;
        REQUIRE(toBytes(Mebibytes{mib}).value() == expected);
    }
    REQUIRE(toBytes(Mebibytes{4096U}).value() == 4'294'967'296ULL);
}

// Split one claim per case, as this suite's siblings are: Catch2's REQUIRE
// expands to a branch, so three claims inside two nested loops score past
// readability-function-cognitive-complexity on the generated function. The
// split reads better anyway -- a failure names the property that broke.
TEST_CASE("aligning up lands on a multiple of the step") {
    for (const std::uint64_t value : kByteProbes) {
        for (const std::uint64_t step : kAlignments) {
            if (value > Bytes::kMaximum - step) continue; // the guard's case, not this one
            CAPTURE(value, step);
            REQUIRE(Bytes{value}.alignedUpTo(Bytes{step}).value() % step == 0U);
        }
    }
}

TEST_CASE("aligning up never lands below what was asked for") {
    for (const std::uint64_t value : kByteProbes) {
        for (const std::uint64_t step : kAlignments) {
            if (value > Bytes::kMaximum - step) continue;
            CAPTURE(value, step);
            REQUIRE(Bytes{value}.alignedUpTo(Bytes{step}).value() >= value);
        }
    }
}

TEST_CASE("aligning up never lands more than one step above") {
    for (const std::uint64_t value : kByteProbes) {
        for (const std::uint64_t step : kAlignments) {
            if (value > Bytes::kMaximum - step) continue;
            CAPTURE(value, step);
            REQUIRE(Bytes{value}.alignedUpTo(Bytes{step}).value() - value < step);
        }
    }
}

TEST_CASE("how many fit is the whole number of blocks, with the remainder dropped") {
    for (const std::uint64_t total : kByteProbes) {
        for (const std::uint64_t each : kAlignments) {
            CAPTURE(total, each);
            REQUIRE(Bytes{total}.howManyFit(Bytes{each}) == total / each);
        }
    }
}

// The defining property rather than the division written twice: that many
// blocks fit, and one more would not.
TEST_CASE("that many blocks fit, and one more would not") {
    for (const std::uint64_t total : kByteProbes) {
        for (const std::uint64_t each : kAlignments) {
            CAPTURE(total, each);
            const std::uint64_t fitting = Bytes{total}.howManyFit(Bytes{each});
            REQUIRE(total - (fitting * each) < each);
        }
    }
}

TEST_CASE("pixels add and subtract like the number they carry") {
    for (const f64 a : kPixelProbes) {
        for (const f64 b : kPixelProbes) {
            CAPTURE(a, b);
            // A zero tolerance, because the claim is that the type does not
            // touch the arithmetic: the same two doubles, the same operation,
            // the same rounding. nearlyEqual with a zero tolerance is how this
            // codebase spells an exact comparison of doubles (core/Scalar.hpp).
            REQUIRE(nearlyEqual((Pixels{a} + Pixels{b}).value(), a + b, Tolerance{0.0}));
            REQUIRE(nearlyEqual((Pixels{a} - Pixels{b}).value(), a - b, Tolerance{0.0}));
        }
    }
}

TEST_CASE("a pixel measurement scales and orders like its number") {
    for (const f64 a : kPixelProbes) {
        for (const f64 scale : kPixelProbes) {
            CAPTURE(a, scale);
            REQUIRE(nearlyEqual((Pixels{a} * scale).value(), a * scale, Tolerance{0.0}));
            REQUIRE((Pixels{a} < Pixels{scale}) == (a < scale));
        }
    }
}

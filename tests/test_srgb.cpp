//
// Tests for view/Srgb.hpp and view/SceneClear.hpp: the sRGB transfer function
// on the CPU, and the colour the HDR target is cleared to (M1-14; ADR 0014).
//
// **Where the expected numbers come from.** Not from this code.
// scripts/srgb-reference.py evaluates IEC 61966-2-1's definition in 50-digit
// decimal arithmetic at each input's exact binary value and rounds the result
// once to the nearest double; its output is pasted below unedited. One of its
// values can be checked against a figure published independently of both: the
// 8-bit code 128 decodes to 0.2158605..., the value every sRGB lookup table
// carries.
//
// **Three things are asserted separately, because they are three claims.**
// Agreement with the published definition, point by point, to a few units in
// the last place. The standard's own breaks at its knees, as the numbers the
// reference prints -- so the function is not assumed continuous where the
// standard is not. And the round trip, to 1e-15 everywhere except the one
// window above the linear knee where the standard's constants make it
// impossible, and to the standard's own decode jump inside it.
//
// **No seed.** The sweeps are fixed grids, which cover the interval rather
// than sampling it, and a random draw would visit the 7.3e-9-wide window at
// the knee with a probability indistinguishable from zero -- test_sun.cpp and
// test_render_quality.cpp make the same argument.
//
// **What this suite cannot see**: the GPU's encode in shaders/tonemap.frag. It
// is a second implementation in GLSL, and nothing reads a pixel back before
// M1-16; M1-18 compares the two to 1/255. A known gap, declared in
// scripts/mutants/m1-14.json.
//
#include "core/Scalar.hpp"
#include "view/SceneClear.hpp"
#include "view/Srgb.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

using namespace orb;
using namespace orb::view;

namespace {

// One reference pair from scripts/srgb-reference.py: a function's input and
// the definition's value there, rounded once to a double.
struct Reference {
    f64 input{};
    f64 expected{};
};

// The encode, at each input the script chose: zero, the linear segment, the
// knee and the double just above it, the power segment, and one.
constexpr std::array kEncodeReferences{
    Reference{.input = 0.0, .expected = 0.0},
    Reference{.input = 1e-09, .expected = 1.2920000000000001e-08},
    Reference{.input = 1e-06, .expected = 1.292e-05},
    Reference{.input = 0.001, .expected = 0.012920000000000001},
    Reference{.input = 0.0031308, .expected = 0.040449936},
    Reference{.input = 0.0031308000000000004, .expected = 0.04044990748269016},
    Reference{.input = 0.01, .expected = 0.09985282273412834},
    Reference{.input = 0.018, .expected = 0.1428256813030392},
    Reference{.input = 0.05, .expected = 0.24780052799263166},
    Reference{.input = 0.1, .expected = 0.34919021262829386},
    Reference{.input = 0.214, .expected = 0.4999555493402056},
    Reference{.input = 0.5, .expected = 0.7353569830524495},
    Reference{.input = 0.75, .expected = 0.8808250210902998},
    Reference{.input = 0.9, .expected = 0.9546871718858663},
    Reference{.input = 0.99, .expected = 0.995591277378595},
    Reference{.input = 1.0, .expected = 1.0},
};

constexpr std::array kDecodeReferences{
    Reference{.input = 0.0, .expected = 0.0},
    Reference{.input = 1e-06, .expected = 7.739938080495356e-08},
    Reference{.input = 0.004, .expected = 0.00030959752321981426},
    Reference{.input = 0.006, .expected = 0.0004643962848297214},
    Reference{.input = 0.012, .expected = 0.0009287925696594428},
    Reference{.input = 0.02, .expected = 0.0015479876160990713},
    Reference{.input = 0.04045, .expected = 0.0031308049535603713},
    Reference{.input = 0.04045000000000001, .expected = 0.0031308072830676828},
    Reference{.input = 0.1, .expected = 0.010022825574869035},
    Reference{.input = 0.5019607843137255, .expected = 0.21586050011389915}, // 128/255
    Reference{.input = 0.5, .expected = 0.21404114048223244},
    Reference{.input = 0.75, .expected = 0.5225215539683918},
    Reference{.input = 0.9, .expected = 0.7874122893956171},
    Reference{.input = 1.0, .expected = 1.0},
};

// What the standard's rounded constants leave at the knees, from the same
// script: the power branch minus the linear one, at the knee itself.
constexpr f64 kEncodeJumpAtKnee = -2.8517309848186724e-08;
constexpr f64 kDecodeJumpAtKnee = 2.329507310944346e-09;

// Two non-negative doubles to be compared, by name, so a comparison cannot
// hand its two numbers over the wrong way round (non-negotiable 1).
struct Compared {
    f64 got{};
    f64 expected{};
};

// How far apart the two are, in representable doubles. For values of one
// sign the bit patterns are ordered like the values, so the difference of the
// patterns is the count of doubles between them.
[[nodiscard]] std::uint64_t ulpDistance(Compared pair) {
    const std::uint64_t left = bitsOf(pair.got);
    const std::uint64_t right = bitsOf(pair.expected);
    return left > right ? left - right : right - left;
}

// The budget against the 50-digit reference, in units in the last place.
// Measured before it was written down, with the budget set to zero so every
// point reported its distance: 15 of the 30 points are off at all, the worst
// by 4 -- all on the decode's power segment, which is its conditioning rather
// than a defect: raising to 2.4 multiplies the relative rounding of the base
// (two operations, about 1.5 units) by 2.4, and pow() adds its own. The
// budget is twice the measurement, the project's rule for a reference budget
// (register decisions 54 and 76).
constexpr std::uint64_t kReferenceBudgetUlps = 8;

// The round trip's budget away from the knee window: 4.4e-16 measured over a
// 2.2-million-point grid, and this is about twice that.
constexpr f64 kRoundTripBudget = 1e-15;

// What the round-trip sweep below has seen: the worst error inside and
// outside the knee window, and where the window was found. A type of its own
// so the test case reads as the claim rather than as the bookkeeping.
//
// A value just above the linear knee encodes to just below 0.04045 -- the
// encode's step down -- and so decodes on the linear branch, which is the
// other formula. There the round trip is out by up to the decode's own jump.
// The window is identified by that mechanism, not by numbers copied from a
// measurement.
class RoundTripSweep {
public:
    void visit(f64 x) {
        const f64 error = absOf(srgbDecode(srgbEncode(LinearValue{x})).value() - x);
        if (!inKneeWindow(x)) {
            worstOutside_ = std::max(worstOutside_, error);
            return;
        }
        worstInside_ = std::max(worstInside_, error);
        windowLow_ = std::min(windowLow_, x);
        windowHigh_ = std::max(windowHigh_, x);
    }

    [[nodiscard]] f64 worstOutside() const { return worstOutside_; }
    [[nodiscard]] f64 worstInside() const { return worstInside_; }
    [[nodiscard]] f64 windowLow() const { return windowLow_; }
    [[nodiscard]] f64 windowHigh() const { return windowHigh_; }

private:
    [[nodiscard]] static bool inKneeWindow(f64 x) {
        return x > kSrgbLinearKnee && srgbEncode(LinearValue{x}).value() <= kSrgbEncodedKnee;
    }

    f64 worstOutside_{0.0};
    f64 worstInside_{0.0};
    f64 windowLow_{1.0};
    f64 windowHigh_{0.0};
};

// The whole range on a fixed grid, then the knee on a grid 1000 times finer
// than the window is wide. Both round-trip cases below read the same sweep;
// it is run once for each, which costs a fraction of a second and keeps each
// case independent of the other's order.
[[nodiscard]] RoundTripSweep sweepRoundTrip() {
    constexpr int kWholeRangeSteps = 2'000'000;
    constexpr int kKneeSteps = 200'000;
    constexpr f64 kKneeSpan = 2e-8;

    RoundTripSweep sweep;
    for (int i = 0; i <= kWholeRangeSteps; ++i) {
        sweep.visit(static_cast<f64>(i) / kWholeRangeSteps);
    }
    for (int i = 0; i <= kKneeSteps; ++i) {
        sweep.visit(kSrgbLinearKnee - (kKneeSpan / 2) +
                    (kKneeSpan * static_cast<f64>(i) / kKneeSteps));
    }
    return sweep;
}

} // namespace

TEST_CASE("the encode matches IEC 61966-2-1 at every reference point") {
    for (const Reference& reference : kEncodeReferences) {
        const f64 got = srgbEncode(LinearValue{reference.input}).value();
        CAPTURE(reference.input, reference.expected, got);
        CHECK(ulpDistance({.got = got, .expected = reference.expected}) <= kReferenceBudgetUlps);
    }
}

TEST_CASE("the decode matches IEC 61966-2-1 at every reference point") {
    for (const Reference& reference : kDecodeReferences) {
        const f64 got = srgbDecode(EncodedValue{reference.input}).value();
        CAPTURE(reference.input, reference.expected, got);
        CHECK(ulpDistance({.got = got, .expected = reference.expected}) <= kReferenceBudgetUlps);
    }
}

TEST_CASE("zero is exact, and one is within a unit in the last place") {
    // Zero is on the linear segment, where it is a product with zero.
    CHECK(srgbEncode(LinearValue{0.0}).bitIdentical(EncodedValue{0.0}));
    CHECK(srgbDecode(EncodedValue{0.0}).bitIdentical(LinearValue{0.0}));
    // One is not exact on the encode: 1.055 - 0.055 in doubles is
    // 0.9999999999999999, one double below 1, because neither constant is
    // representable. Measured 2026-09-24; the budget is that one step and no
    // more, so the claim is "the end of the range", not "roughly one".
    CHECK(ulpDistance({.got = srgbEncode(LinearValue{1.0}).value(), .expected = 1.0}) <= 1);
    CHECK(ulpDistance({.got = srgbDecode(EncodedValue{1.0}).value(), .expected = 1.0}) <= 1);
}

TEST_CASE("each side of the knee is on its own branch, and the steps are the standard's") {
    // At the knee itself the encode is on the linear segment, and the next
    // double up is on the power segment. The two differ by the standard's own
    // -2.85e-8, so the encode steps *down* there: asserted, not smoothed over.
    const f64 atKnee = srgbEncode(LinearValue{kSrgbLinearKnee}).value();
    const f64 aboveKnee = srgbEncode(LinearValue{std::nextafter(kSrgbLinearKnee, 1.0)}).value();
    CAPTURE(atKnee, aboveKnee);
    CHECK(ulpDistance({.got = atKnee, .expected = kSrgbLinearSlope * kSrgbLinearKnee}) == 0);
    CHECK(aboveKnee < atKnee);
    // The step is the reference's to within the change of the power branch
    // over one double (about 12.7 * 4.3e-19) and the rounding of the two sides.
    CHECK(nearlyEqual(aboveKnee - atKnee, kEncodeJumpAtKnee, Tolerance{1e-16}));

    const f64 decodedAtKnee = srgbDecode(EncodedValue{kSrgbEncodedKnee}).value();
    const f64 decodedAboveKnee =
        srgbDecode(EncodedValue{std::nextafter(kSrgbEncodedKnee, 1.0)}).value();
    CAPTURE(decodedAtKnee, decodedAboveKnee);
    CHECK(decodedAboveKnee > decodedAtKnee);
    CHECK(nearlyEqual(decodedAboveKnee - decodedAtKnee, kDecodeJumpAtKnee, Tolerance{1e-17}));
}

TEST_CASE("values outside [0, 1] are clamped, as the display target clamps them") {
    CHECK(srgbEncode(LinearValue{-0.5}).bitIdentical(EncodedValue{0.0}));
    CHECK(srgbEncode(LinearValue{-std::numeric_limits<f64>::infinity()})
              .bitIdentical(EncodedValue{0.0}));
    CHECK(srgbEncode(LinearValue{2.0}).bitIdentical(srgbEncode(LinearValue{1.0})));
    CHECK(srgbEncode(LinearValue{std::numeric_limits<f64>::infinity()})
              .bitIdentical(srgbEncode(LinearValue{1.0})));
    CHECK(srgbDecode(EncodedValue{-0.5}).bitIdentical(LinearValue{0.0}));
    CHECK(srgbDecode(EncodedValue{2.0}).bitIdentical(srgbDecode(EncodedValue{1.0})));
}

TEST_CASE("encode then decode returns the input, except in the window at the knee") {
    const RoundTripSweep sweep = sweepRoundTrip();
    CAPTURE(sweep.worstOutside());
    CHECK(sweep.worstOutside() <= kRoundTripBudget);
}

TEST_CASE("the window at the knee is where the standard puts it, and no wider") {
    // Its width is asserted as well as its error, so the exception above
    // cannot quietly grow to cover a real defect.
    const RoundTripSweep sweep = sweepRoundTrip();
    CAPTURE(sweep.worstInside(), sweep.windowLow(), sweep.windowHigh());
    // Not vacuous: the fine grid does land in the window, the window is where
    // the mechanism says, and it is as narrow as measured -- 7.28e-9 -- to
    // within one grid step.
    REQUIRE(sweep.windowHigh() > sweep.windowLow());
    CHECK(sweep.windowLow() > kSrgbLinearKnee);
    CHECK(sweep.windowHigh() - sweep.windowLow() < 7.4e-9);
    // Inside, the loss is the standard's decode jump at most, plus rounding --
    // and more than the budget outside, or there would be no window at all.
    CHECK(sweep.worstInside() <= kDecodeJumpAtKnee + 1e-15);
    CHECK(sweep.worstInside() > kRoundTripBudget);
}

TEST_CASE("the scene clear is the old display colour decoded, and displays as it did") {
    // The three display values the scene was cleared to before M1-14, and
    // the 8-bit codes a display showed for them.
    constexpr std::array kDisplayValues{0.004, 0.006, 0.012};
    const std::array cleared{kSceneClear.red, kSceneClear.green, kSceneClear.blue};

    for (std::size_t i = 0; i < kDisplayValues.size(); ++i) {
        const f64 display = kDisplayValues.at(i);
        const f64 decoded = srgbDecode(EncodedValue{display}).value();
        const f32 literal = cleared.at(i);
        CAPTURE(display, decoded, literal);
        // The literal is the float nearest the decoded value -- the one a
        // correct narrowing would produce, written without a cast in src/.
        CHECK(bitsOf(static_cast<f32>(decoded)) == bitsOf(literal));
        // The HDR target then holds it as a 16-bit float, which keeps 11
        // significant bits, so it is stored to within a relative 2^-11 either
        // way -- a bound rather than the exact half, which the standard
        // library cannot yet produce on every toolchain here. At both ends of
        // that bound, encoded and quantised to 8 bits, it lands on the code
        // the display value itself did: nothing visible changes.
        constexpr f64 kHalfPrecision = 0x1p-11;
        for (const f64 stored : {
                 static_cast<f64>(literal) * (1.0 - kHalfPrecision),
                 static_cast<f64>(literal) * (1.0 + kHalfPrecision),
             }) {
            const f64 shown = srgbEncode(LinearValue{stored}).value();
            CAPTURE(stored, shown);
            CHECK(std::lround(shown * 255.0) == std::lround(display * 255.0));
        }
    }
    CHECK(ulpDistance({.got = static_cast<f64>(kSceneClear.alpha), .expected = 1.0}) == 0);
}

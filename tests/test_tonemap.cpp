//
// Tests for view/Tonemap.hpp: AgX on the CPU (M1-15; ADR 0014).
//
// **Where the expected numbers come from.** Not from this code.
// scripts/tonemap-reference.py evaluates the minimal implementation's
// definition, with the three guards register decision 174 added, in 50-digit
// decimal arithmetic at each input's exact binary value; its output is pasted
// below unedited. It builds the matrices from the GLSL column lists and checks
// the inset against Sobotka's row-major config.ocio before printing anything,
// so a transposed matrix here disagrees with two sources, not one.
//
// **What this suite is, and what it is not.** It shows that this file is the
// definition, to the rounding of doubles, and that the transform has the
// properties the task asks of it: black stays black, grey rises monotonically,
// and a huge input saturates rather than wrapping into a NaN. It is **not** a
// validation of AgX -- a display transform is a choice, not a physical claim --
// and it cannot see the shader. The CPU-GPU comparison is M1-18's, to 1/255,
// and is itself a **port check**: it verifies that one choice was implemented
// twice identically. Until then scripts/check-tonemap-constants.py holds the
// shader's constants to this file's, in `check`.
//
// **What the task asked for and does not get, measured before this was
// written** (register decision 180). "Strictly increasing over 12 orders of
// magnitude" cannot hold: AgX's log range is 16.5 stops, so a grey is strictly
// increasing only from 2.17e-4 to 16.3, 4.88 decades, and flat outside. That
// is asserted as it is: strictly increasing inside, never decreasing across
// twelve decades, and flat -- bit for bit -- beyond the top. Saturated colours
// are not included in the monotonic claim, because one is not true of them:
// the other two channels of a pure primary dip by up to 1e-5 as its exposure
// rises, through the outset matrix's negative entries -- 0.003 of an 8-bit
// step, recorded in the task document rather than asserted.
//
// **No seed.** The sweeps are fixed logarithmic grids.
//
#include "core/Scalar.hpp"
#include "view/Srgb.hpp"
#include "view/Tonemap.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>

using namespace orb;
using namespace orb::view;

namespace {

// One reference row from scripts/tonemap-reference.py: exposed scene light in,
// linear display light out.
struct Reference {
    ExposedRgb input;
    ExposedRgb expected; // the same three numbers, as linear display light
};

constexpr std::array<Reference, 21> kReferences{
    {
        // Grey, from 10 stops below mid-grey to 6 above.
        Reference{
            .input = {.red = 0.00017578125, .green = 0.00017578125, .blue = 0.00017578125},
            .expected =
                {
                    .red = 0.0,
                    .green = 0.0,
                    .blue = 0.0,
                },
        },
        Reference{
            .input = {.red = 0.0003515625, .green = 0.0003515625, .blue = 0.0003515625},
            .expected =
                {
                    .red = 1.0046702461469181e-05,
                    .green = 1.0041989958911168e-05,
                    .blue = 1.0041696688422541e-05,
                },
        },
        Reference{
            .input = {.red = 0.000703125, .green = 0.000703125, .blue = 0.000703125},
            .expected =
                {
                    .red = 6.052376125156484e-05,
                    .green = 6.053023052944499e-05,
                    .blue = 6.0530632988841476e-05,
                },
        },
        Reference{
            .input = {.red = 0.0028125, .green = 0.0028125, .blue = 0.0028125},
            .expected =
                {
                    .red = 0.0005834178889276508,
                    .green = 0.0005834644753932668,
                    .blue = 0.0005834673743096986,
                },
        },
        Reference{
            .input = {.red = 0.01125, .green = 0.01125, .blue = 0.01125},
            .expected =
                {
                    .red = 0.007325087411895336,
                    .green = 0.007325908443923115,
                    .blue = 0.007325959528569394,
                },
        },
        Reference{
            .input = {.red = 0.045, .green = 0.045, .blue = 0.045},
            .expected =
                {
                    .red = 0.05414612944293538,
                    .green = 0.0541578112254641,
                    .blue = 0.05415853812290415,
                },
        },
        Reference{
            .input = {.red = 0.09, .green = 0.09, .blue = 0.09},
            .expected =
                {
                    .red = 0.11564742159241082,
                    .green = 0.1156777777284511,
                    .blue = 0.11567966669297003,
                },
        },
        Reference{
            .input = {.red = 0.18, .green = 0.18, .blue = 0.18},
            .expected =
                {
                    .red = 0.21446743070720395,
                    .green = 0.21453266862054804,
                    .blue = 0.2145367282681333,
                },
        },
        Reference{
            .input = {.red = 0.36, .green = 0.36, .blue = 0.36},
            .expected =
                {
                    .red = 0.3505810840121137,
                    .green = 0.35070082807253466,
                    .blue = 0.35070827970734364,
                },
        },
        Reference{
            .input = {.red = 0.72, .green = 0.72, .blue = 0.72},
            .expected =
                {
                    .red = 0.511562064447352,
                    .green = 0.5117540615365168,
                    .blue = 0.5117660096780586,
                },
        },
        Reference{
            .input = {.red = 2.88, .green = 2.88, .blue = 2.88},
            .expected =
                {
                    .red = 0.8103101920070916,
                    .green = 0.8106601330422728,
                    .blue = 0.8106819108533896,
                },
        },
        Reference{
            .input = {.red = 11.52, .green = 11.52, .blue = 11.52},
            .expected =
                {
                    .red = 0.9662519396935597,
                    .green = 0.9666953176446859,
                    .blue = 0.9667229108652361,
                },
        },
        // Black, the three primaries -- which a transposed matrix gets wrong --
        // colours, a negative channel, and a value far past saturation.
        Reference{
            .input = {.red = 0.0, .green = 0.0, .blue = 0.0},
            .expected =
                {
                    .red = 0.0,
                    .green = 0.0,
                    .blue = 0.0,
                },
        },
        Reference{
            .input = {.red = 1.0, .green = 0.0, .blue = 0.0},
            .expected =
                {
                    .red = 0.7194171070830898,
                    .green = 0.03948762300066515,
                    .blue = 0.03955026020039461,
                },
        },
        Reference{
            .input = {.red = 0.0, .green = 1.0, .blue = 0.0},
            .expected =
                {
                    .red = 0.07678094742148991,
                    .green = 0.6656195095143986,
                    .blue = 0.07681984561322486,
                },
        },
        Reference{
            .input = {.red = 0.0, .green = 0.0, .blue = 1.0},
            .expected =
                {
                    .red = 0.07754176875638012,
                    .green = 0.07751717036176507,
                    .blue = 0.6648329977395647,
                },
        },
        Reference{
            .input = {.red = 0.18, .green = 0.09, .blue = 0.02},
            .expected =
                {
                    .red = 0.22871915852563587,
                    .green = 0.11998841235670604,
                    .blue = 0.026235435277378934,
                },
        },
        Reference{
            .input = {.red = 4.0, .green = 2.0, .blue = 0.5},
            .expected =
                {
                    .red = 0.8910261308621846,
                    .green = 0.7577887198269291,
                    .blue = 0.49351697412501105,
                },
        },
        Reference{
            .input = {.red = 0.05, .green = 0.3, .blue = 0.9},
            .expected =
                {
                    .red = 0.13422323427108077,
                    .green = 0.3289893038183934,
                    .blue = 0.5918387113695239,
                },
        },
        Reference{
            .input = {.red = -1.0, .green = 0.5, .blue = 0.5},
            .expected =
                {
                    .red = 0.06585470538057435,
                    .green = 0.43833854456835797,
                    .blue = 0.4383664074095061,
                },
        },
        Reference{
            .input = {.red = 1.0e12, .green = 1.0e12, .blue = 1.0e12},
            .expected =
                {
                    .red = 0.996502314439189,
                    .green = 0.9969775772285933,
                    .blue = 0.9970071550199865,
                },
        },
    },
};

// From the same script: the largest grey that is exactly black in every
// channel -- where the curve crosses zero, once the matrices have mixed the
// channels -- and the grey above which every channel's log clamps at maxEv.
constexpr f64 kBlackThreshold = 0.0002173449822978082;
constexpr f64 kSaturationThreshold = 16.292520680208433;

// The budget against the 50-digit reference, absolute, in linear display
// light (which runs 0 to 1). Measured before it was written down, with the
// budget set to zero so every point reported its distance: 56 of the 63
// channels are off at all, the worst by 5.22e-15, at a grey of 11.52 -- the
// bright end, where the polynomial's terms, some forty times the result, cancel
// down to a value near one, and multiply the rounding of doubles by about that.
// The budget is twice the measurement, the project's rule for a reference
// budget (register decisions 54 and 76).
constexpr f64 kReferenceBudget = 1e-14;

[[nodiscard]] std::array<f64, 3> channels(const DisplayRgb& rgb) {
    return {{rgb.red.value(), rgb.green.value(), rgb.blue.value()}};
}

[[nodiscard]] std::array<f64, 3> tonemapGrey(f64 grey) {
    return channels(agxTonemap({.red = grey, .green = grey, .blue = grey}));
}

[[nodiscard]] bool isBlack(const std::array<f64, 3>& rgb) {
    return std::ranges::all_of(rgb, [](f64 c) noexcept { return bitsOf(c) == bitsOf(0.0); });
}

} // namespace

TEST_CASE("AgX matches the reference at every point, to the rounding of doubles") {
    f64 worst = 0.0;
    for (const Reference& reference : kReferences) {
        const std::array<f64, 3> got = channels(agxTonemap(reference.input));
        const std::array<f64, 3> expected{
            {reference.expected.red, reference.expected.green, reference.expected.blue},
        };
        for (std::size_t c = 0; c < got.size(); ++c) {
            const f64 error = absOf(got.at(c) - expected.at(c));
            worst = std::max(worst, error);
            CAPTURE(reference.input.red,
                    reference.input.green,
                    reference.input.blue,
                    c,
                    expected.at(c),
                    got.at(c),
                    error);
            CHECK(error <= kReferenceBudget);
        }
    }
    CAPTURE(worst);
    CHECK(worst <= kReferenceBudget);
}

TEST_CASE("black is exactly black, and so is everything below the curve's zero") {
    CHECK(isBlack(tonemapGrey(0.0)));
    CHECK(isBlack(tonemapGrey(1.0e-30)));
    CHECK(isBlack(tonemapGrey(kBlackThreshold * (1.0 - 1e-9))));
    // Just above it, light appears -- so the threshold is where the reference
    // puts it, not merely somewhere below it.
    CHECK(!isBlack(tonemapGrey(kBlackThreshold * (1.0 + 1e-6))));
    // A negative channel is no light at all, as Sobotka's configuration clamps it.
    CHECK(isBlack(channels(agxTonemap({.red = -1.0, .green = -0.5, .blue = -1e300}))));
}

TEST_CASE("grey rises strictly inside AgX's range, and never falls across twelve decades") {
    // 2,000 points per decade from 1e-6 to 1e6. Violations are counted and
    // the first one kept, so a failure names where it happened.
    constexpr int kPerDecade = 2'000;
    constexpr int kDecades = 12;
    std::array<f64, 3> previous = tonemapGrey(1e-6);
    int falls = 0;
    int flatInside = 0;
    int stepsInside = 0;
    f64 firstViolation = 0.0;
    for (int i = 1; i <= kPerDecade * kDecades; ++i) {
        const f64 grey = std::pow(10.0, -6.0 + (static_cast<f64>(i) / kPerDecade));
        const std::array<f64, 3> current = tonemapGrey(grey);
        const bool inside =
            grey > kBlackThreshold * (1.0 + 1e-6) && grey < kSaturationThreshold * (1.0 - 1e-6);
        const bool fell = !std::ranges::equal(current, previous, std::greater_equal<>{});
        const bool flat = inside && !std::ranges::equal(current, previous, std::greater<>{});
        if ((fell || flat) && falls + flatInside == 0) firstViolation = grey;
        falls += fell ? 1 : 0;
        flatInside += flat ? 1 : 0;
        stepsInside += inside ? 1 : 0;
        previous = current;
    }
    CAPTURE(firstViolation);
    CHECK(falls == 0);
    CHECK(flatInside == 0);
    // Not vacuous: the grid does visit the strict range, all 4.88 decades of it.
    CHECK(stepsInside > 9'700);
}

TEST_CASE("a large input saturates, and never wraps or becomes NaN") {
    const std::array<f64, 3> top = tonemapGrey(1.0e12);
    const std::array<f64, 5> huges{
        {
            kSaturationThreshold * (1.0 + 1e-6),
            1.0e6,
            1.0e300,
            std::numeric_limits<f64>::max(),
            std::numeric_limits<f64>::infinity(),
        },
    };
    for (const f64 huge : huges) {
        const std::array<f64, 3> got = tonemapGrey(huge);
        for (std::size_t c = 0; c < got.size(); ++c) {
            CAPTURE(huge, c, got.at(c), top.at(c));
            CHECK(bitsOf(got.at(c)) == bitsOf(top.at(c)));
            CHECK(got.at(c) <= 1.0);
        }
    }
    // One channel huge and the others dark is still a finite colour.
    const std::array<f64, 3> red = channels(
        agxTonemap({.red = std::numeric_limits<f64>::infinity(), .green = 0.0, .blue = 0.0}));
    CHECK(std::ranges::all_of(red,
                              [](f64 c) noexcept { return isFinite(c) && c >= 0.0 && c <= 1.0; }));
}

TEST_CASE("mid-grey reaches the display as code 128") {
    // 0.18, exposed, is what a metered mid-grey card lands on; AgX puts it at
    // 0.2145 linear, and the sRGB encode puts that on 128 of 255 -- the
    // display's own middle. The chain in the order the shader runs it.
    const DisplayRgb display = agxTonemap({.red = 0.18, .green = 0.18, .blue = 0.18});
    for (const LinearValue channel : {display.red, display.green, display.blue}) {
        const f64 encoded = srgbEncode(channel).value();
        CAPTURE(channel.value(), encoded);
        CHECK(std::lround(encoded * 255.0) == 128);
    }
}

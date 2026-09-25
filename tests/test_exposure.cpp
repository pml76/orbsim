//
// Tests for view/Exposure.hpp: the camera settings, the exposure relations and
// the radiometric-to-photometric line (M1-15; ADR 0014).
//
// **Where the expected numbers come from.** Not from this code.
// scripts/tonemap-reference.py evaluates ISO 2720's exposure value and ISO
// 12232's saturation-based exposure in 50-digit decimal arithmetic at each
// input's exact binary value, and its output is pasted below unedited. The
// task's headline case is also worked by hand in the test, from a factorisation
// rather than from the formula the code uses.
//
// **The budget is the task's: exact to 1e-12** against the reference, which
// is loose -- the worst measured was one unit in the last place, 1.8e-15 --
// and deliberately so, because it is the task's stated number and a stop is a
// photographer's unit: 1e-12 of one is nothing a picture can show. The stop
// relations are asserted to the same 1e-12 rather than "exactly", because
// exactly is not what floating point gives: halving the shutter time raises
// the computed EV by exactly 1 in only about 90 % of cases, measured over a
// sweep, and by 1 plus or minus one unit in the last place in the rest. That
// is the rounding of log2, not a wrong exponent -- a wrong exponent is off by
// a whole stop or by a factor, which is what these cases catch.
//
#include "core/Scalar.hpp"
#include "core/Units.hpp"
#include "view/Exposure.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <mp-units/framework.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

using namespace orb;
using namespace orb::view;

namespace {

// The task's error budget for the exposure relations.
constexpr f64 kBudget = 1e-12;

// What a camera is set to, as the three numbers a photographer would say. A
// struct rather than three parameters, because three adjacent doubles are the
// swappable parameters non-negotiable 1 forbids -- in a test as anywhere.
struct Setting {
    f64 fNumber{};
    f64 shutterSeconds{};
    f64 speed{};
};

[[nodiscard]] CameraSettings settings(const Setting& setting) {
    const auto aperture = Aperture::from(setting.fNumber);
    const auto shutter = ShutterTime::from(Seconds{setting.shutterSeconds});
    const auto iso = Iso::from(setting.speed);
    REQUIRE(aperture.has_value());
    REQUIRE(shutter.has_value());
    REQUIRE(iso.has_value());
    return CameraSettings{.aperture = *aperture, .shutterTime = *shutter, .iso = *iso};
}

// One row from scripts/tonemap-reference.py: three settings and EV100.
struct Ev100Reference {
    Setting setting;
    f64 expected{};
};

constexpr std::array<Ev100Reference, 7> kEv100References{
    {
        Ev100Reference{
            .setting = {.fNumber = 16.0, .shutterSeconds = 0.008, .speed = 100.0},
            .expected = 14.965784284662087,
        },
        Ev100Reference{
            .setting = {.fNumber = 16.0, .shutterSeconds = 0.008, .speed = 400.0},
            .expected = 12.965784284662087,
        },
        Ev100Reference{
            .setting = {.fNumber = 16.0, .shutterSeconds = 0.008, .speed = 50.0},
            .expected = 15.965784284662087,
        },
        Ev100Reference{
            .setting = {.fNumber = 5.6, .shutterSeconds = 0.016666666666666666, .speed = 100.0},
            .expected = 10.877744249949002,
        },
        Ev100Reference{
            .setting = {.fNumber = 2.8, .shutterSeconds = 0.03333333333333333, .speed = 1600.0},
            .expected = 3.877744249949002,
        },
        Ev100Reference{
            .setting = {.fNumber = 1.0, .shutterSeconds = 1.0, .speed = 100.0},
            .expected = 0.0,
        },
        Ev100Reference{
            .setting = {.fNumber = 1.4, .shutterSeconds = 30.0, .speed = 3200.0},
            .expected = -8.936036941268036,
        },
    },
};

// One row from the same script: EV100 and the exposure factor, per cd/m^2.
struct FactorReference {
    f64 ev100{};
    f64 expected{};
};

constexpr std::array<FactorReference, 4> kFactorReferences{
    {
        FactorReference{.ev100 = 0.0, .expected = 0.8333333333333334},
        FactorReference{.ev100 = 14.965784284662087, .expected = 2.604166666666667e-05},
        FactorReference{.ev100 = -2.0, .expected = 3.3333333333333335},
        FactorReference{.ev100 = 7.5, .expected = 0.004603559773349919},
    },
};

// The same script's value for a Lambertian patch of albedo 0.3 normal to the
// Sun at 1 AU -- 0.3 * 1361 / pi W/(m^2 sr), M1-18's radiance -- exposed at
// f/16, 1/125 s, ISO 100 with sunlight's efficacy.
constexpr f64 kSunlitPatchExposed = 0.33480610330857674;

// A result and what it should be, by name, so that the two cannot be handed
// over the wrong way round (non-negotiable 1).
struct Compared {
    f64 got{};
    f64 expected{};
};

[[nodiscard]] f64 relativeError(Compared pair) {
    return absOf(pair.got - pair.expected) / absOf(pair.expected);
}

// Every kind of value a setting must refuse: both zeros, a negative, a NaN,
// and both infinities.
constexpr std::array<f64, 6> kNotUsable{
    {
        0.0,
        -0.0,
        -1.0,
        std::numeric_limits<f64>::quiet_NaN(),
        std::numeric_limits<f64>::infinity(),
        -std::numeric_limits<f64>::infinity(),
    },
};

// f/16, 1/125 s, ISO 100: the task's worked example, and "sunny 16".
constexpr Setting kSunny16{.fNumber = 16.0, .shutterSeconds = 1.0 / 125.0, .speed = 100.0};

} // namespace

TEST_CASE("f/16 at 1/125 s and ISO 100 is EV100 14.97, worked by hand") {
    // N^2 / t = 256 * 125 = 32000 = 2^8 * 5^3, so EV100 = 8 + 3 log2(5).
    // log2(5) = 2.321928094887362347870... (to 21 digits; any table of
    // logarithms), which is the one number here that is not arithmetic.
    constexpr f64 kLog2Of5 = 2.321928094887362347870;
    const f64 worked = 8.0 + (3.0 * kLog2Of5);
    const f64 got = exposureValue100(settings(kSunny16)).stops();
    CAPTURE(worked, got);
    CHECK(absOf(got - worked) <= kBudget);
    // And to the two decimals every exposure table prints it with.
    CHECK(std::lround(got * 100.0) == 1497);
}

TEST_CASE("EV100 matches the reference at every point, including a fractional stop") {
    for (const Ev100Reference& reference : kEv100References) {
        const Setting& setting = reference.setting;
        const f64 got = exposureValue100(settings(setting)).stops();
        CAPTURE(setting.fNumber, setting.shutterSeconds, setting.speed, reference.expected, got);
        CHECK(absOf(got - reference.expected) <= kBudget);
    }
}

TEST_CASE("halving the shutter time raises EV by one stop, and doubling N by two") {
    // A fixed logarithmic grid over the settings a camera can plausibly take
    // -- f/0.7 to f/64, 10 us to 100 s, ISO 30 to 100 000, 40 steps each --
    // and the three relations that a wrong exponent anywhere in the formula
    // breaks. A grid rather than a random draw: it covers the space evenly and
    // needs no seed, as test_srgb.cpp argues for its own sweeps.
    constexpr int kSteps = 40;
    // The two ends of one setting's range, by name (non-negotiable 1).
    struct Range {
        f64 low{};
        f64 high{};
    };
    const auto along = [](int i, Range range) {
        return range.low * std::pow(range.high / range.low, static_cast<f64>(i) / (kSteps - 1));
    };
    const auto evAt = [](const Setting& setting) {
        return exposureValue100(settings(setting)).stops();
    };

    f64 worst = 0.0;
    for (int i = 0; i < kSteps; ++i) {
        for (int j = 0; j < kSteps; ++j) {
            for (int k = 0; k < kSteps; ++k) {
                const Setting base{
                    .fNumber = along(i, {.low = 0.7, .high = 64.0}),
                    .shutterSeconds = along(j, {.low = 1e-5, .high = 100.0}),
                    .speed = along(k, {.low = 30.0, .high = 1e5}),
                };
                Setting halvedShutter = base;
                halvedShutter.shutterSeconds /= 2.0;
                Setting doubledAperture = base;
                doubledAperture.fNumber *= 2.0;
                Setting doubledSpeed = base;
                doubledSpeed.speed *= 2.0;
                const f64 ev = evAt(base);
                worst = std::max({
                    worst,
                    absOf(evAt(halvedShutter) - ev - 1.0),
                    absOf(evAt(doubledAperture) - ev - 2.0),
                    absOf(evAt(doubledSpeed) - ev + 1.0),
                });
            }
        }
    }
    CAPTURE(worst);
    CHECK(worst <= kBudget);
}

TEST_CASE("the exposure factor matches the reference, and maps saturation to one") {
    for (const FactorReference& reference : kFactorReferences) {
        const f64 got = exposureFactor(ExposureValue100{reference.ev100}).value();
        CAPTURE(reference.ev100, reference.expected, got);
        CHECK(relativeError({.got = got, .expected = reference.expected}) <= kBudget);
    }
    // What the factor means: the luminance that just saturates the sensor,
    // (78 / (100 * 0.65)) * 2^EV100 = 1.2 * 2^EV100, exposes to exactly 1.
    for (const f64 ev : {-4.0, 0.0, 9.0, 14.965784284662087}) {
        const f64 saturating = 1.2 * std::exp2(ev);
        const f64 exposed = saturating * exposureFactor(ExposureValue100{ev}).value();
        CAPTURE(ev, exposed);
        CHECK(absOf(exposed - 1.0) <= kBudget);
    }
}

TEST_CASE("a sunlit patch of albedo 0.3 exposes at sunny 16 as the reference says") {
    // The radiance M1-18 will read back, computed here from its definition.
    const Radiance patch{0.3 * 1361.0 / std::numbers::pi};
    const PerRadiance exposure = radianceExposure(exposureValue100(settings(kSunny16)));
    const f64 exposed = Scalar<mp_units::one>{patch * exposure}.value();
    CAPTURE(exposed);
    CHECK(relativeError({.got = exposed, .expected = kSunlitPatchExposed}) <= kBudget);
    // And where that lands: 0.89 stops above a metered mid-grey's 0.18 --
    // bright, as a sunlit surface is, and far inside AgX's 6.5 stops of
    // headroom. A 179 lm/W efficacy would have put it 1.75 stops above.
    CHECK(std::log2(exposed / 0.18) > 0.85);
    CHECK(std::log2(exposed / 0.18) < 0.93);
}

// has_value() first, always, in the three cases below: error() on an expected
// that holds a value is undefined, and compares equal to the zero enumerator --
// which is exactly InvalidAperture (VERIFICATION.md rule 23).

TEST_CASE("an f-number that is not finite and positive is refused by name") {
    for (const f64 bad : kNotUsable) {
        CAPTURE(bad);
        const auto aperture = Aperture::from(bad);
        REQUIRE(!aperture.has_value());
        CHECK(aperture.error() == ExposureError::InvalidAperture);
    }
}

TEST_CASE("a shutter time that is not finite and positive is refused by name") {
    for (const f64 bad : kNotUsable) {
        CAPTURE(bad);
        const auto shutter = ShutterTime::from(Seconds{bad});
        REQUIRE(!shutter.has_value());
        CHECK(shutter.error() == ExposureError::InvalidShutterTime);
    }
}

TEST_CASE("an ISO speed that is not finite and positive is refused by name") {
    for (const f64 bad : kNotUsable) {
        CAPTURE(bad);
        const auto iso = Iso::from(bad);
        REQUIRE(!iso.has_value());
        CHECK(iso.error() == ExposureError::InvalidSensitivity);
    }
}

TEST_CASE("the smallest positive setting is accepted: the bound is zero, not a threshold") {
    constexpr f64 kSmallest = std::numeric_limits<f64>::denorm_min();
    CHECK(Aperture::from(kSmallest).has_value());
    CHECK(ShutterTime::from(Seconds{kSmallest}).has_value());
    CHECK(Iso::from(kSmallest).has_value());
}

TEST_CASE("the shader's exposure is the nearest float to the double") {
    for (const f64 ev : {-8.0, 0.0, 7.5, 14.965784284662087, 20.0}) {
        const PerRadiance exposure = radianceExposure(ExposureValue100{ev});
        const f64 wide = exposure.value();
        const f32 narrow = toShaderExposure(exposure);
        CAPTURE(ev, wide, narrow);
        // Round to nearest: within half a float's unit in the last place,
        // which is 2^-24 of the value.
        CHECK(absOf(static_cast<f64>(narrow) - wide) <= wide * 0x1p-24);
    }
}

//
// Tests for astro/Sun.hpp: the geocentric Sun, and the irradiance it delivers
// (M1-08).
//
// Nothing here is checked against ERFA, which computes the thing under test
// (ADR 0016, VERIFICATION.md rule 23). The expected values come from four
// places, none of them this code:
//
//   * **JPL Horizons (DE441)**, through the M1-06 fixture, for the budget --
//     geometric vectors, no light-time and no aberration, or the comparison
//     would measure the correction instead of the code;
//   * **published apsis distances**, 0.9833 and 1.0167 AU, for a physical
//     claim the fixture's forty rows cannot force;
//   * **USNO's Earth's Seasons**, https://aa.usno.navy.mil/data/Earth_Seasons,
//     for the three equinox instants;
//   * **the inverse-square law and Kopp & Lean (2011)**, for the irradiance,
//     where the expected values are exact and the tolerance is zero.
//
// The budgets are register decisions 85 to 88, and each says where its number
// came from.
//
// **The sweeps here are exhaustive daily grids rather than seeded random
// draws**, which is why no seed appears (VERIFICATION.md rule 12). One day at a
// time across 2000-2050 is 18,262 steps and visits every phase of the year
// fifty times; a random sample of the same interval would cover less of it and
// reproduce worse.
//
#include "astro/EarthOrientation.hpp"
#include "astro/Sun.hpp"
#include "astro/Tdb.hpp"
#include "core/Math.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"
#include "tests/FixtureFile.hpp"
#include "tests/OrbitTestSupport.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <numbers>

using namespace orb;
using namespace orb::test;

namespace {

// --- the budgets -------------------------------------------------------------

// The code budget (register decision 85): the Sun against Horizons over
// 2000-2050, about twice the measured worst, which is decision 54's rule.
// Measured at the forty fixture epochs: 0.0085" of direction at JD 2464727.0
// and 2.14e-8 au of distance at JD 2452559.0.
//
// **The span is the fixture's.** eraEpv00's own worst over the wider 1900-2100
// is 0.016", looser than this budget, so widening the asserted span means
// re-measuring rather than pointing the test at more epochs.
constexpr Radians kDirectionBudget{0.02 * (std::numbers::pi_v<f64> / 648'000.0)}; // 0.02"
// In astronomical units, because that is the unit the budget is stated in and
// the one the comparison is made in. 5e-8 au is 7.48 km.
constexpr f64 kDistanceBudgetAu = 5e-8;

// The published apsis distances carry four decimals, and that is the tolerance
// (decision 87). Measured worst gaps: 5.64e-5 and 5.74e-5 au, so 1.8x margin.
constexpr f64 kPerihelionAu = 0.9833;
constexpr f64 kAphelionAu = 1.0167;
constexpr f64 kApsisBudgetAu = 1e-4;

// The daily angular step, two-sided so that a wrapped angle fails in either
// direction (decision 87). Measured 0.952940 to 1.019674 deg over 2000-2050,
// against two-body motion at e = 0.016709, which gives 1.019230 deg at
// perihelion and 0.953284 deg at aphelion; the 4e-4 deg residual is the
// Earth's wobble about the Earth-Moon barycentre, 6.44" over a 27.32-day
// month.
constexpr f64 kStepLowerDegrees = 0.952;
constexpr f64 kStepUpperDegrees = 1.020;

// The equinox (decision 86). The 0.01 deg is the stated budget; the 1e-3 deg
// is the sharper claim beside it, and is derived -- the published instants are
// rounded to the minute, +/-1.4e-4 deg at 2.74e-4 deg a minute, and the
// Earth's monthly wobble reaches about 7e-4 deg of declination. Measured worst
// distance from the prediction at the three instants: 3.13e-4 deg.
constexpr Tolerance kEquinoxBudget{0.01};
constexpr Tolerance kAberrationResidual{1e-3};

// The offset the omitted aberration leaves at the published equinox: the
// constant of aberration times the sine of the obliquity, because the equinox
// is published for the *apparent* Sun and this one is geometric. 20.49551" is
// the IAU 2009 constant of aberration; 23.4393 deg is the mean obliquity near
// J2000, to the four decimals this needs.
constexpr f64 kAberrationArcsec = 20.49551;
constexpr f64 kObliquityDegrees = 23.4393;

[[nodiscard]] f64 predictedEquinoxDeclinationDegrees() {
    return (kAberrationArcsec / 3600.0) * std::sin(toRadians(Degrees{kObliquityDegrees}).value());
}

// --- the published instants --------------------------------------------------

struct PublishedEquinox {
    std::int32_t year;
    std::int32_t month;
    std::int32_t day;
    std::int32_t hour;
    std::int32_t minute;
};

// USNO's Earth's Seasons, in UT. Three rather than one so that a single
// well-chosen instant cannot carry the case, and all three inside the
// leap-second table. USNO publishes UT; reading it as UTC costs at most 0.9 s,
// which is 4e-6 deg of declination -- four orders inside the sharper claim.
constexpr std::array<PublishedEquinox, 3> kMarchEquinoxes{
    {
        {.year = 2024, .month = 3, .day = 20, .hour = 3, .minute = 6},
        {.year = 2025, .month = 3, .day = 20, .hour = 9, .minute = 1},
        {.year = 2026, .month = 3, .day = 20, .hour = 14, .minute = 46},
    },
};

// The span eraEpv00 vouches for: 100 Julian years either side of J2000.0, both
// ends inside. Its own rule is |((date1 - 2451545.0) + date2)/365.25| <= 100,
// and these are the two Julian dates that satisfy it exactly.
constexpr f64 kFirstJulianDay = 2'415'020.0; // 1899-12-31T12:00 TDB
constexpr f64 kLastJulianDay = 2'488'070.0;  // 2100-01-01T12:00 TDB

// A millisecond, not a picosecond: eraEpv00 forms the date as days from
// J2000, where a double resolves 2^-37 day, about 0.63 us, so a picosecond
// outside the boundary reaches ERFA as exactly the boundary.
constexpr Seconds kJustOutside{1e-3};

// --- helpers -----------------------------------------------------------------

[[nodiscard]] TdbTime tdbAtJulianDay(f64 julianDay) {
    const auto instant = TdbTime::fromJulianDate(JulianDate{.day = julianDay, .fraction = 0.0});
    INFO(errorName(instant));
    REQUIRE(instant.has_value());
    return *instant;
}

// The date arrives as a CalendarDate rather than as three integers, so that a
// call site cannot transpose them: three adjacent std::int32_t parameters are
// exactly what bugprone-easily-swappable-parameters is for, and it reports
// them since SuppressParametersUsedTogether was switched off on 2026-09-20.
// CalendarDate rather than a local three-field struct, because a field-for-
// field copy of an existing type is what register decision 89 deleted.
// The time of day in it is not read; this is noon, which is what the name says.
[[nodiscard]] TdbTime tdbAtNoon(const CalendarDate& date) {
    const auto instant = TdbTime::fromCalendar(
        {.year = date.year, .month = date.month, .day = date.day, .hour = 12});
    INFO(errorName(instant));
    REQUIRE(instant.has_value());
    return *instant;
}

[[nodiscard]] Metres distanceAt(TdbTime at) {
    const auto distance = sunDistance(at);
    INFO(errorName(distance));
    REQUIRE(distance.has_value());
    return *distance;
}

[[nodiscard]] Position positionAt(TdbTime at) {
    const auto position = geocentricSunPosition(at);
    INFO(errorName(position));
    REQUIRE(position.has_value());
    return *position;
}

// One day, exactly: a whole number of picoseconds, so stepping by it
// accumulates without drift.
constexpr Seconds kOneDay{86'400.0};

// The extreme of the geocentric distance over a window, and the day it fell
// on. The window is chosen by the caller to hold exactly one apsis.
struct Apsis {
    f64 distanceAu{};
    std::int32_t month{};
    std::int32_t day{};
};

enum class Extreme : std::uint8_t { Nearest, Farthest };

// The two instants travel together, in a struct, because two adjacent
// TdbTime parameters transpose in silence -- I.24, which
// bugprone-easily-swappable-parameters is enabled to catch. A window with its
// ends the wrong way round would sweep nothing and report the sentinel.
struct Window {
    TdbTime from;
    TdbTime to;
};

[[nodiscard]] Apsis apsisWithin(const Window& window, Extreme which) {
    Apsis best{.distanceAu = (which == Extreme::Nearest) ? 9.9 : 0.0, .month = 0, .day = 0};
    REQUIRE(window.from < window.to);
    for (TdbTime at = window.from; at <= window.to; at = at + kOneDay) {
        const f64 au = distanceAt(at).value() / kAstronomicalUnit.value();
        const bool better =
            (which == Extreme::Nearest) ? (au < best.distanceAu) : (au > best.distanceAu);
        if (!better) continue;
        const auto calendar = at.toCalendar();
        INFO(errorName(calendar));
        REQUIRE(calendar.has_value());
        best = {.distanceAu = au, .month = calendar->month, .day = calendar->day};
    }
    return best;
}

} // namespace

// --- the budget, against data this project did not produce -------------------

// Catch2's macros make every assertion a branch, so a test case that sweeps
// anything exceeds the cognitive-complexity threshold that keeps production
// functions on one screen. The same suppression, for the same reason, as the
// seventy-one already in this project's suites.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the Sun is where Horizons says it is", "[sun][horizons]") {
    const auto fixture = readStateVectors(std::filesystem::path{dataDirectory()} / "horizons" /
                                          "sun-geocentric.txt");
    if (!fixture.has_value() && fixture.error().kind == FixtureErrorKind::FileNotFound) {
        SKIP("data/horizons/sun-geocentric.txt has not been generated on this machine: "
             "see data/horizons/README.md");
    }
    INFO(errorName(fixture) << " at line " << (fixture ? std::size_t{0} : fixture.error().line));
    REQUIRE(fixture.has_value());
    REQUIRE(fixture->states.size() == 40);

    Radians worstDirection{0.0};
    f64 worstDistanceAu = 0.0;

    for (const StateAtEpoch& state : fixture->states) {
        const JulianDate epoch = state.epoch.julianDate();
        CAPTURE(epoch.day, epoch.fraction);

        const Position sun = positionAt(state.epoch);
        const Radians offAxis = angleBetween(sun, state.position);
        const f64 distanceErrorAu =
            std::abs((length(sun) - length(state.position)).value() / kAstronomicalUnit.value());

        CAPTURE(toDegrees(offAxis).value() * 3600.0, distanceErrorAu);
        // Compared as values rather than as quantities: Catch2 decomposes the
        // expression to print both sides, and a Scalar is not a type it can
        // decompose. The units are checked by the types on the line above.
        REQUIRE(offAxis.value() <= kDirectionBudget.value());
        REQUIRE(distanceErrorAu <= kDistanceBudgetAu);

        worstDirection = std::max(worstDirection, offAxis);
        worstDistanceAu = std::max(worstDistanceAu, distanceErrorAu);
    }

    // Not vacuous: a reference that agreed to the resolution of a double would
    // mean the fixture had come out of eraEpv00 rather than out of Horizons.
    // eraEpv00's own claim against DE405 is kilometres, and 1e-11 au is 1.5 m.
    CAPTURE(toDegrees(worstDirection).value() * 3600.0, worstDistanceAu);
    REQUIRE(worstDistanceAu > 1e-11);
}

TEST_CASE("the distance is the length of the position", "[sun]") {
    // Decision 90: sunDistance is length(geocentricSunPosition(...)), one
    // series evaluation, so the two cannot disagree. Bit identity is the
    // claim, and it is made by name.
    for (const f64 julianDay : {2'451'545.0, 2'460'000.0, kFirstJulianDay, kLastJulianDay}) {
        CAPTURE(julianDay);
        const TdbTime at = tdbAtJulianDay(julianDay);
        REQUIRE(distanceAt(at).bitIdentical(length(positionAt(at))));
    }
}

// --- the physics the fixture's forty rows cannot force -----------------------

// Catch2's macros make every assertion a branch, so a test case that sweeps
// anything exceeds the cognitive-complexity threshold that keeps production
// functions on one screen. The same suppression, for the same reason, as the
// seventy-one already in this project's suites.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the apsides are where the published figures put them", "[sun]") {
    // **The window is not the calendar year** (decision 87). Perihelion sits
    // astride the turn of the year, so a calendar year holds two of them and
    // the deeper wins -- which puts the "annual minimum" on 31 December in
    // 2003 and 2047. These windows hold exactly one apsis each.
    f64 worstPerihelionGap = 0.0;
    f64 worstAphelionGap = 0.0;

    for (std::int32_t year = 2000; year <= 2049; ++year) {
        CAPTURE(year);

        const Apsis perihelion = apsisWithin(
            {
                .from = tdbAtNoon({.year = year - 1, .month = 10, .day = 1}),
                .to = tdbAtNoon({.year = year, .month = 3, .day = 31}),
            },
            Extreme::Nearest);
        const Apsis aphelion = apsisWithin(
            {
                .from = tdbAtNoon({.year = year, .month = 4, .day = 1}),
                .to = tdbAtNoon({.year = year, .month = 9, .day = 30}),
            },
            Extreme::Farthest);

        CAPTURE(perihelion.distanceAu, perihelion.month, perihelion.day);
        CAPTURE(aphelion.distanceAu, aphelion.month, aphelion.day);

        REQUIRE_THAT(perihelion.distanceAu, WithinAbsOf(kPerihelionAu, Tolerance{kApsisBudgetAu}));
        REQUIRE_THAT(aphelion.distanceAu, WithinAbsOf(kAphelionAu, Tolerance{kApsisBudgetAu}));

        // And near the right dates, which is the half of the claim a distance
        // alone does not make. Measured over these fifty years: 2-5 January
        // and 3-6 July.
        REQUIRE(perihelion.month == 1);
        REQUIRE(perihelion.day >= 1);
        REQUIRE(perihelion.day <= 8);
        REQUIRE(aphelion.month == 7);
        REQUIRE(aphelion.day >= 1);
        REQUIRE(aphelion.day <= 10);

        worstPerihelionGap =
            std::max(worstPerihelionGap, std::abs(perihelion.distanceAu - kPerihelionAu));
        worstAphelionGap = std::max(worstAphelionGap, std::abs(aphelion.distanceAu - kAphelionAu));
    }
    CAPTURE(worstPerihelionGap, worstAphelionGap);
}

TEST_CASE("the Sun's motion is continuous across fifty years", "[sun]") {
    // A wrapped angle handled wrongly shows up here as a discontinuity, and
    // the bound is two-sided so that it fails in either direction.
    const TdbTime start = tdbAtNoon({.year = 2000, .month = 1, .day = 1});
    Position previous = positionAt(start);
    Radians worstStep{0.0};
    Radians smallestStep{std::numbers::pi_v<f64>};

    TdbTime at = start;
    for (std::size_t step = 0; step < 18'262; ++step) {
        at = at + kOneDay;
        const Position now = positionAt(at);
        const Degrees moved = toDegrees(angleBetween(previous, now));

        const JulianDate epoch = at.julianDate();
        CAPTURE(epoch.day, moved.value());
        REQUIRE(moved.value() >= kStepLowerDegrees);
        REQUIRE(moved.value() <= kStepUpperDegrees);

        worstStep = std::max(worstStep, Radians{toRadians(moved)});
        smallestStep = std::min(smallestStep, Radians{toRadians(moved)});
        previous = now;
    }
    // The spread is the eccentricity, not noise: if these two were equal the
    // orbit being sampled would be a circle.
    CAPTURE(toDegrees(worstStep).value(), toDegrees(smallestStep).value());
    REQUIRE(toDegrees(worstStep).value() - toDegrees(smallestStep).value() > 0.05);
}

TEST_CASE("the March equinox puts the Sun on the equator of date", "[sun][equinox]") {
    // A sign error in the rotation fails this and passes several other tests,
    // which is why it is here (M1-07's intermediateFromInertial, decision 77).
    const f64 predicted = predictedEquinoxDeclinationDegrees();
    CAPTURE(predicted);

    for (const PublishedEquinox& equinox : kMarchEquinoxes) {
        CAPTURE(equinox.year, equinox.month, equinox.day, equinox.hour, equinox.minute);

        const auto utc = UtcTime::fromCalendar({
            .year = equinox.year,
            .month = equinox.month,
            .day = equinox.day,
            .hour = equinox.hour,
            .minute = equinox.minute,
        });
        INFO(errorName(utc));
        REQUIRE(utc.has_value());

        const auto tt = ttFromUtc(*utc);
        INFO(errorName(tt));
        REQUIRE(tt.has_value());

        const Position sun = positionAt(tdbFromTt(*tt));

        // Into the celestial intermediate frame, whose third axis is the pole
        // of date, so the third component is the declination's sine times the
        // radius. atan2 rather than asin: the declination here is a few
        // thousandths of a degree, where asin of a ratio loses what atan2 of
        // the two legs keeps.
        const Position ofDate = intermediateFromInertial(*tt).rotate(sun);
        const f64 inEquator = std::hypot(ofDate.x.value(), ofDate.y.value());
        const Degrees declination = toDegrees(Radians{std::atan2(ofDate.z.value(), inEquator)});

        CAPTURE(declination.value(), declination.value() - predicted);

        // The stated budget, as the plan has carried it since 2026-09-11.
        REQUIRE_THAT(declination.value(), WithinAbsOf(0.0, kEquinoxBudget));

        // And the sharper claim beside it (decision 86): the residual *is* the
        // aberration this geometric position omits, not merely something
        // small. A declination of zero fails this one.
        REQUIRE_THAT(declination.value(), WithinAbsOf(predicted, kAberrationResidual));
    }
}

// --- the irradiance ----------------------------------------------------------

TEST_CASE("irradiance follows the inverse-square law", "[sun][irradiance]") {
    // Exact, so the tolerance is zero. At half and twice the astronomical
    // unit the ratio is exactly 2 and exactly 1/2, both representable, so the
    // expected values are 1361 x 4 and 1361 / 4 to the last bit.
    REQUIRE_THAT(solarIrradianceAt(kAstronomicalUnit).value(), WithinAbsOf(1361.0, Tolerance{0.0}));
    REQUIRE_THAT(solarIrradianceAt(kAstronomicalUnit * 0.5).value(),
                 WithinAbsOf(5444.0, Tolerance{0.0}));
    REQUIRE_THAT(solarIrradianceAt(kAstronomicalUnit * 2.0).value(),
                 WithinAbsOf(340.25, Tolerance{0.0}));

    // The falloff across the real orbit, which is what M1-18 will expose for:
    // 6.9% between perihelion and aphelion.
    const f64 atPerihelion = solarIrradianceAt(kAstronomicalUnit * kPerihelionAu).value();
    const f64 atAphelion = solarIrradianceAt(kAstronomicalUnit * kAphelionAu).value();
    CAPTURE(atPerihelion, atAphelion);
    REQUIRE_THAT(atPerihelion / atAphelion, WithinAbsOf(1.0691, Tolerance{1e-4}));
}

// --- the range, by name, at both ends ----------------------------------------

// Catch2's macros make every assertion a branch, so a test case that sweeps
// anything exceeds the cognitive-complexity threshold that keeps production
// functions on one screen. The same suppression, for the same reason, as the
// seventy-one already in this project's suites.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("a date outside ERFA's span is reported by name", "[sun]") {
    // The boundary is ERFA's own status, not a second copy of its rule
    // (decision 31), so these cases are what pins it.
    for (const f64 julianDay : {kFirstJulianDay, kLastJulianDay}) {
        CAPTURE(julianDay);
        const TdbTime edge = tdbAtJulianDay(julianDay);

        // Both ends are inside.
        REQUIRE(geocentricSunPosition(edge).has_value());
        REQUIRE(sunDistance(edge).has_value());

        const bool isFirst = julianDay < 2'451'545.0;
        const TdbTime inside = isFirst ? edge + kJustOutside : edge - kJustOutside;
        const TdbTime outside = isFirst ? edge - kJustOutside : edge + kJustOutside;

        REQUIRE(geocentricSunPosition(inside).has_value());
        REQUIRE(sunDistance(inside).has_value());

        // And a millisecond beyond, both functions report the same name.
        const auto position = geocentricSunPosition(outside);
        const auto distance = sunDistance(outside);
        REQUIRE_FALSE(position.has_value());
        REQUIRE_FALSE(distance.has_value());
        REQUIRE(position.error() == EphemerisError::OutsideEphemerisRange);
        REQUIRE(distance.error() == EphemerisError::OutsideEphemerisRange);
        REQUIRE(describe(position.error()) == describe(EphemerisError::OutsideEphemerisRange));
    }
}

TEST_CASE("a date in mid-2100 is outside the span", "[sun]") {
    // Asked for because "1900-2100" invites the opposite assumption: the span
    // ends on 1 January 2100, not at the end of it.
    const TdbTime midCentury = tdbAtNoon({.year = 2100, .month = 7, .day = 1});
    const auto position = geocentricSunPosition(midCentury);
    REQUIRE_FALSE(position.has_value());
    REQUIRE(position.error() == EphemerisError::OutsideEphemerisRange);
}

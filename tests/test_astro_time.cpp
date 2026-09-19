//
// Tests for astro/Tdb.hpp: TT <-> TDB, which ERFA's eraDtdb computes (M1-05).
//
// Nothing here is checked against ERFA, which is what is under test (ADR 0016,
// VERIFICATION.md rule 23). The expected values come from three places, each
// independent of it:
//
//   * **Skyfield 1.55's tdb_minus_tt**, USNO Circular 179 eq. 2.6 (Kaplan
//     2005): seven terms of Fairhead & Bretagnon (1990), written independently
//     of SOFA, committed as data/skyfield/tdb-minus-tt.txt (register decisions
//     53 to 55). The budget is 20 us, twice that series' own distance from the
//     full one;
//   * **the physics**: the annual term's amplitude worked out from the Kepler
//     problem with JPL's mean elements for the Earth-Moon barycentre, and where
//     it crosses zero from USNO's mean anomaly of the Sun (decision 65);
//   * **integer picoseconds**, for the round trip (decision 57).
//
#include "astro/Tdb.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"
#include "tests/FixtureFile.hpp"
#include "tests/OrbitTestSupport.hpp"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace orb;
using namespace orb::test;

// Catch2 prints an unknown type as "{?}"; an instant prints as its two stored
// numbers, as in tests/test_time.cpp. Exempt from gcc's -Wabi-tag for the
// reason given on WithinAbsOf::describe() in tests/OrbitTestSupport.hpp: the
// std::string is Catch2's.
namespace Catch {

template <orb::TimeScale Scale> struct StringMaker<orb::TimePoint<Scale>> {
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wabi-tag"
#endif
    [[nodiscard]] static std::string convert(const orb::TimePoint<Scale>& value) {
        return std::format(
            "(MJD {:.17g}, {} ps)", value.modifiedJulianDay(), value.picosecondOfDay());
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
};

} // namespace Catch

// A note on the readability-function-cognitive-complexity suppressions below:
// Catch2's REQUIRE expands to a do-while around a try/catch, so a case scores
// about three points per assertion whether or not it branches. Ruled by the
// project owner on 2026-09-09 -- see tests/test_orbit_scales.cpp -- one
// suppression per function, and only where the check fires.

namespace {

// One day and one second in picoseconds, typed here rather than taken from
// core/Time.hpp, so that the header's constants are checked against numbers
// written independently.
constexpr std::int64_t kDay = 86'400'000'000'000'000;
constexpr f64 kSecondsPerPicosecond = 1e-12;
static_assert(kPicosecondsPerDay == kDay);

// The TDB - TT budget: 20 us, twice the 9.28 us by which the reference series
// itself differs from the full one over 1900-2100 (register decision 54).
constexpr Tolerance kBudget{20e-6};

// The round-trip budget: one rounding to the nearest picosecond each way
// (decision 57).
constexpr std::int64_t kRoundTripBudgetPicoseconds = 1;

// How far a TDB instant is ahead of a TT one, exactly, in picoseconds. The two
// days differ by at most one, so the product stays far inside an int64.
[[nodiscard]] std::int64_t picosecondsAhead(TdbTime tdb, TtTime tt) {
    const f64 days = tdb.modifiedJulianDay() - tt.modifiedJulianDay();
    return (static_cast<std::int64_t>(days) * kDay) +
           (tdb.picosecondOfDay() - tt.picosecondOfDay());
}

// Two instants on one scale, a few picoseconds apart at most, exactly.
template <TimeScale Scale>
[[nodiscard]] std::int64_t picosecondsBetween(const TimePoint<Scale>& later,
                                              const TimePoint<Scale>& earlier) {
    const f64 days = later.modifiedJulianDay() - earlier.modifiedJulianDay();
    return (static_cast<std::int64_t>(days) * kDay) +
           (later.picosecondOfDay() - earlier.picosecondOfDay());
}

// TDB - TT at a TT instant, as the conversion under test applies it.
[[nodiscard]] f64 tdbMinusTtAt(TtTime tt) {
    return static_cast<f64>(picosecondsAhead(tdbFromTt(tt), tt)) * kSecondsPerPicosecond;
}

// A seeded sweep, with the seed written down (VERIFICATION.md rule 12).
constexpr std::uint64_t kSweepSeed = 20260919ULL; // the date this suite was written
constexpr std::size_t kSweepCases = 10'000;

// 1990-01-01 and 2050-01-01 as Modified Julian Days: the task's sweep.
constexpr std::int64_t kSweepFirstMjd = 47'892;
constexpr std::int64_t kSweepLastMjd = 69'807;

} // namespace

// --- the type system ------------------------------------------------------------

static_assert(std::is_same_v<decltype(tdbFromTt(std::declval<TtTime>())), TdbTime>,
              "TT to TDB cannot fail, and says so");
static_assert(std::is_same_v<decltype(ttFromTdb(std::declval<TdbTime>())), TtTime>);
static_assert(noexcept(tdbFromTt(std::declval<TtTime>())) &&
              noexcept(ttFromTdb(std::declval<TdbTime>())));

// --- against the independent reference -------------------------------------------

// Every row of the committed fixture: ERFA's TDB - TT, as the conversion
// applies it, within 20 us of Skyfield's. The fixture's epochs are TDB, which
// is the argument Skyfield's function takes, so the conversion checked is
// ttFromTdb -- the direction that evaluates the series at its own argument.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("TDB - TT is within 20 us of Circular 179 over 1900-2100", "[astro][tdb]") {
    const auto fixture =
        readTdbMinusTt(std::filesystem::path{dataDirectory()} / "skyfield" / "tdb-minus-tt.txt");
    INFO(errorName(fixture) << " at line " << (fixture ? std::size_t{0} : fixture.error().line));
    REQUIRE(fixture.has_value());
    REQUIRE(fixture->rows.size() == 1975);

    f64 worst = 0.0;
    for (const TdbMinusTtAtEpoch& row : fixture->rows) {
        const TtTime tt = ttFromTdb(row.epoch);
        const f64 ours = static_cast<f64>(picosecondsAhead(row.epoch, tt)) * kSecondsPerPicosecond;
        const f64 difference = absOf(ours - row.tdbMinusTt.value());
        CAPTURE(row.epoch, ours, row.tdbMinusTt.value(), difference);
        REQUIRE(difference <= kBudget.value());
        worst = std::max(worst, difference);
    }
    // Not vacuous. The full series and the seven-term one genuinely differ, by
    // 9.28 us at worst, so a comparison that came out near zero everywhere
    // would mean the reference had been computed by the code under test --
    // the agreement between ERFA and ERFA that proves nothing.
    CAPTURE(worst);
    REQUIRE(worst > 1e-6);
}

// --- against the physics ------------------------------------------------------------

namespace {

// The annual term's amplitude is 2 e sqrt(GM a) / c^2, from the Kepler problem:
// the periodic part of the integral of (v^2/2 + GM/r) / c^2 dt, which is what
// TT loses to TDB. With the energy integral v^2/2 - GM/r = -GM/2a, that
// integrand is 2GM/r - GM/2a; with r = a(1 - e cos E) and dt = (1 - e cos E)
// dE / n, the first part integrates to (2GM / a n) E = (2GM / a n)(M + e sin E),
// whose periodic part is 2 e sqrt(GM a) sin E once n = sqrt(GM / a^3). Divided
// by c^2, that is TDB - TT, zero at perihelion and aphelion and positive
// between them.
//
// The elements are the Earth-Moon barycentre's, from E. M. Standish and J. G.
// Williams, "Approximate Positions of the Planets", JPL Solar System Dynamics,
// Table 1 (valid 1800-2050): a = 1.00000261 + 0.00000562 T au and
// e = 0.01671123 - 0.00004392 T, T in Julian centuries from J2000.0 -- read
// from the page on 2026-09-19 and evaluated at 2025-01-01, T = 0.25, the middle
// of the two years sampled below.
constexpr f64 kCenturiesFromJ2000 = 0.25;
constexpr f64 kSemiMajorAxisAu = 1.00000261 + (0.00000562 * kCenturiesFromJ2000);
constexpr f64 kEccentricity = 0.01671123 - (0.00004392 * kCenturiesFromJ2000);
constexpr f64 kAstronomicalUnitMetres = 149'597'870'700.0; // IAU 2012 Resolution B2, exact
constexpr f64 kSpeedOfLight = 299'792'458.0;               // m/s, exact: the SI metre

[[nodiscard]] f64 annualAmplitudeSeconds() {
    const f64 a = kSemiMajorAxisAu * kAstronomicalUnitMetres;
    return 2.0 * kEccentricity * std::sqrt(kMuSun.value() * a) / (kSpeedOfLight * kSpeedOfLight);
}

// How far the extremes may lie from that amplitude, and the zero crossings from
// the anomaly where the annual term alone would cross: the terms the Kepler
// problem leaves out. In the reference series those are 22 + 14 + 5 + 5 + 2 us,
// plus 10 us times T, 2.5 us here, and the full series differs from that one by
// up to 9.3 us more: 60 us in all (register decision 65).
constexpr f64 kNeglectedTermsSeconds = 60e-6;

// The annual term's slope where it crosses zero, amplitude times 2 pi over the
// anomalistic year (365.2596 d), is 28.5 us a day, so 60 us moves a crossing by
// at most 2.1 days -- which is 2.07 degrees of mean anomaly.
constexpr f64 kCrossingToleranceDays = 2.1;

// The Sun's mean anomaly, from the US Naval Observatory's "Computing
// Approximate Solar Coordinates": g = 357.529 + 0.98560028 D degrees, with D
// the Julian date less 2451545.0. Good to about an arcminute within two
// centuries of 2000, which is under a minute of time here.
constexpr f64 kMeanAnomalyAtJ2000Degrees = 357.529;
constexpr f64 kMeanAnomalyDegreesPerDay = 0.98560028;

[[nodiscard]] f64 meanAnomalyDegrees(TtTime t) {
    const JulianDate jd = t.julianDate();
    const f64 days = (jd.day - 2'451'545.0) + jd.fraction;
    return std::fmod(kMeanAnomalyAtJ2000Degrees + (kMeanAnomalyDegreesPerDay * days), 360.0);
}

// The angle from g to a target anomaly, the short way round.
[[nodiscard]] f64 degreesFrom(f64 g, f64 target) {
    const f64 d = std::fmod(std::abs(g - target), 360.0);
    return std::min(d, 360.0 - d);
}

struct Crossing {
    TtTime at;
    f64 meanAnomaly;
    bool upward;
};

} // namespace

// Two years, 2024 and 2025, sampled hourly. The annual term peaks at the
// amplitude the Kepler problem gives, crosses zero upward at perihelion and
// downward at aphelion, and nowhere else -- which a wrong sign, a wrong unit
// or a wrong argument would each fail, and which the fixture cannot check.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the annual term peaks and crosses zero where the physics says", "[astro][tdb]") {
    const auto start = TtTime::fromCalendar({.year = 2024, .month = 1, .day = 1});
    REQUIRE(start.has_value());
    constexpr std::int64_t kHours = std::int64_t{366 + 365} * 24; // 2024 is a leap year
    constexpr f64 kHourSeconds = 3'600.0;

    f64 highest = -1.0;
    f64 lowest = 1.0;
    std::vector<Crossing> crossings;
    TtTime previousAt = *start;
    f64 previous = tdbMinusTtAt(previousAt);
    for (std::int64_t hour = 1; hour <= kHours; ++hour) {
        const TtTime at = *start + Seconds{kHourSeconds * static_cast<f64>(hour)};
        const f64 value = tdbMinusTtAt(at);
        highest = std::max(highest, value);
        lowest = std::min(lowest, value);
        if ((previous < 0.0) != (value < 0.0)) {
            // Linear between the two samples: the curve moves 1.2 us in an
            // hour there, and is straight to far better than that.
            const f64 fraction = previous / (previous - value);
            const TtTime crossing = previousAt + Seconds{kHourSeconds * fraction};
            crossings.push_back({
                .at = crossing,
                .meanAnomaly = meanAnomalyDegrees(crossing),
                .upward = value > previous,
            });
        }
        previousAt = at;
        previous = value;
    }

    const f64 amplitude = annualAmplitudeSeconds();
    CAPTURE(amplitude, highest, lowest);
    REQUIRE(absOf(highest - amplitude) <= kNeglectedTermsSeconds);
    REQUIRE(absOf(lowest + amplitude) <= kNeglectedTermsSeconds);

    // Twice a year, and only there: the other terms' slopes are two orders of
    // magnitude below the annual term's where it crosses zero, so they cannot
    // add a crossing of their own.
    REQUIRE(crossings.size() == 4);
    const f64 tolerance = kCrossingToleranceDays * kMeanAnomalyDegreesPerDay;
    for (const Crossing& c : crossings) {
        CAPTURE(c.at, c.meanAnomaly, c.upward);
        // Upward at perihelion, g = 0; downward at aphelion, g = 180.
        REQUIRE(degreesFrom(c.meanAnomaly, c.upward ? 0.0 : 180.0) <= tolerance);
    }
}

namespace {

// Kepler's equation, M = E - e sin E, solved to second order in e: substituting
// E = M + e sin E into itself gives E = M + e sin M + e^2 sin M cos M, which is
// M + e sin M + (e^2 / 2) sin 2M. What is left is of order e^3, 4.7e-6 rad here,
// and it changes too slowly to move a half-day difference by a nanosecond.
[[nodiscard]] f64 eccentricAnomalyRadians(TtTime t) {
    constexpr f64 kRadiansPerDegree = kPi / 180.0;
    const f64 m = meanAnomalyDegrees(t) * kRadiansPerDegree;
    return m + (kEccentricity * std::sin(m)) +
           (0.5 * kEccentricity * kEccentricity * std::sin(2.0 * m));
}

// How far TDB - TT may move in half a day beyond what the annual term moves
// (register decision 69): each neglected term by at most its amplitude, times
// its rate, times half a day. In the reference series that is 22 us at 575.34
// rad per century, 0.17 us; 14 us at 1256.62, 0.24 us; 5 us at 606.98, 0.04
// us; and under 0.03 us for the rest together. Beyond it, the Moon's monthly
// term, about 1.6 us at 0.23 rad a day, 0.18 us. About 0.7 us in all.
constexpr f64 kHalfDayToleranceSeconds = 1e-6;

} // namespace

// Within a day (register decision 69). Every epoch of the fixture is at 0h, and
// the annual term moves at most 28.5 us a day, so a conversion that handed ERFA
// the day but dropped the time of day would pass the budget and the crossings
// above. Twelve hours apart on one day, TDB - TT must change as the Kepler
// problem's annual term does -- A (sin E(noon) - sin E(midnight)) -- to within
// what the neglected terms can move in that time. Every fifth day of 2024-2025.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("TDB - TT changes within a day as the annual term does", "[astro][tdb]") {
    const auto start = TtTime::fromCalendar({.year = 2024, .month = 1, .day = 1});
    REQUIRE(start.has_value());
    constexpr auto kDays = std::int64_t{366 + 365};
    constexpr std::int64_t kEveryFifthDay = 5;
    constexpr f64 kDaySeconds = 86'400.0;

    const f64 amplitude = annualAmplitudeSeconds();
    f64 worst = 0.0;
    f64 largestChange = 0.0;
    for (std::int64_t day = 0; day < kDays; day += kEveryFifthDay) {
        const TtTime midnight = *start + Seconds{kDaySeconds * static_cast<f64>(day)};
        const TtTime noon = midnight + Seconds{kDaySeconds / 2.0};
        const f64 observed = tdbMinusTtAt(noon) - tdbMinusTtAt(midnight);
        const f64 expected = amplitude * (std::sin(eccentricAnomalyRadians(noon)) -
                                          std::sin(eccentricAnomalyRadians(midnight)));
        CAPTURE(midnight, observed, expected);
        REQUIRE(absOf(observed - expected) <= kHalfDayToleranceSeconds);
        worst = std::max(worst, absOf(observed - expected));
        largestChange = std::max(largestChange, absOf(expected));
    }
    // Not vacuous: at its steepest the annual term moves 14 us in half a day,
    // far past the tolerance, so a conversion blind to the time of day fails.
    CAPTURE(worst, largestChange);
    REQUIRE(largestChange > 10.0 * kHalfDayToleranceSeconds);
}

// --- the round trip -----------------------------------------------------------------

// Within a picosecond, both ways, over a seeded sweep of 1990-2050 (decision
// 57). Instants are drawn as a day and a fraction of it straight from the
// engine, as test_time.cpp draws its dates, so that the sweep is the same under
// every standard library.
//
// **And exact almost everywhere, which is what shows the series is evaluated at
// TDB** (register decision 68). The 1 ps budget cannot tell: evaluated at TT
// once, a TDB instant is out by up to 0.6 ps, and rounding keeps the round trip
// inside a picosecond either way. What differs is how often it is exact --
// measured on 2026-09-19 over 1,000,000 instants, every one with the
// fixed-point step, and 83% without it. So at least 99% must be exact. That is
// a rate rather than bit identity at every instant, deliberately: the
// conversion cannot be a bijection on a picosecond grid, and a library that
// rounds a sine differently may move a handful of instants across a rounding
// tie -- not the 1,700 that evaluating at TT would.
constexpr std::size_t kExactAtLeast = kSweepCases * 99 / 100;

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("TT to TDB and back is within 1 ps both ways over a seeded sweep", "[astro][tdb]") {
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
    std::mt19937_64 rng{kSweepSeed};
    const auto width = static_cast<std::uint64_t>(kSweepLastMjd - kSweepFirstMjd);
    std::size_t exactFromTt = 0;
    std::size_t exactFromTdb = 0;
    for (std::size_t i = 0; i < kSweepCases; ++i) {
        const auto mjd = kSweepFirstMjd + static_cast<std::int64_t>(rng() % width);
        const f64 fraction = static_cast<f64>(rng() >> 11U) * 0x1p-53;
        const JulianDate date{.day = kMjdZero + static_cast<f64>(mjd), .fraction = fraction};
        CAPTURE(kSweepSeed, i, mjd, fraction);

        const auto tt = TtTime::fromJulianDate(date);
        REQUIRE(tt.has_value());
        const TtTime ttBack = ttFromTdb(tdbFromTt(*tt));
        CAPTURE(*tt, ttBack);
        const std::int64_t ttError = picosecondsBetween(ttBack, *tt);
        REQUIRE(std::abs(ttError) <= kRoundTripBudgetPicoseconds);
        if (ttError == 0) ++exactFromTt;

        const auto tdb = TdbTime::fromJulianDate(date);
        REQUIRE(tdb.has_value());
        const TdbTime tdbBack = tdbFromTt(ttFromTdb(*tdb));
        CAPTURE(*tdb, tdbBack);
        const std::int64_t tdbError = picosecondsBetween(tdbBack, *tdb);
        REQUIRE(std::abs(tdbError) <= kRoundTripBudgetPicoseconds);
        if (tdbError == 0) ++exactFromTdb;
    }
    CAPTURE(exactFromTt, exactFromTdb, kExactAtLeast);
    REQUIRE(exactFromTt >= kExactAtLeast);
    REQUIRE(exactFromTdb >= kExactAtLeast);
}

// --- far from J2000 -------------------------------------------------------------------

// Converted, never refused, at both ends of the calendar (decision 58): the
// result is an ordinary instant, the term stays inside the bound the header
// states -- within 1.76 ms at six epochs when it was measured, and 2 ms here
// -- and the round trip still holds.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("TDB is converted, not refused, at both ends of the calendar", "[astro][tdb]") {
    for (const CalendarDate& date : {
             CalendarDate{.year = 1, .month = 1, .day = 1, .hour = 12},
             CalendarDate{.year = 9999, .month = 12, .day = 31, .hour = 12},
         }) {
        CAPTURE(date.year);
        const auto tt = TtTime::fromCalendar(date);
        REQUIRE(tt.has_value());
        const TdbTime tdb = tdbFromTt(*tt);
        const f64 ahead = static_cast<f64>(picosecondsAhead(tdb, *tt)) * kSecondsPerPicosecond;
        CAPTURE(ahead);
        REQUIRE(absOf(ahead) < 2e-3);
        REQUIRE(std::abs(picosecondsBetween(ttFromTdb(tdb), *tt)) <= kRoundTripBudgetPicoseconds);
    }
}

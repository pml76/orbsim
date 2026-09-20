//
// Tests for astro/EarthOrientation.hpp: precession, nutation and the Earth
// rotation angle (M1-07).
//
// Nothing here is checked against ERFA, which computes the thing under test
// (ADR 0016, VERIFICATION.md rule 23). The expected values come from three
// places, none of them this code:
//
//   * **the definitions**, for the Earth rotation angle: IAU 2000 Resolution
//     B1.8's two constants, and the stellar day from the IERS list of useful
//     constants;
//   * **Skyfield 1.55**, for the rotation as a whole, through the committed
//     fixture data/skyfield/earth-orientation.txt. It builds the same rotation
//     the other correct way -- Greenwich apparent sidereal time applied to the
//     equinox-based bias-precession-nutation matrix, where ERFA's route is
//     CIO-based -- so the pairing M1-07's plan first carried, an equinox-based
//     matrix with the Earth rotation angle, is 1231" out against it. What the
//     fixture cannot check is the IAU 2000A series itself, which Skyfield
//     ports from NOVAS and NOVAS shares with SOFA: register decision 75
//     records that as the limit of this reference;
//   * **the structure of the rotation**, for the claims a reference cannot
//     make: that only UT1 turns the Earth, that the pole is the pole whether
//     or not the Earth has turned, and that the quaternion is the matrix.
//
// The budgets are register decisions 76 and 80, and each says where its number
// came from.
//
#include "astro/EarthOrientation.hpp"
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
#include <random>
#include <string>

using namespace orb;
using namespace orb::test;

namespace {

// --- the budgets -------------------------------------------------------------

// The code budget (register decision 76): the rotation against Skyfield,
// applied to unit vectors, over 1900-2100. About twice the measured worst,
// which is decision 54's rule -- 53.6 uas on this fixture, of which some
// 47 uas is the TIO locator s' that ERFA applies and the equinox route does
// not. At 0.1" -- the number the plan carried -- this test could not have seen
// an omitted frame bias (23.1 mas), IAU 2000B nutation (3.1 mas), IAU 2000
// precession (2.7 mas) or UT1 wrong by a millisecond (15 mas).
constexpr Radians kRotationBudget{0.1 * (std::numbers::pi_v<f64> / 648'000'000.0)}; // 0.1 mas

// The regression budget named after the error M1-07's plan carried: the
// equation of the origins is a rotation about the pole and nothing else, so
// the spin is where a mixed composition shows itself -- 1231" in 2026, growing
// by 46" a year.
constexpr Radians kSpinBudget{1.0 * (std::numbers::pi_v<f64> / 648'000'000.0)}; // 1 mas

// Derived tolerances (register decision 80), each measured before it was
// chosen: the quaternion's norm at one ulp, its reproduction of ERFA's matrix
// at 8.3e-16 over 450,000 matrices, and the two structural claims at 1.2e-15
// and 1.1e-16.
constexpr Tolerance kUnitQuaternion{1e-15};
constexpr Tolerance kReproducesMatrix{2e-15};
constexpr Tolerance kStructural{4e-15};

// ERA itself, to the resolution ERFA's own arithmetic reaches with the date
// split this wrapper uses: measured 2e-13 rad worst over 1900-2100, against a
// 60-digit evaluation of the defining formula.
constexpr Tolerance kEraResolution{2e-13};

// The rate test's tolerance. The arithmetic resolves about 5 ns of the period;
// 1e-7 s is twenty times that, and four orders tighter than the 1e-4 s the
// plan carried (decision 80).
constexpr Tolerance kStellarDayBudget{1e-7};

// --- the published constants -------------------------------------------------

// IAU 2000 Resolution B1.8, as the note to it states the Earth rotation angle:
// ERA = 2 pi (0.7790572732640 + 1.00273781191135448 Tu), Tu = JD(UT1) -
// 2451545.0. The first is the angle at J2000.0 in turns, the second the turns
// a UT1 day makes.
constexpr f64 kEraAtJ2000Turns = 0.7790572732640;
constexpr f64 kEraTurnsPerDay = 1.00273781191135448;
// The same angle in degrees, as M1-07 and the resolution both write it.
constexpr f64 kEraAtJ2000Degrees = 280.46061837504;

// The stellar day: IERS "useful constants", the period of one turn of ERA.
// **Not the sidereal day**, 86 164.090 530 832 88 s, which follows the
// precessing equinox -- the two differ by 8.4 ms, and the plan for M1-07
// carried the wrong one (ADR 0016).
constexpr f64 kStellarDaySeconds = 86'164.098903691;
constexpr std::int64_t kStellarDayPicoseconds = 86'164'098'903'691'000;

constexpr f64 kTwoPi = 2.0 * std::numbers::pi_v<f64>;
constexpr std::int64_t kPicosecondsPerDayHere = 86'400'000'000'000'000;

// --- helpers -----------------------------------------------------------------

// An instant from a day and a picosecond within it, on any scale, through the
// public Julian-date factory. What it actually built is read back from its
// stored parts, so nothing downstream depends on the rounding.
struct Stamp {
    std::int64_t mjd;
    std::int64_t picosecondOfDay;
};

template <TimeScale Scale> [[nodiscard]] TimePoint<Scale> instantAt(Stamp stamp) {
    const JulianDate date{
        .day = kMjdZero + static_cast<f64>(stamp.mjd),
        .fraction =
            static_cast<f64>(stamp.picosecondOfDay) / static_cast<f64>(kPicosecondsPerDayHere),
    };
    const auto instant = TimePoint<Scale>::fromJulianDate(date);
    INFO(errorName(instant));
    REQUIRE(instant.has_value());
    return *instant;
}

// An ordered pair of instants. A struct rather than two parameters because the
// difference below is signed, and two adjacent TimePoints of one scale
// transpose in silence -- which bugprone-easily-swappable-parameters reports
// since SuppressParametersUsedTogether was switched off on 2026-09-20.
template <TimeScale Scale> struct Interval {
    TimePoint<Scale> from;
    TimePoint<Scale> to;
};
// An explicit deduction guide, because gcc's -Wctad-maybe-unsupported reports
// class template argument deduction on a template that declares none: the
// warning exists to catch CTAD nobody designed for, and here it was designed
// for. clang does not report it, which is what the second compiler is for.
template <TimeScale Scale> Interval(TimePoint<Scale>, TimePoint<Scale>) -> Interval<Scale>;

// to - from in picoseconds, for two instants on the same scale a few days apart.
template <TimeScale Scale>
[[nodiscard]] std::int64_t picosecondsBetween(const Interval<Scale>& interval) {
    const auto days = static_cast<std::int64_t>(interval.to.modifiedJulianDay() -
                                                interval.from.modifiedJulianDay());
    return (days * kPicosecondsPerDayHere) +
           (interval.to.picosecondOfDay() - interval.from.picosecondOfDay());
}

// The angle between two rotations, applied to the three axes: the claim M1-07
// states as "applied to unit vectors".
[[nodiscard]] Radians worstAxisAngle(const Quat& q, const RotationMatrix& m) {
    const auto axes = std::to_array<Direction>({
        Direction{1, 0, 0},
        Direction{0, 1, 0},
        Direction{0, 0, 1},
    });
    Radians worst{0.0};
    for (std::size_t j = 0; j < axes.size(); ++j) {
        const Direction turned = q.rotate(axes.at(j));
        const Direction expected{
            m.rows.at(0).at(j),
            m.rows.at(1).at(j),
            m.rows.at(2).at(j),
        };
        worst = std::max(worst, angleBetween(turned, expected));
    }
    return worst;
}

// Two rotations, in the order the product below reads them. A struct because
// a * b^T is not b * a^T, and two adjacent RotationMatrix parameters transpose
// in silence (see the note on Interval above).
struct RotationPair {
    RotationMatrix left;
    RotationMatrix right;
};

// left * right^T: the rotation that takes what `right` produces to what `left`
// produces. Its axis and angle are how far apart the two rotations are, and in
// which sense.
[[nodiscard]] RotationMatrix timesTranspose(const RotationPair& pair) {
    const RotationMatrix& a = pair.left;
    const RotationMatrix& b = pair.right;
    RotationMatrix product{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            product.rows.at(i).at(j) = (a.rows.at(i).at(0) * b.rows.at(j).at(0)) +
                                       (a.rows.at(i).at(1) * b.rows.at(j).at(1)) +
                                       (a.rows.at(i).at(2) * b.rows.at(j).at(2));
        }
    }
    return product;
}

// The third row of a celestial-to-terrestrial matrix is the terrestrial pole,
// written in celestial coordinates.
[[nodiscard]] Direction poleOf(const RotationMatrix& m) {
    return Direction{m.rows.at(2).at(0), m.rows.at(2).at(1), m.rows.at(2).at(2)};
}

// A seeded sweep, with the seed written down (VERIFICATION.md rule 12).
constexpr std::uint64_t kSweepSeed = 20260920ULL; // the date this suite was written
constexpr std::size_t kSweepCases = 2'000;

// 1900-01-01 and 2100-01-01 as Modified Julian Days: the span the fixture
// covers and the budget is claimed over.
constexpr std::int64_t kFirstMjd = 15'020;
constexpr std::int64_t kLastMjd = 88'069;

// An inclusive range, so that the two bounds cannot transpose at a call site:
// reversed, `hi - lo + 1` is non-positive and the modulus below is undefined
// (see the note on Interval above).
struct InclusiveRange {
    std::int64_t lo{};
    std::int64_t hi{};
};

class Sampler {
public:
    [[nodiscard]] std::int64_t between(const InclusiveRange& range) {
        const auto width = static_cast<std::uint64_t>((range.hi - range.lo) + 1);
        return range.lo + static_cast<std::int64_t>(rng_() % width);
    }

private:
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed)
    std::mt19937_64 rng_{kSweepSeed};
};

[[nodiscard]] EarthOrientationFixture readTheFixture() {
    auto fixture = readEarthOrientation(std::filesystem::path{dataDirectory()} / "skyfield" /
                                        "earth-orientation.txt");
    INFO(errorName(fixture) << " at line " << (fixture ? std::size_t{0} : fixture.error().line));
    REQUIRE(fixture.has_value());
    REQUIRE(!fixture->rows.empty());
    return *fixture;
}

} // namespace

// The angle at J2000.0, which the resolution defines rather than measures.
// J2000.0 in UT1 is Julian date 2451545.0 -- the epoch of the formula, not the
// TT instant kJ2000 names.
TEST_CASE("the Earth rotation angle at J2000.0 is the published value", "[earth][era]") {
    const auto epoch = Ut1Time::fromJulianDate({.day = 2'451'545.0, .fraction = 0.0});
    REQUIRE(epoch.has_value());
    const Radians era = earthRotationAngle(*epoch);
    REQUIRE_THAT(era.value(), WithinAbsOf(kEraAtJ2000Turns * kTwoPi, kEraResolution));

    // The same number as the resolution's degrees, to the digits it prints.
    const f64 degrees = era.value() * (180.0 / std::numbers::pi_v<f64>);
    REQUIRE_THAT(degrees, WithinAbsOf(kEraAtJ2000Degrees, Tolerance{1e-11}));
    // And it is an angle in [0, 2 pi), not a count of turns.
    REQUIRE(era.value() >= 0.0);
    REQUIRE(era.value() < kTwoPi);
}

// One turn of the Earth takes the stellar day of UT1, and the test recovers
// that period from the angle rather than being told it. A wrong rate constant
// fails this; so does TT handed in where UT1 belongs, at the epoch below,
// because the two disagree by 69 s and the angle by 1041".
// Catch2 macro expansion, not written complexity.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("one turn of the Earth rotation angle takes the stellar day", "[earth][era]") {
    // Four epochs across the span, each at a different time of day.
    for (const Stamp start : {
             Stamp{.mjd = 15'020, .picosecondOfDay = 0},
             Stamp{.mjd = 51'544, .picosecondOfDay = 43'200'000'000'000'000},
             Stamp{.mjd = 60'000, .picosecondOfDay = 11'647'000'000'000'000},
             Stamp{.mjd = 88'069, .picosecondOfDay = 70'123'456'789'000'000},
         }) {
        const Ut1Time first = instantAt<TimeScale::Ut1>(start);
        const std::int64_t carried = start.picosecondOfDay + kStellarDayPicoseconds;
        const Ut1Time second = instantAt<TimeScale::Ut1>({
            .mjd = start.mjd + (carried / kPicosecondsPerDayHere),
            .picosecondOfDay = carried % kPicosecondsPerDayHere,
        });

        // What was actually built, in picoseconds, rather than what was asked
        // for: the Julian-date factory rounds, and this test does not care.
        const f64 elapsed =
            static_cast<f64>(picosecondsBetween(Interval{.from = first, .to = second})) /
            1e12; // seconds of UT1
        const f64 turned = earthRotationAngle(second).value() - earthRotationAngle(first).value();
        // Almost a whole turn: unwrap the remainder into (-pi, pi].
        const f64 remainder = turned - (kTwoPi * std::round(turned / kTwoPi));
        const f64 turns = 1.0 + (remainder / kTwoPi);
        const f64 period = elapsed / turns;
        CAPTURE(start.mjd, start.picosecondOfDay, elapsed, period);
        REQUIRE_THAT(period, WithinAbsOf(kStellarDaySeconds, kStellarDayBudget));

        // And the rate the definition states, read back the same way.
        REQUIRE_THAT(86'400.0 / period, WithinAbsOf(kEraTurnsPerDay, Tolerance{1e-12}));
    }
}

// The budget, against an implementation this project did not write and did not
// derive from the one under test.
// Catch2 macro expansion, not written complexity.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the rotation agrees with an independent implementation to 0.1 mas", "[earth][budget]") {
    const EarthOrientationFixture fixture = readTheFixture();
    REQUIRE(fixture.rows.size() == 1029);

    Radians worst{0.0};
    std::string worstAt;
    for (const RotationAtEpoch& row : fixture.rows) {
        const Quat ours = earthFixedFromInertial(row.tt, row.ut1);
        const Radians apart = worstAxisAngle(ours, row.celestialToTerrestrial);
        if (apart > worst) {
            worst = apart;
            worstAt = std::to_string(row.tt.modifiedJulianDay());
        }
        CAPTURE(row.tt, row.ut1, apart.value());
        REQUIRE(apart < kRotationBudget);
    }
    INFO("worst " << (worst.value() / (std::numbers::pi_v<f64> / 648'000'000'000.0))
                  << " uas at MJD " << worstAt);
    // Not vacuous: the two formulations differ by the TIO locator, tens of
    // microarcseconds, so a reference computed by ERFA itself would be far
    // closer than this and a budget of zero would be wrong.
    REQUIRE(worst.value() > 0.0);
}

// **A regression test named after the error M1-07's plan carried.** Composing
// the equinox-based matrix with the Earth rotation angle instead of sidereal
// time leaves the equation of the origins in the answer: a rotation about the
// pole, 1231" in 2026 and growing by 46" a year. The pole itself is untouched
// by that mistake, which is why the spin is where it must be measured.
// Catch2 macro expansion, not written complexity.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the composition is CIO-consistent", "[earth][budget]") {
    const EarthOrientationFixture fixture = readTheFixture();
    Radians worstSpin{0.0};
    Radians worstPole{0.0};
    for (const RotationAtEpoch& row : fixture.rows) {
        const RotationMatrix ours = earthFixedFromInertialMatrix(row.tt, row.ut1);
        // The rotation from Skyfield's answer to ours. Its axis says which
        // part of the frame the two disagree about.
        const RotationMatrix relative =
            timesTranspose({.left = ours, .right = row.celestialToTerrestrial});
        const Quat q = quaternionFrom(relative);
        // For an angle this small the rotation vector is 2 * the vector part,
        // and its z-component is the turn about the pole.
        const Radians spin{2.0 * std::abs(q.z)};
        const Radians tilt{2.0 * std::hypot(q.x, q.y)};
        CAPTURE(row.tt, spin.value(), tilt.value());
        REQUIRE(spin < kSpinBudget);
        worstSpin = std::max(worstSpin, spin);
        worstPole = std::max(worstPole, tilt);
    }
    INFO("worst spin " << worstSpin.value() << " rad, worst pole " << worstPole.value() << " rad");
    REQUIRE(worstSpin.value() > 0.0);
}

// Only UT1 turns the Earth. With TT held, moving UT1 turns the frame about the
// terrestrial pole by exactly the change in the Earth rotation angle, and
// leaves the pole where it was.
// Catch2 macro expansion, not written complexity.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("only UT1 turns the Earth", "[earth][structure]") {
    Sampler sampler;
    for (std::size_t i = 0; i < 200; ++i) {
        const auto mjd = sampler.between({.lo = kFirstMjd, .hi = kLastMjd});
        const TtTime tt = instantAt<TimeScale::Tt>({
            .mjd = mjd,
            .picosecondOfDay = sampler.between({.lo = 0, .hi = kPicosecondsPerDayHere - 1}),
        });
        const Ut1Time first = instantAt<TimeScale::Ut1>({
            .mjd = mjd,
            .picosecondOfDay = sampler.between({.lo = 0, .hi = kPicosecondsPerDayHere - 1}),
        });
        const Ut1Time second = instantAt<TimeScale::Ut1>({
            .mjd = mjd,
            .picosecondOfDay = sampler.between({.lo = 0, .hi = kPicosecondsPerDayHere - 1}),
        });
        CAPTURE(kSweepSeed, i, tt, first, second);

        const RotationMatrix a = earthFixedFromInertialMatrix(tt, first);
        const RotationMatrix b = earthFixedFromInertialMatrix(tt, second);

        // The pole is the same, to the last bits: nothing about it depends on
        // UT1.
        REQUIRE(angleBetween(poleOf(a), poleOf(b)).value() < kStructural.value());

        // And the frame has turned about it by exactly the change in ERA. The
        // relative rotation's vector part is along the pole, and twice its
        // length is the angle.
        const Quat q = quaternionFrom(timesTranspose({.left = b, .right = a}));
        const f64 turned =
            2.0 * std::atan2(std::hypot(q.x, q.y, q.z), q.w) * (q.z < 0.0 ? -1.0 : 1.0);
        const f64 deltaEra = earthRotationAngle(second).value() - earthRotationAngle(first).value();
        // Both wrapped into (-pi, pi], and the frame turns the other way from
        // the Earth: a vector fixed in the sky moves west as the Earth turns
        // east.
        const f64 expected = -(deltaEra - (kTwoPi * std::round(deltaEra / kTwoPi)));
        const f64 got = turned - (kTwoPi * std::round(turned / kTwoPi));
        CAPTURE(deltaEra, expected, got);
        REQUIRE_THAT(got, WithinAbsOf(expected, kStructural));
    }
}

// The pole of date does not depend on the Earth's rotation, which is what lets
// M1-63 evaluate J2 inside an integrator that has no UT1 and M1-08 refer a
// declination to the equator of date. Bit-for-bit in the matrices, because
// ERFA composes the Earth's turn as a rotation about that very axis.
TEST_CASE("the pole of date is the same with and without the Earth's rotation",
          "[earth][structure]") {
    Sampler sampler;
    for (std::size_t i = 0; i < 200; ++i) {
        const auto mjd = sampler.between({.lo = kFirstMjd, .hi = kLastMjd});
        const TtTime tt = instantAt<TimeScale::Tt>({
            .mjd = mjd,
            .picosecondOfDay = sampler.between({.lo = 0, .hi = kPicosecondsPerDayHere - 1}),
        });
        const Ut1Time ut1 = instantAt<TimeScale::Ut1>({
            .mjd = mjd,
            .picosecondOfDay = sampler.between({.lo = 0, .hi = kPicosecondsPerDayHere - 1}),
        });
        CAPTURE(kSweepSeed, i, tt, ut1);

        const RotationMatrix full = earthFixedFromInertialMatrix(tt, ut1);
        const RotationMatrix intermediate = intermediateFromInertialMatrix(tt);
        for (std::size_t column = 0; column < 3; ++column) {
            CAPTURE(column);
            REQUIRE(full.rows.at(2).at(column) == intermediate.rows.at(2).at(column));
        }
        // And through the quaternions, where the two are no longer the same
        // arithmetic.
        REQUIRE(angleBetween(earthFixedFromInertial(tt, ut1).inverseRotate(Direction{0, 0, 1}),
                             intermediateFromInertial(tt).inverseRotate(Direction{0, 0, 1}))
                    .value() < kStructural.value());
    }
}

// The intermediate frame is the whole rotation with the Earth's turn taken
// out, and not merely something with the same pole. Without this, a wrapper
// that returned the full rotation would still have the right pole -- which is
// all the pole test can see -- and M1-08 would refer a declination to a frame
// spun by up to a whole turn.
//
// What is left over is the TIO locator s', which eraC2t06a applies even with
// polar motion zero and which is not in Rz(ERA) x C2I: -47 uas a century from
// J2000, so under 0.1 mas across 1900-2100. That it is a *spin* and not a tilt
// is asserted too, because that is what s' is.
// Catch2 macro expansion, not written complexity.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the intermediate frame is the full rotation less the Earth's turn",
          "[earth][structure]") {
    Sampler sampler;
    for (std::size_t i = 0; i < 200; ++i) {
        const auto mjd = sampler.between({.lo = kFirstMjd, .hi = kLastMjd});
        const TtTime tt = instantAt<TimeScale::Tt>({
            .mjd = mjd,
            .picosecondOfDay = sampler.between({.lo = 0, .hi = kPicosecondsPerDayHere - 1}),
        });
        const Ut1Time ut1 = instantAt<TimeScale::Ut1>({
            .mjd = mjd,
            .picosecondOfDay = sampler.between({.lo = 0, .hi = kPicosecondsPerDayHere - 1}),
        });
        CAPTURE(kSweepSeed, i, tt, ut1);

        // Rz as the IAU writes it: the frame turns about the third axis, so a
        // fixed vector's coordinates turn the other way.
        const f64 era = earthRotationAngle(ut1).value();
        const f64 c = std::cos(era);
        const f64 s = std::sin(era);
        const RotationMatrix spun{.rows = {{{{c, s, 0.0}}, {{-s, c, 0.0}}, {{0.0, 0.0, 1.0}}}}};
        const RotationMatrix intermediate = intermediateFromInertialMatrix(tt);
        RotationMatrix composed{};
        for (std::size_t r = 0; r < 3; ++r) {
            for (std::size_t k = 0; k < 3; ++k) {
                composed.rows.at(r).at(k) =
                    (spun.rows.at(r).at(0) * intermediate.rows.at(0).at(k)) +
                    (spun.rows.at(r).at(1) * intermediate.rows.at(1).at(k)) +
                    (spun.rows.at(r).at(2) * intermediate.rows.at(2).at(k));
            }
        }

        const Quat left = quaternionFrom(
            timesTranspose({.left = earthFixedFromInertialMatrix(tt, ut1), .right = composed}));
        const Radians spin{2.0 * std::abs(left.z)};
        const Radians tilt{2.0 * std::hypot(left.x, left.y)};
        CAPTURE(spin.value(), tilt.value());
        REQUIRE(spin < kRotationBudget);
        // A spin, not a tilt: s' turns the frame about its own third axis.
        REQUIRE(tilt.value() < kStructural.value());
    }
}

// The quaternion is the matrix, and the rotation undoes itself: the two claims
// that are about this project's own arithmetic rather than about the IAU
// models. Over a seeded sweep, and at epochs where the rotation is within a
// whisker of a half turn, which is the case the conversion is shaped for.
// Catch2 macro expansion, not written complexity.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("the quaternion is a unit one and reproduces the matrix", "[earth][structure]") {
    Sampler sampler;
    f64 worstNorm = 0.0;
    f64 worstElement = 0.0;
    f64 worstRoundTrip = 0.0;
    std::size_t nearHalfTurns = 0;

    for (std::size_t i = 0; i < kSweepCases; ++i) {
        const auto mjd = sampler.between({.lo = kFirstMjd, .hi = kLastMjd});
        const TtTime tt = instantAt<TimeScale::Tt>({
            .mjd = mjd,
            .picosecondOfDay = sampler.between({.lo = 0, .hi = kPicosecondsPerDayHere - 1}),
        });
        // Half of the cases at an arbitrary UT1, and half where the Earth
        // rotation angle is within a milliradian of pi -- the fraction of the
        // day that puts it there comes from the defining formula, not from the
        // code under test.
        const bool nearHalfTurn = i % 2 == 1;
        std::int64_t picos = sampler.between({.lo = 0, .hi = kPicosecondsPerDayHere - 1});
        if (nearHalfTurn) {
            const f64 turnsAtMidnight =
                kEraAtJ2000Turns +
                (kEraTurnsPerDay * ((kMjdZero + static_cast<f64>(mjd)) - 2'451'545.0));
            const f64 wanted = 0.5 - (turnsAtMidnight - std::floor(turnsAtMidnight));
            const f64 fraction = (wanted < 0.0 ? wanted + 1.0 : wanted) / kEraTurnsPerDay;
            picos = static_cast<std::int64_t>(fraction * static_cast<f64>(kPicosecondsPerDayHere));
            picos = std::clamp(picos, std::int64_t{0}, kPicosecondsPerDayHere - 1);
        }
        const Ut1Time ut1 = instantAt<TimeScale::Ut1>({.mjd = mjd, .picosecondOfDay = picos});
        CAPTURE(kSweepSeed, i, tt, ut1, nearHalfTurn);

        const RotationMatrix m = earthFixedFromInertialMatrix(tt, ut1);
        const Quat q = earthFixedFromInertial(tt, ut1);
        if (nearHalfTurn) {
            const f64 era = earthRotationAngle(ut1).value();
            CAPTURE(era);
            REQUIRE(std::abs(era - std::numbers::pi_v<f64>) < 1e-3);
            if (std::abs(q.w) < 0.05) ++nearHalfTurns;
        }

        const f64 norm = std::sqrt((q.w * q.w) + (q.x * q.x) + (q.y * q.y) + (q.z * q.z));
        worstNorm = std::max(worstNorm, std::abs(norm - 1.0));
        REQUIRE_THAT(norm, WithinAbsOf(1.0, kUnitQuaternion));

        for (std::size_t column = 0; column < 3; ++column) {
            const auto axes = std::to_array<Direction>({
                Direction{1, 0, 0},
                Direction{0, 1, 0},
                Direction{0, 0, 1},
            });
            const Direction turned = q.rotate(axes.at(column));
            const std::array<f64, 3> got{{turned.x.value(), turned.y.value(), turned.z.value()}};
            for (std::size_t r = 0; r < 3; ++r) {
                const f64 error = std::abs(got.at(r) - m.rows.at(r).at(column));
                worstElement = std::max(worstElement, error);
                REQUIRE_THAT(error, WithinAbsOf(0.0, kReproducesMatrix));
            }
        }

        // Inertial -> body-fixed -> inertial, on a vector that is not an axis.
        const Direction v = directionOf(Direction{0.3, -0.7, 0.64});
        const Direction back = q.inverseRotate(q.rotate(v));
        const f64 roundTrip = length(back - v).value();
        worstRoundTrip = std::max(worstRoundTrip, roundTrip);
        REQUIRE_THAT(roundTrip, WithinAbsOf(0.0, kStructural));
    }

    INFO("worst |q| - 1 " << worstNorm << ", element " << worstElement << ", round trip "
                          << worstRoundTrip);
    // The near-half-turn cases really did reach the branch they were chosen
    // for: a scalar part near zero is where the naive conversion fails.
    CAPTURE(nearHalfTurns);
    REQUIRE(nearHalfTurns > 0);
}

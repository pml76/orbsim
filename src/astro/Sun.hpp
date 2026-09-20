#ifndef ORBSIM_ASTRO_SUN_HPP
#define ORBSIM_ASTRO_SUN_HPP
//
// Where the Sun is, and how much light it delivers (M1-08).
//
// Everything about the image depends on this: the terminator, the limb, the
// length of the shadows, and -- through the inverse-square law -- how much
// light there is to expose for. **It lights the scene; it does not pull on
// anything.** The force model in phase E is Earth point mass and J2 only.
//
// **ERFA computes it** (ADR 0016), through eraEpv00 -- the Earth's
// heliocentric position from a simplified VSOP2000, negated to give the
// geocentric Sun. ERFA's headers are included in Sun.cpp and nowhere else.
//
// **The frame is ICRF, and no rotation follows.** eraEpv00's vectors are
// oriented to the BCRS, whose axes are the ICRS's, which is this simulation's
// inertial frame. Nothing here precesses anything into anything.
//
// **Geometric, not apparent.** This is where the Sun *is* at the instant,
// with neither light-time nor aberration applied:
//
//   * aberration would move it by 20.5", a thousand times this file's budget
//     and invisible on screen, where the Sun's disc is 0.53" -- 0.53 degrees
//     -- across. The lighting uses the geometric Sun;
//   * light-time is negligible here and that is worth knowing rather than
//     assuming: the Sun barely moves relative to the barycentre, so retarding
//     it by 499 s displaces it about 0.008". The 20.5" is all aberration, and
//     it is the Earth that moves.
//
//   A caller wanting the *apparent* Sun -- for an instrument that points at
//   what it sees rather than at what is there -- must add both, and this file
//   deliberately gives it neither.
//
// **Accuracy, and where each claim comes from** (register decisions 85 and
// 88):
//
//   * claimed by ERFA: 3.7 km RMS and 11.2 km worst against JPL DE405 over
//     1900-2100, with the errors roughly doubling by 1800 and 2200
//     (eraEpv00's note 4). That worst case is 0.016" of direction and 7.5e-8
//     au of distance. Recorded, not asserted;
//   * **asserted: within 0.02" of direction and 5e-8 au of distance** against
//     JPL Horizons (DE441) over **2000-2050**, at the forty epochs of
//     data/horizons/sun-geocentric.txt -- tests/test_sun.cpp. About twice the
//     measured worst, which is decision 54's rule: measured 0.0085" and
//     2.14e-8 au;
//   * **the asserted span is the fixture's, and is narrower than ERFA's.**
//     ERFA's own worst over 1900-2100 is looser than this budget, so widening
//     the assertion means re-measuring and re-deriving, not pointing the test
//     at more epochs.
//
// **What the suite cannot see**, stated rather than left to look complete: TT
// handed in place of TDB (7e-5", and eraEpv00's note 1 permits it); a wrong
// astronomical unit (6e-11 relative); and the light-time above.
//
#include "core/Math.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"

#include <cstdint>
#include <expected>
#include <string_view>

namespace orb {

// The one way the Sun can fail to have a position (register decision 90). A
// new type rather than a value on OrbitError: this is a different layer, and
// astro has one failing entry point today.
enum class EphemerisError : std::uint8_t {
    OutsideEphemerisRange, // the date is outside the span ERFA vouches for
};

// A switch with one case, deliberately, and the check that objects is off at
// this line alone (register decision 91, CLAUDE.md working agreement 7).
//
// readability-trivial-switch is right that a one-case switch reads oddly and
// wrong about what this one is for. Every describe() in this project --
// OrbitError's, TimeError's, FixtureErrorKind's -- is a switch with no
// `default`, so that adding a value to the enum is a -Wswitch compile error
// *here*, at the function that must then be updated. Written as an `if`, a
// second error value would instead return "unknown ephemeris error" in
// silence, and nothing would point at this function. The enum has one value
// only because eraEpv00 returns only 0 or +1.
//
[[nodiscard]] constexpr std::string_view describe(EphemerisError error) noexcept {
    // NOLINTNEXTLINE(readability-trivial-switch)
    switch (error) {
    case EphemerisError::OutsideEphemerisRange:
        return "the date is outside the span the ephemeris vouches for";
    }
    return "unknown ephemeris error";
}

// The astronomical unit, IAU 2012 Resolution B2: exactly 149 597 870 700 m, a
// definition rather than a measurement, so it carries no error of its own.
//
// Here rather than in core/Units.hpp (decision 90) because only this file and
// its suite need it, and the suite cannot include ERFA's header to reach
// ERFA's own ERFA_DAU. Sun.cpp static_asserts that the two are bit-identical,
// so this constant cannot drift from the one eraEpv00's series assumes.
inline constexpr Metres kAstronomicalUnit{149'597'870'700.0};

// Total solar irradiance at one astronomical unit.
//
// **1360.8 +/- 0.5 W/m^2**, Kopp & Lean (2011), "A new, lower value of total
// solar irradiance: Evidence and climate significance", Geophys. Res. Lett.
// 38, L01706, doi:10.1029/2010GL045777 -- measured at the 2008 solar minimum
// by the Total Irradiance Monitor on SORCE. It replaced the 1365.4 +/- 1.3
// the 1990s used; the difference was scattered light in the older
// radiometers, not the Sun.
//
// Carried as 1361, which is 0.015% above the measurement and well inside its
// own +/- 0.037%.
//
// **The solar cycle is not modelled**, and that is worth naming here rather
// than discovering in M1-18: total irradiance varies by about 0.1% over the
// eleven-year cycle, 1.3 W/m^2, which is a fifth of M1-18's 0.5% radiometric
// budget.
inline constexpr Irradiance kSolarIrradianceAtOneAu{1361.0};

// The geocentric Sun in metres, ICRF, geometric.
//
// Reports OutsideEphemerisRange more than 100 Julian years from J2000.0 --
// outside JD 2415020.0 to 2488070.0 TDB, 1899-12-31T12:00 to 2100-01-01T12:00,
// **both ends inside** -- because eraEpv00's stated accuracy lapses there and
// it says so with a warning status. The boundary is taken from that status
// rather than from a second copy of the rule, so it is ERFA's to the
// resolution of its own arithmetic: about 0.63 us, since eraEpv00 forms the
// date as days from J2000 where a double resolves 2^-37 day.
//
// The same stance as the leap-second table: it says so rather than quietly
// answering less well. Note that "1900-2100", as ERFA's notes put it, ends on
// **1 January** 2100.
[[nodiscard]] std::expected<Position, EphemerisError> geocentricSunPosition(TdbTime tdb) noexcept;

// How far away it is. Exactly length(geocentricSunPosition(tdb)) -- one series
// evaluation, so the two can never disagree (decision 90) -- and it reports
// the same error at the same dates.
[[nodiscard]] std::expected<Metres, EphemerisError> sunDistance(TdbTime tdb) noexcept;

// The inverse-square law from kSolarIrradianceAtOneAu.
//
// A distance that is not positive is a bug, not a situation -- an observer at
// the centre of the Sun -- so it is asserted rather than reported (ADR 0002).
[[nodiscard]] constexpr Irradiance solarIrradianceAt(Metres distance) noexcept {
    ORBSIM_EXPECTS(distance > Metres{0.0});
    const f64 ratio = kAstronomicalUnit.value() / distance.value();
    return kSolarIrradianceAtOneAu * (ratio * ratio);
}

// Compile-time tests, which run on every build whether or not anyone invokes
// the suite. Exact, so a zero tolerance: at half and twice the astronomical
// unit the ratio is exactly 2 and exactly 1/2, both representable.
static_assert(nearlyEqual(solarIrradianceAt(kAstronomicalUnit).value(), 1361.0, Tolerance{0.0}));
static_assert(nearlyEqual(solarIrradianceAt(kAstronomicalUnit * 0.5).value(),
                          5444.0,
                          Tolerance{0.0}));
static_assert(nearlyEqual(solarIrradianceAt(kAstronomicalUnit * 2.0).value(),
                          340.25,
                          Tolerance{0.0}));

} // namespace orb

#endif // ORBSIM_ASTRO_SUN_HPP

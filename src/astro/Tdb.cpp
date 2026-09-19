#include "astro/Tdb.hpp"

#include "core/Contract.hpp"
#include "core/Scalar.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"

// ERFA's C header, here and in no header of ours (ADR 0016): nothing above
// src/astro/ sees C. It carries its own extern "C", and its include directory
// is SYSTEM, so this project's warnings and lint judge none of it.
#include <erfa.h>

//
// The wrapper around eraDtdb. The reasoning -- the geocentre, the scale the
// series is evaluated at, and what is claimed where -- is on the declarations
// in astro/Tdb.hpp.
//

namespace orb {

namespace {

// TDB - TT at an instant, as eraDtdb gives it at the geocentre.
//
// The date goes in as note 1's "MJD method": 2 400 000.5 and the Modified
// Julian Day with the time of day as its fraction, built from the stored day
// and picoseconds. eraDtdb collapses the two parts into one number of Julian
// millennia at once, so its own resolution is about a microsecond whichever
// split is used; against the J2000 method, which note 1 calls optimal, the
// result differs by at most 3e-4 ps, measured over a million instants on
// 2026-09-19 -- far below the picosecond it is rounded to.
//
// ut, elong, u and v are the observer's: universal time, east longitude, and
// distance from the spin axis and from the equatorial plane. With u and v zero
// the observer is at the geocentre, and every term they multiply vanishes, ut
// and elong included.
template <TimeScale Scale> [[nodiscard]] Seconds tdbMinusTtAt(TimePoint<Scale> at) noexcept {
    const f64 dayFraction =
        static_cast<f64>(at.picosecondOfDay()) / static_cast<f64>(kPicosecondsPerDay);
    constexpr f64 kAtTheGeocentre = 0.0;
    const f64 seconds = eraDtdb(kMjdZero,
                                at.modifiedJulianDay() + dayFraction,
                                kAtTheGeocentre,  // ut
                                kAtTheGeocentre,  // elong
                                kAtTheGeocentre,  // u
                                kAtTheGeocentre); // v
    return Seconds{seconds};
}

// An instant's stored parts, for TimePoint's own arithmetic.
template <TimeScale Scale> [[nodiscard]] detail::DayAndPicos partsOf(TimePoint<Scale> at) noexcept {
    return {.mjd = at.modifiedJulianDay(), .picos = at.picosecondOfDay()};
}

} // namespace

TdbTime tdbFromTt(TtTime tt) noexcept {
    // As TimePoint::operator+: a precondition, and a NaN back in a Release
    // build rather than a cast of one to an integer, which would be undefined.
    ORBSIM_EXPECTS(isFinite(tt.modifiedJulianDay()));
    if (!isFinite(tt.modifiedJulianDay())) {
        return detail::Builder::make<TimeScale::Tdb>(detail::kNotAnInstant);
    }
    // The series' argument is TDB, which is what is being computed, so it is
    // found by one fixed-point step. The first evaluation, at TT, puts the
    // estimate within 1.1 ps of TDB; the term's slope is at most 3.3e-10, so
    // the second evaluation is within 1e-21 s of the one at TDB itself, and a
    // third would change nothing a double can hold.
    const TdbTime estimate =
        detail::Builder::make<TimeScale::Tdb>(detail::advance(partsOf(tt), tdbMinusTtAt(tt)));
    return detail::Builder::make<TimeScale::Tdb>(
        detail::advance(partsOf(tt), tdbMinusTtAt(estimate)));
}

TtTime ttFromTdb(TdbTime tdb) noexcept {
    ORBSIM_EXPECTS(isFinite(tdb.modifiedJulianDay()));
    if (!isFinite(tdb.modifiedJulianDay())) {
        return detail::Builder::make<TimeScale::Tt>(detail::kNotAnInstant);
    }
    // The series at its own argument: TT = TDB - (TDB - TT)(TDB).
    return detail::Builder::make<TimeScale::Tt>(detail::advance(partsOf(tdb), -tdbMinusTtAt(tdb)));
}

} // namespace orb

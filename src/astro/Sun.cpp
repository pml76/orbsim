#include "astro/Sun.hpp"

#include "core/Math.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"

// ERFA's C headers, here and in no header of ours (ADR 0016): nothing above
// src/astro/ sees C. They carry their own extern "C", and the include
// directory is SYSTEM, so this project's warnings and lint judge none of it.
// erfam.h is the macros -- ERFA_DAU below.
#include <erfa.h>
#include <erfam.h>

#include <expected>

//
// The wrapper around eraEpv00. The reasoning -- the frame, what is geometric,
// the span and what is claimed where -- is on the declarations in
// astro/Sun.hpp.
//

namespace orb {

namespace {

// **The astronomical unit is ERFA's, proved rather than assumed.** eraEpv00's
// series is expressed in au, so a value here that differed from the one ERFA
// works in would rescale the result by the difference -- silently, and by an
// amount no test in this project resolves (6e-11 relative, about 9 m). The
// compiler checks it instead. Bit identity is the claim, so it is made by
// name, not with a floating-point == (CODING_GUIDELINES section 11).
static_assert(kAstronomicalUnit.bitIdentical(Metres{ERFA_DAU}),
              "astro/Sun.hpp's astronomical unit must be the one ERFA's series assumes");

// **The three suppressions below, and why they are here.**
//
// ERFA's interface is C: it fills two `double[2][3]` the caller supplies.
// That is the same fact, at the same interface, that register decision 84
// ruled for astro/EarthOrientation.cpp on 2026-09-20 -- the array's extents
// are part of its type, every index here is a literal, and the alternative is
// to reimplement VSOP2000 in our own code, which is precisely what ADR 0016
// says not to do. Switched off at the sites, with the reason at each, rather
// than in .clang-tidy, which would cover whatever is written in src/astro/
// next (CLAUDE.md working agreement 7).
//
// **Both arrays are passed, and only the heliocentric one is read.** ERFA's
// note 5 permits one array for both, but then it receives the *barycentric*
// values -- which are the Earth against the solar-system barycentre, up to
// about 0.01 au from the heliocentric ones and a 0.6 degree error in the
// Sun's direction. Two arrays, and the unread one is named for what it holds.

} // namespace

std::expected<Position, EphemerisError> geocentricSunPosition(TdbTime tdb) noexcept {
    // The Julian day of the midnight and the fraction of the day: the split
    // core/Time.hpp hands out, and the one that keeps the most of an instant
    // (register decisions 83 and 89). eraEpv00 collapses the two into Julian
    // years at once, so the split cannot matter to its answer the way it does
    // for the Earth rotation angle; it is the same split because there is no
    // reason for it to be a different one.
    const JulianDate date = tdb.julianDate();

    // ERFA's out-parameters. The extents are part of the type, as above.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    double heliocentric[2][3]{};
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    double barycentric[2][3]{};

    // The status is the boundary: 1 more than 100 Julian years from J2000.0,
    // and the wrapper reports that rather than keeping a second copy of the
    // rule (register decision 31).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    const int status = eraEpv00(date.day, date.fraction, heliocentric, barycentric);
    if (status != 0) return std::unexpected(EphemerisError::OutsideEphemerisRange);

    // The Earth's heliocentric position, negated, is the geocentric Sun. Row 0
    // is position and row 1 velocity, which nothing here wants.
    //
    // Reading ERFA's array is the one place this function indexes a C array,
    // and clang's -Wunsafe-buffer-usage is about exactly that: an index it
    // cannot bound. Here it can be bounded by eye -- every subscript is a
    // literal inside a `double[2][3]`. Off at this site alone, for clang
    // alone, with the reason here (ADR 0017); gcc has no warning of that name
    // and reports a pragma it does not know.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    const Position sun{Metres{-heliocentric[0][0] * ERFA_DAU},
                       // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
                       Metres{-heliocentric[0][1] * ERFA_DAU},
                       // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
                       Metres{-heliocentric[0][2] * ERFA_DAU}};
#ifdef __clang__
#pragma clang diagnostic pop
#endif
    return sun;
}

std::expected<Metres, EphemerisError> sunDistance(TdbTime tdb) noexcept {
    // One series evaluation, so the distance is exactly the length of the
    // position and the two cannot drift apart (register decision 90).
    const std::expected<Position, EphemerisError> position = geocentricSunPosition(tdb);
    if (!position.has_value()) return std::unexpected(position.error());
    return length(*position);
}

} // namespace orb

#include "astro/EarthOrientation.hpp"

#include "core/Math.hpp"
#include "core/Time.hpp"
#include "core/Units.hpp"

// ERFA's C header, here and in no header of ours (ADR 0016): nothing above
// src/astro/ sees C. It carries its own extern "C", and its include directory
// is SYSTEM, so this project's warnings and lint judge none of it.
#include <erfa.h>

#include <cstddef>

//
// The wrappers around eraC2t06a, eraC2i06a and eraEra00. The reasoning -- what
// is modelled, what is not, which way the rotations point, and what is
// asserted where -- is on the declarations in astro/EarthOrientation.hpp.
//

namespace orb {

namespace {

// ERFA's matrix, as this project's type. The parameter is a reference to a C
// array rather than a pointer, so the size is part of the type and nothing
// here can walk off the end of it.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
[[nodiscard]] RotationMatrix rotationMatrixOf(const double (&m)[3][3]) noexcept {
    RotationMatrix matrix{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            // Reading ERFA's array is the one place this project indexes a C
            // array, and clang's -Wunsafe-buffer-usage is about exactly that:
            // an index it cannot bound. Here it can be bounded by eye -- the
            // parameter is a reference to a double[3][3], so its extents are
            // part of its type, and both loops run to 3. Off at this site
            // alone, for clang alone, with the reason here (ADR 0017); gcc has
            // no warning of that name and reports a pragma it does not know.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            matrix.rows.at(i).at(j) = m[i][j];
#ifdef __clang__
#pragma clang diagnostic pop
#endif
        }
    }
    return matrix;
}

// Polar motion, which this project does not model: the pole offsets go in as
// zero, and the header states what that is worth (<= 0.6", about 19 m).
constexpr double kNoPolarMotion = 0.0;

// **The three suppressions below, and why they are here.**
//
// ERFA's interface is C: it fills a `double[3][3]` the caller supplies. That
// costs exactly three lines of lint in this project, measured on 2026-09-20
// with the suppressions stripped: three for the arrays themselves, two where
// they are handed over, and one where one is read back. (It cost six and not
// three because the first measurement of it was taken with a grep that could
// not match a finding naming two checks -- the instrument was wrong, not the
// code.) Each is the same fact: the array's extents are part of its type, both
// loops run to 3, and the only alternative is to compose the rotation from
// eraXys06a's X, Y and s in our own code, which is precisely what ADR 0016
// says not to do. Switched off at the three sites, on the owner's ruling of
// 2026-09-20 (register decision 84, CLAUDE.md working agreement 7) -- not in
// .clang-tidy, which would cover whatever anybody writes in src/astro/ next.
//
// The three suppressions are spelled out at their sites below.

} // namespace

Radians earthRotationAngle(Ut1Time ut1) noexcept {
    // The Julian day of the midnight and the fraction of the day, as
    // core/Time.hpp hands them out -- the split this angle needs, and why, is
    // on JulianDate there (register decisions 83 and 89).
    const JulianDate date = ut1.julianDate();
    return Radians{eraEra00(date.day, date.fraction)};
}

RotationMatrix earthFixedFromInertialMatrix(TtTime tt, Ut1Time ut1) noexcept {
    // The day-and-fraction split, as on JulianDate in core/Time.hpp.
    const JulianDate terrestrial = tt.julianDate();
    const JulianDate universal = ut1.julianDate();
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    double rc2t[3][3]{};
    eraC2t06a(terrestrial.day,
              terrestrial.fraction,
              universal.day,
              universal.fraction,
              kNoPolarMotion,
              kNoPolarMotion,
              // ERFA's out-parameter, as above.
              // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
              rc2t);
    return rotationMatrixOf(rc2t);
}

Quat earthFixedFromInertial(TtTime tt, Ut1Time ut1) noexcept {
    return quaternionFrom(earthFixedFromInertialMatrix(tt, ut1));
}

RotationMatrix intermediateFromInertialMatrix(TtTime tt) noexcept {
    // The day-and-fraction split, as on JulianDate in core/Time.hpp.
    const JulianDate date = tt.julianDate();
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    double rc2i[3][3]{};
    // ERFA's out-parameter, as above.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    eraC2i06a(date.day, date.fraction, rc2i);
    return rotationMatrixOf(rc2i);
}

Quat intermediateFromInertial(TtTime tt) noexcept {
    return quaternionFrom(intermediateFromInertialMatrix(tt));
}

} // namespace orb

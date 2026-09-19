#ifndef ORBSIM_ASTRO_TDB_HPP
#define ORBSIM_ASTRO_TDB_HPP
//
// TT <-> TDB: terrestrial time and barycentric dynamical time (M1-05).
//
// TDB is what solar-system ephemerides are tabulated in, and it differs from
// TT by a periodic term of about 1.7 ms, set by the Earth's motion through the
// Sun's gravity well: an annual term of 1.66 ms, planetary terms up to about
// 20 us, and lunar ones up to 2 us. ERFA computes it -- eraDtdb, Fairhead &
// Bretagnon (1990) in full -- and these two functions are the typed wrapper
// ADR 0016 asks for. ERFA's headers are included in Tdb.cpp and nowhere else.
//
// **At the geocentre.** eraDtdb also carries terms for where on the Earth's
// surface the clock stands -- diurnal and lunar, up to 2 us, its note 4 -- and
// they are evaluated with the observer at the Earth's centre, where they
// vanish: this simulation's origin is the geocentre, and a clock on the
// surface would be a different question. **This is held by the code, not by a
// test** (register decision 70): the terms are about 2 us, a tenth of the
// budget below, and nothing but ERFA itself resolves them.
//
// **The series is evaluated at TDB, in both directions** (register decision
// 56). Its argument is formally TDB; eraDtdb's note 1 allows TT instead "with
// no practical effect", and that allowance is not taken:
//
//   * ttFromTdb evaluates the series at its own argument, which is TDB.
//   * tdbFromTt evaluates it at TT, then again at TT plus that first answer,
//     which is TDB to within 1e-21 s: the first estimate is out by at most
//     1.1 ps, and the term's steepest slope is 3.3e-10.
//
// Evaluated at TT once, the answer would be out by up to 0.6 ps -- the 1.7 ms
// the argument moves, times that slope -- and 17% of round trips would come
// back a picosecond out. With the second evaluation, none of 1,000,000 did, in
// either direction (measured 2026-09-19). It costs one more eraDtdb call, about
// 6 us.
//
// **The round trip is within 1 ps** either way, asserted (decision 57): each
// direction rounds to the nearest picosecond once. That it is almost always
// exact is asserted as a rate -- at least 99% of a seeded sweep, both ways
// (decision 68) -- and not at every instant: on a picosecond grid the
// conversion cannot be a bijection. The rate is what shows the second
// evaluation is there; without it, 83% are exact, and the 1 ps budget alone
// cannot tell the difference.
//
// **Accuracy, and where each claim comes from** (decisions 54 and 58):
//
//   * claimed by ERFA: better than 3 ns against a time ephemeris integrated on
//     DE405, over 1950-2050 (eraDtdb's note 7). Recorded, not asserted: no
//     reference in hand can check it;
//   * asserted: within 20 us of USNO Circular 179's seven-term series as
//     Skyfield evaluates it, over 1900-2100 -- tests/test_astro_time.cpp,
//     against data/skyfield/tdb-minus-tt.txt. That series is itself within
//     9.28 us of eraDtdb there, which is why the budget can be no tighter.
//     And within a day, where the fixture's midnights cannot look: the change
//     over twelve hours matches the Kepler problem's annual term to 1 us
//     (decision 69);
//   * measured: within 9.51 us of the same series over 1600-2200;
//   * beyond, nothing is claimed, and nothing is refused. eraDtdb returns no
//     status, and TDB - TT is defined at every instant. At six sample epochs
//     from year 1 to year 9999 the whole term stayed within 1.76 ms, so an
//     error there is a few milliseconds -- about 100 m of the Earth's orbital
//     motion.
//
// Neither function can fail, and neither returns std::expected (the reasoning
// of register decision 36). A day that is not finite can reach them only from
// a Release build whose arithmetic precondition was violated upstream
// (TimePoint::operator+), and comes back as the same kind of NaN instant.
//
#include "core/Time.hpp"

namespace orb {

// TT -> TDB.
[[nodiscard]] TdbTime tdbFromTt(TtTime tt) noexcept;

// TDB -> TT.
[[nodiscard]] TtTime ttFromTdb(TdbTime tdb) noexcept;

} // namespace orb

#endif // ORBSIM_ASTRO_TDB_HPP

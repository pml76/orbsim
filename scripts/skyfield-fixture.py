#!/usr/bin/env python3
"""Write a reference fixture from Skyfield, for a budget ERFA must not judge.

    skyfield-fixture.py tdb-minus-tt FIXTURE       M1-05's TDB - TT
    skyfield-fixture.py earth-orientation FIXTURE  M1-07's celestial-to-terrestrial rotation

Why this exists: M1-05 computes TDB - TT with ERFA's eraDtdb, and agreement
between ERFA and ERFA proves nothing (ADR 0016, VERIFICATION.md rule 23). So
the budget is asserted against an implementation this project did not write
and that was written independently of SOFA: Skyfield's tdb_minus_tt, which is
USNO Circular 179 eq. 2.6 (Kaplan 2005), a truncation of Fairhead & Bretagnon
(1990). Register decisions 53 to 55.

What it writes, in the plain format tests/FixtureFile.hpp reads:

  * a header naming exactly what produced the numbers -- Skyfield's version,
    the function, the equation it evaluates, and numpy's version, because the
    function takes its sine from numpy;
  * one row per epoch: the Julian date in TDB, and TDB - TT in seconds, written
    with Python's repr of a float, which is the shortest text that reads back
    as the same double;
  * epochs at 0h TDB from 1900-01-01 to 2100-01-01 on a 37-day stride. 1900 to
    2100 is ERFA's span for the Sun, which M1-08 converts through; 37 days is
    not commensurate with the year, so the rows walk through every phase of the
    annual term rather than sampling one. Every Julian date ends in .5 and is
    exact.

It is deterministic: no timestamp, no machine name, nothing that changes
between runs. Unlike the Horizons fixtures its output is committed -- Skyfield
is MIT-licensed, and the 2026-09-17 ruling not to redistribute was about
Horizons' unstated terms (decision 55) -- so a rerun that changes a byte is a
change to the reference, and git says so.

**The second fixture, for M1-07** (register decisions 75, 81 and 82), is the
same argument one frame further out. M1-07 takes the rotation from the
celestial frame to the terrestrial one from ERFA's eraC2t06a, which is
CIO-based; Skyfield builds the same rotation the other correct way, as
Greenwich apparent sidereal time applied to the equinox-based
bias-precession-nutation matrix. So the pairing that M1-07's plan first
carried -- an equinox-based matrix with the Earth rotation angle, 1231" out --
fails against it, and so does every other misuse the budget was sized to
catch. What it cannot check is the IAU 2000A nutation series itself:
Skyfield's is a port of NOVAS's, which shares its IERS modules with SOFA's.
The coefficient table *is* the model, and every implementation shares it;
decision 75 records that as the limit of this reference.

Its rows are chosen so that no instant is rounded anywhere:

  * epochs every 71 days from 1900-01-01 to 2100-01-01, a stride commensurate
    with neither the year (5.14 per year) nor either lunar month;
  * a time of day that walks the whole day, in steps of the golden ratio, in
    units of 2^-19 day (0.164794921875 s) -- so every fraction is exact in a
    double and a whole number of picoseconds;
  * DeltaT held at 420 of those units, 69.2138671875 s, which makes UT1 exact
    in the same two ways. Which DeltaT does not matter here: the test passes
    the fixture's own TT and UT1 to the code, and a rotation is not a
    prediction of the Earth's clock.

It **refuses** to run under any other Skyfield or numpy than the pinned ones,
since a different version could compute different numbers under the same
header. The recipe, with the virtual environment, is data/skyfield/README.md.
"""
import sys
from fractions import Fraction

SKYFIELD_VERSION = '1.55'
NUMPY_VERSION = '2.5.3'

# 1900-01-01 and 2100-01-01 as Modified Julian Days, and the day number that
# turns one into a Julian date (JD = MJD + 2 400 000.5).
FIRST_MJD = 15020
LAST_MJD = 88069
STRIDE_DAYS = 37
JD_OF_MJD_ZERO = 2400000.5

# M1-07's fixture. The stride is 71 days -- 5.14 a year, and 2.6 lunar months.
# The time of day advances by GOLDEN_STEP of the 2^19 units a day is divided
# into, which walks the day without ever repeating over these 1,029 rows:
# 324 044 / 524 288 is the golden ratio's fractional part to six places, and
# shares only a factor of four with 2^19, so the cycle is 131 072 long.
ORIENTATION_STRIDE_DAYS = 71
DAY_UNITS = 2**19
GOLDEN_STEP = 324044
# 420 units of a day: 69.2138671875 s, near the DeltaT of 2026 (69.19 s) and
# exact in binary, so the UT1 instants are exact too.
DELTA_T_UNITS = 420
DELTA_T_SECONDS = DELTA_T_UNITS * 86400 / DAY_UNITS

ORIENTATION_HEADER = """\
# orbsim fixture: the rotation from the celestial frame to the terrestrial one,
# from an implementation independent of ERFA in everything but the nutation
# series (M1-07, register decisions 75, 81 and 82). Written by
# scripts/skyfield-fixture.py; the recipe is data/skyfield/README.md.
source      = Skyfield {skyfield} (Brandon Rhodes, MIT licence)
function    = skyfield.framelib.itrs.rotation_at
models      = IAU 2006 precession, IAU 2000A nutation, IERS 2003 frame bias
route       = equinox based: R3(GAST) x N x P x B
numpy       = {numpy}
frame       = ICRS to ITRS
polar motion = none
delta t     = {delta_t!r} s, held constant
time scale  = TT and UT1
columns     = jd_tt_day jd_tt_fraction jd_ut1_day jd_ut1_fraction c2t_11 c2t_12 c2t_13 c2t_21 c2t_22 c2t_23 c2t_31 c2t_32 c2t_33
"""

HEADER = """\
# orbsim fixture: TDB - TT at the geocentre, from an implementation independent
# of ERFA (M1-05, register decisions 53-55). Written by
# scripts/skyfield-fixture.py; the recipe is data/skyfield/README.md.
source      = Skyfield {skyfield} (Brandon Rhodes, MIT licence)
function    = skyfield.timelib.tdb_minus_tt
model       = USNO Circular 179 (Kaplan 2005), eq. 2.6
numpy       = {numpy}
time scale  = TDB
columns     = jd_tdb tdb_minus_tt_s
"""


class Refused(Exception):
    """The environment is not the one the fixture's header will claim."""


def orientation_rows():
    """The epochs, as exact (day, fraction) pairs in TT and in UT1."""
    for i, mjd in enumerate(range(FIRST_MJD, LAST_MJD + 1, ORIENTATION_STRIDE_DAYS)):
        units = (i * GOLDEN_STEP) % DAY_UNITS
        tt_day = JD_OF_MJD_ZERO + mjd  # a half-integer far below 2^52: exact
        ut1_units = units - DELTA_T_UNITS
        if ut1_units >= 0:
            yield tt_day, units / DAY_UNITS, tt_day, ut1_units / DAY_UNITS
        else:
            yield tt_day, units / DAY_UNITS, tt_day - 1.0, (ut1_units + DAY_UNITS) / DAY_UNITS


def orientation_text():
    try:
        import numpy
        import skyfield
        from skyfield.api import load
        from skyfield.framelib import itrs
    except ImportError as missing:
        raise Refused(f'{missing}; run this in the virtual environment '
                      'data/skyfield/README.md sets up') from missing
    check_versions(skyfield, numpy)

    rows = list(orientation_rows())
    timescale = load.timescale(delta_t=DELTA_T_SECONDS)
    epochs = timescale.tt_jd(numpy.array([r[0] for r in rows]),
                             numpy.array([r[1] for r in rows]))
    # Skyfield derives UT1 from TT and DeltaT; this fixture writes UT1 out, so
    # the two must name the same instant exactly, not merely closely. Compared
    # as rationals, because the two pairs are split at different days and
    # summing them as doubles would round.
    for row, fraction in zip(rows, epochs.ut1_fraction):
        skyfield_ut1 = Fraction(row[0]) + Fraction(float(fraction))
        written_ut1 = Fraction(row[2]) + Fraction(row[3])
        if skyfield_ut1 != written_ut1:
            raise Refused('the UT1 written is not the UT1 Skyfield used')
    rotations = itrs.rotation_at(epochs)  # (3, 3, n), ICRS -> ITRS

    lines = [ORIENTATION_HEADER.format(skyfield=SKYFIELD_VERSION, numpy=NUMPY_VERSION,
                                       delta_t=DELTA_T_SECONDS)]
    for i, (tt_day, tt_fraction, ut1_day, ut1_fraction) in enumerate(rows):
        elements = ' '.join(repr(float(rotations[r][c][i])) for r in range(3) for c in range(3))
        lines.append(f'{tt_day:.1f} {tt_fraction!r} {ut1_day:.1f} {ut1_fraction!r} '
                     f'{elements}\n')
    return ''.join(lines)


def check_versions(skyfield, numpy):
    if skyfield.__version__ != SKYFIELD_VERSION:
        raise Refused(f'Skyfield {skyfield.__version__}, but this fixture is pinned to '
                      f'{SKYFIELD_VERSION}; see data/skyfield/README.md')
    if numpy.__version__ != NUMPY_VERSION:
        raise Refused(f'numpy {numpy.__version__}, but this fixture is pinned to '
                      f'{NUMPY_VERSION}; see data/skyfield/README.md')


def fixture_text():
    # Imported here rather than at the top, so that a Python without them gets
    # a refusal that names the recipe rather than a traceback.
    try:
        import numpy
        import skyfield
        from skyfield.timelib import tdb_minus_tt
    except ImportError as missing:
        raise Refused(f'{missing}; run this in the virtual environment '
                      'data/skyfield/README.md sets up') from missing

    check_versions(skyfield, numpy)

    lines = [HEADER.format(skyfield=SKYFIELD_VERSION, numpy=NUMPY_VERSION)]
    for mjd in range(FIRST_MJD, LAST_MJD + 1, STRIDE_DAYS):
        jd = JD_OF_MJD_ZERO + mjd  # a half-integer far below 2^52: exact
        # float() because numpy's sin hands back a numpy.float64, whose repr is
        # not the bare number.
        seconds = float(tdb_minus_tt(jd))
        lines.append(f'{jd:.1f} {seconds!r}\n')
    return ''.join(lines)


WRITERS = {'tdb-minus-tt': lambda: fixture_text(), 'earth-orientation': orientation_text}


def main(argv):
    if len(argv) != 3 or argv[1] not in WRITERS:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        text = WRITERS[argv[1]]()
    except Refused as refusal:
        print(f'skyfield-fixture.py: refused: {refusal}', file=sys.stderr)
        return 1
    # LF on every platform: the file is committed, and a CRLF would change
    # every byte of it on a Windows run.
    with open(argv[2], 'w', encoding='utf-8', newline='\n') as out:
        out.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
"""Write the TDB - TT reference fixture, from Skyfield.

    skyfield-fixture.py FIXTURE    write the fixture to FIXTURE

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

It **refuses** to run under any other Skyfield or numpy than the pinned ones,
since a different version could compute different numbers under the same
header. The recipe, with the virtual environment, is data/skyfield/README.md.
"""
import sys

SKYFIELD_VERSION = '1.55'
NUMPY_VERSION = '2.5.3'

# 1900-01-01 and 2100-01-01 as Modified Julian Days, and the day number that
# turns one into a Julian date (JD = MJD + 2 400 000.5).
FIRST_MJD = 15020
LAST_MJD = 88069
STRIDE_DAYS = 37
JD_OF_MJD_ZERO = 2400000.5

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

    if skyfield.__version__ != SKYFIELD_VERSION:
        raise Refused(f'Skyfield {skyfield.__version__}, but this fixture is pinned to '
                      f'{SKYFIELD_VERSION}; see data/skyfield/README.md')
    if numpy.__version__ != NUMPY_VERSION:
        raise Refused(f'numpy {numpy.__version__}, but this fixture is pinned to '
                      f'{NUMPY_VERSION}; see data/skyfield/README.md')

    lines = [HEADER.format(skyfield=SKYFIELD_VERSION, numpy=NUMPY_VERSION)]
    for mjd in range(FIRST_MJD, LAST_MJD + 1, STRIDE_DAYS):
        jd = JD_OF_MJD_ZERO + mjd  # a half-integer far below 2^52: exact
        # float() because numpy's sin hands back a numpy.float64, whose repr is
        # not the bare number.
        seconds = float(tdb_minus_tt(jd))
        lines.append(f'{jd:.1f} {seconds!r}\n')
    return ''.join(lines)


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        text = fixture_text()
    except Refused as refusal:
        print(f'skyfield-fixture.py: refused: {refusal}', file=sys.stderr)
        return 1
    # LF on every platform: the file is committed, and a CRLF would change
    # every byte of it on a Windows run.
    with open(argv[1], 'w', encoding='utf-8', newline='\n') as out:
        out.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

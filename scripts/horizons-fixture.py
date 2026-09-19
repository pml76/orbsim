#!/usr/bin/env python3
"""Turn a JPL Horizons API response into an orbsim fixture.

    horizons-fixture.py RESPONSE FIXTURE    convert one response
    horizons-fixture.py --self-test         run the golden test (CTest does)

Why this exists: Horizons stamps every response with the moment it was made,
and its Earth-orientation lines change daily. Measured on 2026-09-19: two
identical queries five seconds apart hash differently. The owner's ruling of
2026-09-17 was to commit the fixtures' SHA-256 sums rather than the fixtures,
so that a regenerated file can be shown to be the one the error budgets were
measured against -- and a raw response can never be shown that. So the
response is converted, and the conversion is deterministic: the same data in
gives the same bytes out, whenever it was fetched (register decision 42).

What the conversion keeps and what it drops:

  * the numbers, **as text, character for character**. Nothing is parsed and
    printed again, so no digit can change on the way through;
  * Horizons' own header, as comments, because it is the provenance -- the
    frame, the corrections, the ephemeris -- less exactly three lines: the
    request's timestamp, the EOP file and the EOP coverage. None of the three
    affects a geometric geocentric state vector, and all three change without
    the data changing;
  * not the calendar-date column, which repeats the Julian date in words.

It also **refuses** a response that is not what the fixture will claim to be:
anything but KM-S, ICRF, geometric, position-and-velocity, from a body centre.
A refused response is a query that went wrong, and it is cheaper to find out
here than when a budget fails for a reason nobody can see.

It does not download anything; fetching is the curl in data/horizons/README.md,
by hand, which keeps this script inside M1-06's scope and out of the network.
"""
import re
import sys

EXPECTED = {
    'Center-site name': 'BODY CENTER',
    'Output units': 'KM-S',
    'Output type': 'GEOMETRIC cartesian states',
    'Output format': '2 (position and velocity)',
    'Reference frame': 'ICRF',
}

# The header lines dropped because they change without the data changing.
VOLATILE = ('Ephemeris / ', 'EOP file', 'EOP coverage')

COLUMN_TITLES = ['JDTDB', 'Calendar Date (TDB)', 'X', 'Y', 'Z', 'VX', 'VY', 'VZ']
COLUMNS = 'jd_tdb x_km y_km z_km vx_km_s vy_km_s vz_km_s'

JULIAN_DATE = re.compile(r'^\d+\.\d+$')
NUMBER = re.compile(r'^-?\d+\.\d+(?:E[+-]\d+)?$')
BODY = re.compile(r'^(.*?)\s*\{source:\s*([^}]+?)\s*\}$')


class Refused(Exception):
    """The response is not one this fixture format can honestly carry."""


def header_facts(lines):
    """`Key : value` lines before $$SOE, as a dict. Keys are stripped."""
    facts = {}
    for line in lines:
        key, colon, value = line.partition(':')
        if colon and key.strip() and not key.startswith('*'):
            facts.setdefault(key.strip(), value.strip())
    return facts


def body_and_source(facts, key):
    value = facts.get(key)
    if value is None:
        raise Refused('no "%s" line in the header' % key)
    match = BODY.match(value)
    if not match:
        raise Refused('"%s" does not name its source: %r' % (key, value))
    return match.group(1), match.group(2)


def data_rows(block):
    rows = []
    for number, line in enumerate(block, start=1):
        fields = [field.strip() for field in line.split(',')]
        if fields and fields[-1] == '':
            fields.pop()  # Horizons ends every CSV row with a comma
        if len(fields) != len(COLUMN_TITLES):
            raise Refused('data row %d has %d fields, not %d'
                          % (number, len(fields), len(COLUMN_TITLES)))
        jd, numbers = fields[0], fields[2:]
        if not JULIAN_DATE.match(jd):
            raise Refused('data row %d: %r is not a Julian date' % (number, jd))
        for value in numbers:
            if not NUMBER.match(value):
                raise Refused('data row %d: %r is not a number' % (number, value))
        rows.append(' '.join([jd] + numbers))
    if not rows:
        raise Refused('the ephemeris block is empty')
    return rows


def convert(response):
    """The fixture text for a Horizons response, or Refused."""
    lines = response.splitlines()
    starts = [i for i, line in enumerate(lines) if line.strip() == '$$SOE']
    ends = [i for i, line in enumerate(lines) if line.strip() == '$$EOE']
    if len(starts) != 1 or len(ends) != 1 or ends[0] < starts[0]:
        raise Refused('no single $$SOE ... $$EOE block: Horizons reports a malformed '
                      'query as text inside a 200 response, so read the file')
    header, block = lines[:starts[0]], lines[starts[0] + 1:ends[0]]

    facts = header_facts(header)
    for key, value in EXPECTED.items():
        if facts.get(key) != value:
            raise Refused('%s is %r, and this fixture needs %r'
                          % (key, facts.get(key), value))
    target, target_source = body_and_source(facts, 'Target body name')
    center, center_source = body_and_source(facts, 'Center body name')
    if target_source != center_source:
        raise Refused('target and centre come from different ephemerides: %s and %s'
                      % (target_source, center_source))

    titles = next((l for l in header if l.strip().startswith('JDTDB')), None)
    if titles is None:
        raise Refused('no column titles: the epochs are not in TDB')
    found = [t.strip() for t in titles.split(',') if t.strip()]
    if found != COLUMN_TITLES:
        raise Refused('the columns are %r, not %r' % (found, COLUMN_TITLES))

    kept = [line.rstrip() for line in header
            if line.strip()
            and set(line.strip()) != {'*'}
            and not line.lstrip().startswith(VOLATILE)]

    out = [
        '# orbsim fixture: geocentric state vectors, converted from a JPL Horizons',
        '# API response by scripts/horizons-fixture.py. Horizons output is queried and',
        '# never redistributed: data/horizons/README.md has the query, and',
        '# checksums.sha256 beside this file the hash it must have.',
    ]
    for key, value in [
        ('source', 'JPL Horizons API'),
        ('ephemeris', target_source),
        ('target', target),
        ('center', center),
        ('frame', 'ICRF'),
        ('corrections', 'none'),
        ('time scale', 'TDB'),
        ('columns', COLUMNS),
    ]:
        out.append('%-11s = %s' % (key, value))
    out.append('#')
    out.append("# Horizons' own header, less the three lines that change without the data")
    out.append("# changing: the request's timestamp, and the EOP file and its coverage.")
    out.append('#')
    out.extend('#   ' + line for line in kept)
    out.append('#')
    out.extend(data_rows(block))
    return '\n'.join(out) + '\n'


# --- the golden test ----------------------------------------------------------
#
# Synthetic, and deliberately so: Horizons output is not redistributed, so the
# test input imitates its layout with numbers nobody measured.
#
# One of them is 9.007199254740993E+15, which is 2^53 + 1 and has no double:
# a conversion that parsed the numbers and printed them again would write
# ...992E+15 and fail the golden comparison. With only round values here, that
# mutation passed on 2026-09-19 -- the numbers must be copied, not re-derived.

SYNTHETIC = """API VERSION: 1.2
API SOURCE: NASA/JPL Horizons API



*******************************************************************************
Ephemeris / API_USER Fri Sep 18 21:42:50 2026 Pasadena, USA      / Horizons
*******************************************************************************
Target body name: Sun (10)                        {source: DE441}
Center body name: Earth (399)                     {source: DE441}
Center-site name: BODY CENTER
*******************************************************************************
Output units    : KM-S
Output type     : GEOMETRIC cartesian states
Output format   : 2 (position and velocity)
EOP file        : eop.260918.p261215
EOP coverage    : DATA-BASED 1962-JAN-20 TO 2026-SEP-18. PREDICTS-> 2026-DEC-14
Reference frame : ICRF
*******************************************************************************
            JDTDB,            Calendar Date (TDB),                      X,                      Y,                      Z,                     VX,                     VY,                     VZ,
*******************************************************************************
$$SOE
2451545.000000000, A.D. 2000-Jan-01 12:00:00.0000,  9.007199254740993E+15, -2.000000000000000E+07,  3.000000000000000E+06, -4.000000000000000E+01,  5.000000000000000E+00, -6.000000000000000E-01,
2451546.500000000, A.D. 2000-Jan-03 00:00:00.0000,  1.100000000000000E+08, -2.100000000000000E+07,  3.100000000000000E+06, -4.100000000000000E+01,  5.100000000000000E+00, -6.100000000000000E-01,
$$EOE
*******************************************************************************

TIME
  Explanatory text after the block, which the fixture does not carry.
"""

GOLDEN = """# orbsim fixture: geocentric state vectors, converted from a JPL Horizons
# API response by scripts/horizons-fixture.py. Horizons output is queried and
# never redistributed: data/horizons/README.md has the query, and
# checksums.sha256 beside this file the hash it must have.
source      = JPL Horizons API
ephemeris   = DE441
target      = Sun (10)
center      = Earth (399)
frame       = ICRF
corrections = none
time scale  = TDB
columns     = jd_tdb x_km y_km z_km vx_km_s vy_km_s vz_km_s
#
# Horizons' own header, less the three lines that change without the data
# changing: the request's timestamp, and the EOP file and its coverage.
#
#   API VERSION: 1.2
#   API SOURCE: NASA/JPL Horizons API
#   Target body name: Sun (10)                        {source: DE441}
#   Center body name: Earth (399)                     {source: DE441}
#   Center-site name: BODY CENTER
#   Output units    : KM-S
#   Output type     : GEOMETRIC cartesian states
#   Output format   : 2 (position and velocity)
#   Reference frame : ICRF
#               JDTDB,            Calendar Date (TDB),                      X,                      Y,                      Z,                     VX,                     VY,                     VZ,
#
2451545.000000000 9.007199254740993E+15 -2.000000000000000E+07 3.000000000000000E+06 -4.000000000000000E+01 5.000000000000000E+00 -6.000000000000000E-01
2451546.500000000 1.100000000000000E+08 -2.100000000000000E+07 3.100000000000000E+06 -4.100000000000000E+01 5.100000000000000E+00 -6.100000000000000E-01
"""


def self_test():
    checks = 0

    def expect(condition, what):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(what)

    expect(convert(SYNTHETIC) == GOLDEN, 'the synthetic response converts to the golden text')

    # Deterministic: a second fetch, at another moment, with the next day's
    # Earth-orientation file, converts to the same bytes.
    later = (SYNTHETIC
             .replace('Fri Sep 18 21:42:50 2026', 'Sat Sep 19 09:15:02 2026')
             .replace('eop.260918.p261215', 'eop.260919.p261216')
             .replace('TO 2026-SEP-18', 'TO 2026-SEP-19')
             .replace('Explanatory text', 'Different explanatory text'))
    expect(convert(later) == GOLDEN, 'a later fetch of the same data converts identically')

    # CRLF in, LF out: the hash must not depend on how the response travelled.
    expect(convert(SYNTHETIC.replace('\n', '\r\n')) == GOLDEN, 'CRLF converts identically')

    refusals = [
        ('astronomical units', SYNTHETIC.replace(': KM-S', ': AU-D')),
        ('an ecliptic frame', SYNTHETIC.replace(': ICRF', ': Ecliptic of J2000.0')),
        ('light-time corrected',
         SYNTHETIC.replace('GEOMETRIC cartesian states', 'ASTROMETRIC cartesian states')),
        ('position only', SYNTHETIC.replace('2 (position and velocity)', '1 (position only)')),
        ('a surface site', SYNTHETIC.replace('BODY CENTER', 'Greenwich')),
        ('no ephemeris block', SYNTHETIC.replace('$$SOE', 'SOE')),
        ('an empty ephemeris block',
         re.sub(r'\$\$SOE\n.*\$\$EOE', '$$SOE\n$$EOE', SYNTHETIC, flags=re.S)),
        ('a malformed number', SYNTHETIC.replace('9.007199254740993E+15', '9.0071992547409O3E+15')),
        ('a missing field', SYNTHETIC.replace(' -6.000000000000000E-01,', '')),
        ('mixed ephemerides', SYNTHETIC.replace('Earth (399)                     {source: DE441}',
                                                'Earth (399)                     {source: DE440}')),
        ('epochs not in TDB', SYNTHETIC.replace('            JDTDB,', '            JDUT,')),
    ]
    for name, response in refusals:
        expect(response != SYNTHETIC, 'the "%s" case changed its input' % name)
        try:
            convert(response)
        except Refused:
            checks += 1
            continue
        raise AssertionError('%s was converted, and should have been refused' % name)

    print('horizons-fixture self-test: %d checks passed' % checks)


def main(argv):
    if argv[1:] == ['--self-test']:
        self_test()
        return 0
    if len(argv) != 3:
        print(__doc__.split('\n\n')[1], file=sys.stderr)
        return 2
    with open(argv[1], encoding='ascii', newline='') as source:
        response = source.read()
    try:
        fixture = convert(response)
    except Refused as refusal:
        print('%s: refused: %s' % (argv[1], refusal), file=sys.stderr)
        return 1
    # newline='' so that the file holds exactly '\n', on every platform: the
    # hash is taken over these bytes, and a CRLF written on Windows would make
    # two machines disagree about a fixture they both generated correctly.
    with open(argv[2], 'w', encoding='ascii', newline='') as target:
        target.write(fixture)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

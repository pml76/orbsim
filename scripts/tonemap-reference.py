#!/usr/bin/env python3
"""Print the reference values tests/test_exposure.cpp and tests/test_tonemap.cpp
assert against (M1-15).

    tonemap-reference.py

Why this exists: view/Exposure.hpp and view/Tonemap.hpp compute in double
precision, and a test that recomputed the same formulas in the same arithmetic
would check the code against itself (VERIFICATION.md rule 2). The values here
come from the published definitions evaluated in 50-digit decimal arithmetic,
which shares no code and no rounding with the C++ -- the pattern
scripts/srgb-reference.py set for M1-14.

**The exposure relations.** ISO 2720 and ISO 12232, as Lagarde & de Rousiers
(2014), "Moving Frostbite to Physically Based Rendering", section 4.2, and
Filament's Exposure.cpp write them:

    EV100 = log2(N^2 / t * 100 / S)
    Lmax  = 78 / (S q) * N^2 / t = (78 / (100 q)) * 2^EV100,  q = 0.65
    exposure factor = 1 / Lmax                          (per cd/m^2)

78 / (100 * 0.65) is 1.2 exactly, which is why the factor is usually written
1 / (1.2 * 2^EV100).

**AgX**, as Benjamin Wrensch's minimal implementation writes it
(https://iolite-engine.com/blog_posts/minimal_agx_implementation, MIT,
Copyright (c) 2024 Missing Deadlines), with the three guards register decision
174 added, in the order view/Tonemap.hpp applies them:

    1. negative input clamped to zero (Sobotka's own configuration does this,
       as a RangeTransform before its matrix);
    2. the inset matrix;
    3. each channel raised to at least 2^minEv before log2, because GLSL leaves
       log2(0) undefined; the result is unchanged, since the next step clamps
       there anyway;
    4. log2, clamped to [minEv, maxEv] and normalised to [0, 1];
    5. the 6th-order polynomial approximation of Sobotka's contrast curve;
    6. the outset matrix -- the published inverse of the inset;
    7. clamped to [0, 1], because GLSL leaves pow() of a negative undefined;
    8. raised to 2.2, which undoes the display encoding the curve carries --
       Wrensch's agxEotf -- so that view/Srgb.hpp's encode can follow it, as
       register decision 173 chose.

**The matrices are copied as GLSL writes them**, column by column, because the
shader holds them that way and a transposed matrix is the likeliest copying
slip. This script builds the rows from those columns explicitly, and then
checks the inset against Troy Sobotka's own OCIO configuration
(https://github.com/sobotka/AgX, config.ocio), which lists the same matrix row
by row: two sources, two layouts, one matrix, or the script stops.

Each input is a double, taken at its exact binary value (Decimal(float) is
exact), and each result is rounded once to the nearest double and printed with
repr. Deterministic, and needs nothing beyond the standard library.
"""

from decimal import Decimal, getcontext

getcontext().prec = 50

# --- exposure ---------------------------------------------------------------

SATURATION_CONSTANT = Decimal(78)          # ISO 12232 saturation-based speed
LENS_ATTENUATION = Decimal("0.65")         # q: transmittance, vignetting, cos^4
# The luminous efficacy of sunlight, from scripts/solar-efficacy.py.
SUNLIGHT_EFFICACY = Decimal("98.9225")     # lm/W


def log2(x: Decimal) -> Decimal:
    return x.ln() / Decimal(2).ln()


def exposure_value_100(aperture: float, shutter: float, iso: float) -> Decimal:
    n, t, s = Decimal(aperture), Decimal(shutter), Decimal(iso)
    return log2(n * n / t * Decimal(100) / s)


def exposure_factor(ev100: Decimal) -> Decimal:
    maximum_luminance = SATURATION_CONSTANT / (Decimal(100) * LENS_ATTENUATION) \
        * (ev100 * Decimal(2).ln()).exp()
    return Decimal(1) / maximum_luminance


# --- AgX --------------------------------------------------------------------

# Exactly as the minimal implementation's GLSL lists them: mat3(...) takes its
# nine arguments column by column.
INSET_GLSL = [
    "0.842479062253094", "0.0423282422610123", "0.0423756549057051",
    "0.0784335999999992", "0.878468636469772", "0.0784336",
    "0.0792237451477643", "0.0791661274605434", "0.879142973793104",
]
OUTSET_GLSL = [
    "1.19687900512017", "-0.0528968517574562", "-0.0529716355144438",
    "-0.0980208811401368", "1.15190312990417", "-0.0980434501171241",
    "-0.0990297440797205", "-0.0989611768448433", "1.15107367264116",
]
# Sobotka's config.ocio, MatrixTransform of "AgX Log (Kraken)": a 4x4
# row-major matrix, of which this is the upper-left 3x3.
INSET_OCIO_ROWS = [
    ["0.842479062253094", "0.0784335999999992", "0.0792237451477643"],
    ["0.0423282422610123", "0.878468636469772", "0.0791661274605434"],
    ["0.0423756549057051", "0.0784336", "0.879142973793104"],
]
MIN_EV = Decimal("-12.47393")   # log2(0.18) - 10 stops
MAX_EV = Decimal("4.026069")    # log2(0.18) + 6.5 stops
# Mean error^2 3.6705141e-06 against Sobotka's curve, as published.
POLYNOMIAL = [  # coefficient of x^6 down to x^0
    Decimal("15.5"), Decimal("-40.14"), Decimal("31.96"), Decimal("-6.868"),
    Decimal("0.4298"), Decimal("0.1191"), Decimal("-0.00232"),
]
DISPLAY_EXPONENT = Decimal("2.2")


def rows_from_glsl(columns: list[str]) -> list[list[Decimal]]:
    # Column j is arguments 3j, 3j+1, 3j+2, so row i, column j is argument 3j+i.
    return [[Decimal(columns[3 * j + i]) for j in range(3)] for i in range(3)]


INSET = rows_from_glsl(INSET_GLSL)
OUTSET = rows_from_glsl(OUTSET_GLSL)
assert INSET == [[Decimal(v) for v in row] for row in INSET_OCIO_ROWS], \
    "the inset from the GLSL columns is not Sobotka's matrix"


def multiply(matrix: list[list[Decimal]], v: list[Decimal]) -> list[Decimal]:
    return [sum((matrix[i][j] * v[j] for j in range(3)), Decimal(0)) for i in range(3)]


def power(base: Decimal, exponent: Decimal) -> Decimal:
    if base == 0:
        return Decimal(0)
    return (exponent * base.ln()).exp()


def contrast(x: Decimal) -> Decimal:
    result = Decimal(0)
    for c in POLYNOMIAL:
        result = result * x + c
    return result


def clamp(x: Decimal, low: Decimal, high: Decimal) -> Decimal:
    return max(low, min(high, x))


FLOOR = power(Decimal(2), MIN_EV)


def agx(rgb: list[float]) -> list[Decimal]:
    v = [max(Decimal(0), Decimal(c)) for c in rgb]
    v = multiply(INSET, v)
    v = [max(c, FLOOR) for c in v]
    v = [(clamp(log2(c), MIN_EV, MAX_EV) - MIN_EV) / (MAX_EV - MIN_EV) for c in v]
    v = [contrast(c) for c in v]
    v = multiply(OUTSET, v)
    return [power(clamp(c, Decimal(0), Decimal(1)), DISPLAY_EXPONENT) for c in v]


def grey_is_black(g: Decimal) -> bool:
    return all(c == 0 for c in agx([float(g)] * 3))


def bisect_black_threshold() -> Decimal:
    low, high = Decimal("1e-5"), Decimal("1e-2")  # black at low, not at high
    for _ in range(200):
        middle = (low + high) / 2
        if grey_is_black(middle):
            low = middle
        else:
            high = middle
    return low


def main() -> None:
    print("// EV100: {aperture, shutter time in s, ISO, reference EV100}")
    for n, t, s in [(16.0, 1 / 125, 100.0), (16.0, 1 / 125, 400.0), (16.0, 1 / 125, 50.0),
                    (5.6, 1 / 60, 100.0), (2.8, 1 / 30, 1600.0), (1.0, 1.0, 100.0),
                    (1.4, 30.0, 3200.0)]:
        print(f"    {{{n!r}, {t!r}, {s!r}, {float(exposure_value_100(n, t, s))!r}}},")

    print("// exposure factor at EV100, per cd/m^2: {EV100, reference factor}")
    for ev in [0.0, 14.965784284662087, -2.0, 7.5]:
        print(f"    {{{ev!r}, {float(exposure_factor(Decimal(ev)))!r}}},")

    ev = exposure_value_100(16.0, 1 / 125, 100.0)
    radiance = Decimal("0.3") * Decimal(1361) / Decimal("3.14159265358979323846264338327950288")
    print(f"// sunlit albedo 0.3 at f/16 1/125 ISO 100, exposed: "
          f"{float(radiance * SUNLIGHT_EFFICACY * exposure_factor(ev))!r}")

    print("// AgX: {red, green, blue in} -> {red, green, blue out}, linear display light")
    grey = [0.18 * 2.0 ** k for k in (-10, -9, -8, -6, -4, -2, -1, 0, 1, 2, 4, 6)]
    colours = [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0],
               [0.18, 0.09, 0.02], [4.0, 2.0, 0.5], [0.05, 0.3, 0.9], [-1.0, 0.5, 0.5],
               [1.0e12, 1.0e12, 1.0e12]]
    for rgb in [[g, g, g] for g in grey] + colours:
        out = agx(rgb)
        print(f"    {{{{{rgb[0]!r}, {rgb[1]!r}, {rgb[2]!r}}}, "
              f"{{{float(out[0])!r}, {float(out[1])!r}, {float(out[2])!r}}}}},")

    threshold = bisect_black_threshold()
    saturation = power(Decimal(2), MAX_EV) / min(sum(row) for row in INSET)
    print(f"// the largest grey that is exactly black in every channel: {float(threshold)!r}")
    print(f"// grey above which every channel's log clamps at maxEv:   {float(saturation)!r}")
    print(f"// the curve at 0 and at 1: {float(contrast(Decimal(0)))!r}, "
          f"{float(contrast(Decimal(1)))!r}")


if __name__ == "__main__":
    main()

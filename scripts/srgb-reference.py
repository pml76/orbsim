#!/usr/bin/env python3
"""Print the sRGB reference values tests/test_srgb.cpp asserts against (M1-14).

    srgb-reference.py

Why this exists: view/Srgb.hpp evaluates the sRGB transfer function in double
precision, and a test that recomputed the same formula in the same arithmetic
would check the code against itself (VERIFICATION.md rule 2). The values here
come from the published definition evaluated in 50-digit decimal arithmetic,
which shares no code and no rounding with the C++ -- the pattern the orbit
suite set with its 60-digit references.

The definition is IEC 61966-2-1:1999, the sRGB standard, amendment 1:

    encode  V = 12.92 L                      for 0 <= L <= 0.0031308
            V = 1.055 L^(1/2.4) - 0.055      for 0.0031308 < L <= 1
    decode  L = V / 12.92                    for 0 <= V <= 0.04045
            L = ((V + 0.055) / 1.055)^2.4    for 0.04045 < V <= 1

Each input is a double, taken at its exact binary value (Decimal(float) is
exact), so the reference is the true function at the very number the C++ is
handed; each result is rounded once to the nearest double (float(Decimal) is
correctly rounded) and printed with repr, the shortest text that reads back
as that double.

It also prints the two jumps the standard's rounded constants leave at the
knees, because the tests assert them as properties of the definition rather
than assuming the function is continuous: the two branches of the encode do
not meet at 0.0031308, and those of the decode do not meet at 0.04045.

Deterministic, and needs nothing beyond the standard library.
"""

from decimal import Decimal, getcontext
import math

getcontext().prec = 50

LINEAR_KNEE = Decimal("0.0031308")
ENCODED_KNEE = Decimal("0.04045")
SLOPE = Decimal("12.92")
SCALE = Decimal("1.055")
OFFSET = Decimal("0.055")
EXPONENT = Decimal("2.4")


def power(base: Decimal, exponent: Decimal) -> Decimal:
    # Decimal's ** with a non-integral exponent is evaluated to the context
    # precision; exp(ln) is spelled out so the method is visible.
    if base == 0:
        return Decimal(0)
    return (exponent * base.ln()).exp()


def encode_power(linear: Decimal) -> Decimal:
    return SCALE * power(linear, Decimal(1) / EXPONENT) - OFFSET


def decode_power(encoded: Decimal) -> Decimal:
    return power((encoded + OFFSET) / SCALE, EXPONENT)


def encode(linear: Decimal) -> Decimal:
    return SLOPE * linear if linear <= LINEAR_KNEE else encode_power(linear)


def decode(encoded: Decimal) -> Decimal:
    return encoded / SLOPE if encoded <= ENCODED_KNEE else decode_power(encoded)


def row(argument: float, result: Decimal) -> str:
    return f"    {{{argument!r}, {float(result)!r}}},"


def main() -> None:
    knee = float(LINEAR_KNEE)
    encoded_knee = float(ENCODED_KNEE)
    linear_inputs = [0.0, 1e-9, 1e-6, 0.001, knee, math.nextafter(knee, 1.0),
                     0.01, 0.018, 0.05, 0.1, 0.2140, 0.5, 0.75, 0.9, 0.99, 1.0]
    encoded_inputs = [0.0, 1e-6, 0.004, 0.006, 0.012, 0.02, encoded_knee,
                      math.nextafter(encoded_knee, 1.0), 0.1, 128 / 255, 0.5,
                      0.75, 0.9, 1.0]

    print("// encode: {linear input, reference encoded value}")
    for x in linear_inputs:
        print(row(x, encode(Decimal(x))))
    print("// decode: {encoded input, reference linear value}")
    for y in encoded_inputs:
        print(row(y, decode(Decimal(y))))

    encode_jump = encode_power(LINEAR_KNEE) - SLOPE * LINEAR_KNEE
    decode_jump = decode_power(ENCODED_KNEE) - ENCODED_KNEE / SLOPE
    print(f"// encode jump at 0.0031308, power branch minus linear: {float(encode_jump)!r}")
    print(f"// decode jump at 0.04045,   power branch minus linear: {float(decode_jump)!r}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Print the downsample references tests/test_image_compare.cpp asserts (M1-17).

    downsample-reference.py
    downsample-reference.py --self-test

Why this exists: view/ImageCompare.cpp averages each 2x2 block of an 8-bit
image in the display's linear light (register decision 228) -- decode each
value with the sRGB standard, average the four, encode the average and round
it to the nearest 8-bit value -- and a test that recomputed that in the same
double arithmetic would check the code against itself (VERIFICATION.md rule 2).
Here it is evaluated in 50-digit decimal arithmetic, with the sRGB definition
taken from scripts/srgb-reference.py, the script the sRGB suite's references
come from, so the two cannot drift apart.

An 8-bit value k stands for the encoded value k/255, exactly. The average is
rounded to the nearest 8-bit value, and an exact tie rounds up; exact ties
occur only where all four values lie on the straight segment of the curve
(0 to 10), where the encoded average is exactly the mean of the four.

For every result the script also prints how far the exact value lay from a
rounding boundary, in 8-bit steps, so that a case chosen for the test is seen
not to sit on a knife edge the C++ double arithmetic could fall either side of.

**The straight segment is taken in whole numbers** (register decision 301):
until 2026-10-03 it went through the 50-digit decode and encode like the rest,
landed a hair under an exact tie, and rounded (3, 3, 4, 4) to 3 rather than 4.
None of the cases below is such a tie, so the references pasted into the test
were right; the independent check of a golden found it (decision 300).
--self-test holds average() to hand-worked values, the ties first, and runs in
`check` as the CTest test downsample_reference_self_test.

Deterministic, and needs nothing beyond the standard library.
"""

from decimal import Decimal, ROUND_HALF_UP, getcontext
import importlib.util
import pathlib
import sys

getcontext().prec = 50

# Loading the sibling script would otherwise leave a __pycache__ directory in
# scripts/, which nothing ignores.
sys.dont_write_bytecode = True

_spec = importlib.util.spec_from_file_location(
    "srgb_reference", pathlib.Path(__file__).with_name("srgb-reference.py"))
srgb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(srgb)

STEPS = Decimal(255)

# The 4x4 test image, as four 2x2 blocks; each block's four pixels in the
# order top-left, top-right, bottom-left, bottom-right, each pixel R, G, B.
# Chosen so that every channel of every block is different, the straight
# segment, the curved one and a mix of the two all appear, and one block
# (black beside white) shows how far linear light is from the mean of the
# stored values: 188 against 128.
BLOCKS = {
    "top-left": [(0, 11, 3), (0, 12, 7), (255, 200, 9), (255, 201, 10)],
    "top-right": [(128, 250, 10), (64, 5, 10), (32, 100, 10), (16, 60, 11)],
    "bottom-left": [(255, 1, 90), (254, 1, 180), (253, 2, 45), (252, 2, 135)],
    "bottom-right": [(77, 0, 200), (77, 0, 30), (77, 0, 220), (77, 1, 10)],
}


# The largest 8-bit value on the sRGB curve's straight segment: 10/255 is below
# the decode's knee at 0.04045 and 11/255 above it -- kLastStraightValue in
# src/view/ImageCompare.hpp, by the same reasoning.
LAST_STRAIGHT = 10


def average(values: list[int]) -> tuple[int, Decimal]:
    """The 8-bit result, and the exact value before rounding, in 8-bit steps."""
    if max(values) <= LAST_STRAIGHT:
        # On the straight segment the decode divides by 12.92 and the encode
        # multiplies by it, so the encoded average is exactly the mean of the
        # four -- taken here in whole numbers. Through the decode and encode
        # in 50 digits it lands a hair off, and an exact tie -- (3, 3, 4, 4)
        # is 3.5 -- rounded down until 2026-10-03 (decisions 300 and 301).
        exact = Decimal(sum(values)) / 4
    else:
        linear = sum(srgb.decode(Decimal(v) / STEPS) for v in values) / 4
        exact = srgb.encode(linear) * STEPS
    return int(exact.quantize(Decimal(1), rounding=ROUND_HALF_UP)), exact


def self_test() -> int:
    """Hold average() to values worked out by hand, ties first (decision 301).

    Each case is (four values, the 8-bit result, the exact value or None). The
    straight-segment cases are means of four whole numbers, so their exact
    values are quarters and are written here exactly; an exact tie rounds up
    (decision 228). The one curved case, black beside white, is the value
    tests/test_image_compare.cpp already holds, to show the fix left the
    curved segment alone.
    """
    cases = [
        ((3, 3, 4, 4), 4, Decimal("3.5")),     # the tie found on 2026-10-03
        ((7, 7, 8, 8), 8, Decimal("7.5")),
        ((9, 9, 10, 10), 10, Decimal("9.5")),  # the segment's last value
        ((0, 0, 0, 1), 0, Decimal("0.25")),
        ((3, 7, 9, 10), 7, Decimal("7.25")),
        ((10, 10, 10, 10), 10, Decimal("10")),
        ((0, 0, 255, 255), 188, None),         # curved: 187.516 steps
    ]
    failures = 0
    for values, want, want_exact in cases:
        got, exact = average(list(values))
        good = got == want and (want_exact is None or exact == want_exact)
        failures += not good
        print(f"self-test: {values} -> {got}, exact {exact:.12f} ({'ok' if good else 'WRONG'})")
    return 1 if failures else 0


def main() -> None:
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    for name, pixels in BLOCKS.items():
        results = []
        for channel in range(3):
            got, exact = average([pixel[channel] for pixel in pixels])
            margin = abs(Decimal("0.5") - abs(exact - int(exact)))
            results.append(got)
            print(f"// {name}, channel {'RGB'[channel]}: exact {exact:.12f} steps, "
                  f"{float(margin):.3e} steps from a rounding boundary -> {got}")
        print(f"// {name}: {{{results[0]}, {results[1]}, {results[2]}}}")


if __name__ == "__main__":
    main()

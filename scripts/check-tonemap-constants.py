#!/usr/bin/env python3
"""Check that the shader's AgX constants are the CPU's, exactly (M1-15).

    check-tonemap-constants.py <repository root>
    check-tonemap-constants.py --self-test

Why this exists: AgX is written twice -- shaders/tonemap.frag for the GPU and
src/view/Tonemap.hpp for the CPU -- and nothing reads a pixel back until
M1-18, which compares the two to 1/255. Until then a constant copied wrongly
into one of them would pass every test, and the likeliest slip is not a typo
but a **transposed matrix**: GLSL's mat3() takes its arguments column by
column, and the C++ writes the rows. This compares every AgX constant in the
two files, numerically and exactly, reading the GLSL matrices as columns.
Register decision 182; `check` runs it as the CTest test `tonemap_constants`,
and its self-test as `tonemap_constants_self_test`.

It compares the *text* of the literals, as exact decimals, rather than their
rounded values: the claim is that the same numbers were written down, and a
difference below float precision is still a difference in what was copied.

What it does not see: the order of the operations, and the guards. Those are
code, not constants, and M1-18's port check is what compares them.

Proven able to fail before it was trusted: --self-test alters a copy of the
real files in each of the ways this exists for -- a transposed matrix, a
changed coefficient, a changed range, a missing constant -- and exits non-zero
unless every one is reported.
"""

import pathlib
import re
import sys
from decimal import Decimal

NUMBER = r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?"

# Each pair: the GLSL name and the C++ name, and what kind of value it holds.
SCALARS = [("kAgxMinEv", "kMinEv"), ("kAgxMaxEv", "kMaxEv"),
           ("kAgxDisplayExponent", "kDisplayExponent")]
MATRICES = [("kAgxInset", "kInset"), ("kAgxOutset", "kOutset")]
ARRAYS = [("kAgxContrast", "kContrast")]


class Missing(Exception):
    pass


def numbers(text: str) -> list[Decimal]:
    # Strip the C++ float suffix and nothing else; GLSL literals have none here.
    return [Decimal(n) for n in re.findall(NUMBER, text)]


def glsl_value(source: str, name: str) -> list[Decimal]:
    # `const <type> name = <initialiser>;`, or `const float name[7] = ...;` --
    # the initialiser up to the semicolon.
    match = re.search(r"const\s+\w+\s+" + name + r"(?:\[\d+\])?\s*=\s*([^;]*);", source)
    if not match:
        raise Missing(f"shaders/tonemap.frag has no constant {name}")
    initialiser = match.group(1)
    # Drop the constructor's own type name and any array size, e.g. float[7](
    initialiser = re.sub(r"^\s*\w+(?:\[\d+\])?\s*\(", "(", initialiser)
    return numbers(initialiser)


def cpp_value(source: str, name: str) -> list[Decimal]:
    match = re.search(r"inline\s+constexpr\s+[\w:<>, ]+?\s+" + name + r"\s*(?:=\s*|\{)([^;]*);",
                      source)
    if not match:
        raise Missing(f"src/view/Tonemap.hpp has no constant {name}")
    # std::array<f64, 7> would contribute its 7 and its 64; only the
    # initialiser is read, and it is everything after the name.
    return numbers(match.group(1))


def columns_to_rows(values: list[Decimal]) -> list[Decimal]:
    # mat3(a0..a8): column j is a[3j], a[3j+1], a[3j+2]; row i, column j is a[3j+i].
    return [values[3 * j + i] for i in range(3) for j in range(3)]


def compare(glsl: str, cpp: str) -> list[str]:
    problems = []
    try:
        for g, c in SCALARS + ARRAYS:
            gv, cv = glsl_value(glsl, g), cpp_value(cpp, c)
            if gv != cv:
                problems.append(f"{g} {[str(v) for v in gv]} != {c} {[str(v) for v in cv]}")
        for g, c in MATRICES:
            gv, cv = glsl_value(glsl, g), cpp_value(cpp, c)
            if len(gv) != 9 or len(cv) != 9:
                problems.append(f"{g} or {c} is not nine numbers ({len(gv)}, {len(cv)})")
            elif columns_to_rows(gv) != cv:
                problems.append(f"{g}, read column by column, is not {c}, read row by row")
    except Missing as missing:
        problems.append(str(missing))
    return problems


def self_test(root: pathlib.Path) -> int:
    glsl = (root / "shaders" / "tonemap.frag").read_text(encoding="utf-8")
    cpp = (root / "src" / "view" / "Tonemap.hpp").read_text(encoding="utf-8")
    if compare(glsl, cpp):
        print("self-test: the real files disagree before anything was altered")
        return 1
    inset_row0 = "{{0.842479062253094, 0.0784335999999992, 0.0792237451477643}}"
    alterations = {
        "a transposed inset": (glsl, cpp.replace(
            inset_row0, "{{0.842479062253094, 0.0423282422610123, 0.0423756549057051}}", 1)),
        "a changed coefficient": (glsl.replace("-6.868", "-6.886", 1), cpp),
        "a changed range": (glsl, cpp.replace("kMaxEv = 4.026069", "kMaxEv = 4.026096", 1)),
        "a changed exponent": (glsl.replace("kAgxDisplayExponent = 2.2", "kAgxDisplayExponent = 2.4", 1), cpp),
        "a missing constant": (glsl.replace("kAgxOutset", "kAgxOutsetRenamed"), cpp),
    }
    failed = 0
    for what, (g, c) in alterations.items():
        if (g, c) == (glsl, cpp):
            print(f"self-test: the alteration for {what} matched nothing")
            failed += 1
        elif not compare(g, c):
            print(f"self-test: {what} was NOT reported")
            failed += 1
        else:
            print(f"self-test: {what} is reported")
    return 1 if failed else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return self_test(pathlib.Path(__file__).resolve().parent.parent)
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = pathlib.Path(sys.argv[1])
    glsl = (root / "shaders" / "tonemap.frag").read_text(encoding="utf-8")
    cpp = (root / "src" / "view" / "Tonemap.hpp").read_text(encoding="utf-8")
    problems = compare(glsl, cpp)
    for problem in problems:
        print(f"tonemap-constants: {problem}")
    if problems:
        return 1
    count = len(SCALARS) + len(ARRAYS) + len(MATRICES)
    print(f"tonemap-constants: {count} AgX constants agree between the shader and the CPU")
    return 0


if __name__ == "__main__":
    sys.exit(main())

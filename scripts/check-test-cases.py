#!/usr/bin/env python3
"""Check that every test case is written so that its compiled code is keyed by its name (M1-129).

    check-test-cases.py <repository root>
    check-test-cases.py --self-test

Why this exists: `check` reruns a test only when a function it entered at its
last run has changed in its compiled code (register decision 543), and
tests/TestCase.hpp is what makes a test's compiled code depend on the test
alone -- Catch2's line numbers fixed at 0, and each test's function and
registration named after the test's own name and tags (decisions 550 and 573).
A test file that bypassed the header would compile and pass, and quietly bring
back the extra reruns; and with the line fixed, two sections of one name in a
test case would merge, and one of them would silently stop running, since
Catch2 tells sections apart by name and line (decision 558). So this holds
tests/ to the header's rules, by structure (decision 575):

  * **only tests/TestCase.hpp includes catch2/catch_test_macros.hpp**, and it
    does, and it defines TEST_CASE;
  * **every file that writes a TEST_CASE includes "tests/TestCase.hpp"**;
  * **none of Catch2's other macros that register a test or a computed
    section** appears: TEST_CASE_METHOD, TEST_CASE_PERSISTENT_FIXTURE,
    SCENARIO and SCENARIO_METHOD, the TEMPLATE_..._TEST_CASE and
    TEMPLATE_..._SCENARIO family, REGISTER_TEST_CASE, METHOD_AS_TEST_CASE,
    DYNAMIC_SECTION, and GIVEN, WHEN, THEN and their AND_ forms, whose names
    Catch2 builds at run time. They would bring back the counter's names;
  * **every SECTION is inside a test case's body, its name is one plain string
    literal** -- or several side by side, which the compiler joins -- **and no
    two sections of one test case share a name.** A name that is not a plain
    literal cannot be compared, so it is refused rather than guessed at.

Comments are read as blank, and string literals as text, so a commented-out
SECTION or a "SECTION(" inside a string is not a section.

Proven able to fail before it was trusted: --self-test runs the check on small
trees of its own, one per way it must refuse and one it must accept, and exits
non-zero unless each is judged right, for its own reason.
"""

import pathlib
import re
import sys
import tempfile

HEADER = "TestCase.hpp"
INCLUDE_HEADER = re.compile(r'^\s*#\s*include\s*"tests/TestCase\.hpp"', re.MULTILINE)
INCLUDE_CATCH_MACROS = re.compile(r'^\s*#\s*include\s*[<"]catch2/catch_test_macros\.hpp[>"]', re.MULTILINE)
DEFINES_TEST_CASE = re.compile(r"^\s*#\s*define\s+TEST_CASE\b", re.MULTILINE)
TEST_CASE = re.compile(r"\bTEST_CASE\s*\(")
SECTION = re.compile(r"\bSECTION\s*\(")
REFUSED_MACROS = re.compile(
    r"\b(TEST_CASE_METHOD|TEST_CASE_PERSISTENT_FIXTURE|SCENARIO|SCENARIO_METHOD"
    r"|TEMPLATE_(?:[A-Z]+_)*(?:TEST_CASE|SCENARIO)(?:_[A-Z]+)*"
    r"|REGISTER_TEST_CASE|METHOD_AS_TEST_CASE|DYNAMIC_SECTION"
    r"|(?:AND_)?GIVEN|(?:AND_)?WHEN|(?:AND_)?THEN)\s*\(")
PLAIN_LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
RAW_PREFIXES = ("R", "u8R", "uR", "UR", "LR")
SIMPLE_ESCAPES = {"'": "'", '"': '"', "?": "?", "\\": "\\", "a": "\a", "b": "\b", "f": "\f",
                  "n": "\n", "r": "\r", "t": "\t", "v": "\v"}


def blank(text: str) -> tuple:
    """Two copies of the text, each as long as it and with its line breaks:
    `code`, with every comment replaced by spaces; and `shape`, with the
    contents of every string and character literal blanked as well, so that
    brackets and macro names are found only where the compiler sees them."""
    code, shape = list(text), list(text)

    def wipe(start, end, both):
        for k in range(start, end):
            if text[k] != "\n":
                shape[k] = " "
                if both:
                    code[k] = " "

    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = i
            while j < n and text[j] != "\n":
                j += 2 if text.startswith("\\\n", j) else 1  # a backslash continues it
            wipe(i, j, True)
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            wipe(i, j, True)
            i = j
        elif c == '"':
            word = re.search(r"[A-Za-z0-9_]*$", text[:i]).group(0)
            if word in RAW_PREFIXES:
                close = re.match(r'"([^()\\\s]{0,16})\(', text[i:])
                delim = close.group(1) if close else ""
                end = text.find(")" + delim + '"', i)
                j = n if end < 0 else end + len(delim) + 2
            else:
                j = i + 1
                while j < n and text[j] != '"' and text[j] != "\n":
                    j += 2 if text[j] == "\\" else 1
                j = min(j + 1, n)
            wipe(i + 1, j - 1, False)
            i = j
        elif c == "'":
            token = re.search(r"[A-Za-z0-9_'.]*$", text[:i]).group(0)
            if token[:1].isdigit():
                i += 1  # a digit separator, as in 1'000
                continue
            j = i + 1
            while j < n and text[j] != "'" and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            wipe(i + 1, j - 1, False)
            i = j
        else:
            i += 1
    return "".join(code), "".join(shape)


def closing(shape: str, start: int, opening: str, close: str) -> int:
    """The index of the bracket closing the one at `start`, or -1."""
    depth = 0
    for k in range(start, len(shape)):
        if shape[k] == opening:
            depth += 1
        elif shape[k] == close:
            depth -= 1
            if depth == 0:
                return k
    return -1


def decoded(literal: str):
    """A plain literal's contents as the compiler reads them, or None where
    this cannot say -- a universal character name."""
    out, k = [], 0
    while k < len(literal):
        if literal[k] != "\\":
            out.append(literal[k])
            k += 1
            continue
        e = literal[k + 1]
        if e in SIMPLE_ESCAPES:
            out.append(SIMPLE_ESCAPES[e])
            k += 2
        elif e in "01234567":
            digits = re.match(r"[0-7]{1,3}", literal[k + 1:]).group(0)
            out.append(chr(int(digits, 8)))
            k += 1 + len(digits)
        elif e == "x":
            digits = re.match(r"[0-9A-Fa-f]*", literal[k + 2:]).group(0)
            if not digits:
                return None
            out.append(chr(int(digits, 16)))
            k += 2 + len(digits)
        else:
            return None
    return "".join(out)


def section_name(argument: str):
    """The name a SECTION's argument spells, or None if it is not plain string
    literals side by side."""
    pieces, k = [], 0
    while True:
        while k < len(argument) and argument[k].isspace():
            k += 1
        if k == len(argument):
            break
        literal = PLAIN_LITERAL.match(argument, k)
        if not literal:
            return None
        pieces.append(decoded(literal.group(1)))
        k = literal.end()
    if not pieces or any(p is None for p in pieces):
        return None
    return "".join(pieces)


def line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def file_problems(name: str, text: str) -> tuple:
    """The problems in one file, and how many test cases and sections it has."""
    found = []
    code, shape = blank(text)
    if name == HEADER:
        return found, 0, 0
    where = f"tests/{name}"
    for match in INCLUDE_CATCH_MACROS.finditer(code):
        found.append(f"{where}:{line_of(text, match.start())}: includes catch2/catch_test_macros.hpp "
                     f"-- include \"tests/TestCase.hpp\" instead, which names each test case after "
                     "itself (decision 573)")
    for match in REFUSED_MACROS.finditer(shape):
        found.append(f"{where}:{line_of(text, match.start())}: uses Catch2's {match.group(1)}, which "
                     "names what it registers by a counter or at run time -- write a TEST_CASE, "
                     "with SECTIONs named by string literals (decision 575)")
    cases = []
    for match in TEST_CASE.finditer(shape):
        open_paren = match.end() - 1
        close_paren = closing(shape, open_paren, "(", ")")
        body = re.match(r"\s*\{", shape[close_paren + 1:]) if close_paren >= 0 else None
        if body is None:
            found.append(f"{where}:{line_of(text, match.start())}: a TEST_CASE whose body this "
                         "check cannot find, so it cannot read its sections")
            continue
        start = close_paren + 1 + body.end() - 1
        end = closing(shape, start, "{", "}")
        if end < 0:
            found.append(f"{where}:{line_of(text, match.start())}: a TEST_CASE whose body does not close")
            continue
        cases.append((start, end, line_of(text, match.start())))
    if cases and not INCLUDE_HEADER.search(code):
        found.append(f"{where}: writes a TEST_CASE without including \"tests/TestCase.hpp\", so "
                     "Catch2's own would name it by a counter (decision 573)")
    seen = {}
    sections = 0
    for match in SECTION.finditer(shape):
        sections += 1
        number = line_of(text, match.start())
        open_paren = match.end() - 1
        close_paren = closing(shape, open_paren, "(", ")")
        if close_paren < 0:
            found.append(f"{where}:{number}: a SECTION whose argument does not close")
            continue
        name_ = section_name(code[open_paren + 1:close_paren])
        if name_ is None:
            found.append(f"{where}:{number}: a SECTION not named by a plain string literal -- a "
                         "computed name cannot be compared with its neighbours' (decision 572)")
            continue
        owner = [c for c in cases if c[0] < match.start() < c[1]]
        if not owner:
            found.append(f"{where}:{number}: a SECTION outside every test case's body -- write it "
                         "in the test case, where its name can be compared (decision 575)")
            continue
        key = (owner[-1][0], name_)
        if key in seen:
            found.append(f"{where}:{number}: two sections named \"{name_}\" in the test case at line "
                         f"{owner[-1][2]}, here and at line {seen[key]} -- with the line fixed at 0, "
                         "Catch2 would merge them and run one (decision 558)")
        else:
            seen[key] = number
    return found, len(cases), sections


def problems(files: dict) -> tuple:
    """Every way the files break the rules, each naming its file and line, and
    the counts read; `files` maps a name relative to tests/ to its text."""
    if not files:
        return ["no file was found under tests/: the check would pass by reading nothing"], 0, 0
    found = []
    header = files.get(HEADER)
    if header is None:
        found.append(f"tests/{HEADER} is missing: no test case can be named after itself")
    else:
        code, _ = blank(header)
        if not INCLUDE_CATCH_MACROS.search(code):
            found.append(f"tests/{HEADER} does not include catch2/catch_test_macros.hpp: the header "
                         "is not what this check expects, so it would pass without checking anything")
        if not DEFINES_TEST_CASE.search(code):
            found.append(f"tests/{HEADER} does not define TEST_CASE: Catch2's own would name every "
                         "test case by a counter")
    cases = sections = 0
    for name, text in sorted(files.items()):
        more, file_cases, file_sections = file_problems(name, text)
        found.extend(more)
        cases += file_cases
        sections += file_sections
    if cases == 0:
        found.append("no TEST_CASE was found under tests/: the check would pass by reading nothing")
    return found, cases, sections


def sources(tests: pathlib.Path) -> dict:
    return {path.relative_to(tests).as_posix(): path.read_text(encoding="utf-8", errors="replace")
            for path in sorted(tests.rglob("*")) if path.suffix in (".cpp", ".hpp", ".h")}


def check(root: pathlib.Path) -> int:
    files = sources(root / "tests")
    found, cases, sections = problems(files)
    for line in found:
        print(f"check-test-cases: {line}")
    if found:
        return 1
    print(f"check-test-cases: {len(files)} files under tests/, {cases} test cases and {sections} "
          f"sections, written through tests/{HEADER}")
    return 0


def self_test() -> int:
    header = ('#include <catch2/catch_test_macros.hpp> // IWYU pragma: export\n'
              "#undef TEST_CASE\n#define TEST_CASE(...) x\n")
    suite = ('#include "tests/TestCase.hpp"\n'
             "// SECTION(\"a\") in a comment, and /* SECTION(\"a\") */ in another\n"
             'TEST_CASE("one", "[tag]") {\n'
             "    const int big = 1'000'000;\n"
             "    const char brace = '{';\n"
             '    const char* text = R"x(a ) SECTION("a") { )x";\n'
             '    INFO("SECTION(\\"a\\") in a string");\n'
             '    SECTION("a") {\n'
             '        SECTION("inner, with a comma") {}\n'
             "    }\n"
             '    SECTION("b" " joined") {}\n'
             '    SECTION("a\\tb") {}\n'
             "}\n"
             'TEST_CASE("two") {\n'
             '    SECTION("a") {}\n'
             "    auto lambda = [] { CHECK(true); };\n"
             "}\n")
    support = "inline void helper() { CHECK(1 == 1); }\n"
    clean = {HEADER: header, "test_a.cpp": suite, "Support.hpp": support}

    def with_suite(body):
        return {**clean, "test_b.cpp": '#include "tests/TestCase.hpp"\n' + body}

    twice = "two sections named"
    plain = "not named by a plain string literal"
    other = "uses Catch2's"
    cases = [
        ("the files as they should be", clean, None),
        ("two sections of one name", with_suite('TEST_CASE("t") {\n SECTION("x") {}\n SECTION("x") {}\n}\n'),
         twice),
        ("two of one name at different depths",
         with_suite('TEST_CASE("t") {\n SECTION("x") {\n  SECTION("x") {}\n }\n}\n'), twice),
        ("two of one name, one joined from two literals",
         with_suite('TEST_CASE("t") {\n SECTION("ab") {}\n SECTION("a" "b") {}\n}\n'), twice),
        ("two of one name, one spelled with an escape",
         with_suite('TEST_CASE("t") {\n SECTION("ab") {}\n SECTION("a\\x62") {}\n}\n'), twice),
        ("a section named by a variable", with_suite('TEST_CASE("t") {\n SECTION(name) {}\n}\n'), plain),
        ("a section named by a std::string",
         with_suite('TEST_CASE("t") {\n SECTION(std::string("x")) {}\n}\n'), plain),
        ("a section named by a raw literal", with_suite('TEST_CASE("t") {\n SECTION(R"(x)") {}\n}\n'), plain),
        ("a section named by a prefixed literal", with_suite('TEST_CASE("t") {\n SECTION(u8"x") {}\n}\n'),
         plain),
        ("a section with no name", with_suite('TEST_CASE("t") {\n SECTION() {}\n}\n'), plain),
        ("a section in a helper function", with_suite('void helper() {\n SECTION("x") {}\n}\n'),
         "outside every test case"),
        ("a suite includes Catch2's macros",
         {**clean, "test_b.cpp": '#include <catch2/catch_test_macros.hpp>\n#include "tests/TestCase.hpp"\n'},
         "includes catch2/catch_test_macros.hpp"),
        ("a support header includes Catch2's macros",
         {**clean, "Support.hpp": '#include "catch2/catch_test_macros.hpp"\n'},
         "includes catch2/catch_test_macros.hpp"),
        ("a suite writes a TEST_CASE without the header", {**clean, "test_b.cpp": 'TEST_CASE("t") {}\n'},
         "without including"),
        ("a TEST_CASE without a body", with_suite('TEST_CASE("t");\n'), "body this check cannot find"),
        ("TEST_CASE_METHOD", with_suite('TEST_CASE_METHOD(Fixture, "t") {}\n'), other),
        ("SCENARIO", with_suite('SCENARIO("t") {}\n'), other),
        ("TEMPLATE_TEST_CASE", with_suite('TEMPLATE_TEST_CASE("t", "", int) {}\n'), other),
        ("TEMPLATE_LIST_TEST_CASE", with_suite('TEMPLATE_LIST_TEST_CASE("t", "", Types) {}\n'), other),
        ("REGISTER_TEST_CASE", with_suite('void f();\nREGISTER_TEST_CASE(f, "t");\n'), other),
        ("METHOD_AS_TEST_CASE", with_suite('METHOD_AS_TEST_CASE(C::f, "t")\n'), other),
        ("DYNAMIC_SECTION", with_suite('TEST_CASE("t") {\n DYNAMIC_SECTION("x" << 1) {}\n}\n'), other),
        ("GIVEN", with_suite('TEST_CASE("t") {\n GIVEN("x") {}\n}\n'), other),
        ("AND_THEN", with_suite('TEST_CASE("t") {\n AND_THEN("x") {}\n}\n'), other),
        ("the header does not define TEST_CASE", {**clean, HEADER: "#include <catch2/catch_test_macros.hpp>\n"},
         "does not define TEST_CASE"),
        ("the header does not include Catch2", {**clean, HEADER: "#define TEST_CASE(...) x\n"},
         "does not include catch2/catch_test_macros.hpp"),
        ("the header is missing", {k: v for k, v in clean.items() if k != HEADER}, "is missing"),
        ("no test case anywhere", {HEADER: header, "Support.hpp": support}, "no TEST_CASE was found"),
        ("nothing to read", {}, "no file was found"),
    ]
    failures = 0
    for name, files, reason in cases:
        found = problems(files)[0]
        if reason is None and found:
            failures += 1
            print(f"self-test FAILED: {name}: refused it: {found}")
        elif reason is not None and not any(reason in line for line in found):
            failures += 1
            print(f"self-test FAILED: {name}: expected a refusal saying '{reason}', got {found}")

    # The clean tree's counts, which say that the comments and the strings
    # were not read as sections and the real ones were: 2 test cases; 5
    # sections, 4 in the first and 1 in the second.
    _, counted_cases, counted_sections = problems(clean)
    if (counted_cases, counted_sections) != (2, 5):
        failures += 1
        print(f"self-test FAILED: the clean tree counted {counted_cases} test cases and "
              f"{counted_sections} sections, not 2 and 5")

    # And through the file system once, as the real check reads it: the clean
    # tree must pass, and one with a repeated section must fail.
    with tempfile.TemporaryDirectory() as scratch:
        root = pathlib.Path(scratch)
        (root / "tests").mkdir()
        for name, text in clean.items():
            (root / "tests" / name).write_text(text, encoding="utf-8")
        if check(root) != 0:
            failures += 1
            print("self-test FAILED: the clean tree on disk was refused")
        (root / "tests" / "test_b.cpp").write_text(
            '#include "tests/TestCase.hpp"\nTEST_CASE("t") {\n SECTION("x") {}\n SECTION("x") {}\n}\n',
            encoding="utf-8")
        if check(root) == 0:
            failures += 1
            print("self-test FAILED: a tree on disk with a repeated section passed")
    total = len(cases) + 3
    print(f"check-test-cases self-test: {total - failures} of {total} cases")
    return 1 if failures else 0


def main() -> int:
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    return check(pathlib.Path(sys.argv[1]))


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Write the list of every warning gcc has, for CMakeLists.txt to switch on.

clang has -Weverything; gcc does not. This is the nearest thing: it asks the
compiler for every warning it knows, keeps those that apply to C++, and writes
them to cmake/GccWarnings.cmake, which CMakeLists.txt includes for a gcc build.
ADR 0017 is why; the owner's rule is as many warnings as possible, as errors.

Re-run it whenever gcc is upgraded, from inside WSL, and commit the result:

    python3 scripts/gcc-warnings.py g++-14 cmake/GccWarnings.cmake

A new gcc brings new warnings, and they arrive the same way a new clang's do
under -Weverything: as errors, to be triaged.

Three things are not switched on, and each is here with its reason rather than
silently absent:

  * the exclusions below, each a ruling or a warning that is not about code;
  * any flag this compiler rejects for C++ -- the script compiles an empty
    file with each one and drops those that fail, printing them;
  * warnings that need a size or a threshold to mean anything
    (-Wlarger-than=, -Wframe-larger-than=, -Wstack-usage=, ...): a number
    chosen here would be arbitrary, and the default is off.

Exit codes: 0 written, 1 the compiler could not be queried, 2 usage.
"""

from __future__ import annotations

import datetime
import os
import re
import subprocess
import sys
import tempfile

# Switched off, and why. Each is a decision; see ADR 0017.
EXCLUDED = {
    "-Wsystem-headers": "reports inside dependencies' headers, which are not "
    "this project's code; a library's interface is answered at the site",
    "-Wabi": "compatibility with the ABI of other gcc versions: about linking "
    "against their output, not about this code",
    "-Wnamespaces": "warns on every namespace: for style guides that ban the "
    "feature, and this project uses namespaces by design",
    "-Wtemplates": "warns on every template, likewise",
    "-Wmultiple-inheritance": "warns on every use of the feature, likewise",
    "-Wvirtual-inheritance": "warns on every use of the feature, likewise",
    "-Wswitch-default": "demands a default: in every switch, which hides a "
    "newly added enumerator from -Wswitch -- ruled 2026-09-11",
    "-Wpadded": "reports struct layout, not style or correctness -- ruled "
    "2026-09-11",
    "-Weffc++": "Effective C++, 2nd edition (1998): on this code it reports "
    "only the aggregates Elements and OrbitInfo, whose members initialize "
    "themselves as CODING_GUIDELINES section 6 asks, for not naming them in a "
    "member initializer list that needs a constructor they do not have -- "
    "ruled 2026-09-11",
}

# The strictest level each levelled warning defines, per the gcc manual.
LEVELLED = [
    "-Wimplicit-fallthrough=5",  # only [[fallthrough]] counts
    "-Wcast-align=strict",
    "-Wformat=2",
    "-Wformat-overflow=2",
    "-Wformat-truncation=2",
    "-Wshift-overflow=2",
    "-Wunused-const-variable=2",
    "-Wstringop-overflow=4",
    "-Warray-bounds=2",
    "-Wplacement-new=2",
    "-Wcatch-value=3",
    "-Wbidi-chars=any",
    "-Wuse-after-free=3",
    "-Wdangling-pointer=2",
    "-Wstrict-overflow=5",
    "-Wstrict-aliasing=1",
    "-Wattribute-alias=2",
]

STATE = re.compile(r"^\s+(-W[\w+-]+)\s+(\[disabled\]|\[available in C\+\+, ObjC\+\+\])\s*$")


def query(compiler: str) -> list[str]:
    """Every plain warning the compiler lists as off, or as C++-only.

    Asked without a source language, gcc reports its C++-only warnings as
    "[available in C++, ObjC++]" rather than as on or off. Those are the ones
    that matter most here, so they are taken as well; switching on a warning
    that is already on changes nothing.
    """
    listing = subprocess.run([compiler, "-Q", "--help=warning,c++"],
                             capture_output=True, text=True, check=True).stdout
    found = []
    for line in listing.splitlines():
        m = STATE.match(line)
        if m and m[1] not in found:
            found.append(m[1])
    return found


def accepted(compiler: str, flag: str, source: str) -> bool:
    """Whether the compiler takes the flag for C++ without complaint."""
    result = subprocess.run([compiler, "-x", "c++", "-std=c++23", "-fsyntax-only", "-Werror",
                             flag, source], capture_output=True, text=True)
    return result.returncode == 0


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    compiler, output = argv[1], argv[2]
    try:
        version = subprocess.run([compiler, "--version"], capture_output=True, text=True,
                                 check=True).stdout.splitlines()[0]
        candidates = query(compiler)
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"cannot query {compiler}: {error}", file=sys.stderr)
        return 1

    with tempfile.NamedTemporaryFile("w", suffix=".cpp", delete=False) as empty:
        empty.write("int main() { return 0; }\n")
    try:
        rejected = [f for f in candidates + LEVELLED
                    if f not in EXCLUDED and not accepted(compiler, f, empty.name)]
    finally:
        os.unlink(empty.name)

    plain = sorted(f for f in candidates if f not in EXCLUDED and f not in rejected)
    flags = ["-Wall", "-Wextra", "-Wpedantic"] + [f for f in plain if f not in
                                                  ("-Wall", "-Wextra", "-Wpedantic")]
    flags += [f for f in LEVELLED if f not in rejected]

    today = datetime.date.today().isoformat()
    with open(output, "w", encoding="utf-8", newline="\n") as out:
        out.write("# Generated by scripts/gcc-warnings.py -- do not edit by hand; re-run it.\n")
        out.write(f"# From {version}, on {today}.\n")
        out.write("#\n")
        out.write("# Every warning this gcc has that applies to C++, switched on, because the\n")
        out.write("# owner's rule is as many warnings as possible, as errors (ADR 0017).\n")
        out.write("# Switched off, each with its reason in the script:\n")
        for flag, reason in EXCLUDED.items():
            out.write(f"#   {flag}\n")
        if rejected:
            out.write("# Rejected by this compiler for C++, and so absent:\n")
            for flag in rejected:
                out.write(f"#   {flag}\n")
        out.write("set(ORBSIM_GCC_WARNINGS\n")
        for flag in flags:
            out.write(f"    {flag}\n")
        out.write(")\n")

    print(f"{output}: {len(flags)} warnings from {version}; "
          f"{len(EXCLUDED)} excluded, {len(rejected)} rejected by the compiler")
    for flag in rejected:
        print(f"  rejected: {flag}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

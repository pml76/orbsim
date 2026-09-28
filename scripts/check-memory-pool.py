#!/usr/bin/env python3
"""Check that the heavy build steps run in the memory-sized pool (M1-88, M1-96).

    check-memory-pool.py <build directory>
    check-memory-pool.py --self-test

Why this exists: each clang-tidy process needs about 1.04 GB on this project
(measured 2026-09-27, docs/measurements/verification-cost.md), and Ninja's
default parallelism ran a lint step out of memory beside an open IDE. So the
lint steps share a Ninja pool whose depth is the machine's physical memory
divided by 3 GiB, at least 1 (ADR 0024, register decision 208). Since M1-96
this project's own compiles share it too (decision 222): a full rebuild ran
`check` out of memory on 2026-09-27 with about 20 compiles, at 0.6-1 GB each,
beside the 10 lint jobs. A heavy step missing from the pool, or a depth that
does not follow the rule, would not fail anything else: the build still runs,
only with more processes at once than the machine holds. This makes that loud.

"This project's own compiles" are those of the targets the top-level
CMakeLists.txt defines: a compile rule whose object lies under CMakeFiles/,
not under a dependency's _deps/ directory, which keeps Ninja's default.

It reads the Ninja files CMake generated, not CMakeLists.txt, because the
claim is about what Ninja will run. And it computes the expected depth itself,
from the operating system's own figure for physical memory, rather than
reading CMake's -- a second implementation of the rule, not a copy of it.

Proven able to fail before it was trusted: --self-test feeds it Ninja text
with a lint step outside the pool, a compile of this project outside it, the
wrong depth, no pool at all, no lint steps and no compiles, and exits non-zero
unless every one is reported -- and accepts a dependency's compile outside it.
"""

import ctypes
import os
import pathlib
import re
import sys

POOL = "orbsim_memory"
GIB_PER_JOB = 3  # ADR 0024: about 1.04 GB per clang-tidy, 0.6-1 GB per compile, the rest headroom


def physical_memory_mib() -> int:
    """Total physical memory in MiB, from the operating system."""
    if os.name == "nt":
        class MemoryStatus(ctypes.Structure):
            _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong),
                        ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong),
                        ("ullTotalPageFile", ctypes.c_ulonglong),
                        ("ullAvailPageFile", ctypes.c_ulonglong),
                        ("ullTotalVirtual", ctypes.c_ulonglong),
                        ("ullAvailVirtual", ctypes.c_ulonglong),
                        ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]
        status = MemoryStatus()
        status.dwLength = ctypes.sizeof(MemoryStatus)
        if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
            raise OSError("GlobalMemoryStatusEx failed")
        return status.ullTotalPhys // (1024 * 1024)
    return os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES") // (1024 * 1024)


def expected_depth(memory_mib: int) -> int:
    return max(1, memory_mib // (GIB_PER_JOB * 1024))


def in_pool(body: str) -> bool:
    return re.search(rf"^\s+pool\s*=\s*{POOL}\s*$", body, re.MULTILINE) is not None


def problems(ninja_text: str, depth: int) -> list:
    """Everything wrong with the memory pool in this Ninja text."""
    found = []
    pools = re.findall(r"^pool\s+(\S+)\s*\n\s+depth\s*=\s*(\d+)", ninja_text, re.MULTILINE)
    declared = {name: int(value) for name, value in pools}
    if POOL not in declared:
        found.append(f"no pool named {POOL}")
    elif declared[POOL] != depth:
        found.append(f"pool {POOL} has depth {declared[POOL]}, the rule gives {depth}")

    # A build edge: "build <outputs>: <rule> <inputs>" followed by indented variables.
    edges = re.findall(r"^build ([^\n]*?): (\S+)[^\n]*\n((?:[ \t]+[^\n]*\n)*)", ninja_text, re.MULTILINE)

    # `lint` as a whole directory name: `notlint/a.ok` is not a lint step.
    lint_edges = [(outputs, body) for outputs, _, body in edges
                  if re.search(r"(^|[/\\ ])lint[/\\][^ /\\]*\.ok", outputs)]
    if not lint_edges:
        found.append("no lint steps found -- the check would be checking nothing")
    for outputs, body in lint_edges:
        if not in_pool(body):
            found.append(f"lint step outside the pool: {outputs.split()[0]}")

    # This project's own compiles: a compile rule, and an object under
    # CMakeFiles/ rather than under a dependency's _deps/ directory.
    compiles = [(outputs.split()[0], body) for outputs, rule, body in edges
                if re.match(r"(C|CXX)_COMPILER__", rule) and outputs.startswith("CMakeFiles/")]
    if not compiles:
        found.append("no compiles of this project's own targets found -- the check would be checking nothing")
    outside = [obj for obj, body in compiles if not in_pool(body)]
    for obj in outside[:5]:
        found.append(f"compile outside the pool: {obj}")
    if len(outside) > 5:
        found.append(f"... and {len(outside) - 5} more compiles of this project outside the pool")
    return found


def check(build: pathlib.Path) -> int:
    text = "".join(p.read_text(encoding="utf-8", errors="replace")
                   for p in (build / "build.ninja", build / "CMakeFiles" / "rules.ninja") if p.exists())
    depth = expected_depth(physical_memory_mib())
    found = problems(text, depth)
    for line in found:
        print(f"check-memory-pool: {line}")
    if found:
        return 1
    print(f"check-memory-pool: every lint step and every compile of this project is in {POOL}, depth {depth}")
    return 0


def self_test() -> int:
    good = (f"pool {POOL}\n  depth = 4\n\n"
            f"build lint/a.cpp.ok: CUSTOM_COMMAND x\n  COMMAND = tidy\n  pool = {POOL}\n\n"
            f"build lint/b.cpp.ok: CUSTOM_COMMAND y\n  COMMAND = tidy\n  pool = {POOL}\n\n"
            f"build CMakeFiles/t.dir/a.cpp.obj: CXX_COMPILER__t_unscanned_Debug a.cpp\n  FLAGS = -O2\n  pool = {POOL}\n\n"
            "build _deps/x-build/CMakeFiles/x.dir/y.c.obj: C_COMPILER__x_unscanned_Debug y.c\n  FLAGS = -O2\n")
    cases = {
        "a correct pool, a dependency's compile outside it": (good, 4, 0),
        "a lint step outside the pool": (good.replace(f"  pool = {POOL}\n\nbuild lint/b", "\nbuild lint/b"), 4, 1),
        "a compile of this project outside the pool": (
            good.replace(f"  FLAGS = -O2\n  pool = {POOL}\n", "  FLAGS = -O2\n"), 4, 1),
        "the wrong depth": (good, 5, 1),
        "no pool at all": (good.replace(f"pool {POOL}\n  depth = 4\n\n", ""), 4, 1),
        # Each "nothing found" case keeps everything else correct, so that it is
        # the case's only fault -- M1-88's pass found one that was reported for
        # a missing pool instead, and so tested nothing of its own.
        "no lint steps": (good.replace("build lint/", "build notlint/"), 4, 1),
        "no compiles of this project": (good.replace("build CMakeFiles/t.dir/", "build _deps/t-build/"), 4, 1),
    }
    failures = 0
    for name, (text, depth, want_problem) in cases.items():
        got = 1 if problems(text, depth) else 0
        status = "ok" if got == want_problem else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name}: {'reported' if got else 'accepted'} ({status})")
    if expected_depth(32208) != 10 or expected_depth(2000) != 1:
        print("self-test: the depth rule is wrong (WRONG)")
        failures += 1
    return 1 if failures else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return self_test()
    if len(sys.argv) == 2:
        return check(pathlib.Path(sys.argv[1]))
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())

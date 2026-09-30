#!/usr/bin/env python3
"""Check that `check` runs the tests in parallel, and the GPU tests never together (M1-91).

    check-parallel-tests.py <build directory> <ctest executable> --gpu-tests expected|none
    check-parallel-tests.py --self-test

Why this exists: `check` ran every test one at a time -- 48-52 s in release and
103-113 s in Debug, measured 2026-09-27 -- although nearly every test is an
independent program. Since M1-91 it runs `ctest -j N`, with N half the
processor threads (ADR 0024, register decision 211), and every test that needs
the GPU holds one shared lock, `RESOURCE_LOCK gpu`, so no two of them run at
once. A GPU test added later without the lock would pass on most runs and
fail on some, which is the worst kind of test. So:

  * every test labelled `gpu` must hold the lock `gpu` -- read from CTest's own
    description of the tests, `ctest --show-only=json-v1`, not from CMake;
  * there must be at least one such test, or this would check nothing -- in a
    tree that builds the application, which CMake states as
    `--gpu-tests expected`. A tree that builds the core only, as the two Linux
    presets do, states `--gpu-tests none`, and there a test labelled `gpu` is
    the fault instead (M1-100). There is no default: a tree that stated
    nothing would be checked against a guess;
  * the `check` target's ctest command must carry `-j N`, with N computed here
    again from the operating system's thread count, a second implementation of
    the rule rather than a copy of it.

Proven able to fail before it was trusted: --self-test feeds it a GPU test
without the lock, no GPU tests, a command without -j and one with the wrong
N, and exits non-zero unless each is reported -- and, for a core-only tree, a
GPU test, no -j and the wrong N, and requires that tree's correct listing to
be accepted.
"""

import json
import os
import pathlib
import re
import subprocess
import sys


GPU_TESTS = ("expected", "none")


def expected_jobs(threads: int) -> int:
    return max(1, threads // 2)


def problems(tests: list, check_command: str, jobs: int, gpu_tests: str) -> list:
    found = []
    gpu = [t for t in tests
           if any(p["name"] == "LABELS" and "gpu" in p["value"] for p in t.get("properties", []))]
    if gpu_tests == "expected" and not gpu:
        found.append("no test is labelled gpu -- the check would be checking nothing")
    if gpu_tests == "none":
        found.extend(f"GPU test in a tree that builds no application: {t['name']}" for t in gpu)
    for test in gpu:
        locks = [p["value"] for p in test.get("properties", []) if p["name"] == "RESOURCE_LOCK"]
        if not any("gpu" in value for value in locks):
            found.append(f"GPU test without the gpu lock: {test['name']}")
    match = re.search(r"--output-on-failure\s+-j\s*(\d+)|-j\s*(\d+)\s+--output-on-failure", check_command)
    if not match:
        found.append("the check target's ctest command runs the tests one at a time (no -j)")
    else:
        given = int(match.group(1) or match.group(2))
        if given != jobs:
            found.append(f"the check target runs ctest -j {given}, the rule gives {jobs}")
    return found


def check(build: pathlib.Path, ctest: str, gpu_tests: str) -> int:
    listing = subprocess.run([ctest, "--show-only=json-v1"], cwd=build, capture_output=True,
                             text=True, encoding="utf-8", errors="replace", check=True).stdout
    tests = json.loads(listing)["tests"]
    ninja = (build / "build.ninja").read_text(encoding="utf-8", errors="replace")
    commands = [line for line in ninja.splitlines()
                if "--output-on-failure" in line and "ctest" in line.lower()]
    found = problems(tests, "\n".join(commands), expected_jobs(os.cpu_count() or 1), gpu_tests)
    for line in found:
        print(f"check-parallel-tests: {line}")
    if found:
        return 1
    gpu_line = "every GPU test holds the gpu lock" if gpu_tests == "expected" else "no test needs the GPU"
    print(f"check-parallel-tests: ctest -j {expected_jobs(os.cpu_count() or 1)}, and {gpu_line}")
    return 0


def self_test() -> int:
    def test(name, gpu, locked):
        props = [{"name": "LABELS", "value": ["gpu"] if gpu else ["fixtures"]}]
        if locked:
            props.append({"name": "RESOURCE_LOCK", "value": ["gpu"]})
        return {"name": name, "properties": props}

    good_tests = [test("smoke", True, True), test("probe", True, True), test("math", False, False)]
    good_cmd = "ctest.exe --output-on-failure -j 10"
    core_tests = [test("math", False, False)]
    cases = {
        "all correct": (good_tests, good_cmd, 10, "expected", False),
        "a GPU test without the lock": (good_tests[:1] + [test("probe", True, False)], good_cmd, 10, "expected", True),
        "no GPU tests": (core_tests, good_cmd, 10, "expected", True),
        "no -j": (good_tests, "ctest.exe --output-on-failure", 10, "expected", True),
        "the wrong N": (good_tests, good_cmd, 8, "expected", True),
        # A tree that builds the core only (M1-100): no GPU test is what is right
        # there, a GPU test is what is wrong, and -j is checked as everywhere.
        "core only, all correct": (core_tests, good_cmd, 10, "none", False),
        "core only, a GPU test": (core_tests + [test("smoke", True, True)], good_cmd, 10, "none", True),
        "core only, no -j": (core_tests, "ctest.exe --output-on-failure", 10, "none", True),
        "core only, the wrong N": (core_tests, good_cmd, 8, "none", True),
    }
    failures = 0
    for name, (tests, cmd, jobs, gpu_tests, want) in cases.items():
        got = bool(problems(tests, cmd, jobs, gpu_tests))
        status = "ok" if got == want else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name}: {'reported' if got else 'accepted'} ({status})")
    if expected_jobs(20) != 10 or expected_jobs(1) != 1:
        print("self-test: the jobs rule is wrong (WRONG)")
        failures += 1
    return 1 if failures else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return self_test()
    if len(sys.argv) == 5 and sys.argv[3] == "--gpu-tests" and sys.argv[4] in GPU_TESTS:
        return check(pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[4])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())

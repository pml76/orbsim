#!/usr/bin/env python3
"""Check that `check` runs the tests in parallel, and the GPU tests never together (M1-91).

    check-parallel-tests.py <build directory> <ctest executable>
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
  * there must be at least one such test, or this would check nothing;
  * the `check` target's ctest command must carry `-j N`, with N computed here
    again from the operating system's thread count, a second implementation of
    the rule rather than a copy of it.

Proven able to fail before it was trusted: --self-test feeds it a GPU test
without the lock, no GPU tests, a command without -j and one with the wrong
N, and exits non-zero unless each is reported.
"""

import json
import os
import pathlib
import re
import subprocess
import sys


def expected_jobs(threads: int) -> int:
    return max(1, threads // 2)


def problems(tests: list, check_command: str, jobs: int) -> list:
    found = []
    gpu = [t for t in tests
           if any(p["name"] == "LABELS" and "gpu" in p["value"] for p in t.get("properties", []))]
    if not gpu:
        found.append("no test is labelled gpu -- the check would be checking nothing")
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


def check(build: pathlib.Path, ctest: str) -> int:
    listing = subprocess.run([ctest, "--show-only=json-v1"], cwd=build, capture_output=True,
                             text=True, encoding="utf-8", errors="replace", check=True).stdout
    tests = json.loads(listing)["tests"]
    ninja = (build / "build.ninja").read_text(encoding="utf-8", errors="replace")
    commands = [line for line in ninja.splitlines()
                if "--output-on-failure" in line and "ctest" in line.lower()]
    found = problems(tests, "\n".join(commands), expected_jobs(os.cpu_count() or 1))
    for line in found:
        print(f"check-parallel-tests: {line}")
    if found:
        return 1
    print(f"check-parallel-tests: ctest -j {expected_jobs(os.cpu_count() or 1)}, "
          f"and every GPU test holds the gpu lock")
    return 0


def self_test() -> int:
    def test(name, gpu, locked):
        props = [{"name": "LABELS", "value": ["gpu"] if gpu else ["fixtures"]}]
        if locked:
            props.append({"name": "RESOURCE_LOCK", "value": ["gpu"]})
        return {"name": name, "properties": props}

    good_tests = [test("smoke", True, True), test("probe", True, True), test("math", False, False)]
    good_cmd = "ctest.exe --output-on-failure -j 10"
    cases = {
        "all correct": (good_tests, good_cmd, 10, False),
        "a GPU test without the lock": (good_tests[:1] + [test("probe", True, False)], good_cmd, 10, True),
        "no GPU tests": ([test("math", False, False)], good_cmd, 10, True),
        "no -j": (good_tests, "ctest.exe --output-on-failure", 10, True),
        "the wrong N": (good_tests, good_cmd, 8, True),
    }
    failures = 0
    for name, (tests, cmd, jobs, want) in cases.items():
        got = bool(problems(tests, cmd, jobs))
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
    if len(sys.argv) == 3:
        return check(pathlib.Path(sys.argv[1]), sys.argv[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())

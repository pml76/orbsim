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
  * every test that runs a script asking Ninja for the tree's header record
    (`ninja -t deps`, from mutants-due.py) must hold the lock `ninja_deps`:
    past a size threshold Ninja rewrites that record whenever it is read, and
    two readers at once on Windows can destroy it (M1-102);
  * the `check` and `check-full` targets' commands must carry the job count N,
    with N computed here again from the operating system's thread count, a
    second implementation of the rule rather than a copy of it. Since M1-129
    they run the tests through scripts/run-tests.py, which takes it as
    `--jobs N` and hands it to ctest as `-j N` (register decision 555); before
    that they called `ctest -j N` themselves.

Proven able to fail before it was trusted: --self-test feeds it a GPU test
without the lock, no GPU tests, no driver command, a command without --jobs
and one with the wrong N, and exits non-zero unless each is reported -- and,
for a core-only tree, a GPU test, no --jobs and the wrong N, and requires that
tree's correct listing to be accepted.
"""

import json
import os
import pathlib
import re
import subprocess
import sys


GPU_TESTS = ("expected", "none")
# The scripts that ask Ninja for a tree's header record, `ninja -t deps`. Past
# a size threshold Ninja rewrites that record whenever it is read, and on
# Windows two readers at once can destroy it (M1-102, reproduced 2026-09-30):
# so a test running one of these on a tree holds the lock `ninja_deps`.
NINJA_QUERIES = ("mutants-due.py",)
# The driver `check` and `check-full` run the tests through (M1-129).
RUN_TESTS = "run-tests.py"


def expected_jobs(threads: int) -> int:
    return max(1, threads // 2)


def problems(tests: list, check_commands: list, jobs: int, gpu_tests: str) -> list:
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
    for test in tests:
        command = [str(part) for part in test.get("command", [])]
        asks_ninja = any(part.endswith(NINJA_QUERIES) for part in command) and "--self-test" not in command
        locks = [p["value"] for p in test.get("properties", []) if p["name"] == "RESOURCE_LOCK"]
        if asks_ninja and not any("ninja_deps" in value for value in locks):
            found.append(f"a test that asks Ninja for the header record without the ninja_deps lock: {test['name']}")
    if not check_commands:
        found.append("no target runs the tests through run-tests.py")
    for command in check_commands:
        match = re.search(r"--jobs\s+(\d+)", command)
        if not match:
            found.append(f"a check command runs the tests one at a time (no --jobs): {command}")
        elif int(match.group(1)) != jobs:
            found.append(f"a check command runs --jobs {match.group(1)}, the rule gives {jobs}: {command}")
    return found


def check(build: pathlib.Path, ctest: str, gpu_tests: str) -> int:
    listing = subprocess.run([ctest, "--show-only=json-v1"], cwd=build, capture_output=True,
                             text=True, encoding="utf-8", errors="replace", check=True).stdout
    tests = json.loads(listing)["tests"]
    ninja = (build / "build.ninja").read_text(encoding="utf-8", errors="replace")
    commands = [line.strip() for line in ninja.splitlines() if RUN_TESTS in line]
    found = problems(tests, commands, expected_jobs(os.cpu_count() or 1), gpu_tests)
    for line in found:
        print(f"check-parallel-tests: {line}")
    if found:
        return 1
    gpu_line = "every GPU test holds the gpu lock" if gpu_tests == "expected" else "no test needs the GPU"
    print(f"check-parallel-tests: --jobs {expected_jobs(os.cpu_count() or 1)} in {len(commands)} commands, "
          f"and {gpu_line}")
    return 0


def self_test() -> int:
    def test(name, gpu, locked):
        props = [{"name": "LABELS", "value": ["gpu"] if gpu else ["fixtures"]}]
        if locked:
            props.append({"name": "RESOURCE_LOCK", "value": ["gpu"]})
        return {"name": name, "properties": props}

    def query(name, locked, argument="build/tree"):
        props = [{"name": "LABELS", "value": ["fixtures"]}]
        if locked:
            props.append({"name": "RESOURCE_LOCK", "value": ["ninja_deps"]})
        return {"name": name, "command": ["python", "scripts/mutants-due.py", argument],
                "properties": props}

    good_tests = [test("smoke", True, True), test("probe", True, True), test("math", False, False)]
    good_cmd = ["python scripts/run-tests.py build/tree --ctest ctest.exe --jobs 10",
                "python scripts/run-tests.py build/tree --ctest ctest.exe --jobs 10 --full"]
    no_jobs = [good_cmd[0], "python scripts/run-tests.py build/tree --ctest ctest.exe --full"]
    core_tests = [test("math", False, False)]
    cases = {
        "all correct": (good_tests, good_cmd, 10, "expected", False),
        "a GPU test without the lock": (good_tests[:1] + [test("probe", True, False)], good_cmd, 10, "expected", True),
        "no GPU tests": (core_tests, good_cmd, 10, "expected", True),
        "no driver command": (good_tests, [], 10, "expected", True),
        "no --jobs in one command": (good_tests, no_jobs, 10, "expected", True),
        "the wrong N": (good_tests, good_cmd, 8, "expected", True),
        # A tree that builds the core only (M1-100): no GPU test is what is right
        # there, a GPU test is what is wrong, and -j is checked as everywhere.
        "core only, all correct": (core_tests, good_cmd, 10, "none", False),
        "core only, a GPU test": (core_tests + [test("smoke", True, True)], good_cmd, 10, "none", True),
        "core only, no --jobs in one command": (core_tests, no_jobs, 10, "none", True),
        "core only, the wrong N": (core_tests, good_cmd, 8, "none", True),
        # A test that asks Ninja for the tree's header record (M1-102): two at
        # once can destroy it, so each holds the ninja_deps lock. The script's
        # self-test reads no tree and needs none.
        "a Ninja query holding the lock": (good_tests + [query("due", True)], good_cmd, 10, "expected", False),
        "a Ninja query without the lock": (good_tests + [query("due", False)], good_cmd, 10, "expected", True),
        "a Ninja query without the lock, core only": (core_tests + [query("due", False)], good_cmd, 10, "none", True),
        "the query script's self-test, without the lock": (
            good_tests + [query("due_self_test", False, "--self-test")], good_cmd, 10, "expected", False),
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

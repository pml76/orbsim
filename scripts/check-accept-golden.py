#!/usr/bin/env python3
"""Check that no test can accept a golden image (M1-17, register decision 233).

    check-accept-golden.py <build directory> <ctest executable>
    check-accept-golden.py --self-test

Why this exists: a golden image is a frame the owner has approved (ADR 0008),
and `--accept-golden` is how one is written. It is run by the owner, never by
a script and never from `check` -- a golden re-accepted to clear a failure is
the tolerance loosened until the test passes, with a picture attached. The
help text says so; this makes a machine say so too. Read from CTest's own
description of the tests, `ctest --show-only=json-v1`:

  * no test's command line may name both `--accept-golden` and `--golden`,
    the only combination that can write a golden. `--golden` is matched
    anywhere in an argument, so `--golden-dir` (M1-110, register decision
    289) counts as it, and the self-test proves that it does.
    `--accept-golden` alone is allowed: it is refused as a usage error, and
    the usage tests check that;
  * no script a test runs -- a `.cmake` or `.py` file named on its command
    line -- may contain `--accept-golden` at all, since a script's own
    arguments cannot be seen from the command line. This script is the one
    exception, because it has to name the flag to look for it;
  * at least one script a test runs must pass `--golden`, or this would be
    looking in the wrong place and passing anyway (VERIFICATION.md rule 23).

Proven able to fail before it was trusted: --self-test feeds it a command with
both flags, a script with the flag, and no golden anywhere, and exits
non-zero unless each is reported and a clean set is not.
"""

import json
import pathlib
import subprocess
import sys
import tempfile

ACCEPT = "--accept-golden"
GOLDEN = "--golden"
SELF = pathlib.Path(__file__).resolve()


def scripts_of(command: list) -> list:
    """The .cmake and .py files a command names, other than this one."""
    found = []
    for argument in command:
        path = pathlib.Path(argument)
        if path.suffix in (".cmake", ".py") and path.is_file() and path.resolve() != SELF:
            found.append(path)
    return found


def problems(tests: list) -> list:
    found = []
    golden_seen = False
    for test in tests:
        command = test.get("command", [])
        name = test.get("name", "?")
        if any(ACCEPT in a for a in command) and any(GOLDEN in a for a in command):
            found.append(f"{name} runs {ACCEPT} with {GOLDEN}, which writes a golden")
        for script in scripts_of(command):
            text = script.read_text(encoding="utf-8", errors="replace")
            if ACCEPT in text:
                found.append(f"{name} runs {script}, which contains {ACCEPT}")
            golden_seen = golden_seen or GOLDEN in text
    if not golden_seen:
        found.append(f"no script a test runs passes {GOLDEN} -- the check would be checking nothing")
    return found


def check(build: pathlib.Path, ctest: str) -> int:
    listing = subprocess.run([ctest, "--show-only=json-v1"], cwd=build, capture_output=True,
                             text=True, encoding="utf-8", errors="replace", check=True).stdout
    tests = json.loads(listing)["tests"]
    found = problems(tests)
    for line in found:
        print(f"check-accept-golden: {line}")
    if found:
        return 1
    print(f"check-accept-golden: {len(tests)} tests, none can accept a golden")
    return 0


def self_test() -> int:
    with tempfile.TemporaryDirectory() as scratch:
        directory = pathlib.Path(scratch)
        comparing = directory / "compare.cmake"
        comparing.write_text(f'execute_process(COMMAND orbsim {GOLDEN} "${{GOLDEN}}")\n',
                             encoding="utf-8")
        accepting = directory / "accept.cmake"
        accepting.write_text(f"execute_process(COMMAND orbsim {GOLDEN} x.png {ACCEPT})\n",
                             encoding="utf-8")

        def test(name, *command):
            return {"name": name, "command": list(command)}

        probe = test("probe", "cmake", "-DGOLDEN=g.png", "-P", str(comparing))
        usage = test("usage", "cmake", f"-DARGS=--probe|clear|{ACCEPT}", "-P", "RunUsage.cmake")
        cases = {
            "a clean set": ([probe, usage], False),
            "a command with both flags": ([probe, test("bad", "orbsim", GOLDEN, "g.png", ACCEPT)],
                                          True),
            "a command with --golden-dir and the flag": (
                [probe, test("bad", "orbsim", f"{GOLDEN}-dir", "tests/golden", ACCEPT)], True),
            "a script with the flag": ([probe, test("bad", "cmake", "-P", str(accepting))], True),
            "no golden anywhere": ([usage], True),
        }
        failures = 0
        for name, (tests, want) in cases.items():
            got = bool(problems(tests))
            status = "ok" if got == want else "WRONG"
            failures += status == "WRONG"
            print(f"self-test: {name}: {'reported' if got else 'accepted'} ({status})")
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

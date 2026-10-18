#!/usr/bin/env python3
"""Check that a program compiled and linked again comes out the same, byte for byte (M1-129).

    check-reproducible-build.py <build directory> <ninja> <object> <program>
    check-reproducible-build.py --self-test

Why this exists: M1-129 skips a test when nothing it reads has changed, and a
test reads its program. Until 2026-10-12 an unchanged file compiled and linked
to different bytes every time -- measured over a clean rebuild of each Windows
clang tree, 945 of 963 objects, libraries and programs differed, every program
among them -- so every rebuilt program would have looked changed and every
test would have run. The compiler and linker settings of register decisions
488 and 504 make the same input give the same bytes; this is the instrument
that shows they still do.

tests/reproducible_build_probe.cpp's object and program are built by the tree
as usual. This compiles and links them again, at the same paths and with the
exact commands Ninja's build plan holds for them, and requires the same bytes.
The same paths, because a program names its debug-information file by its full
path; the same commands, so that it is the tree's own settings being checked.
If they come out the same, their old times are put back, so the tree is
exactly as it was and nothing is relinked or re-linted on its account; if not,
the new files stay, and Ninja rebuilds from them as from any other.

**And the same in either shell** (register decision 691): the object is
compiled once more with NO_COLOR set in the environment and once without, and
the two must be the same bytes. clang adds -fno-color-diagnostics to its own
command when NO_COLOR is set, and keeps that command in the object's debug
information, so unless the build names colour itself, one compile gives other
bytes from a shell that sets NO_COLOR than from one that does not -- which
made this test fail whenever its object had been built in the one and the
test ran in the other (measured 2026-10-18).

A script running Ninja's commands rather than Ninja itself, because `check`
runs CTest from inside Ninja, and a second Ninja building in the same tree at
the same time is not safe. `ninja -t commands` only reads the build plan.

Proven able to fail before it was trusted: --self-test feeds the verdict each
of the outcomes it exists to refuse, and exits non-zero unless every one is
refused, for its own reason, and the good outcome accepted.
"""

import hashlib
import os
import pathlib
import subprocess
import sys
from typing import Optional


def verdict(before: dict, after: dict) -> list:
    """Every reason the rebuild does not show the same bytes; empty if it does.

    `before` and `after` map each file's name to (its hash, its time in
    nanoseconds), read before and after the commands ran.
    """
    problems = []
    for name, (old_hash, old_time) in before.items():
        if name not in after:
            problems.append(f"{name} was not there after the rebuild")
            continue
        new_hash, new_time = after[name]
        # A rebuild that wrote nothing would compare equal and prove nothing.
        if new_time <= old_time:
            problems.append(f"{name} was not written again, so the comparison would prove nothing")
        elif new_hash != old_hash:
            problems.append(f"{name} came out different from the same command: the build is "
                            "not reproducible (register decisions 488 and 504)")
    return problems


def state(paths: dict) -> dict:
    """(hash, time in nanoseconds) for each file that exists."""
    found = {}
    for name, path in paths.items():
        if path.is_file():
            found[name] = (hashlib.sha256(path.read_bytes()).hexdigest(), path.stat().st_mtime_ns)
    return found


def plan_command(build_dir: pathlib.Path, ninja: str, output: str) -> str:
    """The one command Ninja's plan runs to make `output`, as Ninja would run it."""
    done = subprocess.run([ninja, "-C", str(build_dir), "-t", "commands", "-s", output],
                          capture_output=True, text=True, check=False)
    lines = done.stdout.splitlines()
    if done.returncode != 0 or len(lines) != 1:
        sys.exit(f"check-reproducible-build: expected one command for {output} from "
                 f"`ninja -t commands -s`, got exit {done.returncode}:\n{done.stdout}{done.stderr}")
    return lines[0]


def run_command(command: str, build_dir: pathlib.Path, environment: Optional[dict] = None) -> None:
    # Ninja hands a command to CreateProcess on Windows and to /bin/sh elsewhere.
    done = subprocess.run(command, cwd=build_dir, shell=os.name != "nt", env=environment,
                          capture_output=True, text=True, check=False)
    if done.returncode != 0:
        sys.exit(f"check-reproducible-build: the command failed, exit {done.returncode}:\n"
                 f"{command}\n{done.stdout}{done.stderr}")


# The variable clang reads to switch colour off (decision 691).
NO_COLOR = "NO_COLOR"


def colour_verdict(name: str, with_variable: str, without: str) -> list:
    """The reason the object's hashes with NO_COLOR set and without show a
    build that depends on it; empty if they are the same."""
    if with_variable != without:
        return [f"{name} came out different with {NO_COLOR} set and without: the build does "
                "not name colour itself (register decision 691)"]
    return []


def colour_case(build_dir: pathlib.Path, command: str, name: str, path: pathlib.Path) -> list:
    """The object compiled with NO_COLOR set, then without, and compared."""
    hashes = []
    for value in ("1", None):
        environment = {k: v for k, v in os.environ.items() if k.upper() != NO_COLOR}
        if value is not None:
            environment[NO_COLOR] = value
        run_command(command, build_dir, environment)
        hashes.append(hashlib.sha256(path.read_bytes()).hexdigest())
    return colour_verdict(name, *hashes)


def run(build_dir: pathlib.Path, ninja: str, obj: pathlib.Path, program: pathlib.Path) -> int:
    # Ninja names an output by its path relative to the tree, with forward slashes.
    files = {pathlib.Path(os.path.relpath(path, build_dir)).as_posix(): path for path in (obj, program)}
    commands = [plan_command(build_dir, ninja, name) for name in files]
    before = state(files)
    if len(before) != len(files):
        sys.exit(f"check-reproducible-build: build {', '.join(files)} first; "
                 f"found only {', '.join(before) or 'neither'}")
    for command in commands:
        run_command(command, build_dir)
    after = state(files)
    problems = verdict(before, after)
    obj_name = next(iter(files))
    problems += colour_case(build_dir, commands[0], obj_name, files[obj_name])
    for problem in problems:
        print(f"check-reproducible-build: {problem}")
    if problems:
        return 1
    for name, path in files.items():
        os.utime(path, ns=(before[name][1], before[name][1]))
    print(f"check-reproducible-build: {' and '.join(files)} compiled and linked again "
          f"to the same bytes, and the object the same with {NO_COLOR} set and without")
    return 0


def self_test() -> int:
    same = {"probe.obj": ("a", 1), "probe.exe": ("b", 2)}
    rewritten = {"probe.obj": ("a", 3), "probe.exe": ("b", 4)}
    # Each case names the reason it must be refused for, not only that it is:
    # a refusal for the wrong reason is a wrong diagnosis (register decision
    # 464). None means the outcome must be accepted.
    different = "came out different"
    unwritten = "was not written again"
    cases = [
        ("both written again, both the same", (same, rewritten), None),
        ("the object differs", (same, {"probe.obj": ("x", 3), "probe.exe": ("b", 4)}), different),
        ("the program differs", (same, {"probe.obj": ("a", 3), "probe.exe": ("x", 4)}), different),
        ("nothing was written", (same, same), unwritten),
        ("the program was not written", (same, {"probe.obj": ("a", 3), "probe.exe": ("b", 2)}),
         unwritten),
        ("the program is gone", (same, {"probe.obj": ("a", 3)}), "was not there"),
    ]
    # And the colour case (decision 691): the same hashes accepted, and two
    # different ones refused for the reason that names it.
    colour_cases = [
        ("the object the same with NO_COLOR and without", ("a", "a"), None),
        ("the object different with NO_COLOR", ("a", "x"), "with NO_COLOR set and without"),
    ]
    failures = 0
    for name, (with_variable, without), reason in colour_cases:
        problems = colour_verdict("probe.obj", with_variable, without)
        if (reason is None) != (not problems) or (reason and not any(reason in p for p in problems)):
            failures += 1
            print(f"self-test FAILED: {name}: got {problems}")
    cases_run = len(cases) + len(colour_cases)
    for name, outcome, reason in cases:
        problems = verdict(*outcome)
        if reason is None and problems:
            failures += 1
            print(f"self-test FAILED: {name}: refused it: {problems}")
        elif reason is not None and not any(reason in problem for problem in problems):
            failures += 1
            print(f"self-test FAILED: {name}: expected a refusal saying '{reason}', got {problems}")
    print(f"check-reproducible-build self-test: {cases_run - failures} of {cases_run} cases")
    return 1 if failures else 0


def main() -> int:
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    build_dir, ninja, obj, program = sys.argv[1:]
    return run(pathlib.Path(build_dir).resolve(), ninja,
               pathlib.Path(obj).resolve(), pathlib.Path(program).resolve())


if __name__ == "__main__":
    sys.exit(main())

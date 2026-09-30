#!/usr/bin/env python3
"""Refuse a build tree set up from text that is no longer on disk (M1-98).

    check-configure-current.py <build directory> <repository root>
    check-configure-current.py --self-test

Why this exists: CMake reads CMakeLists.txt when a configure starts and writes
the build plan when it ends, and Ninja decides whether to configure again by
comparing dates. An edit that lands while a configure runs -- CLion reloading
on its own mid-edit, or a mutation pass writing the file -- leaves a plan built
from the old text but dated after the new, so nothing configures again and
`check` builds and tests the old text in silence. Reproduced on 2026-09-28
(register decision 225, cmake/ConfigureInputs.cmake).

The configure records the SHA-256 -- a fingerprint of the exact bytes -- of
every file it read from this repository, taken as soon as each was read, in
`orbsim-configured-from.sha256` in the tree. This compares each with the file
on disk now, and fails naming the command that sets the tree up again. It is
the `configure-current` step `check` waits for, and the CTest test of the same
name. It computes the fingerprints itself, in Python -- a second
implementation, not a copy of CMake's.

Two more things it requires, because either would make it pass by checking
nothing: the record must list the repository's own CMakeLists.txt, and
`check` in the tree's Ninja file must depend on `configure-current`.

Proven able to fail before it was trusted: --self-test gives it a changed
file, a missing file, no record, an empty one, a record without
CMakeLists.txt, and a `check` that does not wait for it, and exits non-zero
unless each is reported -- and accepts a record that matches.
"""

import hashlib
import os
import pathlib
import re
import sys
import tempfile

RECORD = "orbsim-configured-from.sha256"
STEP = "configure-current"


def same_path(a: str, b: str) -> bool:
    return os.path.normcase(os.path.abspath(a)) == os.path.normcase(os.path.abspath(b))


def fingerprint(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def problems(record_text, root: pathlib.Path, ninja_text: str) -> list:
    """Everything that makes this tree untrustworthy as a picture of the files on disk."""
    found = []
    if record_text is None:
        return [f"no {RECORD} in the tree: it was set up before M1-98, or the record was lost"]
    entries = []
    for line in record_text.splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match:
            found.append(f"a line of {RECORD} is not '<sha256>  <path>': {line!r}")
            continue
        entries.append((match.group(1), match.group(2)))
    # This also refuses an empty record, which would check nothing. A separate
    # "lists no files" check stood here until M1-98's mutation pass showed it
    # could never decide anything on its own (register decision 227).
    if not any(same_path(path, root / "CMakeLists.txt") for _, path in entries):
        found.append(f"{RECORD} does not list {root / 'CMakeLists.txt'}")
    for was, path in entries:
        file = pathlib.Path(path)
        if not file.is_file():
            found.append(f"{path} was read by the configure and is gone")
        elif fingerprint(file) != was:
            found.append(f"{path} has changed since this tree was configured")
    # The step `check` runs this through: CMake writes `check`'s dependencies
    # as order-only inputs of its CUSTOM_COMMAND edge; any input makes it wait.
    # Either separator: CMake writes the MSVC tree's paths with backslashes, and
    # a search for "CMakeFiles/check" found no edge there (M1-101).
    edge = re.search(r"^build CMakeFiles[/\\]check[ |][^\n]*", ninja_text, re.MULTILINE)
    if edge is None or STEP not in edge.group(0).split(":", 1)[-1].split():
        found.append(f"`check` does not wait for `{STEP}`")
    return found


def check(build: pathlib.Path, root: pathlib.Path) -> int:
    record = build / RECORD
    found = problems(record.read_text(encoding="utf-8") if record.is_file() else None, root,
                     (build / "build.ninja").read_text(encoding="utf-8", errors="replace"))
    for line in found:
        print(f"check-configure-current: {line}")
    if found:
        print("check-configure-current: this tree's build plan may not match the files on disk. "
              "Set it up again, then rerun:\n"
              f"  cmake -S {root.as_posix()} -B {build.as_posix()}")
        return 1
    print(f"check-configure-current: every file the configure read is unchanged")
    return 0


def self_test() -> int:
    with tempfile.TemporaryDirectory() as scratch:
        root = pathlib.Path(scratch)
        lists = root / "CMakeLists.txt"
        lists.write_bytes(b"project(x)\n")
        other = root / "cmake" / "More.cmake"
        other.parent.mkdir()
        other.write_bytes(b"set(y 1)\n")
        good = f"{fingerprint(lists)}  {lists.as_posix()}\n{fingerprint(other)}  {other.as_posix()}\n"
        ninja = f"build CMakeFiles/check | $x: CUSTOM_COMMAND || lint {STEP} doc-links\n  COMMAND = ctest\n"
        msvc = ninja.replace("CMakeFiles/check", "CMakeFiles\\check")
        cases = {
            "a record that matches": (good, ninja, None, False),
            "a changed file": (good, ninja, (other, b"set(y 2)\n"), True),
            "a file that is gone": (good, ninja, (other, None), True),
            "no record": (None, ninja, None, True),
            "an empty record": ("", ninja, None, True),
            "a record without CMakeLists.txt": (good.split("\n", 1)[1], ninja, None, True),
            "a malformed line": (good + "not a fingerprint\n", ninja, None, True),
            "check does not wait for the step": (good, ninja.replace(f" {STEP}", ""), None, True),
            # CMake's Ninja generator writes the MSVC tree's paths with
            # backslashes (M1-101): read that way, the check found no edge.
            "check waits, with backslashes": (good, msvc, None, False),
            "check does not wait, with backslashes": (good, msvc.replace(f" {STEP}", ""), None, True),
        }
        failures = 0
        for name, (record, ninja_text, change, want) in cases.items():
            other.write_bytes(b"set(y 1)\n")
            if change is not None:
                path, content = change
                if content is None:
                    path.unlink()
                else:
                    path.write_bytes(content)
            got = bool(problems(record, root, ninja_text))
            status = "ok" if got == want else "WRONG"
            failures += status == "WRONG"
            print(f"self-test: {name}: {'reported' if got else 'accepted'} ({status})")
    return 1 if failures else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return self_test()
    if len(sys.argv) == 3:
        return check(pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve())
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())

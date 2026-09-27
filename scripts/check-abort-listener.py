#!/usr/bin/env python3
"""Check that every Catch2 suite carries the abort listener (M1-90).

    check-abort-listener.py <suite program> [<suite program> ...]
    check-abort-listener.py --self-test

Why this exists: tests/AbortBehaviour.cpp registers a Catch2 listener,
AbortWithoutADialog, which stops the Windows debug runtime turning a failed
assertion into a modal dialog nobody can see -- two mutants hung the machine
twice in one run before it existed (M1-12). Since M1-90 it is compiled once,
as an OBJECT library linked into every suite, rather than compiled into each
(ADR 0024, register decision 210). A listener that stopped being linked into
a suite would not fail anything: the suite still builds and passes, and only
a failed assertion under a debugger shows the dialog again. So every suite is
asked, through Catch2's own --list-listeners, and each must name it.

Proven able to fail before it was trusted: --self-test feeds it listings with
the listener, without it, with another listener only, and from a program that
fails, and exits non-zero unless each is judged right. And the real test was
seen failing with the listener left out of one suite.
"""

import subprocess
import sys

LISTENER = "AbortWithoutADialog"


def carries_listener(listing: str, returncode: int) -> bool:
    if returncode != 0:
        return False
    names = [line.strip().split(":")[0] for line in listing.splitlines()[1:] if line.strip()]
    return LISTENER in names


def check(programs: list) -> int:
    missing = []
    for program in programs:
        try:
            run = subprocess.run([program, "--list-listeners"], capture_output=True, text=True,
                                 encoding="utf-8", errors="replace", timeout=60)
        except OSError as error:
            print(f"check-abort-listener: {program} could not be run: {error}")
            missing.append(program)
            continue
        if not carries_listener(run.stdout, run.returncode):
            missing.append(program)
    for program in missing:
        print(f"check-abort-listener: {program} does not register {LISTENER}")
    if missing or not programs:
        print(f"check-abort-listener: {len(missing)} of {len(programs)} suites without the listener"
              + (" -- and no suites were given" if not programs else ""))
        return 1
    print(f"check-abort-listener: all {len(programs)} suites register {LISTENER}")
    return 0


def self_test() -> int:
    cases = {
        "the listener registered": ("Registered listeners:\n  AbortWithoutADialog:  (No description)\n", 0, True),
        "no listener at all": ("Registered listeners:\n", 0, False),
        "another listener only": ("Registered listeners:\n  SomethingElse:  (No description)\n", 0, False),
        "the program failed": ("Registered listeners:\n  AbortWithoutADialog:  (No description)\n", 1, False),
        "the name only inside a description": (
            "Registered listeners:\n  Other:  replaces AbortWithoutADialog\n", 0, False),
    }
    failures = 0
    for name, (listing, code, want) in cases.items():
        got = carries_listener(listing, code)
        status = "ok" if got == want else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name}: {'accepted' if got else 'refused'} ({status})")
    return 1 if failures else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return self_test()
    return check(sys.argv[1:])


if __name__ == "__main__":
    sys.exit(main())

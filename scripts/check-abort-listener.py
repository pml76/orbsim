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


def verdict(results: list) -> tuple:
    """(exit code, lines to print) for a list of (program, listing, returncode).

    Kept apart from running the programs so that the self-test reaches it: the
    mutation pass showed a check whose self-test covered only one listing at a
    time could lose its report of a missing listener unnoticed (M1-90)."""
    missing = [program for program, listing, code in results if not carries_listener(listing, code)]
    lines = [f"check-abort-listener: {program} does not register {LISTENER}" for program in missing]
    if missing or not results:
        lines.append(f"check-abort-listener: {len(missing)} of {len(results)} suites without the listener"
                     + (" -- and no suites were given" if not results else ""))
        return 1, lines
    lines.append(f"check-abort-listener: all {len(results)} suites register {LISTENER}")
    return 0, lines


def check(programs: list) -> int:
    results = []
    for program in programs:
        try:
            run = subprocess.run([program, "--list-listeners"], capture_output=True, text=True,
                                 encoding="utf-8", errors="replace", timeout=60)
        except OSError as error:
            print(f"check-abort-listener: {program} could not be run: {error}")
            results.append((program, "", -1))
            continue
        results.append((program, run.stdout, run.returncode))
    code, lines = verdict(results)
    for line in lines:
        print(line)
    return code


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

    good = "Registered listeners:\n  AbortWithoutADialog:  (No description)\n"
    runs = {
        "every suite carries it": ([("a", good, 0), ("b", good, 0)], 0),
        "one suite of two is missing it": ([("a", good, 0), ("b", "Registered listeners:\n", 0)], 1),
        "no suites were given": ([], 1),
    }
    for name, (results, want) in runs.items():
        got, _ = verdict(results)
        status = "ok" if got == want else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name}: exit {got} ({status})")
    return 1 if failures else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return self_test()
    return check(sys.argv[1:])


if __name__ == "__main__":
    sys.exit(main())

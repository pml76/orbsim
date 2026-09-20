#!/usr/bin/env python3
"""Run a task's mutation pass, and say honestly what each mutant proved.

    mutate.py scripts/mutants/m1-08.json                 run the pass
    mutate.py scripts/mutants/m1-08.json --verify        check the anchors only
    mutate.py scripts/mutants --verify                   every task's anchors
    mutate.py scripts/mutants/m1-08.json --tree build/debug

Why this exists: VERIFICATION.md rule 19 calls mutation testing "an act, not a
state", and it has been run by hand on M1-04, M1-05, M1-06, M1-07, M1-86 and
M1-08. Doing it by hand is where the two standing traps live, and both have
been walked into:

  * **a mutant that does not compile is not a kill.** M1-06's pass recorded
    five, where `if (false)` is a -Wunreachable-code error under -Werror -- so
    the mutants said nothing about the tests. This script separates a
    static_assert firing (a kill, and it names which assertion) from any other
    compile error (an INVALID mutant, which fails the run);
  * **a kill can be for the wrong reason.** M1-08's date-shift mutant is
    caught only by the span cases, never by the accuracy budget it appears to
    test. The report names the failing test cases, so that a kill can be read
    rather than counted.

**It restores the tree through git**, not through copies: it refuses to start
unless every file it will touch is clean, and it restores with `git checkout
--` in a finally block, so an exception or a Ctrl-C cannot leave a mutant in
the working tree. That is the one hazard a committed harness must not have.

A full pass is deliberately **not** part of `check`: it builds the tree once
per mutant and takes minutes, and rule 19 is periodic by design. **`--verify`
is, since 2026-09-20** -- it only checks that every mutant's anchor still
appears exactly once in its file, which is the half that rots as the code
moves, and it costs milliseconds. Given a directory it verifies every task's
file, and **it fails on an empty directory rather than passing**: a check that
silently stops checking is the failure mode ADR 0005 exists to prevent.

The mutant file is JSON:

    {
      "task": "M1-08",
      "tree": "build/relwithdebinfo",
      "mutants": [
        {
          "name": "the Sun is not negated",
          "file": "src/astro/Sun.cpp",
          "find": "Metres{-heliocentric[0][0] * ERFA_DAU}",
          "replace": "Metres{heliocentric[0][0] * ERFA_DAU}",
          "suites": ["test_sun"],
          "expect": "caught"
        }
      ]
    }

`expect` is "caught" or "survives". A survivor is not automatically a failure
-- M1-05 has three that the owner ruled acceptable, each with its reason -- but
it must be *declared*, so that a mutant which starts surviving after a change
is a failure rather than a line nobody reads. A declared survivor carries a
"why" field saying who accepted it and when.
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

# CLion's cmake, which is the one the build trees were configured with
# (docs/STATUS.md). Overridable for a machine that keeps it elsewhere.
DEFAULT_CMAKE = (
    r"C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe"
)


def repo_root() -> Path:
    out = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        capture_output=True, text=True, check=True,
    )
    return Path(out.stdout.strip())


def dirty_files(root: Path, files: set) -> list:
    out = subprocess.run(
        ["git", "status", "--porcelain", "--"] + sorted(files),
        cwd=root, capture_output=True, text=True, check=True,
    )
    return [line[3:] for line in out.stdout.splitlines() if line.strip()]


def restore(root: Path, files: set) -> None:
    if files:
        subprocess.run(["git", "checkout", "--"] + sorted(files),
                       cwd=root, capture_output=True, text=True, check=False)


def first_error(text: str) -> str:
    for line in text.splitlines():
        if "error:" in line:
            return line.strip()
    return ""


def static_assert_message(text: str) -> str:
    """The assertion that fired, not merely that something did not compile."""
    for line in text.splitlines():
        low = line.lower()
        if "static assertion" in low or "static_assert" in low:
            return line.strip()
    return ""


def failing_cases(stdout: str) -> list:
    """The Catch2 test-case names that failed, so a kill can be read."""
    names, lines = [], stdout.splitlines()
    for i, line in enumerate(lines):
        if set(line.strip()) == {"-"} and i + 1 < len(lines):
            candidate = lines[i + 1].strip()
            if candidate and not set(candidate) <= {"-", "."} and candidate not in names:
                names.append(candidate)
    return names


def run_mutant(root: Path, cmake: str, tree: str, mutant: dict) -> tuple:
    path = root / mutant["file"]
    text = path.read_text(encoding="utf-8")
    found = text.count(mutant["find"])
    if found != 1:
        return "INVALID", f"anchor appears {found} times in {mutant['file']}"

    path.write_text(text.replace(mutant["find"], mutant["replace"]),
                    encoding="utf-8", newline="\n")

    build = subprocess.run([cmake, "--build", tree, "--target", *mutant["suites"]],
                           cwd=root, capture_output=True, text=True)
    if build.returncode != 0:
        output = build.stdout + build.stderr
        fired = static_assert_message(output)
        if fired:
            return "CAUGHT (static_assert)", fired[:160]
        return "INVALID", f"does not compile: {first_error(output)[:140]}"

    caught = []
    for suite in mutant["suites"]:
        run = subprocess.run([str(root / tree / f"{suite}.exe")],
                             cwd=root, capture_output=True, text=True)
        if run.returncode != 0:
            cases = failing_cases(run.stdout)
            caught.append(f"{suite}: " + ("; ".join(cases) if cases else "nonzero exit"))
    if caught:
        return "CAUGHT (test)", " | ".join(caught)[:160]
    return "SURVIVED", ""


def verify(root: Path, given: Path) -> int:
    """Every anchor still matches its file exactly once.

    An anchor that no longer matches is a mutant that cannot run, and a mutant
    that cannot run is not a kill -- so this rots into a pass unless something
    checks it. Hence `check`.
    """
    files = sorted(given.glob("*.json")) if given.is_dir() else [given]
    if not files:
        print(f"no mutant files in {given}: a check that checks nothing must fail "
              f"rather than pass (ADR 0005)", file=sys.stderr)
        return 1

    problems, anchors = [], 0
    for path in files:
        spec = json.loads(path.read_text(encoding="utf-8"))
        for mutant in spec["mutants"]:
            anchors += 1
            target = root / mutant["file"]
            if not target.is_file():
                problems.append(f"{path.name}: {mutant['name']}: "
                                f"no such file {mutant['file']}")
                continue
            found = target.read_text(encoding="utf-8").count(mutant["find"])
            if found != 1:
                problems.append(f"{path.name}: {mutant['name']}: anchor appears "
                                f"{found} times in {mutant['file']}")
    for problem in problems:
        print(problem, file=sys.stderr)
    print(f"mutant-anchors: {len(files)} tasks, {anchors} anchors, "
          f"{len(problems)} stale",
          file=sys.stderr if problems else sys.stdout)
    return 1 if problems else 0


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("mutants",
                        help="a JSON mutant file, or a directory of them with --verify")
    parser.add_argument("--tree", help="build tree; default is the file's")
    parser.add_argument("--cmake", default=DEFAULT_CMAKE)
    parser.add_argument("--verify", action="store_true",
                        help="only check that every anchor still matches once")
    args = parser.parse_args(argv[1:])

    root = repo_root()
    given = Path(args.mutants)

    if args.verify:
        return verify(root, given)

    if given.is_dir():
        print("a directory is only accepted with --verify; name one file to run a pass",
              file=sys.stderr)
        return 2

    spec = json.loads(given.read_text(encoding="utf-8"))
    mutants = spec["mutants"]
    tree = args.tree or spec["tree"]
    files = {m["file"] for m in mutants}

    dirty = dirty_files(root, files)
    if dirty:
        print("refusing to run: these files have uncommitted changes, and the "
              "harness restores with `git checkout --`:", file=sys.stderr)
        for name in dirty:
            print(f"  {name}", file=sys.stderr)
        return 2

    results = []
    try:
        for mutant in mutants:
            restore(root, files)
            verdict, detail = run_mutant(root, args.cmake, tree, mutant)
            results.append((mutant, verdict, detail))
            print(f"{verdict:24} {mutant['name']}"
                  + (f"\n{'':24} {detail}" if detail else ""), flush=True)
    finally:
        restore(root, files)
        subprocess.run([args.cmake, "--build", tree], cwd=root,
                       capture_output=True, text=True, check=False)

    print(f"\n==== {spec['task']}: {len(results)} mutants ====")
    bad = 0
    for mutant, verdict, detail in results:
        expected = mutant.get("expect", "caught")
        ok = (expected == "caught" and verdict.startswith("CAUGHT")) or (
            expected == "survives" and verdict == "SURVIVED")
        if not ok:
            bad += 1
            print(f"  UNEXPECTED {verdict} (expected {expected}): {mutant['name']}")
    caught = sum(1 for _, v, _ in results if v.startswith("CAUGHT"))
    survived = sum(1 for _, v, _ in results if v == "SURVIVED")
    invalid = sum(1 for _, v, _ in results if v == "INVALID")
    print(f"{caught} caught, {survived} survived, {invalid} invalid")
    if invalid:
        print("an invalid mutant is not a kill: rewrite it so that it compiles")
    return 1 if (bad or invalid) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

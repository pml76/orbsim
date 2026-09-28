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
import datetime
import json
import re
import subprocess
import sys
from pathlib import Path

# CLion's cmake, which is the one the build trees were configured with
# (docs/STATUS.md). Overridable for a machine that keeps it elsewhere.
DEFAULT_CMAKE = (
    r"C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe"
)

# **Nothing here waits forever**, added 2026-09-24 after a pass hung twice in
# one run. A mutant makes the code wrong on purpose, and wrong code can hang:
# on Windows the debug C runtime used to turn a failed assertion into a modal
# dialog, so the suite sat there alive and the pass sat there with it, with a
# box on the screen of whoever was running it. `tests/AbortBehaviour.cpp` stops
# that particular cause; these stop the harness being at the mercy of the next
# one, whatever it turns out to be.
#
# Generous on purpose -- the point is to bound a hang, not to police a slow
# machine. The slowest suite in a Debug tree runs in about twenty seconds and
# a full rebuild of one suite in about two minutes, so each limit is an order
# of magnitude clear of anything healthy. A mutant that trips one is reported
# as HUNG, which is neither a kill nor a survivor: it means the harness could
# not decide, and the run fails so that somebody looks.
SUITE_TIMEOUT_SECONDS = 300
BUILD_TIMEOUT_SECONDS = 1800

# Every child's output is decoded as UTF-8 with undecodable bytes replaced.
# `text=True` alone decodes in the locale's code page -- cp1252 here -- and a
# suite that prints one byte outside it (a mutated PNG test did, 2026-09-26)
# made the reader thread fail, handed run_mutant `None` for stdout, and
# stopped the whole pass. Replacement keeps every case name readable.


def repo_root() -> Path:
    out = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        capture_output=True, text=True, encoding="utf-8", errors="replace", check=True,
    )
    return Path(out.stdout.strip())


def dirty_files(root: Path, files: set) -> list:
    out = subprocess.run(
        ["git", "status", "--porcelain", "--"] + sorted(files),
        cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace", check=True,
    )
    return [line[3:] for line in out.stdout.splitlines() if line.strip()]


def restore(root: Path, files: set) -> None:
    if files:
        subprocess.run(["git", "checkout", "--"] + sorted(files),
                       cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)


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

    targets = list(mutant["suites"]) + list(mutant.get("targets", []))
    try:
        build = subprocess.run([cmake, "--build", tree, "--target", *targets],
                               cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace",
                               timeout=BUILD_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        return "HUNG", f"the build did not finish in {BUILD_TIMEOUT_SECONDS} s"
    if build.returncode != 0:
        output = build.stdout + build.stderr
        fired = static_assert_message(output)
        if fired:
            return "CAUGHT (static_assert)", fired[:160]
        return "INVALID", f"does not compile: {first_error(output)[:140]}"

    caught = []
    for suite in mutant["suites"]:
        try:
            run = subprocess.run([str(root / tree / f"{suite}.exe")],
                                 cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace",
                                 timeout=SUITE_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired:
            # Deliberately not counted as a kill. A hung suite has told us
            # nothing about whether the mutant was detected, and calling it
            # caught would turn a broken run into a green one.
            return "HUNG", f"{suite} did not finish in {SUITE_TIMEOUT_SECONDS} s"
        if run.returncode != 0:
            cases = failing_cases(run.stdout)
            caught.append(f"{suite}: " + ("; ".join(cases) if cases else "nonzero exit"))
    # A CTest entry rather than an executable, for a test whose **success is a
    # non-zero exit** (added 2026-09-24, register decision 139). The
    # count-wraparound probes abort on purpose, so running them directly here
    # would report every mutant as caught -- the loop above reads any non-zero
    # exit as a kill. `ctest` inverts it correctly, because the CMake script
    # behind the entry already knows which way round the probe's exit means,
    # including that a tree with assertions compiled out reports SKIPPED rather
    # than passing. A skip is not a kill: ctest exits zero on it, so a mutant
    # judged only this way survives in the release tree, which is honest.
    for entry in mutant.get("ctest", []):
        try:
            run = subprocess.run(["ctest", "-R", f"^{entry}$", "--output-on-failure"],
                                 cwd=root / tree, capture_output=True, text=True, encoding="utf-8", errors="replace",
                                 timeout=SUITE_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired:
            return "HUNG", f"ctest -R {entry} did not finish in {SUITE_TIMEOUT_SECONDS} s"
        if run.returncode != 0:
            caught.append(f"ctest: {entry}")

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
    previous = None
    try:
        for mutant in mutants:
            restore(root, files)
            # Rebuild what the previous mutant built, now that its file is back
            # (M1-95, register decision 221). Ninja notices the restored file but
            # rebuilds only what it is asked to build, and run_mutant asks only
            # for this mutant's targets -- so a program the previous mutant
            # rebuilt would otherwise be judged still built from the mutated
            # code. M1-90's first pass counted two kills that way. Only the
            # restored file is recompiled and those programs relinked.
            if previous is not None:
                rebuild_targets(root, args.cmake, tree, previous)
            previous = mutant
            verdict, detail = run_mutant(root, args.cmake, tree, mutant)
            results.append((mutant, verdict, detail))
            print(f"{verdict:24} {mutant['name']}"
                  + (f"\n{'':24} {detail}" if detail else ""), flush=True)
    finally:
        restore(root, files)
        subprocess.run([args.cmake, "--build", tree], cwd=root,
                       capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)

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
    hung = sum(1 for _, v, _ in results if v == "HUNG")
    print(f"{caught} caught, {survived} survived, {invalid} invalid, {hung} hung")
    if invalid:
        print("an invalid mutant is not a kill: rewrite it so that it compiles")
    if hung:
        print("a hung mutant is not a kill either: the harness could not decide, "
              "so find out why before believing anything else in this run")
    if bad or invalid or hung:
        return 1
    record_pass(root, given, tree)
    return 0


def rebuild_targets(root: Path, cmake: str, tree: str, mutant: dict) -> None:
    """Rebuild a restored mutant's targets, or fail the pass loudly if that fails:
    a program that could not be rebuilt from the real code would judge the next
    mutant wrongly, and a pass must not continue on it."""
    targets = list(mutant["suites"]) + list(mutant.get("targets", []))
    if not targets:
        return
    build = subprocess.run([cmake, "--build", tree, "--target", *targets], cwd=root,
                           capture_output=True, text=True, encoding="utf-8", errors="replace",
                           timeout=BUILD_TIMEOUT_SECONDS, check=False)
    if build.returncode != 0:
        raise RuntimeError(f"the restored tree does not build the targets of "
                           f"'{mutant['name']}' again:\n{(build.stdout + build.stderr)[-2000:]}")


# Where a clean pass is recorded, file by file: the commit it ran at. The strict
# rerun rule (M1-92, ADR 0024, register decision 212) reads it -- a mutant file
# is due again when anything its judges depend on has changed since.
PASSES = "scripts/mutation-passes.json"
# A pass vouches for the committed code only if these match the commit. The
# mutant files and the measurement logs are left out: a pass edits its own
# file's note, and that changes no verdict.
VOUCHED_PATHS = ("CMakeLists.txt", "cmake", "data", "scripts", "shaders", "src", "tests")


def record_pass(root: Path, given: Path, tree: str) -> None:
    status = subprocess.run(["git", "status", "--porcelain", "--", *VOUCHED_PATHS], cwd=root,
                            capture_output=True, text=True, check=True).stdout
    uncommitted = [line for line in status.splitlines()
                   if not re.search(r"scripts/(mutants|measurements)/|" + re.escape(PASSES), line)]
    if uncommitted:
        print(f"pass not recorded in {PASSES}: uncommitted changes it would not vouch for:")
        for line in uncommitted[:5]:
            print(f"  {line}")
        return
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, capture_output=True,
                          text=True, check=True).stdout.strip()
    path = root / PASSES
    passes = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
    passes[given.name] = {"commit": head, "tree": tree,
                          "date": datetime.date.today().isoformat()}
    path.write_text(json.dumps(dict(sorted(passes.items())), indent=2) + "\n",
                    encoding="utf-8", newline="\n")
    print(f"pass recorded in {PASSES}: {given.name} at {head[:12]}")


if __name__ == "__main__":
    sys.exit(main(sys.argv))

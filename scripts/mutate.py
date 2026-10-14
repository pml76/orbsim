#!/usr/bin/env python3
"""Run a task's mutation pass, and say honestly what each mutant proved.

    mutate.py scripts/mutants/m1-08.json                 run the pass
    mutate.py scripts/mutants/m1-08.json --verify        check the anchors only
    mutate.py scripts/mutants --verify                   every task's anchors
    mutate.py scripts/mutants/m1-08.json --tree build/debug
    mutate.py --restore --tree build/debug               finish a stopped pass's restore
    mutate.py --self-test [cmake ninja]                  prove the harness's own rules right

Why this exists: VERIFICATION.md rule 19 calls mutation testing "an act, not a
state", and it has been run by hand on M1-04, M1-05, M1-06, M1-07, M1-86 and
M1-08. Doing it by hand is where the two standing traps live, and both have
been walked into:

  * **a mutant that does not compile is not a kill.** M1-06's pass recorded
    five, where `if (false)` is a -Wunreachable-code error under -Werror -- so
    the mutants said nothing about the tests. This script separates a
    static_assert firing (a kill, and it names which assertion) from any other
    compile error (an INVALID mutant, which fails the run). **One more kind
    of compile-time kill since M1-117**: a mutant may name the compiler
    warning it is caught by, as `"diagnostic": "float-equal"`, and then only
    a build whose every error is that warning, in the mutated file, counts
    (register decision 462);
  * **a kill can be for the wrong reason.** M1-08's date-shift mutant is
    caught only by the span cases, never by the accuracy budget it appears to
    test. The report names the failing test cases, so that a kill can be read
    rather than counted.

**It restores the source through git**: it refuses to start unless every file
it will touch is clean, and it restores with `git checkout --` in a finally
block, so an exception or a Ctrl-C cannot leave a mutant in the working tree.
That is the one hazard a committed harness must not have.

**And the build tree from a snapshot** (M1-129, register decisions 506 and
512-519). Before the first mutant it builds the tree and copies it; after
each mutant it puts back the outputs, then the source's text, then its time,
and requires the build to have nothing to do. A pass therefore leaves no
rebuild behind -- `git checkout` alone gives a file a new time, and the next
`check` rebuilt everything that depended on it, 18 minutes for both trees.
A pass stopped part-way keeps its snapshot, and the next pass refuses to
start until `--restore` has used it.

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

Where the verdict depends on the graphics card, `expect` is a map from card
to verdict, the card named as the goldens' folders are -- `<vendor>-<device>`
in hexadecimal (M1-110) -- with no default (the phase A gate, register
decision 410):

        "expect": {"10de-25ba": "caught", "1002-744c": "survives"}

The card is read before any mutant runs, from a clear probe as a pass records
it (decision 397), and a pass on a card the map does not name, or where the
card cannot be read, does not start: a new card is a decision, not an
inheritance.
"""

import argparse
import contextlib
import datetime
import hashlib
import importlib.util
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
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
    """Put the mutated files back, and stop the pass if they are not: a mutant
    left in the tree would be judged under by every later mutant and every
    later mutant file (M1-104, register decision 252)."""
    if files:
        done = subprocess.run(["git", "checkout", "--"] + sorted(files),
                              cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
        left = subprocess.run(["git", "status", "--porcelain", "--"] + sorted(files),
                              cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace",
                              check=False).stdout
        problem = restore_problem(done.returncode, left)
        if problem:
            raise RuntimeError(f"the mutated files could not be put back ({problem}; {done.stderr.strip()}). "
                               "Stop, and restore them with `git checkout --` before anything else runs.")


# The snapshot a pass restores the build tree from (M1-129, register decisions
# 506 and 512-519). Everything in the tree is listed -- size and modification
# time, which is what Ninja itself compares -- and everything is copied but the
# dependencies' downloaded sources, `_deps/<name>-src/` (decision 513). That
# takes in what Ninja does not track: the PDBs (562 MiB of the Debug tree), the
# probes' images (283 MiB) and CTest's own files, which the tests write, and
# what a configure writes -- for a CMake mutant that includes 236 files under
# `_deps/`, every dependency's download build and generated scripts, measured
# on m1-95.json's pass, 2026-10-13. Copying and deleting, measured the same
# day: Debug 2,297 files, 1.38 GiB, 11.1 s and 4.1 s; RelWithDebInfo 5,150
# files, 1.16 GiB, 22.8 s and 9.0 s. The sources would add 398 MiB in about
# 7,000 small files, more than doubling the time, and 36 read-only git files;
# no configure or build changed one of them. They are listed and not copied,
# so a change there stops the pass rather than passing unseen.
UNCOPIED = "_deps/*-src"
SNAPSHOT_FILES = "files"
SNAPSHOT_SOURCES = "sources.json"
# Written last: a snapshot without it was never finished, and no mutant ran.
SNAPSHOT_LISTING = "listing.json"
# What Ninja prints when nothing is out of date. Its dry run (`-n`) cannot
# say it here: it always counts the glob check's re-run of CMake as work
# (measured 2026-10-13), so the check is a real build, 0.3-0.5 s.
NO_WORK = "ninja: no work to do."


def snapshot_place(root: Path, tree: str) -> Path:
    """Beside the tree, never inside it, where neither Ninja nor CTest looks,
    and on the same disk, which is what makes the copy fast (decision 515)."""
    return root / "build" / f"mutate-snapshot-{Path(tree).name}"


def list_tree(tree: Path) -> tuple:
    """({file: [size, modification time in ns]}, {directory}), relative paths
    with forward slashes. 8,691 files in 0.5 s, measured 2026-10-13."""
    files, folders = {}, set()
    for folder, subfolders, names in os.walk(tree):
        here = Path(folder).relative_to(tree)
        folders.update((here / name).as_posix() for name in subfolders)
        for name in names:
            stat = (Path(folder) / name).stat()
            files[(here / name).as_posix()] = [stat.st_size, stat.st_mtime_ns]
    return files, folders


def uncopied(relative: str) -> bool:
    """Under a dependency's downloaded sources, `_deps/<name>-src/`."""
    parts = relative.split("/")
    return len(parts) > 2 and parts[0] == "_deps" and parts[1].endswith("-src")


def differences(wanted: dict, found: dict) -> list:
    """Each file that is not as the snapshot listed it, by name."""
    return ([f"{name}: not there" for name in sorted(set(wanted) - set(found))]
            + [f"{name}: not in the snapshot" for name in sorted(set(found) - set(wanted))]
            + [f"{name}: size and time {found[name]}, not {wanted[name]}"
               for name in sorted(set(wanted) & set(found)) if wanted[name] != found[name]])


def source_stamps(root: Path, files: set) -> dict:
    """{source: [SHA-256 of its bytes, modification time in ns]} as the pass
    finds them, clean in git, so that both can be put back."""
    return {name: [hashlib.sha256((root / name).read_bytes()).hexdigest(),
                   (root / name).stat().st_mtime_ns] for name in sorted(files)}


def take_snapshot(tree: Path, place: Path, sources: dict) -> None:
    """Copy the tree, then check every copy kept its size and time: a copy
    that lost its time would put back a file Ninja reads as newer."""
    files, folders = list_tree(tree)
    for name in files:
        if not uncopied(name):
            copy = place / SNAPSHOT_FILES / name
            copy.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(tree / name, copy)
    copies, _ = list_tree(place / SNAPSHOT_FILES)
    wrong = differences({name: stamp for name, stamp in files.items() if not uncopied(name)}, copies)
    if wrong:
        raise RuntimeError(f"the snapshot's copies are not the tree's files: {'; '.join(wrong[:5])}")
    (place / SNAPSHOT_SOURCES).write_text(json.dumps(sources), encoding="utf-8")
    (place / SNAPSHOT_LISTING).write_text(json.dumps({"files": files, "folders": sorted(folders)}),
                                          encoding="utf-8")


def restore_outputs(tree: Path, place: Path) -> list:
    """Make the tree what the snapshot listed: what changed or went missing
    copied back with its time, what is new deleted, files and then folders.
    Returns what could not be put back, by name, checked by listing the tree
    again -- an empty list is a tree identical in every size and time."""
    listing = json.loads((place / SNAPSHOT_LISTING).read_text(encoding="utf-8"))
    wanted, wanted_folders = listing["files"], set(listing["folders"])
    found, found_folders = list_tree(tree)
    for name in sorted(set(found) - set(wanted)):
        (tree / name).unlink()
    for name, stamp in sorted(wanted.items()):
        # A file under _deps/ has no copy to put back: the listing below
        # names it, as it names anything else this could not put right.
        if found.get(name) == stamp or uncopied(name):
            continue
        (tree / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(place / SNAPSHOT_FILES / name, tree / name)
    # The deepest first, so that a folder is empty when its turn comes.
    for name in sorted(found_folders - wanted_folders, key=lambda folder: folder.count("/"), reverse=True):
        try:
            (tree / name).rmdir()
        except OSError:
            pass  # not empty, so the listing below names it
    after, after_folders = list_tree(tree)
    return (differences(wanted, after)
            + [f"{name}/: a folder not in the snapshot" for name in sorted(after_folders - wanted_folders)])


def restore_sources(root: Path, sources: dict) -> list:
    """The mutated files' text through git, then their times, but only where
    git gave back the very bytes the pass started from: a different file with
    the old time would be one Ninja never rebuilds from."""
    restore(root, set(sources))
    problems = []
    for name, (digest, modified) in sorted(sources.items()):
        path = root / name
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            problems.append(f"{name}: git gave back other bytes than the pass started from, "
                            "so its time is left new for the build to see")
            continue
        os.utime(path, ns=(path.stat().st_atime_ns, modified))
    return problems


def confirm_no_work(cmake: str, tree: Path):
    """Why the build still had something to do, or None. A real build: what
    it rebuilds is then built from the restored source, and the pass stops."""
    build = subprocess.run([cmake, "--build", str(tree)], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=BUILD_TIMEOUT_SECONDS, check=False)
    output = build.stdout + build.stderr
    if build.returncode != 0 or NO_WORK not in output:
        return f"the restored tree was not up to date: {output.strip()[-600:]}"
    return None


def put_back(root: Path, cmake: str, tree: Path, place: Path) -> None:
    """Everything a mutant changed, **in decision 506's order -- the outputs,
    then the file's text, then its time** -- so that a pass stopped between
    two steps leaves a source newer than its outputs, which the next build
    repairs; the other way round would leave outputs built from a mutant
    beside a source with its old time, which no build ever repairs. Then the
    build must have nothing to do, and the outputs are put back once more,
    since even a build with nothing to do rewrites `.ninja_log` (measured
    2026-10-13). Raises if anything is not as the snapshot had it."""
    sources = json.loads((place / SNAPSHOT_SOURCES).read_text(encoding="utf-8"))
    problems = restore_outputs(tree, place)
    problems += restore_sources(root, sources)
    if not problems:
        problem = confirm_no_work(cmake, tree)
        problems += [problem] if problem else []
        problems += restore_outputs(tree, place)
    if problems:
        raise RuntimeError(f"the build tree could not be put back as the snapshot has it: "
                           f"{'; '.join(problems[:5])}")


def leftover_problem(place: Path, tree: str):
    """Why a pass must not start, or None: a stopped pass left its snapshot,
    and starting another would copy over the one copy of the real tree
    (decision 514)."""
    if place.exists():
        return (f"a stopped pass left its snapshot in {place}: put the tree back first, with "
                f"`python scripts/mutate.py --restore --tree {tree}`")
    return None


def restore_command(root: Path, cmake: str, tree: Path, place: Path) -> int:
    """`--restore`: finish what a stopped pass left (decision 514)."""
    if not place.exists():
        print(f"nothing to restore: there is no snapshot in {place}")
        return 0
    if not (place / SNAPSHOT_LISTING).exists():
        # Taken before any mutant ran, so the tree is as the pass found it.
        shutil.rmtree(place)
        print(f"the snapshot in {place} was never finished, so no mutant ran on it; deleted")
        return 0
    put_back(root, cmake, tree, place)
    shutil.rmtree(place)
    print(f"the tree is as the stopped pass found it, and the snapshot in {place} is deleted")
    return 0


# What ctest prints when -R matches nothing, as 4.3.1 and 4.4.4 both do; with
# --no-tests=error it then exits 8 rather than 0 (measured 2026-10-14).
NO_TESTS = "No tests were found"


def ctest_command(entry: str) -> list:
    """One CTest judge, by its exact name. `--no-tests=error` because without
    it a name that matches nothing exits 0 -- a judge renamed since the mutant
    was written made its mutant survive in silence (M1-129, M1-121 finding
    4.5, register decision 522)."""
    return ["ctest", "-R", f"^{entry}$", "--no-tests=error", "--output-on-failure"]


def ctest_judge(tree: Path, entry: str) -> str:
    """"passed", "failed", or "no test" -- never a kill, which a missing judge's
    failure would otherwise be counted as."""
    run = subprocess.run(ctest_command(entry), cwd=tree, capture_output=True, text=True, encoding="utf-8",
                         errors="replace", timeout=SUITE_TIMEOUT_SECONDS)
    if run.returncode == 0:
        return "passed"
    return "no test" if NO_TESTS in run.stdout + run.stderr else "failed"


def tree_tests(tree: Path) -> set:
    """The names of every test CTest knows in `tree`."""
    listing = subprocess.run(["ctest", "--show-only=json-v1"], cwd=tree, capture_output=True, text=True,
                             encoding="utf-8", errors="replace", check=True).stdout
    return {test["name"] for test in json.loads(listing)["tests"]}


def unknown_judges(mutants: list, names: set) -> list:
    """Every CTest judge the mutants name that `names` does not hold."""
    return sorted({entry for mutant in mutants for entry in mutant.get("ctest", []) if entry not in names})


def first_error(text: str) -> str:
    for line in text.splitlines():
        if "error:" in line:
            return line.strip()
    return ""


DIAGNOSTIC_NAME = re.compile(r"[a-z0-9][a-z0-9+-]*")


def diagnostic_problem(diagnostic):
    """Why a mutant's `diagnostic` cannot be judged, or None: the warning's own
    name, as `-W<name>` spells it, without the `-W`."""
    if diagnostic is None:
        return None
    if not isinstance(diagnostic, str) or not DIAGNOSTIC_NAME.fullmatch(diagnostic):
        return f"diagnostic {diagnostic!r} is not a warning's name, such as \"float-equal\""
    return None


def diagnostic_kill(text: str, diagnostic: str, mutated: str) -> str:
    """The first error line, when **every** error the build printed is the named
    warning, raised as an error in the mutated file; "" otherwise (M1-117,
    register decision 462).

    For a mutant whose catch is a compiler warning rather than a static_assert:
    `-Wfloat-equal` refusing a `==` put back. All of the errors, not one of
    them, so that an unrelated error beside the right one is still INVALID;
    and in the mutated file, so that the warning firing somewhere else says
    nothing about this mutant. clang writes `[-Werror,-Wname]`, gcc
    `[-Werror=name]`."""
    reported = re.compile(r"\[-Werror(?:,-W|=)" + re.escape(diagnostic) + r"\]")
    where = mutated.replace("\\", "/")
    errors = [line for line in text.splitlines() if "error:" in line]
    if not errors:
        return ""
    for line in errors:
        if not reported.search(line) or where not in line.replace("\\", "/"):
            return ""
    return errors[0].strip()


def static_assert_message(text: str) -> str:
    """The assertion that fired, not merely that something did not compile."""
    for line in text.splitlines():
        low = line.lower()
        if "static assertion" in low or "static_assert" in low:
            return line.strip()
    return ""


CATCH2_SEPARATOR = "-" * 79

# What Catch2 3.16 prints, for the self-test of failing_cases (M1-115). A skip,
# from test_file_replace on Windows, 2026-10-09:
CATCH2_SKIPPED = """\
Randomness seeded to: 2036146725

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
test_file_replace.exe is a Catch2 v3.16.0 host application.
Run with -? for options

-------------------------------------------------------------------------------
a write the disk refuses only when the file is closed is reported
-------------------------------------------------------------------------------
C:/orbsim/tests/test_file_replace.cpp(124)
...............................................................................

C:/orbsim/tests/test_file_replace.cpp(128): SKIPPED:
explicitly with message:
  this platform has no /dev/full, the device that refuses every write as a full
  disk would, so a refusal at close cannot be produced here

===============================================================================
test cases: 10 |  9 passed | 1 skipped
assertions: 42 | 42 passed
"""
# A failed case with a section, a skipped case and a passing one, from a
# three-case program built with gcc 14 under WSL, 2026-10-09:
CATCH2_MIXED = """\
Randomness seeded to: 2245458000

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
t is a Catch2 v3.16.0 host application.
Run with -? for options

-------------------------------------------------------------------------------
a case
  a section
-------------------------------------------------------------------------------
/tmp/m/t.cpp:3
...............................................................................

/tmp/m/t.cpp:3: FAILED:
  CHECK( 1 == 2 )

-------------------------------------------------------------------------------
a skipped case
-------------------------------------------------------------------------------
/tmp/m/t.cpp:5
...............................................................................

/tmp/m/t.cpp:5: SKIPPED:
explicitly with message:
  not here

===============================================================================
test cases: 3 | 1 passed | 1 failed | 1 skipped
assertions: 2 | 1 passed | 1 failed
"""


def failing_cases(stdout: str) -> list:
    """The Catch2 test-case names that failed, each followed by its location, so
    a kill can be read.

    Catch2 prints a block for every case that did not simply pass: the case's
    name between two dashed lines, then its location, then what happened. A
    skipped case gets one too, so a block counts only if what happened says
    FAILED: -- Catch2's word for a failed check, an unexpected exception and a
    fatal error alike (M1-115). Until then a skip was named among the cases
    that caught a mutant, though the verdict, from the exit code, was right."""
    names, lines = [], stdout.splitlines()
    # Catch2's separator is 79 dashes, whatever the console's width; asking for
    # exactly that keeps a test's own line of dashes from passing for one.
    starts = [i for i, line in enumerate(lines) if line.rstrip() == CATCH2_SEPARATOR]
    # Dashed lines come in pairs around a heading; a block runs from its
    # heading to the next heading, or to the end.
    for k in range(0, len(starts) - 1, 2):
        opening, closing = starts[k], starts[k + 1]
        end = starts[k + 2] if k + 2 < len(starts) else len(lines)
        body = lines[closing + 1:end]
        if not any("FAILED:" in line for line in body):
            continue
        for candidate in (lines[opening + 1].strip(), body[0].strip() if body else ""):
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

    targets = built_by(mutant)
    # A mutant of a Python script names nothing to build (M1-109, register
    # decision 392): the script is read when its test runs, and `--verify`
    # refuses a mutant of anything else that names no build.
    try:
        build = subprocess.run([cmake, "--build", tree, "--target", *targets],
                               cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace",
                               timeout=BUILD_TIMEOUT_SECONDS) if targets else None
    except subprocess.TimeoutExpired:
        return "HUNG", f"the build did not finish in {BUILD_TIMEOUT_SECONDS} s"
    if build is not None and build.returncode != 0:
        output = build.stdout + build.stderr
        # A mutant that names the warning it is caught by is judged by that
        # alone (M1-117, register decision 462). Not after the static_assert
        # test below, which reads any line mentioning static_assert as a kill:
        # the compiler quotes the mutated source line, and a `==` put back
        # inside a static_assert would pass for an assertion firing.
        if mutant.get("diagnostic"):
            reported = diagnostic_kill(output, mutant["diagnostic"], mutant["file"])
            if reported:
                return f"CAUGHT (-W{mutant['diagnostic']})", reported[:160]
            return "INVALID", f"does not compile, and not only for -W{mutant['diagnostic']}: " \
                              f"{first_error(output)[:120]}"
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
            judged = ctest_judge(root / tree, entry)
        except subprocess.TimeoutExpired:
            return "HUNG", f"ctest -R {entry} did not finish in {SUITE_TIMEOUT_SECONDS} s"
        if judged == "no test":
            return "INVALID", f"the tree has no test {entry} to judge by"
        if judged == "failed":
            caught.append(f"ctest: {entry}")

    if caught:
        return "CAUGHT (test)", " | ".join(caught)[:160]
    return "SURVIVED", ""


def verify(root: Path, given: Path, names=None) -> int:
    """Every anchor still matches its file exactly once.

    An anchor that no longer matches is a mutant that cannot run, and a mutant
    that cannot run is not a kill -- so this rots into a pass unless something
    checks it. Hence `check`.

    Since M1-129 (M1-121 finding 4.5, register decision 523) also: a file
    names at least one mutant, and, where `names` holds a tree's tests, every
    CTest judge is one of them. `check` gives its tree's names where the tree
    builds the application; a core-only tree has neither the GPU tests nor
    the `mutants_due` ones, and there the names are said to be unchecked.
    """
    files = sorted(given.glob("*.json")) if given.is_dir() else [given]
    if not files:
        print(f"no mutant files in {given}: a check that checks nothing must fail "
              f"rather than pass (ADR 0005)", file=sys.stderr)
        return 1

    problems, anchors = [], 0
    for path in files:
        spec = json.loads(path.read_text(encoding="utf-8"))
        if not spec["mutants"]:
            problems.append(f"{path.name}: names no mutants, so a pass of it would prove nothing")
        if names is not None:
            problems += [f"{path.name}: judges by {entry}, which is not a test in this tree"
                         for entry in unknown_judges(spec["mutants"], names)]
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
            problem = build_problem(mutant)
            if problem:
                problems.append(f"{path.name}: {mutant['name']}: {problem}")
            problem = expectation_problem(mutant.get("expect", "caught"))
            if problem:
                problems.append(f"{path.name}: {mutant['name']}: {problem}")
            problem = diagnostic_problem(mutant.get("diagnostic"))
            if problem:
                problems.append(f"{path.name}: {mutant['name']}: {problem}")
    for problem in problems:
        print(problem, file=sys.stderr)
    if names is None:
        print("mutant-anchors: judge names not checked: no tree that builds the application was named")
    print(f"mutant-anchors: {len(files)} tasks, {anchors} anchors, "
          f"{len(problems)} stale",
          file=sys.stderr if problems else sys.stdout)
    return 1 if problems else 0


def main(argv: list) -> int:
    if argv[1:2] == ["--self-test"]:
        return self_test(argv[2:])
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("mutants", nargs="?",
                        help="a JSON mutant file, or a directory of them with --verify")
    parser.add_argument("--tree", help="build tree; default is the file's")
    parser.add_argument("--cmake", default=DEFAULT_CMAKE)
    parser.add_argument("--verify", action="store_true",
                        help="only check that every anchor still matches once")
    parser.add_argument("--restore", action="store_true",
                        help="put the --tree back from the snapshot a stopped pass left")
    args = parser.parse_args(argv[1:])

    root = repo_root()
    if args.restore:
        if args.mutants or not args.tree:
            parser.error("--restore takes --tree and no mutant file")
        return restore_command(root, args.cmake, root / args.tree, snapshot_place(root, args.tree))
    if not args.mutants:
        parser.error("name a mutant file")
    given = Path(args.mutants)

    if args.verify:
        return verify(root, given, tree_tests(root / args.tree) if args.tree else None)

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

    # Every mutant's verdict on this card, before anything runs (register
    # decision 410): a pass that could not judge its last mutant should not
    # spend an hour on the others first.
    card = None
    if any(isinstance(m.get("expect"), dict) for m in mutants):
        card, why = card_here(root, tree)
        print(f"graphics card: {card}" if card else f"graphics card unknown: {why}")
    expected = [expected_on(m, card) for m in mutants]
    unjudged = [(m, problem) for m, (_, problem) in zip(mutants, expected) if problem]
    if unjudged:
        print("refusing to run: these mutants have no verdict on this card:", file=sys.stderr)
        for mutant, problem in unjudged:
            print(f"  {mutant['name']}: {problem}", file=sys.stderr)
        return 2

    # Every CTest judge in this tree, before anything runs (register decision
    # 522), as for the card: a missing one is a pass that cannot judge.
    if any(m.get("ctest") for m in mutants):
        missing = unknown_judges(mutants, tree_tests(root / tree))
        if missing:
            print(f"refusing to run: {tree} has no test {', '.join(missing)} to judge by", file=sys.stderr)
            return 2

    place = snapshot_place(root, tree)
    problem = leftover_problem(place, tree)
    if problem:
        print(f"refusing to run: {problem}", file=sys.stderr)
        return 2
    # Built before the snapshot, so that the snapshot is a finished tree and
    # the check after each mutant has nothing to do (decision 517).
    build = subprocess.run([args.cmake, "--build", tree], cwd=root, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=BUILD_TIMEOUT_SECONDS, check=False)
    if build.returncode != 0:
        print(f"refusing to run: {tree} does not build as it stands:\n"
              f"{(build.stdout + build.stderr)[-2000:]}", file=sys.stderr)
        return 2
    take_snapshot(root / tree, place, source_stamps(root, files))

    results = []
    finished = False
    try:
        for mutant in mutants:
            verdict, detail = run_mutant(root, args.cmake, tree, mutant)
            results.append((mutant, verdict, detail))
            print(f"{verdict:24} {mutant['name']}"
                  + (f"\n{'':24} {detail}" if detail else ""), flush=True)
            # Every program, image and record as it was before this mutant,
            # so the next is judged on the real code (M1-95's lesson, register
            # decision 221) and nothing is left to rebuild after the pass.
            put_back(root, args.cmake, root / tree, place)
        finished = True
    finally:
        if not finished:
            try:
                put_back(root, args.cmake, root / tree, place)
            except (RuntimeError, OSError, subprocess.SubprocessError) as error:
                print(f"the pass stopped, and putting the tree back failed: {error}\n"
                      f"The snapshot is kept: {leftover_problem(place, tree)}", file=sys.stderr)
                place = None
        if place is not None:
            shutil.rmtree(place)

    print(f"\n==== {spec['task']}: {len(results)} mutants ====")
    bad = 0
    for (mutant, verdict, detail), (wanted, _) in zip(results, expected):
        ok = (wanted == "caught" and verdict.startswith("CAUGHT")) or (
            wanted == "survives" and verdict == "SURVIVED")
        if not ok:
            bad += 1
            print(f"  UNEXPECTED {verdict} (expected {wanted}): {mutant['name']}")
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
    record_pass(root, given, tree, spec)
    return 0


# A mutant's verdicts, and a card as the goldens' folders name it (M1-110).
EXPECTATIONS = ("caught", "survives")
CARD_KEY = re.compile(r"[0-9a-f]{4}-[0-9a-f]{4}")


def card_key(identity):
    """'10de-25ba' from a recorded identity's card, '0x10de 0x25ba' -- the
    name of the card's goldens folder -- or None where it is not in that form."""
    words = (identity or {}).get("card", "").split()
    if len(words) != 2:
        return None
    try:
        vendor, device = (int(word, 16) for word in words)
    except ValueError:
        return None
    return f"{vendor:04x}-{device:04x}"


def expectation_problem(expect):
    """Why a mutant's `expect` is malformed, or None: a verdict, or a map from
    card to verdict naming at least one card (register decision 410)."""
    if isinstance(expect, str):
        return None if expect in EXPECTATIONS else f"expect '{expect}' is neither caught nor survives"
    if not isinstance(expect, dict):
        return "expect is neither a verdict nor a map from card to verdict"
    if not expect:
        return "expect's map names no card"
    for card, verdict in expect.items():
        if not CARD_KEY.fullmatch(card):
            return f"'{card}' is not a card: <vendor>-<device>, four lower-case hex digits each"
        if verdict not in EXPECTATIONS:
            return f"expect '{verdict}' for {card} is neither caught nor survives"
    return None


def expected_on(mutant: dict, card):
    """(verdict, None) for this mutant on this card, or (None, why): a map
    with no entry for the card, or a card that could not be read, has no
    verdict -- there is no default (register decision 410)."""
    expect = mutant.get("expect", "caught")
    if isinstance(expect, str):
        return expect, None
    if card is None:
        return None, "its verdict depends on the graphics card, which could not be read here"
    if card not in expect:
        return None, (f"its verdict names {', '.join(sorted(expect))} and not this card, "
                      f"{card}: say what it is on this card first (register decision 410)")
    return expect[card], None


def card_here(root: Path, tree: str):
    """(card, why): this tree's graphics card as card_key names it, read the
    way a pass records it -- mutants-due.py's own gpu_identity, from a clear
    probe -- or None and why."""
    sys.dont_write_bytecode = True  # no __pycache__ in scripts/
    loader = importlib.util.spec_from_file_location("mutants_due", root / "scripts" / "mutants-due.py")
    module = importlib.util.module_from_spec(loader)
    loader.loader.exec_module(module)
    identity, why = module.gpu_identity((root / tree).resolve())
    if identity is None:
        return None, why
    found = card_key(identity)
    return (found, "") if found else (None, f"the card reads '{identity.get('card')}'")


def build_problem(mutant: dict):
    """Why a mutant cannot go without a build, or None (M1-109, register
    decision 392). Only a Python script is read as it stands when its test
    runs; a C++ file must be compiled, and a CMake file is read by a configure
    that only a build triggers -- `build.ninja` is the target that does that
    and nothing else. A mutant of either that builds nothing changes nothing,
    and would survive as a hole that is not there."""
    if built_by(mutant) or mutant["file"].endswith(".py"):
        return None
    return (f"names nothing to build, but {mutant['file']} is not a Python script: name the "
            "targets it reaches, or build.ninja for a CMake file")


def built_by(mutant: dict) -> list:
    """The targets a mutant's own build asks for: its suites, then its targets."""
    return list(mutant["suites"]) + list(mutant.get("targets", []))


# The snapshot's scratch project: one output built from one committed source,
# by CMake and Ninja as the real trees are built, and no compiler needed. The
# glob checked at every build is there because the real trees have them, and
# it is what makes a build with nothing to do still write `.ninja_log`
# (measured 2026-10-13: 291 bytes before, 414 after); without it the second
# put-back after the build's check was a mutant no case could catch.
SCRATCH_PROJECT = """\
cmake_minimum_required(VERSION 3.20)
project(snapshot_self_test LANGUAGES NONE)
file(GLOB listed CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.txt)
add_custom_command(OUTPUT out.txt
                   COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/in.txt out.txt
                   DEPENDS in.txt)
add_custom_target(out ALL DEPENDS out.txt)
"""


def snapshot_cases(tools: list) -> dict:
    """The snapshot's rules (M1-129, register decisions 506 and 512-519), on a
    tree of plain files and then on a scratch project really built."""
    cmake, ninja = tools if len(tools) == 2 else (DEFAULT_CMAKE, shutil.which("ninja") or "ninja")
    cases = {}
    with tempfile.TemporaryDirectory() as scratch:
        tree, place = Path(scratch) / "plain", Path(scratch) / "plain-snapshot"
        for name in ("a.txt", "sub/b.txt", "_deps/x-src/d.txt", "_deps/x-build/e.txt"):
            (tree / name).parent.mkdir(parents=True, exist_ok=True)
            (tree / name).write_text(name, encoding="utf-8")
        cases["a pass may start where no snapshot is left"] = leftover_problem(place, "plain") is None
        take_snapshot(tree, place, {})
        cases["a pass does not start while a snapshot is left"] = leftover_problem(place, "plain") is not None
        listed = list_tree(tree)
        # What a mutant's build, its configure and its tests do: rewrite a
        # file, one in a dependency's build among them, delete one, write new
        # ones. The times are set, so the case does not rest on the clock.
        for name in ("a.txt", "_deps/x-build/e.txt"):
            (tree / name).write_text("mutated", encoding="utf-8")
            os.utime(tree / name, ns=(1, 1))
        (tree / "sub/b.txt").unlink()
        (tree / "new/deeper").mkdir(parents=True)
        (tree / "new/deeper/c.txt").write_text("c", encoding="utf-8")
        problems = restore_outputs(tree, place)
        cases["what a mutant changed is put back, and what it wrote is deleted"] = (
            problems == [] and list_tree(tree) == listed
            and (tree / "a.txt").read_text(encoding="utf-8") == "a.txt"
            and (tree / "_deps/x-build/e.txt").read_text(encoding="utf-8") == "_deps/x-build/e.txt")
        (tree / "_deps/x-src/d.txt").write_text("changed", encoding="utf-8")
        cases[f"a change under {UNCOPIED}, which is not copied, is reported"] = (
            restore_outputs(tree, place) != [])
        cases.update(scratch_build_cases(Path(scratch), cmake, ninja))
        cases.update(scratch_pass_cases(Path(scratch), cmake, ninja))
    cases.update(verify_cases())
    return cases


# A whole pass on a scratch repository, shaped as m1-95.json is (register
# decision 221): mutant A breaks the output a test compares, and mutant B
# changes nothing that test reads and must survive. B is judged right after
# A, so it is caught only if A's output was left in the tree -- the loop not
# putting the tree back after each mutant (decision 519).
SCRATCH_PASS_PROJECT = SCRATCH_PROJECT + """\
enable_testing()
add_test(NAME same COMMAND ${CMAKE_COMMAND} -E compare_files
         ${CMAKE_CURRENT_BINARY_DIR}/out.txt ${CMAKE_CURRENT_SOURCE_DIR}/expected.txt)
"""


def scratch_pass_cases(scratch: Path, cmake: str, ninja: str) -> dict:
    repo = scratch / "pass"
    for name, text in (("CMakeLists.txt", SCRATCH_PASS_PROJECT), ("in.txt", "real\n"),
                       ("expected.txt", "real\n"), ("other.txt", "a comment\n"),
                       ("scripts/mutation-passes.json", "{}\n")):
        (repo / name).parent.mkdir(parents=True, exist_ok=True)
        (repo / name).write_text(text, encoding="utf-8", newline="\n")

    def git(*words):
        subprocess.run(["git", *words], cwd=repo, check=True, capture_output=True)

    git("init", "-q")
    git("config", "core.autocrlf", "false")
    git("add", ".")
    git("-c", "user.name=self-test", "-c", "user.email=self-test@invalid", "commit", "-q", "-m", "scratch")
    tree = repo / "build" / "b"
    configure = subprocess.run([cmake, "-S", str(repo), "-B", str(tree), "-G", "Ninja",
                                f"-DCMAKE_MAKE_PROGRAM={ninja}"], capture_output=True, text=True,
                               encoding="utf-8", errors="replace", check=False)
    if configure.returncode != 0:
        print(configure.stdout + configure.stderr)
        return {"the scratch pass's project configures": False}

    def mutant(name, file, find, replace, expect):
        return {"name": name, "file": file, "find": find, "replace": replace, "expect": expect,
                "suites": [], "targets": ["out"], "ctest": ["same"]}

    spec = scratch / "pass.json"
    spec.write_text(json.dumps({"task": "self-test", "tree": "build/b", "mutants": [
        mutant("A: the output is wrong", "in.txt", "real", "mutated", "caught"),
        mutant("B: nothing the test reads", "other.txt", "a comment", "another comment", "survives"),
    ]}), encoding="utf-8")
    run = subprocess.run([sys.executable, __file__, str(spec), "--tree", "build/b", "--cmake", cmake],
                         cwd=repo, capture_output=True, text=True, encoding="utf-8", errors="replace",
                         timeout=BUILD_TIMEOUT_SECONDS, check=False)
    if run.returncode != 0:
        print(run.stdout + run.stderr)
    cases = {
        "a pass judges each mutant on a tree put back after the one before":
            run.returncode == 0 and (tree / "out.txt").read_text(encoding="utf-8") == "real\n",
        "a pass leaves its tree with nothing to build, and no snapshot":
            confirm_no_work(cmake, tree) is None and not snapshot_place(repo, "build/b").exists(),
    }
    # A judge the tree does not have (M1-129, M1-121 finding 4.5, register
    # decision 522): CTest's own answer for it, and a pass refused before its
    # first mutant, leaving the source as it was.
    cases["ctest judges a test that passes as passed"] = ctest_judge(tree, "same") == "passed"
    cases["ctest judges a name that matches no test as no test, never as a kill"] = (
        ctest_judge(tree, "no_such_test") == "no test")
    renamed = scratch / "renamed.json"
    renamed.write_text(json.dumps({"task": "self-test", "tree": "build/b", "mutants": [
        mutant("A, judged by a test since renamed", "in.txt", "real", "mutated", "caught")
        | {"ctest": ["renamed_test"]}]}), encoding="utf-8")
    refused = subprocess.run([sys.executable, __file__, str(renamed), "--tree", "build/b", "--cmake", cmake],
                             cwd=repo, capture_output=True, text=True, encoding="utf-8", errors="replace",
                             timeout=BUILD_TIMEOUT_SECONDS, check=False)
    cases["a pass whose judge the tree does not have is refused before its first mutant"] = (
        refused.returncode == 2 and "renamed_test" in refused.stderr
        and (repo / "in.txt").read_text(encoding="utf-8") == "real\n")
    return cases


def verify_cases() -> dict:
    """--verify's rules of M1-129 (register decision 523), on mutant files in a
    scratch folder: a file with no mutants, and a judge the tree does not have."""
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        (root / "a.py").write_text("x = 1\n", encoding="utf-8")
        judged = {"name": "m", "file": "a.py", "find": "x = 1", "replace": "x = 2", "suites": [],
                  "ctest": ["known"]}

        def verdict(name, mutants, names):
            folder = root / name
            folder.mkdir()
            (folder / "t.json").write_text(json.dumps({"task": "T", "tree": "b", "mutants": mutants}),
                                           encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                return verify(root, folder, names)

        return {
            "--verify accepts a file whose judges all exist": verdict("good", [judged], {"known"}) == 0,
            "--verify refuses a judge the tree does not have": verdict("renamed", [judged], {"other"}) == 1,
            "--verify without a tree leaves judge names alone": verdict("unnamed", [judged], None) == 0,
            "--verify refuses a file with no mutants": verdict("empty", [], {"known"}) == 1,
            "a judge is unknown only where the names lack it": (
                unknown_judges([judged, dict(judged, ctest=["b", "known"])], {"known"}) == ["b"]
                and unknown_judges([judged], {"known"}) == []),
        }


def scratch_build_cases(scratch: Path, cmake: str, ninja: str) -> dict:
    """The whole put-back on a project CMake configures and Ninja builds."""
    repo, tree, place = scratch / "repo", scratch / "build", scratch / "snapshot"
    repo.mkdir()
    (repo / "CMakeLists.txt").write_text(SCRATCH_PROJECT, encoding="utf-8", newline="\n")
    (repo / "in.txt").write_text("real\n", encoding="utf-8", newline="\n")

    def git(*words):
        subprocess.run(["git", *words], cwd=repo, check=True, capture_output=True)

    def build():
        return subprocess.run([cmake, "--build", str(tree)], capture_output=True, text=True,
                              encoding="utf-8", errors="replace", check=False)

    git("init", "-q")
    git("config", "core.autocrlf", "false")
    git("add", ".")
    git("-c", "user.name=self-test", "-c", "user.email=self-test@invalid", "commit", "-q", "-m", "scratch")
    configure = subprocess.run([cmake, "-S", str(repo), "-B", str(tree), "-G", "Ninja",
                                f"-DCMAKE_MAKE_PROGRAM={ninja}"], capture_output=True, text=True,
                               encoding="utf-8", errors="replace", check=False)
    built = configure.returncode == 0 and build().returncode == 0
    cases = {"the scratch project configures and builds": built}
    if not built:
        print(configure.stdout + configure.stderr)
        return cases

    sources = source_stamps(repo, {"in.txt"})
    take_snapshot(tree, place, sources)
    listed = list_tree(tree)
    real_output = (tree / "out.txt").read_bytes()
    real_time = (repo / "in.txt").stat().st_mtime_ns

    def as_found():
        return ((tree / "out.txt").read_bytes() == real_output
                and (repo / "in.txt").stat().st_mtime_ns == real_time and list_tree(tree) == listed)

    def attempt(step):
        try:
            step()
            return ""
        except RuntimeError as error:
            print(error)
            return str(error)

    # A mutant, its build, and a file its test writes.
    (repo / "in.txt").write_text("mutated\n", encoding="utf-8", newline="\n")
    build()
    (tree / "written-by-a-test.txt").write_text("x", encoding="utf-8")
    failed = attempt(lambda: put_back(repo, cmake, tree, place))
    cases["a mutant's outputs, then its text, then its time are put back"] = failed == "" and as_found()
    cases["after that the build has nothing to do"] = confirm_no_work(cmake, tree) is None
    # A source newer than its output: the check must see the work.
    os.utime(repo / "in.txt")
    cases["a tree with work left is refused"] = confirm_no_work(cmake, tree) is not None
    # A pass stopped with its mutant in place, and its snapshot left.
    (repo / "in.txt").write_text("mutated\n", encoding="utf-8", newline="\n")
    build()
    failed = attempt(lambda: restore_command(repo, cmake, tree, place))
    cases["--restore puts a stopped pass's tree back and deletes the snapshot"] = (
        failed == "" and as_found() and not place.exists())
    unfinished = scratch / "unfinished"
    (unfinished / SNAPSHOT_FILES).mkdir(parents=True)
    restore_command(repo, cmake, tree, unfinished)
    cases["--restore deletes a snapshot that was never finished"] = not unfinished.exists()
    # Git giving back other bytes than the pass started from: this machine's
    # own core.autocrlf, with no .gitattributes, writes CRLF. The old time on
    # those bytes would hide them from Ninja, so the time is left new.
    git("config", "core.autocrlf", "true")
    (repo / "in.txt").write_text("mutated\n", encoding="utf-8", newline="\n")
    problems = restore_sources(repo, sources)
    cases["a source git gives back with other bytes keeps its new time"] = (
        problems != [] and (repo / "in.txt").stat().st_mtime_ns != real_time)
    return cases


def self_test(tools: list) -> int:
    """Prove the harness's own rules right before a pass trusts them. `tools`
    is the cmake and the ninja to build a scratch project with, which CTest
    passes; by hand, the defaults."""
    failures = 0
    for name, ok in snapshot_cases(tools).items():
        failures += not ok
        print(f"self-test: {name}: {'ok' if ok else 'WRONG'}")
    # A pass records its judges' fingerprints (M1-103): without them the build
    # definition makes the file due again, whatever the change did.
    judges = {"ctest t": "0123456789abcdef"}
    record_cases = {
        "a record holds the judges' fingerprints": pass_record("c0ffee", "build/rel", judges).get("judges") == judges,
        "a record without fingerprints says nothing about them":
            "judges" not in pass_record("c0ffee", "build/rel", None),
        "a record holds its commit and tree": pass_record("c0ffee", "build/rel", judges)["commit"] == "c0ffee"
            and pass_record("c0ffee", "build/rel", judges)["tree"] == "build/rel",
    }
    parts = {"ctest t": {"definition": "0123456789abcdef"}}
    card = {"card": "0x10de 0x25ba"}
    record_cases["a record holds the judges' parts and the graphics card (M1-109)"] = (
        pass_record("c0ffee", "build/rel", judges, parts, card).get("judge_parts") == parts
        and pass_record("c0ffee", "build/rel", judges, parts, card).get("gpu") == card)
    record_cases["a record for a file not judged on a card names none"] = (
        "gpu" not in pass_record("c0ffee", "build/rel", judges, parts, None))
    # A mutant that names no build (M1-109, register decision 392): allowed for
    # a Python script, refused for anything a build must read first.
    script = {"file": "scripts/x.py", "suites": [], "targets": []}
    record_cases["a script's mutant may build nothing"] = build_problem(script) is None
    record_cases["a C++ mutant that builds nothing is refused"] = build_problem(
        dict(script, file="src/a.cpp")) is not None
    record_cases["a CMake mutant that builds nothing is refused"] = build_problem(
        dict(script, file="cmake/ScriptTests.cmake")) is not None
    record_cases["a CMake mutant that builds build.ninja is accepted"] = build_problem(
        dict(script, file="CMakeLists.txt", targets=["build.ninja"])) is None
    # An expectation that differs by graphics card (the phase A gate, register
    # decision 410): a map from card to verdict, with no default.
    per_card = {"expect": {"1002-744c": "survives", "10de-25ba": "caught"}}
    record_cases["a card is named as the goldens' folders are"] = (
        card_key({"card": "0x10de 0x25ba"}) == "10de-25ba"
        and card_key({"card": "0x1002 0x744c"}) == "1002-744c")
    record_cases["a card in another form is no card"] = (
        card_key({"card": "NVIDIA"}) is None and card_key(None) is None)
    record_cases["a word is the expectation on every card"] = (
        expected_on({"expect": "survives"}, None) == ("survives", None)
        and expected_on({}, "10de-25ba") == ("caught", None))
    record_cases["a map gives the card's own expectation, not its first"] = (
        expected_on(per_card, "10de-25ba") == ("caught", None)
        and expected_on(per_card, "1002-744c") == ("survives", None))
    record_cases["a card the map does not name is refused"] = (
        expected_on(per_card, "8086-4626")[0] is None
        and expected_on(per_card, "8086-4626")[1] is not None)
    record_cases["a map where the card cannot be read is refused"] = (
        expected_on(per_card, None)[0] is None and expected_on(per_card, None)[1] is not None)
    record_cases["--verify accepts both forms"] = (
        expectation_problem("caught") is None and expectation_problem(per_card["expect"]) is None)
    record_cases["--verify refuses a malformed expectation"] = all(
        expectation_problem(bad) is not None
        for bad in ("kills", {}, {"10DE-25BA": "caught"}, {"10de-25ba": "kills"}, ["caught"]))
    # The cases a kill names (M1-115): Catch2 prints a heading for a skipped
    # case as well as for a failed one, and only a failed one caught anything.
    # Both texts are Catch2 3.16's own output, the paths shortened.
    record_cases["a skipped case is not named"] = failing_cases(CATCH2_SKIPPED) == []
    record_cases["a failed case is named by its case and its location, a skipped one beside it not"] = (
        failing_cases(CATCH2_MIXED) == ["a case", "/tmp/m/t.cpp:3"])
    record_cases["a run that passed names nothing"] = (
        failing_cases(CATCH2_SKIPPED.split(CATCH2_SEPARATOR)[0]) == [])
    record_cases["a test's own shorter line of dashes is not a heading"] = (
        failing_cases(CATCH2_MIXED.replace("Run with -? for options\n",
                                           "Run with -? for options\n" + "-" * 40 + "\n"))
        == ["a case", "/tmp/m/t.cpp:3"])
    # A kill by a named compiler warning (M1-117, register decision 462). The
    # error lines are clang's and gcc's own, 2026-10-10, the paths shortened;
    # clang's quotes the source, which is how a `==` put back inside a
    # static_assert reads.
    clang_kill = ("FAILED: [code=1] CMakeFiles/test_math.dir/tests/test_math.cpp.obj\n"
                  "In file included from C:\\u\\tests\\test_math.cpp:20:\n"
                  "C:/u/src\\core/Units.hpp:782:44: error: comparing floating point with == or != "
                  "is unsafe [-Werror,-Wfloat-equal]\n"
                  "  782 | static_assert(eccentricity(0.7306).value() == 0.7306);\n"
                  "1 error generated.\n")
    gcc_kill = ("/mnt/c/u/src/core/Units.hpp:734:38: error: comparing floating-point with "
                "'==' or '!=' is unsafe [-Werror=float-equal]\n"
                "cc1plus: all warnings being treated as errors\n")
    elsewhere = clang_kill.replace("src\\core/Units.hpp", "tests/test_math.cpp")
    beside = clang_kill + "C:/u/src\\core/Units.hpp:9:1: error: unknown type name 'x'\n"
    other_warning = clang_kill.replace("-Wfloat-equal", "-Wshadow")
    diagnostic_cases = {
        "clang's warning in the mutated file is a kill":
            diagnostic_kill(clang_kill, "float-equal", "src/core/Units.hpp") != "",
        "gcc's warning in the mutated file is a kill":
            diagnostic_kill(gcc_kill, "float-equal", "src/core/Units.hpp") != "",
        "the warning in another file is not": diagnostic_kill(elsewhere, "float-equal", "src/core/Units.hpp") == "",
        "another error beside it is not": diagnostic_kill(beside, "float-equal", "src/core/Units.hpp") == "",
        "another warning is not": diagnostic_kill(other_warning, "float-equal", "src/core/Units.hpp") == "",
        "a name the warning only begins with is not":
            diagnostic_kill(clang_kill, "float", "src/core/Units.hpp") == "",
        "a failed build with no error line is not": diagnostic_kill("FAILED: x\n", "float-equal", "x") == "",
        "--verify accepts a warning's name, or none":
            diagnostic_problem("float-equal") is None and diagnostic_problem(None) is None,
        "--verify refuses a name spelt with -W, or not a name":
            all(diagnostic_problem(bad) is not None for bad in ("-Wfloat-equal", "", 3, ["float-equal"])),
    }
    record_cases.update(diagnostic_cases)
    for name, ok in record_cases.items():
        failures += not ok
        print(f"self-test: {name}: {'ok' if ok else 'WRONG'}")
    # A restore that did not happen must stop the pass (M1-104): on 2026-09-30 a
    # mutant stayed in src/core/Scalar.hpp after its restore, and the next
    # mutant files ran on top of it.
    restore_cases = {
        "a restore that worked is accepted": restore_problem(0, "") is None,
        "a failed git checkout is refused": restore_problem(1, "") is not None,
        "a file still modified after the checkout is refused": restore_problem(0, " M src/core/Scalar.hpp\n") is not None,
    }
    for name, ok in restore_cases.items():
        failures += not ok
        print(f"self-test: {name}: {'ok' if ok else 'WRONG'}")
    # And the stop itself, in a scratch repository: a checkout that fails -- here
    # a file git does not know, which fails the same way on every system; on
    # Windows a file still held open fails it too, exit status 255, measured
    # 2026-09-30 -- must raise, not return.
    with tempfile.TemporaryDirectory() as scratch:
        subprocess.run(["git", "init", "-q", scratch], check=True, capture_output=True)
        try:
            restore(Path(scratch), {"not-in-git.hpp"})
            stopped = False
        except RuntimeError:
            stopped = True
    failures += not stopped
    print(f"self-test: a restore that fails stops the pass: {'ok' if stopped else 'WRONG'}")
    return 1 if failures else 0


def restore_problem(checkout_status: int, left: str):
    """Why a restore did not put the files back, or None: git's own exit status,
    and what `git status` still reports for the files afterwards."""
    if checkout_status != 0:
        return f"git checkout exited {checkout_status}"
    if left.strip():
        return f"still modified after git checkout: {' '.join(line.strip() for line in left.splitlines())}"
    return None


def pass_record(head: str, tree: str, judges, parts=None, gpu=None) -> dict:
    """What mutation-passes.json holds for a clean pass: since M1-109 also each
    judge's fingerprint in parts, and the graphics card for a file judged on it
    (register decisions 396 and 397)."""
    record = {"commit": head, "tree": tree, "date": datetime.date.today().isoformat()}
    if judges is not None:
        record["judges"] = judges
    if parts is not None:
        record["judge_parts"] = parts
    if gpu is not None:
        record["gpu"] = gpu
    return record


def judge_fingerprints(root: Path, tree: str, spec: dict) -> tuple:
    """(fingerprints, their parts, the graphics card), computed by
    mutants-due.py's own code so that the two can never disagree about what
    one is (M1-103, M1-109). Fingerprints and parts are None, with the reason
    printed, if they cannot be taken -- the file then stays under the strict
    rule, which is the safe side; the card is None for a file not judged on
    one, and None with the reason printed where it cannot be read, which
    leaves a GPU-judged file due."""
    sys.dont_write_bytecode = True  # no __pycache__ in scripts/
    loader = importlib.util.spec_from_file_location("mutants_due", root / "scripts" / "mutants-due.py")
    module = importlib.util.module_from_spec(loader)
    try:
        loader.loader.exec_module(module)
        reading = module.Tree((root / tree).resolve())
        parts = reading.fingerprint_parts(spec)
        found = {key: module.combine(value) for key, value in parts.items()}
        gpu = None
        if reading.needs_gpu(spec):
            gpu, why = module.gpu_identity((root / tree).resolve())
            if gpu is None:
                print(f"no graphics card recorded, so this file stays due: {why}")
        reading.save_hashes()
        return found, parts, gpu
    except (SystemExit, OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"no fingerprints recorded, so the build definition keeps this file due: {error}")
        return None, None, None


# Where a clean pass is recorded, file by file: the commit it ran at. The strict
# rerun rule (M1-92, ADR 0024, register decision 212) reads it -- a mutant file
# is due again when anything its judges depend on has changed since.
PASSES = "scripts/mutation-passes.json"
# A pass vouches for the committed code only if these match the commit. The
# mutant files and the measurement logs are left out: a pass edits its own
# file's note, and that changes no verdict.
VOUCHED_PATHS = ("CMakeLists.txt", "cmake", "data", "scripts", "shaders", "src", "tests")


def record_pass(root: Path, given: Path, tree: str, spec: dict) -> None:
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
    passes[given.name] = pass_record(head, tree, *judge_fingerprints(root, tree, spec))
    path.write_text(json.dumps(dict(sorted(passes.items())), indent=2) + "\n",
                    encoding="utf-8", newline="\n")
    print(f"pass recorded in {PASSES}: {given.name} at {head[:12]}")


if __name__ == "__main__":
    sys.exit(main(sys.argv))

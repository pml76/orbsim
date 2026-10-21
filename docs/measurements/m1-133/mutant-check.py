"""M1-133: the mutant check of register decision 552 (and 722 to 725 and 733).

    python -I docs/measurements/m1-133/mutant-check.py <mutant file> --tree <tree> --out <folder>
                  [--only <n,n,...>] [--plant]

Run from the root of the repository, with a clean working tree, after `check`
passed in the tree. For each mutant of the file, the script:

  1. writes the mutant into its file, as scripts/mutate.py does;
  2. builds the default target of the tree, which holds each program that a
     test starts;
  3. runs scripts/run-tests.py with the command line of the target `check`,
     read from the tree's build.ninja. The driver writes its selection into
     run-tests/selected.txt and run-tests/skipped.txt, and runs the selected
     tests;
  4. runs with ctest each test that the driver skipped, in the environment
     that the driver gives CTest;
  5. puts the source and the tree back with the snapshot functions of
     scripts/mutate.py, and makes sure that the store of the driver has the
     bytes that it had before the mutant.

A fault is a skipped test that does not pass. A skipped test passed at its
last run, thus each skipped test must have a result, and the result must be a
pass. A skipped test with no result, or with a skip of its own, is also a
fault, because then the check did not examine it (VERIFICATION.md rule 23).
A mutant that fails the build at a static_assert has no selection, and is
recorded as such.

--plant puts in one fault, to show that the check finds it: for
the first mutant with a failed selected test, it moves that test from the
selection to the skipped tests. The run then passes only if that one test,
and no other, is reported as a fault.

The snapshot is the one of scripts/mutate.py, in build/mutate-snapshot-<tree>,
thus `python scripts/mutate.py --restore --tree <tree>` puts the tree back
after a stop. The results go into <folder>/results.json, one entry for each
mutant: a run with --only replaces the entries of its mutants and keeps the
others. The output of each step goes into <folder>/<n>-*.txt.
"""

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path.cwd()
# A full test run is 265 s in Debug (check-full, 2026-10-21): each limit is
# a bound for a hang, not a limit for a slow machine.
DRIVER_SECONDS = 3600
CTEST_SECONDS = 3600
# The command of the target `check` in build.ninja, as CMakeLists.txt writes
# it: Python, the driver, the tree, the ctest program and the jobs.
CHECK_COMMAND = re.compile(r'"(?P<python>[^"]+)" (?P<driver>\S+/scripts/run-tests\.py) (?P<tree>\S+) '
                           r'--ctest (?P<ctest>\S+) --jobs (?P<jobs>\d+)"$')


def load(name: str, path: Path):
    """A script of scripts/ as a module, as mutate.py loads mutants-due.py."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


MUTATE = load("mutate", ROOT / "scripts" / "mutate.py")
DRIVER = load("run_tests", ROOT / "scripts" / "run-tests.py")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command: list, cwd: Path, seconds: int, env=None) -> tuple:
    """(exit code, output) of a child. The output is decoded as UTF-8, with
    the bytes that cannot be decoded replaced, as scripts/mutate.py does."""
    done = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=seconds, check=False)
    return done.returncode, done.stdout + done.stderr


def check_command(tree: Path) -> list:
    """The command line of `check` in this tree, from its build.ninja: one
    command, and only one, without --full."""
    found = []
    for line in (tree / "build.ninja").read_text(encoding="utf-8").splitlines():
        match = CHECK_COMMAND.search(line.strip())
        if match:
            found.append(match)
    if len(found) != 1:
        raise RuntimeError(f"{tree / 'build.ninja'} holds {len(found)} commands of `check`, not one")
    match = found[0]
    if Path(match["tree"]).resolve() != tree:
        raise RuntimeError(f"the command of `check` names the tree {match['tree']}, not {tree}")
    return [match["python"], match["driver"], match["tree"], "--ctest", match["ctest"], "--jobs", match["jobs"]]


def names_in(path: Path):
    """The names in a file of the driver, one on each line, or None if the
    driver did not write the file."""
    if not path.is_file():
        return None
    return [line for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def driver_env(tree: Path) -> dict:
    """The environment that the driver gives CTest (run_tree in run-tests.py)."""
    env = {**os.environ, DRIVER.RECORDS_VARIABLE: str(tree / DRIVER.COVERAGE_RECORDS)}
    return DRIVER.without_test_name(env)


def run_driver(tree: Path, command: list, out: Path) -> tuple:
    """(exit code, selected, skipped, results of the selected tests). The two
    files of the selection are removed first. Thus, if the driver stops
    before it writes them, the files of an earlier run are not read."""
    state = tree / DRIVER.STATE
    for name in (DRIVER.SELECTED, DRIVER.SKIPPED, DRIVER.REPORT):
        (state / name).unlink(missing_ok=True)
    code, text = run(command, tree, DRIVER_SECONDS)
    out.write_text(f"exit {code}\n{text}", encoding="utf-8")
    selected, skipped = names_in(state / DRIVER.SELECTED), names_in(state / DRIVER.SKIPPED)
    report = state / DRIVER.REPORT
    results = DRIVER.outcomes(report.read_text(encoding="utf-8")) if selected and report.is_file() else {}
    return code, selected, skipped, results


def run_skipped(tree: Path, command: list, names: list, out: Path) -> dict:
    """The result of each test in `names`, run with the ctest and the jobs of
    `check`. The setup tests of their fixtures run with them, as in a full
    run."""
    if not names:
        return {}
    listing, report = out.with_suffix(".tests"), out.with_suffix(".xml")
    listing.write_text("".join(f"{name}\n" for name in names), encoding="utf-8")
    report.unlink(missing_ok=True)
    ctest, jobs = command[4], command[6]
    code, text = run([ctest, "--output-on-failure", "-j", jobs, "--tests-from-file", str(listing),
                      "--output-junit", str(report)], tree, CTEST_SECONDS, env=driver_env(tree))
    out.write_text(f"exit {code}\n{text}", encoding="utf-8")
    return DRIVER.outcomes(report.read_text(encoding="utf-8")) if report.is_file() else {}


def faults_of(skipped: list, results: dict) -> list:
    """Each skipped test that did not pass, with why."""
    return [f"{name}: {'no result' if results.get(name) is None else results[name]}"
            for name in skipped if results.get(name) != "passed"]


def apply_mutant(mutant: dict) -> str:
    """Why the mutant cannot be written, or an empty string."""
    path = ROOT / mutant["file"]
    text = path.read_text(encoding="utf-8")
    found = text.count(mutant["find"])
    if found != 1:
        return f"its anchor is {found} times in {mutant['file']}"
    path.write_text(text.replace(mutant["find"], mutant["replace"]), encoding="utf-8", newline="\n")
    return ""


def check_mutant(number: int, mutant: dict, tree: Path, args, plant: bool) -> dict:
    entry = {"number": number, "name": mutant["name"], "file": mutant["file"]}
    began = time.monotonic()
    problem = apply_mutant(mutant)
    if problem:
        return {**entry, "verdict": "INVALID", "why": problem}
    code, text = run([args.cmake, "--build", str(tree)], ROOT, MUTATE.BUILD_TIMEOUT_SECONDS)
    (args.out / f"{number}-build.txt").write_text(text, encoding="utf-8")
    entry["build_seconds"] = round(time.monotonic() - began, 1)
    if code != 0:
        fired = MUTATE.static_assert_message(text)
        if fired:
            return {**entry, "verdict": "BUILD FAILED AT A STATIC_ASSERT", "why": fired[:200]}
        return {**entry, "verdict": "INVALID", "why": f"does not compile: {MUTATE.first_error(text)[:200]}"}

    began = time.monotonic()
    code, selected, skipped, results = run_driver(tree, args.command, args.out / f"{number}-driver.txt")
    entry["driver_seconds"] = round(time.monotonic() - began, 1)
    entry["driver_exit"] = code
    if selected is None or skipped is None:
        return {**entry, "verdict": "PROBLEM", "why": "the driver did not write its selection"}
    failed_selected = sorted(name for name in selected if results.get(name) == "failed")
    planted = None
    if plant and failed_selected:
        planted = failed_selected[0]
        selected = [name for name in selected if name != planted]
        skipped = sorted([*skipped, planted])
    if set(selected) & set(skipped) or not selected + skipped:
        return {**entry, "verdict": "PROBLEM", "why": "the selection and the skipped tests overlap, or are empty"}

    began = time.monotonic()
    found = run_skipped(tree, args.command, skipped, args.out / f"{number}-skipped.txt")
    entry["skipped_seconds"] = round(time.monotonic() - began, 1)
    faults = faults_of(skipped, found)
    entry.update({
        "selected": len(selected), "skipped": len(skipped),
        "failed_selected": [name for name in failed_selected if name != planted],
        "faults": faults, "planted": planted,
        "verdict": "FAULT" if faults else ("CAUGHT, SELECTED" if failed_selected else "NO TEST FAILED"),
    })
    return entry


def put_back(tree: Path, args, place: Path, store_before: str) -> None:
    MUTATE.put_back(ROOT, args.cmake, tree, place)
    store = tree / DRIVER.STATE / DRIVER.STORE
    if sha256(store) != store_before:
        raise RuntimeError(f"{store} has other bytes than before the mutant")


def start_state(tree: Path, args) -> dict:
    """The tree built, and the driver run one time with no mutant: then the
    store holds a pass of each test (decision 724). None if one of the two
    fails."""
    code, text = run([args.cmake, "--build", str(tree)], ROOT, MUTATE.BUILD_TIMEOUT_SECONDS)
    (args.out / "0-build.txt").write_text(text, encoding="utf-8")
    if code != 0:
        return None
    code, selected, skipped, results = run_driver(tree, args.command, args.out / "0-driver.txt")
    if code != 0 or selected is None or skipped is None:
        return None
    return {"commit": DRIVER.git(ROOT, "rev-parse", "HEAD").strip(), "tree": args.tree,
            "selected": selected, "skipped": len(skipped), "results": results}


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("mutants")
    parser.add_argument("--tree", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--cmake", default=MUTATE.DEFAULT_CMAKE)
    parser.add_argument("--only", help="the numbers of the mutants to run, from 1, with commas")
    parser.add_argument("--plant", action="store_true")
    args = parser.parse_args(argv[1:])
    args.out = Path(args.out).resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    tree = (ROOT / args.tree).resolve()
    args.command = check_command(tree)

    spec = json.loads((ROOT / args.mutants).read_text(encoding="utf-8"))
    chosen = {int(n) for n in args.only.split(",")} if args.only else None
    mutants = [(n, m) for n, m in enumerate(spec["mutants"], 1) if chosen is None or n in chosen]
    if DRIVER.git(ROOT, "status", "--porcelain").strip():
        print("refusing to start: the working tree is not clean", file=sys.stderr)
        return 2
    place = MUTATE.snapshot_place(ROOT, args.tree)
    problem = MUTATE.leftover_problem(place, args.tree)
    if problem:
        print(f"refusing to start: {problem}", file=sys.stderr)
        return 2
    start = start_state(tree, args)
    if start is None:
        print(f"refusing to start: the tree does not build, or `check` fails with no mutant; see {args.out}",
              file=sys.stderr)
        return 2
    print(f"start at {start['commit'][:7]}: {len(start['selected'])} tests run with no mutant "
          f"({', '.join(start['selected'][:8])}), {start['skipped']} skipped", flush=True)
    store_before = sha256(tree / DRIVER.STATE / DRIVER.STORE)
    MUTATE.take_snapshot(tree, place, MUTATE.source_stamps(ROOT, {m["file"] for _, m in mutants}))

    path = args.out / "results.json"
    kept = json.loads(path.read_text(encoding="utf-8")) if path.is_file() else {"starts": [], "mutants": []}
    kept["starts"].append(start)
    entries, planted_once, finished = [], False, False
    try:
        for number, mutant in mutants:
            entry = check_mutant(number, mutant, tree, args, args.plant and not planted_once)
            planted_once = planted_once or bool(entry.get("planted"))
            put_back(tree, args, place, store_before)
            entry["put_back"] = True
            entry["commit"] = start["commit"]
            entries.append(entry)
            print(f"{entry['verdict']:32} {number:2} {mutant['name']}"
                  + (f" -- selected {entry['selected']}, skipped {entry['skipped']}, failed selected "
                     f"{len(entry['failed_selected'])}" if "selected" in entry else f" -- {entry.get('why', '')}"),
                  flush=True)
            for fault in entry.get("faults", []):
                print(f"{'':32}    FAULT {fault}", flush=True)
            done = {e["number"] for e in entries}
            kept["mutants"] = sorted([e for e in kept["mutants"] if e["number"] not in done] + entries,
                                     key=lambda e: e["number"])
            path.write_text(json.dumps(kept, indent=1), encoding="utf-8")
        finished = True
    finally:
        if not finished:
            try:
                MUTATE.put_back(ROOT, args.cmake, tree, place)
            except (RuntimeError, OSError, subprocess.SubprocessError) as error:
                print(f"stopped, and the tree could not be put back: {error}\n"
                      f"The snapshot is kept: {MUTATE.leftover_problem(place, args.tree)}", file=sys.stderr)
                place = None
        if place is not None:
            shutil.rmtree(place)

    faults = [(e["number"], f) for e in entries for f in e.get("faults", [])]
    invalid = [e for e in entries if e["verdict"] in ("INVALID", "PROBLEM")]
    print(f"\n{len(entries)} mutants: {sum(e['verdict'] == 'CAUGHT, SELECTED' for e in entries)} caught by a "
          f"selected test, {sum(e['verdict'].startswith('BUILD FAILED') for e in entries)} at the build, "
          f"{sum(e['verdict'] == 'NO TEST FAILED' for e in entries)} with no failed test, {len(faults)} faults, "
          f"{len(invalid)} invalid or with a problem")
    if args.plant:
        wanted = [e for e in entries if e.get("planted")]
        ok = (len(wanted) == 1 and faults == [(wanted[0]["number"], f"{wanted[0]['planted']}: failed")]
              and not invalid)
        print("the planted fault was found, and no other" if ok else
              "the planted fault was NOT found as the only fault: the check cannot be trusted")
        return 0 if ok else 1
    return 1 if faults or invalid else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
"""One run of item 4 of M1-137: all tests of a tree, with some variables changed.

    environment-runs.py <build tree> <scratch folder> <run>

M1-137 item 4, register decisions 850, 851 and 882-887; the results are in
docs/measurements/m1-137-environment.md. <run> is `none`, `group-1`,
`group-2`, or the name of one entry of a group, for example `PYTHONHOME` or
`GIT_CONFIG_*`. The script:

  - makes, in the scratch folder, a folder with no Python for PYTHONHOME and
    a new repository with one commit for GIT_DIR and GIT_WORK_TREE, and in
    the temporary folder of the session a folder with a space and `ä` in its
    name for TEMP and TMP. Not beside it: on this machine Windows sends the
    files that the desktop app of Claude writes in AppData/Local, but not in
    its Temp, to the folder of its package, and the path then becomes about
    55 characters longer, which is a second change;
  - makes the environment that scripts/run-tests.py gives CTest, with the
    functions of the driver, and changes in it the variables of the run, and
    nowhere else (decision 882): the script itself, and CTest, find their
    programs with the environment of the session;
  - empties coverage-records/ of the tree, as the driver does, and runs
    CTest for all tests, with the jobs of `check`;
  - writes <scratch>/<tree>-<run>.json with the result of each test, and
    gives each test whose result is not the result of the run `none`
    (decision 884).

Run no build and no other run of the tests at the same time. The store of
the driver is not changed; the tests write their outputs and records as in
each run.
"""
import importlib.util
import json
import os
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]
# The jobs of `check` on the first machine (ORBSIM_TEST_JOBS, half of the
# logical processors), and a limit for a test that the run makes wait.
JOBS = 10
TEST_SECONDS = 600


def driver():
    """scripts/run-tests.py as a module; its name has a `-`, thus no import."""
    spec = importlib.util.spec_from_file_location("run_tests", ROOT / "scripts" / "run-tests.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["run_tests"] = module
    spec.loader.exec_module(module)
    return module


def places(scratch: pathlib.Path) -> dict:
    """The folders and the repository that the values of decision 883 name."""
    temp = pathlib.Path(os.environ["TEMP"]) / "m137 ä t"
    no_python = scratch / "no-python"
    repository = scratch / "other-repository"
    for folder in (temp, no_python):
        folder.mkdir(parents=True, exist_ok=True)
    if not (repository / ".git").is_dir():
        repository.mkdir(parents=True, exist_ok=True)
        (repository / "a.txt").write_text("a\n", encoding="utf-8")
        for command in (["init", "-q"], ["add", "a.txt"], ["-c", "user.name=t", "-c", "user.email=t@t", "commit",
                                                          "-q", "-m", "one commit"]):
            subprocess.run(["git", *command], cwd=repository, check=True, capture_output=True)
    return {"temp": str(temp), "no python": str(no_python), "repository": str(repository)}


def groups(where: dict) -> dict:
    """The two groups of decision 850, with the values of decision 883: each
    entry a name and {variable: value}, where None removes the variable."""
    group_1 = {
        "TEMP": {"TEMP": where["temp"]},
        "TMP": {"TMP": where["temp"]},
        "TZ": {"TZ": "EST5EDT"},
        "LANG": {"LANG": "de_DE.UTF-8"},
        "LC_ALL": {"LC_ALL": "de_DE.UTF-8"},
        "HTTP_PROXY": {"HTTP_PROXY": "http://127.0.0.1:9"},
        "HTTPS_PROXY": {"HTTPS_PROXY": "http://127.0.0.1:9"},
        "NO_PROXY": {"NO_PROXY": None},
    }
    group_2 = {
        "PATHEXT": {"PATHEXT": ".COM"},
        "NoDefaultCurrentDirectoryInExePath": {"NoDefaultCurrentDirectoryInExePath": None},
        "__COMPAT_LAYER": {"__COMPAT_LAYER": "WIN8RTM"},
        "PYTHONHOME": {"PYTHONHOME": where["no python"]},
        "PYTHONSAFEPATH": {"PYTHONSAFEPATH": "1"},
        "PYTHONNOUSERSITE": {"PYTHONNOUSERSITE": "1"},
        "PYTHONUTF8": {"PYTHONUTF8": "1"},
        "PYTHONIOENCODING": {"PYTHONIOENCODING": "ascii"},
        "PYTHONDONTWRITEBYTECODE": {"PYTHONDONTWRITEBYTECODE": None},
        "PYTHONHASHSEED": {"PYTHONHASHSEED": "0"},
        "GIT_DIR": {"GIT_DIR": str(pathlib.Path(where["repository"]) / ".git")},
        "GIT_WORK_TREE": {"GIT_WORK_TREE": where["repository"]},
        "GIT_CONFIG_*": {"GIT_CONFIG_COUNT": "1", "GIT_CONFIG_KEY_0": "color.ui", "GIT_CONFIG_VALUE_0": "always"},
        "CTEST_*": {"CTEST_NO_TESTS_ACTION": "error", "CTEST_PARALLEL_LEVEL": "1"},
        "CMAKE_*": {"CMAKE_BUILD_TYPE": "Release",
                    "CMAKE_CXX_COMPILER_LAUNCHER": str(pathlib.Path(where["no python"]) / "no-launcher.exe")},
    }
    return {"group-1": group_1, "group-2": group_2}


def changes_of(run: str, where: dict) -> dict:
    """{variable: value or None} of the run `run`."""
    if run == "none":
        return {}
    found = groups(where)
    if run in found:
        return {name: value for entry in found[run].values() for name, value in entry.items()}
    for entry in found.values():
        if run in entry:
            return dict(entry[run])
    raise SystemExit(f"{run} is not none, a group or an entry of a group")


def changed(env: dict, changes: dict) -> dict:
    """`env` with `changes`, names compared without case on Windows, as
    Windows does."""
    fold = (lambda n: n.upper()) if os.name == "nt" else (lambda n: n)
    names = {fold(name) for name in changes}
    kept = {name: value for name, value in env.items() if fold(name) not in names}
    return {**kept, **{name: value for name, value in changes.items() if value is not None}}


def main(tree: pathlib.Path, scratch: pathlib.Path, run: str) -> int:
    rt = driver()
    tree, scratch = tree.resolve(), scratch.resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    where = places(scratch)
    changes = changes_of(run, where)
    records = tree / rt.COVERAGE_RECORDS
    env = rt.with_hook(rt.without_test_name({**os.environ, rt.RECORDS_VARIABLE: str(records)}), tree, ROOT)
    env = changed(env, changes)
    left = rt.empty_folder(records)
    if left:
        raise SystemExit(f"{records} could not be emptied: {left[0].name}")
    (tree / rt.RECORDS).mkdir(parents=True, exist_ok=True)
    report = scratch / f"{tree.name}-{run}.xml".replace("*", "x")
    ctest = rt.cache_value(tree, "CMAKE_CTEST_COMMAND") or "ctest"
    began = time.monotonic()
    with open(scratch / f"{tree.name}-{run}.log".replace("*", "x"), "w", encoding="utf-8") as log:
        code = subprocess.run([ctest, "--output-on-failure", "-j", str(JOBS), "--timeout", str(TEST_SECONDS),
                               "--output-junit", str(report)], cwd=tree, env=env, stdout=log,
                              stderr=subprocess.STDOUT, check=False).returncode
    seconds = time.monotonic() - began
    outcomes = rt.outcomes(report.read_text(encoding="utf-8"))
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True,
                          check=True).stdout.strip()
    result = {"tree": tree.name, "run": run, "commit": head, "changes": changes, "exit": code,
              "seconds": round(seconds, 1), "outcomes": outcomes}
    (scratch / f"{tree.name}-{run}.json".replace("*", "x")).write_text(json.dumps(result, indent=1, sort_keys=True),
                                                                        encoding="utf-8")
    counts = {kind: sum(1 for value in outcomes.values() if value == kind) for kind in ("passed", "failed", "skipped")}
    print(f"{tree.name} {run}: exit {code}, {seconds:.0f} s, {len(outcomes)} tests: {counts}")
    base = scratch / f"{tree.name}-none.json"
    if run != "none" and base.is_file():
        before = json.loads(base.read_text(encoding="utf-8"))["outcomes"]
        moved = sorted(name for name in set(before) | set(outcomes) if before.get(name) != outcomes.get(name))
        print(f"  {len(moved)} tests with a result that is not the result of the run none")
        for name in moved:
            print(f"    {name}: {before.get(name)} -> {outcomes.get(name)}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    sys.exit(main(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3]))

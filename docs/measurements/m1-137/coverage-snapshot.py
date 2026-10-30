#!/usr/bin/env python3
"""Keep a snapshot of the store of a tree after a full run.

    coverage-snapshot.py <build tree> <out.json>

M1-137 item 1, register decisions 845, 858 and 870; the results are in
docs/measurements/m1-137-coverage.md. The script reads the store of the
driver, <tree>/run-tests/fingerprints.json, through the function load_store
of scripts/run-tests.py, and writes two things to <out.json>:

    functions   for each test that takes a program function by function,
                the names of the functions that it entered in each program
    hashes      the SHA-256 of each counted file of the build tree, from the
                read lines of the store, `missing` for a path where there is
                no file and `folder` for a folder

A counted file (decision 858) is each file of the build tree in a read line,
but not the files of the tools: those in run-tests/, test-reads/ and Testing/,
the three files of scripts/mutants-due.py, .ninja_deps and .ninja_log. The
tree is only read. coverage-compare.py compares two snapshots.
"""
import hashlib
import importlib.util
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
# Decision 858: the files of the tools, in lower case, relative to the tree.
TOOL_FOLDERS = ("run-tests/", "test-reads/", "testing/")
TOOL_FILES = ("mutants-due-graph.json", "mutants-due-hashes-2.json", "mutants-due-sidecars.json", ".ninja_deps",
              ".ninja_log")


def driver():
    """scripts/run-tests.py as a module; its name has a `-`, thus no import."""
    spec = importlib.util.spec_from_file_location("run_tests", ROOT / "scripts" / "run-tests.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["run_tests"] = module
    spec.loader.exec_module(module)
    return module


def counted(relative: str) -> bool:
    return not (relative.startswith(TOOL_FOLDERS) or relative in TOOL_FILES)


def main(tree: pathlib.Path, out: pathlib.Path) -> int:
    run_tests = driver()
    store = run_tests.load_store(tree / run_tests.STATE)
    prefix = str(tree.resolve()).replace("\\", "/").lower() + "/"
    functions, paths = {}, set()
    for name, entry in store["tests"].items():
        programs = entry.get("programs") or {}
        if programs:
            functions[name] = {key: sorted(program["keys"]) for key, program in programs.items()}
        for _, path in entry.get("reads") or []:
            lower = path.replace("\\", "/").lower()
            if lower.startswith(prefix) and counted(lower[len(prefix):]):
                paths.add(path)
    hashes = {}
    for path in sorted(paths):
        p = pathlib.Path(path)
        hashes[path] = (hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file()
                        else "folder" if p.is_dir() else "missing")
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True,
                            check=True).stdout.strip()
    out.write_text(json.dumps({"tree": str(tree), "commit": commit, "tests": len(store["tests"]),
                               "functions": functions, "hashes": hashes}, sort_keys=True), encoding="utf-8")
    print(f"{out.name}: {len(store['tests'])} tests in the store, {len(functions)} by function, "
          f"{len(hashes)} counted files, at {commit[:12]}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sys.exit(main(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])))

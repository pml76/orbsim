#!/usr/bin/env python3
"""List the mutant files a change makes due to run again (M1-92).

    mutants-due.py <build tree> [--assume-changed <path>] ... [--only-assumed]
    mutants-due.py --self-test

Why this exists: whether a test catches a mutant depends on three things --
the mutated file, the test, and all the code between them, test helpers and
the production code the test runs through. A change to any of them can turn a
caught mutant into a survivor, and nothing said which older mutant files a
task should run again: that was judgement, and judgement can forget. The rule
since M1-92 (ADR 0024, register decision 212, the *strict* one): **a mutant
file is due when anything its judges depend on has changed since it last
passed.** A task runs every file this lists before its commit; the full pass
runs at every gate.

"Last passed" is the commit `scripts/mutate.py` recorded in
`scripts/mutation-passes.json` after a clean run. "Anything its judges depend
on" is, deliberately, more than it strictly needs:

  * every file its mutants mutate;
  * everything each judging program is built from -- Ninja's `-t inputs`,
    transitively, and every header its objects include, from Ninja's
    dependency log; the judges are the file's `suites` and `targets`, and the
    programs and scripts behind its `ctest` entries;
  * at run time: `shaders/` for a judge that runs the application, `data/`
    for every judge, and every file named on a ctest entry's command line;
  * and everything that decides how anything is built or judged: the build
    definition (`CMakeLists.txt`, `cmake/`, `CMakePresets.json`), and the
    harness itself.

It over-approximates on purpose: listing a file that did not need to run
costs minutes, and missing one costs a hole nobody sees.

Proven able to fail before it was trusted: --self-test gives the decision
function changed files inside and outside a file's inputs, and no record at
all, and exits non-zero unless each is judged right.
"""

import json
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PASSES = ROOT / "scripts" / "mutation-passes.json"
ALWAYS = ("CMakeLists.txt", "CMakePresets.json", "cmake/", "scripts/mutate.py", "data/")


def norm(path) -> str:
    text = os.path.abspath(str(path)).replace("\\", "/")
    return text.lower() if os.name == "nt" else text


def is_due(changed: set, inputs: set, recorded: bool) -> tuple:
    """(due, reasons): due when there is no record, or a changed file is an input."""
    if not recorded:
        return True, ["no recorded pass"]
    hits = sorted(changed & inputs)
    root = norm(ROOT) + "/"
    always = [c for c in changed if any(c.startswith(root + a.lower() if os.name == "nt" else root + a)
                                        for a in ALWAYS)]
    reasons = hits + sorted(set(always) - set(hits))
    return bool(reasons), reasons


def run(cmd, cwd) -> str:
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", check=True).stdout


def ninja_program(tree: pathlib.Path) -> str:
    for line in (tree / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CMAKE_MAKE_PROGRAM:"):
            return line.split("=", 1)[1]
    return "ninja"


def ctest_program(tree: pathlib.Path) -> str:
    for line in (tree / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CMAKE_COMMAND:"):
            cmake = pathlib.Path(line.split("=", 1)[1])
            return str(cmake.with_name("ctest" + cmake.suffix))
    return "ctest"


class Tree:
    def __init__(self, tree: pathlib.Path):
        self.tree = tree
        self.ninja = ninja_program(tree)
        self.headers = self._headers()
        tests = json.loads(run([ctest_program(tree), "--show-only=json-v1"], tree))["tests"]
        self.tests = {t["name"]: t for t in tests}
        self._inputs = {}

    def _headers(self) -> dict:
        """object -> the headers the compiler reported for it, from Ninja's log."""
        found, current = {}, None
        for line in run([self.ninja, "-t", "deps"], self.tree).splitlines():
            if line and not line.startswith(" ") and ": #deps" in line:
                current = norm(self.tree / line.split(": #deps")[0])
                found[current] = set()
            elif line.startswith("    ") and current:
                found[current].add(norm(line.strip()))
        return found

    def build_inputs(self, target: str) -> set:
        if target not in self._inputs:
            files = set()
            for line in run([self.ninja, "-t", "inputs", target], self.tree).splitlines():
                path = norm(self.tree / line.strip()) if not os.path.isabs(line.strip()) else norm(line.strip())
                files.add(path)
                files |= self.headers.get(path, set())
            self._inputs[target] = files
        return self._inputs[target]

    def program_target(self, name: str) -> str:
        for candidate in (name + ".exe", name):
            if (self.tree / candidate).exists():
                return candidate
        return name

    @staticmethod
    def runtime_inputs(program: str) -> set:
        """What a program reads when it runs rather than when it is built: the
        application loads its shaders from disk at start-up, so a shader edit
        leaves orbsim.exe byte for byte the same (the review of 2026-09-26)."""
        if program == "orbsim":
            return {norm(p) for p in (ROOT / "shaders").rglob("*") if p.is_file()}
        return set()

    def judge_inputs(self, spec: dict) -> set:
        files = set()
        for mutant in spec["mutants"]:
            files.add(norm(ROOT / mutant["file"]))
            programs = list(mutant.get("suites", [])) + list(mutant.get("targets", []))
            for name in programs:
                files |= self.build_inputs(self.program_target(name))
                files |= self.runtime_inputs(name)
            for entry in mutant.get("ctest", []):
                test = self.tests.get(entry)
                if test is None:
                    files.add(f"<unknown ctest entry {entry}>")
                    continue
                for arg in test.get("command", []):
                    value = arg.split("=", 1)[1] if arg.startswith("-D") and "=" in arg else arg
                    path = pathlib.Path(value)
                    if path.is_file():
                        files.add(norm(path))
                        # Only a program this tree builds has build inputs; cmake
                        # or python running a wrapper is a tool, not a judge.
                        inside = norm(path).startswith(norm(self.tree) + "/")
                        if inside and (path.suffix.lower() == ".exe" or os.access(path, os.X_OK)):
                            files |= self.build_inputs(os.path.relpath(path, self.tree).replace("\\", "/"))
                            files |= self.runtime_inputs(path.stem)
        return files


def changed_since(commit: str) -> set:
    names = run(["git", "diff", "--name-only", commit], ROOT).splitlines()
    names += run(["git", "ls-files", "--others", "--exclude-standard"], ROOT).splitlines()
    return {norm(ROOT / n) for n in names if n}


def main_check(tree: pathlib.Path, assumed: list, only_assumed: bool, expect_due: list) -> int:
    passes = json.loads(PASSES.read_text(encoding="utf-8")) if PASSES.exists() else {}
    t = Tree(tree)
    due_count = 0
    due_names = set()
    files = sorted((ROOT / "scripts" / "mutants").glob("*.json"))
    for spec_path in files:
        spec = json.loads(spec_path.read_text(encoding="utf-8"))
        record = passes.get(spec_path.name)
        changed = changed_since(record["commit"]) if record and not only_assumed else set()
        changed |= {norm(ROOT / a) for a in assumed}
        due, reasons = is_due(changed, t.judge_inputs(spec), record is not None or only_assumed)
        root = norm(ROOT) + "/"
        shown = [r[len(root):] if r.startswith(root) else r for r in reasons[:3]]
        if due:
            due_count += 1
            due_names.add(spec_path.name)
            more = f" and {len(reasons) - 3} more" if len(reasons) > 3 else ""
            print(f"DUE      {spec_path.name}: {', '.join(shown)}{more}")
        else:
            since = f"passed at {record['commit'][:12]}" if record else "no recorded pass, judged on the assumed changes only"
            print(f"current  {spec_path.name} ({since})")
    print(f"mutants-due: {due_count} of {len(files)} mutant files due")
    # --expect-due: how CTest checks the real computation against this tree.
    missing = [name for name in expect_due if name not in due_names]
    for name in missing:
        print(f"mutants-due: expected {name} to be due, and it is not")
    return 1 if missing else 0


def self_test() -> int:
    root = norm(ROOT) + "/"
    inputs = {root + "src/view/camera.cpp", root + "tests/test_camera.cpp", root + "src/core/units.hpp"}
    cases = {
        "nothing changed": (set(), True, False),
        "the mutated file changed": ({root + "src/view/camera.cpp"}, True, True),
        "a header between changed": ({root + "src/core/units.hpp"}, True, True),
        "an unrelated file changed": ({root + "src/astro/sun.cpp"}, True, False),
        "the build definition changed": ({root + "cmakelists.txt"}, True, True),
        "the harness changed": ({root + "scripts/mutate.py"}, True, True),
        "data changed": ({root + "data/skyfield/x.txt"}, True, True),
        "no recorded pass": (set(), False, True),
    }
    if os.name != "nt":
        cases["the build definition changed"] = ({root + "CMakeLists.txt"}, True, True)
    failures = 0
    for name, (changed, recorded, want) in cases.items():
        got, _ = is_due(changed, inputs, recorded)
        status = "ok" if got == want else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name}: {'due' if got else 'current'} ({status})")
    return 1 if failures else 0


def main() -> int:
    args = sys.argv[1:]
    if args == ["--self-test"]:
        return self_test()
    if not args:
        print(__doc__)
        return 2
    tree = pathlib.Path(args[0]).resolve()
    assumed = [args[i + 1] for i, a in enumerate(args) if a == "--assume-changed" and i + 1 < len(args)]
    # --only-assumed: ignore git and the records, and judge only the files named
    # with --assume-changed -- how the rule's precision is checked.
    expect_due = [args[i + 1] for i, a in enumerate(args) if a == "--expect-due" and i + 1 < len(args)]
    return main_check(tree, assumed, "--only-assumed" in args, expect_due)


if __name__ == "__main__":
    sys.exit(main())

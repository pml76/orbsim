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
passed.** Every file this lists runs at each phase gate, and a task runs its
own file after its commit and before its push (register decision 276, since
2026-10-03).

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
  * the harness itself, and `data/`;
  * and the build definition (`CMakeLists.txt`, `cmake/`, `CMakePresets.json`)
    through what it changed (M1-103, register decision 250): a clean pass
    records a fingerprint of each judge, and a judge whose fingerprint moved
    makes its file due. Where no fingerprint can be compared -- none recorded,
    or recorded in another tree -- the build definition makes every file due,
    as before.

Each file is judged in the tree its pass was recorded in, whichever tree is
named (M1-106): only there can its fingerprints be compared.

It over-approximates on purpose: listing a file that did not need to run
costs minutes, and missing one costs a hole nobody sees.

Proven able to fail before it was trusted: --self-test gives the decision
function changed files inside and outside a file's inputs, and no record at
all, and exits non-zero unless each is judged right.
"""

import contextlib
import hashlib
import io
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
PASSES = ROOT / "scripts" / "mutation-passes.json"
ALWAYS = ("scripts/mutate.py", "data/")
# The build definition makes every file due only where no fingerprints can say
# what a change to it did (M1-103, register decision 250).
BUILD_DEFINITION = ("CMakeLists.txt", "CMakePresets.json", "cmake/")


def norm(path) -> str:
    text = os.path.abspath(str(path)).replace("\\", "/")
    return text.lower() if os.name == "nt" else text


def is_due(changed: set, inputs: set, recorded: bool, judged=None) -> tuple:
    """(due, reasons): due when there is no record, or a changed file is an input.

    `judged` is the list of judges whose fingerprint changed since the pass, or
    None where no fingerprints can be compared (M1-103)."""
    if not recorded:
        return True, ["no recorded pass"]
    hits = sorted(changed & inputs)
    root = norm(ROOT) + "/"
    # The build definition counts by itself only where no fingerprint can say
    # what a change to it did (M1-103).
    watched = ALWAYS + BUILD_DEFINITION if judged is None else ALWAYS
    always = [c for c in changed if any(c.startswith(root + a.lower() if os.name == "nt" else root + a)
                                        for a in watched)]
    reasons = hits + sorted(set(always) - set(hits)) + sorted(judged or [])
    return bool(reasons), reasons


def placeholders(text: str, tree: pathlib.Path, root: pathlib.Path) -> str:
    """`text` with the build tree's path and the repository's replaced by <TREE> and <ROOT>.

    Either separator between the parts of a path, since Windows tools mix them
    in one path, and either case on Windows. The tree first: it lies inside
    the repository."""
    flags = re.IGNORECASE if os.name == "nt" else 0
    for path, mark in ((tree, "<TREE>"), (root, "<ROOT>")):
        parts = [p for p in re.split(r"[/\\]", os.path.abspath(str(path))) if p]
        text = re.sub(r"[/\\]*".join(re.escape(p) for p in parts) if os.name == "nt"
                      else "/" + "/".join(re.escape(p) for p in parts), mark, text, flags=flags)
    return text


def fingerprint(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]


def program_fingerprint(commands: str, files: dict, tree: pathlib.Path, root: pathlib.Path) -> str:
    """A judging program: every command Ninja runs to build it, and the contents
    of each file it is built from that git does not track and no build step makes."""
    listed = "\n".join(f"{placeholders(path, tree, root)} {digest}" for path, digest in sorted(files.items()))
    return fingerprint(placeholders(commands, tree, root) + "\n--\n" + listed)


def test_fingerprint(test: dict, tree: pathlib.Path, root: pathlib.Path) -> str:
    """A judging CTest entry: its command and properties -- not the line of
    CMakeLists.txt it was declared on, which moves with every edit above it."""
    kept = {key: value for key, value in test.items() if key != "backtrace"}
    return fingerprint(placeholders(json.dumps(kept, sort_keys=True), tree, root))


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


def parse_deps(listing: str, tree: pathlib.Path) -> dict:
    """object -> headers, from the text of `ninja -t deps`."""
    found, current = {}, None
    for line in listing.splitlines():
        if line and not line.startswith(" ") and ": #deps" in line:
            current = norm(tree / line.split(": #deps")[0])
            found[current] = set()
        elif line.startswith("    ") and current:
            found[current].add(norm(line.strip()))
    return found


def deps_problem(found: dict, tree: pathlib.Path):
    """Why these header dependencies cannot be used, or None.

    Every tree this reads has compiled something, so an empty record is never
    the truth: it is a record lost or being rewritten (M1-102, reproduced on
    2026-09-30 with three `ninja -t deps` at once on Windows)."""
    if not found:
        return (f"mutants-due: Ninja reported no header dependencies for {tree} -- the "
                "check would be checking nothing. Is the tree built, and is nothing else "
                "reading or building it?")
    return None


class Tree:
    def __init__(self, tree: pathlib.Path):
        self.tree = tree
        self.ninja = ninja_program(tree)
        self.headers = self._headers()
        tests = json.loads(run([ctest_program(tree), "--show-only=json-v1"], tree))["tests"]
        self.tests = {t["name"]: t for t in tests}
        self._inputs = {}
        self._fingerprints = {}
        self._tracked = None
        self._outputs = None
        self._hashes = None

    def _headers(self) -> dict:
        """object -> the headers the compiler reported for it, from Ninja's log."""
        found = parse_deps(run([self.ninja, "-t", "deps"], self.tree), self.tree)
        problem = deps_problem(found, self.tree)
        if problem:
            raise SystemExit(problem)
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
                        if inside and is_program(path, os.name):
                            files |= self.build_inputs(os.path.relpath(path, self.tree).replace("\\", "/"))
                            files |= self.runtime_inputs(path.stem)
        return files

    # --- fingerprints (M1-103, register decision 250) ---------------------------

    def fingerprints(self, spec: dict) -> dict:
        """judge -> fingerprint, for the judges of one mutant file: the same
        programs and CTest entries judge_inputs reads."""
        found = {}
        for mutant in spec["mutants"]:
            for name in list(mutant.get("suites", [])) + list(mutant.get("targets", [])):
                target = self.program_target(name)
                found[f"program {target}"] = self.program_fingerprint(target)
            for entry in mutant.get("ctest", []):
                test = self.tests.get(entry)
                if test is None:
                    found[f"ctest {entry}"] = "unknown"
                    continue
                found[f"ctest {entry}"] = test_fingerprint(test, self.tree, ROOT)
                for arg in test.get("command", []):
                    value = arg.split("=", 1)[1] if arg.startswith("-D") and "=" in arg else arg
                    path = pathlib.Path(value)
                    inside = path.is_file() and norm(path).startswith(norm(self.tree) + "/")
                    if inside and is_program(path, os.name):
                        target = os.path.relpath(path, self.tree).replace("\\", "/")
                        found[f"program {target}"] = self.program_fingerprint(target)
        return found

    def program_fingerprint(self, target: str) -> str:
        if target not in self._fingerprints:
            commands = run([self.ninja, "-t", "commands", target], self.tree)
            files = {path: self.content_hash(path) for path in self.build_inputs(target)
                     if path not in self.tracked() and path not in self.outputs()}
            self._fingerprints[target] = program_fingerprint(commands, files, self.tree, ROOT)
        return self._fingerprints[target]

    def tracked(self) -> set:
        """Files git tracks: their changes are judged by `git diff` instead."""
        if self._tracked is None:
            self._tracked = {norm(ROOT / n) for n in run(["git", "ls-files"], ROOT).splitlines() if n}
        return self._tracked

    def outputs(self) -> set:
        """Files a build step makes: their commands are in the fingerprint instead."""
        if self._outputs is None:
            self._outputs = set()
            for line in run([self.ninja, "-t", "targets", "all"], self.tree).splitlines():
                name = line.rsplit(": ", 1)[0].strip()
                self._outputs.add(norm(name) if os.path.isabs(name) else norm(self.tree / name))
        return self._outputs

    def content_hash(self, path: str) -> str:
        """A file's SHA-256, remembered by its size and date in the build tree:
        a file rewritten with the same size and the same date would be missed,
        which decision 250 accepts against hashing every file on every run."""
        if self._hashes is None:
            try:
                self._hashes = json.loads(self._hash_file().read_text(encoding="utf-8"))
            except (OSError, ValueError):
                self._hashes = {}
        try:
            info = os.stat(path)
        except OSError:
            return "missing"
        key = [info.st_size, info.st_mtime_ns]
        known = self._hashes.get(path)
        if known and known[:2] == key:
            return known[2]
        digest = hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
        self._hashes[path] = key + [digest]
        return digest

    def _hash_file(self) -> pathlib.Path:
        return self.tree / "mutants-due-hashes.json"

    def save_hashes(self) -> None:
        if self._hashes is not None:
            self._hash_file().write_text(json.dumps(self._hashes), encoding="utf-8")


def same_tree(recorded: str, tree: pathlib.Path) -> bool:
    return norm(ROOT / recorded) == norm(tree)


def same_tree_path(one: pathlib.Path, two: pathlib.Path) -> bool:
    return norm(one) == norm(two)


def judged_since(record: dict, now: dict) -> list:
    """The judges whose fingerprint differs from the one the pass recorded."""
    was = record["judges"]
    return [f"{key}: fingerprint changed" for key in sorted(set(was) | set(now)) if was.get(key) != now.get(key)]


def changed_since(commit: str) -> set:
    names = run(["git", "diff", "--name-only", commit], ROOT).splitlines()
    names += run(["git", "ls-files", "--others", "--exclude-standard"], ROOT).splitlines()
    return {norm(ROOT / n) for n in names if n}


def judging_tree(record, named: pathlib.Path, only_assumed: bool, exists) -> tuple:
    """(tree, why): the tree a mutant file is judged in, and what to say about it.

    The tree its pass was recorded in, whatever tree was named (M1-106, register
    decision 255): a file is judged in one tree, `m1-12` to `m1-17` in Debug and
    the rest in RelWithDebInfo, and only there can its fingerprints be compared.
    Asked about the other tree, the strict rule used to list it due for any
    build-definition change -- safe, but 19 files instead of 7 on 2026-10-01.
    The named tree where there is no record or the changes are made up
    (--only-assumed); None, so the file is due, where the recorded tree is not
    on this machine."""
    if record is None or only_assumed:
        return named, ""
    recorded = ROOT / record["tree"]
    if not exists(recorded):
        return None, f"recorded in {record['tree']}, which is not here"
    return recorded, ""


def main_check(tree: pathlib.Path, assumed: list, only_assumed: bool, expect_due: list) -> int:
    passes = json.loads(PASSES.read_text(encoding="utf-8")) if PASSES.exists() else {}
    readings = {}

    def reading(path: pathlib.Path) -> Tree:
        """One reading of each tree used, made when it is first needed."""
        if norm(path) not in readings:
            readings[norm(path)] = Tree(path.resolve())
        return readings[norm(path)]

    due_count = 0
    due_names = set()
    files = sorted((ROOT / "scripts" / "mutants").glob("*.json"))
    for spec_path in files:
        spec = json.loads(spec_path.read_text(encoding="utf-8"))
        record = passes.get(spec_path.name)
        changed = changed_since(record["commit"]) if record and not only_assumed else set()
        changed |= {norm(ROOT / a) for a in assumed}
        where, absent = judging_tree(record, tree, only_assumed, pathlib.Path.is_dir)
        if where is None:
            due_count += 1
            due_names.add(spec_path.name)
            print(f"DUE      {spec_path.name}: {absent}")
            continue
        t = reading(where)
        # Fingerprints decide a build-definition change where the pass recorded
        # them in this tree; anywhere else the strict rule stands (M1-103).
        judged = None
        if record and not only_assumed and "judges" in record and same_tree(record["tree"], where):
            judged = judged_since(record, t.fingerprints(spec))
        due, reasons = is_due(changed, t.judge_inputs(spec), record is not None or only_assumed, judged)
        root = norm(ROOT) + "/"
        shown = [r[len(root):] if r.startswith(root) else r for r in reasons[:3]]
        in_tree = "" if same_tree_path(where, tree) else f" [in {os.path.relpath(where, ROOT).replace(chr(92), '/')}]"
        if due:
            due_count += 1
            due_names.add(spec_path.name)
            more = f" and {len(reasons) - 3} more" if len(reasons) > 3 else ""
            print(f"DUE      {spec_path.name}{in_tree}: {', '.join(shown)}{more}")
        else:
            since = f"passed at {record['commit'][:12]}" if record else "no recorded pass, judged on the assumed changes only"
            print(f"current  {spec_path.name}{in_tree} ({since})")
    for t in readings.values():
        t.save_hashes()
    print(f"mutants-due: {due_count} of {len(files)} mutant files due")
    # --expect-due: how CTest checks the real computation against this tree.
    missing = [name for name in expect_due if name not in due_names]
    for name in missing:
        print(f"mutants-due: expected {name} to be due, and it is not")
    return 1 if missing else 0


def main_expect_unmet(tree: pathlib.Path, assumed: list, name: str) -> int:
    """--expect-unmet: the expectation must be able to fail. Expecting `name` to be
    due where it is not must make the check fail *and* say so, and this passes
    only when both happen. A CTest rule could check one or the other, never both:
    "expected to fail" also passed a script that refused the tree or crashed, and
    a message rule passed one that said so but exited 0 (register decision 251,
    measured in a scratch CTest project on 2026-09-30). A refusal still fails
    here, since it leaves by SystemExit."""
    said = io.StringIO()
    with contextlib.redirect_stdout(said):
        code = main_check(tree, assumed, True, [name])
    sys.stdout.write(said.getvalue())
    reported = f"mutants-due: expected {name} to be due, and it is not" in said.getvalue()
    if code == 1 and reported:
        print(f"mutants-due: expecting {name} failed the check and said so, as it must")
        return 0
    print(f"mutants-due: expecting {name} where it is not due must fail the check and say so; "
          f"it returned {code}, and {'said' if reported else 'did not say'} so")
    return 1


def main_fingerprints(tree: pathlib.Path, name: str) -> int:
    """--fingerprints: one mutant file's judges, fingerprinted twice from two
    fresh readings of the tree. A fingerprint that is not the same twice would
    make files due at random; one that names no judge would decide nothing."""
    spec = json.loads((ROOT / "scripts" / "mutants" / name).read_text(encoding="utf-8"))
    first, second = Tree(tree).fingerprints(spec), Tree(tree).fingerprints(spec)
    problems = [f"no judges fingerprinted for {name}"] if not first else []
    problems += [f"{key} cannot be fingerprinted" for key, value in first.items() if value == "unknown"]
    problems += [f"{key} differs between two readings" for key in first if first[key] != second.get(key)]
    for line in problems:
        print(f"mutants-due: {line}")
    if not problems:
        print(f"mutants-due: {len(first)} judges of {name}, the same fingerprints twice")
    return 1 if problems else 0


def is_program(path: pathlib.Path, system: str) -> bool:
    """Whether a file a test names is a program the build made.

    On Windows only an .exe is: Windows has no execute bit, so os.access with
    X_OK is true of any file that exists -- which made a golden image a test
    writes into the build tree look like a program, and Ninja then refused to
    name its inputs (found by M1-17, 2026-09-29, once the golden tests put
    clear-block.png there). Elsewhere the execute bit decides."""
    if system == "nt":
        return path.suffix.lower() == ".exe"
    return os.access(path, os.X_OK)


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
    # A build-definition change judged by what it changed (M1-103): through the
    # judges' fingerprints where they can be compared, as before where not.
    judged_cases = {
        "the build definition changed, no judge's fingerprint did": ({root + "cmakelists.txt"}, []),
        "the build definition changed, a judge's fingerprint did": ({root + "cmakelists.txt"}, ["ctest x"]),
        "a judge's fingerprint changed, and no file": (set(), ["program y"]),
        "the harness changed, no judge's fingerprint did": ({root + "scripts/mutate.py"}, []),
        "data changed, no judge's fingerprint did": ({root + "data/skyfield/x.txt"}, []),
    }
    judged_want = [False, True, True, True, True]
    for (name, (changed, judged)), want in zip(judged_cases.items(), judged_want):
        got, _ = is_due(changed, inputs, True, judged)
        status = "ok" if got == want else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name}: {'due' if got else 'current'} ({status})")
    # The fingerprints themselves. The same tree in two places, spelled with
    # either separator and, on Windows, either case, must agree; a changed flag,
    # a changed untracked file or a changed test property must not. The two
    # places are real absolute paths on whichever system runs this: the first
    # version used Windows paths, which are not absolute on Linux, and failed
    # both Linux trees (2026-10-01).
    scratch_root = pathlib.Path(tempfile.gettempdir()).resolve()
    one, two = scratch_root / "repo-one", scratch_root / "elsewhere" / "copy"
    commands = "clang++ -O2 -I{r}/src -c {r}\\src\\a.cpp -o {t}/CMakeFiles/a.obj\n"
    cmds_one = commands.format(r=str(one), t=str(one / "build" / "rel"))
    # Capitals name the same path only where the file system ignores case.
    two_spelled = str(two).upper() if os.name == "nt" else str(two)
    cmds_two = commands.format(r=two_spelled, t=str(two / "build" / "rel"))
    files_one = {norm(one / "build/_deps/x-src/x.hpp"): "h1", "c:/sdk/include/stdio.h": "h2"}
    files_two = {norm(two / "build/_deps/x-src/x.hpp"): "h1", "c:/sdk/include/stdio.h": "h2"}

    def prog(cmds, files, where):
        return program_fingerprint(cmds, files, where / "build" / "rel", where)

    test_one = {"name": "t", "backtrace": 3, "command": [str(one / "build/rel/t.exe")],
                "properties": [{"name": "WILL_FAIL", "value": True}]}
    test_two = dict(test_one, backtrace=9, command=[str(two / "build/rel/t.exe")])
    fp_cases = {
        "the same program in two places agrees": prog(cmds_one, files_one, one) == prog(cmds_two, files_two, two),
        "a changed flag changes a program's": prog(cmds_one, files_one, one) != prog(
            cmds_one.replace("-O2", "-O3"), files_one, one),
        "a changed untracked file changes a program's": prog(cmds_one, files_one, one) != prog(
            cmds_one, dict(files_one, **{"c:/sdk/include/stdio.h": "h3"}), one),
        "the same test in two places, declared on another line, agrees":
            test_fingerprint(test_one, one / "build/rel", one) == test_fingerprint(test_two, two / "build/rel", two),
        "a changed test property changes a test's": test_fingerprint(test_one, one / "build/rel", one) != test_fingerprint(
            dict(test_one, properties=[{"name": "WILL_FAIL", "value": False}]), one / "build/rel", one),
    }
    in_capitals = placeholders(str(one).upper() + "/x", one / "build" / "rel", one)
    fp_cases["a path in capitals is the same path on Windows, and another one elsewhere"] = (
        in_capitals == "<ROOT>/x" if os.name == "nt" else in_capitals == str(one).upper() + "/x")
    fp_cases["a changed, a new and a lost judge are all named, an unchanged one not"] = judged_since(
        {"judges": {"same": "1", "moved": "2", "lost": "3"}}, {"same": "1", "moved": "9", "new": "4"}) == [
        "lost: fingerprint changed", "moved: fingerprint changed", "new: fingerprint changed"]
    for name, got in fp_cases.items():
        status = "ok" if got else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name} ({status})")
    # Which tree a file is judged in (M1-106): the one its pass was recorded in,
    # whatever tree was named -- where else could its fingerprints be compared?
    # The named one where there is no record, or the changes are made up; and
    # none, so the file is due, where the recorded tree is not on this machine.
    named = pathlib.Path(tempfile.gettempdir()) / "build" / "relwithdebinfo"
    in_debug = {"commit": "c0ffee", "tree": "build/debug"}
    tree_cases = {
        "a file is judged in the tree its pass was recorded in":
            judging_tree(in_debug, named, False, lambda p: True)[0] == ROOT / "build" / "debug",
        "a file with no record is judged in the named tree":
            judging_tree(None, named, False, lambda p: True)[0] == named,
        "made-up changes are judged in the named tree":
            judging_tree(in_debug, named, True, lambda p: True)[0] == named,
        "a recorded tree this machine does not have makes the file due, and says so":
            judging_tree(in_debug, named, False, lambda p: False) == (None, "recorded in build/debug, which is not here"),
    }
    for name, got in tree_cases.items():
        status = "ok" if got else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name} ({status})")
    # Ninja's header record (M1-102). Read while it is being compacted, it can
    # come back empty with exit status 0, and every judge would then seem to
    # include no header -- fewer files due, and nothing said. So an empty
    # answer is refused, and a real one must parse.
    tree = pathlib.Path(tempfile.gettempdir()) / "tree"
    listing = ("CMakeFiles/t.dir/a.cpp.obj: #deps 2, deps mtime 1 (VALID)\n"
               "    src/a.cpp\n    src/core/Units.hpp\n\n")
    parsed = parse_deps(listing, tree)
    deps_cases = {
        "a header record parses": (parsed == {norm(tree / "CMakeFiles/t.dir/a.cpp.obj"):
                                              {norm("src/a.cpp"), norm("src/core/Units.hpp")}}, True),
        "a header record is accepted": (deps_problem(parsed, tree) is None, True),
        "an empty header record is refused": (deps_problem(parse_deps("", tree), tree) is not None, True),
    }
    for name, (got, want) in deps_cases.items():
        status = "ok" if got == want else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name} ({status})")
    # Which files a test names are programs: an image is not, on either system.
    with tempfile.TemporaryDirectory() as scratch:
        image = pathlib.Path(scratch) / "clear-block.png"
        image.write_bytes(b"not a program")
        program = pathlib.Path(scratch) / "orbsim.exe"
        program.write_bytes(b"a program")
        program_cases = {
            "an image, on Windows": (image, "nt", False),
            "an .exe, on Windows": (program, "nt", True),
        }
        # The execute bit can only be asked about where there is one.
        if os.name != "nt":
            image.chmod(0o644)
            program_cases["an image without the execute bit, elsewhere"] = (image, "posix", False)
        for name, (path, system, want) in program_cases.items():
            got = is_program(path, system)
            status = "ok" if got == want else "WRONG"
            failures += status == "WRONG"
            print(f"self-test: {name}: {'a program' if got else 'not a program'} ({status})")
    return 1 if failures else 0


def main() -> int:
    args = sys.argv[1:]
    if args == ["--self-test"]:
        return self_test()
    if not args:
        print(__doc__)
        return 2
    tree = pathlib.Path(args[0]).resolve()
    if len(args) == 3 and args[1] == "--fingerprints":
        return main_fingerprints(tree, args[2])
    assumed = [args[i + 1] for i, a in enumerate(args) if a == "--assume-changed" and i + 1 < len(args)]
    # --only-assumed: ignore git and the records, and judge only the files named
    # with --assume-changed -- how the rule's precision is checked.
    expect_due = [args[i + 1] for i, a in enumerate(args) if a == "--expect-due" and i + 1 < len(args)]
    unmet = [args[i + 1] for i, a in enumerate(args) if a == "--expect-unmet" and i + 1 < len(args)]
    if len(unmet) == 1 and not expect_due:
        return main_expect_unmet(tree, assumed, unmet[0])
    return main_check(tree, assumed, "--only-assumed" in args, expect_due)


if __name__ == "__main__":
    sys.exit(main())

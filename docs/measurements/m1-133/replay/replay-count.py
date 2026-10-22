#!/usr/bin/env python3
"""The counts of the replay of M1-133 (register decisions 729, 730 and 731).

This is a measurement script. It is not project code.

    replay-count.py count <label> [--previous <label>] [--full]
        Run this at one commit, after the build and BEFORE the driver runs.
        <label> of --previous is the commit before, for the digests.
    replay-count.py after <label> --log <driver log> [--plant-remove <test>]
        Run this after the driver ran at the same commit.
    replay-count.py digests <label>
        Only the digests of the functions that the stored tests entered, from
        the store as it is now (for the base, after its full run).
    replay-count.py table <label>...
        The table of the commits, in sequence, from what `count` and `after`
        wrote, with the totals and the percentages (decision 731).

`count` loads the worktree's own scripts/run-tests.py as a module, as
scripts/mutate.py loads scripts/mutants-due.py. Then it makes the calls of
run_tree() up to and including `chosen = selection(...)`, in the same sequence
and with the same inputs: Run, load_store, card, forced, learn_all_imports,
the environment, parts, link_commands(changed_programs(...)) and selection.
Only one thing is different: after load_store, the folder of the run's own
files is a folder of this script (`<label>/state/`). Thus the clear probe of
the card, the summaries of the objects and the kept tables of this script do
not go into the driver's run-tests/, and the driver's own counts stay as they
are. The summaries and the tables are a cache that holds the SHA-256 of each
object, thus the folder does not change a result.

`count` also calculates the selection by program, with the rule of ADR 0027
(decision 748): a program that the test takes function by function counts as
unchanged only when its bytes are unchanged (program_unchanged with a
function part that is always None). That is "program". It also writes the
call selection(..., programs_unchanged=None) as "program_none", which is not
the selection by program. A test that only the selection by program chooses
has the cause "by program only" (decision 751).

For each test in a selection, `count` writes its causes (decision 730), and
the parts that differ between the stored entry and the parts now. For the
causes 1 to 3 it uses the digests of the functions that the stored tests
entered: `after` keeps them for each commit, and `count` compares the digests
now with those of the commit before. Each changed function has its name, its
name in plain text (llvm-undname), the objects that hold it and their source
files, so that it can be sorted by hand against the diff of the commit.

`after` compares the function-level selection of `count` with the driver's
run-tests/selected.txt. A difference makes it exit with 1. --plant-remove
removes one name from the computed set first, to show that the comparison
can fail.
"""

import sys

sys.dont_write_bytecode = True  # no __pycache__ beside the driver

import argparse  # noqa: E402
import gzip  # noqa: E402
import hashlib  # noqa: E402
import importlib.util  # noqa: E402
import json  # noqa: E402
import os  # noqa: E402
import pathlib  # noqa: E402
import re  # noqa: E402
import shutil  # noqa: E402
import subprocess  # noqa: E402
import time  # noqa: E402
import xml.etree.ElementTree as ElementTree  # noqa: E402

WORKTREE = pathlib.Path(r"C:\Users\U439644\Projects\untitled\build\wt133")
TREE = WORKTREE / "build" / "debug"
CTEST = r"C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\ctest.exe"
OUT = pathlib.Path(r"C:\Users\U439644\AppData\Local\Temp\claude\C--Users-U439644-Projects-untitled"
                   r"\4fe89f80-6ba9-4110-b2ad-13efa52050eb\scratchpad\replay")
# CTest's report of the full run at the base: the time of each test (decision 731 (4)).
BASE_RESULTS = OUT / "base-results.xml"
UNDNAME = r"C:\GitHub\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin\llvm-undname.exe"
GPU = "gpu"
# When this file is there, `after` stops the chain when its commit is complete,
# and `count` counts nothing.
STOP_FILE = OUT / "stop-after-this-commit"


def load_driver():
    """The worktree's scripts/run-tests.py, loaded as scripts/mutate.py loads
    scripts/mutants-due.py."""
    loader = importlib.util.spec_from_file_location("run_tests", WORKTREE / "scripts" / "run-tests.py")
    module = importlib.util.module_from_spec(loader)
    loader.loader.exec_module(module)
    return module


def sha(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8", "surrogateescape")).hexdigest()


def git(*args) -> str:
    return subprocess.run(["git", "-C", str(WORKTREE), *args], capture_output=True, text=True,
                          encoding="utf-8", errors="replace", check=True).stdout


def write_json(path: pathlib.Path, data) -> None:
    path.write_bytes(json.dumps(data, sort_keys=True, indent=1).encode("utf-8"))


def write_gzip(path: pathlib.Path, data) -> None:
    path.write_bytes(gzip.compress(json.dumps(data, sort_keys=True).encode("utf-8")))


def read_gzip(path: pathlib.Path):
    return json.loads(gzip.decompress(path.read_bytes()).decode("utf-8"))


def unescaped(text: str) -> str:
    return re.sub(r"\\(.)", r"\1", text)


def is_cpp_test(rt, name: str, test: dict) -> bool:
    """A test that Catch2 discovers: its command is a program of the tree with
    the test's name as its one argument (the brief, step 3)."""
    command = test.get("command") or []
    if len(command) != 2:
        return False
    program = pathlib.Path(command[0])
    return (rt.under(rt.norm(program), rt.norm(TREE)) and rt.is_program(program)
            and unescaped(command[1]) == unescaped(name))


def base_times() -> dict:
    found = {}
    for case in ElementTree.parse(BASE_RESULTS).getroot().iter("testcase"):
        found[case.get("name")] = float(case.get("time") or 0.0)
    return found


def plain_names(names) -> dict:
    """{decorated name: plain name} through llvm-undname, for the hand sort."""
    names = sorted(set(names))
    if not names:
        return {}
    done = subprocess.run([UNDNAME], input="\n".join(names) + "\n", capture_output=True, text=True,
                          encoding="utf-8", errors="replace", check=False).stdout
    blocks = [b for b in done.split("\n\n") if b.strip()]
    found = {}
    for block in blocks:
        lines = block.splitlines()
        if len(lines) >= 2 and lines[0] in names:
            found[lines[0]] = lines[1]
    return {name: found.get(name, name) for name in names}


# --- the sources of the objects ------------------------------------------------

def object_sources() -> dict:
    """{object key: source file relative to the worktree}, from the tree's
    compile_commands.json. A member of a library gets the key that
    compiled-code.py's object_key gives it: `<library>(<member>)`."""
    commands = json.loads((TREE / "compile_commands.json").read_text(encoding="utf-8"))
    found = {}
    tree = str(TREE).replace("\\", "/").lower() + "/"
    root = str(WORKTREE).replace("\\", "/").lower() + "/"
    for entry in commands:
        output = entry.get("output", "").replace("\\", "/")
        source = entry.get("file", "").replace("\\", "/")
        if not output.lower().startswith(tree):
            continue
        relative = output[len(tree):]
        source = source[len(root):] if source.lower().startswith(root) else source
        found[relative] = source
        match = re.match(r"CMakeFiles/([^/]+)\.dir/.*/([^/]+)$", relative) or re.match(
            r"CMakeFiles/([^/]+)\.dir/([^/]+)$", relative)
        if match:
            target, member = match.group(1), match.group(2)
            for library in (f"{target}_d.lib", f"{target}.lib"):
                found.setdefault(f"{library}({member})", source)
    return found


def source_of(key: str, sources: dict) -> str:
    if key in sources:
        return sources[key]
    match = re.match(r"(.*)\(([^()]*)\)$", key)
    if match:
        return sources.get(f"{pathlib.PurePosixPath(match.group(1)).name}({match.group(2)})", "")
    return ""


# --- the digests of the functions ---------------------------------------------------

def digest_table(rt, run, stored: dict) -> dict:
    """{program: its data} for each program that a stored test takes function by
    function: the digest of each function that a stored test entered and of
    each function without a flag (Program.digests, through Run.code_of), the
    objects that hold each one, the functions without a flag, the digest of
    the linker directives, the binary part and the link command."""
    keys_of = {}
    for entry in stored.values():
        for key, program in (entry.get("programs") or {}).items():
            keys_of.setdefault(key, set()).update(program["keys"])
    links = run.link_commands([pathlib.Path(key) for key in keys_of])
    table = {}
    for key, keys in sorted(keys_of.items()):
        path = pathlib.Path(key)
        item = {"bytes": run.current_bytes(key), "hooked": False}
        table[key] = item
        if item["bytes"] is None or not run.hooked(path):
            continue
        item["hooked"] = True
        code = run.code_of(path)
        program = code._program
        if program is None:
            item["problems"] = code.problems
            continue
        unflagged = program.unflagged()
        wanted = sorted(set(keys) | set(unflagged))
        digests = program.digests(wanted)
        objects = sorted({index.key for name in wanted for index, _ in program._held(name)})
        place = {name: number for number, name in enumerate(objects)}
        item["objects"] = objects
        item["functions"] = {name: [digests.get(name, "missing"),
                                    sorted({place[index.key] for index, _ in program._held(name)})]
                             for name in wanted}
        item["unflagged"] = unflagged
        item["directives"] = program.directives()
        item["binary"] = sha(json.dumps(code._binary, sort_keys=True))
        item["link"] = links.get(rt.norm(path))
        item["problems"] = code.problems
    return table


def changed_functions(before: dict, now: dict) -> dict:
    """{program: {"functions": {name: [old, new]}, "unflagged": [added, removed],
    "directives", "binary", "link", "bytes"}} between two digest tables."""
    found = {}
    for key, item in now.items():
        old = before.get(key)
        if old is None:
            found[key] = {"new program in the table": True}
            continue
        change = {}
        if old.get("bytes") != item.get("bytes"):
            change["bytes"] = True
        if old.get("hooked") != item.get("hooked"):
            change["hooked"] = [old.get("hooked"), item.get("hooked")]
        functions = {}
        old_functions, new_functions = old.get("functions") or {}, item.get("functions") or {}
        for name in sorted(set(old_functions) & set(new_functions)):
            if old_functions[name][0] != new_functions[name][0]:
                functions[name] = [old_functions[name][0], new_functions[name][0]]
        if functions:
            change["functions"] = functions
        added = sorted(set(item.get("unflagged") or []) - set(old.get("unflagged") or []))
        removed = sorted(set(old.get("unflagged") or []) - set(item.get("unflagged") or []))
        if added or removed:
            change["unflagged"] = [added, removed]
        for part in ("directives", "binary", "link"):
            if old.get(part) != item.get(part):
                change[part] = True
        if change:
            found[key] = change
    return found


def objects_of(item: dict, name: str) -> list:
    entry = (item.get("functions") or {}).get(name)
    if entry is None:
        return []
    return [item["objects"][number] for number in entry[1]]


# --- the causes (decision 730) ---------------------------------------------------

def own_source(program_key: str) -> str:
    """The test's own file: tests/<name of the program>.cpp."""
    return f"tests/{pathlib.PurePosixPath(program_key).stem}.cpp"


def function_causes(name: str, function: str, program_key: str, now: dict, before: dict, sources: dict,
                    diff: set) -> list:
    """The cause (1) or (2) of one changed function that the test entered, with
    the facts for the hand sort."""
    objects = objects_of(now.get(program_key) or {}, function) or objects_of(before.get(program_key) or {}, function)
    files = sorted({source_of(o, sources) for o in objects})
    if own_source(program_key) in files:
        cause = "2"
    elif any(f.startswith("src/") for f in files):
        cause = "1"
    else:
        cause = "1/2: another file of tests/" if any(f.startswith("tests/") for f in files) else "1/2: no source found"
    return [cause, {"function": function, "objects": objects, "sources": files,
                    "source in the diff": sorted(f for f in files if f in diff)}]


def test_causes(rt, run, name: str, entry, parts: dict, forced: set, own: set, change: dict, now: dict,
                before: dict, sources: dict, diff: set) -> dict:
    """{cause: [details]} of one selected test."""
    causes = {}

    def add(cause, detail=None):
        causes.setdefault(cause, [])
        if detail is not None:
            causes[cause].append(detail)

    if name in forced:
        add("forced", "the card could not be read, or --full")
    if name not in own:
        add("4", "a test that it is related to ran (DEPENDS)")
        return causes
    if entry is None:
        add("7", "not stored at the commit before")
        return causes
    differing = sorted(k for k in set(entry["parts"]) | set(parts) if entry["parts"].get(k) != parts.get(k))
    for part in differing:
        if part == "scripts":
            add("5", "scripts")
        elif part == "cautious":
            add("6", "cautious")
        else:
            add("8", part)
    reads = [path for stamp, path in entry.get("reads") or [] if run.world.file(path) != stamp]
    if reads:
        add("8", "reads: " + ", ".join(sorted(reads)[:5]) + (" ..." if len(reads) > 5 else ""))
    for key, program in sorted((entry.get("programs") or {}).items()):
        bytes_now = run.current_bytes(key)
        if bytes_now is None:
            add("8", f"{key}: the program is not there")
            continue
        if bytes_now == program.get("bytes"):
            continue
        add("note", f"{key}: its bytes changed")
        part = run.function_part(key, program.get("keys") or [])
        if part is None:
            add("8", f"{key}: no function part (a problem)")
            continue
        if part == program.get("part"):
            continue
        # The function part changed: why.
        changes = change.get(key) or {}
        functions = changes.get("functions") or {}
        entered = [f for f in program.get("keys") or [] if f in functions]
        unflagged = [f for f in (now.get(key) or {}).get("unflagged") or [] if f in functions]
        for function in entered:
            add(*function_causes(name, function, key, now, before, sources, diff))
        for function in unflagged:
            cause, detail = function_causes(name, function, key, now, before, sources, diff)
            add(cause, {**detail, "without a flag": True})
        moved = changes.get("unflagged")
        if moved:
            add("1/2: the functions without a flag changed", {"added": moved[0], "removed": moved[1]})
        if not entered and not unflagged and not moved:
            if changes.get("directives"):
                add("3", f"{key}: the linker directives")
            if changes.get("link"):
                add("8", f"{key}: the link command")
            if changes.get("binary"):
                add("8", f"{key}: a library (the binary part)")
            if not (changes.get("directives") or changes.get("link") or changes.get("binary")):
                add("unexplained", f"{key}: the function part changed, and no digest, directive, link or "
                                   "binary part")
        elif changes.get("directives"):
            add("note", f"{key}: the linker directives changed also")
    if not any(cause != "note" for cause in causes):
        add("unexplained", "selected, but no part, read or function part changed")
    return causes


def program_only_causes(run, name: str, entry, own_program: set) -> dict:
    """The cause of a test that only the selection by program chooses (decision
    751): "by program only", with the programs whose bytes changed, or with
    DEPENDS when a test that it is related to runs by program only."""
    if name not in own_program:
        return {"by program only": ["a test that it is related to runs by program only (DEPENDS)"]}
    changed = [key for key, program in sorted((entry.get("programs") or {}).items())
               if run.current_bytes(key) != program.get("bytes")]
    return {"by program only": [f"{key}: its bytes changed" for key in changed]}


# --- count ---------------------------------------------------------------------------

def count(label: str, full: bool) -> int:
    if STOP_FILE.is_file():
        print(f"replay-count: {label}: STOP: {STOP_FILE} is there; nothing is counted")
        return 3
    started = time.time()
    folder = OUT / label
    folder.mkdir(parents=True, exist_ok=True)
    rt = load_driver()
    root = rt.ROOT
    # run_tree(), up to and including `chosen = selection(...)`.
    run = rt.Run(TREE, CTEST, root)
    if not run.tests:
        print("replay-count: CTest lists no tests")
        return 1
    store = rt.load_store(run.state)
    stored = store["tests"]
    # The run's own files go to this script's folder from here (see the docstring).
    run.state = folder / "state"
    run.state.mkdir(parents=True, exist_ok=True)
    if any(rt.GPU_LABEL in rt.labels(t) for t in run.tests.values()):
        run.card()
    forced = set()
    if full:
        forced = set(run.tests)
    elif run._card is None:
        forced = {n for n, t in run.tests.items() if rt.GPU_LABEL in rt.labels(t)}
    run.learn_all_imports()
    records = TREE / rt.COVERAGE_RECORDS
    env = {**os.environ, rt.RECORDS_VARIABLE: str(records)}
    env = rt.without_test_name(env)
    parts = {name: run.parts(test, (stored.get(name) or {}).get("reads"), env,
                             (stored.get(name) or {}).get("shaders"),
                             set((stored.get(name) or {}).get("programs") or ()))
             for name, test in run.tests.items()}
    run.link_commands(run.changed_programs({n: e for n, e in stored.items() if n in run.tests and n not in forced}))
    chosen = rt.selection(run.tests, stored, parts, run.world.file, forced, run.programs_unchanged)
    decided = time.time()

    # The selection by program, with the rule of ADR 0027 (decision 748), and
    # the call with None, which is not the selection by program.
    def bytes_unchanged(programs: dict) -> bool:
        return all(rt.program_unchanged(entry, run.current_bytes(key), lambda keys: None)
                   for key, entry in sorted(programs.items()))

    rules = {"function": run.programs_unchanged, "program": bytes_unchanged, "program_none": None}
    selections = {"function": chosen,
                  "program": rt.selection(run.tests, stored, parts, run.world.file, forced, bytes_unchanged),
                  "program_none": rt.selection(run.tests, stored, parts, run.world.file, forced, None)}
    own = {rule: {n for n in run.tests
                  if n in forced or not rt.unchanged(stored.get(n), parts[n], run.world.file, rules[rule])}
           for rule in rules}

    # The digests now, against those of the commit before.
    now = digest_table(rt, run, stored)
    write_gzip(folder / "digests-before.json.gz", now)
    previous = json.loads((folder / "previous.txt").read_text(encoding="utf-8")).get("label") if (
        folder / "previous.txt").is_file() else None
    before = read_gzip(OUT / previous / "digests-after.json.gz") if previous else {}
    change = changed_functions(before, now) if previous else {}
    sources = object_sources()
    diff = set(git("diff", "--name-only", "HEAD~1", "HEAD").split()) if label != "base" else set()
    names = sorted({f for c in change.values() for f in (c.get("functions") or {})})
    plain = plain_names(names)
    changed = {key: {**{k: v for k, v in c.items() if k != "functions"},
                     "functions": [{"function": f, "plain": plain.get(f, f),
                                    "objects": objects_of(now.get(key) or {}, f) or objects_of(before.get(key) or {}, f),
                                    "sources": sorted({source_of(o, sources) for o in (
                                        objects_of(now.get(key) or {}, f) or objects_of(before.get(key) or {}, f))})}
                                   for f in sorted(c.get("functions") or {})]}
               for key, c in change.items()}

    times = base_times() if BASE_RESULTS.is_file() else {}
    gpu = {n for n, t in run.tests.items() if GPU in rt.labels(t)}
    cpp = {n for n, t in run.tests.items() if is_cpp_test(rt, n, t)}
    result = {"label": label, "commit": git("rev-parse", "HEAD").strip(),
              "subject": git("log", "-1", "--format=%s").strip(), "full": full,
              "tests": len(run.tests), "cpp_tests": len(cpp), "gpu_tests": len(gpu),
              "card": run._card is not None, "forced": sorted(forced),
              "diff": sorted(diff), "changed programs": changed, "seconds to decide": round(decided - started, 1),
              # The selection by function must be a part of the selection by program.
              "by function, not by program": sorted(chosen - selections["program"])}
    for rule, chosen_set in selections.items():
        causes = {}
        for name in sorted(chosen_set):
            # A test of both selections has the causes of the selection by function.
            if rule == "program" and name in chosen:
                causes[name] = result["function"]["causes"][name]
                continue
            if rule == "program":
                causes[name] = program_only_causes(run, name, stored.get(name) or {}, own["program"])
                continue
            causes[name] = test_causes(rt, run, name, stored.get(name), parts[name], forced, own[rule], change, now,
                                       before, sources, diff)
        missing = sorted(n for n in chosen_set if n not in times)
        result[rule] = {
            "selected": sorted(chosen_set), "all": len(chosen_set), "cpp": len(chosen_set & cpp),
            "gpu": len(chosen_set & gpu), "seconds": round(sum(times.get(n, 0.0) for n in chosen_set), 2),
            "without a base time": missing, "causes": causes}
    write_json(folder / "count.json", result)
    (folder / "count-started.txt").write_text(str(started), encoding="utf-8")
    for rule in selections:
        r = result[rule]
        print(f"replay-count: {label}: by {rule}: {r['all']} tests, {r['cpp']} C++, {r['gpu']} GPU, "
              f"{r['seconds']} s from the base's times, {len(r['without a base time'])} without a base time")
    print(f"replay-count: {label}: {len(changed)} programs with a change in the digest table")
    return 0


# --- after ---------------------------------------------------------------------------

def compared(computed: set, selected: set) -> list:
    """The difference between the script's function-level selection and the
    driver's selected.txt, as lines; empty when they agree."""
    lines = [f"selected by the script, not by the driver: {n}" for n in sorted(computed - selected)]
    lines += [f"selected by the driver, not by the script: {n}" for n in sorted(selected - computed)]
    return lines


def after(label: str, log, plant_remove) -> int:
    folder = OUT / label
    result = json.loads((folder / "count.json").read_text(encoding="utf-8"))
    computed = set(result["function"]["selected"])
    if plant_remove is not None:
        if plant_remove not in computed:
            print(f"replay-count: the planted name {plant_remove!r} is not in the computed set")
            return 2
        computed.discard(plant_remove)
        print(f"replay-count: PLANTED: {plant_remove!r} removed from the computed set")
    state = TREE / "run-tests"
    selected = {line for line in (state / "selected.txt").read_text(encoding="utf-8").splitlines() if line}
    lines = compared(computed, selected)
    for line in lines:
        print(f"replay-count: DIFFERENCE: {line}")
    if plant_remove is not None:
        write_json(folder / "planted-compare.json", {"planted": plant_remove, "difference": lines})
        print(f"replay-count: {len(lines)} difference(s) with the planted name")
        return 1 if lines else 0
    write_json(folder / "compare.json", {"difference": lines, "computed": len(computed), "selected": len(selected)})
    started = float((folder / "count-started.txt").read_text(encoding="utf-8"))
    for name in ("selected.txt", "skipped.txt"):
        shutil.copyfile(state / name, folder / f"driver-{name}")
    report = state / "results.xml"
    failed, outcomes = [], {}
    if selected and report.is_file() and report.stat().st_mtime > started:
        shutil.copyfile(report, folder / "driver-results.xml")
        rt = load_driver()
        outcomes = rt.outcomes(report.read_text(encoding="utf-8"))
        failed = sorted(n for n, o in outcomes.items() if o == "failed")
    summary = ""
    if log:
        text = pathlib.Path(log).read_text(encoding="utf-8", errors="replace")
        summary = next((line for line in text.splitlines() if re.match(r"run-tests: \d+ ran, ", line)), "")
        problems = [line for line in text.splitlines()
                    if line.startswith("run-tests: ") and line != summary]
    else:
        problems = []
    write_json(folder / "after.json", {"failed": failed, "skipped by itself": sorted(
        n for n, o in outcomes.items() if o == "skipped"), "ran": len(outcomes), "summary": summary,
        "problem lines": problems, "difference": lines})
    print(f"replay-count: {label}: {len(lines)} difference(s); failed: {failed}; summary: {summary}")
    for line in problems:
        print(f"replay-count: driver line: {line}")
    if lines:
        return 1
    code = digests(label)
    # A problem line of the driver stops the replay, also with no failed test (decision 753).
    if problems:
        print(f"replay-count: {label}: STOP: {len(problems)} problem line(s) of the driver")
        return 1
    # A stop that the helper asks for, after this commit is complete.
    if STOP_FILE.is_file():
        print(f"replay-count: {label}: STOP: {STOP_FILE} is there; the commit is complete")
        return 3
    return code


def first_times(labels: list) -> dict:
    """{test: (seconds, label)}: the time of each test at its first run in the
    replay, from the driver's report at each commit (decision 750)."""
    found = {}
    for label in labels:
        report = OUT / label / "driver-results.xml"
        if not report.is_file():
            continue
        for case in ElementTree.parse(report).getroot().iter("testcase"):
            if case.get("status") == "run" and case.get("name") not in found:
                found[case.get("name")] = (float(case.get("time") or 0.0), label)
    return found


SELF_SKIP = "a write the disk refuses only when the file is closed is reported"
PROBE_SHADERS = "mutants_due_probe_shaders"
SPIKE = (840, 4646)
SPIKE_SECONDS = (1997, 6158)


def cause_counts(causes: dict) -> dict:
    found = {}
    for test_causes_of_one in causes.values():
        for cause in test_causes_of_one:
            if cause != "note":
                found[cause] = found.get(cause, 0) + 1
    return found


def table(labels: list) -> int:
    """The table of the commits, the totals and the percentages (decision 731)."""
    base = base_times()
    first = first_times(labels)
    rows, totals, late_times = [], {}, {}
    for label in labels:
        result = json.loads((OUT / label / "count.json").read_text(encoding="utf-8"))
        done = json.loads((OUT / label / "after.json").read_text(encoding="utf-8"))
        patch = (OUT / label / "patch-id.txt").read_text(encoding="utf-8").strip() if (
            OUT / label / "patch-id.txt").is_file() else ""
        row = {"label": label, "copy": result["commit"][:7], "subject": result["subject"],
               "listed": result["tests"], "listed C++": result["cpp_tests"], "listed GPU": result["gpu_tests"],
               "patch-id": patch, "failed": done["failed"], "summary": done["summary"],
               "problem lines": done["problem lines"], "difference": done["difference"],
               "by function, not by program": result["by function, not by program"]}
        for rule in ("function", "program", "program_none"):
            r = result[rule]
            seconds = r["seconds"]
            for name in r["without a base time"]:
                if name in first:
                    seconds += first[name][0]
                    late_times[name] = first[name]
                else:
                    late_times.setdefault(name, (None, None))
            row[rule] = {"all": r["all"], "cpp": r["cpp"], "gpu": r["gpu"], "seconds": round(seconds, 2),
                         "causes": cause_counts(r["causes"]),
                         "self-skip": SELF_SKIP in r["selected"], "probe shaders": PROBE_SHADERS in r["selected"]}
        row["cause names"] = {cause: sorted(n for n, c in result["program"]["causes"].items() if cause in c)
                              for cause in row["program"]["causes"]}
        row["cause 8 parts"] = {}
        for name, causes in result["program"]["causes"].items():
            for detail in causes.get("8", []):
                part = detail.split(":")[0] if not str(detail).startswith("c:/") else detail.split(": ", 1)[1]
                row["cause 8 parts"].setdefault(part, []).append(name)
        rows.append(row)
        for rule in ("function", "program", "program_none"):
            for key in ("all", "cpp", "gpu", "seconds"):
                totals[f"{rule} {key}"] = totals.get(f"{rule} {key}", 0) + row[rule][key]
        for key in ("listed", "listed C++", "listed GPU"):
            totals[key] = totals.get(key, 0) + row[key]
    listed = totals["listed"]
    figures = {
        "(1) C++ by function / C++ by program": [totals["function cpp"], totals["program cpp"]],
        "spike (543)": list(SPIKE),
        "(2) all by program / all listed": [totals["program all"], listed],
        "(3) all by program, GPU counted as run at each commit / all listed": [
            totals["program all"] - totals["program gpu"] + totals["listed GPU"], listed],
        "all by function / all listed": [totals["function all"], listed],
        "(4) seconds by function / seconds by program": [round(totals["function seconds"], 2),
                                                          round(totals["program seconds"], 2)],
        "spike seconds (543)": list(SPIKE_SECONDS)}
    percent = {k: round(100.0 * a / b, 1) if b else None for k, (a, b) in figures.items()}
    write_json(OUT / "table.json", {"rows": rows, "totals": totals, "figures": figures, "percent": percent,
                                    "times of first runs (750)": late_times})
    for row in rows:
        f, p = row["function"], row["program"]
        print(f"{row['label']} -> {row['copy']} {row['subject'][:60]}")
        print(f"   listed {row['listed']} ({row['listed C++']} C++, {row['listed GPU']} GPU); "
              f"by function {f['all']}/{f['cpp']}/{f['gpu']} {f['seconds']} s; "
              f"by program {p['all']}/{p['cpp']}/{p['gpu']} {p['seconds']} s; None-call {row['program_none']['all']}")
        print(f"   causes (program): {dict(sorted(p['causes'].items()))}")
        print(f"   self-skip test selected: {p['self-skip']}; {PROBE_SHADERS} selected: {p['probe shaders']}; "
              f"failed: {row['failed']}")
        print(f"   {row['summary']}")
    print("totals:", totals)
    for key, value in figures.items():
        print(f"{key}: {value} = {percent[key]} %")
    print("times of first runs:", late_times)
    return 0


def digests(label: str) -> int:
    """The digests of the functions that the stored tests entered, from the
    store after the driver ran: the commit after compares with these."""
    folder = OUT / label
    folder.mkdir(parents=True, exist_ok=True)
    rt = load_driver()
    run = rt.Run(TREE, CTEST, rt.ROOT)
    store = rt.load_store(run.state)
    run.state = folder / "state"
    run.state.mkdir(parents=True, exist_ok=True)
    table = digest_table(rt, run, store["tests"])
    write_gzip(folder / "digests-after.json.gz", table)
    print(f"replay-count: {label}: digests of {len(table)} programs, "
          f"{sum(len(t.get('functions') or {}) for t in table.values())} functions")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="what", required=True)
    one = sub.add_parser("count")
    one.add_argument("label")
    one.add_argument("--full", action="store_true")
    one.add_argument("--previous")
    two = sub.add_parser("after")
    two.add_argument("label")
    two.add_argument("--log")
    two.add_argument("--plant-remove")
    three = sub.add_parser("digests")
    three.add_argument("label")
    four = sub.add_parser("table")
    four.add_argument("labels", nargs="+")
    arguments = parser.parse_args()
    if arguments.what == "table":
        return table(arguments.labels)
    if arguments.what == "count":
        if arguments.previous:
            (OUT / arguments.label).mkdir(parents=True, exist_ok=True)
            (OUT / arguments.label / "previous.txt").write_bytes(
                json.dumps({"label": arguments.previous}).encode("utf-8"))
        return count(arguments.label, arguments.full)
    if arguments.what == "after":
        return after(arguments.label, arguments.log, arguments.plant_remove)
    return digests(arguments.label)


if __name__ == "__main__":
    sys.exit(main())

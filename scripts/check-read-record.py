#!/usr/bin/env python3
"""The self-test of the read record of the Python processes (M1-138).

    check-read-record.py --self-test <cmake>

Why this exists: `check` skips a script test only when each file that its
Python processes read is as it was (register decisions 765 and 781). The
records come from scripts/read-record/sitecustomize.py, which Python loads at
its start when the driver puts that folder on PYTHONPATH. A record that
misses one type of access is a skipped test that could fail, and a skipped
test looks the same as a passing one. So this starts a Python process with
the hook, in a scratch tree, which makes each type of access one time, and
makes sure that the line of each is in the records (decisions 770, 782, 783
and 785):

  * a file opened to read, through open(), pathlib and os.open; a file only
    written is not in the record, and a file that is not in the tree is not
    in it;
  * each question about a file through os.stat, os.lstat, os.path.exists,
    lexists, isfile and isdir, and the same functions of pathlib, also for a
    file that is not there;
  * each folder listed, through os.listdir, os.scandir, os.walk, glob and
    pathlib;
  * the source of a module loaded from its cached .pyc file, and the hook's
    own file;
  * each process started, and the record of each child Python process: one
    with a test name of its own, one with none, one that ends with os._exit,
    and two with a fault of the hook on a second thread, through the audit
    hook and through a replaced function; the record of each of the last
    three must have no last line `end` (decisions 782 and 791);
  * a child that names records of its own, as a driver in a test does:
    its record goes into its own folder and into the folder of the outer test,
    each with the files of its own tree only (decision 783);
  * the file name of a record, from the test name as tests/TestReads.cpp and
    scripts/run-tests.py escape it (decision 528);
  * each replaced function of os in each set of os that holds the function
    before the replacement, so that shutil gives the same answers (decision
    780);
  * the driver, scripts/run-tests.py, which reads these records (decision
    795): it reads each record with the last line `end` as complete and each
    other record as not complete, and for each `read` line and each `list`
    line it makes the same line for that path at this time.

Proven able to fail before it was trusted: the self-test then removes each
type of line from the records, one at a time, and makes sure that the check
fails for each; and it changes the hash of one `list` line, and makes sure
that the driver then finds a difference.
"""

import importlib.util
import json
import os
import pathlib
import py_compile
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
HOOK_FOLDER = ROOT / "scripts" / "read-record"
HOOK = HOOK_FOLDER / "sitecustomize.py"
RECORDS_VARIABLE = "ORBSIM_READ_RECORDS"
OWNER_VARIABLE = "ORBSIM_READ_RECORD_OWNER"
TEST_NAME_VARIABLE = "ORBSIM_TEST_NAME"
# A test name with characters that the escape rule of decision 528 changes.
OUTER_TEST = "self test: a/b?"
INNER_TEST = "inner test"
HEAD = "python-read-record 1"

PROBE = r'''
import glob, importlib.util, json, os, pathlib, subprocess, sys
tree, outside, inner, cmake = (pathlib.Path(a) for a in sys.argv[1:5])
open(tree / "read-open.txt", encoding="utf-8").read()
(tree / "read-pathlib.txt").read_bytes()
os.close(os.open(tree / "read-os-open.txt", os.O_RDONLY))
(tree / "written.txt").write_text("x\n", encoding="utf-8")
open(outside / "outside.txt", encoding="utf-8").read()
os.stat(tree / "stat.txt")
os.lstat(tree / "lstat.txt")
os.path.exists(tree / "exists.txt")
os.path.exists(tree / "missing.txt")
os.path.lexists(tree / "lexists.txt")
os.path.isfile(tree / "isfile.txt")
os.path.isdir(tree / "isdir")
(tree / "pathlib-exists.txt").exists()
(tree / "pathlib-is-file.txt").is_file()
(tree / "pathlib-is-dir").is_dir()
(tree / "pathlib-stat.txt").stat()
os.listdir(tree / "list-listdir")
list(os.scandir(tree / "list-scandir"))
list(os.walk(tree / "list-walk"))
glob.glob(str(tree / "list-glob" / "*.txt"))
list((tree / "list-iterdir").iterdir())
list((tree / "list-rglob").rglob("*.txt"))
spec = importlib.util.spec_from_file_location("helper_mod", tree / "helper-mod.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def child(code, env=None):
    subprocess.run([sys.executable, "-c", code], check=False, env=env)

child("open(" + repr(str(tree / "child.txt")) + ").read()")
env = dict(os.environ, ORBSIM_TEST_NAME="a different test")
child("open(" + repr(str(tree / "different.txt")) + ").read()", env)
env = {k: v for k, v in os.environ.items() if k.upper() != "ORBSIM_TEST_NAME"}
child("open(" + repr(str(tree / "no-name.txt")) + ").read()", env)
child("import os; open(" + repr(str(tree / "dies.txt")) + ").read(); os._exit(0)")

def thread_fault(name, call):
    # A fault of the hook on a second thread (decision 791): the hook raises an
    # exception for one file, and the main thread, which writes the record at
    # exit, must see it. `call` goes through the audit hook, or through one of
    # the replaced functions.
    child("import os, sitecustomize, threading\n"
          "real = sitecustomize._text\n"
          "def failing(path):\n"
          "    if " + repr(name) + " in str(path):\n"
          "        raise RuntimeError('a planted fault of the hook')\n"
          "    return real(path)\n"
          "sitecustomize._text = failing\n"
          "thread = threading.Thread(target=lambda: " + call.format(repr(str(tree / name))) + ")\n"
          "thread.start()\n"
          "thread.join()\n"
          "sitecustomize._text = real\n")

thread_fault("thread-open.txt", "open({}).close()")
thread_fault("thread-stat.txt", "os.stat({})")
env = dict(os.environ, ORBSIM_READ_RECORDS=str(inner / "test-reads"), ORBSIM_TEST_NAME="inner test")
child("open(" + repr(str(tree / "nested.txt")) + ").read(); open(" + repr(str(inner / "inner.txt")) + ").read()", env)
subprocess.run([str(cmake), "-E", "echo", "x"], cwd=outside, check=True, capture_output=True)
sets = {}
for name in ("stat", "lstat"):
    replacement = getattr(os, name)
    original = getattr(replacement, "__wrapped__", None)
    for set_name in ("supports_fd", "supports_follow_symlinks", "supports_dir_fd"):
        members = getattr(os, set_name)
        sets[name + " " + set_name] = [original is not None, (replacement in members) == (original in members)]
print(json.dumps(sets))
'''


_DRIVER = []


def driver():
    """scripts/run-tests.py, loaded one time: the check uses its escape of a
    test name and its split of a command line, so that it cannot agree with a
    copy that has the same fault."""
    if not _DRIVER:
        spec = importlib.util.spec_from_file_location("run_tests", ROOT / "scripts" / "run-tests.py")
        module = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True
        spec.loader.exec_module(module)
        _DRIVER.append(module)
    return _DRIVER[0]


def escaped_name(name: str) -> str:
    """The escape of decision 528."""
    return driver().escaped_name(name)


def words(argv) -> list:
    """The parts of a command: on Windows, Python gives the audit event of
    subprocess.Popen all of the command line as one string."""
    return driver().split_command_line(argv) if isinstance(argv, str) else [str(a) for a in argv]


def write(path: pathlib.Path, text: str = "x\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode("utf-8"))


def make_tree(scratch: pathlib.Path) -> tuple:
    """The scratch tree, a folder that is not in it, and the tree of the inner test."""
    tree, outside, inner = scratch / "tree", scratch / "outside", scratch / "inner"
    for name in ("read-open.txt", "read-pathlib.txt", "read-os-open.txt", "stat.txt", "lstat.txt", "exists.txt",
                 "lexists.txt", "isfile.txt", "pathlib-exists.txt", "pathlib-is-file.txt", "pathlib-stat.txt",
                 "child.txt", "different.txt", "no-name.txt", "dies.txt", "nested.txt", "thread-open.txt",
                 "thread-stat.txt"):
        write(tree / name)
    for folder in ("isdir", "pathlib-is-dir", "list-listdir", "list-scandir", "list-glob", "list-iterdir"):
        write(tree / folder / "a.txt")
    write(tree / "list-walk" / "sub" / "a.txt")
    write(tree / "list-rglob" / "sub" / "a.txt")
    write(tree / "helper-mod.py", "VALUE = 1\n")
    py_compile.compile(str(tree / "helper-mod.py"), doraise=True)
    write(outside / "outside.txt")
    write(inner / "inner.txt")
    (tree / "test-reads").mkdir()
    (inner / "test-reads").mkdir()
    return tree, outside, inner


def run_probe(scratch: pathlib.Path, cmake: str) -> tuple:
    """Runs the probe with the hook; returns the scratch folders and what the
    probe printed about the sets of os."""
    tree, outside, inner = make_tree(scratch)
    probe = scratch / "probe.py"
    write(probe, PROBE)
    env = {k: v for k, v in os.environ.items()
           if k.upper() not in (OWNER_VARIABLE, RECORDS_VARIABLE, TEST_NAME_VARIABLE, "PYTHONPATH")}
    env.update({"PYTHONPATH": str(HOOK_FOLDER), RECORDS_VARIABLE: str(tree / "test-reads"),
                TEST_NAME_VARIABLE: OUTER_TEST})
    done = subprocess.run([sys.executable, str(probe), str(tree), str(outside), str(inner), cmake], env=env,
                          capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
    lines = [line for line in done.stdout.splitlines() if line.startswith("{")]
    sets = json.loads(lines[-1]) if lines else None
    return tree, outside, inner, sets, done


def records(folder: pathlib.Path, test: str) -> list:
    """[(file name, lines)] of each record of `test` in `folder`."""
    prefix = escaped_name(test) + ".py-"
    return [(p.name, p.read_text(encoding="utf-8").split("\n")[:-1])
            for p in sorted(folder.glob("*.txt")) if p.name.startswith(prefix)]


def argv_of(lines: list) -> list:
    for line in lines:
        if line.startswith("argv "):
            return json.loads(line[len("argv "):])
    return []


def paths(lines: list, kind: str) -> dict:
    """{path in small letters: hash} of each line of a type: `read` or `list`."""
    found = {}
    for line in lines:
        if line.startswith(kind + " "):
            stamp, _, path = line[len(kind) + 1:].partition(" ")
            found[os.path.normcase(path)] = stamp
    return found


def problems_of(tree, outside, inner, sets, outer, inner_records, plant=None) -> list:
    """Each difference between the records and what the probe did."""
    def drop(lines):
        return [l for l in lines if not (plant and (l.startswith(plant + " ") or l == plant))]
    outer = [(name, drop(lines)) for name, lines in outer]
    problems = []
    top = [lines for _, lines in outer if any(a.endswith("probe.py") for a in argv_of(lines))]
    if len(top) != 1:
        return [f"{len(top)} records of the probe process itself, not one"]
    top = top[0]
    if not top or top[0] != HEAD:
        problems.append("the record of the probe does not start with its head line")
    if not top or top[-1] != "end":
        problems.append("the record of the probe has no last line `end`")
    read, listed = paths(top, "read"), paths(top, "list")
    t = lambda name: os.path.normcase(str(tree / name))  # noqa: E731
    for name in ("read-open.txt", "read-pathlib.txt", "read-os-open.txt", "stat.txt", "lstat.txt", "exists.txt",
                 "lexists.txt", "isfile.txt", "pathlib-exists.txt", "pathlib-is-file.txt", "pathlib-stat.txt",
                 "helper-mod.py"):
        if read.get(t(name), "missing") == "missing":
            problems.append(f"no `read` line with a hash for {name}")
    if read.get(t("missing.txt")) != "missing":
        problems.append("no `read missing` line for the file that is not there")
    if os.path.normcase(str(HOOK)) not in read:
        problems.append("no `read` line for the hook's own file")
    for name in ("written.txt",):
        if t(name) in read:
            problems.append(f"a `read` line for {name}, which the probe only wrote")
    if any(os.path.normcase(str(outside)) in p for p in read):
        problems.append("a `read` line for a file that is not in the tree or the repository")
    for folder in ("isdir", "pathlib-is-dir", "list-listdir", "list-scandir", "list-walk", "list-glob",
                   "list-iterdir", "list-rglob"):
        if listed.get(t(folder), "missing") == "missing":
            problems.append(f"no `list` line with a hash for the folder {folder}")
    for folder in ("list-walk\\sub" if os.name == "nt" else "list-walk/sub",):
        if t(folder) not in listed:
            problems.append("no `list` line for a folder that os.walk went into")
    children = [words(json.loads(l[len("child "):]).get("argv", [])) for l in top if l.startswith("child ")]
    if not any(c and c[0].lower().endswith(("cmake", "cmake.exe")) for c in children):
        problems.append("no `child` line for the program that the probe started")
    if len([c for c in children if "-c" in c]) != 7:
        problems.append("not one `child` line for each of the seven child Python processes")
    others = {name: lines for name, lines in outer if lines is not top}
    for name, wanted, how in (("child.txt", "end", ""), ("different.txt", "end", ""), ("no-name.txt", "end", ""),
                              ("nested.txt", "end", ""), ("dies.txt", None, "ends with os._exit"),
                              ("thread-open.txt", None, "has a fault of the audit hook on a second thread"),
                              ("thread-stat.txt", None, "has a fault of a replaced function on a second thread")):
        found = [lines for lines in others.values() if any(a.endswith(name + "').read()") or name + "')" in a
                                                           for a in argv_of(lines))]
        if len(found) != 1:
            problems.append(f"{len(found)} records of the outer test for the child that reads {name}, not one")
            continue
        lines = found[0]
        if wanted == "end" and (not lines or lines[-1] != "end"):
            problems.append(f"the record of the child that reads {name} has no last line `end`")
        if wanted is None and "end" in lines:
            problems.append(f"the record of the child that reads {name} and {how} has a line `end`")
        if wanted is None and (not lines or lines[0] != HEAD):
            problems.append(f"the child that reads {name} and {how} left no record from its start")
        if wanted == "end" and t(name) not in paths(lines, "read"):
            problems.append(f"the record of the child that reads {name} has no `read` line for it")
        if name == "nested.txt" and any(os.path.normcase(str(inner)) in p for p in paths(lines, "read")):
            problems.append("the outer record of the nested child holds a file of the inner tree")
    nested = [lines for _, lines in inner_records if any("nested.txt" in a for a in argv_of(lines))]
    if len(nested) != 1:
        problems.append(f"{len(nested)} records of the inner test for the nested child, not one")
    else:
        inner_read = paths(nested[0], "read")
        if os.path.normcase(str(inner / "inner.txt")) not in inner_read:
            problems.append("the inner record of the nested child has no `read` line for its own file")
        if t("nested.txt") in inner_read:
            problems.append("the inner record of the nested child holds a file of the outer tree")
    if sets is None:
        problems.append("the probe did not report the sets of os")
    else:
        for key, (replaced, same) in sorted(sets.items()):
            if not replaced:
                problems.append(f"os.{key.split()[0]} is not replaced")
            elif not same:
                problems.append(f"the replacement of os.{key.split()[0]} is not in os.{key.split()[1]} as the "
                                "function before it")
    return problems


def driver_problems(tree: pathlib.Path, found: list) -> list:
    """Each difference between the driver and the hook (decision 795), for the
    records `found`, [(file name, lines)]: a record that the driver reads as
    complete without the last line `end`, or as not complete with it; and a
    `read` line or a `list` line for which the driver makes a different line
    for that path at this time."""
    run_tests = driver()
    world = run_tests.World(tree, ROOT)
    problems = []
    for name, lines in found:
        parsed = run_tests.parse_python_record("\n".join(lines) + "\n")
        complete = bool(lines) and lines[-1] == "end"
        if (parsed is not None) != complete:
            problems.append(f"the driver reads the record {name} as {'complete' if parsed else 'not complete'}")
            continue
        for kind, key in (("read", "reads"), ("list", "listings")):
            for stamp, path in (parsed or {}).get(key, []):
                made = world.hook_line(kind, path)
                if made != (kind, stamp):
                    problems.append(f"the driver makes {made} for the line `{kind} {stamp} {path}` of {name}")
    return problems


def changed_listing(found: list) -> list:
    """The records `found` with a different hash in the first `list` line of a
    record with the last line `end`: a planted fault, which the driver must
    find. Only in such a record: the driver does not examine the lines of a
    record that is not complete, and the records come in the sequence of
    their file names, which hold the numbers of their processes."""
    changed, done = [], False
    for name, lines in found:
        new = []
        for line in lines:
            if not done and lines and lines[-1] == "end" and line.startswith("list "):
                kind, _, path = line.split(" ", 2)
                line, done = f"{kind} {'0' * 64} {path}", True
            new.append(line)
        changed.append((name, new))
    return changed


def self_test(cmake: str) -> int:
    with tempfile.TemporaryDirectory() as scratch_name:
        scratch = pathlib.Path(scratch_name)
        tree, outside, inner, sets, done = run_probe(scratch, cmake)
        outer = records(tree / "test-reads", OUTER_TEST)
        inner_records = records(inner / "test-reads", INNER_TEST)
        problems = problems_of(tree, outside, inner, sets, outer, inner_records)
        problems += driver_problems(tree, outer + inner_records)
        if done.returncode != 0:
            problems.insert(0, f"the probe exited with {done.returncode}: {done.stderr.strip()[-400:]}")
        for problem in problems:
            print(f"read-record FAILED: {problem}")
        planted = 0
        for kind in ("read", "list", "child", "end", HEAD.split()[0]):
            if not problems_of(tree, outside, inner, sets, outer, inner_records, plant=kind):
                print(f"read-record FAILED: the check passed with each `{kind}` line removed")
                problems.append(kind)
            else:
                planted += 1
        if not driver_problems(tree, changed_listing(outer + inner_records)):
            print("read-record FAILED: the driver found no difference with one `list` line changed")
            problems.append("driver")
        else:
            planted += 1
    print(f"read-record self-test: {len(problems)} problems; {planted} of 6 planted faults found; "
          f"{len(outer)} records of the outer test, {len(inner_records)} of the inner test")
    return 1 if problems else 0


def main() -> int:
    if len(sys.argv) == 3 and sys.argv[1] == "--self-test":
        return self_test(sys.argv[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())

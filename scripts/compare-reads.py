#!/usr/bin/env python3
"""Compare what the tests record with a recording of Process Monitor.

Process Monitor records each operation on a file by each process. This script
examines, for each test of the store of the driver, that the fingerprint of the
test holds each path in the repository, the build tree included, that a process
of the test read, examined or listed. M1-137 item 3, register decisions 612,
823, 830, 831, 834, 848, 849 and 892-898. It runs at each phase gate that stays,
in each of the two `check` trees.

    python scripts/compare-reads.py <tree> <store copy> <out json>
        <csv> <records copy> [<csv> <records copy> ...] [--plant]
    python scripts/compare-reads.py --short-run <tree> <file of test names> <out folder>
    python scripts/compare-reads.py --self-test

The steps of a gate, for each tree, from the root of the repository (decision
897). The assistant gives the commands of Process Monitor, which decision 499
leaves for the owner to install, and the owner accepts each prompt of Windows:

  1. Start: Procmon64.exe /AcceptEula /Quiet /Minimized /BackingFile <pml file>
     /LoadConfig docs/measurements/m1-138/m1-138.pmc
  2. Run: cmake --build <tree> --target check-full
  3. Stop, when the events in Process Monitor are later than the end of the
     run, which can be minutes after it (decision 903):
     Procmon64.exe /Terminate
  4. Save: Procmon64.exe /OpenLog <pml file> /SaveAs <csv file>
  5. Copy <tree>/run-tests/fingerprints.json and the folder <tree>/test-reads.
  6. Compare, with --plant. The script writes <out json> and the file
     <out json>.not-compared.txt, with each test that it could not compare.
  7. If that file names tests: steps 1 to 5 again, with --short-run as the run
     of step 2. Then step 6 again with the two pairs of a CSV file and a copy
     of the records, the full recording first (decision 896).

Each miss goes to the owner before a change (decision 849). The list of folders
outside the repository in <out json> goes into the record of the gate, and a
later gate compares its list with that record, by hand (decision 895).

Each pair of a CSV file and a copy of the folder test-reads/ is one recording,
the recording of the full run first. A test is compared in the first recording
that includes it: the newest of its read records in that copy was written
between the first and the last event of the recording (decision 831). A test
with no record file is included by a process of it in the recording. The store
copy is the store after the full run. It gives the parts of each test, and, for
the recording of the full run, its read lines (decision 823). For each other
recording, the read lines come from the records of the test in that copy, read
with the functions of the driver (decisions 830 and 831). A test with no
complete record is compared with its other parts (decision 892).

What a process of a test did, for each path in the repository:
  read     CreateFile that asks for the contents of a file (Generic Read or
           Read Data)
  query    a CreateFile with no such access, QueryOpen, or a query of the
           information of a file, with a result that answers the question
  listing  QueryDirectory, with a pattern at the end of the path removed
A folder that a process opened is not a read, and a read of a file that is not
there is a query. A process belongs to a test by its command line, as the
driver compares a command line, else by ORBSIM_READ_RECORD_OWNER or
ORBSIM_TEST_NAME in its environment, else by its parent.

A path is known (decision 823) when it is:
  1. in a `read` or `list` line of the test;
  2. in a part of the test (part_files);
  3. a file in the build tree that a process of the same recording wrote.
The files in test-reads/ and coverage-records/ are not inputs. A query of a file
in a folder that a `list` line names is counted apart, because the hash of the
names in the folder holds the answer. A path in a group of ruled_group is not
an input: the script counts the group and does not call it a miss (decision
893). Each other path is a miss.

The limits. The script sees only what the recorded run did. It holds its own
copy of what each part of the driver holds, and it stops when the store has a
part that it does not know, but it cannot see a changed part (decision 894).
For a test with the part `build tree`, it takes each path of the tree as known,
thus it does not see a file of the tree that somebody changes by hand (decision
901); the full run before a push does.

The exit code is 1 for a miss, for a test that the script did not compare, and
for a planted fault that it did not find, because a comparison that examines
nothing looks the same as one that finds nothing (VERIFICATION.md rule 23).

--plant (decision 823): the script compares the tests of the full recording a
second time, with one `read` line removed from a copy of the store: the first
line, by the order of the tests and the paths, of a path that a process of the
test read and that only that line makes known. That path must be the only new
miss.
"""

import collections
import contextlib
import copy
import csv
import datetime
import importlib.util
import inspect
import io
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

# Process Monitor can write a very long Environment field in a Process Start.
csv.field_size_limit(1 << 30)
ROOT = pathlib.Path(__file__).resolve().parent.parent
FOLDER = re.compile(r"(?<!Non-)Directory File")
QUERY_OPS = {"QueryOpen", "QueryBasicInformationFile", "QueryStandardInformationFile",
             "QueryNetworkOpenInformationFile", "QueryAttributeTagFile", "QueryAllInformationFile"}
# The results that answer the question about a path.
FOUND = ("SUCCESS", "NAME NOT FOUND", "PATH NOT FOUND", "NO SUCH FILE", "BUFFER OVERFLOW")
READ = ("Generic Read", "Read Data")
WRITE = ("Generic Write", "Write Data", "Append Data")
# CMake writes a file with a temporary name and renames it, and the settings of
# the recording record no rename (measured 2026-10-29: CMakeCache.txt.tmp9f9bd
# and CMakeSystem.cmake.tmp, written with OverwriteIf, then opened with Delete).
TEMPORARY = re.compile(r"(.+)\.tmp[0-9a-f]*")
# The parts of the driver with no file of the repository in them, and the parts
# that part_files knows (decision 894).
NO_FILES = frozenset(("definition", "environment", "machine", "card"))
WITH_FILES = frozenset(("programs", "libraries", "files", "scripts", "store", "cautious", "build tree", "children",
                        "shaders"))
# The groups that are not inputs (decisions 834, 864 and 899-902), each path by
# its rule in ruled_group.
TREE_NOT_THERE = "not there, in the build tree"
TREE_FOLDER = "a folder of the build tree"
ROOT_FOLDER = "a folder of the repository"
RESOURCES = "build/SystemResources"
HASHED_NAME = "a name in a folder that a part hashes"
WRITTEN_AGAIN = "a listing of a folder that the test writes again"
ROOT_LISTING = "a listing of the root or of build/"
OWN_PDB = "the .pdb file of a program of the test"
KNOWN_SOURCE = "a .pyc file of a known .py file"
BUILT = "a path of the build tree, with the part build tree"
GIT_FILES = "a file of .git, with the part build tree"
NINJA_QUESTION = "a question of CMake about build.ninja"
SYSTEM_RESOURCES = pathlib.Path("build") / "SystemResources"
# A compiled file of Python, as `<folder>/__pycache__/<name>.<tag>.pyc`.
COMPILED = re.compile(r"(.*)/__pycache__/([^/.]+)\.[^/]*\.pyc")
# The folders of the build tree that these tests remove and write again in the
# same run, thus the listing of each is not an input (decision 834).
WRITTEN_FOLDERS = {
    "configure_guard": ("configure_guard/edited", "configure_guard/edited/build/CMakeFiles",
                        "configure_guard/edited/build/CMakeFiles/pkgRedirects", "configure_guard/edited/src",
                        "configure_guard/steady", "configure_guard/steady/build/CMakeFiles/pkgRedirects",
                        "configure_guard/steady/src"),
    "orbsim_bench_smoke": ("bench-smoke",),
    "probe_clear_determinism": ("probes", "probes-rerun"),
}
# The folders that these tests list, from the root of the repository: through
# REALPATH, and by cmd.exe (decisions 834 and 902).
LISTED_FOLDERS = {"probe_clear_determinism": (".", "build"), "reproducible_build": ("build",)}
# CMake asks for the attributes of build.ninja in its working folder, also for
# a scratch project, in these tests (decision 902).
ASK_FOR_NINJA = ("configure_guard", "mutate_self_test")
# The name in the list of decision 895 of the temporary folder, where tests make
# folders with new names at each run.
TEMPORARY_FOLDER = "<temporary folder>"


def load_driver():
    """scripts/run-tests.py as a module; its name has a `-`, thus no import."""
    spec = importlib.util.spec_from_file_location("run_tests", ROOT / "scripts" / "run-tests.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["run_tests"] = module
    spec.loader.exec_module(module)
    return module


DRIVER = load_driver()
norm, under = DRIVER.norm, DRIVER.under


def clock(text: str) -> datetime.time:
    """The time of day of an event, to the second: '8:59:01.1714987 AM', or,
    with a different setting of Windows, '08:59:01,1714987'."""
    time_of_day, _, half = text.partition(" ")
    hours, minutes, seconds = time_of_day.split(":")
    hour = int(hours) % 12 + (12 if half == "PM" else 0) if half else int(hours)
    return datetime.time(hour, int(minutes), int(seconds[:2]))


def command_key(arguments: list) -> tuple:
    """A command in the form in which the driver compares two commands
    (same_command, decision 704): the program as Windows compares a path, and
    each other argument with the same text."""
    return (DRIVER.windows_path(arguments[0]), *arguments[1:]) if arguments else ()


def detail_field(detail: str, key: str) -> str:
    match = re.search(re.escape(key) + r": (.*?)(?:, (?:Command line|Current directory|Environment|Parent PID):|$)",
                      detail, re.S)
    return match.group(1).strip() if match else ""


class Process:
    def __init__(self, parent, command, env, image):
        self.parent, self.command, self.env, self.image = parent, command, env, image
        self.owner = None
        self.parent_process = None
        self.reads, self.queries, self.listings, self.outside = set(), set(), set(), set()


def owner_in(env: str, names: set, records: str):
    """The test of a process from its environment: the outermost test of
    ORBSIM_READ_RECORD_OWNER whose folder is the test-reads/ of the tree, else
    ORBSIM_TEST_NAME. A value ends at a line end or at `; `, the separator of
    Process Monitor."""
    match = re.search(r"(?:^|[\s;])ORBSIM_READ_RECORD_OWNER=(.*?)(?:;\s|[\r\n]|$)", env)
    if match:
        try:
            for folder, test in json.loads(match.group(1).strip()):
                if norm(folder) == records and test in names:
                    return test
        except (ValueError, TypeError):
            pass
    match = re.search(r"(?:^|[\s;])ORBSIM_TEST_NAME=(.*?)(?:;\s|[\r\n]|$)", env)
    return match.group(1).strip() if match and match.group(1).strip() in names else None


def state_of(key: str, known: dict):
    """"folder", "file", or None for a path that is not there at this time;
    each path examined one time."""
    if key not in known:
        known[key] = "folder" if os.path.isdir(key) else "file" if os.path.exists(key) else None
    return known[key]


def assign_owners(every: list, run) -> None:
    """Give each process its test (decision 823): by its command line, else by
    its environment, else by its parent."""
    names = set(run.tests)
    records = norm(run.tree / DRIVER.RECORDS)
    commands = {command_key(t["command"]): n for n, t in run.tests.items() if t.get("command")}
    for process in every:
        process.owner = (commands.get(command_key(DRIVER.split_command_line(process.command)))
                         or owner_in(process.env, names, records))
    for process in every:
        walk, steps = process, 0
        while walk.owner is None and walk.parent_process is not None and steps < 64:
            walk, steps = walk.parent_process, steps + 1
        process.owner = process.owner or walk.owner
        process.queries -= process.listings
        process.reads -= process.listings


def read_recording(path: pathlib.Path, run) -> tuple:
    """(processes, the files written, first time, last time) of one CSV file."""
    root_key = norm(run.root)
    current, every, writes, known = {}, [], set(), {}
    first = last = None
    with open(path, encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            first, last = first or row["Time of Day"], row["Time of Day"]
            pid, operation, detail = row["PID"], row["Operation"], row["Detail"]
            if operation == "Process Start":
                env = detail.split("Environment:", 1)[1] if "Environment:" in detail else ""
                process = Process(detail_field(detail, "Parent PID"), detail_field(detail, "Command line"), env,
                                  row["Process Name"].lower())
                # The parent is the process that holds the number now, not a
                # later process that Windows gives the same number.
                process.parent_process = current.get(process.parent)
                current[pid] = process
                every.append(process)
                continue
            process = current.get(pid)
            if process is None or not row["Path"]:
                continue
            key, result = norm(row["Path"]), row["Result"]
            access = detail.split("Desired Access: ", 1)[-1].split(", Disposition", 1)[0]
            wants_contents = operation == "CreateFile" and any(word in access for word in READ)
            if not under(key, root_key):
                # Not a path of a device, which begins with a backslash.
                if (wants_contents and result == "SUCCESS" and not FOLDER.search(detail)
                        and os.path.isabs(row["Path"]) and not row["Path"].startswith("\\")
                        and state_of(key, known) != "folder"):
                    process.outside.add(key.rpartition("/")[0])
                continue
            if operation == "CreateFile":
                if result == "SUCCESS" and any(word in access for word in WRITE):
                    writes.add(key)
                    renamed = TEMPORARY.fullmatch(key)
                    if renamed:
                        writes.add(renamed.group(1))
                # A folder is not read, also when it is opened with no
                # "Directory File" option; a file that is not there is only
                # a question about it.
                if FOLDER.search(detail) or state_of(key, known) == "folder":
                    if result in FOUND:
                        process.queries.add(key)
                elif wants_contents and result == "SUCCESS":
                    process.reads.add(key)
                elif result in FOUND:
                    process.queries.add(key)
            elif operation in QUERY_OPS and result in FOUND:
                process.queries.add(key)
            elif operation == "QueryDirectory":
                head, _, tail = key.rpartition("/")
                process.listings.add(head if head and ("*" in tail or "?" in tail) else key)
    if first is None:
        sys.exit(f"{path} holds no event")
    assign_owners(every, run)
    return every, writes, clock(first), clock(last)


def included(folder: pathlib.Path, names: list, by_test: dict, moment: tuple) -> set:
    """The tests that this recording includes (decision 831): the newest read
    record of the test in `folder` was written between the first and the last
    event, `moment` being (day, first, last); or the test has no record file,
    and a process of it is in the recording. A record is
    `<escaped name>.py-<process>-<start>.txt`, `<escaped name>.trace.json` or
    `<escaped name>.txt`."""
    day, first, last = moment
    by_escape = {DRIVER.escaped_name(name): name for name in names}
    newest = {}
    for path in folder.iterdir():
        match = DRIVER.PYTHON_RECORD.fullmatch(path.name)
        escaped = match.group(1) if match else (path.name[:-len(DRIVER.TRACE)] if path.name.endswith(DRIVER.TRACE)
                                                else path.name[:-len(".txt")] if path.name.endswith(".txt") else None)
        if escaped in by_escape:
            name = by_escape[escaped]
            newest[name] = max(newest.get(name, 0), path.stat().st_mtime)
    low = datetime.datetime.combine(day, first).timestamp()
    high = datetime.datetime.combine(day, last).timestamp() + 1
    return ({name for name, mtime in newest.items() if low <= mtime <= high}
            | {name for name in names if name not in newest and by_test.get(name)})


def lines_from_records(run, name: str, folder: pathlib.Path) -> tuple:
    """(reads, listings) of a test from its records in `folder`, as the driver
    reads them, or two empty lists where they are not complete (decision 892)."""
    test = run.tests[name]
    command = test.get("command") or []
    escaped = DRIVER.escaped_name(name)
    if DRIVER.trace_test(command, run.cmake):
        try:
            text = (folder / (escaped + DRIVER.TRACE)).read_bytes().decode("utf-8", "replace")
        except OSError:
            return [], []
        named = {norm(p) for p in run.arguments(test)} | {norm(command[0])}
        cwd = DRIVER.properties(test).get("WORKING_DIRECTORY") or str(run.tree)
        paths = DRIVER.trace_inputs(text, command, named, cwd, str(run.tree))
        return ([], []) if paths is None else (paths[0], paths[1])
    files = DRIVER.python_record_files(folder, [name]).get(name)
    if files:
        hook = str(run.root.joinpath(*DRIVER.HOOK_FOLDER, DRIVER.HOOK_FILE))
        texts = [p.read_bytes().decode("utf-8", errors="surrogateescape") for p in files]
        parsed = DRIVER.python_reads(texts, command, hook)
        children = None if parsed is None else DRIVER.child_rules(parsed[2], str(run.tree), str(run.root))
        if children is None:
            return [], []
        return [p for _, p in parsed[0]] + [p for child in children for p in child["paths"]], [p for _, p in parsed[1]]
    try:
        parsed = DRIVER.parse_record((folder / (escaped + ".txt")).read_text(encoding="utf-8"))
    except OSError:
        parsed = None
    return [p for _, p in parsed or []], []


def stored_lines(entry: dict) -> tuple:
    return [p for _, p in entry.get("reads") or []], [p for _, p in entry.get("listings") or []]


def program_files(run, path) -> set:
    return {norm(path)} | {norm(p) for p in run.world.libraries(pathlib.Path(path))}


def ctest_files(run) -> set:
    """The files that Run.ctest_files hashes, by the same walk."""
    found = [p for p in run.tree.rglob("*") if p.name in ("CTestTestfile.cmake", "DartConfiguration.tcl")
             or p.name.endswith(("_include.cmake", "_tests.cmake", "_test-list.cmake"))]
    return {norm(p) for p in found + DRIVER.tree_programs(run.tree)}


def part_files(run, name: str, entry: dict, shared: dict) -> set:
    """Each file that a part of the test holds, by the rules of Run.parts
    (decisions 823 and 894). The script stops for a part that it does not know,
    which is a part that it cannot examine."""
    test, parts = run.tests[name], set(entry.get("parts") or {})
    unknown = parts - NO_FILES - WITH_FILES
    if unknown:
        sys.exit(f"the store has a part that this script does not know, {sorted(unknown)}: add it to part_files "
                 "(decision 894)")
    tree, root = norm(run.tree), norm(run.root)
    recorded = entry.get("listings") is not None and not entry.get("trace")
    found = set()
    for path in run.arguments(test):
        key = norm(path)
        if key == tree:
            continue
        if under(key, tree):
            if path.is_file() and DRIVER.is_program(path):
                found |= program_files(run, path)
        elif under(key, root):
            if not path.is_dir():
                found.add(key)
            elif not recorded:
                found |= {norm(p) for p in run.world.unignored_under(path)}
        elif path.is_file():
            found |= program_files(run, path) if DRIVER.is_program(path) else {key}
    if "cautious" in parts:
        found |= shared.setdefault("cautious", {norm(p) for f in DRIVER.CAUTIOUS
                                                for p in run.world.everything_under(run.root / f)})
    if "scripts" in parts:
        found |= shared.setdefault("scripts", {norm(p) for f in DRIVER.SCRIPT_INPUTS
                                               for p in run.world.unignored_under(run.root / f)})
    if "build tree" in parts:
        found |= shared.setdefault("tree", {norm(run.tree / f) for f in DRIVER.TREE_FILES} | set(run.world.listed()))
    if "store" in parts:
        found.add(norm(run.state / DRIVER.STORE))
    if "libraries" in parts:
        for program in entry.get("programs") or {}:
            found |= program_files(run, program)
    for child in entry.get("children") or []:
        found |= program_files(run, child["program"])
        if child["kind"] == "ctest":
            found |= shared.setdefault("ctest", ctest_files(run))
    shaders = entry.get("shaders")
    if isinstance(shaders, list):
        found |= {norm(p) for p in shaders}
    elif "shaders" in parts:
        found |= {norm(p) for p in run.world.everything_under(run.tree / "shaders")}
    return found


def source_of(key: str):
    """The .py file of a .pyc file in __pycache__, or None for each other path."""
    compiled = COMPILED.fullmatch(key)
    return f"{compiled.group(1)}/{compiled.group(2)}.py" if compiled else None


def hashed_folders(run, parts) -> list:
    """The folders of the repository whose files a part of the test holds, by
    name: a new file in one of them moves the part (decisions 834 and 900). The
    part `build tree` holds each file that git does not ignore."""
    folders = [run.root] if "build tree" in parts else []
    if "scripts" in parts:
        folders += [run.root / folder for folder in DRIVER.SCRIPT_INPUTS]
    if "cautious" in parts:
        folders += [run.root / folder for folder in DRIVER.CAUTIOUS]
    return [norm(folder) for folder in folders]


def ruled_group(run, name: str, entry: dict, kind: str, key: str, known: set, shared: dict):
    """The group that holds a path that is not known, or None for a miss
    (decisions 834, 864, 893 and 899-902). `known` holds the lines of the test
    and the files of its parts. A program of the test that reads its own .pdb
    file: its bytes hold the identifier of that file, or decision 864 rules the
    test by name. The two limits of decision 834 are in the task document: a
    file that comes into the top folder of the tree and that the program does
    not import, and a file that comes into build/SystemResources."""
    tree, parts = norm(run.tree), entry.get("parts") or {}
    state = state_of(key, shared.setdefault("states", {}))
    if kind == "listing":
        if key in {norm(run.tree / folder) for folder in WRITTEN_FOLDERS.get(name, ())}:
            return WRITTEN_AGAIN
        if key in {norm(run.root / folder) for folder in LISTED_FOLDERS.get(name, ())}:
            return ROOT_LISTING
    if key.endswith(".pdb") and key[:-len(".pdb")] + ".exe" in known:
        return OWN_PDB
    if kind == "query":
        if under(key, norm(run.root / SYSTEM_RESOURCES)):
            return RESOURCES
        if under(key, tree):
            if state is None or state == "folder":
                return TREE_NOT_THERE if state is None else TREE_FOLDER
            if key == norm(run.tree / "build.ninja") and name in ASK_FOR_NINJA:
                return NINJA_QUESTION
        elif state == "folder":
            return ROOT_FOLDER
    if source_of(key) in known:
        return KNOWN_SOURCE
    if "build tree" in parts:
        if under(key, tree):
            return BUILT
        if under(key, norm(run.root / ".git")):
            return GIT_FILES
    if ((kind == "listing" or state is None) and not under(key, norm(run.root / "build"))
            and any(under(key, folder) for folder in hashed_folders(run, parts))):
        return HASHED_NAME
    return None


def compare(run, name: str, entry: dict, lines: tuple, processes: list, writes: set, shared: dict) -> dict:
    """The misses of one test, the paths of each ruled group, and the queries
    that only a `list` line of their folder makes known."""
    reads, listings = lines
    listed = {norm(p) for p in listings}
    lined = {norm(p) for p in reads} | listed
    parts = part_files(run, name, entry, shared)
    skip = (norm(run.tree / DRIVER.RECORDS), norm(run.tree / DRIVER.COVERAGE_RECORDS))
    tree = norm(run.tree)
    misses, by_folder, ruled = set(), set(), {}
    for process in processes:
        for kind, paths in (("read", process.reads), ("query", process.queries), ("listing", process.listings)):
            for key in paths:
                if key in lined or key in parts or any(under(key, s) for s in skip):
                    continue
                if under(key, tree) and key in writes:
                    continue
                if kind == "query" and key.rpartition("/")[0] in listed:
                    by_folder.add((process.image, key))
                    continue
                group = ruled_group(run, name, entry, kind, key, parts | lined, shared)
                if group:
                    ruled.setdefault(group, set()).add((kind, process.image, key))
                else:
                    misses.add((kind, process.image, key))
    return {"misses": [list(row) for row in sorted(misses)], "ruled": {g: len(v) for g, v in sorted(ruled.items())},
            "known by the list line of the folder": [list(row) for row in sorted(by_folder)],
            "processes": len(processes)}


def planted(run, store: dict, first_recording: tuple, report: dict, shared: dict) -> int:
    """The planted fault of decision 823."""
    here, by_test, writes = first_recording
    for name in here:
        entry = store[name]
        if "misses" not in report[name]:
            continue
        parts = part_files(run, name, entry, shared)
        listed = {norm(p) for _, p in entry.get("listings") or []}
        read = {key for process in by_test[name] for key in process.reads}
        for at, (_, path) in enumerate(entry.get("reads") or []):
            key = norm(path)
            if key in read and key not in parts and key not in listed and key not in writes:
                changed = copy.deepcopy(entry)
                del changed["reads"][at]
                result = compare(run, name, changed, stored_lines(changed), by_test[name], writes, shared)
                new = {tuple(m) for m in result["misses"]} - {tuple(m) for m in report[name]["misses"]}
                # The line made the path known for each kind of access, and
                # its .pyc file (decision 899): the read must come back, and no
                # miss of a different path.
                found = (("read" in {m[0] for m in new if m[2] == key})
                         and all(key in (m[2], source_of(m[2])) for m in new))
                print(f"planted: the line of {path} removed from {name}: new misses {sorted(new)}")
                print("the planted fault was found, and no other" if found else
                      "the planted fault was NOT found as the only new miss: this result cannot be trusted")
                return 0 if found else 1
    print("no line to plant a fault in: this result cannot be trusted")
    return 1


def compare_recordings(run, store: dict, out: pathlib.Path, pairs: list, plant: bool) -> int:
    """Compare each test of `store` in the first recording of `pairs` that
    includes it, write the report and the names of the tests not compared, and
    give the exit code."""
    names = sorted(name for name in store if name in run.tests)
    print(f"tests in the store: {len(names)}", flush=True)
    report, done, shared, first_recording, outside = {}, set(), {}, None, collections.defaultdict(set)
    temporary = norm(tempfile.gettempdir())
    for index, (csv_path, folder) in enumerate(pairs):
        processes, writes, first, last = read_recording(csv_path, run)
        by_test = collections.defaultdict(list)
        for process in processes:
            if process.owner:
                by_test[process.owner].append(process)
                for place in process.outside:
                    outside[TEMPORARY_FOLDER if under(place, temporary) else place].add(process.owner)
        # Process Monitor writes no day; the CSV file is written on the day of
        # its recording, at its export.
        day = datetime.date.fromtimestamp(csv_path.stat().st_mtime)
        here = sorted(included(folder, names, by_test, (day, first, last)) - done)
        print(f"{csv_path.name}: events {first} to {last}, {len(processes)} processes, {len(here)} tests included",
              flush=True)
        for name in here:
            if not by_test.get(name):
                report[name] = {"recording": csv_path.name, "not compared": "no process in the recording"}
                continue
            lines = stored_lines(store[name]) if index == 0 else lines_from_records(run, name, folder)
            report[name] = {"recording": csv_path.name,
                            **compare(run, name, store[name], lines, by_test[name], writes, shared)}
        done |= set(here)
        if index == 0:
            first_recording = (here, by_test, writes)
    for name in names:
        report.setdefault(name, {"not compared": "no recording includes it"})
    misses = sum(len(r.get("misses", [])) for r in report.values())
    not_compared = sorted(name for name, r in report.items() if "not compared" in r)
    ruled = collections.Counter()
    for entry in report.values():
        ruled.update(entry.get("ruled", {}))
    print(f"compared: {len(names) - len(not_compared)} of {len(names)}; misses: {misses}; tests with a miss: "
          f"{sum(1 for r in report.values() if r.get('misses'))}; queries known by a list line: "
          f"{sum(len(r.get('known by the list line of the folder', [])) for r in report.values())}")
    for name in not_compared:
        print(f"  not compared: {name}: {report[name]['not compared']}")
    for group, count in sorted(ruled.items()):
        print(f"  not inputs (decision 893): {count:6} {group}")
    for name, entry in sorted(report.items()):
        for kind, image, key in entry.get("misses", []):
            print(f"  MISS {kind} {key}: {image} in {name}")
    folders = {place: len(tests) for place, tests in sorted(outside.items())}
    print(f"folders outside the repository where tests read files (decision 895): {len(folders)}")
    # write_bytes, not write_text, which writes CRLF on Windows.
    out.write_bytes((json.dumps({"tree": str(run.tree), "ruled": dict(sorted(ruled.items())), "outside": folders,
                                 "tests": report}, indent=1) + "\n").encode("utf-8"))
    listed = pathlib.Path(f"{out}.not-compared.txt")
    listed.write_bytes("".join(f"{name}\n" for name in not_compared).encode("utf-8"))
    found = planted(run, store, first_recording, report, shared) if plant else 0
    return 1 if misses or not_compared or found else 0


def short_run(tree: pathlib.Path, names: pathlib.Path, out: pathlib.Path, ctest=None) -> int:
    """The short run of decisions 831 and 896: the given tests with CTest, in
    the environment that the driver gives CTest, with the jobs of `check`, half
    of the logical processors. CTest adds the fixtures of the tests."""
    env = {**os.environ, DRIVER.RECORDS_VARIABLE: str(tree / DRIVER.COVERAGE_RECORDS)}
    env = DRIVER.with_hook(DRIVER.without_test_name(env), tree, ROOT)
    head = ctest or [DRIVER.cache_value(tree, "CMAKE_CTEST_COMMAND")]
    jobs = max(1, (os.cpu_count() or 2) // 2)
    return subprocess.run([*head, "--output-on-failure", "-j", str(jobs), "--tests-from-file", str(names),
                           "--output-junit", str(out / f"short-{tree.name}.xml")], cwd=tree, env=env,
                          check=False).returncode


# --- the self-test ---


class Scene:
    """A repository, a tree, a store and the records of a stand-in run. Each
    case gives its events after the starts of the three tests, and gets what
    compare_recordings said (decisions 848 and 855). `native` runs a stand-in
    program that the case can name on a command line, on each machine."""

    NOON = datetime.datetime(2026, 10, 30, 12).timestamp()
    HASH = "a" * 64

    def __init__(self, folder: pathlib.Path):
        self.folder = folder
        self.root = folder / "repo"
        self.tree = self.root / "build" / "rel"
        self.outside = folder / "elsewhere"
        for name in ("src", "data", "scripts", "cmake", "tests/golden", "build/rel/shaders", "build/rel/run-tests",
                     "build/rel/coverage-records", "build/rel/configure_guard/steady/src", "elsewhere"):
            (folder / "repo" / name if name != "elsewhere" else self.outside).mkdir(parents=True, exist_ok=True)
        for name in ("src/in.txt", "src/out.txt", "data/d.txt", "scripts/s.py", "scripts/run-tests.py", "cmake/c.cmake",
                     "build/rel/t.exe", "build/rel/t.pdb", "build/rel/other.pdb", "build/rel/lib.dll",
                     "build/rel/build.ninja", "build/rel/shaders/a.spv", "build/rel/run-tests/fingerprints.json",
                     "build/rel/CTestTestfile.cmake"):
            (self.root / name).write_text("x", encoding="utf-8")
        (self.tree / "t.exe").chmod(0o755)
        self.run = DRIVER.stand_in_run(self.tree, self.root)
        self.run.cmake = "cmake"
        self.run.world.library_map = {norm(self.tree / "t.exe"): [str(self.tree / "lib.dll")]}
        self.script = self.root / "scripts" / "s.py"
        self.run.tests = {"cpp": {"name": "cpp", "command": [str(self.tree / "t.exe"), "a case"]},
                          "script": {"name": "script", "command": ["python", str(self.script)]},
                          "plain": {"name": "plain", "command": ["plain.exe"]}}
        self.store = {"cpp": {"parts": {"definition": "d", "libraries": "l"}, "reads": [[self.HASH, self.path("src/in.txt")]]},
                      "script": {"parts": {"definition": "d", "files": "f"}, "reads": [], "listings": []},
                      "plain": {"parts": {"environment": "e"}, "reads": []}}

    def path(self, name: str) -> str:
        return str(self.root / name)

    def records(self, name: str = "records", day_before: tuple = (), texts=None) -> pathlib.Path:
        """A copy of test-reads/: a record for `cpp` and for `script`, none
        for `plain`, each written at noon of the day of the recording, or a
        day before it for a test in `day_before`."""
        folder = self.folder / name
        folder.mkdir(exist_ok=True)
        written = texts or {"cpp.txt": "end\n", "script.py-1-2.txt": "not read in the first recording\n"}
        for file, text in written.items():
            (folder / file).write_bytes(text.encode("utf-8"))
            moment = self.NOON - (86400 if any(file.startswith(name) for name in day_before) else 0)
            os.utime(folder / file, (moment, moment))
        return folder

    def recording(self, events: list, name: str = "events.csv", starts=None) -> pathlib.Path:
        """A CSV file as Process Monitor saves it, with the BOM it writes: the
        start of each test, then `events`, on the day of `NOON`."""
        path = self.folder / name
        rows = (starts if starts is not None else [self.start("11", f'"{self.tree / "t.exe"}" "a case"'),
                                                   self.start("12", f'python "{self.script}"'),
                                                   self.start("13", "plain.exe")]) + events
        with open(path, "w", encoding="utf-8-sig", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(("Time of Day", "Process Name", "PID", "Operation", "Path", "Result", "Detail"))
            writer.writerow(("12:00:00.0000000 AM", "first", "1", "Process Start", "", "SUCCESS", "Parent PID: 0"))
            writer.writerows(("12:00:01.0000000 PM", *row) for row in rows)
            writer.writerow(("11:59:59.0000000 PM", "last", "1", "Process Start", "", "SUCCESS", "Parent PID: 0"))
        os.utime(path, (self.NOON, self.NOON))
        return path

    @staticmethod
    def start(pid: str, command: str, parent: str = "1", env: str = "") -> tuple:
        return ("T.exe", pid, "Process Start", "", "SUCCESS",
                f"Parent PID: {parent}, Command line: {command}, Current directory: C:\\, Environment: \n;\t{env}\n")

    @staticmethod
    def opened(pid: str, path, access: str = "Generic Read", options: str = "Non-Directory File",
               result: str = "SUCCESS") -> tuple:
        return ("T.exe", pid, "CreateFile", str(path), result,
                f"Desired Access: {access}, Disposition: Open, Options: {options}, Attributes: n/a")

    @staticmethod
    def asked(pid: str, path, operation: str = "QueryOpen", result: str = "NAME NOT FOUND") -> tuple:
        return ("T.exe", pid, operation, str(path), result, "")

    @staticmethod
    def listed(pid: str, path) -> tuple:
        return ("T.exe", pid, "QueryDirectory", f"{path}/*", "SUCCESS", "")

    def said(self, events: list, change=None, pairs=None, plant: bool = False, starts=None) -> dict:
        """What compare_recordings said: its exit code, the misses as (test,
        kind, the end of the path), what it printed, and its report."""
        store = copy.deepcopy(self.store)
        for name, fields in (change or {}).items():
            store[name].update(fields)
        out = self.folder / "out.json"
        printed = io.StringIO()
        with contextlib.redirect_stdout(printed):
            code = compare_recordings(self.run, store, out, pairs or [(self.recording(events, starts=starts),
                                                                       self.records())], plant)
        report = json.loads(out.read_bytes().decode("utf-8"))
        misses = sorted((name, kind, key[len(norm(self.root)) + 1:]) for name, entry in report["tests"].items()
                        for kind, _, key in entry.get("misses", []))
        return {"exit": code, "misses": misses, "printed": printed.getvalue(), "report": report,
                "not compared": pathlib.Path(f"{out}.not-compared.txt").read_text(encoding="utf-8")}


def self_test() -> int:
    failures, cases = [], []

    def expect(name, got, wanted):
        cases.append(name)
        if got != wanted:
            failures.append(f"{name}: got {got!r}, wanted {wanted!r}")

    expect("a time before noon", clock("8:59:01.1714987 AM"), datetime.time(8, 59, 1))
    expect("a time after noon", clock("1:02:03.1 PM"), datetime.time(13, 2, 3))
    expect("midnight and noon in the 12-hour form", (clock("12:00:00.0 AM"), clock("12:00:00.0 PM")),
           (datetime.time(0), datetime.time(12)))
    expect("the 24-hour form, with a comma", clock("13:02:03,1234567"), datetime.time(13, 2, 3))

    with tempfile.TemporaryDirectory() as scratch:
        scene = Scene(pathlib.Path(scratch))
        run, tree, root, opened, asked, listed = (scene.run, scene.tree, scene.root, scene.opened, scene.asked,
                                                  scene.listed)
        src_new = root / "src" / "new.txt"

        said = scene.said([])
        expect("only the starts: each test compared, no miss, exit 0", (said["exit"], said["misses"], said["not compared"]),
               (0, [], ""))
        expect("the report: the three tests, each with its processes",
               {name: entry.get("processes") for name, entry in said["report"]["tests"].items()},
               {"cpp": 1, "plain": 1, "script": 1})

        # The three kinds of a miss, and that a miss fails the run.
        said = scene.said([opened("11", root / "src" / "in.txt")])
        expect("a read with a `read` line: no miss", (said["exit"], said["misses"]), (0, []))
        said = scene.said([opened("11", root / "src" / "out.txt")])
        expect("a read with no line: a miss, and exit 1", (said["exit"], said["misses"]),
               (1, [("cpp", "read", "src/out.txt")]))
        expect("the miss is printed with its program and its test", "MISS read " in said["printed"]
               and "t.exe in cpp" in said["printed"], True)
        said = scene.said([asked("11", src_new)])
        expect("a question about a file that is not there, in no ruled group: a miss", (said["exit"], said["misses"]),
               (1, [("cpp", "query", "src/new.txt")]))
        said = scene.said([listed("11", root / "src")])
        expect("a listing with no line: a miss", (said["exit"], said["misses"]), (1, [("cpp", "listing", "src")]))
        said = scene.said([opened("11", root / "src" / "out.txt", access="Read Attributes")])
        expect("a CreateFile for the attributes only: a question", said["misses"], [("cpp", "query", "src/out.txt")])
        said = scene.said([opened("11", src_new, result="NAME NOT FOUND")])
        expect("a read of a file that is not there: a question", said["misses"], [("cpp", "query", "src/new.txt")])
        said = scene.said([opened("11", root / "src" / "out.txt", result="ACCESS DENIED")])
        expect("a read that Windows refused: nothing", said["misses"], [])
        said = scene.said([asked("11", root / "src" / "out.txt", "QueryAllInformationFile", "SUCCESS")])
        expect("a query of the information of a file: a question", said["misses"], [("cpp", "query", "src/out.txt")])
        said = scene.said([asked("11", root / "src" / "out.txt", "QueryAllInformationFile", "ACCESS DENIED")])
        expect("a query with no answer: nothing", said["misses"], [])

        # What a folder is.
        said = scene.said([opened("11", root / "src", options="Directory File")])
        expect("a folder opened as a folder is a question, not a read", said["report"]["ruled"], {ROOT_FOLDER: 1})
        said = scene.said([opened("11", root / "src")])
        expect("a folder opened with no such option is a question, not a read", (said["misses"], said["report"]["ruled"]),
               ([], {ROOT_FOLDER: 1}))
        said = scene.said([opened("11", root / "src" / "out.txt", options="Synchronous IO Non-Directory File")])
        expect('"Non-Directory File" is a file', said["misses"], [("cpp", "read", "src/out.txt")])

        # Known by a line, by a part, or as an output.
        change = {"script": {"listings": [[scene.HASH, str(root / "src")]]}}
        said = scene.said([listed("12", root / "src")], change)
        expect("a listing with a `list` line: no miss", said["misses"], [])
        said = scene.said([asked("12", src_new)], change)
        expect("a question in a folder with a `list` line: counted apart, and no miss",
               (said["misses"], said["report"]["tests"]["script"]["known by the list line of the folder"]),
               ([], [["t.exe", norm(src_new)]]))
        said = scene.said([opened("12", src_new.parent / "out.txt")], change)
        expect("a read in a folder with a `list` line is still a miss", said["misses"],
               [("script", "read", "src/out.txt")])
        said = scene.said([opened("11", tree / "lib.dll"), opened("11", tree / "t.exe")])
        expect("the program of the test and its libraries: no miss", said["misses"], [])
        said = scene.said([opened("12", scene.script)])
        expect("a file on the command line: no miss", said["misses"], [])
        for part, name in (("cautious", "data/d.txt"), ("scripts", "cmake/c.cmake"),
                           ("build tree", "build/rel/build.ninja"), ("store", "build/rel/run-tests/fingerprints.json"),
                           ("shaders", "build/rel/shaders/a.spv")):
            event = opened("11", root / name)
            said = scene.said([event], {"cpp": {"parts": {part: "p"}}})
            expect(f"the part {part} makes {name} known", said["misses"], [])
            expect(f"with no part {part}, {name} is a miss", scene.said([event])["misses"], [("cpp", "read", name)])
        said = scene.said([opened("11", root / "build" / "rel" / "shaders" / "a.spv")],
                          {"cpp": {"shaders": [scene.path("build/rel/shaders/a.spv")]}})
        expect("a shader that the records of the test name: no miss", said["misses"], [])
        program = str(tree / "t.exe")
        said = scene.said([opened("13", tree / "lib.dll")],
                          {"plain": {"parts": {"libraries": "l"}, "programs": {program: {}}}})
        expect("a library of a program of the store: no miss", said["misses"], [])
        said = scene.said([opened("13", tree / "lib.dll")], {"plain": {"children": [{"program": program, "kind": "x"}]}})
        expect("a library of a child: no miss", said["misses"], [])
        event = opened("13", tree / "CTestTestfile.cmake")
        said = scene.said([event], {"plain": {"children": [{"program": program, "kind": "ctest"}]}})
        expect("a CTest file for a ctest child: no miss", said["misses"], [])
        said = scene.said([event], {"plain": {"children": [{"program": program, "kind": "x"}]}})
        expect("a CTest file for a different child: a miss", said["misses"],
               [("plain", "read", "build/rel/ctesttestfile.cmake" if os.name == "nt" else "build/rel/CTestTestfile.cmake")])
        written = tree / "out.txt"
        said = scene.said([opened("11", written, access="Generic Write"), opened("11", written, result="SUCCESS")])
        expect("a file of the tree that a process wrote: an output, not a miss", said["misses"], [])
        said = scene.said([opened("11", tree / "x.txt.tmp9f", access="Generic Write"), opened("12", tree / "x.txt")])
        expect("a file that CMake writes under a temporary name: an output", said["misses"], [])
        said = scene.said([opened("11", root / "src" / "w.txt", access="Generic Write"),
                           opened("12", root / "src" / "out.txt")])
        expect("a write outside the tree makes no read known, and it is a question", said["misses"],
               [("cpp", "query", "src/w.txt"), ("script", "read", "src/out.txt")])
        said = scene.said([opened("11", root / "src" / "out.txt", access="Generic Write"),
                           opened("11", root / "src" / "out.txt")])
        expect("a file of the repository that the test wrote: still a read of it", said["misses"],
               [("cpp", "query", "src/out.txt"), ("cpp", "read", "src/out.txt")])
        said = scene.said([opened("11", tree / "test-reads" / "cpp.txt"), opened("11", tree / "coverage-records" / "a")])
        expect("the records of the tests are not inputs", said["misses"], [])

        # A test with no complete record, by its other parts (decision 892).
        folder_test = {"name": "plain", "command": ["plain.exe", str(root / "src")]}
        run.tests["plain"] = folder_test
        said = scene.said([opened("11", root / "src" / "in.txt"), opened("13", root / "src" / "out.txt")],
                          starts=[scene.start("11", f'"{tree / "t.exe"}" "a case"'), scene.start("12", "python x"),
                                  scene.start("13", f'plain.exe "{root / "src"}"')])
        expect("a test with no record: the files of a folder on its command line are known", said["misses"], [])
        run.tests["plain"] = {"name": "plain", "command": ["plain.exe"]}

        # The groups that are not inputs (decisions 834, 864 and 893).
        said = scene.said([asked("11", tree / "vcruntime140.dll")])
        expect("not there, in the build tree: no miss, and counted", (said["misses"], said["report"]["ruled"]),
               ([], {TREE_NOT_THERE: 1}))
        expect("the count is printed", f"1 {TREE_NOT_THERE}" in said["printed"], True)
        said = scene.said([asked("11", tree / "shaders", result="SUCCESS")])
        expect("a folder of the build tree: no miss", said["report"]["ruled"], {TREE_FOLDER: 1})
        said = scene.said([asked("11", root / "cmake", result="SUCCESS")])
        expect("a folder of the repository: no miss", said["report"]["ruled"], {ROOT_FOLDER: 1})
        said = scene.said([asked("11", root / "build" / "SystemResources" / "t.exe.mun")])
        expect("build/SystemResources: no miss", said["report"]["ruled"], {RESOURCES: 1})
        (root / "build" / "SystemResources").mkdir()
        (root / "build" / "SystemResources" / "t.exe.mun").write_text("x", encoding="utf-8")
        said = scene.said([opened("11", root / "build" / "SystemResources" / "t.exe.mun")])
        expect("a read of a file that is there in build/SystemResources: a miss", (said["exit"], len(said["misses"])),
               (1, 1))
        said = scene.said([asked("11", tree / "build.ninja", result="SUCCESS")])
        expect("a question about a file of the tree that is there: a miss", said["misses"],
               [("cpp", "query", "build/rel/build.ninja")])
        for kind, event in (("read", opened("11", tree / "t.pdb")), ("query", asked("11", tree / "t.pdb", result="SUCCESS"))):
            said = scene.said([event])
            expect(f"a {kind} of the .pdb file of its own program: no miss", (said["misses"], said["report"]["ruled"]),
                   ([], {OWN_PDB: 1}))
        said = scene.said([opened("11", tree / "other.pdb")])
        expect("a read of the .pdb file of a different program: a miss", said["misses"],
               [("cpp", "read", "build/rel/other.pdb")])
        no_such = root / "data" / "no-such-file.txt"
        said = scene.said([asked("11", no_such)], {"cpp": {"parts": {"cautious": "c"}}})
        expect("not there, in the cautious set, with that part: no miss", said["report"]["ruled"], {HASHED_NAME: 1})
        said = scene.said([asked("11", no_such)])
        expect("not there, in the cautious set, with no such part: a miss", said["misses"],
               [("cpp", "query", "data/no-such-file.txt")])
        guard = run.tests.pop("plain")
        names = ("configure_guard", "probe_clear_determinism", "reproducible_build", "mutate_self_test")
        run.tests.update({name: {"name": name, "command": [f"{name}.exe"]} for name in names})
        store = {name: {"parts": {}, "reads": []} for name in names}
        starts = [scene.start(f"2{at}", f"{name}.exe") for at, name in enumerate(names)]
        steady = tree / "configure_guard" / "steady" / "src"
        ninja = asked("23", tree / "build.ninja", result="SUCCESS")
        events = [listed("20", steady), listed("20", root), listed("21", root), listed("21", root / "build"),
                  listed("21", steady), listed("22", root / "build"), listed("22", root), ninja]
        out = scene.folder / "listings.json"
        with contextlib.redirect_stdout(io.StringIO()):
            compare_recordings(run, store, out, [(scene.recording(events, starts=starts), scene.records())], False)
        report = json.loads(out.read_bytes().decode("utf-8"))
        expect("the ruled listings and the question of CMake, each for its test only", (
            report["ruled"], [report["tests"][name]["misses"] for name in names]),
            ({ROOT_LISTING: 3, WRITTEN_AGAIN: 1, NINJA_QUESTION: 1},
             [[["listing", "t.exe", norm(root)]], [["listing", "t.exe", norm(steady)]],
              [["listing", "t.exe", norm(root)]], []]))
        for name in names:
            del run.tests[name]
        run.tests["plain"] = guard

        # A .pyc file (decision 899).
        compiled = root / "scripts" / "__pycache__" / "s.cpython-314.pyc"
        said = scene.said([opened("12", compiled), asked("12", compiled, result="SUCCESS")])
        expect("a .pyc file of a .py file on the command line: no miss", (said["misses"], said["report"]["ruled"]),
               ([], {KNOWN_SOURCE: 2}))
        compiled = root / "src" / "__pycache__" / "in.cpython-314.pyc"
        said = scene.said([opened("11", compiled)])
        expect("a .pyc file of a .py file that a part or a line does not hold: a miss", said["misses"],
               [("cpp", "read", "src/__pycache__/in.cpython-314.pyc")])
        said = scene.said([opened("11", compiled)], {"cpp": {"reads": [[scene.HASH, scene.path("src/in.py")]]}})
        expect("a .pyc file of a .py file with a `read` line: no miss", said["report"]["ruled"], {KNOWN_SOURCE: 1})
        said = scene.said([opened("11", root / "src" / "other.cpython-314.pyc")],
                          {"cpp": {"reads": [[scene.HASH, scene.path("src/other.py")]]}})
        expect("a .pyc file that is not in __pycache__: a miss", said["misses"],
               [("cpp", "read", "src/other.cpython-314.pyc")])

        # The build tree for a test with that part, and .git (decisions 901 and 902).
        change = {"cpp": {"parts": {"build tree": "b"}}}
        built = opened("11", tree / "_deps" / "lib" / "a.hpp")
        said = scene.said([built], change)
        expect("a file of the tree with the part build tree: no miss", said["report"]["ruled"], {BUILT: 1})
        expect("a file of the tree with no such part: a miss", scene.said([built])["misses"],
               [("cpp", "read", "build/rel/_deps/lib/a.hpp")])
        git = opened("11", root / ".git" / "index")
        said = scene.said([git], change)
        expect("a file of .git with the part build tree: no miss", said["report"]["ruled"], {GIT_FILES: 1})
        expect("a file of .git with no such part: a miss", scene.said([git])["misses"], [("cpp", "read", ".git/index")])
        said = scene.said([asked("11", root / "src" / "out.txt", result="SUCCESS")], change)
        expect("a question about a file of the repository that is there: not a name that is not there", said["misses"],
               [("cpp", "query", "src/out.txt")])

        # A name in a folder that a part hashes (decision 900).
        for part, folder in (("build tree", "src"), ("build tree", "."), ("scripts", "scripts"), ("scripts", "cmake"),
                             ("cautious", "data"), ("cautious", "tests/golden")):
            change = {"cpp": {"parts": {part: "p"}}}
            said = scene.said([asked("11", root / folder / "new.txt"), listed("11", root / folder)], change)
            expect(f"a new name and a listing in {folder}, with the part {part}: no miss",
                   (said["misses"], said["report"]["ruled"]), ([], {HASHED_NAME: 2}))
        said = scene.said([asked("11", src_new), listed("11", root / "src")], {"cpp": {"parts": {"scripts": "p"}}})
        expect("a folder that the part does not hold: two misses", len(said["misses"]), 2)
        said = scene.said([asked("11", root / "build" / "new.txt"), listed("11", root / "build")],
                          {"cpp": {"parts": {"build tree": "p"}}})
        expect("build/ is not in the part build tree", [(kind, path) for _, kind, path in said["misses"]],
               [("listing", "build"), ("query", "build/new.txt")])
        said = scene.said([opened("11", root / "src" / "out.txt")], {"cpp": {"parts": {"build tree": "p"}}})
        expect("a read in a folder that a part hashes needs the file in the part", said["misses"],
               [("cpp", "read", "src/out.txt")])

        # A process and its test (decision 823).
        read = opened("31", root / "src" / "out.txt")
        spelled = str(tree / "T.EXE").replace("/", "\\")
        said = scene.said([scene.start("31", f'"{spelled}" "a case"'), read])
        expect("by the command line, as Windows compares the path of the program", said["misses"],
               [("cpp", "read", "src/out.txt")])
        said = scene.said([scene.start("31", f'"{tree / "t.exe"}" "A CASE"'), read])
        expect("each other argument with the same text: no test, thus no miss", said["misses"], [])
        said = scene.said([scene.start("31", "x.exe", env="ORBSIM_TEST_NAME=script"), read])
        expect("by ORBSIM_TEST_NAME", said["misses"], [("script", "read", "src/out.txt")])
        said = scene.said([scene.start("31", "x.exe", env="ORBSIM_TEST_NAME=no test"), read])
        expect("a name that is no test of the run: no test", said["misses"], [])
        owners = json.dumps([["C:/other/test-reads", "plain"], [str(tree / "test-reads"), "script"]])
        said = scene.said([scene.start("31", "x.exe", env=f"ORBSIM_TEST_NAME=cpp\n;\tORBSIM_READ_RECORD_OWNER={owners}"),
                           read])
        expect("by ORBSIM_READ_RECORD_OWNER, the pair of this tree, before ORBSIM_TEST_NAME", said["misses"],
               [("script", "read", "src/out.txt")])
        said = scene.said([scene.start("31", "x.exe", env="ORBSIM_READ_RECORD_OWNER=not JSON"), read])
        expect("an owner that is not JSON: no test", said["misses"], [])
        said = scene.said([scene.start("31", "x.exe", parent="13"), scene.start("32", "y.exe", parent="31"),
                           opened("32", root / "src" / "out.txt")])
        expect("by its parent, through a process with no test", said["misses"], [("plain", "read", "src/out.txt")])
        said = scene.said([scene.start("11", "x.exe", parent="1"), opened("11", root / "src" / "out.txt")])
        expect("a process number that Windows gave again: the new process, with no test", said["misses"], [])
        said = scene.said([opened("99", root / "src" / "out.txt")])
        expect("an event of a process with no start: nothing", said["misses"], [])

        # The tests that a recording includes (decisions 831 and 892).
        pairs = [(scene.recording([]), scene.records(day_before=("cpp",)))]
        said = scene.said([], pairs=pairs)
        expect("a record of the day before: not compared, exit 1, and the name in the file", (
            said["exit"], said["report"]["tests"]["cpp"], said["not compared"]),
            (1, {"not compared": "no recording includes it"}, "cpp\n"))
        expect("the test is printed", "not compared: cpp" in said["printed"], True)
        pairs = [(scene.recording([], starts=[scene.start("12", f'python "{scene.script}"')]), scene.records())]
        said = scene.said([], pairs=pairs)
        expect("a record, and no process in the recording: not compared", (
            said["exit"], said["report"]["tests"]["cpp"]["not compared"], said["not compared"]),
            (1, "no process in the recording", "cpp\nplain\n"))
        expect("a test with no record file and no process: not compared",
               said["report"]["tests"]["plain"], {"not compared": "no recording includes it"})
        expect("a comparison that compares nothing fails", scene.said([], pairs=[
            (scene.recording([], starts=[]), scene.records(day_before=("cpp", "script")))])["exit"], 1)
        plain = run.tests.pop("plain")
        said = scene.said([])
        expect("a test of the store that the tree does not list is left out",
               (said["exit"], "plain" in said["report"]["tests"]), (0, False))
        run.tests["plain"] = plain

        # A second recording, with the read lines of its own records.
        first = scene.recording([], "first.csv", starts=[scene.start("13", "plain.exe"),
                                                         scene.start("12", f'python "{scene.script}"')])
        old = scene.records("old", day_before=("cpp",))
        wanted = opened("11", root / "src" / "out.txt")
        second = scene.recording([wanted], "second.csv", starts=[scene.start("11", f'"{tree / "t.exe"}" "a case"')])
        out_line = f"{scene.HASH} {scene.path('src/out.txt')}\nend\n"
        said = scene.said([], pairs=[(first, old), (second, scene.records("new", texts={"cpp.txt": out_line}))])
        expect("a second recording: the lines of its C++ record, not of the store",
               (said["exit"], said["report"]["tests"]["cpp"]["recording"], said["misses"]), (0, "second.csv", []))
        said = scene.said([], pairs=[(first, old), (second, scene.records("none", texts={"cpp.txt": "end\n"}))])
        expect("a second recording: a read that its record leaves out is a miss", said["misses"],
               [("cpp", "read", "src/out.txt")])
        said = scene.said([], pairs=[(first, old), (second, scene.records("cut", texts={"cpp.txt": "no end\n"}))])
        expect("a C++ record that is not whole: compared with its other parts", said["misses"],
               [("cpp", "read", "src/out.txt")])
        hook = root.joinpath(*DRIVER.HOOK_FOLDER, DRIVER.HOOK_FILE)
        command = scene.run.tests["script"]["command"]
        text = DRIVER.python_record(["python.exe", *command[1:]], f"read {scene.HASH} {hook}",
                                    f"read {scene.HASH} {scene.path('src/out.txt')}",
                                    f"list {scene.HASH} {scene.path('cmake')}")
        first_cpp = scene.recording([], "first2.csv", starts=[scene.start("13", "plain.exe"),
                                                              scene.start("11", f'"{tree / "t.exe"}" "a case"')])
        old = scene.records("old2", day_before=("script",))
        events = [opened("12", root / "src" / "out.txt"), listed("12", root / "cmake"), opened("12", root / "src" / "in.txt")]
        second = scene.recording(events, "second2.csv", starts=[scene.start("12", f'python "{scene.script}"')])
        said = scene.said([], pairs=[(first_cpp, old), (second, scene.records("py", texts={"script.py-5-6.txt": text}))])
        expect("a second recording: the lines of the records of Python", said["misses"],
               [("script", "read", "src/in.txt")])
        said = scene.said([], pairs=[(first_cpp, old), (second, scene.records("pycut", texts={
            "script.py-5-6.txt": DRIVER.python_record(["python.exe", *command[1:]], end=False)}))])
        expect("records of Python that are not complete: compared with its other parts", said["misses"], [
            ("script", "listing", "cmake"), ("script", "read", "src/in.txt"), ("script", "read", "src/out.txt")])
        cmake_script = root / "cmake" / "c.cmake"
        run.tests["script"] = {"name": "script", "command": ["cmake", "-P", str(cmake_script)]}
        trace = DRIVER.cmake_trace(cmake_script, ("file", "READ", scene.path("src/out.txt"), "v"))
        events = [opened("12", root / "src" / "out.txt"), opened("12", root / "src" / "in.txt")]
        second = scene.recording(events, "second3.csv", starts=[scene.start("12", f'cmake -P "{cmake_script}"')])
        said = scene.said([], pairs=[(first_cpp, old), (second, scene.records("trace", texts={"script.trace.json": trace}))])
        expect("a second recording: the lines of the trace of CMake", said["misses"], [("script", "read", "src/in.txt")])
        said = scene.said([], pairs=[(first_cpp, old), (second, scene.records("notrace", texts={"script.py-9-9.txt": "x"}))])
        expect("no trace: compared with its other parts", said["misses"],
               [("script", "read", "src/in.txt"), ("script", "read", "src/out.txt")])
        said = scene.said([], pairs=[(first_cpp, old), (second, scene.records("badtrace", texts={
            "script.trace.json": DRIVER.cmake_trace(cmake_script, ("file", "READ"))}))])
        expect("a trace that the driver refuses: compared with its other parts", said["misses"],
               [("script", "read", "src/in.txt"), ("script", "read", "src/out.txt")])
        run.tests["script"] = {"name": "script", "command": ["python", str(scene.script)]}

        # The folders outside the repository (decision 895).
        place = scene.outside / "x"
        tempfile.tempdir = str(scene.folder / "temporary")
        said = scene.said([opened("11", place / "a.dll"), opened("12", place / "b.dll"), opened("12", scene.outside / "c.dll")])
        expect("each folder outside the repository, with its number of tests", said["report"]["outside"],
               {norm(scene.outside): 1, norm(place): 2})
        expect("the number of folders is printed", "(decision 895): 2" in said["printed"], True)
        expect("an outside read is no miss", said["exit"], 0)
        said = scene.said([asked("11", place / "a.dll", result="SUCCESS"), listed("11", place),
                           opened("11", place / "a.dll", access="Read Attributes"),
                           opened("11", place / "n.dll", result="NAME NOT FOUND"),
                           opened("11", place, options="Directory File"), opened("11", scene.outside),
                           opened("99", place / "a.dll"), opened("11", "\\Device\\Afd\\Endpoint")])
        expect("only a read of a file by a process of a test counts", said["report"]["outside"], {})
        temporary = pathlib.Path(tempfile.gettempdir()) / "tmpabc" / "repo"
        said = scene.said([opened("11", temporary / "a.txt"), opened("12", temporary.parent / "b.txt")])
        expect("the temporary folder is one entry", said["report"]["outside"], {TEMPORARY_FOLDER: 2})
        tempfile.tempdir = None

        # The planted fault of decision 823, and a run with nothing to plant it in.
        line = root / "src" / "in.txt"
        said = scene.said([opened("11", line)], plant=True)
        expect("the planted fault: found, and the only new miss", (said["exit"], "was found" in said["printed"]), (0, True))
        said = scene.said([], plant=True)
        expect("no line to plant a fault in: exit 1", (said["exit"], "no line to plant" in said["printed"]), (1, True))
        said = scene.said([opened("11", line)], {"cpp": {"parts": {"cautious": "c", "libraries": "l"},
                                                         "reads": [[scene.HASH, scene.path("data/d.txt")]]}}, plant=True)
        expect("a path that a part also holds is not a line to plant in", said["exit"], 1)
        said = scene.said([opened("11", line), opened("11", root / "src" / "out.txt")], plant=True)
        expect("a miss and a plant found: exit 1 for the miss", said["exit"], 1)
        source = root / "src" / "in.py"
        said = scene.said([opened("11", source), opened("11", root / "src" / "__pycache__" / "in.cpython-314.pyc")],
                          {"cpp": {"reads": [[scene.HASH, str(source)]]}}, plant=True)
        expect("the planted fault of a .py line: its .pyc file comes back with it", (
            said["exit"], "was found" in said["printed"]), (0, True))
        processes, writes, _, _ = read_recording(scene.recording([opened("11", line)]), run)
        tested = [process for process in processes if process.owner == "cpp"]
        with contextlib.redirect_stdout(io.StringIO()):
            code = planted(run, scene.store, (["cpp"], {"cpp": tested}, writes),
                           {"cpp": {"misses": [["read", "t.exe", norm(line)]]}}, {})
        expect("a planted fault that brings no new miss is not found", code, 1)

        # A part that the script does not know stops it (decision 894).
        try:
            scene.said([], {"cpp": {"parts": {"a new part": "x"}}})
            expect("a part that the script does not know stops it", "no stop", "a stop")
        except SystemExit as stop:
            expect("a part that the script does not know stops it, and names it", "a new part" in str(stop), True)
        written = {name or start for name, start in re.findall(
            r'found\["([^"]+)"\]|"(\w+)": (?:definition|environment|self\.machine)\(', inspect.getsource(DRIVER.Run.parts))}
        expect("each part that the driver writes is a part that this script knows", written, set(NO_FILES | WITH_FILES))
        empty = scene.folder / "empty.csv"
        empty.write_bytes(b"Time of Day,Process Name,PID,Operation,Path,Result,Detail\n")
        try:
            scene.said([], pairs=[(empty, scene.records())])
            expect("a recording with no event stops the script", "no stop", "a stop")
        except SystemExit as stop:
            expect("a recording with no event stops the script, and names it", "holds no event" in str(stop), True)

        # The short run (decision 896).
        stub = scene.folder / "ctest_stand_in.py"
        seen = scene.folder / "seen.json"
        stub.write_text("import json, os, sys\n"
                        f"json.dump({{'args': sys.argv[1:], 'cwd': os.getcwd(), 'env': dict(os.environ)}}, "
                        f"open({str(seen)!r}, 'w', encoding='utf-8'))\nsys.exit(7)\n", encoding="utf-8")
        names = scene.folder / "names.txt"
        before = os.environ.get(DRIVER.TEST_NAME_VARIABLE)
        os.environ[DRIVER.TEST_NAME_VARIABLE] = "the name of this test"
        code = short_run(tree, names, scene.folder, [sys.executable, "-I", str(stub)])
        if before is None:
            del os.environ[DRIVER.TEST_NAME_VARIABLE]
        else:
            os.environ[DRIVER.TEST_NAME_VARIABLE] = before
        got = json.loads(seen.read_text(encoding="utf-8"))
        env = {name.upper() if os.name == "nt" else name: value for name, value in got["env"].items()}
        expect("the short run gives the exit code of CTest", code, 7)
        expect("the short run asks CTest for the tests of the file, with a report", (
            got["args"][:1], got["args"][3:]),
            (["--output-on-failure"], ["--tests-from-file", str(names), "--output-junit",
                                       str(scene.folder / "short-rel.xml")]))
        expect("the short run runs in the tree", norm(got["cwd"]), norm(tree))
        expect("the short run gives the variables of the driver", (
            env[DRIVER.RECORDS_VARIABLE], env[DRIVER.READ_RECORDS_VARIABLE],
            env["PYTHONPATH"].split(os.pathsep)[0]),
            (str(tree / DRIVER.COVERAGE_RECORDS), str(tree / DRIVER.RECORDS), str(ROOT.joinpath(*DRIVER.HOOK_FOLDER))))
        expect("the short run does not give the test name", DRIVER.TEST_NAME_VARIABLE in env, False)

    for failure in failures:
        print(f"self-test FAILED: {failure}")
    print(f"compare-reads self-test: {len(cases) - len(failures)} of {len(cases)} cases")
    return 1 if failures else 0


def main(argv: list) -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    if argv[1:] == ["--self-test"]:
        return self_test()
    if len(argv) == 5 and argv[1] == "--short-run":
        return short_run(pathlib.Path(argv[2]).resolve(), pathlib.Path(argv[3]).resolve(),
                         pathlib.Path(argv[4]).resolve())
    plant = "--plant" in argv
    args = [a for a in argv[1:] if a != "--plant"]
    if len(args) < 5 or (len(args) - 3) % 2:
        sys.exit(__doc__)
    tree = pathlib.Path(args[0]).resolve()
    run = DRIVER.Run(tree, DRIVER.cache_value(tree, "CMAKE_CTEST_COMMAND"), ROOT)
    store = json.loads(pathlib.Path(args[1]).read_text(encoding="utf-8"))["tests"]
    pairs = [(pathlib.Path(args[i]), pathlib.Path(args[i + 1])) for i in range(3, len(args), 2)]
    return compare_recordings(run, store, pathlib.Path(args[2]), pairs, plant)


if __name__ == "__main__":
    sys.exit(main(sys.argv))

"""M1-138: the read records compared with Process Monitor (register decisions
767, 822, 823, 830 and 831). A measurement script, not code of the task.

    python -I -B docs/measurements/m1-138/compare.py <tree> <store copy> <out json>
        <csv> <records copy> [<csv> <records copy> ...] [--plant]

Run from the root of the repository. Each pair of a CSV of Process Monitor and
a copy of the folder test-reads/ is one recording of the tree, the recording of
the full run first. A script test is compared in the first recording that
includes it: the newest of its read records in that copy was written between
the first and the last event of the recording (decision 831). Only the tests
with the new rules are compared: a Python test with complete records, or a
`cmake -P` test with its trace, as the store copy gives them. The store copy is
the store after the full run. It gives the parts of each test, and, for the
recording of the full run, its read lines (decision 823). For each other
recording, the read lines come from the test's records in that copy, read with
the functions of the driver (decisions 830 and 831).

What a process of a test did, for each path in the repository, the build tree
included:
  read     CreateFile that asks for the contents of a file (Generic Read or
           Read Data)
  query    a CreateFile with no such access, QueryOpen, or a query of the
           information of a file, with a result that answers the question
  listing  QueryDirectory, with a pattern at the end of the path removed
A folder that a process opened to list it is a listing, not a read. A process
belongs to a test by its command line, else by ORBSIM_READ_RECORD_OWNER or
ORBSIM_TEST_NAME in its environment, else by its parent, as in the spike.

A path is known (decision 823) when it is:
  1. in a `read` or `list` line of the test;
  2. in a part of the test: a file on its command line, or a file that git
     does not ignore in a folder on its command line where the part has it; a
     program on its command line, with its libraries; the cautious set; the
     build tree part; the shaders; the children, each program with its
     libraries, and for a ctest child the CTest files of the tree;
  3. a file in the build tree that a process of the same recording wrote.
Not inputs: a query, with no read, of the working folder of the test or of a
folder on the start path of Python; and each file in test-reads/ and
coverage-records/. Each other path is a miss. A query of a file in a folder
that a `list` line names is written apart, because decision 823 does not give
it, although the hash of the names in the folder holds the answer.

--plant (decision 823): the script compares the tests of the full recording a
second time, with one `read` line removed from a copy of the store: the first
line, by the order of the tests and the paths, of a path that a process of
the test read and that only that line makes known. The run passes only if
that path of that test, and no other, is a new miss.
"""

import collections
import copy
import csv
import datetime
import importlib.util
import json
import os
import pathlib
import re
import sys

csv.field_size_limit(1 << 30)
ROOT = pathlib.Path.cwd()
FOLDER = re.compile(r"(?<!Non-)Directory File")
QUERY_OPS = {"QueryOpen", "QueryBasicInformationFile", "QueryStandardInformationFile",
             "QueryNetworkOpenInformationFile", "QueryAttributeTagFile", "QueryAllInformationFile"}
# The results that answer the question about a path, as in the spike.
FOUND = ("SUCCESS", "NAME NOT FOUND", "PATH NOT FOUND", "NO SUCH FILE", "BUFFER OVERFLOW")
WRITE = ("Generic Write", "Write Data", "Append Data")
TEMPORARY = re.compile(r"(.+)\.tmp[0-9a-f]*")


def load(name: str, path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


DRIVER = load("run_tests", ROOT / "scripts" / "run-tests.py")
norm, under = DRIVER.norm, DRIVER.under


def clock(text: str) -> datetime.time:
    """'8:59:01.1714987 AM' of Process Monitor as a time of day."""
    hms, half = text.split(" ")
    h, m, s = hms.split(":")
    return datetime.time(int(h) % 12 + (12 if half == "PM" else 0), int(m), int(s[:2]))


def command_key(text: str) -> str:
    """A command line in one form for a comparison, as in the spike."""
    return " ".join(text.replace('"', " ").replace("\\", "/").lower().split())


def detail_field(detail: str, key: str) -> str:
    match = re.search(re.escape(key) + r": (.*?)(?:, (?:Command line|Current directory|Environment|Parent PID):|$)",
                      detail, re.S)
    return match.group(1).strip() if match else ""


class Process:
    def __init__(self, pid, parent, command, env, image):
        self.pid, self.parent, self.command, self.env, self.image = pid, parent, command, env, image
        self.owner = None
        self.reads, self.queries, self.listings = set(), set(), set()


def owner_in(env: str, names: set, records: str):
    """The test of a process from its environment: the outermost test of
    ORBSIM_READ_RECORD_OWNER whose folder is the tree's test-reads/, else
    ORBSIM_TEST_NAME. A value ends at a line end or at `; `, the separator of
    Process Monitor."""
    match = re.search(r"(?:^|[\s;])ORBSIM_READ_RECORD_OWNER=(.*?)(?:;\s|[\r\n]|$)", env)
    if match:
        try:
            pairs = json.loads(match.group(1).strip())
            for folder, test in pairs:
                if norm(folder) == records and test in names:
                    return test
        except (ValueError, TypeError):
            pass
    match = re.search(r"(?:^|[\s;])ORBSIM_TEST_NAME=(.*?)(?:;\s|[\r\n]|$)", env)
    return match.group(1).strip() if match and match.group(1).strip() in names else None


def is_folder(key: str, folders: dict) -> bool:
    """Is the path a folder at this time; each path examined one time."""
    if key not in folders:
        folders[key] = os.path.isdir(key)
    return folders[key]


def read_recording(path: pathlib.Path, run, root_key: str):
    """(processes, writes, first time, last time) of one CSV."""
    current, every, writes, folders = {}, [], set(), {}
    first = last = None
    with open(path, encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            when = clock(row["Time of Day"])
            first, last = first or when, when
            pid, op, detail = row["PID"], row["Operation"], row["Detail"]
            if op == "Process Start":
                env = detail.split("Environment:", 1)[1] if "Environment:" in detail else ""
                proc = Process(pid, detail_field(detail, "Parent PID"), detail_field(detail, "Command line"), env,
                               row["Process Name"].lower())
                # The parent is the process that holds the number now, not a
                # later process that Windows gives the same number.
                proc.parent_proc = current.get(proc.parent)
                current[pid] = proc
                every.append(proc)
                continue
            proc = current.get(pid)
            key = norm(row["Path"]) if row["Path"] else ""
            if proc is None or not under(key, root_key):
                continue
            result = row["Result"]
            if op == "CreateFile":
                access = detail.split("Desired Access: ", 1)[-1].split(", Disposition", 1)[0]
                if result == "SUCCESS" and any(w in access for w in WRITE):
                    writes.add(key)
                    # CMake writes a file under a temporary name and renames
                    # it, and the settings of the recording record no rename
                    # (measured 2026-10-29: CMakeCache.txt.tmp9f9bd and
                    # CMakeSystem.cmake.tmp, written with OverwriteIf, then
                    # opened with Delete).
                    renamed = TEMPORARY.fullmatch(key)
                    if renamed:
                        writes.add(renamed.group(1))
                # A folder is not read, also when it is opened with no
                # "Directory File" option; a file that is not there is only
                # a question about it.
                if FOLDER.search(detail) or is_folder(key, folders):
                    if result in FOUND:
                        proc.queries.add(key)
                elif ("Generic Read" in access or "Read Data" in access) and result == "SUCCESS":
                    proc.reads.add(key)
                elif result in FOUND:
                    proc.queries.add(key)
            elif op in QUERY_OPS and result in FOUND:
                proc.queries.add(key)
            elif op == "QueryDirectory":
                head, _, tail = key.rpartition("/")
                proc.listings.add(head if head and ("*" in tail or "?" in tail) else key)
    names = set(run.tests)
    records = norm(run.tree / DRIVER.RECORDS)
    commands = {command_key(" ".join(t["command"])): n for n, t in run.tests.items() if t.get("command")}
    for proc in every:
        proc.owner = commands.get(command_key(proc.command)) or owner_in(proc.env, names, records)
    for proc in every:
        walk, steps = proc, 0
        while walk.owner is None and getattr(walk, "parent_proc", None) is not None and steps < 64:
            walk, steps = walk.parent_proc, steps + 1
        proc.owner = proc.owner or walk.owner
        proc.queries -= proc.listings
        proc.reads -= proc.listings
    return every, writes, first, last


def included(folder: pathlib.Path, names: list, day, first, last) -> set:
    """The tests whose newest read record in `folder` was written between the
    first and the last event of the recording (decision 831). A record is
    `<escaped name>.py-<process>-<start>.txt` or `<escaped name>.trace.json`."""
    by_escape = {DRIVER.escaped_name(name): name for name in names}
    newest = {}
    for path in folder.iterdir():
        match = DRIVER.PYTHON_RECORD.fullmatch(path.name)
        escaped = match.group(1) if match else (path.name[:-len(DRIVER.TRACE)]
                                                 if path.name.endswith(DRIVER.TRACE) else None)
        if escaped in by_escape:
            name = by_escape[escaped]
            newest[name] = max(newest.get(name, 0), path.stat().st_mtime)
    low = datetime.datetime.combine(day, first).timestamp()
    high = datetime.datetime.combine(day, last).timestamp() + 1
    return {name for name, mtime in newest.items() if low <= mtime <= high}


def lines_from_records(run, name: str, folder: pathlib.Path):
    """(reads, listings) of a test from its records in `folder`, as the driver
    reads them (python_records and traces), or None."""
    test = run.tests[name]
    command = test.get("command") or []
    if DRIVER.trace_test(command, run.cmake):
        try:
            text = (folder / (DRIVER.escaped_name(name) + DRIVER.TRACE)).read_bytes().decode("utf-8", "replace")
        except OSError:
            return None
        named = {norm(p) for p in run.arguments(test)} | {norm(command[0])}
        cwd = DRIVER.properties(test).get("WORKING_DIRECTORY") or str(run.tree)
        paths = DRIVER.trace_inputs(text, command, named, cwd, str(run.tree))
        return None if paths is None else (paths[0], paths[1])
    hook = str(run.root.joinpath(*DRIVER.HOOK_FOLDER, DRIVER.HOOK_FILE))
    files = DRIVER.python_record_files(folder, [name]).get(name, [])
    texts = [p.read_bytes().decode("utf-8", errors="surrogateescape") for p in files]
    parsed = DRIVER.python_reads(texts, command, hook)
    children = None if parsed is None else DRIVER.child_rules(parsed[2], str(run.tree), str(run.root))
    if children is None:
        return None
    reads = [p for _, p in parsed[0]] + [p for child in children for p in child["paths"]]
    return reads, [p for _, p in parsed[1]]


def program_files(run, path) -> set:
    return {norm(path)} | {norm(p) for p in run.world.libraries(pathlib.Path(path))}


def ctest_files(run) -> set:
    """The files that Run.ctest_files hashes, by the same walk."""
    found = [p for p in run.tree.rglob("*") if p.name in ("CTestTestfile.cmake", "DartConfiguration.tcl")
             or p.name.endswith(("_include.cmake", "_tests.cmake", "_test-list.cmake"))]
    return {norm(p) for p in found + DRIVER.tree_programs(run.tree)}


def part_files(run, name: str, entry: dict, shared: dict) -> set:
    """Each file that a part of the test holds (decision 823, point 2), by the
    rules of Run.parts."""
    test, parts = run.tests[name], set(entry.get("parts") or {})
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
    if "build tree" in parts:
        found |= shared.setdefault("tree", {norm(run.tree / f) for f in DRIVER.TREE_FILES} | set(run.world.listed()))
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


def start_folders(run, name: str, procs: list) -> set:
    """The working folder of the test, the folder of the hook, and the folder
    of the script of each Python process of the test (decision 823)."""
    test = run.tests[name]
    found = {norm(DRIVER.properties(test).get("WORKING_DIRECTORY") or run.tree),
             norm(run.root.joinpath(*DRIVER.HOOK_FOLDER))}
    for proc in procs:
        for part in re.findall(r'"([^"]+\.py)"|(\S+\.py)\b', proc.command):
            script = part[0] or part[1]
            if os.path.isabs(script):
                found.add(norm(os.path.dirname(script)))
    return found


def compare(run, name, entry, lines, procs, writes, shared) -> dict:
    """The misses of one test, and the queries that only a `list` line of
    their folder makes known."""
    reads, listings = lines
    lined = {norm(p) for p in reads} | {norm(p) for p in listings}
    listed = {norm(p) for p in listings}
    parts = part_files(run, name, entry, shared)
    skip = (norm(run.tree / DRIVER.RECORDS), norm(run.tree / DRIVER.COVERAGE_RECORDS))
    starts = start_folders(run, name, procs)
    tree = norm(run.tree)
    misses, by_folder = [], []
    for proc in procs:
        for kind, paths in (("read", proc.reads), ("query", proc.queries), ("listing", proc.listings)):
            for key in sorted(paths):
                if any(under(key, s) for s in skip) or key in lined or key in parts:
                    continue
                if under(key, tree) and key in writes:
                    continue
                if kind == "query" and key in starts:
                    continue
                if kind == "query" and key.rpartition("/")[0] in listed:
                    by_folder.append([proc.image, key])
                    continue
                misses.append([kind, proc.image, key])
    unique = lambda rows: sorted({tuple(r) for r in rows})  # noqa: E731
    return {"misses": [list(r) for r in unique(misses)], "known by the list line of the folder":
            [list(r) for r in unique(by_folder)], "processes": len(procs)}


def group_of(run, key: str) -> str:
    """A group of misses for the report only, by where the path is and what
    is there at this time. It changes no result."""
    tree, root = norm(run.tree), norm(run.root)
    if under(key, norm(run.root.joinpath(*DRIVER.HOOK_FOLDER))):
        return "the folder of the hook"
    if under(key, norm(run.root / "build" / "SystemResources")):
        return "build/SystemResources"
    there = os.path.exists(key)
    if under(key, tree):
        if not there:
            return ("not there, in the top folder of the tree" if key.rpartition("/")[0] == tree
                    else "not there, in the build tree")
        return "a folder of the build tree" if os.path.isdir(key) else "a file of the build tree"
    if not there:
        return "not there, in the repository"
    return "a folder of the repository" if os.path.isdir(key) else "a file of the repository"


def summary(run, report: dict) -> dict:
    groups = {}
    for name, entry in sorted(report.items()):
        for kind, image, key in entry.get("misses", []):
            group = groups.setdefault(group_of(run, key), {"misses": 0, "tests": set(), "kinds": collections.Counter(),
                                                           "programs": collections.Counter(), "examples": []})
            group["misses"] += 1
            group["tests"].add(name)
            group["kinds"][kind] += 1
            group["programs"][image] += 1
            if len(group["examples"]) < 6:
                group["examples"].append([name, kind, image, key])
    return {g: {"misses": v["misses"], "tests": len(v["tests"]), "kinds": dict(v["kinds"]),
                "programs": dict(v["programs"].most_common()), "examples": v["examples"]}
            for g, v in sorted(groups.items(), key=lambda item: -item[1]["misses"])}


def stored_lines(entry: dict):
    return [p for _, p in entry.get("reads") or []], [p for _, p in entry.get("listings") or []]


def main(argv: list) -> int:
    plant = "--plant" in argv
    args = [a for a in argv[1:] if a != "--plant"]
    tree, store_path, out = (ROOT / args[0]).resolve(), pathlib.Path(args[1]), pathlib.Path(args[2])
    pairs = [(pathlib.Path(args[i]), pathlib.Path(args[i + 1])) for i in range(3, len(args), 2)]
    run = DRIVER.Run(tree, DRIVER.cache_value(tree, "CMAKE_CTEST_COMMAND"), ROOT)
    store = json.loads(store_path.read_text(encoding="utf-8"))["tests"]
    new = sorted(n for n, e in store.items() if n in run.tests and (e.get("trace") or e.get("listings") is not None))
    print(f"tests with the new rules in the store: {len(new)}", flush=True)
    report, done, shared, first_recording = {}, set(), {}, None
    for index, (csv_path, folder) in enumerate(pairs):
        procs, writes, first, last = read_recording(csv_path, run, norm(ROOT))
        by_test = collections.defaultdict(list)
        for proc in procs:
            if proc.owner:
                by_test[proc.owner].append(proc)
        # Process Monitor writes no day; the CSV is written on the day of its
        # recording, at its export.
        day = datetime.date.fromtimestamp(csv_path.stat().st_mtime)
        here = sorted(included(folder, new, day, first, last) - done)
        print(f"{csv_path.name}: events {first} to {last}, {len(procs)} processes, {len(here)} tests included",
              flush=True)
        for name in here:
            lines = stored_lines(store[name]) if index == 0 else lines_from_records(run, name, folder)
            if lines is None:
                report[name] = {"recording": csv_path.name, "not compared": "its records are not complete"}
                continue
            if not by_test.get(name):
                report[name] = {"recording": csv_path.name, "not compared": "no process in the recording"}
                continue
            report[name] = {"recording": csv_path.name,
                            **compare(run, name, store[name], lines, by_test[name], writes, shared)}
        done |= set(here)
        if index == 0:
            first_recording = (here, by_test, writes)
    for name in new:
        report.setdefault(name, {"not compared": "no recording includes it"})
    misses = sum(len(r.get("misses", [])) for r in report.values())
    compared = sum(1 for r in report.values() if "misses" in r)
    print(f"compared: {compared} of {len(new)}; misses: {misses}; tests with a miss: "
          f"{sum(1 for r in report.values() if r.get('misses'))}; queries known by a list line: "
          f"{sum(len(r.get('known by the list line of the folder', [])) for r in report.values())}")
    for name, r in sorted(report.items()):
        if "not compared" in r:
            print(f"  not compared: {name}: {r['not compared']}")
    groups = summary(run, report)
    for group, v in groups.items():
        print(f"  {v['misses']:5} misses in {v['tests']:3} tests: {group}; {v['kinds']}; "
              f"{dict(list(v['programs'].items())[:4])}")
    # write_bytes, not write_text, which writes CRLF on Windows.
    out.write_bytes((json.dumps({"tree": str(tree), "groups": groups, "tests": report}, indent=1) + "\n")
                    .encode("utf-8"))
    if not plant:
        return 0
    return planted(run, store, first_recording, report, shared)


def planted(run, store, first_recording, report, shared) -> int:
    """The planted fault of decision 823."""
    here, by_test, writes = first_recording
    for name in here:
        entry = store[name]
        if "misses" not in report.get(name, {}):
            continue
        parts = part_files(run, name, entry, shared)
        listed = {norm(p) for _, p in entry.get("listings") or []}
        read = sorted({k for proc in by_test[name] for k in proc.reads})
        for at, (stamp, path) in enumerate(entry.get("reads") or []):
            key = norm(path)
            if key in read and key not in parts and key not in listed and key not in writes:
                changed = copy.deepcopy(entry)
                del changed["reads"][at]
                result = compare(run, name, changed, stored_lines(changed), by_test[name], writes, shared)
                new = {tuple(m) for m in result["misses"]} - {tuple(m) for m in report[name]["misses"]}
                # The line made the path known for each kind of access: the
                # read must come back, and no miss of a different path.
                ok = any(m[0] == "read" for m in new) and all(m[2] == key for m in new)
                print(f"planted: the line of {path} removed from {name}: new misses {sorted(new)}")
                print("the planted fault was found, and no other" if ok else
                      "the planted fault was NOT found as the only new miss: the script cannot be trusted")
                return 0 if ok else 1
    print("no line to plant a fault in: the script cannot be trusted")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

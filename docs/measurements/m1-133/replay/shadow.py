"""A measurement of the selection by program with a record of its own (for the
owner's ruling; not a count of the replay).

The selection by program of replay-count.py compares the bytes of each program
with the bytes in the driver's store, and the driver writes a test into its
store only when the test runs by function. Thus a test that the selection by
program chose, but the selection by function did not, keeps the old bytes in
the store, and the selection by program chooses it again at each later commit.
A driver by program would store the new bytes when the test runs.

This script calculates that second count from the files of the replay: a
record of the bytes of each program of each test at its last run by program.
At the base each test has the bytes of the base (the full run ran all tests).
At each commit a test runs by program when it runs by function for a reason
other than its programs (each cause but 1, 2, 3 and 4), or when the bytes of
one of its programs differ from its record, or when a test that it depends on
runs (DEPENDS). Each test that runs gets the bytes of this commit in its
record. It supposes that each test that runs by program only passes; the
function parts of its programs are unchanged. It also gives the time of the
C++ tests, by function and by program, from the same times (decision 757).

    shadow.py <label>...      the commits in sequence, the base before them
"""
import gzip
import importlib.util
import json
import sys as _sys
_sys.dont_write_bytecode = True
import pathlib
import subprocess
import sys

OUT = pathlib.Path(__file__).resolve().parent
TREE = r"C:\Users\U439644\Projects\untitled\build\wt133\build\debug"
CTEST = r"C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\ctest.exe"
STORE = pathlib.Path(TREE) / "run-tests" / "fingerprints.json"


def read_gzip(path):
    return json.loads(gzip.decompress(path.read_bytes()).decode("utf-8"))


def load_counter():
    """replay-count.py, for its base times, its first-run times and its rule
    of a C++ test."""
    loader = importlib.util.spec_from_file_location("replay_count", OUT / "replay-count.py")
    module = importlib.util.module_from_spec(loader)
    loader.loader.exec_module(module)
    return module


def depends_of() -> tuple:
    """({test: DEPENDS}, C++ tests, GPU tests) from CTest's listing now."""
    listing = subprocess.run([CTEST, "--show-only=json-v1"], cwd=TREE, capture_output=True, text=True,
                             encoding="utf-8", errors="replace", check=True).stdout
    counter = load_counter()
    rt = counter.load_driver()
    found, cpp, gpu = {}, set(), set()
    for test in json.loads(listing)["tests"]:
        values = {p["name"]: p["value"] for p in test.get("properties", [])}
        found[test["name"]] = values.get("DEPENDS", [])
        if counter.is_cpp_test(rt, test["name"], test):
            cpp.add(test["name"])
        if "gpu" in values.get("LABELS", []):
            gpu.add(test["name"])
    return found, cpp, gpu


def with_dependents(chosen: set, depends: dict) -> set:
    chosen = set(chosen)
    grew = True
    while grew:
        grew = False
        for name, wanted in depends.items():
            if name not in chosen and any(d in chosen for d in wanted):
                chosen.add(name)
                grew = True
    return chosen


def main() -> int:
    labels = sys.argv[1:]
    store = json.loads(STORE.read_text(encoding="utf-8"))
    # The programs of each test, from the store as it is now, as numbers into
    # its tables; only the keys of the programs are used.
    programs = {name: sorted((entry.get("programs") or {}).keys()) for name, entry in store["tests"].items()}
    depends, cpp, gpu = depends_of()
    counter = load_counter()
    times = counter.base_times()
    first = counter.first_times(labels)
    base = read_gzip(OUT / "base" / "digests-after.json.gz")
    record = {}
    for name, keys in programs.items():
        for key in keys:
            if key in base:
                record[(name, key)] = base[key]["bytes"]
    rows = []
    for label in labels:
        result = json.loads((OUT / label / "count.json").read_text(encoding="utf-8"))
        now = read_gzip(OUT / label / "digests-before.json.gz")
        function = result["function"]
        # The tests that CTest listed at this commit: those the driver selected and skipped.
        listed = set()
        for name in ("driver-selected.txt", "driver-skipped.txt"):
            listed |= {line for line in (OUT / label / name).read_text(encoding="utf-8").splitlines() if line}
        own = set()
        for name, causes in function["causes"].items():
            kinds = {c for c in causes if c != "note"}
            if kinds - {"1", "2", "3", "4", "1/2: another file of tests/", "1/2: no source found",
                        "1/2: the functions without a flag changed"}:
                own.add(name)
        moved = set()
        for name, keys in programs.items():
            if name not in listed:
                continue
            for key in keys:
                current = (now.get(key) or {}).get("bytes")
                if current is None:
                    continue
                if (name, key) not in record:
                    record[(name, key)] = current  # a test or a program that is new: its first run
                    continue
                if current != record[(name, key)]:
                    moved.add(name)
        chosen = with_dependents((own | moved | set(function["selected"])) & listed,
                                 {n: d for n, d in depends.items() if n in listed})
        for name in chosen:
            for key in programs.get(name, []):
                current = (now.get(key) or {}).get("bytes")
                if current is not None:
                    record[(name, key)] = current
        def seconds_of(names) -> float:
            return round(sum(times.get(n, first.get(n, (0.0, None))[0]) for n in names), 2)

        by_function = set(function["selected"])
        # The time of the C++ tests, by function and by program, is the
        # figure that compares with the spike, which counted the C++ tests only
        # (decision 757).
        rows.append({"label": label, "by function": function["all"], "by program, the driver's store":
                     result["program"]["all"], "by program, a record of its own": len(chosen),
                     "own record: C++": len(chosen & cpp), "own record: GPU": len(chosen & gpu),
                     "own record: by program only": len(chosen - by_function),
                     "own record: seconds": seconds_of(chosen),
                     "own record: C++ seconds": seconds_of(chosen & cpp),
                     "by function: C++": len(by_function & cpp),
                     "by function: seconds": seconds_of(by_function),
                     "by function: C++ seconds": seconds_of(by_function & cpp)})
        print(rows[-1])
    (OUT / "shadow.json").write_bytes(json.dumps(rows, indent=1).encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

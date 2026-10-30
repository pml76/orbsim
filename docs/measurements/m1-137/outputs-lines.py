#!/usr/bin/env python3
"""The read and list lines of the build tree in the store of a tree.

    outputs-lines.py <build tree>

M1-137 item 2, register decisions 846, 847 and 872-877; the results are in
docs/measurements/m1-137-outputs.md. The script reads the store of the
driver, <tree>/run-tests/fingerprints.json, through scripts/run-tests.py, and
gives:

    lines       the read and list lines of the build tree of the tests with
                complete records, by mark, and the paths that they name
    counted     the lines that the new hash of decision 846 examines, from
                the function counted_line of the driver; the time of a new
                hash of them; and each test with a line that is not as the
                store holds it, from the function changed_lines of the
                driver, which compares as the driver compares after a run
    no record   the tests with no complete record, and of these the tests
                that name scripts/mutants-due.py, which get the store as a
                part (decision 875)

The tree is only read. Run with no build and no run of the tests at the same
time. The top folder of the tree holds `.ninja_lock` only while Ninja runs,
thus a list line of it that a run of `check` wrote is not as the folder is
outside Ninja.
"""
import collections
import importlib.util
import pathlib
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[3]


def driver():
    """scripts/run-tests.py as a module; its name has a `-`, thus no import."""
    spec = importlib.util.spec_from_file_location("run_tests", ROOT / "scripts" / "run-tests.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["run_tests"] = module
    spec.loader.exec_module(module)
    return module


def main(tree: pathlib.Path) -> int:
    rt = driver()
    tree = tree.resolve()
    key = rt.norm(tree)
    stored = rt.load_store(tree / rt.STATE)["tests"]
    complete = {name: entry for name, entry in stored.items() if entry.get("reads") is not None}
    lines = [(name, kind, stamp, path) for name, entry in complete.items()
             for kind, group in (("read", entry.get("reads") or []), ("list", entry.get("listings") or []))
             for stamp, path in group if rt.under(rt.norm(path), key)]
    marks = collections.Counter(stamp if stamp in (rt.MISSING, rt.UNREADABLE) else kind
                                for _, kind, stamp, _ in lines)
    print(f"{tree.name}: {len(stored)} tests stored, {len(complete)} with complete records; "
          f"{len({line[0] for line in lines})} tests with {len(lines)} lines of the build tree, for "
          f"{len({rt.norm(line[3]) for line in lines})} paths; by mark: {dict(sorted(marks.items()))}")
    counted = [line for line in lines if rt.counted_line(line[3], tree)]
    world = rt.World(tree, ROOT)
    began = time.monotonic()
    changed = rt.changed_lines(complete, tree, lambda kind, path, by_hook: world.hook_line(kind, path) if by_hook
                               else ("read", world.file(path)))
    seconds = time.monotonic() - began
    print(f"  counted: {len(counted)} lines of {len({line[0] for line in counted})} tests, hashed again in "
          f"{seconds:.1f} s; tests with a line that is not as stored: {len(changed)}")
    for name, paths in changed.items():
        print(f"    {name}: {', '.join(rt.norm(path)[len(key):] or '/' for path in paths)}")
    reader = rt.norm(ROOT.joinpath(*rt.STORE_READER))
    no_record = sorted(name for name, entry in stored.items() if entry.get("reads") is None)
    print(f"  no complete record: {len(no_record)} tests: {', '.join(no_record)}")
    listing = rt.subprocess.run([rt.cache_value(tree, "CMAKE_CTEST_COMMAND") or "ctest", "--show-only=json-v1"],
                                cwd=tree, capture_output=True, text=True, encoding="utf-8", errors="replace",
                                check=True).stdout
    tests = {test["name"]: test for test in rt.json.loads(listing)["tests"]}
    readers = sorted(name for name, test in tests.items()
                     if any(rt.norm(argument) == reader for argument in test.get("command") or []))
    print(f"  name scripts/mutants-due.py: {len(readers)} tests; of these with no complete record: "
          f"{', '.join(name for name in readers if name in no_record) or 'none'}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(pathlib.Path(sys.argv[1])))

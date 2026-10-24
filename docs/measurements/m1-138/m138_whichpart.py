"""Read only: for each test of a tree, which parts of its fingerprint differ
from the stored ones, computed as run_tree() in scripts/run-tests.py does.

    python m138_whichpart.py <repository root> <tree>
"""
import collections
import importlib.util
import json
import os
import pathlib
import sys

sys.dont_write_bytecode = True
root, tree = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
spec = importlib.util.spec_from_file_location("run_tests", root / "scripts" / "run-tests.py")
rt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rt)
run = rt.Run(tree, rt.cache_value(tree, "CMAKE_CTEST_COMMAND"), root)
stored = rt.load_store(run.state)["tests"]
run.learn_all_imports()
env = rt.without_test_name({**os.environ, rt.RECORDS_VARIABLE: str(tree / rt.COVERAGE_RECORDS)})
moved = collections.Counter()
examples = {}
for name, test in run.tests.items():
    entry = stored.get(name)
    if entry is None:
        moved["(not stored)"] += 1
        continue
    parts = run.parts(test, entry.get("reads"), env, entry.get("shaders"), set(entry.get("programs") or ()))
    old = entry.get("parts", {})
    for key in sorted(set(parts) | set(old)):
        if parts.get(key) != old.get(key):
            moved[key] += 1
            examples.setdefault(key, name)
print(f"tests {len(run.tests)}, stored {len(stored)}")
for key, n in moved.most_common():
    print(f"  part '{key}' differs in {n} tests, for example '{examples.get(key, '')}'")
kept = {k: v for k, v in env.items()
        if k.upper() == "PATH" or k.upper().startswith(("VK_", "ORBSIM_"))}
print("environment part, as this shell gives it:")
for k in sorted(kept):
    print(f"  {k} = {kept[k][:300]}")

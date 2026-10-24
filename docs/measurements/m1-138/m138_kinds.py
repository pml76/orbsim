"""How the 102 script tests start, read only: the first program of each, the
CMake script of a `cmake -P` test, and the Python scripts on its command line.

    python m138_kinds.py <repository root> <tree>
"""
import collections
import importlib.util
import pathlib
import sys
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
root = pathlib.Path(sys.argv[1])
tree = pathlib.Path(sys.argv[2])
spec = importlib.util.spec_from_file_location("run_tests", root / "scripts" / "run-tests.py")
rt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rt)
run = rt.Run(tree, rt.cache_value(tree, "CMAKE_CTEST_COMMAND"), root)
times = {c.get("name"): float(c.get("time") or 0)
         for c in ET.parse(tree / "run-tests" / "results.xml").getroot().iter("testcase")}
ntree = rt.norm(tree)
kinds = collections.defaultdict(list)
for name, test in run.tests.items():
    command = test.get("command", [])
    if command and rt.under(rt.norm(command[0]), ntree) and rt.is_program(pathlib.Path(command[0])):
        continue
    first = pathlib.Path(command[0]).name.lower() if command else "(none)"
    if first.startswith("cmake"):
        script = next((command[i + 1] for i, a in enumerate(command[:-1]) if a == "-P"), None)
        what = "cmake -P " + (pathlib.Path(script).name if script else "?") if script else "cmake " + " ".join(command[1:3])
    elif first.startswith("python"):
        py = [pathlib.Path(a).name for a in command[1:] if a.endswith(".py")]
        what = "python " + (py[0] if py else "?")
    else:
        what = first
    kinds[what].append(name)
total = 0
for what, names in sorted(kinds.items(), key=lambda kv: -len(kv[1])):
    t = sum(times.get(n, 0) for n in names)
    total += len(names)
    gpu = sum(1 for n in names if rt.GPU_LABEL in rt.labels(run.tests[n]))
    print(f"{len(names):3d}  gpu {gpu:2d}  {t:7.1f} s  {what}")
print(f"total {total}")

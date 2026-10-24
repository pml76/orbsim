"""Which script tests the 30 dependents of M1-138's measurement 4 depend on:
through Python or through `cmake -P`.

    python m138_dependents.py <repository root> <tree>
"""
import importlib.util
import pathlib
import sys
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
root, tree = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
spec = importlib.util.spec_from_file_location("run_tests", root / "scripts" / "run-tests.py")
rt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rt)
run = rt.Run(tree, rt.cache_value(tree, "CMAKE_CTEST_COMMAND"), root)
times = {c.get("name"): float(c.get("time") or 0)
         for c in ET.parse(tree / "run-tests" / "results.xml").getroot().iter("testcase")}
ntree = rt.norm(tree)


def kind(test):
    command = test.get("command", [])
    if command and rt.under(rt.norm(command[0]), ntree) and rt.is_program(pathlib.Path(command[0])):
        return None
    return "python" if pathlib.Path(command[0]).name.lower().startswith("python") else "cmake"


python = {n for n, t in run.tests.items() if kind(t) == "python"}
cmake = {n for n, t in run.tests.items() if kind(t) == "cmake"}
from_python = rt.with_dependents(python, run.tests) - python - cmake
from_cmake = rt.with_dependents(cmake, run.tests) - python - cmake
print(f"python {len(python)}, cmake {len(cmake)}")
print(f"dependents through python tests: {len(from_python)}, {sum(times[n] for n in from_python):.1f} s")
print(f"dependents through cmake tests: {len(from_cmake)}, {sum(times[n] for n in from_cmake):.1f} s")
print(f"in both: {len(from_python & from_cmake)}")
print(f"cmake tests and their dependents: {len(cmake | from_cmake)}, "
      f"{sum(times[n] for n in cmake | from_cmake):.1f} s")
print(f"python tests and their dependents: {len(python | from_python)}, "
      f"{sum(times[n] for n in python | from_python):.1f} s")

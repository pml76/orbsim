"""Re-check the counts of M1-138's task document, read only, with the
driver's own functions (scripts/run-tests.py), so that the grouping is the
driver's and not a copy of it.

    python m138_counts.py <repository root> <tree>...
"""
import importlib.util
import json
import pathlib
import sys
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
root = pathlib.Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("run_tests", root / "scripts" / "run-tests.py")
rt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rt)

for tree_arg in sys.argv[2:]:
    tree = pathlib.Path(tree_arg)
    ctest = rt.cache_value(tree, "CMAKE_CTEST_COMMAND")
    run = rt.Run(tree, ctest, root)
    tests = run.tests
    store = rt.load_store(run.state)["tests"]
    times = {}
    for case in ET.parse(tree / "run-tests" / "results.xml").getroot().iter("testcase"):
        times[case.get("name")] = float(case.get("time") or 0)
    ntree, nroot = rt.norm(tree), rt.norm(root)
    script, gpu, names_tree, names_root_dir, docs_dirs = set(), set(), set(), set(), {}
    for name, test in tests.items():
        command = test.get("command", [])
        built = bool(command) and rt.under(rt.norm(command[0]), ntree) and rt.is_program(pathlib.Path(command[0]))
        if not built:
            script.add(name)
        if rt.GPU_LABEL in rt.labels(test):
            gpu.add(name)
        for path in run.arguments(test):
            key = rt.norm(path)
            if key == ntree:
                names_tree.add(name)
            elif rt.under(key, nroot) and not rt.under(key, ntree) and path.is_dir():
                names_root_dir.add(name)
                docs_dirs.setdefault(name, []).append(key[len(nroot):] or "/")
    docs = rt.norm(root / "docs")
    reads_docs = {n for n, dirs in docs_dirs.items()
                  if any(d == "/" or rt.under(rt.norm(root) + d, docs) or rt.under(docs, rt.norm(root) + d)
                         for d in dirs)}
    not_stored = set(tests) - set(store)
    doc_change = rt.with_dependents(names_tree | reads_docs | not_stored, tests)
    script_change = rt.with_dependents(script | not_stored, tests)
    print(f"== {tree}")
    print(f"tests listed: {len(tests)}; stored: {len(store)}; not stored: {len(not_stored)}")
    print(f"not stored: {sorted(not_stored)}")
    print(f"script tests (first command not a program of the tree): {len(script)}, gpu among them: {len(script & gpu)}")
    dep = rt.with_dependents(script, tests) - script
    print(f"their dependents by DEPENDS: {len(dep)}, gpu among them: {len(dep & gpu)}")
    both = script | dep
    print(f"a change in scripts/: {len(both)} tests, gpu {len(both & gpu)}, "
          f"time {sum(times.get(n, 0) for n in both):.1f} s of {sum(times.values()):.1f} s "
          f"(results.xml of the last run, {len(times)} cases)")
    print(f"name the build tree: {len(names_tree)}; name a folder of the repository: {len(names_root_dir)}")
    for n in sorted(names_root_dir):
        print(f"   {n}: {docs_dirs[n]}")
    print(f"a change to a document: {len(doc_change)} tests, script tests among them {len(doc_change & script)}, "
          f"time {sum(times.get(n, 0) for n in doc_change):.2f} s")
    for n in sorted(doc_change, key=lambda n: -times.get(n, 0)):
        why = [w for w, s in (("tree", names_tree), ("folder", reads_docs), ("not stored", not_stored)) if n in s]
        print(f"   {times.get(n, 0):7.2f} s  {n}  {why or ['dependent']}")

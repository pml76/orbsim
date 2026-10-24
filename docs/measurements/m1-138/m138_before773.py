"""M1-138, decision 773: the "before" figures of a change to a record file.

One empty line is added at the end of scripts/mutation-passes.json, after a
copy is kept here. `check` runs in Debug, then in RelWithDebInfo. The copy
goes back, byte for byte, in a `finally`, and `check` runs again in the two
trees. Each run gives: its exit code, the tests that ran and were skipped,
CTest's real time, the summed time of the tests from results.xml, the test
step from Ninja's log, and the time of the whole `check`.

    python m138_before773.py <repository root> <scratch folder>
"""
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ElementTree

ROOT = pathlib.Path(sys.argv[1])
SCRATCH = pathlib.Path(sys.argv[2])
CMAKE = r"C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe"
RECORD = ROOT / "scripts" / "mutation-passes.json"
COPY = SCRATCH / "mutation-passes.json.copy"
TREES = ("debug", "relwithdebinfo")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_check(tree_name, label):
    tree = ROOT / "build" / tree_name
    log = SCRATCH / f"check-{label}-{tree_name}.log"
    began = time.monotonic()
    with open(log, "wb") as out:
        code = subprocess.run([CMAKE, "--build", str(tree), "--target", "check"], stdout=out,
                              stderr=subprocess.STDOUT, check=False).returncode
    whole = time.monotonic() - began
    text = log.read_text(encoding="utf-8", errors="replace")
    ran = re.search(r"run-tests: (\d+) ran, (\d+) skipped", text)
    real = re.search(r"Total Test time \(real\) =\s*([\d.]+) sec", text)
    summed = None
    try:
        cases = list(ElementTree.parse(tree / "run-tests" / "results.xml").getroot().iter("testcase"))
        summed = round(sum(float(c.get("time") or 0) for c in cases), 2)
    except (OSError, ElementTree.ParseError):
        cases = []
    step = None
    for line in (tree / ".ninja_log").read_text(encoding="utf-8", errors="replace").splitlines():
        fields = line.split("\t")
        if len(fields) >= 4 and fields[3] == "CMakeFiles/check":
            step = (int(fields[1]) - int(fields[0])) / 1000.0
    result = {"label": label, "tree": tree_name, "exit": code, "whole_s": round(whole, 1),
              "ran": int(ran.group(1)) if ran else None, "skipped": int(ran.group(2)) if ran else None,
              "ctest_real_s": float(real.group(1)) if real else None, "summed_s": summed,
              "cases_in_report": len(cases), "test_step_s": step}
    print(json.dumps(result), flush=True)
    with open(SCRATCH / "before773.jsonl", "a", encoding="utf-8") as out:
        out.write(json.dumps(result) + "\n")
    return code


def main():
    status = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True,
                            check=True).stdout
    if status.strip():
        print(f"refused: the working tree is not clean:\n{status}")
        return 1
    original = sha(RECORD)
    shutil.copyfile(RECORD, COPY)
    if sha(COPY) != original:
        print("refused: the copy differs from the record")
        return 1
    print(f"record {original}, copy kept at {COPY}", flush=True)
    passed = True
    try:
        with open(RECORD, "ab") as out:
            out.write(b"\n")
        print(f"changed record {sha(RECORD)}", flush=True)
        for tree in TREES:
            if run_check(tree, "changed") != 0:
                passed = False
                break
    finally:
        shutil.copyfile(COPY, RECORD)
        restored = sha(RECORD)
        print(f"restored record {restored}, same as before: {restored == original}", flush=True)
        if restored != original:
            return 1
    if not passed:
        print("a check failed with the changed record; the runs after the restore do not start")
        return 1
    for tree in TREES:
        if run_check(tree, "restored") != 0:
            return 1
    left = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True,
                          check=True).stdout
    print(f"working tree after the runs: {'clean' if not left.strip() else left}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

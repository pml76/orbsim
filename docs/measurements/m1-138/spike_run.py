"""M1-138, the spike of decision 766: measurement only.

    spike_run.py full  <tree> <out folder>   all tests, with the hook, while
                                             Process Monitor records
    spike_run.py trace <tree> <out folder>   each `cmake -P` test, one at a
                                             time, without and then with
                                             CMake's trace (decision 768)

`full` runs CTest itself over every test of the tree, with the job count that
`check` gives the driver, PYTHONPATH on the hook, and the coverage records in
the out folder, as the driver gives them. It changes no tracked file and does
not touch the driver's store. `trace` reads each test's command, folder and
environment from `ctest --show-only=json-v1`, and gives the trace arguments
right after the cmake program."""
import json
import os
import pathlib
import subprocess
import sys
import time
import xml.etree.ElementTree as ElementTree

ROOT = pathlib.Path(r"C:\Users\U439644\Projects\untitled")
HOOK = pathlib.Path(__file__).resolve().parent / "hook"
TRACE = ["--trace-expand", "--trace-format=json-v1"]
JOBS = "10"  # what build.ninja gives run-tests.py --jobs in both trees, 2026-10-24


def cache(tree, name):
    for line in (tree / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith(name + ":"):
            return line.split("=", 1)[1]
    return None


def clean():
    out = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True,
                         check=True).stdout
    return out.strip() == ""


def base_env(out):
    env = {k: v for k, v in os.environ.items()
           if k.upper() not in ("ORBSIM_TEST_NAME", "M1138_SPIKE_OWNER", "PYTHONPATH")}
    env["PYTHONPATH"] = str(HOOK)
    env["M1138_SPIKE_LOGS"] = str(out / "logs")
    env["ORBSIM_COVERAGE_RECORDS"] = str(out / "coverage-records")
    return env


# The short run of the `probe` phase, which tests procmon_analyse.py on a
# real recording before the full runs: Python with no child (lint_deps,
# test_reads), Python that loads a script from its .pyc file
# (mutants_due_self_test), Python with git (doc_links_self_test) and with
# ctest (accept_golden) as a child, and `cmake -P` with a program of the tree
# (probe_clear) and with CMake only (horizons_fixture_checksums).
PROBE_TESTS = ("^(lint_deps|test_reads|mutants_due_self_test|doc_links_self_test|accept_golden|probe_clear"
               "|horizons_fixture_checksums)$")


def full(tree, out, select=None):
    ctest = cache(tree, "CMAKE_CTEST_COMMAND")
    (out / "logs").mkdir(parents=True, exist_ok=True)
    (out / "coverage-records").mkdir(parents=True, exist_ok=True)
    began = time.monotonic()
    code = subprocess.run([ctest, "--test-dir", str(tree), "-j", JOBS, "--output-on-failure",
                           *(["-R", select] if select else []),
                           "--output-junit", str(out / "results.xml")], env=base_env(out),
                          stdout=open(out / "ctest.log", "wb"), stderr=subprocess.STDOUT, check=False).returncode
    took = time.monotonic() - began
    cases = list(ElementTree.parse(out / "results.xml").getroot().iter("testcase"))
    failed = [c.get("name") for c in cases if c.find("failure") is not None or c.find("error") is not None]
    print(json.dumps({"phase": "full", "tree": str(tree), "exit": code, "seconds": round(took, 1),
                      "cases": len(cases), "failed": failed,
                      "logs": len(list((out / "logs").glob("*.log")))}))
    return code


def verdict(props, code, text):
    """CTest's verdict on one run, from the properties that change it."""
    import re
    skip = props.get("SKIP_RETURN_CODE")
    if skip is not None and code == int(skip):
        return "skipped"
    if any(re.search(p, text) for p in props.get("SKIP_REGULAR_EXPRESSION", [])):
        return "skipped"
    if any(re.search(p, text) for p in props.get("FAIL_REGULAR_EXPRESSION", [])):
        return "failed"
    if props.get("PASS_REGULAR_EXPRESSION"):
        ok = any(re.search(p, text) for p in props["PASS_REGULAR_EXPRESSION"])
    else:
        ok = code == 0
    if props.get("WILL_FAIL"):
        ok = not ok
    return "passed" if ok else "failed"


def trace(tree, out):
    ctest = cache(tree, "CMAKE_CTEST_COMMAND")
    listing = json.loads(subprocess.run([ctest, "--show-only=json-v1"], cwd=tree, capture_output=True,
                                        text=True, check=True).stdout)
    folder = out / "trace"
    folder.mkdir(parents=True, exist_ok=True)
    rows = []
    for test in listing["tests"]:
        command = test.get("command") or []
        if not command or not pathlib.Path(command[0]).name.lower().startswith("cmake") or "-P" not in command:
            continue
        props = {p["name"]: p["value"] for p in test.get("properties", [])}
        env = {k: v for k, v in os.environ.items() if k.upper() not in ("ORBSIM_TEST_NAME", "PYTHONPATH")}
        for item in props.get("ENVIRONMENT", []):
            key, _, value = item.partition("=")
            env[key] = value
        cwd = props.get("WORKING_DIRECTORY", str(tree))
        timeout = float(props.get("TIMEOUT", 1500))
        name = test["name"]
        safe = "".join(c if c.isalnum() or c in "-_." else "_" for c in name)
        row = {"test": name}
        for label, extra in (("plain", []), ("traced", TRACE + [f"--trace-redirect={folder / (safe + '.json')}"])):
            began = time.monotonic()
            done = subprocess.run([command[0], *extra, *command[1:]], cwd=cwd, env=env, capture_output=True,
                                  timeout=timeout, check=False)
            text = (done.stdout + done.stderr).decode("utf-8", "replace")
            row[label] = {"exit": done.returncode, "seconds": round(time.monotonic() - began, 3),
                          "verdict": verdict(props, done.returncode, text)}
            (folder / f"{safe}.{label}.out").write_bytes(done.stdout + b"\n--- stderr\n" + done.stderr)
        rows.append(row)
        print(json.dumps(row), flush=True)
    (out / "trace.json").write_text(json.dumps(rows, indent=1), encoding="utf-8")
    same = sum(1 for r in rows if r["plain"]["verdict"] == r["traced"]["verdict"] != "failed")
    plain = sum(r["plain"]["seconds"] for r in rows)
    traced = sum(r["traced"]["seconds"] for r in rows)
    print(json.dumps({"phase": "trace", "tests": len(rows), "both_passed": same, "plain_s": round(plain, 1),
                      "traced_s": round(traced, 1)}))
    return 0 if same == len(rows) else 1


def main():
    phase, tree, out = sys.argv[1], pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
    if not clean():
        print("refused: the working tree is not clean")
        return 1
    out.mkdir(parents=True, exist_ok=True)
    if phase == "full":
        code = full(tree, out)
    elif phase == "probe":
        code = full(tree, out, PROBE_TESTS)
    else:
        code = trace(tree, out)
    if not clean():
        print("the working tree changed during the run")
        return 1
    return code


if __name__ == "__main__":
    sys.exit(main())

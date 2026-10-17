"""Scratch measurement, not part of the repository: M1-129's before and after
(register decision 607). For one source root and one build tree, in order:

  warm      check, so the tree starts from a passing check
  nothing   check again, nothing changed
  one-file  a comment line added at the top of src/orbit/Orbit.cpp, then check
  revert    the line taken out again (the original bytes), then check
  header    the same at the top of src/core/Units.hpp, then check
  revert    taken out again, then check

Each run: wall time, CTest's own "Total Test time (real)", the tests run and
skipped (the driver's line, after M1-129), and Ninja's compile and lint steps
counted from its output. Results appended to timings.jsonl beside this script.

    timings.py <label> <source root> <build tree>
"""
import datetime
import json
import pathlib
import re
import subprocess
import sys
import time

CMAKE = r"C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe"
HERE = pathlib.Path(__file__).resolve().parent
COMMENT = b"// M1-129's before-and-after measurement; this line is taken out again.\n"


def check(label: str, scenario: str, root: pathlib.Path, tree: pathlib.Path) -> dict:
    began = time.monotonic()
    done = subprocess.run([CMAKE, "--build", str(tree), "--target", "check"], cwd=root, capture_output=True,
                          text=True, encoding="utf-8", errors="replace")
    wall = time.monotonic() - began
    out = done.stdout + done.stderr
    test_time = re.findall(r"Total Test time \(real\) =\s*([\d.]+) sec", out)
    driver = re.findall(r"run-tests: (\d+) ran, (\d+) skipped", out)
    ctest_summary = re.findall(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", out)
    result = {"label": label, "scenario": scenario, "exit": done.returncode, "wall_s": round(wall, 1),
              "test_s": float(test_time[-1]) if test_time else None,
              "ran": int(driver[-1][0]) if driver else (int(ctest_summary[-1][2]) if ctest_summary else None),
              "skipped": int(driver[-1][1]) if driver else 0,
              "compiles": len(re.findall(r"\] Building (?:CXX|C) object", out)),
              "links": len(re.findall(r"\] Linking ", out)),
              "lints": len(re.findall(r"\] clang-tidy ", out)),
              "when": datetime.datetime.now().isoformat(timespec="seconds")}
    (HERE / f"timings-{label}-{scenario}.log").write_text(out, encoding="utf-8")
    with open(HERE / "timings.jsonl", "a", encoding="utf-8") as stream:
        stream.write(json.dumps(result) + "\n")
    print(json.dumps(result), flush=True)
    if done.returncode != 0:
        sys.exit(f"check failed in {tree} at {scenario}; the log is {HERE / f'timings-{label}-{scenario}.log'}")
    return result


def edited(label: str, scenario: str, root: pathlib.Path, tree: pathlib.Path, relative: str) -> None:
    path = root / relative
    original = path.read_bytes()
    path.write_bytes(COMMENT + original)
    try:
        check(label, scenario, root, tree)
    finally:
        path.write_bytes(original)
    check(label, f"{scenario}-revert", root, tree)


def main() -> int:
    label, root, tree = sys.argv[1], pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
    check(label, "warm", root, tree)
    check(label, "nothing", root, tree)
    edited(label, "one-file", root, tree, "src/orbit/Orbit.cpp")
    edited(label, "header", root, tree, "src/core/Units.hpp")
    return 0


if __name__ == "__main__":
    sys.exit(main())

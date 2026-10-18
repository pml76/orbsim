"""M1-131 item 4: a tree's full build time, then one `check`.

    python -I measure-build.py <tree> <label>

<tree> is the build tree's absolute path; <label> names the files this writes
beside this script (e.g. "rel-nohook"). It runs, in this order and nothing
else:

    C:\\Strawberry\\c\\bin\\ninja.exe -C <tree> -t clean
    <CLion cmake> --build <tree>                     (timed: the full build)
    <CLion cmake> --build <tree> --target check      (timed, once)

and keeps a copy of <tree>/.ninja_log before the build, after it and after
the check, so that ninja-log-analysis.py can read exactly this build's entries.
"""

import datetime
import hashlib
import shutil
import subprocess
import sys
import time
from pathlib import Path

NINJA = r"C:\Strawberry\c\bin\ninja.exe"
CMAKE = r"C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe"
HERE = Path(__file__).resolve().parent


def now() -> str:
    return datetime.datetime.now().astimezone().isoformat(timespec="seconds")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(label: str, step: str, command: list) -> tuple:
    """Run one command, its output to <label>-<step>.log; (exit code, seconds)."""
    log = HERE / f"{label}-{step}.log"
    print(f"[{now()}] {step}: {subprocess.list2cmdline(command)}", flush=True)
    start = time.perf_counter()
    with log.open("w", encoding="utf-8", errors="replace") as out:
        done = subprocess.run(command, stdout=out, stderr=subprocess.STDOUT, check=False)
    seconds = time.perf_counter() - start
    print(f"[{now()}] {step}: exit {done.returncode}, {seconds:.1f} s wall clock, output in {log.name}",
          flush=True)
    return done.returncode, seconds


def keep_log(tree: Path, label: str, when: str) -> None:
    copy = HERE / f"{label}-ninja_log-{when}.txt"
    shutil.copy2(tree / ".ninja_log", copy)
    print(f"[{now()}] .ninja_log {when}: {copy.stat().st_size} bytes, sha256 {digest(copy)[:16]}", flush=True)


def main(argv: list) -> int:
    tree, label = Path(argv[1]), argv[2]
    print(f"tree {tree}, label {label}")
    code, _ = run(label, "clean", [NINJA, "-C", str(tree), "-t", "clean"])
    if code != 0:
        return code
    keep_log(tree, label, "before-build")
    code, _ = run(label, "build", [CMAKE, "--build", str(tree)])
    keep_log(tree, label, "after-build")
    if code != 0:
        return code
    code, _ = run(label, "check", [CMAKE, "--build", str(tree), "--target", "check"])
    keep_log(tree, label, "after-check")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))

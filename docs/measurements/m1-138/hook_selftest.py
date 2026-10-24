"""The spike's hook, seen to record each kind of access before it is trusted
(VERIFICATION.md rule 23). It starts a Python process with the hook, which
does each access one time, and then requires the line of each access in that
process's log. --plant <kind> removes the lines of one kind from the log
before the comparison, so that the comparison can be seen to fail.

    python hook_selftest.py <scratch folder> [--plant <kind>]
"""
import os
import pathlib
import subprocess
import sys

scratch = pathlib.Path(sys.argv[1]).resolve()
plant = sys.argv[3] if len(sys.argv) == 4 and sys.argv[2] == "--plant" else None
work = scratch / "hook-selftest"
logs = work / "logs"
work.mkdir(exist_ok=True)
logs.mkdir(exist_ok=True)
for old in logs.glob("*.log"):
    old.unlink()
(work / "data.txt").write_text("x\n", encoding="utf-8")
(work / "helper-mod.py").write_text("VALUE = 1\n", encoding="utf-8")
subprocess.run([sys.executable, "-c", f"import py_compile; py_compile.compile(r'{work / 'helper-mod.py'}')"],
               check=True, env={k: v for k, v in os.environ.items() if k not in ("PYTHONPATH",)})
probe = work / "probe.py"
probe.write_text(f'''
import glob, importlib.util, os, pathlib, subprocess, sys
w = pathlib.Path(r"{work}")
d = w / "data.txt"
m = w / "missing.txt"
open(d, encoding="utf-8").read()
d.read_bytes()
os.close(os.open(d, os.O_RDONLY))
os.stat(d); os.lstat(d); os.access(d, os.R_OK)
os.path.exists(d); os.path.exists(m); os.path.isfile(d); os.path.isdir(w); os.path.lexists(d)
d.exists(); m.exists(); d.is_file(); w.is_dir(); d.stat()
os.path.getsize(d)
os.listdir(w); list(os.scandir(w)); list(os.walk(w)); glob.glob(str(w / "*.txt")); list(w.iterdir())
list(w.glob("*.txt")); list(w.rglob("*.py"))
spec = importlib.util.spec_from_file_location("helper_mod", w / "helper-mod.py")
mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
subprocess.run([sys.executable, "-c", "open(" + repr(str(d)) + ").read()"], check=True)
env = dict(os.environ); env["ORBSIM_TEST_NAME"] = "a different test"
subprocess.run([sys.executable, "-c", "pass"], check=True, env=env)
env = {{k: v for k, v in os.environ.items() if k != "ORBSIM_TEST_NAME"}}
subprocess.run([sys.executable, "-c", "pass"], check=True, env=env)
subprocess.run(["git", "--version"], check=True, capture_output=True)
''', encoding="utf-8")
env = dict(os.environ, PYTHONPATH=str(pathlib.Path(__file__).resolve().parent / "hook"), M1138_SPIKE_LOGS=str(logs),
           ORBSIM_TEST_NAME="hook self-test")
env.pop("M1138_SPIKE_OWNER", None)
subprocess.run([sys.executable, str(probe)], check=True, env=env)

logs_found = sorted(logs.glob("*.log"))
texts = [p.read_text(encoding="utf-8") for p in logs_found]
top = next(t for t in texts if "probe.py" in t.split("\n")[4])
lines = top.split("\n")
if plant:
    lines = [l for l in lines if not l.startswith(plant + "\t")]
D, M, W = (str(work / "data.txt"), str(work / "missing.txt"), str(work))
H = str(work / "helper-mod.py")
expected = {
    "open() of a file": lambda l: l.startswith(f"open\t{D}\tr"),
    "os.open": lambda l: l.startswith(f"open\t{D}\tNone"),
    "os.stat": lambda l: l == f"probe\tstat\t{D}\tfound",
    "os.lstat": lambda l: l == f"probe\tlstat\t{D}\tfound",
    "os.access": lambda l: l == f"probe\taccess\t{D}\tTrue",
    "os.path.exists, a file": lambda l: l == f"probe\texists\t{D}\tTrue",
    "os.path.exists, no file": lambda l: l == f"probe\texists\t{M}\tFalse",
    "os.path.isfile": lambda l: l == f"probe\tisfile\t{D}\tTrue",
    "os.path.isdir": lambda l: l == f"probe\tisdir\t{W}\tTrue",
    "os.path.lexists": lambda l: l == f"probe\tlexists\t{D}\tTrue",
    "a listing": lambda l: l.startswith(f"list\t{W}\t") and len(l.split("\t")[2]) == 64,
    "glob": lambda l: l.startswith("glob\t"),
    "the source of a module loaded from its .pyc": lambda l: l == f"module\t{H}",
    "a child process": lambda l: l.startswith("start\t") and "git" in l,
    "the last line": lambda l: l == "end",
}
failed = [name for name, test in expected.items() if not any(test(l) for l in lines)]
counts = {name: sum(1 for l in lines if test(l)) for name, test in expected.items()}
pyc_opened = any(l.startswith("open\t") and "helper-mod.cpython" in l for l in lines)
source_opened = any(l.startswith(f"open\t{H}\t") for l in lines)
owners = sorted((t.split("\n")[0].split("\t")[1], t.split("\n")[1].split("\t")[1]) for t in texts)
print(f"logs: {len(texts)}; owner and test name of each: {owners}")
print(f"the .pyc opened: {pyc_opened}; the .py opened: {source_opened}")
for name, n in counts.items():
    print(f"  {n:3d}  {name}")
children_ok = len(texts) == 4 and all(o == "hook self-test" for o, _ in owners)
print(f"each child carries the owner: {children_ok}")
if failed or not children_ok:
    print(f"FAILED: {failed}")
    sys.exit(1)
print("hook self-test: all kinds seen")

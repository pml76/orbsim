"""The short run of decision 831: the given tests with ctest, in the
environment that the driver gives CTest, with the jobs of `check`.

    python -I -B short_run.py <tree> <file of test names> <out folder>
"""
import importlib.util
import os
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(r"C:\Users\U439644\Projects\untitled")
CTEST = r"C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\ctest.exe"
spec = importlib.util.spec_from_file_location("run_tests", ROOT / "scripts" / "run-tests.py")
DRIVER = importlib.util.module_from_spec(spec)
sys.modules["run_tests"] = DRIVER
spec.loader.exec_module(DRIVER)

tree, names, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
env = {**os.environ, DRIVER.RECORDS_VARIABLE: str(tree / DRIVER.COVERAGE_RECORDS)}
env = DRIVER.with_hook(DRIVER.without_test_name(env), tree, ROOT)
report = out / f"short-{tree.name}.xml"
report.unlink(missing_ok=True)
print(f"short run started {time.strftime('%H:%M:%S')}", flush=True)
done = subprocess.run([CTEST, "--output-on-failure", "-j", "10", "--tests-from-file", str(names),
                       "--output-junit", str(report)], cwd=tree, env=env, capture_output=True, text=True,
                      encoding="utf-8", errors="replace")
(out / f"short-{tree.name}.out").write_text(done.stdout + done.stderr, encoding="utf-8")
print(f"short run exit {done.returncode}, ended {time.strftime('%H:%M:%S')}", flush=True)
results = DRIVER.outcomes(report.read_text(encoding="utf-8")) if report.is_file() else {}
for name, result in sorted(results.items()):
    print(f"  {result:8} {name}")
sys.exit(done.returncode)

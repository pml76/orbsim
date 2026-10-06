"""Which tests depend on which compiled shader: hide one .spv at a time in a
scratch tree, run the tests that start the application or carry the gpu label,
and record which fail that passed with every shader present (M1-109).

    python shaderloads.py <build tree> <ctest program> <result.json>

Run it in a scratch tree: it renames the tree's compiled shaders one at a
time and puts each back. About 41 minutes on the RTX A2000 machine."""
import json, pathlib, subprocess, sys
tree = pathlib.Path(sys.argv[1]).resolve()
ctest = sys.argv[2]
tests = json.loads(subprocess.run([ctest, "--show-only=json-v1"], cwd=tree, capture_output=True, text=True, check=True).stdout)["tests"]
def labels(t): return [v for p in t.get("properties", []) if p["name"] == "LABELS" for v in p["value"]]
chosen = [t["name"] for t in tests if "gpu" in labels(t) or any("orbsim.exe" in a.lower() for a in t.get("command", []))]
def failing():
    # one regex of exact names; ctest -j1 so the GPU is used by one test at a time
    import re
    pattern = "^(" + "|".join(re.escape(n) for n in chosen) + ")$"
    out = subprocess.run([ctest, "-R", pattern, "--no-tests=error"], cwd=tree, capture_output=True, text=True, encoding="utf-8", errors="replace")
    failed = set()
    take = False
    for line in out.stdout.splitlines():
        if "The following tests FAILED" in line: take = True; continue
        if take and " - " in line:
            failed.add(line.split(" - ", 1)[1].rsplit(" (", 1)[0].strip())
    ran = sum(1 for l in out.stdout.splitlines() if "Test #" in l)
    return failed, ran
base, ran = failing()
print(f"baseline: {ran} tests run of {len(chosen)} chosen, {len(base)} failing: {sorted(base)}", flush=True)
result = {"chosen": chosen, "baseline": sorted(base)}
for spv in sorted((tree / "shaders").glob("*.spv")):
    hidden = spv.with_suffix(".spv.hidden")
    spv.rename(hidden)
    try:
        failed, ran = failing()
    finally:
        hidden.rename(spv)
    new = sorted(failed - base)
    result[spv.name] = new
    print(f"{spv.name}: {len(new)} newly failing of {ran} run", flush=True)
(pathlib.Path(sys.argv[3])).write_text(json.dumps(result, indent=1), encoding="utf-8")

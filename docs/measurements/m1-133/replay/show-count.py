"""Print what replay-count.py and replay-one.sh wrote for one label."""
import collections
import json
import pathlib
import sys

OUT = pathlib.Path(__file__).resolve().parent
label = sys.argv[1]
names_shown = int(sys.argv[2]) if len(sys.argv) > 2 else 6
folder = OUT / label
for extra in ("steps.txt", "patch-id.txt"):
    if (folder / extra).is_file():
        print((folder / extra).read_text(encoding="utf-8").strip())
result = json.loads((folder / "count.json").read_text(encoding="utf-8"))
print(label, result["commit"][:7], result["subject"])
print("diff:", result["diff"])
print("tests", result["tests"], "C++", result["cpp_tests"], "GPU", result["gpu_tests"], "card read", result["card"],
      "forced", len(result["forced"]), "by function, not by program:", result.get("by function, not by program"))
for rule in ("function", "program", "program_none"):
    r = result[rule]
    counter = collections.Counter(cause for causes in r["causes"].values() for cause in causes)
    print(f"  {rule}: all {r['all']}, C++ {r['cpp']}, GPU {r['gpu']}, {r['seconds']} s, "
          f"without a base time: {r['without a base time']}")
    if rule == "program_none":
        continue
    for cause, number in sorted(counter.items()):
        names = [n for n, c in r["causes"].items() if cause in c]
        print(f"    cause {cause}: {number}: {names[:names_shown]}{' ...' if len(names) > names_shown else ''}")
        details = collections.Counter(str(d)[:160] for n in names for d in r["causes"][n][cause])
        for detail, number in details.most_common(8):
            print(f"        {number} x {detail}")
print("changed programs:", len(result["changed programs"]))
functions = {}
for key, change in sorted(result["changed programs"].items()):
    print("  ", key.rsplit("/", 1)[-1], {k: (len(v) if isinstance(v, list) else v) for k, v in change.items()})
    for f in change.get("functions") or []:
        functions.setdefault(f["function"], (f["plain"], tuple(f["objects"]), tuple(f["sources"])))
print("changed functions (each one time):", len(functions))
for name, (plain, objects, sources) in sorted(functions.items(), key=lambda x: x[1][2]):
    print(f"   {plain[:140]} | {', '.join(objects)[:120]} | {', '.join(sources)}")
if (folder / "after.json").is_file():
    done = json.loads((folder / "after.json").read_text(encoding="utf-8"))
    print("after:", {k: v for k, v in done.items() if k != "summary"})
    print(done["summary"])

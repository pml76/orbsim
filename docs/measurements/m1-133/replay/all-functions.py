"""All changed functions that a selected test entered, per commit, for the hand
sort (decision 730): one line each, with the automatic cause, the object, the
source of the object, and whether that source is in the diff."""
import json
import pathlib
import sys

OUT = pathlib.Path(__file__).resolve().parent
for label in sys.argv[1:]:
    result = json.loads((OUT / label / "count.json").read_text(encoding="utf-8"))
    plain = {}
    for change in result["changed programs"].values():
        for f in change.get("functions") or []:
            plain[f["function"]] = f["plain"]
    found = {}
    for name, causes in result["function"]["causes"].items():
        for cause, details in causes.items():
            for detail in details:
                if isinstance(detail, dict) and "function" in detail:
                    key = (detail["function"], tuple(detail["objects"]))
                    item = found.setdefault(key, [cause, set(), detail["sources"], detail["source in the diff"],
                                                  detail.get("without a flag", False)])
                    item[1].add(name)
    print(f"== {label} {result['subject']}")
    print(f"   diff: {result['diff']}")
    for (function, objects), (cause, tests, sources, in_diff, unflagged) in sorted(found.items(),
                                                                                   key=lambda x: x[1][2]):
        text = plain.get(function, function)
        print(f"   [{cause}] {len(tests)} tests | {text[:150]} | {','.join(objects)} | in diff: {bool(in_diff)}"
              f"{' | no flag' if unflagged else ''}")

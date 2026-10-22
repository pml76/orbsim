"""For the hand sort (decision 730): each changed function that a selected test
entered, at one commit, with its plain name, its objects, their sources, the
causes it gave and how many tests it gave them to."""
import json
import pathlib
import sys

OUT = pathlib.Path(__file__).resolve().parent
label = sys.argv[1]
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
                item = found.setdefault(key, {"causes": set(), "tests": set(), "sources": detail["sources"],
                                              "in diff": detail["source in the diff"],
                                              "unflagged": detail.get("without a flag", False)})
                item["causes"].add(cause)
                item["tests"].add(name)
print(label, result["commit"][:7], result["subject"])
print("diff:", result["diff"])
for (function, objects), item in sorted(found.items(), key=lambda x: (x[1]["sources"], x[0][0])):
    print(f"- {plain.get(function, function)}")
    print(f"    decorated {function}")
    print(f"    objects {', '.join(objects)}; sources {item['sources']}; source in diff {item['in diff']}; "
          f"causes {sorted(item['causes'])}; tests {len(item['tests'])}"
          f"{'; without a flag' if item['unflagged'] else ''}")
moved = [n for n, c in result["function"]["causes"].items() if "1/2: the functions without a flag changed" in c]
if moved:
    print("functions without a flag changed for", len(moved), "tests")

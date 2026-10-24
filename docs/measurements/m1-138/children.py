"""The facts for question 7 of M1-138, from procmon-by-test.json: for each
script test, which programs it started and which parts of the repository each
program read. A part is the first folder under the root, `.git`, or, in the
build tree, `tree`. Also: which tests read a document, and which read
scripts/mutation-passes.json, through any of their programs.

    python children.py <procmon-by-test.json> <tree name>
"""
import collections
import json
import sys

ROOT = "c:\\users\\u439644\\projects\\untitled\\"
report = json.load(open(sys.argv[1], encoding="utf-8"))
tree = "build\\" + sys.argv[2] + "\\"


def part(path):
    rel = path[len(ROOT):] if path.startswith(ROOT) else path
    if rel.startswith(tree):
        return "tree"
    if rel.startswith("build\\"):
        return "build (other)"
    return rel.split("\\", 1)[0] if "\\" in rel else "(root file)"


def document(path):
    rel = path[len(ROOT):]
    return rel.endswith(".md") or rel.startswith("docs\\")


by_program = collections.defaultdict(lambda: collections.Counter())
docs, record, children_of = {}, set(), collections.defaultdict(set)
for test, programs in report.items():
    for program, data in programs.items():
        seen = set(data["read"]) | set(data["queried"])
        parts = collections.Counter(part(p) for p in seen)
        if not program.startswith("python"):
            children_of[test].add(program)
            by_program[program].update(parts)
        hits = sorted(p[len(ROOT):] for p in seen if document(p))
        if hits:
            docs.setdefault(test, {})[program] = hits[:5]
        if any(p.endswith("scripts\\mutation-passes.json") for p in seen):
            record.add(test)
print(f"script tests: {len(report)}")
print(f"tests whose programs read or asked about a document: {len(docs)}")
for test, programs in sorted(docs.items()):
    print(f"  {test}: {programs}")
print(f"tests that read or asked about scripts/mutation-passes.json: {sorted(record)}")
print("child programs (not Python), with the parts of the repository that they read or asked about:")
for program, parts in sorted(by_program.items(), key=lambda kv: -sum(kv[1].values())):
    tests = sorted(t for t, ps in children_of.items() if program in ps)
    print(f"  {program:28s} tests {len(tests):3d}  parts {dict(parts.most_common(8))}")

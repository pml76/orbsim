#!/usr/bin/env python3
"""The parts of decision 857 of the mutant files recorded in one tree.

    judges-read-files.py <build tree>

M1-137 item 2, register decisions 857-863 and 879-881; the results are in
docs/measurements/m1-137-outputs.md. For each mutant file whose pass is
recorded in the tree, from scripts/mutation-passes.json, the script gives,
through the code of scripts/mutants-due.py:

    parts       the number of parts of decision 857 of its judges at this
                time, the counted files of the build tree that they read
    moved       whether those parts moved against the record of its pass
                (judged_since), with the rule of decision 879 for a record
                without the mark: then the files come from the store
    no record   its judges with no complete record in the store of the
                driver, which get no part (decision 861)

The tree is only read, but for the answers of Ninja and the hashes that a
reading of scripts/mutants-due.py keeps in the tree, as each reading does.
"""
import importlib.util
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]


def mutants_due():
    """scripts/mutants-due.py as a module; its name has a `-`, thus no import."""
    spec = importlib.util.spec_from_file_location("mutants_due", ROOT / "scripts" / "mutants-due.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["mutants_due"] = module
    spec.loader.exec_module(module)
    return module


def main(tree: pathlib.Path) -> int:
    md = mutants_due()
    tree = tree.resolve()
    reading = md.Tree(tree)
    passes = json.loads((ROOT / "scripts" / "mutation-passes.json").read_text(encoding="utf-8"))
    moved_files, limited, total = [], {}, 0
    recorded = 0
    for path in sorted((ROOT / "scripts" / "mutants").glob("*.json")):
        record = passes.get(path.name)
        if not record or not md.same_tree(record["tree"], tree) or "judges" not in record:
            continue
        recorded += 1
        spec = json.loads(path.read_text(encoding="utf-8"))
        parts = reading.fingerprint_parts(spec, record)
        count = sum(1 for value in parts.values() for part in value if part.startswith(md.READ_PART))
        moved = [line for line in md.judged_since(record, {k: md.combine(v) for k, v in parts.items()}, parts)
                 if md.READ_PART in line]
        without = reading.judges_without_record(spec)
        total += count
        if moved:
            moved_files.append(path.name)
        if without:
            limited[path.name] = without
        print(f"  {path.name:14} parts {count:5}  moved {'yes' if moved else 'no ':3}  "
              f"mark {'yes' if md.READ_MARK in record else 'no '}  no record: {', '.join(without) or '-'}")
    judges = sorted({judge for names in limited.values() for judge in names})
    print(f"{tree.name}: {recorded} files recorded here, {total} parts; {len(moved_files)} files move by the parts "
          f"of decision 857; {len(limited)} files have {len(judges)} judges with no complete record: "
          f"{', '.join(judges)}")
    reading.save_hashes()
    reading.save_graph()
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(pathlib.Path(sys.argv[1])))

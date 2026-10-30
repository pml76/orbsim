#!/usr/bin/env python3
"""Compare two snapshots of one tree, kept after two full runs at one commit.

    coverage-compare.py <first.json> <second.json>

M1-137 item 1, register decisions 845, 858 and 870; the results are in
docs/measurements/m1-137-coverage.md. coverage-snapshot.py makes the
snapshots. At one commit the code does not change, thus:

  - each function that a test entered in one run and not in the other is a
    difference of coverage (decision 842), and the test gets the rule of
    decision 844;
  - each counted file with a different hash makes mutant files due at random
    through decision 857 (decision 858).

The script gives each test with a difference, with its new and its gone
functions, then each counted file with a different hash.
"""
import json
import pathlib
import sys


def main(first: pathlib.Path, second: pathlib.Path) -> int:
    a = json.loads(first.read_text(encoding="utf-8"))
    b = json.loads(second.read_text(encoding="utf-8"))
    print(f"tree {a['tree']}: commits {a['commit'][:12]} and {b['commit'][:12]}"
          + ("" if a["commit"] == b["commit"] else "  -- NOT THE SAME COMMIT"))
    fa, fb = a["functions"], b["functions"]
    only = sorted(set(fa) ^ set(fb))
    print(f"tests by function: {len(fa)} and {len(fb)}; in one run only: {len(only)} {only[:10]}")
    different = 0
    for name in sorted(set(fa) & set(fb)):
        pa, pb = fa[name], fb[name]
        lines = []
        for program in sorted(set(pa) | set(pb)):
            before, after = set(pa.get(program, [])), set(pb.get(program, []))
            new, gone = sorted(after - before), sorted(before - after)
            if new or gone:
                lines.append(f"    {program.rsplit('/', 1)[-1]}: {len(new)} new, {len(gone)} gone; new: {new[:3]}; "
                             f"gone: {gone[:3]}")
        if lines:
            different += 1
            print(f"  {name}")
            print("\n".join(lines))
    print(f"tests with a difference of functions: {different}")
    ha, hb = a["hashes"], b["hashes"]
    changed = sorted(p for p in set(ha) & set(hb) if ha[p] != hb[p])
    print(f"counted files: {len(ha)} and {len(hb)}; in one run only: {len(set(ha) ^ set(hb))}; "
          f"with a different hash: {len(changed)}")
    for p in changed[:40]:
        print("   ", p, ha[p][:12], "->", hb[p][:12])
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sys.exit(main(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])))

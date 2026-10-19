"""M1-131 item 4 (decisions 582 and 504): are the maps and the programs the
same, byte for byte, from one clean rebuild with the hook to the next?

    python -I maps-compare.py <first.json> <second.json> [...]

Reads the files maps-census.py wrote after each rebuild -- each map's and
each top-level program's size, SHA-256 and time -- and prints, for each file
that the first names, whether every other file names it with the same hash.
The times are not compared: a clean rebuild writes every file again.
"""

import json
import sys
from pathlib import Path


def main(argv: list) -> int:
    records = [json.loads(Path(a).read_text(encoding="utf-8")) for a in argv[1:]]
    first = records[0]
    same, different, missing = [], [], []
    for name, (_, digest, _) in sorted(first.items()):
        others = [r.get(name) for r in records[1:]]
        if any(o is None for o in others):
            missing.append(name)
        elif all(o[1] == digest for o in others):
            same.append(name)
        else:
            different.append(name)
    extra = sorted({n for r in records[1:] for n in r} - set(first))
    for kind in ("map", "exe"):
        print(f".{kind}: {sum(n.endswith(kind) for n in same)} the same in all {len(records)}, "
              f"{sum(n.endswith(kind) for n in different)} different, "
              f"{sum(n.endswith(kind) for n in missing)} missing from a later one")
    print(f"different: {different[:10]}")
    print(f"missing from a later one: {missing[:10]}; only in a later one: {extra[:10]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

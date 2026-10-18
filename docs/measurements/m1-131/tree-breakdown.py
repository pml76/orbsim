"""What the mutation harness's snapshot of a tree holds, by file extension.

    python -I tree-breakdown.py <tree>

Uses scripts/mutate.py's own list_tree and uncopied, as measure-snapshot.py
does, and copies nothing: the files a pass copies, grouped by extension, with
their count and bytes. The linker maps the hook brings (.map) will show here.
"""

import collections
import importlib.util
import sys
from pathlib import Path

REPO = Path(r"C:\Users\U439644\Projects\untitled")


def main(argv: list) -> int:
    spec = importlib.util.spec_from_file_location("mutate", REPO / "scripts" / "mutate.py")
    mutate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mutate)
    files, _ = mutate.list_tree(Path(argv[1]))
    groups = collections.defaultdict(lambda: [0, 0])
    for name, (size, _) in files.items():
        if mutate.uncopied(name):
            continue
        suffix = Path(name).suffix.lower() or "(none)"
        groups[suffix][0] += 1
        groups[suffix][1] += size
    total = sum(size for _, size in groups.values())
    print(f"{'extension':12} {'files':>7} {'MiB':>9} {'share':>6}")
    for suffix, (count, size) in sorted(groups.items(), key=lambda kv: -kv[1][1])[:15]:
        print(f"{suffix:12} {count:7,} {size / 2**20:9.1f} {100 * size / total:5.1f}%")
    rest = sorted(groups.items(), key=lambda kv: -kv[1][1])[15:]
    print(f"{'the rest':12} {sum(c for _, (c, _) in rest):7,} {sum(s for _, (_, s) in rest) / 2**20:9.1f}")
    print(f"{'all':12} {sum(c for c, _ in groups.values()):7,} {total / 2**20:9.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

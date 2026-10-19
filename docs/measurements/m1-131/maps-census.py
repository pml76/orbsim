"""M1-131 item 4 (decision 582): the linker's maps in one build tree.

    python -I maps-census.py <tree> <out.json>

Lists every file ending .map in the tree, outside the dependencies'
downloaded sources (_deps/<name>-src/), and prints their count, their total
size, the five largest, and how many lie beside a program of the same name.
It also says whether each map was written at its program's link: a map older
than its program by more than 60 s is stale, left from an earlier link.
<out.json> receives each map's and each top-level program's size, SHA-256 and
time, so that two clean builds can be compared file by file.
"""

import hashlib
import json
import os
import sys
from pathlib import Path


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main(argv: list) -> int:
    tree, out = Path(argv[1]), Path(argv[2])
    maps = []
    for folder, subfolders, files in os.walk(tree):
        relative = Path(folder).relative_to(tree).as_posix()
        parts = relative.split("/")
        if len(parts) >= 2 and parts[0] == "_deps" and parts[1].endswith("-src"):
            subfolders[:] = []
            continue
        maps += [Path(folder) / f for f in files if f.lower().endswith(".map")]
    total = sum(m.stat().st_size for m in maps)
    beside = [m for m in maps if m.with_suffix(".exe").is_file()]
    stale = [m for m in beside if m.stat().st_mtime < m.with_suffix(".exe").stat().st_mtime - 60]
    print(f"maps: {len(maps)}, {total:,} bytes ({total / 2**20:,.1f} MiB, {total / 1e6:,.1f} MB)")
    print(f"beside a program of the same name: {len(beside)}; older than their program by over 60 s: {len(stale)}")
    for m in sorted(maps, key=lambda p: -p.stat().st_size)[:5]:
        print(f"  {m.relative_to(tree).as_posix()}: {m.stat().st_size:,} bytes ({m.stat().st_size / 2**20:.1f} MiB)")
    elsewhere = [m.relative_to(tree).as_posix() for m in maps if m not in beside]
    print(f"maps not beside a program: {len(elsewhere)} {elsewhere[:5]}")
    record = {}
    for m in maps:
        record[m.relative_to(tree).as_posix()] = [m.stat().st_size, digest(m), m.stat().st_mtime_ns]
    for program in sorted(tree.glob("*.exe")):
        record[program.name] = [program.stat().st_size, digest(program), program.stat().st_mtime_ns]
    out.write_bytes(json.dumps(record, indent=1, sort_keys=True).encode("utf-8") + b"\n")
    print(f"programs at the top: {len(list(tree.glob('*.exe')))}; sizes and hashes in {out.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

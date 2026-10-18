"""M1-131 item 4 (decision 631): the size and time of the mutation harness's
copy of a build tree, without running a pass.

    python -I measure-snapshot.py <tree> <label>

It loads scripts/mutate.py as a module (its top level only defines functions
and constants; main() runs only under __main__) and calls the harness's own
take_snapshot(tree, place, sources) -- the very function a pass calls before
its first mutant (mutate.py, main()) -- with place in this script's folder,
never build/mutate-snapshot-<tree>, which is where mutate.py looks for a
stopped pass's snapshot (snapshot_place, leftover_problem). sources is {}:
a pass passes the SHA-256 and time of its mutant file's few sources, a file of
a few hundred bytes. The copy is then deleted with shutil.rmtree, as a pass
deletes its snapshot at the end, and that is timed too.
"""

import importlib.util
import shutil
import sys
import time
from pathlib import Path

REPO = Path(r"C:\Users\U439644\Projects\untitled")
HERE = Path(__file__).resolve().parent


def load_mutate():
    spec = importlib.util.spec_from_file_location("mutate", REPO / "scripts" / "mutate.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def mib(count: int) -> str:
    return f"{count:,} bytes ({count / 2**20:,.1f} MiB, {count / 2**30:.3f} GiB)"


def main(argv: list) -> int:
    tree, label = Path(argv[1]), argv[2]
    mutate = load_mutate()
    place = HERE / f"snapshot-{label}"
    if place.exists():
        raise SystemExit(f"{place} exists already; not overwriting")
    if mutate.snapshot_place(REPO, str(tree)).exists():
        raise SystemExit("a real snapshot is left in the tree's place; not measuring")

    start = time.perf_counter()
    files, folders = mutate.list_tree(tree)
    listing_s = time.perf_counter() - start
    copied = {name: stamp for name, stamp in files.items() if not mutate.uncopied(name)}
    skipped = {name: stamp for name, stamp in files.items() if mutate.uncopied(name)}
    print(f"tree {tree}: {len(files):,} files in {len(folders):,} folders, listed in {listing_s:.2f} s")
    print(f"copied by a pass: {len(copied):,} files, {mib(sum(s for s, _ in copied.values()))}")
    print(f"listed, not copied (_deps/*-src): {len(skipped):,} files, "
          f"{mib(sum(s for s, _ in skipped.values()))}")
    biggest = collections_top(copied)
    print("the largest top-level parts of the copy: " + "; ".join(biggest))

    start = time.perf_counter()
    mutate.take_snapshot(tree, place, {})
    snapshot_s = time.perf_counter() - start
    on_disk = sum(p.stat().st_size for p in place.rglob("*") if p.is_file())
    count = sum(1 for p in place.rglob("*") if p.is_file())
    print(f"take_snapshot: {snapshot_s:.1f} s (it lists the tree, copies, lists the copy and compares, "
          f"writes sources.json and listing.json)")
    print(f"the snapshot on disk: {count:,} files, {mib(on_disk)}; "
          f"listing.json {(place / mutate.SNAPSHOT_LISTING).stat().st_size:,} bytes")

    start = time.perf_counter()
    shutil.rmtree(place)
    delete_s = time.perf_counter() - start
    print(f"shutil.rmtree of the snapshot: {delete_s:.1f} s; still there afterwards: {place.exists()}")
    return 0


def collections_top(copied: dict) -> list:
    sizes = {}
    for name, (size, _) in copied.items():
        parts = name.split("/")
        key = "/".join(parts[:2]) if parts[0] in ("_deps", "CMakeFiles") and len(parts) > 2 else (
            parts[0] if len(parts) > 1 else "(top-level files)")
        sizes[key] = sizes.get(key, 0) + size
    return [f"{key} {size / 2**20:,.0f} MiB" for key, size in sorted(sizes.items(), key=lambda kv: -kv[1])[:8]]


if __name__ == "__main__":
    sys.exit(main(sys.argv))

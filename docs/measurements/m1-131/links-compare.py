"""M1-131 item 4 (decision 582): the link of each program, with the map and
without it, over the six rebuilds of one tree.

    python -I links-compare.py <short>          (rel or debug)

Reads the build's entries of each rebuild's .ninja_log copies, as
compare-builds.py does, and keeps the time of each link of a program at the
top of the tree by the program's name. A link runs beside the compiles of a
full build, so its time holds the wait for the processor and the disk too.

Printed: for each program linked in all six rebuilds, the average time
without the hook and with it; then the median and the average of the
differences, and the programs with the largest difference.
"""

import importlib.util
import statistics
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def load():
    spec = importlib.util.spec_from_file_location("compare_builds", HERE / "compare-builds.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main(argv: list) -> int:
    short = argv[1]
    compare = load()
    links = {}
    for kind in ("nohook", "hook"):
        for n in (1, 2, 3):
            label = f"{short}-{kind}-{n}"
            found = {}
            for ms, outputs in compare.edges(HERE / f"{label}-ninja_log-before-build.txt",
                                             HERE / f"{label}-ninja_log-after-build.txt"):
                for output in outputs:
                    if output.endswith(".exe") and "/" not in output:
                        found[output] = ms / 1000
            links[label] = found
    common = set.intersection(*(set(f) for f in links.values()))
    rows = []
    for name in common:
        off = statistics.mean(links[l][name] for l in links if "-nohook-" in l)
        on = statistics.mean(links[l][name] for l in links if "-hook-" in l)
        rows.append((on - off, off, on, name))
    rows.sort(reverse=True)
    deltas = [r[0] for r in rows]
    print(f"programs linked in all six rebuilds: {len(common)}")
    print(f"link time, average over the programs: without {statistics.mean(r[1] for r in rows):.2f} s, "
          f"with {statistics.mean(r[2] for r in rows):.2f} s")
    print(f"difference per program: median {statistics.median(deltas):+.2f} s, average "
          f"{statistics.mean(deltas):+.2f} s, lowest {min(deltas):+.2f} s, highest {max(deltas):+.2f} s; "
          f"grew for {sum(d > 0 for d in deltas)} of {len(deltas)}")
    print("the five largest differences (without -> with, s):")
    for delta, off, on, name in rows[:5]:
        print(f"  {delta:+6.2f}  {off:6.2f} -> {on:6.2f}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

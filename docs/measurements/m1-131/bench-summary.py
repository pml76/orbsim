"""M1-131 item 4 (decision 589): the benchmark runs of the six rebuilds.

    python -I bench-summary.py

Reads rel-nohook-<n>-bench.txt and rel-hook-<n>-bench.txt (bench.sh's
printout, three runs each) beside this script, and prints each run's
interval, processor and GPU figures, then for each state the median of each
figure over its nine runs, the lowest and the highest.
"""

import re
import statistics
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROW = re.compile(r"^(frame interval|CPU working|GPU)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)")


def runs(path: Path) -> list:
    found, current = [], None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("=== run") and "exit" not in line:
            current = {"when": line.split(", ", 1)[1]}
            found.append(current)
        match = ROW.match(line)
        if match and current is not None:
            mean, median, p95, p99, top = map(float, match.groups()[1:])
            current[match.group(1)] = {"mean": mean, "median": median, "p95": p95, "p99": p99, "max": top}
    return found


def main() -> int:
    table = {}
    for kind in ("nohook", "hook"):
        for n in (1, 2, 3):
            path = HERE / f"rel-{kind}-{n}-bench.txt"
            if path.exists():
                table[f"rel-{kind}-{n}"] = runs(path)
    print(f"{'rebuild, run':24}{'interval median':>16}{'p95':>8}{'p99':>8}{'CPU median':>12}{'p95':>8}"
          f"{'GPU median':>12}{'p95':>8}")
    for label, found in table.items():
        for index, run in enumerate(found, start=1):
            f, c, g = run["frame interval"], run["CPU working"], run["GPU"]
            print(f"{label + ' ' + str(index):24}{f['median']:16.4f}{f['p95']:8.4f}{f['p99']:8.4f}"
                  f"{c['median']:12.4f}{c['p95']:8.4f}{g['median']:12.4f}{g['p95']:8.4f}")
    print()
    print(f"{'over nine runs':24}{'median':>10}{'low':>9}{'high':>9}   (without)  {'median':>10}{'low':>9}{'high':>9}"
          "   (with)")
    for row, column in (("frame interval", "median"), ("frame interval", "mean"), ("frame interval", "p95"),
                        ("CPU working", "median"), ("CPU working", "p95"), ("GPU", "median"), ("GPU", "p95")):
        values = {}
        for kind in ("nohook", "hook"):
            values[kind] = [run[row][column] for label, found in table.items() if f"-{kind}-" in label
                            for run in found]
        off, on = values["nohook"], values["hook"]
        print(f"{row + ' ' + column:24}{statistics.median(off):10.4f}{min(off):9.4f}{max(off):9.4f}"
              f"{'':13}{statistics.median(on):10.4f}{min(on):9.4f}{max(on):9.4f}"
              f"   change of the medians {100 * (statistics.median(on) / statistics.median(off) - 1):+.1f} %")
    return 0


if __name__ == "__main__":
    sys.exit(main())

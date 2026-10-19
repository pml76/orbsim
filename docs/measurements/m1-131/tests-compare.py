"""M1-131 item 4 (decision 559): the test times of the six checks of one tree.

    python -I tests-compare.py <short>          (rel or debug)

Reads the CTest lines of each check's log, <short>-nohook-1..3-check.log and
<short>-hook-1..3-check.log beside this script -- "n/N Test #k: <name> ...
Passed <s> sec" -- and keeps each test's time by its name, since the test
numbers move when the hook adds tests. The tests run in parallel, so a test's
time holds the wait for the processor as well as its own work.

Printed: for each check, the tests that ran and their summed time; the same
sum over the tests that ran in all six checks; the average of each state and
the difference; the tests that ran only with the hook or only without it; and
the ten tests whose average time grew the most with the hook.
"""

import collections
import re
import statistics
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
LINE = re.compile(r"^\s*\d+/\d+ Test\s+#\d+: (.*?) \.{2,}\s*(.*?)\s+([0-9.]+) sec\s*$")


def times(path: Path) -> dict:
    found = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = LINE.match(line)
        if match:
            found[match.group(1).strip()] = (float(match.group(3)), match.group(2).strip())
    return found


def main(argv: list) -> int:
    short = argv[1]
    runs = {}
    for kind in ("nohook", "hook"):
        for n in (1, 2, 3):
            label = f"{short}-{kind}-{n}"
            path = HERE / f"{label}-check.log"
            if path.exists():
                runs[label] = times(path)
    common = set.intersection(*(set(r) for r in runs.values()))
    print(f"{'check':18}{'tests':>7}{'summed s':>10}{'not passed':>12}{'common tests':>14}{'summed s':>10}")
    sums = {}
    for label, found in runs.items():
        total = sum(t for t, _ in found.values())
        sums[label] = sum(found[name][0] for name in common)
        bad = sum(1 for _, how in found.values() if how != "Passed")
        print(f"{label:18}{len(found):7}{total:10.1f}{bad:12}{len(common):14}{sums[label]:10.1f}")
    off = [sums[l] for l in sums if "-nohook-" in l]
    on = [sums[l] for l in sums if "-hook-" in l]
    print(f"common tests summed: without {statistics.mean(off):.1f} s (lowest {min(off):.1f}, highest "
          f"{max(off):.1f}), with {statistics.mean(on):.1f} s (lowest {min(on):.1f}, highest {max(on):.1f}); "
          f"difference {statistics.mean(on) - statistics.mean(off):+.1f} s, "
          f"{100 * (statistics.mean(on) / statistics.mean(off) - 1):+.1f} %")
    only_on = set.union(*(set(r) for l, r in runs.items() if "-hook-" in l)) - \
        set.union(*(set(r) for l, r in runs.items() if "-nohook-" in l))
    only_off = set.union(*(set(r) for l, r in runs.items() if "-nohook-" in l)) - \
        set.union(*(set(r) for l, r in runs.items() if "-hook-" in l))
    print(f"ran only with the hook: {sorted(only_on)}")
    print(f"ran only without the hook: {sorted(only_off)}")
    growth = []
    for name in common:
        mean_off = statistics.mean(runs[l][name][0] for l in runs if "-nohook-" in l)
        mean_on = statistics.mean(runs[l][name][0] for l in runs if "-hook-" in l)
        growth.append((mean_on - mean_off, mean_off, mean_on, name))
    growth.sort(reverse=True)
    print("the ten tests whose average time grew the most (without -> with, s):")
    for delta, mean_off, mean_on, name in growth[:10]:
        print(f"  {delta:+7.2f}  {mean_off:7.2f} -> {mean_on:7.2f}  {name[:90]}")
    by_share = collections.Counter()
    for delta, _, _, _ in growth:
        by_share["grew" if delta > 0 else "fell or same"] += 1
    print(f"common tests whose average grew: {by_share['grew']}, fell or stayed: {by_share['fell or same']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

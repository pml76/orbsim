"""M1-131 item 4 (decisions 634 and 638): what the writes of the coverage
records show, from a run with the temporary recorder.

    python -I records-count.py <folder>

The temporary recorder (recorder-timing.diff beside this script) gives each
write its own file, <process number>-<start time>-<write number>.functions.txt,
and adds lines after `end`: for a write by the crash handler, `code <the
exception's code>`; then for every write `write_us <microseconds>`, the time
from the start of the write to the end of its one WriteFile call, and last
`duration_us <microseconds>`, the time from the start of the write until the
file is closed -- the time the writer holds its turn (decision 638). The last
two are written through the file opened again, after it was closed.

Printed: the files and the processes; how many processes wrote more than once,
and in which order; how many wrote "by a crash" and then "normally" -- the
case decision 634 asks about; the exception codes the crash handler saw; and
both times: the longest, the 99th and 50th percentiles, by how the program
ended, with the five longest writes and their programs.
"""

import collections
import re
import statistics
import sys
from pathlib import Path

NAME = re.compile(r"^(\d+)-(\d+)-(\d+)\.functions\.txt$")


def percentile(values: list, share: float) -> float:
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, round(share * (len(ordered) - 1))))
    return ordered[index]


def main(argv: list) -> int:
    folder = Path(argv[1])
    processes = collections.defaultdict(list)
    other_files = []
    for path in folder.iterdir():
        match = NAME.match(path.name)
        if not match:
            other_files.append(path.name)
            continue
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        fields = {"program": "", "test": "", "ended": "", "code": "", "duration": None, "write": None,
                  "end": False}
        for line in lines:
            key, _, value = line.partition(" ")
            if key in ("program", "test", "ended", "code"):
                fields[key] = value
            elif key == "end":
                fields["end"] = True
            elif key == "duration_us":
                fields["duration"] = int(value)
            elif key == "write_us":
                fields["write"] = int(value)
        processes[(match.group(1), match.group(2))].append((int(match.group(3)), fields, path.name))
    writes = [w for found in processes.values() for w in found]
    print(f"files: {len(writes)} records, {len(other_files)} other files {sorted(other_files)[:5]}")
    print(f"processes: {len(processes)}")
    orders = collections.Counter()
    crash_then_normal = []
    for key, found in processes.items():
        found.sort(key=lambda item: item[0])
        orders[" -> ".join(f["ended"] for _, f, _ in found)] += 1
        endings = [f["ended"] for _, f, _ in found]
        for first in range(len(endings)):
            if endings[first] == "by a crash" and "normally" in endings[first + 1:]:
                crash_then_normal.append((key, found))
                break
    print("the writes of one process, in order:")
    for order, count in orders.most_common():
        print(f"  {count:6}  {order}")
    print(f"processes that wrote 'by a crash' and then 'normally': {len(crash_then_normal)}")
    for key, found in crash_then_normal[:10]:
        _, fields, _ = found[0]
        print(f"  process {key[0]}: {Path(fields['program']).name}, test '{fields['test']}', "
              f"codes {[f['code'] for _, f, _ in found if f['code']]}, writes {len(found)}")
    by_program = collections.Counter(Path(found[0][1]["program"]).name for _, found in crash_then_normal)
    if by_program:
        print("  by program: " + ", ".join(f"{p} {n}" for p, n in by_program.most_common()))
    codes = collections.Counter(f["code"] for _, f, _ in writes if f["ended"] == "by a crash")
    print(f"exception codes of the crash handler's writes: {dict(codes)}")
    cut = [name for _, f, name in writes if not f["end"]]
    print(f"records without their end line: {len(cut)} {cut[:5]}")
    for field, what in (("write", "write_us: to the end of WriteFile"),
                        ("duration", "duration_us: until the file is closed, the turn held")):
        durations = [f[field] for _, f, _ in writes if f[field] is not None]
        missing = len(writes) - len(durations)
        print(f"{what}: {len(durations)} writes timed, {missing} without the line")
        if not durations:
            continue
        print(f"  longest {max(durations):,} us; 99th percentile {percentile(durations, 0.99):,} us; "
              f"99.9th {percentile(durations, 0.999):,} us; median {statistics.median(durations):,.0f} us; "
              f"mean {statistics.mean(durations):,.0f} us")
        by_ending = collections.defaultdict(list)
        for _, f, _ in writes:
            if f[field] is not None:
                by_ending[f["ended"]].append(f[field])
        for ending, values in sorted(by_ending.items()):
            print(f"  ended {ending}: {len(values)} writes, longest {max(values):,} us, "
                  f"99th percentile {percentile(values, 0.99):,} us, median {statistics.median(values):,.0f} us")
        longest = sorted(((f[field], Path(f["program"]).name, f["test"], name) for _, f, name in writes
                          if f[field] is not None), reverse=True)[:5]
        print("  the five longest:")
        for duration, program, test, name in longest:
            print(f"    {duration:,} us  {program}  test '{test}'  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

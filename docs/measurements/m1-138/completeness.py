"""Is a recording of Process Monitor complete (decision 822)? Events in each
minute of the run, and events of each test of the run.

    python -I -B completeness.py <csv> <tree> <run start HH:MM:SS> <run end HH:MM:SS>

A process gets its test from ORBSIM_TEST_NAME in the environment of its
"Process Start" event; else, if its parent is a ctest process, from its
command line, which must be the command of one test; else from its parent.
"""
import collections
import csv
import json
import re
import subprocess
import sys

csv.field_size_limit(1 << 30)
CTEST = r"C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\ctest.exe"


def clock(text):
    """'8:59:01.1714987 AM' -> '08:59:01'."""
    hms, half = text.split(" ")
    h, m, s = hms.split(":")
    h = int(h) % 12 + (12 if half == "PM" else 0)
    return f"{h:02d}:{m}:{s[:2]}"


def norm(text):
    return re.sub(r"\s+", " ", text.replace("/", "\\").replace('"', "")).strip().lower()


def test_commands(tree):
    listing = json.loads(subprocess.run([CTEST, "--show-only=json-v1"], cwd=tree, capture_output=True,
                                        text=True, check=True).stdout)
    return {norm(" ".join(t["command"])): t["name"] for t in listing["tests"] if t.get("command")}, \
        [t["name"] for t in listing["tests"]]


def main():
    path, tree, start, end = sys.argv[1:5]
    commands, names = test_commands(tree)
    minutes = collections.Counter()
    first = last = None
    test_of, parent_of, image_of = {}, {}, {}
    events = collections.Counter()
    with open(path, encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            when = clock(row["Time of Day"])
            first = first or when
            last = when
            minutes[when[:5]] += 1
            pid = row["PID"]
            if row["Operation"] == "Process Start":
                detail = row["Detail"]
                parent = re.search(r"Parent PID: (\d+)", detail)
                parent = parent.group(1) if parent else None
                command = re.search(r"Command line: (.*?)(?:, Current directory:|$)", detail, re.S)
                command = command.group(1) if command else ""
                env = re.search(r"ORBSIM_TEST_NAME=(.*?)(?:;\s|[\r\n]|$)", detail)
                test = env.group(1).strip() if env else None
                if test is None and parent and image_of.get(parent, "").startswith("ctest"):
                    test = commands.get(norm(command))
                if test is None and parent:
                    test = test_of.get(parent)
                test_of[pid], parent_of[pid] = test, parent
                image_of[pid] = row["Process Name"].lower()
            elif pid not in image_of:
                image_of[pid] = row["Process Name"].lower()
            if test_of.get(pid):
                events[test_of[pid]] += 1
    run = sorted(m for m in minutes if start[:5] <= m <= end[:5])
    wanted = [f"{h:02d}:{m:02d}" for h in range(int(start[:2]), int(end[:2]) + 1) for m in range(60)
              if start[:5] <= f"{h:02d}:{m:02d}" <= end[:5]]
    empty = [m for m in wanted if minutes[m] == 0]
    print(f"first event {first}, last event {last}; run {start} to {end}")
    print(f"minutes of the run: {len(wanted)}, with no event: {empty or 'none'}")
    print(f"fewest events in a minute of the run: {min(minutes[m] for m in wanted)}")
    missing = [n for n in names if events[n] == 0]
    print(f"tests: {len(names)}, with events: {len(names) - len(missing)}, without: {missing or 'none'}")
    unknown = sorted(set(t for t in test_of.values() if t) - set(names))
    print(f"test names in the recording that CTest does not have: {unknown or 'none'}")


if __name__ == "__main__":
    main()

"""Why did a recording stop before the run ended? From a CSV of Process
Monitor: each process start of taskkill, with its command line and the test
that started it, and the processes with events in the last 20 seconds.

    python last_events.py <csv>
"""
import collections
import csv
import sys

rows_by_pid = {}
kills, tail = [], collections.deque(maxlen=20000)
with open(sys.argv[1], encoding="utf-8-sig", newline="") as stream:
    for row in csv.DictReader(stream):
        if row["Operation"] == "Process Start":
            detail = row["Detail"]
            rows_by_pid[row["PID"]] = detail[:200]
            if row["Process Name"].lower().startswith("taskkill"):
                parent = detail.split("Parent PID: ", 1)[-1].split(",", 1)[0]
                kills.append((row["Time of Day"], detail.split("Command line: ", 1)[-1][:160],
                              rows_by_pid.get(parent, "(parent not seen)")[:160]))
        tail.append((row["Time of Day"], row["Process Name"]))
print(f"taskkill starts: {len(kills)}")
for when, command, parent in kills:
    print(f"  {when}  {command}\n      started by: {parent}")
last = tail[-1][0] if tail else ""
names = collections.Counter(name for when, name in tail if when[:8] == last[:8] or when[:7] == last[:7])
print(f"last event: {last}; processes in the last seconds: {names.most_common(15)}")

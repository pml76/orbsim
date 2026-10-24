"""For question 7 of M1-138: does a child program whose working folder is
outside the repository read or ask about a file in the repository? From a
CSV of Process Monitor, process by process: the working folder from the
process start, and each CreateFile, attribute query or listing in the
repository. Python processes are left out; the hook covers them.

    python scratch_children.py <csv>
"""
import collections
import csv
import re
import sys

csv.field_size_limit(1 << 30)
REPO = "c:\\users\\u439644\\projects\\untitled"
OPS = {"CreateFile", "QueryOpen", "QueryBasicInformationFile", "QueryStandardInformationFile",
       "QueryNetworkOpenInformationFile", "QueryAttributeTagFile", "QueryAllInformationFile", "QueryDirectory"}
current, procs = {}, []
with open(sys.argv[1], encoding="utf-8-sig", newline="") as stream:
    for row in csv.DictReader(stream):
        pid, op = row["PID"], row["Operation"]
        if op == "Process Start":
            detail = row["Detail"]
            cwd = re.search(r"Current directory: (.*?), Environment:", detail, re.S)
            command = re.search(r"Command line: (.*?), Current directory:", detail, re.S)
            proc = {"image": row["Process Name"].lower(), "cwd": (cwd.group(1) if cwd else "").lower().rstrip("\\"),
                    "command": command.group(1)[:160] if command else "", "repo": set()}
            current[pid] = proc
            procs.append(proc)
            continue
        proc = current.get(pid)
        if proc is None or op not in OPS:
            continue
        path = row["Path"].lower()
        if path.startswith(REPO + "\\") and not path.startswith(REPO + "\\build\\m1-138-spike"):
            proc["repo"].add(path[len(REPO) + 1:])
outside = [p for p in procs if p["cwd"] and not p["cwd"].startswith(REPO) and not p["image"].startswith("python")
           and "\\temp\\" in p["cwd"]]
by_image = collections.defaultdict(lambda: [0, 0, set(), set()])
for p in outside:
    entry = by_image[p["image"]]
    entry[0] += 1
    if p["repo"]:
        entry[1] += 1
        entry[2] |= {r for r in p["repo"] if "\\" not in r or not r.startswith("build\\")} | {
            r for r in p["repo"] if r.startswith("build\\")}
        entry[3].add(p["command"][:110])
print("programs whose working folder is in the temporary folder, outside the repository:")
for image, (count, reading, paths, commands) in sorted(by_image.items(), key=lambda kv: -kv[1][0]):
    print(f"  {image:24s} processes {count:4d}; of them read or asked about the repository: {reading:4d}")
    for path in sorted(paths)[:6]:
        print(f"      {path}")
    for command in sorted(commands)[:3]:
        print(f"      command: {command}")

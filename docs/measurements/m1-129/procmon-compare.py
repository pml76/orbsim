"""Scratch analysis, not part of the repository (register decision 612): every
file each Catch2 test process opened for reading, from Process Monitor's CSV,
against the read record that process wrote.

A test process is tied to its test by the record it writes: CTest runs each
Catch2 case as a process of its own, which opens
build/debug/test-reads/<escaped name>.txt for writing as the case ends and
then exits. Windows reuses process numbers, so the events of one number are
cut into one process each at its record write: everything a number opened
before its record belongs to the process that wrote it. The record is then
read from disk and compared, path for path.

A read is a CreateFile asking for the file's contents -- Generic Read or Read
Data -- not one asking only for its attributes; opening a folder is not one.

    procmon_compare2.py [--plant <file name ending>]

--plant drops that file from every record before comparing, so the
comparison can be seen to report a miss (VERIFICATION.md rule 23).
"""
import collections
import csv
import pathlib
import re
import sys

REPO = pathlib.Path(r"C:\Users\U439644\Projects\untitled")
TREE = REPO / "build" / "debug"
RECORDS = TREE / "test-reads"
CSV = REPO / "build" / "procmon-m1-129.csv"
FOLDER = re.compile(r"(?<!Non-)Directory File")


def norm(path: str) -> str:
    return path.replace("/", "\\").lower()


def access(detail: str) -> str:
    return detail.split("Desired Access: ", 1)[-1].split(", Disposition", 1)[0]


def main() -> int:
    plant = sys.argv[2] if len(sys.argv) == 3 and sys.argv[1] == "--plant" else None
    records_dir, tree_dir, repo_dir = (norm(str(p)) + "\\" for p in (RECORDS, TREE, REPO))
    open_reads = collections.defaultdict(dict)     # pid -> {path: result}, the process now running
    open_name = {}
    processes = []                                 # (program, pid, record path, reads)
    with open(CSV, encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            name = row["Process Name"]
            if not (name.startswith("test_") and name.endswith(".exe")) or row["Operation"] != "CreateFile":
                continue
            pid, path, detail, result = row["PID"], row["Path"], row["Detail"], row["Result"]
            key = norm(path)
            wanted = access(detail)
            if key.startswith(records_dir) and ("Generic Write" in wanted or "Write Data" in wanted):
                processes.append((name, pid, path, open_reads.pop(pid, {})))
                open_name.pop(pid, None)
            elif (("Generic Read" in wanted or "Read Data" in wanted) and not FOLDER.search(detail)
                  and result in ("SUCCESS", "NAME NOT FOUND", "PATH NOT FOUND")):
                open_reads[pid][key] = result
                open_name[pid] = name

    def inside(key: str) -> bool:
        if key.startswith(records_dir):
            return False
        if key.startswith(tree_dir):
            stem = key[len(tree_dir):]
            return not ("\\" not in stem and stem.endswith((".exe", ".dll", ".pdb")))
        return key.startswith(repo_dir)

    misses = []
    for name, pid, record, reads in processes:
        lines = pathlib.Path(record).read_text(encoding="utf-8").splitlines()
        recorded = {norm(line.split(" ", 1)[1]) for line in lines[:-1]}
        if plant:
            recorded = {k for k in recorded if not k.endswith(plant)}
        misses += [(name, pathlib.Path(record).name, key, result)
                   for key, result in sorted(reads.items()) if inside(key) and key not in recorded]
    unrecorded = [(open_name.get(pid, "?"), pid, sorted(k for k in reads if inside(k)))
                  for pid, reads in open_reads.items()]
    print(f"test processes that wrote a record: {len(processes)}; compared path for path")
    print(f"reads in the repository or build tree that the process's record leaves out: {len(misses)}")
    for miss in misses:
        print("  MISSING", miss)
    print(f"processes that read something and wrote no record: {len(unrecorded)}")
    for item in unrecorded:
        print("  NO RECORD", item)
    return 0


if __name__ == "__main__":
    sys.exit(main())

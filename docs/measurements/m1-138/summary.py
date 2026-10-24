"""A short view of procmon-by-test.json: for each script test and each of its
programs, how many files it read, queried and listed in the repository.

    python summary.py <procmon-by-test.json>
"""
import json
import sys

report = json.load(open(sys.argv[1], encoding="utf-8"))
for test, programs in report.items():
    for program, data in programs.items():
        examples = [p.split("untitled\\", 1)[-1] for p in data["read"]][:3]
        print(f"{test:28s} {program:18s} processes {data['processes']:3d}  read {len(data['read']):4d}  "
              f"queried {len(data['queried']):4d}  listed {len(data['listed']):3d}  for example {examples}")

"""M1-138, decision 768: what CMake's trace shows of the `cmake -P` tests.
Each trace file holds one JSON object for each command that a script ran,
with its arguments expanded. This lists the commands that all scripts use,
the files that each script reads or asks about, and the programs that it
starts, and it reports a script that starts Python or names scripts/.

    python trace_analyse.py <trace folder> <trace.json of spike_run.py>
"""
import collections
import json
import pathlib
import sys

folder, summary = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
# The commands whose arguments name a file that the script reads or asks
# about, and the position of that file among the arguments.
FILE_COMMANDS = {"include": 0}
FILE_SUBCOMMANDS = {"READ", "STRINGS", "SHA256", "MD5", "SHA1", "SIZE", "TIMESTAMP", "GLOB", "GLOB_RECURSE",
                    "COPY", "CONFIGURE"}
commands = collections.Counter()
programs = collections.Counter()
alarms, reads = [], collections.defaultdict(set)
for trace in sorted(folder.glob("*.json")):
    test = trace.stem
    for line in trace.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            entry = json.loads(line)
        except json.JSONDecodeError:
            continue
        name = entry.get("cmd")
        if not name:
            continue
        name = name.lower()
        args = entry.get("args", [])
        commands[name] += 1
        if name == "include" and args:
            reads[test].add(args[0])
        elif name == "file" and args and args[0].upper() in FILE_SUBCOMMANDS and len(args) > 1:
            reads[test].add(f"{args[0].upper()} {args[1]}")
        elif name in ("if", "elseif") and "EXISTS" in args:
            reads[test].add(f"EXISTS {args[args.index('EXISTS') + 1]}" if args.index("EXISTS") + 1 < len(args)
                            else "EXISTS ?")
        elif name == "execute_process" and "COMMAND" in args:
            for i, a in enumerate(args):
                if a == "COMMAND" and i + 1 < len(args):
                    program = args[i + 1].replace("\\", "/").rsplit("/", 1)[-1].lower()
                    programs[program] += 1
                    if program.startswith("python"):
                        alarms.append((test, "starts Python", args[i + 1]))
        text = " ".join(args).replace("\\", "/").lower()
        if "/scripts/" in text:
            alarms.append((test, f"names scripts/ in {name}", text[:160]))
rows = json.loads(summary.read_text(encoding="utf-8"))
differ = [r["test"] for r in rows if r["plain"]["verdict"] != r["traced"]["verdict"]]
print(f"traces: {len(list(folder.glob('*.json')))}; tests in the run: {len(rows)}")
print(f"a different verdict with the trace: {differ or 'none'}")
print(f"verdicts: {collections.Counter((r['plain']['verdict'], r['traced']['verdict']) for r in rows)}")
print(f"the CMake commands that the scripts use: {dict(commands.most_common())}")
print(f"the programs that they start: {dict(programs.most_common())}")
print(f"alarms (Python, or a path in scripts/): {alarms or 'none'}")
kinds = collections.Counter(item.split(' ', 1)[0] if ' ' in item and item.split(' ', 1)[0].isupper() else 'include'
                            for items in reads.values() for item in items)
print(f"the kinds of file access: {dict(kinds)}")

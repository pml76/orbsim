"""M1-138, the spike of decision 766: Process Monitor's CSV of one full run,
against the hook's logs. Measurement only.

    procmon_analyse.py <csv> <tree> <spike out folder> [--plant <file name end>]

Each process is tied to its test: by M1138_SPIKE_OWNER or ORBSIM_TEST_NAME in
the environment that its "Process Start" event shows, and else by its parent.
Windows reuses process numbers, so a process is the events of one number from
its "Process Start" to the next start of that number.

A read is a CreateFile that asks for the contents (Generic Read or Read Data)
of a file, not of a folder. A query is a CreateFile or QueryOpen that asks for
attributes only, or a query of a file's information with no open: the
questions that decision 770 is about. A listing is a QueryDirectory. Only
paths in the repository count, the build tree among them, but not the logs of
the hook and not the test-reads folder.

For each script test it writes, by program, what was read, queried and
listed. For each Python process it compares Process Monitor with the hook's
log: a read with no `open` or `module` line, a query with no `probe` line, a
listing with no `list` line. --plant drops each path that ends with the text
from every log first, so that the comparison can be seen to report a miss
(VERIFICATION.md rule 23)."""
import collections
import csv
import json
import pathlib
import re
import subprocess
import sys

csv.field_size_limit(1 << 30)
REPO = r"c:\users\u439644\projects\untitled" + "\\"
FOLDER = re.compile(r"(?<!Non-)Directory File")
QUERY_OPS = {"QueryOpen", "QueryBasicInformationFile", "QueryStandardInformationFile",
             "QueryNetworkOpenInformationFile", "QueryAttributeTagFile", "QueryAllInformationFile"}
FOUND = ("SUCCESS", "NAME NOT FOUND", "PATH NOT FOUND", "NO SUCH FILE", "BUFFER OVERFLOW")


def norm(path):
    return path.replace("/", "\\").lower().rstrip("\\")


def detail_field(detail, key):
    match = re.search(re.escape(key) + r": (.*?)(?:, (?:Command line|Current directory|Environment|Parent PID):|$)",
                      detail, re.S)
    return match.group(1).strip() if match else ""


class Process:
    def __init__(self, pid, parent, command, env):
        self.pid, self.parent, self.command, self.env = pid, parent, command, env
        self.image = ""
        self.owner = None
        self.reads, self.queries, self.listings, self.writes = {}, {}, set(), set()
        self.outside = collections.Counter()


def owner_of(env_text):
    """The test of a process, from the text of its environment: the hook's
    owner first, because a self-test can give a child a test name of its own.
    A value ends at a line end or at the separator of Process Monitor, a `;`
    and a space; `PATH` is never read."""
    for key in ("M1138_SPIKE_OWNER", "ORBSIM_TEST_NAME"):
        match = re.search(r"(?:^|[\s;])" + key + r"=(.*?)(?:;\s|[\r\n]|$)", env_text)
        if match:
            return match.group(1).strip()
    return None


def main():
    csv_path, tree, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
    plant = sys.argv[5].lower() if len(sys.argv) == 6 and sys.argv[4] == "--plant" else None
    # Not counted: the out folder of the spike, the read records of the C++
    # tests, and the hook itself, which is the instrument.
    hook = pathlib.Path(__file__).resolve().parent / "hook"
    skip = (norm(str(out)) + "\\", norm(str(tree / "test-reads")) + "\\", norm(str(hook)))
    current, every = {}, []
    minutes = collections.Counter()  # events in each minute, to find a gap in the recording
    with open(csv_path, encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            pid, op, path, result, detail = row["PID"], row["Operation"], row["Path"], row["Result"], row["Detail"]
            stamp = row["Time of Day"]
            minutes[stamp.split(":")[0] + ":" + stamp.split(":")[1] + stamp[-3:]] += 1
            if op == "Process Start":
                env_text = detail.split("Environment:", 1)[1] if "Environment:" in detail else ""
                proc = Process(pid, detail_field(detail, "Parent PID"), detail_field(detail, "Command line"), env_text)
                proc.image = row["Process Name"].lower()
                # The parent is the process that holds the number now, not a
                # later process that Windows gives the same number.
                proc.parent_proc = current.get(proc.parent)
                current[pid] = proc
                every.append(proc)
                continue
            proc = current.get(pid)
            if proc is None:
                continue
            key = norm(path)
            if key.startswith(skip):
                continue
            if not key.startswith(REPO):
                if op == "CreateFile" and result == "SUCCESS" and not FOLDER.search(detail) and (
                        "Generic Read" in detail or "Read Data" in detail):
                    proc.outside["\\".join(key.split("\\")[:4])] += 1
                continue
            if op == "CreateFile":
                access = detail.split("Desired Access: ", 1)[-1].split(", Disposition", 1)[0]
                if FOLDER.search(detail):
                    continue
                if "Generic Write" in access or "Write Data" in access or "Append Data" in access:
                    proc.writes.add(key)
                if "Generic Read" in access or "Read Data" in access:
                    if result in FOUND:
                        proc.reads.setdefault(key, result)
                elif result in FOUND:
                    proc.queries.setdefault(key, result)
            elif op in QUERY_OPS and result in FOUND:
                proc.queries.setdefault(key, result)
            elif op == "QueryDirectory":
                # The path can end with the pattern of the listing, as `\*`.
                last = key.rsplit("\\", 1)
                proc.listings.add(last[0] if len(last) == 2 and ("*" in last[1] or "?" in last[1]) else key)

    print(f"events in each minute of the recording: {sorted(minutes.items())}")
    listing = json.loads(subprocess.run([cache_ctest(tree), "--show-only=json-v1"], cwd=tree, capture_output=True,
                                        text=True, check=True).stdout)
    script = {t["name"] for t in listing["tests"] if t.get("command")
              and pathlib.Path(t["command"][0]).name.lower().startswith(("python", "cmake"))}
    names = {t["name"] for t in listing["tests"]}
    # The top process of a test, from its command line: Process Monitor cuts
    # the text of a process start at 4,095 characters, and the environment,
    # in alphabetical order, can lose ORBSIM_TEST_NAME there.
    commands = {command_key(" ".join(t["command"])): t["name"] for t in listing["tests"] if t.get("command")}
    for proc in every:
        proc.owner = commands.get(command_key(proc.command))
        if proc.owner is None:
            named = owner_of(proc.env)
            proc.owner = named if named in names else None
    for proc in every:  # a process with no name of its own takes its parent's
        walk, steps = proc, 0
        while walk.owner is None and getattr(walk, "parent_proc", None) is not None and steps < 64:
            walk, steps = walk.parent_proc, steps + 1
        if proc.owner is None:
            proc.owner = walk.owner
    children = collections.defaultdict(list)
    for proc in every:
        if getattr(proc, "parent_proc", None) is not None:
            children[id(proc.parent_proc)].append(proc)
        # A folder that a process opened to list it is a listing, not a read.
        for key in list(proc.reads):
            if key in proc.listings:
                del proc.reads[key]
    report = {}
    for proc in every:
        if proc.owner not in script:
            continue
        entry = report.setdefault(proc.owner, collections.defaultdict(
            lambda: {"processes": 0, "commands": set(), "read": set(), "queried": set(), "listed": set(),
                     "written": set(), "outside": collections.Counter()}))
        program = entry[proc.image]
        program["processes"] += 1
        program["commands"].add(proc.command[:300])
        program["read"] |= set(proc.reads)
        program["queried"] |= set(proc.queries)
        program["listed"] |= proc.listings
        program["written"] |= proc.writes
        program["outside"].update(proc.outside)

    def plain(value):
        if isinstance(value, set):
            return sorted(value)
        if isinstance(value, collections.Counter):
            return dict(value.most_common())
        return value

    with open(out / "procmon-by-test.json", "w", encoding="utf-8") as stream:
        json.dump({t: {p: {k: plain(v) for k, v in d.items()} for p, d in progs.items()}
                   for t, progs in sorted(report.items())}, stream, indent=1)
    print(f"processes: {len(every)}; with a script test: {sum(1 for p in every if p.owner in script)}; "
          f"script tests seen: {len(report)} of {len(script)}")

    logs = {}
    for log in (out / "logs").glob("*.log"):
        lines = log.read_text(encoding="utf-8", errors="replace").split("\n")
        head = dict(l.split("\t", 1) for l in lines[:8] if "\t" in l)
        logs.setdefault(head.get("pid"), []).append((log, lines))
    misses = collections.Counter()
    examples = collections.defaultdict(list)
    compared = 0
    for proc in every:
        if not proc.image.startswith("python") or proc.owner not in script:
            continue
        if not program_of(proc.command).startswith("python"):
            # The name of the process and its command line disagree, as for
            # `git.exe ls-files` under the name python.exe: no hook log is
            # expected from a program that is not Python.
            misses["(examine) a process name that its command line does not agree with"] += 1
            examples["(examine) a process name that its command line does not agree with"].append(
                (proc.owner, proc.command[:120]))
            continue
        # Windows gives a number again, thus a log belongs to a process only
        # when its script also stands on the process's command line.
        found = [(log, ls) for log, ls in logs.get(proc.pid) or [] if script_of(ls).lower() in proc.command.lower()]
        if not found and any(c.image.startswith("python") and arguments_of(c.command)
                             and arguments_of(c.command) == arguments_of(proc.command) for c in children[id(proc)]):
            # A launcher, such as PyManager's python3.exe: it starts the real
            # interpreter with the same arguments, and that one runs the hook.
            misses["(not a fault) a launcher of the interpreter"] += 1
            continue
        if not found:
            misses["a Python process with no log"] += 1
            examples["a Python process with no log"].append((proc.owner, proc.command[:120]))
            continue
        lines = [l for _, ls in found for l in ls]
        if plant:
            lines = [l for l in lines if not (len(l.split("\t")) > 1 and l.split("\t")[1].lower().endswith(plant))
                     and not (len(l.split("\t")) > 2 and l.split("\t")[2].lower().endswith(plant))]
        compared += 1
        opened = {norm(l.split("\t")[1]) for l in lines if l.startswith(("open\t", "module\t"))}
        probed = {norm(l.split("\t")[2]) for l in lines if l.startswith("probe\t")}
        listed = {norm(l.split("\t")[1]) for l in lines if l.startswith("list\t")}
        # Windows opens the program file of each process that the Python
        # process starts; a `start` line of the hook names that program.
        started = started_programs(lines)
        opened |= {key for key in set(proc.reads) | set(proc.queries)
                   if key in started or (key.endswith(".exe") and key.rsplit("\\", 1)[-1] in started)}
        for key in proc.reads:
            if key not in opened:
                misses["a read with no open or module line"] += 1
                examples["a read with no open or module line"].append((proc.owner, key))
        for key in proc.queries:
            # A question inside a folder that the process listed is covered
            # by the hash of the names in that folder.
            if key not in probed and key not in opened and key not in listed and key.rsplit("\\", 1)[0] not in listed:
                kind = ("(examine) a query of a folder with no probe line" if pathlib.Path(key).is_dir()
                        else "a query with no probe line")
                misses[kind] += 1
                examples[kind].append((proc.owner, key))
        for key in proc.listings:
            if key not in listed:
                misses["a listing with no list line"] += 1
                examples["a listing with no list line"].append((proc.owner, key))
    print(f"Python processes compared with their logs: {compared}")
    for kind, n in misses.most_common():
        print(f"  {n:6d}  {kind}")
        for example in sorted(set(map(tuple, examples[kind])))[:12]:
            print(f"            {example}")
    with open(out / "procmon-misses.json", "w", encoding="utf-8") as stream:
        json.dump({k: sorted(set(map(tuple, v))) for k, v in examples.items()}, stream, indent=1)
    return 0


def started_programs(lines):
    """The program of each `start` line of a hook log, as its normalised
    path and as its file name, with `.exe` added where it has no suffix."""
    import ast
    found = set()
    for line in lines:
        if not line.startswith("start\t"):
            continue
        try:
            argv = ast.literal_eval(line.split("\t")[1])
        except (ValueError, SyntaxError):
            continue
        first = argv if isinstance(argv, (str, bytes)) else (argv[0] if argv else "")
        first = first.decode("utf-8", "replace") if isinstance(first, bytes) else str(first)
        first = first.split()[0] if isinstance(argv, str) and first else first
        if not first:
            continue
        name = first.replace("/", "\\").rsplit("\\", 1)[-1].lower()
        found |= {norm(first), name, name if name.endswith(".exe") else name + ".exe"}
    return found


def program_of(command):
    """The file name of the program of a command line, in small letters."""
    text = command.strip()
    first = text[1:].split('"', 1)[0] if text.startswith('"') else text.split(" ", 1)[0]
    return first.replace("/", "\\").rsplit("\\", 1)[-1].lower()


def arguments_of(command):
    """A command line without its program, in the form of command_key. A
    program in quotation marks can hold spaces, as `C:/Program Files/...`."""
    text = command.strip()
    if text.startswith('"'):
        rest = text[1:].split('"', 1)
        text = rest[1] if len(rest) == 2 else ""
    else:
        text = text.split(" ", 1)[1] if " " in text else ""
    return command_key(text)


def command_key(text):
    """A command line in one form for a comparison: no quotation marks, one
    space between the parts, forward slashes, small letters."""
    return " ".join(text.replace('"', " ").replace("\\", "/").lower().split())


def script_of(lines):
    """The script name of a hook log, from its `argv` line: the last part of
    its first argument, or `-c` for a command given on the command line."""
    import ast
    for line in lines[:8]:
        if line.startswith("argv\t"):
            try:
                argv = ast.literal_eval(line.split("\t", 1)[1])
            except (ValueError, SyntaxError):
                return ""
            first = argv[0] if argv else ""
            return first.replace("/", "\\").rsplit("\\", 1)[-1] if first else ""
    return ""


def cache_ctest(tree):
    for line in (tree / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith("CMAKE_CTEST_COMMAND:"):
            return line.split("=", 1)[1]
    raise SystemExit("no CMAKE_CTEST_COMMAND in the cache")


if __name__ == "__main__":
    sys.exit(main())

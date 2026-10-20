"""M1-132 step 2 (decisions 560 and 709 (8)): each recorded address against the procedure ranges of the .pdb file.

    python -I docs/measurements/m1-132/procedure-ranges.py <tree> <records folder>
        <llvm-pdbutil> <llvm-undname> [--show N]

Run from the root of the repository. This is a measurement, done one time:
it reads the text of llvm-pdbutil and of llvm-undname, which
scripts/compiled-code.py does not do (decision 546). It loads read_record from
scripts/check-coverage-recorder.py, and the map's reader from
scripts/compiled-code.py.

For each program at the top of <tree> that a record gives, the script reads:

  - the procedures of the .pdb file: `llvm-pdbutil dump --symbols`, the
    records S_GPROC32 and S_LPROC32, each with `addr = <section>:<offset>`
    and `code size = <length>`. The two numbers of `addr` are decimal: the
    procedure at `0001:0048` is at the offset 0x30 in the map. Also the
    records S_THUNK32, with `size = <length>` and `addr`: a thunk is a
    procedure too (decision 712), and its records are also counted apart;
  - the section headers: `llvm-pdbutil dump --section-headers`, the virtual
    address of each section, in hexadecimal.

The start of a procedure, less the base address, is the virtual address of
its section and its offset, as a recorded address is. For each different
recorded address of the run, the script must find one procedure, and only
one, that starts at that address, and no other function of the map (a
symbol in a code section) that starts in the range of that procedure. The
symbols of the map in a range are put into three classes:

  - a funclet: the name starts with `?dtor$`, `?catch$`, `?fin$` or `?filt$`
    and holds the decorated name of the function at the address. clang puts
    the code of a clean-up or of a `catch` block after its function, and the
    .pdb file gives one range for the two;
  - a label: the name starts with `$ehgcr_`. key-agreement.py shows that the
    objects give each such symbol as a static symbol that is not of function
    type;
  - each other symbol, which is a disagreement.

An address with no procedure, or with two, is a disagreement too. A
procedure is an S_GPROC32, S_LPROC32 or S_THUNK32 record (decision 712):
Microsoft's cvinfo.h gives S_THUNK32 an offset, a segment and a length, as
a procedure has, and LLVM writes it, not a procedure record, for a thunk.

The names are compared for information only: the .pdb file gives readable
names, and the map gives decorated names. llvm-undname gives the readable
text of each map name. The script counts the procedures with a name that is
equal to the map name, or that is a part of the text of llvm-undname when the
two texts are without spaces and without the words `class`, `struct`, `union`
and `enum`.

The instrument fails one time before its result is used (VERIFICATION.md
rule 23): the same check on each recorded address plus 1 must find no
procedure that starts there.
"""

import bisect
import collections
import importlib.util
import re
import subprocess
import sys
from pathlib import Path, PureWindowsPath

PROCEDURE = re.compile(r"^\s*\d+ \| S_([GL])PROC32 \[size = \d+\] `(.*)`\s*$")
THUNK = re.compile(r"^\s*\d+ \| S_THUNK32 \[size = \d+\] `(.*)`\s*$")
ADDRESS = re.compile(r"addr = (\d+):(\d+), code size = (\d+)")
THUNK_ADDRESS = re.compile(r"size = (\d+), addr = (\d+):(\d+)")
SECTION = re.compile(r"^\s*SECTION HEADER #(\d+)\s*$")
VIRTUAL_ADDRESS = re.compile(r"^\s*([0-9A-Fa-f]+) virtual address\s*$")
FUNCLET_PREFIXES = ("?dtor$", "?catch$", "?fin$", "?filt$")
LABEL_PREFIX = "$ehgcr_"
# The words that llvm-undname writes before a type and the .pdb file does not.
TYPE_WORDS = re.compile(r"\b(class|struct|union|enum)\b|\s+")


def load(name: str, path: Path):
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def section_addresses(pdbutil: str, pdb: Path) -> dict:
    text = subprocess.run([pdbutil, "dump", "--section-headers", str(pdb)], capture_output=True, text=True,
                          check=True).stdout
    found, current = {}, None
    for line in text.splitlines():
        match = SECTION.match(line)
        if match:
            current = int(match.group(1))
            continue
        match = VIRTUAL_ADDRESS.match(line)
        if match and current is not None:
            found[current] = int(match.group(1), 16)
            current = None
    return found


def procedures(pdbutil: str, pdb: Path, sections: dict) -> tuple:
    """([(start, length, type, name)], [(start, length, name)] of the thunks,
    the count of records with a section that the headers do not give). Each
    start is less the base address."""
    found, thunks, unknown = [], [], 0
    pending, pending_thunk = None, None
    with subprocess.Popen([pdbutil, "dump", "--symbols", str(pdb)], stdout=subprocess.PIPE, text=True,
                          encoding="utf-8", errors="replace") as run:
        for line in run.stdout:
            if pending is not None:
                match = ADDRESS.search(line)
                if match:
                    section, offset, length = (int(g) for g in match.groups())
                    if section in sections:
                        found.append((sections[section] + offset, length, pending[0], pending[1]))
                    else:
                        unknown += 1
                pending = None
                continue
            if pending_thunk is not None:
                match = THUNK_ADDRESS.search(line)
                if match:
                    length, section, offset = (int(g) for g in match.groups())
                    if section in sections:
                        thunks.append((sections[section] + offset, length, pending_thunk))
                    else:
                        unknown += 1
                    pending_thunk = None
                elif "|" in line:
                    pending_thunk = None
                continue
            match = PROCEDURE.match(line)
            if match:
                pending = (match.group(1), match.group(2))
                continue
            match = THUNK.match(line)
            if match:
                pending_thunk = match.group(1)
    if run.returncode != 0:
        raise RuntimeError(f"llvm-pdbutil ended with {run.returncode} for {pdb}")
    return found, thunks, unknown


def undecorated(undname: str, names: list) -> dict:
    """{map name: the text of llvm-undname, or None}. On its output, llvm-undname
    writes the name, then its readable text, then an empty line. For a name
    that it cannot read, the readable text is an empty line, and the error is
    on the error output."""
    if not names:
        return {}
    done = subprocess.run([undname], input="\n".join(names) + "\n", capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    lines = done.stdout.split("\n")
    found, at = {}, 0
    for name in names:
        while at < len(lines) and lines[at] != name:
            at += 1
        result = lines[at + 1] if at + 1 < len(lines) else ""
        found[name] = None if not result or result.startswith("error:") else result
        at += 2
    return found


def judge(addresses: set, starts: dict, thunk_starts: dict, code_starts: list, at_address) -> tuple:
    """(counts, [disagreement]) for the addresses. `at_address(address)` gives the map's names there."""
    counts, wrong = collections.Counter(), []
    for address in sorted(addresses):
        found = starts.get(address, [])
        counts[f"addresses where procedure records (S_GPROC32, S_LPROC32, S_THUNK32) start: {min(len(found), 2)}"
               + (" or more" if len(found) >= 2 else "")] += 1
        if len(found) != 1:
            thunk = thunk_starts.get(address, [])
            if not found:
                counts[f"addresses where no procedure record starts, "
                       f"with {len(thunk)} S_THUNK32 there"] += 1
            wrong.append((address, f"{len(found)} procedures; S_THUNK32 there: {[t[2] for t in thunk]}",
                          [p[3] for p in found], at_address(address)))
            continue
        start, length, _, name = found[0]
        low = bisect.bisect_right(code_starts, start)
        high = bisect.bisect_left(code_starts, start + length)
        own = at_address(address)
        funclets, labels, others = [], [], []
        for other in code_starts[low:high]:
            for symbol_name in at_address(other):
                if symbol_name.startswith(FUNCLET_PREFIXES) and any(o in symbol_name for o in own):
                    funclets.append(symbol_name)
                elif symbol_name.startswith(LABEL_PREFIX):
                    labels.append(symbol_name)
                else:
                    others.append(symbol_name)
        counts["one procedure: no other symbol of the map in its range"] += not (funclets or labels or others)
        counts["one procedure: only funclets and labels in its range"] += bool(funclets or labels) and not others
        counts["one procedure: a different symbol of the map in its range (disagreement)"] += bool(others)
        counts["funclets in the ranges"] += len(funclets)
        counts["labels $ehgcr_ in the ranges"] += len(labels)
        counts["ranges with a funclet"] += bool(funclets)
        counts["ranges with a label"] += bool(labels)
        if others:
            wrong.append((address, f"in the range of {name} ({start:#x}, {length} bytes)", others, own))
    return counts, wrong


def normal(text: str) -> str:
    return TYPE_WORDS.sub("", text)


def short(names: list, width: int = 160) -> list:
    return [n if len(n) <= width else n[:width] + "..." for n in names]


def main(argv: list) -> int:
    tree, folder, pdbutil, undname = Path(argv[1]).resolve(), Path(argv[2]), argv[3], argv[4]
    show = int(argv[argv.index("--show") + 1]) if "--show" in argv else 5
    recorder = load("check_coverage_recorder", Path.cwd() / "scripts" / "check-coverage-recorder.py")
    compiled = recorder.compiled_code
    recorded = collections.defaultdict(set)
    occurrences = collections.Counter()
    for file in sorted(folder.glob("*.functions.txt")):
        record = recorder.read_record(file.read_text(encoding="utf-8"))
        if record is None:
            continue
        path = Path(PureWindowsPath(record.program))
        if path.is_file() and path.resolve().parent == tree:
            recorded[path].update(record.functions)
            occurrences[path] += len(record.functions)
    totals, planted = collections.Counter(), collections.Counter()
    disagreements, names, name_examples = [], collections.Counter(), []
    for path in sorted(recorded):
        linker_map = compiled.read_map(path.with_suffix(".map").read_text(encoding="utf-8", errors="replace"))
        index = compiled.address_index(linker_map)
        code_starts = sorted(index)
        pdb = path.with_suffix(".pdb")
        found, thunks, unknown = procedures(pdbutil, pdb, section_addresses(pdbutil, pdb))
        starts, thunk_starts = collections.defaultdict(list), collections.defaultdict(list)
        for procedure in found:
            starts[procedure[0]].append(procedure)
        for thunk in thunks:
            thunk_starts[thunk[0]].append(thunk)
            # A thunk is a procedure (decision 712).
            starts[thunk[0]].append((thunk[0], thunk[1], "T", thunk[2]))

        def at_address(address: int) -> list:
            return sorted(s.name for s in index.get(address, []))

        counts, wrong = judge(recorded[path], starts, thunk_starts, code_starts, at_address)
        totals.update(counts)
        totals["programs"] += 1
        totals["S_GPROC32 and S_LPROC32 records in the .pdb files"] += len(found)
        totals["S_THUNK32 records in the .pdb files"] += len(thunks)
        totals["records with a section that the section headers do not give"] += unknown
        totals["procedure starts that are the address of a code symbol of the map"] += sum(p[0] in index for p in found)
        totals["different recorded addresses"] += len(recorded[path])
        totals["recorded addresses in all records"] += occurrences[path]
        disagreements.extend((path.name, *w) for w in wrong)
        fault, _ = judge({a + 1 for a in recorded[path]}, starts, thunk_starts, code_starts, at_address)
        planted.update(fault)
        chosen = {}
        for address in recorded[path]:
            symbol = compiled._one_function(index.get(address, []))
            if symbol is not None and len(starts.get(address, [])) == 1:
                chosen[address] = (symbol.name, starts[address][0][3])
        readable = undecorated(undname, sorted({name for name, _ in chosen.values()}))
        for map_name, pdb_name in chosen.values():
            text = readable.get(map_name)
            if pdb_name == map_name:
                names["the .pdb name equals the map name"] += 1
            elif text is None:
                names["llvm-undname does not read the map name"] += 1
            elif normal(pdb_name) in normal(text):
                names["the .pdb name is a part of the text of llvm-undname"] += 1
            else:
                names["the .pdb name is not a part of the text of llvm-undname"] += 1
                name_examples.append(f"{path.name}: {short([map_name])[0]} -> {short([text])[0]!r}; "
                                     f".pdb {short([pdb_name])[0]!r}")
        print(f"{path.name}: {len(found)} procedures, {len(thunks)} thunks, {len(recorded[path])} different "
              f"recorded addresses, {len(wrong)} disagreements")
    print()
    print(f"{tree}:")
    for key, value in sorted(totals.items()):
        print(f"  {key}: {value:,}")
    print("the names, for information:")
    for key, value in sorted(names.items()):
        print(f"  {key}: {value:,}")
    for line in name_examples[:show]:
        print(f"  example: {line}")
    print("the instrument, each recorded address plus 1:")
    for key, value in sorted(planted.items()):
        print(f"  {key}: {value:,}")
    print(f"disagreements ({len(disagreements)}):")
    for program, address, what, found_names, own in disagreements:
        print(f"  {program} {address:#x}: {what}: {short(found_names)}; the map's names at the address: {short(own)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

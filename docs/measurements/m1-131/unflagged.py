"""M1-131 item 4 (decision 586): the functions without a flag, object by object.

    python -I unflagged.py <tree> <out.txt>

For every object of <tree>'s compile database that carries the coverage hook
-- whose compile command names -fsanitize-coverage= -- this reads the
object's sections, symbols and relocations (COFF, ordinary and "big
object"). An object that holds no section .SCOVP$M, the hook's address table
(decision 584), is listed apart with its count of functions: either the hook
left out every function it has, or its command did not ask for the table.
For the others it sets two lists against each other:

  * the functions the object defines: each symbol of the function type (0x20)
    defined in one of its sections, external or static -- keyed by the
    section and the offset in it, so that two names for one place count once;
  * the functions with an entry in the address table: the place each
    relocation of a .SCOVP$M section points at (its symbol's section and
    offset, plus the 8-byte value at the relocation, its addend).

A function of the first list that is not in the second carries no flag: its
entry is never recorded. Printed: the counts over all objects, the counts by
object, and each unflagged function's name, decorated and as llvm-undname reads
it, sorted into the scope it is defined in (std, the project's `orb`, Catch2,
the rest). <out.txt> receives the full list.
"""

import collections
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

UNDNAME = r"C:\GitHub\clang+llvm-23.1.2-x86_64-pc-windows-msvc\bin\llvm-undname.exe"
REPO = "c:/users/u439644/projects/untitled/"
BIGOBJ_CLASS = bytes([0xC7, 0xA1, 0xBA, 0xD1, 0xEE, 0xBA, 0xA9, 0x4B,
                      0xAF, 0x20, 0xFA, 0xF6, 0x6A, 0xA4, 0xDC, 0xB8])
ADDR64 = 0x0001            # IMAGE_REL_AMD64_ADDR64 (Microsoft's PE format specification)
NRELOC_OVFL = 0x01000000   # IMAGE_SCN_LNK_NRELOC_OVFL: the count is in the first relocation
FUNCTION_TYPE = 0x20       # a symbol's Type for a function, as Microsoft's tools set it
EXTERNAL, STATIC = 2, 3    # IMAGE_SYM_CLASS_EXTERNAL, IMAGE_SYM_CLASS_STATIC


def string_at(table: bytes, offset: int) -> str:
    end = table.find(b"\0", offset)
    return table[offset:end if end >= 0 else len(table)].decode("utf-8", errors="replace")


def base64_offset(text: str) -> int:
    alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    value = 0
    for character in text:
        value = value * 64 + alphabet.index(character)
    return value


def read_coff(data: bytes) -> tuple:
    """(sections, symbols): sections a list of dicts, 1-based by index + 1;
    symbols a dict {index: (name, value, section number, type, class)}."""
    if data[0:4] == b"\0\0\xff\xff" and data[12:28] == BIGOBJ_CLASS:
        count, symbols_at, symbol_count = struct.unpack_from("<III", data, 44)
        table_at, record, section_format = 56, 20, "<i"
    else:
        _, count, _, symbols_at, symbol_count, optional, _ = struct.unpack_from("<HHIIIHH", data, 0)
        table_at, record, section_format = 20 + optional, 18, "<h"
    strings = data[symbols_at + record * symbol_count:]
    sections = []
    for index in range(count):
        at = table_at + 40 * index
        raw = data[at:at + 8].rstrip(b"\0").decode("utf-8", errors="replace")
        if raw.startswith("//"):
            raw = string_at(strings, base64_offset(raw[2:]))
        elif raw.startswith("/") and raw[1:].isdigit():
            raw = string_at(strings, int(raw[1:]))
        size, raw_at, relocations_at = struct.unpack_from("<III", data, at + 16)
        relocation_count = struct.unpack_from("<H", data, at + 32)[0]
        characteristics = struct.unpack_from("<I", data, at + 36)[0]
        sections.append({"name": raw, "size": size, "raw": raw_at, "relocations": relocations_at,
                         "relocation_count": relocation_count, "flags": characteristics})
    symbols = {}
    index = 0
    while index < symbol_count:
        at = symbols_at + record * index
        if data[at:at + 4] == b"\0\0\0\0":
            name = string_at(strings, struct.unpack_from("<I", data, at + 4)[0])
        else:
            name = data[at:at + 8].rstrip(b"\0").decode("utf-8", errors="replace")
        value = struct.unpack_from("<I", data, at + 8)[0]
        section = struct.unpack_from(section_format, data, at + 12)[0]
        type_at = at + 12 + (4 if record == 20 else 2)
        kind, storage, aux = struct.unpack_from("<HBB", data, type_at)
        symbols[index] = (name, value, section, kind, storage)
        index += 1 + aux
    return sections, symbols


def relocations(data: bytes, section: dict) -> list:
    """[(offset, symbol index, type)] of one section."""
    count, at = section["relocation_count"], section["relocations"]
    found = [struct.unpack_from("<IIH", data, at + 10 * i) for i in range(count)]
    if section["flags"] & NRELOC_OVFL and count == 0xFFFF:
        real = found[0][0]
        found = [struct.unpack_from("<IIH", data, at + 10 * i) for i in range(1, real)]
    return found


def judge(data: bytes) -> dict:
    sections, symbols = read_coff(data)
    functions = collections.defaultdict(list)
    for name, value, section, kind, storage in symbols.values():
        if kind == FUNCTION_TYPE and section > 0 and storage in (EXTERNAL, STATIC):
            functions[(section, value)].append(name)
    targets, entries, other = set(), 0, 0
    for number, section in enumerate(sections, start=1):
        if section["name"] != ".SCOVP$M":
            continue
        entries += section["size"] // 16
        for offset, index, kind in relocations(data, section):
            if kind != ADDR64:
                other += 1
                continue
            _, value, target_section, _, _ = symbols[index]
            addend = struct.unpack_from("<Q", data, section["raw"] + offset)[0]
            targets.add((target_section, value + addend))
    return {"functions": functions, "targets": targets, "entries": entries, "other relocations": other,
            "hooked": any(s["name"] == ".SCOVP$M" for s in sections)}


def demangle(names: list) -> dict:
    found = {}
    for start in range(0, len(names), 2000):
        chunk = names[start:start + 2000]
        # Read from standard input: a long list does not fit a command line.
        out = subprocess.run([UNDNAME], input="".join(f"{n}\n" for n in chunk), capture_output=True,
                             text=True, errors="replace").stdout
        # llvm-undname prints each name, then its reading, then an empty line.
        lines = [line for line in out.splitlines()]
        position = 0
        for name in chunk:
            while position < len(lines) and lines[position] != name:
                position += 1
            reading = lines[position + 1] if position + 1 < len(lines) else name
            found[name] = reading
            position += 2
    return found




# Symbols of the function type that are not functions a caller enters:
# Windows' exception handling compiles each cleanup and each catch block of a
# function into a piece of its own, a "funclet", named ?dtor$<n>@?0?<parent>,
# ?catch$<n>@..., ?fin$<n>@... or ?filt$<n>@..., which only the exception
# machinery runs; and the hook's own start-up function, sancov.module_ctor*.
FUNCLET = re.compile(r"^\?(dtor|catch|fin|filt)\$\d+@")
HOOK_OWN = "sancov."
CALLING = re.compile(r"__(cdecl|stdcall|fastcall|vectorcall|thiscall|clrcall) ")


def outermost(reading: str) -> str:
    """The outermost scope of the function's qualified name, read from the
    demangled text: what follows the calling convention, up to its first
    `::` outside <>, () and `' -- for a lambda, the scope of the function
    that holds it."""
    match = CALLING.search(reading)
    if not match:
        return "(no calling convention) " + reading.split("(")[0][:60]
    name = reading[match.end():]
    if name.startswith("`dynamic"):
        return "`dynamic initializer or atexit destructor'"
    if name.startswith("`anonymous namespace'"):
        return "`anonymous namespace'"
    if name.startswith("`"):
        depth = 0
        for position, character in enumerate(name):
            if character == "`":
                depth += 1
            elif character == "'":
                depth -= 1
                if depth == 0:
                    return outermost(name[1:position])
        return "(unreadable) " + name[:60]
    depth = 0
    for position, character in enumerate(name):
        if character == "(" and depth == 0:
            break
        if character in "<`(":
            depth += 1
        elif character in ">')":
            depth -= 1
        elif depth == 0 and name.startswith("::", position):
            return name[:position]
    return "(global)"


def main(argv: list) -> int:
    tree, out = Path(argv[1]), Path(argv[2])
    database = json.loads((tree / "compile_commands.json").read_text(encoding="utf-8"))
    totals = collections.Counter()
    unflagged = []
    by_object = []
    no_table = []
    for entry in database:
        obj = Path(entry["output"])
        if not obj.is_absolute():
            obj = Path(entry["directory"]) / obj
        if obj.suffix != ".obj" or not obj.is_file():
            continue
        # An object carries the hook when its compile command names it -- not
        # only when it holds a table: an object whose every function the hook
        # leaves out has no table at all, and its functions count too.
        command = entry.get("command", " ".join(entry.get("arguments", [])))
        if "-fsanitize-coverage=" not in command:
            continue
        result = judge(obj.read_bytes())
        if not result["hooked"]:
            real = sum(1 for names in result["functions"].values()
                       if not FUNCLET.match(names[0]) and not names[0].startswith(HOOK_OWN))
            no_table.append((entry["file"].replace("\\", "/"), real,
                             "-fno-sanitize-coverage=pc-table" not in command))
            continue
        functions, targets = result["functions"], result["targets"]
        stray = [place for place in targets if place not in functions]
        totals["objects"] += 1
        totals["function-type symbols (places)"] += len(functions)
        totals["table entries"] += result["entries"]
        totals["entries pointing at a function"] += len(targets) - len(stray)
        totals["entries pointing elsewhere"] += len(stray)
        totals["relocations other than ADDR64"] += result["other relocations"]
        source = entry["file"].replace("\\", "/")
        missing_here = 0
        real_here = 0
        for place, names in functions.items():
            name = names[0]
            flagged = place in targets
            if FUNCLET.match(name):
                totals["funclets"] += 1
                totals["funclets with an entry"] += flagged
                continue
            if name.startswith(HOOK_OWN):
                totals["the hook's own start-up functions"] += 1
                totals["the hook's own start-up functions with an entry"] += flagged
                continue
            real_here += 1
            totals["functions"] += 1
            if not flagged:
                totals["functions without a flag"] += 1
                missing_here += 1
                unflagged.append((source, name))
        by_object.append((source, real_here, missing_here))
    readings = demangle(sorted({name for _, name in unflagged}))
    scopes = collections.Counter()
    naming_ours = 0
    lines = []
    for source, name in unflagged:
        reading = readings.get(name, name)
        scope = outermost(reading)
        scopes[scope] += 1
        naming_ours += "orb::" in reading
        lines.append(f"{scope}\t{source.lower().removeprefix(REPO)}\t{name}\t{reading}")
    share = 100 * totals["functions without a flag"] / max(1, totals["functions"])
    print(f"objects carrying the hook: {totals['objects']}")
    for key in ("function-type symbols (places)", "funclets", "funclets with an entry",
                "the hook's own start-up functions", "the hook's own start-up functions with an entry",
                "functions", "table entries", "entries pointing at a function", "entries pointing elsewhere",
                "relocations other than ADDR64", "functions without a flag"):
        print(f"  {key}: {totals[key]:,}")
    print(f"  share of functions without a flag: {share:.2f} %")
    print(f"objects compiled with the hook that hold no table: {len(no_table)}")
    for source, real, table_asked in no_table:
        print(f"  {source.lower().removeprefix(REPO)}: {real} functions, the table "
              f"{'asked for' if table_asked else 'not asked for (no pc-table)'}")
    print(f"  without a flag and naming the project's namespace orb:: anywhere: {naming_ours}")
    print("without a flag, by the outermost scope of the name:")
    for scope, count in scopes.most_common():
        print(f"  {scope}: {count}")
    bases = collections.Counter()
    for line in lines:
        reading = line.split("\t")[3]
        match = CALLING.search(reading)
        tail = reading[match.end():] if match else reading
        bases[re.sub(r"<.*", "", tail.split("(")[0]).split("::")[-1]] += 1
    print("without a flag, the most frequent last parts of the name:")
    for base, count in bases.most_common(30):
        print(f"  {base}: {count}")
    print("objects with the most functions without a flag:")
    for source, count, missing in sorted(by_object, key=lambda r: -r[2])[:8]:
        print(f"  {source.lower().removeprefix(REPO)}: {missing} of {count}")
    out.write_bytes(("\n".join(sorted(lines)) + "\n").encode("utf-8"))
    print(f"the full list: {out.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

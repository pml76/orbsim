"""M1-131 item 4 (decision 551): no two functions at one address.

    python -I address-table.py <tree> [--show N]

For each program at the top of <tree> that carries the coverage hook -- that
holds the section .SCOVP, the hook's address table -- this reads the table
from the program's file and the program's map (lld-link's /map, beside it),
and requires each entry of the table to name exactly one function in the map.

The table is .SCOVP: the recorder's start marker (8 bytes), then one entry of
two 8-byte words for each function -- the function's address, with the
program's preferred load address added, and the hook's note that it is a
function's entry (1) -- then the stop marker (8 bytes). The flags are .SCOV:
the start marker (8 bytes), one byte for each function, the stop marker (1
byte). A function in the map is a symbol in a section of class CODE
(scripts/compiled-code.py's read_map, which this loads).

Printed for each program: the entries, the flags, the entries naming one
function, none, and more than one; and the addresses that occur twice in the
table. Then the totals, and up to N examples of each kind of exception.
"""

import collections
import importlib.util
import struct
import sys
from pathlib import Path

REPO = Path(r"C:\Users\U439644\Projects\untitled")


def load(name: str, file: str):
    spec = importlib.util.spec_from_file_location(name, REPO / "scripts" / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def pe_sections(data: bytes) -> tuple:
    """(image base, {name: (virtual address, virtual size, raw bytes)})."""
    header = struct.unpack_from("<I", data, 0x3C)[0]
    if data[header:header + 4] != b"PE\0\0":
        raise ValueError("not a PE program")
    count = struct.unpack_from("<H", data, header + 6)[0]
    optional_size = struct.unpack_from("<H", data, header + 20)[0]
    optional = header + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic != 0x20B:
        raise ValueError("not a PE32+ program")
    base = struct.unpack_from("<Q", data, optional + 24)[0]
    found = {}
    for index in range(count):
        at = optional + optional_size + 40 * index
        name = data[at:at + 8].rstrip(b"\0").decode("utf-8", errors="replace")
        vsize, vaddr, rsize, rptr = struct.unpack_from("<IIII", data, at + 8)
        raw = data[rptr:rptr + min(vsize, rsize)] + b"\0" * max(0, vsize - rsize)
        found[name] = (vaddr, vsize, raw)
    return base, found


def main(argv: list) -> int:
    tree = Path(argv[1])
    show = int(argv[argv.index("--show") + 1]) if "--show" in argv else 5
    compiled = load("compiled_code", "compiled-code.py")
    totals = collections.Counter()
    examples = collections.defaultdict(list)
    programs = 0
    without = []
    for program in sorted(tree.glob("*.exe")):
        data = program.read_bytes()
        base, sections = pe_sections(data)
        if ".SCOVP" not in sections:
            without.append(program.name)
            continue
        programs += 1
        _, table_size, table = sections[".SCOVP"]
        _, flags_size, _ = sections[".SCOV"]
        words = struct.unpack_from(f"<{table_size // 8}Q", table)
        start, body, stop = words[0], words[1:-1], words[-1]
        entries = list(zip(body[0::2], body[1::2]))
        map_path = program.with_suffix(".map")
        linker_map = compiled.read_map(map_path.read_text(encoding="utf-8", errors="replace"))
        if linker_map is None:
            print(f"{program.name}: its map cannot be read")
            totals["maps unreadable"] += 1
            continue
        by_address = collections.defaultdict(list)
        for symbol in linker_map.symbols:
            if symbol.code:
                by_address[symbol.address].append(symbol)
        counts = collections.Counter()
        seen = collections.Counter(pc for pc, _ in entries)
        twice = {pc: n for pc, n in seen.items() if n > 1}
        notes = collections.Counter(note for _, note in entries)
        for pc, note in entries:
            named = by_address.get(pc - base, [])
            kind = "one" if len(named) == 1 else "none" if not named else "several"
            counts[kind] += 1
            if kind == "several":
                # A class's deleting destructor has two names in Microsoft's
                # scheme, the scalar one (??_G) and the vector one (??_E);
                # clang emits the second as another name for the first.
                names = sorted(s.name for s in named)
                same_object = len({(s.library, s.object) for s in named}) == 1
                alias = (len(names) == 2 and names[0].startswith("??_E") and names[1].startswith("??_G")
                         and names[0][4:] == names[1][4:] and same_object)
                sort = "several: one destructor's two names (??_E, ??_G)" if alias else "several: other"
                totals[sort] += 1
                if not alias:
                    examples["several, other"].append(f"{program.name} {pc - base:#x}: " +
                                                      ", ".join(f"{s.name} ({s.object})" for s in named))
            if kind != "one" and len(examples[kind]) < show:
                examples[kind].append(f"{program.name} {pc - base:#x}: " +
                                      ", ".join(f"{s.name} ({s.object})" for s in named))
        for pc in list(twice)[:show]:
            if len(examples["twice"]) < show:
                examples["twice"].append(f"{program.name} {pc - base:#x} x{twice[pc]}: " +
                                         ", ".join(s.name for s in by_address.get(pc - base, [])))
        flag_count = flags_size - 8 - 1
        print(f"{program.name}: base {base:#x} (map {linker_map.base:#x}), {len(entries)} entries, "
              f"{flag_count} flags (.SCOV {flags_size} bytes), markers {start}/{stop}, notes {dict(notes)}; "
              f"one function {counts['one']}, none {counts['none']}, several {counts['several']}; "
              f"addresses twice in the table {len(twice)}")
        totals["entries"] += len(entries)
        totals["one"] += counts["one"]
        totals["none"] += counts["none"]
        totals["several"] += counts["several"]
        totals["twice"] += len(twice)
        totals["flags differ from entries"] += flag_count != len(entries)
        totals["base differs from map"] += base != linker_map.base
        totals["markers not zero"] += start != 0 or stop != 0
        totals["notes other than 1"] += sum(n for note, n in notes.items() if note != 1)
    print()
    print(f"programs carrying the hook: {programs}; without it: {len(without)} {without}")
    for key in ("entries", "one", "none", "several", "several: one destructor's two names (??_E, ??_G)",
                "several: other", "twice", "flags differ from entries",
                "base differs from map", "markers not zero", "notes other than 1", "maps unreadable"):
        print(f"  {key}: {totals[key]:,}")
    for kind, lines in examples.items():
        print(f"examples, {kind}:")
        for line in lines:
            print(f"  {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

"""Are the ??_E / ??_G pairs one function with two names in each object?

    python -I alias-check.py <tree>

For each object of the tree's compile database that carries the hook, reads
its symbol table, auxiliary records included. A vector deleting destructor
??_E<rest> that clang emits as another name for the scalar one ??_G<rest> is
a weak external (storage class 105) whose auxiliary record names its target
by symbol index (Microsoft's PE format specification, "Auxiliary Format 3:
Weak Externals"). Counted: each ??_E that is such a name for its own ??_G,
each that is a weak external naming something else, and each that is
defined in a section of its own (a function apart).
"""

import collections
import json
import struct
import sys
from pathlib import Path

BIGOBJ_CLASS = bytes([0xC7, 0xA1, 0xBA, 0xD1, 0xEE, 0xBA, 0xA9, 0x4B,
                      0xAF, 0x20, 0xFA, 0xF6, 0x6A, 0xA4, 0xDC, 0xB8])
WEAK_EXTERNAL = 105


def symbols_of(data: bytes) -> dict:
    """{index: (name, section, class, weak target index or None)}."""
    if data[0:4] == b"\0\0\xff\xff" and data[12:28] == BIGOBJ_CLASS:
        _, symbols_at, count = struct.unpack_from("<III", data, 44)
        record, section_format = 20, "<i"
    else:
        _, _, _, symbols_at, count, _, _ = struct.unpack_from("<HHIIIHH", data, 0)
        record, section_format = 18, "<h"
    strings = data[symbols_at + record * count:]
    found = {}
    index = 0
    while index < count:
        at = symbols_at + record * index
        if data[at:at + 4] == b"\0\0\0\0":
            offset = struct.unpack_from("<I", data, at + 4)[0]
            end = strings.find(b"\0", offset)
            name = strings[offset:end].decode("utf-8", errors="replace")
        else:
            name = data[at:at + 8].rstrip(b"\0").decode("utf-8", errors="replace")
        section = struct.unpack_from(section_format, data, at + 12)[0]
        type_at = at + 12 + (4 if record == 20 else 2)
        _, storage, aux = struct.unpack_from("<HBB", data, type_at)
        target = None
        if storage == WEAK_EXTERNAL and aux:
            target = struct.unpack_from("<I", data, at + record)[0]
        found[index] = (name, section, storage, target)
        index += 1 + aux
    return found


def main(argv: list) -> int:
    tree = Path(argv[1])
    database = json.loads((tree / "compile_commands.json").read_text(encoding="utf-8"))
    counts = collections.Counter()
    for entry in database:
        obj = Path(entry["output"])
        if not obj.is_absolute():
            obj = Path(entry["directory"]) / obj
        if obj.suffix != ".obj" or not obj.is_file():
            continue
        data = obj.read_bytes()
        if b".SCOVP$M" not in data:
            continue
        counts["objects carrying the hook"] += 1
        symbols = symbols_of(data)
        for name, section, storage, target in symbols.values():
            if not name.startswith("??_E"):
                continue
            if storage == WEAK_EXTERNAL and target is not None:
                aimed = symbols[target][0]
                counts["??_E another name for its own ??_G" if aimed == "??_G" + name[4:]
                       else "??_E another name for something else"] += 1
            elif section > 0:
                counts["??_E defined in a section of its own"] += 1
            else:
                counts["??_E only referred to"] += 1
    for key, value in sorted(counts.items()):
        print(f"{key}: {value}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

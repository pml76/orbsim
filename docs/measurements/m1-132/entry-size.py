"""M1-132 step 2 (decision 704): the length of one entry of the hook's table, in the objects with the hook.

    python -I docs/measurements/m1-132/entry-size.py <tree>...

Run from the root of the repository. The script loads read_object from
scripts/compiled-code.py. For each object of the tree's compile database
(compile_commands.json) with a section `.SCOVP$M`, it reads each such section:
its length, the offset and the type of each relocation, and the second word
of 8 bytes. It prints each different layout with its count.
"""

import collections
import importlib.util
import json
import struct
import sys
from pathlib import Path


def load(name: str, path: Path):
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main(argv: list) -> int:
    compiled = load("compiled_code", Path.cwd() / "scripts" / "compiled-code.py")
    for tree in argv[1:]:
        database = json.loads((Path(tree) / "compile_commands.json").read_text(encoding="utf-8"))
        forms, objects = collections.Counter(), 0
        for entry in database:
            obj = Path(entry["output"])
            if not obj.is_absolute():
                obj = Path(entry["directory"]) / obj
            if not obj.is_file():
                continue
            found = compiled.read_object(obj.read_bytes())
            if found is None or not any(s.name == ".SCOVP$M" for s in found.sections):
                continue
            objects += 1
            for section in found.sections:
                if section.name != ".SCOVP$M":
                    continue
                value = struct.unpack_from("<Q", section.data, 8)[0] if section.data and section.size >= 16 else None
                forms[(section.size, tuple((r.offset, r.type) for r in section.relocations), value)] += 1
        print(f"{tree}: {objects} objects with .SCOVP$M")
        for (size, relocations, value), count in forms.most_common():
            print(f"  length {size}, relocations (offset, type) {relocations}, second word {value}: {count}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

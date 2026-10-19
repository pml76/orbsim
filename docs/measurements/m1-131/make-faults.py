"""Rule 23 for address-table.py: make it fail once before its zero is believed.

    python -I make-faults.py <program.exe> <out folder>

Writes two doctored copies of one hooked program and its map into <out folder>:

  * none-<name>.exe/.map: the map loses the symbol line of the function that
    the table's first entry names -> address-table.py must count one entry
    that names no function ("none 1");
  * twice-<name>.exe/.map: the table's second entry gets the first entry's
    address -> address-table.py must count one address twice in the table.
"""

import re
import shutil
import struct
import sys
from pathlib import Path


def scovp(data: bytes) -> tuple:
    header = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, header + 6)[0]
    optional_size = struct.unpack_from("<H", data, header + 20)[0]
    optional = header + 24
    base = struct.unpack_from("<Q", data, optional + 24)[0]
    for index in range(count):
        at = optional + optional_size + 40 * index
        name = data[at:at + 8].rstrip(b"\0").decode()
        vsize, vaddr, rsize, rptr = struct.unpack_from("<IIII", data, at + 8)
        if name == ".SCOVP":
            return base, rptr
    raise SystemExit("no .SCOVP")


def main(argv: list) -> int:
    program, out = Path(argv[1]), Path(argv[2])
    out.mkdir(parents=True, exist_ok=True)
    data = program.read_bytes()
    text = program.with_suffix(".map").read_text(encoding="utf-8", errors="replace")
    base, raw = scovp(data)
    # The start marker is 8 bytes; then (address, note) pairs of 8 bytes each.
    first = struct.unpack_from("<Q", data, raw + 8)[0]
    second_at = raw + 8 + 16
    # none-: drop the map line that names the first entry's address.
    needle = f"{first:016x}"
    lines = text.splitlines(keepends=True)
    kept = [line for line in lines if not re.search(rf"\s{needle}\s", line)]
    print(f"first entry {first:#x}: {len(lines) - len(kept)} map line(s) removed")
    shutil.copy2(program, out / f"none-{program.name}")
    (out / f"none-{program.stem}.map").write_bytes("".join(kept).encode("utf-8"))
    # twice-: the second entry's address set to the first's.
    doctored = bytearray(data)
    struct.pack_into("<Q", doctored, second_at, first)
    (out / f"twice-{program.name}").write_bytes(bytes(doctored))
    shutil.copy2(program.with_suffix(".map"), out / f"twice-{program.stem}.map")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

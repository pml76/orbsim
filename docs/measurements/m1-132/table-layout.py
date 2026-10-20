"""M1-132 step 2 (decision 704): the layout of the section .SCOVP in each program with the hook.

    python -I docs/measurements/m1-132/table-layout.py <tree>...

Run from the root of the repository. For each program at the top of each
tree with a section `.SCOVP`, the script reads the VirtualSize and the
SizeOfRawData of that section from the program, and the parts of `.SCOVP` and
of `.SCOV` from the map next to it: `.SCOVP$A` and `.SCOVP$Z`, the two markers
of the recorder (tests/CoverageRecorder.cpp), `.SCOVP$M`, the entries of the
objects, and `.SCOV$BM`, the flags. It prints the first three programs, then
each program that does not have the layout of the others: `$A` of 8 bytes at
offset 0, `$M` at offset 8, `$Z` of 8 bytes after `$M`, a VirtualSize 16 bytes
more than `$M`, and `$M` 16 bytes for each flag.
"""

import re
import struct
import sys
from pathlib import Path

# A line of the map for a part of a section: start, length, name, class.
PART = re.compile(r"^ ([0-9a-f]{4}):([0-9a-f]{8}) ([0-9a-f]{8})H (\.SCOVP?\$\w+)\s+DATA$")


def section_headers(data: bytes) -> dict:
    """{name: (VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData)} of a PE program."""
    header = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, header + 6)[0]
    optional = struct.unpack_from("<H", data, header + 20)[0]
    found = {}
    for index in range(count):
        at = header + 24 + optional + 40 * index
        found[data[at:at + 8].rstrip(b"\0").decode()] = struct.unpack_from("<IIII", data, at + 8)
    return found


def main(argv: list) -> int:
    for tree in argv[1:]:
        rows, odd = 0, []
        for program in sorted(Path(tree).glob("*.exe")):
            headers = section_headers(program.read_bytes())
            if ".SCOVP" not in headers:
                continue
            rows += 1
            virtual, _, raw, _ = headers[".SCOVP"]
            parts = {}
            for line in program.with_suffix(".map").read_text(encoding="utf-8", errors="replace").splitlines():
                match = PART.match(line)
                if match:
                    parts[match.group(4)] = (int(match.group(2), 16), int(match.group(3), 16))
            entries, flags = parts[".SCOVP$M"], parts[".SCOV$BM"][1]
            start, stop = parts[".SCOVP$A"], parts[".SCOVP$Z"]
            line = (f"{program.name}: VirtualSize {virtual}, SizeOfRawData {raw}; $A {start}, $M {entries}, "
                    f"$Z {stop}; flags {flags}; $M / 16 = {entries[1] / 16}; VirtualSize / 16 = {virtual / 16}; "
                    f"(VirtualSize - 16) / 16 = {(virtual - 16) / 16}")
            layout = start == (0, 8) and entries[0] == 8 and stop == (8 + entries[1], 8)
            if not layout or virtual != 16 + entries[1] or entries[1] != 16 * flags:
                odd.append(line)
            if rows <= 3:
                print(line)
        print(f"{tree}: {rows} programs with .SCOVP; not as the others: {len(odd)}")
        for line in odd:
            print(f"   {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

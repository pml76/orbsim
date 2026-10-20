"""M1-132 step 2: faults for records-functions.py, so that the instrument fails one time (VERIFICATION.md rule 23).

    python -I docs/measurements/m1-132/make-faults.py <tree> <program name> <records folder> <empty output folder>

Run from the root of the repository. From the first record in <records
folder> of <program name>, the script writes three records into
<output folder>/records:

  - `address.functions.txt`: the first address plus 1, where the map gives no
    name. records-functions.py must give one problem;
  - `flags.functions.txt`: the count of flags plus 1. records-functions.py must
    give one record with flags not equal to table_entries;
  - `twice.functions.txt`: the record unchanged, but for a copy of the program
    in <output folder>/program, with its map and its .pdb file, in which the
    second entry of the hook's table has the address of the first entry.
    records-functions.py must give one address two times in one table.

The paths go into the records with the separators of Windows, as the
recorder writes them.
"""

import shutil
import struct
import sys
from pathlib import Path, PureWindowsPath


def table_at(data: bytes) -> int:
    """The file offset of the first entry of the hook's table: after the start marker of 8 bytes."""
    header = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, header + 6)[0]
    optional_size = struct.unpack_from("<H", data, header + 20)[0]
    for index in range(count):
        at = header + 24 + optional_size + 40 * index
        if data[at:at + 8].rstrip(b"\0") == b".SCOVP":
            return struct.unpack_from("<I", data, at + 20)[0] + 8
    raise ValueError("no .SCOVP section")


def main(argv: list) -> int:
    tree, name, folder, out = Path(argv[1]).resolve(), argv[2], Path(argv[3]), Path(argv[4])
    program = tree / name
    source = next(f for f in sorted(folder.glob("*.functions.txt"))
                  if f.read_text(encoding="utf-8").startswith(f"program {PureWindowsPath(program)}\n"))
    text = source.read_text(encoding="utf-8")
    lines = text.split("\n")
    records = out / "records"
    records.mkdir(parents=True)
    first = next(i for i, line in enumerate(lines[5:], 5) if line and not line.startswith("end "))
    shifted = list(lines)
    shifted[first] = f"{int(lines[first], 16) + 1:x}"
    (records / "address.functions.txt").write_bytes("\n".join(shifted).encode("utf-8"))
    flags = list(lines)
    flags[4] = f"flags {int(lines[4].split()[1]) + 1}"
    (records / "flags.functions.txt").write_bytes("\n".join(flags).encode("utf-8"))
    copy = out / "program"
    copy.mkdir()
    for suffix in (".exe", ".map", ".pdb"):
        shutil.copyfile(program.with_suffix(suffix), copy / (program.stem + suffix))
    data = bytearray((copy / name).read_bytes())
    at = table_at(bytes(data))
    data[at + 16:at + 24] = data[at:at + 8]
    (copy / name).write_bytes(bytes(data))
    moved = list(lines)
    moved[0] = f"program {PureWindowsPath(copy / name)}"
    (records / "twice.functions.txt").write_bytes("\n".join(moved).encode("utf-8"))
    print(f"from {source.name}: address {lines[first]} -> {shifted[first]}; {lines[4]} -> {flags[4]}; "
          f"the table's second entry in {copy / name} has the first entry's address")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

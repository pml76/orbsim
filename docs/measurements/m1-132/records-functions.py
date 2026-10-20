"""M1-132 step 2 (decisions 551, 582, 590, 695 and 704): the functions of each record of a full run.

    python -I docs/measurements/m1-132/records-functions.py <tree> <records folder> [--show N]

Run from the root of the repository. The script loads read_record from
scripts/check-coverage-recorder.py, and with it scripts/compiled-code.py, and
does not read a record or a map with a different reader.

For each record (`*.functions.txt`) in the folder, the script finds the
program that the record gives, and reads the map and the .pdb file next to it.
It then gives these figures:

  - the keys of the addresses of the record, from record_functions and
    symbol_key, and the problems (the result must be none);
  - the count of flags of the record against table_entries of its program
    (decision 704; the two must be equal), and, to compare, against the
    VirtualSize of `.SCOVP` divided by 16 with the two markers not subtracted;
  - decision 551: no two entries of the program's table at one address. The
    script reads the addresses from the section `.SCOVP` of the program, with
    a parser of the PE format that is part of this script, between the two
    markers of the recorder. It also compares the number of entries with table_entries, and
    turns each address of the table into a function, as for a record.

At the end: the totals, then each problem, and up to N examples of each other
result that is not correct. The output is text only.
"""

import collections
import importlib.util
import struct
import sys
from pathlib import Path, PureWindowsPath

# The layout of the table in a program: the start marker of 8 bytes, entries of
# two 8-byte words (the address of the function with the base added, and the
# value 1 of the hook), and the stop marker of 8 bytes (tests/CoverageRecorder.cpp).
MARKER = 8
WORD = 8
PE32_PLUS = 0x20B


def load(name: str, path: Path):
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def raw_table(data: bytes) -> tuple:
    """(base, [address with the base], start marker, stop marker, notes,
    VirtualSize) from the section .SCOVP, read with the parser of this script."""
    header = struct.unpack_from("<I", data, 0x3C)[0]
    if data[header:header + 4] != b"PE\0\0":
        raise ValueError("not a PE program")
    count = struct.unpack_from("<H", data, header + 6)[0]
    optional_size = struct.unpack_from("<H", data, header + 20)[0]
    optional = header + 24
    if struct.unpack_from("<H", data, optional)[0] != PE32_PLUS:
        raise ValueError("not a PE32+ program")
    base = struct.unpack_from("<Q", data, optional + 24)[0]
    for index in range(count):
        at = optional + optional_size + 40 * index
        if data[at:at + 8].rstrip(b"\0") != b".SCOVP":
            continue
        vsize, _, rsize, rptr = struct.unpack_from("<IIII", data, at + 8)
        raw = data[rptr:rptr + min(vsize, rsize)] + bytes(max(0, vsize - rsize))
        words = struct.unpack_from(f"<{vsize // WORD}Q", raw)
        body = words[1:-1]
        return base, list(body[0::2]), words[0], words[-1], collections.Counter(body[1::2]), vsize
    raise ValueError("no .SCOVP section")


class Program:
    def __init__(self, path: Path, compiled, tree: str):
        self.path = path
        data = path.read_bytes()
        text = path.with_suffix(".map").read_text(encoding="utf-8", errors="replace")
        self.map = compiled.read_map(text)
        modules = compiled.pdb_modules(path.with_suffix(".pdb").read_bytes())
        self.paths, self.path_problems = compiled.module_paths(self.map, modules)
        self.index = compiled.address_index(self.map)
        self.entries = compiled.table_entries(data)
        self.base, self.table, self.start, self.stop, self.notes, self.virtual_size = raw_table(data)
        self.key_of = lambda symbol: compiled.symbol_key(symbol, self.paths, tree)
        relative = [address - self.base for address in self.table]
        self.table_keys, self.table_problems = compiled.record_functions(self.index, relative, self.key_of)
        self.twice = {a: n for a, n in collections.Counter(self.table).items() if n > 1}


def main(argv: list) -> int:
    tree, folder = Path(argv[1]).resolve(), Path(argv[2])
    show = int(argv[argv.index("--show") + 1]) if "--show" in argv else 5
    recorder = load("check_coverage_recorder", Path.cwd() / "scripts" / "check-coverage-recorder.py")
    compiled = recorder.compiled_code
    programs, totals, examples = {}, collections.Counter(), collections.defaultdict(list)
    per_program = collections.defaultdict(collections.Counter)
    distinct = collections.defaultdict(set)
    largest = (0, "")
    problems = []
    for file in sorted(folder.glob("*.functions.txt")):
        record = recorder.read_record(file.read_text(encoding="utf-8"))
        totals["records"] += 1
        if record is None:
            totals["records that read_record does not read"] += 1
            examples["not read"].append(file.name)
            continue
        path = Path(PureWindowsPath(record.program))
        if not path.is_file():
            totals["records of a program that is not there now"] += 1
            examples["program not there"].append(f"{file.name}: {record.program}")
            continue
        if path.resolve().parent != tree:
            totals["records of a program outside the top of the tree"] += 1
            examples["outside the tree"].append(f"{file.name}: {record.program}")
        if path not in programs:
            programs[path] = Program(path, compiled, str(tree))
        program = programs[path]
        name = path.name if path.resolve().parent == tree else str(path)
        keys, found = compiled.record_functions(program.index, record.functions, program.key_of)
        per_program[name]["records"] += 1
        per_program[name]["addresses"] += len(record.functions)
        per_program[name]["keys"] += len(keys)
        per_program[name]["problems"] += len(found)
        distinct[name].update(record.functions)
        totals["addresses"] += len(record.functions)
        totals["keys"] += len(keys)
        totals["problems"] += len(found)
        totals["records out of line"] += record.out_of_line
        largest = max(largest, (len(record.functions), f"{file.name} ({name}, test '{record.test}')"))
        problems.extend(f"{name} ({file.name}): {p}" for p in found)
        if len(keys) != len(set(record.functions)):
            totals["records with fewer keys than different addresses"] += 1
            examples["fewer keys"].append(f"{file.name} ({name}): {len(set(record.functions))} addresses, "
                                          f"{len(keys)} keys")
        totals["records with flags equal to VirtualSize / 16, the markers not subtracted"] += (
            record.flags * 16 == program.virtual_size)
        if program.entries == record.flags:
            totals["records with flags equal to table_entries"] += 1
        else:
            totals["records with flags not equal to table_entries"] += 1
            examples["flags not equal"].append(f"{file.name} ({name}): flags {record.flags}, "
                                               f"table_entries {program.entries}")
    for name, counts in sorted(per_program.items()):
        program = next(p for p in programs.values() if name in (p.path.name, str(p.path)))
        print(f"{name}: records {counts['records']}, addresses {counts['addresses']} "
              f"({len(distinct[name])} different), keys {counts['keys']}, problems {counts['problems']}; "
              f"table_entries {program.entries}, entries read {len(program.table)}, markers "
              f"{program.start}/{program.stop}, notes {dict(program.notes)}, addresses two times "
              f"{len(program.twice)}; table addresses: {len(program.table_keys)} keys, "
              f"{len(program.table_problems)} problems; module_paths problems {len(program.path_problems)}")
    for program in programs.values():
        totals["programs"] += 1
        totals["different (program, address) pairs"] += len(distinct[program.path.name] | distinct[str(program.path)])
        totals["table entries, all programs"] += len(program.table)
        totals["table entries, table_entries differs from the entries read"] += program.entries != len(program.table)
        totals["table addresses two times in one table"] += len(program.twice)
        totals["table addresses with a problem"] += len(program.table_problems)
        totals["table: markers not zero"] += program.start != 0 or program.stop != 0
        totals["table: notes other than 1"] += sum(n for note, n in program.notes.items() if note != 1)
        totals["module_paths problems"] += len(program.path_problems)
        problems.extend(f"{program.path} (its table): {p}" for p in program.table_problems)
        problems.extend(f"{program.path} (module_paths): {p}" for p in program.path_problems)
    print()
    for key in ("records", "records that read_record does not read", "records of a program that is not there now",
                "records of a program outside the top of the tree", "programs", "records out of line", "addresses",
                "different (program, address) pairs", "keys", "problems",
                "records with fewer keys than different addresses",
                "records with flags equal to table_entries", "records with flags not equal to table_entries",
                "records with flags equal to VirtualSize / 16, the markers not subtracted",
                "table entries, all programs", "table entries, table_entries differs from the entries read",
                "table addresses two times in one table", "table addresses with a problem",
                "table: markers not zero", "table: notes other than 1", "module_paths problems"):
        print(f"  {key}: {totals[key]:,}")
    print(f"  the largest record: {largest[0]:,} addresses, {largest[1]}")
    print(f"problems ({len(problems)}):")
    for line in problems:
        print(f"  {line}")
    for kind, lines in examples.items():
        print(f"examples, {kind} ({len(lines)}):")
        for line in lines[:show]:
            print(f"  {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

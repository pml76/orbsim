"""M1-132 step 2 (decisions 709 (3) and 711 (4)): the key of a map symbol against the key that Program gives.

    python -I docs/measurements/m1-132/key-agreement.py <tree> [--show N] [--compiled <a copy of compiled-code.py>]

Run from the root of the repository. The script loads scripts/compiled-code.py,
or the copy that --compiled gives: a copy with a fault makes the instrument
fail one time (VERIFICATION.md rule 23).

For each program at the top of <tree> with the hook's table, the script reads
the map, the .pdb file, and each object that the map gives with a path
(module_paths): an object linked directly from its path, a member of a library
from the library, by the file name that the map gives. An object with a
section `.SCOVP$M` has the hook. For the objects with the hook, it makes
one Program, with each object keyed by object_key, and compares two sets of
(object key, name):

  - the functions of Program (`Program._functions`, which `digests` uses),
    each with the key that Program gives it;
  - the symbols of the map in code sections of these objects, each with
    symbol_key.

For each (object, name) in the two sets, the two keys must be equal. The
key of a member of a library comes from the name that the map gives it; the
script also counts each member with the hook with a name in the library
(archive_members) that is not that name. Each name in one set only is
counted with its cause, from the symbol table of the object. Last, each
address of the program's hook table is turned into a function with
record_functions, and its key must be a function of Program in the same
object: this is the key that a record gives.
"""

import collections
import importlib.util
import struct
import sys
from pathlib import Path, PureWindowsPath

# IMAGE_SCN_LNK_COMDAT (Microsoft's PE format specification, "Section Flags").
COMDAT = 0x1000


def load(name: str, path: Path):
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def table_addresses(data: bytes) -> list:
    """The addresses of the hook's table less the base, between the two markers."""
    header = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, header + 6)[0]
    optional_size = struct.unpack_from("<H", data, header + 20)[0]
    optional = header + 24
    base = struct.unpack_from("<Q", data, optional + 24)[0]
    for index in range(count):
        at = optional + optional_size + 40 * index
        if data[at:at + 8].rstrip(b"\0") == b".SCOVP":
            vsize, _, rsize, rptr = struct.unpack_from("<IIII", data, at + 8)
            raw = data[rptr:rptr + min(vsize, rsize)] + bytes(max(0, vsize - rsize))
            words = struct.unpack_from(f"<{vsize // 8}Q", raw)[1:-1]
            return [address - base for address in words[0::2]]
    return []


class Objects:
    """The objects of a tree, each read one time: (CoffObject or None, the text
    `.SCOVP$M` is in its bytes, its summary when it has the hook)."""

    def __init__(self, compiled):
        self.compiled = compiled
        self.archives, self.read, self.names = {}, {}, {}

    def member(self, library: str, name: str):
        if library not in self.archives:
            members = self.compiled.archive_members(Path(library).read_bytes()) or []
            found = collections.defaultdict(list)
            for member, data in members:
                found[PureWindowsPath(member).name].append((member, data))
            self.archives[library] = found
        return self.archives[library].get(name, [])

    def get(self, path: str, member):
        key = (path, member)
        if key not in self.read:
            stored = None
            if member is None:
                data = Path(path).read_bytes()
            else:
                found = self.member(path, member)
                data = found[0][1] if len(found) == 1 else None
                stored = found[0][0] if len(found) == 1 else None
            self.names[key] = stored
            if data is None:
                self.read[key] = (None, None, None)
            else:
                obj = self.compiled.read_object(data)
                hooked = obj is not None and any(s.name == ".SCOVP$M" for s in obj.sections)
                summary = self.compiled.object_summary(obj) if hooked else None
                self.read[key] = (obj, b".SCOVP$M" in data, summary)
        return self.read[key]


def cause_map_only(compiled, obj, name: str) -> str:
    """Why a map symbol of an object with the hook is not a function of Program."""
    found = [s for s in obj.symbols if s.name == name]
    if not found:
        return "no symbol of that name in the object"
    kinds = set()
    for symbol in found:
        if symbol.storage == compiled.COFF_WEAK_EXTERNAL:
            default = next((s.name for s in obj.symbols if s.index == symbol.default), None)
            same = default == "??_G" + name[4:]
            kinds.add(f"a weak external, default {'the ??_G name of the same class' if same else default}")
        elif symbol.section <= 0:
            kinds.add(f"section number {symbol.section}")
        elif not obj.sections[symbol.section - 1].characteristics & compiled.COFF_CODE:
            kinds.add("in a section that holds no code")
        elif not symbol.function:
            kinds.add(f"not of function type (type {symbol.type:#x}, storage {symbol.storage})")
        else:
            kinds.add("a function, which must not occur")
    return "; ".join(sorted(kinds))


def main(argv: list) -> int:
    tree = Path(argv[1]).resolve()
    show = int(argv[argv.index("--show") + 1]) if "--show" in argv else 5
    source = Path.cwd() / "scripts" / "compiled-code.py"
    if "--compiled" in argv:
        source = Path(argv[argv.index("--compiled") + 1])
    compiled = load("compiled_code", source)
    print(f"compiled-code: {source}")
    objects = Objects(compiled)
    totals, examples = collections.Counter(), collections.defaultdict(list)
    causes = collections.Counter()
    for path in sorted(tree.glob("*.exe")):
        data = path.read_bytes()
        if compiled.table_entries(data) is None:
            continue
        totals["programs with the hook's table"] += 1
        linker_map = compiled.read_map(path.with_suffix(".map").read_text(encoding="utf-8", errors="replace"))
        modules = compiled.pdb_modules(path.with_suffix(".pdb").read_bytes())
        paths, path_problems = compiled.module_paths(linker_map, modules)
        totals["module_paths problems"] += len(path_problems)
        summaries, owners, read = {}, {}, {}
        for (library, name), full in sorted(paths.items()):
            member = name if library else None
            obj, raw_hook, summary = objects.get(full, member)
            if obj is None:
                totals["objects that read_object does not read, summed over the programs"] += 1
                if raw_hook:
                    totals["objects not read that hold the text .SCOVP$M"] += 1
                    examples["not read, with .SCOVP$M"].append(f"{path.name}: {library}:{name}")
                continue
            if summary is None:
                totals["objects read, without the hook, summed over the programs"] += 1
                continue
            key = compiled.object_key(str(tree), full, member)
            if member is not None:
                totals["members with the hook, summed over the programs"] += 1
                if objects.names[(full, member)] != member:
                    totals["members with the hook with a name in the library that is not the map's name"] += 1
                    examples["member name"].append(f"{path.name}: {library}: {objects.names[(full, member)]!r}, "
                                                   f"map {member!r}")
            summaries[key] = summary
            owners[(library, name)] = key
            read[key] = obj
            totals["objects with the hook, summed over the programs"] += 1
        program = compiled.Program(summaries)
        program_names, sections_of = {}, {}
        for key, entries in program._functions().items():
            for index, number, symbol in entries:
                program_names.setdefault((index.key, symbol[1]), set()).add(key)
                sections_of[(index.key, symbol[1])] = (index, number)
        map_names = {}
        for symbol in linker_map.symbols:
            owner = owners.get((symbol.library, symbol.object))
            if symbol.code and owner is not None:
                map_names.setdefault((owner, symbol.name), set()).add(compiled.symbol_key(symbol, paths, str(tree)))
        for pair in sorted(program_names.keys() & map_names.keys()):
            totals["(object, name) in the two sets"] += 1
            if program_names[pair] == map_names[pair]:
                totals["... with equal keys"] += 1
            else:
                totals["... with different keys"] += 1
                examples["different keys"].append(f"{path.name}: {pair}: Program {program_names[pair]}, "
                                                  f"map {map_names[pair]}")
        all_map_names = collections.defaultdict(set)
        for symbol in linker_map.symbols:
            if symbol.code:
                all_map_names[symbol.name].add(owners.get((symbol.library, symbol.object), "without the hook"))
        for pair in sorted(map_names.keys() - program_names.keys()):
            totals["map symbols that Program does not have in that object"] += 1
            cause = cause_map_only(compiled, read[pair[0]], pair[1])
            causes[f"map only: {cause}"] += 1
            examples[f"map only: {cause}"].append(f"{path.name}: {pair}")
        for pair in sorted(program_names.keys() - map_names.keys()):
            totals["Program functions that the map does not give in that object"] += 1
            index, number = sections_of[pair]
            comdat = "a COMDAT section" if index.sections[number][2] & COMDAT else "not a COMDAT section"
            others = [s[1] for s in index.functions.get(number, ()) if s[1] != pair[1]]
            if any((pair[0], other) in map_names for other in others):
                cause = "the map gives a different function of the same section in this object: NOT EXPECTED"
            elif pair[1] in all_map_names:
                cause = "the map gives the name in a different object: a copy that the linker did not take"
            elif any(other in all_map_names for other in others):
                cause = ("the map gives a different function of the same section in a different object: "
                         "a copy that the linker did not take")
            else:
                cause = "the map gives no function of the same section: the linker did not take the section"
            cause += f" ({comdat})"
            causes[f"Program only: {cause}"] += 1
            examples[f"Program only: {cause}"].append(f"{path.name}: {pair}")
        index = compiled.address_index(linker_map)
        program_keys = {}
        for key, entries in program._functions().items():
            program_keys[key] = {entry[0].key for entry in entries}
        for address in table_addresses(data):
            names = index.get(address, [])
            keys, problems = compiled.record_functions(index, [address],
                                                       lambda s: compiled.symbol_key(s, paths, str(tree)))
            totals["table addresses"] += 1
            if problems:
                totals["table addresses with a problem"] += 1
                examples["table problems"].append(f"{path.name}: {problems}")
                continue
            symbol = compiled._one_function(names)
            owner = owners.get((symbol.library, symbol.object))
            if keys[0] in program_keys and owner in program_keys[keys[0]]:
                totals["table addresses with a key of Program in the same object"] += 1
            else:
                totals["table addresses with a key that Program does not have there"] += 1
                examples["table key not in Program"].append(f"{path.name}: {address:#x} {keys[0]} ({owner})")
    for key in ("programs with the hook's table", "module_paths problems",
                "objects with the hook, summed over the programs", "members with the hook, summed over the programs",
                "members with the hook with a name in the library that is not the map's name",
                "objects read, without the hook, summed over the programs",
                "objects that read_object does not read, summed over the programs",
                "objects not read that hold the text .SCOVP$M", "(object, name) in the two sets", "... with equal keys",
                "... with different keys", "map symbols that Program does not have in that object",
                "Program functions that the map does not give in that object", "table addresses",
                "table addresses with a problem", "table addresses with a key of Program in the same object",
                "table addresses with a key that Program does not have there"):
        print(f"  {key}: {totals[key]:,}")
    print("causes:")
    for cause, count in causes.most_common():
        print(f"  {count:,}: {cause}")
    for kind, lines in examples.items():
        print(f"examples, {kind} ({len(lines)}):")
        for line in lines[:show]:
            print(f"  {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

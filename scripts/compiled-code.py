#!/usr/bin/env python3
"""Read compiled code: an object file's or a program's sections, and the linker's map (M1-131).

    compiled-code.py --self-test

A library first: scripts/check-coverage-hook.py and
scripts/check-coverage-recorder.py load it with importlib, as scripts/mutate.py
loads scripts/mutants-due.py, and the selection of M1-132 extends it -- the file
register decision 594 names for reading object files, libraries and the linker's
map, so that each reader is written once (decision 632).

**Sections** (`sections`): the name, size and kind of each section of a COFF
object -- the ordinary layout and the "big object" one, which clang and MSVC
switch to past 65,279 sections -- of a PE program, and of an ELF object or
program, 32- or 64-bit, little-endian. That is enough to tell whether a module
carries the coverage hook, which register decision 584 rules is known by its
content: a module carries the hook when it holds the hook's address table. The
names were measured 2026-10-18 with clang 23.1.2's llvm-readobj on objects
compiled with `-fsanitize-coverage=func,inline-bool-flag,pc-table`: on Windows
the flags are in `.SCOV$BM` and the address table in `.SCOVP$M` of each object,
which lld-link merges into `.SCOV` and `.SCOVP` in the program; on Linux, for
the same flags with `--target=x86_64-linux-gnu`, `__sancov_bools` and
`__sancov_pcs`. Without `pc-table` the flags' section is there and the table's
is not.

**The linker's map** (`read_map`): lld-link's `/map` (register decision 582) --
the address the program prefers to be loaded at, each section's class, and
every symbol with its address, its object file and the library that object
came from, the internal ones under "Static symbols" included. A coverage record
names each function by its address less the program's base address (decision
644), which is the symbol's address in the map less the preferred one.

Proven able to fail before it was trusted: --self-test reads made-up objects,
programs and maps, each built here byte by byte, and exits non-zero unless
every one is read as it was made -- a long name through the string table, a big
object, an ELF section count past the field that holds it, and a map line of
each kind -- and unless a file that is none of these is refused rather than read
as one with no sections.
"""

import re
import struct
import sys
from typing import NamedTuple, Optional


class Section(NamedTuple):
    name: str
    size: int
    code: bool        # holds machine code: COFF's IMAGE_SCN_CNT_CODE, ELF's SHF_EXECINSTR


# The coverage hook's sections, by the format they are found in (measured
# 2026-10-18, see the docstring): the flags, and the address table.
HOOK_SECTIONS = {
    "coff-object": (".SCOV$BM", ".SCOVP$M"),
    "pe-program": (".SCOV", ".SCOVP"),
    "elf": ("__sancov_bools", "__sancov_pcs"),
}

# COFF's section flag for machine code (IMAGE_SCN_CNT_CODE, Microsoft's PE
# format specification), and ELF's (SHF_EXECINSTR, the System V ABI).
COFF_CODE = 0x20
ELF_EXECUTABLE = 0x4
# The class identifier that marks a big COFF object (ANON_OBJECT_HEADER_BIGOBJ),
# as Microsoft's winnt.h and LLVM's COFF.h spell it.
BIGOBJ_CLASS = bytes([0xC7, 0xA1, 0xBA, 0xD1, 0xEE, 0xBA, 0xA9, 0x4B,
                      0xAF, 0x20, 0xFA, 0xF6, 0x6A, 0xA4, 0xDC, 0xB8])
# A COFF symbol record's size, ordinary and big (the string table follows them).
COFF_SYMBOL = 18
BIGOBJ_SYMBOL = 20
# The machines a COFF object of this project can be for: x86-64, x86 and ARM64.
COFF_MACHINES = (0x8664, 0x014C, 0xAA64)


def _string(table: bytes, offset: int) -> str:
    end = table.find(b"\0", offset)
    return table[offset:end if end >= 0 else len(table)].decode("utf-8", errors="replace")


def _base64_offset(text: str) -> int:
    """A section name's string-table offset written `//` and base 64, which
    COFF uses past 9,999,999 (LLVM's COFFObjectFile, decodeBase64StringEntry)."""
    alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    value = 0
    for character in text:
        value = value * 64 + alphabet.index(character)
    return value


def _coff_sections(data: bytes, table_at: int, count: int, strings: bytes) -> list:
    found = []
    for index in range(count):
        at = table_at + 40 * index
        raw = data[at:at + 8].rstrip(b"\0").decode("utf-8", errors="replace")
        size, characteristics = struct.unpack_from("<I", data, at + 16)[0], struct.unpack_from("<I", data, at + 36)[0]
        if raw.startswith("//"):
            raw = _string(strings, _base64_offset(raw[2:]))
        elif raw.startswith("/") and raw[1:].isdigit():
            raw = _string(strings, int(raw[1:]))
        found.append(Section(raw, size, bool(characteristics & COFF_CODE)))
    return found


def _coff_object(data: bytes) -> Optional[list]:
    if len(data) >= 56 and data[0:4] == b"\0\0\xff\xff" and data[12:28] == BIGOBJ_CLASS:
        count, symbols_at, symbols = struct.unpack_from("<III", data, 44)
        strings = data[symbols_at + BIGOBJ_SYMBOL * symbols:] if symbols_at else b""
        return _coff_sections(data, 56, count, strings)
    if len(data) < 20:
        return None
    machine, count, _, symbols_at, symbols, optional, _ = struct.unpack_from("<HHIIIHH", data, 0)
    if machine not in COFF_MACHINES or 20 + optional + 40 * count > len(data):
        return None
    strings = data[symbols_at + COFF_SYMBOL * symbols:] if symbols_at else b""
    return _coff_sections(data, 20 + optional, count, strings)


def _pe_program(data: bytes) -> Optional[list]:
    if len(data) < 0x40 or data[0:2] != b"MZ":
        return None
    header = struct.unpack_from("<I", data, 0x3C)[0]
    if data[header:header + 4] != b"PE\0\0":
        return None
    count = struct.unpack_from("<H", data, header + 6)[0]
    optional = struct.unpack_from("<H", data, header + 20)[0]
    return _coff_sections(data, header + 24 + optional, count, b"")


def _elf(data: bytes) -> Optional[list]:
    if data[0:4] != b"\x7fELF" or len(data) < 0x34 or data[5] != 1:
        return None
    wide = data[4] == 2
    if wide:
        at, entry, count, names = struct.unpack_from("<Q", data, 0x28)[0], *struct.unpack_from("<HHH", data, 0x3A)
    else:
        at, entry, count, names = struct.unpack_from("<I", data, 0x20)[0], *struct.unpack_from("<HHH", data, 0x2E)

    def header(index: int) -> tuple:
        base = at + entry * index
        if wide:
            name, kind, flags = struct.unpack_from("<IIQ", data, base)
            offset, size = struct.unpack_from("<QQ", data, base + 0x18)
            link = struct.unpack_from("<I", data, base + 0x28)[0]
        else:
            name, kind, flags, _, offset, size, link = struct.unpack_from("<IIIIIII", data, base)
        return name, flags, offset, size, link

    if at == 0:
        return []
    # Past 0xff00 sections the counts live in section 0 (the System V ABI's
    # extended numbering): its size holds the count, its link the names' index.
    first = header(0)
    if count == 0:
        count = first[3]
    if names == 0xFFFF:
        names = first[4]
    _, _, strings_at, strings_size, _ = header(names)
    strings = data[strings_at:strings_at + strings_size]
    found = []
    for index in range(count):
        name, flags, _, size, _ = header(index)
        found.append(Section(_string(strings, name), size, bool(flags & ELF_EXECUTABLE)))
    return found


def sections(data: bytes) -> Optional[tuple]:
    """(format, [Section]) for an object file or a program, or None for a file
    that is none of the formats read here -- refused, never read as empty."""
    for kind, reader in (("pe-program", _pe_program), ("elf", _elf), ("coff-object", _coff_object)):
        try:
            found = reader(data)
        except (struct.error, IndexError, ValueError):
            found = None
        if found is not None:
            return kind, found
    return None


def carries(kind: str, found: list) -> tuple:
    """(flags, table): whether the module holds the hook's flags and its
    address table, by their sections."""
    flags, table = HOOK_SECTIONS[kind]
    names = {s.name for s in found}
    return flags in names, table in names


def holds_code(found: list) -> bool:
    return any(s.code and s.size > 0 for s in found)


# --- the linker's map ---------------------------------------------------------

class Symbol(NamedTuple):
    address: int       # less the preferred load address: what a coverage record names
    name: str          # as the object file names it, decorated
    library: str       # "" for an object linked directly
    object: str
    code: bool         # in a section of class CODE
    static: bool       # listed under "Static symbols"


class LinkerMap(NamedTuple):
    base: int
    symbols: list

    def at(self, address: int) -> list:
        return [s for s in self.symbols if s.address == address]


MAP_BASE = re.compile(r"^ Preferred load address is ([0-9a-fA-F]+)$")
MAP_SECTION = re.compile(r"^ ([0-9a-fA-F]{4}):([0-9a-fA-F]{8}) ([0-9a-fA-F]{8})H (\S+)\s+(CODE|DATA)$")
# lld-link's line: section:offset, the name, the address with the preferred
# base added, an optional `f` that MSVC's link.exe writes for a function, and
# the library and object as `library:object`, or the object alone.
MAP_SYMBOL = re.compile(r"^ ([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{16})\s+(?:f\s+)?(\S+)$")


def read_map(text: str) -> Optional[LinkerMap]:
    """The map lld-link writes for `/map`, or None if `text` is not one: no
    preferred load address, or no symbol."""
    base, code_sections, symbols, static = None, set(), [], False
    for line in text.splitlines():
        match = MAP_BASE.match(line)
        if match:
            base = int(match.group(1), 16)
            continue
        match = MAP_SECTION.match(line)
        if match:
            if match.group(5) == "CODE":
                code_sections.add(int(match.group(1), 16))
            continue
        if line.strip() == "Static symbols":
            static = True
            continue
        match = MAP_SYMBOL.match(line)
        if match is None or base is None:
            continue
        section, name, address, where = int(match.group(1), 16), match.group(3), int(match.group(4), 16), match.group(5)
        library, _, obj = where.rpartition(":")
        symbols.append(Symbol(address - base if address >= base else address, name, library, obj,
                              section in code_sections, static))
    if base is None or not symbols:
        return None
    return LinkerMap(base, symbols)


# --- self-test ----------------------------------------------------------------

def made_coff(names: list, code: tuple = (), big: bool = False) -> bytes:
    """A COFF object holding only section headers, a long name through the
    string table as compilers write one."""
    strings = b""
    headers = b""
    for name in names:
        raw = name.encode()
        if len(raw) > 8:
            raw = f"/{4 + len(strings)}".encode()
            strings += name.encode() + b"\0"
        flags = COFF_CODE if name in code else 0x40
        headers += raw.ljust(8, b"\0") + struct.pack("<IIIIIIHHI", 0, 0, 16, 0, 0, 0, 0, 0, flags)
    table = struct.pack("<I", 4 + len(strings)) + strings
    if big:
        head = b"\0\0\xff\xff" + struct.pack("<HHI", 2, 0x8664, 0) + BIGOBJ_CLASS + struct.pack("<IIII", 0, 0, 0, 0)
        head += struct.pack("<III", len(names), 56 + len(headers), 0)
        return head + headers + table
    head = struct.pack("<HHIIIHH", 0x8664, len(names), 0, 20 + len(headers), 0, 0, 0)
    return head + headers + table


def made_pe(names: list) -> bytes:
    dos = b"MZ" + b"\0" * 0x3A + struct.pack("<I", 0x40)
    headers = b"".join(n.encode().ljust(8, b"\0") + struct.pack("<IIIIIIHHI", 0, 0, 16, 0, 0, 0, 0, 0, 0x40)
                       for n in names)
    return dos + b"PE\0\0" + struct.pack("<HHIIIHH", 0x8664, len(names), 0, 0, 0, 0, 0) + headers


def made_elf(names: list, code: tuple = (), extended: bool = False) -> bytes:
    strings = b"\0" + b"".join(n.encode() + b"\0" for n in names) + b".shstrtab\0"
    count = len(names) + 2                     # the null section, the names, the string table
    shoff = 64
    strings_at = shoff + 64 * count
    head = b"\x7fELF" + bytes([2, 1, 1]) + b"\0" * 9
    head += struct.pack("<HHIQQQIHHHHHH", 1, 62, 1, 0, 0, shoff, 0, 64, 0, 0, 64,
                        0 if extended else count, 0xFFFF if extended else count - 1)
    null = struct.pack("<IIQQQQIIQQ", 0, 0, 0, 0, 0, count if extended else 0, count - 1 if extended else 0, 0, 0, 0)
    body, offset = b"", 1
    for name in names:
        flags = ELF_EXECUTABLE if name in code else 0x2
        body += struct.pack("<IIQQQQIIQQ", offset, 1, flags, 0, 0, 16, 0, 0, 1, 0)
        offset += len(name) + 1
    body += struct.pack("<IIQQQQIIQQ", offset, 3, 0, 0, strings_at, len(strings), 0, 0, 1, 0)
    return head + null + body + strings


SAMPLE_MAP = """\
 p

 Timestamp is 00000000 (Repro mode)

 Preferred load address is 0000000140000000

 Start         Length     Name                   Class
 0001:00000000 000103a8H .text                   CODE
 0002:00000000 00004354H .rdata                  DATA

  Address         Publics by Value              Rva+Base               Lib:Object

 0000:00000000       __AbsoluteZero             0000000000000000     <absolute>
 0001:00010140       ?twice@coverage_probe@orb@@YAHH@Z 0000000140011140     libp:p.obj
 0001:00010170       main                       0000000140011170     rec.obj
 0002:00000708       ?_Static@std@@3HB          0000000140090708     rec.obj

 entry point at         0001:0008d920

 Static symbols

 0001:00010270       ?thrice@?A0x5E76B424@coverage_probe@orb@@YAHH@Z 0000000140011270     libp:p.obj
"""


def self_test() -> int:
    failures = 0

    def expect(what: str, got, want) -> None:
        nonlocal failures
        good = got == want
        failures += not good
        print(f"self-test: {what}: {'ok' if good else f'WRONG, got {got!r}, wanted {want!r}'}")

    hooked = [".text", ".SCOV$BM", ".SCOVP$M", ".CRT$XCA00002"]
    read = sections(made_coff(hooked, code=(".text",)))
    expect("a COFF object's long section name, through the string table", read and read[1][3].name, ".CRT$XCA00002")
    expect("a COFF object's hook", read and carries(*read), (True, True))
    expect("a COFF object's code", read and holds_code(read[1]), True)
    read = sections(made_coff([".text", ".SCOV$BM"]))
    expect("a COFF object with the flags and no table", read and carries(*read), (True, False))
    expect("a COFF object with no code", read and holds_code(read[1]), False)
    read = sections(made_coff(hooked, big=True))
    expect("a big COFF object", read and (read[0], [s.name for s in read[1]]), ("coff-object", hooked))
    read = sections(made_pe([".text", ".SCOV", ".SCOVP"]))
    expect("a program's hook", read and (read[0], carries(*read)), ("pe-program", (True, True)))
    read = sections(made_pe([".text", ".rdata"]))
    expect("a program without it", read and carries(*read), (False, False))
    read = sections(made_elf([".text.f", "__sancov_bools", "__sancov_pcs"], code=(".text.f",)))
    expect("an ELF object's hook", read and (read[0], carries(*read), holds_code(read[1])), ("elf", (True, True), True))
    read = sections(made_elf(["__sancov_pcs"], extended=True))
    expect("an ELF object's sections counted in section 0", read and [s.name for s in read[1]],
           ["", "__sancov_pcs", ".shstrtab"])
    expect("a file that is none of them, refused", sections(b"not an object file at all"), None)
    expect("an empty file, refused", sections(b""), None)

    found = read_map(SAMPLE_MAP)
    expect("the map's preferred load address", found and found.base, 0x140000000)
    expect("a symbol in a library's object", found and found.at(0x11140),
           [Symbol(0x11140, "?twice@coverage_probe@orb@@YAHH@Z", "libp", "p.obj", True, False)])
    expect("a symbol in an object linked directly", found and [(s.library, s.object) for s in found.at(0x11170)],
           [("", "rec.obj")])
    expect("a static symbol", found and [(s.name, s.static) for s in found.at(0x11270)],
           [("?thrice@?A0x5E76B424@coverage_probe@orb@@YAHH@Z", True)])
    expect("data is not code", found and [s.code for s in found.at(0x90708)], [False])
    expect("text without a base address is not a map", read_map(SAMPLE_MAP.replace("Preferred", "Assumed")), None)
    expect("a map without symbols is not one", read_map(" Preferred load address is 0000000140000000\n"), None)
    return 1 if failures else 0


def main() -> int:
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())

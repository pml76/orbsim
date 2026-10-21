#!/usr/bin/env python3
"""Read compiled code: objects, programs, libraries, the linker's map and the .pdb file (M1-131, M1-132).

    compiled-code.py --self-test [--compiler <clang++> --archiver <llvm-ar> --linker <lld-link>]

A library first: scripts/check-coverage-hook.py and
scripts/check-coverage-recorder.py load it with importlib, as scripts/mutate.py
loads scripts/mutants-due.py, and the selection of M1-132 extends it -- the file
register decision 594 names for reading object files, libraries and the linker's
map, so that each reader is written one time (decision 632).

**Sections** (`sections`): the name, the length and the type of each section
of a COFF object -- the ordinary layout and the "big object" layout, which
clang and MSVC use for more than 65,279 sections -- of a PE program, and of an
ELF object or program, 32- or 64-bit, little-endian. That is sufficient to
know if a module carries the coverage hook, which register decision 584 rules
is known by its content: a module carries the hook when it holds the hook's
address table. The names were measured 2026-10-18 with clang 23.1.2's llvm-readobj on objects
compiled with `-fsanitize-coverage=func,inline-bool-flag,pc-table`: on Windows
the flags are in `.SCOV$BM` and the address table in `.SCOVP$M` of each object,
which lld-link merges into `.SCOV` and `.SCOVP` in the program; on Linux, for
the same flags with `--target=x86_64-linux-gnu`, `__sancov_bools` and
`__sancov_pcs`. Without `pc-table` the flags' section is there and the table's
is not.

**The linker's map** (`read_map`): lld-link's `/map` (register decision 582) --
the address the program prefers to be loaded at, each section's class, and
each symbol with its address, its object file and the library that object
came from, the internal ones under "Static symbols" included. A coverage record
names each function by its address less the program's base address (decision
644), which is the symbol's address in the map less the preferred one.

The readers that M1-132 adds give the digests of decision 545 their input. The
script reads each file directly, not the text of a different tool (decision 546).

**A full object** (`read_object`): each section of a COFF object with its
bytes and its relocations, and each symbol with its section, its type, its
storage class and, for the definition of a section, its COMDAT selection and
its associated section. The ordinary layout and the layout of a big object.

**Archives** (`archive_members`, `is_import_member`): the members of a .lib
file, with the long names in the COFF format and in the GNU format. A member of an
import library names one function of a DLL and is not an object.

**The module list of a .pdb file** (`pdb_modules`): the module name and the
object file name of each module, from the DBI stream of the MSF file.

**The paths of a program's modules** (`module_paths`, `direct_objects`,
`same_file_names`): the map names a library only by its short name and an
object only by its file name. The .pdb file gives the full path of each, and
each library and each object must have one path only (decision 705).

**The functions of a record** (`address_index`, `record_functions`,
`symbol_key`, `table_entries`, `map_table_entries`, `hook_table_entries`):
the function at each address of a coverage record, through the map
(decisions 582 and 695), with the key that Program gives the same function;
and the number of entries in the hook's table of a program, which the count
of flags in its record must equal (decision 704). The map's markers and the
length of the section `.SCOVP` give that number, and the two must agree
(decision 712).

**The digests** (`object_summary`, `Program`): a function's digest (decision
545) is a hash of its machine code, the names of the functions that it points
at, and the contents of the data that it uses, followed through data that
points at other data. A test runs again only when a function that it entered
has a different digest (M1-132). The summary of an object holds all the data
that the digests use and survives a JSON round trip, so that a driver can keep
it (decisions 567 and 709). A program also gives the functions without a flag,
judged by section, the start-up functions, and one digest of the linker
directives of its objects; `object_key` gives the key of an object (decision
711).

**The part of each program** (`CodeReader`, `ProgramCode`, `SummaryCache`):
scripts/run-tests.py makes one CodeReader for each run, and gets from it the
part of each program with the hook that it examines (decision 713 (4)). The
ProgramCode of a program reads its map, its .pdb file and each object and
library that the map names. It gives the keys of a record's addresses, the
count of the hook's table, one SHA-256 of the program's function part, and
each problem, which names the program. The function part holds the digests
of the functions that a test entered and of the functions without a flag, the
bytes of the code without the hook, and the linker directives (decisions 583,
585, 586, 711 and 713). SummaryCache keeps the summary of each object with the
hook in the build tree between runs, one file for each object, and only a full
run deletes files (decisions 567 and 714).

Proven able to fail before it was trusted: --self-test reads made-up objects,
programs, maps, archives and MSF files, each built here byte by byte, and exits
non-zero unless each one is read as it was made -- a long name through the
string table, a big object, an ELF section count too large for the field that
holds it, a map line of each type, more relocations than the 16-bit count can
hold, and the blocks of a stream out of sequence -- and unless a file that is
none of these is refused rather than read as one with no sections. It computes
the digests of made-up objects, each with one change, and exits non-zero
unless the change gives a different digest to the correct functions and to no
other. It writes a made-up build tree with a program, its map, its .pdb file,
objects and libraries, changes it at one location for each case, and exits
non-zero unless the function part changes only when it must, each problem is
found, and the summaries kept give the same result. With --compiler,
it also compiles small samples at -O0 and at -O2 with that clang, archives one
with llvm-ar, links a DLL from the other and the archive with lld-link, and
reads the results (decisions 591 and 709); and it compiles pairs of samples
with one difference, at the two levels, and holds the digests to the same
rule. The archiver and the linker are the two files that the command line
names, so that the fingerprint of the test holds them (decision 710). Without
--compiler, it runs only the made-up cases, as check-coverage-hook.py's
self-test calls it.
"""

import hashlib
import itertools
import json
import re
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path, PureWindowsPath
from typing import Iterable, NamedTuple, Optional


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


# --- a full COFF object (M1-132) ----------------------------------------------
#
# The layout is Microsoft's PE format specification (learn.microsoft.com, "PE
# Format": "COFF File Header", "Section Table", "COFF Relocations", "COFF Symbol
# Table" and "Auxiliary Format 5: Section Definitions"). Where the specification
# says less, LLVM's reader is the source: llvm/include/llvm/Object/COFF.h and
# llvm/lib/Object/COFFObjectFile.cpp.

# The lengths in bytes of a section header and of a relocation record.
COFF_SECTION_HEADER = 40
COFF_RELOCATION = 10
# IMAGE_SCN_LNK_NRELOC_OVFL: the section has more relocations than the 16-bit
# count holds. The count is then 0xFFFF, and the first relocation's
# VirtualAddress holds the correct count, that first record included (LLVM's
# coff_section::hasExtendedRelocations and getNumberOfRelocations).
COFF_RELOCATION_OVERFLOW = 0x01000000
COFF_RELOCATION_COUNT_FULL = 0xFFFF
# The storage classes of an external and of a static symbol
# (IMAGE_SYM_CLASS_EXTERNAL and IMAGE_SYM_CLASS_STATIC).
COFF_EXTERNAL, COFF_STATIC = 2, 3
# The storage class of a weak external (IMAGE_SYM_CLASS_WEAK_EXTERNAL). Its
# first auxiliary record starts with TagIndex, the index of its default symbol:
# Microsoft's PE format specification, "Auxiliary Format 3: Weak Externals".
# For a class with external linkage, clang makes the vector deleting
# destructor (`??_E...`) a weak external, with the scalar deleting destructor
# (`??_G...`) as its default and the search type 3, an alias. For a class in an
# anonymous namespace, `??_E...` is a static function at the same offset in the
# same section as `??_G...` (measured 2026-10-20 with llvm-readobj).
COFF_WEAK_EXTERNAL = 105
# A function's complex type, IMAGE_SYM_DTYPE_FUNCTION (2), in the high nibble
# of a symbol's type (LLVM's COFFSymbolRef::getComplexType).
COFF_FUNCTION = 0x20
# The highest section number in the ordinary layout. A 16-bit number above it
# is a reserved, negative number: 0xFFFF is -1 (absolute), 0xFFFE is -2 (debug)
# (COFF::MaxNumberOfSections16 and COFFSymbolRef::getSectionNumber).
COFF_MAX_SECTIONS_16 = 0xFEFF
# The lowest version of a big object (COFF::BigObjHeader::MinBigObjectVersion).
BIGOBJ_VERSION = 2
# In a section-definition record: the offsets of the low 16 bits of the
# associated section's number, of the COMDAT selection, and, in a big object
# only, of the high 16 bits (LLVM's coff_aux_section_definition).
DEFINITION_NUMBER, DEFINITION_SELECTION, DEFINITION_NUMBER_HIGH = 12, 14, 16


class Relocation(NamedTuple):
    offset: int        # from the start of the section (VirtualAddress)
    symbol: int        # the index of the symbol in the symbol table
    type: int          # IMAGE_REL_AMD64_* for x86-64


class CoffSection(NamedTuple):
    number: int        # numbered from 1, as symbols name it
    name: str
    characteristics: int
    size: int          # SizeOfRawData
    data: Optional[bytes]  # None when the file holds no bytes for it: uninitialised data
    relocations: list


class SectionDefinition(NamedTuple):
    selection: int     # IMAGE_COMDAT_SELECT_*, 0 for a section that is not a COMDAT
    number: int        # the associated section, for selection 5 (associative)


class CoffSymbol(NamedTuple):
    index: int         # in the symbol table, the auxiliary records counted
    name: str
    value: int
    section: int       # numbered from 1; 0 undefined, -1 absolute, -2 debug
    type: int
    storage: int       # COFF_EXTERNAL, COFF_STATIC, or a different storage class
    definition: Optional[SectionDefinition]
    default: Optional[int] = None    # a weak external's default symbol: its index (TagIndex)

    @property
    def function(self) -> bool:
        return self.type & 0xF0 == COFF_FUNCTION


class CoffObject(NamedTuple):
    machine: int
    big: bool
    sections: list
    symbols: list      # the primary records only, each with its index


def _text(raw: bytes) -> str:
    # surrogateescape keeps two names apart that are different only in bytes that are not UTF-8.
    return raw.decode("utf-8", errors="surrogateescape")


def _object_header(data: bytes) -> Optional[tuple]:
    """(machine, `big`, section table, section count, symbol table, record count)."""
    if data[0:4] == b"\0\0\xff\xff":
        if len(data) < 56 or data[12:28] != BIGOBJ_CLASS or struct.unpack_from("<H", data, 4)[0] < BIGOBJ_VERSION:
            return None
        count, symbols_at, records = struct.unpack_from("<III", data, 44)
        return struct.unpack_from("<H", data, 6)[0], True, 56, count, symbols_at, records
    if len(data) < 20:
        return None
    machine, count, _, symbols_at, records, optional, _ = struct.unpack_from("<HHIIIHH", data, 0)
    return machine, False, 20 + optional, count, symbols_at, records


def _string_table(data: bytes, at: int) -> Optional[bytes]:
    """The string table at `at`, which starts with its length in 4 bytes, so
    that an offset indexes it directly. LLVM's
    COFFObjectFile::initSymbolTablePtr reads a length below 4 as an empty table,
    and does not accept a table that does not end with NUL."""
    if at + 4 > len(data):
        return None
    size = max(struct.unpack_from("<I", data, at)[0], 4)
    table = data[at:at + size]
    if len(table) < size or (size > 4 and table[-1] != 0):
        return None
    return table


def _table_name(strings: bytes, offset: int) -> Optional[str]:
    if not 4 <= offset < len(strings):
        return None
    return _text(strings[offset:strings.index(b"\0", offset)])


def _section_name(raw: bytes, strings: bytes) -> Optional[str]:
    name = raw.split(b"\0", 1)[0]
    if name.startswith(b"//"):
        if not re.fullmatch(rb"[A-Za-z0-9+/]{1,6}", name[2:]):
            return None
        return _table_name(strings, _base64_offset(name[2:].decode()))
    if re.fullmatch(rb"/[0-9]+", name):
        return _table_name(strings, int(name[1:]))
    return _text(name)


def _relocations(data: bytes, at: int, count: int, characteristics: int) -> Optional[list]:
    if characteristics & COFF_RELOCATION_OVERFLOW and count == COFF_RELOCATION_COUNT_FULL:
        if at + COFF_RELOCATION > len(data) or struct.unpack_from("<I", data, at)[0] < 1:
            return None
        count = struct.unpack_from("<I", data, at)[0] - 1
        at += COFF_RELOCATION
    if at + COFF_RELOCATION * count > len(data):
        return None
    return list(map(Relocation._make, struct.iter_unpack("<IIH", data[at:at + COFF_RELOCATION * count])))


def _object_sections(data: bytes, table_at: int, count: int, strings: bytes) -> Optional[list]:
    found = []
    for number in range(1, count + 1):
        at = table_at + COFF_SECTION_HEADER * (number - 1)
        size, data_at, relocations_at = struct.unpack_from("<III", data, at + 16)
        relocation_count, _, characteristics = struct.unpack_from("<HHI", data, at + 32)
        name = _section_name(data[at:at + 8], strings)
        relocations = _relocations(data, relocations_at, relocation_count, characteristics)
        # LLVM's COFFObjectFile::getSectionContents: no bytes when PointerToRawData is 0.
        if name is None or relocations is None or (data_at and data_at + size > len(data)):
            return None
        found.append(CoffSection(number, name, characteristics, size, data[data_at:data_at + size] if data_at else None,
                                 relocations))
    return found


def _object_symbols(data: bytes, at: int, records: int, big: bool, strings: bytes) -> Optional[list]:
    record = BIGOBJ_SYMBOL if big else COFF_SYMBOL
    layout = struct.Struct("<8sIiHBB" if big else "<8sIHHBB")
    found, index = [], 0
    while index < records:
        base = at + record * index
        raw, value, section, kind, storage, aux = layout.unpack_from(data, base)
        if index + 1 + aux > records:
            return None
        if not big and section > COFF_MAX_SECTIONS_16:
            section -= 0x10000
        if raw[0:4] == b"\0\0\0\0":
            name = _table_name(strings, struct.unpack_from("<I", raw, 4)[0])
        else:
            name = _text(raw.split(b"\0", 1)[0])
        definition = None
        # LLVM's COFFSymbolRef::getSectionDefinition: a static symbol's first auxiliary record.
        if storage == COFF_STATIC and aux:
            number = struct.unpack_from("<H", data, base + record + DEFINITION_NUMBER)[0]
            if big:
                number |= struct.unpack_from("<H", data, base + record + DEFINITION_NUMBER_HIGH)[0] << 16
            definition = SectionDefinition(data[base + record + DEFINITION_SELECTION], number)
        default = None
        if storage == COFF_WEAK_EXTERNAL and aux:
            default = struct.unpack_from("<I", data, base + record)[0]     # TagIndex
        if name is None:
            return None
        found.append(CoffSymbol(index, name, value, section, kind, storage, definition, default))
        index += 1 + aux
    return found


def read_object(data: bytes) -> Optional[CoffObject]:
    """Each section of a COFF object, with its bytes and relocations, and each
    symbol, in the ordinary layout or in the layout of a big object; or None for
    a file that is not a COFF object, or that does not keep to the layout:
    refused, and at no time read as empty. An import member of an import
    library is not an object."""
    try:
        header = _object_header(data)
        if header is None:
            return None
        machine, big, table_at, count, symbols_at, records = header
        record = BIGOBJ_SYMBOL if big else COFF_SYMBOL
        if machine not in COFF_MACHINES or table_at + COFF_SECTION_HEADER * count > len(data):
            return None
        if symbols_at + record * records > len(data) or (records and not symbols_at):
            return None
        strings = _string_table(data, symbols_at + record * records) if symbols_at else b""
        if strings is None:
            return None
        found = _object_sections(data, table_at, count, strings)
        symbols = _object_symbols(data, symbols_at, records, big, strings)
    except (struct.error, ValueError):
        return None
    if found is None or symbols is None:
        return None
    return CoffObject(machine, big, found, symbols)


# --- archives: the libraries that hold objects (M1-132) -----------------------
#
# The layout is Microsoft's PE format specification, "Archive (Library) File
# Format" and "Import Library Format", and LLVM's reader, llvm/lib/Object/Archive.cpp,
# for the GNU format of the long names.

ARCHIVE_MAGIC = b"!<arch>\n"
ARCHIVE_HEADER = 60                       # the length in bytes of a member header
ARCHIVE_HEADER_END = b"`\n"               # the last two bytes of a member header
# The members that hold no object: the symbol tables of the linker -- "/" one
# time in the GNU format and two times in the COFF format, "/SYM64/" for 64-bit
# offsets in the GNU format, "/<ECSYMBOLS>/" for ARM64EC (Archive.cpp) -- and
# "//", the long names.
ARCHIVE_SYMBOL_TABLES = ("/", "/SYM64/", "/<ECSYMBOLS>/")
ARCHIVE_LONG_NAMES = "//"
# An import member starts with the import header: Sig1 0
# (IMAGE_FILE_MACHINE_UNKNOWN), Sig2 0xFFFF, and Version 0. A big object starts
# with the same two signatures, but its version is 2 or more.
IMPORT_START = b"\0\0\xff\xff\0\0"
IMPORT_HEADER = 20


def _long_name(table: bytes, offset: int, coff: bool) -> Optional[str]:
    """A name in the long-name member: ended by NUL in the COFF format, by "/\\n"
    in the GNU format (Archive.cpp, ArchiveMemberHeader::getName)."""
    if offset >= len(table):
        return None
    end = table.find(b"\0" if coff else b"\n", offset)
    if end < 0:
        return None
    if coff:
        return _text(table[offset:end])
    return _text(table[offset:end - 1]) if table[end - 1:end] == b"/" else None


def _archive_entries(data: bytes) -> Optional[list]:
    """(name field, bytes) of each member, each member aligned to 2 bytes."""
    found, at = [], len(ARCHIVE_MAGIC)
    while at < len(data):
        header = data[at:at + ARCHIVE_HEADER]
        if len(header) < ARCHIVE_HEADER or header[58:60] != ARCHIVE_HEADER_END:
            return None
        size = header[48:58].strip()
        if not re.fullmatch(rb"[0-9]+", size) or at + ARCHIVE_HEADER + int(size) > len(data):
            return None
        body = data[at + ARCHIVE_HEADER:at + ARCHIVE_HEADER + int(size)]
        found.append((_text(header[0:16].rstrip(b" ")), body))
        at += ARCHIVE_HEADER + len(body) + len(body) % 2
    return found


def archive_members(data: bytes) -> Optional[list]:
    """[(name, bytes)] of each member of an archive, an import member included
    (see is_import_member), in the sequence of the file. None for a file that
    is not an archive, or that does not keep to the layout: refused, and at no
    time read as empty. The long names are in the COFF format, each ended by
    NUL, when the name of each of the first two members is "/", as lib.exe and
    llvm-ar write them; else in the GNU format (LLVM's Archive constructor uses
    the same rule)."""
    if not data.startswith(ARCHIVE_MAGIC):
        return None
    entries = _archive_entries(data)
    if entries is None:
        return None
    coff = [name for name, _ in entries[:2]] == ["/", "/"]
    long_names, found = None, []
    for name, body in entries:
        if name in ARCHIVE_SYMBOL_TABLES:
            continue
        if name == ARCHIVE_LONG_NAMES:
            long_names = body
            continue
        if name.startswith("/"):
            if long_names is None or not re.fullmatch(r"/[0-9]+", name):
                return None
            name = _long_name(long_names, int(name[1:]), coff)
            if name is None:
                return None
        elif name.endswith("/"):
            name = name[:-1]
        found.append((name, body))
    return found


def is_import_member(data: bytes) -> bool:
    """Examine if an archive's member is a short import member, which names one
    function of a DLL, and is not an object."""
    return len(data) >= IMPORT_HEADER and data.startswith(IMPORT_START)


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


# --- the module list of a .pdb file (M1-132, decision 705) --------------------
#
# The layouts come from LLVM's documentation of the format:
# llvm.org/docs/PDB/MsfFile.html for the superblock and the stream directory,
# llvm.org/docs/PDB/index.html for the stream numbers that do not change, and
# llvm.org/docs/PDB/DbiStream.html for the DBI stream and its module
# information. The checks are LLVM's own:
# validateSuperBlock in llvm/lib/DebugInfo/MSF/MSFCommon.cpp, PDBFile's
# parseFileHeaders and parseStreamData, DbiStream::reload, and
# DbiModuleDescriptor::getRecordLength, which aligns each entry to 4 bytes.

MSF_MAGIC = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0"
MSF_BLOCK_SIZES = (512, 1024, 2048, 4096, 8192, 16384, 32768)   # isValidBlockSize, MSFCommon.h
MSF_FREE_BLOCK_MAPS = (1, 2)              # the free-block map is at block 1 or 2
MSF_NIL_STREAM = 0xFFFFFFFF               # a stream length that gives no block
DBI_STREAM = 3                            # the number of the DBI stream, which does not change
DBI_HEADER = 64                           # sizeof(DbiStreamHeader)
DBI_SIGNATURE = -1                        # VersionSignature
DBI_V70 = 19990903                        # DbiStreamVersion::V70, the lowest version that LLVM reads
MODULE_INFO_HEADER = 64                   # sizeof(ModuleInfoHeader), before the two names


def _blocks(data: bytes, block_size: int, indices: tuple, size: int) -> Optional[bytes]:
    """The bytes of a stream, its blocks put one after the other in the sequence of its list."""
    if any(index * block_size + block_size > len(data) for index in indices):
        return None
    return b"".join(data[i * block_size:(i + 1) * block_size] for i in indices)[:size]


def _msf_stream(data: bytes, wanted: int) -> Optional[bytes]:
    """Stream `wanted` of an MSF 7.00 file, or None."""
    if len(data) < len(MSF_MAGIC) + 24 or not data.startswith(MSF_MAGIC):
        return None
    block_size, free_map, _, directory_size, _, block_map = struct.unpack_from("<6I", data, len(MSF_MAGIC))
    if block_size not in MSF_BLOCK_SIZES or free_map not in MSF_FREE_BLOCK_MAPS or len(data) % block_size:
        return None
    directory_blocks = -(-directory_size // block_size)
    if directory_size % 4 or directory_blocks > block_size // 4 or block_map == 0:
        return None
    if (block_map + 1) * block_size > len(data):
        return None
    directory = _blocks(data, block_size, struct.unpack_from(f"<{directory_blocks}I", data, block_map * block_size),
                        directory_size)
    if directory is None or len(directory) < 4:
        return None
    count = struct.unpack_from("<I", directory, 0)[0]
    if wanted >= count or 4 + 4 * count > len(directory):
        return None
    sizes = struct.unpack_from(f"<{count}I", directory, 4)
    at = 4 + 4 * count
    for number, size in enumerate(sizes):
        blocks = 0 if size == MSF_NIL_STREAM else -(-size // block_size)
        if at + 4 * blocks > len(directory):
            return None
        if number == wanted:
            return _blocks(data, block_size, struct.unpack_from(f"<{blocks}I", directory, at), size if blocks else 0)
        at += 4 * blocks
    return None


def pdb_modules(data: bytes) -> Optional[list]:
    """[(module name, object file name)] of each module in a .pdb file's DBI
    stream, or None for a file that is not a PDB, or that has no DBI stream:
    refused, and at no time read as empty."""
    try:
        dbi = _msf_stream(data, DBI_STREAM)
        if dbi is None or len(dbi) < DBI_HEADER:
            return None
        signature, version = struct.unpack_from("<iI", dbi, 0)
        parts = struct.unpack_from("<iiiiiIii", dbi, 24)
        modules_size = parts[0]
        # The lengths of the six parts after the header; parts[5] is MFCTypeServerIndex, not a length.
        sizes = parts[:5] + parts[6:]
        if signature != DBI_SIGNATURE or version < DBI_V70 or modules_size % 4 or min(sizes) < 0:
            return None
        if DBI_HEADER + sum(sizes) != len(dbi):
            return None
        found, at, end = [], DBI_HEADER, DBI_HEADER + modules_size
        while at < end:
            name_end = dbi.index(b"\0", at + MODULE_INFO_HEADER, end)
            obj_end = dbi.index(b"\0", name_end + 1, end)
            found.append((_text(dbi[at + MODULE_INFO_HEADER:name_end]), _text(dbi[name_end + 1:obj_end])))
            at += -(-(obj_end + 1 - at) // 4) * 4
        return found
    except (struct.error, ValueError):
        return None


# --- the paths of a program's modules (M1-132, decision 705) ------------------
#
# Measured 2026-10-20 with llvm-pdbutil dump --modules on build/debug/test_orbit.pdb
# and its map. lld-link writes one module for each object, and two for each DLL
# that an import library names:
#   - an object linked directly: the module name and the object file name are
#     each the full path of the object, for example
#     C:\...\build\debug\CMakeFiles\test_orbit.dir\tests\test_orbit.cpp.obj.
#     The map names it by its file name only: test_orbit.cpp.obj.
#   - a member of a library: the module name is the member's name in the
#     library -- a file name from llvm-ar (Orbit.cpp.obj), a full path from
#     Microsoft's lib.exe (`D:\a\_work\...\exe_main.obj`) -- and the object file
#     name is the full path of the library, for example
#     C:\...\build\debug\orbsim_core_d.lib. The map names the library by its
#     file name without .lib (orbsim_core_d), and the member by its file name.
#   - a DLL from an import library: "KERNEL32.dll" and "Import:KERNEL32.dll",
#     each with the full path of kernel32.lib. The map names kernel32:KERNEL32.dll.
#   - the linker's own: "* Linker *", with no object file name, and
#     "C:\...\build\debug\internal .obj file created from .res files", with
#     the same text for the two names. The map does not name them.
# test_orbit.pdb has 163 modules: the 151 objects of the map, 5 DLLs with two
# modules each, and the linker's two. The map names 156: the 151 objects and
# the 5 DLLs.

# The map's own entries, which no object file holds.
MAP_PSEUDO_OBJECTS = ("<absolute>", "<linker-defined>", "<common>")


def _file_name(path: str) -> str:
    return PureWindowsPath(path).name


def module_paths(linker_map: LinkerMap, modules: list) -> tuple:
    """({(library, object): full path}, [problem]): for each object that the
    map names, the full path from the .pdb file's modules -- the library's path
    for a member of a library, the object's path for an object linked directly.
    Each library and each object must have one path, and only one; else a
    problem names it, and it has no path. And the library must hold the member
    as a module, so that the .pdb file of a different program gives a problem
    (decision 710)."""
    libraries, direct, members = {}, {}, {}
    for module, obj in modules:
        if module == obj:
            direct.setdefault(_file_name(obj), set()).add(obj)
        elif obj:
            libraries.setdefault(PureWindowsPath(obj).stem, set()).add(obj)
            members.setdefault(obj, set()).add(_file_name(module))
    found, problems, named = {}, [], set()
    for library, obj in sorted({(s.library, s.object) for s in linker_map.symbols}):
        if not library and obj in MAP_PSEUDO_OBJECTS:
            continue
        paths = sorted(libraries.get(library, ()) if library else direct.get(obj, ()))
        if len(paths) == 1 and library and obj not in members[paths[0]]:
            problems.append(f"the library {library} holds no module {obj} in the .pdb file")
        elif len(paths) == 1:
            found[(library, obj)] = paths[0]
        elif (library or obj) not in named:
            named.add(library or obj)
            what = f"the library {library}" if library else f"the object {obj}, linked directly,"
            problems.append(f"{what} has {len(paths)} paths in the .pdb file, not one: {paths}")
    return found, problems


def direct_objects(modules: list) -> list:
    """The full path of each object linked directly, from a .pdb file's modules."""
    return [obj for module, obj in modules if module == obj and obj]


def same_file_names(where: str, names: list) -> list:
    """A problem for each pair of objects with one file name, in one library or
    in the objects linked directly into one program (decision 705): the map
    names an object by its file name only. The case of the letters is ignored,
    as Windows ignores it. An import library has many members with one name;
    the caller examines only a library or a program with objects that carry
    the hook."""
    groups = {}
    for name in names:
        groups.setdefault(_file_name(name).casefold(), []).append(name)
    return [f"{where}: two objects with one file name: {first} and {second}"
            for group in groups.values() for first, second in itertools.combinations(group, 2)]


# --- each recorded address turned into a function (M1-132, decisions 582 and 695)
#
# A coverage record gives each function that a test entered by its address
# less the base address of the program (decision 644). The linker's map gives
# the function at that address, with the name that its object file gives it:
# the name of its digest (decision 582). The debug information does not give
# the name, because it cannot tell apart two functions with the same bytes.

# The start of the decorated name of a vector deleting destructor and of a
# scalar deleting destructor. clang makes the vector deleting destructor an
# alias of the scalar deleting destructor of the same class: one code at one
# address, with two names in the map (decision 695). Measured 2026-10-19:
# 546 entries of the hook's tables in RelWithDebInfo and 754 in Debug.
VECTOR_DELETING, SCALAR_DELETING = "??_E", "??_G"
# The hook's table in a program (decision 704). lld-link puts the two markers
# of the recorder, `.SCOVP$A` and `.SCOVP$Z` of 8 bytes each
# (tests/CoverageRecorder.cpp), and the `.SCOVP$M` sections of the objects
# into the section `.SCOVP`, in the sequence of the text after `$`. Measured
# 2026-10-20 in the 54 programs with the hook in each tree: the markers are at
# the start and at the end of `.SCOVP`, with no padding, thus its VirtualSize
# is 16 bytes more than the entries.
TABLE_MARKERS = 2 * 8
# The length of one entry: the address of the function and the value 1 that
# the hook writes, 8 bytes each. Measured 2026-10-20: each `.SCOVP$M` section
# of the objects with the hook has 16 bytes, with one relocation of type
# IMAGE_REL_AMD64_ADDR64 at offset 0 -- 8,794 sections in 91 objects in
# RelWithDebInfo, and 63,069 sections in 92 objects in Debug.
TABLE_ENTRY = 16
# The offset of VirtualSize in a section header of a PE program. The length
# of the bytes in the file, SizeOfRawData at offset 16, is a multiple of
# FileAlignment and thus can be larger (Microsoft's PE format specification,
# "Section Table").
SECTION_VIRTUAL_SIZE = 8
# The magic number of a PE32+ optional header, and the offset of its ImageBase,
# the preferred base address (Microsoft's PE format specification, "Optional
# Header Standard Fields" and "Optional Header Windows-Specific Fields").
PE32_PLUS = 0x20B
PE_IMAGE_BASE = 24
# The names of the two markers in the linker's map (tests/CoverageRecorder.cpp).
TABLE_START, TABLE_STOP = "__start___sancov_pcs", "__stop___sancov_pcs"
# The length of the start marker. LLVM's SanitizerCoverage pass adds 8 bytes to
# the start symbol on Windows, because there the start symbol is a value of 8
# bytes before the table (CreateSecStartEnd in
# llvm/lib/Transforms/Instrumentation/SanitizerCoverage.cpp, read 2026-10-20).
TABLE_START_LENGTH = 8


def map_table_entries(linker_map: LinkerMap) -> Optional[int]:
    """The number of entries in the hook's table from the linker's map: from
    the end of the start marker to the stop marker, as LLVM's pass gives the
    hook the start and the end of its table (decision 712). None when the map
    does not have each marker one time, or when the length between them is not
    a number of complete entries."""
    found = {}
    for symbol in linker_map.symbols:
        if symbol.name in (TABLE_START, TABLE_STOP):
            found.setdefault(symbol.name, []).append(symbol.address)
    if any(len(found.get(name, ())) != 1 for name in (TABLE_START, TABLE_STOP)):
        return None
    length = found[TABLE_STOP][0] - found[TABLE_START][0] - TABLE_START_LENGTH
    if length < 0 or length % TABLE_ENTRY:
        return None
    return length // TABLE_ENTRY


def hook_table_entries(data: bytes, linker_map: LinkerMap) -> tuple:
    """(entries, problem): the number of entries in the hook's table of a
    program, from the map's markers and from the length of the section
    `.SCOVP` (decision 712). The two must agree; if not, or if one is not
    available, no number and a problem that gives the two. Thus a change of
    the layout, such as padding or a different marker, makes `check` fail and
    does not give an incorrect count (decision 704)."""
    from_section, from_map = table_entries(data), map_table_entries(linker_map)
    if from_section is None or from_section != from_map:
        return None, (f"the hook's table has {from_section} entries from the length of the section .SCOVP "
                      f"and {from_map} from the map's markers, not one number")
    return from_section, None


def address_index(linker_map: LinkerMap) -> dict:
    """{address: [Symbol]} of the symbols in the code sections of the map, in
    the sequence of the map. LinkerMap.at examines each symbol for each
    address: a map of the Debug tree has up to 42,789 symbols, and a record
    gave up to 3,616 addresses in the full run of 2026-10-20. This function
    reads the map one time."""
    index = {}
    for symbol in linker_map.symbols:
        if symbol.code:
            index.setdefault(symbol.address, []).append(symbol)
    return index


def _one_function(names: list) -> Optional[Symbol]:
    """The symbol of the one function at an address, or None (decision 695).
    One name is that function. Two names that start with `??_E` and `??_G`,
    and that are the same after these four characters, are one destructor,
    with its `??_G` name. Each other set of names can be two functions at one
    address (decision 551)."""
    if len(names) == 1:
        return names[0]
    if len(names) == 2:
        vector, scalar = sorted(names, key=lambda symbol: symbol.name)
        rest = len(VECTOR_DELETING)
        if (vector.name.startswith(VECTOR_DELETING) and scalar.name.startswith(SCALAR_DELETING)
                and vector.name[rest:] == scalar.name[rest:]):
            return scalar
    return None


def record_functions(index: dict, addresses: Iterable, key_of) -> tuple:
    """(keys, problems) for the addresses of one record. `index` is the
    address_index of the program's map, and `key_of` gives the key of a map
    symbol, or None (symbol_key). The keys are sorted, each key one time. Each
    problem gives an address in hexadecimal and the names at it, so that the
    caller can name the program and make `check` fail (decisions 590 and 695).
    An address with no name, or with names that are not one function, gives
    no key."""
    keys, problems = set(), []
    for address in addresses:
        names = index.get(address, [])
        symbol = _one_function(names)
        if symbol is None:
            problems.append(f"the address {address:#x} has {len(names)} names in the map, not one function: "
                            f"{sorted(s.name for s in names)}")
            continue
        key = key_of(symbol)
        if key is None:
            where = f"{symbol.library}:{symbol.object}" if symbol.library else symbol.object
            problems.append(f"the address {address:#x}: {symbol.name}, a static symbol of {where}, "
                            "which has no path in the .pdb file")
            continue
        keys.add(key)
    return sorted(keys), problems


def symbol_key(symbol: Symbol, paths: dict, tree: str) -> Optional[str]:
    """The key of a map symbol: the key that Program gives the same function
    (decisions 709 (3) and 711 (4)). A symbol that the map gives as public is
    external, and its key is its decorated name. A symbol in the part "Static
    symbols" of the map is known only in its object, and its key is the key of
    the object and its name. `paths` comes from module_paths. A member of a
    library has the name that the map gives it, its file name; llvm-ar gives
    the same name to the members of the libraries of the tree. None for a
    static symbol of an object without a path: module_paths gives a problem
    for that object."""
    if not symbol.static:
        return symbol.name
    path = paths.get((symbol.library, symbol.object))
    if path is None:
        return None
    return function_key(object_key(tree, path, symbol.object if symbol.library else None), symbol.name, COFF_STATIC)


def _virtual_sizes(data: bytes, name: bytes) -> Optional[list]:
    """The VirtualSize of each section with the name `name` in a PE program,
    or None for a file that is not a PE program."""
    if len(data) < 0x40 or data[0:2] != b"MZ":
        return None
    header = struct.unpack_from("<I", data, 0x3C)[0]
    if data[header:header + 4] != b"PE\0\0":
        return None
    count, optional = struct.unpack_from("<H", data, header + 6)[0], struct.unpack_from("<H", data, header + 20)[0]
    first = header + 24 + optional
    if first + COFF_SECTION_HEADER * count > len(data):
        return None
    headers = (first + COFF_SECTION_HEADER * number for number in range(count))
    return [struct.unpack_from("<I", data, at + SECTION_VIRTUAL_SIZE)[0] for at in headers
            if data[at:at + 8].rstrip(b"\0") == name]


def table_entries(data: bytes) -> Optional[int]:
    """The number of entries in the hook's table of a PE program (decision
    704): the VirtualSize of its section `.SCOVP`, less the two markers,
    divided by the length of one entry. None for a file that is not a PE
    program, for a program with no `.SCOVP` section or with two, and for a
    section that does not hold the two markers and a number of complete
    entries. Thus the caller does not compare a record with an incorrect
    number."""
    try:
        sizes = _virtual_sizes(data, HOOK_SECTIONS["pe-program"][1].encode())
    except struct.error:
        return None
    if sizes is None or len(sizes) != 1 or sizes[0] < TABLE_MARKERS or (sizes[0] - TABLE_MARKERS) % TABLE_ENTRY:
        return None
    return (sizes[0] - TABLE_MARKERS) // TABLE_ENTRY


def table_addresses(data: bytes) -> Optional[list]:
    """The address of each entry of the hook's table of a PE program, less the
    program's preferred base address, in the sequence of the table: the
    addresses that a coverage record can name (decision 644). Read from the
    bytes of the section `.SCOVP`, after the start marker: each entry is the
    function's address and the value 1, 8 bytes each (decision 712), and the
    address in the file has the preferred base, the ImageBase of the PE32+
    optional header, added. None for a file that is not a PE32+ program, for
    a program with no `.SCOVP` section or with two, and for a section that
    does not hold the two markers and complete entries in the file. Thus the
    caller keeps no table for such a program (decision 715 (1))."""
    try:
        if len(data) < 0x40 or data[0:2] != b"MZ":
            return None
        header = struct.unpack_from("<I", data, 0x3C)[0]
        if data[header:header + 4] != b"PE\0\0":
            return None
        count, optional = struct.unpack_from("<H", data, header + 6)[0], struct.unpack_from("<H", data, header + 20)[0]
        start = header + 24
        if optional < PE_IMAGE_BASE + 8 or struct.unpack_from("<H", data, start)[0] != PE32_PLUS:
            return None
        base = struct.unpack_from("<Q", data, start + PE_IMAGE_BASE)[0]
        first = start + optional
        found = [first + COFF_SECTION_HEADER * n for n in range(count)
                 if data[first + COFF_SECTION_HEADER * n:first + COFF_SECTION_HEADER * n + 8].rstrip(b"\0")
                 == HOOK_SECTIONS["pe-program"][1].encode()]
        if len(found) != 1:
            return None
        virtual, _, size, at = struct.unpack_from("<IIII", data, found[0] + SECTION_VIRTUAL_SIZE)
        if virtual < TABLE_MARKERS or (virtual - TABLE_MARKERS) % TABLE_ENTRY or virtual > size \
                or at + virtual > len(data):
            return None
        places = range(at + TABLE_START_LENGTH, at + virtual - (TABLE_MARKERS - TABLE_START_LENGTH), TABLE_ENTRY)
        entries = [struct.unpack_from("<QQ", data, place) for place in places]
    except struct.error:
        return None
    # The second word of each entry is the hook's value 1 (decision 712):
    # measured 2026-10-21 in each entry of the 54 programs with the hook in
    # each tree. A different value is a different layout.
    if any(value != 1 for _, value in entries):
        return None
    return [address - base for address, _ in entries]


# --- the digests of a program's functions (M1-132, decision 545) --------------
#
# A function's digest is a hash of three things: its machine code, the names of
# the functions that it points at, and the contents of the data that it uses.
# The digest follows data that points at other data. No piece of data is known
# by its name or by its position. The name of a static piece of data has a
# counter (`__sancov_gen_.5`, `__unnamed_1`). That counter and the section
# numbers move when a function is added before it (measured 2026-10-20 on the
# compiled samples of the self-test).

# The version of a summary's layout. Change it when the layout changes, so that
# a driver does not read a summary that it kept from a previous layout (decision
# 709). Version 2 adds the default symbol of a weak external (decision 711).
SUMMARY_VERSION = 2
# The section number of an absolute symbol: IMAGE_SYM_ABSOLUTE, Microsoft's PE
# format specification, "Section Number Values".
COFF_ABSOLUTE = -1
# The prefix of the sections that the C runtime calls at start and at exit:
# the initialisers .CRT$XC*, .CRT$XI*, the thread callbacks .CRT$XL* and the
# exit tables .CRT$XP* and .CRT$XT* (decision 702). A function that such a
# section points at is a start-up function (decision 624).
START_UP_PREFIX = ".CRT$"
# The linker's directives (Microsoft's PE format specification, ".drectve
# Section (Object Only)").
DIRECTIVES = ".drectve"
# IMAGE_COMDAT_SELECT_ASSOCIATIVE: the linker keeps the section only with the
# section that its definition names (the same specification, "COMDAT Sections").
COMDAT_ASSOCIATIVE = 5
# The sections that hold only debug information, which no digest includes:
# CodeView's .debug$S and .debug$T, DWARF's .debug_*, and LLVM's
# address-significance table, which only tells the linker which functions it
# can fold. A changed line number changes only these sections.
DEBUG_PREFIX = ".debug"
ADDRESS_SIGNIFICANCE = ".llvm_addrsig"


def left_out(name: str) -> bool:
    """Examine if a section holds only debug information."""
    return name.startswith(DEBUG_PREFIX) or name == ADDRESS_SIGNIFICANCE


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def object_summary(obj: CoffObject) -> dict:
    """All the data that the digests use from one object, and no raw bytes. Each
    section is [number, name, characteristics, size, SHA-256 of its bytes or
    None for uninitialised data, relocations as [offset, type, symbol index],
    COMDAT selection, associated section]; a debug section keeps only its
    number and name. Each symbol is what read_object gives, as a list:
    [index, name, value, section, type, storage class, COMDAT selection,
    associated section, default symbol of a weak external]. Lists, strings
    and numbers only, so that the summary survives a JSON round trip unchanged
    (decision 709)."""
    definitions = {}
    for symbol in obj.symbols:
        if symbol.definition is not None and 0 < symbol.section <= len(obj.sections) \
                and obj.sections[symbol.section - 1].name == symbol.name:
            definitions.setdefault(symbol.section, symbol.definition)
    sections, debug = [], []
    for section in obj.sections:
        if left_out(section.name):
            debug.append([section.number, section.name])
            continue
        selection, associated = definitions.get(section.number, (0, 0))
        sections.append([section.number, section.name, section.characteristics, section.size,
                         None if section.data is None else _sha(section.data),
                         [[r.offset, r.type, r.symbol] for r in section.relocations], selection, associated])
    symbols = [[s.index, s.name, s.value, s.section, s.type, s.storage, *(s.definition or (None, None)), s.default]
               for s in obj.symbols]
    return {"version": SUMMARY_VERSION, "machine": obj.machine, "sections": sections, "debug": debug,
            "symbols": symbols}


def object_key(tree: str, path: str, member: Optional[str] = None) -> str:
    """The key of an object (decision 711 (4)). In the build tree: its path
    relative to the tree, with `/`, for example
    `CMakeFiles/test_orbit.dir/tests/test_orbit.cpp.obj`. Not in the tree:
    its full path, with `/`. A member of a library: the library's key and the
    member's name, `orbsim_core_d.lib(Orbit.cpp.obj)`.

    The paths are Windows paths: only a Windows program holds COFF objects and
    has a .pdb file. Windows compares file names without a difference between
    upper case and lower case: NTFS keeps the case of a name but does not use
    it when it compares names, unless a folder is set to be case-sensitive.
    Thus this function compares the parts of the paths after str.casefold,
    which is the same as Windows' rule for ASCII letters. The rule is the same
    on each machine that runs the script, so that the self-test gives one
    result in the Linux trees too."""
    folder, file = PureWindowsPath(tree), PureWindowsPath(path)
    depth = len(folder.parts)
    inside = len(file.parts) > depth and [p.casefold() for p in file.parts[:depth]] == [
        p.casefold() for p in folder.parts]
    key = "/".join(file.parts[depth:]) if inside else file.as_posix()
    return key if member is None else f"{key}({member})"


def function_key(object_key: str, name: str, storage: int) -> str:
    """A function's key (decision 709): its decorated name when it is external;
    else the object's key and the name, because a static function, or one in
    an anonymous namespace, is known only in its own object."""
    return name if storage == COFF_EXTERNAL else f"{object_key}|{name}"


class _Index:
    """One object's summary, indexed for the digests."""

    def __init__(self, key: str, summary: dict):
        if summary.get("version") != SUMMARY_VERSION:
            raise ValueError(f"{key}: a summary of version {summary.get('version')}, not {SUMMARY_VERSION}")
        self.key = key
        self.sections = {s[0]: s for s in summary["sections"]}
        self.symbols = {s[0]: s for s in summary["symbols"]}
        self.functions, self.associated, self._keys = {}, {}, {}
        for symbol in summary["symbols"]:
            section = self.sections.get(symbol[3])
            if section is not None and section[2] & COFF_CODE and symbol[4] & 0xF0 == COFF_FUNCTION:
                self.functions.setdefault(symbol[3], []).append(symbol)
        for section in summary["sections"]:
            if section[6] == COMDAT_ASSOCIATIVE and section[7] in self.sections:
                self.associated.setdefault(section[7], []).append(section[0])

    def key_of(self, symbol: list) -> str:
        return function_key(self.key, symbol[1], symbol[5])

    def keys_in(self, number: int) -> tuple:
        """The sorted keys of the functions in a code section: one function and
        its funclets. A funclet is the code of a `catch` block or of a clean-up,
        which clang puts after its function in the same section, as a static
        function with a name of its own (measured 2026-10-20)."""
        if number not in self._keys:
            self._keys[number] = tuple(sorted(self.key_of(s) for s in self.functions.get(number, ())))
        return self._keys[number]


class Program:
    """The digests of the functions of one program (decision 545).

    `summaries` maps the key of each object with the hook to its summary
    (object_summary). `unhooked` holds the keys of the objects without the
    hook (decision 711 (3)). Where more than one object defines an external name,
    `chosen` maps the name to the key of the object that the linker's map
    gives for it: the copy that the program holds. When that object is
    without the hook, the program's fingerprint holds it by its bytes
    (decision 583): the name stays a name, and `outside` lists it. When it is
    in none of the two sets, a problem names it, so that `check` fails.

    A function is a symbol of function type in a section that holds code. The
    digest of a code section is made of these parts:
      - its name, its characteristics, its length (SizeOfRawData) and the hash
        of its bytes;
      - for each relocation, its offset, its type and what it points at;
      - the digest of each section that goes with the code section and is
        not debug information: its unwind data, its exception tables, its
        jump tables, the hook's flag and its entry in the hook's table.
    A relocation points at one of these:
      - a symbol in the same section: the parts are its offset there;
      - a function in a different code section: its key, but not its code;
      - a symbol in a different code section that is not a function: the keys
        of the functions there, and its offset;
      - data: the digest of the data, made as above, and the offset in it;
      - an external name that no given object defines: the name;
      - a weak external that no given object defines: its name, and what its
        default symbol points at -- for an alias, the function's key
        (decision 711 (2));
      - an absolute symbol: its name and its value.
    No digest includes debug information."""

    def __init__(self, summaries: dict, chosen: Optional[dict] = None, unhooked: Iterable = ()):
        self._objects = [_Index(key, summary) for key, summary in summaries.items()]
        self._given, self._unhooked = set(summaries), set(unhooked)
        self._chosen = dict(chosen or {})
        self._externals = {}
        for index in self._objects:
            for symbol in index.symbols.values():
                if symbol[5] == COFF_EXTERNAL and symbol[3] > 0:
                    self._externals.setdefault(symbol[1], []).append((index, symbol))
        self._copies_of, self._data, self._targets, self._texts, self._problems = {}, {}, {}, {}, {}
        self._code, self._found = {}, {}
        self._by_key, self._start = None, None
        self.outside = set()

    @property
    def problems(self) -> list:
        """Each problem the digests found, one time, in the sequence found."""
        return list(self._problems)

    def _problem(self, text: str) -> None:
        self._problems.setdefault(text, None)

    def _copies(self, name: str) -> list:
        """The definitions of an external name that the program can hold: the
        one that the map names; each one, when the map names none; none, when
        the map names an object that is not given."""
        found = self._copies_of.get(name)
        if found is None:
            found = self._externals.get(name, [])
            owner = self._chosen.get(name)
            if owner is not None:
                held = [copy for copy in found if copy[0].key == owner]
                if held:
                    found = held
                elif owner in self._given:
                    self._problem(f"{name}: the map gives it to {owner}, which does not define it")
                elif owner in self._unhooked:
                    self.outside.add(name)
                    found = []
                else:
                    self._problem(f"{name}: the map gives it to {owner}, "
                                  "which is neither an object with the hook nor one without it")
                    found = []
            self._copies_of[name] = found
        return found

    def _target(self, index: _Index, here: int, number: int) -> tuple:
        """What a relocation's symbol points at. A piece of data is a node,
        (index, section number), which `_describe` turns into its digest."""
        symbol = index.symbols.get(number)
        if symbol is None:
            self._problem(f"{index.key}: a relocation names symbol {number}, which the symbol table does not hold")
            return ("unknown", number)
        _, name, value, section, _, storage = symbol[:6]
        if 0 < section == here:
            return ("here", value)
        if storage == COFF_EXTERNAL or (storage == COFF_WEAK_EXTERNAL and name in self._externals):
            copies = self._copies(name)
            if not copies:
                return ("external", name)
            if len(copies) > 1:
                return ("copies", name, [self._defined(*copy) for copy in copies])
            return self._defined(*copies[0])
        if section > 0:
            return self._defined(index, symbol)
        if storage == COFF_WEAK_EXTERNAL:
            return self._weak(index, here, symbol)
        if section == COFF_ABSOLUTE:
            return ("absolute", name, value)
        return ("undefined", storage, name)

    def _weak(self, index: _Index, here: int, symbol: list) -> tuple:
        """A weak external that no given object defines: the names of the
        weak externals in its chain of defaults, and what the last default
        points at. The chain is followed one time for each symbol of the
        object at most: a longer chain uses a symbol two times, and is a
        problem."""
        names, current = [symbol[1]], symbol
        for _ in index.symbols:
            if current[8] is None:
                return "weak", tuple(names)
            default = index.symbols.get(current[8])
            if default is None:
                self._problem(f"{index.key}: the weak external {current[1]} names symbol {current[8]} as its "
                              "default, which the symbol table does not hold")
                return "weak", tuple(names), ("unknown", current[8])
            if default[5] != COFF_WEAK_EXTERNAL or default[3] != 0 or default[1] in self._externals:
                return "weak", tuple(names), self._target(index, here, default[0])
            names.append(default[1])
            current = default
        self._problem(f"{index.key}: the chain of defaults of the weak external {symbol[1]} does not end")
        return "weak", tuple(names), ("endless",)

    def _defined(self, index: _Index, symbol: list) -> tuple:
        section = index.sections.get(symbol[3])
        if section is None:
            self._problem(f"{index.key}: a relocation points into section {symbol[3]}, which holds debug information")
            return ("debug", symbol[1])
        if section[2] & COFF_CODE:
            if symbol[4] & 0xF0 == COFF_FUNCTION:
                return ("function", index.key_of(symbol))
            if index.keys_in(symbol[3]):
                return ("code", index.keys_in(symbol[3]), symbol[2])
        # Data, or code in which no function starts: its contents count.
        return ("data", (index, symbol[3]), symbol[2])

    def _describe(self, target: tuple, label) -> str:
        """A target as text, with each node replaced by `label` of it: its
        digest. The text of a target without a node is kept: many relocations
        point at one function."""
        if target[0] == "data":
            return f"('data', {label(target[1])!r}, {target[2]})"
        if target[0] != "copies":
            text = self._texts.get(target)
            if text is None:
                text = self._texts[target] = repr(target)
            return text
        unique = sorted({self._describe(copy, label) for copy in target[2]})
        if len(unique) == 1:
            return unique[0]
        self._problem(f"{target[1]}: {len(target[2])} objects define it with different contents, "
                      "and the map names none of them")
        return repr(("copies", tuple(unique)))

    def _resolve(self, node: tuple) -> list:
        index, number = node
        return [(offset, kind, self._target(index, number, symbol))
                for offset, kind, symbol in index.sections[number][5]]

    @staticmethod
    def _edges(node: tuple, targets: list) -> list:
        """The pieces of data that a node points at directly: through its
        relocations, and the sections that go with it."""
        index, number = node
        found = [(index, n) for n in index.associated.get(number, ())]
        for _, _, target in targets:
            found.extend(t[1] for t in (target[2] if target[0] == "copies" else (target,)) if t[0] == "data")
        return found

    def _parts(self, node: tuple, targets: list, label) -> bytes:
        """What a section's digest is made of, put together with NUL between
        the parts, because no name in COFF holds NUL: the relocations in the
        sequence of their offsets, then the sections that go with it by name
        and digest. No section number."""
        index, number = node
        _, name, characteristics, size, digest = index.sections[number][:5]
        relocations = sorted(f"{offset:010} {kind} {self._describe(target, label)}" for offset, kind, target in targets)
        associated = sorted(f"{index.sections[n][1]} {label((index, n))}" for n in index.associated.get(number, ()))
        parts = (name, str(characteristics), str(size), str(digest), str(len(relocations)), *relocations, *associated)
        # surrogateescape gives back the bytes of a name that is not UTF-8 (see _text).
        return "\0".join(parts).encode("utf-8", "surrogateescape")

    def _data_digest(self, node: tuple) -> str:
        """The digest of a piece of data. Tarjan's algorithm, with a list of
        its own and not with recursion, finds the cycles: a group of data that
        point at each other gets its digests after each piece of data that the
        group points at."""
        found = self._data.get(node)
        if found is not None:
            return found
        targets = self._resolve(node)
        edges = self._edges(node, targets)
        if not edges:
            # Most data points at no other data: the flags, the unwind data, the constants.
            found = self._data[node] = _sha(self._parts(node, targets, None))
            return found
        order, low, place, stack, work = {}, {}, {}, [], []

        def visit(vertex: tuple, resolved: list, reached: list) -> None:
            order[vertex] = low[vertex] = len(order)
            place[vertex] = len(stack)
            stack.append(vertex)
            self._targets[vertex] = resolved
            work.append((vertex, iter(reached)))

        visit(node, targets, edges)
        while work:
            vertex, edges = work[-1]
            for other in edges:
                if other in self._data:
                    continue
                if other not in order:
                    resolved = self._resolve(other)
                    visit(other, resolved, self._edges(other, resolved))
                    break
                if place.get(other) is not None:
                    low[vertex] = min(low[vertex], order[other])
            else:
                work.pop()
                if work:
                    low[work[-1][0]] = min(low[work[-1][0]], low[vertex])
                if low[vertex] == order[vertex]:
                    component = stack[place[vertex]:]
                    del stack[place[vertex]:]
                    for member in component:
                        place[member] = None
                    self._close(component)
        return self._data[node]

    def _close(self, component: list) -> None:
        """Give each member of a group its digest. In a cycle, rounds tell the
        members apart (colour refinement). Each round hashes a member's parts
        with the last round's hashes of the members that it points at. The
        rounds stop when a round tells no more members apart, after one round
        for each member at most. Each member's digest is then made of all the
        members' hashes, sorted, and its own hash. Thus the file's layout does
        not change it, and a pointer to one member is not a pointer to a
        different member."""
        if len(component) == 1:
            member = component[0]
            targets = self._targets.pop(member)
            self._data[member] = _sha(self._parts(member, targets, lambda n: "" if n == member else self._data[n]))
            return
        members = set(component)
        hashes, distinct = dict.fromkeys(component, ""), 1
        for _ in component:
            last = hashes
            hashes = {m: _sha(self._parts(m, self._targets[m], lambda n: last[n] if n in members else self._data[n]))
                      for m in component}
            found, distinct = distinct, len(set(hashes.values()))
            if distinct == found:
                break
        whole = _sha(" ".join(sorted(hashes.values())).encode())
        for member in component:
            self._data[member] = _sha((whole + hashes[member]).encode())
            del self._targets[member]

    def _functions(self) -> dict:
        """{function key: [(index, section number, symbol)]} of each function
        in the given objects: the copies of an external function, and a
        static function in its object."""
        if self._by_key is None:
            self._by_key = {}
            for index in self._objects:
                for number, symbols in index.functions.items():
                    for symbol in symbols:
                        self._by_key.setdefault(index.key_of(symbol), []).append((index, number, symbol))
        return self._by_key

    def _held(self, key: str) -> list:
        """The (index, section number) of the copies of a function that the
        program can hold: for an external function, those of _copies."""
        entries = self._functions().get(key, [])
        if entries and entries[0][2][5] == COFF_EXTERNAL:
            owners = {copy[0].key for copy in self._copies(key)}
            entries = [entry for entry in entries if entry[0].key in owners]
        return [(index, number) for index, number, _ in entries]

    def _code_digest(self, index: _Index, number: int) -> str:
        node = (index, number)
        found = self._code.get(node)
        if found is None:
            found = self._code[node] = _sha(self._parts(node, self._resolve(node), self._data_digest))
        return found

    def _function_digest(self, key: str) -> Optional[str]:
        """The digest of one function, or None when the program does not
        hold it, or when its copies give different digests: then a problem
        names it."""
        if key not in self._found:
            found = {self._code_digest(index, number) for index, number in self._held(key)}
            entries = self._functions().get(key, [])
            if len(found) > 1 and entries[0][2][5] == COFF_EXTERNAL:
                self._problem(f"{key}: {len(entries)} objects define it with {len(found)} different digests, "
                              "and the map names none of them")
            elif len(found) > 1:
                self._problem(f"{key}: {len(entries)} static functions in one object have this name, "
                              f"with {len(found)} different digests")
            self._found[key] = found.pop() if len(found) == 1 else None
        return self._found[key]

    def digests(self, keys: Optional[Iterable] = None) -> dict:
        """{function key: digest} of the functions that `keys` names, or of
        each function in the given objects when `keys` is None (decision 711
        (8)). Only those functions' code sections, and the data that they
        point at directly or through other data, are digested, and each digest
        is kept for the next call. An external function that more than one
        object defines uses the copy that the map names. When the map names
        none, the copies must give one digest. If not, the result does not
        include the function, and a problem names it. A key that the program
        does not hold is also not in the result: a function that the result
        does not include counts as changed (decision 547)."""
        found = {}
        for key in self._functions() if keys is None else keys:
            digest = self._function_digest(key)
            if digest is not None:
                found[key] = digest
        return found

    def _start_up(self) -> tuple:
        """(keys, code sections) of the functions that a section with a name
        that starts with `.CRT$` points at, in the given objects (decision
        624)."""
        if self._start is None:
            keys, sections = set(), set()
            for index in self._objects:
                for number, section in index.sections.items():
                    if section[1].startswith(START_UP_PREFIX):
                        for _, _, target in self._resolve((index, number)):
                            self._start_up_target(index.key, section[1], target, keys)
            for key in keys:
                sections.update(self._held(key))
            self._start = (keys, sections)
        return self._start

    def _start_up_target(self, where: str, section: str, target: tuple, keys: set) -> None:
        kind = target[0]
        if kind in ("function", "external"):
            keys.add(target[1])
        elif kind == "code":
            keys.update(target[1])
        elif kind == "weak" and len(target) > 2:
            self._start_up_target(where, section, target[2], keys)
        elif kind == "copies":
            for copy in target[2]:
                self._start_up_target(where, section, copy, keys)
        else:
            self._problem(f"{where}: the start-up entry {section} points at {kind}, not at a function")

    def start_up(self) -> list:
        """The sorted keys of the start-up functions: the functions that a
        section with a name that starts with `.CRT$` points at.
        `sancov.module_ctor_bool_flag` is one of them, through
        `.CRT$XCA00002`. No record of a test holds them (decisions 624 and 711
        (6))."""
        return sorted(self._start_up()[0])

    def unflagged(self) -> list:
        """The sorted keys of the functions without a flag (decisions 586 and
        711 (5)). A code section that has no entry in the hook's table
        (.SCOVP$M) counts as entered by each test of each program that links
        it: each function in it is in the result, its funclets too. A funclet
        in a section that has an entry is part of its function's digest, and
        is not in the result. The sections of the start-up functions are not
        in the result, with their funclets (decision 711 (6)). An external
        function is in the result when a copy that the program can hold is
        without an entry."""
        table = HOOK_SECTIONS["coff-object"][1]
        start = self._start_up()[1]
        return sorted(key for key in self._functions() if any(
            (index, number) not in start
            and not any(index.sections[n][1] == table for n in index.associated.get(number, ()))
            for index, number in self._held(key)))

    def directives(self) -> str:
        """One digest of the linker directives of all given objects, `/INCLUDE`
        included (decision 711 (7)): the SHA-256 of each .drectve section,
        sorted, so that the sequence of the objects does not change it. Two
        objects with the same directives count two times."""
        found = sorted(str(s[4]) for index in self._objects for s in index.sections.values() if s[1] == DIRECTIVES)
        return _sha("\0".join(found).encode())


# --- the part of each program (M1-132 step 4, decision 713 (4)) ---------------
#
# scripts/run-tests.py makes one CodeReader for each run, and one ProgramCode
# for each program with the hook that it examines. This part reads the map,
# the .pdb file and the objects, keeps the summaries, and gives the keys of a
# record, the digests, the functions without a flag, the directives and the
# binary part. The driver keeps the records, the store, the rule that skips a
# test, the link commands from Ninja and the problems.

# The section of the hook's address table in an object: an object carries the
# hook when it has this section (decision 584). COFF writes the name of each
# section as text, in the section header or in the string table. Thus an
# object that does not hold these bytes cannot have the section, and the
# reader does not read it.
HOOK_TABLE = HOOK_SECTIONS["coff-object"][1]
# The text of the function part for a key that the program does not hold at
# this time: a removed function counts as changed (decision 547).
MISSING_FUNCTION = "missing"
# The parts of the time that a CodeReader adds up (decisions 567 and 709 (2)).
TIME_PARTS = ("reading", "summaries", "digests")


class SummaryCache:
    """The summary of each object with the hook, kept between runs in `folder`
    (decisions 567, 711 (8) and 714) -- the driver gives
    `<tree>/run-tests/objects/`. Each object has one JSON file, with a name
    made from the object's key (decision 711 (4)): the SHA-256 of the key,
    which is a safe file name for each key. The file holds the key, the
    SHA-256 of the object's bytes, SUMMARY_VERSION and the summary. The reader
    uses a summary only when the three agree with the object. If not, the
    reader makes the summary again, and the new file replaces the file of that
    key. Thus the folder holds one file for each object. Only a full run
    deletes files (`prune`). With no folder, no summary is kept. `made` and
    `loaded` count the summaries made from an object and read from a file."""

    def __init__(self, folder: Optional[Path] = None):
        self.folder = None if folder is None else Path(folder)
        self.made, self.loaded = 0, 0

    @staticmethod
    def file_name(key: str) -> str:
        """The name of the file of the object with the key `key`."""
        return _sha(key.encode("utf-8", "surrogateescape")) + ".json"

    def get(self, key: str, sha: str) -> Optional[dict]:
        """The summary kept for the object with the key `key` and the SHA-256
        `sha`, or None."""
        if self.folder is None:
            return None
        try:
            kept = json.loads((self.folder / self.file_name(key)).read_bytes())
        except (OSError, ValueError):
            return None
        if not isinstance(kept, dict) or (kept.get("key"), kept.get("sha256"), kept.get("version")) != (
                key, sha, SUMMARY_VERSION):
            return None
        summary = kept.get("summary")
        if not isinstance(summary, dict) or summary.get("version") != SUMMARY_VERSION:
            return None
        self.loaded += 1
        return summary

    def put(self, key: str, sha: str, summary: dict) -> None:
        """Keep the summary of the object with the key `key` and the SHA-256
        `sha`: the new file replaces the file of that key. The file is written
        with write_bytes: write_text adds CR to each line end on Windows."""
        self.made += 1
        if self.folder is not None:
            self.folder.mkdir(parents=True, exist_ok=True)
            kept = {"key": key, "sha256": sha, "version": SUMMARY_VERSION, "summary": summary}
            (self.folder / self.file_name(key)).write_bytes(json.dumps(kept, separators=(",", ":")).encode())

    def prune(self, keys: Iterable) -> list:
        """Delete each file in the folder that is not the file of a key in
        `keys` (decision 714) -- the file of an object that no program of the
        tree links at this time, and each file that is not a summary file of this
        layout -- and give their names, sorted. Only a full run calls it,
        because only a full run reads each program with the hook: `keys` are
        then the `objects` of each program with the hook of the tree."""
        if self.folder is None or not self.folder.is_dir():
            return []
        wanted = {self.file_name(key) for key in keys}
        unused = sorted(p.name for p in self.folder.iterdir() if p.is_file() and p.name not in wanted)
        for name in unused:
            (self.folder / name).unlink()
        return unused


# The version of the layout of a kept table. Change it when the layout or the
# meaning of a table changes, so that a driver does not use a table kept in a
# previous layout (decision 715 (1)).
TABLE_VERSION = 1


class TableCache:
    """The table of each program with the hook, kept between runs in `folder`
    (decision 715 (1)) -- the driver gives `<tree>/run-tests/tables/`. Each
    program has one JSON file, with a name made from the program's key as
    SummaryCache makes the name of a summary. The file holds the key,
    TABLE_VERSION, the SHA-256 of the program's bytes and the SHA-256 of its
    map, which the driver gives, and the table that ProgramCode.table gives.
    The driver uses a table only when the key, the version and the two
    SHA-256 agree; if not, it reads the program in full, and the new file
    replaces the file of that key. Only a full run deletes files (`prune`, as
    SummaryCache.prune). With no folder, no table is kept."""

    file_name = staticmethod(SummaryCache.file_name)
    prune = SummaryCache.prune

    def __init__(self, folder: Optional[Path] = None):
        self.folder = None if folder is None else Path(folder)

    def get(self, key: str, program: str, linker_map: str) -> Optional[dict]:
        """The table kept for the program with the key `key`, the SHA-256
        `program` of its bytes and `linker_map` of its map, or None."""
        if self.folder is None:
            return None
        try:
            kept = json.loads((self.folder / self.file_name(key)).read_bytes())
        except (OSError, ValueError):
            return None
        if not isinstance(kept, dict) or (kept.get("key"), kept.get("version"), kept.get("program"),
                                          kept.get("map")) != (key, TABLE_VERSION, program, linker_map):
            return None
        table = kept.get("table")
        return table if valid_table(table) else None

    def put(self, key: str, program: str, linker_map: str, table: dict) -> None:
        """Keep the table of the program with the key `key`: the new file
        replaces the file of that key. Written with write_bytes, as
        SummaryCache.put writes."""
        if self.folder is not None:
            self.folder.mkdir(parents=True, exist_ok=True)
            kept = {"key": key, "version": TABLE_VERSION, "program": program, "map": linker_map, "table": table}
            (self.folder / self.file_name(key)).write_bytes(json.dumps(kept, separators=(",", ":")).encode())


def valid_table(table) -> bool:
    """Examine if `table` has the layout that ProgramCode.table gives: so that a
    file that is not complete, or that a person changed, is not used."""
    if not isinstance(table, dict) or not isinstance(table.get("functions"), dict):
        return False
    if not (table.get("entries") is None or type(table.get("entries")) is int):
        return False
    if not (table.get("problem") is None or isinstance(table.get("problem"), str)):
        return False
    return all(isinstance(address, str) and isinstance(found, list) and len(found) == 2
               and (found[0] is None or isinstance(found[0], str)) and isinstance(found[1], list)
               and all(isinstance(problem, str) for problem in found[1])
               for address, found in table["functions"].items())


class ProgramTable:
    """The record checks of one program from its kept table (decision 715
    (1)), with the methods of ProgramCode that a record uses: table_entries,
    keys and problems. It reads no map, .pdb file, library or summary."""

    def __init__(self, name: str, table: dict):
        self.name = name
        self._table = table

    @property
    def problems(self) -> list:
        """No problems: a program with a problem has no kept table."""
        return []

    def table_entries(self) -> tuple:
        """(entries, problem), as ProgramCode.table_entries gave them."""
        return self._table["entries"], self._table["problem"]

    def keys(self, addresses: Iterable) -> tuple:
        """(keys, problems) of a record's addresses, as ProgramCode.keys gives
        them: the sorted keys, with the start-up functions left out, and a
        problem for each address that is not one function, which names the
        program. An address that is not an entry of the hook's table is a
        problem too: the recorder writes only the addresses of the table."""
        keys, problems = set(), []
        for address in addresses:
            found = self._table["functions"].get(f"{address:x}")
            if found is None:
                problems.append(f"{self.name}: the address {address:#x} is not an entry of the hook's table of "
                                "the program")
                continue
            problems += [f"{self.name}: {problem}" for problem in found[1]]
            if found[0] is not None:
                keys.add(found[0])
        return sorted(keys), problems


class _Member(NamedTuple):
    name: str          # as the library names it
    key: str           # object_key, with the member's file name
    kind: str          # "hooked", "without", "import" or "unreadable"
    sha: str           # of its bytes
    summary: Optional[dict]


class _Library(NamedTuple):
    sha: str           # of its bytes
    hooked: bool       # a member carries the hook
    members: dict      # {file name, casefolded: [_Member]}
    problems: list


class ProgramCode:
    """The part of one program with the hook (decision 713 (4)), which
    CodeReader.program makes. `problems` gives each problem found."""

    def __init__(self, name: str, reader: "CodeReader"):
        self.name = name
        self._reader = reader
        self._problems, self._binary = [], []
        self._program, self._index, self._key_of, self._unflagged = None, None, None, None
        # The addresses of the hook's table, from the program's bytes (table_addresses), or None.
        self._addresses = None
        self._table = (None, f"{name}: the hook's table is not counted: the program or its map cannot be read")
        # The sorted keys of the objects with the hook that the program links, as SummaryCache names them: a
        # full run gives the keys of each program to SummaryCache.prune (decision 714).
        self.objects = []

    def problem(self, text: str) -> None:
        self._problems.append(f"{self.name}: {text}")

    @property
    def problems(self) -> list:
        """Each problem found, as a text that starts with the program's name
        (decisions 590, 704, 705, 710 (1) and 713 (3)). The digests add their
        problems when the driver uses them: the driver examines this list
        after the last call of function_part."""
        return self._problems + [f"{self.name}: {p}" for p in (self._program.problems if self._program else ())]

    def keys(self, addresses: Iterable) -> tuple:
        """(keys, problems) of a record's addresses: the sorted keys of the
        functions at the addresses (decisions 582 and 695), with the start-up
        functions left out (decisions 624 and 711 (6)), and a problem for each
        address that is not one function, which names the program."""
        if self._program is None:
            return [], [f"{self.name}: the addresses are not turned into functions: the program has problems"]
        keys, problems = record_functions(self._index, addresses, self._key_of)
        start = set(self._reader.timed("digests", self._program.start_up))
        return [key for key in keys if key not in start], [f"{self.name}: {p}" for p in problems]

    def table_entries(self) -> tuple:
        """(entries, problem): the number of entries in the hook's table, from
        the map's markers and from the length of the section `.SCOVP`, which
        must agree (decision 712)."""
        return self._table

    def table(self) -> Optional[dict]:
        """The table that TableCache keeps for the program (decision 715 (1)),
        or None: for a program with a problem, and for a program with table
        addresses that cannot be read or that are not as many as the entries.
        Then no table is kept, and the driver reads the program in full at
        each run. "entries" and "problem" are what table_entries gives.
        "functions" holds, for each address of the table in hexadecimal, what
        keys gives for only that address: the key of its function -- None
        for a start-up function, which keys leaves out (decisions 624 and 711
        (6)), and for an address with a problem -- and its problems, without
        the name of the program, which ProgramTable adds."""
        if self.problems or self._addresses is None or len(self._addresses) != self._table[0]:
            return None
        start = set(self._reader.timed("digests", self._program.start_up))
        functions = {}
        for address in self._addresses:
            keys, problems = record_functions(self._index, [address], self._key_of)
            functions[f"{address:x}"] = [keys[0] if keys and keys[0] not in start else None, problems]
        return {"entries": self._table[0], "problem": self._table[1], "functions": functions}

    def function_part(self, keys: Iterable, link_command: Optional[str] = None) -> Optional[str]:
        """One SHA-256 of the part of the program that a test with the
        functions `keys` uses (decision 583), or None when the program has a
        problem: then its tests run (decision 713 (3)). The part holds, each in
        a sequence that the files do not set:
          - each key of `keys` and each function without a flag (decisions 586
            and 711 (5)), with its digest, or MISSING_FUNCTION when the program
            does not hold it at this time (decision 547). Only these digests are
            computed (decision 711 (8));
          - the binary part (decisions 583, 585 and 713 (1)): each library
            without the hook with its SHA-256 and the names of the modules
            taken from it, each object without the hook linked directly with
            its SHA-256, and each member without the hook of a library with
            the hook with its key and its SHA-256;
          - the digest of the linker directives (decision 711 (7));
          - `link_command`, the link command from Ninja (decision 709 (4)),
            which the driver gives."""
        if self._program is None:
            return None
        timed = self._reader.timed
        if self._unflagged is None:
            self._unflagged = timed("digests", self._program.unflagged)
        wanted = sorted(set(keys) | set(self._unflagged))
        digests = timed("digests", self._program.digests, wanted)
        directives = timed("digests", self._program.directives)
        if self.problems:
            return None
        part = {"functions": [[key, digests.get(key, MISSING_FUNCTION)] for key in wanted], "binary": self._binary,
                "directives": directives, "link": link_command}
        return _sha(json.dumps(part, sort_keys=True).encode())


class CodeReader:
    """The data that more than one program of a run uses (decision 713 (4)):
    the SHA-256 of each library and each object read, the members of each
    library, and the summary of each object with the hook, which `cache` also
    keeps between runs. `seconds` adds up the time of the reading, the
    summaries and the digests (decisions 567 and 709 (2)). `tree` is the build
    tree, which the keys of the objects are relative to (decision 711 (4))."""

    def __init__(self, tree, cache_folder: Optional[Path] = None):
        self.tree = str(tree)
        self.cache = SummaryCache(cache_folder)
        self.seconds = dict.fromkeys(TIME_PARTS, 0.0)
        self._objects, self._direct, self._libraries = {}, {}, {}

    def timed(self, part: str, work, *arguments):
        """The result of `work(*arguments)`, its time added to `seconds[part]`."""
        start = time.perf_counter()
        try:
            return work(*arguments)
        finally:
            self.seconds[part] += time.perf_counter() - start

    def _read(self, path) -> Optional[bytes]:
        try:
            return self.timed("reading", Path(path).read_bytes)
        except OSError:
            return None

    def _object(self, data: bytes, key: str) -> tuple:
        """(`kind`, SHA-256, summary) of the object or the member of a library
        with the key `key`. The first item is "hooked", with the summary;
        "import", an import member; "without", without the hook; or
        "unreadable", when it holds the text of the hook's table and
        read_object cannot read it."""
        sha = self.timed("reading", _sha, data)
        if (key, sha) not in self._objects:
            self._objects[(key, sha)] = self._kind(data, key, sha)
        return self._objects[(key, sha)][0], sha, self._objects[(key, sha)][1]

    def _kind(self, data: bytes, key: str, sha: str) -> tuple:
        if is_import_member(data):
            return "import", None
        if HOOK_TABLE.encode() not in data:
            return "without", None
        summary = self.timed("summaries", self.cache.get, key, sha)
        if summary is None:
            found = self.timed("summaries", read_object, data)
            if found is None:
                return "unreadable", None
            if not any(s.name == HOOK_TABLE for s in found.sections):
                return "without", None
            summary = self.timed("summaries", object_summary, found)
            self.timed("summaries", self.cache.put, key, sha, summary)
        if not any(s[1] == HOOK_TABLE for s in summary["sections"]):
            return "without", None
        return "hooked", summary

    def _library(self, path: str) -> Optional[_Library]:
        """The members of a library, read one time in a run, or None for a
        file that is not there or that is not an archive. A library carries
        the hook when one of its members carries it. Then the decision 583
        does not hash the library, each member that is not an import member
        must be an object that read_object can read (decision 710 (1)), and no
        two members can have one file name (decision 705)."""
        if path in self._libraries:
            return self._libraries[path]
        key, data = object_key(self.tree, path), self._read(path)
        members = None if data is None else self.timed("reading", archive_members, data)
        if members is None:
            self._libraries[path] = None
            return None
        found = []
        for name, body in members:
            member = object_key(self.tree, path, _file_name(name))
            found.append(_Member(name, member, *self._object(body, member)))
        problems = [f"the member {m.name} of the library {key} holds the text {HOOK_TABLE}, and read_object cannot "
                    "read it" for m in found if m.kind == "unreadable"]
        hooked = any(m.kind == "hooked" for m in found)
        if hooked:
            for place, (member, (_, body)) in enumerate(zip(found, members)):
                if member.kind == "without" and self.timed("summaries", read_object, body) is None:
                    found[place] = member._replace(kind="unreadable")
                    problems.append(f"the member {member.name} of the library {key}, which carries the hook, is not "
                                    "an import member, and read_object cannot read it")
            problems += same_file_names(f"the library {key}, which carries the hook", [m.name for m in found])
        by_name = {}
        for member in found:
            by_name.setdefault(_file_name(member.name).casefold(), []).append(member)
        self._libraries[path] = _Library(self.timed("reading", _sha, data), hooked, by_name, problems)
        return self._libraries[path]

    def _linked(self, path: str) -> Optional[tuple]:
        """_object of an object linked directly, read one time in a run, or None."""
        if path not in self._direct:
            data = self._read(path)
            self._direct[path] = None if data is None else self._object(data, object_key(self.tree, path))
        return self._direct[path]

    def program(self, path) -> ProgramCode:
        """The part of the program at `path`, which carries the hook, read from
        the program, its map and its .pdb file (decision 705), and from each
        object and library that the map names. A problem is kept in the
        result, and does not stop the reader."""
        path = Path(path)
        code = ProgramCode(object_key(self.tree, str(path)), self)
        data, text, pdb = self._read(path), self._read(path.with_suffix(".map")), self._read(path.with_suffix(".pdb"))
        linker_map = None if text is None else self.timed("reading", read_map, text.decode("utf-8", "surrogateescape"))
        modules = None if pdb is None else self.timed("reading", pdb_modules, pdb)
        for found, file in ((data, path), (linker_map, path.with_suffix(".map")), (modules, path.with_suffix(".pdb"))):
            if found is None:
                code.problem(f"{object_key(self.tree, str(file))} is not there, or cannot be read")
        if data is not None and linker_map is not None and modules is not None:
            self._fill(code, data, linker_map, modules)
        return code

    def _fill(self, code: ProgramCode, data: bytes, linker_map: LinkerMap, modules: list) -> None:
        paths, problems = module_paths(linker_map, modules)
        problems += same_file_names("the objects linked directly", direct_objects(modules))
        held, unhooked, binary, taken = {}, set(), [], {}
        for (library, name), full in sorted(paths.items()):
            if library:
                taken.setdefault(full, []).append(name)
                continue
            key, found = object_key(self.tree, full), self._linked(full)
            if found is None:
                problems.append(f"the object {key}, linked directly, is not there, or cannot be read")
            elif found[0] == "hooked":
                held[key] = (key, found[2])
            elif found[0] == "unreadable":
                problems.append(f"the object {key}, linked directly, holds the text {HOOK_TABLE}, and read_object "
                                "cannot read it")
            else:
                unhooked.add(key)
                binary.append(["object", key, found[1]])
        for full, names in sorted(taken.items()):
            problems += self._take(full, names, held, unhooked, binary)
        for text in problems:
            code.problem(text)
        keys = {pair: object_key(self.tree, full, pair[1] if pair[0] else None) for pair, full in paths.items()}
        chosen = {}
        for symbol in linker_map.symbols:
            if not symbol.static and (symbol.library, symbol.object) in keys:
                chosen.setdefault(symbol.name, keys[(symbol.library, symbol.object)])
        summaries = {key: summary for key, (_, summary) in held.items()}
        code._program = self.timed("digests", Program, summaries, chosen, unhooked)
        code.objects = sorted({cached for cached, _ in held.values()})
        code._binary = sorted(binary)
        code._index, code._key_of = address_index(linker_map), lambda s: symbol_key(s, paths, self.tree)
        count, problem = hook_table_entries(data, linker_map)
        code._table = (count, None if problem is None else f"{code.name}: {problem}")
        code._addresses = table_addresses(data)
        if problem is not None:
            code.problem(problem)

    def _take(self, path: str, names: list, held: dict, unhooked: set, binary: list) -> list:
        """Add the modules `names` that the program takes from the library at
        `path`, and give the problems. From a library without the hook: the
        library's SHA-256 and the names (decision 585). From a library with
        the hook: each member with the hook by its summary, into `held` with
        its key in SummaryCache, and each member without it by the SHA-256 of
        the member (decision 713 (1))."""
        key, library = object_key(self.tree, path), self._library(path)
        if library is None:
            return [f"the library {key} is not there, or is not an archive"]
        problems = list(library.problems)
        if not library.hooked:
            unhooked.update(object_key(self.tree, path, name) for name in names)
            binary.append(["library", key, library.sha, sorted(names)])
            return problems
        for name in names:
            found, member = library.members.get(_file_name(name).casefold(), []), object_key(self.tree, path, name)
            if not found:
                problems.append(f"the library {key} holds no member {name}")
            elif len(found) == 1 and found[0].kind == "hooked":
                held[member] = (found[0].key, found[0].summary)
            elif len(found) == 1 and found[0].kind in ("without", "import"):
                unhooked.add(member)
                binary.append(["member", member, found[0].sha])
            # Two members with one file name, or a member that read_object cannot read: library.problems
            # names each.
        return problems


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


def made_pe(names: list, sizes: Optional[dict] = None) -> bytes:
    """A PE program holding only section headers. `sizes` maps a name to its
    (VirtualSize, SizeOfRawData); each other section has (0, 16)."""
    dos = b"MZ" + b"\0" * 0x3A + struct.pack("<I", 0x40)
    headers = b""
    for name in names:
        virtual, raw = (sizes or {}).get(name, (0, 16))
        headers += name.encode().ljust(8, b"\0") + struct.pack("<IIIIIIHHI", virtual, 0, raw, 0, 0, 0, 0, 0, 0x40)
    return dos + b"PE\0\0" + struct.pack("<HHIIIHH", 0x8664, len(names), 0, 0, 0, 0, 0) + headers


# The preferred base address of the made-up programs, as the made-up maps give it.
MADE_BASE = 0x140000000


def made_table_program(addresses: list, extra: tuple = (), base: int = MADE_BASE) -> bytes:
    """A PE program with the bytes of the hook's table, as lld-link links it
    with the recorder's markers (decision 712): the start of a PE32+ optional
    header, to its ImageBase, and the section .SCOVP with its bytes -- the
    start marker, an entry for each address, which is the address with `base`
    added and the value 1, and the stop marker. `extra` names more sections,
    so that a program can have other bytes and the same table."""
    table = bytes(TABLE_START_LENGTH) + b"".join(struct.pack("<QQ", base + a, 1) for a in addresses) + bytes(8)
    names = [".text", ".SCOV", ".SCOVP", *extra]
    optional = struct.pack("<HBBIIIIIQ", PE32_PLUS, 0, 0, 0, 0, 0, 0, 0, base)
    header = 0x40
    raw = header + 4 + 20 + len(optional) + COFF_SECTION_HEADER * len(names)
    headers = b""
    for name in names:
        size, at = (len(table), raw) if name == ".SCOVP" else (0, 0)
        headers += name.encode().ljust(8, b"\0") + struct.pack("<IIIIIIHHI", size, 0, size, at, 0, 0, 0, 0, 0x40)
    dos = b"MZ" + b"\0" * 0x3A + struct.pack("<I", header)
    coff = struct.pack("<HHIIIHH", 0x8664, len(names), 0, 0, 0, len(optional), 0)
    return dos + b"PE\0\0" + coff + optional + headers + table


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


def _base64_name(value: int) -> str:
    """The six base-64 digits of a string-table offset, as LLVM writes a `//` name."""
    alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    text = ""
    for _ in range(6):
        text = alphabet[value % 64] + text
        value //= 64
    return text


def made_definition(number: int, selection: int, big: bool = False) -> bytes:
    """A section-definition auxiliary record: the associated section and the
    COMDAT selection, the high 16 bits of the number at offset 16 in a big object."""
    record = struct.pack("<IHHIHBB", 0, 0, 0, 0, number & 0xFFFF, selection, 0)
    return record + (struct.pack("<H", number >> 16) if big else b"")


def made_object(sections: list, symbols: list, big: bool = False, base64: tuple = ()) -> bytes:
    """A COFF object, built byte by byte. Each section is (name, characteristics,
    contents, relocations, overflow): the contents are bytes, or a number of
    bytes for uninitialised data; each relocation is (offset, symbol index, type); overflow
    writes the count in the first relocation. Each symbol is (name, value,
    section number, type, storage class, auxiliary records). The names in
    `base64` go through the string table in the `//` format."""
    record = BIGOBJ_SYMBOL if big else COFF_SYMBOL
    strings = b""

    def long_name(text: str) -> int:
        nonlocal strings
        offset = 4 + len(strings)
        strings += text.encode() + b"\0"
        return offset

    body_at = (56 if big else 20) + 40 * len(sections)
    headers, body = b"", b""
    for name, characteristics, contents, relocations, overflow in sections:
        raw = name.encode()
        if len(raw) > 8:
            offset = long_name(name)
            raw = ("//" + _base64_name(offset) if name in base64 else f"/{offset}").encode()
        uninitialised = isinstance(contents, int)
        size = contents if uninitialised else len(contents)
        data_at = 0 if uninitialised else body_at + len(body)
        body += b"" if uninitialised else contents
        written, count = list(relocations), len(relocations)
        if overflow:
            characteristics |= 0x01000000
            written, count = [(len(relocations) + 1, 0, 0)] + written, 0xFFFF
        relocations_at = body_at + len(body) if written else 0
        body += b"".join(struct.pack("<IIH", *r) for r in written)
        headers += raw.ljust(8, b"\0") + struct.pack("<IIIIIIHHI", 0, 0, size, data_at, relocations_at, 0, count, 0,
                                                     characteristics)
    table, records = b"", 0
    for name, value, section, kind, storage, aux in symbols:
        raw = name.encode()
        raw = b"\0\0\0\0" + struct.pack("<I", long_name(name)) if len(raw) > 8 else raw.ljust(8, b"\0")
        number = struct.pack("<i", section) if big else struct.pack("<H", section & 0xFFFF)
        table += raw + struct.pack("<I", value) + number + struct.pack("<HBB", kind, storage, len(aux))
        table += b"".join(a.ljust(record, b"\0") for a in aux)
        records += 1 + len(aux)
    symbols_at = body_at + len(body)
    if big:
        head = b"\0\0\xff\xff" + struct.pack("<HHI", 2, 0x8664, 0) + BIGOBJ_CLASS
        head += struct.pack("<IIIIIII", 0, 0, 0, 0, len(sections), symbols_at, records)
    else:
        head = struct.pack("<HHIIIHH", 0x8664, len(sections), 0, symbols_at, records, 0, 0)
    return head + headers + body + table + struct.pack("<I", 4 + len(strings)) + strings


def made_import(symbol: str, dll: str) -> bytes:
    """A short import member of an import library: the import header of
    Microsoft's PE format specification, then the symbol and the DLL names."""
    names = symbol.encode() + b"\0" + dll.encode() + b"\0"
    return struct.pack("<HHHHIIHH", 0, 0xFFFF, 0, 0x8664, 0, len(names), 0, 1 << 2) + names


def made_archive(members: list, linkers: int = 2, end: bytes = b"\0") -> bytes:
    """An archive, built byte by byte: `linkers` linker members (two in the COFF
    format, one or none in the GNU format), the long-name member with each name
    ended by `end`, then the members, each padded so that it is aligned to 2
    bytes."""
    def member(name: str, body: bytes) -> bytes:
        header = name.encode().ljust(16) + b"0".ljust(12) + b"0".ljust(6) + b"0".ljust(6) + b"644".ljust(8)
        return header + str(len(body)).encode().ljust(10) + b"`\n" + body + b"\n" * (len(body) % 2)

    longs, written = b"", []
    for name, _ in members:
        if len(name) > 15:
            written.append(f"/{len(longs)}")
            longs += name.encode() + end
        else:
            written.append(name + "/")
    out = b"!<arch>\n" + member("/", b"\0\0\0\0\0") * linkers
    if longs:
        out += member("//", longs)
    return out + b"".join(member(w, body) for w, (_, body) in zip(written, members))


def made_dbi(modules: list, signature: int = -1, extra: int = 0) -> bytes:
    """A DBI stream: its 64-byte header, then a module-info entry for each
    (module name, object file name), each entry aligned to 4 bytes. `extra`
    adds to the length of the module information that the header gives."""
    entries = b""
    for module, obj in modules:
        entry = struct.pack("<I", 0) + b"\0" * 28 + struct.pack("<HHIIIHHIII", 0, 0xFFFF, 0, 0, 0, 0, 0, 0, 0, 0)
        entry += module.encode() + b"\0" + obj.encode() + b"\0"
        entries += entry + b"\0" * (-len(entry) % 4)
    header = struct.pack("<iIIHHHHHHiiiiiIiiHHI", signature, 19990903, 1, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0,
                         len(entries) + extra, 0, 0, 0, 0, 0, 0, 0, 0, 0x8664, 0)
    return header + entries


def made_pdb(dbi: Optional[bytes], block_size: int = 512) -> bytes:
    """An MSF 7.00 file, built byte by byte: the superblock, the block map, the
    stream directory, and four streams -- an empty one, a short one, a nil one
    and the DBI stream, or three streams when `dbi` is None. The DBI stream's
    blocks are in the opposite sequence, so that only a reader that follows
    the block lists reads it."""
    streams = [b"", b"PDB info", None] + ([dbi] if dbi is not None else [])
    blocks, free = {}, 3                  # block 0 is the superblock, 1 and 2 the free-block maps

    def place(content: bytes, reverse: bool = False) -> list:
        nonlocal free
        count = -(-len(content) // block_size)
        indices = list(range(free, free + count))
        free += count
        for part, index in enumerate(reversed(indices) if reverse else indices):
            blocks[index] = content[part * block_size:(part + 1) * block_size]
        return list(reversed(indices)) if reverse else indices

    lists = [place(s or b"", reverse=number == 3) for number, s in enumerate(streams)]
    directory = struct.pack("<I", len(streams))
    directory += b"".join(struct.pack("<I", 0xFFFFFFFF if s is None else len(s)) for s in streams)
    directory += b"".join(struct.pack(f"<{len(each)}I", *each) for each in lists)
    directory_blocks = place(directory)
    block_map = place(struct.pack(f"<{len(directory_blocks)}I", *directory_blocks))[0]
    out = bytearray(free * block_size)
    out[0:56] = MSF_MAGIC + struct.pack("<6I", block_size, 1, free, len(directory), 0, block_map)
    for index, content in blocks.items():
        out[index * block_size:index * block_size + len(content)] = content
    return bytes(out)


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

# The modules of a program that links SAMPLE_MAP's objects, as lld-link
# writes them into the .pdb file (measured 2026-10-20, see module_paths).
SAMPLE_MODULES = [
    ("C:\\t\\rec.obj", "C:\\t\\rec.obj"),
    ("p.obj", "C:\\l\\libp.lib"),
    ("KERNEL32.dll", "C:\\k\\kernel32.lib"),
    ("Import:KERNEL32.dll", "C:\\k\\kernel32.lib"),
    ("* Linker *", ""),
]
# SAMPLE_MAP with the other entries that lld-link writes: the linker's own
# symbols, a `<common>` symbol, and a function from an import library.
MODULE_MAP = SAMPLE_MAP.replace("     <absolute>\n", """     <absolute>
 0000:00000000       __ImageBase                0000000140000000     <linker-defined>
 0003:00000010       common_thing               0000000140095010     <common>
 0001:00011f00       CloseHandle                0000000140012f00     kernel32:KERNEL32.dll
""")


def object_cases(expect) -> None:
    """The reader of a full COFF object, on made-up objects."""
    text, data, bss = 0x60501020, 0xC0000040, 0xC0000080
    sections = [
        (".text$mn", text, b"\x55\xe8\0\0\0\0\xc3", [(2, 6, 4)], False),
        (".data", data, b"\1\2\3\4\5\6\7\x08\x09\x0a", [(0, 5, 1), (2, 7, 1)], True),
        (".bss", bss, 16, [], False),
        (".text$a_long_section_name", 0x60000020, b"\xc3", [], False),
        (".rdata$another_long_name", 0x40000040, b"xyz", [], False),
        (".pdata", 0x40001040, b"\0" * 12, [(0, 3, 3)], False),
        (".xdata", 0x41000040, b"\0" * 8, [(4, 5, 3)], False),
    ]
    symbols = [
        (".file", 0, -2, 0, 103, [b"sample.cpp", b""]),
        (".text$mn", 0, 1, 0, 3, [made_definition(0, 2)]),
        ("?a_function_with_a_long_name@@YAHH@Z", 0, 1, 0x20, 2, []),
        ("imported", 0, 0, 0x20, 2, []),
        ("absolute", 7, -1, 0, 3, []),
        (".pdata", 0, 6, 0, 3, [made_definition(1, 5)]),
        ("label", 2, 1, 0, 3, []),
        ("high", 0, 0xFEFF, 0, 2, []),
        ("int_f", 0, 1, 0x24, 2, []),        # a function that gives an int: also a function
        ("array", 0, 2, 0x34, 3, []),        # an array of int: not a function
    ]
    made = made_object(sections, symbols, base64=(".rdata$another_long_name",))
    read = read_object(made)
    expect("an object's sections, numbered from 1", read and [(s.number, s.name) for s in read.sections],
           [(1, ".text$mn"), (2, ".data"), (3, ".bss"), (4, ".text$a_long_section_name"),
            (5, ".rdata$another_long_name"), (6, ".pdata"), (7, ".xdata")])
    expect("an object's section contents", read and [s.data for s in read.sections],
           [b"\x55\xe8\0\0\0\0\xc3", b"\1\2\3\4\5\6\7\x08\x09\x0a", None, b"\xc3", b"xyz", b"\0" * 12, b"\0" * 8])
    expect("an object's section sizes and flags", read and [(s.size, s.characteristics) for s in read.sections],
           [(7, text), (10, data | 0x01000000), (16, bss), (1, 0x60000020), (3, 0x40000040), (12, 0x40001040),
            (8, 0x41000040)])
    expect("an object's relocations", read and [s.relocations for s in read.sections],
           [[(2, 6, 4)], [(0, 5, 1), (2, 7, 1)], [], [], [], [(0, 3, 3)], [(4, 5, 3)]])
    expect("a relocation's fields, the count's record left out",
           read and [(r.offset, r.symbol, r.type) for r in read.sections[1].relocations], [(0, 5, 1), (2, 7, 1)])
    expect("the symbols, the auxiliary records skipped",
           read and [(s.index, s.name, s.value, s.section) for s in read.symbols],
           [(0, ".file", 0, -2), (3, ".text$mn", 0, 1), (5, "?a_function_with_a_long_name@@YAHH@Z", 0, 1),
            (6, "imported", 0, 0), (7, "absolute", 7, -1), (8, ".pdata", 0, 6), (10, "label", 2, 1),
            (11, "high", 0, 0xFEFF), (12, "int_f", 0, 1), (13, "array", 0, 2)])
    expect("the symbols' kinds", read and [(s.storage, s.function) for s in read.symbols],
           [(103, False), (3, False), (2, True), (2, True), (3, False), (3, False), (3, False), (2, False),
            (2, True), (3, False)])
    expect("the section definitions", read and [s.definition for s in read.symbols],
           [None, SectionDefinition(2, 0), None, None, None, SectionDefinition(5, 1), None, None, None, None])
    expect("an ordinary object is not big", read and (read.machine, read.big), (0x8664, False))

    big_sections = [(".text", text, b"\xc3", [(0, 2, 4)], False), (".pdata", 0x40001040, b"\0" * 12, [], False)]
    big_symbols = [
        (".text", 0, 1, 0, 3, [made_definition(0, 2, big=True)]),
        ("f", 0, 1, 0x20, 2, []),
        (".pdata", 0, 2, 0, 3, [made_definition(0x10001, 5, big=True)]),
        ("absolute", 0, -1, 0, 3, []),
        ("past_65279", 0, 70000, 0, 2, []),
    ]
    read = read_object(made_object(big_sections, big_symbols, big=True))
    expect("a big object", read and (read.big, [s.name for s in read.sections], read.sections[0].relocations),
           (True, [".text", ".pdata"], [(0, 2, 4)]))
    expect("a big object's symbols, 20 bytes each",
           read and [(s.index, s.name, s.section, s.function) for s in read.symbols],
           [(0, ".text", 1, False), (2, "f", 1, True), (3, ".pdata", 2, False), (5, "absolute", -1, False),
            (6, "past_65279", 70000, False)])
    expect("a big object's associated section, its high bits at offset 16",
           read and [s.definition for s in read.symbols if s.definition], [(2, 0), (5, 0x10001)])

    expect("text, refused", read_object(b"not an object file at all"), None)
    expect("an empty file, refused", read_object(b""), None)
    expect("an import member, refused", read_object(made_import("GetTickCount", "KERNEL32.dll")), None)
    expect("a program, refused", read_object(made_pe([".text"])), None)
    expect("an object cut short, refused", read_object(made[:-10]), None)
    moved = bytearray(made)
    struct.pack_into("<I", moved, 20 + 24, len(made) - 4)      # the first section's PointerToRelocations
    expect("relocations past the end, refused", read_object(bytes(moved)), None)
    moved = bytearray(made)
    moved[20 + 40 * 3:20 + 40 * 3 + 8] = b"/9999\0\0\0"        # the fourth section's name
    expect("a long name past the string table, refused", read_object(bytes(moved)), None)
    strings_at = struct.unpack_from("<I", made, 8)[0] + COFF_SYMBOL * struct.unpack_from("<I", made, 12)[0]
    unended = bytearray(made + b"zz")
    struct.pack_into("<I", unended, strings_at, len(made) + 2 - strings_at)
    expect("a string table that does not end with NUL, refused", read_object(bytes(unended)), None)
    expect("the same table ended with NUL, read", read_object(bytes(unended[:-1]) + b"\0") is not None, True)
    big = made_object(big_sections, big_symbols, big=True)
    expect("a big object of another class, refused", read_object(big[:12] + bytes(16) + big[28:]), None)
    expect("a big object of version 1, refused", read_object(big[:4] + b"\1\0" + big[6:]), None)


def archive_cases(expect) -> None:
    """The reader of an archive, a .lib file, on made-up archives."""
    members = [
        ("short.obj", made_coff([".text"]) + b"\x01"),
        ("a_member_with_a_long_name.obj", b"\1\2\3"),
        ("D:\\a\\_work\\objd\\amd64\\exe_main.obj", b"a member of Microsoft's own"),
        ("KERNEL32.dll", made_import("GetTickCount", "KERNEL32.dll")),
    ]
    coff = made_archive(members)
    expect("an archive in the COFF form, names ended by NUL", archive_members(coff), members)
    expect("an archive in the GNU form, names ended by '/\\n'", archive_members(made_archive(members, 1, b"/\n")),
           members)
    expect("an archive without a linker member", archive_members(made_archive(members[:1], 0)), members[:1])
    expect("an empty archive", archive_members(b"!<arch>\n"), [])
    expect("an import member", [is_import_member(body) for _, body in members], [False, False, False, True])
    expect("a big object is not an import member", is_import_member(made_coff([".text"], big=True)), False)

    expect("text, refused", archive_members(b"not an archive"), None)
    expect("a thin archive, refused", archive_members(b"!<thin>\n"), None)
    expect("a header without its end, refused", archive_members(coff.replace(b"`\n", b"x\n", 3)), None)
    expect("a size that is not a number, refused", archive_members(coff[:56] + b"x" + coff[57:]), None)
    expect("a member past the end, refused", archive_members(coff[:-3]), None)
    expect("the GNU form with names ended by NUL, refused", archive_members(made_archive(members, 1)), None)
    expect("the COFF form with no NUL, refused",
           archive_members(made_archive([("a_member_with_a_long_name.obj", b"")], 2, b"")), None)
    expect("a long name without the long-name member, refused",
           archive_members(b"!<arch>\n" + b"/0".ljust(16) + b" " * 32 + b"0".ljust(10) + b"`\n"), None)
    expect("a long name past the long-name member, refused",
           archive_members(coff.replace(b"/0      ", b"/99     ", 1)), None)
    expect("a name that starts with '/' and is none of the known ones, refused",
           archive_members(coff.replace(b"short.obj/      ", b"/short.obj      ", 1)), None)


def pdb_cases(expect) -> None:
    """The reader of a .pdb file's module list, on made-up MSF files."""
    two = [("C:\\build\\" + "deep\\" * 60 + "direct.cpp.obj",) * 2,
           ("member.obj", "C:\\libraries\\" + "x" * 101 + "\\some.lib")]
    expect("a .pdb file's two modules, blocks out of order", pdb_modules(made_pdb(made_dbi(two))), two)
    expect("the same in blocks of 4096 bytes", pdb_modules(made_pdb(made_dbi(two), 4096)), two)
    expect("a .pdb file of no module", pdb_modules(made_pdb(made_dbi([]))), [])
    good = made_pdb(made_dbi(two))
    expect("text, refused", pdb_modules(b"not a program database"), None)
    expect("an older program database, refused",
           pdb_modules(b"Microsoft C/C++ program database 2.00\r\n\x1aJG\0\0" + good[44:]), None)
    expect("a block size that is not valid, refused", pdb_modules(good[:32] + struct.pack("<I", 500) + good[36:]),
           None)
    expect("a file cut short, refused", pdb_modules(good[:-512]), None)
    expect("no DBI stream, refused", pdb_modules(made_pdb(None)), None)
    expect("a DBI stream of another signature, refused", pdb_modules(made_pdb(made_dbi(two, signature=0))), None)
    expect("module information past the stream, refused", pdb_modules(made_pdb(made_dbi(two, extra=8))), None)


def module_cases(expect) -> None:
    """The paths of a program's modules, and two objects with one file name."""
    linker_map = read_map(MODULE_MAP)
    found, problems = module_paths(linker_map, SAMPLE_MODULES)
    expect("each object's path, the pseudo entries left out", found,
           {("libp", "p.obj"): "C:\\l\\libp.lib", ("", "rec.obj"): "C:\\t\\rec.obj",
            ("kernel32", "KERNEL32.dll"): "C:\\k\\kernel32.lib"})
    expect("no problem", problems, [])
    cases = {
        "a library with two paths": (SAMPLE_MODULES + [("q.obj", "C:\\m\\libp.lib")], "libp"),
        "a library with no path": ([m for m in SAMPLE_MODULES if m[0] != "p.obj"], "libp"),
        "an object linked directly with no path": (SAMPLE_MODULES[1:], "rec.obj"),
        "an object linked directly with two paths": (SAMPLE_MODULES + [("C:\\u\\rec.obj",) * 2], "rec.obj"),
        # Decision 710 (3): the .pdb file of a different program.
        "a member that its library does not hold in the .pdb file":
            ([("other.obj", "C:\\l\\libp.lib")] + [m for m in SAMPLE_MODULES if m[0] != "p.obj"], "p.obj"),
    }
    for name, (modules, named) in cases.items():
        found, problems = module_paths(linker_map, modules)
        expect(name, ([named in p for p in problems], any(named in key[0] or named == key[1] for key in found)),
               ([True], False))

    expect("the objects linked directly", direct_objects(SAMPLE_MODULES), ["C:\\t\\rec.obj"])
    expect("a library with no two objects of one name", same_file_names("x.lib", ["Orbit.cpp.obj", "Sun.cpp.obj"]),
           [])
    problems = same_file_names("libx.lib", ["debug.c.obj", "a.obj", "debug.c.obj"])
    expect("a library with two objects of one name", [("libx.lib" in p, "debug.c.obj" in p) for p in problems],
           [(True, True)])
    problems = same_file_names("p.exe", ["C:\\a\\x.obj", "C:\\b\\x.obj", "C:/c/X.OBJ"])
    expect("a program with three objects of one name, each pair", len(problems), 3)


# A made-up map for the functions at the recorded addresses (decisions 582 and
# 695): .text at 0x1000 and .rdata at 0x2000, less the base. Each address of
# .text holds one case. No symbol is at 0x1090.
RECORD_MAP = """\
 r

 Preferred load address is 0000000140000000

 Start         Length     Name                   Class
 0001:00000000 00001000H .text                   CODE
 0002:00000000 00000200H .rdata                  DATA

  Address         Publics by Value              Rva+Base               Lib:Object

 0001:00000000       ?one@@YAHH@Z               0000000140001000     liba:a.obj
 0001:00000010       ??_EShape@@UEAAPEAXI@Z     0000000140001010     liba:a.obj
 0001:00000010       ??_GShape@@UEAAPEAXI@Z     0000000140001010     liba:a.obj
 0001:00000020       ??_GBox@@UEAAPEAXI@Z       0000000140001020     direct.obj
 0001:00000020       ??_EBox@@UEAAPEAXI@Z       0000000140001020     direct.obj
 0001:00000030       ??_ESquare@@UEAAPEAXI@Z    0000000140001030     liba:a.obj
 0001:00000030       ??_GCircle@@UEAAPEAXI@Z    0000000140001030     liba:a.obj
 0001:00000040       ?a@@YAXXZ                  0000000140001040     liba:a.obj
 0001:00000040       ?b@@YAXXZ                  0000000140001040     liba:a.obj
 0001:00000040       ?c@@YAXXZ                  0000000140001040     liba:a.obj
 0001:000000a0       ?first@@YAXXZ              00000001400010a0     direct.obj
 0001:000000a0       ?second@@YAXXZ             00000001400010a0     direct.obj
 0002:00000100       ?_Data@@3HB                0000000140002100     liba:a.obj

 Static symbols

 0001:00000050       ?hidden@?A0x1@@YAHH@Z      0000000140001050     liba:a.obj
 0001:00000060       ?local@@YAHXZ              0000000140001060     direct.obj
 0001:00000070       ??_EImpl@?A0x1@@UEAAPEAXI@Z 0000000140001070     liba:a.obj
 0001:00000070       ??_GImpl@?A0x1@@UEAAPEAXI@Z 0000000140001070     liba:a.obj
 0001:00000080       ?orphan@@YAXXZ             0000000140001080     other.obj
"""
RECORD_TREE = "C:\\b\\tree"
# The modules of RECORD_MAP's program: other.obj has none.
RECORD_MODULES = [("a.obj", "C:\\b\\tree\\liba.lib"), ("C:\\b\\tree\\CMakeFiles\\t.dir\\direct.obj",) * 2]


def record_cases(expect) -> None:
    """Each recorded address turned into a function (decisions 582 and 695),
    the key of a map symbol (decisions 709 (3) and 711 (4)), and the number of
    entries in a program's hook table (decision 704)."""
    linker_map = read_map(RECORD_MAP)
    index = address_index(linker_map)
    expect("the index of the map by address: code only",
           sorted(index), [0x1000, 0x1010, 0x1020, 0x1030, 0x1040, 0x1050, 0x1060, 0x1070, 0x1080, 0x10A0])
    paths, _ = module_paths(linker_map, RECORD_MODULES)

    def key_of(symbol: Symbol) -> Optional[str]:
        return symbol_key(symbol, paths, RECORD_TREE)

    found = [
        ("one name", 0x1000, ["?one@@YAHH@Z"]),
        ("the ??_E and ??_G names of one destructor", 0x1010, ["??_GShape@@UEAAPEAXI@Z"]),
        ("the same pair in the other sequence", 0x1020, ["??_GBox@@UEAAPEAXI@Z"]),
        ("one static name in a library", 0x1050, ["liba.lib(a.obj)|?hidden@?A0x1@@YAHH@Z"]),
        ("one static name in an object linked directly", 0x1060, ["CMakeFiles/t.dir/direct.obj|?local@@YAHXZ"]),
        ("a static pair", 0x1070, ["liba.lib(a.obj)|??_GImpl@?A0x1@@UEAAPEAXI@Z"]),
    ]
    for what, address, keys in found:
        expect(f"a recorded address: {what}", record_functions(index, [address], key_of), (keys, []))
    refused = [
        ("??_E and ??_G with a different rest", 0x1030, ["??_ESquare@@UEAAPEAXI@Z", "??_GCircle@@UEAAPEAXI@Z"]),
        ("three names", 0x1040, ["?a@@YAXXZ", "?b@@YAXXZ", "?c@@YAXXZ"]),
        ("two names that are not the pair", 0x10A0, ["?first@@YAXXZ", "?second@@YAXXZ"]),
        ("no name", 0x1090, []),
        ("a data symbol and no code", 0x2100, []),
        ("a static name of an object without a path", 0x1080, ["?orphan@@YAXXZ"]),
    ]
    for what, address, names in refused:
        keys, problems = record_functions(index, [address], key_of)
        found_names = [(f"{address:#x}" in p, all(name in p for name in names)) for p in problems]
        expect(f"a recorded address, a problem: {what}", (keys, found_names), ([], [(True, True)]))
    keys, problems = record_functions(index, [0x1060, 0x1000, 0x1090, 0x1010, 0x1000], key_of)
    expect("the addresses of one record: each key one time, sorted, and each problem", (keys, len(problems)),
           (["??_GShape@@UEAAPEAXI@Z", "?one@@YAHH@Z", "CMakeFiles/t.dir/direct.obj|?local@@YAHXZ"], 1))

    public = Symbol(0x1000, "?one@@YAHH@Z", "liba", "a.obj", True, False)
    expect("the key of a public symbol: its decorated name", symbol_key(public, paths, RECORD_TREE), "?one@@YAHH@Z")
    expect("the key of a public symbol of an object without a path",
           symbol_key(public._replace(library="", object="other.obj"), paths, RECORD_TREE), "?one@@YAHH@Z")
    expect("the key of a static symbol: its object's key and its name",
           symbol_key(public._replace(static=True), paths, RECORD_TREE), "liba.lib(a.obj)|?one@@YAHH@Z")
    summary = made_sample({"f": (".text", "code", b"\xc3", (), None), "s": (".text", "code", b"\x90\xc3", (), None)},
                          {"f": (0, "f", "function"), "s": (0, "s", "static function")})
    program = Program({object_key(RECORD_TREE, paths[("liba", "a.obj")], "a.obj"): summary})
    mapped = [Symbol(0x1000, "f", "liba", "a.obj", True, False), Symbol(0x1010, "s", "liba", "a.obj", True, True)]
    expect("the keys of the map's symbols are the keys that Program gives",
           ([symbol_key(s, paths, RECORD_TREE) for s in mapped], sorted(program.digests())),
           (["f", "liba.lib(a.obj)|s"], ["f", "liba.lib(a.obj)|s"]))

    # The table of a program: the two markers of 8 bytes and 3 entries of 16
    # bytes, in a section that the file pads to 512 bytes.
    table = made_pe([".text", ".SCOV", ".SCOVP"], {".SCOVP": (8 + 3 * 16 + 8, 512)})
    expect("the entries of a program's hook table, from VirtualSize", table_entries(table), 3)
    expect("a table with the two markers only", table_entries(made_pe([".SCOVP"], {".SCOVP": (16, 512)})), 0)
    expect("a program without the table", table_entries(made_pe([".text", ".SCOV"])), None)
    expect("a table that does not hold complete entries, refused",
           table_entries(made_pe([".SCOVP"], {".SCOVP": (8 + 16 + 8 + 8, 512)})), None)
    expect("a table shorter than its markers, refused", table_entries(made_pe([".SCOVP"], {".SCOVP": (8, 512)})), None)
    expect("two tables, refused", table_entries(made_pe([".SCOVP", ".SCOVP"], {".SCOVP": (32, 512)})), None)
    expect("an object, refused", table_entries(made_coff([".SCOVP$M"])), None)
    expect("text, refused", table_entries(b"not a program"), None)
    table_map_cases(expect, table)


def marker_map(start: int, stop: int, extra: str = "") -> str:
    """A map with the two markers of the hook's table at `start` and `stop`,
    addresses less the preferred one."""
    return (" Preferred load address is 0000000140000000\n\n"
            " 0006:00000000 00000100H .SCOVP                  DATA\n\n"
            f" 0006:00000000       {TABLE_START:26} {0x140000000 + start:016x}     CoverageRecorder.cpp.obj\n"
            f" 0006:00000000       {TABLE_STOP:26} {0x140000000 + stop:016x}     CoverageRecorder.cpp.obj\n"
            + extra)


def table_map_cases(expect, table: bytes) -> None:
    """The entries of the hook's table from the map's markers, and the two
    counts that must agree (decision 712)."""
    three = read_map(marker_map(0x1000, 0x1000 + 8 + 3 * 16))
    expect("the entries of the hook's table, from the map's markers", map_table_entries(three), 3)
    expect("no entry between the markers", map_table_entries(read_map(marker_map(0x1000, 0x1008))), 0)
    expect("a stop marker before the end of the start marker, refused",
           map_table_entries(read_map(marker_map(0x1000, 0x1004))), None)
    # 16 bytes less than no entry: an integer number of entries, below 0.
    expect("a stop marker before the start marker, refused", map_table_entries(read_map(marker_map(0x1008, 0x1000))),
           None)
    expect("a part of an entry between the markers, refused",
           map_table_entries(read_map(marker_map(0x1000, 0x1000 + 8 + 20))), None)
    expect("a map without the stop marker, refused",
           map_table_entries(read_map(marker_map(0x1000, 0x1038).replace(TABLE_STOP, "other"))), None)
    twice = marker_map(0x1000, 0x1038, f" 0006:00000040       {TABLE_STOP:26} {0x140001048:016x}     x.obj\n")
    expect("a map with the stop marker two times, refused", map_table_entries(read_map(twice)), None)
    expect("the two counts agree: the number, and no problem", hook_table_entries(table, three), (3, None))
    four = read_map(marker_map(0x1000, 0x1000 + 8 + 4 * 16))
    count, problem = hook_table_entries(table, four)
    expect("the two counts differ: no number, and a problem that gives the two",
           (count, problem is not None and "3" in problem and "4" in problem), (None, True))
    count, problem = hook_table_entries(made_pe([".text"]), three)
    expect("no section `.SCOVP`: no number, and a problem", (count, problem is not None), (None, True))


# The samples that compiled_cases compiles. The archived one has an external
# function, a function in a COMDAT with its unwind data associated with it, and
# static data; the object linked directly calls into the archive and is the
# DLL's export. The third has 70,000 relocations in one section, more than the
# 16-bit count can hold. `twice` is not inlined: if it is, at -O2, clang makes
# `shared` a function
# that calls nothing, and such a function has no unwind data (measured
# 2026-10-20 with llvm-readobj).
ARCHIVED_SAMPLE = """\
int twice(int x);
__declspec(noinline) int twice(int x) { return 2 * x; }
__declspec(noinline) inline int shared(int x) { return twice(x) + 1; }
static const int table[] = {3, 5, 7, 11};
int lookUp(int x) { return shared(x) + table[x & 3]; }
"""
DIRECT_SAMPLE = """\
int twice(int x);
int lookUp(int x);
extern "C" __declspec(dllexport) int sampleEntry(int x) { return twice(x) + lookUp(x) + 1; }
"""
MANY_RELOCATIONS = 70000
# IMAGE_REL_AMD64_REL32 and IMAGE_REL_AMD64_ADDR64 (Microsoft's PE format specification).
REL32, ADDR64 = 4, 1


def run_tool(command: list, folder: Path) -> Optional[str]:
    """Run one compile, archive or link; the text of its error, or None. A
    tool that is not there, or that does not stop in time, is also an error,
    so that the case reports it by name."""
    try:
        done = subprocess.run([str(c) for c in command], cwd=folder, capture_output=True, text=True, timeout=120,
                              check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        return f"{' '.join(map(str, command))}: {error}"
    if done.returncode == 0:
        return None
    return f"{' '.join(map(str, command))}: exit {done.returncode}\n{done.stdout}{done.stderr}"


def compiled_cases(compiler: Path, archiver: Path, linker: Path, expect) -> None:
    """The readers on samples compiled with `compiler` at -O0 and at -O2
    (register decisions 591 and 709): an archive made with llvm-ar, and a DLL
    linked with lld-link from an object and the archive, with its map and .pdb
    file. /nodefaultlib and /noentry keep all Visual Studio libraries out.
    The archiver and the linker are the files that the command line names
    (decision 710)."""
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary).resolve()
        for name, text in (("archived_sample.cpp", ARCHIVED_SAMPLE), ("direct.cpp", DIRECT_SAMPLE),
                           ("many.cpp", "extern int target;\nint* many[] = {" + ",".join(["&target"] * MANY_RELOCATIONS)
                            + "};\n")):
            (root / name).write_bytes(text.encode())
        for level in ("-O0", "-O2"):
            folder = root / level[1:]
            folder.mkdir()
            archived, direct, many = folder / "archived_sample.cpp.obj", folder / "direct.obj", folder / "many.obj"
            library, program = folder / "samples.lib", folder / "sample.dll"
            failed = (run_tool([compiler, "-c", level, root / "archived_sample.cpp", "-o", archived], folder)
                      or run_tool([compiler, "-c", level, root / "direct.cpp", "-o", direct], folder)
                      or run_tool([compiler, "-c", level, root / "many.cpp", "-o", many], folder)
                      or run_tool([archiver, "rcs", library, archived], folder)
                      or run_tool([linker, "/dll", "/noentry", "/nodefaultlib", "/debug", "/map",
                                   f"/out:{program}", direct, library], folder))
            expect(f"the samples compiled, archived and linked at {level}", failed, None)
            if failed is None:
                compiled_level(level, folder, expect)


def compiled_level(level: str, folder: Path, expect) -> None:
    """The checks on one level's archive, objects, map and .pdb file."""
    archived, direct = folder / "archived_sample.cpp.obj", folder / "direct.obj"
    library, program = folder / "samples.lib", folder / "sample.dll"
    members = archive_members(library.read_bytes())
    expect(f"{level}: the archive's member, by its long name", members,
           [("archived_sample.cpp.obj", archived.read_bytes())])

    def named(found: Optional[CoffObject], name: str) -> Optional[CoffSymbol]:
        return next((s for s in found.symbols if s.name == name), None) if found else None

    def calls(found: Optional[CoffObject], caller: str) -> list:
        """The names a function's section points at with REL32 relocations."""
        if found is None or named(found, caller) is None:
            return []
        by_index = {s.index: s.name for s in found.symbols}
        section = found.sections[named(found, caller).section - 1]
        return sorted({by_index.get(r.symbol, "?") for r in section.relocations if r.type == REL32})

    found = read_object(archived.read_bytes())
    twice, shared = named(found, "?twice@@YAHH@Z"), named(found, "?shared@@YAHH@Z")
    expect(f"{level}: an external function", twice and (twice.function, twice.storage, twice.section > 0),
           (True, 2, True))
    expect(f"{level}: a COMDAT function", shared and found.sections[shared.section - 1].characteristics & 0x1000,
           0x1000)
    expect(f"{level}: its section defined as a COMDAT that any copy satisfies",
           shared and [s.definition.selection for s in found.symbols
                       if s.definition and s.section == shared.section], [2])
    expect(f"{level}: its unwind data associated with it",
           shared and sorted({found.sections[s.section - 1].name for s in found.symbols
                              if s.definition and s.definition.selection == 5
                              and s.definition.number == shared.section}), [".pdata", ".xdata"])
    expect(f"{level}: the static data", [(s.storage, s.section > 0) for s in found.symbols if s.name == "table"]
           if found else None, [(3, True)])
    expect(f"{level}: a call and a read of the data, as relocations", calls(found, "?lookUp@@YAHH@Z"),
           ["?shared@@YAHH@Z", "table"])
    expect(f"{level}: a long section name, through the string table",
           found and ".llvm_addrsig" in [s.name for s in found.sections], True)
    found = read_object(direct.read_bytes())
    expect(f"{level}: the direct object's calls into the archive", calls(found, "sampleEntry"),
           ["?lookUp@@YAHH@Z", "?twice@@YAHH@Z"])
    found = read_object((folder / "many.obj").read_bytes())
    expect(f"{level}: {MANY_RELOCATIONS} relocations, the count in the first one",
           found and [(len(s.relocations), {r.type for r in s.relocations}) for s in found.sections
                      if len(s.relocations) > 0xFFFF], [(MANY_RELOCATIONS, {ADDR64})])

    linker_map = read_map(program.with_suffix(".map").read_text(encoding="utf-8", errors="replace"))
    expect(f"{level}: the map's archived function", linker_map and
           [(s.library, s.object) for s in linker_map.symbols if s.name == "?twice@@YAHH@Z"],
           [("samples", "archived_sample.cpp.obj")])
    expect(f"{level}: the map's direct function", linker_map and
           [(s.library, s.object) for s in linker_map.symbols if s.name == "sampleEntry"], [("", "direct.obj")])
    modules = pdb_modules(program.with_suffix(".pdb").read_bytes())
    expect(f"{level}: the .pdb file's modules", modules,
           [(str(direct), str(direct)), ("archived_sample.cpp.obj", str(library)), ("* Linker *", "")])
    expect(f"{level}: the path of each module",
           linker_map and modules is not None and module_paths(linker_map, modules),
           ({("samples", "archived_sample.cpp.obj"): str(library), ("", "direct.obj"): str(direct)}, []))


# --- self-test: the digests on made-up objects ----------------------------------

# The section flags of the made-up objects, as clang writes them with
# -ffunction-sections and -fdata-sections (measured 2026-10-20 with
# llvm-readobj on the compiled samples): code and read-only data, each a COMDAT
# aligned to 16 bytes; uninitialised data; debug information; the linker's
# directives (.drectve).
MADE_FLAGS = {"code": 0x60501020, "data": 0x40501040, "bss": 0xC0501080, "debug": 0x42100040, "info": 0x100A00}
# The types of a made-up symbol: (complex type, storage class, the section
# number of a symbol that no section holds).
MADE_SYMBOLS = {
    "function": (COFF_FUNCTION, COFF_EXTERNAL, None), "static function": (COFF_FUNCTION, COFF_STATIC, None),
    "external": (0, COFF_EXTERNAL, None), "static": (0, COFF_STATIC, None), "undefined": (0, COFF_EXTERNAL, 0),
    "weak": (0, COFF_WEAK_EXTERNAL, 0), "absolute": (0, COFF_STATIC, COFF_ABSOLUTE),
}
# IMAGE_REL_AMD64_ADDR32NB and IMAGE_REL_AMD64_SECREL (Microsoft's PE format specification).
ADDR32NB, SECREL = 3, 11


def made_sample(sections: dict, symbols: dict, order: Optional[list] = None,
                symbol_order: Optional[list] = None) -> dict:
    """The summary of a made-up object of made_sample_object, read back with read_object."""
    return object_summary(read_object(made_sample_object(sections, symbols, order, symbol_order)))


def made_sample_object(sections: dict, symbols: dict, order: Optional[list] = None,
                       symbol_order: Optional[list] = None) -> bytes:
    """A made-up object, built byte by byte with made_object. `sections` maps
    a label to a tuple: the name, a type of MADE_FLAGS, the contents or a
    length, the relocations as (offset,
    symbol, type), and the label of the section that it goes with or None.
    Each section gets its definition symbol, which a relocation names
    "#<label>". A number names a symbol index directly. `symbols` maps a name
    to (value, the label of its section or None, a type of MADE_SYMBOLS). For
    a weak external, the second item names its default symbol, or is None.
    The two sequences set the file's layout."""
    order = order or list(sections)
    symbol_order = symbol_order or list(symbols)
    position = {label: number for number, label in enumerate(order, 1)}
    index = {f"#{label}": 2 * number for number, label in enumerate(order)}
    records = 2 * len(order)
    for name in symbol_order:
        index[name] = records
        records += 2 if symbols[name][2] == "weak" else 1
    made_sections, made_symbols = [], []
    for label in order:
        name, kind, contents, relocations, partner = sections[label]
        made_sections.append((name, MADE_FLAGS[kind], contents,
                              [(o, index[s] if isinstance(s, str) else s, t) for o, s, t in relocations], False))
        definition = made_definition(position[partner], COMDAT_ASSOCIATIVE) if partner else made_definition(0, 2)
        made_symbols.append((name, 0, position[label], 0, COFF_STATIC, [definition]))
    for name in symbol_order:
        value, label, kind = symbols[name]
        complex_type, storage, number = MADE_SYMBOLS[kind]
        if kind == "weak":
            # The auxiliary record: TagIndex, then the search type 3, an alias.
            aux = [struct.pack("<II", index[label], 3) if label else b""]
            made_symbols.append((name, value, number, complex_type, storage, aux))
            continue
        made_symbols.append((name, value, position[label] if label else number, complex_type, storage, []))
    return made_object(made_sections, made_symbols)


# The made-up object of the digest cases. f calls g, reads a table that points
# at an inner table, and calls an undefined external; f has unwind data and
# debug information that go with it. g points at a label in its own section. h
# reads a table of functions that points at type data, which points back at the
# table: a cycle. k points at g's section, at uninitialised data, at an
# absolute symbol and at a weak external. s is a static function.
DIGEST_SECTIONS = {
    "f": (".text", "code", b"\xe8\0\0\0\0\x48\x8d\x05\0\0\0\0\xe8\0\0\0\0\xc3",
          ((1, "g", REL32), (8, "table", REL32), (13, "external", REL32)), None),
    "g": (".text", "code", b"\xeb\0\xc3\x90", ((1, "label", REL32),), None),
    "table": (".rdata", "data", b"\1\2\3\4" + bytes(8), ((4, "inner", ADDR64),), None),
    "inner": (".rdata", "data", b"\x09\x09\x09\x09", (), None),
    "unwind": (".xdata", "data", b"\1\2\3\4", (), "f"),
    "lines": (".debug$S", "debug", b"line 1", ((0, "f", SECREL),), "f"),
    "significance": (".llvm_addrsig", "debug", b"\1\2", (), None),
    "vtable": (".rdata", "data", bytes(16), ((0, "rtti", ADDR64), (8, "g", ADDR64)), None),
    "rtti": (".rdata", "data", bytes(8) + b"\7", ((0, "vtable", ADDR64),), None),
    "h": (".text", "code", b"\x48\x8d\x05\0\0\0\0\xc3", ((3, "vtable", REL32),), None),
    "s": (".text", "code", b"\xc3", (), None),
    "zeros": (".bss", "bss", 16, (), None),
    "k": (".text", "code", bytes(20),
          ((1, "#g", REL32), (6, "zeros", REL32), (11, "constant", ADDR32NB), (15, "alias", REL32)), None),
}
DIGEST_SYMBOLS = {
    "f": (0, "f", "function"), "g": (0, "g", "function"), "label": (2, "g", "static"),
    "table": (0, "table", "static"), "inner": (0, "inner", "static"), "external": (0, None, "undefined"),
    "vtable": (0, "vtable", "external"), "rtti": (0, "rtti", "static"), "h": (0, "h", "function"),
    "s": (0, "s", "static function"), "zeros": (0, "zeros", "static"), "k": (0, "k", "function"),
    "constant": (7, None, "absolute"), "alias": (0, "g", "weak"),
}


def with_contents(label: str, value) -> dict:
    """DIGEST_SECTIONS' section `label` with other contents."""
    name, kind, _, relocations, partner = DIGEST_SECTIONS[label]
    return {label: (name, kind, value, relocations, partner)}


def with_relocations(label: str, *relocations) -> dict:
    name, kind, contents, _, partner = DIGEST_SECTIONS[label]
    return {label: (name, kind, contents, relocations, partner)}


# Each case changes DIGEST_SECTIONS and DIGEST_SYMBOLS at one location: (what,
# sections replaced, symbols replaced or removed with None, the layout, the
# functions with digests that change, the functions added).
DIGEST_CASES = [
    ("nothing changed", {}, {}, None, set(), set()),
    ("the file's layout reversed", {}, {}, "reversed", set(), set()),
    ("a label moved in its own section", {}, {"label": (3, "g", "static")}, None, {"g"}, set()),
    ("a called function's code changed", with_contents("g", b"\xeb\0\xc3\xcc"), {}, None, {"g"}, set()),
    ("data two levels away changed", with_contents("inner", b"\x09\x09\x09\x08"), {}, None, {"f"}, set()),
    ("data in a cycle changed", with_contents("rtti", bytes(8) + b"\6"), {}, None, {"h"}, set()),
    ("a pointer to another member of a cycle", with_relocations("h", (3, "rtti", REL32)), {}, None, {"h"}, set()),
    ("associated unwind data changed", with_contents("unwind", b"\1\2\3\5"), {}, None, {"f"}, set()),
    ("debug information changed", {**with_contents("lines", b"line 2"), **with_contents("significance", b"\2\1")}, {},
     None, set(), set()),
    ("an undefined external renamed",
     with_relocations("f", (1, "g", REL32), (8, "table", REL32), (13, "renamed", REL32)),
     {"external": None, "renamed": (0, None, "undefined")}, None, {"f"}, set()),
    ("uninitialised data grew", with_contents("zeros", 32), {}, None, {"k"}, set()),
    ("an absolute symbol's value changed", {}, {"constant": (8, None, "absolute")}, None, {"k"}, set()),
    ("a weak external renamed",
     with_relocations("k", (1, "#g", REL32), (6, "zeros", REL32), (11, "constant", ADDR32NB), (15, "other", REL32)),
     {"alias": None, "other": (0, "g", "weak")}, None, {"k"}, set()),
    ("a weak external's default changed", {}, {"alias": (0, "h", "weak")}, None, {"k"}, set()),
    ("data at another offset in its section",
     with_relocations("f", (1, "g", REL32), (8, "tail", REL32), (13, "external", REL32)),
     {"tail": (4, "table", "static")}, None, {"f"}, set()),
    ("a relocation's type changed",
     with_relocations("f", (1, "g", REL32), (8, "table", ADDR32NB), (13, "external", REL32)), {}, None, {"f"}, set()),
    ("a relocation moved",
     with_relocations("f", (1, "g", REL32), (9, "table", REL32), (13, "external", REL32)), {}, None, {"f"}, set()),
    ("a static function's code changed", with_contents("s", b"\x90\xc3"), {}, None, {"a|s"}, set()),
    ("a function and its flag inserted before the others",
     {"new": (".text", "code", b"\x90\xc3", (), None), "flag": (".SCOV$BM", "data", b"\0", (), "new")},
     {"new": (0, "new", "function")}, "inserted", set(), {"new"}),
]


def digest_variant(sections: dict, symbols: dict, layout: Optional[str]) -> dict:
    """The digests of DIGEST_SECTIONS and DIGEST_SYMBOLS with one change."""
    all_sections = {**DIGEST_SECTIONS, **sections}
    all_symbols = {name: value for name, value in {**DIGEST_SYMBOLS, **symbols}.items() if value is not None}
    order, symbol_order = list(all_sections), list(all_symbols)
    if layout == "reversed":
        order, symbol_order = order[::-1], symbol_order[::-1]
    elif layout == "inserted":
        order = list(sections) + list(DIGEST_SECTIONS)
        symbol_order = list(symbols) + list(DIGEST_SYMBOLS)
    return Program({"a": made_sample(all_sections, all_symbols, order, symbol_order)}).digests()


def compared(before: dict, after: dict) -> tuple:
    """(changed, added, removed): the keys of the functions that changed, that
    only `after` has, and that only `before` has."""
    changed = {key for key in before.keys() & after.keys() if before[key] != after[key]}
    return changed, set(after) - set(before), set(before) - set(after)


def digest_cases(expect) -> None:
    """The digests of decision 545, on made-up objects."""
    summary = made_sample(DIGEST_SECTIONS, DIGEST_SYMBOLS)
    program = Program({"a": summary})
    base = program.digests()
    expect("the made-up functions, a static one by its object", sorted(base), ["a|s", "f", "g", "h", "k"])
    expect("the made-up object, no problem", program.problems, [])
    expect("five different digests", len(set(base.values())), 5)
    for what, sections, symbols, layout, changed, added in DIGEST_CASES:
        expect(f"made up: {what}", compared(base, digest_variant(sections, symbols, layout)), (changed, added, set()))
    copied = json.loads(json.dumps(summary))
    expect("a summary after a JSON round trip", copied, summary)
    expect("the digests after a JSON round trip", Program({"a": copied}).digests(), base)
    expect("a summary holds no debug section's hash or relocations", summary["debug"],
           [[6, ".debug$S"], [7, ".llvm_addrsig"]])
    unknown = Program({"a": made_sample({**DIGEST_SECTIONS, **with_relocations("f", (1, 999, REL32))}, DIGEST_SYMBOLS)})
    expect("a relocation to a symbol that is not there, a problem",
           [("a:" in p, "999" in p) for p in (unknown.digests() and unknown.problems)], [(True, True)])
    chosen_cases(expect)


# Two made-up objects for `chosen`: each defines the external data `shared`,
# the external function `dup` and a static function `s`, with other contents;
# a's `user` reads `shared`.
CHOSEN_A = {
    "user": (".text", "code", b"\x48\x8d\x05\0\0\0\0\xc3", ((3, "shared", REL32),), None),
    "shared": (".rdata", "data", b"AAAA", (), None),
    "dup": (".text", "code", b"\x90\xc3", (), None),
    "s": (".text", "code", b"\xc3", (), None),
}
CHOSEN_B = {"shared": (".rdata", "data", b"BBBB", (), None), "dup": (".text", "code", b"\xcc\xc3", (), None),
            "s": (".text", "code", b"\xc3\xc3", (), None)}
CHOSEN_SYMBOLS = {"user": (0, "user", "function"), "shared": (0, "shared", "external"),
                  "dup": (0, "dup", "function"), "s": (0, "s", "static function")}


def chosen_cases(expect) -> None:
    """An external name that more than one object defines, with and without the object that the map selects."""
    a = made_sample(CHOSEN_A, CHOSEN_SYMBOLS)
    b = made_sample(CHOSEN_B, {k: v for k, v in CHOSEN_SYMBOLS.items() if k != "user"})
    alone_a, alone_b = Program({"a": a}).digests(), Program({"b": b}).digests()
    program = Program({"a": a, "b": b})
    found = program.digests()
    expect("copies without the map's choice: a problem for each name, by name",
           sorted((("dup" in p) + 2 * ("shared" in p)) for p in program.problems), [1, 2])
    expect("copies without the map's choice: the result does not include the function", "dup" in found, False)
    expect("two static functions of one name: two keys", sorted(k for k in found if k.endswith("|s")), ["a|s", "b|s"])
    program = Program({"a": a, "b": b}, {"dup": "b", "shared": "b"})
    expect("the map's choice b: no problem", (program.digests() and program.problems), [])
    expect("the map's choice b: b's function", program.digests().get("dup"), alone_b["dup"])
    from_b = program.digests()["user"]
    program = Program({"a": a, "b": b}, {"dup": "a", "shared": "a"})
    expect("the map's choice a: a's data and function", (program.digests()["user"], program.digests().get("dup")),
           (alone_a["user"], alone_a["dup"]))
    expect("the map's choice changes the reader of the data", from_b != alone_a["user"], True)
    same = made_sample({**CHOSEN_B, "shared": CHOSEN_A["shared"], "dup": CHOSEN_A["dup"]},
                       {k: v for k, v in CHOSEN_SYMBOLS.items() if k != "user"})
    program = Program({"a": a, "b": same})
    expect("equal copies without the map's choice: no problem", (program.digests().get("dup"), program.problems),
           (alone_a["dup"], []))
    without = made_sample({k: v for k, v in CHOSEN_A.items() if k != "dup"},
                          {k: v for k, v in CHOSEN_SYMBOLS.items() if k != "dup"})
    program = Program({"a": without, "b": b}, {"dup": "a", "shared": "a"})
    expect("the map's choice that does not define the name, a problem",
           [p.startswith("dup:") for p in (program.digests() and program.problems)], [True])


# --- self-test: the digests on compiled samples ---------------------------------

# The flags of the compiled digest cases: the hook and the sections of the
# trees that carry it (decisions 559 and 545), and the debug information of
# CodeView, which must not be part of a digest.
DIGEST_FLAGS = ("-fsanitize-coverage=func,inline-bool-flag,pc-table", "-ffunction-sections", "-fdata-sections",
                "-ffp-contract=off", "-g", "-gcodeview", "-std=c++23", "-c")
# The basic sample of the compiled digest cases (decisions 545, 591 and 709).
# Each case changes it at one location. [[clang::noinline]] keeps each function
# apart at -O2, where clang puts a short function into its callers if it can.
DIGEST_SAMPLE = """\
[[clang::noinline]] int callee(int x) { return x + 1; }
[[clang::noinline]] int caller(int x) { return callee(x) * 2; }
constexpr int kTable[] = {2, 3, 5, 7};
[[clang::noinline]] int readsTable(int i) { return kTable[i & 3]; }
[[clang::noinline]] const char* greeting() { return "hello"; }
[[clang::noinline]] double scaled(double x) { return x * 2.5; }
struct Shape {
    virtual int first() const;
    virtual int second() const;
};
int Shape::first() const { return 1; }
int Shape::second() const { return 2; }
struct Square : Shape {
    int first() const override;
};
int Square::first() const { return 4; }
const Square kSquare{};
[[clang::noinline]] const Shape& square() { return kSquare; }
constexpr int kInner[] = {10, 20, 30, 40};
constexpr const int* kOuter[] = {kInner, kInner + 1, kInner + 2, kInner + 3};
[[clang::noinline]] int readsOuter(int i) { return *kOuter[i & 3]; }
[[clang::noinline]] int one(int x) { return x * 7; }
[[clang::noinline]] int two(int x) { return x * 7; }
[[clang::noinline]] int pick(int x) { return one(x); }
[[clang::noinline]] int mayThrow(int x) {
    if (x > 2) {
        throw x;
    }
    return x;
}
[[clang::noinline]] int catcher(int x) {
    try {
        return mayThrow(x);
    } catch (int) {
        return -1;
    }
}
[[clang::noinline]] int f0(int x) { return x + 10; }
[[clang::noinline]] int f1(int x) { return x + 11; }
[[clang::noinline]] int f2(int x) { return x + 12; }
[[clang::noinline]] int f3(int x) { return x + 13; }
[[clang::noinline]] int f4(int x) { return x + 14; }
[[clang::noinline]] int jump(int x) {
    switch (x) {
    case 0: return f0(x);
    case 1: return f1(x);
    case 2: return f2(x);
    case 3: return f3(x);
    case 4: return f4(x);
    default: return 0;
    }
}
[[clang::noinline]] int guarded(int x) noexcept { return mayThrow(x) + 1; }
[[clang::noinline]] static int hidden(int x) { return x - 3; }
[[clang::noinline]] int usesHidden(int x) { return hidden(x) + 1; }
"""
# A second object: a static function of the same name as the one in the basic
# sample; a function that only throws, which the hook gives no flag (decision
# 586); and a class with a virtual destructor, with a vector deleting
# destructor that clang makes a weak external (decision 711 (2)). makeWeak
# makes clang write the table of virtual functions, which the MSVC ABI writes
# only where an object is made.
DIGEST_OTHER = """\
[[clang::noinline]] static int hidden(int x) { return x - 3; }
int usesOtherHidden(int x) { return hidden(x) + 2; }
[[noreturn, clang::noinline]] void fail(int x) { throw x; }
int callsFail(int x) {
    if (x > 5) {
        fail(x);
    }
    return x;
}
struct Weak {
    virtual ~Weak();
};
Weak::~Weak() = default;
Weak* makeWeak() { return new Weak; }
"""
# The cases: (file stem, what, the text to replace, the new text, the functions
# with digests that change, the functions added). An empty text to replace
# puts the new text first. A
# function's `catch` and clean-up funclets carry its decorated name in theirs
# and change with it.
DIGEST_SAMPLE_CASES = [
    ("comment", "a comment added", "", "// A comment that moves each line down.\n", (), ()),
    ("body", "one function's body changed", "return x + 1;", "return x + 2;", ("?callee@@YAHH@Z",), ()),
    ("table", "a constexpr table value changed", "{2, 3, 5, 7}", "{2, 3, 5, 8}", ("?readsTable@@YAHH@Z",), ()),
    ("inserted", "a function inserted before the others", "",
     "[[clang::noinline]] int inserted(int x) { return x ^ 5; }\n", (), ("?inserted@@YAHH@Z",)),
    ("string", "a string literal changed", '"hello"', '"hullo"', ("?greeting@@YAPEBDXZ",), ()),
    ("float", "a floating-point constant changed", "x * 2.5", "x * 3.5", ("?scaled@@YANN@Z",), ()),
    ("slots", "another override in each slot of a virtual-function table",
     "    virtual int first() const;\n    virtual int second() const;\n",
     "    virtual int second() const;\n    virtual int first() const;\n", ("?square@@YAAEBUShape@@XZ",), ()),
    ("inner", "a value in the inner table of data that points at data", "{10, 20, 30, 40}", "{10, 21, 30, 40}",
     ("?readsOuter@@YAHH@Z",), ()),
    ("call", "a call to another function with the same bytes", "return one(x);", "return two(x);",
     ("?pick@@YAHH@Z",), ()),
    ("catch", "a changed catch clause", "catch (int)", "catch (long)", ("?catcher@@YAHH@Z",), ()),
    ("switch", "another mapping of a switch statement's cases",
     "case 1: return f1(x);\n    case 2: return f2(x);", "case 2: return f1(x);\n    case 1: return f2(x);",
     ("?jump@@YAHH@Z",), ()),
    ("noexcept", "a changed noexcept", "int guarded(int x) noexcept", "int guarded(int x)", ("?guarded@@YAHH@Z",), ()),
    ("directive", "a linker directive added", "", '#pragma comment(linker, "/INCLUDE:?callee@@YAHH@Z")\n', (), ()),
]


def digest_sources(root: Path) -> list:
    """Write the basic sample, each case's variant and the other object into
    `root`; their paths, the basic sample first and the other object last."""
    paths = [root / "digest_base.cpp"]
    paths[0].write_bytes(DIGEST_SAMPLE.encode())
    for stem, _, old, new, _, _ in DIGEST_SAMPLE_CASES:
        if old and DIGEST_SAMPLE.count(old) != 1:
            raise ValueError(f"the digest case {stem} must change one place: {old!r}")
        text = DIGEST_SAMPLE.replace(old, new, 1) if old else new + DIGEST_SAMPLE
        paths.append(root / f"digest_{stem}.cpp")
        paths[-1].write_bytes(text.encode())
    paths.append(root / "digest_other.cpp")
    paths[-1].write_bytes(DIGEST_OTHER.encode())
    return paths


def digest_compiled_cases(compiler: Path, expect) -> None:
    """The digests on samples compiled with `compiler` at -O0 and at -O2, the
    two levels at the same time: one clang for each level compiles all the files."""
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary).resolve()
        sources = digest_sources(root)
        runs = {}
        for level in ("-O0", "-O2"):
            folder = root / level[1:]
            folder.mkdir()
            command = [str(compiler), level, *DIGEST_FLAGS, *map(str, sources)]
            runs[level] = (folder, subprocess.Popen(command, cwd=folder, stdout=subprocess.PIPE,
                                                    stderr=subprocess.STDOUT, text=True))
        for level, (folder, run) in runs.items():
            output, _ = run.communicate(timeout=120)
            expect(f"the digest samples compiled at {level}", (run.returncode, output), (0, ""))
            if run.returncode == 0:
                objects = {path.stem: read_object((folder / f"{path.stem}.o").read_bytes()) for path in sources}
                digest_level(level, objects, expect)


def _of(key: str, names: tuple) -> bool:
    """Examine if a function key is one of the named functions, or a funclet of one."""
    return any(name in key for name in names)


def digest_level(level: str, objects: dict, expect) -> None:
    """The checks of the digest cases at one level."""
    summaries = {stem: object_summary(found) for stem, found in objects.items()}
    program = Program({"sample": summaries["digest_base"]})
    base = program.digests()
    expect(f"{level}: the base sample, no problem", program.problems, [])
    for stem, what, _, _, changed, added in DIGEST_SAMPLE_CASES:
        variant = Program({"sample": summaries[f"digest_{stem}"]})
        found = compared(base, variant.digests())
        common = base.keys() & variant.digests().keys()
        expect(f"{level}: {what}: the functions named are in both versions",
               [name for name in changed if name not in common], [])
        expect(f"{level}: {what}: the digests that change, and no problem",
               (sorted(found[0]), sorted(k for k in found[1] | found[2] if not _of(k, changed + added)),
                sorted(name for name in added if name not in found[1]), variant.problems),
               (sorted(k for k in common if _of(k, changed)), [], [], []))
    program = Program({"sample": summaries["digest_base"], "other": summaries["digest_other"]})
    expect(f"{level}: two objects, each with a static function of one name: two keys",
           (sorted(k for k in program.digests() if k.endswith("|?hidden@@YAHH@Z")), program.problems),
           (["other|?hidden@@YAHH@Z", "sample|?hidden@@YAHH@Z"], []))
    digest_witnesses(level, objects, expect)
    digest_rulings(level, objects, summaries, expect)


def _code_of(found: CoffObject, name: str) -> CoffSection:
    return found.sections[next(s.section for s in found.symbols if s.name == name) - 1]


def _going_with(found: CoffObject, number: int) -> list:
    """The sections that go with section `number`: COMDAT selection 5."""
    return [found.sections[s.section - 1] for s in found.symbols
            if s.definition and s.definition.selection == COMDAT_ASSOCIATIVE and s.definition.number == number
            and found.sections[s.section - 1].name == s.name]


def _pointed_at(found: CoffObject, section: CoffSection) -> list:
    by_index = {s.index: s for s in found.symbols}
    return [(r.offset, r.type, by_index[r.symbol].name, by_index[r.symbol].section) for r in section.relocations]


def digest_witnesses(level: str, objects: dict, expect) -> None:
    """That each compiled case changes what it is for, so that no case passes
    because clang made it empty."""
    base = objects["digest_base"]
    expect(f"{level}: the comment changes the debug information",
           [s.data for s in base.sections if s.name.startswith(".debug$")] !=
           [s.data for s in objects["digest_comment"].sections if s.name.startswith(".debug$")], True)
    flags = []
    for stem in ("digest_base", "digest_inserted"):
        code = _code_of(objects[stem], "?callee@@YAHH@Z")
        flag = [s for s in _going_with(objects[stem], code.number) if s.name == HOOK_SECTIONS["coff-object"][0]]
        flags.append((code.number, [s.name for s in objects[stem].symbols if flag and s.section == flag[0].number
                                    and s.definition is None]))
    expect(f"{level}: the inserted function moves the others' sections and renames their flags",
           flags[0][0] != flags[1][0] and len(flags[0][1]) == 1 and flags[0][1] != flags[1][1], True)
    inner = next(s.section for s in base.symbols if s.name == "kInner")
    expect(f"{level}: readsOuter points at the inner table only through the outer one",
           [p[2] for p in _pointed_at(base, _code_of(base, "?readsOuter@@YAHH@Z")) if p[3] == inner], [])
    expect(f"{level}: the two calls have the same bytes",
           _code_of(base, "?pick@@YAHH@Z").data == _code_of(objects["digest_call"], "?pick@@YAHH@Z").data, True)
    versions = [(objects[stem], _code_of(objects[stem], "?catcher@@YAHH@Z"))
                for stem in ("digest_base", "digest_catch")]
    parts = [{s.name: (s.data, [p[:3] for p in _pointed_at(found, s)]) for s in _going_with(found, code.number)
              if not left_out(s.name)} for found, code in versions]
    expect(f"{level}: the catch clause changes only the exception tables",
           ([(code.data, [p[:3] for p in _pointed_at(found, code)]) for found, code in versions].count(
               (versions[0][1].data, [p[:3] for p in _pointed_at(*versions[0])])),
            sorted(name for name in parts[0].keys() | parts[1].keys() if parts[0].get(name) != parts[1].get(name))),
           (2, [".xdata"]))
    jump = _code_of(base, "?jump@@YAHH@Z")
    tables = [s for s in _going_with(base, jump.number) if s.name == ".rdata"]
    expect(f"{level}: the switch statement's jump table points back into its function",
           [len(t.relocations) > 0 and all(p[3] == jump.number for p in _pointed_at(base, t)) for t in tables], [True])


# --- self-test: the rulings of decision 711 ------------------------------------

def weak_cases(expect) -> None:
    """The default symbol of a weak external (decision 711 (2))."""
    for big in (False, True):
        sections = [(".text", MADE_FLAGS["code"], b"\xc3", [], False)]
        symbols = [(".text", 0, 1, 0, COFF_STATIC, [made_definition(0, 2, big=big)]),
                   ("target", 0, 1, COFF_FUNCTION, COFF_EXTERNAL, []),
                   ("alias", 0, 0, 0, COFF_WEAK_EXTERNAL, [struct.pack("<II", 2, 3)])]
        read = read_object(made_object(sections, symbols, big=big))
        expect(f"a weak external's default symbol, the {'big' if big else 'ordinary'} layout",
               read and [(s.name, s.default) for s in read.symbols], [(".text", None), ("target", None), ("alias", 2)])
    summary = made_sample(DIGEST_SECTIONS, DIGEST_SYMBOLS)
    rows = {row[1]: row for row in summary["symbols"]}
    expect("the summary keeps the default symbol", (rows["alias"][-1], rows["g"][-1]), (rows["g"][0], None))
    code = {"g": (".text", "code", b"\xc3", (), None), "h": (".text", "code", b"\xcc", (), None),
            "k": (".text", "code", bytes(8), ((1, "outer", REL32),), None)}
    functions = {"g": (0, "g", "function"), "h": (0, "h", "function"), "k": (0, "k", "function")}

    def chain(end: str, middle: str = "inner") -> Program:
        return Program({"a": made_sample(code, {**functions, "inner": (0, end, "weak"),
                                                "outer": (0, middle, "weak")})})

    to_g, to_h = chain("g"), chain("h")
    expect("a chain of two weak externals, followed to its end",
           (to_g.digests()["k"] != to_h.digests()["k"], to_g.problems, to_h.problems), (True, [], []))
    endless = chain("outer")
    expect("a chain of weak externals that does not end, a problem",
           [("a:" in p, "outer" in p) for p in (endless.digests() and endless.problems)], [(True, True)])


def unhooked_cases(expect) -> None:
    """A name that the map gives to an object that is not given (decision 711 (3))."""
    a = made_sample(CHOSEN_A, CHOSEN_SYMBOLS)
    b = made_sample(CHOSEN_B, {k: v for k, v in CHOSEN_SYMBOLS.items() if k != "user"})
    undefined = made_sample({k: v for k, v in CHOSEN_A.items() if k != "shared"},
                            {**CHOSEN_SYMBOLS, "shared": (0, None, "undefined")})
    program = Program({"a": a, "b": b}, {"dup": "c", "shared": "c"}, unhooked={"c"})
    expect("the map's choice is an object without the hook: names only, no problem",
           ("dup" in program.digests(), program.digests()["user"], program.problems, sorted(program.outside)),
           (False, Program({"a": undefined}).digests()["user"], [], ["dup", "shared"]))
    program = Program({"a": a, "b": b}, {"dup": "c", "shared": "c"}, unhooked={"d"})
    expect("the map's choice is in neither set: a problem for each name, which names the object",
           sorted((("dup" in p) + 2 * ("shared" in p), " c" in p) for p in (program.digests() and program.problems)),
           [(1, True), (2, True)])


def object_key_cases(expect) -> None:
    """The key of an object (decision 711 (4))."""
    tree = "C:\\b\\debug"
    cases = [
        ("a member of a library in the tree", (tree, "C:\\b\\debug\\orbsim_core_d.lib", "Orbit.cpp.obj"),
         "orbsim_core_d.lib(Orbit.cpp.obj)"),
        ("an object linked directly",
         (tree, "C:\\b\\debug\\CMakeFiles\\test_orbit.dir\\tests\\test_orbit.cpp.obj", None),
         "CMakeFiles/test_orbit.dir/tests/test_orbit.cpp.obj"),
        ("a library outside the tree", (tree, "C:\\VS\\lib\\x64\\msvcrtd.lib", "exe_main.obj"),
         "C:/VS/lib/x64/msvcrtd.lib(exe_main.obj)"),
        ("an object outside the tree", (tree, "D:\\other\\x.obj", None), "D:/other/x.obj"),
        ("the tree in other letters", ("C:\\B\\Debug", "c:\\b\\debug\\sub\\a.obj", None), "sub/a.obj"),
        ("the tree with / and a separator at its end", ("C:/b/debug/", "C:\\b\\debug\\a.obj", None), "a.obj"),
        ("a folder beside the tree with the same start", (tree, "C:\\b\\debug2\\a.obj", None), "C:/b/debug2/a.obj"),
    ]
    for what, arguments, want in cases:
        expect(f"the key of an object: {what}", object_key(*arguments), want)


# A made-up object for the functions without a flag and the start-up functions
# (decision 711 (5) and (6)). f has an entry in the hook's table, g has none.
# c holds a function and its funclet, with an entry; d holds the same, without
# one. init is a static start-up function and ctor the hook's own, which goes
# with its start-up entry as `sancov.module_ctor_bool_flag` does.
FLAG_SECTIONS = {
    "f": (".text", "code", b"\xc3", (), None),
    "f entry": (".SCOVP$M", "data", bytes(16), ((0, "f", ADDR64),), "f"),
    "g": (".text", "code", b"\x90\xc3", (), None),
    "c": (".text", "code", b"\xc3\xcc\xcc\xcc\xc3", (), None),
    "c entry": (".SCOVP$M", "data", bytes(16), ((0, "c", ADDR64),), "c"),
    "d": (".text", "code", b"\xc3\xcc\xc3", (), None),
    "init": (".text", "code", b"\x90\x90\xc3", (), None),
    "start": (".CRT$XCU", "data", bytes(8), ((0, "init", ADDR64),), None),
    "ctor": (".text", "code", b"\x90\x90\x90\xc3", (), None),
    "hook start": (".CRT$XCA00002", "data", bytes(8), ((0, "ctor", ADDR64),), "ctor"),
}
FLAG_SYMBOLS = {
    "f": (0, "f", "function"), "g": (0, "g", "function"), "c": (0, "c", "function"),
    "c_funclet": (4, "c", "static function"), "d": (0, "d", "function"), "d_funclet": (2, "d", "static function"),
    "init": (0, "init", "static function"), "ctor": (0, "ctor", "function"),
}


def flag_cases(expect) -> None:
    """Functions without a flag judged by section, and the start-up functions."""
    program = Program({"a": made_sample(FLAG_SECTIONS, FLAG_SYMBOLS)})
    expect("the functions without a flag, by section", program.unflagged(), ["a|d_funclet", "d", "g"])
    expect("the start-up functions", program.start_up(), ["a|init", "ctor"])
    copy = {"g": (".text", "code", b"\x90\xc3", (), None),
            "g entry": (".SCOVP$M", "data", bytes(16), ((0, "g", ADDR64),), "g")}
    b = made_sample(copy, {"g": (0, "g", "function")})
    a = made_sample(FLAG_SECTIONS, FLAG_SYMBOLS)
    expect("an external function's flag: the copy that the map names",
           ["g" in Program({"a": a, "b": b}, {"g": owner}).unflagged() for owner in ("a", "b")], [True, False])
    data = made_sample({"table": (".rdata", "data", bytes(8), (), None),
                        "start": (".CRT$XCU", "data", bytes(8), ((0, "table", ADDR64),), None)},
                       {"table": (0, "table", "static")})
    program = Program({"a": data})
    expect("a start-up entry that points at data, a problem",
           (program.start_up(), [".CRT$XCU" in p for p in program.problems]), ([], [True]))


def directive_cases(expect) -> None:
    """The linker directives of a program (decision 711 (7))."""
    def directives(text: bytes) -> dict:
        return made_sample({"f": (".text", "code", b"\xc3", (), None),
                            "directives": (".drectve", "info", text, (), None)}, {"f": (0, "f", "function")})

    a, b = directives(b" /DEFAULTLIB:x.lib /INCLUDE:one"), directives(b" /FAILIFMISMATCH:k=v")
    expect("the summary holds the SHA-256 of the directives",
           [s[4] for s in a["sections"] if s[1] == ".drectve"], [_sha(b" /DEFAULTLIB:x.lib /INCLUDE:one")])
    expect("the directives in another order of objects",
           Program({"a": a, "b": b}).directives(), Program({"b": b, "a": a}).directives())
    expect("another /INCLUDE changes the directives",
           Program({"a": a, "b": b}).directives() != Program({"a": directives(b" /DEFAULTLIB:x.lib /INCLUDE:two"),
                                                              "b": b}).directives(), True)
    expect("a second object with the same directives changes them",
           Program({"a": a}).directives() != Program({"a": a, "b": a}).directives(), True)


def request_cases(expect) -> None:
    """The digests of the functions that a call names, and only those (decision 711 (8))."""
    summary = made_sample(DIGEST_SECTIONS, DIGEST_SYMBOLS)
    base = Program({"a": summary}).digests()
    program = Program({"a": summary})
    expect("one digest on request: the same value, one code section digested",
           (program.digests(["f"]), len(program._code)), ({"f": base["f"]}, 1))
    expect("a key that the program does not have: absent", program.digests(["missing", "a|missing"]), {})
    expect("then each digest: the same values, each code section digested once",
           (program.digests(), len(program._code)), (base, 5))
    fresh = Program({"a": summary})
    expect("a static function on request", (fresh.digests(["a|s"]), len(fresh._code)), ({"a|s": base.get("a|s")}, 1))


def ruling_cases(expect) -> None:
    weak_cases(expect)
    unhooked_cases(expect)
    object_key_cases(expect)
    flag_cases(expect)
    directive_cases(expect)
    request_cases(expect)


def digest_rulings(level: str, objects: dict, summaries: dict, expect) -> None:
    """Decision 711 on the compiled samples at one level."""
    other = objects["digest_other"]
    names = {s.index: s.name for s in other.symbols}
    expect(f"{level}: clang's weak external and its default symbol",
           [(s.storage, names.get(s.default)) for s in other.symbols if s.name == "??_EWeak@@UEAAPEAXI@Z"],
           [(COFF_WEAK_EXTERNAL, "??_GWeak@@UEAAPEAXI@Z")])
    base = summaries["digest_base"]
    program = Program({"sample": base})
    every = program.digests()
    expect(f"{level}: the catch case's funclets, there and not without a flag",
           (any("?catch$" in k for k in every), program.unflagged()), (True, []))
    expect(f"{level}: the hook's start-up function, a start-up function and not without a flag",
           ("sancov.module_ctor_bool_flag" in program.start_up(),
            "sancov.module_ctor_bool_flag" in program.unflagged()), (True, False))
    expect(f"{level}: a function that only throws has no flag",
           Program({"other": summaries["digest_other"]}).unflagged(), ["?fail@@YAXH@Z"])
    expect(f"{level}: a linker directive changes the directives",
           program.directives() != Program({"sample": summaries["digest_directive"]}).directives(), True)
    expect(f"{level}: the directives in another order of objects",
           Program({"sample": base, "other": summaries["digest_other"]}).directives(),
           Program({"other": summaries["digest_other"], "sample": base}).directives())
    funclet = next(k for k in every if "?catch$" in k)
    for key in ("?jump@@YAHH@Z", "sample|?hidden@@YAHH@Z", funclet):
        fresh = Program({"sample": base})
        expect(f"{level}: the digest of {key} on request", (fresh.digests([key]), len(fresh._code)),
               ({key: every.get(key)}, 1))


# --- self-test: the part of each program (decision 713 (4)) -------------------

# A made-up tree for the part of each program. The program p.exe links two
# objects directly: direct.obj, with the hook, and recorder.obj, without it. It
# takes two members from libh.lib, a library with the hook: hooked.obj, with
# the hook, and data.obj, which holds no function and thus is without the hook
# (decision 584). It takes ext1.obj from ext.lib, a library without the hook
# that is not in the tree, and a DLL from an import library. In direct.obj, main calls
# into hooked.obj and ext1.obj. `entered`, `other` and `init` have an entry in
# the hook's table, and `nflag` has none (decision 586). `init` is a start-up
# function (decision 624).
CODE_DIRECT = {
    "main": (".text", "code", b"\xe8\0\0\0\0\xe8\0\0\0\0\xc3", ((1, "?lib@@YAHXZ", REL32), (6, "ext_f", REL32)),
             None),
    "main entry": (".SCOVP$M", "data", bytes(16), ((0, "main", ADDR64),), "main"),
    "entered": (".text", "code", b"\x90\xc3", (), None),
    "entered entry": (".SCOVP$M", "data", bytes(16), ((0, "?entered@@YAHXZ", ADDR64),), "entered"),
    "other": (".text", "code", b"\x90\x90\xc3", (), None),
    "other entry": (".SCOVP$M", "data", bytes(16), ((0, "?other@@YAHXZ", ADDR64),), "other"),
    "nflag": (".text", "code", b"\xcc\xc3", (), None),
    "init": (".text", "code", b"\x90\x90\x90\xc3", (), None),
    "init entry": (".SCOVP$M", "data", bytes(16), ((0, "init", ADDR64),), "init"),
    "start": (".CRT$XCU", "data", bytes(8), ((0, "init", ADDR64),), None),
    "directives": (".drectve", "info", b" /DEFAULTLIB:x.lib", (), None),
}
CODE_DIRECT_SYMBOLS = {
    "main": (0, "main", "function"), "?entered@@YAHXZ": (0, "entered", "function"),
    "?other@@YAHXZ": (0, "other", "function"), "?nflag@@YAHXZ": (0, "nflag", "function"),
    "init": (0, "init", "static function"), "?lib@@YAHXZ": (0, None, "undefined"), "ext_f": (0, None, "undefined"),
}
CODE_HOOKED = {
    "lib": (".text", "code", b"\x48\x8d\x05\0\0\0\0\xc3", ((3, "?table@@3HB", REL32),), None),
    "lib entry": (".SCOVP$M", "data", bytes(16), ((0, "?lib@@YAHXZ", ADDR64),), "lib"),
    "spare": (".text", "code", b"\x90\xc3", (), None),
    "spare entry": (".SCOVP$M", "data", bytes(16), ((0, "?spare@@YAHXZ", ADDR64),), "spare"),
}
CODE_HOOKED_SYMBOLS = {"?lib@@YAHXZ": (0, "lib", "function"), "?spare@@YAHXZ": (0, "spare", "function"),
                       "?table@@3HB": (0, None, "undefined")}
# The map of p.exe: each public symbol with its section, its offset there and
# its object, as `library:object` for a member. .text starts at 0x1000 and
# .rdata at 0x2000, less the base. made_tree adds the markers of the hook's
# table, which has 6 entries: main, entered, other and init, lib and spare.
CODE_MAP = {
    "main": (1, 0x000, "direct.obj"), "?entered@@YAHXZ": (1, 0x010, "direct.obj"),
    "?other@@YAHXZ": (1, 0x020, "direct.obj"), "?nflag@@YAHXZ": (1, 0x030, "direct.obj"),
    "?lib@@YAHXZ": (1, 0x100, "libh:hooked.obj"), "?spare@@YAHXZ": (1, 0x110, "libh:hooked.obj"),
    "ext_f": (1, 0x200, "ext:ext1.obj"), "?record@@YAXXZ": (1, 0x300, "recorder.obj"),
    "CloseHandle": (1, 0x400, "kernel32:KERNEL32.dll"), "?table@@3HB": (2, 0x000, "libh:data.obj"),
    "__ImageBase": (0, 0, "<linker-defined>"),
}
CODE_ENTRIES = 6
CODE_STATIC = {"init": (1, 0x040, "direct.obj")}
# The modules of p.exe in its .pdb file. A tuple is a path, in parts from the
# folder of the made-up files; a text is the text.
CODE_DIRECT_OBJECT = ("tree", "CMakeFiles", "p.dir", "direct.obj")
CODE_RECORDER = ("tree", "CMakeFiles", "p.dir", "recorder.obj")
CODE_MODULES = [(CODE_DIRECT_OBJECT, CODE_DIRECT_OBJECT), ("hooked.obj", ("tree", "libh.lib")),
                ("data.obj", ("tree", "libh.lib")), ("ext1.obj", ("outside", "ext.lib")),
                ("KERNEL32.dll", ("outside", "kernel32.lib")), ("Import:KERNEL32.dll", ("outside", "kernel32.lib")),
                (CODE_RECORDER, CODE_RECORDER), ("* Linker *", "")]
# A record of p.exe: main, entered, init and lib.
CODE_RECORD = [0x1000, 0x1010, 0x1040, 0x1100]
CODE_KEYS = ["?entered@@YAHXZ", "?lib@@YAHXZ", "main"]


def code_map(public: dict, static: dict, reverse: bool) -> str:
    """The map of p.exe as lld-link writes it, the public lines in the other sequence when `reverse` is set."""
    starts = {0: 0, 1: 0x1000, 2: 0x2000}

    def line(name: str, place: tuple) -> str:
        section, offset, where = place
        address = 0x140000000 + starts[section] + offset
        return f" {section:04x}:{offset:08x}       {name:26} {address:016x}     {where}\n"

    lines = [line(name, place) for name, place in public.items()]
    return (" p\n\n Preferred load address is 0000000140000000\n\n"
            " 0001:00000000 00001000H .text                   CODE\n"
            " 0002:00000000 00001000H .rdata                  DATA\n\n"
            + "".join(lines[::-1] if reverse else lines) + "\n Static symbols\n\n"
            + "".join(line(name, place) for name, place in static.items()))


def made_tree(root: Path, change: Optional[dict] = None) -> Path:
    """Write the made-up tree of the program cases into `root`, with one
    change, and give the program's path. `change` maps a part to its value:
    "direct", "direct symbols", "hooked" and "map" replace entries, and None
    removes one; "data", "ext2" and "recorder" give other contents; "libh" and
    "ext" add members, "modules" adds modules, and "files" adds files;
    "entries" gives the number of entries in the hook's table, and "table"
    gives a different number to the program only; "no" removes files; and
    "reverse" puts the modules, the members of the library with the hook and
    the map's lines in the other sequence."""
    change = change or {}
    reverse = change.get("reverse", False)
    entries = change.get("entries", CODE_ENTRIES)

    def path(where) -> str:
        return str(root.joinpath(*where)) if isinstance(where, tuple) else where

    def merged(base: dict, part: str) -> dict:
        return {k: v for k, v in {**base, **change.get(part, {})}.items() if v is not None}

    def one_function(name: str, code: bytes) -> bytes:
        return made_sample_object({"f": (".text", "code", code, (), None)}, {name: (0, "f", "function")})

    data = made_sample_object({"table": (".rdata", "data", change.get("data", b"\1\2\3\4"), (), None)},
                              {"?table@@3HB": (0, "table", "external")})
    hooked = [("hooked.obj", made_sample_object(merged(CODE_HOOKED, "hooked"), CODE_HOOKED_SYMBOLS)),
              ("data.obj", data)] + change.get("libh", [])
    modules = [(path(module), path(obj)) for module, obj in CODE_MODULES + change.get("modules", [])]
    markers = {TABLE_START: (2, 0x100, "recorder.obj"),
               TABLE_STOP: (2, 0x100 + TABLE_START_LENGTH + TABLE_ENTRY * entries, "recorder.obj")}
    files = {
        CODE_DIRECT_OBJECT: made_sample_object(merged(CODE_DIRECT, "direct"), merged(CODE_DIRECT_SYMBOLS,
                                                                                     "direct symbols")),
        CODE_RECORDER: one_function("?record@@YAXXZ", change.get("recorder", b"\xc3")),
        ("tree", "libh.lib"): made_archive(hooked[::-1] if reverse else hooked),
        ("outside", "ext.lib"): made_archive([("ext1.obj", one_function("ext_f", b"\xc3")),
                                              ("ext2.obj", one_function("ext_g", change.get("ext2", b"\x90\xc3")))]
                                             + change.get("ext", [])),
        ("outside", "kernel32.lib"): made_archive([("KERNEL32.dll", made_import("CloseHandle", "KERNEL32.dll"))]),
        ("tree", "p.exe"): made_pe([".text", ".SCOV", ".SCOVP"],
                                   {".SCOVP": (TABLE_MARKERS + TABLE_ENTRY * change.get("table", entries), 512)}),
        ("tree", "p.map"): code_map({**merged(CODE_MAP, "map"), **markers}, CODE_STATIC, reverse).encode(),
        ("tree", "p.pdb"): made_pdb(made_dbi(modules[::-1] if reverse else modules)),
        **change.get("files", {}),
    }
    for where, contents in files.items():
        root.joinpath(*where).parent.mkdir(parents=True, exist_ok=True)
        root.joinpath(*where).write_bytes(contents)
    for where in change.get("no", ()):
        root.joinpath(*where).unlink()
    return root / "tree" / "p.exe"


def changed_code(label: str, code: bytes, part: str = "direct") -> dict:
    """A change of made_tree: the section `label` of direct.obj, or of
    hooked.obj for the part "hooked", with other contents."""
    name, kind, _, relocations, partner = (CODE_HOOKED if part == "hooked" else CODE_DIRECT)[label]
    return {part: {label: (name, kind, code, relocations, partner)}}


# Each case changes the made-up tree at one location: (what, the change, the
# keys, the link command, the function part the same as before).
CODE_PART_CASES = [
    ("no change, read again", {}, CODE_KEYS, None, True),
    ("the modules, the members and the map's lines in the other sequence", {"reverse": True}, CODE_KEYS, None,
     True),
    ("a function that the keys name changed", changed_code("entered", b"\x90\x90\x90\xc3"), CODE_KEYS, None, False),
    ("a function that the keys do not name changed", changed_code("other", b"\xcc\x90\xc3"), CODE_KEYS, None, True),
    ("a function of the library with the hook that the keys name changed",
     changed_code("lib", b"\x48\x8d\x05\0\0\0\0\x90\xc3", "hooked"), CODE_KEYS, None, False),
    ("a function of the library with the hook that the keys do not name changed",
     changed_code("spare", b"\xcc\xc3", "hooked"), CODE_KEYS, None, True),
    ("an import member added to the library with the hook",
     {"libh": [("KERNEL32.dll", made_import("CloseHandle", "KERNEL32.dll"))]}, CODE_KEYS, None, True),
    ("a function without a flag changed", changed_code("nflag", b"\xcc\xcc\xc3"), CODE_KEYS, None, False),
    ("a function without a flag changed, for one key", changed_code("nflag", b"\xcc\xcc\xc3"), ["main"], None,
     False),
    ("a function without a flag changed, for no key", changed_code("nflag", b"\xcc\xcc\xc3"), [], None, False),
    ("a start-up function changed", changed_code("init", b"\xcc\x90\x90\xc3"), CODE_KEYS, None, True),
    ("debug information added", {"direct": {"lines": (".debug$S", "debug", b"line 2", (), "entered")}}, CODE_KEYS,
     None, True),
    ("a function that the keys name removed from the program",
     {"direct": {"entered": None, "entered entry": None}, "direct symbols": {"?entered@@YAHXZ": None},
      "map": {"?entered@@YAHXZ": None}, "entries": 5}, CODE_KEYS, None, False),
    ("the library without the hook changed its bytes", {"ext2": b"\xcc\xc3"}, CODE_KEYS, None, False),
    ("the program takes one more module from the library without the hook",
     {"map": {"ext_g": (1, 0x210, "ext:ext2.obj")}, "modules": [("ext2.obj", ("outside", "ext.lib"))]}, CODE_KEYS,
     None, False),
    ("the member without the hook changed", {"data": b"\5\6\7\x08"}, CODE_KEYS, None, False),
    ("the object without the hook linked directly changed", {"recorder": b"\x90\xc3"}, CODE_KEYS, None, False),
    ("a linker directive changed", changed_code("directives", b" /DEFAULTLIB:y.lib"), CODE_KEYS, None, False),
    ("a different link command", {}, CODE_KEYS, "lld-link.exe /out:p.exe", False),
]
# Each case gives the program a problem: (what, the change, a text that the
# problems hold).
CODE_PROBLEM_CASES = [
    ("no .pdb file", {"no": [("tree", "p.pdb")]}, "p.pdb"),
    ("no map", {"no": [("tree", "p.map")]}, "p.map"),
    ("an object that the map names is not there", {"no": [CODE_RECORDER]}, "recorder.obj"),
    ("a member of the library with the hook that cannot be read", {"libh": [("broken.obj", b"not an object")]},
     "broken.obj"),
    ("a member with the text of the hook's table that cannot be read, in a library without the hook",
     {"ext": [("bad.obj", b"not an object, but .SCOVP$M")]}, "bad.obj"),
    ("two members with one file name in the library with the hook",
     {"libh": [("other\\hooked.obj", made_sample_object(CODE_HOOKED, CODE_HOOKED_SYMBOLS))]},
     "libh.lib, which carries the hook: two objects with one file name"),
    # DIRECT.OBJ is different from direct.obj only in upper case and lower case. module_paths gives no
    # problem, because the map does not name DIRECT.OBJ.
    ("two objects with one file name linked directly",
     {"files": {("tree", "q", "DIRECT.OBJ"): made_sample_object(CODE_DIRECT, CODE_DIRECT_SYMBOLS)},
      "modules": [(("tree", "q", "DIRECT.OBJ"), ("tree", "q", "DIRECT.OBJ"))]},
     "the objects linked directly: two objects with one file name"),
    ("a table count that does not agree", {"table": 7}, "7 entries"),
]


def program_cases(expect) -> None:
    """The part of each program (decision 713 (4)), on the made-up tree."""
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary).resolve()
        program, tree = made_tree(root), root / "tree"
        code = CodeReader(tree).program(program)
        expect("a program's code: no problem", code.problems, [])
        expect("the keys of a record, the start-up function left out", code.keys(CODE_RECORD), (CODE_KEYS, []))
        keys, problems = code.keys([0x1000, 0x1090])
        expect("a record's address with no function: a problem that names the program and the address",
               (keys, [p.startswith("p.exe: ") and "0x1090" in p for p in problems]), (["main"], [True]))
        expect("the table count", code.table_entries(), (6, None))
        base = code.function_part(CODE_KEYS)
        expect("the function part: a SHA-256", isinstance(base, str) and re.fullmatch("[0-9a-f]{64}", base) is not None,
               True)
        expect("the function part two times", code.function_part(CODE_KEYS), base)
        expect("the keys in a different sequence", code.function_part(CODE_KEYS[::-1]), base)
        # Decision 547: a key that the program does not hold also gives a line.
        expect("a key that the program does not hold is not the same as no key",
               code.function_part(CODE_KEYS + ["?absent@@YAXXZ"]) != base, True)
        references = {tuple(keys): code.function_part(keys) for keys in (CODE_KEYS, ["main"], [])}
        for what, change, keys, link, same in CODE_PART_CASES:
            made_tree(root, change)
            found = CodeReader(tree).program(program)
            part = found.function_part(keys, link)
            expect(f"the function part: {what}", (part == references[tuple(keys)], part is not None, found.problems),
                   (same, True, []))
            made_tree(root)
        for what, change, text in CODE_PROBLEM_CASES:
            made_tree(root, change)
            found = CodeReader(tree).program(program)
            problems = found.problems
            expect(f"a problem: {what}", (found.function_part(CODE_KEYS), bool(problems),
                                          all(p.startswith("p.exe: ") for p in problems),
                                          any(text in p for p in problems)), (None, True, True, True))
            made_tree(root)
        made_tree(root, {"no": [("tree", "p.map")]})
        keys, problems = CodeReader(tree).program(program).keys(CODE_RECORD)
        expect("no map: no key, and a problem", (keys, len(problems)), ([], 1))
        made_tree(root)
        cache_cases(root, program, base, expect)


def made_second(root: Path) -> Path:
    """A second program in the made-up tree, q.exe: one object with the hook,
    q.obj, linked directly, with one function and one entry in the hook's
    table. Its path."""
    obj = ("tree", "CMakeFiles", "q.dir", "q.obj")
    root.joinpath(*obj).parent.mkdir(parents=True, exist_ok=True)
    root.joinpath(*obj).write_bytes(made_sample_object(
        {"q": (".text", "code", b"\x90\x90\xc3", (), None),
         "q entry": (".SCOVP$M", "data", bytes(16), ((0, "?q@@YAHXZ", ADDR64),), "q")},
        {"?q@@YAHXZ": (0, "q", "function")}))
    public = {"?q@@YAHXZ": (1, 0, "q.obj"), TABLE_START: (2, 0x100, "q.obj"),
              TABLE_STOP: (2, 0x100 + TABLE_START_LENGTH + TABLE_ENTRY, "q.obj")}
    tree = root / "tree"
    (tree / "q.map").write_bytes(code_map(public, {}, False).encode())
    (tree / "q.pdb").write_bytes(made_pdb(made_dbi([(str(root.joinpath(*obj)),) * 2, ("* Linker *", "")])))
    (tree / "q.exe").write_bytes(made_pe([".text", ".SCOV", ".SCOVP"], {".SCOVP": (TABLE_MARKERS + TABLE_ENTRY, 512)}))
    return tree / "q.exe"


def cache_cases(root: Path, program: Path, base: str, expect) -> None:
    """The summaries kept between runs, one file for each object (decisions
    567, 711 (8) and 714)."""
    tree, folder, second = root / "tree", root / "cache", made_second(root)
    direct, hooked, q = "CMakeFiles/p.dir/direct.obj", "libh.lib(hooked.obj)", "CMakeFiles/q.dir/q.obj"

    def name(key: str) -> str:
        return _sha(key.encode()) + ".json"

    def listed() -> list:
        return sorted(p.name for p in folder.glob("*"))

    def kept(key: str):
        try:
            return json.loads((folder / name(key)).read_bytes())
        except (OSError, ValueError):
            return None

    def run(*programs: Path) -> tuple:
        reader = CodeReader(tree, folder)
        parts = [reader.program(path).function_part(CODE_KEYS) for path in programs]
        return parts, reader.cache.made, reader.cache.loaded

    cold = CodeReader(tree, folder)
    code = cold.program(program)
    expect("a cold run: the same part, and a file made for each object with the hook",
           (code.function_part(CODE_KEYS), cold.program(second).function_part([]) is not None, cold.cache.made,
            cold.cache.loaded, listed()), (base, True, 3, 0, sorted(name(key) for key in (direct, hooked, q))))
    expect("the keys of the objects with the hook that the program links", code.objects, [direct, hooked])
    expect("a cold run: each part of the time measured", sorted(k for k, v in cold.seconds.items() if v > 0),
           ["digests", "reading", "summaries"])
    sha = _sha(root.joinpath(*CODE_DIRECT_OBJECT).read_bytes())
    found = kept(direct)
    expect("a file holds the key, the SHA-256, the version and the summary, with no CR",
           (sorted(found or ()), found and (found["key"], found["sha256"], found["version"]),
            b"\r" in (folder / name(direct)).read_bytes() if found else None),
           (["key", "sha256", "summary", "version"], (direct, sha, SUMMARY_VERSION), False))
    q_file = (folder / name(q)).read_bytes() if found else None
    expect("a warm run of one program: the same part, and no summary made", run(program), ([base], 0, 2))
    expect("a run of one program with no prune: the files of the other programs stay",
           (listed(), (folder / name(q)).read_bytes() if found else None), (sorted(map(name, (direct, hooked, q))),
                                                                              q_file))
    reader = CodeReader(tree, folder)
    reader.program(program).function_part(CODE_KEYS)
    reader.program(second).function_part([])
    expect("one program, then a different program, with no prune: no summary made", (reader.cache.made,
                                                                                    reader.cache.loaded), (0, 3))
    made_tree(root, changed_code("entered", b"\x90\x90\x90\xc3"))
    parts, made, loaded = run(program)
    changed = _sha(root.joinpath(*CODE_DIRECT_OBJECT).read_bytes())
    expect("a changed object: its new summary replaces its file, and the count of files stays",
           (parts[0] != base, made, loaded, len(listed()), (kept(direct) or {}).get("sha256")),
           (True, 1, 1, 3, changed))
    made_tree(root)
    expect("the object as before: its file replaced again", run(program), ([base], 1, 1))
    good = (folder / name(direct)).read_bytes() if found else b""
    folder.mkdir(exist_ok=True)
    for what, edit in (("a different key", {"key": "CMakeFiles/p.dir/other.obj"}),
                       ("a different SHA-256", {"sha256": "0" * 64}),
                       ("a different version", {"version": SUMMARY_VERSION - 1})):
        (folder / name(direct)).write_bytes(json.dumps({**(found or {}), **edit}).encode())
        unused = SummaryCache(folder).get(direct, sha)
        expect(f"a file with {what}: not used, and replaced",
               (unused, run(program), (folder / name(direct)).read_bytes() == good), (None, ([base], 1, 1), True))
    (folder / "unused.json").write_bytes(b"{}")
    (folder / "notes.txt").write_bytes(b"not a summary")
    expect("prune: the files of the keys that are not in the set, and each file that is not a summary",
           (SummaryCache(folder).prune([direct, hooked]), listed()),
           (sorted([name(q), "notes.txt", "unused.json"]), sorted([name(direct), name(hooked)])))
    expect("prune with no folder", SummaryCache(None).prune([direct]), [])


# The entries of the hook's table of p.exe in the made-up tree, by address less
# the base: main, entered, other, init, lib and spare, as CODE_MAP gives them.
CODE_TABLE = [0x1000, 0x1010, 0x1020, 0x1040, 0x1100, 0x1110]


def kept_table_cases(expect) -> None:
    """The kept table of each program (decision 715 (1)): the addresses of the
    hook's table read from the program, the key of each, the table that
    TableCache keeps, and ProgramTable, which gives what ProgramCode gives for
    a record."""
    expect("the addresses of the hook's table, read from the program", table_addresses(made_table_program(CODE_TABLE)),
           CODE_TABLE)
    expect("a program with no optional header gives no addresses",
           table_addresses(made_pe([".text", ".SCOVP"], {".SCOVP": (TABLE_MARKERS + TABLE_ENTRY, 512)})), None)
    cut = made_table_program(CODE_TABLE)
    expect("a table section cut off in the file gives no addresses", table_addresses(cut[:-9]), None)
    # The last entry's second word is the 8 bytes before the stop marker.
    other = cut[:-16] + struct.pack("<Q", 2) + cut[-8:]
    expect("a table with a second word that is not the hook's 1 gives no addresses",
           (len(other) == len(cut), table_addresses(other)), (True, None))
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary).resolve()
        tree, program = root / "tree", made_tree(root)
        program.write_bytes(made_table_program(CODE_TABLE))
        code = CodeReader(tree).program(program)
        table = code.table() or {}
        expect("the table of a program: its count, and the key of each entry, none for the start-up function",
               (table.get("entries"), table.get("problem"), sorted(table.get("functions") or {}),
                (table.get("functions") or {}).get("1040"), (table.get("functions") or {}).get("1010")),
               (CODE_ENTRIES, None, sorted(f"{a:x}" for a in CODE_TABLE), [None, []], ["?entered@@YAHXZ", []]))
        kept = ProgramTable("p.exe", table)
        expect("a record's keys from the kept table, as from the program", kept.keys(CODE_RECORD),
               code.keys(CODE_RECORD))
        expect("the count from the kept table, as from the program", kept.table_entries(), code.table_entries())
        keys, problems = kept.keys([0x1000, 0x1090])
        expect("an address that is not an entry of the table: a problem that names the program and the address",
               (keys, [p.startswith("p.exe: ") and "0x1090" in p for p in problems]), (["main"], [True]))
        made_tree(root, {"map": {"?alias@@YAHXZ": (1, 0x010, "direct.obj")}})
        program.write_bytes(made_table_program(CODE_TABLE))
        code = CodeReader(tree).program(program)
        found = ProgramTable("p.exe", code.table() or {}).keys([0x1010])
        expect("an entry that is not one function: its problem kept, as the program gives it",
               (found, bool(found[1])), (code.keys([0x1010]), True))
        made_tree(root, {"no": [("tree", "p.pdb")]})
        program.write_bytes(made_table_program(CODE_TABLE))
        expect("a program with a problem has no table", CodeReader(tree).program(program).table(), None)
        made_tree(root, {"no": [CODE_RECORDER]})
        program.write_bytes(made_table_program(CODE_TABLE))
        code = CodeReader(tree).program(program)
        expect("a program with a problem in its objects, and a table that can be read, has no table",
               (bool(code.problems), code._addresses, code.table()), (True, CODE_TABLE, None))
        made_tree(root)
        expect("a program with a table that cannot be read has no table", CodeReader(tree).program(program).table(),
               None)

        cache, folder = TableCache(root / "tables"), root / "tables"
        good = ("C:/t/p.exe", "a" * 64, "b" * 64)
        cache.put(*good, table)
        expect("a kept table read again", cache.get(*good), table)
        for what, key in (("the key of a different program", ("C:/t/q.exe",) + good[1:]),
                          ("other bytes of the program", (good[0], "c" * 64, good[2])),
                          ("a different map", good[:2] + ("c" * 64,))):
            expect(f"a kept table not used for {what}", cache.get(*key), None)
        file = folder / TableCache.file_name(good[0])
        kept_file = json.loads(file.read_bytes()) if file.is_file() else {}
        for what, edit in (("a different version", {"version": TABLE_VERSION + 1}),
                           ("a table that is not one", {"table": {"entries": 6, "functions": [1]}})):
            file.write_bytes(json.dumps({**kept_file, **edit}).encode())
            expect(f"a kept table with {what} is not used", cache.get(*good), None)
        cache.put(*good, table)
        cache.put("C:/t/q.exe", "d" * 64, "e" * 64, table)
        expect("prune: the files of the programs that are not in the set",
               (cache.prune([good[0]]), sorted(p.name for p in folder.iterdir())),
               ([TableCache.file_name("C:/t/q.exe")], [file.name]))


def self_test(tools: Optional[tuple] = None) -> int:
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

    object_cases(expect)
    archive_cases(expect)
    pdb_cases(expect)
    module_cases(expect)
    record_cases(expect)
    digest_cases(expect)
    ruling_cases(expect)
    program_cases(expect)
    kept_table_cases(expect)
    if tools is not None:
        compiler, archiver, linker = (Path(tool) for tool in tools)
        compiled_cases(compiler, archiver, linker, expect)
        digest_compiled_cases(compiler, expect)
    return 1 if failures else 0


def main() -> int:
    arguments = sys.argv[1:]
    if arguments == ["--self-test"]:
        return self_test()
    tools = ["--compiler", "--archiver", "--linker"]
    if len(arguments) == 7 and arguments[0] == "--self-test" and arguments[1:7:2] == tools:
        return self_test(tuple(arguments[2:7:2]))
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())

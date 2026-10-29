#!/usr/bin/env python3
"""Can a change change the .pdb file of a program and keep its bytes?

    pdb-measure.py <build tree> <scratch folder>

M1-137 item 6, register decisions 853 and 864 to 866; the results are in
docs/measurements/m1-137-pdb.md. A small program is compiled with the
arguments with which the tree compiles tests/assertion_message_probe.cpp, from
its compile_commands.json, and linked with the arguments with which the tree
links assertion_message_probe.exe, from Ninja, with the tree's coverage
recorder. The tree is only read. The program is built four times, one
time after the other in one folder, thus each path stays the same:

    base    the source as it is
    again   the same source again: the build must give the same bytes
    top     a comment line at the top: the lines of the code move
    end     a comment line at the end: only the text of the source changes

For each build the script gives the hashes of the object, the program and the
.pdb file, the identifier of the .pdb file that the program holds (its
CodeView record), and each range of bytes in which the program is not as in
the build base, with the section that holds it.
"""
import hashlib
import json
import pathlib
import shlex
import shutil
import struct
import subprocess
import sys

PROBE_SOURCE = "tests/assertion_message_probe.cpp"
PROBE_PROGRAM = "assertion_message_probe.exe"
SOURCE = """namespace {
int twice(int value) { return 2 * value; }
} // namespace

int main(int argc, char** /*argv*/) { return twice(argc); }
"""
FORMS = {"base": SOURCE, "again": SOURCE, "top": "// a comment line\n" + SOURCE, "end": SOURCE + "// a comment line\n"}


def compile_arguments(tree: pathlib.Path) -> list:
    """The tree's compile command of the probe, with its source and object
    replaced by probe.cpp and probe.obj."""
    for entry in json.loads((tree / "compile_commands.json").read_text(encoding="utf-8")):
        if entry["file"].replace("\\", "/").endswith(PROBE_SOURCE):
            words = entry.get("arguments") or shlex.split(entry["command"], posix=False)
            out = words.index("-o")
            source = words.index("-c")
            words[out + 1] = "probe.obj"
            words[source + 1] = "probe.cpp"
            return words
    sys.exit(f"{tree}: no compile command of {PROBE_SOURCE}")


def link_arguments(tree: pathlib.Path) -> list:
    """The tree's link command of the probe from Ninja, with its objects and
    its outputs replaced: the tree's objects and libraries by their full
    paths, the probe's own object by probe.obj, and the outputs by probe.*."""
    commands = subprocess.run(["ninja", "-C", str(tree), "-t", "commands", PROBE_PROGRAM], capture_output=True,
                              text=True, check=True).stdout.splitlines()
    line = commands[-1]
    start, end = line.index("cd . && ") + len("cd . && "), line.rindex(" && cd .")
    words = shlex.split(line[start:end], posix=False)
    found = []
    for word in words:
        if word.endswith("assertion_message_probe.cpp.obj"):
            found.append("probe.obj")
        elif word.endswith((".obj", ".lib")) and not word.startswith(("-", "/")):
            found.append(str(tree / word))
        elif word == PROBE_PROGRAM:
            found.append("probe.exe")
        elif word.startswith(("/implib:", "/pdb:")):
            found.append(word.split(":", 1)[0] + ":probe." + word.rsplit(".", 1)[1])
        else:
            found.append(word)
    return found


def sections(data: bytes) -> list:
    """(name, first byte, end) of the headers and of each section of a PE file."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    table = pe + 24 + optional
    found = [("headers", 0, struct.unpack_from("<I", data, table + 20)[0])]
    for i in range(count):
        at = table + 40 * i
        size, raw = struct.unpack_from("<II", data, at + 16)
        found.append((data[at:at + 8].rstrip(b"\0").decode(), raw, raw + size))
    return found


def differences(base: bytes, other: bytes) -> list:
    """Each range of bytes in which `other` is not as `base`, as (section,
    first byte, length)."""
    if len(base) != len(other):
        return [("size", len(base), len(other))]
    found = []
    for i, (a, b) in enumerate(zip(base, other)):
        if a == b:
            continue
        if found and found[-1][1] + found[-1][2] == i:
            found[-1] = (found[-1][0], found[-1][1], found[-1][2] + 1)
        else:
            name = next((n for n, first, end in sections(base) if first <= i < end), "?")
            found.append((name, i, 1))
    return found


def codeview(data: bytes):
    at = data.find(b"RSDS")
    return (at, data[at + 4:at + 24].hex()) if at >= 0 else (None, None)


def sha(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()[:12]


def main(argv: list) -> int:
    if len(argv) != 2:
        print(__doc__)
        return 2
    tree, scratch = pathlib.Path(argv[0]).resolve(), pathlib.Path(argv[1]).resolve()
    work = scratch / "work"
    work.mkdir(parents=True, exist_ok=True)
    compile_words, link_words = compile_arguments(tree), link_arguments(tree)
    for form, text in FORMS.items():
        (work / "probe.cpp").write_bytes(text.encode("ascii"))
        subprocess.run(compile_words, cwd=work, check=True)
        subprocess.run(link_words, cwd=work, check=True)
        (scratch / form).mkdir(exist_ok=True)
        for name in ("probe.obj", "probe.exe", "probe.pdb"):
            shutil.copy2(work / name, scratch / form / name)
    base = (scratch / "base" / "probe.exe").read_bytes()
    for form in FORMS:
        folder = scratch / form
        exe = (folder / "probe.exe").read_bytes()
        at, identifier = codeview(exe)
        print(f"{form:6} object {sha(folder / 'probe.obj')}  program {sha(folder / 'probe.exe')}  "
              f"pdb {sha(folder / 'probe.pdb')}  CodeView record at {at:#x}: {identifier}")
        for name, first, length in differences(base, exe):
            print(f"         differs from base: {name}, {length} bytes at {first:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

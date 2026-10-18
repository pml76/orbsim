#!/usr/bin/env python3
"""Check that every file a test reads is named through the recording helper (M1-129).

    check-test-reads.py <repository root>
    check-test-reads.py --self-test

Why this exists: `check` skips a test whose program, command line,
environment and every file it read are unchanged since its last passing run
(scripts/run-tests.py, register decision 483). A file a test reads that its
read record does not name is a file whose change the driver cannot see, and
the test it decides would then be skipped wrongly -- and a skipped test looks
exactly like a passing one. So the tests name every file they read through
tests/TestReads.hpp, whose dataFile() and probeFile() record as they name, and
this holds the rest of tests/ to that, by structure (register decision 530):

  * **no folder reaches a test but through the helper**: an `ORBSIM_..._DIR`
    macro -- the build's name for a folder -- appears in no file under tests/
    but tests/TestReads.cpp. A mention in a comment is refused too, which is
    simpler than telling the two apart and costs nothing;
  * **no test opens a file itself**: `std::ifstream`, `std::fstream`,
    `std::filebuf`, `fopen` and its relatives, `CreateFile`, and stb_image's
    readers by path appear only in the three reader helpers --
    tests/TestReads.cpp, tests/FixtureFile.cpp and tests/HdrDumpFile.cpp,
    whose callers hand them recorded paths -- and in the stand-alone programs,
    the files that define `main` or a fuzzer's entry point. Those write no
    record, and the driver runs them whenever anything they could read
    changes (decision 505). Writing a file is not reading one, and is allowed:
    so a `CreateFile`, `CreateFileA` or `CreateFileW` whose access is
    GENERIC_WRITE alone, read across the whole call, is let through anywhere --
    the coverage recorder writes its record so (decisions 659 and 690) -- and
    every other `CreateFile` is refused, an access the check cannot read
    among them.

What this cannot see -- a path spelled as a literal and handed to code under
src/ -- the audit with Process Monitor at each phase gate does (decisions 499
and 509), and the full run before every push catches regardless (decision 503).

Proven able to fail before it was trusted: --self-test runs the check on
small trees of its own, one per way it must refuse and one it must accept,
and exits non-zero unless each is judged right, for its own reason.
"""

import pathlib
import re
import sys
import tempfile
from typing import Optional

READERS = ("TestReads.cpp", "FixtureFile.cpp", "HdrDumpFile.cpp")
HELPER = "TestReads.cpp"
FOLDER_MACRO = re.compile(r"\bORBSIM_[A-Z0-9_]*_DIR\b")
# What opens a file for reading. `std::ofstream` is not here: a test writes
# its own scratch files, which are outputs, not inputs.
OPENERS = re.compile(
    r"\bstd::(?:basic_)?ifstream\b|\bstd::(?:basic_)?fstream\b|\bstd::(?:basic_)?filebuf\b"
    r"|\b(?:_w)?fopen(?:_s)?\s*\(|\bfreopen\s*\(|\bCreateFile[AW2]?\s*\("
    r"|\bstbi_load(?:_16|f)?\s*\(|\bstbi_load(?:_16|f)?_from_file\s*\(")
PROGRAM = re.compile(r"\bint\s+main\s*\(|\bLLVMFuzzerTestOneInput\s*\(")
# A CreateFile call that may ask only to write, and the one access that does
# (decision 690). CreateFile2 is not among them, and stays refused.
CREATE_FILE = re.compile(r"\bCreateFile[AW]?\s*\(")
WRITE_ONLY = "GENERIC_WRITE"


def call_arguments(text: str, opening: int) -> Optional[list]:
    """The arguments of the call whose `(` is at `opening`, split at its own
    commas, or None where the call is never closed."""
    depth = 0
    arguments = [""]
    for char in text[opening + 1:]:
        if char == ")" and depth == 0:
            return arguments
        if char == "," and depth == 0:
            arguments.append("")
            continue
        depth += {"(": 1, ")": -1}.get(char, 0)
        arguments[-1] += char
    return None


def without_writes(text: str) -> str:
    """The text with the name of every CreateFile call whose access is
    GENERIC_WRITE alone blanked out, every line kept where it was."""
    masked = list(text)
    for call in CREATE_FILE.finditer(text):
        arguments = call_arguments(text, call.end() - 1)
        if arguments is not None and len(arguments) > 1 and arguments[1].strip() == WRITE_ONLY:
            masked[call.start():call.end()] = " " * (call.end() - call.start())
    return "".join(masked)


def problems(files: dict) -> list:
    """Every way the files break the rule, each naming its file and line; empty
    if they keep it. `files` maps a name relative to tests/ to its text."""
    found = []
    if not files:
        return ["no file was found under tests/: the check would pass by reading nothing"]
    helper = files.get(HELPER)
    if helper is None or not FOLDER_MACRO.search(helper):
        found.append(f"tests/{HELPER} names no folder: the recording helper is not where this "
                     "check expects it, so it would pass without checking anything")
    for name, text in sorted(files.items()):
        program = PROGRAM.search(text) is not None
        lines = zip(text.splitlines(), without_writes(text).splitlines())
        for number, (line, unwritten) in enumerate(lines, start=1):
            if name != HELPER and FOLDER_MACRO.search(line):
                found.append(f"tests/{name}:{number}: names a folder ({FOLDER_MACRO.search(line).group(0)}) "
                             f"-- only tests/{HELPER} may; ask it for the one file with dataFile() "
                             "or probeFile()")
            if name not in READERS and not program and OPENERS.search(unwritten):
                found.append(f"tests/{name}:{number}: opens a file itself "
                             f"({OPENERS.search(unwritten).group(0).strip()}) -- read through "
                             "tests/TestReads.hpp, readRecordedFile() or readScratchFile()")
    return found


def sources(tests: pathlib.Path) -> dict:
    return {path.relative_to(tests).as_posix(): path.read_text(encoding="utf-8", errors="replace")
            for path in sorted(tests.rglob("*")) if path.suffix in (".cpp", ".hpp", ".h")}


def check(root: pathlib.Path) -> int:
    files = sources(root / "tests")
    found = problems(files)
    for line in found:
        print(f"check-test-reads: {line}")
    if found:
        return 1
    print(f"check-test-reads: {len(files)} files under tests/ name their reads through "
          f"tests/{HELPER}")
    return 0


def self_test() -> int:
    helper = 'std::string_view f() { return ORBSIM_DATA_DIR; }\nstd::ifstream in{p};\n'
    clean = {
        HELPER: helper,
        "FixtureFile.cpp": "std::ifstream in{path, std::ios::binary};\n",
        "HdrDumpFile.cpp": "std::ifstream file(path, std::ios::binary);\n",
        "test_a.cpp": 'auto f = readFixtureText(std::filesystem::path{dataFile("a/b.txt")});\n'
                      "std::ofstream out{scratch};\n",
        "make_tool.cpp": "int main(int argc, char** argv) { std::ifstream in{argv[1]}; }\n",
        "fuzz_x.cpp": 'extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t) {\n'
                      "    std::FILE* f = fopen(\"x\", \"rb\");\n}\n",
    }
    folder = "names a folder"
    opens = "opens a file itself"
    cases = [
        ("the files as they should be", clean, None),
        ("a suite names data/'s macro", {**clean, "test_b.cpp": "auto p = ORBSIM_DATA_DIR;\n"}, folder),
        ("a suite names the probes' macro in a comment",
         {**clean, "test_b.cpp": "// was ORBSIM_PROBE_DIR\n"}, folder),
        ("a reader names a folder", {**clean, "FixtureFile.cpp": "ORBSIM_DATA_DIR\n"}, folder),
        ("a stand-alone program names a folder",
         {**clean, "make_tool.cpp": "int main() { return ORBSIM_GOLDEN_DIR[0]; }\n"}, folder),
        ("a suite opens an ifstream", {**clean, "test_b.cpp": "std::ifstream in(p, std::ios::binary);\n"},
         opens),
        ("a suite opens an fstream", {**clean, "test_b.cpp": "std::fstream in(p);\n"}, opens),
        ("a suite calls fopen", {**clean, "test_b.cpp": 'FILE* f = fopen("x", "rb");\n'}, opens),
        ("a suite calls _wfopen_s", {**clean, "test_b.cpp": "_wfopen_s(&f, L\"x\", L\"rb\");\n"}, opens),
        ("a suite calls CreateFileW", {**clean, "test_b.cpp": "CreateFileW(name, GENERIC_READ);\n"}, opens),
        # Writing is not reading: a CreateFile asking for GENERIC_WRITE alone
        # is let through, read across the whole call, and every other refused
        # (decision 690).
        ("a write-only CreateFileW", {**clean, "test_b.cpp": "CreateFileW(name, GENERIC_WRITE, 0, nullptr);\n"},
         None),
        ("a write-only CreateFileW over several lines",
         {**clean, "test_b.cpp": "auto* opened = CreateFileW(file.c_str(),\n"
                                 "                           GENERIC_WRITE,\n"
                                 "                           0);\n"}, None),
        ("a write-only CreateFileA and CreateFile",
         {**clean, "test_b.cpp": "CreateFileA(a, GENERIC_WRITE, 0);\nCreateFile(b,  GENERIC_WRITE , 0);\n"}, None),
        ("a CreateFileW reading over several lines",
         {**clean, "test_b.cpp": "CreateFileW(name,\n    GENERIC_READ,\n    0);\n"}, opens),
        ("a CreateFileW reading and writing",
         {**clean, "test_b.cpp": "CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0);\n"}, opens),
        ("a CreateFileW whose access is a variable", {**clean, "test_b.cpp": "CreateFileW(name, access, 0);\n"},
         opens),
        ("a CreateFileW whose access is an expression",
         {**clean, "test_b.cpp": "CreateFileW(name, GENERIC_WRITE | extra, 0);\n"}, opens),
        ("a CreateFileW whose access is a call", {**clean, "test_b.cpp": "CreateFileW(name, access(GENERIC_WRITE));\n"},
         opens),
        ("a write-only CreateFileW beside a read on its line",
         {**clean, "test_b.cpp": "CreateFileW(a, GENERIC_WRITE, 0); std::ifstream in(p);\n"}, opens),
        ("a write-only CreateFile2", {**clean, "test_b.cpp": "CreateFile2(name, GENERIC_WRITE, 0, 2, nullptr);\n"},
         opens),
        ("a CreateFileW never closed", {**clean, "test_b.cpp": "CreateFileW(name, GENERIC_WRITE\n"}, opens),
        ("a suite reads an image by path", {**clean, "test_b.cpp": 'stbi_load("x.png", &w, &h, &c, 4);\n'},
         opens),
        ("a support header opens a file", {**clean, "Support.hpp": "inline std::ifstream open();\n"}, opens),
        ("a nested file opens one", {**clean, "sub/test_c.cpp": "std::basic_ifstream<char> in;\n"}, opens),
        ("the helper names no folder", {**clean, HELPER: "int x;\n"}, "names no folder"),
        ("the helper is missing", {k: v for k, v in clean.items() if k != HELPER}, "names no folder"),
        ("nothing to read", {}, "no file was found"),
    ]
    failures = 0
    for name, files, reason in cases:
        found = problems(files)
        if reason is None and found:
            failures += 1
            print(f"self-test FAILED: {name}: refused it: {found}")
        elif reason is not None and not any(reason in line for line in found):
            failures += 1
            print(f"self-test FAILED: {name}: expected a refusal saying '{reason}', got {found}")

    # And through the file system once, as the real check reads it: a tree
    # whose one suite opens a file must fail, and the clean tree must pass.
    with tempfile.TemporaryDirectory() as scratch:
        root = pathlib.Path(scratch)
        (root / "tests").mkdir()
        for name, text in clean.items():
            (root / "tests" / name).write_text(text, encoding="utf-8")
        if check(root) != 0:
            failures += 1
            print("self-test FAILED: the clean tree on disk was refused")
        (root / "tests" / "test_b.cpp").write_text("std::ifstream in(p);\n", encoding="utf-8")
        if check(root) == 0:
            failures += 1
            print("self-test FAILED: a tree on disk whose suite opens a file passed")
    print(f"check-test-reads self-test: {len(cases) + 2 - failures} of {len(cases) + 2} cases")
    return 1 if failures else 0


def main() -> int:
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    return check(pathlib.Path(sys.argv[1]))


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Run the tests whose inputs changed, and skip the rest (M1-129).

    run-tests.py <build tree> --ctest <ctest> [--jobs <n>] [--full]
    run-tests.py --pre-push <repository root>      (reads git's pre-push lines)
    run-tests.py --self-test
    run-tests.py --end-to-end <cmake> <ctest> <ninja>
    run-tests.py --function-level <cmake> <ctest> <ninja> <clang++> <llvm-ar> <lld-link>
                 <tests/CoverageRecorder.cpp> <tests/WriterTurns.hpp>
    run-tests.py <build tree> --in-tree --ctest <ctest> --suite <program> --without-listener <program>
                 [--application <program> --library <file name>...]

Why this exists: CTest remembers nothing, so every `check` reran all 560
tests whatever had changed -- on M1-118's 16 commits, 8,771 test runs where a
simulation found 2,284 needed (register decision 483). This is the driver
`check` calls instead of calling CTest itself (decision 496): it decides from
fingerprints which tests to hand to CTest, and hands it those alone. A plain
`ctest`, and `mutate.py`'s `ctest -R`, still run every test they are given.

**A fingerprint** is a hash of everything a test's result rests on. A test is
skipped when every part of its fingerprint is what it was at its last
**passing** run (decisions 483 and 536):

  * its definition, as CTest describes it -- command line and properties;
  * its environment: PATH, each VK_* and ORBSIM_* variable (decision 501),
    and PYTHONPATH (decision 796), but ORBSIM_TEST_NAME, which the driver
    removes from the environment that it gives CTest (decision 715 (2)), and
    ORBSIM_READ_RECORD_OWNER, the outer tests of a driver in a test (decision
    781);
  * every program on its command line that this tree builds, by its bytes and
    those of every library it loads (decision 525), the libraries found from
    the program's import table with llvm-readobj on Windows and by ldd
    elsewhere; and every other file on its command line, by its bytes;
  * a program that the test takes function by function (M1-132, decisions
    583, 703, 706 and 713): the libraries that it loads at start, as one part
    that must be as it was; and, in the store next to the parts, its bytes,
    its function part and the functions that the test entered. The test is
    skipped only when its bytes or its function part is as it was; the
    driver computes the function part only for a program with changed bytes.
    The function part is the SHA-256 that ProgramCode.function_part in
    scripts/compiled-code.py gives for those functions and for the link
    command of the program, the last command of Ninja for it. Which programs
    a test takes function by function, from the coverage records that the
    programs with the hook write into `<tree>/coverage-records/`: the
    program of a test that starts a program with the hook directly; each
    program that the records of a test through one of the seven scripts of
    LISTED_SCRIPTS name; and, for each other test, each program that its
    records name and that is not on its command line -- a program on its
    command line keeps its bytes. A record belongs to the test that it
    names or, if it names no test, to the test with the same command line;
  * a directory on its command line inside the repository: every file in it
    git does not ignore; the build tree itself: its Ninja files and every file
    git does not ignore (decisions 500 and 510);
  * a test run through a program this tree does not build -- cmake, python:
    every file under scripts/ and cmake/, CMakeLists.txt and CMakePresets.json
    (decision 531), since scripts load other scripts;
  * a test with complete records of its Python processes (M1-138): not the
    two parts above, but its records. scripts/read-record/sitecustomize.py,
    which this script puts first on PYTHONPATH for each test that it runs
    (decisions 771 and 781), writes one record for each Python process into
    `<tree>/test-reads/`: each file that the process read, and the names in
    each folder that it examined, with their hash (decision 765). The
    records are complete when each has its last line `end` and the `read`
    line of the hook of this repository (decisions 782 and 793), and one is
    of the interpreter of the test's own script (decision 801 (7)). Each
    child must be of rule 2 or 5 of decision 778 (decisions 811-817):
    `ctest --show-only=json-v1` in the tree, with the CTest files of the
    tree in the part of its children, or a child with its working folder
    not in the repository or the tree, by its program and the paths on its
    command line.
    The test is unchanged when each line is the line that the hook writes for
    its path at this time (decision 792). Else the two parts above stay
    (decision 801 (5));
  * a `cmake -P` test with a complete trace (M1-138): not the part of
    scripts/, but the files of its trace that are not in the build tree.
    The arguments of cmake/TraceArguments.cmake make CMake write the trace
    into `<tree>/test-reads/<test name>.trace.json` (decisions 768 and 784).
    The driver uses only the commands that it knows, and a child only when
    the command line of the test names it (decisions 775, 803, 805 and 806);
    the part of the build tree, the parts of the folders on its command line
    and the cautious part stay (decision 809 (2));
  * the files it read, from its read record (decisions 489, 498 and 527):
    each must still hash to what the record says it was when the test read it;
  * the cautious rule (decisions 489 and 505): a test with no complete record,
    or whose record names a file under data/, tests/golden/ or the build files,
    is fingerprinted on all of those -- every file on disk under data/ and
    tests/golden/, ignored or not, since a generated fixture is ignored by git
    and still read;
  * the compiled shaders its runs of the application asked for (M1-131,
    decisions 629, 635 and 637), from the records the application writes into
    `<tree>/coverage-records/`, which this script names to every test it runs
    and empties before the run (decisions 570 and 642): each file named in
    every whole record carrying the test's name, read in full -- a test that
    starts the application through a script among them -- and every compiled
    shader for a test that names the application on its command line and left
    no whole record, a crash or an abort, or that left one record not whole;
    for a test labelled gpu, the graphics card, its driver and the Vulkan
    versions, read from a clear probe this script runs before CTest
    (decisions 490 and 532);
  * the "machine" part, in every test (decisions 509, 533 and 615): the
    leap seconds the leap-second test compares against -- on Windows the
    registry value Microsoft's library reads them from, and the five files of
    C:\\Windows\\Globalization\\ICU\\ for time zones; elsewhere the two files
    of /usr/share/zoneinfo/ that libstdc++ reads, measured 2026-10-14 -- and
    the Python, CMake, CTest, compiler and linker programs, each with its
    libraries (the linker since decision 583); and the physical memory and
    the count of processors (decision 794), from which memory_pool and
    parallel_tests calculate what they examine.

**A coverage record that is incorrect or missing fails `check`** (decisions
590, 704, 712 and 713): a record that matches no test or two tests, or that
cannot be given to a test; a record that is not complete, of a test that
passed; a test that starts a program with the hook directly and passed with
no complete record of it; a record with a count of flags different from the
hook's table of its program, or with its flags out of line; and each problem
that compiled-code.py finds in a program. A problem at no time causes the
driver to skip a test: the program counts as changed, and a test with a
problem is not stored, so that it runs again. Only a run with --full deletes
the summaries of the objects that no program of the tree links (decision
714).

**Every file is hashed in full on every run** (decision 526): no size and
date are trusted.

**A test runs whenever a test it depends on runs** (decision 508), by CTest's
DEPENDS, which holds its fixtures' setup tests too. CTest is told not to add
a skipped setup test back by itself: a setup test whose fingerprint is
unchanged left its outputs as they were.

**What is written** (decisions 491 and 497), all in the build tree's
`run-tests/`: the fingerprints of the last passing run of each test -- with
the shader files its records named and the application's SHA-256 at that run,
which mutants-due.py reads (decisions 616 and 629); a test skipped as
unchanged gets the SHA-256 of the application at the run that skipped it,
written before CTest starts (decisions 737 and 738) -- the list of tests
skipped, and CTest's own report. One line in `check`'s output says how many
ran and how many were skipped. Where the hook is on, also the summary of each
object with the hook, in `run-tests/objects/` (decision 714), and the kept
table of each program with the hook, in `run-tests/tables/` (decision 715
(1)): the key of the function at each entry of its hook table and the count
of the table, which the driver uses to examine the records of a program with
bytes and a map that did not change, with no read of its map, its .pdb file,
its libraries or its summaries.

**--full** runs every test (decision 484), requires that CTest ran as many as
it lists (decision 537), and, from a clean working tree whose commit did not
change while it ran, writes `run-tests/full-check.json` naming that commit.
**--pre-push**, from scripts/git-hooks/pre-push, refuses a push unless both
`check` trees hold that stamp for exactly the commit being pushed
(decision 503).

**A test that passes without its read record** is an error, where its program
carries the listener that writes one (tests/ReadRecordListener.cpp): that is
a listener lost, or a name escaped differently here and there.

Proven able to fail before it was trusted: --self-test gives the decision
rules made-up tests and fingerprints, and exits non-zero unless each is
judged right -- and, since register decision 602, changes each part's input
and requires the fingerprint to move, and reads what llvm-readobj, ldd, the
clear probe and --list-listeners answered on this project's programs; the
CTest test run_tests_end_to_end runs the driver on a scratch project,
requires exactly the expected tests to run, and pushes through the real
pre-push hook (decision 603); run_tests_in_tree uses the tools again, on the
tree's own programs; and, where the hook is on, run_tests_function_level
(--function-level) builds a scratch project with the hook, with the tools of
the tree, changes it one step at a time, and, at each step, makes sure that
the driver runs the tests that the change affects and no other test, with no
problem (decision 713 (4)).
"""

import argparse
import datetime
import hashlib
import importlib.util
import json
import ntpath
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ElementTree

ROOT = pathlib.Path(__file__).resolve().parent.parent
STATE = "run-tests"
STORE = "fingerprints.json"
SELECTED = "selected.txt"
SKIPPED = "skipped.txt"
REPORT = "results.xml"
FULL_STAMP = "full-check.json"
CARD = "card"
RECORDS = "test-reads"
# The folder ORBSIM_COVERAGE_RECORDS names for the tests this script runs,
# beside test-reads/ (M1-131, decisions 570 and 642): every program carrying
# the coverage hook writes its .functions.txt there, which M1-132 reads, and
# the application its .shaders.txt (decision 629).
COVERAGE_RECORDS = "coverage-records"
RECORDS_VARIABLE = "ORBSIM_COVERAGE_RECORDS"
SHADER_RECORD = ".shaders.txt"
# What a test keeps when one of its shader records is not whole -- an abort or
# a crash before main() wrote `end` (decision 637): every shader, whether or
# not it names the application, since what that run asked for is not known.
EVERY_SHADER = "every"
# Raised whenever what the store holds, or how a part is computed, changes:
# a store of another version is not read, so every test runs once. 2: the
# shaders a test's records name, and the application's hash (M1-131). 3: the
# programs that a test takes function by function, and the table of function
# names of each program (M1-132, decision 709 (3)). 4: the key `listings` of
# a test with the records of its Python processes (M1-138, decision 801 (1)).
# 5: the key `trace` of a `cmake -P` test with a complete trace (decision 809
# (1)). 6: the key `children` of a Python test with children of rules 2 and 5
# (decision 817 (1)).
STORE_VERSION = 6
# The coverage recorder's records, next to the application's shader records
# (tests/CoverageRecorder.cpp, decision 644), and the first three lines of
# each, which say which test it belongs to (decision 564).
FUNCTION_RECORD = ".functions.txt"
RECORD_HEAD = ("program", "command", "test")
# The variable that names a test to the programs that it starts (decision
# 564): CMakeLists.txt sets it for each test that it defines.
TEST_NAME_VARIABLE = "ORBSIM_TEST_NAME"
# The seven CMake scripts in cmake/ that only start a program and read its
# output (decision 703): the function part of each program that the records
# of such a test name. A script that is added after this stays on the safe
# side, by the bytes of its programs, until it is added here.
LISTED_SCRIPTS = ("RunProbe.cmake", "RunUsage.cmake", "VerifyAssertionProbe.cmake", "VerifyAssertionMessage.cmake",
                  "RunBench.cmake", "VerifyMissingShader.cmake", "VerifyReadRecord.cmake")
# The folder in run-tests/ with the summary of each object with the hook
# (decisions 709 (2) and 714), the folder with the kept table of each program
# with the hook (decision 715 (1)), and the key at the top of the store with
# the table of function names of each program (decision 709 (3)).
OBJECTS = "objects"
TABLES = "tables"
FUNCTIONS = "functions"
LISTENER = "RecordWhatEachCaseRead"
APPLICATION = "orbsim"
GPU_LABEL = "gpu"
# What a test run through cmake or python is fingerprinted on (decision 531).
SCRIPT_INPUTS = ("scripts", "cmake", "CMakeLists.txt", "CMakePresets.json")
# The build files of decisions 489 and 505, as decision 531 names them.
BUILD_FILES = ("CMakeLists.txt", "CMakePresets.json", "cmake")
# What the cautious rule reruns a test on (decisions 489 and 505).
CAUTIOUS = ("data", "tests/golden") + BUILD_FILES
# What a test naming the build tree reads besides the repository (decision 500).
TREE_FILES = ("build.ninja", "CMakeFiles/rules.ninja", ".ninja_deps")
# The time-zone data, per system (decisions 509 and 533). Linux's two were
# measured on 2026-10-14 by logging every open of test_time_leap under
# libstdc++ 14: tzdata.zi and leapseconds, and nothing else of the folder;
# TZDIR is not consulted.
TIME_ZONES_WINDOWS = pathlib.Path(r"C:\Windows\Globalization\ICU")
TIME_ZONES_ELSEWHERE = (pathlib.Path("/usr/share/zoneinfo/tzdata.zi"),
                        pathlib.Path("/usr/share/zoneinfo/leapseconds"))
# Where Microsoft's standard library reads the leap seconds Windows adds to
# its own table: tzdb.cpp's __std_tzdb_get_leap_seconds, in the 14.51.36231
# sources this project builds with, reads this value and no file -- and
# test_time_leap, compared with Process Monitor on 2026-10-16, opened no file
# under the ICU folder (register decision 615).
LEAP_SECONDS_KEY = r"SYSTEM\CurrentControlSet\Control\LeapSecondInformation"
LEAP_SECONDS_VALUE = "LeapSeconds"
# The clear probe that names the card runs in about 4.4 s (decision 502); an
# order of magnitude clear of that, as mutants-due.py allows.
CARD_PROBE_SECONDS = 120
# The sidecar lines that name the card, its driver and the Vulkan versions.
CARD_LINES = ("gpu", "gpu.vendor", "gpu.device", "driver.name", "driver.info", "driver.version",
              "vulkan.api", "validation.layer")
MISSING = "missing"
# The records of the Python processes of a test (M1-138, decisions 765, 781 and
# 801 (2)): scripts/read-record/sitecustomize.py writes them into test-reads/,
# with the records of the C++ tests, as `<escaped name>.py-<process>-<start>.txt`.
# The head line of a record; the stamp of a file that the hook could not read
# (decision 792); the folder of the hook, which the driver puts first on
# PYTHONPATH (decision 771), and its file, which each record of this
# repository's hook names (decision 793); and the two variables of the hook:
# the folder of the records of the tree, and the outer tests of a driver in a
# test (decisions 781 and 783).
PYTHON_RECORD_HEAD = "python-read-record 1"
PYTHON_RECORD = re.compile(r"(.+)\.py-[0-9]+-[0-9]+\.txt")
UNREADABLE = "unreadable"
HOOK_FOLDER = ("scripts", "read-record")
HOOK_FILE = "sitecustomize.py"
READ_RECORDS_VARIABLE = "ORBSIM_READ_RECORDS"
OWNER_VARIABLE = "ORBSIM_READ_RECORD_OWNER"
# The trace of a `cmake -P` test (M1-138, decisions 768, 784 and 803-809):
# `<escaped name>.trace.json` in test-reads/, which the arguments of
# cmake/TraceArguments.cmake name. The commands of the scripts of these
# tests, as section 6 of docs/measurements/m1-138-spike.md lists them, and a
# function that the same trace defines (decision 805).
TRACE = ".trace.json"
TRACE_COMMANDS = frozenset(("if", "elseif", "else", "endif", "string", "list", "math", "set", "file", "foreach",
                            "endforeach", "continue", "return", "message", "execute_process",
                            "get_filename_component", "function", "endfunction"))
# The subcommands of file() that read a file, and those that read none.
TRACE_FILE_READS = frozenset(("READ", "STRINGS", "SHA256", "SIZE"))
TRACE_FILE_NO_READS = frozenset(("TO_CMAKE_PATH", "REMOVE", "REMOVE_RECURSE", "WRITE", "MAKE_DIRECTORY"))
# The operators of if() that examine a file. EXISTS is a read; each other
# keeps the rules before M1-138, and IS_NEWER_THAN compares dates, which
# decision 526 does not trust.
TRACE_FILE_QUESTIONS = frozenset(("EXISTS", "IS_DIRECTORY", "IS_NEWER_THAN", "IS_READABLE", "IS_WRITABLE",
                                  "IS_EXECUTABLE", "IS_SYMLINK"))
# The keywords of execute_process() that the scripts use, measured in the
# traces of the spike; they name no file. Each other keyword keeps the rules
# before M1-138: WORKING_DIRECTORY or INPUT_FILE, for example, change what a
# child reads.
TRACE_PROCESS_KEYWORDS = frozenset(("RESULT_VARIABLE", "OUTPUT_VARIABLE", "ERROR_VARIABLE"))
# Each keyword of execute_process() in the documentation of CMake 4.3, so that
# a trace is read as CMake reads the arguments: a keyword ends a COMMAND.
EXECUTE_PROCESS_KEYWORDS = frozenset((
    "WORKING_DIRECTORY", "TIMEOUT", "RESULT_VARIABLE", "RESULTS_VARIABLE", "OUTPUT_VARIABLE", "ERROR_VARIABLE",
    "INPUT_FILE", "OUTPUT_FILE", "ERROR_FILE", "OUTPUT_QUIET", "ERROR_QUIET", "COMMAND_ECHO",
    "OUTPUT_STRIP_TRAILING_WHITESPACE", "ERROR_STRIP_TRAILING_WHITESPACE", "ENCODING", "ECHO_OUTPUT_VARIABLE",
    "ECHO_ERROR_VARIABLE", "COMMAND_ERROR_IS_FATAL"))
# Whether a Windows program's libraries are followed through the libraries
# Windows loads only when a function in them is first called, as well as the
# ones it loads at the start: not (register decision 539). None of this
# project's programs loads one that way, only Windows' own do, and following
# them took all programs from 41 libraries to 253 (measured 2026-10-14).
FOLLOW_DELAY_LOADED = False


# --- small pieces ----------------------------------------------------------------

def norm(path) -> str:
    text = os.path.abspath(str(path)).replace("\\", "/")
    return text.lower() if os.name == "nt" else text


def under(path: str, folder: str) -> bool:
    """Whether a normalised path is `folder` or lies inside it."""
    return path == folder or path.startswith(folder.rstrip("/") + "/")


def digest(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def escaped_name(name: str) -> str:
    """A test's name as its read record's file name (register decision 528):
    each character Windows forbids in a file name, `%` itself, and a last `.`
    or space, as `%` and two uppercase hexadecimal digits. Written from that
    description, as tests/TestReads.cpp is, and held to the same table."""
    out = []
    for i, c in enumerate(name):
        last = i + 1 == len(name)
        if ord(c) < 0x20 or c in '<>:"/\\|?*%' or (last and c in ". "):
            out.extend(f"%{b:02X}" for b in c.encode("utf-8"))
        else:
            out.append(c)
    return "".join(out)


def parse_record(text: str):
    """[(hash, path)] from a read record, or None where it is not whole: no
    last line `end`, or a line that is not a hash or `missing`, a space and a
    path."""
    lines = text.split("\n")
    if len(lines) < 2 or lines[-1] != "" or lines[-2] != "end":
        return None
    reads = []
    for line in lines[:-2]:
        stamp, _, path = line.partition(" ")
        if not path or not (stamp == MISSING or re.fullmatch(r"[0-9a-f]{64}", stamp)):
            return None
        reads.append((stamp, path))
    return reads


def parse_shader_record(text: str):
    """(test, [shader file]) from the application's record of the shaders a
    run asked for (decision 644): `test <name>`, a line for each shader file,
    and `end`. The shaders, each path normalised -- the renderer is given the
    folder and the name joined with a backslash, its folder written with
    either separator (decision 659 e) -- or None where the record is not
    whole (decision 637): no last line `end`, cut off inside a line, an empty
    line, or a path that is not absolute, which could not be hashed as the
    file the run read. None altogether where there is no `test` line, so that
    the record names no test."""
    first, newline, _ = text.partition("\n")
    if not newline or not (first == "test" or first.startswith("test ")):
        return None
    lines = text.split("\n")
    shaders = lines[1:-2]
    whole = (len(lines) >= 3 and lines[-1] == "" and lines[-2] == "end" and "end" not in shaders
             and all(s and os.path.isabs(s) for s in shaders))
    return first[len("test "):], ([norm(s) for s in shaders] if whole else None)


def shaders_by_test(texts) -> dict:
    """{test: its shader files, or EVERY_SHADER}, from the texts of one run's
    shader records: the files of every record carrying a test's name
    combined, each once, sorted (decisions 564 and 629) -- a script may start
    the application more than once -- and EVERY_SHADER for a test any of whose
    records is not whole (decision 637). A test that left no record is not
    in it."""
    found = {}
    for text in texts:
        parsed = parse_shader_record(text)
        if parsed is None:
            continue
        test, shaders = parsed
        if shaders is None or found.get(test) == EVERY_SHADER:
            found[test] = EVERY_SHADER
        else:
            found[test] = sorted(set(found.get(test, [])) | set(shaders))
    return found


def renewed_applications(stored: dict, skipped, file_hash, application) -> tuple:
    """(the SHA-256 of `application`, the names of the tests with entries that
    got it): each skipped test with a stored list of shaders gets the hash of the
    application at this run (M1-133, decisions 737 and 738). A skipped test is
    unchanged, thus it loads the shaders of its last pass (ADR 0028), and
    mutants-due.py, which uses a list only while the application is the one
    that the entry names (decisions 616 and 629), can trust the list again. A
    test that runs is not changed here: its entry is written again when it
    passes. No entry changes where there is no application."""
    hashed = file_hash(application)
    if hashed == MISSING:
        return hashed, []
    renewed = []
    for name in skipped:
        entry = stored.get(name)
        if entry and isinstance(entry.get("shaders"), list) and entry.get("application") != hashed:
            entry["application"] = hashed
            renewed.append(name)
    return hashed, renewed


def shader_record_texts(folder: pathlib.Path) -> tuple:
    """([text], [(record, why)]): every shader record in `folder`, and those
    that could not be read. The coverage recorder's records beside them are
    M1-132's to read."""
    texts, unreadable = [], []
    for path in sorted(folder.glob("*" + SHADER_RECORD)):
        try:
            texts.append(path.read_bytes().decode("utf-8", errors="replace"))
        except OSError as error:
            unreadable.append((path, error))
    return texts, unreadable


def function_record_texts(folder: pathlib.Path) -> tuple:
    """([(record, text)], [(record, why)]): each coverage record of the
    recorder in `folder` (M1-132), and those that could not be read. Read as
    the recorder writes them, in UTF-8; a byte that is not UTF-8 stays as a
    character that no path or test name holds."""
    found, unreadable = [], []
    for path in sorted(folder.glob("*" + FUNCTION_RECORD)):
        try:
            found.append((path, path.read_bytes().decode("utf-8", errors="replace")))
        except OSError as error:
            unreadable.append((path, error))
    return found, unreadable


def tree_programs(tree: pathlib.Path) -> list:
    """The programs in the folder of the build tree, where CMake puts each
    program that CMakeLists.txt defines. Only these have the hook:
    CMakeLists.txt gives the hook only to the targets that it defines. The
    programs in a folder below it are CMake's checks of the compilers and the
    programs of the dependencies, with no hook (measured 2026-10-21 in
    build/debug: 57 programs in the folder, and 4 below it)."""
    return sorted(p for p in tree.iterdir() if p.is_file() and is_program(p)) if tree.is_dir() else []


def empty_folder(folder: pathlib.Path) -> list:
    """`folder` emptied, or made: what is left in it -- a record a program
    still holds open, on Windows -- which would be read as this run's."""
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True, exist_ok=True)
    return sorted(folder.iterdir())


def properties(test: dict) -> dict:
    return {p["name"]: p["value"] for p in test.get("properties", [])}


def labels(test: dict) -> list:
    return properties(test).get("LABELS", [])


def depends(test: dict) -> list:
    return properties(test).get("DEPENDS", [])


def definition(test: dict) -> str:
    """CTest's description of a test: its command and properties -- not the
    line of CMakeLists.txt it was declared on, which moves with every edit
    above it."""
    return digest(json.dumps({k: v for k, v in test.items() if k != "backtrace"}, sort_keys=True))


def environment(env: dict) -> str:
    """The variables a test's result can rest on (decision 501): PATH, every
    VK_* and ORBSIM_*, and PYTHONPATH (decision 796), which can change the
    module that a script imports. Names compared without case on Windows, as
    Windows does. ORBSIM_TEST_NAME is not one of them (decision 715 (2)): the
    driver does not give it to CTest, and each test that uses it gets it from
    its own definition, which is a part of its own. ORBSIM_READ_RECORD_OWNER
    is also not one of them (decision 781): it names the outer tests of a
    driver in a test, which do not change what a test does."""
    fold = (lambda n: n.upper()) if os.name == "nt" else (lambda n: n)
    kept = {fold(n): v for n, v in env.items()
            if (fold(n) in ("PATH", "PYTHONPATH") or fold(n).startswith(("VK_", "ORBSIM_")))
            and fold(n) not in (TEST_NAME_VARIABLE, OWNER_VARIABLE)}
    return digest(json.dumps(kept, sort_keys=True))


def with_hook(env: dict, tree: pathlib.Path, root: pathlib.Path) -> dict:
    """`env` with the hook of the records of the Python processes (M1-138,
    decisions 771 and 781): ORBSIM_READ_RECORDS names test-reads/ of the tree,
    and the folder of the hook comes first on PYTHONPATH, before the folders
    that PYTHONPATH held. ORBSIM_READ_RECORD_OWNER stays: a driver in a test
    gives the outer tests to the tests that it runs (decision 783). Names
    compared without case on Windows, as Windows does."""
    fold = (lambda n: n.upper()) if os.name == "nt" else (lambda n: n)
    before = [value for name, value in env.items() if fold(name) == "PYTHONPATH" and value]
    kept = {name: value for name, value in env.items() if fold(name) not in ("PYTHONPATH", READ_RECORDS_VARIABLE)}
    return {**kept, READ_RECORDS_VARIABLE: str(tree / RECORDS),
            "PYTHONPATH": os.pathsep.join([str(root.joinpath(*HOOK_FOLDER))] + before)}


def without_test_name(env: dict) -> dict:
    """`env` without ORBSIM_TEST_NAME, the environment that the driver gives
    CTest (decision 715 (2)). If the shell that starts `check` sets it, each
    Catch2 test gets it, the records of the test name that test, and `check`
    fails. Names compared without case on Windows, as Windows does."""
    fold = (lambda n: n.upper()) if os.name == "nt" else (lambda n: n)
    return {name: value for name, value in env.items() if fold(name) != TEST_NAME_VARIABLE}


def is_program(path: pathlib.Path) -> bool:
    """On Windows only an .exe is -- Windows has no execute bit (mutants-due.py
    says what treating every file as one did); elsewhere the execute bit."""
    if os.name == "nt":
        return path.suffix.lower() == ".exe"
    return path.is_file() and os.access(path, os.X_OK)


def leap_seconds_value() -> str:
    """The registry value Windows' leap seconds are read from, as text for a
    fingerprint: its type and its bytes, `missing` where there is none, and
    nothing at all off Windows, where libstdc++ reads the files in
    TIME_ZONES_ELSEWHERE instead."""
    if os.name != "nt":
        return ""
    import winreg  # Windows only, and only here
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, LEAP_SECONDS_KEY) as key:
            value, kind = winreg.QueryValueEx(key, LEAP_SECONDS_VALUE)
    except OSError:
        return MISSING
    return f"{kind} {bytes(value or b'').hex()}"


def machine_capacity() -> str:
    """The physical memory and the count of processors of this machine, as text
    for the machine part (decision 794): memory_pool and parallel_tests
    calculate what they examine from them, and no record can show them. The
    memory from physical_memory_mib() of scripts/check-memory-pool.py itself,
    so that the two cannot read it differently; the processors from
    os.cpu_count(), as scripts/check-parallel-tests.py reads them."""
    memory = script_module("check-memory-pool").physical_memory_mib()
    return f"memory {memory} MiB, processors {os.cpu_count()}"


def cache_value(tree: pathlib.Path, key: str):
    try:
        for line in (tree / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith(key + ":"):
                return line.split("=", 1)[1]
    except OSError:
        pass
    return None


def parse_python_record(text: str):
    """The record of one Python process (M1-138, decisions 765 and 782), as
    {"argv", "cwd", "reads": [(stamp, path)], "listings": [(stamp, path)],
    "children": [JSON]}; or None where it is not complete: not the head line,
    then `argv` with a JSON list of text and `cwd`, then only `read`, `list`
    and `child` lines, and the last line `end` with its newline. A stamp is a
    SHA-256, `missing` or `unreadable`."""
    lines = text.split("\n")
    if len(lines) < 5 or lines[0] != PYTHON_RECORD_HEAD or lines[-2:] != ["end", ""]:
        return None
    if not lines[1].startswith("argv ") or not lines[2].startswith("cwd "):
        return None
    try:
        argv = json.loads(lines[1][len("argv "):])
    except ValueError:
        return None
    if not isinstance(argv, list) or not all(isinstance(a, str) for a in argv):
        return None
    found = {"argv": argv, "cwd": lines[2][len("cwd "):], "reads": [], "listings": [], "children": []}
    for line in lines[3:-2]:
        kind, _, rest = line.partition(" ")
        if kind in ("read", "list"):
            stamp, _, path = rest.partition(" ")
            if not path or not (stamp in (MISSING, UNREADABLE) or re.fullmatch(r"[0-9a-f]{64}", stamp)):
                return None
            found["reads" if kind == "read" else "listings"].append((stamp, path))
        elif kind == "child":
            try:
                found["children"].append(json.loads(rest))
            except ValueError:
                return None
        else:
            return None
    return found


def python_reads(texts: list, command: list, hook: str):
    """(reads, listings, children) for the store, from the texts of the records
    of the Python processes of one test: each read and each listing one time,
    sorted, and each child line one time, which child_rules examines (decision
    778); or None, and
    then the test keeps the rules of decisions 500, 510 and 531 (decision 801
    (5)): there is no record; a record could not be read (None) or is not
    complete (decision 782); no record is of the interpreter of the test's own
    script, with an argv that, without its first item, is the command of the
    test without its first item, because the launcher of PyManager starts the
    interpreter with the same arguments (decision 801 (7)); a record has no
    `read` line for `hook`, the hook of this repository, thus a copy of the
    hook in a different repository wrote it (decision 793)."""
    parsed = [None if text is None else parse_python_record(text) for text in texts]
    if not parsed or not command or any(record is None for record in parsed):
        return None
    if not any(record["argv"] and record["argv"][1:] == command[1:] for record in parsed):
        return None
    if not all(any(norm(path) == norm(hook) for _, path in record["reads"]) for record in parsed):
        return None
    children = []
    for record in parsed:
        children += [child for child in record["children"] if child not in children]
    return (sorted({line for record in parsed for line in record["reads"]}),
            sorted({line for record in parsed for line in record["listings"]}), children)


def child_rules(lines: list, tree: str, root: str):
    """[{"kind", "program", "paths"}] for the store, each child one time,
    sorted, from the `child` lines of the records of one test (decisions 778
    and 811-817); or None, and then the test keeps the rules of decisions
    500, 510 and 531. `paths` holds each path on the command line of the
    child, also after `=`, which counts as decision 788 says.
      - A child with its working folder not in the repository or the tree is
        of rule 5: "program".
      - A child with the arguments `--show-only=json-v1` and no other, a
        program named ctest and its working folder the tree, is of rule 2:
        "ctest" (decision 815).
      - None for each other child (rule 6): also for a child that os.system,
        startfile, spawn, exec or posix_spawn started (decision 817 (3)), one
        with a program that is not an absolute path (decision 812), and one
        with a path of the repository or the tree in its arguments (decision
        813). The command line of a child of subprocess is one text on
        Windows, which split_command_line makes into its arguments by the
        rules of the C runtime (decision 817 (2))."""
    tree_key, root_key = norm(tree), norm(root)
    inside = lambda path: under(norm(path), tree_key) or under(norm(path), root_key)  # noqa: E731
    found = {}
    for line in lines:
        if not isinstance(line, dict) or "event" in line or not isinstance(line.get("cwd"), str):
            return None
        argv = line.get("argv")
        if isinstance(argv, str) and os.name == "nt":
            argv = split_command_line(argv)
        if not isinstance(argv, list) or not argv or not all(isinstance(a, str) for a in argv):
            return None
        if not os.path.isabs(argv[0]):
            return None
        paths = set()
        for arg in argv[1:]:
            for part in (arg, arg.split("=", 1)[1] if "=" in arg else None):
                if part and os.path.isabs(part):
                    if inside(part):
                        return None
                    paths.add(os.path.normpath(part))
        if not inside(line["cwd"]):
            kind = "program"
        elif (os.path.basename(argv[0]).lower() in ("ctest", "ctest.exe") and argv[1:] == ["--show-only=json-v1"]
              and norm(line["cwd"]) == tree_key):
            kind = "ctest"
        else:
            return None
        child = {"kind": kind, "program": argv[0], "paths": sorted(paths)}
        found[(kind, norm(argv[0]), tuple(child["paths"]))] = child
    return [found[key] for key in sorted(found)]


def python_record_files(folder: pathlib.Path, names) -> dict:
    """{test: [record file]} for the tests `names`: each file in `folder` that
    PYTHON_RECORD gives to one of them, by all of its file name (decision 801
    (2)) -- `a` and `a.py-1` are two test names that start with the same text.
    The pattern gives the longest name before the last `.py-<digits>-<digits>`,
    which the hook adds."""
    by_escape = {escaped_name(name): name for name in names}
    try:
        entries = sorted(folder.iterdir())
    except OSError:
        return {}
    found = {}
    for path in entries:
        match = PYTHON_RECORD.fullmatch(path.name)
        if match and match.group(1) in by_escape:
            found.setdefault(by_escape[match.group(1)], []).append(path)
    return found


def trace_test(command: list, cmake) -> bool:
    """Examine if a test runs a script with the tree's CMake and `-P`, and
    thus has a trace (decision 784). Paths compared as Windows compares paths."""
    return (bool(command) and bool(cmake) and windows_path(command[0]) == windows_path(str(cmake))
            and "-P" in command[1:])


def _trace_entries(text: str):
    """The entries of a trace, or None where it is not complete (decision 804):
    the first line is not the version line with 1 in its field `major`, or a
    different line is not an entry with `cmd`, `args` as text and `file`."""
    lines = text.split("\n")
    if len(lines) < 2 or lines[-1] != "":
        return None
    try:
        version = json.loads(lines[0]).get("version")
        entries = [json.loads(line) for line in lines[1:-1]]
    except (ValueError, AttributeError):
        return None
    if not isinstance(version, dict) or version.get("major") != 1:
        return None
    for entry in entries:
        if not (isinstance(entry, dict) and isinstance(entry.get("cmd"), str) and isinstance(entry.get("file"), str)
                and isinstance(entry.get("args"), list) and all(isinstance(a, str) for a in entry["args"])):
            return None
    return entries


def _glob_patterns(args: list) -> list:
    """The patterns of file(GLOB <variable> ...), read as CMake reads them: the
    keyword LIST_DIRECTORIES and RELATIVE each with its value, and
    CONFIGURE_DEPENDS, are not patterns."""
    patterns, skip = [], False
    for arg in args:
        if skip:
            skip = False
        elif arg in ("LIST_DIRECTORIES", "RELATIVE"):
            skip = True
        elif arg != "CONFIGURE_DEPENDS":
            patterns.append(arg)
    return patterns


def _child_known(args: list, named: set, tree: str) -> bool:
    """Decisions 775, 803 and 806, for the arguments of one execute_process(),
    which are read as CMake reads them, by its list of keywords: each COMMAND
    has a program that the command line of the test names; each argument of
    a child that is a path, also after `=`, is in the build tree or named on
    that command line; and each other keyword is one that the scripts use,
    which names no file."""
    commands, group, value = 0, None, False
    for arg in args:
        if value:
            value = False
        elif arg == "COMMAND":
            if group == []:
                return False
            commands, group = commands + 1, []
        elif arg in EXECUTE_PROCESS_KEYWORDS:
            if arg not in TRACE_PROCESS_KEYWORDS or group == []:
                return False
            group, value = None, True
        elif group is not None:
            if not group and norm(arg) not in named:
                return False
            for part in (arg, arg.split("=", 1)[1] if "=" in arg else None):
                if part and os.path.isabs(part) and not under(norm(part), tree) and norm(part) not in named:
                    return False
            group.append(arg)
    return commands > 0 and group != []


def trace_inputs(text: str, command: list, named: set, cwd: str, tree: str):
    """([read path], [folder of a glob]) from the trace of a `cmake -P`
    test, each one time, sorted, and none in the build tree (decision 797);
    or None, and then the test keeps the rules of decisions 500, 510 and 531 (decisions
    805 and 806). `named` holds each path on the command line, as norm gives
    it; a relative path counts from `cwd`, the working folder of the test.
      - A read: the `file` field of each entry, file(READ|STRINGS|SHA256|SIZE),
        if() or elseif() with EXISTS, and get_filename_component(REALPATH).
      - A listing: file(GLOB) with a wildcard in its last part only.
      - None for each other command, but a function that the trace defined
        before; for each other file subcommand, GLOB_RECURSE and a wildcard
        in a folder; for each other operator of if() about a file, and
        get_filename_component(PROGRAM); and for a child of execute_process
        that _child_known refuses."""
    entries = _trace_entries(text)
    if entries is None or not command:
        return None
    tree_key = norm(tree)
    reads, folders, defined = set(), set(), set()

    def resolved(path):
        return os.path.normpath(path if os.path.isabs(path) else os.path.join(cwd, path))

    for entry in entries:
        cmd, args = entry["cmd"].lower(), entry["args"]
        if cmd not in TRACE_COMMANDS and cmd not in defined:
            return None
        reads.add(resolved(entry["file"]))
        if cmd == "function" and args:
            defined.add(args[0].lower())
        elif cmd == "file":
            if not args:
                return None
            if args[0] in TRACE_FILE_READS:
                if len(args) < 2:
                    return None
                reads.add(resolved(args[1]))
            elif args[0] == "GLOB":
                patterns = _glob_patterns(args[2:])
                if not patterns:
                    return None
                for pattern in patterns:
                    folder, _ = os.path.split(resolved(pattern))
                    if any(c in folder for c in "*?["):
                        return None
                    folders.add(folder)
            elif args[0] not in TRACE_FILE_NO_READS:
                return None
        elif cmd in ("if", "elseif"):
            for at, arg in enumerate(args):
                if arg in TRACE_FILE_QUESTIONS:
                    if arg != "EXISTS" or at + 1 == len(args):
                        return None
                    reads.add(resolved(args[at + 1]))
        elif cmd == "get_filename_component" and len(args) >= 3:
            if args[2] == "PROGRAM":
                return None
            if args[2] == "REALPATH":
                reads.add(resolved(args[1]))
        elif cmd == "execute_process" and not _child_known(args, named, tree_key):
            return None
    keep = lambda paths: sorted(p for p in paths if not under(norm(p), tree_key))  # noqa: E731
    return keep(reads), keep(folders)


# --- the decision, pure ------------------------------------------------------------

def unchanged(stored, parts: dict, current_hash, programs_unchanged=None, current_line=None) -> bool:
    """Examine if a test can be skipped: stored at a passing run, each part as
    it was, each file that its record names with the hash that it had when
    the test read it, and each program that the test takes function by
    function unchanged by the rule of decision 706, which
    `programs_unchanged` examines. A stored test with such programs and no
    such function counts as changed. For a test with the records of its
    Python processes, which the key `listings` shows (decision 801 (1)), each
    `read` line and each `list` line must be the line that `current_line`
    gives for its path at this time, as the hook writes it (decision 792),
    and a line with `unreadable` is at no time unchanged; a stored test with
    such lines and no `current_line` counts as changed."""
    if stored is None or stored.get("parts") != parts:
        return False
    if stored.get("programs") and not (programs_unchanged is not None and programs_unchanged(stored["programs"])):
        return False
    if "listings" in stored:
        if current_line is None:
            return False
        lines = ([("read", stamp, path) for stamp, path in stored.get("reads") or []]
                 + [("list", stamp, path) for stamp, path in stored["listings"]])
        return all(stamp != UNREADABLE and current_line(kind, path) == (kind, stamp) for kind, stamp, path in lines)
    return all(current_hash(path) == stamp for stamp, path in stored.get("reads") or [])


def with_dependents(chosen: set, tests: dict) -> set:
    """`chosen`, and every test that depends on one of them, to a fixed point
    (decision 508)."""
    chosen = set(chosen)
    grew = True
    while grew:
        grew = False
        for name, test in tests.items():
            if name not in chosen and any(d in chosen for d in depends(test)):
                chosen.add(name)
                grew = True
    return chosen


def selection(tests: dict, store: dict, parts: dict, current_hash, forced: set, programs_unchanged=None,
              current_line=None) -> set:
    """The tests to run: each test that is not unchanged, each forced test,
    and each test with DEPENDS on one of them. The driver computes no digest
    for a forced test."""
    chosen = {name for name in tests
              if name in forced or not unchanged(store.get(name), parts[name], current_hash, programs_unchanged,
                                                 current_line)}
    return with_dependents(chosen, tests)


def cautious(reads, root: pathlib.Path) -> bool:
    """Decision 505: no complete record, or one naming a file under data/,
    tests/golden/ or the build files."""
    if reads is None:
        return True
    folders = [norm(root / f) for f in CAUTIOUS]
    return any(under(norm(path), f) for _, path in reads for f in folders)


def outcomes(report: str) -> dict:
    """{test: "passed" | "failed" | "skipped"} from CTest's JUnit report. A
    test passed only where CTest ran it and it neither failed nor skipped
    itself."""
    found = {}
    for case in ElementTree.fromstring(report).iter("testcase"):
        children = {child.tag for child in case}
        if case.get("status") == "run" and not children & {"failure", "error", "skipped"}:
            found[case.get("name")] = "passed"
        elif "skipped" in children or case.get("status") in ("notrun", "disabled"):
            found[case.get("name")] = "skipped"
        else:
            found[case.get("name")] = "failed"
    return found


def full_run_problems(listed: set, ran: set, clean: tuple, heads: tuple) -> list:
    """Why a full run may not stamp its commit (decisions 503, 535 and 537):
    it ran other than every test CTest lists; the working tree was not clean
    at its start, or changed before its end -- `clean` is (at the start, at
    the end); or the commit moved -- `heads` is (at the start, at the end)."""
    problems = []
    if ran != listed:
        missing = sorted(listed - ran)
        problems.append(f"the full run ran {len(ran)} of the {len(listed)} tests CTest lists"
                        + (f"; not run: {', '.join(missing[:5])}" if missing else ""))
    if not clean[0]:
        problems.append("the working tree was not clean when the full run started: commit or remove "
                        "every change, untracked files included, and run it again")
    elif not clean[1]:
        problems.append("the working tree changed while the full run ran")
    if heads[0] != heads[1]:
        problems.append("the commit changed while the full run ran")
    return problems


def edited_after_build(times: dict, log_time):
    """The files newer than Ninja's log, newest first, or every file where
    there is no log (decision 542). Ninja writes the log as each step
    finishes, so a file newer than it was changed after the build that
    `check-full` ran -- perhaps changed and committed, which a clean working
    tree cannot show."""
    if log_time is None:
        return sorted(times)
    return sorted((name for name, when in times.items() if when > log_time), key=lambda n: -times[n])


def unrecorded(passed: list, recorded: dict, carries: dict) -> list:
    """The tests that passed without a whole read record, though their program
    carries the listener that writes one: a listener lost, or a name escaped
    differently here and in tests/TestReads.cpp. `recorded` and `carries` map
    each test to whether it left a record and whether its program has the
    listener."""
    return [name for name in passed if carries.get(name) and not recorded.get(name)]


def push_problems(lines: list, stamps: dict) -> list:
    """Why a push must be refused (decision 503), from git's pre-push lines
    and {tree: stamp or None}; empty if it may go ahead."""
    problems = []
    for line in lines:
        fields = line.split()
        if len(fields) != 4 or set(fields[1]) == {"0"}:
            continue
        commit = fields[1]
        for tree, stamp in sorted(stamps.items()):
            if stamp is None:
                problems.append(f"{tree}: no full check has passed there")
            elif stamp.get("commit") != commit:
                problems.append(f"{tree}: the last full check passed at {stamp.get('commit')}, "
                                f"not at {commit}, the commit being pushed")
    return problems


# --- the programs taken function by function, pure (M1-132) ---------------------

_SCRIPTS = {}


def script_module(name: str):
    """A script of scripts/, loaded one time, as scripts/mutate.py loads
    scripts/mutants-due.py: compiled-code.py, for the part of each program
    with the hook (decision 713 (4)), and check-coverage-recorder.py, for
    read_record, the parser of the coverage records (decision 644). Loaded
    only when a function uses it: the copy of this script that the
    end-to-end test puts next to the pre-push hook has no scripts next to it."""
    if name not in _SCRIPTS:
        sys.dont_write_bytecode = True  # no __pycache__ in scripts/
        loader = importlib.util.spec_from_file_location(name.replace("-", "_"), ROOT / "scripts" / f"{name}.py")
        module = importlib.util.module_from_spec(loader)
        loader.loader.exec_module(module)
        _SCRIPTS[name] = module
    return _SCRIPTS[name]


def holds_hook_table(data) -> bool:
    """Examine if the bytes of a program hold the hook's address table: then
    the program has the hook (decision 584). False for no bytes, and for
    bytes of no format that compiled-code.py reads."""
    if data is None:
        return False
    code = script_module("compiled-code")
    found = code.sections(data)
    return found is not None and code.carries(*found)[1]


def split_command_line(text: str) -> list:
    """The arguments of a Windows command line, as the Microsoft C runtime
    gives them to main: parse_command_line in the Universal C runtime's
    argv_parsing.cpp, and Microsoft's "Parsing C++ command-line arguments".
    The first argument, the program, ends at the first space or tab not in
    quotes; a quote only starts or ends quotes, and a backslash is a
    character. In each other argument, 2n backslashes and a quote give n
    backslashes and start or end quotes; 2n + 1 backslashes and a quote give
    n backslashes and a quote; a backslash before a different character is a
    character; two quotes in quotes give one quote. A command line that ends
    in quotes ends its last argument. The recorder writes the command line of
    its program as Windows gives it (GetCommandLineW), and CTest starts a
    test with the quotes of libuv, which this function removes
    (decision 704)."""
    arguments, at, end = [], 0, len(text)
    program, quoted = [], False
    while at < end and (quoted or text[at] not in " \t"):
        if text[at] == '"':
            quoted = not quoted
        else:
            program.append(text[at])
        at += 1
    arguments.append("".join(program))
    while True:
        while at < end and text[at] in " \t":
            at += 1
        if at == end:
            return arguments
        argument, quoted = [], False
        while True:
            slashes = 0
            while at < end and text[at] == "\\":
                slashes, at = slashes + 1, at + 1
            copy = True
            if at < end and text[at] == '"':
                if slashes % 2 == 0:
                    if quoted and at + 1 < end and text[at + 1] == '"':
                        at += 1
                    else:
                        copy, quoted = False, not quoted
                slashes //= 2
            argument.append("\\" * slashes)
            if at == end or (not quoted and text[at] in " \t"):
                break
            if copy:
                argument.append(text[at])
            at += 1
        arguments.append("".join(argument))


def windows_path(text: str) -> str:
    """A path as Windows compares two paths: `/` and `\\` are the same
    separator, and the case of the letters is not important, as in
    compiled-code.py's object_key (decision 710 (2)). The same rule on each
    machine, so that the self-test gives one result on Linux too."""
    return ntpath.normpath(text).casefold()


def same_command(arguments: list, command: list) -> bool:
    """Examine if the arguments of a record's command line are the command
    that CTest lists for a test: the first argument as a path, as Windows
    compares paths -- CTest lists it with `/`, and Windows gives it as CTest
    gave it -- and each other argument with the same text."""
    return (bool(command) and len(arguments) == len(command)
            and windows_path(arguments[0]) == windows_path(command[0]) and arguments[1:] == command[1:])


def record_head(text: str):
    """(program, command, test) from the first three lines of a coverage
    record, or None. Only for a record that read_record refuses, so that the
    driver can name its test (decisions 590 and 713 (2)): read_record, in
    scripts/check-coverage-recorder.py, is the parser of the records
    (decision 644), and this function reads no other line."""
    lines = text.split("\n")
    if len(lines) <= len(RECORD_HEAD):
        return None
    head = []
    for key, line in zip(RECORD_HEAD, lines):
        if line != key and not line.startswith(key + " "):
            return None
        head.append(line[len(key) + 1:])
    return tuple(head)


def record_owner(head: tuple, ran: dict) -> tuple:
    """(test, problem): the test of this run that a coverage record belongs to
    (decisions 564 and 704). A record with a test name belongs to that test;
    a record with an empty test name belongs to the test with the same
    command line. A record that matches no test of this run, or more than
    one, has no test, and the problem names its program and its command line."""
    program, command, test = head
    if test:
        found = [test] if test in ran else []
    else:
        arguments = split_command_line(command)
        found = [name for name, listed in ran.items() if same_command(arguments, listed.get("command") or [])]
    if len(found) == 1:
        return found[0], None
    named = f", for the test {test!r}," if test else ""
    return None, (f"a coverage record of {program}{named} with the command line {command!r} matches "
                  f"{len(found)} tests of this run, not one (decision 704)")


def assign_records(found: list, ran: dict, passed: set) -> tuple:
    """({test: [Record]}, {test: [problem]}, [problem]) from the coverage
    records of one run, [(path, text)]: the complete records of each test,
    which read_record gives; a problem for each record that is not complete,
    of a test that passed (decisions 590 and 713 (2)), which names the record
    and the test; and the problems of the records that cannot be given to a
    test, which belong to no test (decision 704). A record that is not
    complete, of a test that did not pass, is no problem: such a test is not
    stored."""
    read_record = script_module("check-coverage-recorder").read_record
    records, problems, loose = {}, {}, []
    for path, text in found:
        record = read_record(text)
        head = record_head(text) if record is None else (record.program, record.command, record.test)
        if head is None:
            loose.append(f"{path}: a coverage record that cannot be given to a test: its first lines do not name "
                         "its program, its command line and its test (decision 704)")
            continue
        owner, problem = record_owner(head, ran)
        if owner is None:
            loose.append(f"{path}: {problem}")
        elif record is None:
            if owner in passed:
                problems.setdefault(owner, []).append(
                    f"{path}: its coverage record is not complete, and the test passed (decisions 590 and 713 (2))")
        else:
            records.setdefault(owner, []).append(record)
    return records, problems, loose


def flag_problems(record, entries, problem) -> list:
    """The problems of one complete record against the hook's table of its
    program, of `entries` entries, or the `problem` that gives why the table
    was not counted (decision 712): the flags out of line (decision 590), or
    a count of flags different from the table -- a record of a different
    build (decision 704)."""
    if record.out_of_line:
        return [f"{record.program}: the recorder found the flags and the hook's table out of line, and named no "
                "function (decision 590)"]
    if problem is not None:
        return [problem]
    if record.flags != entries:
        return [f"{record.program}: a coverage record with {record.flags} flags, where the hook's table of the "
                f"program has {entries} entries: a record of a different build (decision 704)"]
    return []


def listed_script(command: list, cmake, root) -> bool:
    """Examine if a test runs through one of the seven scripts of decision 703:
    its command line runs CMake -- the tree's CMAKE_COMMAND -- with `-P` and
    the path of the script in cmake/ of the repository, each path compared
    as Windows compares paths (decision 713 (4)). A script with the same name
    in a different folder is not one of them."""
    if not command or not cmake or windows_path(command[0]) != windows_path(str(cmake)) or "-P" not in command[1:]:
        return False
    at = command.index("-P", 1) + 1
    folder = pathlib.Path(root) / "cmake"
    return at < len(command) and any(windows_path(command[at]) == windows_path(str(folder / name))
                                     for name in LISTED_SCRIPTS)


def taken_programs(direct, listed: bool, on_line: set, recorded: set) -> set:
    """The programs that a test takes function by function (decisions 703 and
    713 (4)), from `direct`, the program with the hook that the test starts
    directly, or None; `listed`, if it runs through one of the seven scripts;
    the programs of the tree on its command line; and the programs that its
    complete records name. Each other program on its command line keeps its
    bytes, as before.
      - Its direct program, from its records.
      - Through one of the seven scripts: each program that its records name,
        also when the program is on its command line.
      - Each test: each program that its records name and that is not on its
        command line -- for each other test as decision 703 rules, and for a
        test that starts its program directly, on the safe side: a program
        that it starts in turn has no other part."""
    taken = recorded - on_line
    if listed:
        taken |= recorded
    if direct is not None:
        taken.add(direct)
    return taken


def program_unchanged(stored: dict, now, function_part) -> bool:
    """Decision 706, for one program that a test takes function by function:
    unchanged when its bytes are as they were -- then no digest is computed --
    or when its function part is. `now` is the SHA-256 of its bytes, or None
    where it is not there: then it counts as changed. `function_part` gives
    the function part for the stored keys, or None, which counts as changed:
    a program with a problem (decision 713 (3))."""
    if now is None:
        return False
    if now == stored.get("bytes"):
        return True
    part = function_part(stored.get("keys") or [])
    return part is not None and part == stored.get("part")


def with_numbers(store: dict) -> dict:
    """The store as it is written (decision 709 (3)): one table of function
    names for each program at the top of the store, made again at each write
    from the tests that the store holds, and each test with numbers into the
    table of each program that it takes function by function. Thus a name is
    written one time for each program, not one time for each test that
    entered it."""
    names = {}
    for entry in store["tests"].values():
        for key, program in (entry.get("programs") or {}).items():
            names.setdefault(key, set()).update(program["keys"])
    tables = {key: sorted(found) for key, found in names.items()}
    places = {key: {name: number for number, name in enumerate(table)} for key, table in tables.items()}
    tests = {}
    for name, entry in store["tests"].items():
        if entry.get("programs"):
            entry = {**entry, "programs": {key: {**program, "keys": [places[key][k] for k in program["keys"]]}
                                           for key, program in entry["programs"].items()}}
        tests[name] = entry
    return {**store, "tests": tests, FUNCTIONS: tables}


def with_names(store: dict) -> dict:
    """The store as the driver uses it: the numbers of each test turned back
    into the names of the table of each of its programs. A test with a number
    that the table does not hold is left out, so that it runs again."""
    tables = store.get(FUNCTIONS) or {}
    tests = {}
    for name, entry in (store.get("tests") or {}).items():
        if entry.get("programs"):
            programs = named_programs(entry["programs"], tables)
            if programs is None:
                continue
            entry = {**entry, "programs": programs}
        tests[name] = entry
    return {**{key: value for key, value in store.items() if key != FUNCTIONS}, "tests": tests}


def named_programs(programs: dict, tables: dict):
    """The programs of one stored test with the names of their tables in
    place of the numbers, or None where a program has no table or a number is
    not a place in its table."""
    found = {}
    try:
        for key, program in programs.items():
            table = tables[key]
            if not all(type(number) is int and 0 <= number < len(table) for number in program["keys"]):
                return None
            found[key] = {**program, "keys": [table[number] for number in program["keys"]]}
    except (KeyError, TypeError, AttributeError):
        return None
    return found


# --- hashing the world -------------------------------------------------------------

class World:
    """Everything this run hashes, each file once."""

    def __init__(self, tree: pathlib.Path, root: pathlib.Path):
        self.tree = tree
        self.root = root
        self._files = {}
        self._libraries = {}
        self._listed = None
        self._readobj = None
        self._edges = {}
        self._where = {}
        self._folders = {}
        self._hooked = {}
        # The lines that the hook writes at this time (hook_line), for the
        # records of the Python processes.
        self._read_lines = {}
        self._names = {}
        system = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32")
        self._search = [system] + [p for p in os.environ.get("PATH", "").split(os.pathsep) if p]

    def file(self, path) -> str:
        key = norm(path)
        if key not in self._files:
            try:
                self._files[key] = hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
            except OSError:
                self._files[key] = MISSING
        return self._files[key]

    def files(self, paths) -> str:
        return digest("\n".join(f"{self.file(p)} {norm(p)}" for p in sorted(paths, key=norm)))

    def hook_line(self, kind: str, path: str) -> tuple:
        """(kind, stamp): the line that scripts/read-record/sitecustomize.py writes
        for `path` at this time (decision 792), by its rules. A folder gives
        `list` and the hash of its names. Else a `read` line gives `read` and the
        SHA-256 of the file, `missing` where there is no file, and `unreadable`
        for each other error; a `list` line gives `list`, with `missing` or
        `unreadable` as os.listdir fails. file() is not used: it gives `missing`
        also for a folder, thus with it a path that was not there and is a
        folder at this time counts as unchanged."""
        if kind == "list" or os.path.isdir(path):
            return "list", self.names(path)
        key = norm(path)
        if key not in self._read_lines:
            try:
                self._read_lines[key] = hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
            except FileNotFoundError:
                self._read_lines[key] = MISSING
            except OSError:
                self._read_lines[key] = UNREADABLE
        return "read", self._read_lines[key]

    def names(self, folder: str) -> str:
        """The hash of the names in `folder`, as the hook makes it (decision 765):
        the SHA-256 of the sorted names with a line end between them, in UTF-8
        with surrogateescape; `missing` where there is no folder, and
        `unreadable` for each other error."""
        key = norm(folder)
        if key not in self._names:
            try:
                names = sorted(os.listdir(folder))
                self._names[key] = hashlib.sha256("\n".join(names).encode("utf-8", "surrogateescape")).hexdigest()
            except FileNotFoundError:
                self._names[key] = MISSING
            except (OSError, UnicodeError):
                self._names[key] = UNREADABLE
        return self._names[key]

    def hooked(self, path) -> bool:
        """Examine if a program holds the hook's address table (decision 584),
        its bytes read one time for this and for its SHA-256."""
        key = norm(path)
        if key not in self._hooked:
            try:
                data = pathlib.Path(path).read_bytes()
            except OSError:
                data = None
            if data is not None:
                self._files.setdefault(key, hashlib.sha256(data).hexdigest())
            self._hooked[key] = holds_hook_table(data)
        return self._hooked[key]

    def listed(self) -> list:
        """Every file in the repository git does not ignore, tracked or not."""
        if self._listed is None:
            out = subprocess.run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                                 cwd=self.root, capture_output=True, check=True).stdout
            self._listed = sorted({norm(self.root / n) for n in out.decode("utf-8").split("\0") if n})
        return self._listed

    def unignored_under(self, folder) -> list:
        f = norm(folder)
        return [p for p in self.listed() if under(p, f) and os.path.isfile(p)]

    def everything_under(self, folder) -> list:
        """Every file on disk, ignored or not."""
        folder = pathlib.Path(folder)
        if folder.is_file():
            return [str(folder)]
        return sorted(str(p) for p in folder.rglob("*") if p.is_file())

    def program(self, path) -> str:
        """A program by its bytes and those of every library it loads (decision 525)."""
        found = [str(path)] + self.libraries(pathlib.Path(path))
        return self.files(found)

    def libraries(self, program: pathlib.Path) -> list:
        key = norm(program)
        if key not in self._libraries:
            if os.name == "nt":
                self.learn_imports([program])
                self._libraries[key] = self._windows_closure(program)
            else:
                self._libraries[key] = elf_libraries(program)
        return self._libraries[key]

    # --- Windows: import tables, read in batches ---

    def readobj(self) -> str:
        if self._readobj is None:
            compiler = cache_value(self.tree, "CMAKE_CXX_COMPILER")
            candidates = []
            if compiler:
                folder = pathlib.Path(shutil.which(compiler) or compiler).parent
                candidates += [folder / "llvm-readobj.exe", folder / "llvm-readobj"]
            # apt.llvm.org's packages name it with the version: llvm-readobj-23.
            candidates += [shutil.which("llvm-readobj")]
            if not any(c and pathlib.Path(c).is_file() for c in candidates):
                candidates += [shutil.which(f"llvm-readobj-{n}") for n in range(40, 14, -1)]
            self._readobj = next((str(c) for c in candidates if c and pathlib.Path(c).is_file()), "")
            if not self._readobj:
                raise RuntimeError("llvm-readobj was not found beside the compiler or on PATH; it "
                                   "ships with clang, and the driver reads a program's libraries with it")
        return self._readobj

    def learn_imports(self, programs) -> None:
        """Read the import tables of `programs` and of every library they reach,
        a level at a time, each level in as few calls of llvm-readobj as the
        command line allows, and resolve each name to a file once."""
        level = {norm(p): str(p) for p in programs if norm(p) not in self._edges}
        while level:
            names = {}
            for chunk in chunks(list(level.values())):
                names.update(imports_by_file(self.import_text(chunk)))
            following = {}
            for key, path in level.items():
                edges = []
                for name in names.get(key, []):
                    target = self._find_library(name, os.path.dirname(path))
                    if target is None:
                        if not name.lower().startswith(("api-ms-", "ext-ms-")):
                            edges.append(f"<not found: {name.lower()}>")
                        continue
                    edges.append(norm(target))
                    self._where[norm(target)] = target
                    if norm(target) not in self._edges and norm(target) not in level:
                        following[norm(target)] = target
                self._edges[key] = edges
            level = following

    def import_text(self, files: list) -> str:
        """llvm-readobj's account of the libraries `files` load. The one place
        the driver runs it, so that the self-test can hand learn_imports
        recorded output in its place (register decision 602)."""
        command = [self.readobj(), "--coff-imports" if FOLLOW_DELAY_LOADED else "--needed-libs", *files]
        return subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                              errors="replace", check=False).stdout

    def _find_library(self, name: str, beside: str):
        """Where Windows finds a library: beside the program, then the system
        folder, then PATH. The API sets -- api-ms-win-*, ext-ms-* -- are names
        the loader maps, not files, and are left out."""
        if name.lower().startswith(("api-ms-", "ext-ms-")):
            return None
        for folder in [beside] + self._search:
            found = self._listing(folder).get(name.lower())
            if found is not None:
                return found
        return None

    def _listing(self, folder: str) -> dict:
        if folder not in self._folders:
            try:
                self._folders[folder] = {e.name.lower(): e.path for e in os.scandir(folder) if e.is_file()}
            except OSError:
                self._folders[folder] = {}
        return self._folders[folder]

    def _windows_closure(self, program: pathlib.Path) -> list:
        start = norm(program)
        found, queue, seen = set(), [start], {start}
        while queue:
            for target in self._edges.get(queue.pop(), []):
                if target.startswith("<"):
                    found.add(target)
                elif target not in seen:
                    seen.add(target)
                    found.add(self._where[target])
                    queue.append(target)
        return sorted(found)


def chunks(paths: list) -> list:
    """`paths` in groups whose command line stays well inside Windows' 32,767
    characters."""
    groups, group, length = [], [], 0
    for path in paths:
        if group and length + len(str(path)) > 20_000:
            groups.append(group)
            group, length = [], 0
        group.append(path)
        length += len(str(path)) + 3
    return groups + ([group] if group else [])


def imports_by_file(out: str) -> dict:
    """{normalised file: [library names]} from llvm-readobj run over several
    files, with --needed-libs or --coff-imports: each file's section begins
    with its `File:` line; --needed-libs lists the names one to a line inside
    `NeededLibraries [ ]`, --coff-imports gives each its `Name:` line."""
    found, current, inside = {}, None, False
    for line in out.splitlines():
        if line.startswith("File: "):
            current, inside = norm(line[len("File: "):].strip()), False
            found.setdefault(current, [])
        elif current is None:
            continue
        elif line.strip() == "NeededLibraries [":
            inside = True
        elif inside and line.strip() == "]":
            inside = False
        elif inside and line.strip():
            found[current].append(line.strip())
        elif re.match(r"^\s+Name: (\S+)\s*$", line) and line.startswith("  Name: "):
            found[current].append(line.split("Name:", 1)[1].strip())
    return found


def elf_libraries(program: pathlib.Path) -> list:
    """Every shared library an ELF program loads, as the dynamic loader resolves
    them, through ldd: the transitive list, with paths."""
    out = subprocess.run(["ldd", str(program)], capture_output=True, text=True, encoding="utf-8",
                         errors="replace", check=False).stdout
    return ldd_libraries(out)


def ldd_libraries(out: str) -> list:
    """The libraries in ldd's account of a program: each with the path the
    loader found, `<not found: name>` for one it did not, and the kernel's
    vdso, which has no file, left out."""
    found = set()
    for line in out.splitlines():
        match = re.search(r"=>\s*(\S+)\s*\(", line) or re.match(r"\s*(/\S+)\s*\(", line)
        if match:
            found.add(match.group(1))
        elif "not found" in line:
            found.add(f"<not found: {line.split()[0]}>")
    return sorted(found)


# --- one tree's tests ------------------------------------------------------------

class Run:
    def __init__(self, tree: pathlib.Path, ctest: str, root: pathlib.Path = ROOT):
        self.tree = tree
        self.root = root
        self.ctest = ctest
        self.state = tree / STATE
        self.world = World(tree, root)
        listing = subprocess.run([ctest, "--show-only=json-v1"], cwd=tree, capture_output=True,
                                 text=True, encoding="utf-8", errors="replace", check=True).stdout
        self.tests = {t["name"]: t for t in json.loads(listing)["tests"]}
        self.app = tree / (APPLICATION + (".exe" if os.name == "nt" else ""))
        # The registry's leap seconds, read through this so that the
        # self-test can hand the machine part another table.
        self.leap_seconds = leap_seconds_value
        # The memory and the processors, read through this for the same cause
        # (decision 794).
        self.capacity = machine_capacity
        self._machine = None
        self._card = None
        self._card_read = False
        # The parts many tests share, each computed once a run.
        self._shared = {}
        self.start_function_level(cache_value(tree, "CMAKE_COMMAND"), cache_value(tree, "CMAKE_MAKE_PROGRAM"))

    def start_function_level(self, cmake, ninja) -> None:
        """The data that the programs taken function by function use in one run
        (M1-132): CMake, which a test of the seven scripts runs (decision
        713 (4)); Ninja, which gives the link commands (decision 709 (4)); one
        CodeReader, made when a program is first read; the ProgramCode of
        each program read, the link commands, the function parts computed,
        and the problems found."""
        self.cmake, self.ninja = cmake, ninja
        self._reader, self._codes, self._links, self._function_parts = None, {}, {}, {}
        self._problems = {}
        # The kept tables (decision 715 (1)): the TableCache of the run, and
        # the table of each program that a record named, kept or read.
        self._table_cache, self._tables, self.tables_used = None, {}, 0

    def machine_programs(self) -> list:
        # The linker joins them since decision 583: a program's code from
        # not in the tree, and the linker's own pieces, which no digest sees.
        found = [sys.executable, self.ctest, cache_value(self.tree, "CMAKE_COMMAND"),
                 cache_value(self.tree, "CMAKE_CXX_COMPILER"), cache_value(self.tree, "CMAKE_LINKER")]
        return [pathlib.Path(shutil.which(p) or p) for p in found if p]

    def learn_all_imports(self) -> None:
        """On Windows, every program's import tables read together, a level
        at a time, rather than one program after another."""
        if os.name != "nt":
            return
        programs = {norm(p): p for test in self.tests.values() for p in self.arguments(test)
                    if p.is_file() and is_program(p)}
        programs.update({norm(p): p for p in self.machine_programs()})
        self.world.learn_imports(list(programs.values()))

    # --- parts shared by many tests ---

    def machine(self) -> str:
        if self._machine is None:
            zones = (self.world.everything_under(TIME_ZONES_WINDOWS) if os.name == "nt"
                     else [str(p) for p in TIME_ZONES_ELSEWHERE])
            found = [str(p) for p in zones]
            for program in self.machine_programs():
                found += [str(program)] + self.world.libraries(program)
            self._machine = digest(f"{self.world.files(sorted(set(found)))}\n{self.leap_seconds()}\n{self.capacity()}")
        return self._machine

    def card(self):
        """The card, its driver and the Vulkan versions (decision 532), or None
        where they cannot be read -- and then every GPU test runs."""
        if not self._card_read:
            self._card_read = True
            self._card = read_card(self.app, self.state / CARD, self.tree)
        return self._card

    def shared(self, name: str, compute) -> str:
        if name not in self._shared:
            self._shared[name] = compute()
        return self._shared[name]

    def shaders(self) -> str:
        return self.shared("shaders", lambda: self.world.files(self.world.everything_under(self.tree / "shaders")))

    def script_inputs(self) -> str:
        return self.shared("scripts", lambda: self.world.files(
            [f for name in SCRIPT_INPUTS for f in self.world.unignored_under(self.root / name)]))

    def cautious_inputs(self) -> str:
        return self.shared("cautious", lambda: self.world.files(
            [f for name in CAUTIOUS for f in self.world.everything_under(self.root / name)]))

    def tree_inputs(self) -> str:
        return self.shared("tree", lambda: self.world.files(
            [self.tree / f for f in TREE_FILES] + self.world.listed()))

    # --- one test ---

    def arguments(self, test: dict) -> list:
        """Every absolute path a command line names, `-DNAME=<path>` included.
        Absolute only: CTest writes every path the build names that way, and a
        relative argument is more often a test's name than a file -- a Catch2
        case named like a folder in the tree would otherwise be read as one."""
        found = []
        for argument in test.get("command", []):
            value = argument.split("=", 1)[1] if argument.startswith("-D") and "=" in argument else None
            for candidate in (argument, value):
                if candidate and os.path.isabs(candidate) and os.path.exists(candidate):
                    found.append(pathlib.Path(candidate))
        return found

    def parts(self, test: dict, reads, env: dict, shaders=None, taken=frozenset(), listings=None,
              traced=False, children=None) -> dict:
        """What `test` is fingerprinted on, part by part. The environment is
        handed in rather than read here, so that the self-test can give it
        one (register decision 602); and the shaders its records named, as
        shaders_by_test gives them, or None where it left no record. `taken`
        holds the programs that the test takes function by function (M1-132),
        each as norm gives it: their bytes are not in the part of the
        programs, because the store holds them next to the function part for
        the rule of decision 706, and the libraries that they load at start
        are a part of their own. `listings` holds the `list` lines of a test
        with complete records of its Python processes (M1-138), or None for
        each other test: such a test has no part for the build tree, for a
        folder of the repository on its command line, or for scripts/
        (decisions 764, 765 and 801), because its reads and its listings show
        what it read; the files and the programs on its command line stay,
        and the cautious rule also examines its listings (decision 801 (6)).
        `traced` marks a `cmake -P` test with a complete trace, with its
        lines in `reads` and `listings` too: it has no part for scripts/, but
        the part of the build tree, the parts of the folders on its command
        line and the cautious part stay (decisions 775 and 809 (2)), because
        its children have no record of what they read. `children` holds the
        children of a Python test with complete records, as child_rules
        gives them: their part is children_part (decision 817 (4))."""
        tree, root = norm(self.tree), norm(self.root)
        recorded = listings is not None and not traced
        found = {"definition": definition(test), "environment": environment(env),
                 "machine": self.machine()}
        files, programs, built, built_program = [], [], [], False
        command = test.get("command", [])
        for index, path in enumerate(self.arguments(test)):
            key = norm(path)
            if key == tree:
                if not recorded:
                    found["build tree"] = self.tree_inputs()
            elif under(key, tree):
                if path.is_file() and is_program(path):
                    built.append(path)
                    if key not in taken:
                        programs.append(path)
                    if command and norm(command[0]) == key:
                        built_program = True
                # Anything else in the tree is a test's output, judged by the
                # dependency rule (decision 508), never hashed before the run.
            elif under(key, root):
                if not path.is_dir():
                    files.append(path)
                elif not recorded:
                    files += self.world.unignored_under(path)
            elif path.is_file():
                (programs if is_program(path) else files).append(path)
        if programs:
            found["programs"] = digest("\n".join(sorted(self.world.program(p) for p in programs)))
        if taken:
            found["libraries"] = self.libraries_part(taken)
        if files:
            found["files"] = self.world.files(files)
        if not built_program and listings is None:
            found["scripts"] = self.script_inputs()
        if traced or cautious(list(reads or []) + list(listings) if recorded else reads, self.root):
            found["cautious"] = self.cautious_inputs()
        if children:
            found["children"] = self.children_part(children)
        # The compiled files its records name, each read in full (decisions
        # 629 and 635): which shaders a run asks for follows from what the
        # other parts hold. Every shader where they cannot say.
        if isinstance(shaders, list):
            found["shaders"] = self.world.files(shaders)
        elif shaders == EVERY_SHADER or any(norm(p) == norm(self.app) for p in built):
            found["shaders"] = self.shaders()
        if GPU_LABEL in labels(test):
            found["card"] = digest(json.dumps(self.card(), sort_keys=True))
        return found

    def libraries_part(self, taken) -> str:
        """The libraries that each program taken function by function loads at
        start, each program by its path (decisions 525 and 706): they must be
        as they were, also when only one of its bytes and its function part
        is as it was."""
        return digest("\n".join(f"{key} {self.world.files(self.world.libraries(pathlib.Path(key)))}"
                                for key in sorted(taken)))

    def record_path(self, name: str) -> pathlib.Path:
        return self.tree / RECORDS / (escaped_name(name) + ".txt")

    def read_record(self, name: str):
        try:
            return parse_record(self.record_path(name).read_text(encoding="utf-8"))
        except OSError:
            return None

    def trace_path(self, name: str) -> pathlib.Path:
        """Where the trace of a `cmake -P` test is (decision 784):
        test-reads/<escaped name>.trace.json, as cmake/TraceArguments.cmake names
        it for a name that the escape does not change (decision 798)."""
        return self.tree / RECORDS / (escaped_name(name) + TRACE)

    def traces(self, names: list) -> dict:
        """{test: (reads, listings) or None} for each `cmake -P` test of `names`
        (trace_test), from its trace (trace_inputs), each path with the line that
        the hook writes for it (decision 792): the value that this run
        calculated before CTest started, where there is one; if not, the
        value at this time (decision 807). A trace that is not there, or that
        cannot be read, keeps the rules of decisions 500, 510 and 531 (decision 784)."""
        found = {}
        for name in names:
            test = self.tests[name]
            command = test.get("command") or []
            if not trace_test(command, self.cmake):
                continue
            try:
                text = self.trace_path(name).read_bytes().decode("utf-8", errors="replace")
            except OSError:
                found[name] = None
                continue
            named = {norm(p) for p in self.arguments(test)} | {norm(command[0])}
            cwd = properties(test).get("WORKING_DIRECTORY") or str(self.tree)
            paths = trace_inputs(text, command, named, cwd, str(self.tree))
            if paths is None:
                found[name] = None
                continue
            reads, listings = [], []
            for kind, path in [("read", p) for p in paths[0]] + [("list", p) for p in paths[1]]:
                line, stamp = self.world.hook_line(kind, path)
                (reads if line == "read" else listings).append((stamp, path))
            found[name] = (sorted(set(reads)), sorted(set(listings)))
        return found

    def python_records(self, names: list) -> dict:
        """{test: (reads, listings, children) or None} for the tests `names`,
        from the records of their Python processes in test-reads/
        (python_reads), with the rules for their children (child_rules): the
        paths on the command line of a child go in as the lines that the hook
        writes for them (decisions 788 and 817 (5)). A record that cannot be
        read counts as not complete. Read as the hook writes them, in UTF-8
        with surrogateescape."""
        hook = str(self.root.joinpath(*HOOK_FOLDER, HOOK_FILE))
        files = python_record_files(self.tree / RECORDS, names)
        found = {}
        for name in names:
            texts = []
            for path in files.get(name, []):
                try:
                    texts.append(path.read_bytes().decode("utf-8", errors="surrogateescape"))
                except OSError:
                    texts.append(None)
            parsed = python_reads(texts, self.tests[name].get("command") or [], hook)
            children = None if parsed is None else child_rules(parsed[2], str(self.tree), str(self.root))
            if children is None:
                found[name] = None
                continue
            reads, listings = list(parsed[0]), list(parsed[1])
            for path in sorted({p for child in children for p in child["paths"]}):
                line, stamp = self.world.hook_line("read", path)
                (reads if line == "read" else listings).append((stamp, path))
            found[name] = (sorted(set(reads)), sorted(set(listings)), children)
        return found

    def ctest_files(self) -> str:
        """The CTest files of the tree, which `ctest --show-only=json-v1` reads
        (decisions 789 and 815): each CTestTestfile.cmake and
        DartConfiguration.tcl, each file of the test discovery of Catch2, and
        each program in the top folder of the tree, found by one walk of the
        tree in a run, when they are necessary for a test."""
        def walk():
            found = [p for p in self.tree.rglob("*") if p.name in ("CTestTestfile.cmake", "DartConfiguration.tcl")
                     or p.name.endswith(("_include.cmake", "_tests.cmake", "_test-list.cmake"))]
            return self.world.files(found + tree_programs(self.tree))
        return self.shared("ctest files", walk)

    def children_part(self, children: list) -> str:
        """The part of the children of a Python test (decision 817 (4)): each
        program by its bytes and its libraries, and for a ctest child also the
        CTest files of the tree."""
        lines = []
        for child in sorted(children, key=lambda c: (c["kind"], norm(c["program"]))):
            program = self.world.program(pathlib.Path(child["program"]))
            lines.append(f"{child['kind']} {norm(child['program'])} {program}")
            if child["kind"] == "ctest":
                lines.append(f"ctest files {self.ctest_files()}")
        return digest("\n".join(lines))

    def carries_listener(self, program: pathlib.Path, known: dict) -> bool:
        key = self.world.program(program)
        if key not in known:
            run = subprocess.run([str(program), "--list-listeners"], capture_output=True, text=True,
                                 encoding="utf-8", errors="replace", check=False, timeout=60)
            known[key] = lists_listener(run.returncode, run.stdout)
        return known[key]

    # --- the programs taken function by function (M1-132) ---

    def problem(self, text: str) -> None:
        self._problems.setdefault(text, None)

    def all_problems(self) -> list:
        """Each problem of the programs taken function by function, one time:
        the driver's own, and each problem that compiled-code.py found in a
        program that this run read (decision 713 (3)). Each one fails `check`."""
        found = dict(self._problems)
        for code in self._codes.values():
            found.update(dict.fromkeys(code.problems))
        return list(found)

    def hooked(self, path) -> bool:
        """Examine if `path` is a program of this tree that holds the hook's
        address table (decision 584)."""
        path = pathlib.Path(path)
        return under(norm(path), norm(self.tree)) and is_program(path) and path.is_file() and self.world.hooked(path)

    def reader(self):
        """The one CodeReader of this run (decision 714), which keeps the
        summary of each object with the hook in run-tests/objects/."""
        if self._reader is None:
            self._reader = script_module("compiled-code").CodeReader(self.tree, self.state / OBJECTS)
        return self._reader

    def code_of(self, path):
        """The ProgramCode of a program with the hook, read in full one time in
        a run. Its table is kept again for the next runs (decision 715 (1))."""
        key = norm(path)
        if key not in self._codes:
            code = self._codes[key] = self.reader().program(pathlib.Path(path))
            table = code.table()
            if table is not None:
                self.table_cache().put(key, *self.table_hashes(path), table)
        return self._codes[key]

    def table_cache(self):
        """The one TableCache of this run, in run-tests/tables/."""
        if self._table_cache is None:
            self._table_cache = script_module("compiled-code").TableCache(self.state / TABLES)
        return self._table_cache

    def table_hashes(self, path) -> tuple:
        """The SHA-256 of a program's bytes and of its map, which a kept table
        must agree with (decision 715 (1)). Each is the hash of the file's
        bytes, which this run computes one time; the map is not parsed."""
        path = pathlib.Path(path)
        return self.world.file(path), self.world.file(path.with_suffix(".map"))

    def table_of(self, path):
        """What the records of a program are examined with (decisions 590 and
        704): the ProgramCode of a program that this run read in full; if not,
        its kept table when the program's bytes and its map are as they were
        when the table was kept, with no read of its map, its .pdb file, its
        libraries or its summaries (decision 715 (1)); if not, the program read
        in full."""
        key = norm(path)
        if key in self._codes:
            return self._codes[key]
        if key not in self._tables:
            code = script_module("compiled-code")
            kept = self.table_cache().get(key, *self.table_hashes(path))
            if kept is None:
                self._tables[key] = self.code_of(path)
            else:
                self._tables[key] = code.ProgramTable(code.object_key(str(self.tree), str(path)), kept)
                self.tables_used += 1
        return self._tables[key]

    def ninja_commands(self, targets: list) -> tuple:
        """(exit code, text) of `ninja -n -t commands -s` for `targets`: the
        last command of Ninja for each target, one line each, a dry run as
        decision 399 has it (decision 709 (4)). The one place the driver runs
        Ninja, so that the self-test can give it answers."""
        if not self.ninja:
            return None, ""
        done = subprocess.run([self.ninja, "-n", "-t", "commands", "-s", *targets], cwd=self.tree,
                              capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
        return done.returncode, done.stdout

    def link_commands(self, paths) -> dict:
        """{program: its link command, or None} for each of `paths`, the
        programs not known before read in one call of Ninja (decision 709 (4)).
        Ninja names a target by its path relative to the tree, with `/`. When
        Ninja fails, or gives a number of lines that is not the number of
        programs, no program has a link command, and a problem says why: its
        tests run (decision 713 (3))."""
        wanted = sorted({norm(p): pathlib.Path(p) for p in paths if norm(p) not in self._links}.items())
        if wanted:
            targets = [pathlib.Path(os.path.relpath(path, self.tree)).as_posix() for _, path in wanted]
            code, text = self.ninja_commands(targets)
            lines = [line for line in text.splitlines() if line.strip()]
            if code != 0 or len(lines) != len(wanted):
                self.problem(f"Ninja ({self.ninja}) gave {len(lines)} link commands for {len(wanted)} programs, "
                             f"with exit code {code}, not one for each: {', '.join(targets[:3])}")
                lines = [None] * len(wanted)
            self._links.update(zip((key for key, _ in wanted), lines))
        return {norm(p): self._links.get(norm(p)) for p in paths}

    def function_part(self, path, keys) -> object:
        """The function part of the program at `path` for the functions `keys`
        (decision 583), computed one time for each set of keys, or None: then
        the program counts as changed (decision 713 (3)), and the problem
        that says why is in all_problems. A program that does not have the
        hook now has no function part, and its bytes show that it changed."""
        cached = (norm(path), tuple(sorted(keys)))
        if cached not in self._function_parts:
            part = None
            if self.hooked(path):
                link = self.link_commands([path])[norm(path)]
                if link is not None:
                    part = self.code_of(path).function_part(sorted(keys), link)
            self._function_parts[cached] = part
        return self._function_parts[cached]

    def current_bytes(self, key: str):
        path = pathlib.Path(key)
        return self.world.file(path) if path.is_file() else None

    def programs_unchanged(self, programs: dict) -> bool:
        """Decision 706 for each stored program of one test (program_unchanged)."""
        return all(program_unchanged(stored, self.current_bytes(key),
                                     lambda keys, key=key: self.function_part(key, keys))
                   for key, stored in sorted(programs.items()))

    def changed_programs(self, stored: dict) -> list:
        """The stored programs taken function by function with bytes that are
        not as they were, of the tests in `stored`: the decision computes
        their function parts, and Ninja gives their link commands in one call."""
        found = {}
        for entry in stored.values():
            for key, program in (entry.get("programs") or {}).items():
                now = self.current_bytes(key)
                if now is not None and now != program.get("bytes"):
                    found[key] = pathlib.Path(key)
        return list(found.values())

    def recorded_programs(self, records: list) -> tuple:
        """({program: keys}, [problem]) from the complete coverage records of
        one test: the keys of the functions that each program entered, with
        the records of one program put together (decision 564). A record of a
        program that is not one of this tree with the hook, a record with its
        flags out of line or with a count of flags different from the hook's
        table of its program (decisions 590, 704 and 712), and an address that
        is not one function (decision 695) are problems."""
        found, problems = {}, []
        for record in records:
            path = pathlib.Path(record.program)
            if not self.hooked(path):
                problems.append(f"a coverage record names {record.program}, which is not a program of this tree "
                                "with the hook (decision 584)")
                continue
            code = self.table_of(path)
            problems += flag_problems(record, *code.table_entries())
            keys, more = code.keys(record.functions)
            problems += more
            found.setdefault(norm(path), set()).update(keys)
        return found, problems

    def taken_for(self, test: dict, records: list) -> tuple:
        """({program: keys}, [problem]): the programs that `test` takes function
        by function, each with the functions that the test entered, from its
        complete coverage records (decisions 703 and 713 (4)). A test that
        starts a program with the hook directly and passed with no complete
        record of it has a problem (decision 590)."""
        command = test.get("command") or []
        tree = norm(self.tree)
        on_line = {norm(p) for p in self.arguments(test) if under(norm(p), tree) and p.is_file() and is_program(p)}
        first = norm(command[0]) if command else None
        direct = first if first in on_line and self.hooked(command[0]) else None
        recorded, problems = self.recorded_programs(records)
        if direct is not None and direct not in recorded:
            problems.append(f"it starts {command[0]}, which has the hook, and passed with no complete coverage "
                            "record of it (decision 590)")
        names = taken_programs(direct, listed_script(command, self.cmake, self.root), on_line, set(recorded))
        return {key: recorded.get(key, set()) for key in sorted(names)}, problems

    def stored_programs(self, taken: dict):
        """{program: {"bytes", "part", "keys"}} for the store, from taken_for,
        or None where a program has no function part: then the test is not
        stored, and runs again (decision 713 (3))."""
        found = {}
        for key, keys in sorted(taken.items()):
            part = self.function_part(key, keys)
            if part is None:
                self.problem(f"{key}: the program has no function part; each test that takes it function by "
                             "function runs again")
                return None
            found[key] = {"bytes": self.world.file(pathlib.Path(key)), "part": part, "keys": sorted(keys)}
        return found

    def functions_of_run(self, folder: pathlib.Path, order: list, passed: list) -> dict:
        """{test: stored programs}: from the coverage records in `folder`, the
        programs of each test that passed and that the store can keep. A test
        with a problem is not kept. A record that belongs to no test keeps no
        test of the run, as for a shader record that cannot be read: its
        functions can belong to each of them. Each problem goes into
        all_problems."""
        found, unreadable = function_record_texts(folder)
        owned, problems_of, loose = assign_records(found, {name: self.tests[name] for name in order}, set(passed))
        loose += [f"{path}: a coverage record that could not be read ({why}); no test of this run is stored"
                  for path, why in unreadable]
        for text in loose:
            self.problem(text)
        keys = {}
        for name in passed:
            keys[name], more = self.taken_for(self.tests[name], owned.get(name, []))
            problems_of.setdefault(name, []).extend(more)
        self.link_commands([pathlib.Path(key) for taken in keys.values() for key in taken])
        taken = {}
        for name in passed:
            for text in problems_of[name]:
                self.problem(f"the test {name}: {text}")
            programs = None if problems_of[name] else self.stored_programs(keys[name])
            if programs is not None:
                taken[name] = programs
        return {} if loose else taken

    def prune_summaries(self, full: bool) -> list:
        """Only in a run with --full (decision 714): read each program of the
        tree with the hook -- also those that left no record, and each that
        this run read -- and delete the summary of each object that none of
        them links, and the kept table of each program that is not one of
        them (decision 715 (1)). The names of the files deleted. A program
        with a problem gives no objects: its summaries are made again when the
        driver uses them, and its problem fails `check`."""
        if not full:
            return []
        kept, programs = set(), set()
        for path in tree_programs(self.tree) + [pathlib.Path(key) for key in list(self._codes)]:
            if self.hooked(path):
                kept.update(self.code_of(path).objects)
                programs.add(norm(path))
        return self.reader().cache.prune(kept) + self.table_cache().prune(programs)


def lists_listener(code: int, out: str) -> bool:
    """Whether a program's answer to --list-listeners names the listener that
    writes read records."""
    return code == 0 and LISTENER in out


def read_card(app: pathlib.Path, out: pathlib.Path, tree: pathlib.Path):
    if not app.is_file():
        return None
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run([str(app), "--validate", "--probe", "clear", "--probe-out", str(out)], cwd=tree,
                       capture_output=True, timeout=CARD_PROBE_SECONDS, check=False)
        text = (out / "clear.txt").read_text(encoding="utf-8", errors="replace")
    except (OSError, subprocess.TimeoutExpired):
        return None
    return card_values(text)


def card_values(text: str):
    """The card, its driver and the Vulkan versions from a clear probe's
    sidecar, or None where one is missing or the probe ran without the
    validation layer -- whose version is part of what judges a GPU test."""
    values = {}
    for line in text.splitlines():
        key, equals, value = line.partition("=")
        if equals and key.strip() in CARD_LINES:
            values[key.strip()] = value.strip()
    if set(values) != set(CARD_LINES) or values["validation.layer"].startswith("none"):
        return None
    return values


def load_store(state: pathlib.Path) -> dict:
    """The store of the last runs, with the names of the functions in place of
    the numbers (with_names): mutants-due.py reads it through this function."""
    try:
        store = json.loads((state / STORE).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {"version": STORE_VERSION, "tests": {}, "listeners": {}}
    if not isinstance(store, dict) or store.get("version") != STORE_VERSION:
        return {"version": STORE_VERSION, "tests": {}, "listeners": {}}
    return with_names(store)


def write_store(state: pathlib.Path, store: dict) -> None:
    """The store, with the numbers of with_numbers."""
    (state / STORE).write_text(json.dumps(with_numbers(store), sort_keys=True), encoding="utf-8")


def git(root: pathlib.Path, *args) -> str:
    return subprocess.run(["git", *args], cwd=root, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", check=True).stdout


def run_tree(tree: pathlib.Path, ctest: str, jobs: int, full: bool, root: pathlib.Path = ROOT) -> int:
    started_at = git(root, "rev-parse", "HEAD").strip() if full else None
    clean_at_start = git(root, "status", "--porcelain").strip() == "" if full else None
    run = Run(tree, ctest, root)
    late = []
    if full:
        try:
            log_time = (tree / ".ninja_log").stat().st_mtime_ns
        except OSError:
            log_time = None
        times = {}
        for path in run.world.listed():
            try:
                times[path] = os.stat(path).st_mtime_ns
            except OSError:
                pass
        late = edited_after_build(times, log_time)
    if not run.tests:
        # A tree whose CTest lists nothing would pass every run by running
        # nothing -- the end-to-end test's first run met exactly that, with a
        # generator that lists tests only per configuration (VERIFICATION.md
        # rule 23).
        print(f"run-tests: CTest lists no tests in {tree}; refusing to pass by running none")
        return 1
    run.state.mkdir(parents=True, exist_ok=True)
    if full:
        (run.state / FULL_STAMP).unlink(missing_ok=True)
    store = load_store(run.state)
    stored = store["tests"]

    if any(GPU_LABEL in labels(t) for t in run.tests.values()):
        run.card()
    forced = set()
    if full:
        forced = set(run.tests)
    elif run._card is None:
        forced = {n for n, t in run.tests.items() if GPU_LABEL in labels(t)}
    began = time.monotonic()
    run.learn_all_imports()
    # Every program the tests start writes its records into the one folder
    # (decisions 570 and 642), named the same on every run, so that the
    # environment part does not move with it.
    records = tree / COVERAGE_RECORDS
    env = {**os.environ, RECORDS_VARIABLE: str(records)}
    # The hook of the records of the Python processes (M1-138, decisions 771
    # and 781), in the environment of each test, thus also in its part.
    env = with_hook(without_test_name(env), tree, root)
    parts = {name: run.parts(test, (stored.get(name) or {}).get("reads"), env,
                             (stored.get(name) or {}).get("shaders"),
                             set((stored.get(name) or {}).get("programs") or ()),
                             (stored.get(name) or {}).get("listings"),
                             traced=bool((stored.get(name) or {}).get("trace")),
                             children=(stored.get(name) or {}).get("children"))
             for name, test in run.tests.items()}
    # The link commands of the programs that the decision computes function
    # parts for, in one call of Ninja (decision 709 (4)).
    run.link_commands(run.changed_programs({n: e for n, e in stored.items() if n in run.tests and n not in forced}))
    chosen = selection(run.tests, stored, parts, run.world.file, forced, run.programs_unchanged, run.world.hook_line)
    decided = time.monotonic()
    skipped = sorted(set(run.tests) - chosen)
    order = [name for name in run.tests if name in chosen]

    (run.state / SELECTED).write_text("".join(f"{n}\n" for n in order), encoding="utf-8")
    (run.state / SKIPPED).write_text("".join(f"{n}\n" for n in skipped), encoding="utf-8")
    # The application's bytes at this run. The list of shaders of each
    # skipped test gets them before CTest starts, because
    # mutants_due_probe_shaders reads the store in this run (decisions 737 and
    # 738); each test that passes gets them with its own list below.
    application, renewed = renewed_applications(stored, skipped, run.world.file, run.app)
    if renewed:
        write_store(run.state, store)
    for name in order:
        run.record_path(name).unlink(missing_ok=True)
    # The records of the Python processes of each test that runs: else the
    # driver reads a record of an earlier run as a record of this run.
    for paths in python_record_files(tree / RECORDS, order).values():
        for path in paths:
            path.unlink(missing_ok=True)
    # And the trace of each such test (decision 809 (3)).
    for name in order:
        run.trace_path(name).unlink(missing_ok=True)

    code, results = 0, {}
    if order:
        report = run.state / REPORT
        report.unlink(missing_ok=True)
        # The hook writes no record into a folder that is not there (decision
        # 801 (4)).
        (tree / RECORDS).mkdir(parents=True, exist_ok=True)
        # Emptied first (decision 570): a record left from an earlier run
        # would be read as this one's.
        left = empty_folder(records)
        if left:
            print(f"run-tests: {records} could not be emptied, {left[0].name} among {len(left)} left; "
                  "is a program from an earlier run still running?")
            return 1
        command = [ctest, "--output-on-failure", "-j", str(jobs), "--tests-from-file", str(run.state / SELECTED),
                   "--output-junit", str(report)]
        if not full:
            # A setup test left out was unchanged, and so are its outputs.
            command += ["--fixture-exclude-setup", ".*"]
        code = subprocess.run(command, cwd=tree, env=env, check=False).returncode
        try:
            results = outcomes(report.read_text(encoding="utf-8"))
        except (OSError, ElementTree.ParseError):
            for line in run.all_problems():
                print(f"run-tests: {line}")
            print("run-tests: CTest wrote no report; nothing is remembered from this run")
            return code or 1

    listeners = store.setdefault("listeners", {})
    passed = [name for name in order if results.get(name) == "passed"]
    reads = {name: run.read_record(name) for name in passed}
    carries = {}
    for name in passed:
        command = run.tests[name].get("command", [])
        program = pathlib.Path(command[0]) if command else None
        carries[name] = (program is not None and under(norm(program), norm(tree)) and is_program(program)
                         and run.carries_listener(program, listeners))
    missing_records = set(unrecorded(passed, {n: r is not None for n, r in reads.items()}, carries))
    problems = [f"{name}: passed without its read record, though its program carries {LISTENER}: is the "
                "name escaped differently here and in tests/TestReads.cpp, or the record not written?"
                for name in sorted(missing_records)]
    # The tests with complete records of their Python processes (M1-138): their
    # reads join those of a record of a C++ program, after the driver examined
    # the C++ records above, and their listings go into the store. Each other
    # test keeps the rules before M1-138 (decision 801 (5)).
    listings, children = {}, {}
    for name, found in run.python_records(passed).items():
        if found is not None:
            reads[name] = sorted(set(map(tuple, reads[name] or [])) | set(found[0]))
            listings[name] = found[1]
            if found[2]:
                children[name] = found[2]
    # The `cmake -P` tests with a complete trace (decisions 768, 784 and
    # 803-809), the same way; the store marks them with `trace`.
    traced = set()
    for name, found in run.traces(passed).items():
        if found is not None and name not in listings:
            reads[name] = sorted(set(map(tuple, reads[name] or [])) | set(found[0]))
            listings[name] = found[1]
            traced.add(name)
    # The shaders each test's runs asked for (decision 629). A record that
    # cannot be read belongs to a test this cannot name, so nothing this run
    # ran is remembered, and every one of them runs again.
    texts, unreadable = shader_record_texts(records) if order else ([], [])
    problems += [f"{path}: a shader record that could not be read ({why}); every test this run ran runs again"
                 for path, why in unreadable]
    shaders = shaders_by_test(texts)
    # The programs that each test that passed takes function by function,
    # from the coverage records (M1-132). A test with a problem is not
    # stored; a record that belongs to no test stores none of this run's.
    taken = run.functions_of_run(records, order, passed) if order else {}
    for name in order:
        if name in passed and name not in missing_records and not unreadable and name in taken:
            entry = {"parts": run.parts(run.tests[name], reads[name], env, shaders.get(name), set(taken[name]),
                                        listings.get(name), traced=name in traced, children=children.get(name)),
                     "reads": reads[name]}
            if name in listings:
                entry["listings"] = listings[name]
            if name in traced:
                entry["trace"] = True
            if name in children:
                entry["children"] = children[name]
            if taken[name]:
                entry["programs"] = taken[name]
            if name in shaders:
                entry["shaders"] = shaders[name]
                entry["application"] = application
            stored[name] = entry
        else:
            stored.pop(name, None)
    for name in list(stored):
        if name not in run.tests:
            del stored[name]
    # Only a full run reads each program with the hook of the tree, and thus
    # knows which summaries no program uses (decision 714).
    pruned = run.prune_summaries(full)
    problems += run.all_problems()
    write_store(run.state, store)

    for line in problems:
        print(f"run-tests: {line}")
    ran = len(results)
    reader = run._reader
    print(f"run-tests: {ran} ran, {len(skipped)} skipped as unchanged"
          + (f" -- listed in {run.state / SKIPPED}" if skipped else "")
          + f"; the fingerprints took {decided - began:.1f} s"
          + ("" if reader is None and not run.tables_used else
             f"; the compiled code: programs read in full: {len(run._codes)}, from kept tables: {run.tables_used}")
          + ("" if reader is None else ", " + ", ".join(
              f"{part} {seconds:.1f} s" for part, seconds in reader.seconds.items())
             + f", {reader.cache.made} summaries made, {reader.cache.loaded} read, {len(pruned)} files deleted"))
    if full:
        refusals = full_problems(run, results, started_at, clean_at_start)
        if late:
            refusals.append(f"{len(late)} file(s) changed after the build's last step, {late[0]} the "
                            "newest: build and run the full check again (decision 542)")
        for line in refusals:
            print(f"run-tests: {line}")
        problems += refusals
        if not problems and code == 0:
            stamp = {"commit": started_at, "tree": norm(tree), "tests": len(results),
                     "time": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")}
            (run.state / FULL_STAMP).write_text(json.dumps(stamp, sort_keys=True), encoding="utf-8")
            print(f"run-tests: full check passed at {started_at}, all {len(results)} tests run")
    return code or (1 if problems else 0)


def full_problems(run: Run, results: dict, started_at: str, clean_at_start: bool) -> list:
    clean_at_end = git(run.root, "status", "--porcelain").strip() == ""
    head_at_end = git(run.root, "rev-parse", "HEAD").strip()
    return full_run_problems(set(run.tests), set(results), (clean_at_start, clean_at_end),
                             (started_at, head_at_end))


# --- the push ------------------------------------------------------------------------

CHECK_TREES = ("build/debug", "build/relwithdebinfo")


def pre_push(root: pathlib.Path, lines: list) -> int:
    stamps = {}
    for tree in CHECK_TREES:
        try:
            stamps[tree] = json.loads((root / tree / STATE / FULL_STAMP).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            stamps[tree] = None
    problems = push_problems(lines, stamps)
    for line in problems:
        print(f"pre-push: {line}", file=sys.stderr)
    if problems:
        print("pre-push: run `cmake --build build/debug --target check-full` and the same for "
              "build/relwithdebinfo, at the commit you push, from a clean working tree "
              "(register decisions 503 and 535)", file=sys.stderr)
        return 1
    return 0


# --- the driver on this tree's own programs (decision 602) ----------------------

def in_tree(tree: pathlib.Path, ctest: str, suite: pathlib.Path, other: pathlib.Path,
            application, libraries: list) -> int:
    """What the self-test cannot see, because it reads recorded answers: that
    the tools still answer as recorded, on this tree's own programs. A suite
    carries the read-record listener and a program without it does not; and
    where the tree builds the application, its libraries, read through
    llvm-readobj or ldd and followed as a fingerprint follows them, include
    the ones CMake names."""
    run = Run(tree, ctest)
    known = {}
    problems = []
    if not run.carries_listener(suite, known):
        problems.append(f"{suite} does not report {LISTENER}, which every suite carries")
    if run.carries_listener(other, known):
        problems.append(f"{other} reports {LISTENER}, which it does not carry")
    if application is None:
        print("run-tests in-tree: this tree builds no application; its libraries were not checked")
    else:
        found = {pathlib.Path(p).name.lower() for p in run.world.libraries(pathlib.Path(application))
                 if not p.startswith("<")}
        problems += [f"{application}: {name} is not among the {len(found)} libraries found for it"
                     for name in libraries if name.lower() not in found]
    for line in problems:
        print(f"run-tests in-tree: {line}")
    if problems:
        return 1
    print(f"run-tests in-tree: the listener found where it is and not where it is not"
          + ("" if application is None else f"; {', '.join(libraries)} among the application's libraries"))
    return 0


# --- the end-to-end test (decision 537) ------------------------------------------

# A test of the scratch project's: `reader.py <name> record|none <exit code>
# <file>...` reads the files it is given, relative to the project, writes a
# read record as tests/ReadRecordListener.cpp does -- unless told `none` --
# and exits with the code it is given. The scratch names need no escaping.
SCRATCH_READER = r"""import hashlib, os, sys
here = os.path.dirname(os.path.abspath(__file__))
name, record, code, files = sys.argv[1], sys.argv[2] == "record", int(sys.argv[3]), sys.argv[4:]
lines = []
for f in files:
    path = os.path.join(here, f).replace(os.sep, "/")
    with open(path, "rb") as stream:
        lines.append(hashlib.sha256(stream.read()).hexdigest() + " " + path + "\n")
if record:
    os.makedirs("test-reads", exist_ok=True)
    with open(os.path.join("test-reads", name + ".txt"), "w", newline="\n") as out:
        out.write("".join(sorted(lines)) + "end\n")
sys.exit(code)
"""

SCRATCH_PROJECT = """cmake_minimum_required(VERSION 3.25)
project(scratch NONE)
enable_testing()
function(reader name record code)
    add_test(NAME ${name} COMMAND ${PY} ${CMAKE_SOURCE_DIR}/reader.py ${name} ${record} ${code} ${ARGN})
endfunction()
reader(reads_a record 0 inputs/a.txt)
reader(reads_b record 0 inputs/b.txt)
reader(silent none 0 inputs/a.txt)
reader(setup record 0 inputs/s.txt)
reader(needs_setup record 0)
reader(skips_itself record 4)
reader(on_card record 0)
set_tests_properties(on_card PROPERTIES LABELS gpu)
set_tests_properties(setup PROPERTIES FIXTURES_SETUP made)
set_tests_properties(needs_setup PROPERTIES FIXTURES_REQUIRED made)
set_tests_properties(skips_itself PROPERTIES SKIP_RETURN_CODE 4)
# One build step, so that the tree has a Ninja log for decision 542's check.
add_custom_target(built ALL COMMAND ${CMAKE_COMMAND} -E touch built.txt)
"""


# A test of the second scratch project's (M1-131): `starts.py <name>
# whole|cut|none <shader>...` writes a shader record as the application does,
# into the folder ORBSIM_COVERAGE_RECORDS names, each shader as the build
# tree's shaders folder joined with a backslash -- or one without its `end`
# line, or none -- standing for a script that starts the application.
SCRATCH_STARTS = r"""import os, sys, time
name, kind, shaders = sys.argv[1], sys.argv[2], sys.argv[3:]
folder = os.environ.get("ORBSIM_COVERAGE_RECORDS")
if folder and kind != "none":
    lines = [f"test {name}"] + [os.path.join(os.getcwd(), "shaders") + "\\" + s for s in shaders]
    lines += ["end"] if kind == "whole" else []
    with open(os.path.join(folder, f"{os.getpid()}-{time.time_ns()}.shaders.txt"), "wb") as out:
        out.write("".join(line + "\n" for line in lines).encode("utf-8"))
"""

SCRATCH_SHADER_PROJECT = """cmake_minimum_required(VERSION 3.25)
project(scratch NONE)
enable_testing()
add_test(NAME loads COMMAND ${PY} ${CMAKE_SOURCE_DIR}/starts.py loads whole s1.spv)
add_test(NAME cut COMMAND ${PY} ${CMAKE_SOURCE_DIR}/starts.py cut cut s1.spv)
add_test(NAME none COMMAND ${PY} ${CMAKE_SOURCE_DIR}/starts.py none none)
"""


def shader_records_end_to_end(cmake: str, ctest: str, ninja: str, steps: list) -> list:
    """The shader records through the driver (M1-131, decisions 570, 629, 637
    and 642): the folder it names reaches the tests, a test's record is read
    and its list stored and judged by at the next run, a record without its
    `end` line keeps every shader, and the folder is emptied before each run."""
    failures = []
    with tempfile.TemporaryDirectory() as scratch:
        root = pathlib.Path(scratch) / "repo"
        tree = root / "build"
        root.mkdir()
        (root / "starts.py").write_bytes(SCRATCH_STARTS.encode("utf-8"))
        (root / "CMakeLists.txt").write_bytes(SCRATCH_SHADER_PROJECT.encode("utf-8"))
        (root / ".gitignore").write_bytes(b"build/\n")
        quiet = {"capture_output": True, "check": True, "cwd": root}
        subprocess.run(["git", "init", "-q"], **quiet)
        subprocess.run([cmake, "-S", str(root), "-B", str(tree), "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}",
                        f"-DPY={sys.executable}"], **quiet)
        (tree / "shaders").mkdir()
        s1, s2 = tree / "shaders" / "s1.spv", tree / "shaders" / "s2.spv"
        s1.write_bytes(b"1")
        s2.write_bytes(b"2")

        def step(name: str, wanted: set) -> None:
            (tree / STATE / REPORT).unlink(missing_ok=True)
            done = subprocess.run([sys.executable, __file__, str(tree), "--ctest", ctest, "--root", str(root)],
                                  capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
            try:
                ran = set(outcomes((tree / STATE / REPORT).read_text(encoding="utf-8")))
            except OSError:
                ran = set()
            steps.append(name)
            if ran != wanted or done.returncode != 0:
                failures.append(f"{name}: ran {sorted(ran)}, wanted {sorted(wanted)}, exit "
                                f"{done.returncode}\n{done.stdout}{done.stderr}")

        step("shader records: the first run", {"loads", "cut", "none"})
        step("shader records: nothing changed", set())
        s2.write_bytes(b"2 changed")
        step("shader records: a shader no whole record names", {"cut"})
        s1.write_bytes(b"1 changed")
        step("shader records: a shader a whole record names", {"loads", "cut"})
        # A record left from an earlier run, cut off, which would keep every
        # shader for `loads` if it were read as this run's.
        (tree / COVERAGE_RECORDS / f"1-1{SHADER_RECORD}").write_bytes(b"test loads\n")
        s1.write_bytes(b"1 changed again")
        step("shader records: a record left in the folder before a run", {"loads", "cut"})
        s2.write_bytes(b"2 changed again")
        step("shader records: the left record was not read", {"cut"})
    return failures


# The third scratch project (M1-138, decision 816): a Python test, `py`, that
# reads inputs/p.txt, lists inputs/ and starts `ctest --show-only=json-v1`, the
# child of rule 2; and a `cmake -P` test, `cm`, with a script that reads
# inputs/c.txt, which its command line does not name, with the trace arguments of
# cmake/TraceArguments.cmake of this repository.
SCRATCH_CHECK = """import os, subprocess, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with open(os.path.join(root, "inputs", "p.txt"), encoding="utf-8") as stream:
    stream.read()
os.listdir(os.path.join(root, "inputs"))
subprocess.run([sys.argv[1], "--show-only=json-v1"], check=True, capture_output=True)
"""

SCRATCH_RUN = 'file(READ "${CMAKE_CURRENT_LIST_DIR}/../inputs/c.txt" text)\n'

SCRATCH_RECORDS_PROJECT = """cmake_minimum_required(VERSION 3.25)
project(scratch NONE)
enable_testing()
include(${TRACE_ARGUMENTS})
add_test(NAME py COMMAND ${PY} ${CMAKE_SOURCE_DIR}/scripts/check.py ${CTEST})
set_tests_properties(py PROPERTIES ENVIRONMENT ORBSIM_TEST_NAME=py)
orbsim_trace_arguments(trace cm)
add_test(NAME cm COMMAND ${CMAKE_COMMAND} ${trace} -P ${CMAKE_SOURCE_DIR}/cmake/Run.cmake)
"""

SCRATCH_REFUSED_PROJECT = """cmake_minimum_required(VERSION 3.25)
project(refused NONE)
include(${TRACE_ARGUMENTS})
orbsim_trace_arguments(trace "a:b")
"""


def script_records_end_to_end(cmake: str, ctest: str, ninja: str, steps: list) -> list:
    """The records of the Python processes, the trace and a child of rule 2
    through the driver (M1-138, decisions 786, 808 and 816): the twelve steps
    of decision 816, each with the tests that must run. Then the steps of
    decisions 835 and 836: a plain `ctest` in a new tree, test-reads/ removed
    after the configure, and a `cmake -P` test without its trace arguments.
    The scratch project holds a copy of the hook, because the hook finds its
    repository from the folder of its own file."""
    failures = []
    trace_arguments = (ROOT / "cmake" / "TraceArguments.cmake").as_posix()
    with tempfile.TemporaryDirectory() as scratch:
        root = pathlib.Path(scratch) / "repo"
        tree = root / "build"
        files = {"scripts/check.py": SCRATCH_CHECK, "scripts/other.py": "# read by no test\n",
                 "cmake/Run.cmake": SCRATCH_RUN, "inputs/p.txt": "p\n", "inputs/c.txt": "c\n",
                 "docs/a.md": "a\n", "CMakeLists.txt": SCRATCH_RECORDS_PROJECT, ".gitignore": "build/\n"}
        for name, text in files.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_bytes(text.encode("utf-8"))
        (root / "scripts" / "read-record").mkdir()
        shutil.copy(ROOT.joinpath(*HOOK_FOLDER, HOOK_FILE), root.joinpath(*HOOK_FOLDER, HOOK_FILE))
        # A repository, because the rules before M1-138 list its files with git.
        subprocess.run(["git", "init", "-q"], capture_output=True, check=True, cwd=root)
        configure = [cmake, "-S", str(root), "-B", str(tree), "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}",
                     f"-DPY={sys.executable}", f"-DCTEST={ctest}", f"-DTRACE_ARGUMENTS={trace_arguments}"]
        subprocess.run(configure, capture_output=True, check=True, cwd=root)

        # A plain `ctest` in a new tree, with no run of the driver before it
        # (decisions 810 and 836): CMake stops `cm` when the folder of its
        # trace is not there, thus the configure must make the folder.
        # --no-tests=error, because a `-R` that finds no test passes.
        plain = pathlib.Path(scratch) / "plain"
        subprocess.run([cmake, "-S", str(root), "-B", str(plain)] + configure[5:], capture_output=True, check=True,
                       cwd=root)
        done = subprocess.run([ctest, "--output-on-failure", "--no-tests=error", "-R", "^cm$"], capture_output=True,
                              text=True, encoding="utf-8", errors="replace", check=False, cwd=plain)
        steps.append("script records: a plain ctest in a new tree runs the cmake -P test")
        if done.returncode != 0 or not (plain / RECORDS / f"cm{TRACE}").is_file():
            failures.append(f"a plain ctest in a new tree: exit {done.returncode}, or no trace\n"
                            f"{done.stdout}{done.stderr}")

        def step(name: str, wanted: set) -> None:
            (tree / STATE / REPORT).unlink(missing_ok=True)
            done = subprocess.run([sys.executable, __file__, str(tree), "--ctest", ctest, "--root", str(root)],
                                  capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
            try:
                ran = set(outcomes((tree / STATE / REPORT).read_text(encoding="utf-8")))
            except OSError:
                ran = set()
            steps.append(name)
            if ran != wanted or done.returncode != 0:
                failures.append(f"{name}: ran {sorted(ran)}, wanted {sorted(wanted)}, exit "
                                f"{done.returncode}\n{done.stdout}{done.stderr}")

        def change(name: str, text: str) -> None:
            (root / name).write_bytes(text.encode("utf-8"))

        step("script records: the first run", {"py", "cm"})
        step("script records: nothing changed", set())
        change("scripts/other.py", "# read by no test, changed\n")
        step("script records: a script that no test reads, in scripts/", set())
        change("scripts/new.py", "# new\n")
        step("script records: a new file in scripts/, which the Python test lists", {"py"})
        change("inputs/p.txt", "p changed\n")
        step("script records: the file that the Python test reads", {"py"})
        change("inputs/c.txt", "c changed\n")
        step("script records: the file that the script of the cmake -P test reads", {"cm"})
        change("inputs/q.txt", "q\n")
        step("script records: a new file in the folder that the Python test lists", {"py"})
        change("scripts/check.py", SCRATCH_CHECK + "# changed\n")
        step("script records: the script of the Python test", {"py"})
        change("cmake/Run.cmake", SCRATCH_RUN + "# changed\n")
        step("script records: the script of the cmake -P test", {"cm"})
        change("docs/a.md", "a changed\n")
        step("script records: a document", set())
        change("CMakeLists.txt", SCRATCH_RECORDS_PROJECT + "add_test(NAME added COMMAND ${CMAKE_COMMAND} -E true)\n")
        subprocess.run(configure, capture_output=True, check=True, cwd=root)
        step("script records: a new test: it, the ctest child of the Python test, and the cautious part",
             {"added", "py", "cm"})
        # Decision 801 (4): the driver makes test-reads/ before CTest. Else,
        # after a person removes the folder, CMake stops `cm`, and the hook
        # writes no record for `py` (decision 836).
        shutil.rmtree(tree / RECORDS)
        change("inputs/p.txt", "p changed again\n")
        change("inputs/c.txt", "c changed again\n")
        step("script records: test-reads/ removed after the configure", {"py", "cm"})
        # Decision 809 (3): the driver deletes the trace of each test that it
        # runs before the run. Here `cm` runs without its trace arguments, and
        # its script also reads scripts/r.txt. Else the driver reads the trace of
        # the last run as the trace of this run, and does not run `cm` when
        # scripts/r.txt changes (decision 835). With no trace, `cm` keeps the
        # rules before M1-138, which hold the part of scripts/. If the text
        # of the trace arguments is not there, `cm` keeps its trace, which
        # names scripts/r.txt, and the step passes also without the line of
        # decision 809 (3) (VERIFICATION.md rule 23).
        project = (root / "CMakeLists.txt").read_bytes().decode("utf-8")
        if project.count(" ${trace} -P") != 1:
            failures.append("the definition of cm does not hold its trace arguments one time")
        change("CMakeLists.txt", project.replace(" ${trace} -P", " -P"))
        change("cmake/Run.cmake", SCRATCH_RUN + 'file(READ "${CMAKE_CURRENT_LIST_DIR}/../scripts/r.txt" text)\n')
        change("scripts/r.txt", "r\n")
        subprocess.run(configure, capture_output=True, check=True, cwd=root)
        step("script records: the cmake -P test without its trace arguments", {"added", "py", "cm"})
        change("scripts/r.txt", "r changed\n")
        step("script records: the file in scripts/ that the cmake -P test with no trace reads", {"added", "cm"})

        refused = pathlib.Path(scratch) / "refused"
        refused.mkdir()
        (refused / "CMakeLists.txt").write_bytes(SCRATCH_REFUSED_PROJECT.encode("utf-8"))
        done = subprocess.run([cmake, "-S", str(refused), "-B", str(refused / "build"), "-G", "Ninja",
                               f"-DCMAKE_MAKE_PROGRAM={ninja}", f"-DTRACE_ARGUMENTS={trace_arguments}"],
                              capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
        steps.append("script records: a test name that the escape changes stops the configure")
        if done.returncode == 0 or "orbsim_trace_arguments" not in done.stdout + done.stderr:
            failures.append(f"a test name that the escape changes: the configure ended with {done.returncode}\n"
                            f"{done.stdout}{done.stderr}")
    return failures


def end_to_end(cmake: str, ctest: str, ninja: str) -> int:
    """The driver on a scratch project: each change runs exactly the tests it
    should, a full run runs all and stamps its commit, and a full run from a
    working tree that is not clean stamps nothing."""
    failures, steps = [], []
    with tempfile.TemporaryDirectory() as scratch:
        root = pathlib.Path(scratch) / "repo"
        tree = root / "build"
        (root / "inputs").mkdir(parents=True)
        (root / "data").mkdir()
        (root / "scripts").mkdir()
        (root / "scripts" / "helper.py").write_text("# named on no command line\n", encoding="utf-8")
        (root / "reader.py").write_text(SCRATCH_READER, encoding="utf-8")
        (root / "CMakeLists.txt").write_text(SCRATCH_PROJECT, encoding="utf-8")
        (root / ".gitignore").write_text("build/\n", encoding="utf-8")
        for name in ("a", "b", "s"):
            (root / "inputs" / f"{name}.txt").write_text(f"{name}\n", encoding="utf-8")
        (root / "data" / "d.txt").write_text("d\n", encoding="utf-8")
        quiet = {"capture_output": True, "check": True, "cwd": root}
        subprocess.run(["git", "init", "-q"], **quiet)
        subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "add", "-A"], **quiet)
        subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "s"], **quiet)
        # Ninja, as the real trees: a generator with several configurations
        # lists no tests to --show-only without one named.
        subprocess.run([cmake, "-S", str(root), "-B", str(tree), "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}",
                        f"-DPY={sys.executable}"], **quiet)

        def run(full: bool) -> set:
            command = [sys.executable, __file__, str(tree), "--ctest", ctest, "--root", str(root)]
            done = subprocess.run(command + (["--full"] if full else []), capture_output=True, text=True,
                                  encoding="utf-8", errors="replace", check=False)
            try:
                return set(outcomes((tree / STATE / REPORT).read_text(encoding="utf-8"))), done
            except OSError:
                return set(), done

        def step(name: str, wanted: set, full: bool = False):
            (tree / STATE / REPORT).unlink(missing_ok=True)
            ran, done = run(full)
            steps.append(name)
            if ran != wanted:
                failures.append(f"{name}: ran {sorted(ran)}, wanted {sorted(wanted)}\n{done.stdout}{done.stderr}")
            return done

        everything = {"reads_a", "reads_b", "silent", "setup", "needs_setup", "skips_itself", "on_card"}
        # A test that skipped itself is never remembered (decision 536), and
        # a GPU test runs whenever the card cannot be read -- here, always,
        # since the scratch tree has no application to run the clear probe.
        always = {"skips_itself", "on_card"}
        step("the first run, nothing stored", everything)
        step("nothing changed", always)
        (root / "inputs" / "a.txt").write_text("a changed\n", encoding="utf-8")
        # silent reads a.txt too, but records nothing: it is judged by the
        # cautious rule alone, which a.txt is outside -- the case the rule
        # exists to make visible, and why decision 530 checks the C++ tests.
        step("a file one test recorded", always | {"reads_a"})
        (root / "inputs" / "s.txt").write_text("s changed\n", encoding="utf-8")
        step("a setup test's file", always | {"setup", "needs_setup"})
        (root / "data" / "d.txt").write_text("d changed\n", encoding="utf-8")
        step("a file under data/", always | {"silent"})
        (root / "scripts" / "helper.py").write_text("# edited\n", encoding="utf-8")
        step("a script no command line names", everything)
        (root / "reader.py").write_text(SCRATCH_READER + "\n", encoding="utf-8")
        step("the program the tests run", everything)
        step("nothing changed again", always)

        stamp = tree / STATE / FULL_STAMP
        done = step("a full run from a working tree with changes", everything, full=True)
        if stamp.exists() or done.returncode == 0:
            failures.append("a full run from a working tree with changes stamped its commit or passed")
        subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-am", "c"], **quiet)
        subprocess.run([cmake, "--build", str(tree)], **quiet)
        # Decision 542: changed and committed after the build, so the working
        # tree is clean and still the run must not stamp.
        (root / "inputs" / "b.txt").write_text("b after the build\n", encoding="utf-8")
        subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-am", "late"], **quiet)
        done = step("a full run after a commit newer than the build", everything, full=True)
        if stamp.exists() or done.returncode == 0:
            failures.append("a full run after a commit newer than the build stamped its commit or passed")
        subprocess.run([cmake, "--build", str(tree)], **quiet)
        done = step("a full run from a clean working tree", everything, full=True)
        head = subprocess.run(["git", "rev-parse", "HEAD"], **quiet, text=True).stdout.strip()
        try:
            written = json.loads(stamp.read_text(encoding="utf-8")).get("commit")
        except (OSError, ValueError):
            written = None
        if written != head or done.returncode != 0:
            failures.append(f"a full run from a clean working tree stamped {written}, not {head}, "
                            f"or failed\n{done.stdout}{done.stderr}")
        step("after a full run, nothing changed", always)
        failures += push_through_the_hook(root, pathlib.Path(scratch) / "remote.git", head, steps)
    failures += shader_records_end_to_end(cmake, ctest, ninja, steps)
    failures += script_records_end_to_end(cmake, ctest, ninja, steps)

    for failure in failures:
        print(f"end-to-end FAILED: {failure}")
    print(f"run-tests end-to-end: {len(steps) - len(failures)} of {len(steps)} steps")
    return 1 if failures else 0


def push_through_the_hook(root: pathlib.Path, remote: pathlib.Path, head: str, steps: list) -> list:
    """The pre-push hook as git runs it (register decision 603): this
    repository's scripts/git-hooks/pre-push, on the scratch repository, whose
    scripts/ gets a copy of this driver -- the hook runs the one beside the
    repository it guards. A push is refused without both trees' stamps and
    with stamps at another commit, and goes through with stamps at the commit
    pushed."""
    failures = []
    quiet = {"capture_output": True, "text": True, "encoding": "utf-8", "errors": "replace", "check": False}
    subprocess.run(["git", "init", "-q", "--bare", str(remote)], **quiet)
    shutil.copy(__file__, root / "scripts" / "run-tests.py")
    hooks = (ROOT / "scripts" / "git-hooks").as_posix()

    def stamp(commit) -> None:
        for tree in CHECK_TREES:
            state = root / tree / STATE
            state.mkdir(parents=True, exist_ok=True)
            if commit is None:
                (state / FULL_STAMP).unlink(missing_ok=True)
            else:
                (state / FULL_STAMP).write_text(json.dumps({"commit": commit}), encoding="utf-8")

    def push(name: str, commit, through: bool) -> None:
        stamp(commit)
        done = subprocess.run(["git", "-c", f"core.hooksPath={hooks}", "push", str(remote), "HEAD:refs/heads/master"],
                              cwd=root, **quiet)
        landed = subprocess.run(["git", "--git-dir", str(remote), "rev-parse", "--verify", "-q", "refs/heads/master"],
                                **quiet).stdout.strip()
        steps.append(name)
        if (done.returncode == 0) != through or (landed == head) != through:
            failures.append(f"{name}: the push {'went through' if done.returncode == 0 else 'was refused'} and "
                            f"the remote holds {landed or 'nothing'}\n{done.stdout}{done.stderr}")

    push("a push with no full check stamped", None, through=False)
    push("a push with full checks stamped at another commit", "3" * 40, through=False)
    push("a push with full checks stamped at the commit pushed", head, through=True)
    return failures


# --- the selection by function from end to end (M1-132, decision 713 (4)) ------

# The scratch project of run_tests_function_level. It is built with the tools
# that the test names and with no other -- the compiler, the archiver and the
# linker by their paths -- as the trees with the hook build their programs:
# C++ with the hook, -ffunction-sections and -fdata-sections; the recorder,
# tests/CoverageRecorder.cpp, and one object of plain code without the hook;
# each program linked by lld-link with /map, /debug and /Brepro. prog_a takes
# a member of a library with the hook, core.lib. A glob finds the files in
# extra/, so that an object can go away with no change to CMakeLists.txt, on
# which the cautious rule fingerprints each test of the project.
SCRATCH_HOOK_PROJECT = r"""cmake_minimum_required(VERSION 3.25)
project(scratch LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> qcs <TARGET> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_FINISH "")
set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_LINKER> /nologo <OBJECTS> /out:<TARGET> /pdb:<TARGET_PDB> /map /debug /Brepro\
 /subsystem:console <LINK_LIBRARIES>")
set(CMAKE_CXX_STANDARD_LIBRARIES kernel32.lib)
add_compile_options(-g -gcodeview -mno-incremental-linker-compatible)
set(hook -fsanitize-coverage=func,inline-bool-flag,pc-table -ffunction-sections -fdata-sections)
add_library(recorder OBJECT tests/CoverageRecorder.cpp)
target_include_directories(recorder PRIVATE ${CMAKE_SOURCE_DIR})
add_library(plain OBJECT plain.cpp)
add_library(core STATIC core.cpp)
target_compile_options(core PRIVATE ${hook})
file(GLOB extra CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/extra/*.cpp)
add_executable(prog_a main.cpp ${extra})
target_link_libraries(prog_a PRIVATE core recorder plain)
add_executable(prog_b other.cpp)
target_link_libraries(prog_b PRIVATE recorder)
foreach(program prog_a prog_b)
    target_compile_options(${program} PRIVATE ${hook})
endforeach()
enable_testing()
add_test(NAME a_alpha COMMAND prog_a alpha "two words" [[a"b]] [[c\]] "")
add_test(NAME a_beta COMMAND prog_a beta)
add_test(NAME usage_alpha COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:prog_a> -DMODE=alpha
         -P ${CMAKE_SOURCE_DIR}/cmake/RunUsage.cmake)
add_test(NAME other_b COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:prog_b> -DMODE=b
         -P ${CMAKE_SOURCE_DIR}/cmake/Other.cmake)
add_test(NAME elsewhere_b COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:prog_b> -DMODE=b
         -P ${CMAKE_SOURCE_DIR}/elsewhere/RunUsage.cmake)
foreach(test a_beta usage_alpha other_b elsewhere_b)
    set_tests_properties(${test} PROPERTIES ENVIRONMENT ORBSIM_TEST_NAME=${test})
endforeach()
"""
# a_alpha enters main and alpha, a_beta main, beta, helper and core.lib's
# coreValue; unused is linked and entered by no test.
SCRATCH_MAIN = r"""#include <cstring>
int coreValue();
int plainValue();
int alpha() { return 1; }
int helper() { return 2; }
int beta() { return helper() + coreValue(); }
int unused() { return 3; }
int main(int argc, char** argv) {
    if (argc > 100) return unused() + plainValue();
    if (argc > 1 && std::strcmp(argv[1], "alpha") == 0) return alpha() == 1 ? 0 : 1;
    if (argc > 1 && std::strcmp(argv[1], "beta") == 0) return beta() == 6 ? 0 : 1;
    return 1;
}
"""
SCRATCH_SOURCES = {
    "core.cpp": "int coreValue() { return 4; }\n",
    "plain.cpp": "int plainValue() { return 5; }\n",
    "other.cpp": "int unusedB() { return 7; }\nint main(int argc, char**) { return argc > 100 ? unusedB() : 0; }\n",
    "extra/extra.cpp": "int extraValue() { return 8; }\n",
}
# The three scripts: each starts the program that the test names, and reads
# only its exit code. Only cmake/RunUsage.cmake is one of LISTED_SCRIPTS.
SCRATCH_RUN_SCRIPT = """execute_process(COMMAND ${PROGRAM} ${MODE} RESULT_VARIABLE code)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "${PROGRAM} ${MODE}: exit code ${code}")
endif()
"""
SCRATCH_SCRIPTS = ("cmake/RunUsage.cmake", "cmake/Other.cmake", "elsewhere/RunUsage.cmake")


def function_level(tools: list) -> int:
    """The selection by function on a scratch project with the hook (decision
    713 (4)), built with the tools that the command line names: CMake, CTest,
    Ninja, clang++, llvm-ar, lld-link, and tests/CoverageRecorder.cpp with
    tests/WriterTurns.hpp. The driver runs on it as run_tests_end_to_end runs
    it. At each step, the driver must run the tests that the change affects
    and no other test, and it must print no problem, because a problem makes
    `check` fail."""
    names = ("CMake", "CTest", "Ninja", "the compiler", "the archiver", "the linker", "the recorder", "its header")
    missing = [f"{name}, {path}" for name, path in zip(names, tools) if not pathlib.Path(path).is_file()]
    for line in missing:
        print(f"function-level FAILED: not a file: {line}")
    if missing:
        return 1
    cmake, ctest, ninja, compiler, archiver, linker, recorder, turns = tools
    began = time.monotonic()
    # The driver names the folder of the records to each test that it runs; a
    # folder from the run that started this test must not go to the scratch
    # programs. A test name stays in the driver's environment, as CTest gives
    # it to this test: the driver removes it (decision 715 (2)).
    env = {k: v for k, v in os.environ.items() if k.upper() != RECORDS_VARIABLE}
    env[TEST_NAME_VARIABLE] = "run_tests_function_level"
    quiet = {"capture_output": True, "text": True, "encoding": "utf-8", "errors": "replace", "check": False, "env": env}
    # The steps that failed; after a build that failed, no step runs.
    failures, steps, failed = [], [], []
    with tempfile.TemporaryDirectory() as scratch:
        root = pathlib.Path(scratch).resolve() / "repo"
        tree = root / "build"
        sources = {"CMakeLists.txt": SCRATCH_HOOK_PROJECT, "main.cpp": SCRATCH_MAIN, ".gitignore": "build/\n",
                   **SCRATCH_SOURCES, **dict.fromkeys(SCRATCH_SCRIPTS, SCRATCH_RUN_SCRIPT)}
        for name, text in sources.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_bytes(text.encode("utf-8"))
        (root / "tests").mkdir()
        for name, source in (("CoverageRecorder.cpp", recorder), ("WriterTurns.hpp", turns)):
            (root / "tests" / name).write_bytes(pathlib.Path(source).read_bytes())

        def call(what: str, command: list, cwd=root) -> bool:
            done = subprocess.run([str(c) for c in command], cwd=cwd, **quiet)
            if done.returncode != 0:
                failures.append(f"{what}: exit code {done.returncode}\n{done.stdout}{done.stderr}")
            return done.returncode == 0

        def edit(name: str, old: str, new: str) -> None:
            text = (root / name).read_text(encoding="utf-8")
            if old not in text:
                failures.append(f"the scratch file {name} does not hold {old!r}")
            (root / name).write_bytes(text.replace(old, new).encode("utf-8"))

        def step(name: str, wanted: set, full: bool = False, problem: str = None, removed: str = None,
                 said: str = None, test_name: str = None) -> None:
            # `problem`: a text that the driver must print as a problem, and
            # then fail; `removed`: a file of the tree removed after the build;
            # `said`: a text that the driver must print; `test_name`: a
            # different ORBSIM_TEST_NAME in the driver's environment.
            if "build" in failed:
                return
            steps.append(name)
            if not call(f"{name}: the build", [cmake, "--build", tree]):
                failed.extend((name, "build"))
                return
            if removed is not None:
                (tree / removed).unlink()
            (tree / STATE / REPORT).unlink(missing_ok=True)
            done = subprocess.run([sys.executable, __file__, str(tree), "--ctest", ctest, "--root", str(root)]
                                  + (["--full"] if full else []),
                                  **{**quiet, "env": {**env, TEST_NAME_VARIABLE: test_name or env[TEST_NAME_VARIABLE]}})
            try:
                ran = set(outcomes((tree / STATE / REPORT).read_text(encoding="utf-8")))
            except OSError:
                ran = set()
            failed_as_it_must = done.returncode != 0 and problem in done.stdout if problem else done.returncode == 0
            if ran != wanted or not failed_as_it_must or (said is not None and said not in done.stdout):
                failed.append(name)
                failures.append(f"{name}: ran {sorted(ran)}, wanted {sorted(wanted)}, exit code "
                                f"{done.returncode}" + (f", the problem {problem!r} wanted" if problem else "")
                                + (f", the text {said!r} wanted" if said else "") + f"\n{done.stdout}{done.stderr}")
            # What the driver said of the step: with "the compiled code" only
            # where it read a program, because a program's bytes changed.
            said = [line for line in done.stdout.splitlines() if " ran, " in line]
            print(f"function-level: {name}: ran {sorted(ran)}; {said[-1] if said else 'no summary line'}")

        call("git init", ["git", "init", "-q"])
        if call("the configure", [cmake, "-S", root, "-B", tree, "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}",
                                  f"-DCMAKE_CXX_COMPILER={compiler}", f"-DCMAKE_AR={archiver}",
                                  f"-DCMAKE_LINKER={linker}"]):
            function_level_steps(root, step, edit)
            call("the commit", ["git", "-c", "user.name=t", "-c", "user.email=t@t", "add", "-A"])
            call("the commit", ["git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "s"])
            # The summaries kept (decision 714): a run that is not full
            # deletes none, and the full run deletes only the summary of the
            # object that no program links now. The driver reads prog_b too,
            # because it compares each record with the hook's table of its
            # program (decision 704).
            name = script_module("compiled-code").SummaryCache.file_name
            linked = {name(key) for key in ("CMakeFiles/prog_a.dir/main.cpp.obj", "core.lib(core.cpp.obj)",
                                            "CMakeFiles/prog_b.dir/other.cpp.obj")}
            gone = name("CMakeFiles/prog_a.dir/extra/extra.cpp.obj")
            summaries = tree / STATE / OBJECTS

            def listed() -> set:
                return {p.name for p in summaries.iterdir()} if summaries.is_dir() else set()

            full_step, before = "a full run: each test", listed()
            step(full_step, {"a_alpha", "a_beta", "usage_alpha", "other_b", "elsewhere_b"}, full=True)
            if "build" not in failed and (before != linked | {gone} or listed() != linked):
                failed.append(full_step)
                failures.append(f"the summaries {sorted(before)} before the full run and {sorted(listed())} after "
                                "it: before it, those of the three objects with the hook that the programs link and "
                                "of the one that prog_a no longer links; after it, only those of the three")
    for failure in failures:
        print(f"function-level FAILED: {failure}")
    print(f"run-tests function-level: {len(steps) - len(set(failed) - {'build'})} of {len(steps)} steps, "
          f"{len(failures)} failures, {time.monotonic() - began:.1f} s")
    return 1 if failures else 0


def function_level_steps(root: pathlib.Path, step, edit) -> None:
    """The steps of run_tests_function_level before the full run, each a change
    and the tests that it must run. The tests of prog_a that start it
    directly, a_alpha and a_beta, and the test through the listed script,
    usage_alpha, take it function by function; the two tests of prog_b go
    through scripts that are not listed, and keep its bytes."""
    of_a, of_b = {"a_alpha", "a_beta", "usage_alpha"}, {"other_b", "elsewhere_b"}
    step("the first run: each test", of_a | of_b)
    step("nothing changed, a different ORBSIM_TEST_NAME in the driver's environment: no test", set(),
         test_name="a different test")
    edit("main.cpp", "int alpha() { return 1; }", "int alpha() { volatile int one = 1; return one; }")
    step("a function's body changed: only the tests that entered it, by their records", {"a_alpha", "usage_alpha"})
    edit("main.cpp", "int unused() { return 3; }", "int unused() { return 30; }")
    step("a function that no test entered changed: no test of its program", set())
    edit("main.cpp", "#include <cstring>\n", "// A comment, which moves each line below it.\n#include <cstring>\n")
    step("a comment added: no test", set())
    edit("main.cpp", "int helper() { return 2; }\nint beta() { return helper() + coreValue(); }",
         "int beta() { return 2 + coreValue(); }")
    step("a function that a test entered removed: that test", {"a_beta"})
    edit("plain.cpp", "return 5;", "return 50;")
    step("an object without the hook changed: each test of its program", of_a)
    edit("other.cpp", "return 7;", "return 70;")
    step("a program that the scripts not listed name on the command line: its tests, by its bytes", of_b)
    # The records of the two programs, which did not change, are examined from
    # their kept tables; prog_a is then read in full for its function part,
    # because its tests take it function by function, and prog_b is not read.
    edit("cmake/Other.cmake", "endif()\n", "endif()\n# A script changed.\n")
    step("a script changed: each test, and the records of the programs, which did not change, examined from their "
         "kept tables", of_a | of_b, said="programs read in full: 1, from kept tables: 2")
    edit("main.cpp", "return 30;", "return 31;")
    step("a problem at the decision, the map of a changed program removed: the tests of the program run, and the "
         "run fails", of_a, problem="prog_a.map is not there", removed="prog_a.map")
    (root / "extra" / "extra.cpp").unlink()
    step("an object removed from a program: each test of that program, which the step before did not store", of_a)

# --- the self-test -------------------------------------------------------------------

class StandInWorld:
    """Hashing as the self-test sees it: a digest of each file's bytes and
    path, and no git, no import tables -- each program's libraries are what
    `library_map` says -- so that Run.parts() can be judged on a tree of a
    few files."""

    def __init__(self):
        self.library_map = {}

    def file(self, path) -> str:
        try:
            return digest(f"{norm(path)} {hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()}")
        except OSError:
            return MISSING

    def files(self, paths) -> str:
        return digest("\n".join(sorted(self.file(p) for p in paths if pathlib.Path(p).is_file())))

    def program(self, path) -> str:
        return self.file(path)

    def libraries(self, program) -> list:
        return self.library_map.get(norm(program), [])

    def hooked(self, path) -> bool:
        try:
            return holds_hook_table(pathlib.Path(path).read_bytes())
        except OSError:
            return False

    def unignored_under(self, folder) -> list:
        return self.everything_under(folder)

    def everything_under(self, folder) -> list:
        folder = pathlib.Path(folder)
        return [str(folder)] if folder.is_file() else sorted(str(p) for p in folder.rglob("*") if p.is_file())

    def listed(self) -> list:
        return []


def stand_in_run(tree: pathlib.Path, root: pathlib.Path) -> "Run":
    """A Run over a stand-in tree, without asking CTest or the card."""
    run = Run.__new__(Run)
    run.tree, run.root, run.ctest = tree, root, "ctest"
    run.state = tree / STATE
    run.world = StandInWorld()
    run.tests = {}
    run.app = tree / (APPLICATION + (".exe" if os.name == "nt" else ""))
    run._machine, run._card, run._card_read, run._shared = "machine", {"gpu": "a card"}, True, {}
    run.leap_seconds = lambda: "3 "
    run.capacity = lambda: "memory 1 MiB, processors 1"
    run.start_function_level(None, None)
    return run


# The environment the self-test hands Run.parts().
STAND_IN_ENVIRONMENT = {"PATH": "/stand-in/bin", "VK_LAYER_PATH": "/stand-in/layers", "ORBSIM_X": "x",
                        "HOME": "/stand-in/home"}

# What the tools the driver reads answered on this project's own programs,
# recorded 2026-10-16 (register decision 602), so that the self-test reads
# their real layout. llvm-readobj 23.1.2 --needed-libs, on the Debug tree's
# application and SDL3.dll:
SAMPLE_READOBJ = """File: build/debug/orbsim.exe
Format: COFF-x86-64
Arch: x86_64
AddressSize: 64bit
NeededLibraries [
  KERNEL32.dll
  MSVCP140D.dll
  SDL3_d.dll
  VCRUNTIME140D.dll
  VCRUNTIME140_1D.dll
  ucrtbased.dll
  vulkan-1.dll
]

File: build/debug/SDL3.dll
Format: COFF-x86-64
Arch: x86_64
AddressSize: 64bit
NeededLibraries [
  ADVAPI32.dll
  GDI32.dll
  IMM32.dll
  KERNEL32.dll
  OLEAUT32.dll
  SETUPAPI.dll
  SHELL32.dll
  USER32.dll
  VCRUNTIME140D.dll
  VERSION.dll
  WINMM.dll
  ole32.dll
  ucrtbased.dll
]
"""
# ldd on the linux-gcc tree's test_math, under WSL's Ubuntu 26.04:
SAMPLE_LDD = """\tlinux-vdso.so.1 (0x00007efd0130e000)
\tlibstdc++.so.6 => /usr/lib/x86_64-linux-gnu/libstdc++.so.6 (0x00007efd00e00000)
\tlibm.so.6 => /usr/lib/x86_64-linux-gnu/libm.so.6 (0x00007efd00cda000)
\tlibgcc_s.so.1 => /usr/lib/x86_64-linux-gnu/libgcc_s.so.1 (0x00007efd01114000)
\tlibc.so.6 => /usr/lib/x86_64-linux-gnu/libc.so.6 (0x00007efd00a00000)
\t/lib64/ld-linux-x86-64.so.2 (0x00007efd01310000)
"""
# The clear probe's sidecar in the Debug tree, its lines from the card on:
SAMPLE_SIDECAR = """# orbsim probe sidecar (M1-16): what produced the files beside it
probe                = clear
outcome              = rendered
gpu                  = NVIDIA RTX A2000 8GB Laptop GPU
gpu.vendor           = 0x10de
gpu.device           = 0x25ba
driver.name          = NVIDIA
driver.info          = 582.53
driver.version       = 0x918d4000
vulkan.api           = 1.4.312 (variant 0)
validation.layer     = 1.4.357 (variant 0), implementation 1
build.configuration  = Debug
shaders              = fullscreen.vert.spv probe_gradient.frag.spv tonemap.frag.spv
"""
# test_math --list-listeners in the Debug tree:
SAMPLE_LISTENERS = """Registered listeners:
  AbortWithoutADialog:     (No description provided)
  RecordWhatEachCaseRead:  (No description provided)

"""


def readobj_text(path, names: list) -> str:
    """llvm-readobj --needed-libs's answer for one file, in SAMPLE_READOBJ's layout."""
    listed = "".join(f"  {name}\n" for name in names)
    return f"File: {path}\nFormat: COFF-x86-64\nArch: x86_64\nAddressSize: 64bit\nNeededLibraries [\n{listed}]\n\n"


def each_part_moves_with_its_input(expect) -> None:
    """Each part of a fingerprint moves when its input changes (register
    decision 602). The self-test's other cases check which parts a test has;
    a part computed from the wrong input, or from none, has them all and is
    caught only here -- eight such faults passed both of the driver's tests
    in a spike on 2026-10-16."""
    with tempfile.TemporaryDirectory() as scratch:
        base = pathlib.Path(scratch)
        root, outside = base / "repo", base / "tools"
        tree = root / "build"
        exe = ".exe" if os.name == "nt" else ""
        suite_program, cmake, compiler = tree / f"suite{exe}", outside / f"cmake{exe}", outside / f"clang{exe}"
        compiler_library, suite_library = outside / "clang-support.dll", outside / "suite-support.dll"
        shader, document = tree / "shaders" / "sky.frag.spv", root / "docs" / "a.md"
        linker = outside / f"lld-link{exe}"
        for path in (suite_program, tree / f"{APPLICATION}{exe}", shader, cmake, compiler, compiler_library,
                     suite_library, root / "cmake" / "RunProbe.cmake", document, linker):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("x", encoding="utf-8")
            if os.name != "nt":
                path.chmod(0o755)
        (tree / "CMakeCache.txt").write_text(
            f"CMAKE_COMMAND:INTERNAL={cmake}\nCMAKE_CXX_COMPILER:FILEPATH={compiler}\n"
            f"CMAKE_LINKER:FILEPATH={linker}\n", encoding="utf-8")
        suite = {"name": "s", "command": [str(suite_program), "s"]}
        probe = {"name": "p", "command": [str(cmake), f"-DORBSIM={tree / (APPLICATION + exe)}", "-P",
                                          str(root / "cmake" / "RunProbe.cmake")],
                 "properties": [{"name": "LABELS", "value": [GPU_LABEL]}]}
        folder = {"name": "f", "command": [str(cmake), "-P", str(root / "cmake" / "RunProbe.cmake"),
                                           str(root / "docs")]}

        def part(test, name, env=None, leap_seconds="3 ", capacity="memory 1 MiB, processors 1"):
            # A fresh Run each time, so that nothing it computed earlier is
            # reused; the machine part computed rather than stood in for.
            run = stand_in_run(tree, root)
            run.ctest, run._machine = str(cmake), None
            run.leap_seconds = lambda: leap_seconds
            run.capacity = lambda: capacity
            run.world.library_map = {norm(compiler): [str(compiler_library)]}
            return run.parts(test, [], STAND_IN_ENVIRONMENT if env is None else env).get(name)

        definition_before = part(suite, "definition")
        expect("another command line moves the definition",
               part({**suite, "command": suite["command"] + ["--order", "rand"]}, "definition") != definition_before,
               True)
        expect("another property moves the definition",
               part({**suite, "properties": [{"name": "TIMEOUT", "value": 60.0}]}, "definition") != definition_before,
               True)

        environment_before = part(suite, "environment")
        for name, value in (("PATH", "/elsewhere"), ("VK_ICD_FILENAMES", "a.json"), ("ORBSIM_X", "y")):
            expect(f"{name} moves the environment",
                   part(suite, "environment", {**STAND_IN_ENVIRONMENT, name: value}) != environment_before, True)
        expect("HOME leaves the environment alone",
               part(suite, "environment", {**STAND_IN_ENVIRONMENT, "HOME": "/elsewhere"}), environment_before)

        machine_before = part(suite, "machine")
        compiler.write_text("another compiler", encoding="utf-8")
        machine_after_compiler = part(suite, "machine")
        expect("another compiler moves the machine part", machine_after_compiler != machine_before, True)
        compiler_library.write_text("another library", encoding="utf-8")
        machine_after_library = part(suite, "machine")
        expect("another library of the compiler moves the machine part",
               machine_after_library != machine_after_compiler, True)
        # Decision 615: Windows' leap seconds, from the registry, as the
        # value reads -- here a table of one second where there was none.
        expect("another leap-second table in the registry moves the machine part",
               part(suite, "machine", leap_seconds="3 1e00000000000000ff") != machine_after_library, True)
        # Decision 794: the memory and the processors, from which memory_pool
        # and parallel_tests calculate what they examine.
        expect("more memory moves the machine part",
               part(suite, "machine", capacity="memory 2 MiB, processors 1") != machine_after_library, True)
        expect("more processors move the machine part",
               part(suite, "machine", capacity="memory 1 MiB, processors 2") != machine_after_library, True)
        # Decision 583: the linker, for the code that is not in the tree and
        # its own pieces of each program.
        linker.write_text("a different linker", encoding="utf-8")
        expect("a different linker moves the machine part", part(suite, "machine") != machine_after_library, True)

        # Decision 706: the libraries that a program taken function by
        # function loads at start, a part of their own; its bytes not in the
        # part of the programs, because the store holds them.
        def taken_part(name):
            run = stand_in_run(tree, root)
            run.world.library_map = {norm(suite_program): [str(suite_library)]}
            return run.parts(suite, [], STAND_IN_ENVIRONMENT, taken={norm(suite_program)}).get(name)

        libraries_before = taken_part("libraries")
        suite_library.write_text("a library of the suite", encoding="utf-8")
        expect("a changed library of a program taken function by function moves the libraries part",
               taken_part("libraries") != libraries_before, True)
        expect("and a program taken function by function is not in the part of the programs",
               taken_part("programs"), None)
        suite_library.write_text("x", encoding="utf-8")

        shaders_before = part(probe, "shaders")
        shader.write_text("another shader", encoding="utf-8")
        expect("a changed shader moves the application's tests", part(probe, "shaders") != shaders_before, True)

        folder_before = part(folder, "files")
        document.write_text("another document", encoding="utf-8")
        expect("a changed file in a folder on the command line moves the test",
               part(folder, "files") != folder_before, True)

        # The real World, its library list stood in for: a program is its own
        # bytes and those of every library it loads (decision 525).
        def program_part():
            world = World(tree, root)
            world.libraries = lambda program: [str(suite_library)]
            return world.program(suite_program)

        program_before = program_part()
        suite_library.write_text("another library", encoding="utf-8")
        expect("a changed library moves its program's fingerprint", program_part() != program_before, True)


def tools_read_as_they_answer(expect) -> None:
    """The driver's readings of llvm-readobj, ldd, the clear probe's sidecar
    and --list-listeners, on what each answered on this project's programs
    (register decision 602)."""
    found = imports_by_file(SAMPLE_READOBJ)
    expect("llvm-readobj's answer, read", found.get(norm("build/debug/orbsim.exe")),
           ["KERNEL32.dll", "MSVCP140D.dll", "SDL3_d.dll", "VCRUNTIME140D.dll", "VCRUNTIME140_1D.dll",
            "ucrtbased.dll", "vulkan-1.dll"])
    expect("llvm-readobj's second file, read", len(found.get(norm("build/debug/SDL3.dll")) or []), 13)

    with tempfile.TemporaryDirectory() as scratch:
        folder = pathlib.Path(scratch)
        program, first, second = folder / "prog.exe", folder / "first.dll", folder / "second.dll"
        for path in (program, first, second):
            path.write_text("x", encoding="utf-8")
        answers = {norm(program): ["first.dll", "api-ms-win-crt-runtime-l1-1-0.dll", "orbsim-no-such-library.dll"],
                   norm(first): ["second.dll"], norm(second): []}
        world = World(folder, folder)
        world.import_text = lambda files: "".join(readobj_text(f, answers[norm(f)]) for f in files)
        world.learn_imports([program])
        closure = sorted(p if p.startswith("<") else norm(p) for p in world._windows_closure(program))
        expect("libraries are followed through the libraries they load, an API set left out", closure,
               sorted([norm(first), norm(second), "<not found: orbsim-no-such-library.dll>"]))

    expect("ldd's answer, read", ldd_libraries(SAMPLE_LDD + "\tlibgone.so.1 => not found\n"),
           sorted(["/usr/lib/x86_64-linux-gnu/libstdc++.so.6", "/usr/lib/x86_64-linux-gnu/libm.so.6",
                   "/usr/lib/x86_64-linux-gnu/libgcc_s.so.1", "/usr/lib/x86_64-linux-gnu/libc.so.6",
                   "/lib64/ld-linux-x86-64.so.2", "<not found: libgone.so.1>"]))

    card = card_values(SAMPLE_SIDECAR)
    expect("the card, read from the sidecar", (card or {}).get("gpu.device"), "0x25ba")
    expect("the card's eight lines, read", sorted(card or {}), sorted(CARD_LINES))
    without_layer = SAMPLE_SIDECAR.replace("validation.layer     = 1.4.357 (variant 0), implementation 1",
                                           "validation.layer     = none: not requested")
    expect("a card judged without the validation layer is not taken", card_values(without_layer), None)
    without_device = "".join(line + "\n" for line in SAMPLE_SIDECAR.splitlines() if not line.startswith("gpu.device"))
    expect("a sidecar that does not name the device is not taken", card_values(without_device), None)

    expect("the memory and the processors of this machine, read",
           bool(re.fullmatch(r"memory [1-9][0-9]* MiB, processors [1-9][0-9]*", machine_capacity())), True)

    expect("the listener, found", lists_listener(0, SAMPLE_LISTENERS), True)
    expect("no listener, none found",
           lists_listener(0, SAMPLE_LISTENERS.replace("RecordWhatEachCaseRead", "SomethingElse")), False)
    expect("a program that failed lists nothing", lists_listener(1, SAMPLE_LISTENERS), False)


def shader_record(test: str, *shaders, end: bool = True) -> str:
    """A shader record as the application writes it (decision 644)."""
    return f"test {test}\n" + "".join(f"{s}\n" for s in shaders) + ("end\n" if end else "")


def shader_record_cases(expect) -> None:
    """The shaders a test's runs asked for (M1-131, decisions 629, 635, 637 and
    659 e): a test is fingerprinted on the compiled files its records name, and
    on every shader where it left none, or one not whole."""
    expect("a whole shader record, its separators evened out",
           parse_shader_record(shader_record("t", "C:\\x/shaders\\a.spv")), ("t", [norm("C:/x/shaders/a.spv")]))
    expect("a whole shader record of no shader", parse_shader_record(shader_record("t")), ("t", []))
    expect("a shader record without its end line is not whole",
           parse_shader_record(shader_record("t", "C:/x/a.spv", end=False)), ("t", None))
    expect("a shader record cut off inside a line is not whole",
           parse_shader_record(shader_record("t", "C:/x/a.spv")[:-2]), ("t", None))
    expect("a shader record with an empty line is not whole",
           parse_shader_record(shader_record("t", "C:/x/a.spv", "")), ("t", None))
    expect("a shader record naming a path that is not absolute is not whole",
           parse_shader_record(shader_record("t", "shaders/a.spv")), ("t", None))
    expect("a shader record without its test line names no test", parse_shader_record("C:/x/a.spv\nend\n"), None)

    # The hash of the application on the list of a skipped test (M1-133,
    # decisions 737 and 739): without it, a change to the application in no
    # function that the probes entered made mutants_due_probe_shaders fail.
    app = pathlib.Path("C:/t/orbsim.exe")
    hashes = {norm(app): "new"}
    stored = {"skipped": {"shaders": ["C:/t/a.spv"], "application": "old"},
              "ran": {"shaders": ["C:/t/a.spv"], "application": "old"},
              "listless": {"parts": {}}}
    hashed, renewed = renewed_applications(stored, ["skipped", "listless"],
                                           lambda path: hashes.get(norm(path), "another file"), app)
    expect("a skipped test keeps its shader list trusted when the application changes",
           (stored["skipped"]["application"], hashed, renewed), ("new", "new", ["skipped"]))
    expect("a test that runs keeps the application of its last pass until it passes again",
           stored["ran"]["application"], "old")
    expect("a skipped test with no shader list gets no application", "application" in stored["listless"], False)
    expect("no list changes where there is no application",
           renewed_applications({"skipped": {"shaders": [], "application": "old"}}, ["skipped"],
                                lambda path: MISSING, app), (MISSING, []))

    with tempfile.TemporaryDirectory() as scratch:
        base = pathlib.Path(scratch)
        root, outside = base / "repo", base / "tools"
        tree = root / "build"
        exe = ".exe" if os.name == "nt" else ""
        app, cmake, script = tree / f"{APPLICATION}{exe}", outside / f"cmake{exe}", root / "cmake" / "RunProbe.cmake"
        a, b = tree / "shaders" / "a.frag.spv", tree / "shaders" / "b.frag.spv"
        for path in (app, cmake, script, a, b):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("x", encoding="utf-8")
            if os.name != "nt":
                path.chmod(0o755)
        names = {"name": "p", "command": [str(cmake), f"-DORBSIM={app}", "-P", str(script)]}
        starts = {"name": "s", "command": [str(cmake), "-P", str(script)]}
        # As the renderer is given it: the shader folder joined with a
        # backslash (decision 659 e).
        a_given, b_given = f"{tree / 'shaders'}\\{a.name}", f"{tree / 'shaders'}\\{b.name}"

        def part(test, texts):
            listed = shaders_by_test(texts).get(test["name"])
            return stand_in_run(tree, root).parts(test, [], STAND_IN_ENVIRONMENT, listed).get("shaders")

        def moves(test, texts, changed) -> bool:
            before = part(test, texts)
            changed.write_text(changed.read_text(encoding="utf-8") + "+", encoding="utf-8")
            return part(test, texts) != before

        named_a = [shader_record("p", a_given)]
        expect("a changed shader its record names reruns the test", moves(names, named_a, a), True)
        expect("a changed shader its record does not name leaves the test alone", moves(names, named_a, b), False)
        expect("a test naming the application that left no record keeps every shader", moves(names, [], b), True)
        expect("a record without its end line keeps every shader",
               moves(names, [shader_record("p", a_given, end=False)], b), True)
        expect("one record of two not whole keeps every shader",
               moves(names, named_a + [shader_record("p", a_given, end=False)], b), True)
        expect("a whole record of no shader leaves the test alone", moves(names, [shader_record("p")], a), False)
        expect("the same file spelt with either separator is one file",
               shaders_by_test([shader_record("p", a_given), shader_record("p", str(a).replace("\\", "/"))]).get("p"),
               [norm(a)])
        expect("two records of one test are combined",
               shaders_by_test([shader_record("p", a_given), shader_record("p", b_given)]).get("p"),
               sorted([norm(a), norm(b)]))
        both = [shader_record("p", a_given), shader_record("p", b_given)]
        expect("a changed shader either of its records names reruns the test",
               moves(names, both, a) and moves(names, both, b), True)
        expect("each test takes the records carrying its own name, and no other's",
               shaders_by_test([shader_record("p", a_given), shader_record("p-x", b_given)]),
               {"p": [norm(a)], "p-x": [norm(b)]})
        expect("a script that starts the application is fingerprinted on its records' shaders",
               moves(starts, [shader_record("s", a_given)], a), True)
        expect("and not on the shaders they do not name", moves(starts, [shader_record("s", a_given)], b), False)
        expect("a test that neither names the application nor left a record has no shader part",
               part(starts, []), None)
        expect("a record not whole gives a test that does not name the application every shader",
               moves(starts, [shader_record("s", a_given, end=False)], b), True)

        folder = tree / COVERAGE_RECORDS
        folder.mkdir()
        (folder / f"12-34{SHADER_RECORD}").write_bytes(shader_record("p", a_given).encode("utf-8"))
        (folder / "12-34.functions.txt").write_bytes(b"program x\nend 0\n")
        texts, unreadable = shader_record_texts(folder)
        expect("the folder's shader records are read, the coverage recorder's beside them not",
               (texts, unreadable), ([shader_record("p", a_given)], []))


# --- the self-test of the selection by function (M1-132) -------------------------

# Microsoft's "Parsing C++ command-line arguments", the table "Results of
# parsing command lines": each input, after the name of a program, and the
# arguments after the program.
DOCUMENTED_COMMAND_LINES = [
    ('"abc" d e', ["abc", "d", "e"]),
    (r'a\\b d"e f"g h', [r"a\\b", "de fg", "h"]),
    (r'a\\\"b c d', [r'a\"b', "c", "d"]),
    (r'a\\\\"b c" d e', [r"a\\b c", "d", "e"]),
    ('a"b"" c d', ['ab" c d']),
]
# The first argument by its own rule: a quote only starts or ends quotes, and
# a backslash before a quote is a character.
PROGRAM_COMMAND_LINES = [
    (r'"C:\Program Files\p.exe" x', [r"C:\Program Files\p.exe", "x"]),
    (r'"C:\a\"b c', [r"C:\a\b", "c"]),
    ("p.exe\ta\t\tb ", ["p.exe", "a", "b"]),
    ('p.exe "a b', ["p.exe", "a b"]),
    ('p.exe "" x ""', ["p.exe", "", "x", ""]),
    ("", [""]),
]
# A command line that a coverage record gave, and the command that CTest
# listed for the test: CTest 4.3.1, from CLion, on 2026-10-21, a scratch
# project with add_test(NAME a COMMAND prog_a alpha "two words" [[a"b]] [[c\]]
# "" [[d\"e]] [[f\g]]) -- the folder of the program made shorter.
SAMPLE_RECORD_COMMAND = r'C:/x/build/prog_a.exe alpha "two words" "a\"b" c\ "" "d\\\"e" f\g'
SAMPLE_CTEST_COMMAND = ["C:/x/build/prog_a.exe", "alpha", "two words", 'a"b', "c\\", "", 'd\\"e', "f\\g"]


def quoted_argument(text: str) -> str:
    """An argument with the quotes that libuv gives it when CTest starts a test
    (quote_cmd_arg in libuv's src/win/process.c): a second implementation, the
    opposite of split_command_line, for the self-test. Each backslash before a
    quote or before the closing quote becomes two, and a quote gets a
    backslash."""
    if not text:
        return '""'
    if not any(c in text for c in ' \t"'):
        return text
    if not any(c in text for c in '"\\'):
        return f'"{text}"'
    out, slashes = ['"'], 0
    for c in text:
        if c == "\\":
            slashes += 1
            continue
        out.append("\\" * (2 * slashes + 1) + '"' if c == '"' else "\\" * slashes + c)
        slashes = 0
    out.append("\\" * (2 * slashes) + '"')
    return "".join(out)


def coverage_record(program: str, command: str, test: str = "", flags: int = 0, addresses=(),
                    ended: str = "normally", whole: bool = True) -> str:
    """A coverage record as tests/CoverageRecorder.cpp writes it (decision 644):
    without its `end` line when it is not complete."""
    body = "".join(f"{address:x}\n" for address in addresses)
    text = f"program {program}\ncommand {command}\ntest {test}\nended {ended}\nflags {flags}\n{body}"
    return text + (f"end {len(addresses)}\n" if whole else "")


def command_line_cases(expect) -> None:
    """The arguments of a record's command line (decision 704)."""
    for line, wanted in DOCUMENTED_COMMAND_LINES:
        expect(f"the command line {line!r}, as Microsoft documents it", split_command_line("p.exe " + line),
               ["p.exe"] + wanted)
    for line, wanted in PROGRAM_COMMAND_LINES:
        expect(f"the command line {line!r}", split_command_line(line), wanted)
    tricky = ["C:\\Program Files\\x\\p.exe", "a", "two words", 'a"b', "c\\", "", 'd\\"e', "f\\g", "\\\\", 'x\\\\"y',
              "tab\there", '""', "end\\\\"]
    expect("the quotes of libuv, taken away again", split_command_line(" ".join(map(quoted_argument, tricky))), tricky)
    expect("a command line that a record gave, as CTest listed it", split_command_line(SAMPLE_RECORD_COMMAND),
           SAMPLE_CTEST_COMMAND)
    record = split_command_line(SAMPLE_RECORD_COMMAND.replace("C:/x/build/", "c:\\X\\Build\\"))
    expect("the program compared as Windows compares paths", same_command(record, SAMPLE_CTEST_COMMAND), True)
    expect("each other argument compared with the same text",
           same_command(record[:2] + ["Two words"] + record[3:], SAMPLE_CTEST_COMMAND), False)
    expect("one argument fewer is a different command", same_command(record[:-1], SAMPLE_CTEST_COMMAND), False)


def record_owner_cases(expect) -> None:
    """Which test a coverage record belongs to (decisions 564, 590, 704 and
    713 (2))."""
    ran = {"suite": {"command": ["C:/t/build/suite.exe", "case one"]},
           "named": {"command": ["C:/cmake/cmake.exe", "-P", "C:/t/cmake/RunProbe.cmake"]},
           "twin": {"command": ["C:/t/build/twin.exe"]}, "twin too": {"command": ["C:/t/build/twin.exe"]},
           "failed": {"command": ["C:/t/build/failed.exe"]}}
    expect("a record with no test name: the test with its command line",
           record_owner((r"C:\t\build\suite.exe", r'C:\T\BUILD\suite.exe "case one"', ""), ran), ("suite", None))
    expect("a record with a test name: that test",
           record_owner((r"C:\t\build\probe.exe", r"C:\t\build\probe.exe --probe x", "named"), ran), ("named", None))
    owner, problem = record_owner((r"C:\t\build\suite.exe", r'C:\t\build\suite.exe "case two"', ""), ran)
    expect("a record that matches no test: a problem that names the program and the command line",
           (owner, "suite.exe" in (problem or "") and "case two" in (problem or "")), (None, True))
    owner, problem = record_owner((r"C:\t\build\twin.exe", r"C:\t\build\twin.exe", ""), ran)
    expect("a record that matches two tests: a problem", (owner, "matches 2 tests" in (problem or "")), (None, True))
    owner, problem = record_owner((r"C:\t\build\x.exe", r"C:\t\build\x.exe", "not run"), ran)
    expect("a record that names a test that did not run: a problem", (owner, "'not run'" in (problem or "")),
           (None, True))

    expect("the first lines of a record that is not complete",
           record_head(coverage_record("C:\\p.exe", "C:\\p.exe a", "t", 2, [0x10], whole=False)),
           ("C:\\p.exe", "C:\\p.exe a", "t"))
    expect("a record cut off before its test line names no test", record_head("program C:\\p.exe\ncommand C:\\p"),
           None)
    expect("a record with a different line in place of its test line names no test",
           record_head("program C:\\p.exe\ncommand C:\\p.exe\nended normally\nflags 1\n"), None)

    suite_line = 'C:/t/build/suite.exe "case one"'
    found = [(pathlib.Path("1.functions.txt"), coverage_record("C:/t/build/a.exe", "a", "named", 3, [0x10])),
             (pathlib.Path("2.functions.txt"), coverage_record("C:/t/build/b.exe", "b", "named", 3, [0x20])),
             (pathlib.Path("3.functions.txt"), coverage_record("C:/t/build/suite.exe", suite_line, "", 1, [0x30]))]
    records, problems, loose = assign_records(found, ran, {"suite", "named"})
    expect("the records of one named test put together", sorted(r.program for r in records.get("named", [])),
           ["C:/t/build/a.exe", "C:/t/build/b.exe"])
    expect("and a test's record by its command line, with no problem",
           ([r.functions for r in records.get("suite", [])], problems, loose), ([[0x30]], {}, []))
    cut = [(pathlib.Path("4.functions.txt"), coverage_record("C:/t/build/suite.exe", suite_line, "", 1, [0x30],
                                                             whole=False)),
           (pathlib.Path("5.functions.txt"), coverage_record("C:/x.exe", "x", "named", 3, [0x10], whole=False)),
           (pathlib.Path("6.functions.txt"), coverage_record("C:/x.exe", "x", "other", 3, [0x10], whole=False)),
           (pathlib.Path("7.functions.txt"), coverage_record("C:/t/build/failed.exe", "C:/t/build/failed.exe",
                                                             "", 1, whole=False))]
    ran_too = {**ran, "other": {"command": ["C:/python.exe", "C:/t/scripts/check.py"]}}
    records, problems, loose = assign_records(cut, ran_too, {"suite", "named", "other"})
    named_first = [(problems.get(test) or [""])[0].startswith(str(cut[i][0])) for i, test in
                   enumerate(("suite", "named", "other"))]
    expect("a record not complete: a problem that names the record and the test, for a test that starts its "
           "program directly, through a listed script and through a different script",
           (records, sorted(problems), named_first, loose), ({}, ["named", "other", "suite"], [True, True, True], []))
    expect("a record not complete of a test that failed: no problem", "failed" in problems, False)
    _, _, loose = assign_records([(pathlib.Path("8.functions.txt"), "program C:/p.exe\ncomma")], ran, set())
    expect("a record that cannot be given to a test: a problem", [p.startswith("8.functions.txt") for p in loose],
           [True])


def script_cases(expect) -> None:
    """Which programs a test takes function by function (decisions 703 and 713
    (4))."""
    root, cmake = pathlib.Path("C:/repo"), "C:/tools/cmake.exe"
    for name in LISTED_SCRIPTS:
        expect(f"the script {name}, known by its path",
               listed_script([cmake, "-DX=1", "-P", f"C:/repo/cmake/{name}"], cmake, root), True)
    expect("a listed script, its path in other letters and separators",
           listed_script([cmake, "-P", r"c:\REPO\cmake\runprobe.cmake"], cmake, root), True)
    expect("a script with the same name in a different folder is not known",
           listed_script([cmake, "-P", "C:/repo/elsewhere/RunProbe.cmake"], cmake, root), False)
    expect("a script with a different name is not known",
           listed_script([cmake, "-P", "C:/repo/cmake/Other.cmake"], cmake, root), False)
    expect("a listed script through a CMake that is not the tree's is not known",
           listed_script(["C:/other/cmake.exe", "-P", "C:/repo/cmake/RunProbe.cmake"], cmake, root), False)
    expect("a listed script named after the script that -P runs is not known",
           listed_script([cmake, "-P", "C:/repo/cmake/Other.cmake", "C:/repo/cmake/RunProbe.cmake"], cmake, root),
           False)
    expect("-P with no script", listed_script([cmake, "-P"], cmake, root), False)

    expect("a direct test: its program, and the other program on its command line by its bytes",
           taken_programs("p", False, {"p", "q"}, {"p"}), {"p"})
    expect("a program on a listed script's command line taken by its records",
           taken_programs(None, True, {"app"}, {"app"}), {"app"})
    expect("a program on a listed script's command line that its records do not name keeps its bytes",
           taken_programs(None, True, {"app", "tool"}, {"app"}), {"app"})
    expect("an other script test: the program on its command line keeps its bytes, and a program that only its "
           "records name is taken", taken_programs(None, False, {"app"}, {"app", "child"}), {"child"})
    expect("a direct test that starts a different program: that one taken too",
           taken_programs("p", False, {"p"}, {"p", "child"}), {"p", "child"})


def decision_cases(expect) -> None:
    """The rule of decision 706 on made-up programs."""
    calls = []

    def function_part(keys):
        calls.append(list(keys))
        return answer[0]

    stored = {"bytes": "b1", "part": "f1", "keys": ["k"]}
    answer = ["f2"]
    expect("the bytes equal: unchanged, and no digest computed", (program_unchanged(stored, "b1", function_part),
                                                                  calls), (True, []))
    answer = ["f1"]
    expect("the bytes different and the function part equal: unchanged",
           (program_unchanged(stored, "b2", function_part), calls), (True, [["k"]]))
    answer = ["f2"]
    expect("the bytes and the function part different: changed", program_unchanged(stored, "b2", function_part),
           False)
    answer = [None]
    expect("no function part, a problem: changed", program_unchanged(stored, "b2", function_part), False)
    expect("a stored program that is not there: changed", program_unchanged(stored, None, function_part), False)

    entry = {"parts": {"libraries": "l1"}, "reads": [], "programs": {"p": stored}}
    same = lambda path: MISSING  # noqa: E731
    expect("the programs and each part unchanged: unchanged",
           unchanged(entry, {"libraries": "l1"}, same, lambda programs: True), True)
    expect("the libraries different: changed, the programs as they were",
           unchanged(entry, {"libraries": "l2"}, same, lambda programs: True), False)
    expect("a program changed: changed", unchanged(entry, {"libraries": "l1"}, same, lambda programs: False), False)
    expect("a stored test with programs and no rule to examine them: changed",
           unchanged(entry, {"libraries": "l1"}, same), False)


def store_cases(expect) -> None:
    """The store's tables and numbers (decision 709 (3)), through a write and
    a read."""
    store = {"version": STORE_VERSION, "listeners": {"x": True}, "tests": {
        "a": {"parts": {"d": "1"}, "reads": [], "programs": {"C:/t/p.exe": {"bytes": "b", "part": "x",
                                                                           "keys": ["?k2@@YAXXZ", "?k1@@YAXXZ"]}}},
        "b": {"parts": {"d": "2"}, "reads": [], "programs": {
            "C:/t/p.exe": {"bytes": "b", "part": "y", "keys": ["?k1@@YAXXZ"]},
            "C:/t/q.exe": {"bytes": "c", "part": "z", "keys": ["CMakeFiles/q.dir/q.obj|local"]}}},
        "c": {"parts": {"d": "3"}, "reads": [], "shaders": ["C:/t/shaders/a.spv"], "application": "h"}}}
    written = with_numbers(store)
    expect("one table of function names for each program, each name one time",
           written.get(FUNCTIONS), {"C:/t/p.exe": ["?k1@@YAXXZ", "?k2@@YAXXZ"],
                                    "C:/t/q.exe": ["CMakeFiles/q.dir/q.obj|local"]})
    expect("each test with numbers into the table of its program",
           [written["tests"][n]["programs"]["C:/t/p.exe"]["keys"] for n in ("a", "b")], [[1, 0], [0]])
    with tempfile.TemporaryDirectory() as scratch:
        state = pathlib.Path(scratch)
        write_store(state, store)
        text = (state / STORE).read_text(encoding="utf-8")
        expect("through a write and a read: the store as it was", load_store(state), store)
        expect("a name written one time", text.count("?k1@@YAXXZ"), 1)
        broken = json.loads(text)
        broken["tests"]["a"]["programs"]["C:/t/p.exe"]["keys"] = [0, 2]
        (state / STORE).write_text(json.dumps(broken), encoding="utf-8")
        expect("a number that the table does not hold: that test left out, so that it runs again",
               sorted(load_store(state)["tests"]), ["b", "c"])
        broken.pop(FUNCTIONS, None)
        (state / STORE).write_text(json.dumps(broken), encoding="utf-8")
        expect("a store with no table: its tests with programs left out, the others kept",
               load_store(state)["tests"], {"c": store["tests"]["c"]})
        (state / STORE).write_text(json.dumps({**written, "version": 2}), encoding="utf-8")
        expect("a store of version 2 is not read", load_store(state)["tests"], {})


def program_run(tree: pathlib.Path, root: pathlib.Path, link: str = "lld-link /out:{}") -> "Run":
    """A stand-in Run over the made-up tree of compiled-code.py, with Ninja's
    answers given: one link command for each target."""
    run = stand_in_run(tree, root)
    run.start_function_level("C:/tools/cmake.exe", "ninja")
    run.ninja_commands = lambda targets: (0, "".join(link.format(t) + "\n" for t in targets))
    return run


def made_program(code, root: pathlib.Path, change=None, rebuilt: bool = False) -> pathlib.Path:
    """compiled-code.py's made-up tree, with one change, and its program with
    the bytes of the hook's table; `rebuilt` gives the program other bytes
    with the same table. A change that removes `entered` from the map removes
    its entry, and a change of "table" adds entries that the map does not
    give, as made_tree does for the length of the section."""
    change = change or {}
    program = code.made_tree(root, change)
    removed = change.get("map", {}).get("?entered@@YAHXZ", "kept") is None
    addresses = [a for a in code.CODE_TABLE if not (removed and a == 0x1010)]
    addresses += [0x2000 + code.TABLE_ENTRY * n for n in range(change.get("table", len(addresses)) - len(addresses))]
    program.write_bytes(code.made_table_program(addresses, (".rdata",) if rebuilt else ()))
    if os.name != "nt":
        program.chmod(0o755)
    return program


def program_cases(expect) -> None:
    """The programs taken function by function, read by compiled-code.py from
    its made-up tree: a program, its map and its .pdb file, objects and
    libraries (decisions 590, 704, 706, 713 and 714)."""
    code = script_module("compiled-code")
    read_record = script_module("check-coverage-recorder").read_record
    with tempfile.TemporaryDirectory() as scratch:
        root = pathlib.Path(scratch).resolve()
        tree = root / "tree"
        program = made_program(code, root)
        test = {"name": "t", "command": [str(program), "x"]}
        given = str(program).replace("/", "\\")

        def record(**change):
            fields = {"flags": code.CODE_ENTRIES, "addresses": code.CODE_RECORD, **change}
            return read_record(coverage_record(fields.pop("program", given), f"{program} x", **fields))

        run = program_run(tree, root)
        taken, problems = run.taken_for(test, [record()])
        expect("a direct test: its program, with the keys of its record", (taken, problems),
               ({norm(program): set(code.CODE_KEYS)}, []))
        _, problems = run.taken_for(test, [record(flags=code.CODE_ENTRIES + 1)])
        expect("a record with a count of flags different from the table: a problem",
               [f"{code.CODE_ENTRIES + 1} flags" in p for p in problems], [True])
        _, problems = run.taken_for(test, [record(addresses=list(code.CODE_RECORD) + [0x1090])])
        expect("a record with an address that is not one function: a problem that names it",
               [("0x1090" in p) for p in problems], [True])
        made_program(code, root, {"table": code.CODE_ENTRIES + 1})
        _, problems = program_run(tree, root).taken_for(test, [record()])
        expect("a program with a table that the two sources count differently: a problem that gives the two",
               [("not one number" in p) for p in problems], [True])
        made_program(code, root)
        out_of_line = coverage_record(given, f"{program} x", "", code.CODE_ENTRIES).replace(
            f"flags {code.CODE_ENTRIES}", f"flags {code.CODE_ENTRIES} out of line")
        _, problems = run.taken_for(test, [read_record(out_of_line)])
        expect("a record with its flags out of line: a problem", [("out of line" in p) for p in problems], [True])
        _, problems = run.taken_for(test, [record(program=str(root / "outside" / "p.exe"))])
        expect("a record of a program that is not one of the tree with the hook: a problem, and the direct test "
               "with no record of its own", [("not a program of this tree" in p, "no complete" in p)
                                              for p in problems], [(True, False), (False, True)])
        _, problems = run.taken_for(test, [])
        expect("a direct test that passed with no record: a problem", ["no complete coverage record" in p
                                                                       for p in problems], [True])
        stored = run.stored_programs(taken)
        part = (stored or {}).get(norm(program), {})
        expect("the stored program: its bytes, its function part and its keys",
               (part.get("bytes") == run.world.file(program),
                bool(re.fullmatch("[0-9a-f]{64}", part.get("part") or "")), part.get("keys")),
               (True, True, sorted(code.CODE_KEYS)))
        expect("no problem", run.all_problems(), [])

        fresh = program_run(tree, root)
        expect("the bytes equal: unchanged, and the program not read", (fresh.programs_unchanged(stored or {}),
                                                                         fresh._codes), (True, {}))
        for what, change, link, same in (
                ("a function that no key names changed", code.changed_code("other", b"\xcc\x90\xc3"), None, True),
                ("a function that a key names changed", code.changed_code("entered", b"\x90\x90\x90\xc3"), None, False),
                ("a function that a key names removed",
                 {"direct": {"entered": None, "entered entry": None}, "direct symbols": {"?entered@@YAHXZ": None},
                  "map": {"?entered@@YAHXZ": None}, "entries": 5}, None, False),
                ("another link command", {}, "lld-link /debug /out:{}", False)):
            made_program(code, root, change, rebuilt=True)
            fresh = program_run(tree, root, *([link] if link else []))
            expect(f"the bytes different, {what}: {'unchanged' if same else 'changed'}, with no problem",
                   (fresh.programs_unchanged(stored or {}), bool(fresh._codes), fresh.all_problems()),
                   (same, True, []))
        made_program(code, root, {"no": [("tree", "p.map")]}, rebuilt=True)
        fresh = program_run(tree, root)
        tests = {"t": test}
        entry = {"t": {"parts": {"x": "1"}, "reads": [], "programs": stored or {}}}
        expect("a problem at the decision: the test runs, and the problem fails the run",
               (selection(tests, entry, {"t": {"x": "1"}}, fresh.world.file, set(), fresh.programs_unchanged),
                any("p.map" in p for p in fresh.all_problems())), ({"t"}, True))
        made_program(code, root, rebuilt=True)
        fresh = program_run(tree, root)
        fresh.ninja_commands = lambda targets: (1, "ninja: error: unknown target\n")
        expect("Ninja gives no link command: changed, and a problem",
               (fresh.programs_unchanged(stored or {}), [("link commands" in p) for p in fresh.all_problems()]),
               (False, [True]))
        fresh = program_run(tree, root)
        fresh.ninja_commands = lambda targets: (0, "")
        expect("Ninja gives fewer link commands than programs, with exit code 0: changed, and a problem",
               (fresh.programs_unchanged(stored or {}), [("link commands" in p) for p in fresh.all_problems()]),
               (False, [True]))

        second = code.made_second(root)
        both = {**(stored or {}), **(program_run(tree, root).stored_programs({norm(second): set()}) or {})}
        made_program(code, root, rebuilt=True)
        second.write_bytes(code.made_pe([".text", ".SCOV", ".SCOVP", ".rdata"],
                                        {".SCOVP": (code.TABLE_MARKERS + code.TABLE_ENTRY, 512)}))
        fresh, calls = program_run(tree, root), []
        answer = fresh.ninja_commands
        fresh.ninja_commands = lambda targets: calls.append(list(targets)) or answer(targets)
        fresh.link_commands(fresh.changed_programs({"t": {"programs": both}, "u": {"programs": both}}))
        expect("the link commands of the changed programs of each test, in one call of Ninja",
               (fresh.programs_unchanged(both), calls), (True, [["p.exe", "q.exe"]]))

        plain = tree / "plain.exe"
        plain.write_bytes(code.made_pe([".text", ".rdata"]))
        if os.name != "nt":
            plain.chmod(0o755)
        expect("a test that starts a program of the tree without the hook: no program taken, and no problem",
               program_run(tree, root).taken_for({"name": "plain", "command": [str(plain)]}, []), ({}, []))

        made_program(code, root)
        found = tree / COVERAGE_RECORDS
        found.mkdir(exist_ok=True)
        (found / f"1-1{FUNCTION_RECORD}").write_bytes(
            coverage_record(given, f"{program} x", "", code.CODE_ENTRIES, code.CODE_RECORD).encode("utf-8"))
        def after_run(tests: dict, ninja=None) -> tuple:
            run = program_run(tree, root)
            run.tests = tests
            if ninja is not None:
                run.ninja_commands = ninja
            return run.functions_of_run(found, list(tests), ["t"]), run.all_problems()

        others = {"t": test, "u": {"name": "u", "command": ["C:/python.exe", "C:/check.py"]}}
        taken, problems = after_run(others)
        expect("the records of a run: the programs of each test that passed, for the store, and a test that failed "
               "not kept", (sorted(taken), sorted((taken.get("t") or {}).get(norm(program), {}).get("keys") or []),
                            problems), (["t"], sorted(code.CODE_KEYS), []))
        (found / f"2-2{FUNCTION_RECORD}").write_bytes(coverage_record(given, f"{program} y", "", 6).encode("utf-8"))
        taken, problems = after_run({"t": test})
        expect("a record that belongs to no test: a problem, and no test of the run kept",
               (taken, [("matches 0 tests" in p) for p in problems]), ({}, [True]))
        (found / f"2-2{FUNCTION_RECORD}").unlink()
        (found / f"3-3{FUNCTION_RECORD}").write_bytes(
            coverage_record(given, f"{program} x", "", code.CODE_ENTRIES + 1, code.CODE_RECORD).encode("utf-8"))
        taken, problems = after_run({"t": test})
        expect("a record with a problem: the test not kept, and the problem names the test",
               (taken, [p.startswith("the test t: ") and "flags" in p for p in problems]), ({}, [True]))
        (found / f"3-3{FUNCTION_RECORD}").unlink()
        made_program(code, root, {"no": [("tree", "p.pdb")]})
        taken, problems = after_run({"t": test})
        expect("a program with a problem after the run: the test not kept, and the problem fails the run",
               (taken, any("p.pdb" in p for p in problems)), ({}, True))
        made_program(code, root)
        taken, problems = after_run({"t": test}, lambda targets: (1, ""))
        expect("no link command after the run: the test not kept, and a problem",
               (taken, any("link commands" in p for p in problems)), ({}, True))

        # The rule of decision 629 holds for the application taken function by
        # function: with no shader record, each shader.
        application = tree / f"{APPLICATION}{'.exe' if os.name == 'nt' else ''}"
        application.write_bytes(program.read_bytes())
        if os.name != "nt":
            application.chmod(0o755)
        (tree / "shaders").mkdir(exist_ok=True)
        (tree / "shaders" / "a.spv").write_bytes(b"a")
        usage = {"name": "usage", "command": ["C:/tools/cmake.exe", f"-DORBSIM={application}", "-P",
                                              str(root / "cmake" / "RunUsage.cmake")]}
        expect("the application taken function by function, with no shader record: each shader",
               "shaders" in program_run(tree, root).parts(usage, [], STAND_IN_ENVIRONMENT, None, {norm(application)}),
               True)
        application.unlink()

        program.unlink()
        expect("a stored program that is not there: changed", program_run(tree, root).programs_unchanged(stored or {}),
               False)

        made_program(code, root)
        folder = tree / STATE / OBJECTS
        run = program_run(tree, root)
        run.stored_programs(run.taken_for(test, [record()])[0])
        stray = folder / code.SummaryCache.file_name("CMakeFiles/gone.dir/gone.obj")
        folder.mkdir(parents=True, exist_ok=True)
        stray.write_bytes(b"{}")
        kept = sorted(p.name for p in folder.iterdir())
        expect("a run that is not full deletes no summary", (run.prune_summaries(False),
                                                              sorted(p.name for p in folder.iterdir())), ([], kept))
        expect("a full run deletes the summary of an object that no program links, and keeps the others",
               (program_run(tree, root).prune_summaries(True), sorted(p.name for p in folder.iterdir())),
               ([stray.name], sorted(set(kept) - {stray.name})))


def table_cases(expect) -> None:
    """The kept table of each program (decision 715 (1)): the driver examines
    the records of a program that did not change from its kept table, with no
    read of its map, its .pdb file, its libraries or its summaries, and reads
    a program in full when its bytes or its map changed."""
    code = script_module("compiled-code")
    read_record = script_module("check-coverage-recorder").read_record
    with tempfile.TemporaryDirectory() as scratch:
        root = pathlib.Path(scratch).resolve()
        tree = root / "tree"
        program = made_program(code, root)
        test = {"name": "t", "command": [str(program), "x"]}
        given = str(program).replace("/", "\\")
        # The file of the kept table, named as the summaries are named: from
        # the SHA-256 of the program's key, computed here a second time.
        kept = tree / STATE / "tables" / (hashlib.sha256(norm(program).encode("utf-8")).hexdigest() + ".json")

        def record(**change):
            fields = {"flags": code.CODE_ENTRIES, "addresses": code.CODE_RECORD, **change}
            return read_record(coverage_record(given, f"{program} x", **fields))

        def examined(*records, command=None) -> tuple:
            """(keys, problems, programs read in full) of a run that examines
            the records of one test."""
            run = program_run(tree, root)
            taken, problems = run.taken_for({"name": "t", "command": command or test["command"]}, list(records))
            return sorted(set().union(*taken.values())), problems, len(run._codes)

        expect("a first run reads the program in full, and keeps its table",
               (examined(record()), kept.is_file()), ((sorted(code.CODE_KEYS), [], 1), True))
        expect("a program that did not change: its record examined from the kept table, with no read in full",
               examined(record()), (sorted(code.CODE_KEYS), [], 0))
        _, problems, read = examined(record(addresses=list(code.CODE_RECORD) + [0x1090]))
        expect("from the kept table: an address that is not an entry of the table is a problem",
               ([("0x1090" in p) for p in problems], read), ([True], 0))
        _, problems, read = examined(record(flags=code.CODE_ENTRIES + 1))
        expect("from the kept table: a count of flags different from the table is a problem",
               ([f"{code.CODE_ENTRIES + 1} flags" in p for p in problems], read), ([True], 0))
        for what, change, rebuilt in (("the program's bytes changed", None, True), ("the bytes as before", None, False),
                                      ("only the map changed", {"reverse": True}, False)):
            made_program(code, root, change, rebuilt=rebuilt)
            expect(f"{what}: read in full one time, and its table kept again",
                   (examined(record())[1:], examined(record())[1:]), (([], 1), ([], 0)))
        made_program(code, root, {"map": {"?alias@@YAHXZ": (1, 0x010, "direct.obj")}})
        reads = [examined(record()) for _ in range(2)]
        expect("from the kept table: an entry with two functions at its address is a problem, as from the program",
               [([("0x1010" in p) for p in problems], read) for _, problems, read in reads], [([True], 1), ([True], 0)])

        made_program(code, root)
        examined(record())
        for field, value in (("version", -1), ("key", "C:/t/other.exe"), ("program", "0" * 64), ("map", "0" * 64)):
            text = json.loads(kept.read_bytes()) if kept.is_file() else {}
            kept.parent.mkdir(parents=True, exist_ok=True)
            kept.write_bytes(json.dumps({**text, field: value}).encode("utf-8"))
            expect(f"a kept table with a different {field} is not used: the program is read in full",
                   examined(record())[2], 1)

        second = code.made_second(root)
        second.write_bytes(code.made_table_program([0x1000]))
        if os.name != "nt":
            second.chmod(0o755)
        q_record = read_record(coverage_record(str(second), str(second), "", 1, [0x1000]))
        examined(q_record, command=[str(second)])
        second_kept = kept.with_name(hashlib.sha256(norm(second).encode("utf-8")).hexdigest() + ".json")
        second.unlink()
        program_run(tree, root).prune_summaries(False)
        not_full = second_kept.is_file()
        program_run(tree, root).prune_summaries(True)
        expect("only a full run deletes the kept table of a program that the tree does not hold, and keeps the others",
               (not_full, second_kept.is_file(), kept.is_file()), (True, False, True))


def test_name_cases(expect) -> None:
    """ORBSIM_TEST_NAME in the driver's environment (decision 715 (2)): not in
    the environment part, and not in the environment that CTest gets."""
    base = {"PATH": "a", "ORBSIM_X": "c"}
    expect("ORBSIM_TEST_NAME does not change the environment part",
           (environment({**base, TEST_NAME_VARIABLE: "x"}), environment({**base, TEST_NAME_VARIABLE: "y"})),
           (environment(base), environment(base)))
    lower = environment({**base, TEST_NAME_VARIABLE.lower(): "x"}) == environment(base)
    expect("ORBSIM_TEST_NAME in other letters: not in the environment part on Windows, which compares names so",
           lower, os.name == "nt")
    without = globals().get("without_test_name", lambda env: env)
    expect("the environment that CTest gets has no ORBSIM_TEST_NAME, and keeps each other variable",
           without({**base, TEST_NAME_VARIABLE: "x", "ORBSIM_Y": "y"}), {**base, "ORBSIM_Y": "y"})
    expect("nor in other letters on Windows", TEST_NAME_VARIABLE.lower() in without({TEST_NAME_VARIABLE.lower(): "x"}),
           os.name != "nt")


def function_level_cases(expect) -> None:
    table_cases(expect)
    test_name_cases(expect)
    command_line_cases(expect)
    record_owner_cases(expect)
    script_cases(expect)
    decision_cases(expect)
    store_cases(expect)
    program_cases(expect)


def python_record(argv: list, *lines, end: bool = True) -> str:
    """A record of a Python process as scripts/read-record/sitecustomize.py writes
    it (decision 765)."""
    head = [PYTHON_RECORD_HEAD, "argv " + json.dumps(argv), "cwd C:/t"]
    return "\n".join(head + list(lines) + (["end"] if end else [])) + "\n"


def python_record_cases(expect) -> None:
    """The records of the Python processes of a test (M1-138, decisions 765, 771,
    781-783, 792, 793 and 801)."""
    h, g = "a" * 64, "b" * 64
    child = 'child {"argv": ["git", "status"], "cwd": "C:/r"}'
    whole = python_record(["C:/py.exe", "s.py"], f"read {h} C:/r/a.txt", f"read {MISSING} C:/r/m",
                          f"read {UNREADABLE} C:/r/u", f"list {g} C:/r/d", child)
    expect("a whole record of a Python process", parse_python_record(whole),
           {"argv": ["C:/py.exe", "s.py"], "cwd": "C:/t",
            "reads": [(h, "C:/r/a.txt"), (MISSING, "C:/r/m"), (UNREADABLE, "C:/r/u")],
            "listings": [(g, "C:/r/d")], "children": [{"argv": ["git", "status"], "cwd": "C:/r"}]})
    for name, text in (("no last line end", python_record(["p"], f"read {h} C:/r/a", end=False)),
                       ("the line end before its last line", python_record(["p"], "end", f"read {h} C:/r/a")),
                       ("a stamp that is not a hash", python_record(["p"], "read abc C:/r/a")),
                       ("a line of a different type", python_record(["p"], "open C:/r/a")),
                       ("a read line with no path", python_record(["p"], f"read {h}")),
                       ("a child line that is not JSON", python_record(["p"], "child {")),
                       ("an argv that is not a list of text", python_record(["p"]).replace('argv ["p"]', "argv 5")),
                       ("no head line", python_record(["p"]).replace(PYTHON_RECORD_HEAD + "\n", "")),
                       ("no cwd line", python_record(["p"]).replace("cwd C:/t\n", "")),
                       ("only the lines of the start", python_record(["p"], end=False)),
                       ("no newline after its last line", python_record(["p"])[:-1])):
        expect(f"a record with {name} is not complete", parse_python_record(text), None)

    # Which tests get the new rules (decisions 782, 793 and 801 (7)).
    hook = "C:/r/scripts/read-record/sitecustomize.py"
    command = ["C:/PyManager/python3.exe", "C:/r/scripts/s.py", "--x"]
    own = python_record(["C:/Python/python.exe", "C:/r/scripts/s.py", "--x"], f"read {h} {hook}",
                        f"read {h} C:/r/a.txt", f"list {g} C:/r/d")
    other = python_record(["C:/Python/python.exe", "-c", "x"], f"read {h} {hook}", f"read {g} C:/r/b.txt",
                          f"read {h} C:/r/a.txt")
    expect("the records of a test: each read and each listing one time, sorted, and no child",
           python_reads([own, other], command, hook),
           (sorted([(h, hook), (h, "C:/r/a.txt"), (g, "C:/r/b.txt")]), [(g, "C:/r/d")], []))
    expect("no record: the rules before M1-138", python_reads([], command, hook), None)
    expect("a record that could not be read", python_reads([own, None], command, hook), None)
    expect("a record that is not complete",
           python_reads([own, python_record(["p"], f"read {h} {hook}", end=False)], command, hook), None)
    expect("no record of the interpreter of the test's own script", python_reads([other], command, hook), None)
    expect("a record of the interpreter with different arguments",
           python_reads([own.replace('"--x"', '"--y"')], command, hook), None)
    expect("a record without the line of the hook of this repository",
           python_reads([own, other.replace(f"read {h} {hook}\n", "")], command, hook), None)
    expect("a record from the hook of a different repository",
           python_reads([own.replace(hook, "C:/scratch/scripts/read-record/sitecustomize.py")], command, hook), None)
    # Since step 4 the child lines go to the rules of decision 778, which
    # child_rules applies and child_cases examines.
    expect("a record with a child process: its child lines, for the rules of decision 778",
           (python_reads([own, python_record(["C:/Python/python.exe", "-c", "y"], f"read {h} {hook}", child)],
                         command, hook) or (None, None, None))[2],
           [{"argv": ["git", "status"], "cwd": "C:/r"}])
    expect("the line of the hook with the separators of Windows",
           python_reads([own.replace(hook, hook.replace("/", "\\"))], command, hook) is not None, True)

    # The records of each test, by all of the file name (decision 801 (2)).
    with tempfile.TemporaryDirectory() as scratch:
        folder = pathlib.Path(scratch)
        for name in ("a.py-1-2.txt", "a.py-1.py-3-4.txt", "a.txt", "a.py-x-2.txt", "b.py-1-2.txt",
                     "a%3Ab.py-5-6.txt", "a.py-1-2.txt.tmp"):
            (folder / name).write_text("x", encoding="utf-8")
        found = python_record_files(folder, ["a", "a.py-1", "a:b", "c"])
        expect("the records of each test, by all of the file name",
               {test: sorted(p.name for p in paths) for test, paths in found.items()},
               {"a": ["a.py-1-2.txt"], "a.py-1": ["a.py-1.py-3-4.txt"], "a:b": ["a%3Ab.py-5-6.txt"]})
        expect("no folder: no record", python_record_files(folder / "none", ["a"]), {})

    # The environment that the driver gives CTest (decisions 771, 781 and 783).
    tree, root = pathlib.Path("C:/r/build"), pathlib.Path("C:/r")
    folder = str(root.joinpath(*HOOK_FOLDER))
    hooked = with_hook({"PATH": "p", "PYTHONPATH": "C:/other", OWNER_VARIABLE: "[]",
                        READ_RECORDS_VARIABLE: "C:/elsewhere"}, tree, root)
    expect("the folder of the hook first on PYTHONPATH, the folder before it after it", hooked.get("PYTHONPATH"),
           os.pathsep.join([folder, "C:/other"]))
    expect("the records into test-reads/ of the tree", hooked.get(READ_RECORDS_VARIABLE), str(tree / RECORDS))
    expect("the outer tests of a driver in a test kept", hooked.get(OWNER_VARIABLE), "[]")
    expect("no PYTHONPATH before: the folder of the hook only",
           with_hook({"PATH": "p"}, tree, root).get("PYTHONPATH"), folder)
    if os.name == "nt":
        mixed = with_hook({"PythonPath": "C:/other"}, tree, root)
        expect("on Windows, PYTHONPATH in different letters is the same variable",
               [(n, v) for n, v in mixed.items() if n.upper() == "PYTHONPATH"],
               [("PYTHONPATH", os.pathsep.join([folder, "C:/other"]))])

    # Each line in the store, against the line that the hook writes for its
    # path at this time (decision 792).
    with tempfile.TemporaryDirectory() as scratch:
        base = pathlib.Path(scratch)
        file, later, gone, listed_folder = base / "f.txt", base / "later", base / "gone", base / "d"
        file.write_text("x", encoding="utf-8")
        listed_folder.mkdir()
        (listed_folder / "b").write_text("b", encoding="utf-8")
        (listed_folder / "a").write_text("a", encoding="utf-8")
        expect("the names of a folder, hashed as the hook hashes them",
               World(base, base).names(str(listed_folder)), hashlib.sha256(b"a\nb").hexdigest())
        expect("a folder at the path of a read line: the list line that the hook writes for it",
               World(base, base).hook_line("read", str(listed_folder)), ("list", hashlib.sha256(b"a\nb").hexdigest()))

        def now(kind, path):
            return World(base, base).hook_line(kind, str(path))

        def as_stored(*lines):
            return {"parts": {}, "reads": [[s, str(p)] for k, s, p in lines if k == "read"],
                    "listings": [[s, str(p)] for k, s, p in lines if k == "list"]}

        def same(*lines):
            # A new World each time, so that nothing that it calculated before is used.
            return unchanged(as_stored(*lines), {}, World(base, base).file, None, now)

        read_file = ("read", now("read", file)[1], file)
        expect("a file as it was: unchanged", same(read_file), True)
        file.write_text("y", encoding="utf-8")
        expect("a file changed: changed", same(read_file), False)
        expect("a path that was not there and is not there: unchanged", same(("read", MISSING, later)), True)
        later.mkdir()
        expect("a path that was not there and is a folder at this time: changed",
               same(("read", MISSING, later)), False)
        later.rmdir()
        later.write_text("z", encoding="utf-8")
        expect("a path that was not there and is a file at this time: changed", same(("read", MISSING, later)), False)
        listed = ("list", now("list", listed_folder)[1], listed_folder)
        expect("a folder as it was: unchanged", same(listed), True)
        (listed_folder / "c").write_text("c", encoding="utf-8")
        expect("a new name in a folder: changed", same(listed), False)
        listed = ("list", now("list", listed_folder)[1], listed_folder)
        shutil.rmtree(listed_folder)
        listed_folder.write_text("a file at this time", encoding="utf-8")
        expect("a folder that is a file at this time: changed", same(listed), False)
        read_file = ("read", now("read", file)[1], file)
        file.unlink()
        file.mkdir()
        expect("a file that is a folder at this time: changed", same(read_file), False)
        expect("a folder that was not there and is not there: unchanged", same(("list", MISSING, gone)), True)
        expect("a line with unreadable: changed at each decision, also when the path is unreadable at this time",
               unchanged(as_stored(("read", UNREADABLE, gone)), {}, World(base, base).file, None,
                         lambda kind, path: (kind, UNREADABLE)), False)
        expect("a stored test with listings and no rule for its lines: changed",
               unchanged(as_stored(("list", MISSING, gone)), {}, World(base, base).file), False)


def child_cases(expect) -> None:
    """The rules for the children of a Python test (M1-138, decisions 778, 788,
    789 and 811-817)."""
    base = pathlib.Path(tempfile.gettempdir())
    root, tools, scratch = base / "repo", base / "tools", base / "scratch"
    tree = root / "build"
    ctest, program = tools / "ctest.exe", tree / "probe.exe"

    def rules(*lines):
        return child_rules(list(lines), str(tree), str(root))

    def line(argv, cwd):
        return {"argv": [str(a) for a in argv], "cwd": str(cwd)}

    ctest_child = {"kind": "ctest", "program": str(ctest), "paths": []}
    expect("no child: no rule", rules(), [])
    expect("a ctest child of rule 2", rules(line([ctest, "--show-only=json-v1"], tree)), [ctest_child])
    expect("a child two times: one time", rules(*[line([ctest, "--show-only=json-v1"], tree)] * 2), [ctest_child])
    expect("a child with its working folder outside, of rule 5: its program and the paths on its command line",
           rules(line([program, "--out", scratch / "o", f"--in={scratch / 'i.txt'}"], scratch)),
           [{"kind": "program", "program": str(program),
             "paths": sorted([os.path.normpath(str(scratch / "o")), os.path.normpath(str(scratch / "i.txt"))])}])
    if os.name == "nt":
        expect("a command line in one text, as the hook gets it on Windows",
               rules({"argv": f'"{ctest}" --show-only=json-v1', "cwd": str(tree)}), [ctest_child])
    for name, child in (
            ("a child of os.system", {"event": "os.system", "argv": "('dir',)", "cwd": str(tree)}),
            ("a child of os.startfile, also with its program as a path",
             {"event": "os.startfile", "argv": [str(program)], "cwd": str(scratch)}),
            ("a child with no program", line([], scratch)),
            ("a program by its name only", line(["git", "init"], scratch)),
            ("ctest with different arguments", line([ctest, "-R", "x"], tree)),
            ("ctest in a different folder of the repository", line([ctest, "--show-only=json-v1"], root)),
            ("ninja -t in the tree (decision 811)", line([tools / "ninja.exe", "-t", "query", "x"], tree)),
            ("git ls-files in the repository (decision 811)", line([tools / "git.exe", "ls-files"], root)),
            ("a Python child in the tree (decision 814)",
             line([tools / "python.exe", root / "scripts" / "x.py"], tree)),
            ("a different program in the tree", line([tools / "clang++.exe", "-c", "a.cpp"], tree)),
            ("a path of the repository in the arguments", line([program, root / "docs" / "a.md"], scratch)),
            ("a path of the tree in the arguments", line([program, tree / "x"], scratch)),
            ("such a path after =", line([program, f"--in={root / 'a.txt'}"], scratch))):
        expect(f"{name}: the rules before M1-138", rules(child), None)
    expect("a known child and a child of rule 6: the rules before M1-138",
           rules(line([ctest, "--show-only=json-v1"], tree), line([tools / "git.exe", "status"], root)), None)

    # Through Run.python_records, on a record in a scratch tree: a file on the
    # command line of a child goes into the store as a read line (decisions
    # 788 and 817 (5)), and a folder there as a listing.
    with tempfile.TemporaryDirectory() as folder:
        base = pathlib.Path(folder)
        root, outside = base / "repo", base / "outside"
        tree = root / "build"
        (tree / RECORDS).mkdir(parents=True)
        (outside / "listed").mkdir(parents=True)
        (outside / "input.txt").write_text("i", encoding="utf-8")
        hook = root.joinpath(*HOOK_FOLDER, HOOK_FILE)
        command = [str(outside / "python.exe"), str(root / "scripts" / "s.py")]
        child = json.dumps({"argv": [str(outside / "tool.exe"), str(outside / "input.txt"), str(outside / "listed")],
                            "cwd": str(outside)})
        record = python_record([str(outside / "python.exe"), str(root / "scripts" / "s.py")],
                               f"read {'a' * 64} {hook}", f"child {child}")
        (tree / RECORDS / "t.py-1-2.txt").write_bytes(record.encode("utf-8"))
        run = stand_in_run(tree, root)
        run.world = World(tree, root)
        run.tests = {"t": {"name": "t", "command": command}}
        reads, listings, children = run.python_records(["t"])["t"]
        expect("a file on the command line of a child: a read line with its hash",
               (run.world.hook_line("read", os.path.normpath(str(outside / "input.txt")))[1],
                os.path.normpath(str(outside / "input.txt"))) in reads, True)
        expect("a folder on the command line of a child: a listing",
               [path for _, path in listings], [os.path.normpath(str(outside / "listed"))])
        expect("and the child, of rule 5", [c["kind"] for c in children], ["program"])


def cmake_trace(script, *entries, major: int = 1) -> str:
    """A trace of CMake as `--trace-format=json-v1 --trace-redirect` writes it:
    the version line, then one entry for each command, each with the newline
    after it (measured with CMake 4.3.1). Each entry is (command, argument...)."""
    head = json.dumps({"version": {"major": major, "minor": 2}}, separators=(",", ":"))
    lines = [json.dumps({"args": [str(a) for a in args], "cmd": cmd, "file": str(script), "frame": 1, "line": 1},
                        separators=(",", ":")) for cmd, *args in entries]
    return "\n".join([head] + lines) + "\n"


def trace_cases(expect) -> None:
    """The trace of a `cmake -P` test as its read record (M1-138, decisions 768,
    784 and 803-809)."""
    base = pathlib.Path(tempfile.gettempdir())
    root, tools = base / "repo", base / "tools"
    tree = root / "build"
    script, program = root / "cmake" / "Run.cmake", tree / "orbsim.exe"
    cmake, ninja = tools / "cmake.exe", tools / "ninja.exe"
    command = [str(cmake), f"-DORBSIM={program}", f"-DMAKE={ninja}", "-P", str(script)]
    named = {norm(p) for p in (cmake, program, ninja, script)}

    def inputs(text, cwd=tree):
        return trace_inputs(text, command, named, str(cwd), str(tree))

    def path(p):
        return os.path.normpath(str(p))

    expect("a cmake -P test of the tree's CMake", trace_test(command, cmake), True)
    expect("a test of a different CMake", trace_test([str(tools / "other.exe"), "-P", str(script)], cmake), False)
    expect("a CMake test with no -P", trace_test([str(cmake), "-E", "true"], cmake), False)

    whole = cmake_trace(
        script,
        ("set", "X", "1"),
        ("if", "EXISTS", root / "tests" / "golden" / "a.png"), ("endif",),
        ("if", "DEFINED", "X"), ("endif",),
        ("file", "READ", root / "tests" / "probe.cpp", "text"),
        ("file", "SHA256", tree / "probes" / "x.png", "hash"),
        ("file", "SIZE", "relative.bin", "size"),
        ("file", "GLOB", "found", root / "data" / "*.txt"),
        ("get_filename_component", "real", root / "src" / "a.cpp", "REALPATH"),
        ("get_filename_component", "name", root / "src" / "b.cpp", "NAME"),
        ("function", "helper", "a"), ("endfunction",), ("helper", "1"),
        ("execute_process", "COMMAND", program, "--probe-out", tree / "probes", "RESULT_VARIABLE", "code"),
        ("execute_process", "COMMAND", cmake, "-E", "compare_files", tree / "a", tree / "b", "RESULT_VARIABLE", "d"),
        ("execute_process", "COMMAND", cmake, "-S", tree / "s", f"-DCMAKE_MAKE_PROGRAM={ninja}",
         "OUTPUT_VARIABLE", "out", "ERROR_VARIABLE", "out"),
        ("file", "WRITE", tree / "out.txt", "x"),
        ("message", "STATUS", "done"))
    expect("a whole trace: the scripts and the files that are not in the tree, and the folder of a glob",
           inputs(whole),
           (sorted({path(script), path(root / "tests" / "golden" / "a.png"), path(root / "tests" / "probe.cpp"),
                    path(root / "src" / "a.cpp")}), [path(root / "data")]))
    expect("a relative path counts from the working folder of the test",
           inputs(cmake_trace(script, ("file", "READ", "near.txt", "t")), cwd=root),
           (sorted({path(script), path(root / "near.txt")}), []))
    expect("the keywords of a glob and their values are not patterns",
           inputs(cmake_trace(script, ("file", "GLOB", "v", "LIST_DIRECTORIES", "false", "RELATIVE", root / "x" / "y",
                                       "CONFIGURE_DEPENDS", root / "data" / "*.txt")), cwd=root),
           ([path(script)], [path(root / "data")]))
    for name, text in (
            ("no version line", '{"cmd":"set"}\n'),
            ("a different major version", cmake_trace(script, ("set", "X"), major=2)),
            ("a line that is not JSON", cmake_trace(script, ("set", "X")) + "{\n"),
            ("an entry with no file", cmake_trace(script, ("set", "X")) + '{"args":[],"cmd":"set"}\n'),
            ("an argument that is not text",
             cmake_trace(script, ("set", "X")) + '{"args":[1],"cmd":"set","file":"f"}\n'),
            ("a command that is not known", cmake_trace(script, ("include", root / "cmake" / "X.cmake"))),
            ("a call of a function that the trace did not define", cmake_trace(script, ("helper2", "1"))),
            ("a different file subcommand", cmake_trace(script, ("file", "COPY", root / "a", "DESTINATION", tree))),
            ("a file read with no path", cmake_trace(script, ("file", "READ"))),
            ("GLOB_RECURSE", cmake_trace(script, ("file", "GLOB_RECURSE", "v", root / "data" / "*.txt"))),
            ("a wildcard in a folder of the pattern",
             cmake_trace(script, ("file", "GLOB", "v", root / "d*" / "x.txt"))),
            ("IS_NEWER_THAN", cmake_trace(script, ("if", root / "a", "IS_NEWER_THAN", root / "b"))),
            ("EXISTS with no path", cmake_trace(script, ("if", "EXISTS"))),
            ("get_filename_component with PROGRAM", cmake_trace(script, ("get_filename_component", "p", "git",
                                                                         "PROGRAM"))),
            ("a child that the command line does not name",
             cmake_trace(script, ("execute_process", "COMMAND", tools / "git.exe", "status", "RESULT_VARIABLE", "r"))),
            ("a child by its name only, which PATH finds",
             cmake_trace(script, ("execute_process", "COMMAND", "git", "status", "RESULT_VARIABLE", "r"))),
            ("a program of the tree that the command line does not name",
             cmake_trace(script, ("execute_process", "COMMAND", tree / "other.exe", "RESULT_VARIABLE", "r"))),
            ("a path of a child that is not in the tree and not on the command line",
             cmake_trace(script, ("execute_process", "COMMAND", program, root / "scripts" / "x.py"))),
            ("such a path after =",
             cmake_trace(script, ("execute_process", "COMMAND", program, f"--input={root / 'docs' / 'a.md'}"))),
            ("a keyword of execute_process that the scripts do not use",
             cmake_trace(script, ("execute_process", "COMMAND", program, "WORKING_DIRECTORY", root))),
            ("execute_process with no program", cmake_trace(script, ("execute_process", "COMMAND")))):
        expect(f"a trace with {name}: the rules before M1-138", inputs(text), None)


def self_test() -> int:
    failures, cases = [], []

    def expect(name, got, wanted):
        cases.append(name)
        if got != wanted:
            failures.append(f"{name}: got {got!r}, wanted {wanted!r}")

    # The escape, held to tests/TestReads.cpp's own table.
    for name, wanted in [("propagation composes: one long step", "propagation composes%3A one long step"),
                         ("f/16 at 1/125 s", "f%2F16 at 1%2F125 s"),
                         ('a <b> "c" d\\e |f? *g', "a %3Cb%3E %22c%22 d%5Ce %7Cf%3F %2Ag"),
                         ("100% sure", "100%25 sure"), ("ends in a dot.", "ends in a dot%2E"),
                         ("ends in a space ", "ends in a space%20"), ("a.b c", "a.b c"),
                         ("tab\there", "tab%09here"), ("", "")]:
        expect(f"escape {name!r}", escaped_name(name), wanted)

    h = "a" * 64
    expect("a whole record", parse_record(f"{h} C:/x/a.txt\nmissing C:/x/b c.txt\nend\n"),
           [(h, "C:/x/a.txt"), (MISSING, "C:/x/b c.txt")])
    expect("an empty record", parse_record("end\n"), [])
    expect("a record cut off", parse_record(f"{h} C:/x/a.txt\n"), None)
    expect("a record without its last newline", parse_record("end"), None)
    expect("a record with a bad hash", parse_record("abc C:/x\nend\n"), None)
    expect("a record with no path", parse_record(f"{h}\nend\n"), None)

    # unchanged(): every part, and every read.
    parts = {"definition": "d", "programs": "p"}
    hashes = {"/x/a": h}
    current = lambda p: hashes.get(p, MISSING)  # noqa: E731
    entry = {"parts": dict(parts), "reads": [(h, "/x/a")]}
    expect("all as it was", unchanged(entry, parts, current), True)
    expect("never passed", unchanged(None, parts, current), False)
    expect("a part moved", unchanged(entry, {**parts, "programs": "q"}, current), False)
    expect("a part added", unchanged(entry, {**parts, "card": "c"}, current), False)
    expect("a part gone", unchanged(entry, {"definition": "d"}, current), False)
    expect("a read file changed", unchanged(entry, parts, lambda p: "b" * 64), False)
    expect("a read file deleted", unchanged(entry, parts, lambda p: MISSING), False)
    expect("a file missing then and now", unchanged({"parts": parts, "reads": [(MISSING, "/x/n")]},
                                                    parts, current), True)
    expect("a file missing then, there now", unchanged({"parts": parts, "reads": [(MISSING, "/x/a")]},
                                                       parts, current), False)

    # selection(): dependents follow, transitively, and forced ones run.
    tests = {n: {"name": n, "properties": [{"name": "DEPENDS", "value": d}] if d else []}
             for n, d in [("setup", None), ("reader", ["setup"]), ("reader2", ["reader"]), ("alone", None)]}
    same = {n: {"parts": {"x": "1"}, "reads": []} for n in tests}
    now = {n: {"x": "1"} for n in tests}
    expect("nothing changed", selection(tests, same, now, current, set()), set())
    expect("the setup changed", selection(tests, same, {**now, "setup": {"x": "2"}}, current, set()),
           {"setup", "reader", "reader2"})
    expect("a reader changed", selection(tests, same, {**now, "reader2": {"x": "2"}}, current, set()),
           {"reader2"})
    expect("one forced", selection(tests, same, now, current, {"alone"}), {"alone"})
    expect("nothing stored", selection(tests, {}, now, current, set()), set(tests))

    # cautious(): no record, or one reaching into data/, tests/golden/ or the build files.
    root = pathlib.Path(tempfile.gettempdir()) / "repo"
    expect("no record is cautious", cautious(None, root), True)
    expect("an empty record is not", cautious([], root), False)
    expect("a read under data/ is", cautious([(h, str(root / "data" / "x.txt"))], root), True)
    expect("a read under tests/golden/ is", cautious([(h, str(root / "tests/golden/a/b.png"))], root), True)
    expect("a read of CMakeLists.txt is", cautious([(h, str(root / "CMakeLists.txt"))], root), True)
    expect("a read under cmake/ is", cautious([(h, str(root / "cmake" / "X.cmake"))], root), True)
    expect("a read of a probe image is not", cautious([(h, str(root / "build/debug/probes/x"))], root), False)
    expect("a folder named like data is not", cautious([(h, str(root / "database" / "x"))], root), False)

    # environment(): what counts, and what does not.
    base = {"PATH": "a", "VK_LAYER_PATH": "b", "ORBSIM_X": "c", "HOME": "d"}
    expect("an unrelated variable", environment({**base, "HOME": "e"}), environment(base))
    expect("PATH", environment({**base, "PATH": "z"}) != environment(base), True)
    expect("a VK_ variable", environment({**base, "VK_ICD_FILENAMES": "z"}) != environment(base), True)
    expect("an ORBSIM_ variable", environment({**base, "ORBSIM_Y": "z"}) != environment(base), True)
    # M1-138: the outer tests of a driver in a test are not in the part
    # (decision 781), and PYTHONPATH is (decision 796).
    expect("ORBSIM_READ_RECORD_OWNER", environment({**base, OWNER_VARIABLE: "[]"}), environment(base))
    expect("PYTHONPATH", environment({**base, "PYTHONPATH": "z"}) != environment(base), True)

    # outcomes(): only a clean run is a pass.
    report = ('<testsuite><testcase name="a" status="run"/>'
              '<testcase name="b" status="fail"><failure message="x"/></testcase>'
              '<testcase name="c" status="run"><skipped message="x"/></testcase>'
              '<testcase name="d" status="notrun"/>'
              '<testcase name="e" status="disabled"/></testsuite>')
    expect("the report", outcomes(report), {"a": "passed", "b": "failed", "c": "skipped", "d": "skipped",
                                            "e": "skipped"})

    # push_problems(): exactly the commit, in both trees.
    c = "1" * 40
    line = [f"refs/heads/master {c} refs/heads/master {'2' * 40}"]
    good = {"build/debug": {"commit": c}, "build/relwithdebinfo": {"commit": c}}
    expect("both trees at the commit", push_problems(line, good), [])
    expect("one tree at another commit",
           len(push_problems(line, {**good, "build/debug": {"commit": "3" * 40}})), 1)
    expect("one tree never checked", len(push_problems(line, {**good, "build/relwithdebinfo": None})), 1)
    expect("a branch deleted", push_problems([f"(delete) {'0' * 40} refs/heads/x {c}"], {}), [])
    expect("nothing pushed", push_problems([], {}), [])

    # full_run_problems(): every test, a clean tree from start to end, one commit.
    listed = {"a", "b"}
    expect("a full run as it should be", full_run_problems(listed, listed, (True, True), (c, c)), [])
    expect("a full run that ran one test fewer",
           len(full_run_problems(listed, {"a"}, (True, True), (c, c))), 1)
    expect("a full run that ran a test CTest does not list",
           len(full_run_problems(listed, listed | {"z"}, (True, True), (c, c))), 1)
    expect("a full run from a tree with changes", len(full_run_problems(listed, listed, (False, False), (c, c))), 1)
    expect("a tree changed during a full run", len(full_run_problems(listed, listed, (True, False), (c, c))), 1)
    expect("a commit made during a full run",
           len(full_run_problems(listed, listed, (True, True), (c, "3" * 40))), 1)

    # edited_after_build(): newer than the log, or no log at all.
    expect("every file older than the log", edited_after_build({"a": 1, "b": 2}, 3), [])
    expect("a file newer than the log", edited_after_build({"a": 1, "b": 5, "c": 4}, 3), ["b", "c"])
    expect("as new as the log is not newer", edited_after_build({"a": 3}, 3), [])
    expect("no log", edited_after_build({"a": 1}, None), ["a"])

    # unrecorded(): only a passing test whose program has the listener needs a record.
    expect("records where they are owed", unrecorded(["a", "b", "c"], {"a": True, "b": False, "c": False},
                                                      {"a": True, "b": True, "c": False}), ["b"])

    # Run.parts(): what each kind of test is fingerprinted on, against a
    # stand-in tree whose files exist, and a stand-in for hashing them.
    with tempfile.TemporaryDirectory() as scratch:
        base = pathlib.Path(scratch)
        root, outside = base / "repo", base / "tools"
        tree = root / "build"
        exe = ".exe" if os.name == "nt" else ""
        for path in (tree / f"suite{exe}", tree / f"{APPLICATION}{exe}", outside / f"cmake{exe}",
                     root / "cmake" / "RunProbe.cmake", root / "scripts" / "check.py", tree / "probes" / "x",
                     outside / f"python{exe}"):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("x", encoding="utf-8")
            if os.name != "nt":
                path.chmod(0o755)
        run = stand_in_run(tree, root)
        suite = {"name": "s", "command": [str(tree / f"suite{exe}"), "s"]}
        script = {"name": "k", "command": [str(outside / f"cmake{exe}"), "-P", str(root / "cmake" / "RunProbe.cmake")]}
        probe = {"name": "p", "command": [str(outside / f"cmake{exe}"), f"-DORBSIM={tree / (APPLICATION + exe)}",
                                          f"-DOUT={tree / 'probes'}", "-P", str(root / "cmake" / "RunProbe.cmake")],
                 "properties": [{"name": "LABELS", "value": [GPU_LABEL]}]}
        reader = {"name": "r", "command": [str(outside / f"cmake{exe}"), str(root / "scripts" / "check.py"), str(tree)]}
        recorded = [(h, str(tree / "probes" / "x"))]
        env = dict(STAND_IN_ENVIRONMENT)

        def keys(test, reads=recorded):
            return sorted(run.parts(test, reads, env))

        always = ["definition", "environment", "machine"]
        expect("a suite: its program", keys(suite), sorted(always + ["programs"]))
        expect("a suite with no record: and the cautious rule", keys(suite, None),
               sorted(always + ["programs", "cautious"]))
        expect("a suite whose record reads data/: and the cautious rule",
               keys(suite, [(h, str(root / "data" / "f.txt"))]), sorted(always + ["programs", "cautious"]))
        expect("a script test: its program, its script, every script", keys(script),
               sorted(always + ["programs", "files", "scripts"]))
        expect("a probe test: and the shaders and the card, not its output folder", keys(probe),
               sorted(always + ["programs", "files", "scripts", "shaders", "card"]))
        expect("a test naming the tree: the tree's own files", keys(reader),
               sorted(always + ["programs", "files", "scripts", "build tree"]))
        # M1-138 (decisions 764, 765 and 801): a Python test with complete
        # records of its Python processes, which `listings` gives, has no
        # part for the build tree, for a folder of the repository on its
        # command line, or for scripts/. The files and the programs on its
        # command line stay, and the cautious rule examines its reads and its
        # listings.
        python = {"name": "y", "command": [str(outside / f"python{exe}"), str(root / "scripts" / "check.py"),
                                           str(tree), str(root / "scripts")]}
        only_script = {**python, "command": python["command"][:2]}
        expect("a Python test by the rules before M1-138", keys(python),
               sorted(always + ["programs", "files", "scripts", "build tree"]))
        expect("a Python test with complete records: its program and its files only",
               sorted(run.parts(python, [], env, listings=[])), sorted(always + ["programs", "files"]))
        expect("and a folder of the repository on its command line is not in its files",
               run.parts(python, [], env, listings=[]).get("files"),
               run.parts(only_script, [], env, listings=[]).get("files"))
        expect("a Python test that read a file under data/: and the cautious rule",
               "cautious" in run.parts(python, [(h, str(root / "data" / "f.txt"))], env, listings=[]), True)
        expect("a Python test that listed cmake/: and the cautious rule",
               "cautious" in run.parts(python, [], env, listings=[(h, str(root / "cmake"))]), True)
        # M1-138, step 3 (decisions 775 and 809): a test with a complete trace
        # loses only the part of decision 531. The part of the build tree, the
        # part of a folder of the repository on its command line and the
        # cautious part stay.
        traced = {"name": "t", "command": [str(outside / f"cmake{exe}"), f"-DTREE={tree}",
                                           f"-DDIR={root / 'scripts'}", "-P", str(root / "cmake" / "RunProbe.cmake")]}
        expect("a cmake -P test by the rules before M1-138", keys(traced),
               sorted(always + ["programs", "files", "scripts", "build tree"]))
        expect("a cmake -P test with a complete trace: no scripts part, and the cautious part",
               sorted(run.parts(traced, [], env, listings=[], traced=True)),
               sorted(always + ["programs", "files", "build tree", "cautious"]))
        expect("and a folder of the repository on its command line stays in its files",
               run.parts(traced, [], env, listings=[], traced=True).get("files"),
               run.parts(traced, recorded, env).get("files"))
        expect("the trace of a test: in test-reads/ of the tree, by the escape of decision 528",
               run.trace_path("a:b"), tree / RECORDS / "a%3Ab.trace.json")
        # M1-138, step 4 (decisions 778, 789 and 817): the part of the children
        # of a Python test: the bytes of the program of each child, and for a
        # ctest child also the CTest files of the tree.
        ctest_child = [{"kind": "ctest", "program": str(outside / f"cmake{exe}"), "paths": []}]
        program_child = [{"kind": "program", "program": str(outside / f"python{exe}"), "paths": []}]

        def child_part(children):
            return stand_in_run(tree, root).parts(python, [], env, listings=[], children=children).get("children")

        (tree / "CTestTestfile.cmake").write_text("a", encoding="utf-8")
        ctest_before, program_before = child_part(ctest_child), child_part(program_child)
        expect("a Python test with children: and the part of its children", ctest_before is not None, True)
        expect("a Python test with no child: no part of its children", child_part([]), None)
        (tree / "CTestTestfile.cmake").write_text("b", encoding="utf-8")
        expect("a changed CTest file moves the part of a ctest child", child_part(ctest_child) != ctest_before, True)
        expect("and leaves the part of a different child alone", child_part(program_child), program_before)
        (outside / f"python{exe}").write_text("another python", encoding="utf-8")
        expect("a changed program of a child moves the part of its children",
               child_part(program_child) != program_before, True)
        (outside / f"python{exe}").write_text("x", encoding="utf-8")
        before = run.parts(probe, recorded, env)
        run._card = {"gpu": "another card"}
        expect("another card moves a GPU test's fingerprint", run.parts(probe, recorded, env) != before, True)
        suite_before = stand_in_run(tree, root).parts(suite, recorded, env)
        expect("another card leaves a suite's alone", run.parts(suite, recorded, env), suite_before)
        (tree / f"suite{exe}").write_text("rebuilt", encoding="utf-8")
        expect("a rebuilt program moves its fingerprint",
               stand_in_run(tree, root).parts(suite, recorded, env)["programs"] != suite_before["programs"], True)
        (root / "scripts" / "check.py").write_text("edited", encoding="utf-8")
        expect("an edited script moves a script test's fingerprint",
               stand_in_run(tree, root).parts(script, recorded, env)["scripts"]
               != run.parts(script, recorded, env)["scripts"], True)
        (tree / "probes" / "x").write_text("rewritten", encoding="utf-8")
        expect("a test's output folder is never hashed",
               stand_in_run(tree, root).parts(probe, recorded, env).get("files"), before.get("files"))

    each_part_moves_with_its_input(expect)
    tools_read_as_they_answer(expect)
    shader_record_cases(expect)
    function_level_cases(expect)
    python_record_cases(expect)
    trace_cases(expect)
    child_cases(expect)

    for failure in failures:
        print(f"self-test FAILED: {failure}")
    print(f"run-tests self-test: {len(cases) - len(failures)} of {len(cases)} cases")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("tree", nargs="?")
    parser.add_argument("--ctest")
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--full", action="store_true")
    parser.add_argument("--root", default=str(ROOT), help="the repository; a scratch one in the end-to-end test")
    parser.add_argument("--pre-push", metavar="ROOT")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--end-to-end", nargs=3, metavar=("CMAKE", "CTEST", "NINJA"))
    parser.add_argument("--function-level", nargs=8,
                        metavar=("CMAKE", "CTEST", "NINJA", "CLANG++", "LLVM-AR", "LLD-LINK", "RECORDER", "TURNS"),
                        help="the selection by function from end to end, on a scratch project with the hook")
    parser.add_argument("--in-tree", action="store_true", help="the driver on the tree's own programs")
    parser.add_argument("--suite", help="--in-tree: a program that carries the read-record listener")
    parser.add_argument("--without-listener", help="--in-tree: a program that does not")
    parser.add_argument("--application", help="--in-tree: the application, where the tree builds it")
    parser.add_argument("--library", action="append", default=[],
                        help="--in-tree: a library the application loads, by file name")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.end_to_end:
        return end_to_end(*args.end_to_end)
    if args.function_level:
        return function_level(args.function_level)
    if args.in_tree:
        if not (args.tree and args.ctest and args.suite and args.without_listener):
            parser.error("--in-tree needs the tree, --ctest, --suite and --without-listener")
        if bool(args.application) != bool(args.library):
            parser.error("--application and --library come together")
        return in_tree(pathlib.Path(args.tree).resolve(), args.ctest, pathlib.Path(args.suite),
                       pathlib.Path(args.without_listener), args.application, args.library)
    if args.pre_push:
        return pre_push(pathlib.Path(args.pre_push), sys.stdin.read().splitlines())
    if not args.tree or not args.ctest:
        parser.error("name the build tree and --ctest")
    return run_tree(pathlib.Path(args.tree).resolve(), args.ctest, args.jobs, args.full,
                    pathlib.Path(args.root).resolve())


if __name__ == "__main__":
    sys.exit(main())

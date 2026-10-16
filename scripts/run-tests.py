#!/usr/bin/env python3
"""Run the tests whose inputs changed, and skip the rest (M1-129).

    run-tests.py <build tree> --ctest <ctest> [--jobs <n>] [--full]
    run-tests.py --pre-push <repository root>      (reads git's pre-push lines)
    run-tests.py --self-test
    run-tests.py --end-to-end <cmake> <ctest> <ninja>
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
  * its environment: PATH, every VK_* and ORBSIM_* variable (decision 501);
  * every program on its command line that this tree builds, by its bytes and
    those of every library it loads (decision 525), the libraries found from
    the program's import table with llvm-readobj on Windows and by ldd
    elsewhere; and every other file on its command line, by its bytes;
  * a directory on its command line inside the repository: every file in it
    git does not ignore; the build tree itself: its Ninja files and every file
    git does not ignore (decisions 500 and 510);
  * a test run through a program this tree does not build -- cmake, python:
    every file under scripts/ and cmake/, CMakeLists.txt and CMakePresets.json
    (decision 531), since scripts load other scripts;
  * the files it read, from its read record (decisions 489, 498 and 527):
    each must still hash to what the record says it was when the test read it;
  * the cautious rule (decisions 489 and 505): a test with no complete record,
    or whose record names a file under data/, tests/golden/ or the build files,
    is fingerprinted on all of those -- every file on disk under data/ and
    tests/golden/, ignored or not, since a generated fixture is ignored by git
    and still read;
  * for a test that starts the application, every compiled shader; for a test
    labelled gpu, the graphics card, its driver and the Vulkan versions, read
    from a clear probe this script runs before CTest (decisions 490 and 532);
  * the "machine" part, in every test (decisions 509 and 533): the
    time-zone data the leap-second test compares against -- on Windows the five
    files of C:\\Windows\\Globalization\\ICU\\, elsewhere the two files of
    /usr/share/zoneinfo/ that libstdc++ reads, measured 2026-10-14 -- and the
    Python, CMake, CTest and compiler programs, each with its libraries.

**Every file is hashed in full on every run** (decision 526): no size and
date are trusted.

**A test runs whenever a test it depends on runs** (decision 508), by CTest's
DEPENDS, which holds its fixtures' setup tests too. CTest is told not to add
a skipped setup test back by itself: a setup test whose fingerprint is
unchanged left its outputs as they were.

**What is written** (decisions 491 and 497), all in the build tree's
`run-tests/`: the fingerprints of the last passing run of each test, the list
of tests skipped, and CTest's own report. One line in `check`'s output says
how many ran and how many were skipped.

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
pre-push hook (decision 603); and run_tests_in_tree asks the real tools
again, on the tree's own programs.
"""

import argparse
import datetime
import hashlib
import json
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
# Raised whenever what the store holds, or how a part is computed, changes:
# a store of another version is not read, so every test runs once.
STORE_VERSION = 1
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
# The clear probe that names the card runs in about 4.4 s (decision 502); an
# order of magnitude clear of that, as mutants-due.py allows.
CARD_PROBE_SECONDS = 120
# The sidecar lines that name the card, its driver and the Vulkan versions.
CARD_LINES = ("gpu", "gpu.vendor", "gpu.device", "driver.name", "driver.info", "driver.version",
              "vulkan.api", "validation.layer")
MISSING = "missing"
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
    VK_* and ORBSIM_*. Names compared without case on Windows, as Windows
    does."""
    fold = (lambda n: n.upper()) if os.name == "nt" else (lambda n: n)
    kept = {fold(n): v for n, v in env.items()
            if fold(n) == "PATH" or fold(n).startswith(("VK_", "ORBSIM_"))}
    return digest(json.dumps(kept, sort_keys=True))


def is_program(path: pathlib.Path) -> bool:
    """On Windows only an .exe is -- Windows has no execute bit (mutants-due.py
    says what treating every file as one did); elsewhere the execute bit."""
    if os.name == "nt":
        return path.suffix.lower() == ".exe"
    return path.is_file() and os.access(path, os.X_OK)


def cache_value(tree: pathlib.Path, key: str):
    try:
        for line in (tree / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith(key + ":"):
                return line.split("=", 1)[1]
    except OSError:
        pass
    return None


# --- the decision, pure ------------------------------------------------------------

def unchanged(stored, parts: dict, current_hash) -> bool:
    """Whether a test may be skipped: stored at a passing run, every part as it
    was, and every file its record names hashing as it did when it read it."""
    if stored is None or stored.get("parts") != parts:
        return False
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


def selection(tests: dict, store: dict, parts: dict, current_hash, forced: set) -> set:
    """The tests to run: every test not unchanged, every forced one, and every
    test depending on one of them."""
    chosen = {name for name in tests
              if name in forced or not unchanged(store.get(name), parts[name], current_hash)}
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
        self._machine = None
        self._card = None
        self._card_read = False
        # The parts many tests share, each computed once a run.
        self._shared = {}

    def machine_programs(self) -> list:
        found = [sys.executable, self.ctest, cache_value(self.tree, "CMAKE_COMMAND"),
                 cache_value(self.tree, "CMAKE_CXX_COMPILER")]
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
            self._machine = self.world.files(sorted(set(found)))
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

    def parts(self, test: dict, reads, env: dict) -> dict:
        """What `test` is fingerprinted on, part by part. The environment is
        handed in rather than read here, so that the self-test can give it
        one (register decision 602)."""
        tree, root = norm(self.tree), norm(self.root)
        found = {"definition": definition(test), "environment": environment(env),
                 "machine": self.machine()}
        files, programs, built_program = [], [], False
        command = test.get("command", [])
        for index, path in enumerate(self.arguments(test)):
            key = norm(path)
            if key == tree:
                found["build tree"] = self.tree_inputs()
            elif under(key, tree):
                if path.is_file() and is_program(path):
                    programs.append(path)
                    if command and norm(command[0]) == key:
                        built_program = True
                # Anything else in the tree is a test's output, judged by the
                # dependency rule (decision 508), never hashed before the run.
            elif under(key, root):
                files += self.world.unignored_under(path) if path.is_dir() else [path]
            elif path.is_file():
                (programs if is_program(path) else files).append(path)
        if programs:
            found["programs"] = digest("\n".join(sorted(self.world.program(p) for p in programs)))
        if files:
            found["files"] = self.world.files(files)
        if not built_program:
            found["scripts"] = self.script_inputs()
        if cautious(reads, self.root):
            found["cautious"] = self.cautious_inputs()
        if any(norm(p) == norm(self.app) for p in programs):
            found["shaders"] = self.shaders()
        if GPU_LABEL in labels(test):
            found["card"] = digest(json.dumps(self.card(), sort_keys=True))
        return found

    def record_path(self, name: str) -> pathlib.Path:
        return self.tree / RECORDS / (escaped_name(name) + ".txt")

    def read_record(self, name: str):
        try:
            return parse_record(self.record_path(name).read_text(encoding="utf-8"))
        except OSError:
            return None

    def carries_listener(self, program: pathlib.Path, known: dict) -> bool:
        key = self.world.program(program)
        if key not in known:
            run = subprocess.run([str(program), "--list-listeners"], capture_output=True, text=True,
                                 encoding="utf-8", errors="replace", check=False, timeout=60)
            known[key] = lists_listener(run.returncode, run.stdout)
        return known[key]


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
    try:
        store = json.loads((state / STORE).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {"version": STORE_VERSION, "tests": {}, "listeners": {}}
    if store.get("version") != STORE_VERSION:
        return {"version": STORE_VERSION, "tests": {}, "listeners": {}}
    return store


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
    env = dict(os.environ)
    parts = {name: run.parts(test, (stored.get(name) or {}).get("reads"), env)
             for name, test in run.tests.items()}
    chosen = selection(run.tests, stored, parts, run.world.file, forced)
    decided = time.monotonic()
    skipped = sorted(set(run.tests) - chosen)
    order = [name for name in run.tests if name in chosen]

    (run.state / SELECTED).write_text("".join(f"{n}\n" for n in order), encoding="utf-8")
    (run.state / SKIPPED).write_text("".join(f"{n}\n" for n in skipped), encoding="utf-8")
    for name in order:
        run.record_path(name).unlink(missing_ok=True)

    code, results = 0, {}
    if order:
        report = run.state / REPORT
        report.unlink(missing_ok=True)
        command = [ctest, "--output-on-failure", "-j", str(jobs), "--tests-from-file", str(run.state / SELECTED),
                   "--output-junit", str(report)]
        if not full:
            # A setup test left out was unchanged, and so are its outputs.
            command += ["--fixture-exclude-setup", ".*"]
        code = subprocess.run(command, cwd=tree, check=False).returncode
        try:
            results = outcomes(report.read_text(encoding="utf-8"))
        except (OSError, ElementTree.ParseError):
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
    for name in order:
        if name in passed and name not in missing_records:
            stored[name] = {"parts": run.parts(run.tests[name], reads[name], env), "reads": reads[name]}
        else:
            stored.pop(name, None)
    for name in list(stored):
        if name not in run.tests:
            del stored[name]
    (run.state / STORE).write_text(json.dumps(store, sort_keys=True), encoding="utf-8")

    for line in problems:
        print(f"run-tests: {line}")
    ran = len(results)
    print(f"run-tests: {ran} ran, {len(skipped)} skipped as unchanged"
          + (f" -- listed in {run.state / SKIPPED}" if skipped else "")
          + f"; the fingerprints took {decided - began:.1f} s")
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
        for path in (suite_program, tree / f"{APPLICATION}{exe}", shader, cmake, compiler, compiler_library,
                     suite_library, root / "cmake" / "RunProbe.cmake", document):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("x", encoding="utf-8")
            if os.name != "nt":
                path.chmod(0o755)
        (tree / "CMakeCache.txt").write_text(
            f"CMAKE_COMMAND:INTERNAL={cmake}\nCMAKE_CXX_COMPILER:FILEPATH={compiler}\n", encoding="utf-8")
        suite = {"name": "s", "command": [str(suite_program), "s"]}
        probe = {"name": "p", "command": [str(cmake), f"-DORBSIM={tree / (APPLICATION + exe)}", "-P",
                                          str(root / "cmake" / "RunProbe.cmake")],
                 "properties": [{"name": "LABELS", "value": [GPU_LABEL]}]}
        folder = {"name": "f", "command": [str(cmake), "-P", str(root / "cmake" / "RunProbe.cmake"),
                                           str(root / "docs")]}

        def part(test, name, env=None):
            # A fresh Run each time, so that nothing it computed earlier is
            # reused; the machine part computed rather than stood in for.
            run = stand_in_run(tree, root)
            run.ctest, run._machine = str(cmake), None
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
        expect("another library of the compiler moves the machine part",
               part(suite, "machine") != machine_after_compiler, True)

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

    expect("the listener, found", lists_listener(0, SAMPLE_LISTENERS), True)
    expect("no listener, none found",
           lists_listener(0, SAMPLE_LISTENERS.replace("RecordWhatEachCaseRead", "SomethingElse")), False)
    expect("a program that failed lists nothing", lists_listener(1, SAMPLE_LISTENERS), False)


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
                     root / "cmake" / "RunProbe.cmake", root / "scripts" / "check.py", tree / "probes" / "x"):
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

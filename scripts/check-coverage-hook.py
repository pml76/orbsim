#!/usr/bin/env python3
"""Check that the coverage hook is on exactly the project's own code, and each test carries its name (M1-131).

    check-coverage-hook.py <build tree> <repository root> <ctest> --hook on|off
    check-coverage-hook.py --self-test

Why this exists: M1-132 is to choose tests function by function, from records of
the functions each test entered, which programs carrying the coverage hook
write -- `-fsanitize-coverage=func,inline-bool-flag,pc-table`, which gives each
function a flag it sets when entered and keeps a table of the functions'
addresses beside the flags (register decision 559). A record can only be
trusted as far as the hook reaches, so what CMakeLists.txt sets up with
`ORBSIM_COVERAGE_HOOK` on is checked here against what the build made
(decision 632):

  * **the hook on every object compiled from src/ and tests/, and on no
    other** -- not on someone else's code (decision 588): ERFA's, lodepng's,
    stb_image's decoder in src/view/StbImageImpl.cpp, VMA's in
    src/render/VmaImpl.cpp; not on the recorder, tests/CoverageRecorder.cpp,
    which the hook calls (decision 566); not on the two objects of the
    recorder's plain self-test program, coverage_recorder_probe_plain
    (decision 636); and not on the header self-checks, compiled from files
    CMake generates (decision 644). An object carries the hook when it holds
    the hook's address table (decision 584), read from its sections by
    scripts/compiled-code.py. An object holding no machine code shows nothing
    either way -- src/core/Contract.cpp is empty where assertions are compiled
    out -- and is judged by its command in the compile database instead;
  * **tests/coverage_recorder_unaligned.cpp's object with the flags and
    without the table**, which the recorder's self-test needs (decision 632);
  * **every program linked with its map**, lld-link's `/map`, beside it
    (decision 582), which turns a recorded address into a function;
  * **the recorder linked into every program carrying the hook**, and into
    neither ERFA's two validation programs nor the plain self-test program,
    which carry no hook either (decisions 636 and 639);
  * **every test CMakeLists.txt defines carrying `ORBSIM_TEST_NAME` with its
    own name** in its environment, in every tree, the hook on or off (decision
    564): a record carries the name, so that a test run through a script is
    matched to the records of every program it started. A test CMakeLists.txt
    defines is told from one Catch2 discovered by its definition: CTest's own
    description, `ctest --show-only=json-v1`, gives the first a backtrace into
    CMakeLists.txt and the second none.

With the hook off, no object and no program may carry it, and nothing may link
the recorder.

Proven able to fail before it was trusted: --self-test runs the check on
made-up compile databases, objects, programs, maps and test listings, one for
each fault above and for each way the check could pass by reading nothing,
and exits non-zero unless every fault is reported, for its own reason, and
every correct case accepted -- and it runs scripts/compiled-code.py's own
self-test of the section and map readers first.
"""

import importlib.util
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
HOOK_STATES = ("on", "off")
# The flag that turns the hook on, and the one that leaves out its table.
HOOK_FLAG = "-fsanitize-coverage=func,inline-bool-flag,pc-table"
NO_TABLE_FLAG = "-fno-sanitize-coverage=pc-table"
ANY_HOOK_FLAG = "-fsanitize-coverage="
# Sources of ours compiled without the hook: someone else's code (decision
# 588) and the recorder the hook calls (decision 566).
UNHOOKED_SOURCES = ("src/view/StbImageImpl.cpp", "src/render/VmaImpl.cpp", "tests/CoverageRecorder.cpp")
# The source whose object carries the flags without the table (decision 632).
UNALIGNED_SOURCE = "tests/coverage_recorder_unaligned.cpp"
# The target whose objects are compiled from our sources without the hook,
# for its exit code and output (decision 636).
PLAIN_TARGET = "coverage_recorder_probe_plain"
# The programs that carry neither the hook nor the recorder: ERFA's two
# validation programs, plain C compiled from ERFA's own sources (decisions 588
# and 639), and the plain self-test program (decision 636).
UNHOOKED_PROGRAMS = ("t_erfa_c", "t_erfa_c_extra", PLAIN_TARGET)
# The recorder's object, as a link names it.
RECORDER = re.compile(r"(^|/)CMakeFiles/orbsim_coverage_recorder\.dir/tests/CoverageRecorder\.cpp\.o(bj)?$",
                      re.IGNORECASE)
TEST_NAME = "ORBSIM_TEST_NAME"
OBJECT_SUFFIXES = (".obj", ".o")


def load_compiled_code():
    sys.dont_write_bytecode = True  # no __pycache__ in scripts/
    loader = importlib.util.spec_from_file_location("compiled_code", ROOT / "scripts" / "compiled-code.py")
    module = importlib.util.module_from_spec(loader)
    loader.loader.exec_module(module)
    return module


compiled_code = load_compiled_code()


def norm(path: str) -> str:
    text = str(path).replace("\\", "/")
    return text.lower() if os.name == "nt" else text


def own_source(source: str, root: str):
    """The source's path relative to the repository if it is under src/ or
    tests/ -- ours -- or None."""
    base = norm(root).rstrip("/") + "/"
    path = norm(source)
    if not path.startswith(base):
        return None
    relative = str(source).replace("\\", "/")[len(base):]
    return relative if relative.split("/", 1)[0] in ("src", "tests") else None


def target_of(obj: str) -> str:
    match = re.search(r"(?:^|/)CMakeFiles/([^/]+)\.dir/", str(obj).replace("\\", "/"))
    return match.group(1) if match else ""


def expected(relative, target: str, hook: bool) -> str:
    """What an object must carry: "hook", "flags" (without the table) or "none"."""
    if not hook or relative is None or target == PLAIN_TARGET or relative in UNHOOKED_SOURCES:
        return "none"
    return "flags" if relative == UNALIGNED_SOURCE else "hook"


def judge_object(want: str, read, command: str):
    """Why an object breaks the rule, or None. `read` is compiled-code.py's
    (format, sections), None for a file it cannot read."""
    if read is None:
        return "cannot be read as an object file"
    kind, found = read
    flags, table = compiled_code.carries(kind, found)
    if not compiled_code.holds_code(found):
        if flags or table:
            return "holds the hook's sections and no machine code"
        commanded = {"hook": HOOK_FLAG in command and NO_TABLE_FLAG not in command,
                     "flags": HOOK_FLAG in command and NO_TABLE_FLAG in command,
                     "none": ANY_HOOK_FLAG not in command}[want]
        return None if commanded else f"holds no machine code, and its command does not compile it as '{want}'"
    if want == "hook" and not table:
        return "has no address table: it was compiled without the hook"
    if want == "hook" and not flags:
        return "has the address table and no flags"
    if want == "flags" and (table or not flags):
        return f"must carry the flags without the table, and carries flags {flags}, table {table}"
    if want == "none" and (flags or table):
        return "carries the hook, which it must not"
    return None


def object_problems(objects: list, root: str, hook: bool) -> tuple:
    """(problems, {kind: count}) for [(source, object path, command, read)]."""
    found, counts = [], {"hook": 0, "flags": 0, "none": 0}
    if not objects:
        return ["no object was found: the check would be checking nothing"], counts
    for source, obj, command, read in objects:
        want = expected(own_source(source, root), target_of(obj), hook)
        why = judge_object(want, read, command)
        if why:
            found.append(f"{obj} (from {source}): {why}")
        counts[want] += 1
    if hook and not counts["hook"]:
        found.append("no object is to carry the hook although it is on: the check would be checking nothing")
    if hook and counts["flags"] != 1:
        found.append(f"{counts['flags']} objects of {UNALIGNED_SOURCE} were found, not one")
    return found, counts


def program_problems(programs: list, hook: bool) -> list:
    """For [{name, inputs, link_flags, read, map}], `map` the map's text or None."""
    found = []
    if not programs:
        found.append("no program was found: the check would be checking nothing")
    for program in programs:
        name = program["name"]
        recorder = any(RECORDER.search(i.replace("\\", "/")) for i in program["inputs"])
        read = program["read"]
        if read is None:
            found.append(f"{name}: cannot be read as a program")
            continue
        flags, table = compiled_code.carries(*read)
        if not hook or name in UNHOOKED_PROGRAMS:
            if flags or table:
                found.append(f"{name}: carries the hook's sections, which it must not")
            if recorder:
                found.append(f"{name}: links the recorder, which it must not")
        else:
            if not table:
                found.append(f"{name}: has no address table: it carries no code compiled with the hook")
            if not recorder:
                found.append(f"{name}: carries the hook and does not link the recorder")
        if hook:
            if not re.search(r"(^|\s)(-Xlinker\s+|-Wl,|LINKER:)?/map(\s|$|:)", program["link_flags"], re.IGNORECASE):
                found.append(f"{name}: is not linked with /map")
            if program["map"] is None:
                found.append(f"{name}: has no map beside it")
            elif compiled_code.read_map(program["map"]) is None:
                found.append(f"{name}: its map cannot be read")
    return found


def test_problems(listing: dict, root: str) -> tuple:
    """(problems, count): every test CMakeLists.txt defines must carry
    ORBSIM_TEST_NAME with its own name, once."""
    found = []
    graph = listing.get("backtraceGraph", {})
    files, nodes = graph.get("files", []), graph.get("nodes", [])
    top = norm(pathlib.PurePath(root) / "CMakeLists.txt")
    defined = 0
    for test in listing.get("tests", []):
        node = test.get("backtrace")
        first = None
        while node is not None:
            first = files[nodes[node]["file"]]
            node = nodes[node].get("parent")
        if first is None or norm(first) != top:
            continue
        defined += 1
        environment = next((p["value"] for p in test.get("properties", []) if p["name"] == "ENVIRONMENT"), [])
        names = [v.split("=", 1)[1] for v in environment if v.startswith(TEST_NAME + "=")]
        if names != [test["name"]]:
            found.append(f"the test {test['name']}: its environment names it {names}, not [{test['name']!r}]")
    if not defined:
        found.append("no test is defined in CMakeLists.txt: the check would be checking nothing")
    return found, defined


# --- reading the tree -----------------------------------------------------------

def unescape(token: str) -> str:
    return token.replace("$:", ":").replace("$ ", " ").replace("$$", "$")


def link_statements(ninja_text: str) -> list:
    """[{name, output, inputs, link_flags}] for every program the top-level
    directory links: a link rule's statement whose program is in the tree's
    root, and the LINK_FLAGS bound under it."""
    text = ninja_text.replace("$\n", "")
    found, current = [], None
    for line in text.splitlines():
        if line.startswith("build "):
            current = None
            head, sep, rest = line[len("build "):].partition(": ")
            tokens = [unescape(t) for t in re.split(r"(?<!\$) ", rest) if t]
            if not sep or not tokens or not re.match(r"(C|CXX)_EXECUTABLE_LINKER__", tokens[0]):
                continue
            outputs = [unescape(t) for t in re.split(r"(?<!\$) ", head) if t and t != "|"]
            if "/" in outputs[0] or "\\" in outputs[0]:
                continue
            inputs = []
            for token in tokens[1:]:
                if token in ("|", "||"):
                    break
                inputs.append(token)
            current = {"output": outputs[0], "name": re.sub(r"\.exe$", "", outputs[0]), "inputs": inputs,
                       "link_flags": ""}
            found.append(current)
        elif current is not None and line.startswith("  LINK_FLAGS = "):
            current["link_flags"] = line[len("  LINK_FLAGS = "):]
        elif not line.startswith(" "):
            current = None
    return found


def read_file(path: pathlib.Path):
    try:
        return compiled_code.sections(path.read_bytes())
    except OSError:
        return None


def tree_objects(build: pathlib.Path) -> tuple:
    """([(source, object, command, read)], not built): every object the
    compile database names that is on disk; the others are counted."""
    database = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    objects, missing = [], 0
    for entry in database:
        out = pathlib.Path(entry.get("output", ""))
        if not out.is_absolute():
            out = pathlib.Path(entry["directory"]) / out
        if out.suffix not in OBJECT_SUFFIXES:
            continue
        if not out.is_file():
            missing += 1
            continue
        command = entry.get("command") or " ".join(entry.get("arguments", []))
        objects.append((entry["file"], out.relative_to(build).as_posix() if out.is_relative_to(build) else str(out),
                        command, read_file(out)))
    return objects, missing


def tree_programs(build: pathlib.Path) -> list:
    programs = []
    for statement in link_statements((build / "build.ninja").read_text(encoding="utf-8", errors="replace")):
        program = build / statement["output"]
        mapped = program.with_suffix(".map")
        try:
            map_text = mapped.read_text(encoding="utf-8", errors="replace")
        except OSError:
            map_text = None
        programs.append({**statement, "read": read_file(program), "map": map_text})
    return programs


def check(build: pathlib.Path, root: pathlib.Path, ctest: str, hook: bool) -> int:
    objects, missing = tree_objects(build)
    found, counts = object_problems(objects, str(root), hook)
    programs = tree_programs(build)
    found += program_problems(programs, hook)
    listing = json.loads(subprocess.run([ctest, "--show-only=json-v1"], cwd=build, capture_output=True, text=True,
                                        encoding="utf-8", errors="replace", check=True).stdout)
    test_found, defined = test_problems(listing, str(root))
    found += test_found
    for line in found:
        print(f"check-coverage-hook: {line}")
    summary = (f"{len(objects)} objects judged ({counts['hook']} to carry the hook, {counts['flags']} the flags "
               f"alone, {counts['none']} neither) and {missing} in the compile database not built; "
               f"{len(programs)} programs; {defined} tests defined in CMakeLists.txt")
    if found:
        print(f"check-coverage-hook: {len(found)} problem(s) -- {summary}")
        return 1
    where = "on exactly the project's own code, every program with its map" if hook else "nowhere"
    print(f"check-coverage-hook: the hook {where}, every test carrying its name -- {summary}")
    return 0


# --- self-test --------------------------------------------------------------------

def self_test() -> int:
    failures = compiled_code.self_test()
    root = "C:/r" if os.name == "nt" else "/r"
    coff = compiled_code.made_coff
    pe = compiled_code.made_pe
    hooked = compiled_code.sections(coff([".text", ".SCOV$BM", ".SCOVP$M"], code=(".text",)))
    flags_only = compiled_code.sections(coff([".text", ".SCOV$BM"], code=(".text",)))
    plain = compiled_code.sections(coff([".text", ".data"], code=(".text",)))
    empty = compiled_code.sections(coff([".text", ".data"]))
    on, off = f"clang++ {HOOK_FLAG} -c", "clang++ -c"

    def obj(source: str, target: str, read, command: str) -> tuple:
        name = source.rsplit("/", 1)[-1]
        return (f"{root}/{source}", f"CMakeFiles/{target}.dir/{source}.obj" if target else f"_deps/x/{name}.obj",
                command, read)

    good_objects = [
        obj("src/orbit/Orbit.cpp", "orbsim_core", hooked, on),
        obj("src/core/Contract.cpp", "orbsim_core", empty, on),
        obj("tests/test_math.cpp", "test_math", hooked, on),
        obj("src/view/StbImageImpl.cpp", "orbsim_stb_image", plain, off),
        obj("tests/CoverageRecorder.cpp", "orbsim_coverage_recorder", plain, off),
        obj("tests/coverage_recorder_probe.cpp", PLAIN_TARGET, plain, off),
        obj("src/core/Contract.cpp", PLAIN_TARGET, plain, off),
        obj("tests/coverage_recorder_unaligned.cpp", "coverage_recorder_unaligned", flags_only,
            f"{on} {NO_TABLE_FLAG}"),
        obj("build/selfcheck/orbsim_header_selfcheck_core_Math_hpp.cpp", "orbsim_header_selfcheck", plain, off),
        obj("build/_deps/catch2-src/src/catch.cpp", "", plain, off),
    ]

    def replaced(index: int, read, command: str = None) -> list:
        changed = list(good_objects)
        source, path, old, _ = changed[index]
        changed[index] = (source, path, old if command is None else command, read)
        return changed

    object_cases = {
        "the objects as they should be": (good_objects, True, None),
        "an object of ours without the hook": (replaced(0, plain), True, "compiled without the hook"),
        "a test's object without the hook": (replaced(2, plain), True, "compiled without the hook"),
        "an object with the table and no flags": (
            replaced(0, compiled_code.sections(coff([".text", ".SCOVP$M"], code=(".text",)))), True, "no flags"),
        "stb_image's decoder with the hook": (replaced(3, hooked), True, "which it must not"),
        "the recorder with the hook": (replaced(4, hooked), True, "which it must not"),
        "the plain program's own source with the hook": (replaced(5, hooked), True, "which it must not"),
        "the plain program's Contract.cpp with the hook": (replaced(6, hooked), True, "which it must not"),
        "the unaligned object with its table": (replaced(7, hooked), True, "flags without the table"),
        "the unaligned object without its flags": (replaced(7, plain), True, "flags without the table"),
        "a header self-check with the hook": (replaced(8, hooked), True, "which it must not"),
        "someone else's object with the hook": (replaced(9, hooked), True, "which it must not"),
        "an object with no code compiled without the hook": (replaced(1, empty, off), True, "holds no machine code"),
        "an object of someone else's with no code compiled with it": (
            replaced(9, empty, on), True, "holds no machine code"),
        "an object that cannot be read": (replaced(2, None), True, "cannot be read"),
        "no objects": ([], True, "checking nothing"),
        "no object to carry the hook": ([good_objects[3], good_objects[7]], True, "checking nothing"),
        "the unaligned object missing": (good_objects[:7], True, "not one"),
        "the hook off, nothing carrying it": (
            [obj("src/orbit/Orbit.cpp", "orbsim_core", plain, off), obj("src/core/Contract.cpp", "orbsim_core",
                                                                          empty, off)], False, None),
        "the hook off, an object carrying it": ([obj("src/orbit/Orbit.cpp", "orbsim_core", hooked, on)], False,
                                                "which it must not"),
        "the hook off, an object with no code compiled with it": (
            [obj("src/core/Contract.cpp", "orbsim_core", empty, on)], False, "holds no machine code"),
    }

    good_map = compiled_code.SAMPLE_MAP
    hooked_program = compiled_code.sections(pe([".text", ".SCOV", ".SCOVP"]))
    plain_program = compiled_code.sections(pe([".text", ".rdata"]))
    recorder = "CMakeFiles/orbsim_coverage_recorder.dir/tests/CoverageRecorder.cpp.obj"

    def program(name: str, read, with_recorder: bool, link_flags: str = "-Xlinker /map", map_text=good_map) -> dict:
        inputs = [f"CMakeFiles/{name}.dir/tests/{name}.cpp.obj"] + ([recorder] if with_recorder else [])
        return {"name": name, "output": f"{name}.exe", "inputs": inputs, "link_flags": link_flags, "read": read,
                "map": map_text}

    good_programs = [program("test_math", hooked_program, True), program("t_erfa_c", plain_program, False),
                     program(PLAIN_TARGET, plain_program, False)]
    off_programs = [program("test_math", plain_program, False, ""), program("t_erfa_c", plain_program, False, "")]
    program_cases = {
        "the programs as they should be": (good_programs, True, None),
        "a program without /map": ([program("test_math", hooked_program, True, "-Xlinker /Brepro")], True,
                                   "not linked with /map"),
        "a program without its map": ([program("test_math", hooked_program, True, map_text=None)], True,
                                      "no map beside it"),
        "a map that cannot be read": ([program("test_math", hooked_program, True, map_text="not a map")], True,
                                      "cannot be read"),
        "a hooked program without the recorder": ([program("test_math", hooked_program, False)], True,
                                                  "does not link the recorder"),
        "a program with the recorder and no hooked code": ([program("test_math", plain_program, True)], True,
                                                           "no address table"),
        "ERFA's program linking the recorder": ([program("t_erfa_c", plain_program, True)], True,
                                                "links the recorder"),
        "the plain program carrying the hook": ([program(PLAIN_TARGET, hooked_program, False)], True,
                                                "which it must not"),
        "a program that cannot be read": ([program("test_math", None, True)], True, "cannot be read"),
        "no programs": ([], True, "checking nothing"),
        "the hook off, the programs as they should be": (off_programs, False, None),
        "the hook off, a program carrying it": ([program("test_math", hooked_program, False, "")], False,
                                                "which it must not"),
        "the hook off, a program linking the recorder": ([program("test_math", plain_program, True, "")], False,
                                                         "links the recorder"),
    }

    def listing(*tests) -> dict:
        graph = {"files": [f"{root}/CMakeLists.txt", f"{root}/cmake/ScriptTests.cmake", f"{root}/elsewhere.txt"],
                 "nodes": [{"file": 0}, {"file": 1, "parent": 0}, {"file": 2}], "commands": []}
        entries = []
        for name, node, environment in tests:
            entry = {"name": name, "properties": [{"name": "ENVIRONMENT", "value": environment}]}
            if node is not None:
                entry["backtrace"] = node
            entries.append(entry)
        return {"backtraceGraph": graph, "tests": entries}

    named = [("memory_pool", 1, [f"{TEST_NAME}=memory_pool"]), ("t_erfa_c", 0, ["X=1", f"{TEST_NAME}=t_erfa_c"])]
    test_cases = {
        "every test with its name": (listing(*named), None),
        "a Catch2 test without one, which needs none": (listing(*named, ("Kepler", None, [])), None),
        "a test from another file without one, which needs none": (listing(*named, ("theirs", 2, [])), None),
        "a test without its name": (listing(named[0], ("t_erfa_c", 0, ["X=1"])), "names it []"),
        "a test with another's name": (listing(named[0], ("t_erfa_c", 0, [f"{TEST_NAME}=memory_pool"])),
                                       "names it ['memory_pool']"),
        "a test named twice": (listing(named[0], ("t_erfa_c", 0, [f"{TEST_NAME}=t_erfa_c"] * 2)), "names it"),
        "no test defined in CMakeLists.txt": (listing(("Kepler", None, [])), "checking nothing"),
    }

    def verdict(name: str, got: list, reason) -> None:
        nonlocal failures
        # A fault must be reported for its own reason and for nothing else.
        good = not got if reason is None else bool(got) and all(reason in line for line in got)
        failures += not good
        shown = "accepted" if not got else f"reported: {got[0]}"
        print(f"self-test: {name}: {shown} ({'ok' if good else 'WRONG'})")

    for name, (objects, hook, reason) in object_cases.items():
        verdict(name, object_problems(objects, root, hook)[0], reason)
    for name, (programs, hook, reason) in program_cases.items():
        verdict(name, program_problems(programs, hook), reason)
    for name, (tests, reason) in test_cases.items():
        verdict(name, test_problems(tests, root)[0], reason)

    # The tree's readers on a small tree written here: a compile database, the
    # objects it names, a Ninja file with a program at the top and one below.
    with tempfile.TemporaryDirectory() as scratch:
        build = pathlib.Path(scratch)
        (build / "CMakeFiles/t.dir/src").mkdir(parents=True)
        (build / "CMakeFiles/t.dir/src/a.cpp.obj").write_bytes(coff([".text", ".SCOV$BM", ".SCOVP$M"],
                                                                    code=(".text",)))
        database = [{"directory": scratch, "file": f"{root}/src/a.cpp", "output": "CMakeFiles/t.dir/src/a.cpp.obj",
                     "command": on},
                    {"directory": scratch, "file": f"{root}/src/b.cpp", "output": "CMakeFiles/t.dir/src/b.cpp.obj",
                     "command": on},
                    {"directory": scratch, "file": f"{root}/b.h", "output": "CMakeFiles/t.dir/cmake_pch.hxx.pch",
                     "command": on}]
        (build / "compile_commands.json").write_bytes(json.dumps(database).encode())
        objects, missing = tree_objects(build)
        verdict("the tree's objects read, the one not built counted, the precompiled header left out",
                [] if (len(objects), missing, objects[0][1]) == (1, 1, "CMakeFiles/t.dir/src/a.cpp.obj")
                else [f"read {len(objects)} objects and {missing} missing"], None)
        ninja = ("build t.exe: CXX_EXECUTABLE_LINKER__t_Debug CMakeFiles/t.dir/src/a.cpp.obj "
                 f"{recorder} | orbsim_core.lib || order\n"
                 "  FLAGS = -O0\n  LINK_FLAGS = -Xlinker /Brepro -Xlinker /map\n"
                 "build _deps/x-build/x.exe: CXX_EXECUTABLE_LINKER__x_Debug x.obj\n  LINK_FLAGS = \n"
                 "build CMakeFiles/t.dir/src/a.cpp.obj: CXX_COMPILER__t_Debug C$:/r/src/a.cpp\n")
        statements = link_statements(ninja)
        want = [{"output": "t.exe", "name": "t", "inputs": ["CMakeFiles/t.dir/src/a.cpp.obj", recorder],
                 "link_flags": "-Xlinker /Brepro -Xlinker /map"}]
        verdict("the top-level programs read from the Ninja file", [] if statements == want else [str(statements)],
                None)
    return 1 if failures else 0


def main() -> int:
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    if len(sys.argv) == 6 and sys.argv[4] == "--hook" and sys.argv[5] in HOOK_STATES:
        return check(pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve(), sys.argv[3],
                     sys.argv[5] == "on")
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())

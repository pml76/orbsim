#!/usr/bin/env python3
"""Check that the coverage recorder names exactly the functions a program entered, however it ends (M1-131).

    check-coverage-recorder.py --probe <program> --unaligned <program> --plain <program> --config <config>
    check-coverage-recorder.py --application <orbsim> --shaders <the tree's shader folder>

Why this exists: the recorder, tests/CoverageRecorder.cpp, writes the record
M1-132 will choose tests from: the functions a program entered (register
decisions 559 and 644). A function missing from a record is a function whose
change would rerun no test, so the record is held here to the functions a
small program, tests/coverage_recorder_probe.cpp, is known to enter -- the
names read from the linker's map through scripts/compiled-code.py (decision
582) -- in every way that program can end (decision 634):

  * **a normal end**, the record written after the static destructors, so that
    it names the function one of them entered (decision 626) -- and the same
    after a file written and read back through the standard library's file
    streams, which the recorder's own stream once failed after (decision 659);
  * **each way of stopping the probe has** -- a failed precondition and the
    debug library's own check where the tree is Debug, an exception escaping
    a noexcept function, a read through a null pointer, a plain abort and a
    bad argument to the C runtime -- each record
    naming the functions entered up to the stop and none after it, and saying
    how the program ended; and the exit code and the output, stdout and
    stderr, those of the same program built with neither the hook nor the
    recorder, coverage_recorder_probe_plain (decision 636), once the program's
    name and the call chain's addresses, which differ between any two builds,
    are set aside;
  * **every record whole**: the layout of decision 644, the program, its
    command line and the test it was run for as given, and last the `end`
    line with the number of functions, without which a record counts as none
    (decision 637); one record a run, named after the process number and the
    start time (decisions 564, 627 and 632);
  * **no record without ORBSIM_COVERAGE_RECORDS**, unset or empty (decision 570);
  * **no function named where the flags and the address table do not line up**:
    coverage_recorder_unaligned holds one flag more than its table has entries,
    and its record must say "out of line" and name none (decisions 559 and 627).

Proven able to fail before it was trusted: before any program runs, the
record reader is given made-up records -- whole, without `end`, with the wrong
count, cut off inside a line, out of line yet naming a function, ended in no
way it knows -- and must read exactly the whole ones; and scripts/compiled-code.py's
own self-test runs, which reads made-up maps. The check failed against a
recorder that wrote nothing before the recorder was written.

**--application** checks the application's own record instead, as the CTest
test shader_record (decision 642): the shader files a run asked the renderer
for, which the test driver fingerprints a test on in place of every compiled
shader (decision 629). A shader missing from it is a shader whose change
would rerun no test, so the application is run with ORBSIM_COVERAGE_RECORDS
naming a scratch folder, and

  * **the `clear` probe's record must name exactly fullscreen.vert.spv,
    probe_gradient.frag.spv and tonemap.frag.spv**, the three its sidecar
    names, each once and by its full path in the tree's shader folder;
  * **a usage run's must name none**, since it is refused before a window
    exists;
  * **each record whole**: `test` with the name it was run for, the shader
    files, and last `end`, without which a record counts as none (decisions
    637 and 644); one a run, named after the process number and the start
    time (decision 632), beside the recorder's own where the hook is on;
  * **no record without ORBSIM_COVERAGE_RECORDS**, unset or empty.

Proven able to fail before it was trusted in the same way: the record reader
is first given made-up records -- whole, without `end`, cut off inside a
line, `end` not last -- and must read exactly the whole ones; and the check
failed against an application that wrote no record, and against one that
left out its `end` line and one that left out a shader.
"""

import argparse
import importlib.util
import os
import pathlib
import re
import subprocess
import sys
import tempfile
from typing import NamedTuple, Optional

ROOT = pathlib.Path(__file__).resolve().parent.parent
FOLDER = "ORBSIM_COVERAGE_RECORDS"
TEST_NAME = "ORBSIM_TEST_NAME"
# How the recorder says a program ended (decision 644).
ENDINGS = ("normally", "by abort", "by a crash", "by a bad argument")
RECORD_NAME = re.compile(r"^\d+-\d+\.functions\.txt$")
# A run of the probe takes well under a second; one still running after a
# minute is waiting on something -- a dialog asking a question, most likely.
RUN_SECONDS = 60
# The probe's own functions: declared in orb::coverage_probe, or in an
# anonymous namespace inside it, as the compiler's decorated names spell them.
PROBE_FUNCTION = re.compile(r"^\?([A-Za-z_]\w*)@(?:\?A0x[0-9a-fA-F]+@)?coverage_probe@orb@@")
PROBE_OBJECT = "coverage_recorder_probe.cpp.obj"
# Every function tests/coverage_recorder_probe.cpp writes in that namespace, so
# that one added there and not here fails rather than going unjudged.
PROBE_FUNCTIONS = {"runScenario", "stopAs", "enteredFirst", "enteredAfterTheStop", "neverEntered",
                   "enteredByTheStaticDestructor", "requiresZero", "throwsWhenPositive", "escapesNoexcept",
                   "readsThroughNull", "stepsBeforeBegin", "abortsWhenPositive", "convertsIntoNoBuffer",
                   "usesAFileStream"}


class Scenario(NamedTuple):
    name: str
    ended: str
    entered: frozenset
    debug_only: bool


STOPPED = frozenset({"runScenario", "enteredFirst", "stopAs"})
SCENARIOS = (
    Scenario("normally", "normally", frozenset({"runScenario", "enteredFirst", "enteredByTheStaticDestructor"}),
             False),
    # A program that used a file stream before it ends (decision 659).
    Scenario("file-stream", "normally",
             frozenset({"runScenario", "enteredFirst", "usesAFileStream", "enteredByTheStaticDestructor"}), False),
    Scenario("precondition", "by abort", STOPPED | {"requiresZero"}, True),
    Scenario("noexcept", "by abort", STOPPED | {"escapesNoexcept", "throwsWhenPositive"}, False),
    Scenario("crash", "by a crash", STOPPED | {"readsThroughNull"}, False),
    Scenario("library-check", "by a crash", STOPPED | {"stepsBeforeBegin"}, True),
    Scenario("abort", "by abort", STOPPED | {"abortsWhenPositive"}, False),
    Scenario("bad-argument", "by a bad argument", STOPPED | {"convertsIntoNoBuffer"}, False),
)


def load_compiled_code():
    sys.dont_write_bytecode = True  # no __pycache__ in scripts/
    loader = importlib.util.spec_from_file_location("compiled_code", ROOT / "scripts" / "compiled-code.py")
    module = importlib.util.module_from_spec(loader)
    loader.loader.exec_module(module)
    return module


compiled_code = load_compiled_code()


# --- the record ------------------------------------------------------------------

class Record(NamedTuple):
    program: str
    command: str
    test: str
    ended: str
    flags: int
    out_of_line: bool
    functions: list


def read_record(text: str) -> Optional[Record]:
    """The record decision 644 lays out, or None: a record cut off anywhere --
    without its `end` line, with another count, inside a line -- counts as
    none (decision 637), and so does one that breaks the layout."""
    if not text.endswith("\n"):
        return None
    lines = text[:-1].split("\n")
    if len(lines) < 6:
        return None
    head = []
    for key, line in zip(("program", "command", "test", "ended"), lines):
        if line != key and not line.startswith(key + " "):
            return None
        head.append(line[len(key) + 1:])
    flags = re.fullmatch(r"flags (\d+)( out of line)?", lines[4])
    end = re.fullmatch(r"end (\d+)", lines[-1])
    body = lines[5:-1]
    if flags is None or end is None or head[3] not in ENDINGS:
        return None
    if int(end.group(1)) != len(body) or not all(re.fullmatch(r"[0-9a-f]+", line) for line in body):
        return None
    if flags.group(2) and body:
        return None
    return Record(*head, int(flags.group(1)), bool(flags.group(2)), [int(line, 16) for line in body])


def reader_self_test() -> int:
    whole = "program C:\\p.exe\ncommand C:\\p.exe normally\ntest t\nended normally\nflags 3\n1a0\n2b0\nend 2\n"
    cases = {
        "a whole record": (whole, True),
        "a whole record of no function": (whole.replace("1a0\n2b0\nend 2", "end 0"), True),
        "a record run for no test": (whole.replace("test t\n", "test \n"), True),
        "a record without its end line": (whole.replace("end 2\n", ""), False),
        "a record with the wrong count": (whole.replace("end 2", "end 3"), False),
        "a record cut off inside a line": (whole[:-3], False),
        "a record cut off after a function": (whole.split("2b0")[0], False),
        "out of line and naming a function": (whole.replace("flags 3", "flags 3 out of line"), False),
        "out of line and naming none": (whole.replace("flags 3", "flags 3 out of line").replace(
            "1a0\n2b0\nend 2", "end 0"), True),
        "ended in no way the recorder knows": (whole.replace("ended normally", "ended somehow"), False),
        "a function that is not an address": (whole.replace("2b0", "main"), False),
        "a header line missing": (whole.replace("test t\n", ""), False),
        "nothing": ("", False),
    }
    failures = 0
    for name, (text, readable) in cases.items():
        got = read_record(text) is not None
        failures += got != readable
        print(f"self-test: {name}: {'read' if got else 'counted as none'} ({'ok' if got == readable else 'WRONG'})")
    record = read_record(whole)
    if record is None or record.functions != [0x1A0, 0x2B0] or record.flags != 3 or record.test != "t":
        print(f"self-test: the whole record's fields: WRONG, read {record}")
        failures += 1
    return failures


# --- running the programs ------------------------------------------------------------

class Run(NamedTuple):
    code: int
    stdout: str
    stderr: str
    records: list           # [(file name, text)]


def end_crashes_at_once() -> None:
    """A crash handed to Windows Error Reporting took 6-7 s a run where CTest
    starts this script, against 0.05 s with its dialog switched off as the
    shell had it (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX, measured
    2026-10-18 on both programs): the programs inherit this mode, and the exit
    code and output are the same under either, which is what is compared."""
    import ctypes  # Windows only, as the hook is (decision 640)
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)


def run(program: pathlib.Path, scenario: str, folder: Optional[str], test: Optional[str],
        cwd: pathlib.Path) -> Run:
    environment = {k: v for k, v in os.environ.items() if k not in (FOLDER, TEST_NAME)}
    if folder is not None:
        environment[FOLDER] = folder
    if test is not None:
        environment[TEST_NAME] = test
    done = subprocess.run([str(program), scenario], capture_output=True, env=environment, cwd=cwd,
                          timeout=RUN_SECONDS, check=False)
    records = []
    if folder:
        records = [(p.name, p.read_bytes().decode("utf-8", errors="replace"))
                   for p in sorted(pathlib.Path(folder).iterdir())]
    return Run(done.returncode & 0xFFFFFFFF, done.stdout.decode("utf-8", errors="replace"),
               done.stderr.decode("utf-8", errors="replace"), records)


def comparable(text: str, programs: list) -> str:
    """The output with what differs between any two builds set aside: the
    program's name, and every address and offset in the call chain."""
    for program in programs:
        text = re.sub(re.escape(program.stem) + r"\b", "<program>", text, flags=re.IGNORECASE)
    return re.sub(r"0x[0-9A-Fa-f]+", "0x?", text)


def probe_functions(linker_map) -> dict:
    """{address: name} of the probe's own functions, from its own object."""
    found = {}
    for symbol in linker_map.symbols:
        match = PROBE_FUNCTION.match(symbol.name)
        if match and symbol.code and symbol.object.lower() == PROBE_OBJECT:
            found[symbol.address] = match.group(1)
    return found


def judge_record(found: Run, program: pathlib.Path, scenario: str, test: str):
    """(record, problems) of a run that must leave one whole record."""
    problems = []
    if len(found.records) != 1:
        return None, [f"left {len(found.records)} records, not one: {[n for n, _ in found.records]}"]
    name, text = found.records[0]
    if not RECORD_NAME.match(name):
        problems.append(f"its record is named {name}, not <process number>-<start time>.functions.txt")
    record = read_record(text)
    if record is None:
        return None, problems + [f"its record is not whole: {text[-200:]!r}"]
    if os.path.normcase(os.path.abspath(record.program)) != os.path.normcase(str(program.resolve())):
        problems.append(f"its record names the program {record.program!r}")
    if record.command != subprocess.list2cmdline([str(program), scenario]):
        problems.append(f"its record names the command line {record.command!r}")
    if record.test != test:
        problems.append(f"its record names the test {record.test!r}, not {test!r}")
    if record.flags == 0:
        problems.append("its record counts no flag")
    return record, problems


def judge_scenario(scenario: Scenario, probe: pathlib.Path, plain: pathlib.Path, functions: dict,
                   linker_map, work: pathlib.Path) -> list:
    folder = work / f"records-{scenario.name}"
    folder.mkdir()
    test = f"coverage_recorder {scenario.name}"
    hooked = run(probe, scenario.name, str(folder), test, work)
    bare = run(plain, scenario.name, None, None, work)
    record, problems = judge_record(hooked, probe, scenario.name, test)
    if record is not None:
        if record.ended != scenario.ended:
            problems.append(f"its record says it ended {record.ended}, not {scenario.ended}")
        if record.out_of_line:
            problems.append("its record finds the flags out of line")
        unknown = [a for a in record.functions if not any(s.code for s in linker_map.at(a))]
        if unknown:
            problems.append(f"{len(unknown)} of its {len(record.functions)} functions are no function in the "
                            f"map, e.g. {unknown[0]:x}")
        entered = {functions[a] for a in record.functions if a in functions}
        if entered != set(scenario.entered):
            problems.append(f"its record names the probe's {sorted(entered)}, not {sorted(scenario.entered)}")
    if hooked.code != bare.code:
        problems.append(f"it exits {hooked.code:#x}, and {bare.code:#x} without the recorder")
    for stream in ("stdout", "stderr"):
        ours = comparable(getattr(hooked, stream), [probe, plain])
        theirs = comparable(getattr(bare, stream), [probe, plain])
        if ours != theirs:
            problems.append(f"its {stream} differs from the plain program's: {ours[-300:]!r} against "
                            f"{theirs[-300:]!r}")
    if scenario.name == "normally" and bare.code != 0:
        problems.append(f"the plain program does not end normally: it exits {bare.code:#x}")
    return [f"{scenario.name}: {p}" for p in problems]


def judge_without_folder(probe: pathlib.Path, work: pathlib.Path) -> list:
    problems = []
    for name, value in (("unset", None), ("empty", "")):
        cwd = work / f"without-{name}"
        cwd.mkdir()
        found = run(probe, "normally", value, "coverage_recorder", cwd)
        left = sorted(p.name for p in cwd.iterdir())
        if found.code != 0 or left:
            problems.append(f"with {FOLDER} {name}: exit {found.code:#x}, and it left {left}")
    return problems


def judge_unaligned(unaligned: pathlib.Path, work: pathlib.Path) -> list:
    folder = work / "records-unaligned"
    folder.mkdir()
    found = run(unaligned, "normally", str(folder), "coverage_recorder unaligned", work)
    record, problems = judge_record(found, unaligned, "normally", "coverage_recorder unaligned")
    if record is not None and (not record.out_of_line or record.functions or record.ended != "normally"):
        problems.append(f"its record must find the flags out of line and name no function, and says "
                        f"flags {record.flags}, out of line {record.out_of_line}, {len(record.functions)} "
                        f"functions, ended {record.ended}")
    if found.code != 0:
        problems.append(f"it exits {found.code:#x}")
    return [f"unaligned: {p}" for p in problems]


def check(probe: pathlib.Path, unaligned: pathlib.Path, plain: pathlib.Path, config: str) -> int:
    failures = reader_self_test() + compiled_code.self_test()
    if failures:
        print("check-coverage-recorder: the readers' self-tests failed")
        return 1
    linker_map = compiled_code.read_map(probe.with_suffix(".map").read_text(encoding="utf-8", errors="replace"))
    if linker_map is None:
        print(f"check-coverage-recorder: {probe.with_suffix('.map')} cannot be read as a map")
        return 1
    functions = probe_functions(linker_map)
    problems = []
    if set(functions.values()) != PROBE_FUNCTIONS or len(functions) != len(PROBE_FUNCTIONS):
        problems.append(f"the map names the probe's functions {sorted(functions.values())}, where this check "
                        f"judges {sorted(PROBE_FUNCTIONS)}")
    ran = []
    end_crashes_at_once()
    with tempfile.TemporaryDirectory() as scratch:
        work = pathlib.Path(scratch)
        for scenario in SCENARIOS:
            if scenario.debug_only and config != "Debug":
                print(f"check-coverage-recorder: {scenario.name}: not run, it exists in Debug only")
                continue
            problems += judge_scenario(scenario, probe, plain, functions, linker_map, work)
            ran.append(scenario.name)
        problems += judge_without_folder(probe, work)
        problems += judge_unaligned(unaligned, work)
    for line in problems:
        print(f"check-coverage-recorder: {line}")
    if problems:
        print(f"check-coverage-recorder: {len(problems)} problem(s)")
        return 1
    print(f"check-coverage-recorder: {len(ran)} ways of ending ({', '.join(ran)}) each recorded whole, the probe's "
          f"functions exactly, the exit code and output unchanged; no record without {FOLDER}; the unaligned "
          f"flags refused")
    return 0


# --- the application's record of its shaders (--application) ------------------------

# The application's record, named as the recorder's is, beside it where the
# hook is on (decision 632).
SHADER_RECORD_NAME = re.compile(r"^\d+-\d+\.shaders\.txt$")
# The shader files the `clear` probe asks for: the three its sidecar names,
# read 2026-10-18 in the Debug tree (decision 642).
CLEAR_SHADERS = ("fullscreen.vert.spv", "probe_gradient.frag.spv", "tonemap.frag.spv")
# A usage run, refused before a window exists and so asking for no shader:
# usage_bench_option_without_bench's command line. 2 is kExitUsage in
# src/app/ExitCodes.hpp.
USAGE_ARGUMENTS = ("--frames", "10")
EXIT_USAGE = 2


class ShaderRecord(NamedTuple):
    test: str
    shaders: list


def read_shader_record(text: str) -> Optional[ShaderRecord]:
    """The application's record as decision 644 lays it out -- `test`, a line
    for each shader file, and `end` -- or None: one without its `end` line
    last, or cut off inside a line, counts as none (decision 637)."""
    if not text.endswith("\n"):
        return None
    lines = text[:-1].split("\n")
    if len(lines) < 2 or lines[-1] != "end":
        return None
    if lines[0] != "test" and not lines[0].startswith("test "):
        return None
    shaders = lines[1:-1]
    if not all(shaders) or "end" in shaders:
        return None
    return ShaderRecord(lines[0][len("test "):], shaders)


def shader_reader_self_test() -> int:
    whole = "test t\nC:\\shaders\\a.spv\nC:\\shaders\\b.spv\nend\n"
    cases = {
        "a whole shader record": (whole, True),
        "a whole shader record of no shader": ("test t\nend\n", True),
        "a shader record run for no test": ("test \nend\n", True),
        "a shader record without its end line": (whole.replace("end\n", ""), False),
        "a shader record cut off inside a line": (whole[:-2], False),
        "a shader record whose end line is not last": ("test t\nend\nC:\\shaders\\a.spv\n", False),
        "a shader record with an end line among the shaders": (whole.replace("a.spv\n", "a.spv\nend\n"), False),
        "a shader record with an empty line": (whole.replace("a.spv\n", "a.spv\n\n"), False),
        "a shader record ending with a count": (whole.replace("end\n", "end 2\n"), False),
        "a shader record without its test line": (whole.replace("test t\n", ""), False),
        "nothing as a shader record": ("", False),
    }
    failures = 0
    for name, (text, readable) in cases.items():
        got = read_shader_record(text) is not None
        failures += got != readable
        print(f"self-test: {name}: {'read' if got else 'counted as none'} ({'ok' if got == readable else 'WRONG'})")
    record = read_shader_record(whole)
    if record is None or record.test != "t" or record.shaders != ["C:\\shaders\\a.spv", "C:\\shaders\\b.spv"]:
        print(f"self-test: the whole shader record's fields: WRONG, read {record}")
        failures += 1
    return failures


def run_application(application: pathlib.Path, arguments: list, folder: Optional[str], test: str,
                    cwd: pathlib.Path) -> Run:
    """A run of the application, and the records it left in `folder` but the
    recorder's, which the hook's own self-test judges."""
    environment = {k: v for k, v in os.environ.items() if k not in (FOLDER, TEST_NAME)}
    if folder is not None:
        environment[FOLDER] = folder
    environment[TEST_NAME] = test
    done = subprocess.run([str(application), *arguments], capture_output=True, env=environment, cwd=cwd,
                          timeout=RUN_SECONDS, check=False)
    records = []
    if folder:
        records = [(p.name, p.read_bytes().decode("utf-8", errors="replace"))
                   for p in sorted(pathlib.Path(folder).iterdir()) if not RECORD_NAME.match(p.name)]
    return Run(done.returncode & 0xFFFFFFFF, done.stdout.decode("utf-8", errors="replace"),
               done.stderr.decode("utf-8", errors="replace"), records)


def comparable_path(text: str) -> str:
    return os.path.normcase(os.path.normpath(text))


def judge_shader_record(found: Run, test: str, expected_exit: int, shaders: set) -> list:
    """The problems of a run that must exit `expected_exit` and leave one whole
    record naming exactly `shaders`, each once."""
    problems = []
    if found.code != expected_exit:
        problems.append(f"it exits {found.code:#x}, not {expected_exit}: {found.stderr[-300:]!r}")
    if len(found.records) != 1:
        return problems + [f"left {len(found.records)} shader records, not one: {[n for n, _ in found.records]}"]
    name, text = found.records[0]
    if not SHADER_RECORD_NAME.match(name):
        problems.append(f"its record is named {name}, not <process number>-<start time>.shaders.txt")
    record = read_shader_record(text)
    if record is None:
        return problems + [f"its record is not whole: {text[-300:]!r}"]
    if record.test != test:
        problems.append(f"its record names the test {record.test!r}, not {test!r}")
    if len(set(record.shaders)) != len(record.shaders):
        problems.append(f"its record names a shader file more than once: {record.shaders}")
    if {comparable_path(shader) for shader in record.shaders} != shaders:
        problems.append(f"its record names {sorted(record.shaders)}, not {sorted(shaders)}")
    return problems


def check_shader_record(application: pathlib.Path, shader_folder: pathlib.Path) -> int:
    if shader_reader_self_test():
        print("check-coverage-recorder: the shader record reader's self-test failed")
        return 1
    clear_shaders = {comparable_path(str(shader_folder / name)) for name in CLEAR_SHADERS}
    problems = []
    with tempfile.TemporaryDirectory() as scratch:
        work = pathlib.Path(scratch)
        for folder in ("records-clear", "records-usage", "probe", "without-unset", "without-empty"):
            (work / folder).mkdir()
        clear = ["--validate", "--probe", "clear", "--probe-out", str(work / "probe")]
        usage = list(USAGE_ARGUMENTS)
        for name, arguments, expected_exit, shaders in (("clear", clear, 0, clear_shaders),
                                                         ("usage", usage, EXIT_USAGE, set())):
            test = f"shader_record {name}"
            found = run_application(application, arguments, str(work / f"records-{name}"), test, work)
            problems += [f"{name}: {p}" for p in judge_shader_record(found, test, expected_exit, shaders)]
        # Without the variable, where a record would go is nowhere it names:
        # the working folder is the likeliest, and no record may be anywhere.
        for name, arguments, expected_exit, folder in (("unset", clear, 0, None), ("empty", usage, EXIT_USAGE, "")):
            cwd = work / f"without-{name}"
            found = run_application(application, arguments, folder, "shader_record", cwd)
            left = sorted(p.name for p in cwd.iterdir())
            if found.code != expected_exit or left:
                problems.append(f"with {FOLDER} {name}: exit {found.code:#x}, and it left {left}")
        strays = [str(p.relative_to(work)) for p in work.rglob("*.shaders.txt")
                  if p.parent.name not in ("records-clear", "records-usage")]
        if strays:
            problems.append(f"shader records where none was asked for: {strays}")
    for line in problems:
        print(f"check-coverage-recorder: {line}")
    if problems:
        print(f"check-coverage-recorder: {len(problems)} problem(s)")
        return 1
    print(f"check-coverage-recorder: the clear probe's record names exactly {', '.join(CLEAR_SHADERS)} in "
          f"{shader_folder}, a usage run's none, each whole; no record without {FOLDER}, unset or empty")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--probe", type=pathlib.Path)
    parser.add_argument("--unaligned", type=pathlib.Path)
    parser.add_argument("--plain", type=pathlib.Path)
    parser.add_argument("--config")
    parser.add_argument("--application", type=pathlib.Path)
    parser.add_argument("--shaders", type=pathlib.Path)
    arguments = parser.parse_args()
    recorder = (arguments.probe, arguments.unaligned, arguments.plain, arguments.config)
    if arguments.application is not None:
        if arguments.shaders is None or any(value is not None for value in recorder):
            parser.error("--application takes --shaders, and none of the recorder's arguments")
        return check_shader_record(arguments.application, arguments.shaders)
    if arguments.shaders is not None or any(value is None for value in recorder):
        parser.error("give --probe, --unaligned, --plain and --config, or --application and --shaders")
    return check(arguments.probe, arguments.unaligned, arguments.plain, arguments.config)


if __name__ == "__main__":
    sys.exit(main())

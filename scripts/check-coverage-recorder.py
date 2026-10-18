#!/usr/bin/env python3
"""Check that the coverage recorder names exactly the functions a program entered, however it ends (M1-131).

    check-coverage-recorder.py --probe <program> --unaligned <program> --plain <program> --config <config>

Why this exists: the recorder, tests/CoverageRecorder.cpp, writes the record
M1-132 will choose tests from: the functions a program entered (register
decisions 559 and 644). A function missing from a record is a function whose
change would rerun no test, so the record is held here to the functions a
small program, tests/coverage_recorder_probe.cpp, is known to enter -- the
names read from the linker's map through scripts/compiled-code.py (decision
582) -- in every way that program can end (decision 634):

  * **a normal end**, the record written after the static destructors, so that
    it names the function one of them entered (decision 626);
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
                   "readsThroughNull", "stepsBeforeBegin", "abortsWhenPositive", "convertsIntoNoBuffer"}


class Scenario(NamedTuple):
    name: str
    ended: str
    entered: frozenset
    debug_only: bool


STOPPED = frozenset({"runScenario", "enteredFirst", "stopAs"})
SCENARIOS = (
    Scenario("normally", "normally", frozenset({"runScenario", "enteredFirst", "enteredByTheStaticDestructor"}),
             False),
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--probe", type=pathlib.Path, required=True)
    parser.add_argument("--unaligned", type=pathlib.Path, required=True)
    parser.add_argument("--plain", type=pathlib.Path, required=True)
    parser.add_argument("--config", required=True)
    arguments = parser.parse_args()
    return check(arguments.probe, arguments.unaligned, arguments.plain, arguments.config)


if __name__ == "__main__":
    sys.exit(main())

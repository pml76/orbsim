#!/usr/bin/env python3
"""Check that -Wfloat-equal is live after core/Units.hpp (M1-117).

    check-float-equal-guard.py <build directory>
    check-float-equal-guard.py --self-test

Why this exists: mp-units v2.5.0 closes its `operator==` with a second
diagnostic push where a pop belongs, which left -Wfloat-equal -- and
-Wunknown-pragmas and -Wunknown-warning-option with it -- switched off for the
rest of every file that includes core/Units.hpp. Eight floating-point `==`
comparisons went unseen (register decisions 443 and 451-454).
core/Units.hpp now undoes the two stray pushes; this is the instrument that
shows it does.

tests/float_equal_guard.cpp compares two doubles with `==` after
core/Units.hpp. It is compiled twice, with the exact command the build tree's
compile database holds for it, so with the tree's own flags:

  * as written, it must fail, and the only error must be -Wfloat-equal, in
    that file. A different error, or none, fails this check: "it did not
    compile" is not by itself the claim;
  * with ORBSIM_FLOAT_EQUAL_GUARD_CONTROL defined, which replaces the
    comparison with `bitsOf`, it must compile clean -- so the failure above is
    the comparison's, and not something else in the file.

A script reading the compile database rather than a build of the target,
because `check` runs CTest from inside Ninja, and a second Ninja on the same
tree at the same time is not safe.

Proven able to fail before it was trusted: --self-test feeds the verdict each
of the outcomes it exists to refuse, and exits non-zero unless every one is
refused, for its own reason, and the good outcome accepted.
"""

import json
import os
import pathlib
import re
import shlex
import subprocess
import sys

GUARD = "tests/float_equal_guard.cpp"
CONTROL_DEFINE = "-DORBSIM_FLOAT_EQUAL_GUARD_CONTROL"


def error_lines(output: str) -> list:
    """The compiler's error lines: clang's and gcc's both carry `error:`."""
    return [line for line in output.splitlines() if "error:" in line]


def verdict(guard_rc: int, guard_out: str, control_rc: int, control_out: str) -> list:
    """Every reason the two compiles do not show the warning live; empty if they do."""
    problems = []
    errors = error_lines(guard_out)
    if guard_rc == 0:
        problems.append("the guard compiled: `==` on two doubles after core/Units.hpp "
                        "was accepted, so -Wfloat-equal is not live there")
    elif not errors:
        problems.append("the guard failed without an error line, so not for the comparison:\n"
                        + guard_out)
    for line in errors:
        if "float_equal_guard.cpp" not in line.replace("\\", "/") or "float-equal" not in line:
            problems.append("the guard failed for a reason other than -Wfloat-equal "
                            "in the guard itself: " + line)
    if control_rc != 0 or error_lines(control_out):
        problems.append("the control did not compile clean, so the guard's failure "
                        "cannot be pinned on its comparison:\n" + control_out)
    return problems


def guard_command(build_dir: pathlib.Path):
    """The compile database's command for the guard, made syntax-only, and its directory."""
    entries = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    matches = [e for e in entries if e["file"].replace("\\", "/").endswith(GUARD)]
    if len(matches) != 1:
        sys.exit(f"check-float-equal-guard: expected one compile command for {GUARD} in "
                 f"{build_dir / 'compile_commands.json'}, found {len(matches)}")
    entry = matches[0]
    if "arguments" in entry:
        args = list(entry["arguments"])
    elif os.name == "nt":
        # CMake writes a Windows command line, which Python's shlex cannot
        # split: its backslashes are path separators, not escapes.
        args = None
    else:
        args = shlex.split(entry["command"])
    # `-o` and `-c` both go: with -fsyntax-only there is no object to name,
    # and clang reports a `-c` it did not use, which -Werror makes an error.
    if args is not None:
        if args.count("-o") != 1 or args.count("-c") != 1:
            sys.exit(f"check-float-equal-guard: expected one -o and one -c in the command for {GUARD}")
        at = args.index("-o")
        args = args[:at] + args[at + 2:]
        args.remove("-c")
        return args + ["-fsyntax-only"], entry["directory"]
    command, outputs = re.subn(r" -o \S+", "", entry["command"])
    command, compiles = re.subn(r" -c(?= )", "", command)
    if outputs != 1 or compiles != 1:
        sys.exit(f"check-float-equal-guard: expected one -o and one -c in the command for {GUARD}")
    return command + " -fsyntax-only", entry["directory"]


def compile_once(command, directory: str):
    done = subprocess.run(command, cwd=directory, capture_output=True, text=True, check=False)
    return done.returncode, done.stdout + done.stderr


def with_define(command, define: str):
    return command + [define] if isinstance(command, list) else f"{command} {define}"


def run(build_dir: pathlib.Path) -> int:
    command, directory = guard_command(build_dir)
    guard_rc, guard_out = compile_once(command, directory)
    control_rc, control_out = compile_once(with_define(command, CONTROL_DEFINE), directory)
    problems = verdict(guard_rc, guard_out, control_rc, control_out)
    for problem in problems:
        print(f"check-float-equal-guard: {problem}")
    if problems:
        return 1
    print("check-float-equal-guard: -Wfloat-equal is live after core/Units.hpp, "
          "and the control compiles clean")
    return 0


def self_test() -> int:
    good_error = (r"C:\x\tests\float_equal_guard.cpp:40:14: error: comparing floating point "
                  "with == or != is unsafe [-Werror,-Wfloat-equal]")
    gcc_error = ("/x/tests/float_equal_guard.cpp:40:14: error: comparing floating-point with "
                 "'==' or '!=' is unsafe [-Werror=float-equal]\n"
                 "cc1plus: all warnings being treated as errors")
    # Each case names the reason it must be refused for, not only that it is:
    # a refusal for the wrong reason is a wrong diagnosis, and M1-117's pass
    # found one that the verdict alone could not show (register decision 464).
    # None means the outcome must be accepted.
    compiled = "the guard compiled"
    other = "a reason other than -Wfloat-equal"
    control = "the control did not compile clean"
    cases = [
        ("clang reports the comparison, the control is clean", (1, good_error, 0, ""), None),
        ("gcc reports the comparison, the control is clean", (1, gcc_error, 0, ""), None),
        ("the guard compiles", (0, "", 0, ""), compiled),
        ("the guard compiles with a warning only", (0, good_error.replace("error:", "warning:"),
                                                    0, ""), compiled),
        ("the guard fails for another reason", (1, "x/tests/float_equal_guard.cpp:3:10: error: "
                                                   "'core/Units.hpp' file not found", 0, ""),
         other),
        ("the comparison is reported in another file", (1, good_error.replace(
            "float_equal_guard.cpp", "Units.hpp"), 0, ""), other),
        ("a second error beside the right one", (1, good_error + "\nx/tests/float_equal_guard.cpp"
                                                 ":9:1: error: unknown type name", 0, ""), other),
        ("the guard fails with no error line", (1, "clang: crashed", 0, ""), "without an error line"),
        ("the control fails", (1, good_error, 1, good_error), control),
        ("the control prints an error", (1, good_error, 0, good_error), control),
    ]
    failures = 0
    for name, outcome, reason in cases:
        problems = verdict(*outcome)
        if reason is None and problems:
            failures += 1
            print(f"self-test FAILED: {name}: refused it: {problems}")
        elif reason is not None and not any(reason in problem for problem in problems):
            failures += 1
            print(f"self-test FAILED: {name}: expected a refusal saying '{reason}', got {problems}")
    print(f"check-float-equal-guard self-test: {len(cases) - failures} of {len(cases)} cases")
    return 1 if failures else 0


def main() -> int:
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    return run(pathlib.Path(sys.argv[1]))


if __name__ == "__main__":
    sys.exit(main())

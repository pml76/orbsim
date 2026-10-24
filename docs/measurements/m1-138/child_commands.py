"""For question 7 of M1-138: the command forms of the child programs of the
script tests, from procmon-by-test.json of the complete Debug recording. A
form is the program and its first argument that is not an option or a path,
for example `git ls-files` or `ninja -t`. For each form: the tests that use
it, and the parts of the repository that its processes read or asked about.

    python child_commands.py <procmon-by-test.json> <the tests of a document change, one per line>
"""
import collections
import json
import pathlib
import shlex
import sys

ROOT = "c:\\users\\u439644\\projects\\untitled\\"
report = json.load(open(sys.argv[1], encoding="utf-8"))
doc_tests = set(pathlib.Path(sys.argv[2]).read_text(encoding="utf-8").split()) if len(sys.argv) > 2 else set()


def form(command):
    try:
        parts = shlex.split(command.replace("\\", "/"), posix=False)
    except ValueError:
        parts = command.split()
    if not parts:
        return "?"
    program = parts[0].strip('"').rsplit("/", 1)[-1].lower()
    for arg in parts[1:]:
        arg = arg.strip('"')
        if arg.startswith("-") and arg not in ("-t", "-E", "-S", "-P", "-c", "-C"):
            continue
        if ":" in arg or "/" in arg:
            continue
        return f"{program} {arg}"
    return program


forms = collections.defaultdict(lambda: {"tests": set(), "parts": collections.Counter()})
for test, programs in report.items():
    for program, data in programs.items():
        if program.startswith("python"):
            continue
        for command in data["commands"]:
            forms[form(command)]["tests"].add(test)
print(f"{'form':40s} tests  tests of a document change")
for name, entry in sorted(forms.items(), key=lambda kv: -len(kv[1]["tests"])):
    tests = entry["tests"]
    print(f"{name:40s} {len(tests):5d}  {sorted(tests & doc_tests)}")

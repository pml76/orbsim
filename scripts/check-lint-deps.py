#!/usr/bin/env python3
"""Check that each file is re-linted exactly when its text changes (M1-89).

    check-lint-deps.py <build directory> <repository root>
    check-lint-deps.py --self-test

Why this exists: a lint step used to depend on every header in the project, so
any header edit re-linted all 48 files, 206-313 s per tree (measured
2026-09-27, docs/measurements/verification-cost.md). Since M1-89 each lint step
depends on its own file's compiled object instead (ADR 0024, register
decisions 209 and 217). Ninja rebuilds that object exactly when the file or
anything it includes changes -- it records every header the compiler reports,
system headers too -- so the lint step re-runs exactly when the text
clang-tidy reads can have changed.

That is only true while four things hold, and each is checked here against
the Ninja file CMake generated and the compile database:

  * every lint step depends on its source and on **every** object the
    compile database lists for that source -- a file compiled into several
    programs has several, and each can see different flags;
  * it depends on the root `.clang-tidy`, a `--verify-config` stamp and the
    clang-tidy executable, so a change of configuration or tool re-lints all;
  * it depends on **no project header directly** -- a header listed there is
    the old scheme creeping back, which is correct but re-lints everything;
  * nothing under src/ or tests/ mentions `__clang_analyzer__`, the one way
    clang-tidy could read text the compiler does not, and so the one way an
    object could stay unchanged while the lint result changes.

Proven able to fail before it was trusted: --self-test feeds it each of those
four faults and exits non-zero unless every one is reported.
"""

import json
import os
import pathlib
import re
import sys


def norm(path: str) -> str:
    return path.replace("\\", "/").lower() if os.name == "nt" else path.replace("\\", "/")


def unescape(token: str) -> str:
    return token.replace("$:", ":").replace("$ ", " ").replace("$$", "$")


def lint_edges(ninja_text: str) -> dict:
    """{stamp: [inputs]} for every per-file lint step (not the verify steps)."""
    text = ninja_text.replace("$\n", "")
    found = {}
    for line in text.splitlines():
        if not line.startswith("build "):
            continue
        head, sep, rest = line[len("build "):].partition(": ")
        if not sep:
            continue
        outputs = [unescape(t) for t in re.split(r"(?<!\$) ", head) if t and t != "|"]
        stamp = next((o for o in outputs if re.search(r"(^|/)lint/[^/]+\.ok$", o.replace("\\", "/"))), None)
        if stamp is None or "/verify_" in "/" + stamp.replace("\\", "/"):
            continue
        tokens = [t for t in re.split(r"(?<!\$) ", rest) if t]
        inputs = []
        for token in tokens[1:]:           # tokens[0] is the rule
            if token == "||":
                break                      # order-only inputs do not re-run a step
            if token != "|":
                inputs.append(unescape(token))
        found[stamp] = inputs
    return found


def problems(edges: dict, compile_db: list, root: str, build: str, tidy_exe: str,
             analyzer_hits: list) -> list:
    found = []
    root_n = norm(root).rstrip("/") + "/"
    outputs_by_source = {}
    for entry in compile_db:
        src = norm(entry["file"])
        out = entry.get("output", "")
        if not os.path.isabs(out):
            out = os.path.join(entry["directory"], out)
        outputs_by_source.setdefault(src, set()).add(norm(out))
    if not edges:
        found.append("no lint steps found -- the check would be checking nothing")
    for stamp, inputs in sorted(edges.items()):
        # Ninja writes a path inside the build tree relative to it.
        ins = {norm(i if os.path.isabs(i) else os.path.join(build, i)) for i in inputs}
        sources = [i for i in ins if i.startswith(root_n) and i.endswith((".cpp", ".cc", ".c"))]
        if len(sources) != 1:
            found.append(f"{stamp}: expected one source among its inputs, found {len(sources)}")
            continue
        source = sources[0]
        objects = outputs_by_source.get(source, set())
        if not objects:
            found.append(f"{stamp}: the compile database has no entry for {source}")
        for obj in sorted(objects - ins):
            found.append(f"{stamp}: does not depend on the object {obj}")
        if norm(root_n + ".clang-tidy") not in ins:
            found.append(f"{stamp}: does not depend on .clang-tidy")
        if not any(re.search(r"(^|/)lint/verify_[^/]*\.ok$", i) for i in ins):
            found.append(f"{stamp}: does not depend on a --verify-config stamp")
        if norm(tidy_exe) not in ins:
            found.append(f"{stamp}: does not depend on the clang-tidy executable")
        headers = sorted(i for i in ins if i.startswith(root_n) and i.endswith((".hpp", ".h"))
                         and "/build/" not in i[len(root_n) - 1:])
        if headers:
            found.append(f"{stamp}: depends on {len(headers)} project headers directly, e.g. {headers[0]}")
    for hit in analyzer_hits:
        found.append(f"__clang_analyzer__ appears in {hit}: clang-tidy would read text the compiler does not")
    return found


def analyzer_mentions(root: pathlib.Path) -> list:
    hits = []
    for top in ("src", "tests"):
        for path in (root / top).rglob("*"):
            if path.suffix in (".cpp", ".hpp", ".h", ".cc") and "__clang_analyzer__" in path.read_text(
                    encoding="utf-8", errors="replace"):
                hits.append(str(path.relative_to(root)))
    return hits


def tidy_executable(build: pathlib.Path) -> str:
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("ORBSIM_CLANG_TIDY:"):
            return line.split("=", 1)[1]
    return ""


def check(build: pathlib.Path, root: pathlib.Path) -> int:
    edges = lint_edges((build / "build.ninja").read_text(encoding="utf-8", errors="replace"))
    compile_db = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    found = problems(edges, compile_db, str(root), str(build), tidy_executable(build),
                     analyzer_mentions(root))
    for line in found:
        print(f"check-lint-deps: {line}")
    if found:
        print(f"check-lint-deps: {len(found)} problem(s) in {len(edges)} lint steps")
        return 1
    print(f"check-lint-deps: all {len(edges)} lint steps depend on their objects and not on headers")
    return 0


def self_test() -> int:
    root = "C:/r" if os.name == "nt" else "/r"
    tidy = root + "/tools/clang-tidy.exe"

    def ninja(extra_inputs: str, with_object: bool = True, with_tidy: bool = True,
              with_config: bool = True, with_verify: bool = True) -> str:
        parts = [f"{root}/src/a.cpp".replace(":", "$:")]
        if with_object:
            parts.append(f"{root}/build/CMakeFiles/t.dir/src/a.cpp.obj".replace(":", "$:"))
        if with_config:
            parts.append(f"{root}/.clang-tidy".replace(":", "$:"))
        if with_verify:
            parts.append("lint/verify_src.ok")  # relative, as Ninja writes it
        if with_tidy:
            parts.append(tidy.replace(":", "$:"))
        parts += extra_inputs.split()
        return (f"build lint/src_a.cpp.ok: CUSTOM_COMMAND {' '.join(parts)} || cmake_object_order_depends\n"
                "  COMMAND = tidy\n  pool = orbsim_lint\n")

    db = [{"file": f"{root}/src/a.cpp", "directory": f"{root}/build",
           "output": f"{root}/build/CMakeFiles/t.dir/src/a.cpp.obj"}]
    db_two = db + [{"file": f"{root}/src/a.cpp", "directory": f"{root}/build",
                    "output": f"{root}/build/CMakeFiles/u.dir/src/a.cpp.obj"}]
    header = f"{root}/src/a.hpp".replace(":", "$:")
    cases = {
        "a correct step": (ninja(""), db, [], False),
        "a missing object": (ninja("", with_object=False), db, [], True),
        "the object written relative to the build tree": (
            ninja("CMakeFiles/t.dir/src/a.cpp.obj", with_object=False), db, [], False),
        "a second object missing": (ninja(""), db_two, [], True),
        "a project header listed directly": (ninja(header), db, [], True),
        "no clang-tidy executable": (ninja("", with_tidy=False), db, [], True),
        "no .clang-tidy": (ninja("", with_config=False), db, [], True),
        "no --verify-config stamp": (ninja("", with_verify=False), db, [], True),
        "__clang_analyzer__ in a source": (ninja(""), db, ["src/x.hpp"], True),
        "no lint steps": ("build a.obj: CXX a.cpp\n", db, [], True),
        "a source the compile database does not know": (ninja(""), [], [], True),
    }
    failures = 0
    for name, (text, compile_db, hits, want) in cases.items():
        got = bool(problems(lint_edges(text), compile_db, root, root + "/build", tidy, hits))
        status = "ok" if got == want else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name}: {'reported' if got else 'accepted'} ({status})")
    return 1 if failures else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return self_test()
    if len(sys.argv) == 3:
        return check(pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve())
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())

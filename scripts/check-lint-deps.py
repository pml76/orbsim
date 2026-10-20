#!/usr/bin/env python3
"""Check that every compiled file is linted (M1-135), and re-linted exactly
when its text changes (M1-89).

    check-lint-deps.py <build directory> <repository root> [--exception '<path>: <reason>']...
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

And a fifth, since M1-135 (register decisions 660-667): **every file the
compile database lists in the repository, outside the build folders, is
linted or named as an exception**. The lint list is written by hand, and three
times a compiled file was left off it and never linted, the last
tests/ReadRecordListener.cpp, found by M1-131 (decision 650). An exception
comes from ORBSIM_LINT_EXCEPTIONS in CMakeLists.txt, beside the target that
compiles the file and under the same condition, as `<path>: <reason>`; each
must give its reason and name, once, a file this tree compiles and does not
lint, so that none outlives its file. Not held to it, and why:

  * the build tree's own files: the header self-checks CMake generates, each
    one `#include` line of a header that the files including it lint
    (decision 663), and the code fetched there, which is someone else's;
  * the repository's build/ folder, where another tree's files and the
    dependency cache sit;
  * every folder the tree's CMakeCache.txt names as holding fetched code, each
    FETCHCONTENT_SOURCE_DIR_<NAME> set and FETCHCONTENT_BASE_DIR (decision
    672): a worktree's tree pointed at the main checkout's dependency cache
    reads it from outside both.

A file compiled from anywhere else, outside the repository and the build tree,
is refused rather than passed over, and so is a compile database listing no
file of the repository.

Proven able to fail before it was trusted: --self-test feeds it each of those
faults and exits non-zero unless every one is reported, and accepts what must
pass; the fifth's cases were written first and seen accepted by a check that
did nothing.
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


def linted_sources(edges: dict, root: str, build: str) -> set:
    """The sources the lint steps lint: each step's input that is a source in
    the repository, read the way `problems` reads it."""
    root_n = norm(root).rstrip("/") + "/"
    found = set()
    for inputs in edges.values():
        for i in inputs:
            path = norm(i if os.path.isabs(i) else os.path.join(build, i))
            if path.startswith(root_n) and path.endswith((".cpp", ".cc", ".c")):
                found.add(path)
    return found


def unlinted(edges: dict, compile_db: list, root: str, build: str, exceptions: list, fetched: list) -> list:
    """The fifth check (M1-135, register decisions 662, 663, 665 and 672):
    every file the compile database lists in the repository, outside the build
    folders and the folders `fetched` names, is linted or named as an
    exception; and each exception has a reason and names, once, a file this
    tree compiles and does not lint."""
    found = []
    root_n = norm(root).rstrip("/") + "/"
    skipped = (norm(build).rstrip("/") + "/", root_n + "build/", *fetched)
    linted = linted_sources(edges, root, build)
    compiled = {}  # normalised path -> (the path as the repository spells it, targets)
    for entry in compile_db:
        path = entry["file"] if os.path.isabs(entry["file"]) else os.path.join(entry["directory"], entry["file"])
        key = norm(path)
        if key.startswith(skipped):
            continue
        if not key.startswith(root_n):
            found.append(f"{path} is compiled from outside the repository and the build tree, "
                         "so the check cannot tell whose code it is")
            continue
        target = re.search(r"CMakeFiles/([^/]+)\.dir/", entry.get("output", "").replace("\\", "/"))
        compiled.setdefault(key, (path.replace("\\", "/")[len(root_n):], set()))[1].add(
            target.group(1) if target else "an unnamed target")
    if not compiled:
        found.append("the compile database lists no file of the repository -- the check would be checking nothing")
    excepted = set()
    for exception in exceptions:
        path, _, reason = (part.strip() for part in exception.partition(":"))
        key = norm(os.path.join(root, path))
        if not reason:
            found.append(f"the exception {exception!r} gives no reason: write it as '<path>: <reason>'")
        if key in excepted:
            found.append(f"{path} is named as an exception twice")
        elif key in linted:
            found.append(f"{path} is named as an exception and linted: remove one of the two")
        elif key not in compiled:
            found.append(f"{path} is named as an exception, and this tree does not compile it: remove the "
                         "exception, or add it under the condition its target is built under")
        excepted.add(key)
    for key, (shown, targets) in sorted(compiled.items()):
        if key not in linted and key not in excepted:
            found.append(f"{shown} is compiled, by {', '.join(sorted(targets))}, and neither linted nor named as "
                         "an exception: add it to ORBSIM_LINT_SOURCES in CMakeLists.txt, or to "
                         "ORBSIM_LINT_EXCEPTIONS beside its target, with its reason")
    return found


def analyzer_mentions(root: pathlib.Path) -> list:
    hits = []
    for top in ("src", "tests"):
        for path in (root / top).rglob("*"):
            if path.suffix in (".cpp", ".hpp", ".h", ".cc") and "__clang_analyzer__" in path.read_text(
                    encoding="utf-8", errors="replace"):
                hits.append(str(path.relative_to(root)))
    return hits


def fetched_folders(cache_text: str) -> list:
    """The folders a tree's CMakeCache.txt names as holding fetched code
    (register decision 672): each FETCHCONTENT_SOURCE_DIR_<NAME> set, and
    FETCHCONTENT_BASE_DIR. An entry left empty names no folder."""
    found = []
    for line in cache_text.splitlines():
        entry = re.match(r"FETCHCONTENT_(?:SOURCE_DIR_[A-Z0-9_]+|BASE_DIR):PATH=(.+)$", line)
        if entry and entry.group(1).strip():
            found.append(norm(entry.group(1).strip()).rstrip("/") + "/")
    return found


def tidy_executable(build: pathlib.Path) -> str:
    for line in (build / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("ORBSIM_CLANG_TIDY:"):
            return line.split("=", 1)[1]
    return ""


def check(build: pathlib.Path, root: pathlib.Path, exceptions: list) -> int:
    edges = lint_edges((build / "build.ninja").read_text(encoding="utf-8", errors="replace"))
    compile_db = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    found = problems(edges, compile_db, str(root), str(build), tidy_executable(build),
                     analyzer_mentions(root))
    found += unlinted(edges, compile_db, str(root), str(build), exceptions,
                      fetched_folders((build / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")))
    for line in found:
        print(f"check-lint-deps: {line}")
    if found:
        print(f"check-lint-deps: {len(found)} problem(s) in {len(edges)} lint steps")
        return 1
    print(f"check-lint-deps: all {len(edges)} lint steps depend on their objects and not on headers, and every "
          f"file of the repository compiled here is linted or one of the {len(exceptions)} exceptions")
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
                "  COMMAND = tidy\n  pool = orbsim_memory\n")

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
    failures += self_test_completeness(root, lint_edges(ninja("")))
    return 1 if failures else 0


def self_test_completeness(root: str, edges: dict) -> int:
    """The fifth check (M1-135): one lint step, for src/a.cpp, and compile
    databases around it. The tree sits in the repository's build/ folder, as
    the presets put it, beside another tree's and the dependency cache's."""
    build = root + "/build/t"
    elsewhere = "D:/elsewhere" if os.name == "nt" else "/elsewhere"

    def db(*paths: str) -> list:
        return [{"file": path, "directory": build, "output": f"CMakeFiles/t.dir/{index}.obj"}
                for index, path in enumerate(paths)]

    own = (f"{root}/src/a.cpp", f"{root}/tests/b.cpp")
    cases = {
        "every compiled file linted": (db(own[0]), [], False),
        "a file compiled and not linted": (db(*own), [], True),
        "a file compiled from a folder of its own and not linted": (
            db(own[0], f"{root}/tools/c.cpp"), [], True),
        "an unlinted file named as an exception, with its reason": (
            db(*own), ["tests/b.cpp: a reason"], False),
        "an exception with no reason": (db(*own), ["tests/b.cpp:"], True),
        "an exception with no reason or colon": (db(*own), ["tests/b.cpp"], True),
        "an exception naming a file this tree does not compile": (
            db(own[0]), ["tests/b.cpp: a reason"], True),
        "an exception naming a file that is linted": (db(own[0]), ["src/a.cpp: a reason"], True),
        "the same file named twice": (db(*own), ["tests/b.cpp: one", "tests/b.cpp: two"], True),
        "a file generated into the build tree": (db(own[0], f"{build}/selfcheck/x.cpp"), [], False),
        "code fetched into the build tree": (db(own[0], f"{build}/_deps/sdl3-src/x.c"), [], False),
        "the dependency cache in the build folder": (
            db(own[0], f"{root}/build/_deps-cache/sdl3/x.c"), [], False),
        "a file from outside the repository and the build tree": (
            db(own[0], f"{elsewhere}/x.cpp"), [], True),
        "an empty compile database": ([], [], True),
    }
    # Code fetched into a folder outside the repository and the build tree, as
    # a worktree's tree pointed at the main checkout's dependency cache has it
    # (decision 672): passed over only where CMakeCache.txt names the folder.
    cached = db(own[0], f"{elsewhere}/cache/sdl3/x.c")
    base = f"FETCHCONTENT_BASE_DIR:PATH={build}/_deps\n"
    fetched_cases = {
        "code fetched into a folder CMakeCache.txt names": (
            cached, [], False, base + f"FETCHCONTENT_SOURCE_DIR_SDL3:PATH={elsewhere}/cache/sdl3\n"),
        "the same code, the folder's entry empty": (cached, [], True, base + "FETCHCONTENT_SOURCE_DIR_SDL3:PATH=\n"),
    }
    cases = {name: (*case, "") for name, case in cases.items()} | fetched_cases
    failures = 0
    for name, (compile_db, exceptions, want, cache_text) in cases.items():
        got = bool(unlinted(edges, compile_db, root, build, exceptions, fetched_folders(cache_text)))
        status = "ok" if got == want else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name}: {'reported' if got else 'accepted'} ({status})")
    return failures


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return self_test()
    flags = sys.argv[3::2]
    if len(sys.argv) >= 3 and len(sys.argv) % 2 == 1 and all(flag == "--exception" for flag in flags):
        return check(pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve(), sys.argv[4::2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())

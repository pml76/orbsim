#!/usr/bin/env python3
"""List the mutant files a change makes due to run again (M1-92).

    mutants-due.py <build tree> [--assume-changed <path>] ... [--only-assumed]
                   [--expect-due <file>] ... [--expect-current <file>] ...
    mutants-due.py <build tree> --fingerprints <file>
    mutants-due.py <build tree> --gpu-identity
    mutants-due.py --self-test

Why this exists: whether a test catches a mutant depends on three things --
the mutated file, the test, and all the code between them, test helpers and
the production code the test runs through. A change to any of them can turn a
caught mutant into a survivor, and nothing said which older mutant files a
task should run again: that was judgement, and judgement can forget. The rule
since M1-92 (ADR 0024, register decision 212, the *strict* one): **a mutant
file is due when anything its judges depend on has changed since it last
passed.** Every file this lists runs at each phase gate, and a task runs its
own file after its commit and before its push (register decision 276, since
2026-10-03).

"Last passed" is the commit `scripts/mutate.py` recorded in
`scripts/mutation-passes.json` after a clean run. "Anything its judges depend
on" is, deliberately, more than it strictly needs:

  * every file its mutants mutate;
  * the judges: a file's `suites` and `targets`, its `ctest` entries, and
    since M1-109 every test CTest runs before one of those -- the setup and
    cleanup tests of the fixtures it requires, and the tests it is ordered
    after -- because `ctest -R` runs a required setup test too, and a test
    that reads the images a probe test wrote depends on that probe;
  * everything each judging program is built from: Ninja's graph followed
    through its explicit and implicit edges, and every header its objects
    include, from Ninja's dependency log. **Not through build-order-only
    edges** (M1-109, register decision 393): those say "build that first",
    not "this reads it", and they made every shader an input of the
    application. A generated header an object does include is followed back
    through its own inputs, so a file compiled into a program still counts;
  * at run time: the compiled shaders a judge loads, by their bytes (M1-131,
    register decisions 629 and 635) -- those `scripts/run-tests.py` stored
    from the records of the judge's last passing run, while the application
    is what it was then; otherwise, for a test that starts the application,
    those its probe's last sidecar names, where that sidecar can be trusted,
    and every shader otherwise (register decisions 394 and 616). Each such
    file's hash is among the judge's fingerprint parts, and so is that of a
    shader the file's own mutants change, so an edit that compiles to the
    same bytes makes nothing due; `--assume-changed`, given a shader's
    source, supposes its compiled file changed (decision 643). `data/` for
    every judge; every file named on a ctest entry's command line;
  * the harness itself;
  * the build definition (`CMakeLists.txt`, `cmake/`, `CMakePresets.json`)
    through what it changed (M1-103, register decision 250): a clean pass
    records a fingerprint of each judge, and a judge whose fingerprint moved
    makes its file due. Where no fingerprint can be compared -- none recorded,
    or recorded in another tree -- the build definition makes every file due,
    as before. Since M1-109 a fingerprint is recorded in parts, so a moved one
    says what moved (decision 396);
  * and the graphics card (M1-109, register decision 397): a file judged on
    the GPU records the card, its driver and the validation layer it ran
    with, and is due on any other, or where none was recorded.

Each file is judged in the tree its pass was recorded in, whichever tree is
named (M1-106): only there can its fingerprints be compared.

Since M1-129 (register decisions 520-524) a file's own mutants are an input
of it too, and a judge the tree does not have makes it due. And Ninja's
answers about the graph are stored in the tree, `mutants-due-graph.json`,
and used while what they rest on is unchanged: a reading took 26 s asking
Ninja and 3 s from them (2026-10-14).

It over-approximates on purpose: listing a file that did not need to run
costs minutes, and missing one costs a hole nobody sees. Each narrowing M1-109
made has a test that a file which must stay due still does.

Proven able to fail before it was trusted: --self-test gives the decision
function changed files inside and outside a file's inputs, and no record at
all, and exits non-zero unless each is judged right.
"""

import contextlib
import hashlib
import importlib.util
import io
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
PASSES = ROOT / "scripts" / "mutation-passes.json"
ALWAYS = ("scripts/mutate.py", "data/")
# The build definition makes every file due only where no fingerprints can say
# what a change to it did (M1-103, register decision 250).
BUILD_DEFINITION = ("CMakeLists.txt", "CMakePresets.json", "cmake/")
# The application, and the build step that compiles every shader it can read.
APPLICATION = "orbsim"
SHADER_STEP = "orbsim_shaders"
# Characters of node names per `ninja -t query`: Windows refuses a command line
# past 32,767, and a node's name here is up to about 200.
QUERY_CHARS = 20_000
# A content digest is remembered by a file's size and date, in this file in the
# build tree. The name changed with M1-109, when a file inside the tree began
# to be hashed with the tree's own path replaced (decision 395), so that no
# digest of the old kind is ever read as one of the new.
HASHES = "mutants-due-hashes-2.json"
# How long the clear probe that names the graphics card may take (M1-109): it
# runs in about 4.4 s; an order of magnitude clear of that.
GPU_PROBE_SECONDS = 120
# How a judge the tree does not have stands among a file's inputs.
UNKNOWN_JUDGE = "<unknown ctest entry "
# Ninja's answers about the build graph, stored in the build tree (M1-129,
# register decision 520): the header record, every node asked about, every
# walk and the commands it reached. Asking costs about 25 s a reading -- some
# 120 calls of `ninja -t query`, each loading the 2.4 MB build.ninja again --
# and the seven tests that read a tree asked the same questions one after
# another; read from here, a reading took 1.1 s (measured 2026-10-14). The
# answers are used only while nothing they came from has changed: GRAPH_FILES,
# the ninja program, the tree's path, and this script's own text, so that a
# mutant of the script is never judged by answers its unmutated self wrote.
GRAPH = "mutants-due-graph.json"
# The probe sidecars trusted by their contents (M1-129, register decision
# 616): for each, its SHA-256 and the application's, taken when the time rule
# of decision 394 last trusted it. An application relinked to the same bytes
# -- every build is reproducible since decision 488 -- has a new time but
# wrote nothing new for a sidecar to be read against, so the sidecar stays
# trusted while both files are what they were.
TRUSTED = "mutants-due-sidecars.json"
GRAPH_FILES = ("build.ninja", "CMakeFiles/rules.ninja", ".ninja_deps")
# The test driver, whose store in the build tree holds the shader files each
# test's last passing run asked for, with the application's SHA-256 at that
# run (M1-131, decision 629); read through the driver's own code, so that a
# store of another version is not read here either.
RUN_TESTS = ROOT / "scripts" / "run-tests.py"
# Raised whenever what is stored, or how, changes.
GRAPH_FORMAT = 1


def norm(path) -> str:
    text = os.path.abspath(str(path)).replace("\\", "/")
    return text.lower() if os.name == "nt" else text


def is_due(changed: set, inputs: set, recorded: bool, judged=None, other=()) -> tuple:
    """(due, reasons): due when there is no record, or a changed file is an input.

    `judged` is the list of judges whose fingerprint changed since the pass, or
    None where no fingerprints can be compared (M1-103); `other` is every
    further reason, the graphics card's among them (M1-109)."""
    if not recorded:
        return True, ["no recorded pass"]
    hits = sorted(changed & inputs)
    # A judge this tree does not have can be judged by nothing, so it makes
    # its file due rather than standing for an input that never changes
    # (M1-129, M1-121 finding 4.5).
    hits += sorted(i for i in inputs if i.startswith(UNKNOWN_JUDGE))
    root = norm(ROOT) + "/"
    # The build definition counts by itself only where no fingerprint can say
    # what a change to it did (M1-103).
    watched = ALWAYS + BUILD_DEFINITION if judged is None else ALWAYS
    always = [c for c in changed if any(c.startswith(root + a.lower() if os.name == "nt" else root + a)
                                        for a in watched)]
    reasons = hits + sorted(set(always) - set(hits)) + sorted(judged or []) + list(other)
    return bool(reasons), reasons


def own_mutants_reason(spec: dict, own: str, changed: set, earlier):
    """Why a mutant file is due on its own account, or None (M1-129, M1-121
    finding 4.8, register decision 524): its `mutants` differ from those at the
    commit its pass recorded -- a mutant's text, its verdict or its `why` -- but
    not its note, task or tree. `own` is the file's normalised path, and
    `earlier()` its text at that commit, or None where there is none to
    compare with: a file new since, or a change only assumed."""
    if own not in changed:
        return None
    text = earlier()
    try:
        then = json.loads(text).get("mutants") if text is not None else None
    except (ValueError, AttributeError):
        then = None
    return None if then == spec["mutants"] else "its mutants changed since its pass"


def file_at(commit: str, path: pathlib.Path):
    """A file's text at a commit, or None where it did not exist there."""
    try:
        return run(["git", "show", f"{commit}:{path.relative_to(ROOT).as_posix()}"], ROOT)
    except subprocess.CalledProcessError:
        return None


def placeholders(text: str, tree: pathlib.Path, root: pathlib.Path) -> str:
    """`text` with the build tree's path and the repository's replaced by <TREE> and <ROOT>.

    Either separator between the parts of a path, since Windows tools mix them
    in one path, and either case on Windows. The tree first: it lies inside
    the repository."""
    flags = re.IGNORECASE if os.name == "nt" else 0
    for path, mark in ((tree, "<TREE>"), (root, "<ROOT>")):
        parts = [p for p in re.split(r"[/\\]", os.path.abspath(str(path))) if p]
        text = re.sub(r"[/\\]*".join(re.escape(p) for p in parts) if os.name == "nt"
                      else "/" + "/".join(re.escape(p) for p in parts), mark, text, flags=flags)
    return text


def fingerprint(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]


def combine(parts: dict) -> str:
    """One judge's fingerprint, from its parts (M1-109, register decision 396)."""
    return fingerprint("\n".join(f"{name} {value}" for name, value in sorted(parts.items())))


def program_parts(commands: str, files: dict, tree: pathlib.Path, root: pathlib.Path) -> dict:
    """A judging program's fingerprint in three parts: the commands Ninja runs to
    build it, and the contents of the files it is built from that git does not
    track and no build step makes -- those inside the build tree (fetched and
    generated at configure time) apart from the rest (the toolchain's)."""
    inside = norm(tree) + "/"

    def listing(chosen) -> str:
        return "\n".join(f"{placeholders(path, tree, root)} {digest}" for path, digest in sorted(chosen))

    return {
        "commands": fingerprint(placeholders(commands, tree, root)),
        "build-tree files": fingerprint(listing((p, d) for p, d in files.items() if p.startswith(inside))),
        "other files": fingerprint(listing((p, d) for p, d in files.items() if not p.startswith(inside))),
    }


def program_fingerprint(commands: str, files: dict, tree: pathlib.Path, root: pathlib.Path) -> str:
    return combine(program_parts(commands, files, tree, root))


def test_fingerprint(test: dict, tree: pathlib.Path, root: pathlib.Path) -> str:
    """A judging CTest entry: its command and properties -- not the line of
    CMakeLists.txt it was declared on, which moves with every edit above it."""
    kept = {key: value for key, value in test.items() if key != "backtrace"}
    return fingerprint(placeholders(json.dumps(kept, sort_keys=True), tree, root))


def content_digest(data: bytes, inside_tree: bool, tree: pathlib.Path, root: pathlib.Path) -> str:
    """A file's SHA-256 -- for a file inside the build tree, with the tree's and
    the repository's paths replaced first (M1-109, register decision 395). A
    file generated at configure time can hold the tree's own path: SDL's
    precompiled header names `<tree>/_deps/sdl3-src/src/SDL_internal.h`, and
    hashed as written it made `orbsim.exe`'s fingerprint differ between two
    trees of one commit. The bytes go through text and back unchanged
    (surrogateescape), so a file without the path keeps the digest it had."""
    if inside_tree:
        text = data.decode("utf-8", errors="surrogateescape")
        data = placeholders(text, tree, root).encode("utf-8", errors="surrogateescape")
    return hashlib.sha256(data).hexdigest()


def tool_command(ninja: str, tool: str, args=()) -> list:
    """A Ninja tool's command line, always a dry run (M1-109, register decision
    399). `-t deps` and `-t query` load Ninja's two logs and, outside a dry run,
    first open them for writing -- and a log past Ninja's threshold is rewritten
    then (deps_log.cc, Load and OpenForWrite, v1.12.0): deleted and replaced.
    On Windows that fails while another Ninja holds the file open, as the one
    running `check` does: measured 2026-10-06 in a scratch project, "opening
    deps log: Permission denied", exit 1, and a stray .ninja_deps.recompact
    left behind; with -n the same read returned every record. It is how all
    seven tests that read a tree failed in one RelWithDebInfo `check` that day.
    A dry run never writes either log, so a reader can no longer disturb a
    build or another reader."""
    return [ninja, "-n", "-t", tool, *args]


def run(cmd, cwd) -> str:
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", check=True).stdout


def ninja_program(tree: pathlib.Path) -> str:
    for line in (tree / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CMAKE_MAKE_PROGRAM:"):
            return line.split("=", 1)[1]
    return "ninja"


def ctest_program(tree: pathlib.Path) -> str:
    for line in (tree / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CMAKE_COMMAND:"):
            cmake = pathlib.Path(line.split("=", 1)[1])
            return str(cmake.with_name("ctest" + cmake.suffix))
    return "ctest"


def parse_deps(listing: str, tree: pathlib.Path) -> dict:
    """object -> headers, from the text of `ninja -t deps`.

    A header Ninja names relative to the tree is resolved against the tree --
    a generated header is named that way -- not against the directory the
    script was started in, which it was before M1-109."""
    found, current = {}, None
    for line in listing.splitlines():
        if line and not line.startswith(" ") and ": #deps" in line:
            current = norm(tree / line.split(": #deps")[0])
            found[current] = set()
        elif line.startswith("    ") and current:
            found[current].add(norm(tree / line.strip()))
    return found


def deps_problem(found: dict, tree: pathlib.Path):
    """Why these header dependencies cannot be used, or None.

    Every tree this reads has compiled something, so an empty record is never
    the truth: it is a record lost or being rewritten (M1-102, reproduced on
    2026-09-30 with three `ninja -t deps` at once on Windows)."""
    if not found:
        return (f"mutants-due: Ninja reported no header dependencies for {tree} -- the "
                "check would be checking nothing. Is the tree built, and is nothing else "
                "reading or building it?")
    return None


def parse_query(text: str) -> dict:
    """node -> the inputs it reads, from the text of `ninja -t query`: its
    explicit inputs and its implicit ones (`| `), never its build-order-only
    ones (`|| `). A node no build step makes has an entry with no inputs."""
    found, current, section = {}, None, None
    for line in text.splitlines():
        if line and not line.startswith(" "):
            current = line[:-1] if line.endswith(":") else line
            found[current], section = [], None
        elif line.startswith("  ") and not line.startswith("   "):
            section = line.strip().split(":", 1)[0]
        elif current is not None and section == "input" and line.startswith("    "):
            entry = line.strip()
            if entry.startswith("|| "):
                continue
            found[current].append(entry[2:] if entry.startswith("| ") else entry)
    return found


def stamp(path) -> list:
    """A file's size and modification time in ns, or ["missing"]."""
    try:
        info = os.stat(path)
    except OSError:
        return ["missing"]
    return [info.st_size, info.st_mtime_ns]


def graph_key(tree: pathlib.Path, ninja: str, script: bytes) -> dict:
    """What Ninja's stored answers about `tree` rest on (decision 520): each of
    GRAPH_FILES by its size and time, as decision 250 judges a file's digest;
    the ninja program the same way; the tree's path, which every stored name
    contains; and a hash of the script that asked."""
    return {"format": GRAPH_FORMAT, "tree": norm(tree), "script": hashlib.sha256(script).hexdigest(),
            "files": {name: stamp(tree / name) for name in GRAPH_FILES},
            "ninja": [norm(ninja), stamp(ninja)]}


def stored_graph(text, key: dict):
    """The stored answers, if `text` holds them for exactly this key; None for
    no file, a file that does not parse, or one stored under another key."""
    if text is None:
        return None
    try:
        stored = json.loads(text)
    except ValueError:
        return None
    return stored if isinstance(stored, dict) and stored.get("key") == key else None


def walk_graph(target: str, edges, key, built: dict, headers: dict) -> tuple:
    """(files, built nodes): everything `target` is built from, and the nodes on
    the way that a build step makes, its own included (M1-109).

    `edges(names)` answers each name with the inputs it reads (parse_query's
    shape); `key(name)` is a node's normalised path; `built` maps the
    normalised path of every node a build step makes to Ninja's name for it;
    `headers` maps an object to the headers the compiler reported for it. A
    header that a build step makes is followed back through its own inputs:
    order-only edges are not followed, and a file that is really compiled into
    a program reaches it only that way."""
    files, seen, made = set(), set(), []
    frontier = [target]
    while frontier:
        answers = edges([n for n in frontier if key(n) in built])
        following = []
        for name in frontier:
            node = key(name)
            if node in seen:
                continue
            seen.add(node)
            if name != target:
                files.add(node)
            if node in built:
                made.append(name)
            following += answers.get(name, [])
            for header in headers.get(node, ()):
                files.add(header)
                if header in built and header not in seen:
                    following.append(built[header])
        frontier = following
    return files, made


def closure(names: list, tests: dict) -> list:
    """The ctest entries named, and every test CTest runs before one of them,
    transitively (M1-109): the setup and cleanup tests of each fixture an entry
    requires, and the tests it is ordered after. In the order first reached."""
    setups = {}
    for test in tests.values():
        props = {p["name"]: p["value"] for p in test.get("properties", [])}
        for fixture in list(props.get("FIXTURES_SETUP", [])) + list(props.get("FIXTURES_CLEANUP", [])):
            setups.setdefault(fixture, []).append(test["name"])
    found, frontier = [], list(names)
    while frontier:
        name = frontier.pop(0)
        if name in found:
            continue
        found.append(name)
        test = tests.get(name)
        if test is None:
            continue
        props = {p["name"]: p["value"] for p in test.get("properties", [])}
        for fixture in props.get("FIXTURES_REQUIRED", []):
            frontier += setups.get(fixture, [])
        frontier += list(props.get("DEPENDS", []))
    return found


def labels_of(test: dict) -> list:
    return [v for p in test.get("properties", []) if p["name"] == "LABELS" for v in p["value"]]


def arguments(test: dict) -> list:
    """A ctest entry's command line, with each `-DNAME=value` reduced to its value."""
    return [a.split("=", 1)[1] if a.startswith("-D") and "=" in a else a for a in test.get("command", [])]


def defines(test: dict) -> dict:
    """A ctest entry's `-DNAME=value` arguments, as a CMake script receives them."""
    found = {}
    for arg in test.get("command", []):
        if arg.startswith("-D") and "=" in arg:
            name, value = arg[2:].split("=", 1)
            found[name] = value
    return found


def sidecar_value(text: str, key: str):
    for line in text.splitlines():
        if "=" in line and line.split("=", 1)[0].strip() == key:
            return line.split("=", 1)[1].strip()
    return None


def sidecar_shaders(text: str, written_ns: int, built_ns: int):
    """The shader files a probe's sidecar names, or None where it cannot be
    used (M1-109, register decision 394): written before the application was
    last built -- what a mutation pass leaves, since its sidecars come from
    mutated programs and the restored application is rebuilt after them; a
    run that did not render, which may have stopped before reading them all;
    and a sidecar written before M1-109, which names none."""
    if written_ns < built_ns:
        return None
    if sidecar_value(text, "outcome") != "rendered":
        return None
    named = sidecar_value(text, "shaders")
    if not named or named == "none":
        return None
    return named.split()


def sidecar_trusted(written_ns: int, built_ns: int, contents: dict, memory: dict, key: str) -> bool:
    """Whether a probe's sidecar may be read against the application as it is
    now (register decisions 394 and 616). Written no earlier than the
    application was last built, it may -- and then `contents`, the sidecar's
    and the application's SHA-256, are remembered under `key`. Written before,
    it may only where exactly those contents were remembered: the application
    was relinked to the bytes it had when the sidecar was trusted."""
    if written_ns >= built_ns:
        memory[key] = dict(contents)
        return True
    return memory.get(key) == contents


def gpu_identity_from_sidecar(text: str):
    """The graphics card a probe ran on, as its sidecar records it: the card,
    the driver and the validation layer (M1-109, register decision 397); None
    where a line is missing."""
    keys = {"card": ("gpu.vendor", "gpu.device"), "driver": ("driver.info", "driver.version"),
            "validation layer": ("validation.layer",)}
    found = {}
    for name, lines in keys.items():
        values = [sidecar_value(text, line) for line in lines]
        if any(v is None for v in values):
            return None
        found[name] = " ".join(values)
    return found


def identity_problem(found: dict):
    """Why a card read with --validate cannot stand, or None (M1-109, register
    decision 398): a run that asked for validation and had no layer. The
    application carries on without the layer where it is not installed, so
    every GPU test would pass with no validation at all and nothing would say
    so; this build requires the Vulkan SDK, which ships the layer."""
    if found.get("validation layer", "").startswith("none"):
        return ("the clear probe asked for validation and ran without the validation layer: "
                "is the Vulkan SDK's layer installed and found by the loader?")
    return None


def gpu_reason(record: dict, current, why: str):
    """Why a GPU-judged file is due on the card's account, or None (M1-109):
    no card recorded, the card here unknown, or a different card, driver or
    validation layer."""
    was = record.get("gpu")
    if was is None:
        return "no graphics card recorded"
    if current is None:
        return f"the graphics card here is unknown: {why}"
    moved = [f"{name} {was.get(name)} -> {current.get(name)}"
             for name in sorted(set(was) | set(current)) if was.get(name) != current.get(name)]
    return "graphics card changed: " + "; ".join(moved) if moved else None


def gpu_identity(tree: pathlib.Path) -> tuple:
    """(identity, why): the card, driver and validation layer this tree's
    application runs on, from a clear probe run with validation into a scratch
    folder -- the device the judges themselves get, by the application's own
    choice, which `vulkaninfo` cannot say on a machine with two cards. None,
    and why, where it cannot be read."""
    program = tree / ("orbsim.exe" if os.name == "nt" else "orbsim")
    if not program.is_file():
        return None, f"{program} is not built"
    with tempfile.TemporaryDirectory() as out:
        try:
            subprocess.run([str(program), "--validate", "--probe", "clear", "--probe-out", out,
                            "--shader-dir", str(tree / "shaders")], cwd=tree, capture_output=True,
                           timeout=GPU_PROBE_SECONDS, check=False)
        except subprocess.TimeoutExpired:
            return None, f"the clear probe did not finish in {GPU_PROBE_SECONDS} s"
        sidecar = pathlib.Path(out) / "clear.txt"
        if not sidecar.is_file():
            return None, "the clear probe wrote no sidecar"
        found = gpu_identity_from_sidecar(sidecar.read_text(encoding="utf-8", errors="replace"))
    return (found, "") if found else (None, "the clear probe's sidecar does not name the card")


class Tree:
    def __init__(self, tree: pathlib.Path, stored: bool = True):
        """One reading of `tree`. With `stored`, Ninja's answers come from the
        tree's stored ones where those still hold (decision 520); without, every
        one is asked again -- the fresh reading `--fingerprints` compares with
        them (decision 521)."""
        self.tree = tree
        self.ninja = ninja_program(tree)
        self._key = self._graph_key()
        found = None
        if stored:
            with contextlib.suppress(OSError):
                found = stored_graph((tree / GRAPH).read_text(encoding="utf-8"), self._key)
        self.from_stored = found is not None
        # How many questions this reading put to Ninja itself.
        self.asked_ninja = 0
        self._learned = False
        tests = json.loads(run([ctest_program(tree), "--show-only=json-v1"], tree))["tests"]
        self.tests = {t["name"]: t for t in tests}
        self._headers_found = ({obj: set(headers) for obj, headers in found["headers"].items()}
                               if found else None)
        self._edges = dict(found["edges"]) if found else {}
        self._walks = ({target: (set(files), made) for target, (files, made) in found["walks"].items()}
                       if found else {})
        self._commands = dict(found["commands"]) if found else {}
        self._built = dict(found["built"]) if found else None
        self._fingerprints = {}
        self._tracked = None
        self._hashes = None
        self._trusted = None
        self._application_hash = None
        self._run_tests_store = None

    def _graph_key(self) -> dict:
        return graph_key(self.tree, shutil.which(self.ninja) or self.ninja, pathlib.Path(__file__).read_bytes())

    def _ask(self, tool: str, args=()) -> str:
        """A question to Ninja itself; what it answers is stored at the end."""
        self.asked_ninja += 1
        self._learned = True
        return run(tool_command(self.ninja, tool, args), self.tree)

    @property
    def headers(self) -> dict:
        """object -> the headers the compiler reported for it, from Ninja's log."""
        if self._headers_found is None:
            found = parse_deps(self._ask("deps"), self.tree)
            problem = deps_problem(found, self.tree)
            if problem:
                raise SystemExit(problem)
            self._headers_found = found
        return self._headers_found

    def save_graph(self) -> None:
        """Store what this reading learned from Ninja for the next one -- unless
        a file the answers rest on changed while it read, since they might then
        describe neither state. Written whole and then renamed into place, so a
        reader never finds half a file."""
        if not self._learned or self._graph_key() != self._key:
            return
        stored = {"key": self._key,
                  "headers": {obj: sorted(headers) for obj, headers in self.headers.items()},
                  "edges": self._edges,
                  "walks": {target: [sorted(files), made] for target, (files, made) in self._walks.items()},
                  "commands": self._commands,
                  "built": self.built()}
        partial = self.tree / (GRAPH + ".partial")
        partial.write_text(json.dumps(stored), encoding="utf-8")
        os.replace(partial, self.tree / GRAPH)

    def key(self, name: str) -> str:
        return norm(name) if os.path.isabs(name) else norm(self.tree / name)

    def built(self) -> dict:
        """Every node a build step makes, by its normalised path, to Ninja's name for it."""
        if self._built is None:
            self._built = {}
            for line in self._ask("targets", ["all"]).splitlines():
                name = line.rsplit(": ", 1)[0].strip()
                self._built[self.key(name)] = name
        return self._built

    def outputs(self) -> set:
        """Files a build step makes: their commands are in the fingerprint instead."""
        return set(self.built())

    def edges(self, names: list) -> dict:
        """name -> the inputs it reads, asking Ninja only about names not yet asked."""
        todo = [n for n in dict.fromkeys(names) if self.key(n) not in self._edges]
        batch, length = [], 0
        for name in todo + [None]:
            if name is not None and length + len(name) < QUERY_CHARS:
                batch.append(name)
                length += len(name) + 1
                continue
            if batch:
                answered = parse_query(self._ask("query", batch))
                for asked in batch:
                    self._edges[self.key(asked)] = answered.get(asked, [])
            batch, length = ([name], len(name) + 1) if name is not None else ([], 0)
        return {n: self._edges.get(self.key(n), []) for n in names}

    def walk(self, target: str) -> tuple:
        if target not in self._walks:
            self._walks[target] = walk_graph(target, self.edges, self.key, self.built(), self.headers)
            self._learned = True
        return self._walks[target]

    def build_inputs(self, target: str) -> set:
        return self.walk(target)[0]

    def build_commands(self, target: str) -> str:
        """The commands that make `target` and each node it is built from, one
        each -- Ninja's `commands -s` of every built node the walk reached, so
        the commands follow the same edges the inputs do (M1-109)."""
        if target not in self._commands:
            made = sorted(dict.fromkeys(self.walk(target)[1]))
            text, batch, length = [], [], 0
            for name in made + [None]:
                if name is not None and length + len(name) < QUERY_CHARS:
                    batch.append(name)
                    length += len(name) + 1
                    continue
                if batch:
                    text.append(self._ask("commands", ["-s", *batch]))
                batch, length = ([name], len(name) + 1) if name is not None else ([], 0)
            self._commands[target] = "".join(text)
        return self._commands[target]

    def program_target(self, name: str) -> str:
        for candidate in (name + ".exe", name):
            if (self.tree / candidate).exists():
                return candidate
        return name

    # --- what a test that starts the application reads (M1-109) ---------------

    def application(self) -> pathlib.Path:
        return self.tree / self.program_target(APPLICATION)

    def runs_application(self, test: dict) -> bool:
        return any(pathlib.Path(arg).is_file() and norm(arg) == norm(self.application())
                   for arg in arguments(test))

    def shader_nodes(self) -> dict:
        """Compiled shader file name -> Ninja's name for it: what the shader step builds."""
        made = self.edges([SHADER_STEP]).get(SHADER_STEP, [])
        return {pathlib.Path(name).name: name for name in made if name.endswith(".spv")}

    def shaders_read(self, test: dict):
        """The shader files a probe test read, from the sidecar its last run
        wrote, or None -- every shader -- where that cannot be trusted: not a
        probe test, no sidecar, a run that did not render, or a sidecar older
        than the application (register decision 394). The last is what a
        mutation pass leaves: its sidecars come from mutated programs, and the
        restored application is rebuilt after them."""
        found = defines(test)
        if "PROBE" not in found or "OUT" not in found:
            return None
        sidecar = pathlib.Path(found["OUT"]) / f"{found['PROBE']}.txt"
        try:
            data = sidecar.read_bytes()
            written, built = sidecar.stat().st_mtime_ns, self.application().stat().st_mtime_ns
            contents = {"sidecar": hashlib.sha256(data).hexdigest(), "application": self.application_hash()}
        except OSError:
            return None
        if not sidecar_trusted(written, built, contents, self.trusted_sidecars(), norm(sidecar)):
            return None
        # Trusted, by its time or by its contents: read as one written now.
        return sidecar_shaders(data.decode("utf-8", errors="replace"), built, built)

    def application_hash(self) -> str:
        if self._application_hash is None:
            self._application_hash = hashlib.sha256(self.application().read_bytes()).hexdigest()
        return self._application_hash

    def trusted_sidecars(self) -> dict:
        """The sidecars remembered by their contents (decision 616)."""
        if self._trusted is None:
            try:
                self._trusted = json.loads((self.tree / TRUSTED).read_text(encoding="utf-8"))
            except (OSError, ValueError):
                self._trusted = {}
        return self._trusted

    def save_trusted(self) -> None:
        if self._trusted is not None:
            partial = self.tree / (TRUSTED + ".partial")
            partial.write_text(json.dumps(self._trusted, sort_keys=True), encoding="utf-8")
            os.replace(partial, self.tree / TRUSTED)

    def stored_shaders(self, name: str):
        """The compiled shader files the test driver stored for the ctest entry
        `name`, normalised, or None (decisions 616 and 629): only while the
        application is byte for byte what it was when the list was stored,
        since another application may ask for other shaders."""
        if self._run_tests_store is None:
            module = run_tests_module()
            self._run_tests_store = module.load_store(self.tree / module.STATE)["tests"]
        entry = self._run_tests_store.get(name) or {}
        shaders = entry.get("shaders")
        if not isinstance(shaders, list):
            return None
        try:
            return shaders if entry.get("application") == self.application_hash() else None
        except OSError:
            return None

    def all_shaders(self) -> list:
        """Every compiled shader file, normalised."""
        return sorted(self.key(node) for node in self.shader_nodes().values())

    def judge_shaders(self, test: dict) -> list:
        """The compiled shader files a ctest entry's runs load, normalised
        (decisions 394, 616, 629 and 635): the list the test driver stored
        while the application is what it was then; otherwise, for an entry
        that starts the application, its probe's sidecar where that can be
        trusted, and every shader where not. A list naming a file no build
        step makes -- a shader folder of the test's own -- counts every
        shader. An entry that does not name the application loads none,
        unless the driver stored a list for it: a script that starts it."""
        stored = self.stored_shaders(test["name"])
        if stored is not None:
            known = set(self.all_shaders())
            return sorted(stored) if set(stored) <= known else self.all_shaders()
        if not self.runs_application(test):
            return []
        nodes = self.shader_nodes()
        named = self.shaders_read(test)
        if named is None or any(n not in nodes for n in named):
            return self.all_shaders()
        return sorted(self.key(nodes[n]) for n in named)

    def compiled_from(self, paths: set) -> set:
        """The compiled shader files built from any of `paths`, normalised,
        found through Ninja's graph (decision 643): a shader's source, or a
        file it includes."""
        return {self.key(node) for node in self.shader_nodes().values() if self.build_inputs(node) & paths}

    def mutated_inputs(self, file: str) -> set:
        """A mutated file as an input of its own mutant file: its text, but
        for a shader, which is judged by its compiled file's bytes, as every
        shader a judge loads is (decision 635)."""
        return self.compiled_from({norm(ROOT / file)}) or {norm(ROOT / file)}

    def compiled_hash(self, path: str) -> str:
        """A compiled shader file's SHA-256, its bytes read in full every time
        (decision 635): ten files of 12,452 bytes, measured 2026-10-18, cost
        nothing a size-and-date memory would save."""
        try:
            return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()[:16]
        except OSError:
            return "missing"

    def shader_parts(self, paths) -> dict:
        """A fingerprint's part for each compiled shader file, by its name."""
        return {f"compiled shader {pathlib.Path(path).name}": self.compiled_hash(path) for path in paths}

    # --- the judges ------------------------------------------------------------

    def judging_tests(self, spec: dict) -> list:
        return closure([e for m in spec["mutants"] for e in m.get("ctest", [])], self.tests)

    def test_programs(self, test: dict) -> list:
        """The programs this tree builds that a ctest entry runs, by Ninja's name.
        cmake or python running a wrapper is a tool, not a judge."""
        found = []
        for value in arguments(test):
            path = pathlib.Path(value)
            if path.is_file() and norm(path).startswith(norm(self.tree) + "/") and is_program(path, os.name):
                found.append(os.path.relpath(path, self.tree).replace("\\", "/"))
        return found

    def judge_inputs(self, spec: dict) -> set:
        """Every file a change to which makes the file due. A shader stands
        here by its compiled file, which no change git reports names: a
        supposed change reaches it (decision 643), and its bytes are judged
        through the fingerprints (decision 635)."""
        files = set()
        for mutant in spec["mutants"]:
            files |= self.mutated_inputs(mutant["file"])
            # Built, and for a suite run; a target is only built (M1-109).
            for name in list(mutant.get("suites", [])) + list(mutant.get("targets", [])):
                files |= self.build_inputs(self.program_target(name))
            for name in mutant.get("suites", []):
                if name == APPLICATION:
                    files |= set(self.all_shaders())
        for entry in self.judging_tests(spec):
            test = self.tests.get(entry)
            if test is None:
                files.add(f"{UNKNOWN_JUDGE}{entry}>")
                continue
            for value in arguments(test):
                if pathlib.Path(value).is_file():
                    files.add(norm(value))
            for program in self.test_programs(test):
                files |= self.build_inputs(program)
            files |= set(self.judge_shaders(test))
        return files

    def needs_gpu(self, spec: dict) -> bool:
        """Whether a file is judged on the graphics card (M1-109): a judging
        test labelled gpu or starting the application, or a suite run behind a
        gpu-labelled entry -- a suite run whole reads the probes' images too."""
        tests = [self.tests[e] for e in self.judging_tests(spec) if e in self.tests]
        if any("gpu" in labels_of(t) or self.runs_application(t) for t in tests):
            return True
        suites = {s for m in spec["mutants"] for s in m.get("suites", [])}
        behind_gpu = {pathlib.Path(p).stem for t in self.tests.values() if "gpu" in labels_of(t)
                      for p in self.test_programs(t)}
        return bool(suites & (behind_gpu | {APPLICATION}))

    # --- fingerprints (M1-103, register decision 250; in parts since M1-109) ---

    def fingerprint_parts(self, spec: dict) -> dict:
        """judge -> its fingerprint's parts, for the judges of one mutant file:
        the same programs and CTest entries judge_inputs reads. A ctest entry's
        parts hold each compiled shader file it loads, by its bytes, where
        they held the commands that compile every shader (decision 635): the
        bytes record whatever a compiler or a flag changes, and a change that
        compiles to the same bytes changes nothing. A mutated shader, and the
        shaders of the application run as a suite, are held the same way."""
        found = {}
        for mutant in spec["mutants"]:
            compiled = self.compiled_from({norm(ROOT / mutant["file"])})
            if compiled:
                found[f"mutated shader {mutant['file']}"] = self.shader_parts(sorted(compiled))
            for name in list(mutant.get("suites", [])) + list(mutant.get("targets", [])):
                target = self.program_target(name)
                found[f"program {target}"] = self.program_parts(target)
            if APPLICATION in mutant.get("suites", []):
                found[f"suite {self.program_target(APPLICATION)}"] = self.shader_parts(self.all_shaders())
        for entry in self.judging_tests(spec):
            test = self.tests.get(entry)
            if test is None:
                found[f"ctest {entry}"] = {"definition": "unknown"}
                continue
            parts = {"definition": test_fingerprint(test, self.tree, ROOT)}
            parts.update(self.shader_parts(self.judge_shaders(test)))
            found[f"ctest {entry}"] = parts
            for program in self.test_programs(test):
                found[f"program {program}"] = self.program_parts(program)
        return found

    def fingerprints(self, spec: dict) -> dict:
        return {key: combine(parts) for key, parts in self.fingerprint_parts(spec).items()}

    def program_parts(self, target: str) -> dict:
        if target not in self._fingerprints:
            files = {path: self.content_hash(path) for path in self.build_inputs(target)
                     if path not in self.tracked() and path not in self.outputs()}
            self._fingerprints[target] = program_parts(self.build_commands(target), files, self.tree, ROOT)
        return self._fingerprints[target]

    def tracked(self) -> set:
        """Files git tracks: their changes are judged by `git diff` instead."""
        if self._tracked is None:
            self._tracked = {norm(ROOT / n) for n in run(["git", "ls-files"], ROOT).splitlines() if n}
        return self._tracked

    def content_hash(self, path: str) -> str:
        """A file's digest (content_digest), remembered by its size and date in
        the build tree: a file rewritten with the same size and the same date
        would be missed, which decision 250 accepts against hashing every file
        on every run."""
        if self._hashes is None:
            try:
                self._hashes = json.loads(self._hash_file().read_text(encoding="utf-8"))
            except (OSError, ValueError):
                self._hashes = {}
        try:
            info = os.stat(path)
        except OSError:
            return "missing"
        key = [info.st_size, info.st_mtime_ns]
        known = self._hashes.get(path)
        if known and known[:2] == key:
            return known[2]
        inside = path.startswith(norm(self.tree) + "/")
        digest = content_digest(pathlib.Path(path).read_bytes(), inside, self.tree, ROOT)
        self._hashes[path] = key + [digest]
        return digest

    def _hash_file(self) -> pathlib.Path:
        return self.tree / HASHES

    def save_hashes(self) -> None:
        if self._hashes is not None:
            self._hash_file().write_text(json.dumps(self._hashes), encoding="utf-8")


def same_tree(recorded: str, tree: pathlib.Path) -> bool:
    return norm(ROOT / recorded) == norm(tree)


def same_tree_path(one: pathlib.Path, two: pathlib.Path) -> bool:
    return norm(one) == norm(two)


def supposed_changes(changed: set, assumed: set, judged, compiled_from) -> set:
    """The changes a mutant file is judged on: `changed`, and the compiled
    shader files built from what it holds (decision 643) -- from the files
    supposed changed with --assume-changed, a shader's source among them; and
    from every changed file where no fingerprints are compared (`judged`
    None), since the compiled bytes' parts that judge a real change to a
    shader's source are then not there (decision 635). `compiled_from` maps
    files to the compiled shader files built from them."""
    return changed | compiled_from(changed if judged is None else assumed)


def judged_since(record: dict, now: dict, now_parts=None) -> list:
    """The judges whose fingerprint differs from the one the pass recorded, each
    with the parts that moved where both sides recorded them (M1-109)."""
    was = record["judges"]
    was_parts = record.get("judge_parts", {})
    now_parts = now_parts or {}
    found = []
    for key in sorted(set(was) | set(now)):
        if was.get(key) == now.get(key):
            continue
        before, after = was_parts.get(key), now_parts.get(key)
        moved = sorted(p for p in set(before or {}) | set(after or {})
                       if (before or {}).get(p) != (after or {}).get(p)) if before and after else []
        found.append(f"{key}: fingerprint changed" + (f" ({', '.join(moved)})" if moved else ""))
    return found


def changed_since(commit: str) -> set:
    names = run(["git", "diff", "--name-only", commit], ROOT).splitlines()
    names += run(["git", "ls-files", "--others", "--exclude-standard"], ROOT).splitlines()
    return {norm(ROOT / n) for n in names if n}


def judging_tree(record, named: pathlib.Path, only_assumed: bool, exists) -> tuple:
    """(tree, why): the tree a mutant file is judged in, and what to say about it.

    The tree its pass was recorded in, whatever tree was named (M1-106, register
    decision 255): a file is judged in one tree, `m1-12` to `m1-17` in Debug and
    the rest in RelWithDebInfo, and only there can its fingerprints be compared.
    Asked about the other tree, the strict rule used to list it due for any
    build-definition change -- safe, but 19 files instead of 7 on 2026-10-01.
    The named tree where there is no record or the changes are made up
    (--only-assumed); None, so the file is due, where the recorded tree is not
    on this machine."""
    if record is None or only_assumed:
        return named, ""
    recorded = ROOT / record["tree"]
    if not exists(recorded):
        return None, f"recorded in {record['tree']}, which is not here"
    return recorded, ""


def main_check(tree: pathlib.Path, assumed: list, only_assumed: bool, expect_due: list,
               expect_current=()) -> int:
    passes = json.loads(PASSES.read_text(encoding="utf-8")) if PASSES.exists() else {}
    readings = {}
    cards = {}

    def reading(path: pathlib.Path) -> Tree:
        """One reading of each tree used, made when it is first needed."""
        if norm(path) not in readings:
            readings[norm(path)] = Tree(path.resolve())
        return readings[norm(path)]

    def card(path: pathlib.Path) -> tuple:
        """The graphics card a tree's application runs on, read once (M1-109)."""
        if norm(path) not in cards:
            cards[norm(path)] = gpu_identity(path.resolve())
        return cards[norm(path)]

    due_count = 0
    due_names = set()
    assumed_paths = {norm(ROOT / a) for a in assumed}
    files = sorted((ROOT / "scripts" / "mutants").glob("*.json"))
    for spec_path in files:
        spec = json.loads(spec_path.read_text(encoding="utf-8"))
        record = passes.get(spec_path.name)
        changed = changed_since(record["commit"]) if record and not only_assumed else set()
        changed |= assumed_paths
        where, absent = judging_tree(record, tree, only_assumed, pathlib.Path.is_dir)
        if where is None:
            due_count += 1
            due_names.add(spec_path.name)
            print(f"DUE      {spec_path.name}: {absent}")
            continue
        t = reading(where)
        # Fingerprints decide a build-definition change where the pass recorded
        # them in this tree; anywhere else the strict rule stands (M1-103).
        judged = None
        if record and not only_assumed and "judges" in record and same_tree(record["tree"], where):
            parts = t.fingerprint_parts(spec)
            judged = judged_since(record, {k: combine(p) for k, p in parts.items()}, parts)
        # The graphics card, for a file judged on it (M1-109) -- compared with
        # the record, so not for made-up changes.
        other = []
        own = own_mutants_reason(spec, norm(spec_path), changed,
                                 lambda: None if only_assumed else file_at(record["commit"], spec_path))
        if own:
            other.append(own)
        if record and not only_assumed and t.needs_gpu(spec):
            reason = gpu_reason(record, *card(where))
            if reason:
                other.append(reason)
        seen = supposed_changes(changed, assumed_paths, judged, t.compiled_from)
        due, reasons = is_due(seen, t.judge_inputs(spec), record is not None or only_assumed, judged, other)
        root = norm(ROOT) + "/"
        shown = [r[len(root):] if r.startswith(root) else r for r in reasons[:3]]
        in_tree = "" if same_tree_path(where, tree) else f" [in {os.path.relpath(where, ROOT).replace(chr(92), '/')}]"
        if due:
            due_count += 1
            due_names.add(spec_path.name)
            more = f" and {len(reasons) - 3} more" if len(reasons) > 3 else ""
            print(f"DUE      {spec_path.name}{in_tree}: {', '.join(shown)}{more}")
        else:
            since = f"passed at {record['commit'][:12]}" if record else "no recorded pass, judged on the assumed changes only"
            print(f"current  {spec_path.name}{in_tree} ({since})")
    for t in readings.values():
        t.save_hashes()
        t.save_graph()
        t.save_trusted()
    print(f"mutants-due: {due_count} of {len(files)} mutant files due")
    # --expect-due and --expect-current: how CTest checks the real computation
    # against this tree, in both directions (the second since M1-109).
    missing = [name for name in expect_due if name not in due_names]
    for name in missing:
        print(f"mutants-due: expected {name} to be due, and it is not")
    listed = [name for name in expect_current if name in due_names]
    for name in listed:
        print(f"mutants-due: expected {name} to be current, and it is due")
    return 1 if missing or listed else 0


def main_expect_unmet(tree: pathlib.Path, assumed: list, name: str, kind: str = "due") -> int:
    """--expect-unmet and --expect-unmet-current: the expectation must be able
    to fail. Expecting `name` to be due where it is not -- or, since M1-109,
    current where it is due -- must make the check fail *and* say so, and this
    passes only when both happen. A CTest rule could check one or the other,
    never both: "expected to fail" also passed a script that refused the tree
    or crashed, and a message rule passed one that said so but exited 0
    (register decision 251, measured in a scratch CTest project on 2026-09-30).
    A refusal still fails here, since it leaves by SystemExit."""
    said = io.StringIO()
    with contextlib.redirect_stdout(said):
        if kind == "due":
            code = main_check(tree, assumed, True, [name])
        else:
            code = main_check(tree, assumed, True, [], [name])
    sys.stdout.write(said.getvalue())
    other = "current" if kind == "due" else "due"
    message = f"mutants-due: expected {name} to be {kind}, and it is {'not' if kind == 'due' else 'due'}"
    reported = message in said.getvalue()
    if code == 1 and reported:
        print(f"mutants-due: expecting {name} {kind} where it is {other} failed the check and said so, as it must")
        return 0
    print(f"mutants-due: expecting {name} {kind} where it is {other} must fail the check and say so; "
          f"it returned {code}, and {'said' if reported else 'did not say'} so")
    return 1


def main_fingerprints(tree: pathlib.Path, name: str, reader=None) -> int:
    """--fingerprints: one mutant file's judges, fingerprinted twice -- once
    from a reading that asks Ninja everything again, once from Ninja's stored
    answers where they hold (M1-129, register decision 521). A fingerprint that
    is not the same twice would make files due at random, or say the stored
    answers are not what a fresh reading gives; one that names no judge would
    decide nothing. The stored answers are read before the fresh reading is
    taken, so that the two are never the same computation; where none hold,
    the second reading asks Ninja too, as both did before."""
    spec = json.loads((ROOT / "scripts" / "mutants" / name).read_text(encoding="utf-8"))
    reader = reader or Tree
    stored = reader(tree)
    fresh = reader(tree, stored=False)
    first, second = fresh.fingerprints(spec), stored.fingerprints(spec)
    problems = [f"no judges fingerprinted for {name}"] if not first else []
    if fresh.asked_ninja == 0:
        problems.append("the fresh reading asked Ninja nothing, so nothing was compared")
    problems += [f"{key} cannot be fingerprinted" for key, value in first.items() if value == combine({"definition": "unknown"})]
    problems += [f"{key} differs between two readings" for key in first if first[key] != second.get(key)]
    for line in problems:
        print(f"mutants-due: {line}")
    if not problems:
        fresh.save_graph()
        print(f"mutants-due: {len(first)} judges of {name}, the same fingerprints twice "
              f"({'fresh and stored' if stored.from_stored else 'both fresh: no stored answers held'})")
    return 1 if problems else 0


def main_gpu_identity(tree: pathlib.Path) -> int:
    """--gpu-identity: the graphics card this tree's application runs on, read
    the way a pass records it (M1-109); fails where it cannot be read, so a
    machine on which every GPU-judged file would always be due says so."""
    found, why = gpu_identity(tree)
    if found is None:
        print(f"mutants-due: the graphics card cannot be read: {why}")
        return 1
    for name, value in sorted(found.items()):
        print(f"mutants-due: {name}: {value}")
    problem = identity_problem(found)
    if problem:
        print(f"mutants-due: {problem}")
        return 1
    return 0


def is_program(path: pathlib.Path, system: str) -> bool:
    """Whether a file a test names is a program the build made.

    On Windows only an .exe is: Windows has no execute bit, so os.access with
    X_OK is true of any file that exists -- which made a golden image a test
    writes into the build tree look like a program, and Ninja then refused to
    name its inputs (found by M1-17, 2026-09-29, once the golden tests put
    clear-block.png there). Elsewhere the execute bit decides."""
    if system == "nt":
        return path.suffix.lower() == ".exe"
    return os.access(path, os.X_OK)


def self_test() -> int:
    root = norm(ROOT) + "/"
    inputs = {root + "src/view/camera.cpp", root + "tests/test_camera.cpp", root + "src/core/units.hpp"}
    cases = {
        "nothing changed": (set(), True, False),
        "the mutated file changed": ({root + "src/view/camera.cpp"}, True, True),
        "a header between changed": ({root + "src/core/units.hpp"}, True, True),
        "an unrelated file changed": ({root + "src/astro/sun.cpp"}, True, False),
        "the build definition changed": ({root + "cmakelists.txt"}, True, True),
        "the harness changed": ({root + "scripts/mutate.py"}, True, True),
        "data changed": ({root + "data/skyfield/x.txt"}, True, True),
        "no recorded pass": (set(), False, True),
    }
    if os.name != "nt":
        cases["the build definition changed"] = ({root + "CMakeLists.txt"}, True, True)
    failures = 0
    for name, (changed, recorded, want) in cases.items():
        got, _ = is_due(changed, inputs, recorded)
        status = "ok" if got == want else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name}: {'due' if got else 'current'} ({status})")
    # A build-definition change judged by what it changed (M1-103): through the
    # judges' fingerprints where they can be compared, as before where not.
    judged_cases = {
        "the build definition changed, no judge's fingerprint did": ({root + "cmakelists.txt"}, []),
        "the build definition changed, a judge's fingerprint did": ({root + "cmakelists.txt"}, ["ctest x"]),
        "a judge's fingerprint changed, and no file": (set(), ["program y"]),
        "the harness changed, no judge's fingerprint did": ({root + "scripts/mutate.py"}, []),
        "data changed, no judge's fingerprint did": ({root + "data/skyfield/x.txt"}, []),
    }
    judged_want = [False, True, True, True, True]
    for (name, (changed, judged)), want in zip(judged_cases.items(), judged_want):
        got, _ = is_due(changed, inputs, True, judged)
        status = "ok" if got == want else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name}: {'due' if got else 'current'} ({status})")
    # A further reason -- the graphics card's (M1-109) -- makes a file due by itself.
    got, reasons = is_due(set(), inputs, True, [], ["graphics card changed: card a -> b"])
    status = "ok" if got and reasons == ["graphics card changed: card a -> b"] else "WRONG"
    failures += status == "WRONG"
    print(f"self-test: a further reason alone makes a file due, and is named ({status})")
    # The fingerprints themselves. The same tree in two places, spelled with
    # either separator and, on Windows, either case, must agree; a changed flag,
    # a changed untracked file or a changed test property must not. The two
    # places are real absolute paths on whichever system runs this: the first
    # version used Windows paths, which are not absolute on Linux, and failed
    # both Linux trees (2026-10-01).
    scratch_root = pathlib.Path(tempfile.gettempdir()).resolve()
    one, two = scratch_root / "repo-one", scratch_root / "elsewhere" / "copy"
    commands = "clang++ -O2 -I{r}/src -c {r}\\src\\a.cpp -o {t}/CMakeFiles/a.obj\n"
    cmds_one = commands.format(r=str(one), t=str(one / "build" / "rel"))
    # Capitals name the same path only where the file system ignores case.
    two_spelled = str(two).upper() if os.name == "nt" else str(two)
    cmds_two = commands.format(r=two_spelled, t=str(two / "build" / "rel"))
    files_one = {norm(one / "build/rel/_deps/x-src/x.hpp"): "h1", "c:/sdk/include/stdio.h": "h2"}
    files_two = {norm(two / "build/rel/_deps/x-src/x.hpp"): "h1", "c:/sdk/include/stdio.h": "h2"}

    def prog(cmds, files, where):
        return program_fingerprint(cmds, files, where / "build" / "rel", where)

    def parts(cmds, files, where):
        return program_parts(cmds, files, where / "build" / "rel", where)

    test_one = {"name": "t", "backtrace": 3, "command": [str(one / "build/rel/t.exe")],
                "properties": [{"name": "WILL_FAIL", "value": True}]}
    test_two = dict(test_one, backtrace=9, command=[str(two / "build/rel/t.exe")])
    fp_cases = {
        "the same program in two places agrees": prog(cmds_one, files_one, one) == prog(cmds_two, files_two, two),
        "a changed flag changes a program's": prog(cmds_one, files_one, one) != prog(
            cmds_one.replace("-O2", "-O3"), files_one, one),
        "a changed untracked file changes a program's": prog(cmds_one, files_one, one) != prog(
            cmds_one, dict(files_one, **{"c:/sdk/include/stdio.h": "h3"}), one),
        "the same test in two places, declared on another line, agrees":
            test_fingerprint(test_one, one / "build/rel", one) == test_fingerprint(test_two, two / "build/rel", two),
        "a changed test property changes a test's": test_fingerprint(test_one, one / "build/rel", one) != test_fingerprint(
            dict(test_one, properties=[{"name": "WILL_FAIL", "value": False}]), one / "build/rel", one),
        # In parts (M1-109): each change moves its own part and no other.
        "a changed flag moves the commands part alone": [p for p, v in parts(cmds_one, files_one, one).items()
                                                         if v != parts(cmds_one.replace("-O2", "-O3"), files_one, one)[p]]
            == ["commands"],
        "a changed toolchain file moves the other-files part alone": [
            p for p, v in parts(cmds_one, files_one, one).items()
            if v != parts(cmds_one, dict(files_one, **{"c:/sdk/include/stdio.h": "h3"}), one)[p]] == ["other files"],
        "a changed file in the tree moves the build-tree part alone": [
            p for p, v in parts(cmds_one, files_one, one).items()
            if v != parts(cmds_one, dict(files_one, **{norm(one / "build/rel/_deps/x-src/x.hpp"): "h9"}), one)[p]]
            == ["build-tree files"],
    }
    in_capitals = placeholders(str(one).upper() + "/x", one / "build" / "rel", one)
    fp_cases["a path in capitals is the same path on Windows, and another one elsewhere"] = (
        in_capitals == "<ROOT>/x" if os.name == "nt" else in_capitals == str(one).upper() + "/x")
    fp_cases["a changed, a new and a lost judge are all named, an unchanged one not"] = judged_since(
        {"judges": {"same": "1", "moved": "2", "lost": "3"}}, {"same": "1", "moved": "9", "new": "4"}) == [
        "lost: fingerprint changed", "moved: fingerprint changed", "new: fingerprint changed"]
    fp_cases["a moved judge names the parts that moved, where both sides recorded them"] = judged_since(
        {"judges": {"p": "1", "q": "5"}, "judge_parts": {"p": {"commands": "a", "other files": "b"}}},
        {"p": "2", "q": "6"}, {"p": {"commands": "a", "other files": "c"}, "q": {"commands": "x"}}) == [
        "p: fingerprint changed (other files)", "q: fingerprint changed"]
    # A file in the build tree is hashed with the tree's path taken out (M1-109,
    # register decision 395): SDL's precompiled header, the case that was found.
    pch = "#include \"{t}/_deps/sdl3-src/src/SDL_internal.h\"\n"
    tree_one, tree_two = one / "build" / "rel", two / "build" / "rel"
    digest_one = content_digest(pch.format(t=str(tree_one)).encode(), True, tree_one, one)
    fp_cases["a generated file naming its own tree hashes the same in two trees"] = digest_one == content_digest(
        pch.format(t=str(tree_two)).encode(), True, tree_two, two)
    fp_cases["a generated file that really differs hashes differently"] = digest_one != content_digest(
        pch.format(t=str(tree_one)).replace("SDL_internal", "SDL_other").encode(), True, tree_one, one)
    raw = b"\xff\xfe binary \x00 bytes"
    fp_cases["a file without the tree's path keeps its plain digest, bytes and all"] = content_digest(
        raw, True, tree_one, one) == hashlib.sha256(raw).hexdigest()
    fp_cases["a file outside the tree is hashed as written"] = content_digest(
        pch.format(t=str(tree_one)).encode(), False, tree_one, one) == hashlib.sha256(
        pch.format(t=str(tree_one)).encode()).hexdigest()
    for name, got in fp_cases.items():
        status = "ok" if got else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name} ({status})")
    # Which tree a file is judged in (M1-106): the one its pass was recorded in,
    # whatever tree was named -- where else could its fingerprints be compared?
    # The named one where there is no record, or the changes are made up; and
    # none, so the file is due, where the recorded tree is not on this machine.
    named = pathlib.Path(tempfile.gettempdir()) / "build" / "relwithdebinfo"
    in_debug = {"commit": "c0ffee", "tree": "build/debug"}
    tree_cases = {
        "a file is judged in the tree its pass was recorded in":
            judging_tree(in_debug, named, False, lambda p: True)[0] == ROOT / "build" / "debug",
        "a file with no record is judged in the named tree":
            judging_tree(None, named, False, lambda p: True)[0] == named,
        "made-up changes are judged in the named tree":
            judging_tree(in_debug, named, True, lambda p: True)[0] == named,
        "a recorded tree this machine does not have makes the file due, and says so":
            judging_tree(in_debug, named, False, lambda p: False) == (None, "recorded in build/debug, which is not here"),
    }
    for name, got in tree_cases.items():
        status = "ok" if got else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name} ({status})")
    # Ninja's header record (M1-102). Read while it is being compacted, it can
    # come back empty with exit status 0, and every judge would then seem to
    # include no header -- fewer files due, and nothing said. So an empty
    # answer is refused, and a real one must parse -- a relative header against
    # the tree, since M1-109.
    tree = pathlib.Path(tempfile.gettempdir()) / "tree"
    listing = ("CMakeFiles/t.dir/a.cpp.obj: #deps 2, deps mtime 1 (VALID)\n"
               "    src/a.cpp\n    src/core/Units.hpp\n\n")
    parsed = parse_deps(listing, tree)
    deps_cases = {
        "a header record parses": (parsed == {norm(tree / "CMakeFiles/t.dir/a.cpp.obj"):
                                              {norm(tree / "src/a.cpp"), norm(tree / "src/core/Units.hpp")}}, True),
        "a header record is accepted": (deps_problem(parsed, tree) is None, True),
        "an empty header record is refused": (deps_problem(parse_deps("", tree), tree) is not None, True),
    }
    # Every Ninja tool runs as a dry run (M1-109, register decision 399), so no
    # read can rewrite a log another Ninja holds open.
    deps_cases["every Ninja tool is asked for as a dry run"] = (
        all(tool_command("ninja", tool)[:4] == ["ninja", "-n", "-t", tool]
            for tool in ("deps", "query", "targets", "commands")), True)
    for name, (got, want) in deps_cases.items():
        status = "ok" if got == want else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name} ({status})")
    # Ninja's graph, without its build-order edges (M1-109, register decision
    # 393). The shape that was measured: the application waits for the shader
    # step only so that the shaders exist; an object includes a header a build
    # step generates from a shader -- the case that must still count.
    query = ("app.exe:\n  input: LINK\n    main.obj\n    | lib.lib\n    || shaders_step\n  outputs:\n"
             "main.obj:\n  input: CXX\n    /src/main.cpp\n    || order_step\n  outputs:\n    app.exe\n"
             "/src/main.cpp:\n  outputs:\n    main.obj\n")
    graph = {"app.exe": ["main.obj", "lib.lib"], "main.obj": ["/src/main.cpp", "gen/embedded.h"],
             "lib.lib": ["/src/lib.cpp"], "gen/embedded.h": ["/shaders/embedded.vert"],
             "shaders_step": ["/shaders/a.spv"], "/shaders/a.spv": ["/shaders/a.vert"]}
    built_nodes = {"app.exe": "app.exe", "main.obj": "main.obj", "lib.lib": "lib.lib",
                   "gen/embedded.h": "gen/embedded.h", "shaders_step": "shaders_step",
                   "/shaders/a.spv": "/shaders/a.spv"}
    edge_graph = dict(graph, **{"main.obj": ["/src/main.cpp"]})
    included = {"main.obj": {"gen/embedded.h", "/src/common.hpp"}}
    walked, made = walk_graph("app.exe", lambda names: {n: edge_graph.get(n, []) for n in names},
                              lambda n: n, built_nodes, included)
    graph_cases = {
        "a query's build-order-only inputs are left out, its implicit ones kept":
            parse_query(query) == {"app.exe": ["main.obj", "lib.lib"], "main.obj": ["/src/main.cpp"],
                                   "/src/main.cpp": []},
        "a program's sources and libraries are its inputs": {"/src/main.cpp", "/src/lib.cpp", "lib.lib"} <= walked,
        "a shader the program only waits for is not an input": not {"/shaders/a.vert", "/shaders/a.spv"} & walked,
        "a header included from a generated file still reaches what it is generated from":
            {"gen/embedded.h", "/shaders/embedded.vert", "/src/common.hpp"} <= walked,
        "the built nodes reached are named, the program's own among them":
            set(made) == {"app.exe", "main.obj", "lib.lib", "gen/embedded.h"},
    }
    # A test's judges include what CTest runs before it (M1-109): a fixture's
    # setup and cleanup, transitively, and the tests it is ordered after.
    tests = {
        "reader": {"name": "reader", "properties": [{"name": "FIXTURES_REQUIRED", "value": ["frames"]}]},
        "probe": {"name": "probe", "properties": [{"name": "FIXTURES_SETUP", "value": ["frames"]},
                                                  {"name": "FIXTURES_REQUIRED", "value": ["dir"]}]},
        "mkdir": {"name": "mkdir", "properties": [{"name": "FIXTURES_SETUP", "value": ["dir"]}]},
        "tidy": {"name": "tidy", "properties": [{"name": "FIXTURES_CLEANUP", "value": ["frames"]}]},
        "after": {"name": "after", "properties": [{"name": "DEPENDS", "value": ["first"]}]},
        "first": {"name": "first", "properties": []},
        "alone": {"name": "alone", "properties": []},
    }
    graph_cases["a fixture's setup, its own setup and its cleanup are judges too"] = set(
        closure(["reader"], tests)) == {"reader", "probe", "mkdir", "tidy"}
    graph_cases["a test ordered after another has that one as a judge"] = closure(["after"], tests) == ["after", "first"]
    graph_cases["a test with neither has only itself"] = closure(["alone"], tests) == ["alone"]
    # A probe's sidecar (M1-109, register decision 394): its shaders only from
    # a run that rendered and names them.
    rendered = "probe = lambert\noutcome              = rendered\nshaders              = a.spv b.spv\n"
    graph_cases["a rendered probe's sidecar gives its shaders"] = sidecar_shaders(rendered, 2, 1) == ["a.spv", "b.spv"]
    graph_cases["a sidecar written as the application was built still counts"] = sidecar_shaders(
        rendered, 1, 1) == ["a.spv", "b.spv"]
    graph_cases["a sidecar older than the application gives none"] = sidecar_shaders(rendered, 1, 2) is None
    graph_cases["a run that did not render gives none"] = sidecar_shaders(
        rendered.replace("= rendered", "= failed: no device"), 2, 1) is None
    graph_cases["a sidecar written before M1-109 gives none"] = sidecar_shaders(
        "outcome = rendered\nfiles = a.png\n", 2, 1) is None
    graph_cases["a sidecar naming no shader gives none"] = sidecar_shaders(
        rendered.replace("a.spv b.spv", "none"), 2, 1) is None
    # A sidecar trusted by its contents (M1-129, register decision 616): the
    # time rule's trust is remembered, and holds after a relink to the same
    # bytes, and only then.
    seen = {"sidecar": "s1", "application": "a1"}
    memory = {}
    graph_cases["a sidecar newer than the application is trusted"] = sidecar_trusted(2, 1, seen, memory, "k")
    graph_cases["and remembered by both contents"] = memory == {"k": seen}
    graph_cases["an application relinked to the same bytes keeps it trusted"] = sidecar_trusted(1, 2, seen, memory, "k")
    graph_cases["an application of other bytes does not"] = not sidecar_trusted(
        1, 2, {**seen, "application": "a2"}, memory, "k")
    graph_cases["a sidecar of other bytes does not"] = not sidecar_trusted(1, 2, {**seen, "sidecar": "s2"}, memory, "k")
    graph_cases["a sidecar never trusted by its time is not"] = not sidecar_trusted(1, 2, seen, memory, "other")
    with tempfile.TemporaryDirectory() as scratch:
        writer = Tree.__new__(Tree)
        writer.tree, writer._trusted = pathlib.Path(scratch), {"k": seen}
        writer.save_trusted()
        reader = Tree.__new__(Tree)
        reader.tree, reader._trusted = pathlib.Path(scratch), None
        graph_cases["what was remembered is kept in the tree for the next reading"] = (
            reader.trusted_sidecars() == {"k": seen})
    graph_cases["-D arguments are read as a CMake script receives them"] = defines(
        {"command": ["cmake", "-DPROBE=lambert", "-DOUT=C:/x=y", "-P", "s.cmake"]}) == {"PROBE": "lambert",
                                                                                     "OUT": "C:/x=y"}
    for name, got in graph_cases.items():
        status = "ok" if got else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name} ({status})")
    # The graphics card (M1-109, register decision 397).
    card_text = ("gpu.vendor = 0x10de\ngpu.device = 0x25ba\ndriver.info = 582.53\ndriver.version = 0x918d4000\n"
                 "validation.layer = 1.4.357 (variant 0), implementation 1\n")
    here = gpu_identity_from_sidecar(card_text)
    card_cases = {
        "a sidecar gives the card, the driver and the validation layer": here == {
            "card": "0x10de 0x25ba", "driver": "582.53 0x918d4000",
            "validation layer": "1.4.357 (variant 0), implementation 1"},
        "a sidecar missing a line gives no card": gpu_identity_from_sidecar(
            card_text.replace("validation.layer", "validation.layr")) is None,
        "the same card is no reason": gpu_reason({"gpu": here}, here, "") is None,
        "another card is a reason, and says what moved": gpu_reason(
            {"gpu": dict(here, card="0x1002 0x744c")}, here, "") == "graphics card changed: card 0x1002 0x744c -> 0x10de 0x25ba",
        "a new driver is a reason": gpu_reason({"gpu": dict(here, driver="590.1 0x0")}, here, "") is not None,
        "a new validation layer is a reason": gpu_reason(
            {"gpu": dict(here, **{"validation layer": "1.4.363 (variant 0), implementation 1"})}, here, "") is not None,
        "a record without a card is a reason": gpu_reason({}, here, "") == "no graphics card recorded",
        "a card that cannot be read here is a reason": gpu_reason(
            {"gpu": here}, None, "no device") == "the graphics card here is unknown: no device",
        "a run with its validation layer stands": identity_problem(here) is None,
        "a validated run without the layer is refused": identity_problem(
            dict(here, **{"validation layer": "none: the run had no validation layer"})) is not None,
    }
    for name, got in card_cases.items():
        status = "ok" if got else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name} ({status})")
    # Which files a test names are programs: an image is not, on either system.
    with tempfile.TemporaryDirectory() as scratch:
        image = pathlib.Path(scratch) / "clear-block.png"
        image.write_bytes(b"not a program")
        program = pathlib.Path(scratch) / "orbsim.exe"
        program.write_bytes(b"a program")
        program_cases = {
            "an image, on Windows": (image, "nt", False),
            "an .exe, on Windows": (program, "nt", True),
        }
        # The execute bit can only be asked about where there is one.
        if os.name != "nt":
            image.chmod(0o644)
            program_cases["an image without the execute bit, elsewhere"] = (image, "posix", False)
        for name, (path, system, want) in program_cases.items():
            got = is_program(path, system)
            status = "ok" if got == want else "WRONG"
            failures += status == "WRONG"
            print(f"self-test: {name}: {'a program' if got else 'not a program'} ({status})")
    for name, got in m1_129_cases(root, inputs).items():
        status = "ok" if got else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name} ({status})")
    for name, got in m1_131_cases().items():
        status = "ok" if got else "WRONG"
        failures += status == "WRONG"
        print(f"self-test: {name} ({status})")
    return 1 if failures else 0


def m1_129_cases(root: str, inputs: set) -> dict:
    """M1-129's rules: a judge this tree does not have, a mutant file's own
    mutants, and Ninja's stored answers -- whose key must move with each thing
    they rest on, or a reading would use answers about another graph."""
    unknown = inputs | {f"{UNKNOWN_JUDGE}renamed_test>"}
    spec = {"task": "T", "tree": "build/debug", "note": "n",
            "mutants": [{"name": "m", "file": "f", "find": "a", "replace": "b", "why": "w"}]}
    own = root + "scripts/mutants/t.json"

    def then(**changes):
        return lambda: json.dumps(dict(spec, **changes))

    moved_why = [dict(spec["mutants"][0], why="other")]
    cases = {
        "a judge this tree does not have makes its file due, and is named":
            is_due(set(), unknown, True, []) == (True, [f"{UNKNOWN_JUDGE}renamed_test>"]),
        "a file whose judges all exist is not due for that": not is_due(set(), inputs, True, [])[0],
        "a mutant file that did not change is no reason":
            own_mutants_reason(spec, own, set(), then()) is None,
        "a changed note, task or tree is no reason":
            own_mutants_reason(spec, own, {own}, then(note="x", task="U", tree="build/rel")) is None,
        "a changed mutant is a reason, its why included":
            own_mutants_reason(spec, own, {own}, then(mutants=moved_why)) is not None,
        "a mutant file new since its pass, or assumed changed, is a reason":
            own_mutants_reason(spec, own, {own}, lambda: None) is not None,
    }
    with tempfile.TemporaryDirectory() as scratch:
        tree = pathlib.Path(scratch)
        ninja = tree / "ninja.exe"
        # Named here rather than taken from GRAPH_FILES, so that a file left
        # out of the key is a case that fails, not a case that disappears.
        rested_on = ("build.ninja", "CMakeFiles/rules.ninja", ".ninja_deps", ninja.name)
        for name in rested_on:
            (tree / name).parent.mkdir(parents=True, exist_ok=True)
            (tree / name).write_text(name, encoding="utf-8")
        key = graph_key(tree, str(ninja), b"script")
        text = json.dumps({"key": key, "edges": {}})
        cases["stored answers under this key are read"] = stored_graph(text, key) == json.loads(text)
        cases["no stored answers, or a file that does not parse, give none"] = (
            stored_graph(None, key) is None and stored_graph("{not json", key) is None)
        cases["another version of this script misses the stored answers"] = (
            stored_graph(text, graph_key(tree, str(ninja), b"mutated script")) is None)
        cases["another tree misses them"] = stored_graph(text, graph_key(tree / "elsewhere", str(ninja), b"script")) is None
        for name in rested_on:
            before = (tree / name).stat().st_mtime_ns
            os.utime(tree / name, ns=(before + 10**9, before + 10**9))
            cases[f"a changed {name} misses them"] = stored_graph(text, graph_key(tree, str(ninja), b"script")) is None
            os.utime(tree / name, ns=(before, before))
        cases["put back, every file meets them again"] = stored_graph(text, graph_key(tree, str(ninja), b"script")) is not None
    # --fingerprints compares a fresh reading with the stored answers (decision
    # 521). On a real tree a mutant of this script never meets stored answers
    # -- its own text is in their key -- so the wiring is held here, by a
    # reader that says whether it was asked to read them.
    class Reader:
        def __init__(self, tree, stored=True):
            self.asked_ninja, self.from_stored = (0, True) if stored else (5, False)

        def fingerprints(self, spec):
            return {"ctest t": "0123"}

        def save_graph(self):
            pass

    with contextlib.redirect_stdout(io.StringIO()):
        cases["--fingerprints takes one reading that asks Ninja itself"] = (
            main_fingerprints(pathlib.Path("."), "m1-17.json", Reader) == 0)
    return cases


# The stand-in tree's compiled shaders, built from these sources of this
# repository -- named, never read.
STAND_IN_SHADERS = ("a.frag", "b.frag")


def run_tests_module():
    """scripts/run-tests.py, loaded, for its own reading of its store."""
    sys.dont_write_bytecode = True  # no __pycache__ in scripts/
    loader = importlib.util.spec_from_file_location("run_tests", RUN_TESTS)
    module = importlib.util.module_from_spec(loader)
    loader.loader.exec_module(module)
    return module


def stand_in_tree(folder: pathlib.Path, tests: dict) -> "Tree":
    """A reading of a made-up tree in `folder`, its Ninja answers given rather
    than asked for (M1-131): the shader step building `shaders/<name>.spv` of
    each of STAND_IN_SHADERS, from that source under this repository's
    shaders/, and the application, made here, built from nothing; `tests` its
    ctest entries. Read again, by another call, it hashes everything again."""
    application = folder / (APPLICATION + (".exe" if os.name == "nt" else ""))
    if not application.exists():
        application.parent.mkdir(parents=True, exist_ok=True)
        application.write_bytes(b"the application")
        if os.name != "nt":
            application.chmod(0o755)
    reading = Tree.__new__(Tree)
    reading.tree, reading.tests, reading.ninja = folder, tests, "ninja"
    reading.asked_ninja, reading._learned, reading.from_stored = 0, False, True
    nodes = [f"shaders/{name}.spv" for name in STAND_IN_SHADERS]
    for node in nodes:
        if not (folder / node).exists():
            (folder / node).parent.mkdir(parents=True, exist_ok=True)
            (folder / node).write_bytes(f"compiled {node}".encode("utf-8"))
    target = reading.program_target(APPLICATION)
    reading._built = {reading.key(name): name for name in nodes + [SHADER_STEP, target]}
    reading._edges = {reading.key(SHADER_STEP): nodes}
    reading._walks = {node: ({norm(ROOT / "shaders" / pathlib.Path(node).stem)}, [node]) for node in nodes}
    reading._walks[target] = (set(), [target])
    reading._commands = {target: "link the application\n"}
    reading._headers_found = {}
    reading._fingerprints, reading._tracked, reading._hashes = {}, set(), {}
    reading._trusted, reading._application_hash, reading._run_tests_store = {}, None, None
    return reading


def m1_131_cases() -> dict:
    """M1-131's rules for the shaders a judge loads (decisions 616, 629, 635 and
    643): the test driver's stored list while the application is what it was,
    otherwise a probe's sidecar, otherwise every shader; each judged by its
    compiled file's bytes, the shader a file's own mutants change included;
    and a shader source supposed changed, its compiled file changed."""
    cases = {}
    module = run_tests_module()
    with tempfile.TemporaryDirectory() as scratch:
        tree = pathlib.Path(scratch).resolve() / "build"
        application = tree / (APPLICATION + (".exe" if os.name == "nt" else ""))
        out = tree / "probes"
        probe = {"name": "probe", "command": ["cmake", f"-DORBSIM={application}", "-DPROBE=x", f"-DOUT={out}",
                                              "-P", "run.cmake"]}
        script = {"name": "script", "command": ["cmake", "-P", "run.cmake"]}
        tests = {"probe": probe, "script": script}
        reading = stand_in_tree(tree, tests)
        a, b = (norm(tree / f"shaders/{name}.spv") for name in STAND_IN_SHADERS)
        source_a, source_b = (norm(ROOT / "shaders" / name) for name in STAND_IN_SHADERS)

        def store(entries: dict) -> None:
            (tree / module.STATE).mkdir(parents=True, exist_ok=True)
            (tree / module.STATE / module.STORE).write_bytes(json.dumps(
                {"version": module.STORE_VERSION, "tests": entries, "listeners": {}}).encode("utf-8"))

        def shaders(test) -> list:
            return stand_in_tree(tree, tests).judge_shaders(test)

        built = hashlib.sha256(application.read_bytes()).hexdigest()
        store({"probe": {"shaders": [a], "application": built}})
        cases["a judge's shaders are the list the test driver stored, while the application is what it was"] = (
            shaders(probe) == [a])
        application.write_bytes(b"the application, rebuilt")
        out.mkdir(parents=True, exist_ok=True)
        (out / "x.txt").write_bytes(b"outcome = rendered\nshaders = b.frag.spv\n")
        cases["an application of other bytes since: the probe's sidecar"] = shaders(probe) == [b]
        (out / "x.txt").unlink()
        cases["neither: every shader"] = shaders(probe) == [a, b]
        built = hashlib.sha256(application.read_bytes()).hexdigest()
        store({"probe": {"shaders": [norm(tree / "elsewhere" / "a.frag.spv")], "application": built}})
        cases["a stored list naming a file no build step makes counts every shader"] = shaders(probe) == [a, b]
        cases["an entry that does not name the application loads no shader"] = shaders(script) == []
        store({"probe": {"shaders": [a], "application": built}, "script": {"shaders": [b], "application": built}})
        cases["unless the test driver stored a list for it: a script that starts the application"] = (
            shaders(script) == [b])

        judged_by_probe = {"mutants": [{"file": "src/x.cpp", "ctest": ["probe"]}]}
        before = stand_in_tree(tree, tests).fingerprint_parts(judged_by_probe)
        cases["a judge's fingerprint carries each compiled shader's hash, and no shader builds part"] = (
            sorted(before["ctest probe"]) == ["compiled shader a.frag.spv", "definition"])
        (tree / "shaders" / "a.frag.spv").write_bytes((tree / "shaders" / "a.frag.spv").read_bytes())
        cases["a shader compiled to the same bytes leaves it as it was"] = (
            stand_in_tree(tree, tests).fingerprint_parts(judged_by_probe) == before)
        (tree / "shaders" / "a.frag.spv").write_bytes(b"compiled to other bytes")
        after = stand_in_tree(tree, tests).fingerprint_parts(judged_by_probe)
        cases["other bytes move that shader's part alone"] = [
            (key, part) for key in before for part in before[key]
            if before[key][part] != after[key].get(part)] == [("ctest probe", "compiled shader a.frag.spv")]
        inputs = reading.judge_inputs(judged_by_probe)
        cases["a judge's inputs hold the compiled file it loads, not the shader's source"] = (
            a in inputs and source_a not in inputs and b not in inputs)

        cases["a shader's source is found to its compiled file through the graph"] = (
            reading.compiled_from({source_a}) == {a} and reading.compiled_from({norm(ROOT / "src/x.cpp")}) == set())
        supposed = supposed_changes({source_a}, {source_a}, None, reading.compiled_from)
        cases["a shader's source supposed changed is its compiled file changed"] = (
            is_due(supposed, inputs, True)[0]
            and not is_due(supposed_changes({source_b}, {source_b}, None, reading.compiled_from), inputs, True)[0])
        cases["a shader's source changed, where no fingerprint can be compared, is its compiled file changed"] = (
            supposed_changes({source_a}, set(), None, reading.compiled_from) == {source_a, a})
        cases["where fingerprints are compared, only a supposed change is: the bytes judge a real one"] = (
            supposed_changes({source_a, source_b}, {source_b}, [], reading.compiled_from) == {source_a, source_b, b})

        cases["a file's own mutated shader is judged by its compiled file, a C++ file by its text"] = (
            reading.mutated_inputs("shaders/a.frag") == {a}
            and reading.mutated_inputs("src/x.cpp") == {norm(ROOT / "src/x.cpp")})
        own_shader = {"mutants": [{"file": "shaders/b.frag"}]}
        cases["and its compiled file's hash is among the file's fingerprints"] = (
            sorted(stand_in_tree(tree, tests).fingerprint_parts(own_shader).get("mutated shader shaders/b.frag", {}))
            == ["compiled shader b.frag.spv"])
    return cases


def option_values(args: list, flag: str) -> list:
    return [args[i + 1] for i, a in enumerate(args) if a == flag and i + 1 < len(args)]


def main() -> int:
    args = sys.argv[1:]
    if args == ["--self-test"]:
        return self_test()
    if not args:
        print(__doc__)
        return 2
    tree = pathlib.Path(args[0]).resolve()
    if len(args) == 3 and args[1] == "--fingerprints":
        return main_fingerprints(tree, args[2])
    if args[1:] == ["--gpu-identity"]:
        return main_gpu_identity(tree)
    assumed = option_values(args, "--assume-changed")
    # --only-assumed: ignore git and the records, and judge only the files named
    # with --assume-changed -- how the rule's precision is checked.
    expect_due = option_values(args, "--expect-due")
    expect_current = option_values(args, "--expect-current")
    unmet = option_values(args, "--expect-unmet")
    unmet_current = option_values(args, "--expect-unmet-current")
    if len(unmet) == 1 and not expect_due and not unmet_current:
        return main_expect_unmet(tree, assumed, unmet[0])
    if len(unmet_current) == 1 and not expect_current and not unmet:
        return main_expect_unmet(tree, assumed, unmet_current[0], "current")
    return main_check(tree, assumed, "--only-assumed" in args, expect_due, expect_current)


if __name__ == "__main__":
    sys.exit(main())

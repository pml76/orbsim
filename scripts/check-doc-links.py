#!/usr/bin/env python3
"""Fail if any document points at something that is not there.

Why this exists: a survey of 118 open-source repositories found 59% of them
carried at least one dead reference in their agent instructions -- a path or a
script the file still names and the repository no longer contains. The failure
is invisible from the inside, which is precisely the failure mode ADR 0005 and
VERIFICATION.md rule 21 are about: a step that has silently been doing nothing
looks exactly like a step that passes.

So this is wired into the `check` target rather than left as a good intention.

It checks four things across every Markdown file that is ours -- since
register decision 652, one git does not ignore, outside the folders SKIP_DIRS
and SKIP_PREFIXES name:

  * a relative link resolves to a file or directory that exists;
  * an `#anchor` resolves to a heading that exists, in this file or the one
    the link names;
  * a **backticked path** that names a file in this repository resolves;
  * a **backticked `file:line` citation** names a file that exists and a line
    it actually has.

The third was added on 2026-09-09, because the first two could not see the
thing they were written to catch. `.claude/rules/physics-tests.md` sent readers
to `tests/TestHarness.hpp` for a day after M1-01 deleted it -- in a file the
harness loads automatically whenever a test is touched -- and the link checker
was blind to it because the reference was in backticks rather than in a link.

The fourth was added on 2026-09-13 for the same reason, one level further out.
Four documents cited a line number -- `test_orbit.cpp:414`, `Orbit.cpp:40`,
`Orbit.cpp:189`, `VulkanContext.cpp:474` -- and **all four were wrong**, every
one pointing at unrelated code the file had grown past. The path scan could not
see them, because a span containing a colon does not match a path. A line
number is a reference that rots on every edit above it, so it is the one kind
this project should either check or not write.

What this cannot check is whether the line still says what the prose claims;
it checks only that the file exists and is long enough. That is the cheap half,
and it is the half that catches a citation into a file that has since shrunk.
The expensive half is why the four were rewritten to name the symbol instead.

Note the deliberate asymmetry with the path scan: a `file:line` citation is
resolved **by basename as well**, searching the repository for a unique match,
because none of the four wrote a directory. A rule that only accepted
`tests/test_orbit.cpp:414` would have caught none of them -- which would have
made this check decoration, the exact failure the docstring above is about.
Ambiguous basenames are skipped rather than reported, so a false positive
cannot come out of this.

Code spans and fenced code blocks are stripped before the link scan, because a
regex such as `.*[/\\\\](src|tests)[/\\\\].*` inside backticks otherwise reads
as a link. The path scan reads those same code spans deliberately, and skips
fenced blocks, which hold shell commands naming build output.

Three rules keep the path scan honest, and each costs some coverage on purpose:

  * **Only paths with a file extension are checked**, so `src/view/` is not an
    error before that directory exists. A deleted directory is loud; a deleted
    file is not, and it is the one this check is for.
  * **Only paths whose first segment is a real directory of ours** are read
    as claims about this repository: a top-level one, or -- since M1-115 -- one
    beside the document, above it, or in a `src/` there, so `core/Units.hpp`
    and a measurement's `m1-111-bench-bytes/compare.py` are checked too.
    `vk_mem_alloc.h` and `framework/quantity.h` live in dependencies, and are
    nobody's promise. Until M1-115 only top-level folders counted, and 495
    paths -- all of them right on the day they were counted -- were never
    checked; a wrong one planted by hand passed.
  * **Task documents under `docs/plan/tasks/` are exempt**, because naming a
    file the task will create is their entire job. Everything that describes
    the project as it *is* -- the router, the guidelines, the ADRs, the
    register, the rules files, the READMEs -- is checked.

Usage: check-doc-links.py [repo-root]
Exit codes: 0 clean, 1 broken references found, 2 usage.
"""

from __future__ import annotations

import fnmatch
import os
import re
import subprocess
import sys
import tempfile

# Directories that are not ours: build output, fetched dependencies, git.
# Since register decision 652 only the files git does not ignore are read
# (`unignored`), and these are still passed over among them: a folder of this
# kind that no ignore rule names -- an untracked `node_modules`, a `buildx` --
# is no more ours for that.
SKIP_DIRS = {".git", ".idea", "_deps", "node_modules"}
SKIP_PREFIXES = ("build", "cmake-build")

FENCE = re.compile(r"^\s*(```|~~~)")
CODE_SPAN = re.compile(r"`[^`]*`")
CODE_SPAN_BODY = re.compile(r"`([^`]+)`")
# [text](target) -- target runs to the first whitespace or closing paren, so a
# link carrying a title, [x](y "t"), still yields y.
LINK = re.compile(r"\[[^\]]*\]\(\s*([^)\s]+)")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")

# A repository path in backticks: conservative charset, and a file extension,
# for the reason in the docstring.
CODE_PATH = re.compile(r"^[A-Za-z0-9_.][A-Za-z0-9_./-]*\.[A-Za-z0-9]{1,6}$")
# `docs/adr/0007` is how the whole tree refers to a record, so it is checked as
# a record number rather than as a filename. A wrong number in the router is
# exactly the rot this script exists to find.
ADR_REF = re.compile(r"^docs/adr/(\d{4})$")

# `Orbit.cpp:40`, `tests/test_orbit.cpp:414`, `main.cpp:12:3`. The extension list
# is closed on purpose: `1.8:2` and `J2000:1` are not citations, and a bare
# `foo.md:3` in prose is more likely a quotation than a reference.
CODE_LINE_REF = re.compile(
    r"^(?P<path>[A-Za-z0-9_.][A-Za-z0-9_./-]*\.(?:cpp|hpp|h|c|py|frag|vert|comp|glsl|txt|json|cmake))"
    r":(?P<line>\d{1,7})(?::\d{1,7})?$"
)
# Where a bare basename may live. Deliberately not the whole repository: a
# citation into build output or a dependency is nobody's promise, and `data/`
# holds files a fresh clone does not have.
CODE_LINE_SEARCH_DIRS = ("src", "tests", "shaders", "scripts", "cmake", "coding-guidelines-example")

# Documents whose job is to describe work not yet done.
FORWARD_LOOKING = ("docs/plan/tasks/",)

# Paths a document names on purpose although they are not there, each with the
# reason. The list is meant to shrink, and an entry exempts the path only in
# the document named -- the same name anywhere else is still an error. Since
# M1-115 an entry is itself reported once the file exists or the document no
# longer names it, so the list shrinks without anyone remembering to.
ABSENT_ON_PURPOSE = {
    # Names the deleted hand-rolled harness in order to say that it is gone.
    (".claude/rules/physics-tests.md", "tests/TestHarness.hpp"),
    # History: what M1-01 deleted, recorded as history.
    ("docs/HISTORY.md", "tests/TestHarness.hpp"),
    # Register decision 90 names the layer-wide header it chose not to write.
    ("docs/plan/milestone-1-decisions.md", "astro/AstroError.hpp"),
    # Register decision 329 names the header M1-49 will create; this entry is
    # reported as no longer needed once it exists.
    ("docs/plan/milestone-1-decisions.md", "view/Ellipsoid.hpp"),
}

# `file:line` citations a document makes on purpose, each with its reason. The
# convention since 2026-09-13 is to name the symbol instead, so this list is
# meant to stay empty; an entry is exempt only in the document named, and is
# still checked for the file existing and the line being in range.
LINE_REFS_ON_PURPOSE: set[tuple[str, str]] = set()


def is_ours(root: str, path: str) -> bool:
    rel = os.path.relpath(path, root).replace("\\", "/")
    parts = rel.split("/")
    return not any(
        p in SKIP_DIRS or p.lower().startswith(SKIP_PREFIXES) for p in parts[:-1]
    )


def unignored(root: str) -> list[str]:
    """Every file under `root` git does not ignore -- those it tracks, and the
    untracked ones it does not ignore -- as scripts/run-tests.py reads a folder
    (register decision 652). Until 2026-10-18 this script walked every folder
    but SKIP_DIRS and SKIP_PREFIXES, and so read the documents of a session's
    git worktree under `.claude/worktrees/`, which git's exclude file ignores:
    79 broken references, all inside that copy, failing `check` in both trees
    though nothing of the project was broken. A folder skipped by name would
    have been broken again by the next one git ignores."""
    out = subprocess.run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                         cwd=root, capture_output=True, check=True).stdout
    listed = {os.path.normpath(os.path.join(root, n)) for n in out.decode("utf-8").split("\0") if n}
    # A tracked file deleted from the disk is still in the index.
    return sorted(p for p in listed if os.path.isfile(p))


class Listing:
    """The files under a root git does not ignore, and the folders holding
    them: what the documents are read from (register decision 652), and what
    a reference must name (decision 656), so that a document may point only at
    what every clone has. A folder counts when the list holds a file under it,
    since git keeps no folder of its own; and as every listed file is on the
    disk, so is everything this holds."""

    def __init__(self, root: str) -> None:
        self.files = unignored(root)
        self._files = {os.path.normcase(f) for f in self.files}
        self._folders = {os.path.normcase(root)}
        for path in self.files:
            folder = os.path.normcase(os.path.dirname(path))
            while folder not in self._folders and os.path.dirname(folder) != folder:
                self._folders.add(folder)
                folder = os.path.dirname(folder)

    def file(self, path: str) -> bool:
        return os.path.normcase(os.path.normpath(path)) in self._files

    def folder(self, path: str) -> bool:
        return os.path.normcase(os.path.normpath(path)) in self._folders

    def holds(self, path: str) -> bool:
        return self.file(path) or self.folder(path)


def markdown_files(files: list[str]) -> list[str]:
    return [f for f in files if f.lower().endswith(".md")]


def top_level_dirs(root: str, listing: Listing) -> set[str]:
    """Directory names at the repository root, which is what makes a backticked
    path a claim about *this* repository rather than about a dependency -- the
    ones holding a file git does not ignore (register decision 656)."""
    names = {os.path.relpath(f, root).replace("\\", "/").split("/")[0] for f in listing.files}
    return {
        name
        for name in names
        if listing.folder(os.path.join(root, name))
        and name not in SKIP_DIRS
        and not name.lower().startswith(SKIP_PREFIXES)
    }


def strip_code(lines: list[str]) -> list[str]:
    """Blank out fenced blocks and inline code spans, keeping line numbering."""
    out = []
    in_fence = False
    for line in lines:
        if FENCE.match(line):
            in_fence = not in_fence
            out.append("")
            continue
        out.append("" if in_fence else CODE_SPAN.sub("``", line))
    return out


def code_spans(lines: list[str]) -> list[tuple[int, str]]:
    """Every inline code span outside a fenced block, with its line number."""
    out = []
    in_fence = False
    for number, line in enumerate(lines, start=1):
        if FENCE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        out.extend((number, body) for body in CODE_SPAN_BODY.findall(line))
    return out


def anchors(lines: list[str]) -> set[str]:
    """GitHub's heading anchors: lowercase, drop punctuation, spaces to hyphens.

    Spaces are *not* collapsed, so an em dash between two words leaves a double
    hyphen -- which is what GitHub actually generates, and getting this wrong is
    how a table of contents silently stops working.
    """
    seen: dict[str, int] = {}
    result = set()
    in_fence = False
    for line in lines:
        if FENCE.match(line):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        match = HEADING.match(line)
        if not match:
            continue
        text = re.sub(r"[^\w\s-]", "", match.group(2).lower()).replace(" ", "-")
        count = seen.get(text, 0)
        seen[text] = count + 1
        result.add(text if count == 0 else f"{text}-{count}")
    return result


def search_bases(root: str, path: str) -> list[str]:
    """Where a repository-relative path may resolve from: the document's own
    directory and every ancestor up to the root. That is what lets the worked
    example's README name `tests/test_units.cpp` and mean its own."""
    bases = []
    current = os.path.dirname(path)
    while True:
        bases.append(current)
        if os.path.normcase(current) == os.path.normcase(root):
            return bases
        parent = os.path.dirname(current)
        if parent == current:
            return bases
        current = parent


def path_bases(root: str, path: str) -> list[str]:
    """Where a backticked path in this document may be written from: the
    document's own folder and every ancestor (search_bases), and the `src/`
    folder of each, since `core/Units.hpp` is how the tree names
    `src/core/Units.hpp` (M1-115). Until then only a path starting at a
    top-level folder was checked, and 495 that start elsewhere were not."""
    bases = search_bases(root, path)
    return bases + [os.path.join(b, "src") for b in bases if os.path.isdir(os.path.join(b, "src"))]


def line_count(path: str) -> int:
    with open(path, encoding="utf-8", errors="replace") as handle:
        return sum(1 for _ in handle)


def resolve_code_line_ref(root: str, target: str, listing: Listing) -> list[str]:
    """Every file a `file:line` citation could mean.

    A citation carrying a directory is resolved from the root, exactly as the
    path scan does. A bare basename is searched for under the directories that
    hold source, because that is how all four of the citations this check was
    written for were spelled -- among the files git does not ignore (register
    decision 652), since a copy in an ignored folder made the name ambiguous,
    and an ambiguous one is skipped. Either way the file is one git does not
    ignore (decision 656).
    """
    if "/" in target:
        candidate = os.path.join(root, target.replace("/", os.sep))
        return [candidate] if listing.file(candidate) else []
    found = []
    for base in CODE_LINE_SEARCH_DIRS:
        start = os.path.normcase(os.path.join(root, base)) + os.sep
        found += [
            f
            for f in listing.files
            if os.path.normcase(f).startswith(start) and os.path.basename(f) == target and is_ours(root, f)
        ]
    return found


def check_code_line_refs(root: str, rel: str, lines: list[str], listing: Listing) -> list[str]:
    """A backticked `file:line` citation is reported, because we do not write them.

    Existence-and-length was the obvious check and it was measured first: of the
    four citations in the tree on 2026-09-13, every one pointing at unrelated
    code, it caught **none** -- all four files had grown, not shrunk, so the
    cited line still existed. A check that cannot fail on the cases that
    motivated it is the decoration ADR 0005 is about.

    So the rule is the convention instead: name the symbol, not the line. A
    symbol reference is wrong loudly -- the reader searches and finds nothing --
    where a line number is wrong silently and plausibly. The file-exists and
    line-in-range tests are kept underneath, because they give a better message
    when an allow-listed citation does rot.
    """
    problems = []
    for number, body in code_spans(lines):
        match = CODE_LINE_REF.match(body.strip())
        if not match:
            continue
        target = match.group("path")
        cited = int(match.group("line"))
        if (rel, body.strip()) in LINE_REFS_ON_PURPOSE:
            candidates = resolve_code_line_ref(root, target, listing)
            if not candidates:
                problems.append(f"{rel}:{number}: no such file: {target}")
            elif len(candidates) == 1 and not 1 <= cited <= line_count(candidates[0]):
                problems.append(
                    f"{rel}:{number}: {target} has {line_count(candidates[0])} lines, "
                    f"so :{cited} does not exist"
                )
            continue
        problems.append(
            f"{rel}:{number}: cites a line number ({body.strip()}); "
            f"name the symbol instead -- a line number rots on every edit above it"
        )
    return problems


def check_code_paths(
    root: str,
    path: str,
    lines: list[str],
    roots: set[str],
    absent: set[tuple[str, str]],
    used: set[tuple[str, str]],
    listing: Listing,
) -> list[str]:
    """A backticked path naming a file in this repository must resolve."""
    rel = os.path.relpath(path, root).replace("\\", "/")
    # Line citations are checked everywhere, task documents included. The
    # FORWARD_LOOKING exemption below exists because naming a file a task will
    # create is that document's job -- which is no reason at all to cite a line
    # number, in a file that exists or one that does not.
    problems = check_code_line_refs(root, rel, lines, listing)
    if rel.startswith(FORWARD_LOOKING):
        return problems
    # A record is searched for among the files git does not ignore (register
    # decision 652), as `docs/adr/*` matched on the disk was until then.
    adr_folder = os.path.normcase(os.path.join(root, "docs", "adr"))
    records = [
        os.path.basename(f) for f in listing.files if os.path.normcase(os.path.dirname(f)) == adr_folder
    ]
    for number, body in code_spans(lines):
        target = body.rstrip("/")
        if "/" not in target:
            continue
        adr = ADR_REF.match(target)
        if adr:
            if not fnmatch.filter(records, adr.group(1) + "-*.md"):
                problems.append(f"{rel}:{number}: no ADR numbered {adr.group(1)}")
            continue
        if not CODE_PATH.match(target):
            continue
        # A claim about this repository when its first folder is a top-level
        # one, or a folder beside the document, above it, or in a `src/` there;
        # otherwise a dependency's or a build's, and nobody's promise. The
        # folder and the file are held to git's list (register decision 656),
        # so the answer is the one a fresh clone gives: there a folder holding
        # only what git ignores is not there at all. path_bases finds its
        # `src/` folders on the disk, which cannot change that answer, since
        # whatever is looked for under them is asked of the list.
        bases = path_bases(root, path)
        first = target.split("/")[0]
        if first not in roots and not any(listing.folder(os.path.join(b, first)) for b in bases):
            continue
        native = target.replace("/", os.sep)
        exists = any(listing.holds(os.path.join(b, native)) for b in bases)
        if (rel, target) in absent:
            used.add((rel, target))
            if exists:
                problems.append(f"{rel}:{number}: {target} exists now; remove its ABSENT_ON_PURPOSE entry")
            continue
        if not exists:
            problems.append(f"{rel}:{number}: no such file: {target}")
    return problems


def scan(root: str, absent: set[tuple[str, str]]) -> tuple[int, list[str]]:
    """Every document under `root`, checked: how many there were, and what is
    broken. `absent` is ABSENT_ON_PURPOSE, a parameter so the self-test can
    hand in its own."""
    listing = Listing(root)
    files = [f for f in markdown_files(listing.files) if is_ours(root, f)]
    roots = top_level_dirs(root, listing)
    cache: dict[str, set[str]] = {}
    problems: list[str] = []
    used: set[tuple[str, str]] = set()

    for path in files:
        with open(path, encoding="utf-8", errors="replace") as handle:
            lines = handle.read().split("\n")
        cache[os.path.normcase(path)] = anchors(lines)

    for path in files:
        with open(path, encoding="utf-8", errors="replace") as handle:
            raw = handle.read().split("\n")
        here = os.path.dirname(path)
        problems.extend(check_code_paths(root, path, raw, roots, absent, used, listing))
        for number, line in enumerate(strip_code(raw), start=1):
            for target in LINK.findall(line):
                if target.startswith(("http://", "https://", "mailto:", "<")):
                    continue
                file_part, _, anchor = target.partition("#")
                if file_part:
                    resolved = os.path.normpath(os.path.join(here, file_part))
                    # A file or folder git does not ignore (register decision
                    # 656), so that a document points only at what every clone has.
                    if not listing.holds(resolved):
                        problems.append(
                            f"{os.path.relpath(path, root)}:{number}: "
                            f"no such file: {file_part}"
                        )
                        continue
                else:
                    resolved = path
                if not anchor:
                    continue
                key = os.path.normcase(resolved)
                if key not in cache:
                    continue  # not one of ours; nothing to check the anchor against
                if anchor not in cache[key]:
                    problems.append(
                        f"{os.path.relpath(path, root)}:{number}: "
                        f"no such heading: #{anchor}"
                    )

    # An exception nobody needs any more is reported, so the list shrinks by
    # itself, as its comment asks (M1-115).
    for document, target in sorted(absent - used):
        problems.append(f"{document}: no longer names {target}; remove its ABSENT_ON_PURPOSE entry")
    return len(files), problems


def self_test() -> int:
    """Each rule shown passing and failing, on a small repository of its own in
    a temporary folder (M1-115, register decision 426): until then this script
    had no test, and its blind spot was found only by planting a wrong path by
    hand (M1-111)."""
    # One document, docs/guide.md, in a repository holding src/core/A.hpp and
    # docs/m/compare.py. Each case: its text, its exceptions, and how many
    # problems it must produce.
    cases = {
        "a path from the root that exists passes": ("`src/core/A.hpp`", set(), 0),
        "a path from the root that does not exist is reported": ("`src/core/B.hpp`", set(), 1),
        "a path from src/ that exists passes": ("`core/A.hpp`", set(), 0),
        "a path from src/ that does not exist is reported": ("`core/B.hpp`", set(), 1),
        "a path from the document's folder that exists passes": ("`m/compare.py`", set(), 0),
        "a path from the document's folder that does not exist is reported (M1-111's)":
            ("`m/compare2.py`", set(), 1),
        "a path into a dependency is nobody's promise": ("`framework/quantity.h`", set(), 0),
        "a path named absent on purpose passes":
            ("`core/B.hpp`", {("docs/guide.md", "core/B.hpp")}, 0),
        "an exception for a file that now exists is reported":
            ("`core/A.hpp`", {("docs/guide.md", "core/A.hpp")}, 1),
        "an exception the document no longer names is reported":
            ("nothing", {("docs/guide.md", "core/B.hpp")}, 1),
        "a link to a file that does not exist is reported": ("[x](nothere.md)", set(), 1),
    }
    failures = 0
    for name, (text, absent, want) in cases.items():
        with tempfile.TemporaryDirectory() as scratch:
            # A repository of git's own, since the script reads the files git
            # does not ignore (register decision 652); every file here is one
            # it does not ignore, as before.
            git_repository(scratch, {}, ())
            for made in ("src/core/A.hpp", "docs/m/compare.py"):
                os.makedirs(os.path.dirname(os.path.join(scratch, made)), exist_ok=True)
                with open(os.path.join(scratch, made), "w", encoding="utf-8") as handle:
                    handle.write("\n")
            with open(os.path.join(scratch, "docs", "guide.md"), "w", encoding="utf-8") as handle:
                handle.write(f"# Guide\n\nSee {text}.\n")
            _, problems = scan(scratch, absent)
        ok = len(problems) == want
        failures += not ok
        print(f"self-test: {name}: {'ok' if ok else f'WRONG, got {problems}'}")
    return 1 if failures + git_cases() else 0


def git_repository(scratch: str, files: dict[str, str], tracked: tuple[str, ...]) -> None:
    """A scratch repository of git's own holding `files`, of which `tracked`
    are added to its index -- without a commit, so that no identity is needed
    -- and the rest are left for git's ignore rules to sort."""
    subprocess.run(["git", "init", "-q"], cwd=scratch, capture_output=True, check=True)
    for made, text in files.items():
        full = os.path.join(scratch, made)
        os.makedirs(os.path.dirname(full) or scratch, exist_ok=True)
        with open(full, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
    if tracked:
        subprocess.run(["git", "add", "--", *tracked], cwd=scratch, capture_output=True, check=True)


def git_cases() -> int:
    """The files git ignores are passed over, and the ones it does not are
    still read (register decision 652). Found 2026-10-18: a session's git
    worktree under `.claude/worktrees/`, which git's exclude file ignores, put
    79 broken references in `check`, all inside that copy. Each case is a
    repository holding a folder git ignores, with a broken link in it, and a
    tracked document with a good one; since decision 656 some also hold a
    reference to what git ignores. Returns how many cases failed."""
    base = {
        ".gitignore": "ignored/\ndocs/adr/0002-draft.md\n",
        "ignored/copy.md": "# Copy\n\nSee [x](gone.md).\n",
        "guide.md": "# Guide\n\nSee [x](other.md).\n",
        "other.md": "# Other\n",
    }
    base_tracked = (".gitignore", "guide.md", "other.md")
    # Each case: the files it adds, those of them tracked, the citations it
    # allows on purpose, and the documents its problems must name.
    cases = {
        "a document in a folder git ignores is passed over": ({}, (), set(), []),
        "a broken link in a tracked document is still reported":
            ({"bad.md": "# Bad\n\nSee [x](gone.md).\n"}, ("bad.md",), set(), ["bad.md"]),
        "a broken link in an untracked document git does not ignore is still reported":
            ({"loose.md": "# Loose\n\nSee [x](gone.md).\n"}, (), set(), ["loose.md"]),
        "an ADR only a file git ignores holds is reported":
            ({"docs/adr/0001-kept.md": "# Kept\n", "docs/adr/0002-draft.md": "# Draft\n",
              "names.md": "# Names\n\n`docs/adr/0001` and `docs/adr/0002`.\n"},
             ("docs/adr/0001-kept.md", "names.md"), set(), ["names.md"]),
        # Ambiguous until the ignored copy is passed over, and an ambiguous
        # basename is skipped rather than reported -- so the citation's range
        # went unchecked.
        "a citation's file is searched for among the files git does not ignore":
            ({"src/A.cpp": "\n", "src/ignored/A.cpp": "\n", "cites.md": "# Cites\n\n`A.cpp:5`.\n"},
             ("src/A.cpp", "cites.md"), {("cites.md", "A.cpp:5")}, ["cites.md"]),
        # A reference names only what every clone has (register decision 656):
        # until then a file git ignores, present on one machine only, passed
        # there and would fail on a fresh clone.
        "a link to, and a path naming, a file git ignores are reported":
            ({".gitignore": base[".gitignore"] + "src/secret.hpp\n", "src/kept.hpp": "\n",
              "src/secret.hpp": "\n", "points.md": "# Points\n\nSee [x](src/secret.hpp) and `src/secret.hpp`.\n"},
             ("src/kept.hpp", "points.md"), set(), ["points.md", "points.md"]),
        "a link to a folder holding only files git ignores is reported":
            ({"folder.md": "# Folder\n\nSee [x](ignored/).\n"}, ("folder.md",), set(), ["folder.md"]),
        # As a fresh clone sees it: there the folder is not there at all.
        "a path into a folder holding only files git ignores is nobody's promise":
            ({"into.md": "# Into\n\n`ignored/thing.py`.\n"}, ("into.md",), set(), []),
        "a citation's file named with its folder must be one git does not ignore":
            ({".gitignore": base[".gitignore"] + "src/secret.hpp\n", "src/kept.hpp": "\n",
              "src/secret.hpp": "\n", "cites.md": "# Cites\n\n`src/secret.hpp:1`.\n"},
             ("src/kept.hpp", "cites.md"), {("cites.md", "src/secret.hpp:1")}, ["cites.md"]),
    }
    failures = 0
    for name, (files, tracked, line_refs, want) in cases.items():
        with tempfile.TemporaryDirectory() as scratch:
            git_repository(scratch, base | files, base_tracked + tracked)
            # LINE_REFS_ON_PURPOSE is read where it is used, and is meant to
            # stay empty; the case lends it its own entries and takes them back.
            LINE_REFS_ON_PURPOSE.update(line_refs)
            try:
                _, problems = scan(scratch, set())
            finally:
                LINE_REFS_ON_PURPOSE.difference_update(line_refs)
        named = sorted(p.split(":", 1)[0].replace("\\", "/") for p in problems)
        ok = named == want
        failures += not ok
        print(f"self-test: {name}: {'ok' if ok else f'WRONG, got {problems}'}")
    return failures


def main(argv: list[str]) -> int:
    if argv[1:] == ["--self-test"]:
        return self_test()
    if len(argv) > 2:
        print(__doc__.strip().splitlines()[-3], file=sys.stderr)
        return 2
    root = os.path.abspath(argv[1] if len(argv) == 2 else ".")
    count, problems = scan(root, ABSENT_ON_PURPOSE)
    for problem in sorted(problems):
        print(problem, file=sys.stderr)
    print(
        f"doc-links: {count} documents, {len(problems)} broken",
        file=sys.stderr if problems else sys.stdout,
    )
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

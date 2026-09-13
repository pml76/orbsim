#!/usr/bin/env python3
"""Fail if any document points at something that is not there.

Why this exists: a survey of 118 open-source repositories found 59% of them
carried at least one dead reference in their agent instructions -- a path or a
script the file still names and the repository no longer contains. The failure
is invisible from the inside, which is precisely the failure mode ADR 0005 and
VERIFICATION.md rule 21 are about: a step that has silently been doing nothing
looks exactly like a step that passes.

So this is wired into the `check` target rather than left as a good intention.

It checks four things across every Markdown file that is ours:

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
  * **Only paths whose first segment is a real top-level directory** are read
    as claims about this repository. `vk_mem_alloc.h` and `elv_io.cpp` live in
    a dependency and in a reference clone, and are nobody's promise.
  * **Task documents under `docs/plan/tasks/` are exempt**, because naming a
    file the task will create is their entire job. Everything that describes
    the project as it *is* -- the router, the guidelines, the ADRs, the
    register, the rules files, the READMEs -- is checked.

Usage: check-doc-links.py [repo-root]
Exit codes: 0 clean, 1 broken references found, 2 usage.
"""

from __future__ import annotations

import glob
import os
import re
import sys

# Directories that are not ours: build output, fetched dependencies, git.
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
# the document named -- the same name anywhere else is still an error.
ABSENT_ON_PURPOSE = {
    # Names the deleted hand-rolled harness in order to say that it is gone.
    (".claude/rules/physics-tests.md", "tests/TestHarness.hpp"),
    # History: what M1-01 deleted, recorded as history.
    ("docs/HISTORY.md", "tests/TestHarness.hpp"),
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


def markdown_files(root: str) -> list[str]:
    found = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [
            d
            for d in dirnames
            if d not in SKIP_DIRS and not d.lower().startswith(SKIP_PREFIXES)
        ]
        for name in filenames:
            if name.lower().endswith(".md"):
                found.append(os.path.join(dirpath, name))
    return sorted(found)


def top_level_dirs(root: str) -> set[str]:
    """Directory names at the repository root, which is what makes a backticked
    path a claim about *this* repository rather than about a dependency."""
    return {
        name
        for name in os.listdir(root)
        if os.path.isdir(os.path.join(root, name))
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


def line_count(path: str) -> int:
    with open(path, encoding="utf-8", errors="replace") as handle:
        return sum(1 for _ in handle)


def resolve_code_line_ref(root: str, target: str) -> list[str]:
    """Every file a `file:line` citation could mean.

    A citation carrying a directory is resolved from the root, exactly as the
    path scan does. A bare basename is searched for under the directories that
    hold source, because that is how all four of the citations this check was
    written for were spelled.
    """
    if "/" in target:
        candidate = os.path.join(root, target.replace("/", os.sep))
        return [candidate] if os.path.isfile(candidate) else []
    found = []
    for base in CODE_LINE_SEARCH_DIRS:
        start = os.path.join(root, base)
        if not os.path.isdir(start):
            continue
        for dirpath, dirnames, filenames in os.walk(start):
            dirnames[:] = [
                d
                for d in dirnames
                if d not in SKIP_DIRS and not d.lower().startswith(SKIP_PREFIXES)
            ]
            if target in filenames:
                found.append(os.path.join(dirpath, target))
    return found


def check_code_line_refs(root: str, rel: str, lines: list[str]) -> list[str]:
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
            candidates = resolve_code_line_ref(root, target)
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


def check_code_paths(root: str, path: str, lines: list[str], roots: set[str]) -> list[str]:
    """A backticked path naming a file in this repository must resolve."""
    rel = os.path.relpath(path, root).replace("\\", "/")
    # Line citations are checked everywhere, task documents included. The
    # FORWARD_LOOKING exemption below exists because naming a file a task will
    # create is that document's job -- which is no reason at all to cite a line
    # number, in a file that exists or one that does not.
    problems = check_code_line_refs(root, rel, lines)
    if rel.startswith(FORWARD_LOOKING):
        return problems
    for number, body in code_spans(lines):
        target = body.rstrip("/")
        if "/" not in target:
            continue
        adr = ADR_REF.match(target)
        if adr:
            if not glob.glob(os.path.join(root, "docs", "adr", adr.group(1) + "-*.md")):
                problems.append(f"{rel}:{number}: no ADR numbered {adr.group(1)}")
            continue
        if not CODE_PATH.match(target):
            continue
        if target.split("/")[0] not in roots:
            continue
        if (rel, target) in ABSENT_ON_PURPOSE:
            continue
        native = target.replace("/", os.sep)
        if not any(os.path.exists(os.path.join(b, native)) for b in search_bases(root, path)):
            problems.append(f"{rel}:{number}: no such file: {target}")
    return problems


def main(argv: list[str]) -> int:
    if len(argv) > 2:
        print(__doc__.strip().splitlines()[-3], file=sys.stderr)
        return 2
    root = os.path.abspath(argv[1] if len(argv) == 2 else ".")

    files = [f for f in markdown_files(root) if is_ours(root, f)]
    roots = top_level_dirs(root)
    cache: dict[str, set[str]] = {}
    problems: list[str] = []

    for path in files:
        with open(path, encoding="utf-8", errors="replace") as handle:
            lines = handle.read().split("\n")
        cache[os.path.normcase(path)] = anchors(lines)

    for path in files:
        with open(path, encoding="utf-8", errors="replace") as handle:
            raw = handle.read().split("\n")
        here = os.path.dirname(path)
        problems.extend(check_code_paths(root, path, raw, roots))
        for number, line in enumerate(strip_code(raw), start=1):
            for target in LINK.findall(line):
                if target.startswith(("http://", "https://", "mailto:", "<")):
                    continue
                file_part, _, anchor = target.partition("#")
                if file_part:
                    resolved = os.path.normpath(os.path.join(here, file_part))
                    if not os.path.exists(resolved):
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

    for problem in sorted(problems):
        print(problem, file=sys.stderr)
    print(
        f"doc-links: {len(files)} documents, {len(problems)} broken",
        file=sys.stderr if problems else sys.stdout,
    )
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

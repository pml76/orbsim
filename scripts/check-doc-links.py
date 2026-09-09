#!/usr/bin/env python3
"""Fail if any document links to something that is not there.

Why this exists: a survey of 118 open-source repositories found 59% of them
carried at least one dead reference in their agent instructions -- a path or a
script the file still names and the repository no longer contains. The failure
is invisible from the inside, which is precisely the failure mode ADR 0005 and
VERIFICATION.md rule 21 are about: a step that has silently been doing nothing
looks exactly like a step that passes.

So this is wired into the `check` target rather than left as a good intention.

It checks two things across every Markdown file that is ours:

  * a relative link resolves to a file or directory that exists;
  * an `#anchor` resolves to a heading that exists, in this file or the one
    the link names.

Code spans and fenced code blocks are stripped first, because a regex such as
`.*[/\\\\](src|tests)[/\\\\].*` inside backticks otherwise reads as a link.

Usage: check-doc-links.py [repo-root]
Exit codes: 0 clean, 1 broken references found, 2 usage.
"""

from __future__ import annotations

import os
import re
import sys

# Directories that are not ours: build output, fetched dependencies, git.
SKIP_DIRS = {".git", ".idea", "_deps", "node_modules"}
SKIP_PREFIXES = ("build", "cmake-build")

FENCE = re.compile(r"^\s*(```|~~~)")
CODE_SPAN = re.compile(r"`[^`]*`")
# [text](target) -- target runs to the first whitespace or closing paren, so a
# link carrying a title, [x](y "t"), still yields y.
LINK = re.compile(r"\[[^\]]*\]\(\s*([^)\s]+)")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")


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


def main(argv: list[str]) -> int:
    if len(argv) > 2:
        print(__doc__.strip().splitlines()[-3], file=sys.stderr)
        return 2
    root = os.path.abspath(argv[1] if len(argv) == 2 else ".")

    files = [f for f in markdown_files(root) if is_ours(root, f)]
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

    for problem in problems:
        print(problem, file=sys.stderr)
    print(
        f"doc-links: {len(files)} documents, {len(problems)} broken",
        file=sys.stderr if problems else sys.stdout,
    )
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
"""Claude Code PostToolUse hook: clang-format the C++ file that was just edited.

Wired up in .claude/settings.json. Claude Code hands the tool call to this
script as JSON on stdin; if it names a .cpp or .hpp under src/ or tests/ (of
this project or of the guidelines example), the file is formatted in place.
That turns "clang-format leaves the tree unchanged" from a checklist item
into something that cannot be forgotten, because it happens before anyone
has a chance to forget it.

The hook never blocks an edit: a missing clang-format is reported on stderr
and the edit stands. The `check` target is where formatting becomes a
failure.
"""

import json
import re
import subprocess
import sys

FORMATTED = re.compile(r"/(src|tests)/.*\.(cpp|hpp)$")


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except json.JSONDecodeError as error:
        print(f"clang-format hook: unreadable input: {error}", file=sys.stderr)
        return 0

    tool_input = payload.get("tool_input") or {}
    tool_response = payload.get("tool_response") or {}
    path = tool_input.get("file_path") or tool_response.get("filePath")
    if not path:
        return 0

    if not FORMATTED.search(path.replace("\\", "/")):
        return 0

    try:
        subprocess.run(["clang-format", "-i", path], check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"clang-format hook: {error}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
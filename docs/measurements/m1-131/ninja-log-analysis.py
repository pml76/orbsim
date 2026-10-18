"""M1-131 item 4: what one Ninja run spent, from the tree's .ninja_log.

    python -I ninja-log-analysis.py <tree> <log before> <log after> [--targets N]

<log before> and <log after> are copies of <tree>/.ninja_log taken around the
run (measure-build.py keeps them). The run's entries are the bytes the after
copy has beyond the before copy; the script refuses if the before copy is not
a prefix of the after copy (Ninja recompacted the log), rather than guess.

Each entry is "start ms, end ms, mtime, output, command hash" (Ninja log v6),
the times relative to that Ninja's start. One edge with several outputs (a
link and its PDB and import library; a custom command named both relatively
and absolutely) writes one entry per output with the same times and hash, so
entries are grouped by (start, end, hash) into edges, and each edge counted
once.

An edge is a compile when one of its outputs is an "output" of the tree's
compile_commands.json; the compile is then the project's own when its source
is under the repository's src/ or tests/, a header self-check when its source
is one of the tree's generated selfcheck/ files, someone else's code in a
top-level target when its object is under CMakeFiles/ otherwise (orbsim_erfa,
orbsim_lodepng, orbsim_stb_image, vma_impl), and a dependency's when its object
is under _deps/. Other edges: the C++ module scans (.ddi, .dd), links (.exe,
.dll, .lib), lint stamps (lint/), and everything else.
"""

import collections
import json
import sys
from pathlib import Path

REPO = Path(r"C:\Users\U439644\Projects\untitled")


def slash(path: str) -> str:
    return path.replace("\\", "/")


def relative(tree: str, output: str) -> str:
    output = slash(output)
    prefix = slash(tree).rstrip("/") + "/"
    return output[len(prefix):] if output.lower().startswith(prefix.lower()) else output


def new_entries(before: Path, after: Path) -> list:
    old, new = before.read_bytes(), after.read_bytes()
    if not new.startswith(old):
        raise SystemExit(f"{after} does not begin with {before}: Ninja recompacted the log; not guessing")
    rows = []
    for line in new[len(old):].decode("utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        start, end, mtime, output, command = line.split("\t")
        rows.append((int(start), int(end), output, command))
    return rows


def compile_map(tree: str) -> dict:
    """{object relative to the tree: source, forward slashes}."""
    entries = json.loads((Path(tree) / "compile_commands.json").read_text(encoding="utf-8"))
    return {relative(tree, entry["output"]): slash(entry["file"]) for entry in entries}


def kind_of(tree: str, outputs: list, compiles: dict) -> tuple:
    """(kind, target) of one edge."""
    repo, tree_slash = slash(str(REPO)).lower() + "/", slash(tree).lower().rstrip("/") + "/"
    for output in outputs:
        if output in compiles:
            source = compiles[output].lower()
            parts = output.split("/")
            if parts[0] == "_deps":
                return "compile: a dependency's (_deps/)", "/".join(parts[:2])
            target = parts[1].removesuffix(".dir") if parts[0] == "CMakeFiles" else parts[0]
            if source.startswith(repo + "src/") or source.startswith(repo + "tests/"):
                return "compile: the project's own (src/, tests/)", target
            if source.startswith(tree_slash + "selfcheck/"):
                return "compile: header self-checks (generated)", target
            return "compile: someone else's code in a top-level target", target
    first = outputs[0]
    if any(o.endswith(".obj") or o.endswith(".o") for o in outputs):
        # An object compile_commands.json does not list (seen only for an
        # output an older configure named): reported apart, never guessed.
        return "compile: not in compile_commands.json", first
    if any(o.endswith(".ddi") or o.endswith(".dd") for o in outputs):
        return "C++ module scan (.ddi, .dd)", first.split("/")[1] if "/" in first else first
    if any(o.endswith((".exe", ".dll", ".lib")) for o in outputs):
        where = "a dependency's (_deps/)" if first.startswith("_deps/") else "top-level"
        what = ("link of a program (.exe)" if any(o.endswith(".exe") for o in outputs)
                else "link of a DLL" if any(o.endswith(".dll") for o in outputs)
                else "archive of a static library (.lib)")
        return f"{what}: {where}", first
    if first.startswith("lint/"):
        return "lint (clang-tidy, --verify-config)", first
    return "other (custom commands, CMake re-run, run-tests)", first


def main(argv: list) -> int:
    tree, before, after = argv[1], Path(argv[2]), Path(argv[3])
    top = int(argv[argv.index("--targets") + 1]) if "--targets" in argv else 12
    rows = new_entries(before, after)
    edges = collections.defaultdict(list)
    for start, end, output, command in rows:
        edges[(start, end, command)].append(relative(tree, output))
    compiles = compile_map(tree)
    by_kind = collections.defaultdict(lambda: [0, 0])
    by_target = collections.defaultdict(lambda: [0, 0])
    longest = collections.defaultdict(list)
    for (start, end, _), outputs in edges.items():
        kind, target = kind_of(tree, outputs, compiles)
        by_kind[kind][0] += 1
        by_kind[kind][1] += end - start
        if kind.startswith("compile"):
            by_target[(kind, target)][0] += 1
            by_target[(kind, target)][1] += end - start
        longest[kind].append((end - start, outputs[0]))
    first = min(start for start, _, _ in edges)
    last = max(end for _, end, _ in edges)
    print(f"entries {len(rows)}, edges {len(edges)}")
    print(f"wall time from the log: {(last - first) / 1000:.1f} s (first start {first} ms, last end {last} ms)")
    print()
    print(f"{'kind':68} {'edges':>6} {'summed s':>10}")
    for kind in sorted(by_kind):
        count, total = by_kind[kind]
        print(f"{kind:68} {count:6} {total / 1000:10.1f}")
    total_all = sum(t for _, t in by_kind.values())
    compile_all = sum(t for k, (_, t) in by_kind.items() if k.startswith("compile"))
    print(f"{'all edges':68} {len(edges):6} {total_all / 1000:10.1f}")
    print(f"{'all compiles':68} {sum(c for k, (c, _) in by_kind.items() if k.startswith('compile')):6}"
          f" {compile_all / 1000:10.1f}")
    print()
    print(f"compiles by target, the {top} largest summed times:")
    for (kind, target), (count, total) in sorted(by_target.items(), key=lambda kv: -kv[1][1])[:top]:
        print(f"  {target:44} {count:5} {total / 1000:9.1f} s   {kind.removeprefix('compile: ')}")
    print()
    for kind in sorted(longest):
        worst = sorted(longest[kind], reverse=True)[:3]
        print(f"longest in {kind}: " + "; ".join(f"{ms / 1000:.1f} s {name}" for ms, name in worst))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

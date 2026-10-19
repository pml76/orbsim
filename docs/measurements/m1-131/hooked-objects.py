"""M1-131 item 4: do the "hooked" class of compare-builds.py and the objects
that carry the hook agree?

    python -I hooked-objects.py <tree>

With the hook on, reads the tree's compile_commands.json and, for each
object, whether its command holds -fsanitize-coverage= and whether the object
file holds a section .SCOVP$M (the hook's address table). Prints the objects
compare-builds.py calls "hooked" and "only with the hook", set against the
two, and every object where the command and the file disagree.
"""

import importlib.util
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def load(name: str):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), HERE / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main(argv: list) -> int:
    tree = Path(argv[1])
    compare = load("compare-builds")
    database = json.loads((tree / "compile_commands.json").read_text(encoding="utf-8"))
    rows = []
    prefix = tree.as_posix().rstrip("/") + "/"
    for entry in database:
        # Relative to the tree, as Ninja's log names an output.
        output = entry["output"].replace("\\", "/")
        if output.lower().startswith(prefix.lower()):
            output = output[len(prefix):]
        obj = Path(entry["output"])
        if not obj.is_absolute():
            obj = Path(entry["directory"]) / obj
        command = entry.get("command", " ".join(entry.get("arguments", [])))
        flagged = "-fsanitize-coverage=" in command
        table = obj.is_file() and b".SCOVP$M" in obj.read_bytes()
        rows.append((compare.classify([output]), flagged, table, output))
    for kind in sorted({r[0] for r in rows}):
        mine = [r for r in rows if r[0] == kind]
        print(f"{kind}: {len(mine)} objects; with the flag {sum(r[1] for r in mine)}; "
              f"with the table {sum(r[2] for r in mine)}")
    print("the flag and the table disagree:")
    for kind, flagged, table, output in rows:
        if flagged != table:
            print(f"  {output}: flag {flagged}, table {table} ({kind})")
    print("with the flag, outside the class 'hooked':")
    for kind, flagged, table, output in rows:
        if flagged and kind != "hooked":
            print(f"  {output} ({kind})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

"""M1-132 step 2: faults planted in copies of scripts/compiled-code.py (VERIFICATION.md rule 23).

    python -I docs/measurements/m1-132/plant-faults.py <empty output folder>

Run from the root of the repository. For each fault, the script writes a copy
of scripts/compiled-code.py with that one change into the output folder, runs
the copy with --self-test, and prints the cases that report WRONG. The
file in scripts/ does not change. Each fault must make one case or more report
WRONG. key-agreement.py --compiled uses the copy `compiled-code-static-by-name.py`.
"""

import subprocess
import sys
from pathlib import Path

# Each fault: (the text to find, one time only, the text that replaces it).
FAULTS = {
    # The ??_E and ??_G pair accepted, the text after the prefix not compared.
    "pair-without-rest": (
        "scalar.name.startswith(SCALAR_DELETING)\n                and vector.name[rest:] == scalar.name[rest:]):",
        "scalar.name.startswith(SCALAR_DELETING)):"),
    # A static symbol keyed by its name only.
    "static-by-name": (
        "    return function_key(object_key(tree, path, symbol.object if symbol.library else None), symbol.name, "
        "COFF_STATIC)",
        "    return symbol.name"),
    # The number of entries from SizeOfRawData, at offset 16.
    "size-of-raw-data": ("SECTION_VIRTUAL_SIZE = 8\n", "SECTION_VIRTUAL_SIZE = 16\n"),
    # An address with no name passed.
    "no-name-passed": (
        "        names = index.get(address, [])\n",
        "        names = index.get(address, [])\n        if not names:\n            continue\n"),
}


def main(argv: list) -> int:
    out = Path(argv[1])
    out.mkdir(parents=True, exist_ok=True)
    text = (Path.cwd() / "scripts" / "compiled-code.py").read_text(encoding="utf-8")
    for name, (find, replace) in FAULTS.items():
        if text.count(find) != 1:
            print(f"{name}: the text to find occurs {text.count(find)} times, not one")
            continue
        planted = out / f"compiled-code-{name}.py"
        planted.write_bytes(text.replace(find, replace).encode("utf-8"))
        done = subprocess.run([sys.executable, "-I", str(planted), "--self-test"], capture_output=True, text=True)
        wrong = [line for line in done.stdout.splitlines() if "WRONG" in line]
        print(f"{name}: exit {done.returncode}, {len(wrong)} cases WRONG")
        for line in wrong:
            print(f"    {line[:200]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

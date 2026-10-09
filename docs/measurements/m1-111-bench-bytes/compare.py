"""Compare two benchmark runs' files, apart from the timing figures (M1-111).

    python compare.py <before dir> <before stdout> <after dir> <after stdout>

Each directory holds the one .txt and one .csv a run of
`orbsim --bench grid-orbit --frames 30 --validate --bench-out <dir>` wrote, and
each stdout file is what that run printed. Exits 0 when every check holds,
1 otherwise, printing each check and its verdict.

What it checks, for each run on its own:
  - the .txt is exactly what the run printed before its "Written:" line, once
    the "\r\n" a Windows console stream writes for "\n" is undone;
  - neither file holds a carriage return or a byte-order mark, and each ends
    in one line feed -- the marks of a write in text mode, or of a truncation;
  - every row of the .csv has six fields, its timing figures are finite
    numbers, and its frame numbers run 0, 1, 2, ... without a gap.
And between the two runs:
  - the .txt is identical once the timing figures are masked: the warm-up's
    frame count and length, and the three rows of statistics;
  - the .csv's header is identical, and so are its measured rows' frame
    numbers within the measured phase, path times and phase labels. The
    warm-up holds a time rather than a number of frames, so its row count
    differs from run to run; its rows' path times and labels must still
    agree with each other's.
"""

import math
import re
import sys
from pathlib import Path

failures = 0


def verdict(name, holds):
    global failures
    print(f"{'ok  ' if holds else 'FAIL'} {name}")
    if not holds:
        failures += 1


def only(directory, suffix):
    found = sorted(Path(directory).glob(f"grid-orbit-*{suffix}"))
    if len(found) != 1:
        sys.exit(f"expected one {suffix} in {directory}, found {len(found)}")
    return found[0].read_bytes()


def mask_summary(text):
    text = re.sub(r"warm-up of \d+ frames in \d+\.\d+ s", "warm-up of N frames in T s", text)
    return re.sub(r"(?m)^(frame interval|CPU working|GPU {11})( +\d+\.\d{4}){5}$",
                  r"\1 <figures>", text)


def check_run(label, directory, stdout_file):
    summary = only(directory, ".txt")
    table = only(directory, ".csv")
    # Standard output is a text stream, which on Windows writes each "\n" as
    # "\r\n"; the files are written in binary and must hold none, below.
    printed = Path(stdout_file).read_bytes().replace(b"\r\n", b"\n")
    verdict(f"{label}: the .txt is what the run printed before 'Written:'",
            printed.startswith(summary) and printed[len(summary):].startswith(b"\nWritten: "))
    for name, data in (("txt", summary), ("csv", table)):
        verdict(f"{label}: the .{name} has no carriage return", b"\r" not in data)
        verdict(f"{label}: the .{name} has no byte-order mark", not data.startswith(b"\xef\xbb\xbf"))
        verdict(f"{label}: the .{name} ends in one line feed",
                data.endswith(b"\n") and not data.endswith(b"\n\n"))
    rows = table.decode("ascii").splitlines()
    body = [row.split(",") for row in rows[1:]]
    verdict(f"{label}: every row of the .csv has six fields", all(len(r) == 6 for r in body))
    verdict(f"{label}: the frame numbers run 0, 1, 2, ...",
            [int(r[0]) for r in body] == list(range(len(body))))
    verdict(f"{label}: every timing figure is a finite number",
            all(math.isfinite(float(x)) for r in body for x in r[3:]))
    return summary.decode("ascii"), rows[0], body


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    before = check_run("before", sys.argv[1], sys.argv[2])
    after = check_run("after", sys.argv[3], sys.argv[4])

    verdict("the .txt files agree apart from the timing figures",
            mask_summary(before[0]) == mask_summary(after[0]))
    verdict("the .csv headers agree", before[1] == after[1])

    def measured(body):
        rows = [r for r in body if r[2] == "measured"]
        first = int(rows[0][0])
        return [(int(r[0]) - first, r[1], r[2]) for r in rows]

    def warm_up(body):
        return {(r[1], r[2]) for r in body if r[2] == "warm-up"}

    verdict("the measured rows agree: frame within the phase, path time, label",
            measured(before[2]) == measured(after[2]))
    verdict("the warm-up rows agree: one path time and label in both",
            warm_up(before[2]) == warm_up(after[2]) and len(warm_up(before[2])) == 1)
    verdict("every row is warm-up or measured, warm-up first",
            all([r[2] for r in body] == sorted([r[2] for r in body], key=lambda p: p != "warm-up")
                for body in (before[2], after[2])))
    print(f"\n{failures} check(s) failed" if failures else "\nevery check holds")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

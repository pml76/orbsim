"""Rule 23 for records-count.py: four made-up records whose answer is known.

    python -I make-records.py <folder>

Process 100 writes "by a crash" (code 0xc0000005), then "normally"; process
200 writes once, normally; process 300 writes "by abort" and its record has
no `end` line. records-count.py must report 3 processes, 1 that wrote by a
crash and then normally, 1 record without its end line, and a longest
duration_us of 80 and write_us of 50.
"""

import sys
from pathlib import Path

RECORDS = {
    "100-1-1.functions.txt": "program C:/x/a.exe\ncommand a\ntest t1\nended by a crash\nflags 4\n10\n20\nend 2\n"
                             "code 0xc0000005\nwrite_us 50\nduration_us 80\n",
    "100-1-2.functions.txt": "program C:/x/a.exe\ncommand a\ntest t1\nended normally\nflags 4\n10\n20\n30\nend 3\n"
                             "write_us 40\nduration_us 60\n",
    "200-5-1.functions.txt": "program C:/x/b.exe\ncommand b\ntest t2\nended normally\nflags 2\n10\nend 1\n"
                             "write_us 10\nduration_us 20\n",
    "300-7-1.functions.txt": "program C:/x/c.exe\ncommand c\ntest t3\nended by abort\nflags 2\n10\n",
}


def main(argv: list) -> int:
    folder = Path(argv[1])
    folder.mkdir(parents=True, exist_ok=True)
    for name, text in RECORDS.items():
        (folder / name).write_bytes(text.encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

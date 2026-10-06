"""Ninja's header record, read while another process holds it open (M1-109,
register decision 399).

    python ninja-deps-held-open.py

Makes a scratch project of 400 C files beside itself (needs clang and ninja
on PATH, about two minutes), brings Ninja's header record past its rewrite
threshold with ordinary builds, and reads it four ways: with and without -n,
freely and while this script holds the file open as a running Ninja does."""
import os, pathlib, subprocess, tempfile, time

N = 400
here = pathlib.Path(tempfile.gettempdir()) / "ninja-deps-held-open"
here.mkdir(exist_ok=True)
ninja = "ninja"
for i in range(N):
    (here / f"h{i}.h").write_text(f"#define H{i} {i}\n")
    (here / f"s{i}.c").write_text(f'#include "h{i}.h"\nint f{i}(void) {{ return H{i}; }}\n')
objs = " ".join(f"s{i}.o" for i in range(N))
(here / "build.ninja").write_text(
    "rule cc\n  command = clang -MD -MF $out.d -c $in -o $out\n  deps = gcc\n  depfile = $out.d\n"
    "rule wait\n  command = cmd /c python -c \"import time; time.sleep(40)\" && type nul > $out\n"
    + "".join(f"build s{i}.o: cc s{i}.c\n" for i in range(N))
    + f"build objs: phony {objs}\nbuild slow.stamp: wait | {objs}\ndefault objs\n")


deps = here / ".ninja_deps"


def touch_all():
    now = time.time()
    for i in range(N):
        os.utime(here / f"s{i}.c", (now, now))


def build():
    r = subprocess.run(["ninja", "-j8", "objs"], cwd=here, capture_output=True, text=True)
    assert r.returncode == 0, r.stdout[-300:]


def read(*flags):
    r = subprocess.run(["ninja", *flags, "-t", "deps"], cwd=here, capture_output=True, text=True)
    return r.returncode, r.stdout.count(": #deps"), r.stderr.strip()[:200]


def over_threshold():
    """Start from a compact record, then three rebuilds: 400 + 3 x 400 = 1,600
    records, 4x the 400 live ones. The rebuild that starts at 1,200 does not
    rewrite (not more than 3x) and ends over the threshold."""
    subprocess.run(["ninja", "-t", "recompact"], cwd=here, capture_output=True)
    for _ in range(3):
        time.sleep(1.1); touch_all(); build()


build()  # a fresh project: the first 400 records
over_threshold()
size = deps.stat().st_size
print("1. size after three rebuilds:", size, flush=True)
print("   read with -n:", read("-n"), "size", deps.stat().st_size)
print("   plain read:", read(), "size", deps.stat().st_size, "(smaller means it was rewritten)", flush=True)

over_threshold()
print("2. size before the held read:", deps.stat().st_size, flush=True)
held = open(deps, "ab")  # what a running Ninja does: fopen(path, "ab")
try:
    print("   read with -n while held:", read("-n"), "size", deps.stat().st_size)
    print("   plain read while held:", read(), "size", deps.stat().st_size, flush=True)
finally:
    held.close()
print("   leftover .recompact file:", (here / ".ninja_deps.recompact").exists())
print("   plain read after release:", read(), "size", deps.stat().st_size)

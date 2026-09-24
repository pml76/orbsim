#!/usr/bin/env python3
"""Measure what a task costs per frame, the same way on every machine.

    measure-frame-cost.py scripts/measurements/m1-14.json
    measure-frame-cost.py scripts/measurements/m1-14.json --runs 5 --out results.json

Why this exists: a task that changes the renderer should say what it costs,
and "it costs about 41 microseconds" is only worth something if another
machine can run the same measurement and put its number beside it. So the
measurement is a file, not a session: the spec names the commits to compare,
the temporary timing instrumentation to add to each, and the variants to run,
and this script does the rest and writes everything it saw to a results file.
docs/measurements/m1-14-frame-cost.md describes each test and how to read it.

**What it does, in order:**

  1. For each build the spec names ("before", "after"), checks out that exact
     commit in a git worktree under build/measure/<build>/ -- never in your
     working tree, which it does not touch -- and configures it with the
     relwithdebinfo preset the first time. Every machine therefore measures
     the same code, whatever it has checked out, and the dependencies are
     fetched at the commit's own pinned tags.
  2. Resets the worktree, applies the spec's instrumentation edits (each
     anchor must match exactly once, or the run stops and names it), and
     builds the application.
  3. Runs every variant `--runs` times, **round-robin** -- one run of each
     variant, then the next round -- so that a machine warming up or a
     background task starting affects every variant alike rather than
     whichever happened to run last.
  4. Reads what each run printed: the instrumentation's MEASURE lines (GPU
     time from Vulkan timestamp queries, CPU time from a steady clock), the
     application's own frame count, and the process's wall-clock time.
  5. Writes one JSON file with the machine (GPU, driver, operating system,
     processor, the resolution the swapchain actually got), the commits,
     every run, the median of each figure per variant, and the comparisons
     the spec asks for; and prints the same as a table.

**It is not a test**, and nothing in `check` runs it: a timing threshold on
shared hardware fails for reasons unrelated to the change (M1-22 makes the
same argument for its benchmark). It is a measurement, recorded.

**What it needs**: the environment `check` builds in -- clang, Ninja and the
Vulkan SDK on PATH, and CMake (CLion's by default; --cmake to override) --
a display, since the application opens its window, and network access the
first time, to fetch the dependencies into each worktree. The first run on a
machine therefore takes some minutes; later runs reuse the worktrees.
"""

import argparse
import datetime
import json
import os
import platform
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

# CLion's cmake, which is the one the build trees are configured with
# (docs/STATUS.md). Overridable for a machine that keeps it elsewhere.
DEFAULT_CMAKE = (
    r"C:\Users\U439644\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe"
)

# Bounds, not budgets: a configure that fetches SDL3 and a full build of one
# tree each finish in a few minutes here, and a run is `seconds` plus start-up.
CONFIGURE_TIMEOUT_SECONDS = 1800
BUILD_TIMEOUT_SECONDS = 3600
RUN_GRACE_SECONDS = 120

MEASURE_LINE = re.compile(r"MEASURE (\w+) (.*)")
KEY_VALUE = re.compile(r"(\w+)=(\S+)")
# Keys the instrumentation prints that count samples rather than measure.
COUNTERS = {"samples", "calls"}
FRAMES_LINE = re.compile(r"(\d+) frames in (\d+) ms \(([\d.]+) fps\)")


def git(root: Path, *args: str) -> str:
    out = subprocess.run(["git", *args], cwd=root, capture_output=True, text=True,
                         check=True)
    return out.stdout.strip()


def worktree_for(root: Path, build: str, commit: str) -> Path:
    """build/measure/<build>, checked out at `commit`, created if missing."""
    path = root / "build" / "measure" / build
    if not (path / ".git").exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "worktree", "add", "--detach", str(path), commit],
                       cwd=root, check=True)
    subprocess.run(["git", "checkout", "--detach", "--force", commit], cwd=path,
                   check=True, capture_output=True)
    # Undo the previous run's instrumentation, so every run starts from the
    # commit exactly.
    subprocess.run(["git", "checkout", "--", "."], cwd=path, check=True)
    return path


def apply_edits(tree: Path, build: str, edits: list) -> None:
    for edit in edits:
        target = tree / edit["file"]
        text = target.read_text(encoding="utf-8")
        found = text.count(edit["find"])
        if found != 1:
            raise SystemExit(f"{build}: anchor for '{edit['why']}' appears {found} times "
                             f"in {edit['file']}; the spec no longer matches its commit")
        target.write_text(text.replace(edit["find"], edit["replace"]), encoding="utf-8",
                          newline="\n")


def configure_and_build(tree: Path, cmake: str) -> Path:
    binary = tree / "build" / "relwithdebinfo"
    if not (binary / "CMakeCache.txt").exists():
        subprocess.run([cmake, "--preset", "relwithdebinfo"], cwd=tree, check=True,
                       timeout=CONFIGURE_TIMEOUT_SECONDS)
    result = subprocess.run([cmake, "--build", str(binary), "--target", "orbsim"],
                            cwd=tree, capture_output=True, text=True,
                            timeout=BUILD_TIMEOUT_SECONDS)
    if result.returncode != 0:
        print(result.stdout[-4000:], result.stderr[-4000:], file=sys.stderr)
        raise SystemExit(f"the build in {tree} failed")
    return binary / ("orbsim.exe" if os.name == "nt" else "orbsim")


def run_once(executable: Path, variant: dict, seconds: float) -> dict:
    env = dict(os.environ)
    env.update(variant.get("env", {}))
    args = [str(executable), *variant.get("args", []), "--seconds", str(seconds)]
    started = time.perf_counter()
    proc = subprocess.run(args, cwd=executable.parent, env=env, capture_output=True,
                          text=True, timeout=seconds + RUN_GRACE_SECONDS)
    wall = time.perf_counter() - started
    output = proc.stdout + proc.stderr

    run = {"exit": proc.returncode, "wall_ms": round(wall * 1000.0, 1), "metrics": {},
           "device": {}}
    for line in output.splitlines():
        if line.startswith("GPU: "):
            run["device"]["name"] = line[5:].strip()
        elif line.startswith("Swapchain: "):
            run["device"]["swapchain"] = line[11:].strip()
        frames = FRAMES_LINE.search(line)
        if frames:
            run["metrics"]["fps"] = float(frames.group(3))
        measured = MEASURE_LINE.search(line)
        if measured:
            # Every running mean, in order, so a record can show whether the
            # figure was still moving when the run ended (the warm-up check).
            run.setdefault("trace", []).append(line[measured.start():].strip())
            values = dict(KEY_VALUE.findall(measured.group(2)))
            if measured.group(1) == "device":
                run["device"].update(values)
            else:
                # The instrumentation prints running means; the last line
                # printed is the mean over the most samples.
                for key, value in values.items():
                    # Counters say how many samples a mean is over; they are
                    # kept in the output, not summarised as results.
                    if key in COUNTERS:
                        run.setdefault("counts", {})[key] = int(value)
                    else:
                        run["metrics"][key] = float(value)
    if proc.returncode != 0:
        run["tail"] = output[-2000:]
    return run


def driver_version(vendor: int, raw: int) -> str:
    """The driver version as its vendor writes it; Vulkan leaves the encoding
    to the vendor, and the two that differ from Vulkan's own are these."""
    if vendor == 0x10DE:  # NVIDIA: 10.8.8.6 bits
        return f"{raw >> 22}.{(raw >> 14) & 0xFF}.{(raw >> 6) & 0xFF}.{raw & 0x3F}"
    if vendor == 0x8086 and platform.system() == "Windows":  # Intel on Windows: 18.14
        return f"{raw >> 14}.{raw & 0x3FFF}"
    return f"{raw >> 22}.{(raw >> 12) & 0x3FF}.{raw & 0xFFF}"


def summarise(runs: list, variants: list) -> dict:
    summary = {}
    for variant in variants:
        mine = [r for r in runs if r["variant"] == variant["name"] and r["exit"] == 0]
        keys = sorted({k for r in mine for k in r["metrics"]} | {"wall_ms"})
        figures = {}
        for key in keys:
            values = [r["wall_ms"] if key == "wall_ms" else r["metrics"].get(key)
                      for r in mine]
            values = [v for v in values if v is not None]
            if values:
                figures[key] = {"median": statistics.median(values), "min": min(values),
                                "max": max(values), "n": len(values)}
        summary[variant["name"]] = figures
    return summary


def compare(summary: dict, comparisons: list) -> list:
    results = []
    for c in comparisons:
        left = summary.get(c["of"], {}).get(c["metric"])
        right = summary.get(c["minus"], {}).get(c["metric"])
        value = None if left is None or right is None else left["median"] - right["median"]
        results.append({**c, "difference_of_medians": value})
    return results


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("spec", help="a measurement spec, e.g. scripts/measurements/m1-14.json")
    parser.add_argument("--runs", type=int, help="runs per variant; default is the spec's")
    parser.add_argument("--cmake", default=DEFAULT_CMAKE)
    parser.add_argument("--out", help="results file; default docs/measurements/<task>/...")
    args = parser.parse_args(argv[1:])

    root = Path(git(Path.cwd(), "rev-parse", "--show-toplevel"))
    spec = json.loads((root / args.spec).read_text(encoding="utf-8")
                      if not Path(args.spec).is_absolute()
                      else Path(args.spec).read_text(encoding="utf-8"))
    runs_per_variant = args.runs or spec["runs"]

    executables = {}
    for name, build in spec["builds"].items():
        tree = worktree_for(root, name, build["commit"])
        apply_edits(tree, name, build["edits"])
        print(f"building '{name}' at {build['commit'][:12]} ...", flush=True)
        executables[name] = configure_and_build(tree, args.cmake)

    runs = []
    for round_index in range(runs_per_variant):
        for variant in spec["variants"]:
            seconds = variant.get("seconds", spec["seconds"])
            run = run_once(executables[variant["build"]], variant, seconds)
            run.update({"variant": variant["name"], "round": round_index + 1})
            runs.append(run)
            shown = ", ".join(f"{k}={v:g}" for k, v in sorted(run["metrics"].items()))
            print(f"[{round_index + 1}/{runs_per_variant}] {variant['name']}: exit {run['exit']}, "
                  f"wall {run['wall_ms']:.0f} ms, {shown}", flush=True)

    failed = [r for r in runs if r["exit"] != 0]
    device = next((r["device"] for r in runs if "vendor" in r["device"]), {})
    vendor = int(device.get("vendor", "0"), 0)
    raw_driver = int(device.get("driver", "0"), 0)
    summary = summarise(runs, spec["variants"])
    comparisons = compare(summary, spec.get("comparisons", []))

    results = {
        "task": spec["task"],
        "spec": args.spec,
        "date": datetime.date.today().isoformat(),
        "machine": {
            "gpu": device.get("name") or next((r["device"].get("name") for r in runs), None),
            "vendor_id": f"0x{vendor:04X}",
            "driver_version": driver_version(vendor, raw_driver),
            "driver_version_raw": f"0x{raw_driver:08X}",
            "vulkan_api": device.get("api"),
            "swapchain": next((r["device"].get("swapchain") for r in runs
                               if r["device"].get("swapchain")), None),
            "os": platform.platform(),
            "processor": platform.processor() or platform.machine(),
        },
        "commits": {name: build["commit"] for name, build in spec["builds"].items()},
        "settings": {"runs_per_variant": runs_per_variant, "seconds": spec["seconds"],
                     "warmup_frames": spec["warmup_frames"]},
        "summary": summary,
        "comparisons": comparisons,
        "runs": runs,
    }

    gpu_slug = re.sub(r"[^a-z0-9]+", "-", (results["machine"]["gpu"] or "unknown").lower())
    out = Path(args.out) if args.out else (
        root / "docs" / "measurements" / spec["task"].lower() /
        f"{results['date']}-{gpu_slug.strip('-')}.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8", newline="\n")

    print(f"\n==== {spec['task']} on {results['machine']['gpu']}, "
          f"driver {results['machine']['driver_version']}, "
          f"{results['machine']['swapchain']} ====")
    for variant, figures in summary.items():
        print(f"  {variant}")
        for key, f in figures.items():
            print(f"      {key:24} median {f['median']:10.3f}   "
                  f"(min {f['min']:.3f}, max {f['max']:.3f}, n={f['n']})")
    print("  comparisons (difference of medians):")
    for c in comparisons:
        value = c["difference_of_medians"]
        shown = "n/a" if value is None else f"{value:+.3f}"
        print(f"      {c['name']}: {shown} {c['unit']}")
    print(f"\nwritten to {out}")
    if failed:
        print(f"{len(failed)} run(s) exited non-zero; their output is in the results "
              f"file and they are left out of the medians", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
